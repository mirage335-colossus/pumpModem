#!/usr/bin/env python3
"""Wrap verified portable Linux archives in a signed, release-assets-only APT repository."""
import argparse
from datetime import datetime, timezone
from email.utils import format_datetime
import gzip
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile
import tempfile

ARCHES = {'amd64': 'x86_64', 'arm64': 'aarch64'}
BACKENDS = ('fltk', 'rev')
SUPPORT = {'Packages', 'Packages.gz', 'Release', 'InRelease', 'Release.gpg',
           'datapump-archive-keyring.gpg', 'datapump.sources', 'apt-repository.json'}
KEYRING = '/etc/apt/keyrings/datapump.gpg'


def release_module():
    spec = importlib.util.spec_from_file_location('apt_portable_release', Path(__file__).with_name('release.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(*args, **kwargs):
    return subprocess.run([str(arg) for arg in args], check=True, capture_output=True, **kwargs)


def sha256(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest() if hasattr(hashlib, 'file_digest') else stream_hash(source)


def stream_hash(source):
    value = hashlib.sha256()
    for block in iter(lambda: source.read(1024 * 1024), b''):
        value.update(block)
    return value.hexdigest()


def debian_version(metadata):
    version = metadata['project_version']
    if not re.fullmatch(r'\d+(?:\.\d+){1,3}', version):
        raise ValueError('Invalid Debian upstream version')
    stamp = datetime.fromisoformat(metadata['created_at'].replace('Z', '+00:00'))
    if stamp.utcoffset() is None:
        raise ValueError('Release timestamp needs an offset')
    for name in ('run_id', 'run_attempt'):
        if not re.fullmatch(r'[1-9][0-9]*', str(metadata[name])):
            raise ValueError('Invalid release run identity')
    # UTC sorts correctly across the autumn CDT/CST clock reversal. The custom
    # display label remains in release metadata; underscores are not Debian versions.
    return f'{version}+{stamp.astimezone(timezone.utc):%Y%m%d%H%M%S}.r{metadata["run_id"]}.a{metadata["run_attempt"]}'


def package_name(metadata, arch, backend):
    return f'datapump-{backend}_{debian_version(metadata)}_{arch}.deb'


def asset_names(metadata):
    return SUPPORT | {package_name(metadata, arch, backend) for arch in ARCHES for backend in BACKENDS}


def metadata_identity(metadata):
    return hashlib.sha256(json.dumps(metadata, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def validate_location(metadata, repository):
    release = release_module()
    release.repository_name(repository)
    if metadata.get('schema') not in (3, 4) or metadata.get('gui_backends') != list(BACKENDS):
        raise ValueError('APT packaging requires the complete schema-3+ backend inventory')
    tag = metadata['tag']
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,100}', tag) or '..' in tag:
        raise ValueError('Unsafe release tag')


def sources(metadata, repository):
    base = (f'releases/download/{metadata["tag"]}' if metadata['experiment'] else 'releases/latest/download')
    return (f'Types: deb\nURIs: https://github.com/{repository}/{base}/\nSuites: ./\n'
            f'Architectures: amd64 arm64\nSigned-By: {KEYRING}\n')


def archive_files(path, metadata, target):
    """Read regular portable entries; never extract links, special nodes or traversal."""
    release = release_module()
    release.verify_archive_backend(path, metadata, target)
    roots = set(release.package_bases(metadata, target))
    seen = set()
    files = {}
    with tarfile.open(path, 'r:gz') as archive:
        for member in archive:
            raw = member.name.removeprefix('./').rstrip('/')
            parts = raw.split('/')
            if (not raw or '\\' in raw or ':' in raw or any(p in ('', '.', '..') for p in parts)
                    or parts[0] not in roots or raw in seen or not (member.isfile() or member.isdir())
                    or member.mode & 0o7000):
                raise ValueError(f'Unsafe portable archive entry: {member.name}')
            seen.add(raw)
            if member.isdir():
                continue
            if len(parts) < 2:
                raise ValueError('Portable file has no relative path')
            relative = '/'.join(parts[1:])
            data = archive.extractfile(member).read()
            files[relative] = (data, 0o755 if member.mode & 0o111 else 0o644)
    for name in ('bin/pump', 'bin/datapump-gui', 'manifest.sha256'):
        if name not in files:
            raise ValueError(f'Portable archive lacks {name}')
    expected = {}
    for line in files['manifest.sha256'][0].decode('utf-8').splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  (.+)', line)
        if not match or match[2] in expected:
            raise ValueError('Invalid portable manifest')
        expected[match[2]] = match[1]
    if set(expected) != set(files) - {'manifest.sha256'}:
        raise ValueError('Portable manifest inventory mismatch')
    if any(hashlib.sha256(files[name][0]).hexdigest() != value for name, value in expected.items()):
        raise ValueError('Portable manifest checksum mismatch')
    return files


def package_files(payload, backend):
    prefix = f'opt/datapump/{backend}'
    result = {f'{prefix}/{name}': value for name, value in payload.items()}
    for name, executable in ((f'datapump-{backend}', 'datapump-gui'), (f'datapump-cli-{backend}', 'pump')):
        result[f'usr/bin/{name}'] = (f'#!/bin/sh\nexec /{prefix}/bin/{executable} "$@"\n'.encode(), 0o755)
    desktop = (f'[Desktop Entry]\nType=Application\nName=DataPump ({backend.upper()})\n'
               f'Comment=Portable audio modem\nExec=datapump-{backend}\nTerminal=false\n'
               'Icon=utilities-terminal\nCategories=AudioVideo;Audio;\n')
    result[f'usr/share/applications/datapump-{backend}.desktop'] = (desktop.encode(), 0o644)
    return result


def control(metadata, arch, backend, installed_size):
    floor = '2.36' if arch == 'amd64' and metadata['linux_baseline'] == 'bookworm-sdk' else '2.35'
    # libasound2-plugins supplies the host PulseAudio bridge (including Crostini).
    # Mesa supplies host drivers, which intentionally are not in the portable payload.
    return (f'Package: datapump-{backend}\nVersion: {debian_version(metadata)}\nArchitecture: {arch}\n'
            'Maintainer: DataPump maintainers <noreply@github.com>\nSection: sound\nPriority: optional\n'
            f'Installed-Size: {installed_size}\nDepends: libc6 (>= {floor}), libasound2-plugins, fonts-dejavu-core, fontconfig-config, libgl1, libopengl0, libgl1-mesa-dri, libglx-mesa0\n'
            'Homepage: https://github.com/mirage335-colossus/pumpModem\n'
            f'Description: DataPump audio modem with the {backend.upper()} GUI\n'
            ' Contains the portable CLI and GUI with private bundled libraries.\n'
            f' Release: {metadata["tag"]}\n'
            f' Channel: {"experiment (not certified)" if metadata["experiment"] else "regular (see release certification)"}\n')


def write_file(path, data, mode=0o644):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    path.chmod(mode)


def make_deb(directory, metadata, arch, backend, archive):
    payload = archive_files(archive, metadata, f'linux-{ARCHES[arch]}-{backend}')
    files = package_files(payload, backend)
    with tempfile.TemporaryDirectory(prefix='datapump-deb-') as temporary:
        root = Path(temporary) / 'root'
        root.mkdir()
        for name, (data, mode) in files.items():
            write_file(root / name, data, mode)
        text = control(metadata, arch, backend, (sum(len(value[0]) for value in files.values()) + 1023) // 1024)
        write_file(root / 'DEBIAN/control', text.encode())
        # Signing workflows use umask 077 for secrets; installed directories
        # must still be traversable by ordinary desktop users.
        root.chmod(0o755)
        for path in root.rglob('*'):
            if path.is_dir():
                path.chmod(0o755)
        epoch = int(datetime.fromisoformat(metadata['created_at'].replace('Z', '+00:00')).timestamp())
        env = dict(os.environ, SOURCE_DATE_EPOCH=str(epoch))
        run('dpkg-deb', '--root-owner-group', '-Zgzip', '-z6', '--build', root,
            directory / package_name(metadata, arch, backend), env=env)
    return text


def fingerprint(path):
    with tempfile.TemporaryDirectory(prefix='datapump-public-key-') as temporary:
        os.chmod(temporary, 0o700)
        output = run('gpg', '--batch', '--homedir', temporary, '--with-colons', '--show-keys', path).stdout.decode()
    primaries, pending = [], False
    for line in output.splitlines():
        fields = line.split(':')
        if fields[0] == 'pub':
            pending = True
        elif fields[0] == 'fpr' and pending:
            primaries.append(fields[9])
            pending = False
    if len(primaries) != 1:
        raise ValueError('Expected one repository signing key')
    return primaries[0]


def normalized_fingerprint(value):
    if not isinstance(value, str) or not re.fullmatch(r'(?:[0-9A-Fa-f]{40}|[0-9A-Fa-f]{64})', value):
        raise ValueError('A complete signing fingerprint is required')
    return value.upper()


def sign(directory, signing_key, signing_fingerprint):
    expected = normalized_fingerprint(signing_fingerprint)
    with tempfile.TemporaryDirectory(prefix='datapump-signing-') as temporary:
        os.chmod(temporary, 0o700)
        args = ('gpg', '--batch', '--homedir', temporary)
        run(*args, '--import', signing_key)
        public = run(*args, '--export', expected).stdout
        if not public:
            raise ValueError('Configured signing key is unavailable')
        (directory / 'datapump-archive-keyring.gpg').write_bytes(public)
        if fingerprint(directory / 'datapump-archive-keyring.gpg') != expected:
            raise ValueError('Repository signing key fingerprint mismatch')
        for filename, option in (('InRelease', '--clearsign'), ('Release.gpg', '--detach-sign')):
            run(*args, '--pinentry-mode', 'loopback', '--passphrase', '', '--local-user', expected,
                '--digest-algo', 'SHA256', '--output', directory / filename, option, directory / 'Release')
    return expected


def build(directory, metadata, repository, signing_key, signing_fingerprint):
    directory = Path(directory)
    validate_location(metadata, repository)
    if any((directory / name).exists() or (directory / name).is_symlink() for name in asset_names(metadata)):
        raise ValueError('Refusing to overwrite existing APT assets')
    fpr = normalized_fingerprint(signing_fingerprint)
    release = release_module()
    manifest = {'schema': 1, 'repository': repository, 'tag': metadata['tag'],
                'source_sha': metadata['source_sha'], 'version': debian_version(metadata),
                'metadata_identity_sha256': metadata_identity(metadata),
                'signing_fingerprint': fpr, 'packages': []}
    if metadata['schema'] >= 4:
        manifest['distribution_assets'] = {name: sha256(directory / name)
                                            for name in sorted(release.distro_assets(metadata))}
    stanzas = []
    for arch in ARCHES:
        for backend in BACKENDS:
            target = f'linux-{ARCHES[arch]}-{backend}'
            archive = directory / release.application_names(metadata)[target]
            text = make_deb(directory, metadata, arch, backend, archive)
            name = package_name(metadata, arch, backend)
            path = directory / name
            # Both latest/download/ and download/TAG/ are two levels below
            # releases/. Pin payload URLs so a moving Latest never changes them.
            location = f'../../download/{metadata["tag"]}/{name}'
            stanzas.append(text + f'Filename: {location}\nSize: {path.stat().st_size}\nSHA256: {sha256(path)}\n')
            manifest['packages'].append({'name': name, 'architecture': arch, 'backend': backend,
                                        'sha256': sha256(path), 'size': path.stat().st_size,
                                        'archive': archive.name, 'archive_sha256': sha256(archive)})
    packages = ('\n'.join(stanzas) + '\n').encode()
    (directory / 'Packages').write_bytes(packages)
    (directory / 'Packages.gz').write_bytes(gzip.compress(packages, mtime=0))
    (directory / 'datapump.sources').write_text(sources(metadata, repository), encoding='utf-8')
    manifest['sources_sha256'] = sha256(directory / 'datapump.sources')
    (directory / 'apt-repository.json').write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    instant = datetime.fromisoformat(metadata['created_at'].replace('Z', '+00:00')).astimezone(timezone.utc)
    release_text = (f'Origin: {repository}\nLabel: DataPump\nSuite: datapump\nCodename: datapump\n'
                    f'Date: {format_datetime(instant, usegmt=True)}\nArchitectures: amd64 arm64\n'
                    'Description: DataPump portable application packages\nAcquire-By-Hash: no\nSHA256:\n')
    for name in ('Packages', 'Packages.gz', 'apt-repository.json'):
        path = directory / name
        release_text += f' {sha256(path)} {path.stat().st_size:16d} {name}\n'
    (directory / 'Release').write_text(release_text, encoding='utf-8')
    sign(directory, signing_key, fpr)
    return verify(directory, metadata, repository, fpr)


def deb_files(path):
    raw = run('dpkg-deb', '--fsys-tarfile', path).stdout
    files, directories, seen = {}, set(), set()
    with tarfile.open(fileobj=io.BytesIO(raw)) as archive:
        for member in archive:
            name = member.name.removeprefix('./').rstrip('/')
            if member.isdir() and name == '.':
                name = ''
            if (member.uid != 0 or member.gid != 0 or name in seen or member.mode & 0o7000
                    or (name and any(part in ('', '.', '..') for part in name.split('/')))):
                raise ValueError('Unsafe Debian package member')
            seen.add(name)
            if member.isdir():
                if member.mode & 0o777 != 0o755:
                    raise ValueError('Unsafe Debian directory permissions')
                directories.add(name)
                continue
            if not member.isfile() or not name:
                raise ValueError('Unsafe Debian package member')
            files[name] = (archive.extractfile(member).read(), member.mode & 0o777)
    parents = {''}
    for name in files:
        parents.update(str(p) for p in PurePosixPath(name).parents if str(p) != '.')
    if directories != parents:
        raise ValueError('Unexpected Debian package directories')
    return files


def verify(directory, metadata, repository=None, trusted_fingerprint=None):
    directory = Path(directory)
    for name in asset_names(metadata):
        if not (directory / name).is_file() or (directory / name).is_symlink():
            raise ValueError(f'Missing or unsafe APT asset: {name}')
    manifest = json.loads((directory / 'apt-repository.json').read_text())
    repository = repository or manifest.get('repository')
    validate_location(metadata, repository)
    fpr = fingerprint(directory / 'datapump-archive-keyring.gpg')
    if trusted_fingerprint and fpr != normalized_fingerprint(trusted_fingerprint):
        raise ValueError('Untrusted repository signing fingerprint')
    if any(manifest.get(key) != value for key, value in {
            'schema': 1, 'repository': repository, 'tag': metadata['tag'],
            'source_sha': metadata['source_sha'], 'version': debian_version(metadata),
            'metadata_identity_sha256': metadata_identity(metadata),
            'signing_fingerprint': fpr}.items()):
        raise ValueError('APT manifest release identity mismatch')
    with tempfile.TemporaryDirectory(prefix='datapump-verify-') as temporary:
        extracted = Path(temporary) / 'Release'
        run('gpgv', '--homedir', temporary, '--keyring', (directory / 'datapump-archive-keyring.gpg').resolve(),
            '--output', extracted, directory / 'InRelease')
        run('gpgv', '--homedir', temporary, '--keyring', (directory / 'datapump-archive-keyring.gpg').resolve(),
            directory / 'Release.gpg', directory / 'Release')
        if extracted.read_bytes() != (directory / 'Release').read_bytes():
            raise ValueError('Signed and detached Release metadata differ')
    rows = (directory / 'Release').read_text().split('SHA256:\n')
    if len(rows) != 2:
        raise ValueError('Invalid APT Release hashes')
    hashes = {}
    for line in rows[1].splitlines():
        parts = line.split()
        if len(parts) != 3 or parts[2] in hashes:
            raise ValueError('Invalid or duplicate APT Release checksum')
        hashes[parts[2]] = parts[:2]
    if set(hashes) != {'Packages', 'Packages.gz', 'apt-repository.json'}:
        raise ValueError('Incomplete signed APT repository inventory')
    for name, (digest, size) in hashes.items():
        if sha256(directory / name) != digest or str((directory / name).stat().st_size) != size:
            raise ValueError('Signed APT repository checksum mismatch')
    if metadata['schema'] >= 4:
        expected_distribution = {name: sha256(directory / name)
                                 for name in sorted(release_module().distro_assets(metadata))}
        if manifest.get('distribution_assets') != expected_distribution:
            raise ValueError('Signed distribution recipe checksum mismatch')
    if gzip.decompress((directory / 'Packages.gz').read_bytes()) != (directory / 'Packages').read_bytes():
        raise ValueError('Compressed package index differs')
    if ((directory / 'datapump.sources').read_text() != sources(metadata, repository)
            or manifest.get('sources_sha256') != sha256(directory / 'datapump.sources')):
        raise ValueError('APT source configuration differs')
    expected_entries = []
    expected_stanzas = []
    release = release_module()
    for arch in ARCHES:
        for backend in BACKENDS:
            target = f'linux-{ARCHES[arch]}-{backend}'
            archive = directory / release.application_names(metadata)[target]
            payload = archive_files(archive, metadata, target)
            files = package_files(payload, backend)
            name = package_name(metadata, arch, backend)
            path = directory / name
            if deb_files(path) != files:
                raise ValueError('Debian payload differs from its verified portable archive')
            size = (sum(len(value[0]) for value in files.values()) + 1023) // 1024
            text = control(metadata, arch, backend, size)
            if run('dpkg-deb', '--field', path).stdout.decode() != text:
                raise ValueError('Debian control metadata differs')
            # No maintainer scripts or hidden installation hooks are permitted.
            ctl = run('dpkg-deb', '--ctrl-tarfile', path).stdout
            with tarfile.open(fileobj=io.BytesIO(ctl)) as members:
                entries = members.getmembers()
                if (len(entries) != 2 or any(m.uid != 0 or m.gid != 0 for m in entries)
                        or not entries[0].isdir() or entries[0].name != '.' or entries[0].mode != 0o755
                        or not entries[1].isfile() or entries[1].name.removeprefix('./') != 'control'
                        or entries[1].mode != 0o644):
                    raise ValueError('Unexpected Debian control scripts')
            expected_entries.append({'name': name, 'architecture': arch, 'backend': backend,
                                     'sha256': sha256(path), 'size': path.stat().st_size,
                                     'archive': archive.name, 'archive_sha256': sha256(archive)})
            expected_stanzas.append(text + f'Filename: ../../download/{metadata["tag"]}/{name}\nSize: {path.stat().st_size}\nSHA256: {sha256(path)}\n')
    if manifest.get('packages') != expected_entries:
        raise ValueError('APT package/archive binding mismatch')
    if (directory / 'Packages').read_text() != '\n'.join(expected_stanzas) + '\n':
        raise ValueError('Package index does not identify the immutable release packages')
    return manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('build', 'verify'))
    parser.add_argument('--directory', required=True, type=Path)
    parser.add_argument('--metadata', required=True, type=Path)
    parser.add_argument('--repository')
    parser.add_argument('--signing-key', type=Path)
    parser.add_argument('--signing-fingerprint')
    args = parser.parse_args(argv)
    try:
        metadata = release_module().load_metadata(args.metadata)
        if args.command == 'build':
            if not args.repository or not args.signing_key or not args.signing_fingerprint:
                raise ValueError('build needs repository, signing key and fingerprint')
            result = build(args.directory, metadata, args.repository, args.signing_key, args.signing_fingerprint)
        else:
            result = verify(args.directory, metadata, args.repository, args.signing_fingerprint)
        print(json.dumps(result, sort_keys=True))
    except (OSError, ValueError, subprocess.CalledProcessError, tarfile.TarError) as error:
        parser.exit(1, f'apt-release: {error}\n')


if __name__ == '__main__':
    main()
