#!/usr/bin/env python3
"""Tiny all-valid identity check against the retained pre-erasure-patch run.

Run: OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 PYTHONDONTWRITEBYTECODE=1 \
     /tmp/triad-weak-env/bin/python local-evidence/triad-tracking-200/erasure/compare.py

The old JSON was captured before production mutation. This script reproduces
that exact fixture recipe; it does not rerun or reconstruct the old source.
Its evidence concerns the all-valid likelihood branch, not physical false
admissions or performance equivalence of configurations not exercised here.
"""
import hashlib
import json
import math
from pathlib import Path
import platform
import sys
import time

import numpy as np
import torch

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from triad_tracking_receiver import TrackingReceiver
from triad_timing_forward import SparseTimingForward, TimingState

EXPECTED_OLD = "0dbe8809f454eb9f5afa2592960590d92d27c223eef75bb1c65dea15c6022034"
EXPECTED_NEW = "a113e42683d05f0f8e8d264e22f14ab8b8fa55ebcd00cd9d3cca67de9deb3c79"


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    cpu, wall = time.process_time(), time.perf_counter()
    torch.set_num_threads(1)
    before = json.loads((HERE / "old-reference.json").read_text())
    source_hash = sha(ROOT / "tools/triad_tracking_receiver.py")
    if before["source_sha256"] != EXPECTED_OLD or source_hash != EXPECTED_NEW:
        raise RuntimeError("source identity changed; this comparison needs explicit requalification")
    rng = np.random.default_rng(73183)
    n = 44
    basis = np.zeros((3, 1, 1, n), complex)
    basis[1:] = rng.normal(size=(2, 1, 1, n)) + 1j*rng.normal(size=(2, 1, 1, n))
    values = rng.normal(size=n) + 1j*rng.normal(size=n)
    receiver = TrackingReceiver(sample_rate=16, samples=n,
        segment_samples=19, block_samples=8,
        basis_provider=lambda states, anchor, lo, hi, origin, offsets: basis[..., lo:hi],
        timing_forward=SparseTimingForward([TimingState(0, 0, 0)], model_count=2,
            maximum_rate_ppm=0, maximum_acceleration_ppm_per_second=0),
        required_bands=[[-7, 7]], noise_variance=1.7, stationary_power=(.8,),
        coherence_seconds=(.6, math.inf), precision="fp64", projection_mode="dft",
        maximum_input_chunk_samples=128, memory_limit_bytes=16*1024**2)
    receiver.push(0, values)
    result = receiver.finish()
    assert result["bit_scores"] == before["all_valid_bit_scores"]
    assert result["blocks"] == before["all_valid_blocks"] == 5
    assert result["requested_invalid_rows"] == result["additional_discarded_rows"] == 0
    comparison = {
        "status": "exact_all_valid_score_and_block_identity",
        "before_source_sha256": before["source_sha256"],
        "after_source_sha256": source_hash,
        "old_reference_sha256": sha(HERE / "old-reference.json"),
        "reproducer_sha256": sha(__file__),
        "supporting_source_sha256": {name: sha(ROOT / "tools" / name) for name in (
            "triad_incremental_receiver.py", "triad_timing_forward.py",
            "triad_gaussian_state.py", "triad_dynamic_joint.py")},
        "seed": 73183, "samples": n, "sample_rate": 16,
        "segment_samples": 19, "block_samples": 8, "projection": "dft",
        "arithmetic": "fp64", "mask": "all valid", "timing": "one fixed zero-rate state",
        "noise_variance": 1.7, "stationary_power": [.8], "coherences": [.6, "infinite"],
        "values_sha256": hashlib.sha256(values.tobytes()).hexdigest(),
        "basis_sha256": hashlib.sha256(basis.tobytes()).hexdigest(),
        "before_scores": before["all_valid_bit_scores"], "after_scores": result["bit_scores"],
        "before_blocks": before["all_valid_blocks"], "after_blocks": result["blocks"],
        "before_partial_erasure_failure": before["original_partial_erasure_failure"],
        "score_comparison": "Python float list equality; no tolerance",
        "cpu_seconds_excluding_imports": time.process_time()-cpu,
        "wall_seconds_excluding_imports": time.perf_counter()-wall,
        "environment": {"python": platform.python_version(), "numpy": np.__version__,
            "torch": torch.__version__, "torch_threads": torch.get_num_threads()},
        "scope": "Preserves the all-valid branch supporting safe reuse of earlier cable evidence; not a new cable or physical-null qualification",
    }
    (HERE / "comparison.json").write_text(json.dumps(comparison, indent=2)+"\n")
    print(json.dumps({k: comparison[k] for k in ("status", "before_source_sha256", "after_source_sha256", "after_scores", "cpu_seconds_excluding_imports")}))


if __name__ == "__main__":
    main()
