"""Synthetic PE fixtures test offline dependency logic without a Windows SDK."""
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import pe_dependencies as pe


def fixture(normal=(), delayed=(), *, bits=64, delay_va=False):
    """A minimal PE with independent ordinary/delay import tables and names."""
    data = bytearray(4096)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    optional, optional_size = 0x98, 240 if bits == 64 else 224
    struct.pack_into("<HH", data, 0x84, 0x8664 if bits == 64 else 0x14C, 1)
    struct.pack_into("<H", data, 0x94, optional_size)
    struct.pack_into("<H", data, optional, 0x20B if bits == 64 else 0x10B)
    base = 0x10000000
    struct.pack_into("<Q" if bits == 64 else "<I", data,
                     optional + (24 if bits == 64 else 28), base)
    struct.pack_into("<I", data, optional + 60, 0x200)
    struct.pack_into("<I", data, optional + (108 if bits == 64 else 92), 16)
    directory = optional + (112 if bits == 64 else 96)
    section = optional + optional_size
    data[section:section + 8] = b".rdata\0\0"
    struct.pack_into("<IIII", data, section + 8, 0xE00, 0x1000, 0xE00, 0x200)
    name_offset = 0x700
    for index, names, table, size in ((1, normal, 0x200, 20), (13, delayed, 0x400, 32)):
        if not names:
            continue
        struct.pack_into("<II", data, directory + index * 8, table + 0xE00, (len(names) + 1) * size)
        for number, name in enumerate(names):
            encoded = name.encode("ascii") + b"\0"
            data[name_offset:name_offset + len(encoded)] = encoded
            address = name_offset + 0xE00
            if index == 1:
                struct.pack_into("<I", data, table + number * size + 12, address)
            else:
                struct.pack_into("<II", data, table + number * size,
                                 0 if delay_va else 1, address + (base if delay_va else 0))
            name_offset += len(encoded)
    return bytes(data)


class PortablePETests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.environment = patch.dict(os.environ, {"PATH": ""}, clear=True)
        self.environment.start()

    def tearDown(self):
        self.environment.stop()
        self.temporary.cleanup()

    def image(self, relative, **options):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(fixture(**options))
        return path

    def test_normal_and_delay_imports_for_both_formats(self):
        for bits in (32, 64):
            with self.subTest(bits=bits):
                path = self.image(f"app{bits}.exe", bits=bits,
                                  normal=("KERNEL32.dll", "helper.dll"),
                                  delayed=("HELPER.DLL", "VCRUNTIME140.dll"))
                self.assertEqual(pe.imports(path), ["KERNEL32.dll", "helper.dll", "VCRUNTIME140.dll"])

    def test_old_delay_import_virtual_addresses(self):
        for bits in (32, 64):
            path = self.image(f"delay{bits}.dll", bits=bits, delayed=("late.dll",), delay_va=True)
            self.assertEqual(pe.imports(path), ["late.dll"])

    def test_truncated_images_rejected(self):
        complete = fixture(normal=("helper.dll",))
        for length in (0, 1, 63, 0x83, 0x100, 0x1A0, 0x700):
            with self.subTest(length=length):
                path = self.root / "truncated.exe"
                path.write_bytes(complete[:length])
                with self.assertRaises(pe.PEError):
                    pe.imports(path)

    def test_invalid_rva_and_missing_terminator_rejected(self):
        path = self.image("invalid.exe", normal=("helper.dll",))
        data = bytearray(path.read_bytes())
        struct.pack_into("<I", data, 0x20C, 0x50000000)
        path.write_bytes(data)
        with self.assertRaisesRegex(pe.PEError, "RVA"):
            pe.imports(path)
        data = bytearray(fixture(normal=("helper.dll",)))
        struct.pack_into("<I", data, 0x98 + 112 + 8 + 4, 20)
        path.write_bytes(data)
        with self.assertRaisesRegex(pe.PEError, "terminating"):
            pe.imports(path)

    def test_import_name_cannot_escape_destination(self):
        for name in ("../evil.dll", r"..\evil.dll", "C:evil.dll"):
            path = self.image("escape.exe", normal=(name,))
            with self.assertRaisesRegex(pe.PEError, "basename"):
                pe.imports(path)

    def test_zero_fill_and_unsupported_delay_metadata_rejected(self):
        path = self.image("virtual.exe", normal=("helper.dll",))
        data = bytearray(path.read_bytes())
        struct.pack_into("<I", data, 0x98 + 240 + 16, 16)
        path.write_bytes(data)
        with self.assertRaisesRegex(pe.PEError, "RVA"):
            pe.imports(path)
        data = bytearray(fixture(delayed=("helper.dll",)))
        struct.pack_into("<I", data, 0x400, 2)
        path.write_bytes(data)
        with self.assertRaisesRegex(pe.PEError, "delay-import attributes"):
            pe.imports(path)

    def test_copies_recursive_case_insensitive_closure_and_delay_loads(self):
        app = self.image("source/app.exe", normal=("KERNEL32.dll", "HELPER.dll"),
                         delayed=("api-ms-win-core-file-l1-1-0.dll",))
        helper = self.image("source/helper.DLL", normal=("VCRUNTIME140.dll",), delayed=("late.dll",))
        runtime = self.image("runtime/VCRUNTIME140.DLL", normal=("ucrtbase.dll",))
        late = self.image("runtime/late.dll", normal=("helper.dll",))
        origins, excluded = pe.copy_dependencies([app], self.root / "copied", [self.root / "runtime"])
        self.assertEqual(set(origins), {"helper.dll", "vcruntime140.dll", "late.dll"})
        self.assertEqual(set(excluded), {"kernel32.dll", "ucrtbase.dll", "api-ms-win-core-file-l1-1-0.dll"})
        for name, original in (("helper.dll", helper), ("vcruntime140.dll", runtime), ("late.dll", late)):
            self.assertEqual((self.root / "copied" / name).read_bytes(), original.read_bytes())
            self.assertFalse((self.root / "copied" / name).is_symlink())

    def test_redist_in_system32_is_copied_not_classified_as_os(self):
        app = self.image("app.exe", normal=("MSVCP140.dll", "VCRUNTIME140_1.dll", "CONCRT140.dll"))
        for name in ("MSVCP140.dll", "VCRUNTIME140_1.dll", "CONCRT140.dll"):
            self.image("Windows/System32/" + name)
        with patch.dict(os.environ, {"SystemRoot": str(self.root / "Windows")}):
            origins, excluded = pe.copy_dependencies([app], self.root / "copied", [])
        self.assertEqual(set(origins), {"msvcp140.dll", "vcruntime140_1.dll", "concrt140.dll"})
        self.assertFalse(excluded)

    def test_private_runtime_precedes_other_installed_versions(self):
        app = self.image("source/app.exe", normal=("VCRUNTIME140.dll",))
        private = self.image("source/vcruntime140.dll")
        self.image("Windows/System32/VCRUNTIME140.dll", normal=("kernel32.dll",))
        self.image("on-path/VCRUNTIME140.dll", normal=("ucrtbase.dll",))
        with patch.dict(os.environ, {"SystemRoot": str(self.root / "Windows"),
                                     "PATH": str(self.root / "on-path")}):
            origins, _ = pe.copy_dependencies([app], self.root / "copied", [])
        self.assertEqual(origins["vcruntime140.dll"], str(private))

    def test_ambient_fallback_follows_search_order(self):
        app = self.image("source/app.exe", normal=("VCRUNTIME140.dll",))
        first = self.image("first/VCRUNTIME140.dll")
        self.image("second/VCRUNTIME140.dll", normal=("kernel32.dll",))
        self.image("Windows/System32/VCRUNTIME140.dll", normal=("ucrtbase.dll",))
        with patch.dict(os.environ, {"SystemRoot": str(self.root / "Windows"),
                                     "PATH": os.pathsep.join(map(str, [self.root / "first", self.root / "second"]))}):
            origins, _ = pe.copy_dependencies([app], self.root / "copied", [])
        self.assertEqual(origins["vcruntime140.dll"], str(first))

    def test_missing_transitive_dependency_fails_before_copy(self):
        app = self.image("source/app.exe", normal=("helper.dll",))
        self.image("source/helper.dll", delayed=("missing.dll",))
        with self.assertRaisesRegex(pe.PEError, "missing.dll"):
            pe.copy_dependencies([app], self.root / "copied", [])
        self.assertFalse((self.root / "copied").exists())

    def test_conflicting_local_dependencies_rejected(self):
        app = self.image("source/app.exe", normal=("helper.dll",))
        self.image("source/helper.dll")
        self.image("other/HELPER.DLL", normal=("kernel32.dll",))
        with self.assertRaisesRegex(pe.PEError, "Conflicting"):
            pe.copy_dependencies([app], self.root / "copied", [self.root / "other"])

    def test_identical_duplicates_and_source_symlink_are_owned(self):
        app = self.image("source/app.exe", normal=("helper.dll",))
        real = self.image("actual/helper.dll")
        try:
            (self.root / "source/helper.dll").symlink_to(real)
        except OSError as error:
            self.skipTest(f"Creating test symlinks is unavailable: {error}")
        self.image("other/HELPER.DLL")
        origins, _ = pe.copy_dependencies([app], self.root / "copied", [self.root / "other"])
        self.assertEqual(origins["helper.dll"], str(real))
        self.assertFalse((self.root / "copied/helper.dll").is_symlink())
        self.assertEqual((self.root / "copied/helper.dll").read_bytes(), real.read_bytes())

    def test_broken_destination_link_cannot_redirect_a_copy(self):
        app = self.image("source/app.exe", normal=("helper.dll",))
        self.image("source/helper.dll")
        destination = self.root / "copied"
        destination.mkdir()
        outside = self.root / "outside.dll"
        try:
            (destination / "helper.dll").symlink_to(outside)
        except OSError as error:
            self.skipTest(f"Creating test symlinks is unavailable: {error}")
        with self.assertRaisesRegex(pe.PEError, "conflicting DLL"):
            pe.copy_dependencies([app], destination, [])
        self.assertFalse(outside.exists())


if __name__ == "__main__":
    unittest.main()
