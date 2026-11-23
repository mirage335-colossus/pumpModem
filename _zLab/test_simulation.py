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
import re
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

        def bitmap_pixels(bitmap):
            if "rgbRuns" in bitmap:
                runs = base64.b64decode(bitmap["rgbRuns"])
                pixels = b"".join(runs[i+1:i+4] * (runs[i]+1) for i in range(0, len(runs), 4))
            else:
                pixels = base64.b64decode(bitmap["rgb"])
            self.assertEqual(len(pixels), bitmap["width"] * bitmap["height"] * 3)
            return pixels

        def fullscreen_spectrum():
            return next(item for item in snapshot["overlay"] if item["kind"] == "bitmap")

        def amplitude_coverage(bitmap):
            width, height = bitmap["width"], bitmap["height"]
            self.assertGreaterEqual(width, 64, "fullscreen spectrum should use paired columns")
            pixels = bitmap_pixels(bitmap)
            # Newest rows are at the bottom. Count the continuous observed
            # suffix using amplitude (even columns), independently of phase.
            distinct = set()
            count = 0
            row_bytes = width * 3
            for end in range(len(pixels), 0, -row_bytes):
                begin = end - row_bytes
                row = (pixels[begin:end:6], pixels[begin+1:end:6], pixels[begin+2:end:6])
                if not any(any(channel) for channel in row):
                    break
                count += 1
                distinct.add(row)
            return count, len(distinct)

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

            def overview_span():
                caption = control("Spectrum").get("caption", "")
                interval = re.search(r"(\d+(?:\.\d+)?)([/–])(\d+(?:\.\d+)?) modeled s", caption)
                if not interval:
                    return 0.0, 0.0
                first, last = float(interval[1]), float(interval[3])
                return (0.0, first) if interval[2] == "/" else (first, last)

            def preview_visible():
                return ("dB relative to modeled noise" in control("Spectrum").get("caption", "") or
                        overview_span()[1] > 0.0)

            wait(lambda: preview_visible() or replaying() or not control("Cancel TX")["enabled"])
            self.assertTrue(preview_visible(), "simulated samples never reached the waterfall")
            self.assertFalse(replaying(), "the first waterfall update should precede playback")
            self.assertTrue(control("Cancel TX")["enabled"], "the preview should arrive during simulation")
            self.assertTrue(any("s compute" in item.get("text", "") and
                                "s modeled RX" in item.get("text", "") for item in snapshot["controls"]))
            if rate == "3.6 Hz":
                wait(lambda: overview_span()[1] >= 7.2 or replaying() or not control("Cancel TX")["enabled"])
                self.assertGreaterEqual(overview_span()[1], 7.2)
                self.assertFalse(replaying(), "expected the fullscreen spectrum during capture")
                self.assertTrue(control("Cancel TX")["enabled"])
                spectrum = control("Spectrum")
                self.assertIn("overview", spectrum["caption"])
                event(8, spectrum["id"], "")
                def transmitting():
                    return any(item["text"] == "Robust simulation" for item in snapshot["controls"])

                detail = fullscreen_spectrum()
                bitmap = detail["bitmap"]
                count, distinct = amplitude_coverage(bitmap)
                if count * 10 < bitmap["height"] * 9:
                    # The open event can be acknowledged before the next
                    # capture reaches the newly visible overlay.
                    revision = bitmap["revision"]
                    wait(lambda: fullscreen_spectrum()["bitmap"]["revision"] != revision or
                         replaying() or not transmitting())
                    detail = fullscreen_spectrum()
                    bitmap = detail["bitmap"]
                    count, distinct = amplitude_coverage(bitmap)
                self.assertFalse(replaying())
                self.assertTrue(transmitting())
                self.assertIn("Last 7.2 modeled s", detail["caption"].splitlines()[0])
                self.assertGreaterEqual(count * 10, bitmap["height"] * 9,
                                        f"fullscreen waterfall has only {count}/{bitmap['height']} "
                                        "contiguous observed amplitude rows")
                self.assertGreaterEqual(distinct, 8, "fullscreen history repeats too few observed FFT rows")

                def fullscreen_end():
                    caption = fullscreen_spectrum()["caption"].splitlines()[0]
                    value = re.search(r"end (\d+\.\d+) modeled s", caption)
                    return float(value[1]) if value else 0.0

                first_end = fullscreen_end()
                self.assertGreater(first_end, 0.0)
                wait(lambda: fullscreen_end() > first_end or replaying() or not transmitting())
                second_end = fullscreen_end()
                self.assertGreater(second_end, first_end)
                self.assertFalse(replaying())
                self.assertNotEqual(fullscreen_spectrum()["bitmap"]["revision"], bitmap["revision"])
                event(8, fullscreen_spectrum()["id"], "")

                def capture_finished():
                    return replaying() or not control("Cancel TX")["enabled"]

                wait(lambda: overview_span()[0] > 0.0 or capture_finished())
                start, stop = overview_span()
                self.assertGreater(start, 0.0, "compact waterfall never scrolled past the first DATA bit")
                self.assertFalse(capture_finished(), "expected the compact waterfall to scroll during capture")
                scrolled_bitmap = control("Spectrum")["bitmap"]
                wait(lambda: overview_span()[0] > start or capture_finished())
                later_start, later_stop = overview_span()
                self.assertGreater(later_start, start)
                self.assertAlmostEqual(later_stop - later_start, stop - start, delta=0.2)
                later_bitmap = control("Spectrum")["bitmap"]
                self.assertNotEqual(later_bitmap["revision"], scrolled_bitmap["revision"])
                self.assertNotEqual(bitmap_pixels(later_bitmap), bitmap_pixels(scrolled_bitmap),
                                    "compact waterfall advanced its label without advancing the image")
                print(f"Fullscreen ends at {first_end:.3f} and {second_end:.3f} modeled s. "
                      f"Compact range advances from {start:.1f}–{stop:.1f} to "
                      f"{later_start:.1f}–{later_stop:.1f} modeled s.", flush=True)
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
                final_span = overview_span()
                self.assertGreater(final_span[0], 0.0)
                event(4, control("Clear waterfall")["id"], "")
                wait(lambda: not any(bitmap_pixels(control("Spectrum")["bitmap"])))
                event(4, control("Replay sim")["id"], "")
                wait(replaying)
                self.assertEqual(overview_span(), final_span)
                self.assertFalse(any(bitmap_pixels(control("Spectrum")["bitmap"])),
                                 "replay restored cleared compact waterfall rows")
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
            pixels = bitmap_pixels(bitmap)

            def peak(first_hz, last_hz):
                first, last = (hz * width // 3000 * 3 for hz in (first_hz, last_hz))
                return max(max(pixels[y*width*3+first:y*width*3+last]) for y in range(height))

            occupied, noise = peak(1575, 1875), peak(300, 1000)
            self.assertGreater(noise, 0, "The modeled noise floor was entirely black")
            self.assertGreater(occupied, noise + 40, "The retained waterfall lost its occupied band")
            self.assertIn("dB relative to modeled noise", spectrum["caption"])

            event(8, spectrum["id"], "")
            wait(lambda: amplitude_coverage(fullscreen_spectrum()["bitmap"])[0] > 0)
            detail = fullscreen_spectrum()
            replay_rows, _ = amplitude_coverage(detail["bitmap"])
            self.assertLess(replay_rows * 2, detail["bitmap"]["height"],
                            "fullscreen replay filled unobserved leading time")
            event(8, detail["id"], "")
            event(4, control("Clear waterfall")["id"], "")
            event(8, control("Spectrum")["id"], "")
            detail = fullscreen_spectrum()
            if any(bitmap_pixels(detail["bitmap"])):
                revision = detail["bitmap"]["revision"]
                wait(lambda: fullscreen_spectrum()["bitmap"]["revision"] != revision)
                detail = fullscreen_spectrum()
            self.assertFalse(any(bitmap_pixels(detail["bitmap"])),
                             "opening fullscreen restored cleared replay samples")
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
