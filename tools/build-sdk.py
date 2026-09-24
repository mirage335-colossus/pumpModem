#!/usr/bin/env python3
"""Build/export the pinned source SDK. Application builds never invoke this tool."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import posixpath
import re
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
RECIPE = ROOT / 'third_party/build-support/source-sdk'
DEFAULT_CACHE = ROOT / 'third_party/build-support/cache/source-sdk'


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def recipe_id():
    h = hashlib.sha256()
    for path in [Path(__file__).resolve(), *sorted(RECIPE.rglob('*'))]:
        if path.is_file():
            h.update(str(path.relative_to(ROOT)).encode() + b'\0')
            h.update(path.read_bytes())
    return h.hexdigest()[:20]


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def run(args, **kwargs):
    print('+', ' '.join(str(x) for x in args), flush=True)
    subprocess.run([str(x) for x in args], check=True, **kwargs)


def safe_extract(archive, destination, allow_absolute_links=False):
    """Accept ordinary source/SDK tarballs, never paths or links escaping the tree."""
    destination = destination.resolve()
    with tarfile.open(archive) as tar:
        members = tar.getmembers()
        seen = {}
        for member in members:
            name = PurePosixPath(member.name)
            duplicate = str(name) in seen
            if name.is_absolute() or '..' in name.parts or (duplicate and not (member.isdir() and seen[str(name)])):
                raise ValueError(f'Unsafe or duplicate archive path: {member.name}')
            seen[str(name)] = member.isdir()
            if not (member.isfile() or member.isdir() or member.issym() or member.islnk()):
                raise ValueError(f'Unsupported archive entry: {member.name}')
            if member.issym() or member.islnk():
                target = member.linkname
                combined = target if member.islnk() else posixpath.join(str(name.parent), target)
                normalized = posixpath.normpath(combined)
                if (target.startswith('/') and (not allow_absolute_links or member.islnk())) or normalized == '..' or normalized.startswith('../'):
                    raise ValueError(f'Escaping archive link: {member.name}')
        for member in members:
            # Inspect the parent, not the link itself. A hash-verified Buildroot
            # source contains inert absolute links in target skeletons; no file
            # may ever be extracted through one of those links.
            output = (destination / member.name).parent.resolve() / Path(member.name).name
            if output != destination and destination not in output.parents:
                raise ValueError(f'Escaping archive path: {member.name}')
            if not member.issym() and output.is_symlink():
                raise ValueError(f'Archive entry writes through a symlink: {member.name}')
            if member.islnk():
                target = (destination / member.linkname).resolve()
                if destination not in target.parents:
                    raise ValueError(f'Escaping archive hardlink: {member.name}')
            # No device nodes or setuid bits; source packages need ordinary modes only.
            member.mode &= 0o777
            options = {'filter': 'fully_trusted'} if hasattr(tarfile, 'fully_trusted_filter') else {}
            tar.extract(member, destination, set_attrs=True, **options)
        if not allow_absolute_links:
            for path in destination.rglob('*'):
                if path.is_symlink() and destination not in path.resolve().parents:
                    raise ValueError(f'Escaping archive symlink chain: {path}')


def fetch_file(item, destination, download):
    if destination.is_file():
        if digest(destination) != item['sha256']:
            raise ValueError(f'Checksum mismatch: {destination}')
        return
    if not download:
        raise ValueError(f'Missing {destination}; run fetch or restore the source archive first')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as stream:
        partial = Path(stream.name)
    try:
        print(f'Downloading {item["url"]}', flush=True)
        with urllib.request.urlopen(item['url'], timeout=120) as source, partial.open('wb') as output:
            shutil.copyfileobj(source, output)
        if digest(partial) != item['sha256']:
            raise ValueError(f'Checksum mismatch for {item["url"]}')
        partial.replace(destination)
    finally:
        partial.unlink(missing_ok=True)


def patch_buildroot(source, manifest):
    """Small, explicit overlay on a hash-verified upstream Buildroot tree."""
    glibc = manifest['glibc_source']
    # No locales are generated by this SDK. Leave Buildroot's independent
    # host-localedef package untouched; its patches target its own libc version.
    for package, prefix in [('glibc', 'GLIBC')]:
        path = source / f'package/{package}/{package}.mk'
        text = path.read_text()
        for variable, value in {
            'VERSION': glibc['version'],
            'SITE': glibc['site'],
            'SITE_METHOD': 'wget',
        }.items():
            text, count = re.subn(rf'^{prefix}_{variable} = .*$',
                                 f'{prefix}_{variable} = {value}', text, flags=re.M)
            if count != 1:
                raise ValueError(f'Buildroot overlay no longer matches {path}: {variable}')
        # Downloads are prefetched under the original filename; Buildroot also
        # has its own verified source mirror fallback if explicitly fetching.
        text = re.sub(rf'^{prefix}_SOURCE = .*\n', '', text, flags=re.M)
        text += f'\n' if not text.endswith('\n') else ''
        insertion = f'{prefix}_SOURCE = {glibc["file"]}\n'
        if package == 'glibc':
            # GCC 15 defaults to C23; the older libc's implementation uses C11.
            insertion += 'GLIBC_EXTRA_CFLAGS += -std=gnu11\n'
            text = re.sub(r'^GLIBC_LICENSE = .*?\nGLIBC_LICENSE_FILES = .*?\n',
                          'GLIBC_LICENSE = LGPL-2.1+, GPL-2.0+, BSD-3-Clause\n'
                          'GLIBC_LICENSE_FILES = COPYING COPYING.LIB LICENSES\n',
                          text, flags=re.M | re.S)
            text = re.sub(r'^GLIBC_IGNORE_CVES.*\n', '', text, flags=re.M)
        text = text.replace(f'$(eval $(', insertion + '$(eval $(', 1)
        path.write_text(text)
        hash_text = f'sha256  {glibc["sha256"]}  {glibc["file"]}\n'
        hash_text += ''.join(f'sha256  {sha}  {name}\n' for name, sha in glibc['licenses'].items())
        (path.parent / f'{package}.hash').write_text(hash_text)


def preserved_source_path(cache, name):
    """Validate a portable inventory path before hashing or archiving its file."""
    relative = PurePosixPath(name)
    if (relative.is_absolute() or '..' in relative.parts or
            not name.startswith('downloads/') or str(relative) != name):
        raise ValueError(f'Invalid preserved source path: {name}')
    path = cache / name
    download_root = cache.resolve() / 'downloads'
    if download_root not in path.resolve().parents:
        raise ValueError(f'Preserved source escapes download cache: {name}')
    if not path.is_file():
        raise ValueError(f'Missing or changed preserved source: {name}')
    return path


def resolved_downloads(cache, packages):
    """Keep exactly Buildroot's selected downloads, including shared archives once."""
    names = set()
    for package in packages.values():
        for download in package.get('downloads', []):
            directory = package['dl_dir']
            source = download['source']
            for component in (directory, source):
                path = PurePosixPath(component)
                if (path.is_absolute() or '..' in path.parts or
                        not component or path == PurePosixPath('.')):
                    raise ValueError(f'Invalid resolved download path: {directory}/{source}')
            names.add(str(PurePosixPath('downloads', directory, source)))
    return [preserved_source_path(cache, name) for name in sorted(names)]


def validate_sources(cache, identity):
    path = cache / 'sources.json'
    if not path.is_file():
        raise ValueError('No complete source inventory; run fetch before an offline build')
    state = json.loads(path.read_text())
    if state['id'] != identity:
        raise ValueError('Source inventory belongs to another recipe; run fetch for this recipe')
    for name, sha in state['files'].items():
        path = preserved_source_path(cache, name)
        if digest(path) != sha:
            raise ValueError(f'Missing or changed preserved source: {name}')
    return state['files']


def prepare(cache, manifest, identity, download, jobs):
    if platform.system() != 'Linux' or platform.machine() not in ('x86_64', 'AMD64'):
        raise ValueError('This initial SDK recipe builds on x86_64 Linux')
    # Buildroot and its relocation script do not support arbitrary shell paths.
    for path in (ROOT, cache):
        if not re.fullmatch(r'[A-Za-z0-9_./+-]+', str(path)):
            raise ValueError(f'SDK preparation needs a path without whitespace/metacharacters: {path}')
    bootstrap = cache / 'bootstrap' / manifest['buildroot']['file']
    fetch_file(manifest['buildroot'], bootstrap, download)
    fetch_file(manifest['glibc_source'], cache / 'downloads/glibc' / manifest['glibc_source']['file'], download)
    work = cache / 'work' / identity
    source = work / f'buildroot-{manifest["buildroot"]["version"]}'
    if not source.exists():
        work.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=work) as temporary:
            staged = Path(temporary)
            safe_extract(bootstrap, staged, allow_absolute_links=True)
            extracted = staged / source.name
            patch_buildroot(extracted, manifest)
            extracted.rename(source)
    output = work / 'output'
    command = ['make', '-C', source, f'O={output}', f'BR2_EXTERNAL={RECIPE}',
               f'BR2_DL_DIR={cache / "downloads"}', f'BR2_JLEVEL={jobs}']
    # Network permission is explicit. Every download mechanism is disabled in
    # offline mode, including VCS fallback paths used by Buildroot.
    if not download:
        command += [f'BR2_{tool}=/bin/false' for tool in ('WGET', 'GIT', 'SVN', 'HG', 'CVS', 'BZR', 'SCP', 'SFTP')]
    else:
        command += ['BR2_WGET=wget --timeout=30 --tries=2 -nv']
    run(command + ['datapump_defconfig'])
    config = (output / '.config').read_text()
    for option in ('BR2_GCC_VERSION_15_X=y', 'BR2_PACKAGE_LIBGLEW=y',
                   'BR2_PACKAGE_XLIB_LIBXFT=y', 'BR2_PACKAGE_LIBOPENSSL=y',
                   'BR2_TOOLCHAIN_BUILDROOT_GLIBC=y', 'BR2_GENERATE_LOCALE=""'):
        if option not in config.splitlines():
            raise ValueError(f'Required SDK feature disappeared during configuration: {option}')
    return command, output


def prune_sdk_runtime_paths(sdk, sysroot):
    """Remove target-machine virtual filesystems, not compiler/development data."""
    sdk, sysroot = sdk.resolve(), sysroot.resolve()
    if not sysroot.is_dir() or sdk not in sysroot.parents:
        raise ValueError('SDK runtime pruning requires an existing contained target sysroot')
    # These belong to a booted target machine and are never consulted by a
    # sysroot compiler. In particular, dev/fd and these /etc links refer to
    # procfs or files populated by the target's runtime network configuration.
    # Keep ordinary target configuration, libraries, headers and package data.
    for relative in ('dev', 'proc', 'sys', 'run', 'tmp',
                     'var/run', 'var/lock', 'var/tmp', 'etc/mtab', 'etc/resolv.conf'):
        path = sysroot / relative
        parent = path.parent.resolve()
        if parent != sysroot and sysroot not in parent.parents:
            raise ValueError(f'Target runtime path has an escaping parent: {path}')
        if path.is_symlink():
            path.unlink()
        elif path.is_dir():
            if os.path.ismount(path):
                raise ValueError(f'Target runtime path is mounted; refusing to remove it: {path}')
            shutil.rmtree(path)
        elif path.exists():
            path.unlink()


def normalized_links(sdk, sysroot):
    for path in sorted(sdk.rglob('*')):
        if not path.is_symlink():
            continue
        link = os.readlink(path)
        if link.startswith('/'):
            target = Path(link)
            if target != sdk and sdk not in target.parents:
                if sysroot in path.parents:
                    target = sysroot / link.lstrip('/')
                else:
                    raise ValueError(f'SDK contains external absolute link: {path} -> {link}')
            path.unlink()
            path.symlink_to(os.path.relpath(target, path.parent))
        resolved = path.resolve()
        if (resolved != sdk and sdk not in resolved.parents) or not resolved.exists():
            raise ValueError(f'SDK contains dangling/escaping link: {path}')


def archive_tree(destination, entries, epoch):
    """Stable ordering/metadata; upstream binaries are not claimed bit reproducible."""
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_suffix(destination.suffix + '.partial')
    def metadata(info):
        info.uid = info.gid = 0
        info.uname = info.gname = ''
        info.mtime = epoch
        return info
    try:
        with tarfile.open(partial, 'w:gz', dereference=False) as archive:
            for path, name in entries:
                archive.add(path, arcname=name, filter=metadata)
        partial.replace(destination)
    finally:
        partial.unlink(missing_ok=True)
    sums = destination.parent / 'SHA256SUMS'
    archives = sorted(destination.parent.glob('*.tar.gz'))
    sums.write_text(''.join(f'{digest(path)}  {path.name}\n' for path in archives))


def elf_header(path):
    with path.open('rb') as stream:
        header = stream.read(20)
    if len(header) != 20 or header[:4] != b'\x7fELF' or header[5] not in (1, 2):
        return None
    order = 'little' if header[5] == 1 else 'big'
    return header[4], int.from_bytes(header[16:18], order), int.from_bytes(header[18:20], order)


def elf_versions(path, readelf):
    output = subprocess.check_output([readelf, '--version-info', '--wide', path], text=True,
                                     env=dict(os.environ, LC_ALL='C'))
    provided, required = set(), {}
    section = library = None
    for line in output.splitlines():
        if line.startswith('Version definition section'):
            section = 'provided'
        elif line.startswith('Version needs section'):
            section = 'required'
        elif line.startswith('Version symbols section'):
            section = None
        if section == 'required':
            match = re.search(r'\bFile: (\S+)', line)
            if match:
                library = match.group(1)
                required.setdefault(library, set())
        for name in re.findall(r'\bName: (\S+)', line):
            if section == 'provided':
                provided.add(name)
            elif section == 'required' and library:
                required[library].add(name)
    return provided, required


def bundle_host_cxx_runtime(root, sysroot, architecture):
    """Give host tools their own same-architecture GNU C++ runtime, never libc."""
    if (platform.system() != 'Linux' or architecture != 'x86_64' or
            platform.machine() not in ('x86_64', 'AMD64')):
        raise ValueError('Reusing target C++ runtimes requires an x86_64 Linux host and target')
    root, sysroot = root.resolve(), sysroot.resolve()
    if root not in sysroot.parents:
        raise ValueError('Target sysroot must remain inside the SDK')
    patcher = root / 'bin/patchelf'
    if not patcher.is_file() or root not in patcher.resolve().parents:
        raise ValueError('SDK host runtime export needs its own bin/patchelf')
    readelf = root / 'bin' / f'{sysroot.parent.name}-readelf'
    if not readelf.is_file() or root not in readelf.resolve().parents:
        raise ValueError('SDK host runtime export needs its target readelf')
    # The patcher can begin using these as soon as they appear in host/lib.
    # Install libgcc_s first so the new libstdc++ never sees an older host copy.
    names = ('libgcc_s.so.1', 'libstdc++.so.6')
    sources = {}
    for name in names:
        candidates = set()
        for directory in ('lib', 'lib64', 'usr/lib', 'usr/lib64'):
            for path in (sysroot / directory).rglob(name):
                resolved = path.resolve()
                if sysroot not in resolved.parents or not resolved.is_file():
                    raise ValueError(f'Missing/escaping target C++ runtime: {path}')
                candidates.add(resolved)
        if not candidates:
            raise ValueError(f'Missing target C++ runtime for SDK host tools: {name}')
        if len({digest(path) for path in candidates}) != 1:
            raise ValueError(f'Conflicting target C++ runtimes named {name}')
        source = sorted(candidates)[0]
        header = elf_header(source)
        if not header or header[0] != 2 or header[1] != 3 or header[2] != 62:
            raise ValueError(f'Target C++ runtime is not x86_64 ELF: {source}')
        soname = subprocess.check_output([patcher, '--print-soname', source], text=True).strip()
        if soname != name:
            raise ValueError(f'Unexpected target C++ runtime SONAME: {source}: {soname}')
        sources[name] = source

    providers = {name: elf_versions(path, readelf)[0] for name, path in sources.items()}
    target_directory = sysroot.parent
    consumers = []
    glibc_library = re.compile(r'^(?:ld-linux[^/]*|ld64[^/]*|lib(?:c|m|mvec|dl|pthread|rt|util|resolv|anl|thread_db|BrokenLocale|nss_[^.]+)\.so(?:\..*)?)$')
    for path in sorted(root.rglob('*')):
        if (not path.is_file() or path.is_symlink() or target_directory in path.parents or
                path in [root / 'lib' / name for name in names]):
            continue
        header = elf_header(path)
        if not header or header[1] not in (2, 3):
            continue
        needed = subprocess.check_output([patcher, '--print-needed', path], text=True).splitlines()
        for name in needed:
            if name in names or glibc_library.fullmatch(name):
                continue
            candidates = [root / directory / name for directory in ('lib', 'lib64')]
            if ('/' in name or not any(candidate.is_file() and root in candidate.resolve().parents
                    and target_directory not in candidate.resolve().parents for candidate in candidates)):
                raise ValueError(f'SDK host dependency {name}, needed by {path}, is absent from SDK host/lib; host libraries are not searched')
        if set(names).intersection(needed):
            consumers.append(path)
    # A future host bootstrap compiler may be newer than the selected SDK GCC.
    # Check named ABI versions before changing any SDK files, including the
    # target libstdc++ library's own requirements on the copied libgcc_s.
    for path in [*sources.values(), *consumers]:
        required = elf_versions(path, readelf)[1]
        for name, offered in providers.items():
            missing = required.get(name, set()) - offered
            if missing:
                raise ValueError(f'SDK host consumer {path} needs unsupported {name} ABI versions: {", ".join(sorted(missing))}')

    def patch_copy(source, destination, rpath):
        # patchelf may itself be using these libraries. Never rewrite an inode
        # that another running process has mapped; patch then atomically replace.
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(prefix='.datapump-runtime-', dir=destination.parent,
                                         delete=False) as stream:
            temporary = Path(stream.name)
        try:
            shutil.copy2(source, temporary)
            subprocess.run([patcher, '--set-rpath', rpath, temporary], check=True,
                           capture_output=True, text=True)
            temporary.replace(destination)
        finally:
            temporary.unlink(missing_ok=True)

    for name, source in sources.items():
        patch_copy(source, root / 'lib' / name, '$ORIGIN')

    # Buildroot normally makes these RPATHs relative in prepare-sdk. Enforce the
    # runtime lookup for direct consumers, including deep GCC plugin locations.
    # The target compiler's libraries outside sysroot remain target files too.
    for path in consumers:
        relative_lib = os.path.relpath(root / 'lib', path.parent)
        rpaths = ['$ORIGIN' if relative_lib == '.' else f'$ORIGIN/{relative_lib}']
        existing = subprocess.check_output([patcher, '--print-rpath', path], text=True).strip()
        for entry in existing.split(':'):
            expanded = entry.replace('${ORIGIN}', str(path.parent)).replace('$ORIGIN', str(path.parent))
            if not expanded or not Path(expanded).is_absolute() or '$' in expanded:
                continue
            resolved = Path(expanded).resolve()
            if resolved != root and root not in resolved.parents:
                continue
            relative = os.path.relpath(resolved, path.parent)
            preserved = '$ORIGIN' if relative == '.' else f'$ORIGIN/{relative}'
            if preserved not in rpaths:
                rpaths.append(preserved)
        wanted = ':'.join(rpaths)
        if existing != wanted:
            patch_copy(path, path, wanted)
    return {f'lib/{name}': {'target_source': str(source.relative_to(root)),
                             'sha256': digest(root / 'lib' / name)}
            for name, source in sources.items()}


def export_sdk(cache, output, manifest, identity):
    sdk = output / 'host'
    triple = manifest['target']
    sysroot = sdk / triple / 'sysroot'
    prune_sdk_runtime_paths(sdk, sysroot)
    normalized_links(sdk, sysroot)
    host_cxx_runtime = bundle_host_cxx_runtime(sdk, sysroot, manifest['architecture'])
    meta = sdk / 'share/datapump-sdk'
    meta.mkdir(parents=True, exist_ok=True)
    license_output = output / 'legal-info'
    for name in ('licenses', 'host-licenses'):
        shutil.copytree(license_output / name, meta / 'licenses' / name, dirs_exist_ok=True)
    for name in ('manifest.csv', 'host-manifest.csv', 'README'):
        shutil.copy2(license_output / name, meta / name)
    write_json(meta / 'manifest.json', {
        'schema_version': 1, 'id': identity,
        'baseline': {'glibc': manifest['glibc']},
        'supported_backends': ['fltk', 'rev'],
        'target': {'triple': triple, 'processor': manifest['architecture'],
                   'sysroot': f'{triple}/sysroot', 'c_compiler': f'bin/{triple}-gcc',
                   'cxx_compiler': f'bin/{triple}-g++'},
        'recipe': manifest,
        'host_cxx_runtime': host_cxx_runtime,
    })
    shutil.copy2(cache / 'sources.json', meta / 'sources.json')
    if (cache / 'resolution.json').is_file():
        shutil.copy2(cache / 'resolution.json', meta / 'resolution.json')
    (meta / 'relocated-root.txt').write_text(str(sdk.resolve()) + '\n')
    requirements = verify_sdk(sdk, None)
    metadata = json.loads((meta / 'manifest.json').read_text())
    metadata['observed_glibc'] = {key: '.'.join(map(str, version)) for key, version in requirements.items()}
    write_json(meta / 'manifest.json', metadata)
    destination = cache / 'releases' / f'datapump-sdk-{identity}-linux-x86_64.tar.gz'
    archive_tree(destination, [(sdk, destination.name.removesuffix('.tar.gz'))], manifest['source_date_epoch'])
    print(destination)


def export_sources(cache, manifest, identity):
    preserved = validate_sources(cache, identity)
    bootstrap = cache / 'bootstrap' / manifest['buildroot']['file']
    fetch_file(manifest['buildroot'], bootstrap, download=False)
    name = f'datapump-sdk-sources-{identity}'
    entries = [(RECIPE, f'{name}/third_party/build-support/source-sdk'),
               (Path(__file__).resolve(), f'{name}/tools/build-sdk.py'),
               (ROOT / 'LICENSE', f'{name}/LICENSE')]
    relative_cache = f'{name}/third_party/build-support/cache/source-sdk'
    entries += [(bootstrap, f'{relative_cache}/bootstrap/{bootstrap.name}'),
                (cache / 'sources.json', f'{relative_cache}/sources.json')]
    if (cache / 'resolution.json').is_file():
        entries.append((cache / 'resolution.json', f'{relative_cache}/resolution.json'))
    # Materialize any cache-internal aliases under their inventoried names;
    # unselected old package versions and download bookkeeping stay in cache.
    entries += [((cache / name).resolve(), f'{relative_cache}/{name}') for name in sorted(preserved)]
    destination = cache / 'releases' / f'{name}.tar.gz'
    archive_tree(destination, entries, manifest['source_date_epoch'])
    print(destination)


def check_archive(archive, expected):
    if expected is None:
        sums = archive.parent / 'SHA256SUMS'
        if not sums.is_file():
            raise ValueError('Provide adjacent SHA256SUMS or --sha256 for the SDK archive')
        matches = [line.split()[0] for line in sums.read_text().splitlines()
                   if len(line.split()) == 2 and line.split()[1] == archive.name]
        if len(matches) != 1:
            raise ValueError('SDK archive must appear exactly once in SHA256SUMS')
        expected = matches[0]
    if digest(archive) != expected:
        raise ValueError('SDK archive checksum mismatch')


def install_sdk(archive, destination, expected):
    check_archive(archive, expected)
    destination = destination.absolute()
    if destination.exists() or destination.is_symlink():
        raise ValueError(f'Destination already exists: {destination}')
    if not re.fullmatch(r'[A-Za-z0-9_./+-]+', str(destination)):
        raise ValueError('SDK installation path must not contain whitespace/metacharacters')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
        staged = Path(temporary)
        safe_extract(archive, staged)
        roots = list(staged.iterdir())
        if len(roots) != 1 or not (roots[0] / 'share/datapump-sdk/manifest.json').is_file():
            raise ValueError('Archive is not a DataPump SDK')
        metadata = json.loads((roots[0] / 'share/datapump-sdk/manifest.json').read_text())
        if metadata.get('schema_version') != 1:
            raise ValueError('Unsupported SDK manifest schema')
        relocation = roots[0] / 'relocate-sdk.sh'
        if relocation.is_symlink() or not relocation.is_file():
            raise ValueError('Missing ordinary SDK relocation script')
        roots[0].rename(destination)
    try:
        run([destination / 'relocate-sdk.sh'])
        (destination / 'share/datapump-sdk/relocated-root.txt').write_text(str(destination.resolve()) + '\n')
    except BaseException:
        shutil.rmtree(destination)
        raise
    print(f'Installed SDK: {destination}')


def glibc_versions(path, readelf):
    with path.open('rb') as stream:
        if stream.read(4) != b'\x7fELF':
            return []
    result = subprocess.run([str(readelf), '--version-info', '--wide', str(path)],
                            check=True, capture_output=True, text=True)
    # Definitions in libc itself are useful too: the target sysroot must not
    # accidentally contain a libc newer than its declared baseline.
    return [tuple(map(int, v.split('.'))) for v in re.findall(r'\bGLIBC_([0-9]+(?:\.[0-9]+)+)', result.stdout)]


def verify_sdk(root, max_host):
    root = root.resolve()
    meta = root / 'share/datapump-sdk'
    manifest = json.loads((meta / 'manifest.json').read_text())
    if (meta / 'relocated-root.txt').read_text().strip() != str(root):
        raise ValueError('SDK needs explicit installation/relocation before verification')
    def member(relative):
        path = (root / relative).resolve()
        if root not in path.parents or not path.exists():
            raise ValueError(f'Missing/escaping SDK member: {relative}')
        return path
    sysroot = member(manifest['target']['sysroot'])
    compiler = root / manifest['target']['cxx_compiler']
    member(manifest['target']['cxx_compiler'])
    readelf = member(f'bin/{manifest["target"]["triple"]}-readelf')
    required = {'host': (0,), 'target': (0,)}
    seen = set()
    for path in sorted(root.rglob('*')):
        if not path.is_file() or path.is_symlink():
            continue
        actual = path.resolve()
        if actual in seen:
            continue
        seen.add(actual)
        category = 'target' if sysroot in actual.parents else 'host'
        for version in glibc_versions(path, readelf):
            required[category] = max(required[category], version)
    ceiling = tuple(map(int, manifest['baseline']['glibc'].split('.')))
    if required['target'] > ceiling:
        raise ValueError(f'Target ABI {required["target"]} exceeds {ceiling}')
    if max_host and required['host'] > tuple(map(int, max_host.split('.'))):
        raise ValueError(f'SDK host tools require glibc {required["host"]}, above {max_host}')
    with tempfile.TemporaryDirectory(prefix='sdk-probe-') as temporary:
        source = Path(temporary) / 'probe.cpp'
        source.write_text('#include <span>\n#include <cstdio>\nint main(){int x[]={1}; std::span s(x); return s.size()!=1;}\n')
        executable = Path(temporary) / 'probe'
        run([compiler, '-std=c++20', source, '-static-libstdc++', '-static-libgcc', '-o', executable])
        if any(v > ceiling for v in glibc_versions(executable, readelf)):
            raise ValueError('Compiler probe exceeded target glibc baseline')
        run([executable])
    print('SDK glibc requirements:', required)
    return required


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['id', 'fetch', 'build', 'sources', 'install', 'verify'])
    parser.add_argument('sdk', nargs='?', type=Path, help='SDK directory for verify')
    parser.add_argument('--cache-dir', type=Path, default=DEFAULT_CACHE)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--download', action='store_true', help='Explicitly allow downloads during build')
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--destination', type=Path)
    parser.add_argument('--sha256')
    parser.add_argument('--max-host-glibc', help='Release SDKs must pass with 2.36')
    args = parser.parse_args()
    try:
        if args.jobs < 1:
            raise ValueError('--jobs must be positive')
        if args.command == 'id':
            print(recipe_id())
            return
        if args.command == 'install':
            if not args.archive or not args.destination:
                raise ValueError('install requires --archive and --destination')
            install_sdk(args.archive.resolve(), args.destination, args.sha256)
            return
        if args.command == 'verify':
            if not args.sdk:
                raise ValueError('verify requires an SDK directory')
            verify_sdk(args.sdk, args.max_host_glibc)
            return
        cache = args.cache_dir.resolve()
        cache.mkdir(parents=True, exist_ok=True)
        with (cache / '.lock').open('w') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            manifest = json.loads((RECIPE / 'manifest.json').read_text())
            identity = recipe_id()
            if args.command == 'sources':
                export_sources(cache, manifest, identity)
                return
            download = args.command == 'fetch' or args.download
            if not download:
                validate_sources(cache, identity)
            command, output = prepare(cache, manifest, identity, download, args.jobs)
            if download:
                run(command + ['source'])
                packages = json.loads(subprocess.check_output(
                    command + ['--no-print-directory', '-s', 'show-info'], text=True))
                source_files = resolved_downloads(cache, packages)
                write_json(cache / 'resolution.json', {
                    'id': identity, 'buildroot_config': (output / '.config').read_text(),
                    'packages': packages,
                    'bootstrap': {'system': platform.platform(),
                                  'libc': platform.libc_ver(),
                                  'python': platform.python_version()}})
                write_json(cache / 'sources.json', {'id': identity, 'files': {
                    str(p.relative_to(cache)): digest(p) for p in source_files}})
            if args.command == 'build':
                # No fetch is ever hidden inside the default application build.
                run(command + ['prepare-sdk', 'legal-info'])
                export_sdk(cache, output, manifest, identity)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError, tarfile.TarError) as error:
        parser.exit(1, f'SDK preparation failed: {error}\n')


if __name__ == '__main__':
    main()
