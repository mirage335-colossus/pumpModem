#!/usr/bin/env python3
"""Preserve and reuse a relocatable Windows vcpkg dependency export in base.

The runner supplies Visual Studio and the Windows SDK. These archives contain
only the raw vcpkg export and its preserved recipe/downloads, not that toolchain.
"""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
RECIPE = ROOT / 'third_party/build-support/windows-base.json'
SPEC = importlib.util.spec_from_file_location('datapump_windows_release', ROOT / 'tools/release.py')
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)
download_asset = release.download_asset
DIGEST = re.compile(r'[0-9a-f]{64}')
VERSION = re.compile(r'[0-9]+(?:\.[0-9]+){1,4}')
METADATA_ROOT = 'share/datapump-windows-base/'
MANIFEST = METADATA_ROOT + 'manifest.json'
TOOLCHAIN = 'scripts/buildsystems/vcpkg.cmake'
ARCHIVE_SUFFIXES = ('.zip', '.tar.gz', '.tar.xz', '.tar.bz2', '.tar.zst', '.tar', '.tgz', '.tbz2', '.txz', '.7z')


def recipe_files():
    return {'tools/windows-base.py': Path(__file__).read_bytes(),
            'third_party/build-support/windows-base.json': RECIPE.read_bytes()}


def recipe_identity():
    files = recipe_files()
    recipe = json.loads(files['third_party/build-support/windows-base.json'])
    if (recipe.get('schema') != 1
            or not re.fullmatch(r'[0-9a-f]{40}', recipe.get('vcpkg_ref', ''))
            or recipe.get('triplet') != 'x64-windows-static'
            or set(recipe.get('ports', [])) != {'openssl', 'glew', 'freetype[core]'}
            or len(recipe['ports']) != 3 or recipe.get('toolset') != 'v143'
            or recipe.get('configurations') != ['Debug', 'Release']
            or recipe.get('crt_linkage') != 'static' or recipe.get('library_linkage') != 'static'
            or recipe.get('lto') is not False):
        raise ValueError('Unsupported Windows base recipe')
    hashes = {name: hashlib.sha256(data).hexdigest() for name, data in files.items()}
    identity = hashlib.sha256(json.dumps(hashes, sort_keys=True, separators=(',', ':')).encode()).hexdigest()[:20]
    return recipe, identity, hashes


def names(identity):
    if not re.fullmatch(r'[0-9a-f]{20}', identity):
        raise ValueError('Invalid Windows base recipe identity')
    return (f'windows-base-{identity}-x64-windows-static.zip',
            f'windows-base-sources-{identity}.zip', f'windows-base-{identity}-SHA256SUMS.txt')


def identity_values():
    recipe, identity, _ = recipe_identity()
    return {'recipe_id': identity, 'vcpkg_ref': recipe['vcpkg_ref'], 'triplet': recipe['triplet'],
            'archive': names(identity)[0], 'source_archive': names(identity)[1]}


def valid_path(name):
    path = PurePosixPath(name)
    if (not name or path.is_absolute() or str(path) != name or '\\' in name
            or any(ord(character) < 32 for character in name)
            or any(part in ('.', '..') or ':' in part or part.endswith((' ', '.'))
                   or re.fullmatch(r'(?:con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\..*)?', part, re.I)
                   for part in path.parts)):
        raise ValueError(f'Unsafe archive path: {name!r}')
    return name


def version_tuple(value):
    if not isinstance(value, str) or not VERSION.fullmatch(value):
        raise ValueError('Expected a numeric Visual Studio tool version')
    parts = tuple(int(part) for part in value.split('.'))
    return parts + (0,) * (5 - len(parts))


def validate_provenance(value):
    required = {'source_sha', 'runner_image', 'toolset_version', 'compiler_version', 'linker_version'}
    if (not isinstance(value, dict) or set(value) != required
            or not isinstance(value['source_sha'], str)
            or not re.fullmatch(r'[0-9a-f]{40}|[0-9a-f]{64}', value['source_sha'])
            or not isinstance(value['runner_image'], str)
            or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_. /()+-]{0,159}', value['runner_image'])):
        raise ValueError('Invalid Windows base build provenance')
    for key in ('toolset_version', 'compiler_version', 'linker_version'):
        version_tuple(value[key])
    return value


def checked_files(directory):
    if not directory.is_dir() or directory.is_symlink():
        raise ValueError(f'Missing or unsafe input directory: {directory}')
    files = {}
    folded = set()
    for path in sorted(directory.rglob('*')):
        if path.is_symlink() or not (path.is_dir() or path.is_file()):
            raise ValueError(f'Input contains a non-ordinary file: {path}')
        relative = valid_path(path.relative_to(directory).as_posix())
        if relative.casefold() in folded:
            raise ValueError('Case-insensitive Windows path collision')
        folded.add(relative.casefold())
        if path.is_file():
            files[relative] = path
    return files


def json_bytes(value):
    return (json.dumps(value, sort_keys=True, indent=2) + '\n').encode()


def write_archive(path, root, files, *, compressed=True):
    compression = zipfile.ZIP_DEFLATED if compressed else zipfile.ZIP_STORED
    with zipfile.ZipFile(path, 'x', compression=compression, allowZip64=True) as archive:
        for relative, data in sorted(files.items()):
            info = zipfile.ZipInfo(f'{root}/{valid_path(relative)}', (1980, 1, 1, 0, 0, 0))
            info.compress_type = compression
            info.external_attr = 0o100644 << 16
            with archive.open(info, 'w', force_zip64=True) as destination:
                if isinstance(data, bytes):
                    destination.write(data)
                else:
                    with data.open('rb') as source:
                        shutil.copyfileobj(source, destination)


def verify_export_inventory(files):
    required = {TOOLCHAIN, '.vcpkg-root'} | {
        f'installed/x64-windows-static/share/{port}/copyright' for port in ('openssl', 'glew', 'freetype')}
    # vcpkg export --raw copies package listfiles, not the installation status database.
    if not required <= set(files) or any(not any(re.fullmatch(
            rf'installed/vcpkg/info/{port}_[^/]+_x64-windows-static\.list', name)
            for name in files) for port in ('openssl', 'glew', 'freetype')):
        raise ValueError('Raw export must contain the vcpkg toolchain and installed static dependencies')


def assemble(export_root, sources_dir, directory, provenance_path):
    recipe, identity, recipe_hashes = recipe_identity()
    provenance = validate_provenance(json.loads(provenance_path.read_text(encoding='utf-8-sig')))
    exports = checked_files(export_root)
    verify_export_inventory(exports)
    if any(name.startswith(METADATA_ROOT) for name in exports):
        raise ValueError('Raw export contains reserved Windows base metadata')
    # Preserve ordinary downloaded archives, including archived bootstrap tools
    # if present. Do not claim this is an upstream-source-only collection.
    inputs = checked_files(sources_dir)
    downloads = {'downloads/' + name: path for name, path in inputs.items()
                 if '/' not in name and name.lower().endswith(ARCHIVE_SUFFIXES)}
    if 'downloads/vcpkg-source.zip' not in downloads or len(downloads) < 2:
        raise ValueError('Preservation requires vcpkg-source.zip and downloaded dependency archives')
    with zipfile.ZipFile(downloads['downloads/vcpkg-source.zip']) as vcpkg_source:
        if vcpkg_source.comment != recipe['vcpkg_ref'].encode():
            raise ValueError('Preserved vcpkg-source.zip must be git archive of the pinned vcpkg commit')
    manifest = {'schema': 1, 'recipe_id': identity, 'recipe': recipe,
                'recipe_files': recipe_hashes, 'provenance': provenance,
                'files': {name: release.digest(path) for name, path in exports.items()},
                'downloads': {name: release.digest(path) for name, path in downloads.items()}}
    if directory.exists() or directory.is_symlink():
        raise ValueError('Refusing to replace an existing Windows base staging directory')
    directory.parent.mkdir(parents=True, exist_ok=True)
    binary, source, _ = names(identity)
    metadata = {METADATA_ROOT + name: data for name, data in recipe_files().items()}
    metadata[MANIFEST] = json_bytes(manifest)
    with tempfile.TemporaryDirectory(prefix='.windows-base-', dir=directory.parent) as temporary:
        staged = Path(temporary) / 'archives'
        staged.mkdir()
        write_archive(staged / binary, binary.removesuffix('.zip'), {**exports, **metadata})
        write_archive(staged / source, source.removesuffix('.zip'), {**downloads, **metadata}, compressed=False)
        (staged / 'SHA256SUMS').write_text(''.join(
            f'{release.digest(staged / name)}  {name}\n' for name in (binary, source)), encoding='utf-8')
        validate(staged, binary_only=False)
        staged.rename(directory)
    return {'recipe_id': identity, 'archive': str((directory / binary).resolve()),
            'source_archive': str((directory / source).resolve()), **provenance}


def archive_inventory(path, root):
    hashes, small, seen = {}, {}, set()
    with zipfile.ZipFile(path) as archive:
        for member in archive.infolist():
            valid_path(member.filename)
            if not member.filename.startswith(root + '/'):
                raise ValueError('Unexpected Windows base archive root')
            relative = valid_path(member.filename[len(root) + 1:])
            kind = (member.external_attr >> 16) & 0o170000
            if member.is_dir() or kind not in (0, 0o100000) or member.flag_bits & 1:
                raise ValueError('Windows base ZIP must contain only unencrypted regular files')
            if relative.casefold() in seen:
                raise ValueError('Duplicate or case-insensitive Windows archive path')
            seen.add(relative.casefold())
            digest = hashlib.sha256()
            is_metadata = relative.startswith(METADATA_ROOT)
            if is_metadata and member.file_size > 16 * 1024 * 1024:
                raise ValueError('Windows base metadata is too large')
            with archive.open(member) as stream:
                if is_metadata:
                    data = stream.read()
                    small[relative] = data
                    digest.update(data)
                else:
                    for block in iter(lambda: stream.read(1024 * 1024), b''):
                        digest.update(block)
            hashes[relative] = digest.hexdigest()
    return hashes, small


def verify_archive(path, identity, *, source=False):
    recipe, current_identity, recipe_hashes = recipe_identity()
    if identity != current_identity:
        raise ValueError('Windows base identity differs from this checkout')
    hashes, small = archive_inventory(path, path.stem)
    expected_recipe = {METADATA_ROOT + name: data for name, data in recipe_files().items()}
    if set(small) != set(expected_recipe) | {MANIFEST} or any(small[name] != data for name, data in expected_recipe.items()):
        raise ValueError('Windows base preserved recipe/helper mismatch')
    manifest = json.loads(small[MANIFEST])
    if (set(manifest) != {'schema', 'recipe_id', 'recipe', 'recipe_files', 'provenance', 'files', 'downloads'}
            or manifest['schema'] != 1 or manifest['recipe_id'] != identity
            or manifest['recipe'] != recipe or manifest['recipe_files'] != recipe_hashes):
        raise ValueError('Windows base manifest recipe mismatch')
    validate_provenance(manifest['provenance'])
    for key in ('files', 'downloads'):
        entries = manifest[key]
        if not isinstance(entries, dict) or not entries:
            raise ValueError('Windows base file inventory is missing')
        for name, value in entries.items():
            valid_path(name)
            if (name.startswith(METADATA_ROOT) or not isinstance(value, str) or not DIGEST.fullmatch(value)
                    or (key == 'downloads' and not name.startswith('downloads/'))):
                raise ValueError('Invalid Windows base file inventory')
    verify_export_inventory(manifest['files'])
    if 'downloads/vcpkg-source.zip' not in manifest['downloads']:
        raise ValueError('Windows base is missing its toolchain or preserved vcpkg source')
    expected = manifest['downloads' if source else 'files']
    actual = {name: value for name, value in hashes.items() if name not in small}
    if actual != expected:
        raise ValueError('Windows base internal file checksum mismatch')
    return manifest


def validate(directory, *, binary_only=True):
    _, identity, _ = recipe_identity()
    binary, source, _ = names(identity)
    present = {binary} if binary_only else {binary, source}
    if not directory.is_dir() or directory.is_symlink():
        raise ValueError('Missing or unsafe Windows base archive directory')
    entries = list(directory.iterdir())
    if (any(path.is_symlink() or not path.is_file() for path in entries)
            or {path.name for path in entries} != present | {'SHA256SUMS'}):
        raise ValueError('Windows base stage must contain the exact archives and SHA256SUMS')
    sums = {}
    for line in (directory / 'SHA256SUMS').read_text(encoding='utf-8').splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  ([A-Za-z0-9][A-Za-z0-9_.-]*)', line)
        if not match or match[2] in sums:
            raise ValueError('Invalid Windows base checksum inventory')
        sums[match[2]] = match[1]
    if set(sums) != {binary, source}:
        raise ValueError('Windows base checksum inventory must identify the exact recipe pair')
    for name in present:
        if release.digest(directory / name) != sums[name]:
            raise ValueError(f'Windows base archive checksum mismatch: {name}')
    manifest = verify_archive(directory / binary, identity)
    if not binary_only and verify_archive(directory / source, identity, source=True) != manifest:
        raise ValueError('Windows base binary/source provenance mismatch')
    return manifest, sums


def has_pair(info, identity):
    if info is None:
        return False
    counts = Counter(asset['name'] for asset in info['assets'])
    expected = names(identity)
    if not any(counts[name] for name in expected):
        return False
    if any(counts[name] != 1 for name in expected):
        raise ValueError('Partial or duplicate Windows base assets require inspection; refusing to overwrite')
    return True


def release_info(repository):
    release.repository_name(repository)
    release.gh(['api', f'repos/{repository}'])  # Repository visibility before interpreting a 404.
    response = release.gh(['api', '--include', f'repos/{repository}/releases/tags/base'], check=False)
    text = response.stdout.replace('\r\n', '\n')
    status = re.match(r'HTTP/\S+ ([0-9]{3})(?:\s|$)', text)
    if status and status[1] == '404':
        # The tag endpoint omits drafts; an unfinished base is not a cache miss.
        matches = [item for item in release.api_pages(f'repos/{repository}/releases?per_page=100')
                   if item.get('tag_name') == 'base']
        if len(matches) > 1:
            raise ValueError('Multiple base release drafts require inspection')
        if not matches:
            return None
        info = matches[0]
    elif not status or status[1] != '200' or response.returncode:
        raise RuntimeError(f'Cannot read the base release: {response.stderr.strip()}')
    else:
        info = json.loads(text.split('\n\n', 1)[1])
    if type(info.get('id')) is not int or info['id'] <= 0:
        raise ValueError('Malformed base release response')
    info['assets'] = release.api_pages(f'repos/{repository}/releases/{info["id"]}/assets?per_page=100')
    return info


def require_published_base(info):
    if info is None or info.get('draft') or info.get('name') != 'base' or not info.get('prerelease'):
        raise ValueError('A published prerelease named base is required; finish base maintenance first')


def download_pair(repository, directory, identity, info, *, binary_only=True):
    directory.mkdir()
    binary, source, checksum = names(identity)
    assets = {asset['name']: asset for asset in info['assets']}
    for name in ((binary, checksum) if binary_only else (binary, source, checksum)):
        download_asset(repository, assets[name], directory / name)
    (directory / checksum).rename(directory / 'SHA256SUMS')
    return validate(directory, binary_only=binary_only)


def fetch(repository, directory, *, with_sources=False):
    _, identity, _ = recipe_identity()
    info = release_info(repository)
    if not has_pair(info, identity):
        if info is not None:
            require_published_base(info)
        return {'found': False, 'recipe_id': identity}
    require_published_base(info)
    if directory.exists() or directory.is_symlink():
        raise ValueError('Refusing to replace an existing Windows base archive directory')
    directory.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.windows-base-fetch-', dir=directory.parent) as temporary:
        staged = Path(temporary) / 'archives'
        manifest, _ = download_pair(repository, staged, identity, info, binary_only=not with_sources)
        staged.rename(directory)
    return {'found': True, 'recipe_id': identity, 'archive': str((directory / names(identity)[0]).resolve()),
            **manifest['provenance']}


def publish(repository, directory):
    _, identity, _ = recipe_identity()
    _, local = validate(directory, binary_only=False)
    info = release_info(repository)
    require_published_base(info)
    if has_pair(info, identity):
        with tempfile.TemporaryDirectory(prefix='windows-base-compare-') as temporary:
            _, remote = download_pair(repository, Path(temporary) / 'archives', identity, info, binary_only=False)
        if local != remote:
            raise ValueError('Immutable Windows base assets differ; refusing to overwrite')
        return {'published': False, 'reused': True, 'recipe_id': identity}
    binary, source, checksum = names(identity)
    release.gh(['release', 'upload', 'base', '--repo', repository, str(directory / binary), str(directory / source)])
    with tempfile.TemporaryDirectory(prefix='windows-base-sums-') as temporary:
        path = Path(temporary) / checksum
        shutil.copyfile(directory / 'SHA256SUMS', path)
        release.gh(['release', 'upload', 'base', '--repo', repository, str(path)])
    release.gh(['release', 'edit', 'base', '--repo', repository, '--title', 'base', '--prerelease', '--latest=false'])
    return {'published': True, 'reused': False, 'recipe_id': identity}


def install(directory, destination, *, linker_version=None):
    _, identity, _ = recipe_identity()
    binary, source, _ = names(identity)
    manifest, _ = validate(directory, binary_only=not (directory / source).exists())
    if linker_version is not None and version_tuple(linker_version) < version_tuple(manifest['provenance']['linker_version']):
        raise ValueError('Consumer linker is older than the Windows base producer; use the same or newer Visual Studio toolset')
    if not destination.is_absolute() or destination.exists() or destination.is_symlink():
        raise ValueError('Windows base destination must be a new absolute path')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.windows-base-install-', dir=destination.parent) as temporary:
        staged = Path(temporary) / 'export'
        staged.mkdir()
        root = binary.removesuffix('.zip') + '/'
        with zipfile.ZipFile(directory / binary) as archive:
            # validate() has already rejected links, duplicates and unsafe paths.
            for member in archive.infolist():
                path = staged / member.filename.removeprefix(root)
                path.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(member) as source_stream, path.open('xb') as output:
                    shutil.copyfileobj(source_stream, output)
        staged.rename(destination)
    return {'recipe_id': identity, 'toolchain_file': str(destination / TOOLCHAIN),
            **manifest['provenance']}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('identity', 'assemble', 'fetch', 'publish', 'install'):
        command = commands.add_parser(name)
        command.add_argument('--github-output', type=Path)
        if name != 'identity':
            command.add_argument('--directory', type=Path, required=True)
        if name in ('fetch', 'publish'):
            command.add_argument('--repo', required=True)
        if name == 'assemble':
            command.add_argument('--export-root', type=Path, required=True)
            command.add_argument('--sources-dir', type=Path, required=True)
            command.add_argument('--provenance', type=Path, required=True)
        if name == 'fetch':
            command.add_argument('--with-sources', action='store_true')
        if name == 'install':
            command.add_argument('--destination', type=Path, required=True)
            command.add_argument('--linker-version')
    args = parser.parse_args(argv)
    try:
        if args.command == 'identity':
            value = identity_values()
        elif args.command == 'assemble':
            value = assemble(args.export_root, args.sources_dir, args.directory, args.provenance)
        elif args.command == 'fetch':
            value = fetch(args.repo, args.directory, with_sources=args.with_sources)
        elif args.command == 'publish':
            value = publish(args.repo, args.directory)
        else:
            value = install(args.directory, args.destination, linker_version=args.linker_version)
        if args.github_output:
            with args.github_output.open('a', encoding='utf-8') as output:
                for key, item in value.items():
                    output.write(f'{key}={str(item).lower() if isinstance(item, bool) else item}\n')
    except (ValueError, KeyError, TypeError, OSError, RuntimeError, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        parser.exit(1, f'windows-base: {error}\n')
    print(json.dumps(value, sort_keys=True))


if __name__ == '__main__':
    main()
