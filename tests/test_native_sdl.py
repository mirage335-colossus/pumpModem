#!/usr/bin/env python3
"""Verify source integrity and extraction boundaries before native SDL builds."""
import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("native_sdl", ROOT / "tools/prepare-native-sdl.py")
sdl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdl)


class PreparedSdl(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="datapump native SDL ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_corrupt_source_never_extracts_or_launches_a_build(self):
        archive = self.root / "corrupt.tar.gz"
        archive.write_bytes(b"untrusted source")
        with patch.object(sdl.subprocess, "run", side_effect=AssertionError("build launched")):
            with patch.object(sdl, "extract_source", side_effect=AssertionError("source extracted")):
                with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                    sdl.prepare(self.root / "destination", archive)

    def test_existing_workspace_is_never_reused_or_replaced(self):
        destination = self.root / "existing"
        destination.mkdir()
        sentinel = destination / "keep"
        sentinel.write_text("previous preparation")
        with patch.object(sdl.urllib.request, "urlopen", side_effect=AssertionError("network")):
            with self.assertRaises(FileExistsError):
                sdl.prepare(destination)
        self.assertEqual(sentinel.read_text(), "previous preparation")

    def archive(self, members):
        archive = self.root / "source.tar.gz"
        with tarfile.open(archive, "w:gz") as output:
            for name, target in members:
                member = tarfile.TarInfo(name)
                if target is None:
                    member.size = 3
                    output.addfile(member, io.BytesIO(b"abc"))
                else:
                    member.type = tarfile.SYMTYPE
                    member.linkname = target
                    output.addfile(member)
        return archive

    def test_upstream_relative_link_stays_inside_source(self):
        name = f"SDL2-{sdl.VERSION}"
        archive = self.archive([(f"{name}/real/data", None), (f"{name}/old/link", "../real")])
        destination = self.root / "unpacked"
        destination.mkdir()
        sdl.extract_source(archive, destination)
        self.assertEqual((destination / name / "old/link/data").read_bytes(), b"abc")

    def test_escaping_paths_and_links_fail_before_any_extraction(self):
        name = f"SDL2-{sdl.VERSION}"
        for path, target in [("../escape", None), ("/absolute", None),
                             (f"{name}/link", "../../escape"),
                             (f"{name}/link", "/absolute")]:
            with self.subTest(path=path, target=target):
                archive = self.archive([(f"{name}/safe", None), (path, target)])
                destination = self.root / "unpacked"
                with self.assertRaisesRegex(ValueError, "Unsafe"):
                    sdl.extract_source(archive, destination)
                self.assertFalse(destination.exists())


if __name__ == "__main__":
    unittest.main()
