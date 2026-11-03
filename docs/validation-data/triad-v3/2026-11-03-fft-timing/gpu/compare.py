#!/usr/bin/env python3
"""Bounded cold-start CPU/GPU precision comparison for one sampled FFT capture.

Each invocation runs six fresh CLI processes on the identical sampled input.
This measures CLI cold-start-inclusive latency and does not claim resident
throughput. The complete per-group two-bit likelihood table is retained.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

SOURCE_CLOSURE = (
    "tools/triad_fft_acquire.py",
    "tools/triad_group_acquire.py",
    "tools/triad_gaussian_window.py",
    "tools/triad_noise_likelihood.py",
    "tools/triad_gaussian_state.py",
    "tools/triad_state_workspace.py",
)
JOBS = (
    ("cpu-fp64-a", "cpu", "fp64"),
    ("cpu-fp32-a", "cpu", "fp32"),
    ("cuda-fp32-a", "cuda", "fp32"),
    ("cuda-fp32-b", "cuda", "fp32"),
    ("cpu-fp32-b", "cpu", "fp32"),
    ("cpu-fp64-b", "cpu", "fp64"),
)
CFO_HZ = (-0.0030001725001500084, 0.0, 0.0030001725001500084)
JOB_TIMEOUT_SECONDS = 120
CAMPAIGN_TIMEOUT_SECONDS = 13 * 60


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def versions() -> dict:
    result = {"python": sys.version, "platform": platform.platform()}
    for name in ("numpy", "scipy", "torch"):
        try:
            module = __import__(name)
            result[name] = getattr(module, "__version__", "unknown")
            if name == "torch":
                result["torch_cuda_build"] = getattr(module.version, "cuda", None)
                result["cuda_available"] = bool(module.cuda.is_available())
                result["cuda_devices"] = [module.cuda.get_device_name(i)
                                           for i in range(module.cuda.device_count())]
        except Exception as exc:  # dependency metadata is still recorded
            result[name] = {"error": f"{type(exc).__name__}: {exc}"}
    return result


def rss_bytes(pid: int) -> int | None:
    try:
        for line in Path(f"/proc/{pid}/status").read_text().splitlines():
            if line.startswith("VmRSS:"):
                return int(line.split()[1]) * 1024
    except (OSError, ValueError, IndexError):
        return None
    return None


def run_job(label: str, device: str, precision: str, args, outdir: Path,
            deadline: float) -> dict:
    score_path = outdir / f"{label}.scores.json"
    stdout_path, stderr_path = outdir / f"{label}.stdout.log", outdir / f"{label}.stderr.log"
    status_path = outdir / f"{label}.status.json"
    if any(p.exists() for p in (score_path, stdout_path, stderr_path, status_path)):
        raise FileExistsError(f"refusing to overwrite {label} outputs")
    command = [sys.executable, str(args.receiver), str(args.input),
               "--lag-max-seconds", "14.07", "--lag-step-samples", "1",
               "--frequency-offsets-hz", *(repr(x) for x in CFO_HZ),
               "--drift-offsets-hz-per-second", "0",
               "--group-tile", "512", "--memory-mib", "1024",
               "--device", device, "--precision", precision,
               "--evaluate-simulation",
               "--output", str(score_path)]
    env = os.environ.copy()
    for key in ("OMP_NUM_THREADS", "MKL_NUM_THREADS", "OPENBLAS_NUM_THREADS", "NUMEXPR_NUM_THREADS"):
        env[key] = "1"
    started = time.monotonic()
    remaining = min(JOB_TIMEOUT_SECONDS, max(0.0, deadline - started))
    status = {"schema": "triad-fft-precision-job-v1", "job": label,
              "device": device, "precision": precision, "command": command,
              "timeout_seconds": JOB_TIMEOUT_SECONDS,
              "status": "not-started", "wall_seconds": None,
              "peak_child_rss_bytes": None, "returncode": None,
              "score_file": str(score_path)}
    peak = None
    if remaining <= 0:
        status["status"] = "campaign-time-budget-exhausted"
    else:
        with stdout_path.open("x") as stdout, stderr_path.open("x") as stderr:
            try:
                child = subprocess.Popen(command, cwd=args.cwd, env=env,
                                         stdout=stdout, stderr=stderr, shell=False)
                status["status"] = "running"
                while child.poll() is None:
                    current = rss_bytes(child.pid)
                    if current is not None:
                        peak = max(peak or 0, current)
                    elapsed = time.monotonic() - started
                    if elapsed >= remaining:
                        child.terminate()
                        try:
                            child.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            child.kill(); child.wait()
                        status["status"] = "timeout"
                        break
                    time.sleep(0.025)
                if status["status"] == "running":
                    status["returncode"] = child.returncode
                    status["status"] = "completed" if child.returncode == 0 else "failed-exit"
                else:
                    status["returncode"] = child.returncode
            except OSError as exc:
                status["status"] = "launch-error"
                status["error"] = f"{type(exc).__name__}: {exc}"
    status["wall_seconds"] = round(time.monotonic() - started, 6)
    status["peak_child_rss_bytes"] = peak
    status["stdout_log"] = str(stdout_path)
    status["stderr_log"] = str(stderr_path)
    if status["status"] == "completed":
        if not score_path.is_file():
            status["status"] = "invalid-missing-score-output"
        else:
            try:
                report = json.loads(score_path.read_text())
                rows = report.get("group_bit_log_likelihoods")
                if not isinstance(rows, list) or not rows or any(not isinstance(x, list) or len(x) != 2 for x in rows):
                    status["status"] = "invalid-missing-full-group-scores"
                else:
                    status["score_sha256"] = sha256(score_path)
                    status["group_count"] = len(rows)
                    status["likelihoods"] = report.get("bit_log_likelihoods")
                    status["strict"] = report.get("strict")
                    status["relaxed"] = report.get("relaxed")
                    status["best_group_public_parameters"] = report.get("best_group_public_parameters")
                    status["numerical_refinement"] = report.get("numerical_refinement")
            except Exception as exc:
                status["status"] = "invalid-score-json"
                status["error"] = f"{type(exc).__name__}: {exc}"
    status_path.write_text(json.dumps(status, indent=2, sort_keys=True) + "\n")
    return status


def compare(reference: dict, candidate: dict, reference_path: Path, candidate_path: Path) -> dict:
    result = {"reference": reference["job"], "candidate": candidate["job"]}
    if reference.get("status") != "completed" or candidate.get("status") != "completed":
        result.update(status="not-comparable-job-failed", reference_status=reference.get("status"),
                      candidate_status=candidate.get("status"))
        return result
    a = json.loads(reference_path.read_text()); b = json.loads(candidate_path.read_text())
    av, bv = a["group_bit_log_likelihoods"], b["group_bit_log_likelihoods"]
    if len(av) != len(bv):
        result.update(status="not-comparable-group-count", reference_groups=len(av), candidate_groups=len(bv)); return result
    max_abs = max((abs(float(x)-float(y)) for ar,br in zip(av,bv) for x,y in zip(ar,br)), default=0.)
    max_rel = max((abs(float(x)-float(y))/max(abs(float(x)),1e-300)
                   for ar,br in zip(av,bv) for x,y in zip(ar,br)), default=0.)
    def winner(rows, bit): return max(range(len(rows)), key=lambda i: float(rows[i][bit]))
    group_winner_match = [winner(av,bit)==winner(bv,bit) for bit in (0,1)]
    top10_overlap = []
    for bit in (0,1):
        top_a=set(sorted(range(len(av)),key=lambda i:float(av[i][bit]),reverse=True)[:10])
        top_b=set(sorted(range(len(bv)),key=lambda i:float(bv[i][bit]),reverse=True)[:10])
        top10_overlap.append(len(top_a & top_b))
    gates = {}
    for name in ("strict", "relaxed"):
        ra, rb = a.get(name,{}), b.get(name,{})
        gates[name] = {"accepted_match":ra.get("accepted")==rb.get("accepted"),
                       "bit_match":ra.get("bit")==rb.get("bit"),
                       "reference":ra,"candidate":rb}
    mix_a=a.get("bit_log_likelihoods"); mix_b=b.get("bit_log_likelihoods")
    mix_abs=mix_rel=None
    if isinstance(mix_a,list) and isinstance(mix_b,list) and len(mix_a)==len(mix_b):
        mix_abs=max((abs(float(x)-float(y)) for x,y in zip(mix_a,mix_b)),default=0.)
        mix_rel=max((abs(float(x)-float(y))/max(abs(float(x)),1e-300) for x,y in zip(mix_a,mix_b)),default=0.)
    result.update(status="compared", group_count=len(av), max_abs_group_ll_difference=max_abs,
                  max_rel_group_ll_difference=max_rel, group_winner_index_match_by_bit=group_winner_match,
                  top10_group_rank_overlap_by_bit=top10_overlap,
                  max_abs_mixture_ll_difference=mix_abs,max_rel_mixture_ll_difference=mix_rel,
                  mixture_ll_reference=mix_a,mixture_ll_candidate=mix_b,gates=gates)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--receiver", type=Path, required=True, help="frozen executable triad_fft_acquire.py")
    ap.add_argument("--input", type=Path, required=True, help="wide-pilot sampled workspace directory")
    ap.add_argument("--out", type=Path, required=True, help="new empty output directory")
    ap.add_argument("--cwd", type=Path, help="project checkout used as CLI working directory; defaults to receiver parent")
    args = ap.parse_args()
    args.receiver = args.receiver.resolve(); args.input = args.input.resolve(); args.out = args.out.resolve()
    args.cwd = (args.cwd or args.receiver.parent.parent).resolve()
    if not args.receiver.is_file() or not args.input.is_dir(): raise SystemExit("receiver/input path missing")
    args.out.mkdir(parents=True, exist_ok=True)
    if any(args.out.iterdir()): raise SystemExit("output directory must be empty")
    closure = {}
    for name in SOURCE_CLOSURE:
        path = args.cwd / name
        closure[name] = sha256(path) if path.is_file() else None
    if any(value is None for value in closure.values()): raise SystemExit("source closure incomplete")
    metadata = {"schema":"triad-fft-cpu-gpu-comparison-v1", "claim_scope":"single-capture cold-start-inclusive CLI comparison; no resident throughput claim",
                "input_manifest_sha256":sha256(args.input/"manifest.json"),
                "input_files":{p.name:sha256(p) for p in sorted(args.input.iterdir()) if p.is_file()},
                "receiver_sha256":sha256(args.receiver), "source_closure_before":closure,
                "versions":versions(), "thread_environment":{"OMP_NUM_THREADS":"1","MKL_NUM_THREADS":"1","OPENBLAS_NUM_THREADS":"1","NUMEXPR_NUM_THREADS":"1"},
                "configuration":{"lag_max_seconds":14.07,"lag_step_samples":1,"frequency_offsets_hz":CFO_HZ,
                    "drift_offsets_hz_per_second":[0.0],"group_tile":512,"memory_mib":1024,
                    "retain_full_two_bit_group_scores":True,"jobs":[{"job":j,"device":d,"precision":p} for j,d,p in JOBS]}}
    (args.out/"campaign.json").write_text(json.dumps(metadata,indent=2,sort_keys=True)+"\n")
    deadline = time.monotonic()+CAMPAIGN_TIMEOUT_SECONDS
    statuses=[]
    for label,device,precision in JOBS:
        statuses.append(run_job(label,device,precision,args,args.out,deadline))
    after={name:(sha256(args.cwd/name) if (args.cwd/name).is_file() else None) for name in SOURCE_CLOSURE}
    comparisons=[]
    by_name={r["job"]:r for r in statuses}
    for candidate,reference in (("cpu-fp32-a","cpu-fp64-a"),("cpu-fp32-b","cpu-fp64-b"),
                                ("cuda-fp32-a","cpu-fp64-a"),("cuda-fp32-b","cpu-fp64-b")):
        comparisons.append(compare(by_name[reference],by_name[candidate],
                                   args.out/f"{reference}.scores.json",args.out/f"{candidate}.scores.json"))
    summary={"schema":"triad-fft-cpu-gpu-comparison-summary-v1","jobs":statuses,
             "source_closure_after":after,"source_unchanged_during_campaign":closure==after,
             "comparisons":comparisons,"campaign_wall_seconds":round(CAMPAIGN_TIMEOUT_SECONDS-(deadline-time.monotonic()),6),
             "all_jobs_completed":all(r.get("status")=="completed" for r in statuses)}
    (args.out/"summary.json").write_text(json.dumps(summary,indent=2,sort_keys=True)+"\n")
    print(json.dumps({"summary":str(args.out/"summary.json"),"jobs":[(r["job"],r["status"],r["wall_seconds"],r["peak_child_rss_bytes"]) for r in statuses],
                      "source_unchanged_during_campaign":summary["source_unchanged_during_campaign"]},indent=2))
    return 0 if summary["all_jobs_completed"] and summary["source_unchanged_during_campaign"] else 1

if __name__ == "__main__":
    raise SystemExit(main())
