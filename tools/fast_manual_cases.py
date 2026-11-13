#!/usr/bin/env python3
"""Run the editable Fast manual cases using the compiled native probe."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CASES = ROOT / "_tests_FastModem.md"
DEFAULT_PROBE = ROOT / "build" / "dev" / "fast_channel_probe"
DEFAULT_AUDIO_SECONDS = 75.0
DEFAULT_WALL_SECONDS = 90.0
CASE_FIELDS = {
    "Encryption", "Transmit Power", "Path loss", "Base Oscillator",
    "Shift Oscillator", "Doppler/delay spread",
    "Additional frequency selective amplitude modulation", "Margin",
    "Shift", "Channel profile",
}
OSCILLATORS = {
    "off": "off",
    "free-running crystal": "crystal",
    "gpsdo xo": "gpsdo-xo",
    "gpsdo tcxo": "gpsdo-tcxo",
    "gpsdo ocxo": "gpsdo-ocxo",
}
SHIFT_OSCILLATORS = {
    **OSCILLATORS,
    "baseband clock": "baseband-clock",
    "ic-7100 +/-0.5 ppm/radio": "ic-7100",
    "cb ssb / ats mini v4 ssb: +/-20 ppm/radio": "cb-ssb",
}
SPREAD = {
    "off": "none",
    "none": "none",
    "none — 0 hz 0 ms": "none",
    "sub9 best 0.001hz 0.005ms": "sub9-best",
    "sub9 moderate 0.005hz 0.05ms": "sub9-moderate",
    "sub9 worst 0.05hz 5ms": "sub9-worst",
    "hf best 0.1hz 0.5ms": "hf-best",
    "hf moderate 10hz 6ms": "hf-moderate",
    "hf worst 30hz 7ms": "hf-worst",
    "aurora best 10hz 3ms": "aurora-best",
    "aurora worst 55hz 11ms": "aurora-worst",
    "eme focused 6hz 0.5ms": "eme-focused",
}
PROFILES = {
    "ic-7100 ssb 2.4 khz": "ssb",
    "ic-7100 ssb · 2.4 khz": "ssb",
    "ic-7100 fm · voice band": "fm",
    "audio cable · qam / ldpc": "wire",
    "speakers / microphone": "acoustic",
    "speakers / mic · short": "acoustic-short",
}
PROFILE_OVERRIDES = (
    "expected_snr", "constellation", "code_rate", "depth", "symbol_rate",
    "pilot_spacing", "marker_spacing", "rolloff", "ldpc_bits",
)
RECEIVER_ASSUMPTION_FIELDS = (
    "base_oscillator", "shift_oscillator", "spread", "selective_slow_depth",
    "selective_fast_depth", "margin", "shift_hz",
)
CHANNEL_EFFECT_FIELDS = (
    "transmit_watts", "path_loss_db", "noise_dbm_hz", *RECEIVER_ASSUMPTION_FIELDS,
)


class CaseParseError(ValueError):
    pass


def _one_of(value: str, choices: dict[str, Any], field: str) -> Any:
    key = " ".join(value.casefold().split())
    if key in choices:
        return choices[key]
    if key in choices.values():
        return key
    raise CaseParseError(f"unsupported {field}: {value!r}")


def _units(value: str, choices: dict[str, float], low: float, high: float,
           field: str) -> float:
    parts = value.casefold().split()
    if len(parts) != 2 or parts[1] not in choices:
        raise CaseParseError(f"{field} requires an explicit unit: {value!r}")
    try:
        number = float(parts[0]) * choices[parts[1]]
    except ValueError as exc:
        raise CaseParseError(f"invalid {field}: {value!r}") from exc
    if not math.isfinite(number) or not low <= number <= high:
        raise CaseParseError(f"{field} is outside the supported range: {value!r}")
    return number


def _parse_depths(value: str) -> tuple[float, float]:
    if value.casefold() == "off":
        return 0.0, 0.0
    match = re.fullmatch(r"(each|slow|fast)\s*[-–—]\s*([0-9]{1,3})%", value.casefold())
    if not match or int(match[2]) > 100:
        raise CaseParseError(f"invalid selective amplitude modulation: {value!r}")
    depth = int(match[2]) / 100
    return (0.0 if match[1] == "fast" else depth, 0.0 if match[1] == "slow" else depth)


def _parse_block(lines: list[tuple[int, str]], block_number: int) -> dict[str, Any]:
    fields: dict[str, str] = {}
    for line_number, line in lines:
        if line.startswith("Channel profile "):
            field, value = "Channel profile", line[len("Channel profile "):].strip()
        else:
            parts = line.split("\t")
            if len(parts) != 2:
                raise CaseParseError(f"line {line_number}: expected exactly two tab-separated columns")
            value, field = (part.strip() for part in parts)
        if field in fields:
            raise CaseParseError(f"case {block_number}: duplicate field {field!r}")
        if field not in CASE_FIELDS and field != "Noise":
            raise CaseParseError(f"case {block_number}: unknown field {field!r}")
        fields[field] = value
    missing = CASE_FIELDS - fields.keys()
    if missing:
        raise CaseParseError(f"case {block_number}: missing fields {', '.join(sorted(missing))}")
    slow, fast = _parse_depths(fields["Additional frequency selective amplitude modulation"])
    margin = _one_of(fields["Margin"].replace("×", "x"), {"1x": 1, "2x": 2, "3x": 3}, "margin")
    # Approved reference noise density when the manual row has no Noise field.
    noise = fields.get("Noise", "-174 dBm/Hz")
    return {
        "index": block_number,
        "encryption": _one_of(fields["Encryption"], {"yes": True, "no": False}, "encryption"),
        "transmit_watts": _units(fields["Transmit Power"], {"mw": 1e-3, "w": 1, "kw": 1e3}, 1e-9, 1e9, "power"),
        "path_loss_db": _units(fields["Path loss"], {"db": 1}, 0, 300, "path loss"),
        "base_oscillator": _one_of(fields["Base Oscillator"], OSCILLATORS, "base oscillator"),
        "shift_oscillator": _one_of(fields["Shift Oscillator"], SHIFT_OSCILLATORS, "shift oscillator"),
        "spread": _one_of(fields["Doppler/delay spread"], SPREAD, "Doppler/delay spread"),
        "selective_slow_depth": slow,
        "selective_fast_depth": fast,
        "margin": margin,
        "shift_hz": _units(fields["Shift"], {"hz": 1, "khz": 1e3, "mhz": 1e6, "ghz": 1e9}, 0, 100e9, "shift"),
        "channel": _one_of(fields["Channel profile"], PROFILES, "channel profile"),
        "noise_dbm_hz": None if noise.casefold() == "off" else _units(noise, {"dbm/hz": 1}, -250, 0, "noise density"),
    }


def load_cases(path: Path = DEFAULT_CASES) -> list[dict[str, Any]]:
    """Validate every row and retain its order, including repeated rows."""
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        raise CaseParseError(f"cannot read case list {path}: {exc}") from exc
    blocks: list[list[tuple[int, str]]] = []
    current: list[tuple[int, str]] = []
    for line_number, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            if current:
                blocks.append(current)
                current = []
            continue
        current.append((line_number, line))
    if current:
        blocks.append(current)
    if not blocks:
        raise CaseParseError("expected at least one case")
    return [_parse_block(block, index) for index, block in enumerate(blocks, 1)]


def _selected(cases: list[dict[str, Any]], indices: list[int] | None) -> list[int]:
    selected = list(range(1, len(cases) + 1)) if indices is None else list(indices)
    if not selected or any(index < 1 or index > len(cases) for index in selected):
        raise CaseParseError(f"--case-index values must select rows 1..{len(cases)}")
    return selected


def _validate_limits(seed: int, source_bytes: int, audio_seconds: float,
                     wall_seconds: float, tx_gain: float, live: bool) -> None:
    if not 0 <= seed < 2**64:
        raise CaseParseError("--seed must be an unsigned 64-bit integer")
    if not 1 <= source_bytes <= 524288:
        raise CaseParseError("--source-bytes must be 1..524288")
    if not (math.isfinite(audio_seconds) and 0.05 <= audio_seconds <= 600
            and math.isfinite(wall_seconds) and 0.05 <= wall_seconds <= 900):
        raise CaseParseError("audio and wall bounds must be 0.05..600 and 0.05..900 seconds")
    if not math.isfinite(tx_gain) or not .0001 <= tx_gain <= 1.75:
        raise CaseParseError("--tx-gain must be 0.0001..1.75")
    if not live and tx_gain != 1.0:
        raise CaseParseError("--tx-gain applies only with --live")


def _validate_ldpc_trial(cases: list[dict[str, Any]], selected: list[int],
                         overrides: dict[str, Any], bootstrap_gate: bool,
                         bootstrap_only: bool, live: bool) -> None:
    ssb = all(cases[index - 1]["channel"] == "ssb" for index in selected)
    if "ldpc_bits" in overrides and (overrides["ldpc_bits"] not in (16200, 64800) or not ssb):
        raise CaseParseError("--ldpc-bits requires Fast SSB and either 16200 or 64800 bits")
    if bootstrap_gate and (not ssb or overrides.get("ldpc_bits", 16200) != 16200):
        raise CaseParseError("--bootstrap-gate requires the standard short Fast SSB profile")
    if bootstrap_only and (not bootstrap_gate or live):
        raise CaseParseError("--bootstrap-only requires an offline bootstrap-gated trial")


def _validate_retention(path: Path | None, output: Path | None, live: bool, selected: list[int]) -> None:
    if path is None:
        return
    if not live or len(selected) != 1:
        raise CaseParseError("--retain-capture requires one selected live case")
    if output is not None and path.resolve() == output.resolve():
        raise CaseParseError("capture PCM and the JSON report need distinct output paths")


def _native_args(case: dict[str, Any], seed: int, source_bytes: int,
                 max_audio_seconds: float, max_wall_seconds: float,
                 overrides: dict[str, Any] | None = None) -> list[str]:
    noise = case["noise_dbm_hz"]
    args = [
        "--channel", case["channel"],
        "--encryption", "yes" if case["encryption"] else "no",
        "--transmit-watts", str(case["transmit_watts"]),
        "--path-loss-db", str(case["path_loss_db"]),
        "--base-oscillator", case["base_oscillator"],
        "--shift-oscillator", case["shift_oscillator"],
        "--spread", case["spread"],
        "--selective-slow-depth", str(case["selective_slow_depth"]),
        "--selective-fast-depth", str(case["selective_fast_depth"]),
        "--margin", "1",
        "--shift-hz", str(case["shift_hz"]),
        "--noise-dbm-hz", str(noise) if noise is not None else "off",
        "--seed", str(seed),
        "--source-bytes", str(source_bytes),
        "--max-audio-seconds", str(max_audio_seconds),
        "--max-wall-seconds", str(max_wall_seconds),
    ]
    # Match GUI Search margin: it expands receiver bounds, not injected effects.
    # Other manual conditions initially describe both the receiver declaration
    # and generated channel, through their separate native inputs.
    for name in RECEIVER_ASSUMPTION_FIELDS:
        args.extend(("--assume-" + name.replace("_", "-"), str(case[name])))
    if overrides:
        if overrides.keys() - set(PROFILE_OVERRIDES):
            raise CaseParseError("unknown profile override")
        args.append("--profile-override")
        for name in PROFILE_OVERRIDES:
            if name in overrides:
                args.extend(("--" + name.replace("_", "-"), str(overrides[name])))
    return args


def _run_probe(args: list[str], timeout: float, encrypted: bool, live: bool) -> dict[str, Any]:
    try:
        completed = subprocess.run(args, check=False, capture_output=True, text=True,
                                   encoding="utf-8", errors="replace", timeout=timeout)
    except subprocess.TimeoutExpired:
        return {"status": "process_timeout", "returncode": None,
                "reason": f"native probe did not exit within {timeout:g} seconds"}
    except OSError as exc:
        return {"status": "probe_start_error", "returncode": None, "reason": str(exc)}
    try:
        report = json.loads(completed.stdout)
        if not isinstance(report, dict) or report.get("schema") != "fast_channel_probe.v1":
            raise ValueError("unexpected native result schema")
    except ValueError:
        report = {"status": "probe_error", "stdout": completed.stdout[-4000:],
                  "stderr": completed.stderr[-4000:]}
    report["returncode"] = completed.returncode
    if report.get("status") == "complete" and (
        completed.returncode != 0 or report.get("live") is not live
        or report.get("encrypted") is not encrypted
        or not all(report.get(flag) is True for flag in ("physical_complete", "complete", "exact_bytes"))
        or (encrypted and report.get("authenticated") is not True)
    ):
        report["native_status"] = report["status"]
        report["status"] = "invalid_result"
        report["reason"] = "the native completion receipt did not establish the requested transfer"
    return report


def _probe_sha256(path: Path) -> tuple[str | None, str | None]:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as binary:
            for chunk in iter(lambda: binary.read(1 << 20), b""):
                digest.update(chunk)
    except OSError as exc:
        return None, str(exc)
    return digest.hexdigest(), None


def run_cases(*, probe: Path = DEFAULT_PROBE, case_list: Path = DEFAULT_CASES,
              output: Path, seed: int = 417, source_bytes: int = 384,
              max_audio_seconds: float = DEFAULT_AUDIO_SECONDS, max_wall_seconds: float = DEFAULT_WALL_SECONDS,
              case_indices: list[int] | None = None, live_device: str | None = None,
              capture_device: str | None = None, playback_device: str | None = None,
              profile_overrides: dict[str, Any] | None = None, tx_gain: float = 1.0,
              bootstrap_gate: bool = False, bootstrap_only: bool = False,
              retain_capture: Path | None = None) -> dict[str, Any]:
    cases = load_cases(case_list)
    selected = _selected(cases, case_indices)
    if output.resolve() == case_list.resolve():
        raise CaseParseError("output must not overwrite the editable case list")
    if live_device is not None:
        if capture_device is not None or playback_device is not None:
            raise CaseParseError("choose one shared device or both endpoint devices")
        capture_device = playback_device = live_device
    live = capture_device is not None or playback_device is not None
    if live and not (capture_device and playback_device):
        raise CaseParseError("a live audio test requires both audio endpoints")
    _validate_limits(seed, source_bytes, max_audio_seconds, max_wall_seconds, tx_gain, live)
    _validate_ldpc_trial(cases, selected, profile_overrides or {}, bootstrap_gate,
                         bootstrap_only, live)
    _validate_retention(retain_capture, output, live, selected)
    probe_path = probe.resolve()
    probe_hash, probe_hash_error = _probe_sha256(probe_path)
    results = []
    for index in selected:
        case = cases[index - 1]
        args = [str(probe_path), *_native_args(case, seed, source_bytes, max_audio_seconds,
                                                max_wall_seconds, profile_overrides)]
        if bootstrap_gate:
            args.append("--bootstrap-gate")
        if bootstrap_only:
            args.append("--bootstrap-only")
        if live:
            args.extend(("--live", "--capture-device", capture_device, "--playback-device",
                         playback_device, "--tx-gain", str(tx_gain)))
        if retain_capture is not None:
            args.extend(("--retain-capture", str(retain_capture.resolve())))
        report = _run_probe(args, max_wall_seconds + 5, case["encryption"], live)
        expects_resilient = bootstrap_gate or (case["channel"] == "ssb" and not profile_overrides)
        if (expects_resilient and report.get("status") == "complete"
                and report.get("receiver_backend") != "resilient-ssb"):
            report["status"] = "receiver_mismatch"
            report["reason"] = "native probe did not report the requested resilient SSB receiver"
        if (bootstrap_gate and report.get("status") == "complete"
                and report.get("bootstrap_validated") is not True):
            report["status"] = "bootstrap_mismatch"
            report["reason"] = "native probe did not report a verified first coding cycle"
        if bootstrap_only and report.get("status") in ("complete", "bootstrap_verified"):
            if not (report.get("status") == "bootstrap_verified" and report.get("returncode") == 0
                    and report.get("receiver_backend") == "resilient-ssb"
                    and report.get("live") is False and report.get("encrypted") is case["encryption"]
                    and report.get("bootstrap_only") is True and report.get("bootstrap_validated") is True
                    and report.get("intervals") == 8 and report.get("ldpc_frames") == 1
                    and all(report.get(field) == 0 for field in ("ldpc_failed_frames", "bootstrap_nonfinite_llrs",
                                                               "capture_nonfinite_samples", "digital_nonfinite_samples"))
                    and all(report.get(field) is False for field in ("complete", "physical_complete"))):
                report["status"] = "bootstrap_mismatch"
                report["reason"] = "native bootstrap receipt did not establish the requested finite eight-interval trial"
        report["argv"] = args
        report["case_index"] = index
        report["requested_case"] = case
        report["requested_receiver_assumptions"] = {
            name: case[name] for name in RECEIVER_ASSUMPTION_FIELDS
        }
        report["requested_channel_effects"] = {
            name: case[name] for name in CHANNEL_EFFECT_FIELDS
        }
        report["requested_channel_effects"]["margin"] = 1
        if case["margin"] != 1:
            report["margin_semantics"] = (
                "Margin is receiver-only, matching GUI Search margin; injected effects use "
                "margin 1. Historical reports retain their recorded effect settings."
            )
        results.append(report)
    passes = [row.get("status") == "complete" for row in results]
    bootstraps = bootstrap_only and all(row.get("status") == "bootstrap_verified" for row in results)
    all_selected = set(selected) == set(range(1, len(cases) + 1))
    aggregate = {
        "schema": "fast_manual_cases.v1",
        "status": "bootstrap_verified" if bootstraps else (
            "complete" if all_selected and all(passes) else ("partial" if any(passes) else "incomplete")),
        "all_cases_selected": all_selected,
        "selected_cases_complete": all(passes),
        "cases_file": str(case_list),
        "probe_path": str(probe_path),
        "probe_sha256": probe_hash,
        "seed": seed,
        "source_bytes": source_bytes,
        "profile_overrides": profile_overrides or {},
        "bootstrap_gate": bootstrap_gate,
        "bootstrap_only": bootstrap_only,
        "selected_bootstraps_validated": bootstraps,
        "retain_capture": str(retain_capture.resolve()) if retain_capture is not None else None,
        "mode": "live_audio" if live else "simulation",
        "results": results,
    }
    if probe_hash_error is not None:
        aggregate["probe_sha256_error"] = probe_hash_error
    _write_report(output, aggregate)
    return aggregate


def _write_report(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(allow_abbrev=False,
        description="Run bounded Fast channel cases in the compiled native probe.")
    parser.add_argument("--case-list", type=Path, default=DEFAULT_CASES)
    parser.add_argument("--probe", type=Path, default=DEFAULT_PROBE)
    parser.add_argument("--output", type=Path,
                        help="aggregate JSON report (required unless --list is used)")
    parser.add_argument("--seed", type=int, default=417)
    parser.add_argument("--source-bytes", type=int, default=384)
    parser.add_argument("--max-audio-seconds", type=float, default=DEFAULT_AUDIO_SECONDS)
    parser.add_argument("--max-wall-seconds", type=float, default=DEFAULT_WALL_SECONDS)
    parser.add_argument("--case-index", type=int, action="append")
    parser.add_argument("--list", "--dry-run", action="store_true", dest="list_cases",
                        help="validate and print normalized cases without executing DSP")
    parser.add_argument("--profile-override", action="store_true",
                        help="explicitly allow the following non-default profile controls")
    parser.add_argument("--expected-snr", type=float)
    parser.add_argument("--constellation", type=int)
    parser.add_argument("--code-rate", choices=("1/2", "2/3", "3/4", "7/8", "7/9", "8/9", "9/10"))
    parser.add_argument("--depth", type=int)
    parser.add_argument("--symbol-rate", type=float)
    parser.add_argument("--pilot-spacing", type=int)
    parser.add_argument("--marker-spacing", type=int)
    parser.add_argument("--rolloff", type=float)
    parser.add_argument("--ldpc-bits", type=int, choices=(16200, 64800))
    parser.add_argument("--bootstrap-gate", action="store_true",
                        help="continue the short SSB trial only after a verified bootstrap")
    parser.add_argument("--bootstrap-only", action="store_true",
                        help="stop an offline gated trial after the first coding cycle")
    parser.add_argument("--live", action="store_true",
                        help="send the modeled waveform through a physical audio path (cable or speakers/microphone) with explicit capture/playback devices")
    parser.add_argument("--device", help="shared audio device ID for both endpoints")
    parser.add_argument("--capture-device")
    parser.add_argument("--playback-device")
    parser.add_argument("--tx-gain", type=float, default=1.0)
    parser.add_argument("--retain-capture", type=Path,
                        help="save up to 36 seconds of actual 48 kHz capture PCM to a new float32 LE file")
    args = parser.parse_args(argv)
    if args.device and (args.capture_device or args.playback_device):
        parser.error("choose --device or both --capture-device and --playback-device")
    capture_device = args.device or args.capture_device
    playback_device = args.device or args.playback_device
    if args.live and not (capture_device and playback_device):
        parser.error("--live requires explicit capture and playback devices")
    if (capture_device or playback_device) and not args.live:
        parser.error("audio endpoints require --live")
    overrides = {key: getattr(args, key) for key in PROFILE_OVERRIDES if getattr(args, key) is not None}
    if bool(overrides) != args.profile_override:
        parser.error("use --profile-override together with an explicit profile control")
    for key, value in overrides.items():
        if isinstance(value, float) and not math.isfinite(value):
            parser.error(f"--{key.replace('_', '-')} must be finite")
    if args.output is None and not args.list_cases:
        parser.error("--output is required unless --list is used")
    try:
        _validate_limits(args.seed, args.source_bytes, args.max_audio_seconds,
                         args.max_wall_seconds, args.tx_gain, args.live)
        if args.output is not None and args.output.resolve() == args.case_list.resolve():
            raise CaseParseError("output must not overwrite the editable case list")
        if args.list_cases:
            parsed = load_cases(args.case_list)
            selected = _selected(parsed, args.case_index)
            _validate_ldpc_trial(parsed, selected, overrides, args.bootstrap_gate,
                                 args.bootstrap_only, args.live)
            _validate_retention(args.retain_capture, args.output, args.live, selected)
            report = {
                "schema": "fast_manual_cases.v1", "status": "listed",
                "cases_file": str(args.case_list), "seed": args.seed,
                "source_bytes": args.source_bytes,
                "audio_cap_seconds_per_case": args.max_audio_seconds,
                "wall_cap_seconds_per_case": args.max_wall_seconds,
                "profile_overrides": overrides,
                "bootstrap_gate": args.bootstrap_gate,
                "bootstrap_only": args.bootstrap_only,
                "retain_capture": str(args.retain_capture.resolve()) if args.retain_capture is not None else None,
                "mode": "live_audio" if args.live else "simulation",
                "capture_device": capture_device, "playback_device": playback_device,
                "tx_gain": args.tx_gain if args.live else None,
                "selected_cases": [parsed[index - 1] for index in selected],
            }
            if args.output is not None:
                _write_report(args.output, report)
            print(json.dumps(report, indent=2, sort_keys=True))
            return 0
        report = run_cases(probe=args.probe, case_list=args.case_list, output=args.output,
                           seed=args.seed, source_bytes=args.source_bytes, case_indices=args.case_index,
                           max_audio_seconds=args.max_audio_seconds, max_wall_seconds=args.max_wall_seconds,
                           capture_device=capture_device, playback_device=playback_device,
                           profile_overrides=overrides or None, tx_gain=args.tx_gain,
                           bootstrap_gate=args.bootstrap_gate,
                           bootstrap_only=args.bootstrap_only,
                           retain_capture=args.retain_capture)
    except (CaseParseError, OSError) as exc:
        print(str(exc), file=sys.stderr)
        return 2
    return 0 if report["selected_cases_complete"] or report["selected_bootstraps_validated"] else 2


if __name__ == "__main__":
    sys.exit(main())
