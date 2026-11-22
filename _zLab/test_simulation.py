#!/usr/bin/env python3
"""Opt-in functional message tests through the lab's native pipe relay.

Set LAB_RUN_SIMULATION=1 for the 120 Hz CPU replay or
LAB_RUN_LOW_RATE_SIMULATION=1 for the 3.6 Hz message. Each has a 120 s deadline.
LAB_SIMULATION_WORKER selects the absolute path of an existing worker binary.
The low-rate CPU case uses a GPSDO. LAB_SIMULATION_REQUIRE_GPU=1 uses the
crystal preset with the required GPU policy and checks that GPU search ran.
These strong-link cases check functional completion. Sensitivity and
sustained capacity require separate measurements.
"""
import base64
import json
import os
from pathlib import Path
import select
import struct
import subprocess
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_WORKER = ROOT / "build/dev-cli-web/datapump-worker"
RELAY = ROOT / "_zLab/worker_relay.py"


def frame(kind, payload=b""):
    return b"DPW1" + struct.pack("<II", kind, len(payload)) + payload


def string(value):
    data = value.encode("utf-8")
    return struct.pack("<I", len(data)) + data


class SimulationTest(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("LAB_RUN_SIMULATION") == "1", "explicit opt-in for bounded DSP work")
    def test_complete_message_via_relay(self):
        self.run_message("120 Hz", "gpsdo-ocxo", check_replay=True)

    @unittest.skipUnless(os.environ.get("LAB_RUN_LOW_RATE_SIMULATION") == "1",
                         "explicit opt-in for the 3.6 Hz message")
    def test_low_rate_message_via_relay(self):
        require_gpu = os.environ.get("LAB_SIMULATION_REQUIRE_GPU") == "1"
        # The crystal preset offers the GPU a public frequency search. The
        # compact GPSDO plan reduces CPU work for the functional smoke case.
        self.run_message("3.6 Hz", "crystal" if require_gpu else "gpsdo-ocxo", require_gpu=require_gpu)

    def run_message(self, rate, base_oscillator, *, check_replay=False, require_gpu=False):
        worker = DEFAULT_WORKER
        if selected := os.environ.get("LAB_SIMULATION_WORKER"):
            worker = Path(selected)
            self.assertTrue(worker.is_absolute() and worker.is_file(),
                            "LAB_SIMULATION_WORKER must name an existing absolute worker path")
        elif not worker.is_file():
            if require_gpu:
                self.fail("Set LAB_SIMULATION_WORKER to the deployed GPU worker")
            self.skipTest("build the native pipe worker first")
        command = [sys.executable, str(RELAY)]
        if require_gpu:
            command.append("--require-gpu")
        command.append(str(worker))
        # Preserve the historical smoke allocation and the matched GPU
        # fixture. The low-rate CPU test uses the full available allocation.
        available = sorted(os.sched_getaffinity(0))
        def restrict():
            os.sched_setaffinity(0, available[:4])
        proc = subprocess.Popen(command, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, bufsize=0,
                                preexec_fn=restrict if check_replay or require_gpu else None)
        snapshot = None
        end = time.monotonic() + 120

        def simulation_status():
            for item in (snapshot or {}).get("controls", ()):
                if "s modeled RX" in item["text"]:
                    return item["text"]
            return "no simulation progress shown"

        def exact(size):
            out = bytearray()
            while len(out) < size:
                remaining = end - time.monotonic()
                if remaining <= 0 or not select.select([proc.stdout], [], [], remaining)[0]:
                    self.fail(f"simulation fixture deadline reached ({simulation_status()})")
                part = os.read(proc.stdout.fileno(), size - len(out))
                self.assertTrue(part, "relay output closed")
                out.extend(part)
            return bytes(out)

        def update():
            nonlocal snapshot
            header = exact(12)
            magic, kind, length = struct.unpack("<4sII", header)
            self.assertEqual(magic, b"DPW1")
            self.assertLessEqual(length, 8 * 1024 * 1024)
            payload = exact(length)
            if kind == 101:
                snapshot = json.loads(payload)
            elif kind == 103:
                raise AssertionError("worker error: " + payload[4:].decode("utf-8", "replace"))

        def wait(predicate):
            while not predicate():
                update()

        def control(label):
            return next(item for item in snapshot["controls"] if item["label"].split(" · Budget target ", 1)[0] == label)

        def event(kind, target, value):
            sequence = int(snapshot["ack"]) + 1
            payload = struct.pack("<IQQQIII", 1, int(snapshot["generation"]), sequence,
                                  int(target), kind, 0, 0) + string(value) + string("")
            proc.stdin.write(frame(2, payload))
            wait(lambda: int(snapshot["ack"]) >= sequence)

        try:
            wait(lambda: snapshot is not None)
            wait(lambda: any(item.get("text") == "Robust simulation ready" for item in snapshot["controls"]))
            self.assertFalse(control("Replay sim")["enabled"])
            # The canonical GUI resolves these exact normal control inputs.
            # Strong link, no Helper/FHSS/spreading or selected channel effects.
            for label, value in (("Transmit power", "100 mW"), ("Path loss", "114 dB"),
                                 ("Noise (dBm/Hz)", "-174 dBm/Hz"), ("Clock sync", "1ms"),
                                 ("Region", "1ms"), ("Offset", "0ms"), ("Audio error", "0ms"),
                                 ("Margin", "1x"), ("Rate", rate), ("Carrier", "1725 Hz"),
                                 ("Target SNR (dB-Hz)", "80")):
                event(1, control(label)["id"], value)
            for label, value in (("Baseband Osc", base_oscillator), ("Shift Osc", "gpsdo-ocxo")):
                event(2, control(label)["id"], value)
            self.assertEqual(control("Rate")["text"], rate)
            self.assertEqual(control("Baseband Osc")["selected"], base_oscillator)
            self.assertIn("Budget target 80", control("Target SNR (dB-Hz)")["label"])
            message = next(item for item in snapshot["controls"]
                           if item["kind"] == "text" and item.get("byteLimit") == 4 * 1024 * 1024)
            event(1, message["id"], "e")
            wait(lambda: control("Transmit")["enabled"])
            event(4, control("Transmit")["id"], "")
            def replaying():
                return any("· replaying 3 seconds" in item.get("text", "") for item in snapshot["controls"])

            def preview_visible():
                return "dB relative to modeled noise" in control("Spectrum").get("caption", "")

            wait(lambda: preview_visible() or replaying() or not control("Cancel TX")["enabled"])
            self.assertTrue(preview_visible(), "simulated samples never reached the waterfall")
            self.assertFalse(replaying(), "the first waterfall update should precede playback")
            self.assertTrue(control("Cancel TX")["enabled"], "the preview should arrive during simulation")
            self.assertTrue(any("s compute" in item.get("text", "") and
                                "s modeled RX" in item.get("text", "") for item in snapshot["controls"]))
            wait(lambda: bool(control("Pattern steps").get("records")) or not control("Cancel TX")["enabled"])
            wait(lambda: not control("Cancel TX")["enabled"])
            steps = control("Pattern steps").get("records", [])
            signals = control("Signals").get("records", [])
            self.assertEqual(len(steps), 5, f"unexpected scored slots: {steps!r}")
            descriptions = [" ".join(cell.get("text", "") for cell in row["cells"]) for row in steps]
            for index, decision in enumerate(("DATA 0", "DATA 0", "DATA 1", "QUIET", "QUIET")):
                self.assertIn(f"Slot {index} ·", descriptions[index])
                self.assertIn(f"· {decision} ·", descriptions[index])
                self.assertNotIn("incomplete evidence", descriptions[index])
            for description in descriptions[-2:]:
                self.assertRegex(description, r" · quiet [^·]+ \([1-9]\d* modes\)")
            rendered = [[cell.get("text", "") for cell in signal["cells"]] for signal in signals]
            self.assertTrue(any("decoded" in cells and "e" in cells for cells in rendered),
                            f"expected decoded message missing: {rendered!r}")
            if require_gpu:
                diagnostics = "\n".join(item["text"] for item in snapshot["controls"]
                                        if item["kind"] == "label" and "GPU" in item["text"])
                self.assertRegex(diagnostics, r"\bGPU search(?: \+ CPU tracking)? · [1-9]\d* rows\b",
                                 "no executed GPU search rows in worker diagnostics")
                print(diagnostics, flush=True)
            if not check_replay:
                print(f"{rate} {base_oscillator}: {simulation_status()}", flush=True)
                return
            for label in ("Spectrum", "Waveform", "Receiver input I/Q", "Pattern evidence"):
                self.assertEqual(control(label)["kind"], "bitmap", label)
            pattern = control("Pattern evidence")
            self.assertFalse(pattern["click"],
                             "Pattern evidence should survive an ordinary click")
            self.assertEqual(pattern["geometry"]["widget"]["h"], pattern["geometry"]["frame"]["h"] - 30)
            self.assertNotIn("\n", pattern["caption"], "pattern caption exceeds the visible row")
            event(2, control("Wrong Key Admission")["id"], "guarded")
            wait(lambda: "Browser filter markers" in control("Pattern evidence")["caption"])
            event(2, control("Wrong Key Admission")["id"], "current")
            wait(lambda: "Browser filter markers" not in control("Pattern evidence")["caption"])
            self.assertTrue(control("Replay sim")["enabled"])
            event(4, control("Clear waterfall")["id"], "")
            event(4, control("Replay sim")["id"], "")
            self.assertFalse(control("Replay sim")["enabled"])
            wait(lambda: "Replay · 1/5 slots" in control("Pattern evidence")["caption"] or
                 control("Replay sim")["enabled"])
            self.assertIn("Replay · 1/5 slots", control("Pattern evidence")["caption"])
            self.assertEqual(control("Pattern steps")["records"], steps)
            self.assertEqual(control("Signals")["records"], signals)
            wait(lambda: control("Replay sim")["enabled"])
            self.assertEqual(control("Pattern steps")["records"], steps)
            self.assertEqual(control("Signals")["records"], signals)
            spectrum = control("Spectrum")
            bitmap = spectrum["bitmap"]
            width, height = bitmap["width"], bitmap["height"]
            if "rgbRuns" in bitmap:
                runs = base64.b64decode(bitmap["rgbRuns"])
                pixels = b"".join(runs[i+1:i+4] * (runs[i]+1) for i in range(0, len(runs), 4))
            else:
                pixels = base64.b64decode(bitmap["rgb"])
            self.assertEqual(len(pixels), width * height * 3)

            def peak(first_hz, last_hz):
                first, last = (hz * width // 3000 * 3 for hz in (first_hz, last_hz))
                return max(max(pixels[y*width*3+first:y*width*3+last]) for y in range(height))

            occupied, noise = peak(1575, 1875), peak(300, 1000)
            self.assertGreater(noise, 0, "The modeled noise floor was entirely black")
            self.assertGreater(occupied, noise + 40, "The retained waterfall lost its occupied band")
            self.assertIn("dB relative to modeled noise", spectrum["caption"])
        finally:
            proc.stdin.close()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
            proc.stdout.close()


if __name__ == "__main__":
    unittest.main()
