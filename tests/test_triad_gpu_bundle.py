from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import posixpath
import re
import tarfile
import tempfile
import unittest
from unittest.mock import patch


MODULE = Path(__file__).resolve().parents[1] / "tools" / "triad_gpu_bundle.py"
SPEC = importlib.util.spec_from_file_location("triad_gpu_bundle", MODULE)
bundle = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(bundle)


class TriadGpuBundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.root = self.base / "repo"
        self.root.mkdir()
        xz_sources = {"CMakeLists.txt": "project(xz)\n", "src/lzma.c": "/* vendored */\n"}
        for rel, content in {
            "CMakeLists.txt": "project(x)\n",
            "CMakePresets.json": "{}\n",
            "LICENSE": "Fixture license\n",
            "build.sh": "#!/bin/sh\n",
            "_tests-RobustModem.md": "# Manual cases\n",
            "cmake/VendoredLzma.cmake": "# xz\n",
            "cmake/XzReceiveHardening.cmake": "# overlay\n",
            "cmake/TriadTensor.cmake": "# native accelerator\n",
            **{"tools/" + name: "" for name in bundle._PROBE_TOOLS},
            "tools/build_capacity.py": "def usable_cpus(): return 1\n",
            "tools/diagnostic_iq_noise_options.hpp": "#pragma once\n",
            "tests/test_triad_tensor.cpp": "int main() {}\n",
            "tests/test_triad_gpu_workload.py": "VALUE = 4\n",
            "tools/triad_gpu_bank.py": "# retired Python DSP\n",
            "tools/triad_receiver.py": "# retired Python receiver\n",
            "src/decoder.cpp": "int decode() { return 0; }\n",
            "src/triad_tensor_backend.cpp": "// native GPU source\n",
            "src/pump.manifest": "<assembly/>\n",
            "include/decoder.hpp": "#pragma once\n",
            "include/datapump/triad_acceleration.hpp": "#pragma once\n",
            "third_party/xz/README.datapump.md": "# Upstream license\n",
            "third_party/xz/UPSTREAM.sha256": "".join(
                f"{hashlib.sha256(content.encode()).hexdigest()}  {name}\n"
                for name, content in xz_sources.items()),
            **{f"third_party/xz/{name}": content for name, content in xz_sources.items()},
            "third_party/qrcodegen/LICENSE": "Fixture license\n",
            "third_party/qrcodegen/qrcodegen.cpp": "// vendored\n",
            "third_party/qrcodegen/qrcodegen.hpp": "#pragma once\n",
        }.items():
            path = self.root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)

    def tearDown(self):
        self.temp.cleanup()

    def test_reproducible_archive_and_manifest_hashes(self):
        a, b = self.base / "a.tar.gz", self.base / "b.tar.gz"
        info = bundle.create_bundle(self.root, a)
        bundle.create_bundle(self.root, b)
        self.assertEqual(a.read_bytes(), b.read_bytes())
        with tarfile.open(a, "r:gz") as tar:
            names = tar.getnames()
            self.assertEqual(names, sorted(names[:-1]) + ["BUNDLE-MANIFEST.json"])
            manifest = json.load(tar.extractfile("BUNDLE-MANIFEST.json"))
            files = {row["path"]: row for row in manifest["files"]}
            for name, row in files.items():
                data = tar.extractfile(name).read()
                self.assertEqual(row["size"], len(data))
                self.assertEqual(row["sha256"], hashlib.sha256(data).hexdigest())
        self.assertEqual(info["compressed_bytes"], a.stat().st_size)

    def test_excludes_secrets_hidden_files_and_irrelevant_trees(self):
        (self.root / ".env").write_text("API_TOKEN=secret\n")
        (self.root / "src" / "private_key.pem").write_text("secret")
        (self.root / "src" / "ordinary.cpp").write_text("safe")
        (self.root / "src" / "records.csv").write_text("arbitrary records")
        (self.root / "third_party" / "xz" / "unpinned.c").write_text("unrelated code")
        (self.root / "build").mkdir()
        (self.root / "build" / "secret.txt").write_text("do not ship")
        for rel in ("src/build/cache.cpp", "include/env/leak.hpp", "cmake/customer-secret/config.cmake"):
            path = self.root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("do not ship")
        out = self.base / "bundle.tgz"
        bundle.create_bundle(self.root, out)
        with tarfile.open(out, "r:gz") as tar:
            names = tar.getnames()
        self.assertIn("src/ordinary.cpp", names)
        self.assertNotIn("src/records.csv", names)
        self.assertNotIn("third_party/xz/unpinned.c", names)
        self.assertFalse(any(".env" in n or "private_key" in n or n.startswith("build/")
                             or "/build/" in n or "/env/" in n or "customer-secret" in n
                             for n in names))

    def test_symlink_inputs_are_not_followed(self):
        outside = self.base / "outside.cpp"
        outside.write_text("outside")
        (self.root / "src" / "escape.cpp").symlink_to(outside)
        out = self.base / "bundle.tar.gz"
        bundle.create_bundle(self.root, out)
        with tarfile.open(out, "r:gz") as tar:
            self.assertNotIn("src/escape.cpp", tar.getnames())

    def test_rejects_unsafe_or_colliding_destination(self):
        with self.assertRaises(bundle.BundleError):
            bundle.create_bundle(self.root, self.root / "nested.tar.gz")
        with self.assertRaises(bundle.BundleError):
            bundle.create_bundle("/" + str(self.root), self.root / "nested.tar.gz")
        out = self.base / "exists.tar.gz"
        out.write_bytes(b"existing")
        with self.assertRaises(bundle.BundleError):
            bundle.create_bundle(self.root, out)
        self.assertEqual(out.read_bytes(), b"existing")

    def test_rejects_symlinked_source_root_and_allowlist_root(self):
        source_alias = self.base / "source-alias"
        source_alias.symlink_to(self.root, target_is_directory=True)
        with self.assertRaises(bundle.BundleError):
            bundle.create_bundle(source_alias, self.base / "root-link.tar.gz")

        outside = self.base / "outside-src"
        outside.mkdir()
        (outside / "leak.cpp").write_text("outside")
        (self.root / "src").rename(self.root / "src-original")
        (self.root / "src").symlink_to(outside, target_is_directory=True)
        with self.assertRaises(bundle.BundleError):
            bundle.create_bundle(self.root, self.base / "dir-link.tar.gz")

    def test_required_build_dependencies_are_included(self):
        out = self.base / "dependencies.tar.gz"
        bundle.create_bundle(self.root, out)
        with tarfile.open(out, "r:gz") as tar:
            names = tar.getnames()
        self.assertIn("tools/build_capacity.py", names)
        self.assertIn("tools/diagnostic_iq_noise_options.hpp", names)
        self.assertIn("tools/triad-receiver-probe.cpp", names)
        self.assertIn("tools/triad_probe_spool.hpp", names)
        self.assertIn("tools/triad_fixture_key.hpp", names)
        self.assertIn("tests/test_triad_tensor.cpp", names)
        self.assertIn("cmake/TriadTensor.cmake", names)
        self.assertIn("src/triad_tensor_backend.cpp", names)
        self.assertNotIn("tools/triad_gpu_bank.py", names)
        self.assertNotIn("tools/triad_receiver.py", names)

    def test_native_entrypoints_have_their_quoted_header_closure(self):
        out = self.base / "project.tar.gz"
        bundle.create_bundle(MODULE.parents[1], out)
        pending = ["tools/triad-receiver-probe.cpp", "tools/triad-helper-export.cpp",
                   "tools/triad-audio-stream.cpp", "tests/test_triad_tensor.cpp"]
        seen = set()
        with tarfile.open(out, "r:gz") as tar:
            names = set(tar.getnames())
            while pending:
                source = pending.pop()
                if source in seen:
                    continue
                seen.add(source)
                with tar.extractfile(source) as member:
                    includes = re.findall(rb'^\s*#\s*include\s*"([^"\r\n]+)"', member.read(), re.MULTILINE)
                for header in includes:
                    candidates = [posixpath.normpath(posixpath.join(directory, header.decode("ascii")))
                                  for directory in (posixpath.dirname(source), "src", "include", "tools")]
                    shipped = next((path for path in candidates if path in names), None)
                    self.assertIsNotNone(shipped, f"Bundle omits {header!r} included by {source}")
                    pending.append(shipped)

    def test_rejects_replacement_after_inventory(self):
        outside = self.base / "outside.cpp"
        outside.write_text("outside")
        victim = self.root / "src" / "decoder.cpp"
        original = victim.read_bytes()
        collect = bundle._collect

        def replace_after_inventory(root_fd):
            entries = collect(root_fd)
            victim.unlink()
            victim.symlink_to(outside)
            return entries

        out = self.base / "leaf-replacement.tgz"
        with patch.object(bundle, "_collect", side_effect=replace_after_inventory):
            with self.assertRaises(bundle.BundleError):
                bundle.create_bundle(self.root, out)
        self.assertFalse(out.exists())
        self.assertEqual(outside.read_text(), "outside")

        def replace_with_regular_file(root_fd):
            entries = collect(root_fd)
            replacement = self.base / "replacement.cpp"
            replacement.write_bytes(original)
            replacement.replace(victim)
            return entries

        victim.unlink()
        victim.write_bytes(original)
        out = self.base / "regular-replacement.tgz"
        with patch.object(bundle, "_collect", side_effect=replace_with_regular_file):
            with self.assertRaisesRegex(bundle.BundleError, "changed after inventory"):
                bundle.create_bundle(self.root, out)
        self.assertFalse(out.exists())

    def test_rejects_ancestor_symlink_after_inventory(self):
        outside = self.base / "outside-src"
        outside.mkdir()
        (outside / "decoder.cpp").write_text("outside")
        collect = bundle._collect

        def replace_after_inventory(root_fd):
            entries = collect(root_fd)
            (self.root / "src").rename(self.base / "original-src")
            (self.root / "src").symlink_to(outside, target_is_directory=True)
            return entries

        out = self.base / "ancestor-replacement.tgz"
        with patch.object(bundle, "_collect", side_effect=replace_after_inventory):
            with self.assertRaises(bundle.BundleError):
                bundle.create_bundle(self.root, out)
        self.assertFalse(out.exists())


if __name__ == "__main__":
    unittest.main()
