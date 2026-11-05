#!/usr/bin/env python3
"""Create the compact reproducible Helper broad-study evidence archive.

Use ``--dry-run`` while campaigns are still being written. ``--release`` is an
explicit one-time freeze and refuses to overwrite existing artifacts. Result
JSON/JSONL/log files are preserved verbatim, including full score/calibration
rows. Large I/Q/template arrays and duplicate bulk archives are inventoried but
not embedded. Frozen source overlays are safely unpacked as exact source files.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
EVIDENCE = ROOT / "local-evidence/triad-helper-broad"
ARCHIVE = HERE / "compact-evidence.tar.gz"
MANIFEST = HERE / "artifact-manifest.json"
INVENTORY = HERE / "raw-input-inventory.json"
SUMS = HERE / "SHA256SUMS"
BASE_COMMIT = "eab77e0754d401c4b1e123e3b2e68d8dda4eb133"

TEXT_SUFFIXES = {
    ".json", ".jsonl", ".log", ".txt", ".md", ".py", ".cpp", ".hpp",
    ".h", ".cuh", ".cu", ".csv", ".sha256", ".yaml", ".yml", ".toml",
}
ARRAY_SUFFIXES = {".c128", ".u8", ".c64", ".f32", ".s16le", ".npy", ".npz", ".bin", ".raw", ".pcm", ".iq", ".iq16", ".dat", ".pt"}
IGNORED_PARTS = {"__pycache__", "tmp", ".tmp", ".aws", ".ssh"}
EXCLUDED_ARCHIVES = {
    "frozen-v1/source.tar.gz": "full 1,451-file Git-base source snapshot; reuse declared Git base and exact version overlays",
    "remote/broad-compact-current.tar.gz": "duplicate record bundle; every archive member was confirmed present locally with matching size",
    "completed-evidence.tar.gz": "duplicate subset of expanded evidence files",
    "oracles.tar.gz": "duplicate of expanded remote/oracle JSON and logs",
}
OVERLAY_ARCHIVES = {
    "frozen-v1/overlay.tar.gz": "frozen-v1",
    "frozen-v2/overlay.tar.gz": "frozen-v2",
    "frozen-v3/overlay.tar.gz": "frozen-v3",
    "frozen-v4/overlay.tar.gz": "frozen-v4",
    "frozen-v5/overlay.tar.gz": "frozen-v5",
}
BASE_EXPORTER_MEMBER = "tools/triad-helper-export.cpp"


def digest_stream(stream) -> tuple[str, int]:
    h = hashlib.sha256()
    size = 0
    while True:
        block = stream.read(1024 * 1024)
        if not block:
            break
        h.update(block)
        size += len(block)
    return h.hexdigest(), size


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return digest_stream(stream)[0]


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n")


def safe_relative(path: Path) -> bool:
    return not path.is_absolute() and ".." not in path.parts and "\\" not in path.as_posix()


def permitted_source(path: Path) -> bool:
    if path.is_symlink() or not path.is_file():
        return False
    try:
        rel = path.relative_to(EVIDENCE)
    except ValueError:
        return False
    if any(part.lower() in IGNORED_PARTS for part in rel.parts):
        return False
    low = rel.as_posix().lower()
    if any(word in low for word in ("ssh-key", "private-key", "credential", "billing", "account-metadata", "cloud-credential")):
        return False
    return True


def read_allowed_overlay(archive_path: Path, label: str) -> dict[str, bytes]:
    """Read only safe regular source files from a frozen overlay archive."""
    output: dict[str, bytes] = {}
    with tarfile.open(archive_path, mode="r:gz") as tar:
        for member in tar.getmembers():
            if not member.isfile():
                raise ValueError(f"non-regular source-overlay member: {member.name}")
            original = PurePosixPath(member.name)
            if original.is_absolute() or ".." in original.parts or "\\" in member.name:
                raise ValueError(f"unsafe source-overlay path: {member.name}")
            if Path(member.name).suffix.lower() not in TEXT_SUFFIXES:
                raise ValueError(f"unexpected binary/source-overlay type: {member.name}")
            stream = tar.extractfile(member)
            if stream is None:
                raise ValueError(f"unreadable source-overlay member: {member.name}")
            data = stream.read()
            name = f"source-overlays/{label}/{original.as_posix()}"
            if name in output:
                raise ValueError(f"duplicate source-overlay path: {name}")
            output[name] = data
    return output


def base_exporter_source() -> tuple[str, bytes]:
    source_archive = EVIDENCE / "frozen-v1/source.tar.gz"
    with tarfile.open(source_archive, mode="r:gz") as tar:
        matches = [m for m in tar.getmembers() if m.name == BASE_EXPORTER_MEMBER]
        if len(matches) != 1 or not matches[0].isfile():
            raise ValueError(f"expected exactly one {BASE_EXPORTER_MEMBER} in the frozen v1 base archive")
        stream = tar.extractfile(matches[0])
        if stream is None:
            raise ValueError("frozen base exporter source is unreadable")
        return "source-base/" + BASE_EXPORTER_MEMBER, stream.read()


def validate_duplicate_archives() -> dict:
    """Prove excluded record tarballs are exact duplicates of local text files."""
    specifications = {
        "remote/broad-compact-current.tar.gz": "remote",
        "completed-evidence.tar.gz": "remote",
        "oracles.tar.gz": "remote",
    }
    results = {}
    for archive_rel, local_prefix in specifications.items():
        archive_path = EVIDENCE / archive_rel
        if not archive_path.is_file():
            raise FileNotFoundError(archive_path)
        checked = 0
        with tarfile.open(archive_path, mode="r:gz") as tar:
            names = set()
            for member in tar.getmembers():
                if not member.isfile():
                    raise ValueError(f"duplicate archive includes non-regular member: {member.name}")
                p = PurePosixPath(member.name)
                if p.is_absolute() or ".." in p.parts or "\\" in member.name or member.name in names:
                    raise ValueError(f"unsafe or duplicate tar member in {archive_rel}: {member.name}")
                names.add(member.name)
                local_path = EVIDENCE / local_prefix / p
                if not local_path.is_file() or local_path.stat().st_size != member.size:
                    raise ValueError(f"excluded archive is not a size-matched local duplicate: {archive_rel}:{member.name}")
                archived = tar.extractfile(member)
                if archived is None:
                    raise ValueError(f"unreadable duplicate member: {archive_rel}:{member.name}")
                local = local_path.open("rb")
                try:
                    archived_sha, archived_size = digest_stream(archived)
                    local_sha, local_size = digest_stream(local)
                finally:
                    local.close()
                if archived_size != local_size or archived_sha != local_sha:
                    raise ValueError(f"excluded archive member differs from expanded file: {archive_rel}:{member.name}")
                checked += 1
        results[archive_rel] = {"member_count": checked, "byte_identical_local_copy": True}
    return results


def excluded_inventory() -> dict:
    """Record excluded files cheaply; do not reread regenerable large arrays."""
    records = []
    for path in sorted(EVIDENCE.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        try:
            rel = path.relative_to(EVIDENCE)
        except ValueError:
            continue
        if any(part.lower() in IGNORED_PARTS for part in rel.parts):
            continue
        suffix = path.suffix.lower()
        if suffix in TEXT_SUFFIXES and permitted_source(path):
            continue
        if rel.as_posix() in OVERLAY_ARCHIVES:
            reason = "frozen source overlay expanded verbatim into source-overlays/<version>/"
            checksum = sha256(path)
            method = "sha256"
        elif rel.as_posix() in EXCLUDED_ARCHIVES:
            reason = EXCLUDED_ARCHIVES[rel.as_posix()]
            checksum = sha256(path)
            method = "sha256"
        elif suffix in ARRAY_SUFFIXES:
            reason = "native IQ/template/mask array excluded; generated fixture manifests and results carry their recorded hashes"
            checksum = None
            method = "not reread; regenerable input with existing fixture receipts"
        elif path.name == "exporter-before" or "exporter" in path.name.lower() and not suffix:
            reason = "generated native binary excluded; exact exporter identity/build receipts are retained in JSON"
            checksum = None
            method = "not reread; build identity retained in evidence"
        elif suffix in {".gz", ".tgz", ".tar"}:
            reason = "source/data archive not embedded; see per-member source overlay or duplicate record evidence"
            checksum = sha256(path)
            method = "sha256"
        else:
            reason = "non-allowlisted file type excluded"
            checksum = None
            method = "metadata only"
        item = {"path": rel.as_posix(), "bytes": path.stat().st_size, "embedded": False,
                "reason": reason, "hash_method": method}
        if checksum is not None:
            item["sha256"] = checksum
        records.append(item)
    return {
        "schema": "triad-helper-broad-excluded-input-inventory-v1",
        "scope": EVIDENCE.relative_to(ROOT).as_posix(),
        "entry_count": len(records),
        "total_bytes": sum(r["bytes"] for r in records),
        "entries": records,
        "note": (
            "All allowlisted result JSON, JSONL, logs, source scripts and tests are embedded verbatim and "
            "have hashes in the archive manifest. Large regenerable arrays are not reread for this "
            "inventory; their immutable fixture receipts carry sample/template hashes. Omitted large "
            "archives are hashed where practical and are either exact source-base redundancy or duplicate "
            "expanded result trees."
        ),
    }


def collect_files() -> dict[str, bytes]:
    if not EVIDENCE.is_dir():
        raise FileNotFoundError(EVIDENCE)
    payloads: dict[str, bytes] = {}
    for path in sorted(EVIDENCE.rglob("*")):
        if not permitted_source(path) or path.suffix.lower() not in TEXT_SUFFIXES:
            continue
        relative = path.relative_to(EVIDENCE)
        if relative.as_posix() in OVERLAY_ARCHIVES:
            continue
        name = "evidence/" + relative.as_posix()
        payloads[name] = path.read_bytes()

    # The root-level alias is the source promoted from the local adapter and
    # provides a stable reproduction path independent of local-evidence layout.
    alias = ROOT / "tools/triad_helper_raw_study.py"
    if alias.is_file():
        payloads["source-aliases/tools/triad_helper_raw_study.py"] = alias.read_bytes()
        adapter = EVIDENCE / "mask-diagnosis/raw_study_adapter.py"
        if adapter.is_file() and sha256(alias) != sha256(adapter):
            raise ValueError("promoted raw-study tool no longer matches its frozen adapter alias")

    for rel, label in OVERLAY_ARCHIVES.items():
        path = EVIDENCE / rel
        if not path.is_file():
            raise FileNotFoundError(f"missing frozen overlay: {path}")
        payloads.update(read_allowed_overlay(path, label))

    exporter_name, exporter_data = base_exporter_source()
    payloads[exporter_name] = exporter_data
    packager_name = "evidence/package-evidence.py"
    payloads[packager_name] = Path(__file__).resolve().read_bytes()
    if len(payloads) != len(set(payloads)):
        raise ValueError("duplicate archive member path")
    for name in payloads:
        if not safe_relative(PurePosixPath(name)):
            raise ValueError(f"unsafe member path: {name}")
    return payloads


def check_scope(payloads: dict[str, bytes]) -> dict:
    """Fail a release if a required protocol/data branch is missing."""
    names = set(payloads)
    required = {
        "evidence/protocol.json",
        "evidence/coverage/frozen-protocol.json",
        "evidence/coverage/native-plan-results.json",
        "evidence/coverage/v2/final-frozen-protocol.json",
        "evidence/coverage/v2/final-native-plan-results.json",
        "evidence/assisted-v3-protocol.json",
        "evidence/raw-v3-protocol.json",
        "evidence/frozen-v3/sources.json",
        "evidence/frozen-v4/sources.json",
        "evidence/frozen-v5/sources.json",
        "evidence/local-power-v5/protocol.json",
        "evidence/local-power-v5/rate3p6-corrected-power-independent/protocol.json",
        "evidence/local-power-v5/rate3p6-corrected-power-independent/summary.json",
        "evidence/local-power-v5/rate3p6-corrected-power-independent/trials.jsonl",
        "evidence/local-power-v5/summary.json",
        "evidence/power-v5-cpu.log",
        "evidence/power-v5-cpu-runner.py",
        "evidence/power-v5-cpu-environment.json",
        "evidence/power-v5-cpu-resources.json",
        "evidence/python-tests-cache-v4.log",
        "evidence/analysis/power-v5-audit.json",
        "evidence/analysis/power-v5-audit.md",
        "evidence/analysis/review_power_v5.py",
        "evidence/power-v5-qualification.json",
        "evidence/power-v5-smoke.json",
        "evidence/power-v5-build.log",
        "evidence/power-v5-configure.log",
        "evidence/power-v5-bridge.stdout.json",
        "evidence/power-v5-bridge/results.json",
        "evidence/analysis/cohort-audit.json",
        "evidence/remote/broad-main/external-interruption.json",
        "evidence/remote/broad-main/summary.json",
        "evidence/default-bridge/results.json",
        "evidence/gpu-parity-cpu-final.json",
        "evidence/inference/identity.json",
        "evidence/inference/templates/manifest.json",
        "evidence/mask-diagnosis/source_loss.json",
        "evidence/mask-diagnosis/replay-0000.json",
        "evidence/mask-diagnosis/replay-0001.json",
        "evidence/cache-pilot.py",
        "evidence/source_energy.py",
    }
    required_prefixes = {
        "source-overlays/frozen-v1/",
        "source-overlays/frozen-v2/",
        "source-overlays/frozen-v3/",
        "source-overlays/frozen-v4/",
        "source-overlays/frozen-v5/",
    }
    missing = sorted(required - names)
    missing_prefixes = sorted(prefix for prefix in required_prefixes if not any(n.startswith(prefix) for n in names))
    jsonl = sorted(n for n in names if n.endswith(".jsonl"))
    if missing or missing_prefixes:
        raise ValueError(f"incomplete required evidence: missing={missing}, missing_overlay_content={missing_prefixes}")
    plan = json.loads(payloads["evidence/coverage/v2/final-native-plan-results.json"])
    rows = plan.get("factorized_representative_plan", {}).get("rows", [])
    row_count = plan.get("factorized_representative_plan", {}).get("row_count")
    if row_count != 111 or len(rows) != 111:
        raise ValueError(f"expected 111 final frozen plan rows; found row_count={row_count} actual={len(rows)}")
    power_summary = json.loads(payloads[
        "evidence/local-power-v5/rate3p6-corrected-power-independent/summary.json"
    ])
    power_counts = power_summary.get("outcomes", {}).get("1", {}).get("assisted", {}).get("counts", {})
    if power_summary.get("completed_trials") != 35 or power_counts != {
        "correct_admitted": 8, "wrong_admitted": 0, "missed": 0,
        "false_admitted": 0, "rejected_null": 8, "raw_bit_errors": 0,
    }:
        raise ValueError(f"corrected-power local cohort summary is incomplete or changed: {power_counts}")
    fallback_count = power_summary.get("helper_status", {}).get("1", {}).get("fallback")
    if fallback_count != 35:
        raise ValueError(f"corrected-power local cohort fallback count changed: {fallback_count}")
    test_receipt = payloads["evidence/python-tests-cache-v4.log"].decode("utf-8", errors="strict")
    if "Ran 141 tests in 10.584s" not in test_receipt or "OK" not in test_receipt:
        raise ValueError("expected 141-test v4 receiver/source check receipt is missing or changed")
    receipt_names = sorted(n for n in names if n.startswith("evidence/power-v5-bridge/") and n.endswith("/receipt.json"))
    if len(receipt_names) != 8:
        raise ValueError(f"expected eight native v5 bridge receipts, found {len(receipt_names)}")
    receipt_hashes = {}
    for name in receipt_names:
        value = json.loads(payloads[name])
        checksum = value.get("binary_sha256")
        receipt_hashes[checksum] = receipt_hashes.get(checksum, 0) + 1
    expected_hashes = {
        "41662b4b302d77821353f03245c8c328362cf5e4a88b774adb204273332dff77": 4,
        "3d94458f4b85c73bc9b3364477c550da6cde96dfdd7841f88148588aebc3e4fa": 4,
    }
    if receipt_hashes != expected_hashes:
        raise ValueError(f"native v5 bridge receipt binary hashes changed: {receipt_hashes}")
    if len(jsonl) < 10:
        raise ValueError(f"too few raw JSONL result/source-energy files: {len(jsonl)}")
    v3_sources = json.loads(payloads["evidence/frozen-v3/sources.json"])
    v3_source_checks = {}
    for original_path, expected_hash in v3_sources.items():
        if original_path.startswith("local-evidence/triad-helper-broad/"):
            member = "evidence/" + original_path.removeprefix("local-evidence/triad-helper-broad/")
        else:
            member = "source-overlays/frozen-v3/" + original_path
        data = payloads.get(member)
        if data is None or hashlib.sha256(data).hexdigest() != expected_hash:
            raise ValueError(f"frozen v3 source identity mismatch or missing member: {original_path}")
        v3_source_checks[original_path] = expected_hash
    v4_sources = json.loads(payloads["evidence/frozen-v4/sources.json"])
    v4_source_checks = {}
    for original_path, expected_hash in v4_sources.items():
        member = "source-overlays/frozen-v4/" + original_path
        data = payloads.get(member)
        if data is None or hashlib.sha256(data).hexdigest() != expected_hash:
            raise ValueError(f"frozen v4 source identity mismatch or missing member: {original_path}")
        v4_source_checks[original_path] = expected_hash
    v5_sources = json.loads(payloads["evidence/frozen-v5/sources.json"])
    v5_source_checks = {}
    for original_path, expected_hash in v5_sources.items():
        member = "source-overlays/frozen-v5/" + original_path
        data = payloads.get(member)
        if data is None or hashlib.sha256(data).hexdigest() != expected_hash:
            raise ValueError(f"frozen v5 source identity mismatch or missing member: {original_path}")
        v5_source_checks[original_path] = expected_hash
    duplicate_archives = validate_duplicate_archives()
    return {
        "required_evidence_missing": [],
        "frozen_overlay_file_counts": {
            version: sum(name.startswith(f"source-overlays/{version}/") for name in names)
            for version in ("frozen-v1", "frozen-v2", "frozen-v3", "frozen-v4", "frozen-v5")
        },
        "frozen_plan_records": len(rows),
        "verbatim_jsonl_files": len(jsonl),
        "main_interruption_included": True,
        "default_native_bytebridge_included": True,
        "cpu_cuda_parity_included": True,
        "masked_oracle_and_replays_included": True,
        "cache_pilot_included": True,
        "source_energy_observers_included": True,
        "frozen_v3_identity_entries_verified": len(v3_source_checks),
        "frozen_v4_identity_entries_verified": len(v4_source_checks),
        "frozen_v5_identity_entries_verified": len(v5_source_checks),
        "corrected_power_local_trials": power_summary["completed_trials"],
        "corrected_power_local_outcomes": power_counts,
        "corrected_power_helper_fallbacks": fallback_count,
        "receiver_test_receipt": "141 tests in 10.584s; OK",
        "native_v5_bridge_receipts": len(receipt_names),
        "native_v5_bridge_binary_hash_counts": receipt_hashes,
        "excluded_duplicate_archives_verified": duplicate_archives,
    }


def inventory(payloads: dict[str, bytes]) -> dict:
    excluded = excluded_inventory()
    entries = [
        {"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(), "embedded": True}
        for name, data in sorted(payloads.items())
    ]
    return {
        "schema": "triad-helper-broad-package-input-inventory-v1",
        "source_evidence_root": EVIDENCE.relative_to(ROOT).as_posix(),
        "git_base": BASE_COMMIT,
        "embedded_file_count": len(entries),
        "embedded_total_bytes": sum(x["bytes"] for x in entries),
        "embedded_entries": entries,
        "excluded_inventory": excluded,
        "scope_note": (
            "Result JSON/JSONL, score/calibration records, logs, test/analysis scripts, identities and source "
            "metadata are retained verbatim. Frozen v1-v5 source overlays are embedded as extracted "
            "regular source files. The broad compact bundle is omitted because its 5,648 paths and sizes "
            "were verified against the expanded remote tree; its records are included individually."
        ),
    }


def safe_member(name: str) -> bool:
    path = PurePosixPath(name)
    return bool(name) and not path.is_absolute() and ".." not in path.parts and "\\" not in name


def package() -> dict:
    if any(p.exists() for p in (ARCHIVE, MANIFEST, INVENTORY, SUMS)):
        raise FileExistsError("refusing to overwrite an existing broad Helper evidence package")
    payloads = collect_files()
    scope = check_scope(payloads)
    input_inventory = inventory(payloads)
    payloads["evidence/raw-input-inventory.json"] = (
        json.dumps(input_inventory, indent=2, sort_keys=True, allow_nan=False) + "\n"
    ).encode()
    payloads["evidence/package-scope.json"] = (
        json.dumps(scope, indent=2, sort_keys=True, allow_nan=False) + "\n"
    ).encode()

    entries = [
        {"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        for name, data in sorted(payloads.items())
    ]
    internal_sums = "".join(f"{item['sha256']}  {item['path']}\n" for item in entries)
    payloads["SHA256SUMS"] = internal_sums.encode()
    entries.append({"path": "SHA256SUMS", "bytes": len(payloads["SHA256SUMS"]),
                    "sha256": hashlib.sha256(payloads["SHA256SUMS"]).hexdigest()})
    entries.sort(key=lambda item: item["path"])

    HERE.mkdir(parents=True, exist_ok=True)
    write_json(INVENTORY, input_inventory)
    with ARCHIVE.open("xb") as raw:
        with gzip.GzipFile(filename="", fileobj=raw, mode="wb", mtime=0, compresslevel=6) as gz:
            with tarfile.open(fileobj=gz, mode="w") as tar:
                for entry in entries:
                    data = payloads[entry["path"]]
                    info = tarfile.TarInfo(entry["path"])
                    info.size = len(data)
                    info.mode = 0o644
                    info.mtime = info.uid = info.gid = 0
                    info.uname = info.gname = ""
                    tar.addfile(info, io.BytesIO(data))

    current_head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    manifest = {
        "schema": "triad-helper-broad-evidence-manifest-v1",
        "archive": ARCHIVE.name,
        "archive_bytes": ARCHIVE.stat().st_size,
        "archive_sha256": sha256(ARCHIVE),
        "git_base_revision": BASE_COMMIT,
        "repository_head_at_packaging": current_head,
        "input_inventory": INVENTORY.name,
        "input_inventory_sha256": sha256(INVENTORY),
        "entry_count": len(entries),
        "embedded_total_bytes": sum(x["bytes"] for x in entries),
        "excluded_input_count": input_inventory["excluded_inventory"]["entry_count"],
        "excluded_input_bytes": input_inventory["excluded_inventory"]["total_bytes"],
        "scope": scope,
        "entries": entries,
        "preservation_note": (
            "No result score or calibration data was compacted or dropped. Every allowlisted JSON/JSONL/log "
            "file was retained byte-for-byte; only regenerable native arrays, duplicate bulk archives, and "
            "generated native executables were excluded as detailed in the input inventory."
        ),
    }
    write_json(MANIFEST, manifest)
    SUMS.write_text(
        f"{manifest['archive_sha256']}  {ARCHIVE.name}\n"
        f"{sha256(MANIFEST)}  {MANIFEST.name}\n"
        f"{sha256(INVENTORY)}  {INVENTORY.name}\n"
    )
    return manifest


def verify(manifest: dict) -> dict:
    seen = set()
    with tarfile.open(ARCHIVE, mode="r:gz") as tar:
        members = tar.getmembers()
        if len(members) != len(manifest["entries"]):
            raise ValueError("archive member count mismatch")
        for member, expected in zip(members, manifest["entries"], strict=True):
            if not member.isfile() or member.name != expected["path"] or member.name in seen:
                raise ValueError("archive contains non-regular, duplicate or unexpected path")
            if not safe_member(member.name):
                raise ValueError(f"unsafe archive path: {member.name}")
            low = member.name.lower()
            if any(bad in low for bad in (".c128", ".u8", ".npy", ".npz", ".pem", ".key", "id_rsa", "credentials", "billing", "cloud-account")):
                raise ValueError(f"forbidden large/raw/private artifact in archive: {member.name}")
            seen.add(member.name)
            stream = tar.extractfile(member)
            if stream is None:
                raise ValueError(f"unreadable archive entry: {member.name}")
            checksum, size = digest_stream(stream)
            if size != expected["bytes"] or checksum != expected["sha256"]:
                raise ValueError(f"member hash/size mismatch: {member.name}")
    if sha256(ARCHIVE) != manifest["archive_sha256"] or ARCHIVE.stat().st_size != manifest["archive_bytes"]:
        raise ValueError("whole archive hash/size mismatch")
    return {"verified_entries": len(seen), "archive_bytes": ARCHIVE.stat().st_size,
            "archive_sha256": sha256(ARCHIVE)}


def dry_run() -> dict:
    payloads = collect_files()
    scope = check_scope(payloads)
    inv = inventory(payloads)
    payloads["evidence/raw-input-inventory.json"] = json.dumps(inv, sort_keys=True, allow_nan=False).encode()
    payloads["evidence/package-scope.json"] = json.dumps(scope, sort_keys=True, allow_nan=False).encode()
    uncompressed = sum(len(x) for x in payloads.values())
    output = io.BytesIO()
    with gzip.GzipFile(filename="", fileobj=output, mode="wb", mtime=0, compresslevel=6) as gz:
        with tarfile.open(fileobj=gz, mode="w") as tar:
            for name, data in sorted(payloads.items()):
                info = tarfile.TarInfo(name)
                info.size = len(data)
                info.mode = 0o644
                info.mtime = info.uid = info.gid = 0
                info.uname = info.gname = ""
                tar.addfile(info, io.BytesIO(data))
    return {
        "archive_created": False,
        "embedded_files": len(payloads),
        "uncompressed_bytes_estimate": uncompressed,
        "compressed_bytes_estimate_without_sums_manifest": len(output.getvalue()),
        "raw_jsonl_files": scope["verbatim_jsonl_files"],
        "frozen_plan_records": scope["frozen_plan_records"],
        "overlay_file_counts": scope["frozen_overlay_file_counts"],
        "excluded_input_count": inv["excluded_inventory"]["entry_count"],
        "excluded_input_bytes": inv["excluded_inventory"]["total_bytes"],
        "required_coverage": scope,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--dry-run", action="store_true", help="estimate package and validate scope without writing")
    group.add_argument("--release", action="store_true", help="freeze archive once after explicit release authorization")
    args = parser.parse_args()
    if args.dry_run:
        print(json.dumps(dry_run(), indent=2, sort_keys=True))
    else:
        manifest = package()
        print(json.dumps({k: v for k, v in manifest.items() if k != "entries"}, indent=2, sort_keys=True))
        print(json.dumps(verify(manifest), sort_keys=True))
