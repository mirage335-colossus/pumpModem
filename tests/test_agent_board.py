#!/usr/bin/env python3
"""Atomic publication tests using only per-test temporary board fixtures."""
import errno
import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock


sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / 'tools' / 'agent-board.py'
SPEC = importlib.util.spec_from_file_location('agent_board', TOOL)
BOARD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BOARD)


def record(session='sender', minute='00', state='active'):
    terminal = state in {'done', 'failed', 'cancelled'}
    when = f'2026-09-27T12:{minute}:00Z'
    return f'''# {session}
- Tool / host / local chat reference: test / local / none
- Parent / read-only helpers: none
- Task and approach: test complete publication
- Checkout / coordination root (absolute physical paths): /test / /test/board
- Branch / starting HEAD / current HEAD: main / abc / abc
- Starting worktree and index changes (including work owned by others): none
## Current checkpoint
- State: {state}
- Updated (UTC): {when}
- Last meaningful progress (UTC): {when}
- Last inbox check (UTC): {when}
- Next check (UTC) / action: {'none' if terminal else '2026-09-27T13:00:00Z'} / complete fixture
- Liveness mode / cadence: checkpoint / five minutes
- Last heartbeat (UTC), if supervised: none
- Run token / heartbeat file and writer, if used: none
- Owner process: unavailable
- Running jobs: none
- Closed (UTC), if terminal: {when if terminal else 'none'}
- Delete after (UTC): {'2026-10-27T12:00:00Z' if terminal else 'none'}
- Retention exception: none
- Contact: messages/{session}/
## Claims held
None.
## Baseline and dependencies
- Test fixture, no shared source.
## Progress and checks
- Complete fixture.
## Blockers and handoff
- No pending handoffs.
'''.encode()


@unittest.skipUnless(os.name == 'posix', 'optional helper requires POSIX safe filesystem operations')
class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='agent-board-test-')
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name).resolve()
        self.board = self.base / 'board'
        self.board.mkdir()
        (self.board / 'sessions').mkdir()
        self.source = self.base / 'claimed-input'
        self.source.write_bytes(b'A complete message.\n')
        self.target = self.board / 'sessions' / 'sender.md'
        self.lock = self.board / 'registry.lock'

    def owned_lock(self, session='sender'):
        self.lock.mkdir()
        (self.lock / 'owner.md').write_text(f'- Session: {session}\n'
                                           '- Host: freeform host metadata stays supported\n'
                                           '- PID/start identity: optional here\n')

    def message(self, message_id='one', **kwargs):
        return BOARD.publish_message(self.board, kwargs.get('sender', 'sender'),
                                     kwargs.get('recipient', 'recipient'), message_id,
                                     kwargs.get('body', str(self.source)))

    def publish(self, data=None, **kwargs):
        self.source.write_bytes(record() if data is None else data)
        return BOARD.publish_record(self.board, 'sender', str(self.source), **kwargs)

    def update(self, data=None, expected=None):
        expected = expected or hashlib.sha256(self.target.read_bytes()).hexdigest()
        return self.publish(data or record(minute='01'), expected_sha256=expected)

    def no_temps(self):
        self.assertEqual(list(self.board.rglob('.agent-board-*.tmp')), [])

    def run_cli(self, *args, input=None):
        return subprocess.run([sys.executable, '-B', str(TOOL), *map(str, args)],
                              input=input, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              cwd=self.base, timeout=15)

    def test_missing_recipient_created_and_exact_complete_bytes(self):
        payload = b'\0 leading bytes\r\n' + bytes(range(256))
        self.source.write_bytes(payload)
        result = self.message()
        self.assertEqual(result.read_bytes(), payload)
        self.no_temps()

    def test_existing_message_never_replaced(self):
        result = self.message()
        self.source.write_bytes(b'changed message')
        with self.assertRaisesRegex(BOARD.PublicationError, 'already exists'):
            self.message()
        self.assertEqual(result.read_bytes(), b'A complete message.\n')
        self.no_temps()

    def test_process_race_has_one_winner_and_observed_bytes_are_complete(self):
        # Independent CLI processes race the same destination with different
        # payloads. A concurrent reader must never see a direct-write prefix.
        payloads = [bytes([65 + i]) * (256 * 1024) + b'\nEND\n' for i in range(6)]
        inputs = []
        for i, payload in enumerate(payloads):
            path = self.base / f'input-{i}'
            path.write_bytes(payload)
            inputs.append(path)
        processes = [subprocess.Popen([sys.executable, '-B', str(TOOL), 'message',
                     '--board', str(self.board), '--sender', 'sender', '--recipient', 'recipient',
                     '--id', 'race', '--body', str(path)], cwd=self.base,
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE) for path in inputs]
        for process in processes:
            self.addCleanup(lambda p=process: (p.kill(), p.wait()) if p.poll() is None else None)
        target = self.board / 'messages' / 'recipient' / 'sender-race.md'
        observed = []
        deadline = time.monotonic() + 15
        while any(p.poll() is None for p in processes) and time.monotonic() < deadline:
            try:
                observed.append(target.read_bytes())
            except FileNotFoundError:
                pass
            time.sleep(0.001)
        outcomes = [p.communicate(timeout=5) for p in processes]
        winners = [i for i, p in enumerate(processes) if p.returncode == 0]
        self.assertEqual(len(winners), 1, outcomes)
        self.assertTrue(all(p.returncode in (0, 1) for p in processes), outcomes)
        observed.append(target.read_bytes())
        self.assertTrue(all(data == payloads[winners[0]] for data in observed))
        self.no_temps()

    def test_message_absent_until_staging_finished(self):
        staged = threading.Event()
        proceed = threading.Event()
        errors = []
        original = BOARD.os.fsync

        def pause(fd):
            original(fd)
            staged.set()
            if not proceed.wait(5):
                raise RuntimeError('test publication did not resume')

        def worker():
            try:
                self.message()
            except Exception as exc:
                errors.append(exc)

        with mock.patch.object(BOARD.os, 'fsync', side_effect=pause):
            thread = threading.Thread(target=worker)
            thread.start()
            try:
                self.assertTrue(staged.wait(5))
                self.assertFalse((self.board / 'messages' / 'recipient' / 'sender-one.md').exists())
            finally:
                proceed.set()
                thread.join(5)
        self.assertEqual(errors, [])
        self.assertFalse(thread.is_alive())
        self.assertEqual((self.board / 'messages' / 'recipient' / 'sender-one.md').read_bytes(),
                         self.source.read_bytes())

    def test_link_failure_has_no_fallback_and_preserves_unrelated_temp(self):
        inbox = self.board / 'messages' / 'recipient'
        inbox.mkdir(parents=True)
        foreign = inbox / '.agent-board-foreign.tmp'
        foreign.write_bytes(b'belongs to another operation')
        with mock.patch.object(BOARD.os, 'link', side_effect=OSError(errno.EOPNOTSUPP, 'no links')):
            # Mocking link changes its identity in the capabilities set.
            with mock.patch.object(BOARD, 'require_capabilities'):
                with self.assertRaisesRegex(BOARD.PublicationError, 'no direct-write fallback'):
                    self.message()
        self.assertEqual(list(inbox.iterdir()), [foreign])
        self.assertEqual(foreign.read_bytes(), b'belongs to another operation')

    def test_write_failure_cleans_only_own_temp_and_retains_prior_record(self):
        self.owned_lock()
        self.target.write_bytes(record())
        foreign = self.lock / '.agent-board-foreign.tmp'
        foreign.write_bytes(b'untouched')
        with mock.patch.object(BOARD.os, 'fsync', side_effect=OSError(errno.ENOSPC, 'full')):
            with self.assertRaises(OSError):
                self.update()
        self.assertEqual(self.target.read_bytes(), record())
        self.assertEqual(set(p.name for p in self.lock.iterdir()), {'owner.md', foreign.name})
        self.assertEqual(foreign.read_bytes(), b'untouched')

    def test_registration_and_update_use_complete_current_checker_template(self):
        self.owned_lock()
        self.publish(create=True)
        self.assertEqual(self.target.read_bytes(), record())
        self.update()
        self.assertEqual(self.target.read_bytes(), record(minute='01'))
        self.assertEqual([p.name for p in (self.board / 'sessions').iterdir()], ['sender.md'])
        self.assertEqual([p.name for p in self.lock.iterdir()], ['owner.md'])
        self.no_temps()

    def test_registration_collision_preserves_record(self):
        self.owned_lock()
        self.publish(create=True)
        with self.assertRaisesRegex(BOARD.PublicationError, 'already exists'):
            self.publish(record(minute='01'), create=True)
        self.assertEqual(self.target.read_bytes(), record())
        self.no_temps()

    def test_record_staged_outside_sessions_and_replace_is_atomic(self):
        self.owned_lock()
        self.target.write_bytes(record())
        original = BOARD.os.replace

        def inspect(source, target, **kwargs):
            self.assertEqual([p.name for p in (self.board / 'sessions').iterdir()], ['sender.md'])
            self.assertEqual(self.target.read_bytes(), record())
            temporary = self.lock / source
            self.assertEqual(temporary.read_bytes(), record(minute='01'))
            return original(source, target, **kwargs)

        with mock.patch.object(BOARD.os, 'replace', side_effect=inspect):
            self.update()
        self.assertEqual(self.target.read_bytes(), record(minute='01'))
        self.no_temps()

    def test_cross_filesystem_replace_failure_preserves_old_record(self):
        self.owned_lock()
        self.target.write_bytes(record())
        with mock.patch.object(BOARD.os, 'replace', side_effect=OSError(errno.EXDEV, 'cross device')):
            with self.assertRaises(OSError):
                self.update()
        self.assertEqual(self.target.read_bytes(), record())
        self.no_temps()

    def test_stale_hash_missing_or_foreign_lock_and_mismatched_id(self):
        with self.assertRaises(FileNotFoundError):
            self.publish(create=True)
        self.owned_lock('foreign')
        with self.assertRaisesRegex(BOARD.PublicationError, 'matching --session'):
            self.publish(create=True)
        (self.lock / 'owner.md').write_text('- Session: sender\n- Session: sender\n')
        with self.assertRaisesRegex(BOARD.PublicationError, 'exactly one'):
            self.publish(create=True)
        (self.lock / 'owner.md').write_text('- Session: sender\n')
        with self.assertRaisesRegex(ValueError, 'session ID'):
            self.publish(record('different'), create=True)
        self.assertFalse(self.target.exists())
        self.publish(create=True)
        with self.assertRaisesRegex(BOARD.PublicationError, 'stale baseline'):
            self.update(expected='0' * 64)
        self.assertEqual(self.target.read_bytes(), record())
        self.no_temps()

    def test_invalid_candidate_cannot_publish_or_pollute_sessions(self):
        self.owned_lock()
        for invalid in [b'incomplete', record().replace(b'- Running jobs: none\n', b''),
                        record().replace(b'## Baseline and dependencies', b'## Unknown'),
                        record(state='done').replace(b'Running jobs: none', b'Running jobs: still running'),
                        record().replace(b'2026-09-27T13:00:00Z', b'2026-09-27T11:00:00Z')]:
            with self.subTest(invalid=invalid[:40]), self.assertRaises(ValueError):
                self.publish(invalid, create=True)
            self.assertEqual(list((self.board / 'sessions').iterdir()), [])
            self.no_temps()

    def test_terminal_session_and_backwards_transition_rejected(self):
        self.owned_lock()
        self.target.write_bytes(record(minute='01'))
        with self.assertRaisesRegex(ValueError, 'moved backwards'):
            self.update(record())
        self.target.write_bytes(record(state='done'))
        with self.assertRaisesRegex(BOARD.PublicationError, 'terminal session'):
            self.update(record(minute='01'))
        self.assertEqual(self.target.read_bytes(), record(state='done'))
        self.no_temps()

    def test_source_candidate_inside_sessions_rejected(self):
        self.owned_lock()
        self.target.write_bytes(record())
        with self.assertRaisesRegex(BOARD.PublicationError, 'never sessions'):
            BOARD.publish_record(self.board, 'sender', str(self.target), create=True)
        self.assertEqual(self.target.read_bytes(), record())

    def test_lock_or_record_change_after_validation_rejected(self):
        self.owned_lock()
        self.target.write_bytes(record())
        original = BOARD.os.fsync
        for kind in ('lock', 'record'):
            with self.subTest(kind=kind):
                self.target.write_bytes(record())
                (self.lock / 'owner.md').write_text('- Session: sender\n')

                def change(fd):
                    original(fd)
                    if kind == 'lock':
                        (self.lock / 'owner.md').write_text('- Session: foreign\n')
                    else:
                        self.target.write_bytes(record(minute='02'))

                with mock.patch.object(BOARD.os, 'fsync', side_effect=change):
                    with self.assertRaises(BOARD.PublicationError):
                        self.update()
                expected = record() if kind == 'lock' else record(minute='02')
                self.assertEqual(self.target.read_bytes(), expected)
                self.no_temps()

    def test_unsafe_ids_and_relative_paths_rejected(self):
        for unsafe in ('..', '.', '../escape', 'slash/name', '', 'with space', 'line\nbreak'):
            for field in ('sender', 'recipient'):
                with self.subTest(unsafe=unsafe, field=field), self.assertRaises(BOARD.PublicationError):
                    self.message(**{field: unsafe})
            with self.assertRaises(BOARD.PublicationError):
                self.message(unsafe)
        with self.assertRaises(BOARD.PublicationError):
            BOARD.publish_message('relative', 'sender', 'recipient', 'one', str(self.source))
        with self.assertRaises(BOARD.PublicationError):
            self.message(body='relative')

    def test_symlinked_source_or_source_ancestor_rejected(self):
        link = self.base / 'source-link'
        link.symlink_to(self.source)
        with self.assertRaises(OSError):
            self.message(body=str(link))
        directory = self.base / 'source-parent-link'
        directory.symlink_to(self.base, target_is_directory=True)
        with self.assertRaises(OSError):
            self.message(body=str(directory / self.source.name))

    def test_nonregular_source_rejected_without_fifo_block(self):
        fifo = self.base / 'fifo'
        os.mkfifo(fifo)
        with self.assertRaisesRegex(BOARD.PublicationError, 'regular file'):
            self.message(body=str(fifo))
        with self.assertRaises((OSError, BOARD.PublicationError)):
            self.message(body=str(self.base))

    def test_board_and_child_symlinks_cannot_escape(self):
        outside = self.base / 'outside'
        outside.mkdir()
        alias = self.base / 'board-alias'
        alias.symlink_to(self.board, target_is_directory=True)
        with self.assertRaises(OSError):
            BOARD.publish_message(alias, 'sender', 'recipient', 'one', str(self.source))
        messages = self.board / 'messages'
        messages.symlink_to(outside, target_is_directory=True)
        with self.assertRaises(OSError):
            self.message()
        messages.unlink()
        messages.mkdir()
        (messages / 'recipient').symlink_to(outside, target_is_directory=True)
        with self.assertRaises(OSError):
            self.message()
        self.assertEqual(list(outside.iterdir()), [])

    def test_record_symlinks_cannot_escape(self):
        self.owned_lock()
        outside = self.base / 'outside-record'
        outside.write_bytes(record())
        self.target.symlink_to(outside)
        with self.assertRaises(OSError):
            self.update()
        self.target.unlink()
        (self.lock / 'owner.md').unlink()
        (self.lock / 'owner.md').symlink_to(outside)
        with self.assertRaises(OSError):
            self.publish(create=True)
        self.assertEqual(outside.read_bytes(), record())
        self.no_temps()

    def test_session_or_lock_directory_symlink_and_nonregular_children_rejected(self):
        outside = self.base / 'outside'
        outside.mkdir()
        (outside / 'owner.md').write_text('- Session: sender\n')
        self.lock.symlink_to(outside, target_is_directory=True)
        with self.assertRaises(OSError):
            self.publish(create=True)
        self.lock.unlink()
        self.owned_lock()
        sessions = self.board / 'sessions'
        sessions.rmdir()
        sessions.symlink_to(outside, target_is_directory=True)
        with self.assertRaises(OSError):
            self.publish(create=True)
        sessions.unlink()
        sessions.mkdir()
        self.target.mkdir()
        with self.assertRaises((OSError, BOARD.PublicationError)):
            self.publish(expected_sha256='0' * 64)
        self.target.rmdir()
        messages = self.board / 'messages'
        messages.write_bytes(b'not a directory')
        with self.assertRaises(OSError):
            self.message()
        self.assertEqual([p.name for p in outside.iterdir()], ['owner.md'])
        self.no_temps()

    def test_message_existing_symlink_never_follows_or_replaces_target(self):
        inbox = self.board / 'messages' / 'recipient'
        inbox.mkdir(parents=True)
        external = self.base / 'untouched'
        external.write_bytes(b'untouched')
        target = inbox / 'sender-one.md'
        target.symlink_to(external)
        with self.assertRaisesRegex(BOARD.PublicationError, 'already exists'):
            self.message()
        self.assertTrue(target.is_symlink())
        self.assertEqual(external.read_bytes(), b'untouched')
        self.no_temps()

    def test_record_create_link_failure_preserves_owner_and_registry(self):
        self.owned_lock()
        with mock.patch.object(BOARD.os, 'link', side_effect=OSError(errno.EXDEV, 'cross device')):
            with mock.patch.object(BOARD, 'require_capabilities'):
                with self.assertRaisesRegex(BOARD.PublicationError, 'no direct-write fallback'):
                    self.publish(create=True)
        self.assertFalse(self.target.exists())
        self.assertTrue((self.lock / 'owner.md').is_file())
        self.assertEqual(list((self.board / 'sessions').iterdir()), [])
        self.no_temps()

    def test_unsupported_safe_primitives_fail_before_writing(self):
        with mock.patch.object(BOARD.os, 'supports_dir_fd', set()):
            with self.assertRaisesRegex(BOARD.PublicationError, 'no direct-write fallback'):
                self.message()
        self.assertEqual([p.name for p in self.board.iterdir()], ['sessions'])

    def test_explicit_record_mode_and_hash_required(self):
        self.owned_lock()
        for mode in ({}, {'create': True, 'expected_sha256': '0' * 64},
                     {'expected_sha256': 'not-a-sha256'}):
            with self.subTest(mode=mode), self.assertRaises(BOARD.PublicationError):
                self.publish(**mode)
        self.assertFalse(self.target.exists())
        self.no_temps()

    def test_cli_stdin_registration_and_message(self):
        self.owned_lock()
        created = self.run_cli('record', '--board', self.board, '--session', 'sender',
                               '--candidate', '-', '--create', input=record())
        self.assertEqual(created.returncode, 0, created.stderr)
        self.assertEqual(self.target.read_bytes(), record())
        sent = self.run_cli('message', '--board', self.board, '--sender', 'sender',
                           '--recipient', 'new-inbox', '--id', 'stdin', '--body', '-', input=b'exact\n')
        self.assertEqual(sent.returncode, 0, sent.stderr)
        self.assertEqual((self.board / 'messages' / 'new-inbox' / 'sender-stdin.md').read_bytes(), b'exact\n')
        rejected = self.run_cli('record', '--board', self.board, '--session', 'sender',
                               '--candidate', '-', '--create', input=b'invalid')
        self.assertEqual(rejected.returncode, 1)
        self.assertIn(b'No claims are granted', rejected.stderr)
        self.assertEqual(self.target.read_bytes(), record())
        self.no_temps()


if __name__ == '__main__':
    unittest.main()
