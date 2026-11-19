"""Create per-pod SSH credentials before RunPod starts its official image.

The public host-key fingerprint stays in the ownership record. Its private key
is supplied only in the HTTPS create payload, and is removed from the container
environment before the image's startup script exports that environment.
"""
from __future__ import annotations

import base64
import hashlib
import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile

from host_identity import _private_directory, _public_key


STARTUP = r'''set +x
set -eu
umask 077
test -x /start.sh || { echo 'RunPod Lab requires an image with /start.sh' >&2; exit 1; }
mkdir -p /etc/ssh
printf '%s' "$PUMP_LAB_HOST_KEY_B64" | base64 --decode > /etc/ssh/ssh_host_ed25519_key
unset PUMP_LAB_HOST_KEY_B64
chmod 600 /etc/ssh/ssh_host_ed25519_key
ssh-keygen -y -f /etc/ssh/ssh_host_ed25519_key > /etc/ssh/ssh_host_ed25519_key.pub
chmod 644 /etc/ssh/ssh_host_ed25519_key.pub
exec /start.sh
'''


def _identity_name(name):
    if not isinstance(name, str) or not re.fullmatch(r'pump-lab-[0-9a-f]{24}', name):
        raise ValueError('Invalid owned pod name for SSH setup')
    return 'identity-' + hashlib.sha256(name.encode('ascii')).hexdigest()


def _keypair(path):
    try:
        result = subprocess.run(['ssh-keygen', '-q', '-t', 'ed25519', '-N', '',
                                 '-C', 'pump-lab', '-f', str(path)],
                                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL, timeout=15, check=False)
    except (OSError, subprocess.TimeoutExpired):
        raise ValueError('Could not generate the lab SSH key. Install OpenSSH client tools.') from None
    if result.returncode:
        raise ValueError('Could not generate the lab SSH key')
    public = path.with_suffix('.pub').read_bytes().split()
    if len(public) != 3:
        raise ValueError('Invalid generated SSH public key')
    wire = _public_key(b'local ' + b' '.join(public[:2]), b'local')
    return b' '.join(public[:2]).decode('ascii'), wire


def prepare_bootstrap(state_dir, name, image):
    """Return ownership fields and create fields, without retaining a host secret.

    Persist the returned ownership fields before sending the create request.
    On a definite rejected request, call forget_bootstrap. Preserve the local
    identity for uncertain requests until their provider ownership is resolved.
    """
    if not isinstance(image, str) or not image.strip():
        raise ValueError('Enter a container image for SSH setup')
    directory = Path(state_dir).absolute()
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    identity_name = _identity_name(name)
    fd = _private_directory(directory)
    try:
        with tempfile.TemporaryDirectory(prefix='.bootstrap-', dir=directory) as temporary:
            work = Path(temporary)
            client_public, _ = _keypair(work / 'client')
            _, host_wire = _keypair(work / 'host')
            private_host = (work / 'host').read_bytes()
            if not 0 < len(private_host) < 8192:
                raise ValueError('Invalid generated SSH host key')
            fingerprint = 'SHA256:' + base64.b64encode(hashlib.sha256(host_wire).digest()).decode('ascii').rstrip('=')
            # An existing identity means this name already belongs to an earlier
            # request. Never rotate it during an uncertain-create retry.
            os.link(work / 'client', identity_name, dst_dir_fd=fd, follow_symlinks=False)
            os.fsync(fd)
            record = {'ssh_fingerprint': fingerprint, 'ssh_identity': str(directory / identity_name)}
            payload = {'entrypoint': ['/bin/bash', '-c'], 'cmd': [STARTUP],
                       'env': {'PUBLIC_KEY': client_public,
                               'PUMP_LAB_HOST_KEY_B64': base64.b64encode(private_host).decode('ascii')}}
            return record, payload
    finally:
        os.close(fd)


def forget_bootstrap(record, state_dir):
    """Remove only the generated client identity for the exact owned pod name."""
    if not record.get('ssh_identity'):
        return
    directory = Path(state_dir).absolute()
    name = _identity_name(record.get('name'))
    if record['ssh_identity'] != str(directory / name):
        raise ValueError('The saved SSH identity does not match this owned pod')
    fd = _private_directory(directory)
    try:
        try:
            info = os.stat(name, dir_fd=fd, follow_symlinks=False)
        except FileNotFoundError:
            return
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise ValueError('Unsafe saved SSH identity file')
        os.unlink(name, dir_fd=fd)
        os.fsync(fd)
    finally:
        os.close(fd)
