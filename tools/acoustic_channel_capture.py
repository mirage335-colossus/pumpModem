#!/usr/bin/env python3
"""Bounded real default-speaker/microphone capture for the acoustic sounder.

Uses separate stereo float32 PulseAudio-compatible streams for channel analysis.
This measures the desktop path; actual modem trials separately use production
S16 audio. It never changes mixers, creates a virtual loopback, or substitutes
simulation for a failed device. Existing captures are not overwritten.
"""
import argparse
import hashlib
import json
from pathlib import Path
import signal
import subprocess
import time


def query(*args):
    return subprocess.check_output(args, text=True, timeout=10).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--live", action="store_true", required=True)
    args = parser.parse_args()
    rate = 48000
    size = args.input.stat().st_size
    if not size or size % 8 or size > 120 * rate * 8:
        parser.error("Input must be stereo float32le, between one frame and 120 seconds")
    args.output.mkdir(parents=True, exist_ok=True)
    destination = args.output / "capture-stereo.f32"
    metadata_path = args.output / "capture-metadata.json"
    if destination.exists() or metadata_path.exists():
        parser.error("Capture/metadata already exists; use a new output folder")
    info = {
        "sample_rate": rate, "channels": 2, "format": "float32le",
        "input": str(args.input), "seconds": size / (8 * rate),
        "source": query("pactl", "get-default-source"),
        "sink": query("pactl", "get-default-sink"),
        "source_volume_before": query("pactl", "get-source-volume", "@DEFAULT_SOURCE@"),
        "sink_volume_before": query("pactl", "get-sink-volume", "@DEFAULT_SINK@"),
        "input_sha256": hashlib.sha256(args.input.read_bytes()).hexdigest(),
    }
    if info["source"].endswith(".monitor"):
        raise RuntimeError("Default source is a digital monitor, not the requested physical microphone")
    common = ["--raw", "--format=float32le", "--rate=48000", "--channels=2",
              "--latency-msec=40", "--client-name=DataPump-acoustic-measurement"]
    capture_command = ["parecord", *common, "--device=" + info["source"]]
    play_command = ["paplay", *common, "--device=" + info["sink"],
                    "--volume=65536", str(args.input)]
    info.update(capture_command=capture_command, playback_command=play_command)
    capture = None
    start = time.monotonic()
    try:
        with destination.open("xb") as output, (args.output / "capture.log").open("xb") as errors:
            capture = subprocess.Popen(capture_command, stdout=output, stderr=errors)
            time.sleep(.75)
            if capture.poll() is not None:
                raise RuntimeError("Microphone capture ended before playback")
            info["playback_start_wall_seconds"] = time.monotonic() - start
            play = subprocess.run(play_command, capture_output=True, timeout=info["seconds"] + 10)
            (args.output / "playback.log").write_bytes(play.stderr)
            info["playback_returncode"] = play.returncode
            play.check_returncode()
            time.sleep(1)
            capture.send_signal(signal.SIGINT)
            capture.wait(timeout=5)
            info["capture_returncode"] = capture.returncode
            if capture.returncode != 0:
                raise RuntimeError(f"Microphone recorder failed with status {capture.returncode}")
            if destination.stat().st_size < size:
                raise RuntimeError("Microphone recording is shorter than the transmitted sounder")
            info["complete"] = True
    finally:
        if capture is not None and capture.poll() is None:
            capture.terminate()
            try:
                capture.wait(timeout=5)
            except subprocess.TimeoutExpired:
                capture.kill()
                capture.wait()
        info["wall_seconds"] = time.monotonic() - start
        info["source_volume_after"] = query("pactl", "get-source-volume", "@DEFAULT_SOURCE@")
        info["sink_volume_after"] = query("pactl", "get-sink-volume", "@DEFAULT_SINK@")
        if destination.exists():
            info["capture_bytes"] = destination.stat().st_size
            info["capture_sha256"] = hashlib.sha256(destination.read_bytes()).hexdigest()
        metadata_path.write_text(json.dumps(info, indent=2) + "\n")
    print(json.dumps(info, indent=2))


if __name__ == "__main__":
    main()
