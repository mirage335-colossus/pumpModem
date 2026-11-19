"""Offline checks for private, restartable API-key persistence."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from credentials import ApiKeyStore, MAX_KEY_BYTES, RepositoryApiKeyStore


class ApiKeyStoreTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.store = ApiKeyStore(self.root / 'state' / 'runpod-lab')

    def test_save_replace_restart_and_clear(self):
        self.assertEqual(self.store.load(), '')
        self.assertFalse(self.store.path.parent.exists())
        self.store.save(' temporary-example-token ')
        self.assertEqual(self.store.path.read_text(), 'temporary-example-token\n')
        self.assertEqual(self.store.path.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.store.path.parent.stat().st_mode & 0o777, 0o700)
        self.assertEqual(ApiKeyStore(self.store.path.parent).load(), 'temporary-example-token')
        self.store.save('replacement-token')
        self.assertEqual(self.store.load(), 'replacement-token')
        self.store.save('')
        self.assertFalse(self.store.path.exists())
        self.store.clear()
        self.assertEqual(self.store.load(), '')

    def test_failed_atomic_replace_preserves_old_key(self):
        self.store.save('original-token')
        with patch('credentials.os.replace', side_effect=OSError('injected write failure')):
            with self.assertRaises(OSError):
                self.store.save('replacement-token')
        self.assertEqual(self.store.load(), 'original-token')
        self.assertEqual(list(self.store.path.parent.iterdir()), [self.store.path])

    def test_unsafe_directory_or_file_is_rejected(self):
        self.store.save('original-token')
        for path, mode in ((self.store.path, 0o644), (self.store.path.parent, 0o755)):
            original = path.stat().st_mode & 0o777
            path.chmod(mode)
            try:
                for operation in (self.store.load, lambda: self.store.save('replacement-token'), self.store.clear):
                    with self.assertRaises(ValueError):
                        operation()
            finally:
                path.chmod(original)
        self.assertEqual(self.store.load(), 'original-token')

    def test_symlink_paths_and_fifo_are_rejected(self):
        target = self.root / 'target'
        target.mkdir(mode=0o700)
        link = self.root / 'link'
        link.symlink_to(target, target_is_directory=True)
        with self.assertRaises(OSError):
            ApiKeyStore(link / 'child').save('token')
        self.assertEqual(list(target.iterdir()), [])
        self.store.save('original-token')
        external = self.root / 'external'
        external.write_text('untouched')
        self.store.path.unlink()
        self.store.path.symlink_to(external)
        for operation in (self.store.load, lambda: self.store.save('token'), self.store.clear):
            with self.assertRaises(OSError):
                operation()
        self.assertEqual(external.read_text(), 'untouched')
        self.store.path.unlink()
        os.mkfifo(self.store.path, 0o600)
        with self.assertRaises(ValueError):
            self.store.load()

    def test_malformed_and_large_inputs_never_echo_contents(self):
        self.store.save('original-token')
        for token in ('private value', 'private\nvalue', '\x00private', 'éprivate', 'x' * (MAX_KEY_BYTES + 1)):
            with self.assertRaises(ValueError) as raised:
                self.store.save(token)
            self.assertNotIn(token, str(raised.exception))
            self.assertEqual(self.store.load(), 'original-token')
        for raw in (b'private\nvalue', b'\xffprivate', b'x' * (MAX_KEY_BYTES + 2)):
            self.store.path.write_bytes(raw)
            with self.assertRaises(ValueError):
                self.store.load()
        self.store.path.write_bytes(b'manually-edited-token\r\n')
        self.assertEqual(self.store.load(), 'manually-edited-token')


class RepositoryApiKeyStoreTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        self.legacy = ApiKeyStore(root / 'state')
        repository_directory = root / 'checkout'
        repository_directory.mkdir()
        repository_directory.chmod(0o775)
        self.store = RepositoryApiKeyStore(self.legacy.path.parent,
                                           repository_directory=repository_directory)
        self.previous_path = repository_directory / '_zLab' / 'runpod-api-key'

    def create_previous_key(self, key):
        self.previous_path.parent.mkdir()
        self.previous_path.write_text(key + '\n')
        self.previous_path.chmod(0o600)

    def test_migrate_to_gitignored_file_in_group_writable_checkout(self):
        self.legacy.save('existing-token')
        self.assertEqual(self.store.load(), 'existing-token')
        self.assertEqual(self.store.path.name, 'runpod-api-key')
        self.assertEqual(self.store.path.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.store.path.parent.stat().st_mode & 0o777, 0o775)
        self.assertFalse(self.legacy.path.exists())
        self.assertEqual(self.store.load(), 'existing-token')
        self.assertEqual(list(self.store.path.parent.iterdir()), [self.store.path])

    def test_migrates_from_lab_subdirectory_and_retires_duplicate_state_copy(self):
        self.create_previous_key('previous-lab-token')
        self.legacy.save('previous-lab-token')
        self.assertEqual(self.store.load(), 'previous-lab-token')
        self.assertEqual(self.store.path.read_text(), 'previous-lab-token\n')
        self.assertEqual(self.store.path.stat().st_mode & 0o777, 0o600)
        self.assertFalse(self.previous_path.exists())
        self.assertFalse(self.legacy.path.exists())

    def test_migration_prefers_previous_lab_key_and_preserves_different_state_copy(self):
        self.create_previous_key('previous-lab-token')
        self.legacy.save('older-state-token')
        self.assertEqual(self.store.load(), 'previous-lab-token')
        self.assertFalse(self.previous_path.exists())
        self.assertEqual(self.legacy.load(), 'older-state-token')

    def test_repository_value_wins_and_explicit_clear_cannot_restore_old_key(self):
        self.create_previous_key('previous-lab-token')
        self.legacy.save('old-token')
        self.store.save('repository-token')
        self.assertEqual(self.store.load(), 'repository-token')
        self.assertEqual(self.previous_path.read_text(), 'previous-lab-token\n')
        self.assertEqual(self.legacy.load(), 'old-token')
        self.store.clear()
        self.assertFalse(self.store.path.exists())
        self.assertFalse(self.previous_path.exists())
        self.assertFalse(self.legacy.path.exists())
        self.assertEqual(self.store.load(), '')

    def test_migration_write_failure_keeps_legacy_value(self):
        self.legacy.save('old-token')
        with patch('credentials.os.link', side_effect=OSError('injected write failure')):
            with self.assertRaises(OSError):
                self.store.load()
        self.assertEqual(list(self.store.path.parent.iterdir()), [])
        self.assertEqual(self.legacy.load(), 'old-token')
        self.assertEqual(self.store.load(), 'old-token')

    def test_migration_does_not_replace_key_created_concurrently(self):
        self.legacy.save('old-token')
        original_link = os.link

        def competing_write(*args, **kwargs):
            self.store.save('new-token')
            return original_link(*args, **kwargs)

        with patch('credentials.os.link', side_effect=competing_write):
            self.assertEqual(self.store.load(), 'new-token')
        self.assertEqual(self.legacy.load(), 'old-token')

    def test_world_writable_repository_rejected(self):
        self.legacy.save('old-token')
        self.store.path.parent.chmod(0o777)
        for operation in (self.store.load, lambda: self.store.save('replacement-token')):
            with self.assertRaises(ValueError):
                operation()
        self.assertEqual(self.legacy.load(), 'old-token')
        self.assertFalse(self.store.path.exists())


if __name__ == '__main__':
    unittest.main()
