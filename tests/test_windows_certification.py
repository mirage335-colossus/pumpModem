#!/usr/bin/env python3
"""The hosted WGL exception must not hide crashes or application assertions."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('windows_certification',
    Path(__file__).resolve().parents[1] / 'tools/windows-certification.py')
helper = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(helper)


class WindowsCertificationTests(unittest.TestCase):
    def test_success_keeps_all_graphics_checks(self):
        self.assertEqual(helper.classify(0, 'Native coordinates passed', 'win32'), [])

    def test_exact_wgl_absence_is_a_scoped_warning(self):
        self.assertEqual(helper.classify(1, helper.WGL_MESSAGE + '\r\n', 'win32'),
                         [helper.warning_record()])

    def test_assertions_extra_output_crashes_and_other_platforms_fail(self):
        for code, output, platform in [
            (1, 'Focus assertion failed', 'win32'),
            (1, helper.WGL_MESSAGE + '\nFocus assertion failed', 'win32'),
            (-1, helper.WGL_MESSAGE, 'win32'),
            (3221225477, helper.WGL_MESSAGE, 'win32'),
            (1, helper.WGL_MESSAGE, 'linux'),
            (1, 'Failed to create WGL context', 'win32'),
            (1, '', 'win32'),
        ]:
            with self.subTest(code=code, output=output, platform=platform):
                with self.assertRaises(ValueError):
                    helper.classify(code, output, platform)

    def test_probe_records_warning_and_only_disables_native_smoke(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = root / 'test_rev_coordinates.exe'
            executable.write_bytes(b'fixture')
            output, summary = root / 'output', root / 'summary'
            result = subprocess.CompletedProcess([], 1, helper.WGL_MESSAGE + '\n')
            with patch.object(helper.sys, 'platform', 'win32'), \
                 patch.object(helper.subprocess, 'run', return_value=result) as run:
                helper.probe(executable, output, summary)
            self.assertEqual(run.call_args.args[0], [str(executable.resolve()), '1'])
            self.assertEqual(run.call_args.kwargs['timeout'], 90)
            self.assertIn('gui_smoke=OFF', output.read_text())
            self.assertIn('source:gui_coordinates_1x', output.read_text())
            self.assertNotIn('gui_platform_conformance', output.read_text())
            self.assertIn('does not qualify', summary.read_text())

    def test_probe_timeout_never_emits_success_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = root / 'test.exe'
            executable.touch()
            output = root / 'output'
            with patch.object(helper.sys, 'platform', 'win32'), \
                 patch.object(helper.subprocess, 'run',
                              side_effect=subprocess.TimeoutExpired('test', 90)):
                with self.assertRaises(subprocess.TimeoutExpired):
                    helper.probe(executable, output)
            self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
