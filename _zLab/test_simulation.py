#!/usr/bin/env python3
"""Opt-in, bounded real native simulation through the lab's pipe relay.

This is a functional one-message CPU stratum, not a GPU, capacity, or
sensitivity qualification. Run with LAB_RUN_SIMULATION=1; it can take 90 s.
"""
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
WORKER = ROOT / "build/dev-cli-web/datapump-worker"
RELAY = ROOT / "_zLab/worker_relay.py"


def frame(kind, payload=b""):
    return b"DPW1" + struct.pack("<II", kind, len(payload)) + payload


def string(value):
    data = value.encode("utf-8")
    return struct.pack("<I", len(data)) + data


class SimulationTest(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("LAB_RUN_SIMULATION") == "1", "explicit opt-in for bounded DSP work")
    @unittest.skipUnless(WORKER.is_file(), "build the native pipe worker first")
    def test_complete_message_via_relay(self):
        # Restrict the fixture to at most four available hardware threads. The
        # native receiver still resolves its normal 2*H software-worker budget.
        available = sorted(os.sched_getaffinity(0))
        def restrict():
            os.sched_setaffinity(0, available[:4])
        proc = subprocess.Popen([sys.executable, str(RELAY), str(WORKER)], stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0,
                                preexec_fn=restrict)
        snapshot = None
        end = time.monotonic() + 120

        def exact(size):
            out = bytearray()
            while len(out) < size:
                remaining = end - time.monotonic()
                self.assertGreater(remaining, 0, "simulation fixture deadline")
                self.assertTrue(select.select([proc.stdout], [], [], remaining)[0], "worker output stalled")
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
            return next(item for item in snapshot["controls"] if item["label"] == label)

        def event(kind, target, value):
            sequence = int(snapshot["ack"]) + 1
            payload = struct.pack("<IQQQIII", 1, int(snapshot["generation"]), sequence,
                                  int(target), kind, 0, 0) + string(value) + string("")
            proc.stdin.write(frame(2, payload))
            wait(lambda: int(snapshot["ack"]) >= sequence)

        try:
            wait(lambda: snapshot is not None)
            wait(lambda: any(item.get("text") == "Robust simulation ready" for item in snapshot["controls"]))
            # The canonical GUI resolves these exact normal control inputs.
            # Strong link, no Helper/FHSS/spreading or selected channel effects.
            for label, value in (("Transmit power", "100 mW"), ("Path loss", "114 dB"),
                                 ("Noise (dBm/Hz)", "-174 dBm/Hz"), ("Clock sync", "1ms"),
                                 ("Region", "1ms"), ("Offset", "0ms"), ("Audio error", "0ms"),
                                 ("Margin", "1x"), ("Rate", "120 Hz"),
                                 ("Target SNR (dB-Hz)", "80")):
                event(1, control(label)["id"], value)
            for label, value in (("Baseband Osc", "gpsdo-ocxo"), ("Shift Osc", "gpsdo-ocxo")):
                event(2, control(label)["id"], value)
            message = next(item for item in snapshot["controls"]
                           if item["kind"] == "text" and item.get("byteLimit") == 4 * 1024 * 1024)
            event(1, message["id"], "e")
            wait(lambda: control("Transmit")["enabled"])
            event(4, control("Transmit")["id"], "")
            wait(lambda: bool(control("Pattern steps").get("records")) or not control("Cancel TX")["enabled"])
            wait(lambda: not control("Cancel TX")["enabled"])
            steps = control("Pattern steps").get("records", [])
            signals = control("Signals").get("records", [])
            self.assertEqual(len(steps), 5, f"unexpected scored slots: {steps!r}")
            descriptions = [" ".join(cell.get("text", "") for cell in row["cells"]) for row in steps]
            for index, decision in enumerate(("DATA 0", "DATA 0", "DATA 1", "QUIET", "QUIET")):
                self.assertIn(f"Slot {index} ·", descriptions[index])
                self.assertIn(f"· {decision} ·", descriptions[index])
            rendered = [[cell.get("text", "") for cell in signal["cells"]] for signal in signals]
            self.assertTrue(any("decoded" in cells and "e" in cells for cells in rendered),
                            f"expected decoded message missing: {rendered!r}")
            for label in ("Spectrum", "Waveform", "Receiver input I/Q", "Pattern evidence"):
                self.assertEqual(control(label)["kind"], "bitmap", label)
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
            proc.stderr.close()


if __name__ == "__main__":
    unittest.main()
