#!/usr/bin/env python3
"""Archive, install and reuse exact prepared Wasm SDK recipes from immutable base assets."""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
import posixpath
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile
import tempfile
import sys
sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


sdk = load('datapump_wasm_builder', 'build-wasm-sdk.py')
base = load('datapump_wasm_base_transport', 'sdk-release.py')
gh = base.gh
release_info = base.release_info
download_asset = base.release.download_asset
META = 'share/datapump-wasm-sdk/'


def identity():
    return sdk.recipe_id()


def recipe_files():
    return sdk.recipe_files()


def names(recipe_id):
    if not re.fullmatch(r'[0-9a-f]{20}', recipe_id):
        raise ValueError('Invalid Wasm SDK recipe identity')
    return (f'datapump-wasm-sdk-{recipe_id}-linux-x86_64.tar.gz',
            f'datapump-wasm-sdk-sources-{recipe_id}.tar.gz',
            f'wasm-sdk-{recipe_id}-SHA256SUMS.txt')


def safe_name(name):
    path = PurePosixPath(name)
    if not name or str(path) != name or path.is_absolute() or '..' in path.parts or '\\' in name:
        raise ValueError(f'Unsafe Wasm SDK archive path: {name!r}')
    return name


def checked_tree(root):
    if root.is_symlink() or not root.is_dir():
        raise ValueError('Missing or linked prepared SDK directory')
    result = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink():
            resolved = path.resolve(strict=True)
            if not resolved.is_relative_to(root.resolve()) or not resolved.is_file():
                raise ValueError(f'Escaping or unsupported SDK link: {path}')
        elif path.is_dir():
            continue
        if not path.is_file():
            raise ValueError(f'Unsupported SDK file: {path}')
        result[safe_name(path.relative_to(root).as_posix())] = path
    return result


def check_metadata(read, hashes):
    """Validate retained recipe, sources, preparation and mandatory target inputs."""
    value = sdk.recipe()
    for name, data in recipe_files().items():
        if read(META + 'recipe/' + name) != data:
            raise ValueError('Preserved Wasm SDK recipe differs from this checkout')
    if json.loads(read(META + 'recipe.json')) != value:
        raise ValueError('Preserved Wasm SDK manifest recipe differs from this checkout')
    manifest = json.loads(read(META + 'manifest.json'))
    if (manifest.get('schema_version') != 1 or manifest.get('recipe_id') != identity()
            or manifest.get('recipe_sha256') != sdk.digest(sdk.RECIPE)
            or manifest.get('host') != 'linux-x86_64'
            or manifest.get('target') != 'wasm32-emscripten'
            or manifest.get('emscripten_version') != value['emscripten_version']
            or manifest.get('entropy_patch_sha256') != value['entropy']['patch_sha256']
            or manifest.get('entropy_capability') != sdk.ENTROPY_CAPABILITY
            or manifest.get('entropy_probe') != 'passed' or manifest.get('cache_frozen') is not True
            or manifest.get('sources') != value['inputs']):
        raise ValueError('Prepared Wasm SDK metadata does not match the qualified recipe')
    provenance = json.loads(read(META + 'preparation-provenance.json'))
    support = [value['entropy']['patch'], *value['entropy']['probe_sources']]
    support_hashes = {name: sdk.digest(sdk.RECIPE.parent / name) for name in support}
    if (provenance.get('builder_sha256') != sdk.digest(ROOT / 'tools/build-wasm-sdk.py')
            or provenance.get('recipe_sha256') != sdk.digest(sdk.RECIPE)
            or provenance.get('support_sha256') != support_hashes
            or provenance.get('entropy_probe') != 'passed'
            or manifest.get('support_sha256') != support_hashes):
        raise ValueError('Wasm SDK preparation provenance differs from the current recipe')
    if (hashes.get(META + 'build-wasm-sdk.py') != provenance['builder_sha256']
            or any(hashes.get(META + name) != expected for name, expected in support_hashes.items())):
        raise ValueError('Preserved Wasm SDK builder or support checksum mismatch')
    required_notices = {*sdk.RUNTIME_NOTICES, 'OpenSSL-LICENSE.txt', 'dlmalloc-NOTICE.txt'}
    if any(META + 'licenses/' + name not in hashes for name in required_notices):
        raise ValueError('Missing Wasm SDK runtime license notices')
    required = {'compiler': 'emsdk/upstream/emscripten/emcc',
                'cxx_compiler': 'emsdk/upstream/emscripten/em++',
                'toolchain': 'emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake',
                'openssl_crypto': 'target/lib/libcrypto.a'}
    for field, name in required.items():
        if manifest.get(field) != name or name not in hashes:
            raise ValueError(f'Missing or unexpected Wasm SDK {field}')
    if (manifest.get('openssl_include') != 'target/include'
            or 'target/include/openssl/ssl.h' not in hashes
            or 'node/bin/node' not in hashes or '.emscripten' not in hashes
            or not any(name.startswith('cache/sysroot/') for name in hashes)
            or not any(name.startswith(META + 'licenses/') for name in hashes)):
        raise ValueError('Incomplete Wasm SDK target, cache, host tools or notices')
    if hashes[required['openssl_crypto']] != manifest.get('openssl_crypto_sha256'):
        raise ValueError('Wasm SDK OpenSSL checksum mismatch')
    for item in value['inputs']:
        if hashes.get(META + 'sources/' + item['file']) != item['sha256']:
            raise ValueError('Preserved Wasm SDK source checksum mismatch')
    return manifest


def inspect_archive(path, expected_root, *, source=False):
    hashes, small, links = {}, {}, {}
    with tarfile.open(path, 'r|gz') as archive:
        for member in archive:
            safe_name(member.name)
            prefix = expected_root + '/'
            if not member.name.startswith(prefix) or not (member.isfile() or member.issym()):
                raise ValueError('SDK archives must contain files inside the exact recipe root')
            relative = member.name[len(prefix):]
            safe_name(relative)
            if relative in hashes or relative in links:
                raise ValueError('Duplicate Wasm SDK archive member')
            if member.issym():
                target = posixpath.normpath(posixpath.join(posixpath.dirname(relative), member.linkname))
                if source or member.linkname.startswith('/') or '\\' in member.linkname or target.startswith('../') or target == '..':
                    raise ValueError('Escaping or unsupported Wasm SDK archive link')
                links[relative] = safe_name(target)
                continue
            keep = relative.startswith(META) and not relative.startswith(META + 'sources/')
            if keep and member.size > 16 * 1024 * 1024:
                raise ValueError('Oversized Wasm SDK metadata')
            content = bytearray()
            with archive.extractfile(member) as stream:
                digest = hashlib.sha256()
                for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                    digest.update(chunk)
                    if keep:
                        content.extend(chunk)
                hashes[relative] = digest.hexdigest()
            if keep:
                small[relative] = bytes(content)
    for name in set(hashes) | set(links):
        if any(parent.as_posix() in links for parent in PurePosixPath(name).parents):
            raise ValueError('Archive writes through a linked directory')
    for name in links:
        current, visited = name, set()
        while current in links:
            if current in visited:
                raise ValueError('Cyclic Wasm SDK archive link')
            visited.add(current)
            current = links[current]
        if current not in hashes:
            raise ValueError('Wasm SDK archive link has no regular target')
        hashes[name] = hashes[current]
    if source:
        expected = {META + 'recipe/' + name: hashlib.sha256(data).hexdigest()
                    for name, data in recipe_files().items()}
        expected.update({META + 'sources/' + item['file']: item['sha256'] for item in sdk.recipe()['inputs']})
        if hashes != expected:
            raise ValueError('Preserved Wasm SDK source archive differs from exact recipe and inputs')
    else:
        check_metadata(lambda name: small[name], hashes)
    return hashes


def validate(directory, binary_only=False):
    binary, source, _ = names(identity())
    expected = {binary, source}
    present = {binary} if binary_only else expected
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError('Missing or unsafe Wasm SDK archive directory')
    entries = list(directory.iterdir())
    if any(path.is_symlink() or not path.is_file() for path in entries) or {p.name for p in entries} != present | {'SHA256SUMS'}:
        raise ValueError('Wasm SDK inventory must contain the exact recipe archives and SHA256SUMS')
    sums = {}
    for line in (directory / 'SHA256SUMS').read_text().splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  ([A-Za-z0-9_.-]+)', line)
        if not match or match[2] in sums:
            raise ValueError('Invalid or duplicate Wasm SDK checksum entry')
        sums[match[2]] = match[1]
    if set(sums) != expected:
        raise ValueError('Wasm SDK checksum inventory does not match exact recipe pair')
    for name in present:
        if sdk.digest(directory / name) != sums[name]:
            raise ValueError('Wasm SDK archive checksum mismatch')
        inspect_archive(directory / name, name.removesuffix('.tar.gz'), source=name == source)
    return sums


def write_archive(path, files):
    with tarfile.open(path, 'x:gz', dereference=False) as archive:
        root = path.name.removesuffix('.tar.gz')
        for name, source in sorted(files.items()):
            info = archive.gettarinfo(str(source), root + '/' + name)
            info.uid = info.gid = info.mtime = 0
            info.uname = info.gname = ''
            if info.issym():
                archive.addfile(info)
            else:
                with source.open('rb') as stream:
                    archive.addfile(info, stream)


def package(sdk_root, directory):
    files = checked_tree(sdk_root)
    hashes = {name: sdk.digest(path) for name, path in files.items()}
    check_metadata(lambda name: files[name].read_bytes(), hashes)
    if directory.exists() or directory.is_symlink():
        raise ValueError('Refusing to replace an existing SDK archive directory')
    directory.parent.mkdir(parents=True, exist_ok=True)
    binary, source, _ = names(identity())
    with tempfile.TemporaryDirectory(prefix='.wasm-package-', dir=directory.parent) as temporary:
        staged = Path(temporary) / 'archives'; staged.mkdir()
        write_archive(staged / binary, files)
        write_archive(staged / source, {name: path for name, path in files.items()
                      if name.startswith((META + 'sources/', META + 'recipe/'))})
        (staged / 'SHA256SUMS').write_text(''.join(f'{sdk.digest(staged / name)}  {name}\n' for name in (binary, source)))
        validate(staged)
        staged.rename(directory)
    return {'recipe_id': identity(), 'archive': binary, 'source_archive': source}


def install(directory, destination):
    binary, source, _ = names(identity())
    validate(directory, binary_only=not (directory / source).exists())
    if destination.exists() or destination.is_symlink():
        raise ValueError('Refusing to overwrite an SDK; select an empty destination')
    if sdk.platform.system() != 'Linux' or sdk.platform.machine() != 'x86_64':
        raise ValueError('This pinned SDK host supports Linux x86_64 only')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.wasm-install-', dir=destination.parent) as temporary:
        staged = Path(temporary)
        # validate has rejected escaping links and special/duplicate/traversing members.
        sdk.extract(directory / binary, staged)
        (staged / binary.removesuffix('.tar.gz')).rename(destination)
        try:
            (destination / META / 'relocated-root.txt').write_text(str(destination.resolve()) + '\n')
            env = sdk.configure_environment(destination.resolve()) | {'EM_FROZEN_CACHE': '1'}
            sdk.run([destination / 'emsdk/upstream/emscripten/emcc', '--version'], env=env)
            sdk.qualify_entropy(destination / 'emsdk/upstream/emscripten', destination / 'node/bin/node',
                                destination / 'target', staged, env)
        except BaseException:
            shutil.rmtree(destination)
            raise
    return {'installed': True, 'recipe_id': identity(), 'destination': str(destination.resolve())}


def has_pair(info):
    if info is None:
        return False
    counts = Counter(asset['name'] for asset in info['assets'])
    expected = names(identity())
    if not any(counts[name] for name in expected):
        return False
    if any(counts[name] != 1 for name in expected):
        raise ValueError('Partial or duplicate Wasm SDK assets on base require explicit maintenance')
    return True


def download(repository, directory, info, binary_only=False):
    directory.mkdir()
    binary, source, checksum = names(identity())
    inventory = {asset['name']: asset for asset in info['assets']}
    for name in ((binary, checksum) if binary_only else (binary, source, checksum)):
        download_asset(repository, inventory[name], directory / name)
    (directory / checksum).rename(directory / 'SHA256SUMS')
    return validate(directory, binary_only)


def fetch(repository, directory, binary_only=False):
    info = release_info(repository)
    if not has_pair(info):
        return {'found': False, 'recipe_id': identity()}
    if info.get('draft'):
        raise ValueError('The base release is still a draft; finish publication before reuse')
    if directory.is_symlink() or (directory.exists() and (not directory.is_dir() or any(directory.iterdir()))):
        raise ValueError('Refusing to replace an existing SDK archive directory')
    directory.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.wasm-fetch-', dir=directory.parent) as temporary:
        staged = Path(temporary) / 'archives'
        download(repository, staged, info, binary_only)
        if directory.exists():
            directory.rmdir()
        staged.rename(directory)
    return {'found': True, 'recipe_id': identity()}


def publish(repository, directory, source_sha):
    if not re.fullmatch(r'[0-9a-f]{40}|[0-9a-f]{64}', source_sha):
        raise ValueError('Source SHA must be a complete lowercase Git object ID')
    local = validate(directory)
    info = release_info(repository)
    if has_pair(info):
        with tempfile.TemporaryDirectory(prefix='wasm-base-compare-') as temporary:
            remote = download(repository, Path(temporary) / 'archives', info)
        if remote != local:
            raise ValueError('Immutable Wasm SDK assets differ; refusing to overwrite')
        reused = True
    else:
        if info is None:
            gh(['release', 'create', 'base', '--repo', repository, '--target', source_sha,
                '--title', 'base', '--notes', 'Reusable DataPump SDK recipes and preserved sources.',
                '--draft', '--prerelease', '--latest=false'])
        binary, source, checksum = names(identity())
        gh(['release', 'upload', 'base', '--repo', repository, str(directory / binary), str(directory / source)])
        with tempfile.TemporaryDirectory(prefix='wasm-base-sums-') as temporary:
            path = Path(temporary) / checksum
            path.write_bytes((directory / 'SHA256SUMS').read_bytes())
            gh(['release', 'upload', 'base', '--repo', repository, str(path)])
        reused = False
    gh(['release', 'edit', 'base', '--repo', repository, '--title', 'base', '--draft=false', '--prerelease', '--latest=false'])
    return {'published': not reused, 'reused': reused, 'recipe_id': identity()}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('id')
    for name in ('archive', 'install', 'verify', 'fetch', 'publish'):
        command = commands.add_parser(name)
        command.add_argument('--directory', type=Path, required=True)
        if name == 'archive':
            command.add_argument('--sdk', type=Path, required=True)
        if name == 'install':
            command.add_argument('--destination', type=Path, required=True)
        if name in ('fetch', 'publish'):
            command.add_argument('--repo', required=True)
        if name in ('fetch', 'verify'):
            command.add_argument('--binary-only', action='store_true')
        if name == 'fetch':
            command.add_argument('--require', action='store_true')
        if name == 'publish':
            command.add_argument('--source-sha', required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == 'id':
            print(identity()); return
        directory = args.directory.absolute()
        if args.command == 'archive':
            result = package(args.sdk.absolute(), directory)
        elif args.command == 'install':
            result = install(directory, args.destination.absolute())
        elif args.command == 'fetch':
            result = fetch(args.repo, directory, args.binary_only)
            if not result['found']:
                parser.exit(1 if args.require else 3, 'Exact Wasm SDK recipe absent from base; run explicit Wasm base maintenance.\n')
        elif args.command == 'publish':
            result = publish(args.repo, directory, args.source_sha)
        else:
            validate(directory, args.binary_only)
            result = {'verified': True, 'recipe_id': identity()}
        print(json.dumps(result, sort_keys=True))
    except (ValueError, KeyError, OSError, RuntimeError, tarfile.TarError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'wasm-sdk-release: {error}\n')


if __name__ == '__main__':
    main()
