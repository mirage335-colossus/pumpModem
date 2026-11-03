#!/usr/bin/env python3
"""Verify the preserved extension archive without extracting its 42.9 GB tree.

Memory is bounded by a 32-MiB inventory, at most 100,000 inventory entries,
1-MiB payload reads, and 64-KiB PAX/long-name headers. No tar member is written
to disk. The inventory's receipt-binding counts are metadata from the remote
packer; this verifies all archived bytes, not a second semantic receipt audit.
"""
from __future__ import annotations

import argparse
import contextlib
import datetime
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import signal
import subprocess
import tarfile
import tempfile
import time
import unittest


CHUNK = 1024 * 1024
MAX_JSON = 32 * CHUNK
MAX_FILES = 100_000
MAX_EXTENSION = 64 * 1024
PREFIX = "triad-results/"
INDEX = PREFIX + "evidence-inventory.json"
HEX = re.compile(r"[0-9a-f]{64}\Z")


class VerificationError(ValueError):
    pass


def require(ok, message):
    if not ok:
        raise VerificationError(message)


def canonical(name):
    require(isinstance(name, str) and 0 < len(name) <= MAX_EXTENSION,
            "invalid archive path length/type")
    require("\\" not in name and not any(ord(c) < 32 or ord(c) == 127 for c in name),
            f"invalid archive path characters: {name!r}")
    parts = name.split("/")
    require(all(p not in ("", ".", "..") for p in parts) and
            not PurePosixPath(name).is_absolute(), f"noncanonical/traversing path: {name!r}")
    return name


def digest_file(path):
    h = hashlib.sha256()
    count = 0
    with path.open("rb") as src:
        while data := src.read(CHUNK):
            h.update(data)
            count += len(data)
    return count, h.hexdigest()


def read_json(path):
    require(path.stat().st_size <= MAX_JSON, f"JSON exceeds {MAX_JSON} bytes: {path}")
    raw = path.read_bytes()
    require(len(raw) <= MAX_JSON, f"JSON grew beyond its bound: {path}")
    # Duplicate dictionary keys must not silently replace an inventory entry.
    def pairs(items):
        out = {}
        for key, value in items:
            require(key not in out, f"duplicate JSON key: {key}")
            out[key] = value
        return out
    return raw, json.loads(raw, object_pairs_hook=pairs)


def expected_members(inventory, raw_inventory):
    require(inventory.get("schema") == "triad-extension-preservation-v1", "inventory schema mismatch")
    files = inventory.get("files")
    require(isinstance(files, dict) and 0 < len(files) <= MAX_FILES, "inventory file-count bound")
    expected = {}
    logical = 0
    for name, item in files.items():
        canonical(name)
        require(isinstance(item, dict), f"invalid inventory record: {name}")
        size, sha = item.get("size_bytes"), item.get("sha256")
        require(type(size) is int and size >= 0 and isinstance(sha, str) and HEX.fullmatch(sha),
                f"invalid size/hash: {name}")
        arc = canonical(PREFIX + name)
        require(arc != INDEX, "inventory must not recursively list itself")
        expected[arc] = (size, sha)
        logical += size
    require(len(files) == inventory.get("payload_files"), "inventory payload count mismatch")
    require(logical == inventory.get("logical_bytes"), "inventory logical byte count mismatch")
    expected[INDEX] = (len(raw_inventory), hashlib.sha256(raw_inventory).hexdigest())
    return expected


class BoundedTarInfo(tarfile.TarInfo):
    # tarfile's streaming reader otherwise materializes arbitrary PAX/long-name
    # extension bodies. This guard runs before its extension parser reads one.
    def _proc_member(self, archive):
        require(self.type in (tarfile.REGTYPE, tarfile.AREGTYPE, tarfile.LNKTYPE,
                              tarfile.XHDTYPE, tarfile.GNUTYPE_LONGNAME,
                              tarfile.GNUTYPE_LONGLINK), "unsupported tar header type")
        if self.type in (tarfile.XHDTYPE, tarfile.XGLTYPE, tarfile.GNUTYPE_LONGNAME,
                         tarfile.GNUTYPE_LONGLINK):
            require(0 <= self.size <= MAX_EXTENSION, "tar extension header exceeds 64 KiB")
        return super()._proc_member(archive)

    # PAX sparse extensions are parsed before a completed member is yielded.
    # Refuse them before tarfile can allocate/read their advertised sparse map.
    def _proc_gnusparse_00(self, *args):
        raise VerificationError("GNU sparse PAX member is unsupported")

    _proc_gnusparse_01 = _proc_gnusparse_00
    _proc_gnusparse_10 = _proc_gnusparse_00


def verify_tar_stream(stream, expected):
    verified = {}
    logical_bytes = regular_bytes = hardlinks = 0
    with tarfile.open(fileobj=stream, mode="r|", bufsize=CHUNK, tarinfo=BoundedTarInfo) as archive:
        for member in archive:
            name = canonical(member.name)
            require(name in expected, f"unexpected archive member: {name}")
            require(name not in verified, f"duplicate archive member: {name}")
            wanted_size, wanted_sha = expected[name]
            require(member.sparse is None and not any(k.startswith("GNU.sparse") for k in member.pax_headers),
                    f"sparse member is unsupported: {name}")
            if member.isreg():
                require(member.size == wanted_size, f"member size mismatch: {name}")
                h = hashlib.sha256()
                size = 0
                src = archive.extractfile(member)
                require(src is not None, f"missing member stream: {name}")
                with src:
                    while data := src.read(CHUNK):
                        size += len(data)
                        require(size <= wanted_size, f"member exceeds declared size: {name}")
                        h.update(data)
                require(size == wanted_size and h.hexdigest() == wanted_sha,
                        f"member content hash/size mismatch: {name}")
                if name != INDEX:
                    regular_bytes += size
            elif member.islnk():
                require(name != INDEX, "included inventory must be a regular member")
                require(member.size == 0, f"hardlink has unexpected payload: {name}")
                target = canonical(member.linkname)
                require(target in verified, f"hardlink target is not an earlier verified member: {name}")
                require(verified[target] == (wanted_size, wanted_sha),
                        f"hardlink target content mismatch: {name}")
                hardlinks += 1
            else:
                raise VerificationError(f"unexpected archive member type {member.type!r}: {name}")
            verified[name] = (wanted_size, wanted_sha)
            if name != INDEX:
                logical_bytes += wanted_size
            # Hardlinks are checked against our compact verified digest map,
            # never resolved by tarfile. Do not retain per-file PAX/TarInfo
            # objects across the stream (also supports pre-3.13 tarfile).
            archive.members.clear()
        # tarfile stops at the first zero header. Drain its own buffered stream
        # to EOF, rejecting hidden concatenated tar payloads/trailing garbage.
        padding = 0
        while data := archive.fileobj.read(CHUNK):
            padding += len(data)
            require(padding <= CHUNK and not any(data), "nonzero/excessive data after tar terminator")
    missing = set(expected) - set(verified)
    require(not missing, f"missing {len(missing)} members; first: {sorted(missing)[:3]}")
    return {"payload_files_verified": len(verified) - 1, "included_inventory_verified": True,
            "logical_bytes_verified": logical_bytes, "regular_payload_bytes_verified": regular_bytes,
            "hardlink_aliases_verified": hardlinks, "total_members_verified": len(verified)}


def reap(process):
    if process.poll() is None:
        with contextlib.suppress(ProcessLookupError):
            os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            with contextlib.suppress(ProcessLookupError):
                os.killpg(process.pid, signal.SIGKILL)
            process.wait(timeout=5)
    else:
        process.wait()


@contextlib.contextmanager
def deadline(seconds):
    def expired(signum, frame):
        raise TimeoutError("archive verification exceeded its wall-time bound")
    old_handler = signal.signal(signal.SIGALRM, expired)
    old_timer = signal.setitimer(signal.ITIMER_REAL, seconds)
    try:
        yield
    finally:
        signal.setitimer(signal.ITIMER_REAL, *old_timer)
        signal.signal(signal.SIGALRM, old_handler)


def verify(archive_path, inventory_path, metadata_path, max_seconds):
    started = time.monotonic()
    with deadline(max_seconds):
        metadata_raw, metadata = read_json(metadata_path)
        inventory_raw, inventory = read_json(inventory_path)
        require(metadata.get("schema") == "triad-extension-archive-v1", "archive metadata schema mismatch")
        require(archive_path.name == metadata.get("archive"), "archive filename mismatch")
        require(hashlib.sha256(inventory_raw).hexdigest() == metadata.get("inventory_sha256"),
                "external inventory SHA256 mismatch")
        expected = expected_members(inventory, inventory_raw)
        for key in ("payload_files", "logical_bytes", "receipt_bound_files", "receipt_count"):
            require(metadata.get(key) == inventory.get(key), f"inventory/metadata {key} mismatch")
        before = archive_path.stat()
        require(before.st_size == metadata.get("archive_bytes"), "archive size mismatch/incomplete download")
        size, sha = digest_file(archive_path)
        require(size == metadata["archive_bytes"] and sha == metadata.get("archive_sha256"),
                "full archive SHA256/size mismatch")
        process = subprocess.Popen(["zstd", "-q", "-d", "-c", "--", str(archive_path)],
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, start_new_session=True)
        try:
            result = verify_tar_stream(process.stdout, expected)
            process.stdout.close()
            require(process.wait(timeout=10) == 0, "zstd decompressor failed")
        finally:
            if process.stdout:
                process.stdout.close()
            reap(process)
        after = archive_path.stat()
        require((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns) ==
                (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns),
                "archive changed during verification")
        require(result["hardlink_aliases_verified"] == metadata.get("tar_hardlink_aliases"),
                "hardlink count mismatch")
        require(result["regular_payload_bytes_verified"] == metadata.get("unique_content_bytes"),
                "regular payload byte total mismatch")
        require(result["logical_bytes_verified"] == metadata["logical_bytes"], "logical byte total mismatch")
    return {"schema": "triad-extension-local-archive-verification-v1", "status": "verified",
            "archive": str(archive_path.resolve()), "archive_bytes": size, "archive_sha256": sha,
            "inventory_sha256": hashlib.sha256(inventory_raw).hexdigest(),
            "archive_metadata_sha256": hashlib.sha256(metadata_raw).hexdigest(),
            "verifier_sha256": digest_file(Path(__file__))[1],
            "elapsed_seconds": time.monotonic() - started,
            "completed_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "extracted_payload_bytes": 0, "decompressor_exit_status": 0, "decompressor_reaped": True,
            "payload_read_chunk_bytes": CHUNK, "maximum_json_bytes": MAX_JSON,
            "maximum_extension_header_bytes": MAX_EXTENSION, "maximum_inventory_files": MAX_FILES,
            "receipt_bound_files_reported_by_packer": inventory["receipt_bound_files"],
            "receipt_count_reported_by_packer": inventory["receipt_count"],
            "receipt_scope": "All inventory bytes verified; receipt-to-output semantic binding was checked by the remote packer, not repeated here.",
            **result}


class SelfTests(unittest.TestCase):
    def fixture(self, changes=None):
        contents = {"a": b"payload", "empty": b"", "alias": b"payload"}
        inv = {"schema": "triad-extension-preservation-v1", "payload_files": 3,
               "logical_bytes": 14, "receipt_bound_files": 0, "receipt_count": 0,
               "files": {n: {"size_bytes": len(v), "sha256": hashlib.sha256(v).hexdigest()}
                                               for n, v in contents.items()}}
        raw = json.dumps(inv).encode()
        members = [(PREFIX+"a", b"payload"), (PREFIX+"empty", b""),
                   (PREFIX+"alias", (tarfile.LNKTYPE, PREFIX+"a")), (INDEX, raw)]
        if changes:
            members = changes(members)
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w") as archive:
            for name, data in members:
                info = tarfile.TarInfo(name)
                if isinstance(data, tuple):
                    info.type, info.linkname = data
                    archive.addfile(info)
                else:
                    info.size = len(data)
                    archive.addfile(info, io.BytesIO(data))
        return buf.getvalue(), expected_members(inv, raw)

    def test_regular_empty_earlier_hardlink_and_inventory(self):
        data, expected = self.fixture()
        result = verify_tar_stream(io.BytesIO(data), expected)
        self.assertEqual(result["payload_files_verified"], 3)
        self.assertEqual(result["hardlink_aliases_verified"], 1)
        self.assertEqual(result["logical_bytes_verified"], 14)
        self.assertEqual(result["regular_payload_bytes_verified"], 7)

    def test_rejections(self):
        variants = {
            "traversal": lambda m: [("triad-results/../a", m[0][1]), *m[1:]],
            "absolute": lambda m: [("/triad-results/a", m[0][1]), *m[1:]],
            "duplicate": lambda m: [m[0], *m],
            "missing": lambda m: m[1:],
            "unexpected": lambda m: [*m, (PREFIX+"extra", b"")],
            "symlink": lambda m: [m[0], m[1], (PREFIX+"alias", (tarfile.SYMTYPE, PREFIX+"a")), m[3]],
            "directory": lambda m: [m[0], (PREFIX+"empty", (tarfile.DIRTYPE, "")), *m[2:]],
            "forward_link": lambda m: [m[2], *m[:2], m[3]],
            "wrong_link": lambda m: [*m[:2], (PREFIX+"alias", (tarfile.LNKTYPE, PREFIX+"empty")), m[3]],
            "wrong_hash": lambda m: [(m[0][0], b"PAYLOAD"), *m[1:]],
            "wrong_size": lambda m: [(m[0][0], b"x"), *m[1:]],
            "wrong_inventory": lambda m: [*m[:3], (INDEX, b"x")],
        }
        for label, mutate in variants.items():
            with self.subTest(label=label):
                data, expected = self.fixture(mutate)
                with self.assertRaises(VerificationError):
                    verify_tar_stream(io.BytesIO(data), expected)

    def test_trailing_payload(self):
        data, expected = self.fixture()
        with self.assertRaises(VerificationError):
            verify_tar_stream(io.BytesIO(data + b"hidden"), expected)

    def test_pax_size_bound(self):
        data, expected = self.fixture()
        extension = tarfile.TarInfo("pax")
        extension.type = tarfile.XHDTYPE
        extension.size = MAX_EXTENSION + 1
        with self.assertRaises(VerificationError):
            verify_tar_stream(io.BytesIO(extension.tobuf() + data), expected)

    def test_pax_long_paths_and_hardlink_targets(self):
        name = PREFIX + "long-name-"*20 + "/payload"
        alias = PREFIX + "alias"
        contents = b"abc"
        inv = {"schema": "triad-extension-preservation-v1", "payload_files": 2,
               "logical_bytes": 6, "files": {
                   n[len(PREFIX):]: {"size_bytes": 3, "sha256": hashlib.sha256(contents).hexdigest()}
                   for n in (name, alias)}}
        raw = json.dumps(inv).encode()
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w", format=tarfile.PAX_FORMAT) as archive:
            for path, value in ((name, contents), (alias, None), (INDEX, raw)):
                info = tarfile.TarInfo(path)
                info.mtime = 1.25
                if value is None:
                    info.type = tarfile.LNKTYPE
                    info.linkname = name
                    archive.addfile(info)
                else:
                    info.size = len(value)
                    archive.addfile(info, io.BytesIO(value))
        result = verify_tar_stream(io.BytesIO(buf.getvalue()), expected_members(inv, raw))
        self.assertEqual(result["hardlink_aliases_verified"], 1)

    def test_reap_live_child(self):
        process = subprocess.Popen(["sleep", "30"], start_new_session=True)
        reap(process)
        self.assertIsNotNone(process.returncode)
        with self.assertRaises(ChildProcessError):
            os.waitpid(process.pid, os.WNOHANG)

    def test_full_pipeline_and_external_hash_rejection(self):
        data, expected = self.fixture()
        # These temporary fixtures are unrelated to the real download.
        with tempfile.TemporaryDirectory(prefix="triad-archive-verifier-test-") as directory:
            root = Path(directory)
            arc, inv, meta = root/"test.tar.zst", root/"inventory.json", root/"archive.json"
            with tarfile.open(fileobj=io.BytesIO(data), mode="r:") as archive:
                inv.write_bytes(archive.extractfile(INDEX).read())
            compressed = subprocess.run(["zstd", "-q", "-c"], input=data, capture_output=True,
                                        check=True, timeout=5).stdout
            arc.write_bytes(compressed)
            metadata = {"schema": "triad-extension-archive-v1", "archive": arc.name,
                        "archive_sha256": hashlib.sha256(compressed).hexdigest(), "archive_bytes": len(compressed),
                        "inventory_sha256": digest_file(inv)[1], "payload_files": 3,
                        "logical_bytes": 14, "unique_content_bytes": 7, "tar_hardlink_aliases": 1,
                        "receipt_bound_files": 0, "receipt_count": 0}
            meta.write_text(json.dumps(metadata))
            result = verify(arc, inv, meta, 10)
            self.assertEqual(result["status"], "verified")
            self.assertEqual(result["extracted_payload_bytes"], 0)
            metadata["archive_sha256"] = "0"*64
            meta.write_text(json.dumps(metadata))
            with self.assertRaises(VerificationError):
                verify(arc, inv, meta, 10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    here = Path(__file__).resolve().parent
    parser.add_argument("--archive", type=Path, default=here/"triad-extension-evidence.tar.zst")
    parser.add_argument("--inventory", type=Path, default=here/"evidence-inventory.json")
    parser.add_argument("--metadata", type=Path, default=here/"triad-extension-evidence.archive.json")
    parser.add_argument("--output", type=Path, default=here/"archive-verification.json")
    parser.add_argument("--max-seconds", type=float, default=900)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(SelfTests)
        return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1
    require(0 < args.max_seconds <= 7200, "verification time bound must be in(0,7200] seconds")
    require(not args.output.exists(), "verification output exists; refusing overwrite")
    result = verify(args.archive, args.inventory, args.metadata, args.max_seconds)
    # Create only after full verification; a failed attempt cannot resemble a
    # completed preservation proof or replace an existing one.
    with args.output.open("x") as dst:
        json.dump(result, dst, indent=2, sort_keys=True)
        dst.write("\n")
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
