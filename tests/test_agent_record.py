#!/usr/bin/env python3
"""Candidate records must expose stale/malformed transitions without modifying files."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / 'tools/check-agent-record.py'
spec = importlib.util.spec_from_file_location('agent_record', TOOL)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


def record(**changes):
    values = dict.fromkeys(checker.REQUIRED_FIELDS, 'none')
    values.update({
        'State': 'active', 'Updated (UTC)': '2026-09-27T10:00:00Z',
        'Last meaningful progress (UTC)': '2026-09-27T09:58:00Z',
        'Last inbox check (UTC)': '2026-09-27T09:59:00Z',
        'Next check (UTC) / action': '2026-09-27T10:05:00Z / check inbox and test result',
        'Liveness mode / cadence': 'checkpoint / five minutes',
        'Owner process': 'unavailable', 'Contact': 'messages/test-session/',
    })
    claims = changes.pop('claims', 'None.')
    values.update(changes)
    return ('# test-session\n## Current checkpoint\n' +
            ''.join(f'- {key}: {value}\n' for key, value in values.items()) +
            f'\n## Claims held\n{claims}\n\n## Blockers and handoff\n- Current request: none\n')


def scan_record(**changes):
    text = record(**changes)
    preamble = ''.join(f'- {key}: value\n' for key in checker.PREAMBLE_FIELDS)
    text = text.replace('# test-session\n', '# test-session\n' + preamble)
    return text.replace('## Blockers and handoff',
                        '## Baseline and dependencies\n- baseline\n\n'
                        '## Progress and checks\n- progress\n\n## Blockers and handoff')


class Transitions(unittest.TestCase):
    def test_claim_addition_requires_same_candidate_timestamp_refresh(self):
        before = record()
        after = record(claims='- directory: /tmp/session-output')
        with self.assertRaisesRegex(ValueError, 'Claims held changed without advancing'):
            checker.check_transition(before, after)
        after = after.replace('10:00:00Z', '10:01:00.001+00:00')
        checker.check_transition(before, after)
        # Actual inbox/progress events need not advance just because claims did.
        self.assertIn('Last inbox check (UTC): 2026-09-27T09:59:00Z', after)

    def test_table_and_bullet_claims_are_compared_completely(self):
        claims = '| Kind | Absolute path | Relative | Use |\n| --- | --- | --- | --- |\n'
        claims += '| file | /project/a | a | change |\n### Additional claims\n- file: /project/b'
        before = record(claims=claims)
        for changed in (claims.replace('/project/b', '/project/c'), claims + '\n- resource: git-index', 'None.'):
            with self.subTest(changed=changed), self.assertRaisesRegex(ValueError, 'Claims held changed'):
                checker.check_transition(before, record(claims=changed))

    def test_duplicate_missing_empty_or_misleveled_canonical_sections_fail(self):
        baseline = record()
        for heading in checker.SECTIONS:
            for candidate in (baseline + f'\n## {heading}\nNone.\n',
                              baseline.replace(f'## {heading}', '## Renamed'),
                              baseline.replace(f'## {heading}', f'### {heading}')):
                with self.subTest(heading=heading), self.assertRaisesRegex(ValueError, 'need exactly one'):
                    checker.check_transition(baseline, candidate)
        with self.assertRaisesRegex(ValueError, 'empty section'):
            checker.check_transition(baseline, record(claims=''))

    def test_missing_duplicate_and_empty_checkpoint_fields_fail(self):
        baseline = record()
        line = '- Last inbox check (UTC): 2026-09-27T09:59:00Z\n'
        for replacement in ('', line + line, '- Last inbox check (UTC): \n'):
            with self.subTest(replacement=replacement), self.assertRaises(ValueError):
                checker.check_transition(baseline, baseline.replace(line, replacement))

    def test_fenced_examples_cannot_supply_or_truncate_claims(self):
        baseline = record()
        for fence in ('```markdown', '~~~markdown'):
            with self.subTest(fence=fence), self.assertRaisesRegex(ValueError, 'fenced examples'):
                checker.check_transition(baseline, record(claims='- file: /project/a\n' + fence +
                    '\n## Claims held\nNone.\n' + fence[:3]))

    def test_next_check_can_move_earlier_when_the_plan_changes(self):
        checker.check_transition(record(), record(**{
            'Updated (UTC)': '2026-09-27T10:01:00Z',
            'Next check (UTC) / action': '2026-09-27T10:02:00Z / check new request'}))

    def test_elapsed_next_check_and_invalid_times_fail(self):
        baseline = record()
        for value in ('2026-09-27T09:59:00Z / check', '2026-09-27T10:00:00Z / check',
                      'none / wait', '2026-09-27T10:05:00Z',
                      '2026-09-27T10:05:00+01:00 / check', '2026-13-27T10:05:00Z / check'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                checker.check_transition(baseline, record(**{'Next check (UTC) / action': value}))

    def test_future_events_and_backwards_event_times_fail(self):
        for field in ('Updated (UTC)', 'Last meaningful progress (UTC)', 'Last inbox check (UTC)'):
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, 'moved backwards'):
                checker.check_transition(record(), record(**{field: '2026-09-27T09:00:00Z'}))
        for field in ('Last meaningful progress (UTC)', 'Last inbox check (UTC)'):
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, 'later than Updated'):
                checker.check_transition(record(), record(**{field: '2026-09-27T10:01:00Z'}))

    def test_terminal_claims_never_disappear_due_to_phase(self):
        for state in checker.TERMINAL:
            for claims in ('- file: /project/held', 'None.\n- file: /project/still-held',
                           '| Kind | Absolute path |', 'None; see release note.'):
                candidate = record(claims=claims, State=state, **{'Updated (UTC)': '2026-09-27T10:01:00Z'})
                with self.subTest(state=state, claims=claims), self.assertRaisesRegex(ValueError, 'retains Claims held'):
                    checker.check_transition(record(), candidate)

    def test_closure_requires_job_and_time_disposition(self):
        values = {'State': 'done', 'Updated (UTC)': '2026-09-27T10:01:00Z',
                  'Next check (UTC) / action': 'none / complete',
                  'Closed (UTC), if terminal': '2026-09-27T10:01:00Z',
                  'Delete after (UTC)': '2026-10-27T10:01:00Z'}
        checker.check_transition(record(claims='- file: /project/a'), record(**values))
        for key, bad in (('Running jobs', 'PID 123, still running'),
                         ('Closed (UTC), if terminal', '2026-09-27T10:02:00Z'),
                         ('Delete after (UTC)', '2026-09-27T09:59:00Z')):
            with self.subTest(key=key), self.assertRaises(ValueError):
                checker.check_transition(record(), record(**{**values, key: bad}))

    def test_comparing_different_sessions_fails(self):
        with self.assertRaisesRegex(ValueError, 'session IDs differ'):
            checker.check_transition(record(), record().replace('# test-session', '# another-session'))


class CommandLine(unittest.TestCase):
    def test_success_and_failure_leave_both_files_and_neighbor_records_untouched(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            before, after, neighbor = [root / name for name in ('before.md', 'after.md', 'neighbor.md')]
            before.write_text(record(), encoding='utf-8')
            neighbor.write_bytes(b'not even a valid record\xff')
            for candidate, status in ((record(**{'Updated (UTC)': '2026-09-27T10:01:00Z'}), 0),
                                      (record(claims='- file: /project/new'), 1)):
                after.write_text(candidate, encoding='utf-8')
                snapshot = {p.name: p.read_bytes() for p in root.iterdir()}
                result = subprocess.run([sys.executable, '-B', str(TOOL), '--before', str(before),
                                         '--after', str(after)], capture_output=True, text=True)
                self.assertEqual(result.returncode, status, result.stderr)
                self.assertEqual(snapshot, {p.name: p.read_bytes() for p in root.iterdir()})
                if status == 0:
                    self.assertIn('ownership, handoff and factual review still required', result.stdout)
            for bad_path in (before, root / 'missing.md'):
                result = subprocess.run([sys.executable, '-B', str(TOOL), '--before', str(before),
                                         '--after', str(bad_path)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 1)
                self.assertNotIn('Traceback', result.stderr)


class SessionScan(unittest.TestCase):
    def test_terminal_nested_claims_remain_complete_and_opaque(self):
        claims = ('| Kind | Absolute path | Relative | Use |\n| --- | --- | --- | --- |\n'
                  '| file | /project/a | a | change |\n### Additional claims\n'
                  '- directory: /project/held\n#### Resource\n- resource: git-index\n' +
                  '\n'.join(f'- file: /project/{i}' for i in range(200)))
        path = Path('/board/sessions/test-session.md')
        for state in checker.TERMINAL:
            with self.subTest(state=state):
                output = checker.scan_record(scan_record(claims=claims, State=state), path)
                self.assertEqual(output['claims'], claims)
                self.assertEqual(output['metadata']['State'], state)
                self.assertEqual(output['id'], 'test-session')

    def test_unrelated_task_and_disposition_text_never_enters_scan_output(self):
        text = scan_record(**{'Next check (UTC) / action':
                             '2026-09-27T10:05:00Z / secret-next-action'})
        text = text.replace('Task and approach: value', 'Task and approach: secret-task')
        text = text.replace('work owned by others): value', 'work owned by others): secret-diff')
        text = text.replace('- baseline', '- secret-baseline')
        text = text.replace('- progress', '- secret-progress\n### More progress\nsecret-nested')
        text = text.replace('- Current request: none', '- secret-handoff')
        output = checker.scan_record(text, Path('/board/sessions/test-session.md'))
        self.assertNotIn('secret', json.dumps(output))
        self.assertEqual(output['metadata']['Next check (UTC)'], '2026-09-27T10:05:00Z')
        self.assertIn('Running jobs', output['metadata'])
        self.assertIn('Checkout / coordination root (absolute physical paths)', output['metadata'])

    def test_unknown_or_ambiguous_structures_need_manual_review(self):
        good = scan_record()
        bad_records = [
            record(), good.replace('## Claims held', '## Claims'),
            good + '\n## Private secret summary\nsecret-value\n',
            good.replace('## Progress and checks', '## Claims held'),
            good.replace('- State: active', '- State: active\n- Unknown: secret-value'),
            good.replace('- State: active', '- State: active\n- State: active'),
            good.replace('- Task and approach: value', 'Task and approach: value'),
            good.replace('- Task and approach: value', '- Task and approach: value\n- Extra: secret-value'),
            good.replace('## Claims held', '### Claims held'),
            good.replace('# test-session', '# wrong-session'),
            good.replace('## Claims held\nNone.', '## Claims held\n'),
            good.replace('## Claims held\nNone.', '## Claims held\n```\n## Claims held\n```'),
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'test-session.md'
            for candidate in bad_records:
                with self.subTest(candidate=candidate):
                    path.write_text(candidate, encoding='utf-8')
                    output = checker.scan_sessions(Path(directory))
                    self.assertFalse(output['complete'])
                    self.assertEqual(output['records'], [])
                    self.assertEqual(len(output['errors']), 1)
                    self.assertNotIn('secret', json.dumps(output))

    def test_unreadable_and_invalid_utf8_records_make_partial_output_incomplete(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            good, broken = root / 'test-session.md', root / 'broken.md'
            good.write_text(scan_record(), encoding='utf-8')
            broken.write_bytes(b'\xff')
            output = checker.scan_sessions(root)
            self.assertFalse(output['complete'])
            self.assertEqual([r['id'] for r in output['records']], ['test-session'])
            original = checker.read_scan_record

            def read(path, *args, **kwargs):
                if path == broken:
                    raise PermissionError('cannot read record')
                return original(path, *args, **kwargs)

            with mock.patch.object(checker, 'read_scan_record', read):
                output = checker.scan_sessions(root)
            self.assertFalse(output['complete'])
            self.assertEqual([r['id'] for r in output['records']], ['test-session'])
            self.assertEqual(output['errors'], [{'path': str(broken), 'error': 'cannot read record'}])
            self.assertIn('omitted records may hold claims', output['advisory'])

    def test_direct_entries_only_and_symlinks_are_never_read(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sessions = root / 'sessions'
            sessions.mkdir()
            (sessions / 'test-session.md').write_text(scan_record(), encoding='utf-8')
            (sessions / 'unexpected.tmp').write_text('secret-temp', encoding='utf-8')
            (sessions / 'nested.md').mkdir()
            (sessions / 'nested.md' / 'hidden.md').write_text('secret-nested', encoding='utf-8')
            outside = root / 'outside.md'
            outside.write_text('secret-external', encoding='utf-8')
            link = sessions / 'alias.md'
            try:
                link.symlink_to(outside)
            except (OSError, NotImplementedError):
                self.skipTest('symlinks unavailable')
            output = checker.scan_sessions(sessions)
            self.assertFalse(output['complete'])
            self.assertEqual([r['id'] for r in output['records']], ['test-session'])
            self.assertEqual({Path(e['path']).name for e in output['errors']},
                             {'unexpected.tmp', 'nested.md', 'alias.md'})
            self.assertNotIn('secret', json.dumps(output))
            alias = root / 'alias'
            alias.symlink_to(sessions, target_is_directory=True)
            self.assertFalse(checker.scan_sessions(alias)['complete'])

    def test_observed_concurrent_change_fails_instead_of_publishing_partial_claims(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'test-session.md'
            path.write_text(scan_record(), encoding='utf-8')
            original = checker.read_scan_record

            def change_during_read(p, *args, **kwargs):
                result = original(p, *args, **kwargs)
                p.write_text(result + '\nchanged', encoding='utf-8')
                return result

            with mock.patch.object(checker, 'read_scan_record', change_during_read):
                output = checker.scan_sessions(Path(directory))
            self.assertFalse(output['complete'])
            self.assertEqual(output['records'], [])
            self.assertIn('changed while being read', output['errors'][0]['error'])

    def test_read_access_time_change_does_not_invalidate_unchanged_record(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'test-session.md'
            path.write_text(scan_record(), encoding='utf-8')
            original = Path.lstat
            calls = 0

            def access_time_changes(p):
                nonlocal calls
                info = original(p)
                if p == path:
                    calls += 1
                    # Keep nanosecond content/identity fields; only atime differs.
                    info = mock.Mock(wraps=info, st_atime=info.st_atime + calls,
                                     **{key: getattr(info, key) for key in
                                        ('st_dev', 'st_ino', 'st_mode', 'st_size',
                                         'st_mtime_ns', 'st_ctime_ns')})
                return info

            with mock.patch.object(Path, 'lstat', access_time_changes):
                output = checker.scan_sessions(Path(directory))
            self.assertTrue(output['complete'], output)
            self.assertGreaterEqual(calls, 2)

    def test_replaced_record_descriptor_is_rejected_before_read(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'test-session.md'
            path.write_text(scan_record(), encoding='utf-8')
            before = path.lstat()
            replacement = Path(directory) / 'replacement'
            replacement.write_text('secret unrelated replacement', encoding='utf-8')
            replacement.replace(path)
            with self.assertRaisesRegex(ValueError, 'replaced before read'):
                checker.read_scan_record(path, before)

    def test_scan_cli_status_and_files_are_read_only(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / 'test-session.md'
            path.write_text(scan_record(), encoding='utf-8')
            for malformed in (False, True):
                if malformed:
                    (root / 'legacy.md').write_text('# legacy\nState: done\n', encoding='utf-8')
                snapshot = {p.name: p.read_bytes() for p in root.iterdir()}
                result = subprocess.run([sys.executable, '-B', str(TOOL), '--scan', str(root)],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, int(malformed), result.stderr)
                self.assertEqual(json.loads(result.stdout)['complete'], not malformed)
                self.assertEqual(snapshot, {p.name: p.read_bytes() for p in root.iterdir()})
            for arguments in ([], ['--before', str(path)], ['--after', str(path)],
                              ['--scan', str(root), '--before', str(path)],
                              ['--scan', str(root), '--after', str(path)]):
                result = subprocess.run([sys.executable, '-B', str(TOOL)] + arguments,
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 2)
                self.assertNotIn('Traceback', result.stderr)
            result = subprocess.run([sys.executable, '-B', str(TOOL), '--scan', str(root / 'missing')],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertFalse(json.loads(result.stdout)['complete'])


if __name__ == '__main__':
    unittest.main()
