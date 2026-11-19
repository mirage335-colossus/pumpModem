import hashlib
import json
from pathlib import Path
import tarfile
import tempfile
import unittest

import bundle


class BundleTest(unittest.TestCase):
    def test_allowlisted_deterministic_archive_and_manifest(self):
        root = Path(__file__).resolve().parent.parent
        with tempfile.TemporaryDirectory() as directory:
            first, second = Path(directory) / 'one.tar.gz', Path(directory) / 'two.tar.gz'
            info = bundle.create_bundle(root, first)
            bundle.create_bundle(root, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with tarfile.open(first, 'r:gz') as archive:
                members = archive.getmembers()
                names = {m.name for m in members}
                self.assertIn('_zLab/worker_relay.py', names)
                for name in ('inject.cmake', 'protocol.hpp', 'remote.hpp', 'remote.cpp',
                             'provider.cpp', 'worker.cpp', 'local.hpp', 'local.cpp',
                             'local_native.cpp', 'local_main.cpp'):
                    self.assertIn('_zLab/audio/' + name, names)
                self.assertNotIn('_zLab/audio/test_remote.cpp', names)
                self.assertIn('src/host/worker.cpp', names)
                self.assertIn('tests/gui_adapter_boundary.cmake', names)
                self.assertIn('tests/legacy_boundary.cmake', names)
                self.assertIn('tests/fast_boundary.cmake', names)
                self.assertIn('third_party/ldpc/tables.hpp', names)
                self.assertNotIn('_zLab/runpod.py', names)
                self.assertNotIn('_tests-RobustModem.md', names)
                self.assertTrue(all(m.isfile() and not m.name.startswith('/')
                                    and '..' not in Path(m.name).parts for m in members))
                manifest_bytes = archive.extractfile('BUNDLE-MANIFEST.json').read()
                self.assertEqual(hashlib.sha256(manifest_bytes).hexdigest(), info['manifest_sha256'])
                records = json.loads(manifest_bytes)['files']
                self.assertEqual(len(records), info['file_count'])
                for record in records:
                    self.assertEqual(hashlib.sha256(archive.extractfile(record['path']).read()).hexdigest(),
                                     record['sha256'])
            with self.assertRaises(bundle.BundleError):
                bundle.create_bundle(root, first)

    def test_destination_inside_source_tree_rejected(self):
        root = Path(__file__).resolve().parent.parent
        with self.assertRaises(bundle.BundleError):
            bundle.create_bundle(root, root / '_zLab' / 'archive.tar.gz')


if __name__ == '__main__':
    unittest.main()
