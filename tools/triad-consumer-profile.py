#!/usr/bin/env python3
"""Profile one completed consumer-GPU campaign policy on its recorded input.

This wrapper preserves the campaign receiver arguments while enabling CPU/CUDA
profiler instrumentation. Its measurements are diagnostic and not speed claims.
"""
from __future__ import annotations

import argparse
import cProfile
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import pstats
import shutil
import sys
import time
from typing import Any

try:
    import torch
except Exception as error:  # pragma: no cover - depends on installed CUDA environment
    torch = None
    _TORCH_IMPORT_ERROR = f"{type(error).__name__}: {error}"
else:
    _TORCH_IMPORT_ERROR = None


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


_TRACE_ID_KEYS = {"uuid", "gpuuuid", "deviceuuid", "hostname", "host_name",
                  "hosthostname", "machineid", "serial", "serialnumber",
                  "pcibusid", "pcibus", "providerid"}


def sanitize_trace_metadata(value: Any) -> Any:
    """Remove host/device identity metadata while retaining every trace event."""
    if isinstance(value, dict):
        clean = {}
        for key, item in value.items():
            normalized = "".join(character.lower() for character in str(key)
                                 if character.isalnum())
            if normalized in _TRACE_ID_KEYS or normalized.endswith("uuid") or normalized.endswith("hostname"):
                continue
            clean[key] = sanitize_trace_metadata(item)
        return clean
    if isinstance(value, list):
        return [sanitize_trace_metadata(item) for item in value]
    return value


def relative_to_root(path: Path, root: Path, what: str) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(root.resolve()).as_posix()
    except ValueError as error:
        raise ValueError(f"{what} must be inside --root; external paths are not recorded") from error


def load_campaign(root: Path):
    tool = root / "tools" / "triad-fp4-campaign.py"
    if not tool.is_file():
        raise FileNotFoundError(tool)
    sys.path.insert(0, str(tool.parent.resolve()))
    spec = importlib.util.spec_from_file_location("triad_consumer_profile_campaign", tool)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not import {tool.name}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module, tool


def select_record(index_path: Path, policy: str) -> tuple[dict[str, Any], dict[str, Any]]:
    data = json.loads(index_path.read_text())
    rows = [row for row in data.get("records", [])
            if row.get("stage") == "screen" and row.get("policy") == policy
            and row.get("status") == "completed" and isinstance(row.get("argv"), list)]
    if len(rows) != 1:
        raise ValueError(f"expected one completed screen record for {policy!r}; found {len(rows)}")
    return data, rows[0]


def rewrite_argv(original: list[str], output: Path, packing: str | None) -> list[str]:
    argv = list(original)
    if not argv:
        raise ValueError("screen index record has empty argv")
    result: list[str] = []
    index = 0
    had_output = False
    had_repeats = False
    had_instrument = False
    had_packing = False
    while index < len(argv):
        item = argv[index]
        if item == "--output":
            if index + 1 >= len(argv):
                raise ValueError("screen argv has --output without a value")
            result.extend((item, str(output)))
            had_output = True
            index += 2
        elif item == "--repeats":
            if index + 1 >= len(argv):
                raise ValueError("screen argv has --repeats without a value")
            result.extend((item, "2"))
            had_repeats = True
            index += 2
        elif item == "--instrumented-timing":
            had_instrument = True
            result.append(item)
            index += 1
        elif item == "--tensor-packing":
            if index + 1 >= len(argv):
                raise ValueError("screen argv has --tensor-packing without a value")
            result.extend((item, packing if packing else argv[index + 1]))
            had_packing = True
            index += 2
        else:
            result.append(item)
            index += 1
    if not had_output:
        raise ValueError("screen argv has no --output")
    if not had_repeats:
        result.extend(("--repeats", "2"))
    if not had_instrument:
        result.append("--instrumented-timing")
    if packing is not None and not had_packing:
        result.extend(("--tensor-packing", packing))
    return result


def profiler_summary(prof: Any) -> str:
    sections = ["PyTorch profiler averages (instrumentation enabled; top 40 rows per sort)"]
    for title, key in (("Top by self CUDA time", "self_cuda_time_total"),
                       ("Top by self CPU time", "self_cpu_time_total")):
        sections.extend(("", title, prof.key_averages().table(sort_by=key, row_limit=40)))
    return "\n".join(sections) + "\n"


def dump_cprofile(profile: cProfile.Profile, path: Path, root: Path) -> str:
    stats = pstats.Stats(profile)
    stats.strip_dirs().dump_stats(str(path))
    rendered = pstats.Stats(profile).strip_dirs()
    import io
    stream = io.StringIO()
    rendered.stream = stream
    rendered.sort_stats("cumulative").print_stats(40)
    return stream.getvalue()


def run(args: argparse.Namespace) -> dict[str, Any]:
    root = args.root.resolve()
    index_path = args.index.resolve()
    output_dir = args.output_dir.resolve()
    # Indexes may live in a separate temporary result directory. Retain only
    # their basename and content hash; do not serialize an external path.
    index_name = index_path.name
    index_digest = sha256(index_path) if index_path.is_file() else None
    output_rel = relative_to_root(output_dir, root, "output directory")
    if output_dir.exists():
        raise FileExistsError(f"output directory must be new: {output_rel}")
    if not index_path.is_file():
        raise FileNotFoundError(index_path)
    index_data, record = select_record(index_path, args.policy)
    module, campaign_path = load_campaign(root)
    source_record = Path(record["argv"][0])
    if not source_record.is_absolute():
        source_record = (root / source_record).resolve()
    input_rel = relative_to_root(source_record, root, "capture input")
    output_dir.mkdir(parents=True)
    campaign_output = output_dir / "campaign-profiled.json"
    argv = rewrite_argv(record["argv"], campaign_output, args.tensor_packing)
    # Keep replay and artifact paths under the selected repository root and
    # record them relative to that root in the JSON artifacts.
    argv[0] = str(source_record)
    if not torch.cuda.is_available():
        raise RuntimeError("CUDA is required for the requested CPU+CUDA profiler trace")
    parsed = module.parse_args(argv)
    if not str(parsed.device).startswith("cuda"):
        raise ValueError("selected screen record does not target a CUDA device")
    if args.tensor_packing and not str(record["policy"]).startswith("tensor:"):
        raise ValueError("--tensor-packing override applies only to tensor policies")
    backend = module.make_backend(parsed)
    activities = [torch.profiler.ProfilerActivity.CPU, torch.profiler.ProfilerActivity.CUDA]
    profiler = torch.profiler.profile(activities=activities, record_shapes=True,
                                      profile_memory=False)
    cpu_profile = cProfile.Profile()
    started = time.perf_counter()
    cpu_profile.enable()
    with profiler:
        result = module.run(parsed, native_backend=backend)
    cpu_profile.disable()
    instrumented_wall = time.perf_counter() - started

    trace_json = output_dir / "torch-trace.json"
    profiler.export_chrome_trace(str(trace_json))
    raw_trace = json.loads(trace_json.read_text())
    clean_trace = sanitize_trace_metadata(raw_trace)
    trace_json.write_text(json.dumps(clean_trace, separators=(",", ":")) + "\n")
    trace_gz = output_dir / "torch-trace.json.gz"
    with trace_json.open("rb") as source, gzip.open(trace_gz, "wb", compresslevel=6) as dest:
        shutil.copyfileobj(source, dest)
    trace_json.unlink()
    pstats_path = output_dir / "cpu.pstats"
    cpu_top = dump_cprofile(cpu_profile, pstats_path, root)
    (output_dir / "cpu-top40.txt").write_text(cpu_top)
    (output_dir / "torch-top40.txt").write_text(profiler_summary(profiler))

    source_hashes = dict(result.get("source_hashes", {}))
    source_hashes["triad-consumer-profile.py"] = sha256(Path(__file__).resolve())
    source_hashes["triad-fp4-campaign.py"] = sha256(campaign_path)
    input_hashes = result.get("input_hashes", {})
    output = {
        "schema": "triad-consumer-profile-v1",
        "status": "complete",
        "selected_screen_record": {"index_basename": index_name,
            "index_sha256": index_digest,
            "index_source_revision": index_data.get("source_revision"),
            "policy": args.policy, "input": input_rel,
            "input_manifest_sha256": record.get("input_manifest_sha256"),
            "source_hashes_at_screen_time": record.get("source_hashes"),
            "screen_argv": record["argv"]},
        "profile_argv": [Path(argv[0]).relative_to(root).as_posix(), *argv[1:]],
        "root": ".",
        "output_directory": output_rel,
        "instrumented_campaign_output": campaign_output.relative_to(root).as_posix(),
        "source_hashes": source_hashes,
        "input_hashes": input_hashes,
        "profile_artifacts": {"chrome_trace_gzip": "torch-trace.json.gz",
            "cpu_profile": "cpu.pstats", "cpu_top40": "cpu-top40.txt",
            "torch_top40": "torch-top40.txt"},
        "instrumentation": {"torch_profiler_activities": ["CPU", "CUDA"],
            "record_shapes": True, "profile_memory": False,
            "cprofile": True, "repeats": 2,
            "instrumented_wall_seconds": instrumented_wall,
            "instrumented_timing_flag": True},
        "result_summary": {"timing": result.get("timing"),
            "search": result.get("search"), "precision": result.get("precision"),
            "arithmetic_backend": result.get("arithmetic_backend"),
            "campaign_output_sha256": sha256(campaign_output),
            "campaign_output": campaign_output.relative_to(root).as_posix()},
        "external_profilers": {"nsys_executable_available": shutil.which("nsys") is not None,
            "ncu_executable_available": shutil.which("ncu") is not None,
            "launched_by_this_probe": False,
            "external_traces_or_errors_must_be_retained_separately": True},
        "trace_sanitization": {"removed_metadata_keys": sorted(_TRACE_ID_KEYS),
            "trace_events_retained": len(clean_trace.get("traceEvents", [])),
            "identity_metadata_removed_recursively": True},
        "limitations": [
            "Torch and cProfile instrumentation changes execution time; none of these timings are throughput benchmarks.",
            "No Nsight Systems or Nsight Compute run was launched by this wrapper.",
            "No measured hardware-counter, utilization, memory-traffic, occupancy, or achieved-throughput claim is made.",
            "Any nsys/ncu traces or errors belong in separate artifacts and are not merged into this profile record.",
        ],
    }
    (output_dir / "profile.json").write_text(json.dumps(output, indent=2, sort_keys=True) + "\n")
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd(), help="repository root")
    parser.add_argument("--index", type=Path, required=True,
                        help="completed screen index JSON from run-campaign.py")
    parser.add_argument("--policy", choices=("fp32", "tensor:fp16"), required=True)
    parser.add_argument("--output-dir", type=Path, required=True,
                        help="new output directory inside --root")
    parser.add_argument("--tensor-packing", choices=("torch", "triton"),
                        help="optional override for tensor:fp16 only")
    args = parser.parse_args()
    if torch is None:
        parser.error(f"PyTorch is required to profile a campaign: {_TORCH_IMPORT_ERROR}")
    result = run(args)
    print(json.dumps({"status": result["status"],
                      "profile": str(args.output_dir.resolve() / "profile.json"),
                      "policy": args.policy}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
