"""Fast packaging contracts; the full relocation smoke is an opt-in release check."""
import hashlib
import importlib.util
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


PROJECT = pathlib.Path(__file__).resolve().parents[1]
BUILDER_PATH = PROJECT / "tools/bundle_portable.py"
spec = importlib.util.spec_from_file_location("bundle_portable", BUILDER_PATH)
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class RuntimeCopyTests(unittest.TestCase):
    def test_snapshot_dereferences_files_and_directories(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            external = root / "installed runtime"
            external.mkdir()
            (external / "library.py").write_bytes(b"runtime module\n")
            (external / "resource").mkdir()
            (external / "resource" / "init.tcl").write_bytes(b"runtime resource\n")
            source = root / "source"
            source.mkdir()
            try:
                (source / "library.py").symlink_to(external / "library.py")
                (source / "tk").symlink_to(external / "resource", target_is_directory=True)
            except OSError as error:
                self.skipTest(f"Creating symlinks is unavailable: {error}")
            destination = root / "portable runtime"
            builder.copy_tree(source, destination)
            external.rename(root / "original removed")
            self.assertEqual((destination / "library.py").read_bytes(), b"runtime module\n")
            self.assertEqual((destination / "tk" / "init.tcl").read_bytes(), b"runtime resource\n")
            self.assertFalse(any(path.is_symlink() for path in destination.rglob("*")),
                             "An installation copy must own its runtime, not point at the builder")

    def test_stdlib_snapshot_does_not_pick_up_host_site_packages_or_bytecode(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            source = root / "python stdlib"
            source.mkdir()
            (source / "json.py").write_text("# genuine standard library\n")
            for directory in ("site-packages", "dist-packages", "__pycache__"):
                (source / directory).mkdir()
                (source / directory / "host_only.py").write_text("raise RuntimeError('host')\n")
            destination = root / "snapshot"
            builder.copy_tree(source, destination)
            self.assertEqual((destination / "json.py").read_text(), "# genuine standard library\n")
            for directory in ("site-packages", "dist-packages", "__pycache__"):
                self.assertFalse((destination / directory).exists(), directory)

    def test_cyclic_runtime_symlinks_fail_instead_of_recursing_forever(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            source = root / "source"
            source.mkdir()
            try:
                (source / "cycle").symlink_to(source, target_is_directory=True)
            except OSError as error:
                self.skipTest(f"Creating symlinks is unavailable: {error}")
            with self.assertRaisesRegex(RuntimeError, r"(?i)cycl"):
                builder.copy_tree(source, root / "destination")

    def test_destination_inside_source_is_rejected_before_copying(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = pathlib.Path(temporary)
            destination = source / "nested snapshot"
            with self.assertRaises(RuntimeError):
                builder.copy_tree(source, destination)
            self.assertFalse(destination.exists())


class ManifestTests(unittest.TestCase):
    def test_native_collection_excludes_unrelated_site_packages(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            (root / "lib-dynload").mkdir()
            (root / "site-packages" / "unrelated").mkdir(parents=True)
            standard = root / "lib-dynload" / "_tkinter.so"
            standard.write_bytes(b"local runtime")
            (root / "site-packages" / "unrelated" / "large-library.so").write_bytes(b"not part of GUI")
            self.assertEqual(builder.native_modules(root), [standard])

    def test_manifest_covers_owned_files_with_relative_names_and_content_hashes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            bundle = root / "bundle"
            (bundle / "app").mkdir(parents=True)
            (bundle / "app" / "datapump_gui.py").write_bytes(b"print('GUI')\n")
            (bundle / "pump").write_bytes(b"executable\x00\xff")
            # These belong to the person creating the release, not the release.
            (root / "private.key").write_bytes(b"secret key data")
            (root / "received-payload.txt").write_bytes(b"private received clipboard")
            metadata = {"python_version": "3.13.5", "sys_platform": "linux"}
            builder.write_manifest(bundle, metadata)
            manifest = json.loads((bundle / "manifest.json").read_text())
            self.assertEqual(manifest["schema"], 1)
            self.assertEqual(manifest["metadata"], metadata)
            self.assertEqual(manifest["files"], {
                "app/datapump_gui.py": hashlib.sha256(b"print('GUI')\n").hexdigest(),
                "pump": hashlib.sha256(b"executable\x00\xff").hexdigest(),
            })
            self.assertNotIn(str(root), (bundle / "manifest.json").read_text())
            # Regeneration must not hash the previous manifest into itself.
            builder.write_manifest(bundle, metadata)
            self.assertEqual(json.loads((bundle / "manifest.json").read_text()), manifest)


class BuilderFailureTests(unittest.TestCase):
    def test_missing_interpreter_does_not_leave_a_claimed_installation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            pump = root / "pump"
            pump.write_bytes(b"placeholder compiled executable")
            output = root / "portable"
            result = subprocess.run(
                [sys.executable, str(BUILDER_PATH), "--pump", str(pump), "--output", str(output),
                 "--python", str(root / "absent-python")],
                capture_output=True, text=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())

    def test_existing_installation_cannot_be_overwritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            output = root / "working installation"
            output.mkdir()
            (output / "must-remain").write_bytes(b"installed software and settings")
            pump = root / "pump"
            pump.write_text("not an executable; must reject output before packaging\n")
            result = subprocess.run(
                [sys.executable, str(BUILDER_PATH), "--pump", str(pump), "--output", str(output)],
                capture_output=True, text=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual((output / "must-remain").read_bytes(),
                             b"installed software and settings")
            self.assertEqual(list(output.iterdir()), [output / "must-remain"])
            self.assertRegex(result.stderr.lower(), r"exist|overwrite")

    def test_missing_tk_runtime_is_rejected_before_a_release_can_be_claimed(self):
        failure = subprocess.CompletedProcess(
            [sys.executable], 1, stdout="", stderr="ModuleNotFoundError: No module named '_tkinter'")
        with patch.object(builder.subprocess, "run", return_value=failure):
            with self.assertRaisesRegex(RuntimeError, r"(?i)tkinter|tk"):
                builder.probe_runtime(pathlib.Path(sys.executable))

    def test_runtime_probe_ignores_inherited_python_configuration(self):
        failure = subprocess.CompletedProcess(
            [sys.executable], 1, stdout="", stderr="No module named '_tkinter'")
        with patch.dict(os.environ, {"PYTHONPATH": "/host/third-party", "PYTHONHOME": "/host/python"}), \
                patch.object(builder.subprocess, "run", return_value=failure) as run:
            with self.assertRaises(RuntimeError):
                builder.probe_runtime(pathlib.Path(sys.executable))
        self.assertTrue(run.called)
        command = run.call_args.args[0]
        self.assertIn("-I", command, "Probe must ignore inherited Python paths and user-site settings")


if __name__ == "__main__":
    unittest.main()
