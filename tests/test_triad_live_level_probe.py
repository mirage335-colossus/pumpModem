"""Preparation invariants for repeated physical level diagnostics."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import triad_live_level_probe as probe
import triad_live_margins as campaign


class LevelProbePreparation(unittest.TestCase):
    def fixtures(self, root):
        source_profile = root / "source-profile.json"
        corrected_profile = root / "corrected-profile.json"
        campaign.write_json(source_profile, {"schema": "old-profile"})
        campaign.write_json(corrected_profile, {"schema": "triad-live-audio-profile-v1",
            "sample_rate": 48000, "noise": {"psd_fs_squared_per_hz": [1e-12]}})
        fixture_root = root / "fixtures"
        fixture_root.mkdir()
        for case, carrier in (("baseline", 1725.), ("edge", 15500.)):
            directory = fixture_root / case
            directory.mkdir()
            np.save(directory / "basis.npy", np.zeros((2, 16, 2), dtype=np.complex128))
            waves = [np.full(24, 0.5, dtype="<f4"), np.full(24, -0.25, dtype="<f4")]
            for bit, wave in enumerate(waves):
                wave.tofile(directory / f"signal-{bit}.f32")
            fixture = dict(schema="triad-live-margin-fixture-v1", id=case,
                basis_sha256=campaign.digest(directory / "basis.npy"),
                signal_sha256=[campaign.digest(directory / f"signal-{i}.f32") for i in (0, 1)],
                preprocessing_sources=campaign.preprocessing_sources(),
                profile_sha256=campaign.digest(source_profile), noise_rms=.03,
                payload_audio_samples=32, source_waveform_samples=[24, 24],
                receiver_sample_rate=8., receiver_samples=16,
                carrier=carrier, bandwidth=1.)
            campaign.write_json(directory / "fixture.json", fixture)
        return fixture_root, corrected_profile

    def test_deterministic_22_record_plan_keeps_conditions_and_hashes(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            fixtures, profile = self.fixtures(root)
            out = root / "prepared"
            args = SimpleNamespace(output=str(out), fixtures=str(fixtures), profile=str(profile),
                                   seed=7, noise_levels="0.1,0.03,0.001,0.0001")
            plan = probe.prepare(args)
            self.assertEqual(len(plan["records"]), 22)
            self.assertEqual(plan["repeat_passes"], 2)
            self.assertEqual(plan["seconds"], plan["samples"] / 48000)
            self.assertEqual(plan["profile_sha256"], campaign.digest(profile))
            self.assertEqual(plan["profile_copy_sha256"], campaign.digest(out / "profile.json"))
            self.assertEqual(plan["stimulus_sha256"], campaign.digest(out / "stimulus.f32"))
            self.assertEqual([record["index"] for record in plan["records"]], list(range(22)))
            self.assertEqual(len({record["noise_seed"] for record in plan["records"]}), 22)
            counts = {}
            for record in plan["records"]:
                counts[record["kind"]] = counts.get(record["kind"], 0) + 1
                self.assertEqual(record["calibration"], False)
                if record["kind"] == "signal":
                    self.assertIn(record["case"], ("baseline", "edge"))
                    self.assertIn(record["truth"], (0, 1))
                    self.assertIn(record["quantization"]["meaning"],
                                  ["complete-composite S16 diagnostic; noise dithers signal codes; not an ADC ENOB estimate"])
                    self.assertEqual(record["source_samples"], 32)
                    self.assertEqual(len(probe._load_case(fixtures, record["case"])[2]), 32)
                    diagnostic = record["quantization"]
                    expected_ratio = (diagnostic["signal_induced_quantization_code_energy"]
                        / (32768.**2 * diagnostic["clean_signal_energy"]))
                    self.assertAlmostEqual(diagnostic["quantized_delta_to_clean_energy_ratio"],
                                           expected_ratio, places=16)
                elif record["kind"] == "silence":
                    self.assertIsNone(record["case"])
                    self.assertEqual(record["noise_rms"], 0.)
                else:
                    self.assertEqual(record["kind"], "noise")
                    self.assertIsNone(record["case"])
            self.assertEqual(counts, {"signal": 16, "noise": 4, "silence": 2})
            tx = np.fromfile(out / "stimulus.f32", dtype="<f4")
            for record in plan["records"]:
                piece = tx[record["offset"]:record["offset"] + record["samples"]]
                self.assertEqual(probe._sha(piece.tobytes()), record["stimulus_sha256"])
            with self.assertRaises(FileExistsError):
                probe.prepare(args)

    def test_rejects_same_source_profile_and_excessive_noise(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            fixtures, profile = self.fixtures(root)
            args = SimpleNamespace(output=str(root / "badprofile"), fixtures=str(fixtures),
                                   profile=str(profile), seed=1, noise_levels="0,0.1")
            with self.assertRaisesRegex(ValueError, "noise levels"):
                probe.prepare(args)
            args.output = str(root / "sameprofile")
            args.noise_levels = "0.03"
            # Use the fixture source profile as the forward profile to enforce
            # separation between public templates and the corrected model.
            source = root / "source-profile.json"
            args.profile = str(source)
            with self.assertRaisesRegex(ValueError, "corrected 48-kHz"):
                probe.prepare(args)


if __name__ == "__main__":
    unittest.main()
