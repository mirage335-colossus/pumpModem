#!/usr/bin/env python3
"""Checked coordination transactions against isolated, bounded board fixtures."""
import hashlib
from contextlib import contextmanager
import importlib.util
import json
import os
from pathlib import Path
import selectors
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('agent_session', ROOT / 'tools/agent-session.py')
SESSION = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SESSION)


def record(name='worker', claims='None.', jobs='none', handoff='- None.', state='active'):
    terminal = state == 'done'
    return f'''# {name}
- Tool / host / local chat reference: test / local / fixture
- Parent / read-only helpers: none
- Task and approach: bounded transaction fixture
- Checkout / coordination root (absolute physical paths): /fixture / /fixture/board
- Branch / starting HEAD / current HEAD: main / abc / abc
- Starting worktree and index changes (including work owned by others): none
## Current checkpoint
- State: {state}
- Updated (UTC): 2026-09-27T12:00:00Z
- Last meaningful progress (UTC): 2026-09-27T12:00:00Z
- Last inbox check (UTC): 2026-09-27T12:00:00Z
- Next check (UTC) / action: {'none' if terminal else '2100-01-01T00:00:00Z'} / fixture action
- Liveness mode / cadence: checkpoint / five minutes
- Last heartbeat (UTC), if supervised: none
- Run token / heartbeat file and writer, if used: none
- Owner process: unavailable
- Running jobs: {jobs}
- Closed (UTC), if terminal: {'2026-09-27T12:00:00Z' if terminal else 'none'}
- Delete after (UTC): {'2026-10-27T12:00:00Z' if terminal else 'none'}
- Retention exception: none
- Contact: messages/{name}/
## Claims held
{claims}
## Baseline and dependencies
- Fixture only; no real shared source.
## Progress and checks
- Test pending.
## Blockers and handoff
{handoff}
'''


@unittest.skipUnless(os.name == 'posix', 'safe optional helper requires POSIX primitives')
class Transactions(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='agent-session-test-')
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name).resolve()
        self.board = self.base / 'board'
        (self.board / 'sessions').mkdir(parents=True)
        self.target = self.base / 'shared'
        self.scope = {'kind': 'file', 'value': str(self.target)}

    def save(self, name='worker', **kwargs):
        text = record(name, **kwargs)
        (self.board / 'sessions' / (name + '.md')).write_text(text)
        return text

    def review(self, **kwargs):
        return SESSION.review(self.board, scopes=[self.scope], **kwargs)

    def create(self, *, after=None, precondition=None, reviewed=None):
        return SESSION.commit(self.board, 'worker', record(claims=f'- file: {self.target}'),
                              reviewed or self.review(), create=True, after=after,
                              precondition=precondition)

    def test_complete_mutex_identity_and_bounded_contention(self):
        with SESSION.registry_mutex(self.board, 'worker'):
            text = (self.board / 'registry.lock/owner.md').read_text()
            for field in ('Session', 'Acquisition token', 'Host', 'UTC', 'Role', 'PID/start identity', 'Intent'):
                self.assertIn('- ' + field + ': ', text)
            with self.assertRaisesRegex(ValueError, 'busy'):
                with SESSION.registry_mutex(self.board, 'other', wait=.01):
                    self.fail('foreign lock acquired')
        self.assertFalse((self.board / 'registry.lock').exists())
        with self.assertRaises(ValueError):
            with SESSION.registry_mutex(self.board, 'worker', wait=31):
                self.fail('unbounded wait accepted')

    def test_same_session_cannot_reenter_or_reuse_a_previous_acquisition(self):
        source = self.base / 'candidate'
        source.write_text(record())
        with SESSION.registry_mutex(self.board, 'worker') as first:
            owner = (self.board / 'registry.lock/owner.md').read_bytes()
            with self.assertRaisesRegex(ValueError, 'busy'):
                with SESSION.registry_mutex(self.board, 'worker'):
                    self.fail('same-session reentry must not bypass exclusive acquisition')
            self.assertEqual((self.board / 'registry.lock/owner.md').read_bytes(), owner)
        with SESSION.registry_mutex(self.board, 'worker') as second:
            self.assertNotEqual(first.token, second.token)
            with self.assertRaisesRegex(ValueError, 'token'):
                SESSION.BOARD.publish_record(self.board, 'worker', str(source),
                                           create=True, lock_token=first.token)
            self.assertFalse((self.board / 'sessions/worker.md').exists())
            SESSION.BOARD.publish_record(self.board, 'worker', str(source),
                                       create=True, lock_token=second.token)
        self.assertFalse((self.board / 'registry.lock').exists())

    def process(self, code, *arguments):
        prefix = ('import sys\nfrom pathlib import Path\n'
                  f'sys.path.insert(0, {str(ROOT / "tests")!r})\n'
                  'from test_agent_session import SESSION, record\n')
        child = subprocess.Popen([sys.executable, '-B', '-c', prefix + code, *map(str, arguments)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            cwd=self.base)
        def cleanup():
            if child.poll() is None:
                child.kill()
            child.communicate(timeout=5)
        self.addCleanup(cleanup)
        return child

    def ready(self, children):
        # A bounded pipe barrier, not a timing sleep masquerading as concurrency.
        with selectors.DefaultSelector() as selector:
            for child in children:
                selector.register(child.stdout, selectors.EVENT_READ)
            deadline = time.monotonic() + 15
            while selector.get_map():
                events = selector.select(max(0, deadline - time.monotonic()))
                self.assertTrue(events, 'child did not reach fixture barrier')
                for key, _ in events:
                    self.assertEqual(key.fileobj.readline(), 'ready\n')
                    selector.unregister(key.fileobj)

    def test_independent_processes_have_one_claim_and_one_dependent_write(self):
        code = '''
board, target, output = map(Path, sys.argv[1:4])
name = sys.argv[4]
scope = {'kind': 'file', 'value': str(target)}
reviewed = SESSION.review(board, scopes=[scope])
print('ready', flush=True)
assert sys.stdin.readline() == 'go\\n'
try:
    SESSION.commit(board, name, record(name, claims=f'- file: {target}'), reviewed,
        create=True, wait=10, after=lambda receipt: output.write_text(name))
except SESSION.CoordinationError:
    raise SystemExit(2)
'''
        output = self.target
        children = [self.process(code, self.board, self.target, output, f'process-{i}')
                    for i in range(8)]
        self.ready(children)
        for child in children:
            child.stdin.write('go\n')
            child.stdin.flush()
        outcomes = [child.communicate(timeout=15) for child in children]
        codes = [child.returncode for child in children]
        self.assertEqual(codes.count(0), 1, (codes, outcomes))
        self.assertEqual(codes.count(2), 7, (codes, outcomes))
        winner = 'process-' + str(codes.index(0))
        self.assertEqual(output.read_text(), winner)
        self.assertEqual([p.stem for p in (self.board / 'sessions').iterdir()], [winner])
        self.assertFalse((self.board / 'registry.lock').exists())

    def test_killed_holder_leaves_lock_and_blocks_reentry_without_reclamation(self):
        child = self.process('''
with SESSION.registry_mutex(Path(sys.argv[1]), 'worker'):
    print('ready', flush=True)
    sys.stdin.readline()
''', self.board)
        self.ready([child])
        lock = self.board / 'registry.lock'
        owner = (lock / 'owner.md').read_bytes()
        child.kill()
        child.communicate(timeout=5)
        os.utime(lock, (1, 1))  # Neither age nor observed death permits stealing.
        for name in ('worker', 'other'):
            with self.assertRaisesRegex(ValueError, 'busy'):
                with SESSION.registry_mutex(self.board, name, wait=.01):
                    self.fail('abandoned lock was silently stolen')
        self.assertEqual((lock / 'owner.md').read_bytes(), owner)

    def test_cleanup_rejects_identically_replaced_owner_and_replaced_directory(self):
        for kind in ('owner', 'directory'):
            with self.subTest(kind=kind):
                real = SESSION.BOARD.publish_record
                def replace(*args, **kwargs):
                    result = real(*args, **kwargs)
                    lock = self.board / 'registry.lock'
                    if kind == 'owner':
                        sibling = lock / 'replacement'
                        sibling.write_bytes((lock / 'owner.md').read_bytes())
                        sibling.replace(lock / 'owner.md')
                    else:
                        lock.rename(self.board / 'original-lock')
                        lock.mkdir()
                        (lock / 'owner.md').write_text('foreign replacement')
                    return result
                actions = mock.Mock()
                with mock.patch.object(SESSION.BOARD, 'publish_record', side_effect=replace):
                    with self.assertRaisesRegex(ValueError, 'changed'):
                        self.create(after=actions)
                actions.assert_not_called()
                self.assertTrue((self.board / 'sessions/worker.md').exists())
                self.assertTrue((self.board / 'registry.lock/owner.md').exists())
                if kind == 'directory':
                    self.assertIn('- Session: worker',
                                  (self.board / 'original-lock/owner.md').read_text())
                    self.assertEqual((self.board / 'registry.lock/owner.md').read_text(),
                                     'foreign replacement')
                # Reset only the owned fixture between injected failures.
                shutil.rmtree(self.board)
                (self.board / 'sessions').mkdir(parents=True)

    def test_cleanup_unlink_or_rmdir_failure_suppresses_callback_and_preserves_lock(self):
        for operation in ('unlink', 'rmdir'):
            with self.subTest(operation=operation):
                original = getattr(SESSION.os, operation)
                def fail(name, *args, **kwargs):
                    if name == ('owner.md' if operation == 'unlink' else 'registry.lock'):
                        raise OSError('injected unlock failure')
                    return original(name, *args, **kwargs)
                actions = mock.Mock()
                # Patching an os function changes capability-set membership.
                with mock.patch.object(SESSION.BOARD, 'require_capabilities'), \
                        mock.patch.object(SESSION.os, operation, side_effect=fail):
                    with self.assertRaisesRegex(OSError, 'unlock failure'):
                        self.create(after=actions)
                actions.assert_not_called()
                self.assertTrue((self.board / 'sessions/worker.md').exists())
                self.assertTrue((self.board / 'registry.lock').is_dir())
                with self.assertRaisesRegex(ValueError, 'busy'):
                    with SESSION.registry_mutex(self.board, 'other'):
                        self.fail('partly cleaned mutex treated as available')
                shutil.rmtree(self.board)
                (self.board / 'sessions').mkdir(parents=True)

    def test_precondition_failure_stops_ack_output_and_job(self):
        for precondition in (lambda: False, lambda: 1,
                             mock.Mock(side_effect=AssertionError('unverified stopped writers'))):
            actions = mock.Mock()
            with self.assertRaises((ValueError, AssertionError)):
                self.create(precondition=precondition, after=actions)
            actions.assert_not_called()
            self.assertFalse((self.board / 'sessions/worker.md').exists())
            self.assertFalse((self.board / 'registry.lock').exists())

    def test_cleanup_uses_fresh_listing_when_old_directory_handle_is_stale(self):
        real = os.listdir
        held = []
        def listing(path):
            # Workspace mounts may expose the listing captured when the lock
            # descriptor was opened, before owner.md/staged files were created.
            return [] if held and path == held[0] else real(path)
        with mock.patch.object(SESSION.os, 'listdir', side_effect=listing):
            with SESSION.registry_mutex(self.board, 'worker') as lock:
                held.append(lock.fd)
                self.assertEqual(SESSION.os.listdir(lock.fd), [])
                self.assertTrue((self.board / 'registry.lock/owner.md').exists())
        self.assertFalse((self.board / 'registry.lock').exists())

    def test_success_receipt_and_callback_only_after_cleanup(self):
        events = []
        def action(receipt):
            self.assertFalse((self.board / 'registry.lock').exists())
            self.assertEqual(receipt['record_sha256'], hashlib.sha256(
                (self.board / 'sessions/worker.md').read_bytes()).hexdigest())
            self.target.write_text('one verified edit')
            events.append(receipt)
        result = self.create(precondition=lambda: True, after=action)
        self.assertEqual(events, [result])
        self.assertEqual(self.target.read_text(), 'one verified edit')
        self.assertEqual(result['claims'], [self.scope])

    def test_publisher_failure_suppresses_callback(self):
        actions = mock.Mock()
        with mock.patch.object(SESSION.BOARD, 'publish_record', side_effect=OSError('failed publish')):
            with self.assertRaisesRegex(OSError, 'failed publish'):
                self.create(after=actions)
        actions.assert_not_called()
        self.assertFalse((self.board / 'sessions/worker.md').exists())
        self.assertFalse((self.board / 'registry.lock').exists())

    def test_uncertain_cleanup_keeps_committed_record_but_stops_callback(self):
        real = SESSION.BOARD.publish_record
        def publish(*args, **kwargs):
            result = real(*args, **kwargs)
            (self.board / 'registry.lock/foreign').write_text('preserve')
            return result
        actions = mock.Mock()
        with mock.patch.object(SESSION.BOARD, 'publish_record', side_effect=publish):
            with self.assertRaisesRegex(ValueError, 'unexpected mutex contents'):
                self.create(after=actions)
        actions.assert_not_called()
        self.assertTrue((self.board / 'sessions/worker.md').exists())
        self.assertEqual((self.board / 'registry.lock/foreign').read_text(), 'preserve')
        self.assertIn('- Session: worker', (self.board / 'registry.lock/owner.md').read_text())

    def test_lock_metadata_failure_does_not_claim_success_or_delete_unknown_bytes(self):
        with mock.patch.object(SESSION.os, 'fsync', side_effect=OSError('sync failed')):
            with self.assertRaises(OSError):
                self.create()
        self.assertFalse((self.board / 'sessions/worker.md').exists())
        # Complete owned metadata may be cleaned; no claim has been published.
        self.assertFalse((self.board / 'registry.lock').exists())

    def test_stale_own_record_and_saved_byte_mismatch_fail_closed(self):
        before = self.save()
        reviewed = self.review()
        self.save(jobs='new job')
        with self.assertRaisesRegex(ValueError, 'stale baseline'):
            SESSION.commit(self.board, 'worker', before, reviewed,
                           expected_sha256=reviewed['record_hashes']['worker'])
        real = SESSION.BOARD.publish_record
        def mutate(*args, **kwargs):
            result = real(*args, **kwargs)
            (self.board / 'sessions/worker.md').write_text(record(jobs='unexpected replacement'))
            return result
        reviewed = self.review()
        actions = mock.Mock()
        with mock.patch.object(SESSION.BOARD, 'publish_record', side_effect=mutate):
            with self.assertRaisesRegex(ValueError, 'saved record differs'):
                SESSION.commit(self.board, 'worker', before, reviewed,
                               expected_sha256=reviewed['record_hashes']['worker'], after=actions)
        actions.assert_not_called()

    def test_terminal_record_cannot_replay_even_after_uncertain_output(self):
        self.save(state='done')
        reviewed = self.review()
        with self.assertRaisesRegex(ValueError, 'terminal session'):
            SESSION.commit(self.board, 'worker', record(state='done'), reviewed,
                           expected_sha256=reviewed['record_hashes']['worker'])

    def test_terminal_callback_rejected_before_releasing_scope(self):
        before = self.save(claims=f'- file: {self.target}')
        reviewed = self.review()
        actions = mock.Mock()
        with self.assertRaisesRegex(ValueError, 'terminal commit cannot run'):
            SESSION.commit(self.board, 'worker', record(state='done'), reviewed,
                           expected_sha256=reviewed['record_hashes']['worker'], after=actions)
        actions.assert_not_called()
        self.assertEqual((self.board / 'sessions/worker.md').read_text(), before)
        self.assertFalse((self.board / 'registry.lock').exists())

    def test_directory_terminal_and_resource_conflicts_not_prefix_siblings(self):
        self.save('other', claims=f'- directory: {self.base}', state='done')
        with self.assertRaisesRegex(ValueError, 'overlaps owner other'):
            self.create()
        self.save('other', claims=f'- directory: {self.base}-other')
        self.create()
        self.save('worker', claims='- resource: device:host:audio')
        self.save('other', claims='- resource: device:host:audio')
        reviewed = SESSION.review(self.board)
        with self.assertRaisesRegex(ValueError, 'overlaps owner other'):
            SESSION.commit(self.board, 'worker', record(claims='- resource: device:host:audio'),
                           reviewed, expected_sha256=reviewed['record_hashes']['worker'])

    def test_unknown_record_requires_exact_explicit_legacy_interpretation(self):
        path = self.board / 'sessions/old.md'
        path.write_text('# old\n## Claims held\nNone, released after review.\n')
        with self.assertRaisesRegex(ValueError, 'incomplete registry'):
            self.review()
        assessment = {'old': {'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                              'claims': [], 'reason': 'Entire legacy record manually reviewed; no claims.'}}
        reviewed = self.review(legacy_reviews=assessment)
        self.assertEqual(reviewed['ownership'][0], {'id': 'old', 'claims': []})
        path.write_text(path.read_text() + 'changed\n')
        with self.assertRaisesRegex(ValueError, 'legacy record differs'):
            self.create(reviewed=reviewed)
        path.unlink()
        with self.assertRaisesRegex(ValueError, 'absent'):
            self.review(legacy_reviews=assessment)

    def test_crlf_record_and_legacy_review_hash_exact_publication_bytes(self):
        before = self.save()
        path = self.board / 'sessions/worker.md'
        path.write_bytes(before.replace('\n', '\r\n').encode())
        reviewed = self.review()
        self.assertEqual(reviewed['record_hashes']['worker'], hashlib.sha256(path.read_bytes()).hexdigest())
        SESSION.commit(self.board, 'worker', before, reviewed,
                       expected_sha256=reviewed['record_hashes']['worker'])
        legacy = self.board / 'sessions/old.md'
        legacy.write_bytes(b'# old\r\n## Claims held\r\nNone, manually released.\r\n')
        assessment = {'old': {'sha256': hashlib.sha256(legacy.read_bytes()).hexdigest(),
            'claims': [], 'reason': 'Manually reviewed the complete legacy record with CRLF bytes.'}}
        reviewed = SESSION.review(self.board, legacy_reviews=assessment)
        self.assertEqual(reviewed['record_hashes']['old'], assessment['old']['sha256'])
        SESSION.commit(self.board, 'worker', before, reviewed,
                       expected_sha256=reviewed['record_hashes']['worker'])

    def test_new_claim_requires_exact_scope_review(self):
        with self.assertRaisesRegex(ValueError, 'requires exact scope review'):
            self.create(reviewed=SESSION.review(self.board))

    def test_handoff_change_rejects_unchanged_byte_intervening_owner(self):
        self.save('quiet', handoff='- Prior release.')
        reviewed = self.review()
        self.save('quiet', handoff=f'- Acquired and released {self.target}; writers stopped; new reference.')
        with self.assertRaisesRegex(ValueError, 'handoff or input changed'):
            self.create(reviewed=reviewed)

    def test_scoped_discovery_requires_review_of_quiet_owner_not_only_relay(self):
        for name in ('relay', 'quiet'):
            self.save(name, handoff=f'- Scope: file: {self.target}\n- Release reference: {name}-1; writers stopped.')
        reviewed = self.review(handoffs=['relay'])
        self.assertEqual(set(reviewed['relevant_handoffs']), {'relay', 'quiet'})
        with self.assertRaisesRegex(ValueError, 'every returned candidate'):
            SESSION.commit(self.board, 'worker', record(claims=f'- file: {self.target}'),
                reviewed, create=True, handoffs_reviewed=['relay'])
        SESSION.commit(self.board, 'worker', record(claims=f'- file: {self.target}'),
            reviewed, create=True, handoffs_reviewed=['relay', 'quiet'])

    def test_unrelated_progress_change_does_not_invalidate_ownership_review(self):
        self.save('other')
        reviewed = self.review()
        text = (self.board / 'sessions/other.md').read_text().replace('Test pending.', 'Test passed.')
        text = text.replace('Updated (UTC): 2026-09-27T12:00:00Z',
                            'Updated (UTC): 2026-09-27T12:01:00Z')
        (self.board / 'sessions/other.md').write_text(text)
        self.create(reviewed=reviewed)

    def test_changed_missing_and_aliased_input_baselines(self):
        source = self.base / 'source'
        source.write_text('original')
        missing = self.base / 'future' / 'generated'
        reviewed = self.review(inputs=[source, missing])
        source.write_text('changed')
        with self.assertRaisesRegex(ValueError, 'input changed'):
            self.create(reviewed=reviewed)
        reviewed = self.review(inputs=[source, missing])
        missing.parent.mkdir()
        missing.write_text('created')
        with self.assertRaisesRegex(ValueError, 'input changed'):
            self.create(reviewed=reviewed)
        alias = self.base / 'alias'
        alias.symlink_to(source)
        reviewed = self.review(inputs=[alias])
        alias.unlink()
        alias.symlink_to(missing)
        with self.assertRaisesRegex(ValueError, 'input changed'):
            self.create(reviewed=reviewed)

    def test_board_replacement_invalidates_same_empty_ownership_snapshot(self):
        reviewed = self.review()
        self.board.rename(self.base / 'old-board')
        (self.board / 'sessions').mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, 'changed'):
            self.create(reviewed=reviewed)
        self.assertFalse((self.board / 'sessions/worker.md').exists())

    def test_alias_retargeted_during_input_read_is_rejected(self):
        source, other, alias = (self.base / name for name in ('source', 'other', 'alias'))
        source.write_text('same bytes')
        other.write_text('same bytes')
        alias.symlink_to(source)
        real = SESSION.BOARD.read_regular
        def retarget(parent, name):
            result = real(parent, name)
            alias.unlink()
            alias.symlink_to(other)
            return result
        with mock.patch.object(SESSION.BOARD, 'read_regular', side_effect=retarget):
            with self.assertRaisesRegex(ValueError, 'alias changed'):
                SESSION.input_state(alias)

    def test_checkpoint_preserves_events_and_replaces_current_status_together(self):
        before = record(jobs='job-17 running', handoff='- Awaiting obsolete owner.')
        candidate = SESSION.checkpoint(before, state='waiting', running_jobs='none',
            progress='- Job-17 completed; 8/8 tests passed; 3 internal skips.',
            handoff='- Current owner identified; acquisition pending.',
            next_check='2100-01-01T00:00:00Z / request actual owner',
            updated_at='2026-09-27T12:03:00Z', progress_at='2026-09-27T12:02:00Z')
        fields = SESSION.CHECK.parse_record(candidate)[1]
        self.assertEqual(fields['Last inbox check (UTC)'], '2026-09-27T12:00:00Z')
        self.assertEqual(fields['Last meaningful progress (UTC)'], '2026-09-27T12:02:00Z')
        self.assertEqual(fields['Running jobs'], 'none')
        self.assertNotIn('obsolete owner', candidate)
        self.assertNotIn('Test pending', candidate)
        with self.assertRaises(TypeError):
            SESSION.checkpoint(before, state='active', running_jobs='none')
        with self.assertRaises(ValueError):
            SESSION.checkpoint(before, state='done', running_jobs='none', progress='- Done.',
                handoff='- Done.', next_check='none / done')

    def test_failed_acquisition_does_not_prevent_independent_completion_checkpoint(self):
        before = self.save(jobs='completed but not reconciled')
        reviewed = self.review()
        self.save('other', claims=f'- file: {self.target}')
        with self.assertRaises(ValueError):
            SESSION.commit(self.board, 'worker', record(claims=f'- file: {self.target}'),
                           reviewed, expected_sha256=reviewed['record_hashes']['worker'])
        current = SESSION.review(self.board)
        candidate = SESSION.checkpoint(before, state='waiting', running_jobs='none',
            progress='- Completed test: 8/8 passed.', handoff='- Waiting for other; no ownership.',
            next_check='2100-01-01T00:00:00Z / read inbox')
        SESSION.commit(self.board, 'worker', candidate, current,
                       expected_sha256=current['record_hashes']['worker'])
        self.assertEqual(SESSION.CHECK.parse_record(
            (self.board / 'sessions/worker.md').read_text())[1]['Running jobs'], 'none')

    def test_32_simultaneous_contenders_have_one_acquisition_and_one_callback(self):
        count = 32
        barrier = threading.Barrier(count)
        results, failures, unexpected = [], [], []
        def worker(index):
            name = f'worker-{index}'
            try:
                reviewed = self.review()
                barrier.wait(timeout=15)
                SESSION.commit(self.board, name, record(name, claims=f'- file: {self.target}'),
                    reviewed, create=True, wait=30,
                    after=lambda receipt: results.append(receipt))
            except SESSION.CoordinationError as exc:
                failures.append(str(exc))
            except Exception as exc:
                unexpected.append(repr(exc))
        workers = [threading.Thread(target=worker, args=(i,), daemon=True) for i in range(count)]
        for thread in workers:
            thread.start()
        for thread in workers:
            thread.join(35)
        self.assertTrue(all(not thread.is_alive() for thread in workers))
        self.assertEqual(unexpected, [])
        self.assertEqual(len(results), 1)
        self.assertEqual(len(failures), count - 1)
        self.assertEqual(len(list((self.board / 'sessions').iterdir())), 1)
        self.assertFalse((self.board / 'registry.lock').exists())

    def test_eight_disjoint_workers_make_useful_progress_then_close(self):
        count = 8
        barrier = threading.Barrier(count)
        local = threading.local()
        real_mutex = SESSION.registry_mutex
        completed, failures, attempts = [], [], []
        @contextmanager
        def tracked_mutex(*args, **kwargs):
            with real_mutex(*args, **kwargs) as lock:
                local.held = True
                yield lock
            local.held = False
        def worker(index):
            name = f'disjoint-{index}'
            target = self.base / name
            scope = {'kind': 'file', 'value': str(target)}
            deadline = time.monotonic() + 15
            def checked_retry(closing):
                tries = 0
                while time.monotonic() < deadline:
                    tries += 1
                    try:
                        reviewed = SESSION.review(self.board, scopes=[] if closing else [scope])
                        candidate = record(name, state='done') if closing else record(name, claims=f'- file: {target}')
                        def useful_work(receipt):
                            self.assertFalse(getattr(local, 'held', False))
                            target.write_text(name + ': useful result\n')
                            completed.append(name)
                        SESSION.commit(self.board, name, candidate, reviewed, create=not closing,
                            expected_sha256=reviewed['record_hashes'].get(name) if closing else None,
                            wait=2, after=None if closing else useful_work)
                        attempts.append(tries)
                        return
                    except SESSION.CoordinationError as exc:
                        if not any(word in str(exc) for word in
                                   ('changed', 'incomplete registry', 'busy')):
                            raise
                        time.sleep(.001)
                raise AssertionError('bounded fixture did not finish; not a fairness guarantee')
            try:
                barrier.wait(timeout=10)
                checked_retry(False)
                checked_retry(True)
            except Exception as exc:
                failures.append(repr(exc))
        with mock.patch.object(SESSION, 'registry_mutex', side_effect=tracked_mutex):
            workers = [threading.Thread(target=worker, args=(i,), daemon=True) for i in range(count)]
            for thread in workers:
                thread.start()
            for thread in workers:
                thread.join(20)
        self.assertTrue(all(not thread.is_alive() for thread in workers))
        self.assertEqual(failures, [])
        self.assertEqual(sorted(completed), [f'disjoint-{i}' for i in range(count)])
        self.assertEqual(len(attempts), count * 2)
        for index in range(count):
            name = f'disjoint-{index}'
            self.assertEqual((self.base / name).read_text(), name + ': useful result\n')
            _, fields, claims = SESSION.CHECK.parse_record((self.board / 'sessions' / (name + '.md')).read_text())
            self.assertEqual(fields['State'], 'done')
            self.assertEqual(claims, 'None.')

    def test_cli_json_review_checkpoint_and_failed_commit(self):
        tool = ROOT / 'tools/agent-session.py'
        def run(operation, request):
            return subprocess.run([sys.executable, '-B', str(tool), operation],
                input=json.dumps(request), text=True, capture_output=True, timeout=15)
        result = run('review', {'board': str(self.board), 'scopes': [self.scope]})
        self.assertEqual(result.returncode, 0, result.stderr)
        reviewed = json.loads(result.stdout)
        result = run('checkpoint', {'before': record(claims=f'- file: {self.target}'),
            'state': 'active', 'running_jobs': 'none', 'progress': '- CLI candidate prepared.',
            'handoff': '- None.', 'next_check': '2100-01-01T00:00:00Z / implementation next'})
        self.assertEqual(result.returncode, 0, result.stderr)
        candidate = json.loads(result.stdout)['candidate']
        fields = SESSION.CHECK.parse_record(candidate)[1]
        self.assertEqual(fields['Last inbox check (UTC)'], '2026-09-27T12:00:00Z')
        self.assertIn('CLI candidate prepared', candidate)
        result = run('commit', {'board': str(self.board), 'session': 'worker',
            'candidate': candidate, 'reviewed': reviewed, 'create': True})
        self.assertEqual(result.returncode, 0, result.stderr)
        result = run('commit', {'board': str(self.board), 'session': 'worker',
            'candidate': record(claims=f'- file: {self.target}'), 'reviewed': reviewed, 'create': True})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Stop dependent actions', result.stderr)


if __name__ == '__main__':
    unittest.main()
