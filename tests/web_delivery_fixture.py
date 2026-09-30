"""Complete browser/pipe-worker payload for distribution packaging fixtures.

The compiled module is a fixture; page assembly, notices and inventories use the
production packager. No browser, network server or SDK compiler is launched.
"""
from functools import lru_cache
import importlib.util
import hashlib
import json
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]
WEB = {'schema': 1, 'browser': 'wasm',
       'worker_platforms': ['linux-x86_64', 'linux-aarch64']}


def payload(metadata):
    return _payload(metadata["source_sha"], metadata["dependencies"]["wasm-sdk"]).copy()


@lru_cache(maxsize=8)
def _payload(source_sha, recipe_id):
    spec = importlib.util.spec_from_file_location('delivery_wasm_package', ROOT / 'tools/package-wasm.py')
    package = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(package)
    with tempfile.TemporaryDirectory(prefix='web-delivery-') as temporary:
        root = Path(temporary)
        (root / 'module.js').write_text('const createDataPump = async () => {};')
        (root / 'module.wasm').write_bytes(b'\0asm\x01\0\0\0')
        notices = root / 'notices'
        notices.mkdir()
        for name in package.SDK_NOTICES:
            (notices / name).write_text('Fixture SDK license: ' + name + '\n')
        output = root / 'page'
        package.package(root / 'module.js', root / 'module.wasm', ROOT / 'web', output, notices)
        files = {f'share/datapump/web/wasm/{path.name}': (path.read_bytes(), 0o644)
                 for path in output.iterdir()}
        files.update({f'share/doc/datapump/wasm-runtime-notices/{path.name}': (path.read_bytes(), 0o644)
                      for path in notices.iterdir()})
    files['share/doc/datapump/wasm-sdk-manifest.json'] = (
        json.dumps({'schema_version': 1, 'recipe_id': recipe_id, 'fixture': True, 'recipe_sha256': hashlib.sha256((ROOT / 'third_party/build-support/wasm-sdk/manifest.json').read_bytes()).hexdigest()}).encode(), 0o644)
    files['share/doc/datapump/wasm-build-info.txt'] = (
        (f'Source commit: {source_sha}\nWebAssembly: ON (local messages; no sockets)\n'
         + 'Wasm SDK manifest SHA-256: ' + hashlib.sha256(files['share/doc/datapump/wasm-sdk-manifest.json'][0]).hexdigest() + '\n').encode(), 0o644)
    files['share/doc/datapump/wasm-sdk-recipe.json'] = (
        (ROOT / 'third_party/build-support/wasm-sdk/manifest.json').read_bytes(), 0o644)
    files['bin/datapump-worker'] = (b'#!/bin/sh\nexit 0\n', 0o755)
    files['share/man/man1/datapump-worker.1'] = ((ROOT / 'docs/man/datapump-worker.1').read_bytes(), 0o644)
    files.update({f'share/datapump/web/hosted/{path.name}': (path.read_bytes(), 0o644)
                  for path in (ROOT / 'web').iterdir() if path.is_file() and path.suffix != '.md'})
    manifest = {'schema_version': 1, 'target': 'datapump-worker', 'protocol': 'DPW1',
                'audio': 'browser', 'files': 'host', 'transport': 'inherited-anonymous-pipes',
                'socket_policy': 'forbidden', 'unsupported_platforms': 'fail-closed'}
    files['share/datapump/web/hosted/web-manifest.json'] = (json.dumps(manifest).encode(), 0o644)
    return files
