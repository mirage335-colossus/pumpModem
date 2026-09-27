#!/usr/bin/env python3
"""Candidate records must expose stale/malformed transitions without modifying files."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


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


if __name__ == '__main__':
    unittest.main()
