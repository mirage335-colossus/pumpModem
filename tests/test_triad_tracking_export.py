#!/usr/bin/env python3
"""Bounded native exporter protocol tests, not weak-signal qualification.

Run with a built triad_tracking_export path. The tests exercise real native
waveforms/channels, keep sample counts small, and never run a historical campaign.
"""
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def main(binary):
    checks = 0
    with tempfile.TemporaryDirectory(prefix="triad-tracking-export-") as temp:
        root = Path(temp)

        def run(name, *extra, ok=True):
            nonlocal checks
            destination = root / name
            command = [binary, "--output", str(destination), "--rate", "12", "--added", "1",
                       "--sample-rate", "16", "--seconds", "32", "--clock-profile", "off",
                       "--template-ppm", "0", "--seed", "9321", "--key-seed", "7182", *extra]
            result = subprocess.run(command, text=True, capture_output=True, timeout=25)
            if ok:
                assert result.returncode == 0, (command, result.stderr)
                report = json.loads((destination / "manifest.json").read_text())
                assert report["schema"] == "triad-tracking-export-v1"
                assert report["dtype"] == "<c8"
                assert not report["channel_noise_enabled"]
                assert report["transmit_gain"] == 1
                for item in report["files"]:
                    assert (destination / item["path"]).stat().st_size == report["samples"] * 8
                checks += 1
                return destination, report
            assert result.returncode != 0, command
            checks += 1
            return result

        def samples(path):
            return [complex(a, b) for a, b in struct.iter_unpack("<ff", path.read_bytes())]

        def difference(a, b):
            return math.sqrt(sum(abs(x-y)**2 for x, y in zip(a, b)) / max(1e-300, sum(abs(x)**2 for x in a)))

        def cubic(values, position):
            center = math.floor(position)
            value = 0j
            for index in range(center-1, center+3):
                weight = 1.
                for other in range(center-1, center+3):
                    if index != other:
                        weight *= (position-other)/(index-other)
                value += values[index]*weight
            return value

        flat, fm = run("flat", "--raw-sources", "--chunk", "17")
        other, om = run("flat-other-chunk", "--raw-sources", "--chunk", "131")
        assert fm["samples"] == om["samples"]
        for item in fm["files"]:
            assert (flat/item["path"]).read_bytes() == (other/item["path"]).read_bytes()
        for bit in (0, 1):
            unwarped = (flat/f"source_unwarped_bit{bit}.c64").read_bytes()
            assert unwarped == (flat/f"source_warped_bit{bit}.c64").read_bytes()
            assert unwarped == (flat/f"basis_bit{bit}_ppm0.c64").read_bytes()
            assert difference(samples(flat/f"source_unwarped_bit{bit}.c64"), samples(flat/f"signal_bit{bit}.c64")) < 1e-6
        assert (flat/"basis_bit0_ppm0.c64").read_bytes() != (flat/"basis_bit1_ppm0.c64").read_bytes()
        assert all(row["out_of_passband_samples"] == 0 for row in fm["channel_reports"])

        # Gaussian fields, moving RF phase and dispersive memory all run in
        # the shared full-waveform channel. Chunk boundaries cannot reset them.
        stochastic = ["--spread", "sub9worst", "--random-doppler-width-hz", ".02",
                      "--delay-excursion-ms", "1", "--delay-knot-seconds", "1000",
                      "--shift", "8270", "--selective-each-depth", ".65"]
        random_a, rm = run("random-a", *stochastic, "--chunk", "19")
        random_b, _ = run("random-b", *stochastic, "--chunk", "113")
        for bit in (0, 1):
            name = f"signal_bit{bit}.c64"
            assert (random_a/name).read_bytes() == (random_b/name).read_bytes()
            assert difference(samples(random_a/name), samples(flat/name)) > .05
        assert all("pre_selective_energy_samples" in r for r in rm["channel_reports"])
        bypass, bm = run("zero-rate-selective", "--selective-each-depth", ".65",
                         "--selective-modulation-rate-hz", "0")
        assert (bypass/"signal_bit0.c64").read_bytes() == (flat/"signal_bit0.c64").read_bytes()
        assert all("pre_selective_energy_samples" not in r for r in bm["channel_reports"])

        reciprocal, rec = run("reciprocal", "--rate", ".001", "--sample-rate", "1",
                              "--sample-rate-denominator", "64", "--seconds", "1000")
        assert rec["sample_rate_hz"] == 1/64 and rec["samples"] < 20000
        assert rec["requested_bit_seconds"] == 1000
        assert rec["bit_seconds"] >= 64*6/.001  # minimum pattern complexity is retained
        assert (reciprocal/"signal_bit0.c64").stat().st_size == rec["samples"]*8

        # Native controller updates do not follow export Fs. At common times,
        # both the private clock map and complete waveform must agree.
        common = ["--rate", ".03", "--seconds", "20000", "--clock-profile", "wandering",
                  "--data-if", "1725", "--source-window", "4000", "16"]
        low, lm = run("window-low", *common, "--sample-rate", "4")
        high, hm = run("window-high", *common, "--sample-rate", "8")
        lo_map = [x[0] for x in struct.iter_unpack("<d", (low/"oracle_source_time.f64").read_bytes())]
        hi_map = [x[0] for x in struct.iter_unpack("<d", (high/"oracle_source_time.f64").read_bytes())]
        assert lo_map == hi_map[::2]
        assert max(abs(u-(4000+i/4)) for i, u in enumerate(lo_map)) > 1e-4
        assert lm["mode"] == hm["mode"] == "source-window-no-channel"
        assert not lm["channel_reports"] and lm["clock_controller_tick_seconds"] == 1
        # The diagnostic driver removes native Fs-dependent padding before
        # propagation. Compare identical physical time, without a fitted shift.
        assert lm["source_calendar"] == hm["source_calendar"] == "canonical-8chips-v1"
        calendar_shift = hm["canonical_payload_origin_seconds"]-lm["canonical_payload_origin_seconds"]
        assert calendar_shift == 0
        for bit in (0, 1):
            high_raw = samples(high/f"source_unwarped_bit{bit}.c64")
            low_warped = samples(low/f"source_warped_bit{bit}.c64")
            assert difference(low_warped, samples(high/f"source_warped_bit{bit}.c64")[::2]) < 2e-6
            actual, expected = [], []
            for i, u in enumerate(lo_map):
                position = (u+calendar_shift-4000)*8
                if 1 <= position < len(high_raw)-2:
                    phase = 2*math.pi*math.remainder(1725*(u-(4000+i/4)), 1)
                    expected.append(cubic(high_raw, position)*complex(math.cos(phase), math.sin(phase)))
                    actual.append(low_warped[i])
            assert len(actual) >= len(lo_map)-4
            assert difference(actual, expected) < 2e-6
            assert difference(samples(low/f"source_warped_bit{bit}.c64"), samples(low/f"source_unwarped_bit{bit}.c64")) > .01
        assert 0 < lm["oracle_clock_max_applied_rate_ppm"] <= 200.000001

        # Public basis construction is independent of the private clock seed.
        changed, _ = run("window-seed", *common, "--sample-rate", "4", "--seed", "9322")
        assert (low/"basis_bit0_ppm0.c64").read_bytes() == (changed/"basis_bit0_ppm0.c64").read_bytes()
        assert (low/"source_warped_bit0.c64").read_bytes() != (changed/"source_warped_bit0.c64").read_bytes()
        affine, _ = run("window-affine", *common, "--sample-rate", "4", "--template-ppm", "0,100")
        assert (affine/"basis_bit0_ppm0.c64").read_bytes() != (affine/"basis_bit0_ppm1.c64").read_bytes()

        # Persistent exact-native basis service: same public affine files,
        # different request chunking, and an independently checked delay/IF.
        server_export, sm = run("server-affine", "--template-ppm", "-100,0,100")
        server_command = [binary, "--basis-server", "--clock-profile", "off", "--rate", "12",
                          "--added", "1", "--sample-rate", "16", "--seconds", "32", "--key-seed", "7182"]

        def server(requests, *, ok=True, extra=()):
            result = subprocess.run([*server_command, *extra], input=requests.encode(), capture_output=True, timeout=25)
            if not ok:
                assert result.returncode != 0, (requests, result.stdout)
                assert b"\nok " not in result.stdout, "invalid request wrote a response header"
                return result
            assert result.returncode == 0, result.stderr.decode()
            startup, separator, remainder = result.stdout.partition(b"\n")
            assert separator
            metadata = json.loads(startup)
            assert metadata["schema"] == "triad-tracking-basis-server-v1"
            assert metadata["layout"] == "bit,state,sample"
            assert metadata["source_calendar"] == "canonical-8chips-v1"
            outputs = []
            while remainder:
                header, separator, body = remainder.partition(b"\n")
                assert separator and header.startswith(b"ok ")
                length = int(header[3:])
                assert len(body) >= length
                outputs.append(body[:length])
                remainder = body[length:]
            return metadata, outputs

        _, responses = server(f"basis 0 {sm['samples']} 3 0 -0 -100 0 0 0 100\n"
                              "basis 37 47 3 0 0 -100 0 0 0 100\n"
                              "basis 37 47 1 10 .001 100\n"
                              "basis 37 47 1 0 .125 0\nquit\n")
        assert len(responses) == 4
        for bit in (0, 1):
            for state in range(3):
                expected = samples(server_export/f"basis_bit{bit}_ppm{state}.c64")
                begin = (bit*3+state)*sm["samples"]*8
                actual = [complex(*p) for p in struct.iter_unpack("<ff", responses[0][begin:begin+sm["samples"]*8])]
                assert difference(expected, actual) < 2e-6
                chunk_begin = (bit*3+state)*47*8
                assert responses[0][begin+37*8:begin+84*8] == responses[1][chunk_begin:chunk_begin+47*8]
            anchored = [complex(*p) for p in struct.iter_unpack("<ff", responses[2][bit*47*8:(bit+1)*47*8])]
            assert difference(samples(server_export/f"basis_bit{bit}_ppm2.c64")[37:84], anchored) < 2e-6
            delayed = [complex(*p) for p in struct.iter_unpack("<ff", responses[3][bit*47*8:(bit+1)*47*8])]
            rotation = complex(math.cos(2*math.pi*6*.125), math.sin(2*math.pi*6*.125))
            expected = [x*rotation for x in samples(server_export/f"basis_bit{bit}_ppm1.c64")[39:86]]
            assert difference(expected, delayed) < 2e-6
        for request in ("basis 0 0 1 0 0 0\n", "basis 0 16384 256 0 0 0\n",
                        "basis 0 1 1 0 nan 0\n", "basis 0 1 1 0 0 1001\n",
                        "basis 9223372036854775808 1 1 0 0 0\n",
                        "basis 0 1 1 0 0 0 trailing\n", "basis 0 1 1 0 0\n",
                        "basis 0 1 1 0 1e20 0\n", "basis 0 1 1 0 0 0"):
            server(request, ok=False)
        server("quit\n", ok=False, extra=("--clock-profile", "periodic"))
        server("quit\n", ok=False, extra=("--spread", "sub9best"))
        _, empty = server("")
        assert not empty

        rejected = run("cap", "--max-output-bytes", "1", ok=False)
        assert "max-output-bytes" in rejected.stderr and not (root/"cap").exists()
        run("bad-fs", "--sample-rate", "1", "--sample-rate-denominator", "0", ok=False)
        run("bad-combination", "--sample-rate", "2", "--sample-rate-denominator", "4", ok=False)
        run("unknown-effect", "--white-fm", ".01", ok=False)
        run("bad-window", *common, "--spread", "sub9best", ok=False)
        run("bad-physical-band", "--rate", ".001", "--sample-rate", "1", "--clock-profile", "periodic",
            "--data-if", "8270", ok=False)
        run("bad-depth", "--selective-each-depth", "1", ok=False)
        run("bad-ppm", "--template-ppm", "1001", ok=False)
        run("bad-bit", "--bit", "7", ok=False)
    print(json.dumps({"native_export_checks": checks, "basis_server_protocol_checked": True, "status": "passed",
                      "scope": "small native source/channel/controller protocol; no reception claim"}))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_triad_tracking_export.py /path/to/triad_tracking_export")
    main(str(Path(sys.argv[1]).resolve()))
