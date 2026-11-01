#!/usr/bin/env python3
"""Profile complete finite-state updates on a fixed exported physical design.

This is attribution, not a throughput qualification: profiler instrumentation
perturbs timings. Use triad-optimization-study.py for paired job measurements.
"""
import argparse
import cProfile
import gzip
import hashlib
import json
from pathlib import Path
import time

import numpy as np
import torch

from triad_phase_frequency import PhaseFrequencyLikelihood, PhaseFrequencyModel
from triad_state_workspace import BlockWorkspace


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("input", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--device", default="cuda:0")
    p.add_argument("--backend", choices=("reference", "optimized", "graph"), default="reference")
    p.add_argument("--batch", type=int, default=8)
    p.add_argument("--blocks", type=int, default=16)
    p.add_argument("--phase-states", type=int, default=512)
    p.add_argument("--precision", choices=("fp32", "fp64"), default="fp32")
    p.add_argument("--cn0", type=float, default=-18)
    p.add_argument("--seed", type=int, default=170041)
    p.add_argument("--no-torch-profile", action="store_true", help="for an external Nsight capture")
    a = p.parse_args()
    if a.output.exists() or min(a.batch, a.blocks) < 1:
        p.error("fresh output and positive batch/blocks required")
    a.output.mkdir(parents=True)
    torch.set_num_threads(2)
    torch.set_float32_matmul_precision("highest")
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    device = torch.device(a.device)
    if device.type == "cuda":
        torch.cuda.set_device(device)
    dtype = torch.float32 if a.precision == "fp32" else torch.float64
    cdtype = torch.complex64 if a.precision == "fp32" else torch.complex128
    w = BlockWorkspace(a.input)
    selected = [i for i, m in enumerate(w.column_mapping) if len(m) == 2]
    if not selected:
        p.error("profile needs the public two-tap bank")
    mapping = np.asarray([w.column_mapping[i] for i in selected])
    blocks = []
    for b in w.iter_simulator(seed=a.seed, batch=a.batch, cn0_db_hz=a.cn0,
                              stop=min(a.blocks, w.block_count)):
        r = torch.as_tensor(b.r[:, mapping], device=device, dtype=cdtype)
        g = torch.as_tensor(b.gram[mapping[:, :, None], mapping[:, None, :]], device=device, dtype=cdtype)
        blocks.append((r, g, b.dt_seconds))
    cls = PhaseFrequencyLikelihood
    if a.backend != "reference":
        import triad_phase_optimized as optimized
        cls = (optimized.OptimizedPhaseFrequencyLikelihood if a.backend == "optimized"
               else optimized.GraphPhaseFrequencyLikelihood)
    model = PhaseFrequencyModel(phase_states=a.phase_states,
                               nominal_dt_seconds=w.block_samples/w.sample_rate)

    def state():
        return cls(a.batch, len(selected), 2, model, device=device, dtype=dtype,
                   memory_limit_bytes=24*1024**3, validate_inputs=False)

    def sync():
        if device.type == "cuda":
            torch.cuda.synchronize(device)

    warm = state()
    with torch.inference_mode():
        for r, g, dt in blocks:
            warm.update(r, g, dt)
    sync()
    receiver = state()
    for dt in set(b[2] for b in blocks):
        if dt:
            receiver._transition(dt)
    if a.backend == "graph":
        # Graph pools/captures belong to an instance; warming `warm` above
        # cannot prepare the receiver whose replay is being attributed.
        with torch.inference_mode():
            seen = set()
            for r, g, dt in blocks:
                if dt not in seen:
                    receiver.update(r, g, dt)
                    seen.add(dt)
            receiver.reset()
    sync()
    activities = [torch.profiler.ProfilerActivity.CPU]
    if device.type == "cuda":
        activities.append(torch.profiler.ProfilerActivity.CUDA)
    host_profile = cProfile.Profile()
    start = time.perf_counter()
    execution_seconds = None
    trace_export_seconds = None
    with torch.inference_mode():
        if a.no_torch_profile:
            for r, g, dt in blocks:
                receiver.update(r, g, dt)
            sync()
            execution_seconds = time.perf_counter()-start
        else:
            with torch.profiler.profile(activities=activities, record_shapes=True,
                                        profile_memory=True, with_stack=False) as prof:
                host_profile.enable()
                for r, g, dt in blocks:
                    with torch.profiler.record_function("triad_complete_update"):
                        receiver.update(r, g, dt)
                sync()
                host_profile.disable()
                execution_seconds = time.perf_counter()-start
            export_start = time.perf_counter()
            trace = a.output/"trace.json"
            prof.export_chrome_trace(str(trace))
            with trace.open("rb") as src, gzip.open(a.output/"trace.json.gz", "wb") as dst:
                for chunk in iter(lambda: src.read(1024*1024), b""):
                    dst.write(chunk)
            trace.unlink()
            (a.output/"operators.txt").write_text(prof.key_averages().table(
                sort_by="self_cuda_time_total" if device.type == "cuda" else "self_cpu_time_total",
                row_limit=60))
            host_profile.dump_stats(a.output/"host.pstats")
            trace_export_seconds = time.perf_counter()-export_start
    sync()
    elapsed = time.perf_counter()-start
    props = torch.cuda.get_device_properties(device) if device.type == "cuda" else None
    record = {"schema": "triad-profile-v1", "scope": "instrumented prepared fixed-design complete-update attribution; excludes input preparation, transfer, receiver construction, JIT and graph warmup/capture; wall includes trace formatting/export when enabled",
              "configuration": {k: str(v) if isinstance(v, Path) else v for k, v in vars(a).items()},
              "torch": torch.__version__, "cuda": torch.version.cuda,
              "device": str(props), "instrumented_wall_seconds": elapsed,
              "instrumented_update_wall_seconds": execution_seconds,
              "trace_export_and_formatting_wall_seconds": trace_export_seconds,
              "input_manifest_sha256": sha256_file(a.input/"manifest.json"),
              "input_files": {name: {"bytes": (a.input/name).stat().st_size,
                                      "sha256": sha256_file(a.input/name)}
                              for name in sorted(w.manifest["file_sizes"])},
              "source_sha256": {f.name: sha256_file(f) for f in
                  (Path(__file__), Path(__file__).with_name("triad_phase_frequency.py"),
                   Path(__file__).with_name("triad_phase_optimized.py"))},
              "diagnostics": receiver.diagnostics(),
              "counter_scope": "torch/CPU trace; SM/tensor/DRAM/cache/occupancy hardware counters require separate Nsight capture",
              "scores": receiver.scores().cpu().tolist()}
    (a.output/"summary.json").write_text(json.dumps(record, indent=2)+"\n")


if __name__ == "__main__":
    main()
