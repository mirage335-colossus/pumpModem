#!/usr/bin/env python3
"""Verify notices retained alongside the diagnostic and its manifest."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('retain_paced', ROOT / 'tools/retain-paced-diagnostic.py')
retain = importlib.util.module_from_spec(spec)
spec.loader.exec_module(retain)


class RetainedNoticesTest(unittest.TestCase):
    def test_botan_license_is_copied_and_in_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            parent = root / 'build/symbol-burst-live-qualification'
            parent.mkdir(parents=True)
            binary = root / 'build/test_symbol_burst_live'
            binary.write_bytes(b'fixture binary')
            binary.chmod(0o755)
            source = root / 'tests/test_symbol_burst_live.cpp'
            source.parent.mkdir()
            source.write_text('fixture source')
            for name in ('source.txt', 'source-tree.txt', 'CMakeCache.txt', 'source-inventory.json'):
                (parent / name).write_text('fixture')
            (root / 'build/CMakeCache.txt').write_text(f'CMAKE_CXX_COMPILER:FILEPATH={sys.executable}\n')
            for name in ('LICENSE', 'third_party/qrcodegen/LICENSE', 'third_party/xz/COPYING.0BSD',
                         'third_party/xz/README.datapump.md', 'third_party/botan/LICENSE'):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('fixture ' + name)
            report = parent / 'retained'
            def fake_run(command, **kwargs):
                return 'fixture version\n' if command[:2] == ['ldd', '--version'] else ''
            args = ['retain-paced-diagnostic', '--root', str(root), '--report', str(report), '--runtime-probe', 'off']
            with patch.object(sys, 'argv', args), patch.object(retain, 'run', side_effect=fake_run), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(retain.main(), 0)
            notice = report / 'notices/BOTAN-LICENSE'
            self.assertEqual(notice.read_bytes(), (root / 'third_party/botan/LICENSE').read_bytes())
            manifest = json.loads((report / 'manifest.json').read_text())
            records = {record['retained_path']: record for record in manifest['installed_runtime_notices']}
            self.assertEqual(records['notices/BOTAN-LICENSE']['sha256'], retain.sha256(notice))
            self.assertEqual(manifest['files']['notices/BOTAN-LICENSE']['sha256'], retain.sha256(notice))


if __name__ == '__main__':
    unittest.main()
