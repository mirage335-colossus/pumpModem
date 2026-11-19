import base64
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import bootstrap
import host_identity


class BootstrapTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.name = 'pump-lab-' + 'a' * 24

    def prepare(self, name=None):
        return bootstrap.prepare_bootstrap(self.directory, name or self.name, 'runpod/pytorch:test')

    def test_generation_persistence_and_independent_host_identity(self):
        record, payload = self.prepare()
        private = Path(record['ssh_identity'])
        self.assertEqual(private.stat().st_mode & 0o777, 0o600)
        public = subprocess.check_output(['ssh-keygen', '-y', '-f', private], text=True).strip()
        self.assertEqual(payload['env']['PUBLIC_KEY'], ' '.join(public.split()[:2]))
        self.assertNotIn('PRIVATE', json.dumps(record))
        self.assertNotIn(payload['env']['PUMP_LAB_HOST_KEY_B64'], json.dumps(record))
        self.assertEqual([private], list(self.directory.iterdir()))
        self.assertEqual(payload['entrypoint'], ['/bin/bash', '-c'])
        self.assertEqual(payload['cmd'], [bootstrap.STARTUP])
        other_record, other_payload = self.prepare('pump-lab-' + 'b' * 24)
        self.assertNotEqual(record['ssh_fingerprint'], other_record['ssh_fingerprint'])
        self.assertNotEqual(payload['env']['PUBLIC_KEY'], other_payload['env']['PUBLIC_KEY'])
        bootstrap.forget_bootstrap(dict(record, name=self.name), self.directory)
        self.assertFalse(private.exists())
        self.assertTrue(Path(other_record['ssh_identity']).exists())

    def test_startup_installs_expected_key_and_removes_secret_from_environment(self):
        record, payload = self.prepare()
        root = self.directory / 'container'
        root.mkdir()
        startup = root / 'start.sh'
        startup.write_text('#!/bin/bash\nset -eu\ntest -z "${PUMP_LAB_HOST_KEY_B64+x}"\n'
                           'test -n "$PUBLIC_KEY"\nprintf ready\n')
        startup.chmod(0o700)
        # Keep the production shell's operations and substitute its absolute
        # container paths to execute the exact installation in this fixture.
        command = bootstrap.STARTUP.replace('/etc/ssh', str(root / 'ssh')).replace('/start.sh', str(startup))
        result = subprocess.run(['/bin/bash', '-c', command], env={**os.environ, **payload['env']},
                                capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(result.stdout, b'ready')
        installed = root / 'ssh/ssh_host_ed25519_key'
        self.assertEqual(installed.read_bytes(), base64.b64decode(payload['env']['PUMP_LAB_HOST_KEY_B64']))
        self.assertEqual(installed.stat().st_mode & 0o777, 0o600)
        fingerprint = subprocess.check_output(['ssh-keygen', '-lf', installed.with_suffix('.pub')], text=True).split()[1]
        self.assertEqual(fingerprint, record['ssh_fingerprint'])
        offered = b' '.join(installed.with_suffix('.pub').read_bytes().split()[:2])
        with patch.object(host_identity, '_bounded_process', return_value=(0, b'[localhost]:2200 ' + offered + b'\n')):
            pin = host_identity.verify_host_key('owned-pod', 'localhost', 2200, fingerprint, self.directory)
        self.assertIn(offered, pin.read_bytes())
        other, _ = self.prepare('pump-lab-' + 'c' * 24)
        with patch.object(host_identity, '_bounded_process', return_value=(0, b'[localhost]:2201 ' + offered + b'\n')):
            with self.assertRaisesRegex(ValueError, 'mismatch'):
                host_identity.verify_host_key('wrong-pod', 'localhost', 2201, other['ssh_fingerprint'], self.directory)

    def test_no_overwrite_or_unsafe_identity_cleanup(self):
        record, _ = self.prepare()
        before = Path(record['ssh_identity']).read_bytes()
        with self.assertRaises(FileExistsError):
            self.prepare()
        self.assertEqual(Path(record['ssh_identity']).read_bytes(), before)
        with self.assertRaises(ValueError):
            bootstrap.forget_bootstrap(dict(record, name='pump-lab-' + 'b' * 24), self.directory)
        private = Path(record['ssh_identity'])
        private.unlink()
        target = self.directory / 'unrelated'
        target.write_text('keep')
        private.symlink_to(target)
        with self.assertRaises(ValueError):
            bootstrap.forget_bootstrap(dict(record, name=self.name), self.directory)
        self.assertEqual(target.read_text(), 'keep')

    def test_generation_failure_and_unsafe_state_leave_no_credential(self):
        with patch.object(bootstrap.subprocess, 'run', side_effect=FileNotFoundError):
            with self.assertRaisesRegex(ValueError, 'OpenSSH'):
                self.prepare()
        self.assertEqual(list(self.directory.iterdir()), [])
        self.directory.chmod(0o755)
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertEqual(list(self.directory.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
