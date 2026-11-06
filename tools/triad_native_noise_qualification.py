#!/usr/bin/env python3
"""Four short native-capture loopback measurements of full-rate DAC noise.

This is a device test, not an RF measurement. It retains source/capture PCM,
native metadata, command output and a compact PSD comparison. It never changes
audio mixer settings. Invoke only with explicit authorization for the named
exclusive ALSA endpoints.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

import numpy as np
from scipy import signal


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def measure(path: Path, binary: Path, root: Path, name: str, flags: list[str],
            expected_variance: float, fs: int, source_seconds: int, device: str) -> dict:
    output = root / f"{name}-capture.f32"
    raw_output = root / f"{name}-provider.f32"
    metadata_path = root / f"{name}-capture.json"
    command = [str(binary), "--live", "--exclusive", "--input", str(path),
               "--output", str(output), "--raw-output", str(raw_output),
               "--metadata", str(metadata_path), "--rate", str(fs),
               "--capture-device", device, "--playback-device", device,
               "--preroll-seconds", ".75", "--postroll-seconds", "1",
               "--max-seconds", "30", *flags]
    (root / f"{name}-command.json").write_text(json.dumps(command, indent=2) + "\n")
    began = time.perf_counter()
    try:
        result = subprocess.run(command, capture_output=True, text=True, check=False, timeout=35)
    except subprocess.TimeoutExpired as exc:
        (root / f"{name}-stdout.txt").write_text(exc.stdout or "")
        (root / f"{name}-stderr.txt").write_text(exc.stderr or "")
        raise RuntimeError(f"native cable capture {name} exceeded 35-second timeout") from exc
    elapsed = time.perf_counter() - began
    (root / f"{name}-stdout.txt").write_text(result.stdout)
    (root / f"{name}-stderr.txt").write_text(result.stderr)
    if result.returncode != 0:
        raise RuntimeError(f"native cable capture {name} failed ({result.returncode}); see retained stderr")
    metadata = json.loads(metadata_path.read_text())
    if metadata.get("schema") != "triad-audio-capture-v1" or metadata.get("status") != "complete":
        raise RuntimeError(f"native cable capture {name} returned incomplete metadata")
    if metadata.get("discontinuity", {}).get("capture") or metadata.get("discontinuity", {}).get("playback"):
        raise RuntimeError(f"native cable capture {name} reported a discontinuity")

    # `call_start_capture_sample` precedes the play() call. Select an interior
    # three-second slice after queue startup and before output drain/postroll.
    capture = np.fromfile(output, dtype="<f4").astype(np.float64)
    capmeta = metadata["capture"]
    playback = metadata["playback"]
    call_start = int(playback["call_start_capture_sample"])
    call_return = int(playback["call_return_capture_sample"])
    begin = call_start + 2 * fs
    end = min(call_start + 5 * fs, call_return - fs, len(capture))
    if end - begin < 2 * fs:
        raise RuntimeError(f"native capture {name} has too little marker-aligned interior audio")
    x = capture[begin:end]
    nperseg = min(4096, len(x))
    freq, psd = signal.welch(x, fs=fs, nperseg=nperseg,
                             noverlap=nperseg // 2, detrend=False)
    tone_mask = (freq > 1690) & (freq < 1785)
    tone_power = float(np.sum(psd[tone_mask]) * (freq[1] - freq[0]))
    tone_gain = float(np.sqrt(tone_power / (.05 ** 2 / 2)))
    logical_peak = float(np.max(np.abs(capture)))
    clipping_fraction = float(np.mean(np.abs(capture) >= (32766 / 32767)))
    raw = np.fromfile(raw_output, dtype="<f4")
    record = dict(name=name, expected_dac_variance=expected_variance,
                  command=command, return_code=result.returncode, wall_seconds=elapsed,
                  output_sha256=sha(output), provider_sha256=sha(raw_output),
                  capture_sha256=sha(output), capture_metadata_sha256=sha(metadata_path),
                  metadata=metadata, selected_capture_samples=[begin, end],
                  selected_capture_seconds=[begin / fs, end / fs],
                  selected_sample_count=len(x), tone_gain=tone_gain,
                  capture_peak=logical_peak, clipping_sample_fraction=clipping_fraction,
                  raw_provider_samples=len(raw), raw_provider_rate=capmeta.get("raw_rate"),
                  welch_frequency_hz=freq.tolist(), welch_psd=psd.tolist())
    report = metadata.get("background_noise")
    if expected_variance > 0:
        if not isinstance(report, dict) or report.get("enabled") is not True:
            raise RuntimeError(f"native capture {name} lacks the enabled final-rate noise report")
        if report.get("sample_rate_hz") != metadata["audio_format"]["playback"]["hardware_rate"]:
            raise RuntimeError(f"native capture {name} noise report does not match negotiated playback rate")
    record["background_noise"] = report
    return record


def run(root: Path, binary: Path, device: str):
    fs, seconds = 48000, 6
    source = root / "source-tone-1737hz-0p05rms.f32"
    samples = np.arange(fs * seconds, dtype=np.float64)
    (.05 * np.sin(2 * np.pi * 1737 * samples / fs)).astype("<f4").tofile(source)
    cases = [
        ("off", [], 0.),
        ("db40", ["--background-noise-db", "40"], .5e-4),
        ("db60", ["--background-noise-db", "60"], .5e-6),
        ("bits10", ["--background-noise-bits", "10"], 2. ** (-20) / 3),
    ]
    captures = {}
    for name, flags, expected in cases:
        captures[name] = measure(source, binary, root, name, flags, expected, fs,
                                 seconds, device)
    base = captures["off"]
    f = np.asarray(base["welch_frequency_hz"])
    p0 = np.asarray(base["welch_psd"])
    rows = []
    passband = (f > 1400) & (f < 2100) & ~((f > 1650) & (f < 1820))
    baseline_passband_psd = float(np.mean(p0[passband]))
    bands = ((100, 500), (500, 1400), (2100, 6000), (6000, 12000),
             (12000, 20000), (20000, 23500))
    df = float(f[1] - f[0])
    for name, _, expected in cases[1:]:
        current = captures[name]
        psd = np.asarray(current["welch_psd"])
        added = psd - p0
        measured = float(np.mean(added[passband]))
        total_passband = float(np.mean(psd[passband]))
        # Captured output has the fixed observed analog/DAC loopback gain from
        # the off run. For ideal white sampled noise, one-sided PSD is 2 var/fs.
        predicted = 2 * expected / fs * base["tone_gain"] ** 2
        rows.append(dict(name=name, expected_dac_variance=expected,
                         measured_added_passband_psd=measured,
                         predicted_added_passband_psd=predicted,
                         passband_error_db=float(10 * np.log10(measured / predicted))
                         if measured > 0 and predicted > 0 else None,
                         baseline_passband_psd=baseline_passband_psd,
                         total_passband_psd=total_passband,
                         added_passband_floor_rise_db=float(10 * np.log10(total_passband / baseline_passband_psd))
                         if baseline_passband_psd > 0 and total_passband > 0 else None,
                         added_passband_power=float(np.sum(added[passband]) * df),
                         other_band_psd=[dict(band_hz=[lo, hi],
                             added_mean_psd=float(np.mean(added[(f >= lo) & (f < hi)])))
                             for lo, hi in bands],
                         capture_noise_report=current["background_noise"],
                         tone_gain= current["tone_gain"],
                         capture_peak=current["capture_peak"],
                         clipping_sample_fraction=current["clipping_sample_fraction"],
                         discontinuity=current["metadata"]["discontinuity"],
                         capture_sha256=current["capture_sha256"],
                         capture_metadata_sha256=current["capture_metadata_sha256"]))
    return dict(schema="triad-native-fullrate-noise-cable-v1",
                binary=str(binary), binary_sha256=sha(binary), device=device,
                unchanged_mixer_settings=True, source=str(source), source_sha256=sha(source),
                sample_rate_hz=fs, source_seconds=seconds,
                captures={name: {k: v for k, v in row.items()
                                 if k not in ("welch_frequency_hz", "welch_psd")}
                          for name, row in captures.items()},
                passband_frequency_hz=[1400, 2100], excluded_tone_frequency_hz=[1650, 1820],
                baseline_tone_gain=base["tone_gain"], frequency_resolution_hz=df,
                rows=rows,
                limitation="Four short physical cable observations. The result measures the analog headphone/microphone loopback and does not establish radiated RF noise, an independently calibrated ADC/DAC noise figure, or behavior outside the negotiated capture band.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/dev-cli/triad_audio_capture"))
    parser.add_argument("--device", default="hw:1,0")
    parser.add_argument("--output", type=Path,
                        default=Path("local-evidence/triad-background-noise-fullrate/native-cable"))
    args = parser.parse_args()
    if args.device != "hw:1,0":
        parser.error("this authorized qualification is limited to the explicit hw:1,0 loopback")
    binary = args.binary.resolve()
    output = args.output.resolve()
    if not binary.is_file():
        parser.error(f"native capture executable does not exist: {binary}")
    output.mkdir(parents=True, exist_ok=False)
    (output / "qualification-start.json").write_text(json.dumps(
        dict(binary=str(binary), binary_sha256=sha(binary), device=args.device,
             declared_no_mixer_changes=True, started_unix=time.time()), indent=2) + "\n")
    try:
        report = run(output, binary, args.device)
    except Exception as exc:
        (output / "qualification-failure.json").write_text(json.dumps(
            dict(status="failed", error=f"{type(exc).__name__}: {exc}",
                 finished_unix=time.time()), indent=2) + "\n")
        raise
    report["source_sha256"] = sha(Path(__file__))
    (output / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    summary = dict(cases=len(report["rows"]),
                   passband_errors_db=[r["passband_error_db"] for r in report["rows"]],
                   capture_peak=[r["capture_peak"] for r in report["rows"]],
                   any_discontinuity=any(r["discontinuity"]["capture"] or r["discontinuity"]["playback"]
                                         for r in report["rows"]))
    print(json.dumps(summary, allow_nan=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
