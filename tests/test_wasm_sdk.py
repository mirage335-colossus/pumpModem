#!/usr/bin/env python3
"""Offline recipe/input safety checks; never download or build a compiler."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import sys
sys.dont_write_bytecode = True
import subprocess
import shutil
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('wasm_sdk', ROOT / 'tools/build-wasm-sdk.py')
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)


class WasmSdkTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='wasm sdk ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_recipe_pins_official_inputs_and_disables_socket_library(self):
        value = sdk.recipe()
        self.assertEqual(value['host'], 'linux-x86_64')
        self.assertEqual({item['name'] for item in value['inputs']}, {'emsdk','compiler','node','openssl'})
        self.assertIn('no-sock', value['openssl_options'])
        self.assertIn('no-dso', value['openssl_options'])
        self.assertIn('no-threads', value['openssl_options'])
        self.assertEqual(value['entropy']['capability'], sdk.ENTROPY_CAPABILITY)
        for item in value['inputs']:
            self.assertRegex(item['sha256'], '^[0-9a-f]{64}$')
            self.assertTrue(item['url'].startswith('https://'))

    def test_entropy_patch_is_pinned_and_modified_support_is_rejected(self):
        value = sdk.recipe()
        recipe = self.root / 'manifest.json'
        recipe.write_text(json.dumps(value))
        entropy_patch = self.root / value['entropy']['patch']
        entropy_patch.write_bytes((sdk.RECIPE.parent / value['entropy']['patch']).read_bytes())
        with patch.object(sdk, 'RECIPE', recipe):
            self.assertEqual(sdk.recipe()['entropy'], value['entropy'])
            entropy_patch.write_text('changed adapter')
            with self.assertRaisesRegex(ValueError, 'patch checksum'):
                sdk.recipe()

    def test_entropy_patch_checks_exact_input_and_output(self):
        source = self.root / 'rand_unix.c'
        source.write_bytes(b'original')
        value = {'entropy': {'source': source.name,
            'source_sha256': hashlib.sha256(b'original').hexdigest(),
            'patched_sha256': hashlib.sha256(b'adapted').hexdigest(),
            'patch': 'openssl-emscripten-entropy.patch'}}
        def apply(args, **kwargs):
            self.assertEqual(kwargs['cwd'], self.root)
            self.assertIn('--fuzz=0', args)
            source.write_bytes(b'adapted')
        with patch.object(sdk, 'run', side_effect=apply) as run:
            sdk.patch_openssl_entropy(self.root, value)
            run.assert_called_once()
            with self.assertRaisesRegex(ValueError, 'pinned patch input'):
                sdk.patch_openssl_entropy(self.root, value)
            run.assert_called_once()
        source.write_bytes(b'original')
        with patch.object(sdk, 'run'):
            with self.assertRaisesRegex(ValueError, 'output checksum'):
                sdk.patch_openssl_entropy(self.root, value)

    def test_entropy_probe_uses_target_crypto_and_browser_only_runtime(self):
        em = self.root / 'compiler'; target = self.root / 'target'
        with patch.object(sdk, 'run') as run:
            sdk.qualify_entropy(em, self.root / 'node', target, self.root, {})
        compile_args = run.call_args_list[0].args[0]
        self.assertIn(target / 'lib/libcrypto.a', compile_args)
        self.assertIn('-sENVIRONMENT=web,worker', compile_args)
        self.assertIn('-sEXPORT_NAME=DatapumpEntropyProbe', compile_args)
        self.assertEqual(run.call_args_list[1].args[0][-1], self.root / 'entropy-probe.wasm')

    def test_missing_inputs_do_not_trigger_an_implicit_download(self):
        with patch.object(sdk.urllib.request, 'urlopen') as network:
            with self.assertRaisesRegex(ValueError, 'explicitly use fetch'):
                sdk.source_inputs(self.root)
            network.assert_not_called()

    def test_corrupt_or_linked_inputs_are_rejected(self):
        source = self.root / 'source.tar'
        source.write_bytes(b'expected')
        value = {'inputs':[{'file':source.name,'sha256':hashlib.sha256(b'expected').hexdigest()}]}
        with patch.object(sdk, 'recipe', return_value=value):
            sdk.source_inputs(self.root)
            source.write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
                sdk.source_inputs(self.root)
            source.unlink()
            source.symlink_to(self.root / 'other')
            (self.root / 'other').write_bytes(b'expected')
            with self.assertRaisesRegex(ValueError, 'linked source'):
                sdk.source_inputs(self.root)

    def test_archive_rejects_traversal_and_escaping_links(self):
        for case in ('../outside','/outside','link'):
            archive = self.root / 'bad.tar'
            with tarfile.open(archive, 'w') as output:
                entry = tarfile.TarInfo(case)
                if case == 'link':
                    entry.type = tarfile.SYMTYPE; entry.linkname = '../outside'
                output.addfile(entry, io.BytesIO())
            with self.assertRaises(ValueError):
                sdk.extract(archive, self.root / 'extract')
            self.assertFalse((self.root / 'outside').exists())

    def test_prepare_refuses_to_replace_existing_sdk(self):
        destination = self.root / 'prepared'; destination.mkdir()
        sentinel = destination / 'preserve'; sentinel.write_text('prior SDK')
        with patch.object(sdk, 'source_inputs', return_value={}), patch.object(sdk.platform, 'system', return_value='Linux'), patch.object(sdk.platform, 'machine', return_value='x86_64'):
            with self.assertRaisesRegex(ValueError, 'overwrite'):
                sdk.prepare(self.root, destination, 2)
        self.assertEqual(sentinel.read_text(), 'prior SDK')

    def test_target_runtime_notices_are_preserved_without_compiler_rebuild(self):
        em=self.root/'emscripten'; openssl=self.root/'openssl'; openssl.mkdir()
        for relative in sdk.RUNTIME_NOTICES.values():
            source=em/relative; source.parent.mkdir(parents=True,exist_ok=True); source.write_text('Exact notice '+relative)
        (openssl/'LICENSE.txt').write_text('Exact OpenSSL notice')
        (em/'system/lib/dlmalloc.c').write_text('/*\n This is a version (aka dlmalloc)\nPublic domain\n * Quickstart\nOther documentation */')
        output=self.root/'notices'; sdk.copy_runtime_notices(em,openssl,output)
        for name,relative in sdk.RUNTIME_NOTICES.items():
            self.assertEqual((output/name).read_bytes(),(em/relative).read_bytes())
        self.assertEqual((output/'OpenSSL-LICENSE.txt').read_text(),'Exact OpenSSL notice')
        self.assertIn('Public domain',(output/'dlmalloc-NOTICE.txt').read_text())
        self.assertNotIn('Quickstart',(output/'dlmalloc-NOTICE.txt').read_text())

@unittest.skipUnless(shutil.which('cmake'), 'CMake is required for toolchain isolation checks')
class WasmToolchainTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='wasm toolchain ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sdk = self.root / 'sdk'
        self.metadata = self.sdk / 'share/datapump-wasm-sdk'
        self.metadata.mkdir(parents=True)
        for name in ('emcc','em++','libcrypto.a'):
            (self.sdk / name).write_text('fixture')
        (self.sdk / 'include').mkdir()
        (self.sdk / 'toolchain.cmake').write_text('set(CMAKE_C_COMPILER "${DATAPUMP_WASM_SDK_ROOT}/emcc")\nset(CMAKE_CXX_COMPILER "${DATAPUMP_WASM_SDK_ROOT}/em++")\n')
        self.manifest = {'schema_version':1,'target':'wasm32-emscripten','toolchain':'toolchain.cmake',
                         'compiler':'emcc','cxx_compiler':'em++','openssl_include':'include',
                         'openssl_crypto':'libcrypto.a','openssl_crypto_sha256':sdk.digest(self.sdk/'libcrypto.a'),
                         'entropy_capability':sdk.ENTROPY_CAPABILITY,'entropy_probe':'passed'}
        (self.metadata/'relocated-root.txt').write_text(str(self.sdk))

    def invoke(self, extra='', success=True):
        (self.metadata/'manifest.json').write_text(json.dumps(self.manifest))
        script=self.root/'check.cmake'
        script.write_text('cmake_minimum_required(VERSION 3.21)\nset(DATAPUMP_WASM_SDK_ROOT "'+str(self.sdk)+'")\n'+extra+'\ninclude("'+str(ROOT/'cmake/toolchains/wasm-sdk.cmake')+'")\nif(NOT CMAKE_FIND_ROOT_PATH_MODE_LIBRARY STREQUAL "ONLY" OR NOT "$ENV{EM_FROZEN_CACHE}" STREQUAL "1")\nmessage(FATAL_ERROR "uncontained target discovery")\nendif()\n')
        result=subprocess.run(['cmake','-P',str(script)],capture_output=True,text=True)
        self.assertEqual(result.returncode==0,success,result.stdout+result.stderr)
        return result

    def test_valid_sdk_uses_only_its_own_target_root(self):
        self.invoke()

    def test_unqualified_entropy_sdk_requires_new_preparation(self):
        del self.manifest['entropy_capability']
        self.assertIn('prepare a new SDK destination', self.invoke(success=False).stderr)
        self.manifest['entropy_capability'] = sdk.ENTROPY_CAPABILITY
        self.manifest['entropy_probe'] = 'failed'
        self.assertIn('qualified browser entropy', self.invoke(success=False).stderr)

    def test_paths_cannot_escape_sdk_or_use_native_prefix(self):
        self.manifest['openssl_crypto']='../outside'
        self.invoke(success=False)
        self.manifest['openssl_crypto']='libcrypto.a'
        self.invoke('set(DATAPUMP_DEPENDENCY_PREFIX /native)',success=False)

    def test_changed_crypto_or_moved_sdk_is_rejected(self):
        (self.sdk/'libcrypto.a').write_text('modified')
        self.invoke(success=False)
        (self.sdk/'libcrypto.a').write_text('fixture')
        (self.metadata/'relocated-root.txt').write_text('/old/sdk')
        self.invoke(success=False)

if __name__ == '__main__':
    unittest.main()
