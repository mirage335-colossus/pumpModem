#!/usr/bin/env python3
"""Standalone page, socket/thread import and artifact inventory checks."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import sys
sys.dont_write_bytecode = True
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('wasm_package', ROOT / 'tools/package-wasm.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


def uint(value):
    data = bytearray()
    while value > 127:
        data.append((value & 127) | 128); value >>= 7
    return bytes(data + bytes([value]))


def string(value):
    data = value.encode(); return uint(len(data)) + data


def wasm_import(name):
    body = b'\x01' + string('env') + string(name) + b'\x00\x00'
    return b'\0asm\x01\0\0\0\x02' + uint(len(body)) + body


class PackageWasmTests(unittest.TestCase):
    def test_runtime_socket_and_thread_imports_are_rejected(self):
        for name in ('__syscall_socket','__syscall_connect','emscripten_fetch','pthread_create','emscripten_proxy_to_main_thread'):
            with self.assertRaisesRegex(ValueError, 'Forbidden'):
                package.audit_wasm(wasm_import(name))
        self.assertEqual(package.audit_wasm(wasm_import('emscripten_get_now'))[0]['name'], 'emscripten_get_now')

    def test_shared_memory_and_minified_imports_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Shared'):
            package.audit_wasm(b'\0asm\x01\0\0\0\x05\x04\x01\x03\x01\x02')
        with self.assertRaisesRegex(ValueError, 'Minified'):
            package.audit_wasm(wasm_import('a'))
        with self.assertRaises(ValueError):
            package.audit_wasm(wasm_import('emscripten_get_now')[:-1])

    def test_page_contains_preloaded_assets_restrictive_csp_and_exact_inventory(self):
        with tempfile.TemporaryDirectory(prefix='wasm package ') as directory:
            root=Path(directory); assets=root/'assets'; assets.mkdir()
            for name in ('renderer.mjs','protocol.mjs','browser_audio.mjs','wasm_client.mjs','wasm_worker.js','audio_worklet.js','style.css'):
                (assets/name).write_text('/* asset </script> safe when encoded */' if name != 'style.css' else 'body{color:black}')
            (root/'module.js').write_text('const createDataPump = async () => {};')
            (root/'module.wasm').write_bytes(wasm_import('emscripten_get_now'))
            notices=root/'notices'; notices.mkdir()
            for name in package.SDK_NOTICES:
                (notices/name).write_text('License '+name+' <script>notice data</script>')
            output=root/'page'
            result=package.package(root/'module.js',root/'module.wasm',assets,output,notices)
            html=(output/'datapump-wasm.html').read_text()
            self.assertIn("connect-src 'none'",html)
            self.assertIn("worker-src blob:",html)
            self.assertNotIn('/* asset </script>',html)
            self.assertNotIn('SharedArrayBuffer',html)
            self.assertNotIn('<script src=',html)
            self.assertEqual(result['transport'],'local-messages-only')
            self.assertEqual(set(result['notices']),set(package.SDK_NOTICES)|set(package.PROJECT_NOTICES))
            self.assertIn('id="dependency-notices"',html)
            self.assertIn('&lt;script&gt;notice data&lt;/script&gt;',html)
            self.assertNotIn('<script>notice data</script>',html)
            (notices/package.SDK_NOTICES[0]).unlink()
            with self.assertRaisesRegex(ValueError,'Missing'):
                package.package(root/'module.js',root/'module.wasm',assets,output,notices)
            for entry in (output/'manifest.sha256').read_text().splitlines():
                expected,name=entry.split('  ',1)
                self.assertEqual(hashlib.sha256((output/name).read_bytes()).hexdigest(),expected)
            (output/'unknown').write_text('user file')
            with self.assertRaisesRegex(ValueError,'overwrite'):
                package.package(root/'module.js',root/'module.wasm',assets,output,notices)

if __name__ == '__main__':
    unittest.main()
