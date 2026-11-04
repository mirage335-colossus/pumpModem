#!/usr/bin/env python3
"""Hardware-free campaign guards and bookkeeping; not physical qualification."""
import contextlib
import copy
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import triad_live_margins as campaign
from triad_live_receiver import freeze_calibration


def metadata(samples, *, synthetic_only=True):
    return dict(status="complete", failure="", discontinuity=dict(capture=0, playback=0),
                audio_format=dict(capture=dict(logical_rate=48000, hardware_rate=48000,
                    hardware_channels=2, logical_channels=1, selected_capture_channel=0)),
                capture=dict(logical_samples=samples), synthetic_only=synthetic_only)


def fixture(root, *, noise_rms=.03, samples=16):
    directory = root / "fixtures" / "baseline"
    directory.mkdir(parents=True)
    np.save(directory / "basis.npy", np.zeros((2, samples, 2), np.complex128))
    for bit in (0, 1):
        np.full(samples, 1. if bit else -.8, dtype="<f4").tofile(directory / f"signal-{bit}.f32")
    profile_path = root / "original-profile.json"
    campaign.write_json(profile_path, {})
    value = dict(schema="triad-live-margin-fixture-v1", id="baseline", epoch=1234,
        basis_sha256=campaign.digest(directory / "basis.npy"),
        signal_sha256=[campaign.digest(directory / f"signal-{bit}.f32") for bit in (0, 1)],
        preprocessing_sources=campaign.preprocessing_sources(),
        profile_sha256=campaign.digest(profile_path), noise_rms=noise_rms,
        receiver_sample_rate=100., receiver_samples=samples, payload_audio_samples=samples,
        carrier=10., bandwidth=10., lags=[0], public_key={}, reference_variance=1.,
        frequency_offsets_hz=[0.], preprocessing_identity="synthetic-harness-test-v1")
    campaign.write_json(directory / "fixture.json", value)
    return directory, value, profile_path


class FixtureAndCaptureGuards(unittest.TestCase):
    def test_fixture_detects_each_bound_input_change(self):
        with tempfile.TemporaryDirectory() as temp:
            directory, value, _ = fixture(Path(temp))
            campaign.verify_fixture(directory, value)
            for name, message in (("basis.npy", "public basis"),
                                  ("signal-0.f32", "source waveform"),
                                  ("signal-1.f32", "source waveform")):
                with self.subTest(name=name):
                    path = directory / name
                    original = path.read_bytes()
                    path.write_bytes(original + b"changed")
                    with self.assertRaisesRegex(ValueError, message):
                        campaign.verify_fixture(directory, value)
                    path.write_bytes(original)
            different = copy.deepcopy(value)
            different["preprocessing_sources"]["triad_live_model.py"] = "changed"
            with self.assertRaisesRegex(ValueError, "preprocessing source changed"):
                campaign.verify_fixture(directory, different)
            legacy = dict(value)
            del legacy["signal_sha256"]
            with self.assertRaisesRegex(ValueError, "lacks complete provenance"):
                campaign.verify_fixture(directory, legacy)
            campaign.verify_fixture(directory, legacy, require_complete=False)

    def test_capture_requires_complete_exact_continuous_stereo_route(self):
        baseline = metadata(32)
        campaign.validate_capture(baseline, 32)
        for field, value in (("logical_rate", 44100), ("hardware_rate", 44100),
                             ("hardware_channels", 1), ("logical_channels", 2),
                             ("selected_capture_channel", 1)):
            with self.subTest(field=field):
                bad = copy.deepcopy(baseline)
                bad["audio_format"]["capture"][field] = value
                with self.assertRaisesRegex(ValueError, "capture format"):
                    campaign.validate_capture(bad, 32)
        for endpoint in ("capture", "playback"):
            bad = copy.deepcopy(baseline)
            bad["discontinuity"][endpoint] = 1
            with self.assertRaisesRegex(ValueError, "discontinuous"):
                campaign.validate_capture(bad, 32)
        for field, value in (("status", "timeout"), ("failure", "EPIPE")):
            bad = copy.deepcopy(baseline); bad[field] = value
            with self.assertRaisesRegex(ValueError, "incomplete"):
                campaign.validate_capture(bad, 32)
        bad = copy.deepcopy(baseline); del bad["discontinuity"]
        with self.assertRaisesRegex(ValueError, "discontinuous"):
            campaign.validate_capture(bad, 32)
        with self.assertRaisesRegex(ValueError, "sample count mismatch"):
            campaign.validate_capture(baseline, 31)

    def test_exact_interval_includes_zero_counts(self):
        lo, hi = campaign.interval(0, 20)
        self.assertEqual(lo, 0.)
        self.assertAlmostEqual(hi, 1 - .025**(1 / 20))
        lo, hi = campaign.interval(20, 20)
        self.assertAlmostEqual(lo, .025**(1 / 20))
        self.assertEqual(hi, 1.)


class PreparationTests(unittest.TestCase):
    def args(self, root, profile_path, **changes):
        binary = root / "fake-binary"
        binary.write_bytes(b"hash-only; never executed")
        values = dict(output=str(root / "prepared"), profile=str(profile_path), phase="validation",
            noise_rms=.03, trials=2, calibration_trials=19, null_trials=1,
            cases="baseline", epoch=None, fixtures=str(root / "fixtures"), binary=str(binary),
            key_seed=55, levels="0,10", gains=".001", seed=21)
        values.update(changes)
        return SimpleNamespace(**values)

    def test_pre_dac_gain_convention_and_deterministic_composite(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            directory, _, profile_path = fixture(root)
            args = self.args(root, profile_path)
            # Only wrapping is omitted. The actual preparation noise, gains,
            # bit alternation, quantizer diagnostics and hashes are exercised.
            def wrap(payload, fs, **kwargs):
                return payload, dict(payload=dict(start=0, samples=len(payload)), markers=[])
            with patch.object(campaign.model, "wrap_with_markers", side_effect=wrap), \
                    patch.object(campaign, "make_fixture", side_effect=AssertionError("unexpected native export")), \
                    contextlib.redirect_stdout(io.StringIO()):
                campaign.prepare(args)
            plan = campaign.read_json(root / "prepared" / "plan.json")
            tx = np.fromfile(root / "prepared" / "stimulus.f32", dtype="<f4")
            self.assertEqual(len(plan["records"]), 5)
            self.assertIn("not receiver-input AWGN", plan["noise_convention"])
            for record in plan["records"]:
                expected = np.random.default_rng(record["noise_seed"]).normal(0., .03, record["samples"]).astype(np.float32)
                if record["truth"] is not None:
                    expected_gain = np.sqrt(2 * .03**2 / 48000 * 10**(record["cn0_db_hz"] / 10))
                    self.assertAlmostEqual(record["gain"], expected_gain)
                    signal = np.fromfile(directory / f"signal-{record['truth']}.f32", dtype="<f4")
                    expected += record["gain"] * signal
                np.testing.assert_array_equal(tx[record["offset"]:record["offset"] + record["samples"]], expected)
            self.assertEqual(plan["stimulus_sha256"], campaign.digest(root / "prepared" / "stimulus.f32"))

    def test_invalid_noise_and_trial_counts_stop_before_preparation(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            _, _, profile_path = fixture(root)
            for value in (0., -1., float("nan"), float("inf")):
                with self.subTest(noise_rms=value), self.assertRaisesRegex(ValueError, "noise RMS"):
                    campaign.prepare(self.args(root, profile_path, noise_rms=value))
                self.assertFalse((root / "prepared").exists())
            with self.assertRaisesRegex(ValueError, "trial counts"):
                campaign.prepare(self.args(root, profile_path, trials=0))
            self.assertFalse((root / "prepared").exists())


class MockedAnalysisTests(unittest.TestCase):
    """Exercises campaign control flow, explicitly not receiver DSP or hardware."""
    def make_inputs(self, root):
        directory, f, profile_path = fixture(root)
        src = root / "prepared"; src.mkdir()
        (src / "profile.json").write_bytes(profile_path.read_bytes())
        chunks, records, offset = [], [], 0
        for index, (truth, calibration, level) in enumerate(((None, True, .01), (None, True, .02),
                                                           (None, False, .01), (1, False, .1))):
            chunk = np.full(16, level, dtype="<f4")
            chunks.append(chunk)
            records.append(dict(index=index, offset=offset, samples=16, case=None if truth is None else "baseline",
                truth=truth, calibration=calibration, gain=0. if truth is None else .1,
                cn0_db_hz=None if truth is None else 10., noise_seed=index,
                markers=dict(markers=[dict(start=0, samples=1)], payload=dict(start=0, samples=16)),
                stimulus_sha256=hashlib.sha256(chunk.tobytes()).hexdigest()))
            offset += 16
        np.concatenate(chunks).tofile(src / "stimulus.f32")
        capture = root / "capture.f32"
        (np.concatenate(chunks) * 1.1).astype("<f4").tofile(capture)
        capture_metadata = root / "capture.json"
        campaign.write_json(capture_metadata, metadata(offset))
        plan = dict(phase="validation", cases=["baseline"], fixtures_directory=str(directory.parent),
            profile_sha256=f["profile_sha256"], profile_copy_sha256=campaign.digest(src / "profile.json"),
            stimulus_sha256=campaign.digest(src / "stimulus.f32"),
            fixture_json_sha256={"baseline": campaign.digest(directory / "fixture.json")}, records=records)
        campaign.write_json(src / "plan.json", plan)
        args = SimpleNamespace(prepared=str(src), output=str(root / "analyzed"), capture=str(capture),
                               capture_metadata=str(capture_metadata), calibration=None, alpha=.5)
        return args

    def test_tiny_synthetic_analysis_counts_pairing_and_provenance(self):
        class FakeReceiver:
            def __init__(self, *args, **kwargs):
                self.receiver_id = "mock-harness-statistic"
            def score(self, y):
                return dict(score=float(np.mean(abs(y))), bit=1, opposite_bit_margin=1.,
                            receiver_id=self.receiver_id, calibration=dict(analytic_admission_validated=False))
            def freeze_calibration(self, scores, *, alpha):
                return freeze_calibration(scores, receiver_id=self.receiver_id, alpha=alpha)
        class FakeState:
            instances = []
            def __init__(self, profile):
                self.calls = 0
                self.instances.append(self)
            def process(self, x):
                self.calls += 1
                return np.asarray(x, dtype=np.float64).copy()
        def align(tx, rx, fs, markers):
            return tx * 1.1, np.ones(len(tx), bool), dict(offset_samples=0.)
        def convert(pcm, f):
            return np.asarray(pcm, dtype=np.complex128), np.ones(len(pcm), bool), {"mocked": True}
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            args = self.make_inputs(root)
            with patch("triad_live_receiver.EmpiricalReceiver", FakeReceiver), \
                    patch.object(campaign.model, "_marker_delay", return_value={"delay_samples": 0}), \
                    patch.object(campaign.model, "align_capture", side_effect=align), \
                    patch.object(campaign.model, "AudioModelState", FakeState), \
                    patch.object(campaign, "fixed_noise", side_effect=lambda p, n, s: np.zeros(n)), \
                    patch.object(campaign, "convert", side_effect=convert), \
                    contextlib.redirect_stdout(io.StringIO()):
                campaign.analyze(args)
            result = campaign.read_json(root / "analyzed" / "results.json")
            self.assertTrue(result["synthetic_only"])
            self.assertEqual(result["capture_metadata_sha256"], campaign.digest(args.capture_metadata))
            self.assertEqual(len(result["rows"]), 8)
            self.assertEqual(len(FakeState.instances), 1)
            self.assertEqual(FakeState.instances[0].calls, 4)
            for row in result["rows"]:
                self.assertIsInstance(row["calibration_record"], bool)
                self.assertIsInstance(row["calibration"], dict)
                if row["calibration_record"]:
                    self.assertNotIn("decision", row)
            self.assertEqual(len(result["summaries"]), 4)
            expected = {"correct_admission", "wrong_admission", "miss", "false_admission", "correct_rejection"}
            for summary in result["summaries"]:
                self.assertEqual(set(summary["counts"]), expected)
                self.assertEqual(sum(summary["counts"].values()), 1)
                self.assertEqual(set(summary["intervals_95"]), expected)
                target = "correct_rejection" if summary["gain"] == 0 else "correct_admission"
                self.assertEqual(summary["counts"][target], 1)

    def test_changed_prepared_inputs_fail_before_receiver(self):
        for filename, message in (("stimulus.f32", "stimulus changed"),
                                  ("profile.json", "copied profile changed")):
            with self.subTest(filename=filename), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                args = self.make_inputs(root)
                path = root / "prepared" / filename
                # JSON must remain parseable because loading precedes hashing.
                path.write_bytes(path.read_bytes() + (b" " if filename.endswith("json") else b"bad!"))
                with patch("triad_live_receiver.EmpiricalReceiver", side_effect=AssertionError("receiver must not run")), \
                        self.assertRaisesRegex(ValueError, message):
                    campaign.analyze(args)


if __name__ == "__main__":
    unittest.main()
