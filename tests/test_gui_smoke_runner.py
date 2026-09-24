#!/usr/bin/env python3
"""Only an explicit, typed advancing-work budget outcome can be incomplete."""
import importlib.util
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest


TOOL = Path(__file__).resolve().parents[1] / 'tools/run-gui-smoke.py'
SPEC = importlib.util.spec_from_file_location('gui_smoke_runner', TOOL)
helper = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(helper)

MARKER = ('INCOMPLETE GUI_SMOKE_BUDGET: phase=17 elapsed=600.011000 budget=600.000000 '
          'tx_id=13 fraction=0.032000 media_seconds=4.275000 samples=2928175 '
          'tail=0 progress_age=0.001000 result=incomplete')


def classify(code, text, allow=False):
    output = helper.Output()
    for line in text.splitlines(keepends=True):
        output.observe(line)
    return helper.classify(code, output, allow)


class SmokeClassificationTests(unittest.TestCase):
    def test_success_remains_a_pass(self):
        for allow in (False, True):
            self.assertEqual(classify(0, 'Shared GUI smoke passed.\n', allow), 'passed')

    def test_typed_budget_is_incomplete_only_with_explicit_permission(self):
        with self.assertRaises(ValueError):
            classify(75, MARKER + '\n')
        self.assertEqual(classify(75, MARKER + '\n', True), 'incomplete')
        self.assertEqual(classify(75, '\r\n' + MARKER + '\r\n', True), 'incomplete')
        # Fixed-decimal printing can round elapsed time down to the budget.
        self.assertEqual(classify(75, MARKER.replace('600.011000', '600.000000'), True), 'incomplete')
        tail = MARKER.replace('fraction=0.032000', 'fraction=1.000000').replace('tail=0', 'tail=1')
        self.assertEqual(classify(75, tail, True), 'incomplete')

    def test_failures_crashes_external_timeouts_and_lost_exit_codes_are_fatal(self):
        for code in (0, 1, 2, 3, 74, 76, 124, 126, 127, 134, 137, 139, -6, -9, -11, 3221225477):
            with self.subTest(code=code), self.assertRaises(ValueError):
                classify(code, MARKER, True)
        with self.assertRaises(ValueError):
            classify(0, 'earlier output\n' + MARKER, True)

    def test_extra_diagnostics_never_hide_behind_a_valid_marker(self):
        for extra in ('Shared GUI smoke pending assertion failed', 'Segmentation fault',
                      'terminate called after throwing an exception', 'Xvfb: server failed',
                      'unexpected diagnostic', MARKER):
            for text in (extra + '\n' + MARKER, MARKER + '\n' + extra):
                with self.subTest(text=text), self.assertRaises(ValueError):
                    classify(75, text, True)

    def test_missing_malformed_or_stale_progress_evidence_is_fatal(self):
        invalid = [
            '', 'Shared GUI smoke timed out in phase 17',
            ' INCOMPLETE GUI_SMOKE_BUDGET: phase=17', MARKER + ' ',
            MARKER.replace('GUI_SMOKE_BUDGET:', 'GUI_SMOKE_BUDGET_EXHAUSTED:'),
            MARKER.replace('phase=17', 'phase=24'), MARKER.replace('phase=17', 'phase=-1'),
            MARKER.replace('elapsed=600.011000', 'elapsed=599.999999'),
            MARKER.replace('budget=600.000000', 'budget=601.000000'),
            MARKER.replace('budget=600.000000', 'budget=9.000000'),
            MARKER.replace('tx_id=13', 'tx_id=0'),
            MARKER.replace('fraction=0.032000', 'fraction=1.000001'),
            MARKER.replace('fraction=0.032000', 'fraction=nan'),
            MARKER.replace('media_seconds=4.275000', 'media_seconds=-1.000000'),
            MARKER.replace('samples=2928175', 'samples=-1'),
            MARKER.replace('tail=0', 'tail=2'),
            MARKER.replace('progress_age=0.001000', 'progress_age=30.000001'),
            MARKER.replace('result=incomplete', 'result=passed'),
            MARKER.replace('elapsed=600.011000', 'elapsed=' + '9' * 400 + '.000000'),
            MARKER.replace('tx_id=13', 'tx_id=' + '9' * 5000),
        ]
        for text in invalid:
            with self.subTest(text=text[:160]), self.assertRaises(ValueError):
                classify(75, text, True)

    def test_recoverable_sanitizer_reports_are_fatal_even_with_exit_zero(self):
        diagnostics = ('ERROR: AddressSanitizer: heap-use-after-free',
                       'SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior',
                       'source.cpp:7: runtime error: signed integer overflow',
                       'LeakSanitizer: detected memory leaks',
                       'ThreadSanitizer: data race', 'MemorySanitizer: use-of-uninitialized-value',
                       'HWAddressSanitizer: tag-mismatch')
        for code in (0, 75):
            for diagnostic in diagnostics:
                text = diagnostic if code == 0 else MARKER + '\n' + diagnostic
                with self.subTest(code=code, diagnostic=diagnostic), self.assertRaises(ValueError):
                    classify(code, text, True)


class SmokeRunnerTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='datapump smoke runner ')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.log = self.root / 'evidence' / 'smoke.log'
        self.summary = self.root / 'summary.md'

    def invoke(self, code=0, stdout=b'', stderr=b'', allow=False, command=None):
        if command is None:
            script = ('import os,sys\n'
                      f'os.write(1,{stdout!r})\n'
                      f'os.write(2,{stderr!r})\n'
                      f'sys.exit({code})\n')
            command = [sys.executable, '-c', script]
        args = [sys.executable, str(TOOL), '--log', str(self.log), '--summary', str(self.summary)]
        if allow:
            args.append('--allow-budget-exhaustion')
        return subprocess.run([*args, '--', *command], capture_output=True, check=False, timeout=10)

    def test_cli_keeps_full_output_and_records_success(self):
        out, err = b'ordinary stdout\n', b'ordinary stderr\ninvalid UTF8: \xff\n'
        result = self.invoke(stdout=out, stderr=err)
        self.assertEqual(result.returncode, 0, result.stderr)
        evidence = self.log.read_bytes()
        self.assertIn(out, evidence)
        self.assertIn(err, evidence)
        self.assertIn(b'GUI smoke result: passed', evidence)
        self.assertIn('Native GUI smoke: passed', self.summary.read_text())
        self.assertNotIn(b'::warning', result.stdout)

    def test_cli_warns_for_incomplete_without_claiming_a_pass(self):
        self.summary.write_text('Earlier validation\n', encoding='utf-8')
        result = self.invoke(75, stderr=(MARKER + '\n').encode(), allow=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b'::warning title=Incomplete instrumented GUI smoke::', result.stdout)
        self.assertIn(b'GUI smoke result: incomplete', self.log.read_bytes())
        summary = self.summary.read_text()
        self.assertTrue(summary.startswith('Earlier validation\n'))
        self.assertIn('Native GUI smoke: incomplete', summary)
        self.assertIn('Remaining workflow coverage is incomplete', summary)
        self.assertNotIn('Native GUI smoke: passed', summary)

    def test_cli_strict_mode_does_not_accept_typed_budget(self):
        result = self.invoke(75, stderr=(MARKER + '\n').encode())
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'GUI smoke result: failed', self.log.read_bytes())
        self.assertNotIn(b'::warning', result.stdout)

    def test_cli_failure_preserves_diagnostics_and_full_log(self):
        result = self.invoke(75, stdout=(MARKER + '\n').encode(),
                             stderr=b'fixture.cpp:1: runtime error: invalid shift\n', allow=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'runtime error: invalid shift', self.log.read_bytes())
        self.assertIn('Native GUI smoke: failed', self.summary.read_text())
        self.assertNotIn(b'::warning', result.stdout)

    def test_cli_launch_failure_is_fatal_and_logged(self):
        result = self.invoke(command=[str(self.root / 'missing program')], allow=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'GUI smoke result: failed', self.log.read_bytes())
        self.assertIn(b'missing program', self.log.read_bytes())

    @unittest.skipIf(os.name == 'nt', 'POSIX signal exit semantics')
    def test_cli_signaled_child_never_becomes_incomplete(self):
        script = ('import os,signal\n'
                  f'os.write(2,{(MARKER + chr(10)).encode()!r})\n'
                  'os.kill(os.getpid(),signal.SIGTERM)\n')
        result = self.invoke(command=[sys.executable, '-c', script], allow=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(f'exit {-signal.SIGTERM}'.encode(), self.log.read_bytes())
        self.assertNotIn(b'::warning', result.stdout)

    def test_cli_never_overwrites_prior_run_evidence(self):
        self.log.parent.mkdir()
        self.log.write_bytes(b'prior evidence\n')
        result = self.invoke(allow=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.log.read_bytes(), b'prior evidence\n')
        self.assertFalse(self.summary.exists())

    def test_cli_requires_a_command(self):
        result = subprocess.run([sys.executable, str(TOOL), '--log', str(self.log), '--'],
                                capture_output=True, check=False, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.log.exists())


if __name__ == '__main__':
    unittest.main()
