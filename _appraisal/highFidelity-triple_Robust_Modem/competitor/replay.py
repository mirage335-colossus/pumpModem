#!/usr/bin/env python3
"""Re-evaluate frozen native comparator captures; no channel rerendering.

Useful for a stronger upstream decoder tier or the finite Pulse boundary.
Each original receipt is retained and linked by hash. This is not an
independent channel/noise trial.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time
import run as adapter


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, action="append", required=True, help="original per-episode receipt.json")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--tier", choices=("standard", "strong"), default="standard")
    args = parser.parse_args()
    if not 1 <= len(args.capture) <= 12:
        parser.error("at most twelve selected frozen captures per replay")
    output = args.output_dir.resolve()
    if adapter.ROOT / "local-evidence" not in output.parents:
        parser.error("replay output must be new appraisal local-evidence")
    output.mkdir(parents=True, exist_ok=False)
    inputs = []
    for path in args.capture:
        path = path.resolve()
        if adapter.ROOT / "local-evidence" not in path.parents:
            parser.error("replay input must be appraisal local-evidence")
        old = json.loads(path.read_text())
        mode = old["input"]["mode"]
        filename = ("received.f32" if mode in ("psk31", "olivia8-250") else
                    "000000_0001.c2" if mode == "wspr" else "000000_000001.wav")
        capture = path.parent / filename
        expected_hash = (adapter.sha(path.parent / "rx/input.f32") if mode in ("psk31", "olivia8-250")
                         else old["capture_sha256"])
        if adapter.sha(capture) != expected_hash:
            raise RuntimeError("frozen capture differs from original receiver input")
        inputs.append((path, old, capture))
    interposer = adapter.ROOT / ".build/competitors/pulse_boundary.so"
    adapter.command(["cc", "-shared", "-fPIC", "-O2", Path(__file__).with_name("pulse_boundary.c"), "-o", interposer], output)
    log = (output / "xvfb.log").open("w")
    display = f":{600 + os.getpid() % 300}"
    server = subprocess.Popen([str(adapter.BIN / "Xvfb"), display, "-screen", "0", "1200x800x24", "-nolisten", "tcp", "-ac"], stdout=log, stderr=log)
    result = {"schema_version": 1, "kind": "matched captured-sample re-evaluation; not independent trials",
              "decoder_tier": args.tier, "interposer_sha256": adapter.sha(interposer),
              "script_sha256": adapter.sha(__file__), "adapter_sha256": adapter.sha(Path(adapter.__file__)),
              "maximum_wall_seconds": 600, "episodes": []}
    started = time.monotonic()
    try:
        time.sleep(.4)
        if server.poll() is not None:
            raise RuntimeError("private Xvfb failed")
        for index, (path, old, capture) in enumerate(inputs):
            if time.monotonic() - started > 600:
                raise RuntimeError("predeclared replay wall-time ceiling")
            entry = old["input"]
            directory = output / f"{index:02d}-{entry['id']}"
            directory.mkdir()
            decoder = adapter.decode(entry["mode"], capture, directory, display, interposer, tier=args.tier)
            outcome = adapter.outcomes(entry["mode"], decoder["received_text"], old["expected_text"], entry.get("control", "wanted") == "wanted")
            row = {"input": entry, "original_receipt": str(path), "original_receipt_sha256": adapter.sha(path),
                   "capture_sha256": adapter.sha(capture), "decoder": decoder, **outcome,
                   "original_correct_payload_found": old["correct_payload_found"],
                   "input_binding": "raw float bytes equal preserved original rx/input.f32" if entry["mode"] in ("psk31", "olivia8-250") else "capture hash verified against original receipt"}
            result["episodes"].append(row)
            (directory / "receipt.json").write_text(json.dumps(row, indent=2) + "\n")
            (output / "run.json").write_text(json.dumps(result, indent=2) + "\n")
            print(entry["id"], outcome["correct_payload_found"], repr(decoder["received_text"]), flush=True)
        result["status"] = "completed"
    except BaseException as exc:
        result["status"] = "incomplete"
        result["reason"] = str(exc)
        raise
    finally:
        adapter.stop(server)
        log.close()
        result["wall_seconds"] = time.monotonic() - started
        (output / "run.json").write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
