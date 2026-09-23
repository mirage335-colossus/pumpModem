#!/usr/bin/env python3
"""Verify and atomically refresh a DataPump Gentoo overlay from release assets.

This installed client uses only Python's standard library and gpgv. Its executable
and Portage adapter are never updated automatically by a repository sync.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import urllib.parse
import urllib.request

FINGERPRINT = '8C3DD4A727C83B93374C993B1F94BC4CEC2DF307'
CHANNEL = 'datapump-gentoo-channel.json'
OVERLAY = 'datapump-gentoo-overlay.tar.gz'
HELPER = 'datapump-gentoo-sync.py'
ADAPTER_NAME = 'datapump-gentoo-portage-sync.py'
STATE = '.datapump-release-state.json'
DEFAULT_LOCATION = '/var/db/repos/datapump-bin'
INSTALLED_HELPER = '/usr/local/libexec/datapump-gentoo-sync.py'
CONFIG_PATH = '/etc/portage/datapump-release.json'
MAX_MANIFEST = 64 * 1024
MAX_ARCHIVE = 16 * 1024 * 1024
MAX_EXPANDED = 32 * 1024 * 1024
ASSETS = {OVERLAY, HELPER, ADAPTER_NAME}

# Installed as datapump_release/__init__.py in Portage's discovered module
# directory. Both this adapter asset and the client are authenticated before
# installation. The fixed executable path cannot come from remote metadata.
ADAPTER = '''# DataPump verified GitHub Release sync adapter.
import json
import subprocess
from portage.sync.config_checks import CheckSyncConfig
from portage.sync.syncbase import SyncBase

module_spec = {
    'name': 'datapump_release',
    'description': 'Verified DataPump release overlay',
    'provides': {'datapump-release-module': {
        'name': 'datapump-release', 'sourcefile': '__init__',
        'class': 'DataPumpRelease', 'description': 'Verified release assets',
        'functions': ['sync'], 'func_desc': {'sync': 'Verify and refresh the release overlay'},
        'validate_config': CheckSyncConfig,
        'module_specific_options': ('sync-datapump-config',),
    }},
}

class DataPumpRelease(SyncBase):
    def __init__(self):
        super().__init__(None, None)

    @staticmethod
    def name():
        return 'DataPumpRelease'

    def sync(self, **kwargs):
        self._kwargs(kwargs)
        config = self.repo.module_specific_options.get('sync-datapump-config')
        if not config:
            print('DataPump: sync-datapump-config is required')
            return (1, False)
        result = subprocess.run(
            ['/usr/local/libexec/datapump-gentoo-sync.py', 'sync', '--config', config],
            text=True, capture_output=True)
        if result.stderr:
            print(result.stderr, end='')
        if result.returncode:
            return (result.returncode, False)
        try:
            update = json.loads(result.stdout)
            print('DataPump: ' + update['tag'])
            return (0, update['changed'])
        except (ValueError, KeyError):
            print('DataPump: invalid updater result')
            return (1, False)
'''


def canonical(value):
    return (json.dumps(value, sort_keys=True, indent=2) + '\n').encode()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def strict_json(data):
    def pairs(values):
        result = {}
        for key, value in values:
            if key in result:
                raise ValueError('Duplicate JSON key')
            result[key] = value
        return result
    return json.loads(data, object_pairs_hook=pairs)


def repository_name(value):
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', value) or any(
            component in ('.', '..') for component in value.split('/')):
        raise ValueError('Invalid GitHub repository')
    return value


def safe_tag(value):
    if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,100}', value) or '..' in value:
        raise ValueError('Invalid release tag')
    return value


def fingerprint(value):
    value = str(value).replace(' ', '').upper()
    if not re.fullmatch(r'[A-F0-9]{40}', value):
        raise ValueError('A complete trusted fingerprint is required')
    return value


def verify_signature(data, signature, keyring, expected_fingerprint):
    expected = fingerprint(expected_fingerprint)
    keyring = Path(keyring).resolve(strict=True)
    if not keyring.is_file():
        raise ValueError('Trusted keyring is not a regular file')
    with tempfile.TemporaryDirectory(prefix='datapump-gentoo-gpg-') as temporary:
        directory = Path(temporary)
        (directory / 'manifest').write_bytes(data)
        (directory / 'signature').write_bytes(signature)
        result = subprocess.run(['gpgv', '--homedir', str(directory), '--status-fd=1',
                                 '--keyring', str(keyring), str(directory / 'signature'),
                                 str(directory / 'manifest')], text=True, capture_output=True)
        valid = []
        for line in result.stdout.splitlines():
            fields = line.split()
            if fields[:2] == ['[GNUPG:]', 'VALIDSIG']:
                valid.append(expected in (fields[2], fields[-1]))
        if result.returncode or valid != [True]:
            raise ValueError('Channel signature does not match the trusted key')


def validate_manifest(data, repository, expected_fingerprint, tag=None):
    if len(data) > MAX_MANIFEST:
        raise ValueError('Channel manifest is too large')
    result = strict_json(data)
    fields = {'schema', 'kind', 'repository', 'tag', 'created_at', 'project_version',
              'version', 'run_id', 'run_attempt', 'experiment', 'signing_fingerprint',
              'metadata_sha256', 'assets'}
    if not isinstance(result, dict) or set(result) != fields:
        raise ValueError('Unexpected channel manifest structure')
    if result['schema'] != 1 or result['kind'] != 'datapump-gentoo-channel':
        raise ValueError('Unsupported Gentoo channel schema')
    if result['repository'] != repository_name(repository):
        raise ValueError('Channel repository mismatch')
    if result['signing_fingerprint'] != fingerprint(expected_fingerprint):
        raise ValueError('Channel signing fingerprint mismatch')
    safe_tag(result['tag'])
    if tag is not None and result['tag'] != safe_tag(tag):
        raise ValueError('Pinned release tag mismatch')
    if type(result['experiment']) is not bool or (tag is None and result['experiment']):
        raise ValueError('Latest must contain a regular release, not an experiment')
    if not isinstance(result['version'], str) or not re.fullmatch(r'[A-Za-z0-9_.-]{1,64}', result['version']):
        raise ValueError('Invalid release version label')
    if not re.fullmatch(r'[0-9a-f]{64}', result['metadata_sha256']):
        raise ValueError('Invalid release metadata hash')
    release_order(result)
    if not isinstance(result['assets'], dict) or set(result['assets']) != ASSETS:
        raise ValueError('Unexpected signed Gentoo asset inventory')
    for name, row in result['assets'].items():
        if not isinstance(row, dict) or set(row) != {'size', 'sha256'}:
            raise ValueError('Invalid signed asset row')
        if type(row['size']) is not int or not 0 < row['size'] <= MAX_ARCHIVE:
            raise ValueError('Invalid signed asset size')
        if not isinstance(row['sha256'], str) or not re.fullmatch(r'[0-9a-f]{64}', row['sha256']):
            raise ValueError('Invalid signed asset hash')
    return result


def release_order(manifest):
    version = manifest['project_version']
    if not isinstance(version, str) or not re.fullmatch(r'\d+(?:\.\d+){1,3}', version):
        raise ValueError('Invalid project version')
    stamp = manifest['created_at']
    if not isinstance(stamp, str) or not re.fullmatch(r'\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z', stamp):
        raise ValueError('Release timestamp must be UTC')
    time = datetime.fromisoformat(stamp.replace('Z', '+00:00'))
    values = []
    for key in ('run_id', 'run_attempt'):
        value = manifest[key]
        if not isinstance(value, str) or not re.fullmatch(r'[1-9][0-9]{0,17}', value):
            raise ValueError('Invalid release run identity')
        values.append(int(value))
    version_parts = tuple(int(part) for part in version.split('.'))
    return (version_parts + (0,) * (4 - len(version_parts)), time, *values)


def verify_asset(data, row):
    if len(data) != row['size'] or digest(data) != row['sha256']:
        raise ValueError('Signed asset size or hash mismatch')


def checked_url(url, allow_test_http=False):
    parsed = urllib.parse.urlsplit(url)
    test = allow_test_http and parsed.scheme == 'http' and parsed.hostname in ('127.0.0.1', '::1', 'localhost')
    if (parsed.scheme != 'https' and not test) or parsed.username or parsed.password or parsed.fragment:
        raise ValueError('Release downloads require HTTPS')
    return url


def fetch(url, limit, allow_test_http=False):
    class Redirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, req, fp, code, msg, headers, newurl):
            checked_url(newurl, allow_test_http)
            return super().redirect_request(req, fp, code, msg, headers, newurl)
    checked_url(url, allow_test_http)
    opener = urllib.request.build_opener(Redirect())
    request = urllib.request.Request(url, headers={'User-Agent': 'DataPump-Gentoo-Sync/1'})
    with opener.open(request, timeout=30) as response:
        checked_url(response.url, allow_test_http)
        data = response.read(limit + 1)
    if len(data) > limit:
        raise ValueError('Release download exceeds its size limit')
    return data


def release_base(repository, tag=None, test_base_url=None):
    repository_name(repository)
    if tag is not None:
        safe_tag(tag)
    base = 'https://github.com'
    if test_base_url is not None:
        parsed = urllib.parse.urlsplit(test_base_url)
        if parsed.scheme != 'http' or parsed.hostname not in ('127.0.0.1', '::1', 'localhost') or parsed.path not in ('', '/'):
            raise ValueError('Test server must be a loopback HTTP origin')
        checked_url(test_base_url, True)
        base = test_base_url.rstrip('/')
    suffix = f'download/{tag}' if tag else 'latest/download'
    return f'{base}/{repository}/releases/{suffix}/'


def extract_overlay(data, destination):
    import io
    destination = Path(destination)
    destination.mkdir(mode=0o755)
    seen, files, total = set(), {}, 0
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
        for member in archive:
            name = member.name.rstrip('/')
            parts = name.split('/')
            if (name in seen or not parts or parts[0] != 'datapump-gentoo-overlay'
                    or any(part in ('', '.', '..') for part in parts)
                    or any(not re.fullmatch(r'[A-Za-z0-9_.+\-]+', part) for part in parts)
                    or member.mode & 0o7000 or not (member.isfile() or member.isdir())):
                raise ValueError('Unsafe overlay archive member')
            seen.add(name)
            if len(seen) > 4096:
                raise ValueError('Overlay archive has too many members')
            relative = '/'.join(parts[1:])
            if relative == STATE or any(part.startswith('.datapump-') for part in parts[1:]):
                raise ValueError('Overlay archive uses reserved state paths')
            target = destination.joinpath(*parts[1:])
            if member.isdir():
                if member.mode != 0o755:
                    raise ValueError('Unsafe overlay directory mode')
                target.mkdir(parents=True, exist_ok=True)
                target.chmod(0o755)
                continue
            if not relative or member.mode not in (0o644, 0o755) or member.size < 0:
                raise ValueError('Unsafe overlay file mode')
            total += member.size
            if total > MAX_EXPANDED:
                raise ValueError('Expanded overlay exceeds size limit')
            content = archive.extractfile(member).read(member.size + 1)
            if len(content) != member.size:
                raise ValueError('Truncated overlay member')
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open('xb') as output:
                output.write(content)
            target.chmod(member.mode)
            files[relative] = {'sha256': digest(content), 'mode': member.mode}
    if (destination / 'profiles/repo_name').read_bytes() != b'datapump-bin\n':
        raise ValueError('Unexpected Gentoo repository name')
    if not (destination / 'metadata/layout.conf').is_file():
        raise ValueError('Gentoo overlay metadata is missing')
    for backend in ('fltk', 'rev'):
        package = destination / f'media-radio/datapump-{backend}-bin'
        if len(list(package.glob('*.ebuild'))) != 1 or not (package / 'Manifest').is_file():
            raise ValueError('Gentoo overlay must contain both backend recipes')
    for directory in destination.rglob('*'):
        if directory.is_dir():
            directory.chmod(0o755)
    destination.chmod(0o755)
    return files


def verify_tree(location, files):
    found = {}
    for path in location.rglob('*'):
        relative = path.relative_to(location).as_posix()
        if path.is_symlink() or not (path.is_file() or path.is_dir()):
            raise ValueError('Installed overlay contains an unsafe node')
        if path.is_file() and relative != STATE:
            found[relative] = {'sha256': digest(path.read_bytes()), 'mode': stat.S_IMODE(path.stat().st_mode)}
    if found != files:
        raise ValueError('Installed overlay changed since verified publication')


def exchange_directories(first, second):
    import ctypes
    libc = ctypes.CDLL(None, use_errno=True)
    rename = getattr(libc, 'renameat2', None)
    if rename is None:
        raise ValueError('Atomic overlay exchange requires Linux renameat2')
    rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    rename.restype = ctypes.c_int
    if rename(-100, os.fsencode(first), -100, os.fsencode(second), 2):
        raise OSError(ctypes.get_errno(), 'Atomic overlay exchange failed')


def sync(repository, location, keyring, expected_fingerprint=FINGERPRINT, tag=None, test_base_url=None):
    import fcntl
    location = Path(location).absolute()
    if location == Path('/') or location.is_symlink() or location.resolve() != location:
        raise ValueError('Overlay location must be a real absolute directory path')
    location.parent.mkdir(parents=True, exist_ok=True)
    lock = location.parent / ('.' + location.name + '.lock')
    with os.fdopen(os.open(lock, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600), 'w') as locked:
        fcntl.flock(locked, fcntl.LOCK_EX)
        previous = None
        if location.exists():
            if not location.is_dir():
                raise ValueError('Overlay location is not a directory')
            if (location / STATE).is_symlink():
                raise ValueError('Unsafe overlay state')
            if (location / STATE).exists():
                previous = strict_json((location / STATE).read_bytes())
                validate_manifest(canonical(previous['manifest']), repository, expected_fingerprint,
                                  previous['manifest']['tag'])
                verify_tree(location, previous['files'])
            elif any(location.iterdir()):
                raise ValueError('Refusing to replace an unmanaged overlay directory')
        base = release_base(repository, tag, test_base_url)
        manifest_bytes = fetch(base + CHANNEL, MAX_MANIFEST, test_base_url is not None)
        # Pin the signature request to the untrusted tag hint; trust comes only
        # from signature validation below. A moving Latest URL cannot mix files.
        hint = strict_json(manifest_bytes)
        if not isinstance(hint, dict):
            raise ValueError('Unexpected channel manifest structure')
        hinted_tag = safe_tag(hint.get('tag'))
        immutable = release_base(repository, hinted_tag, test_base_url)
        signature = fetch(immutable + CHANNEL + '.asc', MAX_MANIFEST, test_base_url is not None)
        verify_signature(manifest_bytes, signature, keyring, expected_fingerprint)
        manifest = validate_manifest(manifest_bytes, repository, expected_fingerprint, tag)
        if previous:
            before, after = release_order(previous['manifest']), release_order(manifest)
            if after < before:
                raise ValueError('Refusing a release downgrade')
            if after == before:
                if manifest != previous['manifest']:
                    raise ValueError('Release identity changed without advancing its version')
                return {'changed': False, 'tag': manifest['tag'], 'location': str(location)}
        data = fetch(immutable + OVERLAY, manifest['assets'][OVERLAY]['size'], test_base_url is not None)
        verify_asset(data, manifest['assets'][OVERLAY])
        stage_parent = Path(tempfile.mkdtemp(prefix='.' + location.name + '.staging-', dir=location.parent))
        try:
            staged = stage_parent / 'overlay'
            files = extract_overlay(data, staged)
            (staged / STATE).write_bytes(canonical({'manifest': manifest, 'files': files}))
            (staged / STATE).chmod(0o644)
            if location.exists():
                exchange_directories(staged, location)
            else:
                os.replace(staged, location)
        finally:
            shutil.rmtree(stage_parent, ignore_errors=True)
        return {'changed': True, 'tag': manifest['tag'], 'location': str(location)}


def verify_local_assets(directory, keyring, expected_fingerprint, repository, tag=None):
    directory = Path(directory)
    data = (directory / CHANNEL).read_bytes()
    verify_signature(data, (directory / (CHANNEL + '.asc')).read_bytes(), keyring, expected_fingerprint)
    manifest = validate_manifest(data, repository, expected_fingerprint, tag)
    for name, row in manifest['assets'].items():
        path = directory / name
        if path.is_symlink() or not path.is_file():
            raise ValueError('Missing regular signed Gentoo asset')
        verify_asset(path.read_bytes(), row)
    with tempfile.TemporaryDirectory(prefix='datapump-gentoo-overlay-') as temporary:
        extract_overlay((directory / OVERLAY).read_bytes(), Path(temporary) / 'overlay')
    return manifest


def atomic_write(path, data, mode):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix='.' + path.name + '.', dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        try:
            stream.write(data)
            stream.flush()
            os.fchmod(stream.fileno(), mode)
            os.replace(temporary, path)
        finally:
            temporary.unlink(missing_ok=True)


def install(directory, keyring, expected_fingerprint, repository, tag=None, location=DEFAULT_LOCATION):
    if os.geteuid() != 0:
        raise ValueError('Installing the Portage adapter requires root')
    directory = Path(directory).resolve(strict=True)
    manifest = verify_local_assets(directory, keyring, expected_fingerprint, repository,
                                   tag or strict_json((directory / CHANNEL).read_bytes())['tag'])
    if (directory / HELPER).read_bytes() != Path(__file__).read_bytes():
        raise ValueError('The running installer is not the authenticated helper asset')
    if (directory / ADAPTER_NAME).read_bytes() != ADAPTER.encode():
        raise ValueError('The adapter differs from this authenticated helper version')
    import portage.sync.modules
    module = Path(portage.sync.modules.__path__[0]) / 'datapump_release'
    # Bootstrap by immutable tag, then configure subsequent syncs for the
    # requested channel. A pinned experiment never becomes an implicit Latest.
    if tag is None and manifest['experiment']:
        raise ValueError('An experiment requires explicit --tag registration')
    result = sync(repository, location, keyring, expected_fingerprint, tag or manifest['tag'])
    trusted_key = Path('/etc/portage/datapump-release-keyring.gpg')
    atomic_write(trusted_key, Path(keyring).read_bytes(), 0o644)
    config = {'repository': repository, 'location': str(Path(location).absolute()),
              'keyring': str(trusted_key), 'fingerprint': fingerprint(expected_fingerprint), 'tag': tag}
    atomic_write(INSTALLED_HELPER, (directory / HELPER).read_bytes(), 0o755)
    atomic_write(module / '__init__.py', (directory / ADAPTER_NAME).read_bytes(), 0o644)
    atomic_write(CONFIG_PATH, canonical(config), 0o644)
    text = (f'[datapump-bin]\nlocation = {config["location"]}\nmasters = gentoo\n'
            'sync-type = datapump-release\n'
            f'sync-uri = {release_base(repository, tag)}\nauto-sync = yes\nsync-user = root\n'
            f'sync-datapump-config = {CONFIG_PATH}\n')
    atomic_write('/etc/portage/repos.conf/datapump-bin.conf', text.encode(), 0o644)
    return dict(result, adapter=str(module))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    update = commands.add_parser('sync')
    update.add_argument('--config', required=True)
    update.add_argument('--test-base-url', help=argparse.SUPPRESS)
    setup = commands.add_parser('install')
    setup.add_argument('--assets', required=True)
    setup.add_argument('--keyring', required=True)
    setup.add_argument('--fingerprint', default=FINGERPRINT)
    setup.add_argument('--repository', required=True)
    setup.add_argument('--tag')
    setup.add_argument('--location', default=DEFAULT_LOCATION)
    args = parser.parse_args()
    try:
        if args.command == 'install':
            result = install(args.assets, args.keyring, args.fingerprint, args.repository, args.tag, args.location)
        else:
            config_path = Path(args.config)
            config_stat = config_path.stat()
            if config_path.is_symlink() or config_stat.st_mode & 0o022 or config_stat.st_uid != os.geteuid():
                raise ValueError('Sync configuration must be owned by the updater user and not writable by others')
            config = strict_json(config_path.read_bytes())
            if set(config) != {'repository', 'location', 'keyring', 'fingerprint', 'tag'}:
                raise ValueError('Unexpected sync configuration')
            result = sync(config['repository'], config['location'], config['keyring'],
                          config['fingerprint'], config['tag'], args.test_base_url)
        print(json.dumps(result, sort_keys=True))
    except (ValueError, OSError, KeyError, TypeError, tarfile.TarError, subprocess.SubprocessError) as error:
        print(f'DataPump Gentoo sync failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
