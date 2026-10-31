#!/usr/bin/env python3
"""Deterministic real-mono Helper cable simulation, no hardware required.

The probe is never used to choose timing, carrier, gain or phase.  The same
``--analyze-long`` path also accepts an actual recorded capture.  Numerical
checks are tolerances for this particular channel model, not a claim that it
reproduces arbitrary physical cables or the exact noise in a saved recording.
"""

import array
import cmath
import hashlib
import math
import pathlib
import struct
import subprocess
import sys
import tempfile


def invoke(tool, *args, success=True):
    run = subprocess.run([str(tool), *map(str, args)], text=True,
                         capture_output=True, timeout=60, check=False)
    if (run.returncode == 0) != success:
        raise AssertionError(f"unexpected exit {run.returncode}: {run.args}\n{run.stdout}\n{run.stderr}")
    return run


def fields(text):
    return dict(item.split("=", 1) for item in text.split() if "=" in item)


def digest(path):
    checksum = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            checksum.update(chunk)
    return checksum.hexdigest()


def pcm(path):
    values = array.array("f")
    values.frombytes(path.read_bytes())
    if sys.byteorder != "little":
        values.byteswap()
    return values


def phasor(values, frequency, begin=2., duration=.5, rate=48000):
    """Fixed Helper-bin metrology, never a receiver correction input."""
    first, count = round(begin * rate), round(duration * rate)
    step = cmath.exp(-2j * math.pi * frequency / rate)
    phase = cmath.exp(-2j * math.pi * frequency * first / rate)
    total, weight = 0j, 0.
    for i in range(count):
        window = .5 - .5 * math.cos(2 * math.pi * i / (count - 1))
        total += values[first + i] * window * phase
        weight += window
        phase *= step
    return 2 * total / weight


def check_helper_transfer(stimulus, capture):
    """Validate the independently specified pre-probe gain/filter/delay."""
    source, received = pcm(stimulus), pcm(capture)
    gain, cutoff, latency = .84150, 11.34796, .75 + .0350795
    pole = math.exp(-2 * math.pi * cutoff / 48000)
    for frequency in (550 * 1.0001 + 4, 2400 * 1.0001 + 4):
        observed = phasor(received, frequency) / phasor(source, frequency)
        z = cmath.exp(-2j * math.pi * frequency / 48000)
        expected = gain * .5 * (1 + pole) * (1 - z) / (1 - pole * z) * \
            cmath.exp(-2j * math.pi * frequency * latency)
        # Cubic fractional delay is an approximation. This tolerance remains
        # well below the attenuation error of the rejected linear interpolator.
        require(abs(abs(observed / expected) - 1) < .002,
                "pre-probe Helper gain/filter transfer changed")
        require(abs(cmath.phase(observed / expected)) < .002,
                "pre-probe Helper phase/delay transfer changed")


class StableNoise:
    """Independent reference for the documented fixed integer noise stream."""
    def __init__(self, seed):
        self.state = seed

    def sample(self):
        mask, total = (1 << 64) - 1, 0.
        for _ in range(12):
            self.state ^= self.state >> 12
            self.state ^= (self.state << 25) & mask
            self.state ^= self.state >> 27
            self.state &= mask
            word = self.state * 2685821657736338717 & mask
            total += (word >> 40) / 16777216.
        return total - 6.


def check_physical_rate(tool, root, stimulus):
    # A real analog time constant must be sampled at the physical ADC rate,
    # not the file's nominal rate. A known constant source isolates HP decay;
    # a seeded, unquantized noise-only stream independently checks OU evolution.
    constant = root / "constant.f32"
    constant.write_bytes(struct.pack("<f", .25) * (40 * 48000))
    hp = root / "physical-rate-highpass.f32"
    row = fields(invoke(tool, "--simulate-long", constant, hp, "--gain", "1",
                        "--delay-ms", "0", "--highpass-hz", "100", "--noise-rms", "0",
                        "--quantization-bits", "0", "--adc-ppm", "1000").stdout)
    physical_rate = 48000 * 1.001
    require(abs(float(row["physical_sample_rate_hz"]) - physical_rate) < 1e-6,
            "wrong physical ADC rate")
    values = pcm(hp)
    index = math.ceil(.75 * physical_rate) + 32
    require(abs(values[index + 1] / values[index] - math.exp(-2 * math.pi * 100 / physical_rate)) < 5e-7,
            "high-pass time constant ignored the physical ADC clock")

    colored = root / "physical-rate-colored.f32"
    invoke(tool, "--simulate-long", stimulus, colored, "--gain", "0", "--noise-rms", "0",
           "--colored-noise-rms", ".001", "--colored-noise-tau-ms", "2",
           "--quantization-bits", "0", "--seed", "12345", "--adc-ppm", "-1000")
    values = pcm(colored)
    physical_rate = 48000 * .999
    pole = math.exp(-1 / (physical_rate * .002))
    innovation = .001 * math.sqrt(1 - pole * pole)
    rng = StableNoise(12345)
    expected = .001 * rng.sample()
    for observed in values[:1024]:
        expected = pole * expected + innovation * rng.sample()
        require(abs(observed - expected) < max(1e-10, 1e-6 * abs(expected)),
                "colored-noise time constant ignored the physical ADC clock")


def check_calibrated_noise(tool, root, stimulus):
    capture = root / "calibrated-noise-only.f32"
    row = fields(invoke(tool, "--simulate-long", stimulus, capture,
                        "--preset", "cable-20261030", "--gain", "0").stdout)
    measured = .00004630874542665937
    quantum = 1 / 32768
    inferred = math.sqrt(max(0., measured * measured - quantum * quantum / 12))
    require(row.get("noise_rms_basis") == "inferred_uniform_uncorrelated_s16_quantization",
            "calibrated quantization assumption was not identified")
    require(abs(float(row["noise_rms"]) - inferred) < 1e-12,
            "calibrated white noise double-counts the assumed quantizer variance")
    require(abs(float(row["measured_white_rms_including_quantization"]) - measured) < 1e-12,
            "measured post-ADC white reference changed")
    rejected = invoke(tool, "--analyze-long", capture, success=False)
    require("Helper-only coarse onset unavailable" in rejected.stderr,
            "calibrated colored noise falsely acquired a Helper")

    values = pcm(capture)
    mean = sum(values) / len(values)
    variance = sum((x - mean) ** 2 for x in values) / len(values)
    adjacent = sum((values[i] - mean) * (values[i + 1] - mean)
                   for i in range(len(values) - 1)) / (len(values) - 1)
    pole = math.exp(-1 / (48000 * .0714))
    colored_variance = adjacent / pole
    white = math.sqrt(max(0., variance - colored_variance))
    # Forward validation of this declared quantizer/noise approximation, not
    # proof that the recording uniquely decomposes into independent noises.
    require(abs(white / measured - 1) < .012,
            "forward quantized effective white variance does not match its reference")
    require(abs(math.sqrt(max(0., colored_variance)) / .000084666 - 1) < .2,
            "calibrated colored-noise stationary variance changed")
    require(abs(mean - (-.0000828714)) < .00003, "calibrated preroll DC changed")
    print(f"calibrated-noise: mean={mean:.9g} white_after_quantization={white:.9g} "
          f"colored_rms={math.sqrt(max(0., colored_variance)):.9g}")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def check_capture(tool, root, name, stimulus_mode, expected_audio, expected_raw, extra=()):
    stimulus = root / f"{name}-stimulus.f32"
    capture = root / f"{name}-capture.f32"
    invoke(tool, stimulus_mode, stimulus)
    modeled = invoke(tool, "--simulate-long", stimulus, capture, *extra)
    source = fields(modeled.stdout)
    require(source.get("simulation") == "mono_cable", "model label unavailable")
    require(source.get("clipped_samples") == "0", "simulation clipped")
    analyzed = invoke(tool, "--analyze-long", capture)
    data = fields(analyzed.stdout)
    require(data.get("cable_helper_apertures") == "90/90" and data.get("fitted") == "1", "Helper fit unavailable")
    require(data.get("helper_startup_valid") == "1" and data.get("sequential_phase_clock_valid") == "1", "Helper-only time branch unavailable")
    require(data.get("phase_correction") == "observed_same_dwell" and data.get("phase_map_disagreements") == "0", "same-dwell Helper phase unavailable")
    require(int(data.get("timing_supported_samples", "0")) > 20000, "insufficient supported output")
    require(int(data.get("evaluated_blocks", "0")) >= 20, "insufficient probe metrology")
    require(abs(float(data["measured_audio_ppm"]) - expected_audio) < 1., "wrong Helper audio-clock estimate")
    require(abs(float(data["measured_common_offset_hz"]) - 4.) < .01, "wrong Helper frequency offset")
    require(abs(float(data["raw_tone_frequency_error_hz"]) - expected_raw) < .03, "wrong uncorrected tone error")
    require(abs(float(data["residual_tone_frequency_hz"])) < .01, "frequency correction failed")
    require(float(data["residual_phase_rms_degrees"]) < 5., "phase correction failed")
    print(f"{name}: {modeled.stdout.strip()} {analyzed.stdout.strip()}")
    return stimulus, capture, data


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_helper_live_cable_sim.py HELPER_LIVE_CABLE_TOOL")
    tool = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="helper-live-cable-sim-") as tmp:
        root = pathlib.Path(tmp)
        stimulus, capture, _ = check_capture(tool, root, "positive", "--generate-long", 100., 4.16)
        check_capture(tool, root, "negative", "--generate-long-negative", -100., 3.84)
        check_capture(tool, root, "adc-clock", "--generate-long", 93., 4.15,
                      ("--gain", ".7", "--delay-ms", "37", "--noise-rms", ".0001", "--seed", "12345", "--adc-ppm", "7"))
        _, calibrated_capture, calibrated = check_capture(tool, root, "helper-fitted", "--generate-long", 100., 4.16,
                                    ("--preset", "cable-20261030"))
        check_helper_transfer(stimulus, calibrated_capture)
        check_capture(tool, root, "helper-fitted-negative", "--generate-long-negative", -100., 3.84,
                      ("--preset", "cable-20261030"))
        _, second_seed, _ = check_capture(tool, root, "helper-fitted-second-seed", "--generate-long", 100., 4.16,
                          ("--preset", "cable-20261030", "--seed", "902"))
        require(digest(calibrated_capture) != digest(second_seed), "different noise seeds produced identical captures")
        # These reference values are the held-out 1600 Hz evaluation from one
        # saved cable capture. The preset's gain, delay, high-pass and noise
        # were selected using its pre-probe Helper and preroll alone. Compare
        # observables only after Helper conditioning; never use the probe as a
        # synchronization input. Broad tolerances allow float/libm variation.
        require(abs(float(calibrated["raw_tone_frequency_error_hz"]) - 4.16017559) < .02,
                "Helper-fitted model no longer reproduces held-out raw tone")
        require(abs(float(calibrated["raw_phase_rms_degrees"]) - 101.475527) < .5,
                "Helper-fitted model no longer reproduces held-out raw phase RMS")
        require(abs(float(calibrated["residual_tone_frequency_hz"]) - .000382850576) < .02,
                "Helper-fitted model no longer reproduces held-out frequency")
        require(abs(float(calibrated["residual_phase_rms_degrees"]) - 1.09837918) < .5,
                "Helper-fitted model no longer reproduces held-out phase RMS")

        repeated = root / "same-seed.f32"
        invoke(tool, "--simulate-long", stimulus, repeated)
        require(digest(capture) == digest(repeated), "same seed and configuration changed samples")
        repeated_preset = root / "same-seed-preset.f32"
        invoke(tool, "--simulate-long", stimulus, repeated_preset, "--preset", "cable-20261030")
        require(digest(calibrated_capture) == digest(repeated_preset), "same preset and seed changed samples")
        check_physical_rate(tool, root, stimulus)
        check_calibrated_noise(tool, root, stimulus)
        # Output creation is exclusive: a rerun cannot overwrite an existing
        # observation, even if an input happens to have the same name.
        original_hash = digest(capture)
        invoke(tool, "--simulate-long", stimulus, capture, success=False)
        require(digest(capture) == original_hash, "existing capture was overwritten")

        noise = root / "noise-only.f32"
        invoke(tool, "--simulate-long", stimulus, noise, "--gain", "0")
        rejected = invoke(tool, "--analyze-long", noise, success=False)
        require("Helper-only coarse onset unavailable" in rejected.stderr,
                "noise-only capture did not decline before Helper qualification")
        invalid = root / "invalid.f32"
        invoke(tool, "--simulate-long", stimulus, invalid, "--delay-ms", "-1", success=False)
        require(not invalid.exists(), "invalid simulation created a capture")
    print("Helper simulated cable: +/− clocks, ADC clock, pre-probe Helper-fitted model, held-out tone, deterministic samples, noise-only and bounds passed")


if __name__ == "__main__":
    main()
