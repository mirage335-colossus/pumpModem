import base64
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import host_identity as identity


class HostIdentityTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.directory = Path(tmp.name)
        # RFC 8032 test vector 1, public key only.
        key = bytes.fromhex('d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a')
        encoded = base64.b64encode(bytes.fromhex('0000000b') + b'ssh-ed25519' +
                                   bytes.fromhex('00000020') + key)
        public = b'ssh-ed25519 ' + encoded
        self.endpoint = b'[192.0.2.1]:2200'
        self.line = self.endpoint + b' ' + public + b'\n'
        keyfile = self.directory / 'vector.pub'
        keyfile.write_bytes(public + b' root@test\n')
        self.info = subprocess.check_output(['ssh-keygen', '-lf', keyfile], text=True).strip()
        self.logs = ['Pod Started', 'ED25519 key fingerprint:', self.info, 'SSH host keys:',
                     'Key: /etc/ssh/ssh_host_ed25519_key.pub', self.info,
                     'Key: /etc/ssh/ssh_host_rsa_key.pub', '3072 SHA256:other root@test (RSA)',
                     'Exporting environment variables...']
        self.fingerprint = self.info.split()[1]

    def verify(self, scan, *, host='192.0.2.1', port=2200, state_dir=None, fingerprint=None):
        with patch.object(identity, '_bounded_process', return_value=(0, scan)) as run:
            path = identity.verify_host_key('test-id', host, port, fingerprint or self.fingerprint,
                                            state_dir or self.directory)
            run.assert_called_once_with(['ssh-keyscan', '-T', '5', '-p', str(port),
                                         '-t', 'ed25519', host], None, 8)
            return path

    def test_openssh_match_pin_and_reopen(self):
        actual = identity.startup_fingerprint(iter(self.logs))
        self.assertEqual(self.fingerprint, actual)
        self.assertIsNone(identity.existing_host_key('test-id', '192.0.2.1', 2200, self.directory))
        pin = self.verify(b'# 192.0.2.1:2200 SSH-2.0-OpenSSH\n' + self.line, fingerprint=actual)
        self.assertEqual(0o600, pin.stat().st_mode & 0o777)
        self.assertEqual(self.line, pin.read_bytes())
        # Real ssh, via ssh-keygen -F, resolves exactly the host/port written.
        found = subprocess.check_output(['ssh-keygen', '-F', self.endpoint, '-f', pin])
        self.assertIn(self.line, found)
        self.assertEqual(pin, identity.existing_host_key('test-id', '192.0.2.1', 2200, self.directory))
        self.assertEqual(pin, self.verify(self.line))
        with self.assertRaises(ValueError):
            identity.existing_host_key('test-id', '192.0.2.1', 22, self.directory)
        self.assertIsNone(identity.existing_host_key('second-id', '192.0.2.1', 2200, self.directory))
        self.assertEqual(self.line, pin.read_bytes())

    def test_forget_exact_pin_only(self):
        pin = self.verify(self.line)
        identity.forget_host_key('second-id', self.directory)
        self.assertTrue(pin.exists())
        identity.forget_host_key('test-id', self.directory)
        self.assertFalse(pin.exists())
        identity.forget_host_key('test-id', self.directory)
        original = self.directory / 'vector.pub'
        pin.symlink_to(original)
        with self.assertRaises(OSError):
            identity.forget_host_key('test-id', self.directory)
        self.assertTrue(original.is_file())
        self.assertTrue(pin.is_symlink())

    def test_default_port(self):
        line = self.line.replace(self.endpoint, b'192.0.2.1')
        pin = self.verify(line, port=22)
        self.assertIn(line, subprocess.check_output(['ssh-keygen', '-F', '192.0.2.1', '-f', pin]))

    def test_unrelated_log_lines_and_contradictions(self):
        header, label, info, end = self.logs[3], self.logs[4], self.info, self.logs[-1]
        different = info.replace(self.fingerprint, 'SHA256:' + 'X' * 43)
        for lines in ([label, info, end], [header, label, info], [header, info, end],
                      [header, label, 'not-keygen-output', info, end],
                      [header, 'Key: /etc/ssh/user-key.pub', info, end],
                      [header, label, info.replace('(ED25519)', '(RSA)'), end],
                      [header, label, info, label, different, end]):
            with self.subTest(lines=lines), self.assertRaises(ValueError):
                identity.startup_fingerprint(lines)

    def test_mismatch_malformed_duplicate_and_oversized_scan(self):
        for output in (self.line.replace(self.endpoint, b'[example.test]:2200'),
                       self.line.replace(b'ssh-ed25519 ', b'ssh-rsa '),
                       self.line + self.line.replace(self.endpoint, b'alias'),
                       self.line[:25], self.line + b'#' * 4096):
            with self.subTest(output=output[:30]), self.assertRaises(ValueError):
                self.verify(output)
        with self.assertRaisesRegex(ValueError, 'mismatch'):
            self.verify(self.line, fingerprint='SHA256:' + 'A' * 43)
        self.assertEqual([], list(self.directory.glob('known_hosts-*')))
        with patch.object(identity, '_bounded_process') as run:
            for host, port in [('-f', 2222), ('host,test', 2222), ('host', True), ('host', 0)]:
                with self.assertRaises(ValueError):
                    identity.verify_host_key('test-id', host, port, self.fingerprint, self.directory)
            run.assert_not_called()

    def test_no_replacement_and_symlink_rejection(self):
        target = self.verify(self.line)
        target.write_bytes(b'previous pin\n')
        with self.assertRaises(ValueError):
            self.verify(self.line)
        self.assertEqual(b'previous pin\n', target.read_bytes())
        target.unlink()
        other = self.directory / 'other'
        other.write_bytes(self.line)
        target.symlink_to(other)
        with self.assertRaises(OSError):
            self.verify(self.line)
        with self.assertRaises(OSError):
            identity.existing_host_key('test-id', '192.0.2.1', 2200, self.directory)
        self.assertTrue(target.is_symlink())
        self.assertEqual(self.line, other.read_bytes())
        alias = self.directory / 'alias'
        alias.symlink_to(self.directory)
        with self.assertRaises(OSError):
            self.verify(self.line, state_dir=alias)
        self.assertEqual([], list(self.directory.glob('.ssh-*')))


if __name__ == '__main__':
    unittest.main()
