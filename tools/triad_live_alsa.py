#!/usr/bin/env python3
"""Opt-in stereo raw-ALSA cable measurement; no ordinary receiver dependency.

ALSA utils are deliberately used with an explicit hardware endpoint and all
conversion disabled. The production mono capture path is a different backend.
"""
from __future__ import annotations

import argparse
from contextlib import ExitStack
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time

import numpy as np


def parse_hw_format(log, rate, direction):
    if "Hardware PCM" not in log:
        raise ValueError("ALSA log does not identify a raw Hardware PCM")
    fields = {}
    for key in ("stream", "access", "format", "channels", "rate", "exact rate",
                "msbits", "buffer_size", "period_size"):
        matches = re.findall(r"^\s*" + re.escape(key) + r"\s*:\s*(.*?)\s*$", log, re.M)
        if matches:
            if len(set(matches)) != 1:
                raise ValueError(f"ambiguous negotiated ALSA {key}")
            fields[key] = matches[0]
    required = {"stream": direction, "access": "RW_INTERLEAVED", "format": "S16_LE",
                "channels": "2", "rate": str(rate)}
    for key, expected in required.items():
        if fields.get(key) != expected:
            raise ValueError(f"negotiated ALSA {key}: expected {expected}, got {fields.get(key)}")
    if "exact rate" not in fields or float(fields["exact rate"].split()[0]) != rate:
        raise ValueError("negotiated ALSA exact rate is missing or different")
    return fields


def pcm_s16(x):
    """Exact unit-gain production float32 multiplication and truncation."""
    x = np.asarray(x, dtype=np.float32)
    if x.ndim != 1 or not len(x) or not np.all(np.isfinite(x)):
        raise ValueError("nonempty finite mono float32 input is required")
    if np.max(np.abs(x)) > .98:
        raise ValueError("input peak exceeds 0.98 full scale")
    return np.trunc(np.clip(x, -1., 1.) * np.float32(32767.)).astype("<i2")


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--live", action="store_true")
    p.add_argument("--exclusive", action="store_true")
    for name in ("input", "output", "metadata", "capture-device", "playback-device"):
        p.add_argument("--" + name, required=True)
    p.add_argument("--raw-output")
    p.add_argument("--rate", required=True, type=int)
    p.add_argument("--capture-channel", type=int, choices=(0, 1), default=0)
    p.add_argument("--preroll-seconds", type=float, default=.75)
    p.add_argument("--postroll-seconds", type=float, default=1.)
    p.add_argument("--max-seconds", type=float, default=30.)
    return p


def validate(c):
    if not c.live:
        raise ValueError("opening audio requires explicit --live")
    if not c.exclusive:
        raise ValueError("explicit --exclusive raw hardware access is required")
    for device in (c.capture_device, c.playback_device):
        if not re.fullmatch(r"hw:[A-Za-z0-9_-]+,[0-9]+(?:,[0-9]+)?", device):
            raise ValueError("device must be an explicit hw:C,D endpoint; aliases/monitors are rejected")
    if not 44100 <= c.rate <= 192000:
        raise ValueError("rate must be 44100..192000 Hz")
    if not math.isfinite(c.max_seconds) or not 2 <= c.max_seconds <= 2800:
        raise ValueError("max-seconds must be finite and 2..2800")
    for value in (c.preroll_seconds, c.postroll_seconds):
        if not math.isfinite(value) or not .1 <= value <= 10:
            raise ValueError("pre/postroll must be finite and 0.1..10 seconds")


def _stop(process):
    if process is None or process.poll() is not None:
        return None
    try:
        process.terminate()
        try:
            process.wait(timeout=1.)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=2.)
    except ProcessLookupError:
        process.wait(timeout=2.)
    except Exception as exc:
        return f"child cleanup failed: {type(exc).__name__}: {exc}"
    return None


def run(c):
    started = time.monotonic()
    deadline = started + c.max_seconds
    validate(c)

    def remaining():
        value = deadline - time.monotonic()
        if value <= 0:
            raise TimeoutError("whole capture command exceeded --max-seconds")
        return value

    source = Path(c.input)
    size = source.stat().st_size
    if not size or size % 4 or size > 512 * 1024**2:
        raise ValueError("input must be nonempty aligned float32le within 512 MiB")
    frames = size // 4
    source_seconds = frames / c.rate
    capture_frames = math.ceil((source_seconds + c.preroll_seconds + c.postroll_seconds + 2.) * c.rate)
    if source_seconds + c.preroll_seconds + c.postroll_seconds + 5. > c.max_seconds:
        raise ValueError("source, rolls, and five-second setup/finalization reserve exceed max-seconds")
    if capture_frames * 4 > 512 * 1024**2:
        raise ValueError("stereo capture exceeds 512 MiB bound")
    x = np.fromfile(source, dtype="<f4")
    if len(x) != frames:
        raise ValueError("input changed while loading")
    pcm = pcm_s16(x)
    source_hash = hashlib.sha256(x.tobytes()).hexdigest()
    peak = float(np.max(np.abs(x)))
    del x
    remaining()
    executables = {name: shutil.which(name) for name in ("arecord", "aplay")}
    if not all(executables.values()):
        raise ValueError("arecord and aplay are required")
    paths = {"output": Path(c.output), "metadata": Path(c.metadata),
             "stereo": Path(str(c.output) + ".stereo.s16le"),
             "tx": Path(str(c.output) + ".tx.stereo.s16le"),
             "capture_log": Path(str(c.output) + ".arecord.stderr.log"),
             "playback_log": Path(str(c.output) + ".aplay.stderr.log"),
             "playback_stdout": Path(str(c.output) + ".aplay.stdout.log")}
    if c.raw_output:
        paths["raw"] = Path(c.raw_output)
    resolved = [p.resolve() for p in paths.values()]
    if len(set(resolved)) != len(resolved) or source.resolve() in resolved:
        raise ValueError("all input/output paths must be distinct")
    common = ["-c", "2", "-r", str(c.rate), "-f", "S16_LE", "-t", "raw",
              "--fatal-errors", "-v", "--disable-resample", "--disable-channels",
              "--disable-format", "--disable-softvol"]
    capture_argv = [executables["arecord"], "-D", c.capture_device, *common,
                    "--samples", str(capture_frames)]
    playback_argv = [executables["aplay"], "-D", c.playback_device, *common, str(paths["tx"])]
    report = {"schema": "triad-audio-capture-v1", "backend": "alsa-utils-raw-stereo-v1",
              "status": "incomplete", "failure": None,
              "requested": {"rate": c.rate, "capture_device": c.capture_device,
                            "playback_device": c.playback_device, "exclusive": True,
                            "capture_channel": c.capture_channel, "max_seconds": c.max_seconds,
                            "preroll_seconds": c.preroll_seconds, "postroll_seconds": c.postroll_seconds,
                            "input_peak": peak},
              "input_sha256": source_hash, "input_frames": frames,
              "argv": {"capture": capture_argv, "playback": playback_argv},
              "files": {k: str(v) for k, v in paths.items()},
              "audio_format": {}, "discontinuity": {"capture": False, "playback": False},
              "timing_note": "No provider timestamps. Public probe markers measure relative latency/rate; subprocess/file boundaries are not calibrated analog times.",
              "pcm_contract": "TX float32 clamp*32767 truncate to S16_LE, identical stereo; RX selected fixed channel S16/32768, both channels retained; no effective ADC resolution claim"}
    capture = playback = None
    with ExitStack() as stack:
        # Exclusive creation claims every artifact before starting any device.
        files = {key: stack.enter_context(path.open("x+b")) for key, path in paths.items()}
        try:
            for first in range(0, len(pcm), 65536):
                stereo = np.repeat(pcm[first:first + 65536, None], 2, axis=1)
                files["tx"].write(stereo.tobytes())
            files["tx"].flush()
            del pcm
            versions = {}
            for name, exe in executables.items():
                result = subprocess.run([exe, "--version"], capture_output=True, text=True,
                                        timeout=min(5., remaining()), check=True)
                versions[name] = result.stdout.strip()
            report["versions"] = versions
            capture = subprocess.Popen(capture_argv, stdin=subprocess.DEVNULL,
                                       stdout=files["stereo"], stderr=files["capture_log"])
            readiness_limit = min(deadline, time.monotonic() + 15.)
            pre_bytes = math.ceil(c.preroll_seconds * c.rate) * 4
            while paths["stereo"].stat().st_size < pre_bytes:
                if capture.poll() is not None:
                    raise RuntimeError("capture exited before preroll")
                if time.monotonic() >= readiness_limit:
                    raise TimeoutError("capture preroll exceeded readiness deadline")
                time.sleep(min(.01, remaining()))
            # Refuse a negotiated-rate surprise before transmitting anything.
            parse_hw_format(paths["capture_log"].read_text(errors="replace"),
                            c.rate, "CAPTURE")
            report["playback"] = {"call_start_seconds": time.monotonic() - started,
                                  "call_start_capture_frame_lower_bound": paths["stereo"].stat().st_size // 4}
            playback = subprocess.Popen(playback_argv, stdin=subprocess.DEVNULL,
                                        stdout=files["playback_stdout"], stderr=files["playback_log"])
            playback.wait(timeout=remaining())
            report["playback"]["call_return_seconds"] = time.monotonic() - started
            end_frames = paths["stereo"].stat().st_size // 4
            report["playback"]["call_return_capture_frame_lower_bound"] = end_frames
            if playback.returncode:
                raise RuntimeError(f"aplay exited {playback.returncode}")
            if capture.poll() is not None:
                raise RuntimeError("capture ended before playback drain/postroll")
            if capture_frames - end_frames < math.ceil(c.postroll_seconds * c.rate):
                raise RuntimeError("insufficient capture support remains for postroll")
            capture.wait(timeout=remaining())
            if capture.returncode:
                raise RuntimeError(f"arecord exited {capture.returncode}")
        except Exception as exc:
            report["failure"] = f"{type(exc).__name__}: {exc}"
        finally:
            report["cleanup_errors"] = []
            for child in (playback, capture):
                try:
                    error = _stop(child)
                except Exception as exc:
                    error = f"child cleanup failed: {type(exc).__name__}: {exc}"
                if error:
                    report["cleanup_errors"].append(error)
            if report["cleanup_errors"] and report["failure"] is None:
                report["failure"] = "; ".join(report["cleanup_errors"])
        for name in ("capture", "playback"):
            process = capture if name == "capture" else playback
            report[name + "_returncode"] = None if process is None else process.returncode
            files[name + "_log"].flush()
            log = paths[name + "_log"].read_text(errors="replace")
            # Fatal mode must not silently recover an xrun. Keep raw logs too.
            report["discontinuity"][name] = bool(re.search(r"\b(?:overrun|underrun|xrun)\b", log, re.I))
            try:
                report["audio_format"][name] = parse_hw_format(log, c.rate, name.upper())
                if report["discontinuity"][name]:
                    raise ValueError(name + " discontinuity reported")
            except ValueError as exc:
                if report["failure"] is None:
                    report["failure"] = str(exc)
        try:
            remaining()
            raw_bytes = paths["stereo"].stat().st_size
            if raw_bytes % 4:
                raise ValueError("partial stereo S16 frame")
            actual_frames = raw_bytes // 4
            if actual_frames != capture_frames and report["failure"] is None:
                raise ValueError(f"capture frame count {actual_frames} != requested {capture_frames}")
            peaks = np.zeros(2)
            clips = np.zeros(2, dtype=np.int64)
            rms_sum = np.zeros(2)
            raw_hash = hashlib.sha256()
            files["stereo"].seek(0)
            while block := files["stereo"].read(65536 * 4):
                remaining()
                raw_hash.update(block)
                a = np.frombuffer(block, dtype="<i2").reshape(-1, 2).astype(np.float32) / np.float32(32768.)
                selected = a[:, c.capture_channel].astype("<f4")
                files["output"].write(selected.tobytes())
                if "raw" in files:
                    files["raw"].write(selected.tobytes())
                peaks = np.maximum(peaks, np.max(np.abs(a), axis=0))
                clips += np.sum(np.abs(a) >= .98, axis=0)
                rms_sum += np.sum(a.astype(np.float64)**2, axis=0)
            report["capture"] = {"logical_samples": actual_frames, "logical_rate": c.rate,
                                 "logical_seconds": actual_frames / c.rate,
                                 "logical_peak": float(peaks[c.capture_channel]),
                                 "raw_rate": c.rate, "raw_samples": actual_frames,
                                 "selected_channel": c.capture_channel,
                                 "stereo_peak": peaks.tolist(), "stereo_near_fullscale": clips.tolist(),
                                 "stereo_rms": np.sqrt(rms_sum / max(1, actual_frames)).tolist(),
                                 "stereo_sha256": raw_hash.hexdigest(),
                                 "raw_semantics": "both raw ALSA S16 channels in sidecar; optional raw-output is fixed selected-channel float PCM"}
            if np.any(clips) and report["failure"] is None:
                report["failure"] = "capture near full scale; lower explicit stimulus or input gain"
            for key in ("output", "raw"):
                if key in files:
                    files[key].flush()
            remaining()
        except Exception as exc:
            if report["failure"] is None:
                report["failure"] = f"{type(exc).__name__}: {exc}"
        # A successful transfer is not complete until its data artifacts close.
        # Keep metadata open so a delayed flush/close error is recorded honestly.
        report["finalization_errors"] = []
        for key, handle in files.items():
            if key == "metadata":
                continue
            try:
                handle.flush()
                handle.close()
            except Exception as exc:
                report["finalization_errors"].append(f"{key}: {type(exc).__name__}: {exc}")
        if report["finalization_errors"] and report["failure"] is None:
            report["failure"] = "; ".join(report["finalization_errors"])
        if time.monotonic() > deadline and report["failure"] is None:
            report["failure"] = "whole capture deadline exceeded during finalization"
        report["elapsed_seconds"] = time.monotonic() - started
        report["status"] = "complete" if report["failure"] is None else "incomplete"
        files["metadata"].write((json.dumps(report, indent=2, allow_nan=False) + "\n").encode())
        files["metadata"].flush()
    return report


def main(argv=None):
    previous = {}
    def interrupted(number, frame):
        raise RuntimeError(f"capture interrupted by signal {number}")
    try:
        for number in (signal.SIGTERM, signal.SIGINT):
            previous[number] = signal.signal(number, interrupted)
        result = run(parser().parse_args(argv))
    except Exception as exc:
        print(f"triad_live_alsa: {exc}", file=sys.stderr)
        return 2
    finally:
        for number, handler in previous.items():
            signal.signal(number, handler)
    print(json.dumps({k: result[k] for k in ("status", "failure", "elapsed_seconds")}))
    return 0 if result["status"] == "complete" else 1


if __name__ == "__main__":
    raise SystemExit(main())
