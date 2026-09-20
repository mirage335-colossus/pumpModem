"""Generate and record known signals through the actual desktop default devices.

Requires NumPy and PulseAudio-compatible paplay/parecord/pactl. No virtual
loopback or simulation fallback. --live is required to open audio. Capture
volume is restored in finally. An optional output-volume comparison is also
restored afterward.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import time

import numpy as np

RATE = 48000


def command(args):
    return subprocess.run(args, check=True, capture_output=True, text=True, timeout=10).stdout


def stimulus(mode, amplitude, frequencies, repeats):
    chunks, segments = [], []
    position = 0

    def append(kind, seconds, frequency=0, level=0):
        nonlocal position
        n = round(seconds * RATE)
        t = np.arange(n) / RATE
        if kind == "sync":
            x = .1 * np.sin(2*np.pi*(600*t + (11000-600)/(2*seconds)*t*t))
        elif kind == "tone":
            x = level*np.sin(2*np.pi*frequency*t)
        elif kind == "multitone":
            # Schroeder phases reduce crest factor without clipping.
            x = np.zeros(n)
            for k, f in enumerate(frequencies):
                x += np.cos(2*np.pi*f*t + np.pi*k*(k-1)/len(frequencies))
            x *= level / np.max(np.abs(x))
        else:
            x = np.zeros(n)
        if kind != "silence":
            ramp = min(round(.02*RATE), n//2)
            w = np.sin(np.linspace(0, np.pi/2, ramp))**2
            x[:ramp] *= w
            x[-ramp:] *= w[::-1]
        segments.append(dict(kind=kind, start_sample=position, samples=n,
                             frequency_hz=frequency, tx_peak_requested=level))
        position += n
        chunks.append(x.astype("<f4"))

    append("silence", 1)
    append("sync", .4)
    append("silence", 1)
    if mode == "levels":
        for a in [.03, .06, .12, .25, .35, .5, .7, .9]:
            append("tone", 3, 997, a)
            append("silence", .6)
    elif mode == "frequencies":
        for f in frequencies:
            append("tone", 3, f, amplitude)
            append("silence", .6)
    elif mode == "repeat":
        for _ in range(repeats):
            append("silence", 2)
            append("tone", 4, frequencies[0], amplitude)
            append("tone", 3, frequencies[0], amplitude*.001)
    elif mode == "multitone":
        for _ in range(repeats):
            append("silence", 2)
            append("multitone", 4, 0, amplitude)
            append("multitone", 3, 0, amplitude*.001)
    append("silence", 2)
    return np.concatenate(chunks), segments


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--live", action="store_true")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--mode", choices=["levels", "frequencies", "repeat", "multitone"], default="levels")
    p.add_argument("--gain-db", type=float, default=0)
    p.add_argument("--sink-percent", type=float, choices=[95.0,100.0])
    p.add_argument("--amplitude", type=float, default=.7)
    p.add_argument("--frequencies", type=float, nargs="+", default=[997])
    p.add_argument("--repeats", type=int, default=3)
    args = p.parse_args()
    if not 0 <= args.gain_db <= 18 or not 0 < args.amplitude <= .99:
        p.error("gain must be 0..18 dB and amplitude 0..0.99")
    if not 1 <= args.repeats <= 5 or any(not 20 <= f <= 20000 for f in args.frequencies):
        p.error("invalid repeats or frequency")
    x, segments = stimulus(args.mode, args.amplitude, args.frequencies, args.repeats)
    if len(x) > 120*RATE:
        p.error("stimulus exceeds 120 seconds")
    args.output.mkdir(parents=True, exist_ok=False)
    x.tofile(args.output / "transmit-mono.f32")
    np.repeat(x[:, None], 2, axis=1).tofile(args.output / "transmit-stereo.f32")
    meta = dict(mode=args.mode, gain_db=args.gain_db, sample_rate=RATE,
                frequencies=args.frequencies, segments=segments,
                transmit_seconds=len(x)/RATE, live=args.live,
                pcm_format="float32le", channels=2,
                utc=time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()))
    if not args.live:
        (args.output/"metadata.json").write_text(json.dumps(meta, indent=2)+"\n")
        print(json.dumps(meta, indent=2))
        return
    meta["pactl_info"] = command(["pactl", "info"])
    meta["sources"] = command(["pactl", "list", "sources"])
    meta["sinks"] = command(["pactl", "list", "sinks"])
    old_volume_text = command(["pactl", "get-source-volume", "@DEFAULT_SOURCE@"])
    old_volumes = re.findall(r":\s*(\d+)\s*/", old_volume_text.splitlines()[0])
    if not old_volumes:
        raise RuntimeError("Cannot parse source volume; no change made")
    meta["source_volume_before"] = old_volume_text
    sink_volume_text = command(["pactl", "get-sink-volume", "@DEFAULT_SINK@"])
    sink_volumes = re.findall(r":\s*(\d+)\s*/", sink_volume_text.splitlines()[0])
    if not sink_volumes:
        raise RuntimeError("Cannot parse sink volume; no change made")
    meta["sink_volume_before"] = sink_volume_text
    meta["mixer_before"] = command(["amixer", "-c", "1"])
    capture = None
    started = time.monotonic()
    try:
        if args.sink_percent is not None:
            command(["pactl", "set-sink-volume", "@DEFAULT_SINK@", str(args.sink_percent)+"%"])
        new = [str(round(int(v)*10**(args.gain_db/60))) for v in old_volumes]
        command(["pactl", "set-source-volume", "@DEFAULT_SOURCE@", *new])
        time.sleep(.2)
        meta["source_volume_test"] = command(["pactl", "get-source-volume", "@DEFAULT_SOURCE@"])
        meta["sink_volume_test"] = command(["pactl", "get-sink-volume", "@DEFAULT_SINK@"])
        meta["mixer_test"] = command(["amixer", "-c", "1"])
        common = ["--raw", "--format=float32le", "--rate=48000", "--channels=2",
                  "--latency-msec=40", "--client-name=DataPump-SNR-measurement"]
        capture_cmd = ["parecord", *common, "--device=@DEFAULT_SOURCE@"]
        playback_cmd = ["paplay", *common, "--device=@DEFAULT_SINK@",
                        "--volume=65536", str(args.output/"transmit-stereo.f32")]
        meta.update(capture_command=capture_cmd, playback_command=playback_cmd)
        with (args.output/"capture-stereo.f32").open("wb") as output, (args.output/"capture.log").open("wb") as errors:
            capture = subprocess.Popen(capture_cmd, stdout=output, stderr=errors)
            time.sleep(.8)
            if capture.poll() is not None:
                raise RuntimeError("Capture ended before playback")
            meta["playback_start_wall_offset"] = time.monotonic()-started
            playback = subprocess.run(playback_cmd, capture_output=True, timeout=len(x)/RATE+15)
            (args.output/"playback.log").write_bytes(playback.stderr)
            meta["playback_returncode"] = playback.returncode
            playback.check_returncode()
            time.sleep(.8)
            capture.send_signal(signal.SIGINT)
            capture.wait(timeout=5)
            meta["capture_returncode"] = capture.returncode
        y = np.fromfile(args.output/"capture-stereo.f32", dtype="<f4").reshape(-1, 2)
        meta["captured_seconds"] = len(y)/RATE
        meta["capture_peak_by_channel"] = np.max(np.abs(y), axis=0).tolist()
        meta["capture_near_fullscale_by_channel"] = (np.abs(y)>=.999).sum(axis=0).tolist()
        meta["capture_sha256"] = hashlib.sha256((args.output/"capture-stereo.f32").read_bytes()).hexdigest()
        meta["transmit_sha256"] = hashlib.sha256((args.output/"transmit-stereo.f32").read_bytes()).hexdigest()
    finally:
        if capture is not None and capture.poll() is None:
            capture.terminate()
            try:
                capture.wait(timeout=5)
            except subprocess.TimeoutExpired:
                capture.kill(); capture.wait()
        command(["pactl", "set-source-volume", "@DEFAULT_SOURCE@", *old_volumes])
        if args.sink_percent is not None:
            command(["pactl", "set-sink-volume", "@DEFAULT_SINK@", *sink_volumes])
        meta["source_volume_restored"] = command(["pactl", "get-source-volume", "@DEFAULT_SOURCE@"])
        meta["sink_volume_restored"] = command(["pactl", "get-sink-volume", "@DEFAULT_SINK@"])
        meta["wall_seconds"] = time.monotonic()-started
        (args.output/"metadata.json").write_text(json.dumps(meta, indent=2)+"\n")
    print(json.dumps({k:meta[k] for k in ["transmit_seconds", "captured_seconds", "capture_peak_by_channel", "capture_near_fullscale_by_channel", "wall_seconds"]}))


if __name__ == "__main__":
    main()
