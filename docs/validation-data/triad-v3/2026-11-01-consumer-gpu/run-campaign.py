#!/usr/bin/env python3
"""Run paired sampled consumer-GPU correlation comparisons.

The default screening pass processes the full declared capture bank on one
existing boundary input. CUDA timing copies are marked as timing replays. The
fresh input split is separately marked screening holdout and is too small to
establish operational admission-tail probabilities.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import time
import traceback


FORMATS = ("fp16", "bf16", "tf32", "fp8-e4m3", "fp8-e5m2", "int8")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_policy(policy: str) -> tuple[str, str | None]:
    if policy in ("fp64", "fp32", "native-fp4"):
        return policy, None
    if policy.startswith("tensor:"):
        fmt = policy.split(":", 1)[1]
        if fmt not in FORMATS:
            raise ValueError(f"unknown tensor policy {policy}; expected one of {FORMATS}")
        return "tensor", fmt
    raise ValueError(f"unsupported policy {policy!r}")


def capture_directory(root: Path, pattern: str) -> Path:
    matches = sorted(root.glob(pattern))
    valid = [path for path in matches if path.is_dir() and (path/"manifest.json").is_file()]
    if len(valid) != 1:
        raise ValueError(f"expected one capture for pattern {pattern!r}; found {len(valid)}")
    return valid[0].resolve()


def source_hashes(root: Path) -> dict[str, str]:
    names = ("triad-fp4-campaign.py", "triad_exhaustive_correlation.py",
             "triad_correlation_fusion.py", "triad_fp4_native.py",
             "triad_fp4_representation.py", "triad_tensor_native.py",
             "triad_phase_frequency.py", "triad_phase_reduction.py",
             "triad_phase_optimized.py", "triad-scan-workspace.py",
             "triad_scan_workspace.py")
    return {name: sha256(root/"tools"/name) for name in names if (root/"tools"/name).is_file()}


def load_campaign(root: Path):
    tool = root/"tools"/"triad-fp4-campaign.py"
    if not tool.is_file():
        raise FileNotFoundError(tool)
    sys.path.insert(0, str(tool.parent.resolve()))
    spec = importlib.util.spec_from_file_location("consumer_gpu_campaign_backend", tool)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def selected_inference(module, device: str, requested: str) -> str:
    if requested != "auto":
        return requested
    return module.default_inference_implementation(device)


def common_receiver_argv(capture: Path, out: Path, precision: str, fmt: str | None,
                         engine: str, device: str, phase_states: int,
                         repeats: int, *, representation_mode="both", inference="graph-fp64"):
    manifest = json.loads((capture/"manifest.json").read_text())
    fs = float(manifest["sample_rate"])
    argv = [str(capture), "--output", str(out), "--device", device,
            "--precision", precision, "--trim-zero-suffix",
            "--cfo-hz", "-0.01", "0", "0.01", "--lag-stop", "2",
            "--sample-rate", str(fs), "--block-samples", str(round(fs*4)),
            "--phase-states", str(phase_states), "--relative-phase-states", "8",
            "--residual-frequency-bound", "0.02", "--residual-frequency-std", "0.003",
            "--frequency-correlation", "1000", "--phase-diffusion", "0.00872665",
            "--inference-precision", "fp32", "--inference-implementation", inference,
            "--receiver-memory-mib", "8192", "--repeats", str(repeats), "--save-scores"]
    if fmt is not None:
        argv += ["--tensor-format", fmt, "--tensor-engine", engine]
    if precision == "native-fp4":
        argv += ["--native-format", "nvfp4"]
    if precision == "diagnostic-fp64":
        argv += ["--representation-mode", representation_mode]
    return argv


def result_metadata(record: dict, result: dict, policy: str, fmt: str | None,
                    capture: Path, copied_columns: int):
    manifest = json.loads((capture/"manifest.json").read_text())
    search = result.get("search", {})
    if result.get("input_hashes") is None:
        raise ValueError("campaign output omitted input hashes")
    record.update(status="completed", input_manifest_sha256=sha256(capture/"manifest.json"),
                  input_file_hashes=result["input_hashes"], sampled_noise_only=manifest.get("control") == "null",
                  physical_trial_count=1,
                  copied_columns_are_independent_trials=False,
                  declared_hypotheses=search.get("hypotheses_per_capture"),
                  no_pruning=search.get("no_pruning"),
                  threshold_decisions=result.get("decisions"),
                  precision=result.get("precision"), arithmetic_backend=result.get("arithmetic_backend"),
                  output_json=record["output"], output_npz=record["score_archive"],
                  timing_replays=int(copied_columns),
                  claim="one actual sampled input; correlations cover every public candidate, CFO center and lag; admission calibration not established")
    return record


def run_screen(args, module, capture, source, policies, started, records, index_path):
    inference = selected_inference(module, args.device, args.inference)
    for policy in policies:
        policy_started = time.perf_counter()
        if time.perf_counter()-started > args.max_seconds:
            raise TimeoutError("screen stage reached max-seconds between policies")
        precision, fmt = parse_policy(policy)
        output = args.output/f"{capture.name}-{policy.replace(':','-')}.json"
        argv = common_receiver_argv(capture, output, precision, fmt, args.engine,
                                    args.device, args.phase_states, args.repeats,
                                    inference=inference)
        argv += ["--tensor-packing", args.packing, "--tensor-accumulation",
                 "partial-fp32" if args.fp8_partial and fmt and fmt.startswith("fp8") else "native"]
        parsed = module.parse_args(argv)
        row = dict(stage="screen", status="running", policy=policy, format=fmt,
                   input=capture.name, argv=argv, source_hashes=source,
                   copies=1, physical_trials=1,
                   output=str(output), score_archive=str(output.with_suffix(".npz")),
                   admission_claim="uncalibrated; one screened observation")
        try:
            backend = module.make_backend(parsed)
            value = module.run(parsed, native_backend=backend)
            result_metadata(row, value, policy, fmt, capture, 1)
        except Exception as error:
            row.update(status="failed", error=str(error), traceback=traceback.format_exc())
            output.with_suffix(".failure.json").write_text(json.dumps(row, indent=2)+"\n")
        row["driver_call_wall_seconds"] = time.perf_counter()-policy_started
        records.append(row)
        write_index(index_path, args, source, records, "running", started)
        print(json.dumps({k: row[k] for k in ("stage", "input", "policy", "status", "driver_call_wall_seconds")}),
              flush=True)
        try:
            import torch
            if torch.cuda.is_available():
                torch.cuda.synchronize()
                torch.cuda.empty_cache()
        except Exception:
            pass


def run_diagnostic(args, module, capture, source, started, records, index_path):
    # All four products use CPU FP64 over the actual decoded packed operands;
    # they are deliberately excluded from throughput claims.
    inference = selected_inference(module, args.device, args.inference)
    for fmt in args.formats:
        if fmt not in FORMATS:
            raise ValueError(f"unknown format {fmt}")
        for representation in ("strict", "template-only", "observation-only", "both"):
            if time.perf_counter()-started > args.max_seconds:
                raise TimeoutError("diagnostic stage reached max-seconds between comparisons")
            output = args.output/f"{capture.name}-{fmt}-{representation}.json"
            argv = common_receiver_argv(capture, output, "diagnostic-fp64", fmt,
                    args.engine, args.device, args.phase_states, 1,
                    representation_mode=representation, inference=inference)
            argv += ["--tensor-packing", args.packing, "--tensor-accumulation",
                 "partial-fp32" if args.fp8_partial and fmt and fmt.startswith("fp8") else "native"]
            parsed = module.parse_args(argv)
            row = dict(stage="representation-diagnostic", status="running", format=fmt,
                       representation=representation, arithmetic="CPU FP64 represented products",
                       timing_claim="excluded", input=capture.name, argv=argv, source_hashes=source,
                       physical_trials=1, output=str(output),
                       score_archive=str(output.with_suffix(".npz")))
            try:
                backend = module.make_backend(parsed)
                value = module.run(parsed, native_backend=backend)
                result_metadata(row, value, fmt, fmt, capture, 1)
                row["timing_claim"] = "diagnostic CPU FP64 products; not native arithmetic throughput"
            except Exception as error:
                row.update(status="failed", error=str(error), traceback=traceback.format_exc())
                output.with_suffix(".failure.json").write_text(json.dumps(row, indent=2)+"\n")
            records.append(row)
            write_index(index_path, args, source, records, "running", started)
            print(json.dumps({k: row[k] for k in ("stage", "input", "format", "representation", "status")}), flush=True)


def run_performance(args, capture, source, policies, started, records, index_path):
    script = args.root/"tools"/"triad-scan-workspace.py"
    if not script.is_file():
        raise FileNotFoundError(script)
    for policy in policies:
        if time.perf_counter()-started > args.max_seconds:
            raise TimeoutError("performance stage reached max-seconds between policies")
        precision, fmt = parse_policy(policy)
        if precision == "native-fp4":
            precision_option = "native-fp4"
        else:
            precision_option = precision
        output = args.output/f"{capture.name}-{policy.replace(':','-')}.json"
        argv = [sys.executable, str(script), str(capture), "--output", str(output),
                "--device", args.device, "--precision", precision_option,
                "--fused-reconstruction", "--inference-precision", "fp32",
                "--inference-implementation", "graph-fp64", "--trim-zero-suffix",
                "--cfo-hz", "-0.01", "0", "0.01", "--lag-start", "0", "--lag-stop", "2",
                "--lag-step", "1", "--sample-rate", str(float(json.loads((capture/"manifest.json").read_text())["sample_rate"])),
                "--block-samples", str(round(float(json.loads((capture/"manifest.json").read_text())["sample_rate"])*4)),
                "--phase-states", str(args.phase_states), "--relative-phase-states", "8",
                "--residual-frequency-bound", "0.02", "--residual-frequency-std", "0.003",
                "--frequency-correlation", "1000", "--phase-diffusion", "0.00872665",
                "--attempt-spacing", "60", "--duration-alternatives", "1", "--model-alternatives", "1",
                "--receiver-memory-mib", "8192", "--template-cache-mib", "256",
                "--max-block-output-mib", "1024", "--gram-cache-mib", "16",
                "--repeats", "7", "--repeat-observations", "1", "--workspace-memory-mib", str(args.workspace_mib)]
        if fmt:
            argv += ["--tensor-format", fmt, "--tensor-engine", args.engine, "--tensor-packing", args.packing,
                     "--tensor-accumulation", "partial-fp32" if args.fp8_partial and fmt.startswith("fp8") else "native"]
        if precision == "native-fp4":
            argv += ["--native-format", "nvfp4"]
        row = dict(stage="performance", status="running", policy=policy,
                   input=capture.name, argv=argv, source_hashes=source,
                   physical_trials=1, seven_graph_replays_are_timing_only=True,
                   admission_claim="experimental arithmetic comparison; no false-tail calibration",
                   output=str(output))
        try:
            result = subprocess.run(argv, cwd=args.root, capture_output=True, text=True,
                                    timeout=min(args.child_timeout, max(1, args.max_seconds-(time.perf_counter()-started))),
                                    check=False)
            row.update(returncode=result.returncode, stdout_tail=result.stdout[-2000:],
                       stderr_tail=result.stderr[-4000:])
            if result.returncode:
                raise RuntimeError(f"workspace command exited {result.returncode}")
            data = json.loads(output.read_text())
            if len(data.get("timing_replays", [])) != 7:
                raise ValueError("workspace result must record seven replay timings")
            row.update(status="completed", result_schema=data.get("schema"),
                       input_hashes=data.get("input_hashes"), source_fingerprints=data.get("source_hashes"),
                       fixed_workspace_preparation_seconds=data.get("fixed_workspace_preparation_seconds"),
                       baseline_aba_timings=data.get("baseline_aba_timings"),
                       timing_replays=data.get("timing_replays"),
                       all_baseline_comparisons_within_2e_5=data.get("all_baseline_comparisons_within_2e_5"),
                       a_after_b_bit_identical=data.get("a_after_b_bit_identical"),
                       memory_estimate=data.get("memory_estimate"))
        except Exception as error:
            row.update(status="failed", error=str(error), traceback=traceback.format_exc())
            output.with_suffix(".failure.json").write_text(json.dumps(row, indent=2)+"\n")
        records.append(row)
        write_index(index_path, args, source, records, "running", started)
        print(json.dumps({k: row[k] for k in ("stage", "input", "policy", "status")}), flush=True)


def run_holdout(args, module, captures, source, policies, started, records, index_path):
    inference = selected_inference(module, args.device, args.inference)
    for capture in captures:
        for policy in policies:
            if time.perf_counter()-started > args.max_seconds:
                raise TimeoutError("holdout stage reached max-seconds between evaluations")
            precision, fmt = parse_policy(policy)
            output = args.output/f"{capture.name}-{policy.replace(':','-')}.json"
            argv = common_receiver_argv(capture, output, precision, fmt, args.engine,
                                        args.device, args.phase_states, 1,
                                        inference=inference)
            argv += ["--tensor-packing", args.packing, "--tensor-accumulation",
                 "partial-fp32" if args.fp8_partial and fmt and fmt.startswith("fp8") else "native"]
            parsed = module.parse_args(argv)
            holdout_manifest = json.loads((capture/"manifest.json").read_text())
            group = ("noise-only" if holdout_manifest.get("control") == "null" else
                     "target" if capture.name.startswith("target-") else "boundary")
            row = dict(stage="screening-holdout", status="running", policy=policy,
                       input=capture.name, stratum=group,
                       cn0_db_hz=holdout_manifest.get("cn0_db_hz"),
                       argv=argv, source_hashes=source,
                       input_manifest_sha256=sha256(capture/"manifest.json"),
                       physical_trials=1, output=str(output),
                       score_archive=str(output.with_suffix(".npz")),
                       independent_root_domains="key/channel/oscillator/noise/frontend",
                       admission_claim="screening only; 32 null inputs cannot calibrate daily noise-only admission")
            try:
                value = module.run(parsed, native_backend=module.make_backend(parsed))
                result_metadata(row, value, policy, fmt, capture, 1)
            except Exception as error:
                row.update(status="failed", error=str(error), traceback=traceback.format_exc())
                output.with_suffix(".failure.json").write_text(json.dumps(row, indent=2)+"\n")
            records.append(row)
            write_index(index_path, args, source, records, "running", started)
            print(json.dumps({k: row.get(k) for k in ("stage", "input", "policy", "status", "stratum")}), flush=True)
            try:
                import torch
                if torch.cuda.is_available():
                    torch.cuda.synchronize()
                    torch.cuda.empty_cache()
            except Exception:
                pass


def write_index(path: Path, args, source, records, status, started):
    completed = sum(row.get("status") == "completed" for row in records)
    failed = sum(row.get("status") == "failed" for row in records)
    path.write_text(json.dumps(dict(schema="consumer-gpu-campaign-index-v1",
        stage=args.stage, status=status, source_hashes=source,
        source_revision=subprocess.run(["git", "rev-parse", "HEAD"], cwd=args.root,
            capture_output=True, text=True, check=False).stdout.strip() or "unknown",
        input_root=str(args.inputs.resolve()), output_root=str(args.output.resolve()),
        device=args.device, packing=args.packing, fp8_partial=args.fp8_partial, formats=list(args.formats), policies=list(args.policies),
        phase_states=args.phase_states, max_seconds=args.max_seconds,
        completed=completed, failed=failed, record_count=len(records),
        elapsed_seconds=time.perf_counter()-started, records=records,
        independent_trials_note="Only different sampled input folders are physical trials. Timing replays and candidate columns are not additional trials.",
        calibration_status="No candidate policy calibrated. Screening holdout is not an operational false-admission qualification."), indent=2)+"\n")


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=Path("."))
    p.add_argument("--inputs", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--stage", choices=("screen", "diagnostic", "performance", "holdout"), required=True)
    p.add_argument("--pattern")
    p.add_argument("--formats", default=",".join(FORMATS))
    p.add_argument("--policies", default="fp64,fp32,tensor:fp16")
    p.add_argument("--engine", choices=("auto", "triton", "torch"), default="auto")
    p.add_argument("--packing", choices=("torch", "triton"), default="torch")
    p.add_argument("--fp8-partial", action="store_true", help="explicit K32 FP32 outer accumulation for FP8 formats only")
    p.add_argument("--device", default="cuda:0")
    p.add_argument("--inference", choices=("auto", "reference", "graph-fp64"), default="auto")
    p.add_argument("--phase-states", type=int, default=512)
    p.add_argument("--repeats", type=int, default=1)
    p.add_argument("--max-seconds", type=float, default=1200)
    p.add_argument("--child-timeout", type=int, default=900)
    p.add_argument("--workspace-mib", type=int, default=32768)
    args = p.parse_args(argv)
    args.root, args.inputs, args.output = args.root.resolve(), args.inputs.resolve(), args.output.resolve()
    if args.pattern is None:
        args.pattern = "*" if args.stage == "holdout" else "boundary-cn-10-r000"
    if args.output.exists():
        raise FileExistsError(f"output path already exists; use a new run directory: {args.output}")
    if not args.inputs.is_dir():
        raise FileNotFoundError(args.inputs)
    args.output.mkdir(parents=True)
    args.formats = [x for x in args.formats.split(",") if x]
    args.policies = [x for x in args.policies.split(",") if x]
    if args.stage == "screen":
        args.policies = list(dict.fromkeys(("fp64", "fp32", *(f"tensor:{x}" for x in args.formats))))
    elif args.stage == "diagnostic":
        args.policies = [f"tensor:{x}" for x in args.formats]
    module = load_campaign(args.root)
    source = source_hashes(args.root)
    started, records, index_path = time.perf_counter(), [], args.output/"index.json"
    write_index(index_path, args, source, records, "running", started)
    if args.stage == "holdout":
        captures = [path for path in sorted(args.inputs.glob(args.pattern))
                    if path.is_dir() and (path/"manifest.json").is_file()]
        if len(captures) != 128:
            raise ValueError(f"fresh screening holdout must contain all 128 planned cases; found {len(captures)}")
        run_holdout(args, module, captures, source, args.policies, started, records, index_path)
    else:
        capture = capture_directory(args.inputs, args.pattern)
        if args.stage == "screen":
            run_screen(args, module, capture, source, args.policies, started, records, index_path)
        elif args.stage == "diagnostic":
            run_diagnostic(args, module, capture, source, started, records, index_path)
        else:
            run_performance(args, capture, source, args.policies, started, records, index_path)
    final = "completed_with_failures" if any(r.get("status") == "failed" for r in records) else "complete"
    write_index(index_path, args, source, records, final, started)
    print(json.dumps(dict(index=str(index_path), status=final,
                          completed=sum(r.get("status") == "completed" for r in records),
                          failed=sum(r.get("status") == "failed" for r in records),
                          elapsed_seconds=time.perf_counter()-started), indent=2))
    if final != "complete":
        raise RuntimeError("one or more comparisons failed; inspect retained outputs before continuing")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
