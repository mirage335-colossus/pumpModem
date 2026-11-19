"""Private, local persistence for the standalone lab's RunPod API key."""
from __future__ import annotations

import os
from pathlib import Path
import secrets
import stat


MAX_KEY_BYTES = 4096


class ApiKeyStore:
    def __init__(self, directory):
        self.path = Path(directory).absolute() / 'api-key'

    def _directory(self, create=False):
        """Open the private directory without following a symlink component."""
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
            if info.st_uid != os.getuid() or info.st_mode & 0o077:
                raise ValueError('The API-key directory must be private and owned by this user')
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
        directory = self._directory(create=True)
        temporary = '.api-key-' + secrets.token_hex(16)
        try:
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
                os.replace(temporary, self.path.name, src_dir_fd=directory, dst_dir_fd=directory)
                os.fsync(directory)
            finally:
                try:
                    os.unlink(temporary, dir_fd=directory)
                except FileNotFoundError:
                    pass
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
