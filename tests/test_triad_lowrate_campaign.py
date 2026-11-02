"""Focused exporter argument, provenance, failure and child-cleanup checks."""
from __future__ import annotations

import importlib.util
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "triad-lowrate-campaign.py"
SPEC = importlib.util.spec_from_file_location("lowrate_campaign", SCRIPT)
campaign = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(campaign)


class LowRateCampaignTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="triad-lowrate-campaign-")
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def args(self, *extra, binary=None, output=None):
        return campaign.parser().parse_args([
            "--binary", str(binary or Path(sys.executable)), "--output", str(output or self.root / "result"),
            "--paths", "2", "--split", "development", "--seconds", "3283200", "--cn0", "-46", *extra])

    def fake_binary(self, body):
        path = self.root / "exporter"
        path.write_text("#!" + sys.executable + "\nimport json, os, pathlib, signal, subprocess, sys, time\n" +
                        "args=sys.argv[1:]\nout=pathlib.Path(args[args.index('--output')+1])\n" + body)
        path.chmod(0o755)
        return path

    def success_binary(self):
        return self.fake_binary(
            "out.mkdir()\n"
            f"names={campaign.EXPORT_FILES!r}\n"
            "for name in names: (out/name).write_bytes(b'x')\n"
            "(out/'manifest.json').write_text(json.dumps({'format':'triad-block-stats-v1','stats_noisy':True,"
            "'file_sizes':{name:1 for name in names}}))\n")

    def test_declared_options_and_seeds_are_explicit(self):
        args = self.args("--ray-coherence-seconds", "10000", "--diurnal-phase-radians", "2",
                         "--outage-seconds", "86400", "90000", "--start", "-.0905", ".0905", ".0905",
                         "--epoch-offsets=-1,0,1")
        campaign.validate(args)
        command, identity = campaign.make_command(args, 3, self.root / "path")
        for flag, expected in (("--mode", "export-stats"), ("--control", "signal"), ("--rf-hz", "8270"), ("--added", "1"),
                               ("--sample-rate", "1"), ("--base-oscillator", "gpsdo-ocxo"),
                               ("--shift-oscillator", "gpsdo-ocxo"), ("--spread-preset", "sub9-best"),
                               ("--audio-error-ms", "70"), ("--basis", "two-ray"),
                               ("--tap-delay-ms", "0.005"), ("--tap-doppler-hz", "0.001"),
                               ("--clock-sync", "GPS_10ms-1ms_region-0ms_offset"),
                               ("--ray-coherence-seconds", "10000.0")):
            self.assertEqual(command[command.index(flag)+1], expected)
        self.assertNotIn("--no-encrypt", command)
        self.assertNotIn("--fhss", command)
        self.assertNotIn("--burst-wait-tiles", command)
        self.assertNotIn("--spread-delay-ms", command)
        self.assertNotIn("--spread-doppler-hz", command)
        self.assertNotIn("--ray-gaussian-width-hz", command)
        self.assertIn("--stats-noisy", command)
        self.assertEqual(identity["source_bit"], 1)
        seeds = [identity[d+"_seed"] for d in campaign.SEED_DOMAINS]
        self.assertEqual(len(set(seeds)), 6)
        for domain in campaign.SEED_DOMAINS:
            self.assertEqual(command[command.index("--"+domain+"-seed")+1], str(identity[domain+"_seed"]))

    def test_receiver_basis_override_does_not_change_physical_preset(self):
        args = self.args("--tap-delay-ms", "1.5", "--tap-doppler-hz", ".3")
        campaign.validate(args)
        command, _ = campaign.make_command(args, 0, self.root / "path")
        self.assertEqual(command[command.index("--tap-delay-ms")+1], "1.5")
        self.assertEqual(command[command.index("--tap-doppler-hz")+1], "0.3")
        self.assertEqual(command[command.index("--spread-preset")+1], "sub9-best")

    def test_physical_rf_and_spread_options_preserve_receiver_basis_and_seeds(self):
        baseline = self.args()
        args = self.args("--rf-hz", "5800000000", "--spread-preset", "hf-best",
                         "--spread-delay-ms", "1.25", "--spread-doppler-hz", ".125")
        campaign.validate(args)
        command, _ = campaign.make_command(args, 4, self.root / "path")
        for flag, value in (("--rf-hz", "5800000000"), ("--spread-preset", "hf-best"),
                            ("--spread-delay-ms", "1.25"), ("--spread-doppler-hz", "0.125"),
                            ("--tap-delay-ms", "0.005"), ("--tap-doppler-hz", "0.001")):
            self.assertEqual(command[command.index(flag)+1], value)
        for domain in campaign.SEED_DOMAINS:
            self.assertEqual(campaign.seed_for(baseline, 4, domain), campaign.seed_for(args, 4, domain))
        # Explicit zero differs from omission, and either dimension may be
        # overridden alone without implicitly replacing the other dimension.
        args = self.args("--spread-delay-ms", "0")
        campaign.validate(args)
        command, _ = campaign.make_command(args, 0, self.root / "path")
        self.assertEqual(command[command.index("--spread-delay-ms")+1], "0")
        self.assertNotIn("--spread-doppler-hz", command)

    def test_spread_choices_match_canonical_ids_and_invalid_physics_rejected(self):
        source = (SCRIPT.parents[1] / "src" / "tuning_presets.cpp").read_text()
        canonical = re.findall(r'SpreadPreset\{"([^"]+)"', source)
        self.assertEqual(list(campaign.SPREAD_PRESETS), canonical)
        for preset in canonical:
            args = self.args("--spread-preset", preset)
            campaign.validate(args)
            command, _ = campaign.make_command(args, 0, self.root / "path")
            self.assertEqual(command[command.index("--spread-preset")+1], preset)
        for name, value in (("rf_hz", 0), ("rf_hz", -1), ("rf_hz", float("inf")), ("rf_hz", 11e9),
                            ("spread_delay_ms", -1), ("spread_delay_ms", float("nan")),
                            ("spread_doppler_hz", -1), ("spread_doppler_hz", float("inf")),
                            ("spread_preset", "unknown")):
            args = self.args()
            setattr(args, name, value)
            with self.subTest(name=name, value=value), self.assertRaises(ValueError):
                campaign.validate(args)
            self.assertFalse((self.root / "result").exists())
        campaign.validate(self.args("--rf-hz", "10000000000"))

    def test_gaussian_ray_width_forwarding_pairing_and_exclusions(self):
        baseline = self.args()
        explicit_off = self.args("--ray-gaussian-width-hz", "0")
        self.assertEqual(campaign.make_command(baseline, 0, self.root / "path"),
                         campaign.make_command(explicit_off, 0, self.root / "path"))
        args = self.args("--ray-gaussian-width-hz", ".125")
        campaign.validate(args)
        command, _ = campaign.make_command(args, 0, self.root / "path")
        self.assertEqual(command[command.index("--ray-gaussian-width-hz")+1], "0.125")
        self.assertEqual(command[command.index("--tap-doppler-hz")+1], "0.001")
        for domain in campaign.SEED_DOMAINS:
            self.assertEqual(campaign.seed_for(baseline, 0, domain), campaign.seed_for(args, 0, domain))
        for width in (-1, float("nan"), float("inf")):
            invalid = self.args()
            invalid.ray_gaussian_width_hz = width
            with self.subTest(width=width), self.assertRaises(ValueError):
                campaign.validate(invalid)
        with self.assertRaises(ValueError):
            campaign.validate(self.args("--ray-gaussian-width-hz", ".125", "--ray-coherence-seconds", "100"))
        self.assertFalse((self.root / "result").exists())

    def test_sampled_controls_forward_without_changing_seed_identity(self):
        base = self.args()
        for control in ("signal", "null", "wrong-key"):
            with self.subTest(control=control):
                args = self.args("--control", control)
                campaign.validate(args)
                command, identity = campaign.make_command(args, 4, self.root / "path")
                self.assertEqual(command[command.index("--control")+1], control)
                self.assertEqual(identity["control"], control)
                for domain in campaign.SEED_DOMAINS:
                    self.assertEqual(campaign.seed_for(base, 4, domain), campaign.seed_for(args, 4, domain))

    def test_all_selective_depths_forward_with_paired_seeds_and_invalids_rejected(self):
        baseline = self.args()
        choices = ["off"] + [f"{mode}-{depth}" for depth in (1, 5, 10, 25, 65)
                              for mode in ("slow", "fast", "each")]
        self.assertEqual(list(campaign.SELECTIVE_PERTURBATIONS), choices)
        for choice in choices:
            with self.subTest(choice=choice):
                args = self.args("--selective-perturbation", choice)
                campaign.validate(args)
                command, _ = campaign.make_command(args, 0, self.root / "path")
                self.assertEqual(command[command.index("--selective-perturbation")+1], choice)
                for domain in campaign.SEED_DOMAINS:
                    self.assertEqual(campaign.seed_for(baseline, 0, domain), campaign.seed_for(args, 0, domain))
        for choice in ("slow-0", "fast-2", "each-100", "each-65%", "unknown"):
            with self.subTest(choice=choice):
                with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                    self.args("--selective-perturbation", choice)
                invalid = self.args()
                invalid.selective_perturbation = choice
                with self.assertRaises(ValueError):
                    campaign.validate(invalid)
        self.assertFalse((self.root / "result").exists())

    def test_seed_partition_pairing_and_independence(self):
        a, b = self.args(), self.args("--block-seconds", "256", "--ray-coherence-seconds", "1000")
        for domain in campaign.SEED_DOMAINS:
            self.assertEqual(campaign.seed_for(a, 0, domain), campaign.seed_for(b, 0, domain))
        b.sample_rate = 4
        self.assertNotEqual(campaign.seed_for(a, 0, "noise"), campaign.seed_for(b, 0, "noise"))
        b = self.args("--split", "holdout")
        self.assertNotEqual(campaign.seed_for(a, 0, "key"), campaign.seed_for(b, 0, "key"))
        self.assertNotEqual(campaign.seed_for(a, 0, "path"), campaign.seed_for(a, 1, "path"))

    def test_reciprocal_rate_and_year_duration(self):
        args = self.args("--seconds", "86400000", "--rate", ".0036", "--sample-rate-denominator", "64")
        campaign.validate(args)
        command, _ = campaign.make_command(args, 0, self.root / "path")
        self.assertEqual(command[command.index("--sample-rate-denominator")+1], "64")
        self.assertEqual(command[command.index("--seconds")+1], "86400000.0")
        legacy = self.args()
        identity = ["triad-lowrate-campaign-v1", "development", 3283200., .036, 1, -46., 0, 0, "noise"]
        expected = int.from_bytes(hashlib.sha256(json.dumps(identity, separators=(",", ":")).encode()).digest()[:8], "little")
        self.assertEqual(campaign.seed_for(legacy, 0, "noise"), expected)
        reciprocal = self.args("--sample-rate-denominator", "64")
        self.assertNotEqual(campaign.seed_for(legacy, 0, "noise"), campaign.seed_for(reciprocal, 0, "noise"))

    def test_configuration_bounds_fail_before_output_creation(self):
        cases = [("--paths", "3"), ("--jobs", "33"), ("--seconds", "315576001"),
                 ("--rate", "3.6", "--sample-rate", "1"), ("--timeout", "0"),
                 ("--sample-rate-denominator", "0"), ("--sample-rate-denominator", "4294967296"),
                 ("--sample-rate", "2", "--sample-rate-denominator", "2"),
                 ("--rate", ".036", "--sample-rate-denominator", "64"),
                 ("--rate", ".0036", "--sample-rate-denominator", "64", "--block-seconds", "32"),
                 ("--outage-seconds", "1", "2"), ("--epoch-offsets", "0,0"),
                 ("--tap-delay-ms", "-1"), ("--tap-doppler-hz", "-1"),
                 ("--start", "0", "100", "1e-300"), ("--ray-coherence-seconds", "-1")]
        for extra in cases:
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                campaign.validate(self.args(*extra))
            self.assertFalse((self.root / "result").exists())
        for rate, fs in ((3.6, 4), (.36, 1), (.036, 1), (.0036, 1)):
            campaign.validate(self.args("--rate", str(rate), "--sample-rate", str(fs)))

    def test_actual_driver_smoke_and_no_overwrite(self):
        args = self.args("--jobs", "2", binary=self.success_binary())
        self.assertEqual(campaign.run(args), 0)
        report = json.loads((args.output / "campaign.json").read_text())
        self.assertEqual((report["paths_requested"], report["paths_attempted"], report["paths_succeeded"]), (2, 2, 2))
        self.assertEqual(report["control"], "signal")
        self.assertEqual([r["source_bit"] for r in report["results"]], [0, 1])
        self.assertTrue(all(r["runner_status"]["termination"]["group_empty"] for r in report["results"]))
        self.assertEqual(len(report["binary_sha256"]), 64)
        with self.assertRaises(FileExistsError):
            campaign.run(args)

    def test_failed_export_records_failure_and_stops_new_dispatch(self):
        binary = self.fake_binary("sys.exit(17)\n")
        args = self.args("--paths", "4", binary=binary)
        self.assertEqual(campaign.run(args), 1)
        report = json.loads((args.output / "campaign.json").read_text())
        self.assertEqual(report["paths_attempted"], 1)
        self.assertEqual(report["paths_not_started"], 3)
        self.assertEqual(report["paths_succeeded"], 0)
        self.assertEqual(report["results"][0]["returncode"], 17)

    def test_cancel_before_worker_launch_is_not_an_attempt(self):
        args = self.args("--paths", "4", binary=self.success_binary())
        original = campaign.one_path
        def cancel_before_launch(args, index, cancelled):
            cancelled.set()
            return original(args, index, cancelled)
        with mock.patch.object(campaign, "one_path", side_effect=cancel_before_launch):
            self.assertEqual(campaign.run(args), 1)
        report = json.loads((args.output / "campaign.json").read_text())
        self.assertEqual(report["paths_dispatched"], 1)
        self.assertEqual(report["paths_attempted"], 0)
        self.assertEqual(report["paths_not_started"], 4)
        self.assertEqual(report["paths_start_unknown"], 0)
        self.assertEqual(report["results"][0]["outcome"], "not_started_cancelled")
        self.assertFalse(report["results"][0]["runner_started"])
        self.assertEqual(list(args.output.glob("*.status.json")), [])

    def tree_binary(self):
        pidfile = self.root / "pids"
        child = ("import os,signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); "
                 f"open({str(pidfile)!r},'a').write(str(os.getpid())+'\\n'); time.sleep(60)")
        binary = self.fake_binary(
            "signal.signal(signal.SIGTERM,signal.SIG_IGN)\n"
            f"open({str(pidfile)!r},'a').write(str(os.getpid())+'\\n')\n"
            f"subprocess.Popen([sys.executable,'-c',{child!r}])\n"
            "time.sleep(60)\n")
        return binary, pidfile

    def assert_pids_gone(self, pidfile):
        pids = [int(v) for v in pidfile.read_text().splitlines()]
        self.assertEqual(len(pids), 2)
        for pid in pids:
            self.assertFalse(Path(f"/proc/{pid}").exists(), f"child {pid} remains")

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux process-group runner")
    def test_timeout_reaps_fake_exporter_children(self):
        binary, pidfile = self.tree_binary()
        args = self.args("--timeout", ".5", "--term-grace", ".1", binary=binary)
        self.assertEqual(campaign.run(args), 1)
        report = json.loads((args.output / "campaign.json").read_text())
        result = report["results"][0]
        self.assertEqual(result["returncode"], 124)
        self.assertEqual(result["outcome"], "timeout")
        self.assertTrue(result["runner_status"]["termination"]["kill_sent"])
        self.assert_pids_gone(pidfile)

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux process-group runner")
    def test_campaign_term_forwards_to_runner_and_reaps_children(self):
        binary, pidfile = self.tree_binary()
        output = self.root / "cancelled"
        command = [sys.executable, str(SCRIPT), "--binary", str(binary), "--output", str(output),
                   "--paths", "4", "--split", "holdout", "--seconds", "3600", "--cn0", "-36",
                   "--timeout", "10", "--term-grace", ".1"]
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if pidfile.exists() and len(pidfile.read_text().splitlines()) == 2:
                    break
                time.sleep(.02)
            else:
                self.fail("fake exporter children did not start")
            process.terminate()
            stdout, stderr = process.communicate(timeout=5)
            self.assertEqual(process.returncode, 1, (stdout, stderr))
            report = json.loads((output / "campaign.json").read_text())
            self.assertTrue(report["cancelled"])
            self.assertEqual(report["paths_attempted"], 1)
            self.assertEqual(report["results"][0]["outcome"], "cancelled")
            self.assert_pids_gone(pidfile)
        finally:
            if process.poll() is None:
                process.terminate()
                process.communicate(timeout=15)


if __name__ == "__main__":
    unittest.main()
