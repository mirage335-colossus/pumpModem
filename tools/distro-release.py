#!/usr/bin/env python3
"""Generate immutable Arch binary recipes and a Gentoo binary overlay from portable releases."""
import argparse
from datetime import datetime, timezone
import gzip
import hashlib
import importlib.util
import io
import json
from pathlib import Path, PurePosixPath
import re
import stat
import tarfile

ASSETS = {'datapump-arch-recipes.tar.gz', 'datapump-gentoo-overlay.tar.gz', 'distro-packages.json'}
ROOTS = {'arch': 'datapump-arch-recipes', 'gentoo': 'datapump-gentoo-overlay'}
ARCHES = {'x86_64': 'amd64', 'aarch64': 'arm64'}
BACKENDS = ('fltk', 'rev')


def apt_module():
    spec = importlib.util.spec_from_file_location('distro_apt', Path(__file__).with_name('apt-release.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def asset_names(metadata):
    return set(ASSETS)


def distro_version(metadata):
    apt = apt_module()
    apt.debian_version(metadata)  # Validate numeric project/run fields and timestamp.
    stamp = datetime.fromisoformat(metadata['created_at'].replace('Z', '+00:00')).astimezone(timezone.utc)
    version = f'{metadata["project_version"]}_p{stamp:%Y%m%d%H%M%S}_p{metadata["run_id"]}_p{metadata["run_attempt"]}'
    if any(len(part) > 18 for part in re.findall(r'\d+', version)):
        raise ValueError('Gentoo version components must not exceed 18 digits')
    return version


def document(value):
    return (json.dumps(value, sort_keys=True, indent=2) + '\n').encode()


def digest(data, algorithm='sha256'):
    return hashlib.new(algorithm, data).hexdigest()


def notice_text(payloads):
    sections = [b'DataPump bundled component notices\n\n'
                b'These are preserved notices from the exact portable release; this file grants no additional rights.\n'
                b'DataPump itself is CC0-1.0. Third-party components retain their own terms and provenance.\n']
    unique = set()
    for target, payload in sorted(payloads.items()):
        for name, (data, _) in sorted(payload.items()):
            if not name.startswith('share/doc/datapump/'):
                continue
            lower = name.lower()
            if not any(word in lower for word in ('license', 'copying', 'copyright', 'notice', 'third_party', 'source.md')):
                continue
            if data in unique:
                continue
            data.decode('utf-8')  # License entries must remain readable, unmodified UTF-8.
            unique.add(data)
            sections.append(f'\n===== {target}: {name} =====\n'.encode() + data + b'\n')
    if not unique:
        raise ValueError('Portable release has no preserved component notices')
    return b''.join(sections)


def install_extras(payload, backend):
    apt = apt_module()
    return {PurePosixPath(name).name: value for name, value in apt.package_files(payload, backend).items()
            if name.startswith('usr/')}


def normalization(destination):
    return (f'find "{destination}" -type d -exec chmod 0755 {{}} +\n'
            f'find "{destination}" -type f -perm /111 -exec chmod 0755 {{}} +\n'
            f'find "{destination}" -type f ! -perm /111 -exec chmod 0644 {{}} +')


def arch_recipe(metadata, repository, backend, rows, extras):
    apt = apt_module()
    release = apt.release_module()
    package = f'datapump-{backend}-bin'
    version = distro_version(metadata)
    depends = ['alsa-plugins', 'fontconfig', 'ttf-dejavu', 'libglvnd', 'mesa']
    options = ['!strip', '!debug', '!lto', '!zipman', '!purge', 'staticlibs', 'libtool']
    local_names = sorted(extras)
    fields = [('pkgdesc', f'Portable audio modem with the {backend.upper()} GUI (prebuilt)'),
              ('pkgver', version), ('pkgrel', '1'), ('url', f'https://github.com/{repository}')]
    text = '# Generated from a verified release; no application or SDK compilation.\n'
    text += f'pkgname={package}\n' + ''.join(f"{key}='{value}'\n" for key, value in fields)
    text += "arch=('x86_64' 'aarch64')\nlicense=('LicenseRef-DataPump-Bundled')\n"
    text += 'depends=(' + ' '.join(repr(value) for value in depends) + ')\n'
    text += 'options=(' + ' '.join(repr(value) for value in options) + ')\n'
    text += 'source=(' + ' '.join(repr(name) for name in local_names) + ')\n'
    text += 'sha256sums=(' + ' '.join(repr(digest(extras[name][0])) for name in local_names) + ')\n'
    srcinfo = f'pkgbase = {package}\n' + ''.join(f'\t{key} = {value}\n' for key, value in fields)
    for key, values in [('arch', ARCHES), ('license', ['LicenseRef-DataPump-Bundled']),
                        ('depends', depends), ('options', options), ('source', local_names),
                        ('sha256sums', [digest(extras[name][0]) for name in local_names])]:
        srcinfo += ''.join(f'\t{key} = {value}\n' for value in values)
    for arch in ARCHES:
        row = rows[f'linux-{arch}-{backend}']
        floor = '2.36' if arch == 'x86_64' and metadata['linux_baseline'] == 'bookworm-sdk' else '2.35'
        text += f"depends_{arch}=('glibc>={floor}')\nsource_{arch}=('{row['url']}')\nsha256sums_{arch}=('{row['sha256']}')\n"
        srcinfo += f'\tdepends_{arch} = glibc>={floor}\n\tsource_{arch} = {row["url"]}\n\tsha256sums_{arch} = {row["sha256"]}\n'
    roots = {arch: rows[f'linux-{arch}-{backend}']['package_root'] for arch in ARCHES}
    destination = '${pkgdir}/opt/datapump/' + backend
    text += '\npackage() {\n  local root\n  case "$CARCH" in\n'
    text += ''.join(f"    {arch}) root='{root}' ;;\n" for arch, root in roots.items())
    text += '    *) return 1 ;;\n  esac\n'
    text += f'  install -dm755 "{destination}"\n  cp -a "$srcdir/$root/." "{destination}/"\n'
    text += '\n'.join('  ' + line for line in normalization(destination).splitlines()) + '\n'
    for name, (_, mode) in sorted(extras.items()):
        target = (f'usr/share/licenses/{package}/LICENSE' if name == 'DataPump-Bundled' else
                  f'usr/share/applications/{name}' if name.endswith('.desktop') else f'usr/bin/{name}')
        text += f'  install -Dm{mode:04o} "$srcdir/{name}" "$pkgdir/{target}"\n'
    text += '}\n'
    srcinfo += f'\npkgname = {package}\n'
    return {'PKGBUILD': (text.encode(), 0o644), '.SRCINFO': (srcinfo.encode(), 0o644), **extras}


def gentoo_recipe(metadata, repository, backend, rows):
    release = apt_module().release_module()
    version = distro_version(metadata)
    floor = '2.36' if metadata['linux_baseline'] == 'bookworm-sdk' else '2.35'
    text = ('# Generated DataPump binary recipe; packaging instructions are CC0-1.0.\n'
            '# Bundled application components retain their individual licenses.\n\nEAPI=8\n\n'
            f'DESCRIPTION="Portable audio modem with the {backend.upper()} GUI (prebuilt)"\n'
            f'HOMEPAGE="https://github.com/{repository}"\nSRC_URI="\n')
    text += ''.join(f'\t{gentoo}? ( {rows[f"linux-{arch}-{backend}"]["url"]} )\n' for arch, gentoo in ARCHES.items())
    text += ('"\nLICENSE="DataPump-Bundled"\nSLOT="0"\nKEYWORDS="~amd64 ~arm64"\n'
             'RESTRICT="strip mirror"\nQA_PREBUILT="*"\nS="${WORKDIR}"\n\n'
             f'RDEPEND="\n\tamd64? ( >=sys-libs/glibc-{floor} )\n\tarm64? ( >=sys-libs/glibc-2.35 )\n'
             '\tmedia-libs/fontconfig\n\tmedia-fonts/dejavu\n\tmedia-libs/libglvnd[X]\n\tmedia-libs/mesa[X]\n'
             '\t|| ( media-video/pipewire[pipewire-alsa] media-plugins/alsa-plugins[pulseaudio] )\n"\n\n'
             'src_prepare() { :; }\nsrc_configure() { :; }\nsrc_compile() { :; }\n\n'
             'src_install() {\n\tlocal root\n\tcase "${ARCH}" in\n')
    text += ''.join(f'\t\t{gentoo}) root="${{WORKDIR}}/{rows[f"linux-{arch}-{backend}"]["package_root"]}" ;;\n'
                    for arch, gentoo in ARCHES.items())
    text += '\t\t*) die "Unsupported binary architecture: ${ARCH}" ;;\n\tesac\n'
    destination = '${ED}/opt/datapump/' + backend
    text += f'\tdodir /opt/datapump/{backend}\n\tcp -a "${{root}}/." "{destination}/" || die\n'
    text += '\n'.join('\t' + line + ' || die' for line in normalization(destination).splitlines()) + '\n'
    text += (f'\tdobin "${{FILESDIR}}/datapump-{backend}" "${{FILESDIR}}/datapump-cli-{backend}"\n'
             '\tinsinto /usr/share/applications\n'
             f'\tdoins "${{FILESDIR}}/datapump-{backend}.desktop"\n'
             f'\tdocompress -x /opt/datapump/{backend}\n}}\n')
    manifest = ''
    for arch in ARCHES:
        row = rows[f'linux-{arch}-{backend}']
        manifest += (f'DIST {row["archive"]} {row["size"]} BLAKE2B {row["blake2b"]} '
                     f'SHA256 {row["sha256"]} SHA512 {row["sha512"]}\n')
    return {f'datapump-{backend}-bin-{version}.ebuild': (text.encode(), 0o644),
            'Manifest': (manifest.encode(), 0o644),
            'metadata.xml': (b'<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE pkgmetadata SYSTEM "https://www.gentoo.org/dtd/metadata.dtd">\n<pkgmetadata>\n<!-- maintainer-needed -->\n</pkgmetadata>\n', 0o644)}


def expected(directory, metadata, repository):
    apt = apt_module()
    release = apt.release_module()
    release.repository_name(repository)
    if metadata.get('schema') != 4 or metadata.get('gui_backends') != list(BACKENDS):
        raise ValueError('Distribution recipes require release metadata schema 4 and both backends')
    # Validate canonical fields before interpolating any shell recipe text.
    checked = release.make_metadata(source_sha=metadata['source_sha'], run_id=metadata['run_id'],
        run_attempt=metadata['run_attempt'], version=metadata['version'], experiment=metadata['experiment'],
        linux_baseline=metadata['linux_baseline'], now=datetime.fromisoformat(metadata['created_at'].replace('Z', '+00:00')),
        cmake_version=metadata['project_version'], schema=4, packager_sha=metadata['packager_sha'],
        repackaged_from=metadata.get('repackaged_from'))
    if checked != metadata:
        raise ValueError('Distribution release metadata differs from its canonical identity')
    rows, payloads = {}, {}
    for arch in ARCHES:
        for backend in BACKENDS:
            target = f'linux-{arch}-{backend}'
            name = release.application_names(metadata)[target]
            path = directory / name
            if not path.is_file() or path.is_symlink():
                raise ValueError(f'Missing or unsafe portable source: {name}')
            payloads[target] = apt.archive_files(path, metadata, target)
            data = path.read_bytes()
            with tarfile.open(path, 'r:gz') as stream:
                package_root = next(iter(stream)).name.removeprefix('./').split('/')[0]
            rows[target] = {'archive': name, 'package_root': package_root, 'size': len(data), 'sha256': digest(data),
                            'sha512': digest(data, 'sha512'), 'blake2b': digest(data, 'blake2b'),
                            'url': f'https://github.com/{repository}/releases/download/{metadata["tag"]}/{name}'}
    notices = notice_text(payloads)
    arch_files, gentoo_files = {}, {
        'profiles/repo_name': (b'datapump-bin\n', 0o644),
        'metadata/layout.conf': (b'masters = gentoo\nthin-manifests = true\nmanifest-hashes = BLAKE2B SHA256 SHA512\nmanifest-required-hashes = SHA256 SHA512\n', 0o644),
        'licenses/DataPump-Bundled': (notices, 0o644)}
    for backend in BACKENDS:
        extras = install_extras(payloads[f'linux-x86_64-{backend}'], backend)
        local = {**extras, 'DataPump-Bundled': (notices, 0o644)}
        arch_files.update({f'datapump-{backend}-bin/{name}': value for name, value in
                           arch_recipe(metadata, repository, backend, rows, local).items()})
        package = f'media-radio/datapump-{backend}-bin'
        gentoo_files.update({f'{package}/{name}': value for name, value in gentoo_recipe(metadata, repository, backend, rows).items()})
        gentoo_files.update({f'{package}/files/{name}': value for name, value in extras.items()})
    readme = (f'DataPump {metadata["tag"]}\n\nRelease: https://github.com/{repository}/releases/tag/{metadata["tag"]}\n'
              f'Application source: {metadata["source_sha"]}\nPackaging source: {metadata["packager_sha"]}\n'
              'These recipes install the published binaries without compiling the application or SDK.\n'
              'Use a glibc-based x86-64 or ARM64 system with a compatible desktop/audio service.\n'
              'Original libraries, licenses and notices remain under /opt/datapump/BACKEND.\n'
              'The bundled-notices license entry preserves existing terms; it grants no additional rights.\n'
              'Arch Linux officially supports x86-64; aarch64 recipes target Arch Linux ARM.\n'
              'Gentoo uses ~amd64/~arm64 testing keywords and a local overlay, not the Gentoo main repository.\n'
              f'Channel: {"experiment; not certified" if metadata["experiment"] else "regular; consult certification reports"}.\n').encode()
    arch_files['README.md'] = (readme + b'\nRun makepkg -si inside the selected datapump-BACKEND-bin directory.\n', 0o644)
    gentoo_files['README.md'] = (readme + b'\nRegister this directory as the datapump-bin local overlay, then emerge media-radio/datapump-BACKEND-bin.\n', 0o644)
    trees = {'arch': arch_files, 'gentoo': gentoo_files}
    manifest = {'schema': 1, 'repository': repository, 'tag': metadata['tag'],
                'metadata_identity_sha256': apt.metadata_identity(metadata), 'version': distro_version(metadata),
                'archives': rows, 'recipes': {kind: {name: {'sha256': digest(data), 'mode': mode}
                    for name, (data, mode) in sorted(files.items())} for kind, files in trees.items()}}
    return trees, manifest


def archive_bytes(kind, files):
    data = io.BytesIO()
    with tarfile.open(fileobj=data, mode='w', format=tarfile.PAX_FORMAT) as archive:
        for name, (contents, mode) in sorted(files.items()):
            entry = tarfile.TarInfo(f'{ROOTS[kind]}/{name}')
            entry.size, entry.mode, entry.mtime = len(contents), mode, 0
            entry.uid = entry.gid = 0
            archive.addfile(entry, io.BytesIO(contents))
    compressed = io.BytesIO()
    with gzip.GzipFile(filename='', mode='wb', fileobj=compressed, compresslevel=9, mtime=0) as stream:
        stream.write(data.getvalue())
    return compressed.getvalue()


def archive_name(kind):
    return ROOTS[kind] + '.tar.gz'


def build(directory, metadata, repository):
    directory = Path(directory)
    if any((directory / name).exists() or (directory / name).is_symlink() for name in ASSETS):
        raise ValueError('Refusing to overwrite distribution recipe assets')
    trees, manifest = expected(directory, metadata, repository)
    for kind, files in trees.items():
        (directory / archive_name(kind)).write_bytes(archive_bytes(kind, files))
    (directory / 'distro-packages.json').write_bytes(document(manifest))
    return verify(directory, metadata, repository)


def verify(directory, metadata, repository=None):
    directory = Path(directory)
    for name in ASSETS:
        if not (directory / name).is_file() or (directory / name).is_symlink():
            raise ValueError(f'Missing or unsafe distribution asset: {name}')
    recorded = json.loads((directory / 'distro-packages.json').read_bytes())
    repository = repository or recorded.get('repository')
    trees, manifest = expected(directory, metadata, repository)
    if recorded != manifest:
        raise ValueError('Distribution manifest does not match release identity or portable bytes')
    for kind, files in trees.items():
        # Compare complete tar content/metadata rather than zlib's compressed bytes:
        # packagers and consumers can use different Python/zlib versions.
        with tarfile.open(directory / archive_name(kind), 'r:gz') as archive:
            members = archive.getmembers()
            if archive.pax_headers or [m.name for m in members] != [f'{ROOTS[kind]}/{name}' for name in sorted(files)]:
                raise ValueError(f'Distribution recipe archive differs: {kind}')
            for member, (name, (data, mode)) in zip(members, sorted(files.items())):
                if (not member.isfile() or member.uid != 0 or member.gid != 0 or member.uname or member.gname
                        or member.mode != mode or member.mtime != 0 or member.size != len(data)
                        or set(member.pax_headers) - {'path'} or member.linkname
                        or archive.extractfile(member).read() != data):
                    raise ValueError(f'Distribution recipe archive differs: {kind}/{name}')
    return manifest


def extract(directory, metadata, kind, destination, repository=None):
    verify(directory, metadata, repository)
    if kind not in ROOTS:
        raise ValueError('Unknown distribution recipe kind')
    destination = Path(destination)
    if destination.exists() or destination.is_symlink():
        raise ValueError('Refusing to overwrite an extraction destination')
    destination.mkdir(parents=True, mode=0o755)
    with tarfile.open(Path(directory) / archive_name(kind), 'r:gz') as archive:
        for member in archive:
            path = destination / member.name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(archive.extractfile(member).read())
            path.chmod(member.mode)
    for path in destination.rglob('*'):
        if path.is_dir():
            path.chmod(0o755)
    destination.chmod(0o755)
    return destination / ROOTS[kind]


def verify_installed(directory, metadata, backend, architecture, root=Path('/'), repository=None):
    verify(directory, metadata, repository)
    architecture = {'amd64': 'x86_64', 'arm64': 'aarch64'}.get(architecture, architecture)
    if backend not in BACKENDS or architecture not in ARCHES:
        raise ValueError('Unknown installed backend or architecture')
    apt = apt_module()
    release = apt.release_module()
    target = f'linux-{architecture}-{backend}'
    payload = apt.archive_files(Path(directory) / release.application_names(metadata)[target], metadata, target)
    expected_files = apt.package_files(payload, backend)
    root = Path(root)
    for name, (data, mode) in expected_files.items():
        path = root / name
        if (not path.is_file() or path.is_symlink() or path.read_bytes() != data
                or stat.S_IMODE(path.stat().st_mode) != mode):
            raise ValueError(f'Installed distribution payload differs: {name}')
    installed = root / 'opt/datapump' / backend
    actual = {path.relative_to(root).as_posix() for path in installed.rglob('*') if not path.is_dir()}
    if actual != {name for name in expected_files if name.startswith('opt/')}:
        raise ValueError('Installed bundle contains unexpected or missing files')
    return {'backend': backend, 'architecture': architecture, 'files': len(expected_files)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('build', 'verify', 'extract', 'verify-installed'))
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--repository')
    parser.add_argument('--kind', choices=tuple(ROOTS))
    parser.add_argument('--destination', type=Path)
    parser.add_argument('--backend', choices=BACKENDS)
    parser.add_argument('--architecture', choices=(*ARCHES, *ARCHES.values()))
    parser.add_argument('--root', type=Path, default=Path('/'))
    args = parser.parse_args(argv)
    try:
        metadata = apt_module().release_module().load_metadata(args.metadata)
        if args.command == 'build':
            result = build(args.directory, metadata, args.repository)
        elif args.command == 'verify':
            result = verify(args.directory, metadata, args.repository)
        elif args.command == 'extract':
            if not args.kind or not args.destination:
                raise ValueError('extract requires --kind and --destination')
            result = {'path': str(extract(args.directory, metadata, args.kind, args.destination, args.repository))}
        else:
            result = verify_installed(args.directory, metadata, args.backend, args.architecture, args.root, args.repository)
        print(json.dumps(result, sort_keys=True))
    except (OSError, ValueError, KeyError, tarfile.TarError) as error:
        parser.exit(1, f'distro-release: {error}\n')


if __name__ == '__main__':
    main()
