#!/usr/bin/env python3
"""Preserve exact dependency archives with every binary release.

Recipe identities describe bytes already built and preserved elsewhere. This
helper never builds dependencies or executes helpers from historical source.
"""
import argparse
import base64
from functools import lru_cache
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
IDENTITY = re.compile(r'[0-9a-f]{20}')
SHA256 = re.compile(r'[0-9a-f]{64}')
GIT_SHA = re.compile(r'[0-9a-f]{40}|[0-9a-f]{64}')
WINDOWS_FILES = ('tools/windows-base.py', 'third_party/build-support/windows-base.json')
SDK_HELPER = 'tools/build-sdk.py'
SDK_PREFIX = 'third_party/build-support/source-sdk/'


@lru_cache(maxsize=None)
def _tool(name):
    spec = importlib.util.spec_from_file_location('datapump_dependency_' + name,
                                                 ROOT / 'tools' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def release_tool():
    return _tool('release')


def certification_tool():
    return _tool('certify-release')


def _kinds(linux_baseline):
    if linux_baseline not in ('bookworm-sdk', 'ubuntu-22.04'):
        raise ValueError('Unknown Linux baseline')
    return {'windows-base', 'linux-sdk'} if linux_baseline == 'bookworm-sdk' else {'windows-base'}


def _recipe_ids(files, linux_baseline):
    _kinds(linux_baseline)
    if not all(name in files for name in WINDOWS_FILES):
        raise ValueError('Historical Windows dependency provenance absent: exact recipe/helper required')
    hashes = {name: hashlib.sha256(files[name]).hexdigest() for name in WINDOWS_FILES}
    result = {'windows-base': hashlib.sha256(
        json.dumps(hashes, sort_keys=True, separators=(',', ':')).encode()).hexdigest()[:20]}
    if linux_baseline == 'bookworm-sdk':
        if SDK_HELPER not in files or SDK_PREFIX + 'manifest.json' not in files:
            raise ValueError('Historical Linux SDK dependency provenance absent: exact recipe/helper required')
        digest = hashlib.sha256()
        for name in [SDK_HELPER, *sorted(name for name in files if name.startswith(SDK_PREFIX))]:
            digest.update(name.encode() + b'\0')
            digest.update(files[name])
        result['linux-sdk'] = digest.hexdigest()[:20]
    return result


def recipe_ids(root, linux_baseline):
    """Match existing recipe identities without importing platform build tools."""
    _kinds(linux_baseline)
    root = Path(root)
    paths = [root / name for name in WINDOWS_FILES]
    if linux_baseline == 'bookworm-sdk':
        paths += [root / SDK_HELPER, *sorted((root / SDK_PREFIX).rglob('*'))]
    return _recipe_ids({path.relative_to(root).as_posix(): path.read_bytes()
                        for path in paths if path.is_file()}, linux_baseline)


def remote_recipe_ids(repository, source_sha, linux_baseline):
    """Hash recipe blobs from the exact source commit, never its current branch."""
    _kinds(linux_baseline)
    release = release_tool()
    release.repository_name(repository)
    if not isinstance(source_sha, str) or not GIT_SHA.fullmatch(source_sha):
        raise ValueError('Dependency provenance requires a complete source Git object ID')

    def api(endpoint):
        return json.loads(release.gh(['api', f'repos/{repository}/git/{endpoint}']).stdout)

    commit = api('commits/' + source_sha)
    tree_sha = commit.get('tree', {}).get('sha')
    if (commit.get('sha') != source_sha or not isinstance(tree_sha, str)
            or not GIT_SHA.fullmatch(tree_sha)):
        raise ValueError('Historical dependency source commit/tree identity mismatch')
    tree = api(f'trees/{tree_sha}?recursive=1')
    if tree.get('sha') != tree_sha or tree.get('truncated') is not False or not isinstance(tree.get('tree'), list):
        raise ValueError('Historical dependency source tree is truncated or invalid')
    entries = {}
    for entry in tree['tree']:
        name = entry.get('path')
        if not isinstance(name, str):
            raise ValueError('Historical dependency source tree has an invalid path')
        wanted = name in WINDOWS_FILES or (linux_baseline == 'bookworm-sdk' and
                  (name == SDK_HELPER or name.startswith(SDK_PREFIX)))
        if not wanted or entry.get('type') == 'tree':
            continue
        if (name in entries or entry.get('type') != 'blob' or entry.get('mode') not in ('100644', '100755')
                or not isinstance(entry.get('sha'), str) or not GIT_SHA.fullmatch(entry['sha'])):
            raise ValueError('Historical dependency recipe must contain unique regular Git blobs')
        entries[name] = entry['sha']
    # Fail before downloading unrelated blobs when this older release did not
    # record the Windows dependency recipe. Current-checkout guesses are unsafe.
    if not set(WINDOWS_FILES) <= entries.keys():
        raise ValueError('Historical Windows dependency provenance absent: exact recipe/helper required')
    files = {}
    for name, sha in entries.items():
        blob = api('blobs/' + sha)
        if blob.get('sha') != sha or blob.get('encoding') != 'base64' or not isinstance(blob.get('content'), str):
            raise ValueError('Historical dependency recipe blob identity/encoding mismatch')
        try:
            data = base64.b64decode(''.join(blob['content'].split()), validate=True)
        except ValueError as error:
            raise ValueError('Historical dependency recipe blob has invalid base64') from error
        object_bytes = f'blob {len(data)}\0'.encode() + data
        actual = (hashlib.sha1(object_bytes) if len(sha) == 40 else hashlib.sha256(object_bytes)).hexdigest()
        if actual != sha or blob.get('size') != len(data):
            raise ValueError('Historical dependency recipe blob checksum mismatch')
        files[name] = data
    return _recipe_ids(files, linux_baseline)


def _names(kind, identity):
    if kind == 'windows-base':
        return (f'windows-base-{identity}-x64-windows-static.zip',
                f'windows-base-sources-{identity}.zip', f'windows-base-{identity}-SHA256SUMS.txt')
    return (f'datapump-sdk-{identity}-linux-x86_64.tar.gz',
            f'datapump-sdk-sources-{identity}.tar.gz', f'sdk-{identity}-SHA256SUMS.txt')


def _groups(dependencies, linux_baseline):
    required = _kinds(linux_baseline)
    if (not isinstance(dependencies, dict) or set(dependencies) != required
            or any(not isinstance(value, str) or not IDENTITY.fullmatch(value) for value in dependencies.values())):
        raise ValueError('Release dependencies require exact kinds and 20-character hexadecimal recipe identities')
    return {kind: _names(kind, dependencies[kind]) for kind in sorted(required)}


def asset_names(dependencies, linux_baseline):
    return {name for group in _groups(dependencies, linux_baseline).values() for name in group}


def _verify_groups(directory, groups):
    release = release_tool()
    directory = Path(directory)
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError('Missing or unsafe dependency archive directory')
    for binary, source, checksum in groups.values():
        for name in (binary, source, checksum):
            path = directory / name
            if path.is_symlink() or not path.is_file():
                raise ValueError(f'Missing or unsafe dependency archive: {name}')
        entries = {}
        for line in (directory / checksum).read_text(encoding='utf-8').splitlines():
            match = re.fullmatch(r'([0-9a-f]{64})  ([A-Za-z0-9][A-Za-z0-9_.-]*)', line)
            if not match or match[2] in entries:
                raise ValueError(f'Invalid or duplicate dependency checksum entry: {checksum}')
            entries[match[2]] = match[1]
        if set(entries) != {binary, source}:
            raise ValueError(f'Dependency checksum inventory must contain the exact recipe pair: {checksum}')
        for name, expected in entries.items():
            if release.digest(directory / name) != expected:
                raise ValueError(f'Dependency archive checksum mismatch: {name}')


def verify(directory, dependencies, linux_baseline):
    _verify_groups(directory, _groups(dependencies, linux_baseline))


def _base_assets(repository):
    release = release_tool()
    matches = [item for item in release.api_pages(f'repos/{repository}/releases?per_page=100')
               if item.get('tag_name') == 'base']
    if (len(matches) != 1 or matches[0].get('draft') is not False
            or type(matches[0].get('id')) is not int or matches[0]['id'] <= 0):
        raise ValueError('Exact dependencies require one published base release; explicit base maintenance is required')
    items = release.api_pages(f'repos/{repository}/releases/{matches[0]["id"]}/assets?per_page=100')
    assets = {item['name']: item for item in items}
    if len(assets) != len(items):
        raise ValueError('Duplicate dependency assets on base')
    return assets


def _copy_group(repository, directory, names, assets, inventory=None, *, reconstruct=False):
    release = release_tool()
    wanted = names[:2] if reconstruct else names
    for name in wanted:
        if name not in assets:
            raise ValueError(f'Missing required dependency asset: {name}')
        if inventory is not None:
            expected = inventory.get(name)
            if (not isinstance(expected, str) or not SHA256.fullmatch(expected)
                    or assets[name].get('digest') != 'sha256:' + expected):
                raise ValueError(f'Dependency source inventory differs from GitHub digest: {name}')
        release.download_asset(repository, assets[name], directory / name)
        if inventory is not None and release.digest(directory / name) != inventory[name]:
            raise ValueError(f'Dependency source inventory checksum mismatch: {name}')
    if reconstruct:
        (directory / names[2]).write_text(''.join(f'{inventory[name]}  {name}\n' for name in names[:2]),
                                           encoding='utf-8')


def preserve(repository, directory, dependencies, linux_baseline, *, source_assets=None, source_inventory=None):
    """Copy exact recipe triplets; a finalized source release survives base deletion."""
    release = release_tool()
    release.repository_name(repository)
    groups = _groups(dependencies, linux_baseline)
    directory = Path(directory)
    if directory.is_symlink() or (directory.exists() and not directory.is_dir()):
        raise ValueError('Missing or unsafe dependency archive directory')
    names = asset_names(dependencies, linux_baseline)
    if any((directory / name).exists() or (directory / name).is_symlink() for name in names):
        raise ValueError('Refusing to replace existing preserved dependency assets')
    sources = {} if source_assets is None else source_assets
    if not isinstance(sources, dict) or (source_inventory is not None and not isinstance(source_inventory, dict)):
        raise ValueError('Dependency source assets and checksum inventory must be mappings')
    selected, missing = {}, {}
    for kind, group in groups.items():
        present = set(group) & sources.keys()
        if present == set(group):
            selected[kind] = False
        elif kind == 'linux-sdk' and present == set(group[:2]) and source_inventory is not None:
            # Legacy application releases optionally preserved this complete
            # pair in their main SHA256SUMS, before recipe checksum assets existed.
            selected[kind] = True
        elif present:
            raise ValueError(f'Partial dependency recipe assets on source release: {kind}')
        else:
            missing[kind] = group
    base = _base_assets(repository) if missing else {}
    for kind, group in missing.items():
        if not set(group) <= base.keys():
            raise ValueError(f'Missing or partial dependency recipe on base: {kind}; explicit base maintenance is required')
    directory.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.release-dependencies-', dir=directory.parent) as temporary:
        staged = Path(temporary)
        for kind, reconstruct in selected.items():
            _copy_group(repository, staged, groups[kind], sources, source_inventory, reconstruct=reconstruct)
        for group in missing.values():
            _copy_group(repository, staged, group, base)
        _verify_groups(staged, groups)
        for name in sorted(names):
            (staged / name).rename(directory / name)
    verify(directory, dependencies, linux_baseline)


def fetch(repository, tag, kind, directory, inventory_sha256=None, *, binary_only=False):
    """Restore a release's own preserved archives for bootstrap/certification."""
    if kind not in ('linux-sdk', 'windows-base'):
        raise ValueError('Unknown dependency kind')
    if inventory_sha256 is not None and (not isinstance(inventory_sha256, str)
                                           or not SHA256.fullmatch(inventory_sha256)):
        raise ValueError('Expected a complete inventory SHA-256')
    directory = Path(directory)
    if directory.exists() or directory.is_symlink():
        raise ValueError('Refusing to replace an existing dependency archive directory')
    with tempfile.TemporaryDirectory(prefix='release-dependency-metadata-') as temporary:
        state = certification_tool().prepare(repository, tag, Path(temporary) / 'published', inventory_sha256)
        metadata = state['metadata']
        if metadata['schema'] < 6:
            return {'found': False}
        groups = _groups(metadata['dependencies'], metadata['linux_baseline'])
        if kind not in groups:
            raise ValueError('Release did not use this dependency kind')
        directory.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='.release-dependency-fetch-', dir=directory.parent) as scratch:
            staged = Path(scratch) / 'archives'
            staged.mkdir()
            _copy_group(repository, staged, groups[kind], state['assets'], state['inventory'])
            _verify_groups(staged, {kind: groups[kind]})
            if binary_only:
                (staged / groups[kind][1]).unlink()
            (staged / groups[kind][2]).rename(staged / 'SHA256SUMS')
            staged.rename(directory)
    return {'found': True, 'recipe_id': metadata['dependencies'][kind]}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    command = commands.add_parser('fetch', help="Restore dependencies preserved by a published release")
    command.add_argument('--repo', required=True)
    command.add_argument('--tag', required=True)
    command.add_argument('--kind', choices=('linux-sdk', 'windows-base'), required=True)
    command.add_argument('--directory', type=Path, required=True)
    command.add_argument('--inventory-sha256')
    command.add_argument('--github-output', type=Path)
    command.add_argument('--binary-only', action='store_true',
                         help='Verify the complete pair, then retain only compiler/dependencies for installation')
    args = parser.parse_args(argv)
    try:
        result = fetch(args.repo, args.tag, args.kind, args.directory, args.inventory_sha256,
                       binary_only=args.binary_only)
        if args.github_output:
            with args.github_output.open('a', encoding='utf-8') as output:
                for key, value in result.items():
                    output.write(f'{key}={str(value).lower() if isinstance(value, bool) else value}\n')
        print(json.dumps(result, sort_keys=True))
    except subprocess.CalledProcessError as error:
        parser.error(release_tool().failure_message(error))
    except (ValueError, OSError, RuntimeError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
