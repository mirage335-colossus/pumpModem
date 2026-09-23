#!/usr/bin/env python3
"""Deliver signed native pacman packages and databases without rebuilding the application."""
import argparse
import base64
from datetime import datetime
import gzip
import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import tarfile
import tempfile

ARCHES = ('x86_64', 'aarch64')
BACKENDS = ('fltk', 'rev')
KEYRING = 'datapump-pacman-keyring.gpg'
MANIFEST = 'arch-repository.json'
PACKAGER = 'DataPump release packaging'


def distro_module():
    spec = importlib.util.spec_from_file_location('arch_distro', Path(__file__).with_name('distro-release.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def package_name(metadata, architecture, backend):
    return f'datapump-{backend}-bin-{distro_module().distro_version(metadata)}-1-{architecture}.pkg.tar.gz'


def asset_names(metadata):
    names = {KEYRING, MANIFEST, MANIFEST + '.sig'}
    for architecture in ARCHES:
        names.add(f'datapump-pacman-{architecture}.conf')
        for extension in ('db', 'files'):
            name = f'datapump-{architecture}.{extension}'
            names.update((name, name + '.sig'))
        for backend in BACKENDS:
            name = package_name(metadata, architecture, backend)
            names.update((name, name + '.sig'))
    return names


def config(metadata, repository, architecture):
    channel = f'download/{metadata["tag"]}' if metadata['experiment'] else 'latest/download'
    return (f'# DataPump {metadata["tag"]}; {"experiment" if metadata["experiment"] else "regular"} channel.\n'
            '# Import and locally trust the separately verified repository signing key first.\n'
            '# Latest can move after a database refresh; on a missing old package, retry pacman -Syu.\n'
            f'[datapump-{architecture}]\nSigLevel = PackageRequired DatabaseRequired\n'
            f'Server = https://github.com/{repository}/releases/{channel}\n').encode()


def directories(files):
    result = set()
    for name in files:
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or str(path) != name:
            raise ValueError(f'Unsafe native package path: {name}')
        result.update(str(parent) for parent in path.parents if str(parent) != '.')
    if result & files.keys():
        raise ValueError('Native package path is both a file and a directory')
    return result


def compressed(data):
    output = io.BytesIO()
    with gzip.GzipFile(filename='', mode='wb', fileobj=output, compresslevel=6, mtime=0) as stream:
        stream.write(data)
    return output.getvalue()


def archive_bytes(files, epoch):
    output = io.BytesIO()
    dirs = directories(files)
    with tarfile.open(fileobj=output, mode='w', format=tarfile.PAX_FORMAT) as archive:
        for name in sorted(set(files) | dirs):
            member = tarfile.TarInfo(name)
            member.uid = member.gid = 0
            member.mtime = epoch
            if name in dirs:
                member.type, member.mode = tarfile.DIRTYPE, 0o755
                archive.addfile(member)
            else:
                data, member.mode = files[name]
                member.size = len(data)
                archive.addfile(member, io.BytesIO(data))
    return compressed(output.getvalue())


def mtree(files, epoch):
    # mtree octal quoting also handles whitespace, backslashes and UTF-8 names.
    def quoted(value):
        safe = b'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/._+-'
        return ''.join(chr(byte) if byte in safe else f'\\{byte:03o}' for byte in value.encode())
    lines = ['#mtree']
    dirs = directories(files)
    digest = distro_module().digest
    for name in sorted(set(files) | dirs):
        if name in dirs:
            attributes = 'type=dir mode=755'
        else:
            data, mode = files[name]
            attributes = f'type=file mode={mode:o} size={len(data)} sha256digest={digest(data)}'
        lines.append(f'{quoted("./" + name)} uid=0 gid=0 time={epoch} {attributes}')
    return compressed(('\n'.join(lines) + '\n').encode())


def expected(directory, metadata, repository):
    if metadata.get('schema') != 5:
        raise ValueError('Native pacman delivery requires release metadata schema 5')
    distro = distro_module()
    apt = distro.apt_module()
    # This validates the complete canonical release identity and source manifests.
    trees, source_manifest = distro.expected(Path(directory), metadata, repository)
    epoch = int(datetime.fromisoformat(metadata['created_at'].replace('Z', '+00:00')).timestamp())
    version = distro.distro_version(metadata) + '-1'
    result = {}
    for architecture in ARCHES:
        for backend in BACKENDS:
            target = f'linux-{architecture}-{backend}'
            source = source_manifest['archives'][target]
            payload = apt.archive_files(Path(directory) / source['archive'], metadata, target)
            files = apt.package_files(payload, backend)
            package = f'datapump-{backend}-bin'
            files[f'usr/share/licenses/{package}/LICENSE'] = trees['arch'][f'{package}/DataPump-Bundled']
            srcinfo = trees['arch'][f'{package}/.SRCINFO'][0].decode()
            dependencies = [line.strip().split(' = ', 1)[1] for line in srcinfo.splitlines()
                            if line.strip().startswith(('depends = ', f'depends_{architecture} = '))]
            size = sum(len(data) for data, _ in files.values())
            description = f'Portable audio modem with the {backend.upper()} GUI (prebuilt)'
            fields = [('pkgname', package), ('pkgbase', package), ('pkgver', version),
                      ('pkgdesc', description), ('url', f'https://github.com/{repository}'),
                      ('builddate', str(epoch)), ('packager', PACKAGER), ('size', str(size)),
                      ('arch', architecture), ('license', 'LicenseRef-DataPump-Bundled'),
                      ('xdata', 'pkgtype=pkg')]
            fields.extend(('depend', dep) for dep in dependencies)
            files['.PKGINFO'] = (''.join(f'{key} = {value}\n' for key, value in fields).encode(), 0o644)
            files['.MTREE'] = (mtree(files, epoch), 0o644)
            result[target] = {'name': package_name(metadata, architecture, backend), 'package': package,
                'version': version, 'architecture': architecture, 'backend': backend, 'epoch': epoch,
                'description': description, 'dependencies': dependencies, 'installed_size': size,
                'archive': source['archive'], 'archive_sha256': source['sha256'], 'files': files}
    return result


def database_files(directory, packages, architecture, repository, include_files):
    apt = distro_module().apt_module()
    result = {}
    for row in packages.values():
        if row['architecture'] != architecture:
            continue
        path = Path(directory) / row['name']
        # libalpm rejects slashes in FILENAME; URL is the project homepage, not a download override.
        # Keep versioned basenames. A stale Latest database can safely fail until the next full -Syu.
        values = [('FILENAME', [row['name']]), ('NAME', [row['package']]), ('BASE', [row['package']]),
                  ('VERSION', [row['version']]), ('DESC', [row['description']]),
                  ('CSIZE', [str(path.stat().st_size)]), ('ISIZE', [str(row['installed_size'])]),
                  ('SHA256SUM', [apt.sha256(path)]),
                  ('PGPSIG', [base64.b64encode(path.with_name(path.name + '.sig').read_bytes()).decode()]),
                  ('URL', [f'https://github.com/{repository}']), ('LICENSE', ['LicenseRef-DataPump-Bundled']),
                  ('ARCH', [architecture]), ('BUILDDATE', [str(row['epoch'])]), ('PACKAGER', [PACKAGER]),
                  ('DEPENDS', row['dependencies']), ('DATA', ['pkgtype=pkg'])]
        prefix = f'{row["package"]}-{row["version"]}'
        result[prefix + '/desc'] = (''.join(f'%{key}%\n' + '\n'.join(value) + '\n\n'
                                          for key, value in values).encode(), 0o644)
        if include_files:
            payload = {name: value for name, value in row['files'].items() if not name.startswith('.')}
            entries = sorted(set(payload) | {name + '/' for name in directories(payload)})
            result[prefix + '/files'] = (('%FILES%\n' + '\n'.join(entries) + '\n\n').encode(), 0o644)
    return result


def manifest_identity(metadata, repository, fingerprint, packages):
    apt = distro_module().apt_module()
    return {'schema': 1, 'repository': repository, 'tag': metadata['tag'],
            'metadata_identity_sha256': apt.metadata_identity(metadata),
            'signing_fingerprint': fingerprint, 'channel': 'experiment' if metadata['experiment'] else 'regular',
            'packages': {target: {key: value for key, value in row.items() if key != 'files'}
                         for target, row in sorted(packages.items())}}


def build(directory, metadata, repository, signing_key, signing_fingerprint):
    directory = Path(directory)
    names = asset_names(metadata)
    if any((directory / name).exists() or (directory / name).is_symlink() for name in names):
        raise ValueError('Refusing to overwrite existing pacman release assets')
    distro = distro_module()
    apt = distro.apt_module()
    fingerprint = apt.normalized_fingerprint(signing_fingerprint)
    packages = expected(directory, metadata, repository)
    with tempfile.TemporaryDirectory(prefix='datapump-pacman-signing-') as temporary:
        os.chmod(temporary, 0o700)
        args = ('gpg', '--batch', '--homedir', temporary)
        apt.run(*args, '--import', signing_key)
        public = apt.run(*args, '--export', fingerprint).stdout
        if not public:
            raise ValueError('Configured pacman signing key is unavailable')
        (directory / KEYRING).write_bytes(public)
        if apt.fingerprint(directory / KEYRING) != fingerprint:
            raise ValueError('Pacman signing key fingerprint mismatch')

        def sign(name):
            apt.run(*args, '--pinentry-mode', 'loopback', '--passphrase', '', '--local-user', fingerprint,
                    '--digest-algo', 'SHA256', '--output', directory / (name + '.sig'),
                    '--detach-sign', directory / name)

        for row in packages.values():
            (directory / row['name']).write_bytes(archive_bytes(row['files'], row['epoch']))
            sign(row['name'])
        epoch = next(iter(packages.values()))['epoch']
        for architecture in ARCHES:
            (directory / f'datapump-pacman-{architecture}.conf').write_bytes(config(metadata, repository, architecture))
            for extension in ('db', 'files'):
                name = f'datapump-{architecture}.{extension}'
                files = database_files(directory, packages, architecture, repository, extension == 'files')
                (directory / name).write_bytes(archive_bytes(files, epoch))
                sign(name)
        manifest = manifest_identity(metadata, repository, fingerprint, packages)
        manifest['assets'] = {name: apt.sha256(directory / name) for name in sorted(names - {MANIFEST, MANIFEST + '.sig'})}
        (directory / MANIFEST).write_bytes(distro.document(manifest))
        sign(MANIFEST)
    verify(directory, metadata, repository, fingerprint)
    return manifest


def check_archive(path, expected_files, epoch):
    dirs = directories(expected_files)
    seen = set()
    with tarfile.open(path, 'r:gz') as archive:
        for member in archive:
            name = member.name
            if name in seen or name not in set(expected_files) | dirs:
                raise ValueError(f'Unexpected or duplicate pacman archive member: {name}')
            seen.add(name)
            if (member.uid != 0 or member.gid != 0 or member.uname or member.gname
                    or member.mtime != epoch or member.linkname):
                raise ValueError(f'Pacman archive ownership or timestamp differs: {name}')
            if name in dirs:
                if not member.isdir() or member.mode != 0o755:
                    raise ValueError(f'Pacman archive directory mode differs: {name}')
            else:
                data, mode = expected_files[name]
                if (not member.isfile() or member.mode != mode or member.size != len(data)
                        or archive.extractfile(member).read() != data):
                    raise ValueError(f'Pacman archive payload differs: {name}')
    if seen != set(expected_files) | dirs:
        raise ValueError('Pacman archive file set is incomplete')


def verify(directory, metadata, repository=None, expected_fingerprint=None):
    directory = Path(directory)
    apt = distro_module().apt_module()
    names = asset_names(metadata)
    for name in names:
        path = directory / name
        if not path.is_file() or path.is_symlink():
            raise ValueError(f'Missing or unsafe pacman asset: {name}')
    fingerprint = apt.fingerprint(directory / KEYRING)
    if expected_fingerprint and fingerprint != apt.normalized_fingerprint(expected_fingerprint):
        raise ValueError('Untrusted pacman signing fingerprint')
    with tempfile.TemporaryDirectory(prefix='datapump-pacman-verify-') as temporary:
        os.chmod(temporary, 0o700)

        def verify_signature(name):
            apt.run('gpgv', '--homedir', temporary, '--keyring', (directory / KEYRING).resolve(),
                    directory / (name + '.sig'), directory / name)

        verify_signature(MANIFEST)
        manifest = json.loads((directory / MANIFEST).read_text())
        repository = repository or manifest['repository']
        packages = expected(directory, metadata, repository)
        wanted = manifest_identity(metadata, repository, fingerprint, packages)
        wanted['assets'] = {name: apt.sha256(directory / name) for name in sorted(names - {MANIFEST, MANIFEST + '.sig'})}
        if manifest != wanted:
            raise ValueError('Pacman release identity or asset checksum differs')
        for row in packages.values():
            verify_signature(row['name'])
            check_archive(directory / row['name'], row['files'], row['epoch'])
        epoch = next(iter(packages.values()))['epoch']
        for architecture in ARCHES:
            if (directory / f'datapump-pacman-{architecture}.conf').read_bytes() != config(metadata, repository, architecture):
                raise ValueError('Pacman repository configuration differs')
            for extension in ('db', 'files'):
                name = f'datapump-{architecture}.{extension}'
                verify_signature(name)
                check_archive(directory / name, database_files(directory, packages, architecture, repository,
                                                               extension == 'files'), epoch)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('build', 'verify'))
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--repository')
    parser.add_argument('--signing-key', type=Path)
    parser.add_argument('--signing-fingerprint')
    args = parser.parse_args()
    try:
        metadata = distro_module().apt_module().release_module().load_metadata(args.metadata)
        if args.command == 'build':
            if not args.repository or not args.signing_key or not args.signing_fingerprint:
                raise ValueError('build requires a repository, signing key and fingerprint')
            result = build(args.directory, metadata, args.repository, args.signing_key, args.signing_fingerprint)
        else:
            result = verify(args.directory, metadata, args.repository, args.signing_fingerprint)
        print(json.dumps(result, indent=2, sort_keys=True))
    except (ValueError, KeyError, OSError, tarfile.TarError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'arch-release: {error}\n')


if __name__ == '__main__':
    main()
