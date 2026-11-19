"""Verify an owned RunPod's direct SSH endpoint against its expected host key.

New lab pods receive a locally generated key through the authenticated create
request. Older pods use their authenticated container log or a supplied pin.
RunPod's official container-template/start.sh lists each /etc/ssh/*.pub with
ssh-keygen -lf after starting sshd.
"""
from __future__ import annotations

import base64
import hashlib
import os
from pathlib import Path
import re
import secrets
import stat
import subprocess

from deploy import _bounded_process, _host

_KEY_LABEL = 'Key: /etc/ssh/ssh_host_ed25519_key.pub'
_DIGEST = r'SHA256:[A-Za-z0-9+/]{43}'
_KEY_INFO = re.compile(r'256 (' + _DIGEST + r') [ -~]+ \(ED25519\)')
_END = ('Exporting environment variables...', 'Start script(s) finished, Pod is ready to use.')
# RFC 4253 strings: algorithm name and the 32 bytes of the Ed25519 public key.
_KEY_PREFIX = b'\x00\x00\x00\x0bssh-ed25519\x00\x00\x00\x20'


def startup_fingerprint(lines):
    """Extract a named host-key fingerprint from the official image's startup block."""
    listing = False
    expecting_info = False
    fingerprint = None
    for index, line in enumerate(lines):
        if index >= 1024 or not isinstance(line, str) or len(line) > 8192:
            raise ValueError('Container startup log exceeded the verification limit')
        line = line.rstrip('\r\n')
        if expecting_info:
            match = _KEY_INFO.fullmatch(line)
            if not match:
                raise ValueError('Invalid Ed25519 host key entry in the pod startup log')
            if fingerprint is not None and fingerprint != match[1]:
                raise ValueError('The pod startup log contains conflicting SSH fingerprints')
            fingerprint = match[1]
            expecting_info = False
        elif line == 'SSH host keys:':
            listing = True
        elif listing and line == _KEY_LABEL:
            expecting_info = True
        elif listing and line in _END:
            break
    else:
        raise ValueError('The verified SSH host key listing is incomplete in the container startup log')
    if fingerprint is None:
        raise ValueError('The container startup log did not identify an Ed25519 SSH host key')
    return fingerprint


def _endpoint(host, port):
    host = _host(host)
    if type(port) is not int or not 1 <= port <= 65535:
        raise ValueError('Invalid direct SSH port')
    return (host if port == 22 else f'[{host}]:{port}').encode('ascii')


def _public_key(line, endpoint):
    parts = line.split()
    if len(parts) != 3 or parts[0].lower() != endpoint.lower() or parts[1] != b'ssh-ed25519':
        raise ValueError('Unexpected host or key type in the SSH host-key entry')
    try:
        key = base64.b64decode(parts[2], validate=True)
    except ValueError:
        raise ValueError('The SSH host key is malformed') from None
    if len(key) != len(_KEY_PREFIX) + 32 or not key.startswith(_KEY_PREFIX):
        raise ValueError('The SSH host key is malformed')
    return key


def _entry(endpoint, key):
    return b' '.join((endpoint, b'ssh-ed25519', base64.b64encode(key))) + b'\n'


def _checked_entry(host, port, fingerprint):
    endpoint = _endpoint(host, port)
    if not isinstance(fingerprint, str) or not re.fullmatch(_DIGEST, fingerprint):
        raise ValueError('Invalid expected SHA256 SSH fingerprint')
    # Only the one endpoint and the one key type are queried. The subprocess
    # helper bounds retained output and wall time; never relay its diagnostics.
    try:
        rc, output = _bounded_process(
            ['ssh-keyscan', '-T', '5', '-p', str(port), '-t', 'ed25519', host], None, 8)
    except (subprocess.TimeoutExpired, OSError):
        raise ValueError('Could not run ssh-keyscan against the selected SSH endpoint') from None
    if rc or len(output) >= 4096:
        raise ValueError('Could not read a bounded SSH host key from the selected endpoint')
    entries = {line for line in output.splitlines() if line and not line.startswith(b'#')}
    if len(entries) != 1:
        raise ValueError('The SSH endpoint did not return one distinct Ed25519 key')
    key = _public_key(entries.pop(), endpoint)
    actual = base64.b64encode(hashlib.sha256(key).digest()).rstrip(b'=').decode('ascii')
    if 'SHA256:' + actual != fingerprint:
        raise ValueError('SSH host key mismatch with the expected key for this pod')
    return _entry(endpoint, key)


def _private_directory(path):
    """Pin the existing private state directory, rejecting symlink path components."""
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
    fd = os.open(path.anchor, flags)
    try:
        for part in path.parts[1:]:
            if part == '..':
                raise ValueError('Invalid lab state directory')
            next_fd = os.open(part, flags, dir_fd=fd)
            os.close(fd)
            fd = next_fd
        info = os.fstat(fd)
        if info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise ValueError('The lab state directory must be private and owned by this user')
        return fd
    except BaseException:
        os.close(fd)
        raise


def _pin_name(pod_id):
    if not isinstance(pod_id, str) or not pod_id or len(pod_id) > 256:
        raise ValueError('Invalid pod ID for SSH host verification')
    return 'known_hosts-' + hashlib.sha256(pod_id.encode('utf-8')).hexdigest()


def _read_pin(fd, name):
    existing = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=fd)
    try:
        info = os.fstat(existing)
        if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid()
                or info.st_mode & 0o077 or not 0 < info.st_size <= 512):
            raise ValueError('Unsafe SSH host-key file for this pod')
        return os.read(existing, 513)
    finally:
        os.close(existing)


def existing_host_key(pod_id, host, port, state_dir):
    """Use a previously verified pin only for the same pod and direct endpoint."""
    name = _pin_name(pod_id)
    endpoint = _endpoint(host, port)
    directory = Path(state_dir).absolute()
    fd = _private_directory(directory)
    try:
        try:
            pin = _read_pin(fd, name)
        except FileNotFoundError:
            return None
        if pin != _entry(endpoint, _public_key(pin, endpoint)):
            raise ValueError('Invalid SSH host-key file for this pod')
    finally:
        os.close(fd)
    return directory / name


def forget_host_key(pod_id, state_dir):
    """Remove the one managed pin when the user forgets its verified closed pod."""
    name = _pin_name(pod_id)
    fd = _private_directory(Path(state_dir).absolute())
    try:
        try:
            _read_pin(fd, name)  # Reject unexpected file types, owners or permissions.
            os.unlink(name, dir_fd=fd)
        except FileNotFoundError:
            return
        os.fsync(fd)
    finally:
        os.close(fd)


def verify_host_key(pod_id, host, port, fingerprint, state_dir):
    """Compare the offered key and publish an immutable per-pod known_hosts file."""
    name = _pin_name(pod_id)
    entry = _checked_entry(host, port, fingerprint)
    directory = Path(state_dir).absolute()
    fd = _private_directory(directory)
    temp = '.ssh-' + secrets.token_hex(16)
    try:
        handle = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600, dir_fd=fd)
        try:
            with os.fdopen(handle, 'wb') as out:
                out.write(entry)
                out.flush()
                os.fsync(out.fileno())
            try:
                os.link(temp, name, src_dir_fd=fd, dst_dir_fd=fd, follow_symlinks=False)
            except FileExistsError:
                if _read_pin(fd, name) != entry:
                    raise ValueError('A different SSH host key already exists for this pod')
        finally:
            os.unlink(temp, dir_fd=fd)
        os.fsync(fd)
    finally:
        os.close(fd)
    return directory / name
