#!/usr/bin/env python3
"""Prepare the pinned Emscripten SDK explicitly; application builds are offline."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
RECIPE = ROOT / 'third_party/build-support/wasm-sdk/manifest.json'
ENTROPY_CAPABILITY = 'emscripten-getentropy-webcrypto-v1'
RUNTIME_NOTICES = {
    'Emscripten-LICENSE.txt': 'LICENSE',
    'Emscripten-AUTHORS.txt': 'AUTHORS',
    'compiler-rt-LICENSE.txt': 'system/lib/compiler-rt/LICENSE.TXT',
    'libcxx-LICENSE.txt': 'system/lib/libcxx/LICENSE.TXT',
    'libcxxabi-LICENSE.txt': 'system/lib/libcxxabi/LICENSE.TXT',
    'libunwind-LICENSE.txt': 'system/lib/libunwind/LICENSE.TXT',
    'llvm-libc-LICENSE.txt': 'system/lib/llvm-libc/LICENSE.TXT',
    'musl-COPYRIGHT.txt': 'system/lib/libc/musl/COPYRIGHT',
}


def copy_runtime_notices(emscripten, openssl, destination):
    destination.mkdir(parents=True, exist_ok=True)
    for name, relative in RUNTIME_NOTICES.items():
        shutil.copy2(emscripten / relative, destination / name)
    shutil.copy2(openssl / 'LICENSE.txt', destination / 'OpenSSL-LICENSE.txt')
    allocator = (emscripten / 'system/lib/dlmalloc.c').read_text()
    start = allocator.index(' This is a version (aka dlmalloc)')
    end = allocator.index(' * Quickstart', start)
    (destination / 'dlmalloc-NOTICE.txt').write_text(allocator[start:end].strip() + '\n')


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def recipe():
    value = json.loads(RECIPE.read_text())
    if value.get('schema_version') != 1 or value.get('target') != 'wasm32-emscripten':
        raise ValueError('Unsupported Wasm SDK recipe')
    for item in value['inputs']:
        if Path(item['file']).name != item['file'] or len(item['sha256']) != 64:
            raise ValueError('Invalid pinned input')
    entropy = value.get('entropy', {})
    if entropy.get('capability') != ENTROPY_CAPABILITY:
        raise ValueError('Unsupported Wasm SDK entropy adapter')
    for field in ('patch', 'source'):
        name = PurePosixPath(entropy.get(field, ''))
        if not name.parts or name.is_absolute() or '..' in name.parts:
            raise ValueError('Invalid entropy adapter path')
    patch = RECIPE.parent / entropy['patch']
    if patch.is_symlink() or digest(patch) != entropy.get('patch_sha256'):
        raise ValueError('Wasm SDK entropy patch checksum mismatch')
    if entropy.get('probe_sources') != ['entropy-probe.cpp', 'entropy-probe.mjs']:
        raise ValueError('Unsupported Wasm SDK entropy probe')
    return value


def patch_openssl_entropy(openssl, value):
    """Apply only the exact reviewed source transformation during preparation."""
    entropy = value['entropy']
    source = openssl / entropy['source']
    if source.is_symlink() or digest(source) != entropy['source_sha256']:
        raise ValueError('OpenSSL entropy source differs from the pinned patch input')
    run(['patch', '--batch', '--fuzz=0', '--no-backup-if-mismatch', '-p1',
         '-i', RECIPE.parent / entropy['patch']], cwd=openssl)
    if digest(source) != entropy['patched_sha256']:
        raise ValueError('OpenSSL entropy patch output checksum mismatch')


def qualify_entropy(em, node, target, unpack, env):
    """Exercise real OpenSSL and repeated browser WebCrypto reseeding offline."""
    output = unpack / 'entropy-probe.js'
    run([em / 'em++', RECIPE.parent / 'entropy-probe.cpp', '-std=c++20', '-O2',
         '-I' + str(target / 'include'), target / 'lib/libcrypto.a', '--no-entry',
         '-sMODULARIZE=1', '-sEXPORT_NAME=DatapumpEntropyProbe',
         '-sENVIRONMENT=web,worker', '-sFILESYSTEM=1',
         '-sEXPORTED_FUNCTIONS=["_datapump_entropy_probe"]', '-o', output], env=env)
    run([node, RECIPE.parent / 'entropy-probe.mjs', output, output.with_suffix('.wasm')], env=env)


def source_inputs(sources, download=False):
    sources.mkdir(parents=True, exist_ok=True)
    value = recipe()
    for item in value['inputs']:
        path = sources / item['file']
        if not path.exists():
            if not download:
                raise ValueError(f'Missing {path}; explicitly use fetch --download first')
            partial = path.with_suffix(path.suffix + '.partial')
            with urllib.request.urlopen(item['url'], timeout=120) as source, partial.open('xb') as output:
                shutil.copyfileobj(source, output)
            if digest(partial) != item['sha256']:
                raise ValueError(f'Checksum mismatch: {partial}')
            partial.rename(path)
        if path.is_symlink() or digest(path) != item['sha256']:
            raise ValueError(f'Checksum mismatch or linked source: {path}')
    return value


def extract(archive, destination):
    """Reject traversal, special files, duplicate paths and escaping links."""
    destination.mkdir(parents=True, exist_ok=True)
    seen = set()
    with tarfile.open(archive) as source:
        for member in source.getmembers():
            name = PurePosixPath(member.name)
            if name.is_absolute() or '..' in name.parts or str(name) in seen:
                raise ValueError(f'Unsafe archive member: {member.name}')
            seen.add(str(name))
            if not (member.isfile() or member.isdir() or member.issym() or member.islnk()):
                raise ValueError(f'Unsupported archive entry: {member.name}')
            path = destination / member.name
            if destination.resolve() not in path.parent.resolve().parents and path.parent.resolve() != destination.resolve():
                raise ValueError(f'Escaping archive member: {member.name}')
            if member.issym() or member.islnk():
                target = (path.parent if member.issym() else destination) / member.linkname
                if destination.resolve() not in target.resolve().parents:
                    raise ValueError(f'Escaping archive link: {member.name}')
            if path.is_symlink():
                raise ValueError(f'Archive write through symlink: {member.name}')
            source.extract(member, destination, filter='fully_trusted')


def run(args, **kwargs):
    print('+', ' '.join(str(arg) for arg in args), flush=True)
    subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def configure_environment(destination):
    env = os.environ.copy()
    for name in ('CC', 'CXX', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS', 'CPATH', 'C_INCLUDE_PATH',
                 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH', 'EMMAKEN_CFLAGS', 'EMCC_CFLAGS'):
        env.pop(name, None)
    env.update(EM_CONFIG=str(destination / '.emscripten'), EM_CACHE=str(destination / 'cache'))
    env['PATH'] = str(destination / 'emsdk/upstream/emscripten') + os.pathsep + env['PATH']
    return env


def prepare(sources, destination, jobs):
    value = source_inputs(sources)
    if platform.system() != 'Linux' or platform.machine() != 'x86_64':
        raise ValueError('This pinned host recipe currently qualifies Linux x86_64 only')
    if destination.exists():
        raise ValueError('Refusing to overwrite an SDK; select an empty destination')
    destination.mkdir(parents=True)
    unpack = destination / 'work'
    by_name = {item['name']: item for item in value['inputs']}
    extract(sources / by_name['emsdk']['file'], unpack)
    (unpack / ('emsdk-' + value['emscripten_version'])).rename(destination / 'emsdk')
    extract(sources / by_name['compiler']['file'], unpack)
    # Official compiler archives contain install/{bin,emscripten,...}.
    (unpack / 'install').rename(destination / 'emsdk/upstream')
    extract(sources / by_name['node']['file'], unpack)
    (unpack / ('node-v' + value['node_version'] + '-linux-x64')).rename(destination / 'node')
    config = "import os\nROOT = os.path.dirname(os.path.abspath(__file__))\nLLVM_ROOT = os.path.join(ROOT, 'emsdk/upstream/bin')\nBINARYEN_ROOT = os.path.join(ROOT, 'emsdk/upstream')\nNODE_JS = [os.path.join(ROOT, 'node/bin/node')]\nCACHE = os.path.join(ROOT, 'cache')\n"
    (destination / '.emscripten').write_text(config)
    env = configure_environment(destination)
    em = destination / 'emsdk/upstream/emscripten'
    run([em / 'emcc', '--version'], env=env)
    extract(sources / by_name['openssl']['file'], unpack)
    openssl = unpack / ('openssl-' + value['openssl_version'])
    patch_openssl_entropy(openssl, value)
    target = destination / 'target'
    ssl_env = env | {'CC': str(em / 'emcc'), 'AR': str(em / 'emar'), 'RANLIB': str(em / 'emranlib')}
    run(['perl', 'Configure', 'linux-generic32', '--prefix=' + str(target), '--libdir=lib',
         *value['openssl_options']], cwd=openssl, env=ssl_env)
    run(['make', '-j' + str(jobs), 'build_libs'], cwd=openssl, env=ssl_env)
    run(['make', 'install_dev'], cwd=openssl, env=ssl_env)
    qualify_entropy(em, destination / 'node/bin/node', target, unpack, env)
    # Populate the exact libc/C++/exception variant before freezing the cache.
    probe = unpack / 'cache-probe.cpp'
    probe.write_text('#include <filesystem>\n#include <iostream>\n#include <stdexcept>\n#include <emscripten/fiber.h>\nint main(){ try { throw std::runtime_error("probe"); } catch(...) {std::cout << std::filesystem::path("/");} }\n')
    run([em / 'em++', probe, '-std=c++20', '-fexceptions', '-sASYNCIFY=1',
         '-sENVIRONMENT=web,worker', '-sFILESYSTEM=1', '-o', unpack / 'cache-probe.js'], env=env)
    share = destination / 'share/datapump-wasm-sdk'
    share.mkdir(parents=True)
    shutil.copy2(RECIPE, share / 'recipe.json')
    shutil.copy2(Path(__file__), share / 'build-wasm-sdk.py')
    support = [value['entropy']['patch'], *value['entropy']['probe_sources']]
    for name in support:
        shutil.copy2(RECIPE.parent / name, share / name)
    support_hashes = {name: digest(RECIPE.parent / name) for name in support}
    (share / 'preparation-provenance.json').write_text(json.dumps({
        'builder_sha256': digest(Path(__file__)), 'recipe_sha256': digest(RECIPE),
        'support_sha256': support_hashes, 'entropy_probe': 'passed'}, indent=2, sort_keys=True) + '\n')
    copy_runtime_notices(em, openssl, share / 'licenses')
    # Preserve every exact source/prebuilt input for offline reconstruction.
    (share / 'sources').mkdir()
    for item in value['inputs']:
        shutil.copy2(sources / item['file'], share / 'sources' / item['file'])
    manifest = {'schema_version': 1, 'target': value['target'], 'host': 'linux-x86_64',
                'emscripten_version': value['emscripten_version'], 'recipe_sha256': digest(RECIPE),
                'compiler': 'emsdk/upstream/emscripten/emcc', 'cxx_compiler': 'emsdk/upstream/emscripten/em++',
                'toolchain': 'emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake',
                'openssl_include': 'target/include', 'openssl_crypto': 'target/lib/libcrypto.a',
                'openssl_crypto_sha256': digest(target / 'lib/libcrypto.a'),
                'entropy_capability': ENTROPY_CAPABILITY,
                'entropy_patch_sha256': value['entropy']['patch_sha256'],
                'entropy_probe': 'passed', 'support_sha256': support_hashes,
                'sources': value['inputs'], 'cache_frozen': True}
    (share / 'manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
    (share / 'relocated-root.txt').write_text(str(destination) + '\n')
    print('Prepared offline Wasm SDK:', destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['fetch', 'prepare', 'verify'])
    parser.add_argument('--sources', type=Path, required=True)
    parser.add_argument('--destination', type=Path)
    parser.add_argument('--download', action='store_true')
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    try:
        if args.jobs < 1:
            raise ValueError('--jobs must be positive')
        if args.download and args.command != 'fetch':
            raise ValueError('Only explicit fetch --download may access the network')
        if args.command == 'prepare':
            if not args.destination:
                raise ValueError('prepare requires --destination')
            prepare(args.sources.resolve(), args.destination.resolve(), args.jobs)
        else:
            source_inputs(args.sources.resolve(), args.download)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + '\n')

if __name__ == '__main__':
    main()
