#!/usr/bin/env python3
"""Prepare the optional, pinned Debian native build SDK; never used by CMake."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def command(*args):
    return subprocess.check_output(args, text=True).strip()


def prepare(args):
    manifest_path = args.manifest.resolve()
    manifest = json.loads(manifest_path.read_text())
    if command('dpkg', '--print-architecture') != manifest['architecture']:
        raise ValueError('This SDK is for Debian amd64. Use native development packages on other systems.')
    cache = args.cache_dir.resolve()
    packages = (args.packages_dir or cache / 'packages').resolve()
    versions = {item['name']: item['version'] for item in manifest['packages']}
    runtimes = []
    # Use installed, exactly matching runtime packages, not libraries salvaged
    # from an old DataPump bundle. Record the actual files and hashes as well.
    for item in manifest['runtime_libraries']:
        package = item['package']
        version = command('dpkg-query', '-W', '-f=${Version}', package)
        expected = versions[item['development']]
        if version != expected:
            raise ValueError(f'{package}: installed {version}, SDK requires {expected}; use matching development packages or update the manifest.')
        candidates = [Path(p) for p in command('dpkg-query', '-L', package).splitlines()
                      if Path(p).name == item['soname'] and Path(p).is_file()]
        if len(candidates) != 1:
            raise ValueError(f'Cannot resolve {item["soname"]} from installed {package}')
        source = candidates[0].resolve()
        runtimes.append(dict(item, version=version, source=str(source), sha256=digest(source)))

    for item in manifest['packages']:
        name = item['file']
        if Path(name).name != name or not name.endswith('.deb'):
            raise ValueError(f'Invalid package filename: {name}')
        archive = packages / name
        if not archive.exists() and args.download:
            if not item['url'].startswith('https://deb.debian.org/debian/'):
                raise ValueError(f'Unexpected download origin: {item["url"]}')
            packages.mkdir(parents=True, exist_ok=True)
            # Stage beside the retained archive, never in an ephemeral SDK.
            with tempfile.NamedTemporaryFile(dir=packages, delete=False) as temporary:
                partial = Path(temporary.name)
            try:
                with urllib.request.urlopen(item['url'], timeout=60) as response, partial.open('wb') as output:
                    shutil.copyfileobj(response, output)
                if digest(partial) != item['sha256']:
                    raise ValueError(f'Checksum mismatch: {name}')
                partial.replace(archive)
            finally:
                partial.unlink(missing_ok=True)
        if not archive.is_file():
            raise ValueError(f'Missing {archive}. Supply --packages-dir or explicitly use --download.')
        if digest(archive) != item['sha256']:
            raise ValueError(f'Checksum mismatch: {archive}')

    cache.mkdir(parents=True, exist_ok=True)
    retained = cache / 'packages'
    retained.mkdir(exist_ok=True)
    if packages != retained:
        for item in manifest['packages']:
            destination = retained / item['file']
            if not destination.exists() or digest(destination) != item['sha256']:
                shutil.copyfile(packages / item['file'], destination)
        packages = retained
    sysroot = cache / 'sysroot'
    if sysroot.is_symlink():
        raise ValueError(f'Refusing a symlink SDK destination: {sysroot}')
    marker = sysroot / 'prepared.json'
    state = {'manifest_sha256': digest(manifest_path), 'runtime_libraries': runtimes}
    if sysroot.exists() and not marker.is_file():
        raise ValueError(f'Refusing to replace an unmanaged directory: {sysroot}')
    # Re-extract verified archives to repair deleted headers or changed aliases.
    # Only this tool's own managed SDK is replaced; existing build trees survive.
    with tempfile.TemporaryDirectory(prefix='prepare-', dir=cache) as work:
        staged = Path(work) / 'sysroot'
        staged.mkdir()
        for item in manifest['packages']:
            subprocess.run(['dpkg-deb', '--extract', str(packages / item['file']), str(staged)], check=True)
        libdir = staged / 'usr/lib' / manifest['multiarch']
        for item in runtimes:
            link = libdir / item['link']
            link.unlink(missing_ok=True)
            link.symlink_to(item['source'])
        (staged / 'prepared.json').write_text(json.dumps(state, indent=2) + '\n')
        previous = Path(work) / 'previous'
        if sysroot.exists():
            sysroot.rename(previous)
        try:
            staged.rename(sysroot)
        except OSError:
            if previous.exists():
                previous.rename(sysroot)
            raise
    print(f'Prepared native dependencies: {sysroot / "usr"}')
    print('CMake detects the default cache automatically. For another location use:')
    print(f'  -DDATAPUMP_DEPENDENCY_PREFIX={sysroot / "usr"}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, default=ROOT / 'third_party/build-support/debian-13-amd64.json')
    parser.add_argument('--packages-dir', type=Path, help='Directory containing the verified .deb archives (offline by default)')
    parser.add_argument('--cache-dir', type=Path, default=ROOT / 'third_party/build-support/cache')
    parser.add_argument('--download', action='store_true', help='Explicitly allow downloading missing pinned archives')
    args = parser.parse_args()
    try:
        prepare(args)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Dependency preparation failed: {error}\n')


if __name__ == '__main__':
    main()
