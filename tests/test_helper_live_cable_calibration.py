#!/usr/bin/env python3
"""Optional NumPy regression for Helper-only cable model calibration.

Generates its own mono capture with the compiled simulator; no sound device or
recorded hardware fixture is required.  The probe mutation checks that model
fitting never consumes the held-out probe.  Exit 77 when NumPy is unavailable.
"""

import json
from pathlib import Path
import subprocess
import sys
import tempfile

try:
    import numpy as np
except ImportError:
    print("SKIP: optional Helper cable calibration requires NumPy")
    raise SystemExit(77)


def run(arguments, check=True):
    result = subprocess.run(arguments, capture_output=True, text=True, timeout=30)
    if check and result.returncode:
        raise AssertionError(f"command failed ({result.returncode}): {arguments}\n{result.stdout}{result.stderr}")
    return result


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_helper_live_cable_calibration.py CABLE_TOOL CALIBRATOR")
    tool, calibrator = map(str, map(Path, sys.argv[1:]))
    with tempfile.TemporaryDirectory(prefix="helper-cable-calibration-") as directory:
        root = Path(directory)
        stimulus, capture, changed = (root / name for name in ("stimulus.f32", "capture.f32", "changed.f32"))
        run([tool, "--generate-long", str(stimulus)])
        run([tool, "--simulate-long", str(stimulus), str(capture),
             "--preset", "cable-20261030", "--seed", "1901"])
        command = [sys.executable, calibrator, str(stimulus), str(capture)]
        baseline = json.loads(run(command).stdout)
        model = baseline["model"]
        assert .7 < model["gain"] < 1.0, model
        assert 5 < model["highpass_cutoff_hz"] < 20, model
        assert .77 < model["absolute_capture_latency_seconds"] < .80, model
        validation = baseline["heldout_probe_validation"]
        assert abs(validation["gain_error_db"]) < .02, validation
        assert abs(validation["phase_error_degrees"]) < .2, validation

        # Change only the probe-bearing portion of the received capture.  All
        # fit windows end before 37.5 s; the known probe starts after 38 s plus
        # channel latency.  The receiver does not use this mutation to track.
        pcm = np.fromfile(capture, dtype="<f4")
        cutoff = int((model["absolute_capture_latency_seconds"] + 38) * 48000)
        assert cutoff > int(37.5 * 48000)
        pcm[cutoff:] *= .5
        pcm.tofile(changed)
        mutated_command = [sys.executable, calibrator, str(stimulus), str(changed)]
        mutated = json.loads(run(mutated_command).stdout)
        for field in ("model", "pilots", "preroll_noise", "fit_windows"):
            assert baseline[field] == mutated[field], f"probe changed fitted {field}"
        assert baseline["capture_sha256"] != mutated["capture_sha256"]
        delta = mutated["heldout_probe_validation"]["gain_error_db"] - validation["gain_error_db"]
        assert abs(delta + 6.020599913279624) < .002, delta

        for end in ("37.999", "38.5"):
            invalid = run(command + ["--fit-end", end], check=False)
            assert invalid.returncode != 0, "probe-overlapping fit window was accepted"
            assert "precede" in invalid.stderr, invalid.stderr
        invalid = run(command + ["--onset-min", "37.8", "--onset-max", "38.1"], check=False)
        assert invalid.returncode != 0, "probe-overlapping startup window was accepted"
        assert "precede" in invalid.stderr, invalid.stderr
        oversized = root / "oversized.f32"
        with oversized.open("wb") as stream:
            stream.seek(48000 * 120 * 4)
            stream.write(b"\0")
        invalid = run([sys.executable, calibrator, str(oversized), str(capture)], check=False)
        assert invalid.returncode != 0, "oversized input was accepted"
        assert "at most 120 seconds" in invalid.stderr, invalid.stderr
        print("PASS: generated mono model fits Helper/preroll, predicts held-out probe, "
              "and ignores probe changes during model calibration")
        print(json.dumps({"model": model, "heldout_probe_validation": validation}, sort_keys=True))


if __name__ == "__main__":
    main()
