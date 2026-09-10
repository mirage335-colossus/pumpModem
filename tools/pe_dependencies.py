"""Collect Windows PE dependencies from existing local files, without SDK tools.

Only the Windows 10 operating-system DLLs below and API sets are excluded.
Visual C++ redistributable DLLs remain application dependencies regardless of
where Windows happens to have installed them.
"""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import shutil
import struct


class PEError(RuntimeError):
    """Malformed PE metadata or an incomplete/conflicting local dependency set."""


SYSTEM_DLLS = frozenset("""
    aclui.dll advapi32.dll apphelp.dll authz.dll avrt.dll bcrypt.dll
    bcryptprimitives.dll cabinet.dll cfgmgr32.dll clbcatq.dll comctl32.dll
    comdlg32.dll crypt32.dll cryptbase.dll cryptsp.dll cryptui.dll dbgcore.dll
    dbghelp.dll dhcpcsvc.dll dhcpcsvc6.dll dnsapi.dll dsound.dll dwmapi.dll
    fwpuclnt.dll gdi32.dll gdi32full.dll gdiplus.dll imagehlp.dll imm32.dll
    iphlpapi.dll kernel32.dll kernelappcore.dll kernelbase.dll mpr.dll
    msacm32.dll msimg32.dll msvcp_win.dll msvcrt.dll mswsock.dll ncrypt.dll
    netapi32.dll normaliz.dll ntdll.dll ole32.dll oleacc.dll oleaut32.dll
    powrprof.dll profapi.dll propsys.dll psapi.dll rpcrt4.dll rsaenh.dll
    secur32.dll setupapi.dll shcore.dll shell32.dll shlwapi.dll sspicli.dll
    ucrtbase.dll user32.dll userenv.dll usp10.dll uxtheme.dll version.dll
    windowscodecs.dll winhttp.dll wininet.dll winmm.dll winmmbase.dll
    winrnr.dll winspool.drv wintrust.dll wldap32.dll ws2_32.dll wtsapi32.dll
""".split())
API_SET = re.compile(r"^(?:api|ext)-ms-win-[a-z0-9-]+\.dll$", re.IGNORECASE)
MAX_IMAGE_BYTES = 512 * 1024 * 1024
MAX_DESCRIPTORS = 4096


def is_system_library(name: str) -> bool:
    return name.casefold() in SYSTEM_DLLS or API_SET.fullmatch(name) is not None


class _Image:
    def __init__(self, data: bytes):
        self.data = data
        if self.read(0, 2) != b"MZ":
            raise PEError("Missing DOS MZ signature")
        pe = self.u32(0x3C)
        if self.read(pe, 4) != b"PE\0\0":
            raise PEError("Missing PE signature")
        count = self.u16(pe + 6)
        if not 1 <= count <= 96:
            raise PEError("Invalid PE section count")
        optional_size = self.u16(pe + 20)
        optional = pe + 24
        self.read(optional, optional_size)
        magic = self.u16(optional)
        if magic == 0x10B:
            directory_offset, number_offset = 96, 92
            self.image_base = self.u32(optional + 28)
        elif magic == 0x20B:
            directory_offset, number_offset = 112, 108
            self.image_base = self.u64(optional + 24)
        else:
            raise PEError("Unsupported PE optional-header format")
        if optional_size < directory_offset:
            raise PEError("Truncated PE optional header")
        directory_count = self.u32(optional + number_offset)
        if directory_count > (optional_size - directory_offset) // 8:
            raise PEError("Data directories exceed the optional header")
        self.directories = [struct.unpack("<II", self.read(optional + directory_offset + i * 8, 8))
                            for i in range(directory_count)]
        self.header_size = self.u32(optional + 60)
        section_table = optional + optional_size
        if self.header_size < section_table + count * 40 or self.header_size > len(data):
            raise PEError("Invalid PE header extent")
        self.sections = []
        for index in range(count):
            section = section_table + index * 40
            self.read(section, 40)
            virtual_size, address, raw_size, raw_offset = struct.unpack("<IIII", self.read(section + 8, 16))
            if address + max(virtual_size, raw_size) > 0x100000000:
                raise PEError("Section RVA range overflows")
            if raw_size:
                self.read(raw_offset, raw_size)
            self.sections.append((address, max(virtual_size, raw_size), raw_offset, raw_size))

    def read(self, offset: int, size: int) -> bytes:
        if offset < 0 or size < 0 or offset > len(self.data) or size > len(self.data) - offset:
            raise PEError("Truncated PE metadata")
        return self.data[offset:offset + size]

    def u16(self, offset):
        return struct.unpack("<H", self.read(offset, 2))[0]

    def u32(self, offset):
        return struct.unpack("<I", self.read(offset, 4))[0]

    def u64(self, offset):
        return struct.unpack("<Q", self.read(offset, 8))[0]

    def rva(self, address: int, size: int) -> bytes:
        if address < 0 or address + size > 0x100000000:
            raise PEError("Invalid import RVA")
        candidates = []
        if address < self.header_size and size <= self.header_size - address:
            candidates.append(address)
        for start, extent, raw_offset, raw_size in self.sections:
            if start <= address < start + extent:
                delta = address - start
                if delta <= raw_size and size <= raw_size - delta:
                    candidates.append(raw_offset + delta)
        if len(candidates) != 1:
            raise PEError("Import RVA is unmapped, overlapping, or outside raw section data")
        return self.read(candidates[0], size)

    def name(self, address: int) -> str:
        value = bytearray()
        for offset in range(256):
            char = self.rva(address + offset, 1)[0]
            if char == 0:
                break
            value.append(char)
        else:
            raise PEError("Unterminated or oversized DLL name")
        try:
            name = value.decode("ascii")
        except UnicodeDecodeError as error:
            raise PEError("DLL import name must be ASCII") from error
        if not name or name in (".", "..") or any(c in '/\\:' or ord(c) < 32 for c in name):
            raise PEError("DLL import must contain a basename, not a path")
        return name

    def imports(self) -> list[str]:
        names = {}
        for directory_index, descriptor_size in ((1, 20), (13, 32)):
            if directory_index >= len(self.directories):
                continue
            address, size = self.directories[directory_index]
            if not address and not size:
                continue
            if not address or size < descriptor_size:
                raise PEError("Invalid import directory extent")
            count = min(size // descriptor_size, MAX_DESCRIPTORS)
            for index in range(count):
                descriptor = self.rva(address + index * descriptor_size, descriptor_size)
                if not any(descriptor):
                    break
                if directory_index == 1:
                    name_address = struct.unpack_from("<I", descriptor, 12)[0]
                else:
                    attributes, name_address = struct.unpack_from("<II", descriptor)
                    if attributes & ~1:
                        raise PEError("Unsupported delay-import attributes")
                    if not attributes & 1:
                        name_address -= self.image_base
                if name_address <= 0:
                    raise PEError("Invalid DLL name RVA")
                name = self.name(name_address)
                names.setdefault(name.casefold(), name)
            else:
                raise PEError("Import directory lacks a bounded terminating descriptor")
        return list(names.values())


def imports(path: Path) -> list[str]:
    """Read ordinary and delay imports from PE32 or PE32+ images."""
    path = Path(path)
    try:
        if path.stat().st_size > MAX_IMAGE_BYTES:
            raise PEError("PE image exceeds the supported size limit")
        return _Image(path.read_bytes()).imports()
    except PEError as error:
        raise PEError(f"{path}: {error}") from error
    except OSError as error:
        raise PEError(f"Cannot read local PE dependency {path}: {error}") from error


def copy_dependencies(roots, destination, search_dirs):
    """Resolve and copy a complete local DLL closure, preserving no symlinks.

    Roots are inspected but not copied. Search locations include their parents,
    explicit directories, PATH, and Windows System32. Private application DLLs
    take precedence over ambient PATH/System32 copies. Conflicting private DLL
    contents with the same case-insensitive basename are rejected; ambient
    fallback directories use their search order.
    Return ``(lowercase_basename_to_source_path, sorted_platform_libraries)``.
    """
    roots = [Path(path).resolve(strict=True) for path in roots]
    destination = Path(destination)
    private_dirs = [path.parent for path in roots] + [Path(path) for path in search_dirs]
    fallback_dirs = [Path(part) for part in os.environ.get("PATH", "").split(os.pathsep) if part]
    if os.environ.get("SystemRoot"):
        fallback_dirs.append(Path(os.environ["SystemRoot"]) / "System32")

    def index_directories(directories):
        unique, index = {}, {}
        for directory in directories:
            if directory.is_dir():
                unique.setdefault(directory.resolve(), None)
        for directory in unique:
            for path in directory.iterdir():
                if path.is_file():
                    index.setdefault(path.name.casefold(), []).append(path)
        return index

    private_index = index_directories(private_dirs)
    fallback_index = index_directories(fallback_dirs)
    origins, excluded, hashes = {}, set(), {}

    def digest(path):
        real = path.resolve(strict=True)
        if real not in hashes:
            hashes[real] = hashlib.sha256(real.read_bytes()).digest()
        return hashes[real]

    def resolve(name):
        candidates = private_index.get(name.casefold(), [])
        if not candidates:
            fallback = fallback_index.get(name.casefold(), [])
            candidates = fallback[:1]
        if not candidates:
            raise PEError(f"Missing local Windows dependency: {name}")
        first = candidates[0].resolve(strict=True)
        expected = digest(first)
        if any(digest(candidate) != expected for candidate in candidates[1:]):
            raise PEError(f"Conflicting Windows dependencies named {name}: " +
                          ", ".join(map(str, candidates)))
        return first

    pending, visited = list(roots), set()
    while pending:
        binary = pending.pop()
        if binary in visited:
            continue
        visited.add(binary)
        for name in imports(binary):
            key = name.casefold()
            if is_system_library(key):
                excluded.add(key)
            elif key not in origins:
                source = resolve(name)
                origins[key] = str(source)
                pending.append(source)

    # Resolve the complete graph before copying; a missing transitive DLL must
    # not produce a silently usable-looking partial dependency directory.
    if destination.is_symlink():
        raise PEError(f"Dependency destination must not be a symlink: {destination}")
    for name, source in origins.items():
        target = destination / name
        if target.is_symlink() or (target.exists() and digest(target) != digest(Path(source))):
            raise PEError(f"Destination already contains a conflicting DLL: {target}")
    destination.mkdir(parents=True, exist_ok=True)
    for name, source in origins.items():
        target = destination / name
        if not target.exists():
            shutil.copy2(source, target, follow_symlinks=True)
    return origins, sorted(excluded)
