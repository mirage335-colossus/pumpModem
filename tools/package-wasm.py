#!/usr/bin/env python3
"""Assemble a self-contained browser page from explicitly built local assets."""
import argparse
import base64
import hashlib
from html import escape
import json
from pathlib import Path
import re
import tarfile
import zipfile

MAX_ASSET = 128 * 1024 * 1024
ROOT = Path(__file__).resolve().parents[1]
SDK_NOTICES = ('OpenSSL-LICENSE.txt', 'Emscripten-LICENSE.txt', 'Emscripten-AUTHORS.txt',
               'compiler-rt-LICENSE.txt', 'libcxx-LICENSE.txt', 'libcxxabi-LICENSE.txt',
               'libunwind-LICENSE.txt', 'llvm-libc-LICENSE.txt', 'musl-COPYRIGHT.txt',
               'dlmalloc-NOTICE.txt')
PROJECT_NOTICES = {'DataPump-LICENSE': 'LICENSE', 'QR-LICENSE': 'third_party/qrcodegen/LICENSE',
                   'XZ-LICENSE': 'third_party/xz/COPYING.0BSD',
                   'LDPC-LICENSE': 'third_party/ldpc/LICENSE',
                   'LDPC-WIFI-LICENSE': 'third_party/ldpc/WIFI-LICENSE'}
FORBIDDEN_IMPORT = re.compile(r'socket|sock_|connect|listen|accept|sendto|recvfrom|fetch|http|websocket|pthread|proxy', re.I)


class WasmReader:
    def __init__(self, data):
        self.data, self.at = data, 0

    def byte(self):
        if self.at == len(self.data):
            raise ValueError('Truncated Wasm section')
        result = self.data[self.at]
        self.at += 1
        return result

    def uint(self):
        result = 0
        for shift in range(0, 35, 7):
            value = self.byte()
            if shift == 28 and value > 15:
                raise ValueError('Wasm integer overflow')
            result |= (value & 127) << shift
            if not value & 128:
                return result
        raise ValueError('Invalid Wasm integer')

    def take(self, size):
        if size > len(self.data) - self.at:
            raise ValueError('Truncated Wasm data')
        result = self.data[self.at:self.at + size]
        self.at += size
        return result

    def name(self):
        return self.take(self.uint()).decode('utf-8')

    def limits(self):
        flags = self.uint()
        if flags not in (0, 1):
            raise ValueError('Shared or 64-bit memory is outside this browser profile')
        self.uint()
        if flags & 1:
            self.uint()


def audit_wasm(data):
    reader = WasmReader(data)
    if reader.take(8) != b'\0asm\x01\0\0\0':
        raise ValueError('Expected a WebAssembly version-1 module')
    imports = []
    while reader.at < len(data):
        kind = reader.byte()
        section = WasmReader(reader.take(reader.uint()))
        if kind == 2:
            for _ in range(section.uint()):
                module, name, item = section.name(), section.name(), section.byte()
                if FORBIDDEN_IMPORT.search(module + '.' + name):
                    raise ValueError(f'Forbidden browser import: {module}.{name}')
                if len(name) < 3:
                    raise ValueError('Minified imports cannot be audited; retain ASSERTIONS=1')
                imports.append({'module': module, 'name': name, 'kind': item})
                if item == 0:
                    section.uint()
                elif item == 1:
                    section.byte(); section.limits()
                elif item == 2:
                    section.limits()
                elif item == 3:
                    section.byte(); section.byte()
                elif item == 4:
                    section.byte(); section.uint()
                else:
                    raise ValueError('Unknown Wasm import kind')
        elif kind == 5:
            for _ in range(section.uint()):
                section.limits()
        if kind in (2, 5) and section.at != len(section.data):
            raise ValueError('Trailing data in audited Wasm section')
    return imports


def asset(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_ASSET:
        raise ValueError(f'Missing, linked or oversized asset: {path}')
    return path.read_bytes()


def package(javascript, wasm, assets, destination, sdk_notices):
    if destination.exists() and any(path.name not in {'datapump-wasm.html', 'web-manifest.json', 'manifest.sha256'} for path in destination.iterdir()):
        raise ValueError('Refusing to overwrite a nonempty browser artifact directory')
    blobs = {'factory': asset(javascript), 'wasm': asset(wasm)}
    for name in ('renderer.mjs', 'protocol.mjs', 'browser_audio.mjs', 'wasm_client.mjs', 'wasm_worker.js', 'audio_worklet.js', 'style.css'):
        blobs[name] = asset(assets / name)
    imports = audit_wasm(blobs['wasm'])
    notices = {name: asset(sdk_notices / name) for name in SDK_NOTICES}
    notices.update({name: asset(ROOT / relative) for name, relative in PROJECT_NOTICES.items()})
    notice_text = '\n\n'.join(name + '\n' + data.decode('utf-8') for name, data in sorted(notices.items()))
    # Base64 avoids HTML parser termination and any executable string rewriting.
    encoded = {name: base64.b64encode(data).decode('ascii') for name, data in blobs.items()}
    bootstrap = '''
const assets = JSON.parse(document.getElementById('assets').textContent);
const bytes = name => Uint8Array.from(atob(assets[name]), c => c.charCodeAt(0));
const source = name => new TextDecoder('utf-8', {fatal:true}).decode(bytes(name));
const urls = [];
const localModule = text => {const url=URL.createObjectURL(new Blob([text],{type:'text/javascript'}));urls.push(url);return url;};
try {
    let client=source('wasm_client.mjs');
    for (const name of ['renderer.mjs','protocol.mjs','browser_audio.mjs']) {
        const url=localModule(source(name));
        client=client.replaceAll("'./"+name+"'",JSON.stringify(url)).replaceAll('"./'+name+'"',JSON.stringify(url));
    }
    const {boot}=await import(localModule(client));
    await boot({root:document.getElementById('app'),standalone:true,factorySource:source('factory'),
        wasmBytes:bytes('wasm'),workletSource:source('audio_worklet.js'),workerSource:source('wasm_worker.js')});
} catch(error) {
    const message=document.getElementById('failure');message.hidden=false;message.textContent=String(error);
} finally { for (const url of urls) URL.revokeObjectURL(url); }
'''
    style = blobs['style.css'].decode('utf-8')
    if '</style' in style.lower():
        raise ValueError('Unsafe style element terminator')
    html = '''<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'unsafe-inline' 'wasm-unsafe-eval' blob:; worker-src blob:; connect-src 'none'; style-src 'unsafe-inline'; img-src data: blob:; media-src blob:; object-src 'none'; base-uri 'none'; form-action 'none'">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Data Pump</title><style>''' + style + '''</style></head>
<body><main id="app" aria-label="Data Pump"></main><pre id="failure" role="alert" hidden></pre>
<details id="dependency-notices"><summary>Software licenses and notices</summary><pre>''' + escape(notice_text) + '''</pre></details>
<script type="application/json" id="assets">''' + json.dumps(encoded, separators=(',', ':')) + '''</script>
<script type="module">''' + bootstrap + '</script></body></html>\n'
    destination.mkdir(parents=True, exist_ok=True)
    (destination / 'datapump-wasm.html').write_text(html)
    manifest = {'schema_version':1, 'target':'datapump-wasm', 'runtime':'single-worker-asyncify',
                'audio':'browser', 'files':'browser', 'transport':'local-messages-only',
                'csp_connect_src':'none', 'wasm_imports':imports,
                'notices':{name:hashlib.sha256(data).hexdigest() for name,data in notices.items()},
                'inputs':{name:hashlib.sha256(data).hexdigest() for name,data in blobs.items()}}
    (destination / 'web-manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
    inventory = ''.join(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.name + '\n'
                        for path in sorted(destination.iterdir()) if path.is_file() and path.name != 'manifest.sha256')
    (destination / 'manifest.sha256').write_text(inventory)
    return manifest


def verify_payload(files):
    expected_names = {'datapump-wasm.html', 'web-manifest.json', 'manifest.sha256'}
    if set(files) != expected_names:
        raise ValueError('Incomplete or unexpected browser artifact inventory')
    recorded = set()
    for line in files['manifest.sha256'].decode('utf-8').splitlines():
        expected, separator, name = line.partition('  ')
        if not separator or name not in expected_names - {'manifest.sha256'} or name in recorded:
            raise ValueError('Invalid browser inventory entry')
        if hashlib.sha256(files[name]).hexdigest() != expected:
            raise ValueError('Modified browser artifact: ' + name)
        recorded.add(name)
    if recorded != expected_names - {'manifest.sha256'}:
        raise ValueError('Incomplete browser inventory')
    manifest = json.loads(files['web-manifest.json'])
    if manifest.get('schema_version') != 1 or manifest.get('transport') != 'local-messages-only':
        raise ValueError('Unsupported browser runtime manifest')
    for item in manifest['wasm_imports']:
        if FORBIDDEN_IMPORT.search(item['module'] + '.' + item['name']):
            raise ValueError('Forbidden browser runtime import')
    html = files['datapump-wasm.html'].decode('utf-8')
    if "connect-src 'none'" not in html or '<script src=' in html:
        raise ValueError('Browser page does not preserve its preload/CSP contract')
    notices = manifest.get('notices', {})
    if (set(notices) != set(SDK_NOTICES) | set(PROJECT_NOTICES) or
            'id="dependency-notices"' not in html or
            any(name not in html or not re.fullmatch('[0-9a-f]{64}', value)
                for name, value in notices.items())):
        raise ValueError('Browser page is missing dependency notices')


def verify_archives(directory):
    archives = sorted([*directory.glob('*.tar.gz'), *directory.glob('*.zip')])
    if not archives:
        raise ValueError('No browser archives to verify')
    inventories = []
    for archive in archives:
        files = {}
        seen = set()
        def accept(name, data):
            parts = Path(name).parts
            if Path(name).is_absolute() or '..' in parts or name in seen:
                raise ValueError('Unsafe or duplicated archive path')
            seen.add(name)
            marker = '/share/datapump/web/wasm/'
            if marker in name:
                relative = name.split(marker, 1)[1]
                if '/' in relative:
                    raise ValueError('Unexpected nested browser asset')
                files[relative] = data
        if archive.name.endswith('.zip'):
            with zipfile.ZipFile(archive) as source:
                for item in source.infolist():
                    if item.is_dir():
                        continue
                    if (item.external_attr >> 16) & 0o170000 == 0o120000 or item.file_size > MAX_ASSET:
                        raise ValueError('Linked or oversized archive member')
                    accept(item.filename, source.read(item))
        else:
            with tarfile.open(archive) as source:
                for item in source:
                    if item.isdir():
                        continue
                    if not item.isfile() or item.size > MAX_ASSET:
                        raise ValueError('Unsupported or oversized archive member')
                    accept(item.name, source.extractfile(item).read())
        verify_payload(files)
        inventories.append(files['manifest.sha256'])
        print('Verified browser archive:', archive.name)
    if any(value != inventories[0] for value in inventories):
        raise ValueError('Browser archive formats contain different page inventories')
    (directory / 'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.name + '\n' for path in archives))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify-archives', type=Path)
    parser.add_argument('--javascript', type=Path)
    parser.add_argument('--wasm', type=Path)
    parser.add_argument('--assets', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--sdk-notices', type=Path)
    args = parser.parse_args()
    try:
        if args.verify_archives:
            verify_archives(args.verify_archives.resolve())
        else:
            if not all((args.javascript, args.wasm, args.assets, args.output, args.sdk_notices)):
                raise ValueError('Provide --javascript, --wasm, --assets, --output and --sdk-notices')
            package(args.javascript.resolve(), args.wasm.resolve(), args.assets.resolve(), args.output.resolve(), args.sdk_notices.resolve())
    except (ValueError, OSError, UnicodeError) as error:
        parser.exit(1, str(error) + '\n')

if __name__ == '__main__':
    main()
