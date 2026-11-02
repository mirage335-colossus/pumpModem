#!/usr/bin/env python3
"""Tiny completed-tree fixtures; no large campaign or cloud execution."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("spread_collector", Path(__file__).with_name("collect_evidence.py"))
collector = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = collector
SPEC.loader.exec_module(collector)


class CollectorTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="spread-collector-")
        self.addCleanup(self.temp.cleanup)
        self.parent = Path(self.temp.name); self.source = self.parent/"science"; self.source.mkdir()
        for index in (0, 1):
            workspace = self.source/"cohort"/f"path-{index:05d}"; workspace.mkdir(parents=True)
            sizes = {}
            for name in ("gram.c128", "signal_energy.f64", "valid_rows.u64"):
                data = (f"{index}:{name}".encode()+b"\0")*3
                (workspace/name).write_bytes(data); sizes[name] = len(data)
            (workspace/"manifest.json").write_text(json.dumps({"format": "triad-block-stats-v1", "file_sizes": sizes}))
        (self.source/"failed.json").write_text('{"status":"error","command":["/workspace/triad/tool","--failed"]}')
        (self.source/"error.log").write_text("Original failure is preserved.\n")
        (self.source/"scores.npz").write_bytes(b"opaque saved score artifact; collector does not qualify NPZ contents")
        (self.source/"command.txt").write_text("/workspace/triad/tool --mode actual\n")
        self.output = self.parent/"evidence.tar.gz"

    def collect(self, **overrides):
        options = {"keep_workspaces": ["cohort/path-00000"], "completed_source": True}
        options.update(overrides)
        return collector.collect(self.source, self.output, **options)

    def members(self):
        with tarfile.open(self.output, "r:gz") as archive:
            return {member.name: archive.extractfile(member).read() for member in archive if member.isfile()}

    def snapshot(self):
        return {p.relative_to(self.source).as_posix(): (p.read_bytes(), p.stat().st_mtime_ns, p.stat().st_ctime_ns)
                for p in self.source.rglob("*") if p.is_file()}

    def test_all_raw_hashed_omissions_explicit_and_source_unchanged(self):
        before = self.snapshot(); result = self.collect(); members = self.members()
        self.assertEqual(before, self.snapshot())
        self.assertEqual(result["raw_files_hashed"], 6)
        self.assertEqual(result["omitted_raw_files"], 3)
        self.assertEqual(members["failed.json"], before["failed.json"][0])
        self.assertEqual(members["error.log"], before["error.log"][0])
        self.assertIn(b"/workspace/triad/tool", members["command.txt"])
        arrays = json.loads(members["full-array-inventory.json"])["arrays"]
        self.assertEqual(len(arrays), 6)
        for row in arrays:
            self.assertEqual(row["sha256"], hashlib.sha256(before[row["path"]][0]).hexdigest())
            self.assertEqual(row["retained"], row["path"] in members)
            self.assertEqual(row["omitted"], not row["retained"])
            self.assertTrue(row["declared_manifest_size"]["verified"])
        inventory = json.loads(members["full-source-file-inventory.json"])
        self.assertEqual({r["path"] for r in inventory["files"]}, set(before))
        self.assertFalse(result["scientific_qualification"])
        with tarfile.open(self.output) as archive:
            for item in archive:
                self.assertFalse(item.name.startswith("/")); self.assertNotIn("..", Path(item.name).parts)
                self.assertEqual((item.uid, item.gid, item.mtime), (0, 0, 0))
                self.assertFalse(item.issym() or item.islnk())

    def test_deterministic_archive_and_matching_external_inventories(self):
        sidecar = self.parent/"inventories"
        self.collect(inventory_dir=sidecar); first = self.output.read_bytes(); members = self.members()
        for name in collector.RESERVED: self.assertEqual((sidecar/name).read_bytes(), members[name])
        self.output = self.parent/"second.tar.gz"; self.collect()
        self.assertEqual(first, self.output.read_bytes())

    def test_budget_rejects_without_output(self):
        with self.assertRaisesRegex(collector.EvidenceError, "exceed budget"):
            self.collect(raw_budget_mib=0)
        self.assertFalse(self.output.exists())

    def test_invalid_and_partial_json_fail_before_publication(self):
        for filename, contents in (("pending.json", '{"unfinished":'), ("pending.json.partial", '{"unfinished":'),
                                   ("duplicate.json", '{"x":1,"x":2}')):
            with self.subTest(filename=filename):
                path = self.source/filename; path.write_text(contents)
                with self.assertRaisesRegex(collector.EvidenceError, "invalid JSON"):
                    self.collect()
                self.assertFalse(self.output.exists()); path.unlink()

    def test_completed_manifest_size_mismatch_fails(self):
        path = self.source/"cohort/path-00000/manifest.json"; document = json.loads(path.read_text())
        document["file_sizes"]["gram.c128"] += 1; path.write_text(json.dumps(document))
        with self.assertRaisesRegex(collector.EvidenceError, "completed manifest file_sizes mismatch"):
            self.collect()
        self.assertFalse(self.output.exists())

    def test_orphan_and_explicit_failed_manifest_artifacts_preserved(self):
        orphan = self.source/"failed-export"; orphan.mkdir(); (orphan/"gram.c128.partial").write_bytes(b"partial data")
        (orphan/"failure.log").write_text("export failed before a manifest was written\n")
        path = self.source/"cohort/path-00001/manifest.json"; document = json.loads(path.read_text())
        document.update(status="failed"); document["file_sizes"]["gram.c128"] += 9; path.write_text(json.dumps(document))
        self.collect(keep_workspaces=["failed-export"])
        members = self.members(); inventory = json.loads(members["full-array-inventory.json"])
        row = next(r for r in inventory["arrays"] if r["path"].endswith("gram.c128.partial"))
        self.assertTrue(row["retained"] and row["incomplete_or_undeclared"])
        self.assertIsNone(row["declared_manifest_size"])
        self.assertIn("failed-export/failure.log", members)
        checks = json.loads(members["full-source-file-inventory.json"])["manifest_size_checks"]
        self.assertTrue(any(not c["matched"] and c["manifest_incomplete"] for c in checks))

    def test_symlink_escape_and_private_paths_rejected(self):
        outside = self.parent/"outside.json"; outside.write_text("{}")
        link = self.source/"escape.json"; link.symlink_to(outside)
        with self.assertRaisesRegex(collector.EvidenceError, "symlink prohibited"): self.collect()
        link.unlink(); private = self.source/"runpod-token.json"; private.write_text("{}")
        with self.assertRaisesRegex(collector.EvidenceError, "provider/private path"): self.collect()

    def test_destination_source_and_completion_guards(self):
        with self.assertRaisesRegex(collector.EvidenceError, "completed-source"):
            self.collect(completed_source=False)
        with self.assertRaisesRegex(collector.EvidenceError, "outside source"):
            collector.collect(self.source, self.source/"archive.tar.gz", completed_source=True)
        with self.assertRaisesRegex(collector.EvidenceError, "relative path"):
            self.collect(keep_workspaces=["../outside"])
        self.output.write_bytes(b"do not overwrite")
        with self.assertRaisesRegex(collector.EvidenceError, "already exists"): self.collect()
        self.assertEqual(self.output.read_bytes(), b"do not overwrite")

    def test_explicit_receipt_does_not_bypass_raw_budget(self):
        path = self.source/"planned-command.receipt"; path.write_bytes(b"opaque command receipt")
        with self.assertRaisesRegex(collector.EvidenceError, "declare --receipt"): self.collect()
        self.collect(receipts=["planned-command.receipt"])
        self.assertEqual(self.members()["planned-command.receipt"], path.read_bytes())
        self.output.unlink()
        with self.assertRaisesRegex(collector.EvidenceError, "cannot bypass"):
            self.collect(receipts=["planned-command.receipt", "cohort/path-00001/gram.c128"])

    def test_selected_source_mutation_between_hash_and_copy_fails(self):
        original = collector.write_archive
        def mutate_then_write(*args, **kwargs):
            path = self.source/"cohort/path-00000/gram.c128"
            path.write_bytes(b"X"*path.stat().st_size)
            return original(*args, **kwargs)
        with patch.object(collector, "write_archive", side_effect=mutate_then_write):
            with self.assertRaisesRegex(collector.EvidenceError, "source changed"):
                self.collect(inventory_dir=self.parent/"inventories")
        self.assertFalse(self.output.exists()); self.assertFalse((self.parent/"inventories").exists())

    def test_omitted_source_mutation_or_new_file_during_copy_fails(self):
        original = collector.write_archive
        def write_then_mutate(*args, **kwargs):
            original(*args, **kwargs)
            path = self.source/"cohort/path-00001/gram.c128"
            path.write_bytes(b"Y"*path.stat().st_size)
        with patch.object(collector, "write_archive", side_effect=write_then_mutate):
            with self.assertRaisesRegex(collector.EvidenceError, "source tree changed"):
                self.collect()
        self.assertFalse(self.output.exists())
        def write_then_add(*args, **kwargs):
            original(*args, **kwargs); (self.source/"new-report.json").write_text("{}")
        with patch.object(collector, "write_archive", side_effect=write_then_add):
            with self.assertRaisesRegex(collector.EvidenceError, "source tree changed"):
                self.collect()
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
