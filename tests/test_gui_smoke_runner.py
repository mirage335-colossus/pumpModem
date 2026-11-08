#!/usr/bin/env python3
"""Strict UI smoke execution, timeout, sanitizer and evidence checks."""
import hashlib
import io
import importlib.util
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile


TOOL = Path(__file__).resolve().parents[1] / 'tools/run-gui-smoke.py'
SPEC = importlib.util.spec_from_file_location('gui_smoke_runner', TOOL)
helper = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(helper)

MARKER = 'INCOMPLETE old workload'


def classify(code, text):
    output = helper.Output()
    for line in text.splitlines(keepends=True):
        output.observe(line)
    return helper.classify(code, output)


class SmokeClassificationTests(unittest.TestCase):
    def test_success_remains_a_pass(self):
        self.assertEqual(classify(0, 'Shared GUI smoke passed.\n'), 'passed')


    def test_failures_crashes_external_timeouts_and_lost_exit_codes_are_fatal(self):
        for code in (0, 1, 2, 3, 74, 76, 124, 126, 127, 134, 137, 139, -6, -9, -11, 3221225477):
            with self.subTest(code=code), self.assertRaises(ValueError):
                classify(code, MARKER)
        with self.assertRaises(ValueError):
            classify(0, 'earlier output\n' + MARKER)


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
                    classify(code, text)


class SmokeRunnerTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='datapump smoke runner ')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.log = self.root / 'evidence' / 'smoke.log'
        self.summary = self.root / 'summary.md'

    def invoke(self, code=0, stdout=b'', stderr=b'', command=None, extra=(), env=None):
        if command is None:
            script = ('import os,sys\n'
                      f'os.write(1,{stdout!r})\n'
                      f'os.write(2,{stderr!r})\n'
                      f'sys.exit({code})\n')
            command = [sys.executable, '-c', script]
        args = [sys.executable, str(TOOL), '--log', str(self.log), '--summary', str(self.summary)]
        return subprocess.run([*args, *extra, '--', *command], capture_output=True, check=False, timeout=10, env=env)

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


    def test_external_deadline_is_fatal(self):
        report = self.root/'report.json'
        script = f'import time;print({MARKER!r},flush=True);time.sleep(30)'
        result = self.invoke(command=[sys.executable,'-c',script],
                             extra=['--timeout','.1','--report',str(report)])
        self.assertNotEqual(result.returncode,0)
        self.assertEqual(json.loads(report.read_text())['status'],'failed')
        self.assertNotIn(b'::warning',result.stdout)

    def test_external_deadline_stops_wrapped_child_and_its_inherited_output(self):
        child = 'import time;print("wrapped child ready",flush=True);time.sleep(12)'
        script = ('import subprocess,sys\n'
                  f'subprocess.run([sys.executable,"-c",{child!r}],check=True)\n')
        result = self.invoke( command=[sys.executable,'-c',script],
                             extra=['--timeout','2'])
        self.assertNotEqual(result.returncode,0)
        self.assertIn(b'wrapped child ready',self.log.read_bytes())
        self.assertIn(b'exceeded its 2s process timeout',self.log.read_bytes())
        self.assertNotIn(b'::warning',result.stdout)

    def test_cli_nonzero_exit_is_fatal(self):
        result = self.invoke(75, stderr=(MARKER + '\n').encode())
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'GUI smoke result: failed', self.log.read_bytes())
        self.assertNotIn(b'::warning', result.stdout)

    def test_cli_failure_preserves_diagnostics_and_full_log(self):
        result = self.invoke(75, stdout=(MARKER + '\n').encode(),
                             stderr=b'fixture.cpp:1: runtime error: invalid shift\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'runtime error: invalid shift', self.log.read_bytes())
        self.assertIn('Native GUI smoke: failed', self.summary.read_text())
        self.assertNotIn(b'::warning', result.stdout)

    def test_cli_launch_failure_is_fatal_and_logged(self):
        result = self.invoke(command=[str(self.root / 'missing program')])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'GUI smoke result: failed', self.log.read_bytes())
        self.assertIn(b'missing program', self.log.read_bytes())

    @unittest.skipIf(os.name == 'nt', 'POSIX signal exit semantics')
    def test_cli_signaled_child_is_fatal(self):
        script = ('import os,signal\n'
                  f'os.write(2,{(MARKER + chr(10)).encode()!r})\n'
                  'os.kill(os.getpid(),signal.SIGTERM)\n')
        result = self.invoke(command=[sys.executable, '-c', script])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(f'exit {-signal.SIGTERM}'.encode(), self.log.read_bytes())
        self.assertNotIn(b'::warning', result.stdout)

    def test_cli_never_overwrites_prior_run_evidence(self):
        self.log.parent.mkdir()
        self.log.write_bytes(b'prior evidence\n')
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.log.read_bytes(), b'prior evidence\n')
        self.assertFalse(self.summary.exists())

    def test_cli_requires_a_command(self):
        result = subprocess.run([sys.executable, str(TOOL), '--log', str(self.log), '--'],
                                capture_output=True, check=False, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.log.exists())


@unittest.skipUnless(os.name == 'posix' and os.open in os.supports_dir_fd and hasattr(os, 'O_NOFOLLOW'), 'Linux SDK fixture capture uses directory descriptors')


@unittest.skipUnless(shutil.which('cmake'), 'CMake is required for archive verifier integration')
class NativeArchiveSmokeContextTests(unittest.TestCase):
    def test_each_archive_passes_its_own_digest_to_the_package_verifier(self):
        with tempfile.TemporaryDirectory(prefix='archive context with spaces ') as temporary:
            root = Path(temporary)
            tools = root / 'copied verifier'
            tools.mkdir()
            source_tools = TOOL.parent
            for name in ('verify-native-archives.cmake', 'gui-smoke-timeout.cmake'):
                shutil.copyfile(source_tools / name, tools / name)
            # Exercise the real archive loop; isolate its two child verifiers
            # so this check needs no compiled application or GUI display.
            (tools / 'verify-native-package.cmake').write_text(
                'file(APPEND "$ENV{DATAPUMP_TEST_ARCHIVE_CONTEXT}" '
                '"${GUI_SMOKE_ARCHIVE_NAME} ${GUI_SMOKE_ARCHIVE_SHA256}\\n")\n')
            (tools / 'verify-linux-abi.cmake').write_text('# No binaries in this fixture.\n')
            archives = root / 'archives'
            archives.mkdir()
            data = b'synthetic package inventory\n'
            member = 'DataPump-fixture/manifest.sha256'
            tarball = archives / 'DataPump-fixture-native-rev.tar.gz'
            with tarfile.open(tarball, 'w:gz') as archive:
                info = tarfile.TarInfo(member)
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
            zipfile_path = archives / 'DataPump-fixture-native-rev.zip'
            with zipfile.ZipFile(zipfile_path, 'w') as archive:
                archive.writestr(member, data)
            record = root / 'archive contexts.txt'
            result = subprocess.run(
                [shutil.which('cmake'), f'-DARCHIVE_DIR={archives}', '-DGUI_SMOKE=ON',
                 '-P', str(tools / 'verify-native-archives.cmake')],
                env=dict(os.environ, DATAPUMP_TEST_ARCHIVE_CONTEXT=str(record)),
                capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            contexts = [line.split() for line in record.read_text().splitlines()]
            self.assertEqual(len(contexts), 2)
            self.assertEqual(dict(contexts), {
                archive.name: hashlib.sha256(archive.read_bytes()).hexdigest()
                for archive in (tarball, zipfile_path)})


if __name__ == '__main__':
    unittest.main()
