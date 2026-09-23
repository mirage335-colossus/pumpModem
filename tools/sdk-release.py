#!/usr/bin/env python3
"""Reuse exact source SDK recipes from the durable GitHub release named base."""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('datapump_source_sdk', ROOT / 'tools/build-sdk.py')
sdk = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sdk)
HEX = re.compile(r'[0-9a-f]{64}')


def names(identity):
    if not re.fullmatch(r'[0-9a-f]{20}', identity):
        raise ValueError('Invalid source SDK recipe identity')
    return (f'datapump-sdk-{identity}-linux-x86_64.tar.gz',
            f'datapump-sdk-sources-{identity}.tar.gz', f'sdk-{identity}-SHA256SUMS.txt')


def recipe_files():
    return {str(path.relative_to(ROOT)): path.read_bytes()
            for path in [ROOT / 'tools/build-sdk.py', *sorted(sdk.RECIPE.rglob('*'))]
            if path.is_file()}


def read_small(archive, member):
    if not member.isfile() or member.size > 16 * 1024 * 1024:
        raise ValueError(f'Expected a small regular SDK metadata file: {member.name}')
    with archive.extractfile(member) as stream:
        return stream.read()


def check_sources(value, identity):
    if not isinstance(value, dict) or value.get('id') != identity:
        raise ValueError('Preserved source inventory belongs to another recipe')
    files = value.get('files')
    if not isinstance(files, dict) or not files:
        raise ValueError('Preserved source inventory is empty or malformed')
    for path, digest in files.items():
        if not isinstance(path, str):
            raise ValueError('Invalid preserved source path')
        parts = PurePosixPath(path)
        if (parts.is_absolute() or '..' in parts.parts or not path.startswith('downloads/')
                or not isinstance(digest, str) or not HEX.fullmatch(digest)):
            raise ValueError('Invalid preserved source checksum or path')
    return files


def verify_internals(directory, identity):
    """Read archives without extracting/executing them; verify the source replay."""
    binary, source, _ = names(identity)
    expected_recipe = recipe_files()
    recipe_manifest = json.loads(expected_recipe['third_party/build-support/source-sdk/manifest.json'])
    source_root = source.removesuffix('.tar.gz') + '/'
    cache = source_root + 'third_party/build-support/cache/source-sdk/'
    found_recipe = {}
    found_downloads = {}
    source_inventory = None
    bootstrap = None
    bootstrap_name = 'bootstrap/' + recipe_manifest['buildroot']['file']
    with tarfile.open(directory / source, 'r|gz') as archive:
        for member in archive:
            relative = member.name.removeprefix(source_root)
            if relative == member.name:
                continue
            if relative == 'tools/build-sdk.py' or (not member.isdir() and relative.startswith('third_party/build-support/source-sdk/')):
                if relative in found_recipe:
                    raise ValueError('Duplicate recipe file in preserved sources')
                found_recipe[relative] = read_small(archive, member)
            elif member.name == cache + 'sources.json':
                if source_inventory is not None:
                    raise ValueError('Duplicate source inventory')
                source_inventory = json.loads(read_small(archive, member))
            elif member.name.startswith(cache + 'downloads/') or member.name == cache + bootstrap_name:
                if not member.isfile():
                    if member.isdir():
                        continue
                    raise ValueError('Preserved source archives must be ordinary files')
                with archive.extractfile(member) as stream:
                    digest = hashlib.sha256()
                    for block in iter(lambda: stream.read(1024 * 1024), b''):
                        digest.update(block)
                if member.name == cache + bootstrap_name:
                    if bootstrap is not None:
                        raise ValueError('Duplicate source bootstrap archive')
                    bootstrap = digest.hexdigest()
                else:
                    path = member.name.removeprefix(cache)
                    if path in found_downloads:
                        raise ValueError('Duplicate preserved download')
                    found_downloads[path] = digest.hexdigest()
    if found_recipe != expected_recipe:
        raise ValueError('Preserved source recipe does not match this checkout')
    if bootstrap != recipe_manifest['buildroot']['sha256']:
        raise ValueError('Preserved bootstrap archive checksum mismatch')
    if found_downloads != check_sources(source_inventory, identity):
        raise ValueError('Preserved source archive contents do not match their checksums')

    verify_binary(directory, identity, source_inventory)


def verify_binary(directory, identity, source_inventory=None):
    """Check a compiled consumer SDK against the current recipe and target."""
    binary = names(identity)[0]
    recipe_manifest = json.loads(recipe_files()['third_party/build-support/source-sdk/manifest.json'])
    binary_root = binary.removesuffix('.tar.gz') + '/share/datapump-sdk/'
    metadata = {}
    with tarfile.open(directory / binary, 'r|gz') as archive:
        for member in archive:
            if not member.name.startswith(binary_root):
                continue
            relative = member.name.removeprefix(binary_root)
            if relative in ('manifest.json', 'sources.json'):
                if relative in metadata:
                    raise ValueError('Duplicate binary SDK metadata')
                metadata[relative] = json.loads(read_small(archive, member))
    manifest = metadata.get('manifest.json', {})
    if (manifest.get('schema_version') != 1 or manifest.get('id') != identity
            or manifest.get('recipe') != recipe_manifest
            or manifest.get('baseline', {}).get('glibc') != recipe_manifest['glibc']
            or manifest.get('target', {}).get('triple') != recipe_manifest['target']
            or manifest.get('target', {}).get('processor') != recipe_manifest['architecture']):
        raise ValueError('Binary SDK manifest does not match this recipe and target')
    if source_inventory is not None and metadata.get('sources.json') != source_inventory:
        raise ValueError('Binary SDK and preserved sources do not share the same inventory')
    check_sources(metadata.get('sources.json'), identity)


def validate(directory, identity, binary_only=False):
    binary, source, _ = names(identity)
    expected = {binary, source}
    if not directory.is_dir() or directory.is_symlink():
        raise ValueError('Missing or unsafe SDK archive directory')
    entries = list(directory.iterdir())
    if any(path.is_symlink() or not path.is_file() for path in entries):
        raise ValueError('SDK archive inventory must contain only ordinary files')
    present = {binary} if binary_only else expected
    if {path.name for path in entries} != present | {'SHA256SUMS'}:
        raise ValueError('SDK archive directory must contain the exact recipe pair and SHA256SUMS')
    sums = {}
    for line in (directory / 'SHA256SUMS').read_text(encoding='utf-8').splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  ([A-Za-z0-9][A-Za-z0-9_.-]*)', line)
        if not match or match[2] in sums:
            raise ValueError('Invalid or duplicate SDK checksum entry')
        sums[match[2]] = match[1]
    if set(sums) != expected:
        raise ValueError('SDK checksum inventory does not match the exact recipe pair')
    for name in present:
        expected_hash = sums[name]
        if sdk.digest(directory / name) != expected_hash:
            raise ValueError(f'SDK archive checksum mismatch: {name}')
    if binary_only:
        verify_binary(directory, identity)
    else:
        verify_internals(directory, identity)
    return sums


def gh(arguments, *, check=True):
    return subprocess.run(['gh', *arguments], check=check, text=True, capture_output=True)


def api_pages(endpoint):
    response = gh(['api', '--paginate', endpoint]).stdout
    decoder = json.JSONDecoder()
    rows = []
    while response.strip():
        response = response.lstrip()
        page, end = decoder.raw_decode(response)
        if not isinstance(page, list):
            raise ValueError('Malformed GitHub paginated inventory')
        rows.extend(page)
        response = response[end:]
    return rows


def release_info(repository):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*/[A-Za-z0-9][A-Za-z0-9_.-]*', repository):
        raise ValueError('Repository must be OWNER/REPO')
    gh(['api', f'repos/{repository}'])  # Private-repository denial can be a 404.
    result = gh(['api', '--include', f'repos/{repository}/releases/tags/base'], check=False)
    response = result.stdout.replace('\r\n', '\n')
    status = re.match(r'HTTP/\S+ ([0-9]{3})(?:\s|$)', response)
    if status and status[1] == '404':
        # The tag endpoint omits drafts. A previous incomplete publication
        # must not silently turn into a cache miss and another cold build.
        candidates = [item for item in api_pages(f'repos/{repository}/releases?per_page=100')
                      if item.get('tag_name') == 'base']
        if len(candidates) > 1:
            raise ValueError('Multiple base release drafts require inspection')
        if not candidates:
            return None
        info = candidates[0]
    elif not status or status[1] != '200' or result.returncode:
        raise RuntimeError(f'Cannot read the base release: {result.stderr.strip()}')
    else:
        info = json.loads(response.split('\n\n', 1)[1])
    if not isinstance(info.get('id'), int):
        raise ValueError('Malformed base release response')
    # A durable shelf accumulates recipes. Explicit pagination avoids mistaking
    # an older/newer recipe outside the first asset page for a cache miss.
    info['assets'] = api_pages(f'repos/{repository}/releases/{info["id"]}/assets?per_page=100')
    return info


def has_pair(info, identity):
    if info is None:
        return False
    counts = Counter(asset['name'] for asset in info['assets'])
    expected = names(identity)
    present = [name for name in expected if counts[name]]
    if not present:
        return False
    if len(present) != len(expected) or any(counts[name] != 1 for name in expected):
        raise ValueError('Partial or duplicate recipe assets on base; inspect/remove the incomplete recipe assets before retrying')
    return True


def download_pair(repository, directory, identity, binary_only=False):
    directory.mkdir()
    assets = names(identity)
    if binary_only:
        assets = (assets[0], assets[2])
    gh(['release', 'download', 'base', '--repo', repository, '--dir', str(directory),
        *[argument for name in assets for argument in ('--pattern', name)]])
    (directory / names(identity)[2]).rename(directory / 'SHA256SUMS')
    return validate(directory, identity, binary_only=binary_only)


def fetch(repository, directory, binary_only=False):
    identity = sdk.recipe_id()
    info = release_info(repository)
    if not has_pair(info, identity):
        return {'found': False, 'recipe_id': identity}
    if info.get('draft'):
        raise ValueError('The base release is still a draft; finish its publication before reuse')
    if directory.is_symlink() or (directory.exists() and (not directory.is_dir() or any(directory.iterdir()))):
        raise ValueError('Refusing to replace an existing SDK archive directory')
    directory.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.sdk-fetch-', dir=directory.parent) as temporary:
        staged = Path(temporary) / 'archives'
        download_pair(repository, staged, identity, binary_only=binary_only)
        if directory.exists():
            directory.rmdir()  # Only an empty directory passed the check above.
        staged.rename(directory)
    return {'found': True, 'recipe_id': identity}


def publish(repository, directory, source_sha):
    if not re.fullmatch(r'[0-9a-f]{40}|[0-9a-f]{64}', source_sha):
        raise ValueError('Source SHA must be a complete lowercase Git object ID')
    identity = sdk.recipe_id()
    local = validate(directory, identity)
    info = release_info(repository)
    present = has_pair(info, identity)
    if present:
        with tempfile.TemporaryDirectory(prefix='sdk-base-compare-') as temporary:
            remote = download_pair(repository, Path(temporary) / 'archives', identity)
        if remote != local:
            raise ValueError('The immutable base recipe assets differ from the local archives; refusing to overwrite')
        if info.get('draft') or info.get('name') != 'base' or not info.get('prerelease'):
            gh(['release', 'edit', 'base', '--repo', repository,
                '--title', 'base', '--draft=false', '--prerelease', '--latest=false'])
        return {'published': False, 'reused': True, 'recipe_id': identity}
    if info is None:
        gh(['release', 'create', 'base', '--repo', repository, '--target', source_sha,
            '--title', 'base', '--notes', 'Reusable DataPump source SDKs and their preserved sources, indexed by exact recipe identity.',
            '--draft', '--prerelease', '--latest=false'])
    binary, source, checksum = names(identity)
    # Upload the checksum last as the completed pair's marker. No --clobber;
    # partial attempts remain visible and require deliberate inspection.
    gh(['release', 'upload', 'base', '--repo', repository,
        str(directory / binary), str(directory / source)])
    with tempfile.TemporaryDirectory(prefix='sdk-base-sums-') as temporary:
        path = Path(temporary) / checksum
        path.write_text(''.join(f'{local[name]}  {name}\n' for name in sorted(local)), encoding='utf-8')
        gh(['release', 'upload', 'base', '--repo', repository, str(path)])
    gh(['release', 'edit', 'base', '--repo', repository,
        '--title', 'base', '--draft=false', '--prerelease', '--latest=false'])
    return {'published': True, 'reused': False, 'recipe_id': identity}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('fetch', 'publish', 'verify'):
        command = commands.add_parser(name)
        if name != 'verify':
            command.add_argument('--repo', required=True)
        command.add_argument('--directory', type=Path, required=True)
        command.add_argument('--github-output', type=Path)
        if name == 'fetch':
            command.add_argument('--binary-only', action='store_true')
        if name == 'publish':
            command.add_argument('--source-sha', required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == 'fetch':
            value = fetch(args.repo, args.directory, binary_only=args.binary_only)
        elif args.command == 'verify':
            identity = sdk.recipe_id()
            validate(args.directory, identity)
            value = {'verified': True, 'recipe_id': identity}
        else:
            value = publish(args.repo, args.directory, args.source_sha)
        if args.github_output:
            with args.github_output.open('a', encoding='utf-8') as output:
                for key, item in value.items():
                    output.write(f'{key}={str(item).lower() if isinstance(item, bool) else item}\n')
    except (ValueError, KeyError, OSError, RuntimeError, tarfile.TarError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'sdk-release: {error}\n')
    print(json.dumps(value, sort_keys=True))


if __name__ == '__main__':
    main()
