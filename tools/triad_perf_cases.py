#!/usr/bin/env python3
"""Generate small, source-bound physical fixtures for triad FFT perf tests.

This adapter only prepares actual ``triad_long --mode export-sampled`` inputs.
It does not score receptions or make a native receiver throughput claim.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
MANIFEST_NAME = "fixture.json"
PUBLIC_FILES = ("manifest.json", "basis.c128", "observation.c128", "validity.u8")
CONFIG_PATH = Path(__file__).with_name("triad-performance-cases.json")
SOURCE_FILES = (
    "tools/triad-long.cpp",
    "src/triad_modem_v3.cpp",
    "src/triad_simulation_stream.cpp",
    "src/triad_gaussian_fading.hpp",
    "src/channel_perturbation.hpp",
    "src/channel_spread.hpp",
    "include/datapump/triad_modem_v3.hpp",
    "include/datapump/pattern_pulse.hpp",
    "include/datapump/clock_sync.hpp",
    "src/crypto.cpp",
    "include/datapump/crypto.hpp",
)
OPTIONAL_HISTORICAL_FILES = ("src/serpent.cpp", "include/datapump/serpent.hpp")
ABSENT_SOURCE = "absent"


class FixtureError(RuntimeError):
    """Invalid scenario, native output, workspace, or resource geometry."""


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def _source_hashes(source_root: Path, plan_path: Path, *, allow_historical_missing: bool = False) -> dict[str, str]:
    """Hash the selected candidate source tree plus the harness's input plan."""
    hashes = {relative: _sha256(source_root / relative) for relative in SOURCE_FILES}
    for relative in OPTIONAL_HISTORICAL_FILES:
        path = source_root / relative
        if not path.is_file() and allow_historical_missing and not path.exists():
            hashes[relative] = ABSENT_SOURCE
        else:
            hashes[relative] = _sha256(path)
    hashes["plan"] = _sha256(plan_path)
    hashes["case_config"] = _sha256(CONFIG_PATH)
    return hashes


def _json_hash(value: Any) -> str:
    data = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()
    return hashlib.sha256(data).hexdigest()


def _config() -> dict[str, Any]:
    return json.loads(CONFIG_PATH.read_text())


def scenarios(profile: str = "ci") -> list[dict[str, Any]]:
    """Return raw scenario specifications, without reading plans or running tools."""
    config = _config()
    if profile not in config["profiles"]:
        raise ValueError(f"unknown profile {profile!r}; choose {', '.join(config['profiles'])}")
    selected = set(config["profiles"][profile])
    return [dict(case) for case in config["cases"] if case["id"] in selected]


def _plan(case: dict[str, Any], config: dict[str, Any]) -> tuple[dict[str, Any], dict[str, Any]]:
    plan_path = (CONFIG_PATH.parent / config["source_plans"][case["source_plan"]]).resolve()
    plan = json.loads(plan_path.read_text())
    row = next((r for r in plan["rows"] if r.get("case", {}).get("name") == case["source_case"]), None)
    if row is None:
        raise FixtureError(f"{case['id']}: source case {case['source_case']!r} missing from {plan_path}")
    trial_index = int(case.get("source_trial", 0))
    trials = row.get("trials", [])
    if trial_index >= len(trials):
        raise FixtureError(f"{case['id']}: source trial {trial_index} unavailable")
    if row.get("status") != "eligible":
        raise FixtureError(f"{case['id']}: frozen source scenario is not eligible")
    return plan, {"row": row, "trial": trials[trial_index], "path": plan_path}


def _normalize_args(argv: list[str]) -> list[str]:
    """Canonicalize only output location; preserve every other native option."""
    result = list(argv)
    if "--output" in result:
        index = result.index("--output")
        if index + 1 >= len(result):
            raise FixtureError("native export argv has an empty --output")
        result[index + 1] = "{WORKSPACE}"
    return result


def _replace_option(argv: list[str], option: str, values: list[str]) -> list[str]:
    result = list(argv)
    if option in result:
        index = result.index(option)
        del result[index:index + 2]
    result.extend([option, *values])
    return result


def _native_argv(binary: Path, fixture_case: dict[str, Any], source: dict[str, Any], output: Path,
                 mode: str, limits: dict[str, Any]) -> list[str]:
    argv = list(source["trial"]["export_argv"])
    if not argv or argv[0] != "{BINARY}":
        raise FixtureError(f"{fixture_case['id']}: unsupported frozen native argv")
    argv[0] = str(binary)
    argv[argv.index("--mode") + 1] = mode
    argv = _replace_option(argv, "--memory-mib", [str(limits["memory_mib"])])
    argv = _replace_option(argv, "--export-max-mib", [str(limits["export_max_mib"])])
    argv = _replace_option(argv, "--export-max-samples", [str(limits["max_samples"])])
    if "--output" in argv:
        i = argv.index("--output")
        del argv[i:i + 2]
    if mode == "export-sampled":
        argv.extend(["--output", str(output)])
    for override in fixture_case.get("native_overrides", []):
        # Overrides are a sequence of complete option/value pairs. The broad
        # timing case replaces the physical prior AND its public lower anchor,
        # preserving waveform/channel arguments and the complete lag lattice.
        argv.append(override)
    return argv


def _invoke(argv: list[str], deadline: float | None, maximum: float) -> subprocess.CompletedProcess[str]:
    timeout = maximum
    if deadline is not None:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("fixture preparation deadline exhausted")
        timeout = min(timeout, remaining)
    try:
        return subprocess.run(argv, text=True, capture_output=True, timeout=timeout, check=False)
    except subprocess.TimeoutExpired as exc:
        raise TimeoutError(f"native fixture command exceeded {timeout:.2f}s") from exc


def _configure(binary: Path, fixture_case: dict[str, Any], source: dict[str, Any],
               limits: dict[str, Any], deadline: float | None) -> dict[str, Any]:
    argv = _native_argv(binary, fixture_case, source, Path("unused"), "configure", limits)
    proc = _invoke(argv, deadline, float(limits["subprocess_timeout_seconds"]))
    if proc.returncode:
        raise FixtureError(f"{fixture_case['id']}: configure exited {proc.returncode}: {proc.stderr[-1200:]}")
    try:
        result = json.loads(proc.stdout)
        config = result["configuration"]
    except (ValueError, KeyError, TypeError) as exc:
        raise FixtureError(f"{fixture_case['id']}: native configure did not return configuration JSON") from exc
    return config


def _frequency_grid(config: dict[str, Any], sample_count: int, sample_rate: float,
                    limits: dict[str, Any]) -> list[float]:
    """Cover the native deterministic CFO bound with a first-lobe loss budget."""
    bound = float(config["deterministic_oscillator_cfo_bound_hz"])
    duration = float(config["duration"]["elapsed_source_calendar_seconds"])
    if not math.isfinite(bound) or bound < 0 or not math.isfinite(duration) or duration <= 0:
        raise FixtureError("native configure returned invalid CFO or duration bound")
    loss = float(limits["frequency_loss_db"])
    target = 10.0 ** (-loss / 10.0)
    low, high = 0.0, 0.5
    for _ in range(64):
        x = (low + high) / 2
        power = 1.0 if x == 0 else (math.sin(math.pi * x) / (math.pi * x)) ** 2
        if power >= target:
            low = x
        else:
            high = x
    step = 2.0 * low / duration
    if bound == 0:
        return [0.0]
    count_intervals = math.ceil((2.0 * bound) / step)
    count = count_intervals + 1
    if count > int(limits["max_frequency_offsets"]):
        raise FixtureError(
            f"planned CFO geometry needs {count} offsets for +/-{bound:g} Hz at {loss:g} dB loss; "
            f"limit is {limits['max_frequency_offsets']}; refusing to shrink the declared bank")
    return [-bound + (2.0 * bound * i / count_intervals) for i in range(count)]


def _workspace_hashes(directory: Path) -> dict[str, str]:
    hashes = {}
    for name in PUBLIC_FILES:
        path = directory / name
        if not path.is_file():
            raise FixtureError(f"native sampled workspace missing {name}")
        hashes[name] = _sha256(path)
    manifest = json.loads((directory / "manifest.json").read_text())
    expected = {
        "basis.c128": manifest.get("file_sizes", {}).get("basis.c128"),
        "observation.c128": manifest.get("file_sizes", {}).get("observation.c128"),
        "validity.u8": manifest.get("file_sizes", {}).get("validity.u8"),
    }
    for name, size in expected.items():
        if size is None or (directory / name).stat().st_size != size:
            raise FixtureError(f"native manifest/file size mismatch for {name}")
    if manifest.get("format") != "triad-sampled-waveforms-v1":
        raise FixtureError("native export returned unsupported sampled workspace format")
    if manifest.get("channel") is None or manifest.get("channel_samples") != manifest.get("sample_count"):
        raise FixtureError("sampled workspace omitted channel provenance or full channel extent")
    return hashes


def _valid_cache(directory: Path, key: str, source_hashes: dict[str, str]) -> dict[str, str] | None:
    receipt_path = directory / MANIFEST_NAME
    try:
        receipt = json.loads(receipt_path.read_text())
        if receipt.get("cache_key") != key or receipt.get("source_hashes") != source_hashes:
            return None
        if receipt.get("fixture_hashes") != _workspace_hashes(directory / "workspace"):
            return None
        return dict(receipt["fixture_hashes"])
    except (OSError, ValueError, KeyError, FixtureError, TypeError):
        return None


def prepare_cases(binary: str | os.PathLike[str], output_root: str | os.PathLike[str], *,
                  profile: str = "ci", deadline: float | None = None,
                  source_root: str | os.PathLike[str] | None = None,
                  allow_historical_missing: bool = False,
                  case_ids: list[str] | None = None) -> list[dict[str, Any]]:
    """Create/reuse native sampled fixtures for the selected performance profile.

    ``deadline`` is an absolute ``time.monotonic()`` timestamp. ``source_root``
    selects the candidate implementation tree whose source hashes accompany
    the fixture; native binary and frozen input plans remain independently
    bound. Set ``allow_historical_missing`` only for a historical source tree
    predating Serpent; its absence is explicitly recorded in the identity. The
    returned dictionaries use the schema consumed by the performance
    receiver adapter.
    Native configure and export subprocesses are each limited to 60 seconds and
    to the remaining deadline. Input files are immutable after publication.
    """
    config = _config()
    selected_cases = scenarios(profile)
    if case_ids is not None:
        requested = set(case_ids)
        unknown = requested - {case["id"] for case in selected_cases}
        if unknown:
            raise FixtureError("unknown selected fixture cases: " + ", ".join(sorted(unknown)))
        selected_cases = [case for case in selected_cases if case["id"] in requested]
    native = Path(binary).resolve()
    if not native.is_file() or not os.access(native, os.X_OK):
        raise FixtureError(f"triad_long binary is not executable: {native}")
    output = Path(output_root).resolve()
    output.mkdir(parents=True, exist_ok=True)
    provenance_root = Path(source_root).resolve() if source_root is not None else ROOT
    binary_hash = _sha256(native)
    result: list[dict[str, Any]] = []

    for fixture_case in selected_cases:
        plan, selected = _plan(fixture_case, config)
        export_argv = _native_argv(native, fixture_case, selected, Path("{WORKSPACE}"), "export-sampled", config["limits"])
        normalized_args = _normalize_args(export_argv)
        source_hashes = _source_hashes(provenance_root, selected["path"],
                                       allow_historical_missing=allow_historical_missing)
        identity = {
            "schema": config["schema"],
            "binary_sha256": binary_hash,
            "native_args": normalized_args,
            "source_hashes": source_hashes,
            "source_case": selected["row"]["case"],
            "fixture_id": fixture_case["id"],
        }
        key = _json_hash(identity)
        case_root = output / "triad-physical-fixtures" / fixture_case["id"]
        cache_dir = case_root / key
        hashes = _valid_cache(cache_dir, key, source_hashes) if cache_dir.is_dir() else None
        cache_hit = hashes is not None
        configuration = _configure(native, fixture_case, selected, config["limits"], deadline)
        manifest = None
        if hashes is not None:
            manifest = json.loads((cache_dir / "workspace" / "manifest.json").read_text())
        else:
            case_root.mkdir(parents=True, exist_ok=True)
            staging = Path(tempfile.mkdtemp(prefix=f".{key[:12]}-", dir=case_root))
            workspace = staging / "workspace"
            argv = _native_argv(native, fixture_case, selected, workspace, "export-sampled", config["limits"])
            try:
                proc = _invoke(argv, deadline, float(config["limits"]["subprocess_timeout_seconds"]))
                if proc.returncode:
                    raise FixtureError(f"{fixture_case['id']}: export exited {proc.returncode}: {proc.stderr[-1600:]}")
                hashes = _workspace_hashes(workspace)
                manifest = json.loads((workspace / "manifest.json").read_text())
                receipt = {
                    "schema": "triad-physical-fixture-cache-v1",
                    "cache_key": key,
                    "identity": identity,
                    "source_hashes": source_hashes,
                    "fixture_hashes": hashes,
                    "native_configure": configuration,
                    "native_stdout_tail": proc.stdout[-2000:],
                    "native_stderr_tail": proc.stderr[-2000:],
                }
                (staging / MANIFEST_NAME).write_text(json.dumps(receipt, sort_keys=True, indent=2) + "\n")
                if cache_dir.exists():
                    shutil.rmtree(cache_dir)
                os.replace(staging, cache_dir)
            except BaseException:
                shutil.rmtree(staging, ignore_errors=True)
                raise

        assert hashes is not None and manifest is not None
        sample_count = int(manifest["sample_count"])
        sample_rate = float(manifest["sample_rate"])
        if sample_count > int(config["limits"]["max_samples"]):
            raise FixtureError(f"{fixture_case['id']}: native waveform has {sample_count} samples, over fixture cap")
        bounds = configuration["initial_arrival_bounds_seconds"]
        lag_max = float(bounds[1]) - float(bounds[0])
        # Match the real CLI/adapter long-double endpoint, including fractional
        # samples: no floor or double-rounding may shorten the public lattice.
        import numpy as np
        lag_groups = int(np.ceil(np.longdouble(lag_max) * np.longdouble(sample_rate))) + 1
        if lag_groups > int(config["limits"]["max_groups"]):
            raise FixtureError(f"{fixture_case['id']}: complete lag lattice needs {lag_groups} groups, over cap")
        frequency_offsets = _frequency_grid(configuration, sample_count, sample_rate, config["limits"])
        f_count = len(frequency_offsets)
        max_training = min(f_count, max(3, math.ceil(f_count / 8)))
        duration = float(configuration["duration"]["elapsed_source_calendar_seconds"])
        result.append({
            "id": fixture_case["id"],
            "adapter": "fft",
            "input_directory": str(cache_dir / "workspace"),
            "lag_max_seconds": lag_max,
            "lag_step_samples": 1,
            "frequency_offsets_hz": frequency_offsets,
            "training_counts": fixture_case.get("training_counts", [1, max_training]),
            "holdout_count": fixture_case.get("holdout_count", min(f_count, 2 * max_training + 1)),
            "full_holdout_first_repeat": fixture_case.get("full_holdout_first_repeat", False),
            "group_tile": fixture_case.get("group_tile", 512),
            "memory_mib": int(config["limits"]["memory_mib"]),
            "configuration": {
                "source_case": selected["row"]["case"],
                "source_plan_sha256": source_hashes["plan"],
                "source_plan_identity_current": False,
                "native_configure": configuration,
                "sample_count": sample_count,
                "sample_rate_hz": sample_rate,
                "source_elapsed_seconds": duration,
                "lag_group_count": lag_groups,
                "frequency_grid_spacing_hz_max": max((b - a for a, b in zip(frequency_offsets, frequency_offsets[1:])), default=0.0),
                "fixture_generation": "native sampled point model; continuous path/channel state and full natural tails retained",
            },
            "fixture_hashes": hashes,
            "fixture_scope": fixture_case["scope"],
            "source_bound_metadata": {
                "binary_sha256": binary_hash,
                "source_hashes": source_hashes,
                "source_root": str(provenance_root),
                "cache_key": key,
                "cache_manifest": str(cache_dir / MANIFEST_NAME),
                "cache_hit": cache_hit,
                "four_public_file_hashes_revalidated": True,
                "configuration_source": "fresh triad_long --mode configure for current binary/arguments",
                "source_plan_receipt_reused_as_current": False,
                "channel_manifest": manifest.get("channel"),
                "oscillator_model": manifest.get("oscillator_model"),
            },
        })
    return result


def _main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="print scenario specs for --profile")
    parser.add_argument("--profile", choices=tuple(_config()["profiles"]), default="ci")
    parser.add_argument("--binary", type=Path, default=ROOT / "build/dev-cli/triad_long")
    parser.add_argument("--output-root", type=Path, default=ROOT / "build/triad-perf")
    parser.add_argument("--deadline-seconds", type=float, default=None,
                        help="relative wall-time budget; subprocesses still cap at 60 seconds")
    args = parser.parse_args(argv)
    if args.list:
        print(json.dumps(scenarios(args.profile), indent=2))
        return 0
    deadline = None if args.deadline_seconds is None else time.monotonic() + args.deadline_seconds
    print(json.dumps(prepare_cases(args.binary, args.output_root, profile=args.profile, deadline=deadline), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
