"""Offline checks for private, restartable API-key persistence."""
from concurrent.futures import ThreadPoolExecutor
import fcntl
import os
from pathlib import Path
import tempfile
from threading import Event
import unittest
from unittest.mock import patch

from credentials import ApiKeyStore, LinodeRepositoryApiKeyStore, MAX_KEY_BYTES, RepositoryApiKeyStore


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
        self.checkout = root / 'checkout'
        self.checkout.mkdir(mode=0o775)
        self.checkout.chmod(0o775)
        self.store = RepositoryApiKeyStore(self.legacy.path.parent,
                                           repository_directory=self.checkout)
        self.previous_path = self.checkout / '_zLab' / 'runpod-api-key'

    def create_previous_key(self, key):
        self.previous_path.parent.mkdir()
        self.previous_path.write_text(key + '\n')
        self.previous_path.chmod(0o600)

    def test_linode_token_is_private_and_isolated_from_runpod_migration_and_clear(self):
        self.create_previous_key('old-runpod-token')
        linode = LinodeRepositoryApiKeyStore(self.checkout)
        self.assertEqual(linode.path, self.checkout / 'Cloud_API_Keys' / 'linode-api-key')
        self.assertEqual(linode.load(), '')
        self.assertFalse(linode.path.parent.exists())
        linode.save('linode-token')
        self.assertEqual(linode.path.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.store.load(), 'old-runpod-token')
        self.store.clear()
        self.assertEqual(LinodeRepositoryApiKeyStore(self.checkout).load(), 'linode-token')
        self.store.save('new-runpod-token')
        linode.clear()
        self.assertEqual(linode.load(), '')
        self.assertEqual(self.store.load(), 'new-runpod-token')

    def test_migrate_to_gitignored_file_in_group_writable_checkout(self):
        self.legacy.save('existing-token')
        self.assertEqual(self.store.load(), 'existing-token')
        self.assertEqual(self.store.path, self.checkout / 'Cloud_API_Keys' / 'runpod-api-key')
        self.assertEqual(self.store.path.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.store.path.parent.stat().st_mode & 0o777, 0o700)
        self.assertEqual(self.checkout.stat().st_mode & 0o777, 0o775)
        self.assertFalse(self.legacy.path.exists())
        self.assertEqual(self.store.load(), 'existing-token')
        self.assertEqual(list(self.store.path.parent.iterdir()), [self.store.path])

    def test_both_checkout_root_keys_migrate_to_private_directory(self):
        linode = LinodeRepositoryApiKeyStore(self.checkout)
        self.create_previous_key('earlier-runpod-token')
        for store, token in ((self.store, 'runpod-token'), (linode, 'linode-token')):
            with self.subTest(store=store.path.name):
                old = self.checkout / store.path.name
                old.write_text(token + '\n')
                old.chmod(0o600)
                self.assertEqual(store.load(), token)
                self.assertEqual(store.path.read_text(), token + '\n')
                self.assertFalse(old.exists())
        self.assertEqual(self.previous_path.read_text(), 'earlier-runpod-token\n')

    def test_conflicting_linode_root_key_stays_until_explicit_clear(self):
        store = LinodeRepositoryApiKeyStore(self.checkout)
        store.save('current-token')
        old = self.checkout / 'linode-api-key'
        old.write_text('another-token\n')
        old.chmod(0o600)
        self.assertEqual(store.load(), 'current-token')
        self.assertEqual(old.read_text(), 'another-token\n')
        store.clear()
        self.assertFalse(old.exists())
        self.assertEqual(store.load(), '')

    def test_private_directory_rejects_symlink_or_shared_permissions(self):
        old = self.checkout / 'linode-api-key'
        old.write_text('old-token\n')
        old.chmod(0o600)
        store = LinodeRepositoryApiKeyStore(self.checkout)
        key_directory = self.checkout / 'Cloud_API_Keys'
        target = self.checkout.parent / 'elsewhere'
        target.mkdir(mode=0o700)
        key_directory.symlink_to(target, target_is_directory=True)
        with self.assertRaises(OSError):
            store.load()
        self.assertEqual(list(target.iterdir()), [])
        key_directory.unlink()
        key_directory.mkdir()
        key_directory.chmod(0o755)
        with self.assertRaises(ValueError):
            store.load()
        self.assertEqual(old.read_text(), 'old-token\n')
        self.assertFalse(store.path.exists())

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
        with patch('credentials.os.replace', side_effect=OSError('injected write failure')):
            with self.assertRaises(OSError):
                self.store.load()
        self.assertEqual(list(self.store.path.parent.iterdir()), [])
        self.assertEqual(self.legacy.load(), 'old-token')
        self.assertEqual(self.store.load(), 'old-token')

    def test_migration_does_not_replace_key_created_concurrently(self):
        self.legacy.save('old-token')
        original_write = self.store._write

        def competing_write(directory, key, *, replace):
            original_write(directory, 'new-token', replace=True)
            return original_write(directory, key, replace=replace)

        with patch.object(self.store, '_write', side_effect=competing_write):
            self.assertEqual(self.store.load(), 'new-token')
        self.assertEqual(self.legacy.load(), 'old-token')

    def race_migration_with(self, operation):
        self.legacy.save('old-token')
        reached = Event()
        original_flock = fcntl.flock
        original_write = self.store._write

        def observe_contention(directory, mode):
            try:
                original_flock(directory, mode | fcntl.LOCK_NB)
            except BlockingIOError:
                reached.set()
                original_flock(directory, mode)

        def compete():
            try:
                operation()
            finally:
                reached.set()

        # Pause startup after it reads the legacy key. Give the other controller
        # time to finish the competing operation or block on a held lock.
        with ThreadPoolExecutor(max_workers=1) as worker:
            def publish(*args, **kwargs):
                nonlocal future
                future = worker.submit(compete)
                self.assertTrue(reached.wait(5))
                return original_write(*args, **kwargs)

            future = None
            with patch('fcntl.flock', observe_contention), patch.object(self.store, '_write', publish):
                self.assertEqual(self.store.load(), 'old-token')
                future.result(timeout=5)

    def test_clear_during_migration_cannot_revive_a_removed_key(self):
        other = RepositoryApiKeyStore(self.legacy.path.parent, repository_directory=self.checkout)
        self.race_migration_with(other.clear)
        self.assertEqual(self.store.load(), '')
        self.assertFalse(self.legacy.path.exists())

    def test_replacing_the_legacy_key_during_migration_preserves_the_new_value(self):
        self.race_migration_with(lambda: self.legacy.save('replacement-token'))
        self.assertEqual(self.store.load(), 'old-token')
        self.assertEqual(self.legacy.load(), 'replacement-token')

    def test_legacy_state_can_share_the_private_key_directory(self):
        store = RepositoryApiKeyStore(self.store.path.parent, repository_directory=self.checkout)
        source = ApiKeyStore(self.store.path.parent)
        source.save('old-token')
        self.assertEqual(store.load(), 'old-token')
        self.assertFalse(source.path.exists())

    def test_crash_immediately_after_migration_publishes_a_readable_key(self):
        self.legacy.save('old-token')

        def exit_after(publish):
            def interrupted(*args, **kwargs):
                publish(*args, **kwargs)
                os._exit(42)
            return interrupted

        pid = os.fork()
        if pid == 0:
            try:
                with patch('os.replace', exit_after(os.replace)), patch('os.link', exit_after(os.link)):
                    self.store.load()
            finally:
                os._exit(1)
        _, status = os.waitpid(pid, 0)
        self.assertEqual(os.waitstatus_to_exitcode(status), 42)
        self.assertEqual(self.legacy.load(), 'old-token')
        self.assertEqual(self.store.load(), 'old-token')
        self.assertFalse(self.legacy.path.exists())
        self.assertEqual(list(self.store.path.parent.iterdir()), [self.store.path])

    def test_world_writable_repository_rejected(self):
        self.legacy.save('old-token')
        self.checkout.chmod(0o777)
        for operation in (self.store.load, lambda: self.store.save('replacement-token')):
            with self.assertRaises(ValueError):
                operation()
        self.assertEqual(self.legacy.load(), 'old-token')
        self.assertFalse(self.store.path.exists())


if __name__ == '__main__':
    unittest.main()
