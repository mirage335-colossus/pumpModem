#!/usr/bin/env python3
"""Replay report classification only; never execute or requalify a workload."""
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path

here = Path(__file__).resolve().parent
root = here.parents[4]
source = root / "tools/triad_perf_complete.py"
spec = importlib.util.spec_from_file_location("triad_complete_replay", source)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
read = lambda name: json.loads(gzip.decompress((here / "cpu-complete-001" / name).read_bytes()))
original = read("summary.json.gz")
report = dict(original)
matrix_path = root / "tools/triad-performance-coverage.json"
matrix = json.loads(matrix_path.read_text())
report["inventory_unresolved"] = module.inventory_gaps(matrix)
report["inventory_limitations"] = module.inventory_limitations(matrix)
expected = original["timing_cases"]["expected_sampled_cases"]
sampled = {name: read(f"sampled/{name}/result.json.gz") for name in expected}
identical = {name: read(f"identical-fft/{name}/result.json.gz")
             for name in original["timing_cases"]["expected_identical_fft_cases"]}
result = module.summarize(report, read("native-fft.json.gz"), sampled, expected, identical)
output = {
    "schema": "triad-performance-classification-replay-v1",
    "scope": "Classification only. Original measurements, source identities and coverage receipts are unchanged. Current matrix dispositions may reference separately recorded follow-up checks. No retiming or fresh model qualification.",
    "original_summary_sha256": hashlib.sha256(gzip.decompress((here / "cpu-complete-001/summary.json.gz").read_bytes())).hexdigest(),
    "wrapper_sha256": module.digest(source),
    "matrix_sha256": module.digest(matrix_path),
    "original_wall_seconds": original["wall_seconds"],
    "original_status": original["status"],
    "classification": result,
}
(here / "classification-replay.json").write_text(json.dumps(output, indent=2, allow_nan=False) + "\n")
print(json.dumps({key: result[key] for key in ("status", "warm_target_decisions_complete", "complete_scope_qualification", "scope_blockers")}, indent=2))
