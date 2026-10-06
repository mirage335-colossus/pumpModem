#!/usr/bin/env python3
"""Opt-in local build.sh compatibility checks; no builds or workflow registration."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import unittest

import test_build_wrapper as baseline

SHELL = os.environ.get("DATAPUMP_TEST_SHELL", "/bin/sh")


class ManualShellCompatibilityTests(baseline.BuildWrapperTests):
    def setUp(self):
        super().setUp()
        # Traditional Bourne uses external printf, unlike Dash's builtin.
        printf = shutil.which("printf")
        self.assertIsNotNone(printf, "external printf is required by traditional Bourne")
        (self.bin / "printf").symlink_to(printf)

    def run_wrapper(self, *args, success=True, **environment):
        result = subprocess.run([SHELL, str(self.source / "build.sh"), *args],
                                cwd=self.root, env=dict(self.env, **environment),
                                text=True, capture_output=True, check=False)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        calls = [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []
        return result, calls

    def test_relative_build_directory_uses_actual_invocation_directory(self):
        _, calls = self.run_wrapper("--build-dir", "relative tree",
                                    PWD=str(self.root / "unrelated"))
        self.assertIn(str(self.root / "relative tree"), calls[0])

    def test_failed_detection_output_is_not_used_as_a_job_count(self):
        (self.source / 'tools/build_capacity.py').write_text('print(7); raise OSError("probe failed")\n')
        _, calls = self.run_wrapper()
        self.assertEqual(calls[1][calls[1].index('--parallel') + 1], '1')

    def test_sdk_paths_beginning_with_dash_are_resolved_from_the_caller(self):
        native_sdk = self.root / '-native sdk'
        metadata = native_sdk / 'share/datapump-sdk'
        metadata.mkdir(parents=True)
        (metadata / 'manifest.json').write_text('{}')
        (metadata / 'relocated-root.txt').write_text(str(native_sdk) + '\n')
        _, calls = self.run_wrapper('--sdk', native_sdk.name)
        self.assertIn('-DDATAPUMP_SDK_ROOT=' + str(native_sdk), calls[0])
        self.log.unlink()
        wasm_sdk = self.prepare_wasm_sdk()
        renamed = self.root / '-wasm sdk'
        wasm_sdk.rename(renamed)
        (renamed / 'share/datapump-wasm-sdk/relocated-root.txt').write_text(str(renamed) + '\n')
        _, calls = self.run_wrapper('--wasm-sdk', renamed.name)
        self.assertIn('-DDATAPUMP_WASM_SDK_ROOT=' + str(renamed), calls[0])

    def test_sdk_symlinks_resolve_to_the_prepared_physical_root(self):
        native_sdk = self.root / 'native SDK target'
        metadata = native_sdk / 'share/datapump-sdk'
        metadata.mkdir(parents=True)
        (metadata / 'manifest.json').write_text('{}')
        (metadata / 'relocated-root.txt').write_text(str(native_sdk) + '\n')
        native_alias = self.root / 'native SDK alias'
        native_alias.symlink_to(native_sdk, target_is_directory=True)
        _, calls = self.run_wrapper('--sdk', str(native_alias))
        self.assertIn('-DDATAPUMP_SDK_ROOT=' + str(native_sdk), calls[0])
        self.log.unlink()
        wasm_sdk = self.prepare_wasm_sdk()
        wasm_alias = self.root / 'Wasm SDK alias'
        wasm_alias.symlink_to(wasm_sdk, target_is_directory=True)
        _, calls = self.run_wrapper('--wasm-sdk', str(wasm_alias))
        self.assertIn('-DDATAPUMP_WASM_SDK_ROOT=' + str(wasm_sdk), calls[0])

    def test_source_symlink_resolves_to_the_physical_build_tree(self):
        original = self.source
        alias = self.root / 'source alias'
        alias.symlink_to(original, target_is_directory=True)
        self.source = alias
        _, calls = self.run_wrapper()
        self.assertIn(str(original), calls[0])
        self.assertIn(str(original / 'build/dev'), calls[0])

    def test_full_whitespace_class_between_compiler_and_arguments_is_preserved(self):
        for separator in ('\r', '\f', '\v'):
            with self.subTest(separator=repr(separator)):
                arguments = separator + '-m64'
                self.compiler_cache((("C", "fakecc", arguments),))
                self.log.unlink(missing_ok=True)
                _, calls = self.run_wrapper(CC='fakecc' + arguments)
                self.assertEqual(len(calls), 2)


if __name__ == "__main__":
    if not Path(SHELL).is_file() or not os.access(SHELL, os.X_OK):
        raise SystemExit("Selected shell is not executable: " + SHELL)
    unittest.main()
