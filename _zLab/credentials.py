"""Private, local persistence for the standalone lab's RunPod API key."""
from __future__ import annotations

import os
from pathlib import Path
import secrets
import stat


MAX_KEY_BYTES = 4096


class ApiKeyStore:
    _key_name = 'api-key'
    _unsafe_directory_mode = 0o077

    def __init__(self, directory):
        self.path = Path(directory).absolute() / self._key_name

    def _directory(self, create=False):
        """Open the key directory without following a symlink component."""
        directory = self.path.parent
        flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
        fd = os.open(directory.anchor, flags)
        try:
            for part in directory.parts[1:]:
                if part == '..':
                    raise ValueError('Invalid API-key directory')
                if create:
                    try:
                        os.mkdir(part, 0o700, dir_fd=fd)
                    except FileExistsError:
                        pass
                next_fd = os.open(part, flags, dir_fd=fd)
                os.close(fd)
                fd = next_fd
            info = os.fstat(fd)
            if info.st_uid != os.getuid() or info.st_mode & self._unsafe_directory_mode:
                raise ValueError('The API-key directory has unsafe ownership or permissions')
            return fd
        except BaseException:
            os.close(fd)
            raise

    def _open_key(self, directory):
        fd = os.open(self.path.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK,
                     dir_fd=directory)
        info = os.fstat(fd)
        if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid()
                or info.st_mode & 0o077 or info.st_nlink != 1):
            os.close(fd)
            raise ValueError('The API-key file must be a private regular file owned by this user')
        return fd

    @staticmethod
    def _token(key):
        if not isinstance(key, str):
            raise ValueError('The API key must be text')
        key = key.strip()
        if len(key) > MAX_KEY_BYTES or any(ord(c) < 33 or ord(c) > 126 for c in key):
            raise ValueError('The API key must be one printable token of at most 4096 bytes')
        return key

    def load(self):
        try:
            directory = self._directory()
        except FileNotFoundError:
            return ''
        try:
            try:
                fd = self._open_key(directory)
            except FileNotFoundError:
                return ''
            with os.fdopen(fd, 'rb') as source:
                raw = source.read(MAX_KEY_BYTES + 2)
            if len(raw) > MAX_KEY_BYTES + 1:
                raise ValueError('The API-key file exceeds the size limit')
            try:
                key = raw.decode('ascii')
            except UnicodeDecodeError:
                raise ValueError('The API-key file must contain one ASCII token') from None
            return self._token(key)
        finally:
            os.close(directory)

    def save(self, key):
        key = self._token(key)
        if not key:
            self.clear()
            return
        self._write(key, replace=True)

    def _write(self, key, *, replace):
        directory = self._directory(create=True)
        temporary = f'.{self.path.name}-' + secrets.token_hex(16)
        try:
            if replace:
                try:
                    os.close(self._open_key(directory))
                except FileNotFoundError:
                    pass
            fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                         0o600, dir_fd=directory)
            try:
                with os.fdopen(fd, 'wb') as target:
                    os.fchmod(target.fileno(), 0o600)
                    target.write((key + '\n').encode('ascii'))
                    target.flush()
                    os.fsync(target.fileno())
                if replace:
                    os.replace(temporary, self.path.name, src_dir_fd=directory, dst_dir_fd=directory)
                else:
                    os.link(temporary, self.path.name, src_dir_fd=directory, dst_dir_fd=directory,
                            follow_symlinks=False)
            finally:
                try:
                    os.unlink(temporary, dir_fd=directory)
                except FileNotFoundError:
                    pass
            os.fsync(directory)
        finally:
            os.close(directory)

    def clear(self):
        try:
            directory = self._directory()
        except FileNotFoundError:
            return
        try:
            try:
                os.close(self._open_key(directory))
            except FileNotFoundError:
                return
            os.unlink(self.path.name, dir_fd=directory)
            os.fsync(directory)
        finally:
            os.close(directory)


class _CheckoutApiKeyStore(ApiKeyStore):
    _key_name = 'runpod-api-key'
    _unsafe_directory_mode = 0o002


class RepositoryApiKeyStore(_CheckoutApiKeyStore):
    """Store the key at the checkout root and import keys from older versions."""

    def __init__(self, legacy_directory, *, repository_directory=None):
        super().__init__(repository_directory if repository_directory is not None
                         else Path(__file__).resolve().parents[1])
        self._legacy = (_CheckoutApiKeyStore(self.path.parent / '_zLab'),
                        ApiKeyStore(legacy_directory))

    def load(self):
        key = super().load()
        for store in self._legacy:
            try:
                legacy_key = store.load()
            except (OSError, ValueError):
                if key:
                    continue
                raise
            if not legacy_key:
                continue
            if not key:
                try:
                    self._write(legacy_key, replace=False)
                except FileExistsError:
                    pass
                key = super().load()
            if key == legacy_key and store.load() == legacy_key:
                store.clear()
        return key

    def clear(self):
        # Explicit clearing removes sources of automatic migration first.
        for store in self._legacy:
            store.clear()
        super().clear()
