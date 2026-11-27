"""Private, local persistence for the standalone lab's provider API keys."""
from __future__ import annotations

import fcntl
from contextlib import contextmanager, ExitStack
import os
from pathlib import Path
import secrets
import stat


MAX_KEY_BYTES = 4096
KEY_DIRECTORY = 'Cloud_API_Keys'


class ApiKeyStore:
    _key_name = 'api-key'
    _unsafe_directory_mode = 0o077
    _unsafe_parent_mode = None

    def __init__(self, directory):
        self.path = Path(directory).absolute() / self._key_name

    def _directory(self, create=False):
        """Open the key directory without following a symlink component."""
        directory = self.path.parent
        flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
        fd = os.open(directory.anchor, flags)
        try:
            for index, part in enumerate(directory.parts[1:], start=1):
                if part == '..':
                    raise ValueError('Invalid API-key directory')
                if index == len(directory.parts) - 1 and self._unsafe_parent_mode is not None:
                    self._check_directory(fd, self._unsafe_parent_mode)
                if create:
                    try:
                        os.mkdir(part, 0o700, dir_fd=fd)
                    except FileExistsError:
                        pass
                next_fd = os.open(part, flags, dir_fd=fd)
                os.close(fd)
                fd = next_fd
            self._check_directory(fd, self._unsafe_directory_mode)
            return fd
        except BaseException:
            os.close(fd)
            raise

    @staticmethod
    def _check_directory(fd, unsafe_mode):
        info = os.fstat(fd)
        if info.st_uid != os.getuid() or info.st_mode & unsafe_mode:
            raise ValueError('The API-key directory has unsafe ownership or permissions')

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

    @contextmanager
    def _locked_directory(self, mode, *, create=False, held=None):
        try:
            directory = self._directory(create)
        except FileNotFoundError:
            if create:
                raise
            yield None
            return
        try:
            if held is None or not os.path.samestat(os.fstat(held), os.fstat(directory)):
                fcntl.flock(directory, mode)
            yield directory
        finally:
            os.close(directory)

    def _read(self, directory):
        if directory is None:
            return ''
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

    def load(self):
        with self._locked_directory(fcntl.LOCK_SH) as directory:
            return self._read(directory)

    def save(self, key):
        key = self._token(key)
        if not key:
            self.clear()
            return
        with self._locked_directory(fcntl.LOCK_EX, create=True) as directory:
            self._write(directory, key, replace=True)

    def _write(self, directory, key, *, replace):
        temporary = f'.{self.path.name}-' + secrets.token_hex(16)
        try:
            os.close(self._open_key(directory))
        except FileNotFoundError:
            pass
        else:
            if not replace:
                raise FileExistsError(self.path.name)
        fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                     0o600, dir_fd=directory)
        try:
            with os.fdopen(fd, 'wb') as target:
                os.fchmod(target.fileno(), 0o600)
                target.write((key + '\n').encode('ascii'))
                target.flush()
                os.fsync(target.fileno())
            os.replace(temporary, self.path.name, src_dir_fd=directory, dst_dir_fd=directory)
        except BaseException:
            try:
                os.unlink(temporary, dir_fd=directory)
            except FileNotFoundError:
                pass
            raise
        os.fsync(directory)

    def clear(self):
        with self._locked_directory(fcntl.LOCK_EX) as directory:
            if directory is None:
                return
            try:
                os.close(self._open_key(directory))
            except FileNotFoundError:
                return
            self._remove(directory)

    def _remove(self, directory):
        os.unlink(self.path.name, dir_fd=directory)
        os.fsync(directory)


class _CheckoutApiKeyStore(ApiKeyStore):
    _unsafe_directory_mode = 0o002

    def __init__(self, directory, filename):
        self._key_name = filename
        super().__init__(directory)


class _CloudApiKeyStore(ApiKeyStore):
    """Use a private key directory within the user-owned checkout."""

    _unsafe_parent_mode = 0o002

    def __init__(self, repository_directory=None):
        checkout = Path(repository_directory) if repository_directory is not None else Path(__file__).resolve().parents[1]
        super().__init__(checkout / KEY_DIRECTORY)
        self._legacy = (_CheckoutApiKeyStore(checkout, self._key_name),)

    def load(self):
        key = super().load()
        for store in self._legacy:
            # Clear removes each source before the destination. Hold that source
            # through publication and removal so a cleared key cannot be revived.
            with ExitStack() as locks:
                try:
                    source = locks.enter_context(store._locked_directory(fcntl.LOCK_EX))
                    legacy_key = store._read(source)
                except (OSError, ValueError):
                    if key:
                        continue
                    raise
                if not legacy_key:
                    continue
                target = locks.enter_context(self._locked_directory(
                    fcntl.LOCK_EX, create=True, held=source))
                try:
                    self._write(target, legacy_key, replace=False)
                except FileExistsError:
                    pass
                key = self._read(target)
                if key == legacy_key:
                    store._remove(source)
        return key

    def clear(self):
        # Explicit clearing removes sources of automatic migration first.
        for store in self._legacy:
            store.clear()
        super().clear()


class LinodeRepositoryApiKeyStore(_CloudApiKeyStore):
    _key_name = 'linode-api-key'


class RepositoryApiKeyStore(_CloudApiKeyStore):
    """Migrate RunPod's checkout, lab and private-state files in that order."""

    _key_name = 'runpod-api-key'

    def __init__(self, legacy_directory, *, repository_directory=None):
        super().__init__(repository_directory)
        checkout = self.path.parent.parent
        self._legacy += (_CheckoutApiKeyStore(checkout / '_zLab', self._key_name),
                         ApiKeyStore(legacy_directory))
