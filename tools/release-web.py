#!/usr/bin/env python3
"""Verify and merge independently built browser assets into native releases."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CAPABILITY = {'schema': 1, 'browser': 'wasm',
              'worker_platforms': ['linux-x86_64', 'linux-aarch64']}
WEB = 'share/datapump/web/wasm/'
DOC = 'share/doc/datapump/'
HOSTED = 'share/datapump/web/hosted/'
HOSTED_FILES = ('hosted.mjs', 'renderer.mjs', 'protocol.mjs',
                'browser_audio.mjs', 'audio_worklet.js', 'style.css')
SPEC = importlib.util.spec_from_file_location('datapump_package_wasm', ROOT / 'tools/package-wasm.py')
wasm = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(wasm)


def archive_files(path):
    """Read a confined, regular-file package without extracting paths."""
    files, roots, seen = {}, set(), set()
    total = 0
    def accept(name, size, mode, directory, read):
        nonlocal total
        name = name.removeprefix('./').rstrip('/')
        parts = name.split('/')
        if (not name or any(part in ('', '.', '..') for part in parts)
                or '\\' in name or any(ord(c) < 32 for c in name) or name in seen):
            raise ValueError('Unsafe or duplicate browser delivery archive path')
        seen.add(name)
        roots.add(parts[0])
        if directory:
            return
        total += size
        if len(parts) < 2 or size > 512 * 1024 * 1024 or total > 2 * 1024 * 1024 * 1024:
            raise ValueError('Oversized or unrooted browser delivery archive')
        files['/'.join(parts[1:])] = (read(), mode & 0o777)
    if path.name.endswith('.tar.gz'):
        with tarfile.open(path, 'r:gz') as source:
            for item in source:
                if not item.isdir() and not item.isfile():
                    raise ValueError('Linked or special browser delivery archive member')
                accept(item.name, item.size, item.mode, item.isdir(),
                       lambda item=item: source.extractfile(item).read())
    elif path.name.endswith('.zip'):
        with zipfile.ZipFile(path) as source:
            for item in source.infolist():
                kind = (item.external_attr >> 16) & 0o170000
                if kind not in (0, 0o040000 if item.is_dir() else 0o100000):
                    raise ValueError('Linked or special browser delivery archive member')
                accept(item.filename, item.file_size, item.external_attr >> 16, item.is_dir(),
                       lambda item=item: source.read(item))
    else:
        raise ValueError('Unsupported browser delivery archive format')
    if len(roots) != 1 or not files:
        raise ValueError('Browser delivery archive needs one package root')
    return next(iter(roots)), files


def manifest(files):
    return ''.join(hashlib.sha256(data).hexdigest() + '  ' + name + '\n'
                   for name, (data, _) in sorted(files.items()) if name != 'manifest.sha256').encode()


def verify_manifest(files):
    if 'manifest.sha256' not in files:
        raise ValueError('Browser delivery package lacks its complete package manifest')
    actual = {}
    for line in files['manifest.sha256'][0].decode('utf-8').splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  (.+)', line)
        if not match or match[2] in actual:
            raise ValueError('Invalid or duplicate package manifest entry')
        actual[match[2]] = match[1]
    expected = {name: hashlib.sha256(data).hexdigest() for name, (data, _) in files.items()
                if name != 'manifest.sha256'}
    if actual != expected:
        raise ValueError('Browser delivery package manifest does not match its files')


def verify_browser(files, metadata, *, source_info='wasm-build-info.txt'):
    wasm.verify_payload({name[len(WEB):]: data for name, (data, _) in files.items() if name.startswith(WEB)})
    required = {DOC + name for name in ('wasm-sdk-manifest.json', 'wasm-sdk-recipe.json', source_info)}
    required.update(DOC + 'wasm-runtime-notices/' + name for name in wasm.SDK_NOTICES)
    if not required <= files.keys():
        raise ValueError('Browser delivery lacks SDK provenance or dependency notices')
    sdk = json.loads(files[DOC + 'wasm-sdk-manifest.json'][0])
    if sdk.get('recipe_id') != metadata['dependencies']['wasm-sdk']:
        raise ValueError('Browser delivery SDK recipe differs from release metadata')
    if hashlib.sha256(files[DOC + 'wasm-sdk-recipe.json'][0]).hexdigest() != sdk.get('recipe_sha256'):
        raise ValueError('Browser SDK recipe document differs from prepared SDK manifest')
    info = files[DOC + source_info][0].decode('utf-8')
    sdk_hash = hashlib.sha256(files[DOC + 'wasm-sdk-manifest.json'][0]).hexdigest()
    if ('Wasm SDK manifest SHA-256: ' + sdk_hash) not in info.splitlines():
        raise ValueError('Browser SDK manifest differs from build provenance')
    if ('Source commit: ' + metadata['source_sha']) not in info.splitlines():
        raise ValueError('Browser delivery source differs from release metadata')
    if 'WebAssembly: ON (local messages; no sockets)' not in info.splitlines():
        raise ValueError('Browser delivery build provenance is not a Wasm composition')
    notices = json.loads(files[WEB + 'web-manifest.json'][0])['notices']
    for name in wasm.SDK_NOTICES:
        if hashlib.sha256(files[DOC + 'wasm-runtime-notices/' + name][0]).hexdigest() != notices[name]:
            raise ValueError('Browser SDK notice differs from compiled page inventory')


def browser_payload(directory, metadata):
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError('Missing browser producer artifacts')
    names = {p.name for p in directory.iterdir()}
    archives = sorted(name for name in names if name.endswith(('.tar.gz', '.zip')))
    if (len(archives) != 2 or len({name.removesuffix('.tar.gz').removesuffix('.zip') for name in archives}) != 1
            or names != set(archives) | {'SHA256SUMS.txt'}):
        raise ValueError('Expected one browser TGZ/ZIP pair and its checksum inventory')
    sums = {}
    for line in (directory / 'SHA256SUMS.txt').read_text().splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  ([A-Za-z0-9_.-]+)', line)
        if not match or match[2] in sums:
            raise ValueError('Invalid browser artifact checksum inventory')
        sums[match[2]] = match[1]
    if set(sums) != set(archives):
        raise ValueError('Incomplete browser artifact checksum inventory')
    payloads = []
    for name in archives:
        path = directory / name
        if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest() != sums[name]:
            raise ValueError('Modified browser producer archive')
        _, files = archive_files(path)
        # The Wasm-only CPack installation does not use the native package collector.
        verify_browser(files, metadata, source_info='build-info.txt')
        selected = {key: value for key, value in files.items() if key.startswith((WEB, DOC + 'wasm-'))}
        selected[DOC + 'wasm-build-info.txt'] = files[DOC + 'build-info.txt']
        payloads.append(selected)
    if {k: v[0] for k, v in payloads[0].items()} != {k: v[0] for k, v in payloads[1].items()}:
        raise ValueError('Browser archive formats contain different delivery payloads')
    return payloads[0]


def write_archive(path, root, files):
    if path.name.endswith('.tar.gz'):
        with tarfile.open(path, 'w:gz') as dest:
            for name, (data, mode) in sorted(files.items()):
                item = tarfile.TarInfo(root + '/' + name)
                item.size, item.mode = len(data), mode or 0o644
                dest.addfile(item, io.BytesIO(data))
    else:
        with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as dest:
            for name, (data, mode) in sorted(files.items()):
                item = zipfile.ZipInfo(root + '/' + name)
                item.create_system = 3
                item.external_attr = (0o100000 | (mode or 0o644)) << 16
                item.compress_type = zipfile.ZIP_DEFLATED
                dest.writestr(item, data)


def bundle(source, destination, browser):
    root, files = archive_files(source)
    verify_manifest(files)
    if set(files) & set(browser):
        raise ValueError('Refusing to replace browser files already in native producer output')
    files.update(browser)
    files['manifest.sha256'] = (manifest(files), 0o644)
    write_archive(destination, root, files)


def verify_native(archive, metadata, target):
    _, files = archive_files(archive)
    verify_manifest(files)
    verify_browser(files, metadata)
    native_info = files.get(DOC + 'build-info.txt', (b'', 0))[0].decode('utf-8')
    if ('Source commit: ' + metadata['source_sha']) not in native_info.splitlines():
        raise ValueError('Native producer source differs from release metadata')
    platform = target.rsplit('-', 1)[0]
    if platform in metadata['web']['worker_platforms']:
        required = {'bin/datapump-worker', 'share/man/man1/datapump-worker.1', HOSTED + 'web-manifest.json'}
        required.update(HOSTED + name for name in HOSTED_FILES)
        if not required <= files.keys():
            raise ValueError('Browser delivery lacks the Linux worker, manual or hosted assets')
        profile = json.loads(files[HOSTED + 'web-manifest.json'][0])
        for name, value in {'schema_version': 1, 'target': 'datapump-worker', 'audio': 'browser',
                            'files': 'host', 'transport': 'inherited-anonymous-pipes', 'socket_policy': 'forbidden'}.items():
            if profile.get(name) != value:
                raise ValueError('Invalid Linux worker composition manifest')
        info = files[DOC + 'build-info.txt'][0].decode('utf-8')
        if 'Web worker: ON (inherited-pipes; no sockets)' not in info.splitlines():
            raise ValueError('Browser delivery lacks Linux worker build provenance')
    elif any(name in files for name in ('bin/datapump-worker', 'bin/datapump-worker.exe')):
        raise ValueError('Unsupported worker platform must use the standalone browser application')
