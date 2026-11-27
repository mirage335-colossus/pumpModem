"""Run a deliberately selected Linux portable worker with its matching hosted UI."""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import io
import json
import math
import os
from pathlib import Path
import re
import shlex
import stat
import time
from types import MappingProxyType
from typing import Mapping
import zipfile


WORKER = 'bin/datapump-worker'
HOSTED = 'share/datapump/web/hosted/'
DOC = 'share/doc/datapump/'
REMOTE_ROOT = '/workspace/pump-lab-portable'
LIMIT = 256 * 1024 * 1024   # Also used by checkout deployments.
PROFILE = {'schema_version': 1, 'target': 'datapump-worker', 'protocol': 'DPW1',
           'audio': 'browser', 'files': 'host', 'transport': 'inherited-anonymous-pipes',
           'socket_policy': 'forbidden', 'simulation_startup_version': 1}
RELAY = '_zLab/worker_relay.py'
_DIGEST = re.compile(r'[0-9a-f]{64}')


def _safe_name(name):
    return (name.isprintable() and '\\' not in name and
            all(part and not part.startswith('.') for part in name.split('/')))


def _read(root_fd, name, limit, *, executable=False):
    # Open each component relative to the verified directory, including the
    # final file. Never follow a package entry outside the selected tree.
    parts = name.split('/')
    fd = os.dup(root_fd)
    try:
        for part in parts[:-1]:
            child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
            os.close(fd)
            fd = child
        with os.fdopen(os.open(parts[-1], os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=fd), 'rb') as f:
            info = os.fstat(f.fileno())
            if (not stat.S_ISREG(info.st_mode) or info.st_size > limit or
                    (executable and not info.st_mode & 0o111)):
                raise ValueError(f'Invalid portable package file: {name}')
            data = f.read(info.st_size + 1)
            if len(data) != info.st_size:
                raise ValueError(f'Portable package changed during preparation: {name}')
            return data
    finally:
        os.close(fd)


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def _inventory(data):
    entries = {}
    for line in data.decode('utf-8').splitlines():
        digest, sep, name = line.partition('  ')
        if not sep or not _DIGEST.fullmatch(digest) or not _safe_name(name) or name in entries:
            raise ValueError('Invalid or duplicate entry in portable manifest.sha256')
        entries[name] = digest
    return entries


def _runtime_file(name):
    # Carry the producer's shared runtimes, including libraries loaded with
    # dlopen, and the associated license material. GUI/CLI binaries, browser
    # apps, experiment data and arbitrary package files need no upload.
    if name.startswith('lib/'):
        return re.fullmatch(r'lib/[^/]+\.so(?:\.[A-Za-z0-9_.+-]+)?', name) is not None
    if name.startswith(DOC + 'runtime-notices/'):
        return True
    if name.startswith(DOC):
        suffix = name.removeprefix(DOC)
        if '/' not in suffix and ('LICENSE' in suffix or suffix in ('build-info.txt', 'runtime-info.txt')):
            return True
        return suffix.startswith('third_party/') and any(
            word in suffix.rsplit('/', 1)[-1].upper() for word in ('COPYING', 'LICENSE', 'NOTICE'))
    return False


@dataclass(frozen=True)
class PortablePackage:
    package_sha256: str
    worker_sha256: str
    web_assets: Mapping[str, bytes]
    payload: bytes
    payload_sha256: str
    inventory_sha256: str
    worker_path: Path

    @property
    def remote_worker(self):
        return f'{REMOTE_ROOT}/{self.payload_sha256}/{WORKER}'


def load_package(extracted_root):
    """Capture a checked snapshot of an explicitly selected extracted distribution."""
    from browser_bridge import WEB_ASSETS, MAX_WEB_ASSET
    root = Path(extracted_root).expanduser().absolute()
    root_fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        manifest = _read(root_fd, 'manifest.sha256', 8 * 1024 * 1024)
        recorded = _inventory(manifest)
        size = 0

        def checked(name, *, executable=False, limit=LIMIT):
            nonlocal size
            if name not in recorded:
                raise ValueError(f'Portable package manifest lacks {name}')
            data = _read(root_fd, name, min(limit, LIMIT - size), executable=executable)
            size += len(data)
            if _sha(data) != recorded[name]:
                raise ValueError(f'Portable package checksum mismatch: {name}')
            return data

        description = json.loads(checked(HOSTED + 'web-manifest.json'))
        if isinstance(description, dict) and description.get('target') == PROFILE['target'] and (
                type(description.get('simulation_startup_version')) is not int or
                description['simulation_startup_version'] != PROFILE['simulation_startup_version']):
            raise ValueError('This portable worker lacks the supported simulation startup contract. '
                             'Select a current Linux portable package or build from the checkout.')
        if not isinstance(description, dict) or any(description.get(k) != v for k, v in PROFILE.items()):
            raise ValueError('Portable package has an unsupported hosted worker manifest')
        assets = {name: checked(HOSTED + name, limit=MAX_WEB_ASSET) for name in WEB_ASSETS}
        if not all(assets.values()):
            raise ValueError('Portable package contains an empty hosted asset')

        inputs = {WORKER: checked(WORKER, executable=True)}
        if not inputs[WORKER].startswith(b'\x7fELF'):
            raise ValueError('Portable simulation needs the native Linux ELF worker')
        for name in sorted(recorded):
            if _runtime_file(name):
                inputs[name] = checked(name)
        lines = inputs.get(DOC + 'build-info.txt', b'').decode('utf-8').splitlines()
        if not (any(line.startswith('System: Linux ') for line in lines) and 'Portable: ON' in lines
                and 'Web worker: ON (inherited-pipes; no sockets)' in lines):
            raise ValueError('Select an extracted Linux portable distribution that contains the web worker')
        if DOC + 'LICENSE' not in inputs or DOC + 'runtime-info.txt' not in inputs:
            raise ValueError('Portable worker is missing its runtime provenance or license')

        # The checked-out transport only handles DPW1 pipes. It participates
        # in the deployment identity alongside the package bytes it launches.
        inputs[RELAY] = Path(__file__).with_name('worker_relay.py').read_bytes()
        package_digest = _sha(manifest)
        selection = json.dumps({'package': package_digest,
                                'files': {name: _sha(data) for name, data in inputs.items()}},
                               sort_keys=True, separators=(',', ':')).encode()
        inputs['selection.json'] = selection
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, 'w') as archive:
            for name, data in sorted(inputs.items()):
                # Fixed timestamps keep unchanged packages and relays reusable.
                archive.writestr(zipfile.ZipInfo(name), data, zipfile.ZIP_DEFLATED, 4)
        payload = stream.getvalue()
        if len(payload) > LIMIT:
            raise ValueError('Portable upload exceeds 256 MiB. Use the checkout build on the compute host.')
        return PortablePackage(package_digest, recorded[WORKER], MappingProxyType(dict(assets)),
                               payload, _sha(payload), _sha(selection), root / WORKER)
    finally:
        os.close(root_fd)


# Use the authenticated source deployment transport. Only locally computed
# SHA-256 values and a bounded integer enter its shell command. Copies become
# visible after complete verification and the worker's own self-check.
_INSTALL = r'''import fcntl, hashlib, io, json, os, re, shutil, stat, subprocess, sys, tempfile, zipfile
from pathlib import Path

root = Path('/workspace/pump-lab-portable')
action, digest, inventory, size = sys.argv[1:]
size = int(size)
if (action not in ('check', 'install') or not all(re.fullmatch('[0-9a-f]{64}', d) for d in (digest, inventory))
        or not 0 < size <= 256 * 1024 * 1024):
    raise SystemExit('Invalid portable deployment request')
os.umask(0o077)
root.parent.mkdir(exist_ok=True)
root.mkdir(exist_ok=True, mode=0o700)
root_fd = os.open(root, os.O_RDONLY | os.O_NOFOLLOW | os.O_DIRECTORY)
root_stat = os.fstat(root_fd)
if root_stat.st_uid != os.getuid() or root_stat.st_mode & 0o077:
    raise SystemExit('Portable deployment directory must be private and owned by the SSH user')
os.fchdir(root_fd)
target = Path(digest)
lock = os.open('.install.lock', os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
fcntl.flock(lock, fcntl.LOCK_EX)
def checksum(path):
    result = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024*1024), b''):
            result.update(block)
    return result.hexdigest()
def check(folder):
    folder_info = folder.lstat()
    if (not stat.S_ISDIR(folder_info.st_mode) or folder_info.st_uid != os.getuid()
            or folder_info.st_mode & 0o077):
        raise SystemExit('Unsafe portable revision')
    files = set()
    for p in folder.rglob('*'):
        info = p.lstat()
        if info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise SystemExit('Unsafe portable revision entry')
        if stat.S_ISREG(info.st_mode):
            files.add(p.relative_to(folder).as_posix())
        elif not stat.S_ISDIR(info.st_mode):
            raise SystemExit('Unexpected portable revision entry')
    record = folder/'selection.json'
    if 'selection.json' not in files or record.stat().st_size > 8388608 or checksum(record) != inventory:
        raise SystemExit('Portable deployment inventory mismatch')
    selected = json.loads(record.read_bytes())
    if files != set(selected['files']) | {'selection.json'} or any(checksum(folder/name) != expected
            for name, expected in selected['files'].items()):
        raise SystemExit('Portable deployment content mismatch')
    subprocess.run([str((folder/'bin/datapump-worker').resolve()), '--self-check'], check=True, timeout=30)
    print('PUMP_LAB_PROFILE=simulation')
    print('PUMP_LAB_MODE=portable')
    print('PUMP_LAB_PACKAGE_SHA256=' + selected['package'])
    print('PUMP_LAB_WORKER_SHA256=' + selected['files']['bin/datapump-worker'])
if target.exists() or target.is_symlink():
    if action == 'install':
        # Drain a redundant concurrent upload before closing the remote pipe.
        while sys.stdin.buffer.read(1024 * 1024):
            pass
    check(target)
    print('PUMP_LAB_PORTABLE=ready')
elif action == 'check':
    print('PUMP_LAB_PORTABLE=missing')
else:
    data = sys.stdin.buffer.read(size + 1)
    if len(data) != size or hashlib.sha256(data).hexdigest() != digest:
        raise SystemExit('Portable upload checksum mismatch')
    staging = Path(tempfile.mkdtemp(prefix='.install-', dir='.'))
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            total = 0
            for entry in archive.infolist():
                parts = entry.filename.split('/')
                if (not entry.filename.isprintable() or '\\' in entry.filename
                        or any(not part or part.startswith('.') for part in parts)):
                    raise SystemExit('Unsafe portable deployment path')
                total += entry.file_size
                if total > 257*1024*1024:
                    raise SystemExit('Portable deployment exceeded its runtime bound')
                path = staging.joinpath(*parts)
                path.parent.mkdir(parents=True, exist_ok=True)
                with path.open('xb') as out, archive.open(entry) as inp:
                    shutil.copyfileobj(inp, out)
                if entry.filename == 'bin/datapump-worker':
                    path.chmod(0o700)
        check(staging)
        staging.rename(target)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    print('PUMP_LAB_PORTABLE=ready')
'''


def deploy_package(package, host, user, port, known_hosts, *, identity=None, timeout=900,
                   runner=None, on_output=None, on_progress=None, cancelled=None):
    from deploy import DeployError, _connection, _run
    host, user, options = _connection(host, user, port, known_hosts, identity)
    if (not all(_DIGEST.fullmatch(v) for v in (package.payload_sha256, package.inventory_sha256,
                                               package.package_sha256, package.worker_sha256))
            or not 0 < len(package.payload) <= LIMIT):
        raise ValueError('Invalid portable payload or identity')
    if not isinstance(timeout, (float, int)) or isinstance(timeout, bool) or not math.isfinite(timeout) or timeout <= 0:
        raise ValueError('Timeout must be finite and positive')
    deadline = time.monotonic() + timeout
    ssh = ['ssh', '-F', '/dev/null', '-T', '-p', str(port), '-l', user, *options, host]
    for action in ('check', 'install'):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise DeployError('Portable deployment deadline reached.', uncertain=True)
        if on_progress:
            on_progress('Checking the selected portable worker…' if action == 'check' else
                        f'Uploading the selected portable worker ({len(package.payload)/1024/1024:.1f} MiB)…')
        command = (f'python3 -c {shlex.quote(_INSTALL)} {action} {package.payload_sha256} '
                   f'{package.inventory_sha256} {len(package.payload)}')
        output = _run(ssh + [command], stage='Installing selected portable worker', timeout=remaining,
                      runner=runner, cancelled=cancelled, input_data=package.payload if action=='install' else None)
        lines = output.splitlines()
        if ('PUMP_LAB_PORTABLE=ready' in lines and f'PUMP_LAB_PACKAGE_SHA256={package.package_sha256}' in lines
                and f'PUMP_LAB_WORKER_SHA256={package.worker_sha256}' in lines):
            if on_output:
                on_output(output)
            return package.remote_worker
        if action != 'check' or 'PUMP_LAB_PORTABLE=missing' not in lines:
            raise DeployError('Remote host did not confirm the selected portable worker identity.')
