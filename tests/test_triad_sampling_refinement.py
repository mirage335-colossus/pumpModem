"""Focused actual-runner lifecycle checks for the refinement exporter driver."""
from __future__ import annotations

import importlib.util
from fractions import Fraction
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

import numpy as np


SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "triad-sampling-refinement.py"
SPEC = importlib.util.spec_from_file_location("sampling_refinement", SCRIPT)
refinement = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(refinement)


class ReciprocalSamplingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="triad-refinement-rational-")
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def args(self, *extra):
        return refinement.parse_args(["--binary", sys.executable, "--output", str(self.root/"study"), *extra])

    def test_default_and_reciprocal_grid_commands_are_unambiguous(self):
        default = self.args()
        self.assertEqual(default.sampling_grid, [1, 4, 64])
        self.assertEqual([refinement.sampling_label(fs) for fs in default.sampling_grid], ["1", "4", "64"])
        self.assertNotIn("--sample-rate-denominator", refinement.base_command(default, .36, Fraction(4)))
        sparse = self.args("--sample-rates", "1", "--sample-periods", "16", "64")
        self.assertEqual(sparse.sampling_grid, [Fraction(1,64), Fraction(1,16), Fraction(1)])
        self.assertEqual(max(sparse.sampling_grid), 1)
        command = refinement.base_command(sparse, .00036, Fraction(1,64))
        self.assertEqual(command[command.index("--sample-rate")+1], "1")
        self.assertEqual(command[command.index("--sample-rate-denominator")+1], "64")
        self.assertEqual(refinement.sampling_label(Fraction(1,64)), "1over64")
        only_periods = self.args("--sample-rates", "--sample-periods", "16", "64")
        self.assertEqual(max(only_periods.sampling_grid), Fraction(1,16))

    def test_duplicate_overlap_and_invalid_clock_values_reject(self):
        for rates, periods in (([1,1], []), ([], [16,16]), ([1], [1]), ([], []),
                               ([0], []), ([], [-1]), ([], [2**32])):
            with self.subTest(rates=rates, periods=periods), self.assertRaises(ValueError):
                refinement.sampling_grid(rates, periods)
        self.assertFalse((self.root/"study").exists())

    def test_waveform_and_doppler_capture_limits_are_separate(self):
        shape = refinement.capture_eligibility(.36, Fraction(1,16), "baseline")
        self.assertEqual(shape["status"], "unsupported_shaped_bandwidth")
        capture = refinement.capture_eligibility(.00036, Fraction(1,512), "baseline")
        self.assertEqual(capture["status"], "unsupported_deterministic_capture")
        for rate in (.0036, .00036):
            for period in (1,16,64):
                self.assertIsNone(refinement.capture_eligibility(rate, Fraction(1,period), "diurnal"))

    def test_outage_is_common_physical_profile_with_four_sample_ramps(self):
        sparse = self.args("--sample-rates", "1", "--sample-periods", "16", "64")
        profiles = []
        for period in (1,16,64):
            manifest = {"actual_elapsed_seconds": 160000.4,
                        "source_format": {"calendar_origin_sample": (13334+period-1)//period+2}}
            profiles.append(refinement.outage_profile(sparse, manifest, Fraction(1,period)))
        self.assertEqual({p["span_seconds"] for p in profiles}, {768})
        self.assertEqual({p["ramp_seconds"] for p in profiles}, {256})
        self.assertTrue(all(p["ramp_samples_at_slowest_rate"] == 4 for p in profiles))
        self.assertTrue(all(p["adjusted_for_sampling"] and not p["clipped_to_tile"] for p in profiles))
        self.assertEqual(len({p["start_seconds"] for p in profiles}), 3)
        ordinary = refinement.outage_profile(self.args(),
            {"actual_elapsed_seconds":1600.4, "source_format":{"calendar_origin_sample":136}}, Fraction(1))
        self.assertEqual((ordinary["span_seconds"], ordinary["ramp_seconds"]), (400,30))
        self.assertFalse(ordinary["adjusted_for_sampling"])
        explicit = self.args("--sample-rates", "1", "--sample-periods", "64",
                             "--outage-span-seconds", "1024", "--outage-ramp-seconds", "300")
        profile = refinement.outage_profile(explicit,
            {"actual_elapsed_seconds":160000.4,"source_format":{"calendar_origin_sample":13336}}, Fraction(1))
        self.assertEqual((profile["span_seconds"], profile["ramp_seconds"]), (1024,300))

    def test_exact_calendar_alignment_for_reciprocal_and_nonnested_periods(self):
        def data(fs, origin, rows):
            return {"rational_fs":fs, "rows":rows, "lead":.05,
                    "manifest":{"source_format":{"calendar_origin_sample":origin}}}
        low, ref = data(Fraction(1,64),4,20), data(Fraction(1),128,1280)
        alignment = refinement.aligned_sample_grid(low, ref)
        self.assertEqual(alignment, (2,1,0,64,18,Fraction(64),Fraction(-128)))
        low, ref = data(Fraction(1,4),2,10), data(Fraction(1,3),4,20)
        alignment = refinement.aligned_sample_grid(low, ref)
        self.assertEqual(alignment[:5], (2,3,4,4,3))
        for j in range(alignment[4]):
            nl, nr = alignment[0]+j*alignment[1], alignment[2]+j*alignment[3]
            self.assertEqual(Fraction(nl-2)/low["rational_fs"], Fraction(nr-4)/ref["rational_fs"])
        ref["lead"] = .051
        with self.assertRaises(ValueError):
            refinement.aligned_sample_grid(low, ref)

    def test_exported_effective_clock_drives_energy_and_metadata(self):
        signal_values = np.array([1,2,3,4], dtype=np.complex64)
        basis = np.stack((signal_values, np.array([1,-1,1,-1], dtype=np.complex64)))[:,:,None]
        signal_values.tofile(self.root/"signal.c64")
        basis.tofile(self.root/"basis.c64")
        manifest = {"sample_rate":1/64, "sample_rate_numerator":1, "sample_rate_denominator":64,
            "complex_type":"complex64", "layout":"candidate,sample,column",
            "endpoint_truth":{name:0 for name in ("tx_audio_ppm","rx_audio_ppm","tx_rf_ppm","rx_rf_ppm",
                "tx_phase_diffusion_rad_sqrt_s","rx_phase_diffusion_rad_sqrt_s")},
            "resolved_configuration":{"clock_policy_active":False,"oscillator_presets_active":False},
            "truth_clock_ppm":0, "truth_frequency_hz":0, "truth_frequency_drift_hz_per_second":0,
            "truth_arrival_seconds":.05, "source_format":{"calendar_origin_sample":3},
            "signal_file":"signal.c64", "signal_shape":[4], "basis_file":"basis.c64", "basis_shape":[2,4,1],
            "candidateparams":[{"bit":0,"tap_count":1},{"bit":1,"tap_count":1}],
            "requested_active_seconds":8, "actual_elapsed_seconds":256,
            "channel_model":{"path_evolution":{}}}
        (self.root/"manifest.json").write_text(json.dumps(manifest))
        data = refinement.inspect(self.root)
        try:
            metrics, gram = refinement.metrics(data)
            self.assertEqual(data["rational_fs"], Fraction(1,64))
            self.assertEqual(data["origin"], 192)
            self.assertEqual(metrics["signal_energy_seconds"], 30*64)
            self.assertEqual(metrics["sample_period_seconds"], 64)
            self.assertAlmostEqual(metrics["candidate_projection"][0]["retained_signal_energy_fraction"], 1.)
            self.assertEqual(gram[0,0], 30*64)
        finally:
            data["signal"]._mmap.close(); data["basis"]._mmap.close()
        manifest["sample_rate"] = 1
        (self.root/"manifest.json").write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "effective sample rate"):
            refinement.inspect(self.root)

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux owned-group runner")
    def test_commandline_sweep_uses_fs1_reference_without_fs64(self):
        # A tiny complete fake export exercises orchestration and metadata;
        # these arrays are not a propagation-accuracy experiment.
        exporter = self.root/"fake-exporter"
        exporter.write_text("#!" + sys.executable + "\n" + '''
import json, pathlib, sys
import numpy as np
a=sys.argv[1:]
value=lambda flag, default: a[a.index(flag)+1] if flag in a else default
out=pathlib.Path(value('--output',''))
numerator=int(value('--sample-rate','1')); denominator=int(value('--sample-rate-denominator','1'))
y=np.array([1,2,3,4],dtype=np.complex64)
np.stack([y,y[::-1]])[:,:,None].tofile(out/'basis.c64'); y.tofile(out/'signal.c64')
m={'complex_type':'complex64','layout':'candidate,sample,column',
   'sample_rate':numerator/denominator,'sample_rate_numerator':numerator,'sample_rate_denominator':denominator,
   'signal_file':'signal.c64','signal_shape':[4],'basis_file':'basis.c64','basis_shape':[2,4,1],
   'source_format':{'calendar_origin_sample':0},'truth_arrival_seconds':.05,
   'truth_clock_ppm':0,'truth_frequency_hz':0,'truth_frequency_drift_hz_per_second':0,
   'endpoint_truth':dict.fromkeys(['tx_audio_ppm','rx_audio_ppm','tx_rf_ppm','rx_rf_ppm',
       'tx_phase_diffusion_rad_sqrt_s','rx_phase_diffusion_rad_sqrt_s'],0),
   'resolved_configuration':{'clock_policy_active':False,'oscillator_presets_active':False},
   'requested_active_seconds':8,'actual_elapsed_seconds':256,'channel_model':{'path_evolution':{}},
   'candidateparams':[{'bit':bit,'tap_count':1,'taps':[{'delay_seconds':0,'doppler_hz':0}]} for bit in (0,1)]}
(out/'manifest.json').write_text(json.dumps(m))
''')
        exporter.chmod(0o755)
        output = self.root/"sparse-sweep"
        command = [sys.executable, str(SCRIPT), "--binary", str(exporter), "--output", str(output),
                   "--rates", ".00036", "--sample-rates", "1", "--sample-periods", "16", "64",
                   "--profiles", "baseline", "--timeout", "5"]
        result = subprocess.run(command, capture_output=True, text=True, timeout=10,
                                env=dict(os.environ, OPENBLAS_NUM_THREADS="1", OMP_NUM_THREADS="1"))
        self.assertEqual(result.returncode, 0, (result.stdout,result.stderr))
        report = json.loads((output/"results.json").read_text())
        self.assertEqual(report["completed_exports"], 3)
        self.assertEqual(report["reference_sample_rate"], 1)
        self.assertEqual(len(report["comparisons"]), 2)
        self.assertEqual({c["reference_sample_rate"] for c in report["comparisons"]}, {1})
        self.assertEqual({c["sample_rate_denominator"] for c in report["cases"]}, {1,16,64})
        self.assertTrue((output/"rate-0.00036-baseline-fs-1over64"/"manifest.json").exists())
        self.assertFalse((output/"rate-0.00036-baseline-fs-1over64"/"basis.c64").exists())


@unittest.skipUnless(sys.platform.startswith("linux"), "Linux owned-group runner")
class RefinementLifecycleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="triad-refinement-cleanup-")
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def fake_tree(self, label):
        pids = self.root / (label + "-pids")
        descendant = ("import os, signal, time; signal.signal(signal.SIGTERM,signal.SIG_IGN); "
                      f"open({str(pids)!r},'a').write(str(os.getpid())+'\\n'); time.sleep(60)")
        binary = self.root / (label + "-exporter")
        binary.write_text("#!" + sys.executable + "\n"
                          "import os, signal, subprocess, sys, time\n"
                          "signal.signal(signal.SIGTERM,signal.SIG_IGN)\n"
                          f"open({str(pids)!r},'a').write(str(os.getpid())+'\\n')\n"
                          f"subprocess.Popen([sys.executable,'-c',{descendant!r}])\n"
                          "time.sleep(60)\n")
        binary.chmod(0o755)
        return binary, pids

    def wait_ready(self, pidfile):
        deadline = time.monotonic()+5
        while time.monotonic() < deadline:
            if pidfile.exists() and len(pidfile.read_text().splitlines()) == 2:
                return
            time.sleep(.02)
        self.fail("fake exporter and child did not become ready")

    def assert_gone(self, pidfile):
        pids = [int(row) for row in pidfile.read_text().splitlines()]
        self.assertEqual(len(pids), 2)
        for pid in pids:
            self.assertFalse(Path(f"/proc/{pid}").exists(), f"owned child {pid} remains")

    def test_success_requires_runner_cleanup_receipt(self):
        result = refinement.run([sys.executable, "-c", "print('complete')"], self.root/"success", 2, .1)
        self.assertEqual(result["status"], "complete")
        self.assertEqual(result["returncode"], 0)
        self.assertTrue(result["runner_status"]["termination"]["group_empty"])
        self.assertIn("complete", (self.root/"success"/"export.log").read_text())

    def test_timeout_reaps_term_ignoring_exporter_and_descendant(self):
        binary, pids = self.fake_tree("timeout")
        result = refinement.run([str(binary)], self.root/"timeout", .5, .1)
        self.assertEqual((result["status"], result["returncode"]), ("timeout", 124))
        self.assertTrue(result["runner_status"]["termination"]["kill_sent"])
        self.assertTrue(result["runner_status"]["termination"]["group_empty"])
        self.assert_gone(pids)

    def test_driver_int_and_term_forward_and_stop_new_exports(self):
        for sig in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signal=sig):
                binary, pids = self.fake_tree(str(sig))
                output = self.root / ("cancelled-" + str(sig))
                env = dict(os.environ, OPENBLAS_NUM_THREADS="1", OMP_NUM_THREADS="1")
                process = subprocess.Popen([sys.executable, str(SCRIPT), "--binary", str(binary),
                    "--output", str(output), "--rates", ".36", "--sample-rates", "1", "64",
                    "--profiles", "baseline", "--timeout", "10", "--term-grace", ".1"],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
                try:
                    self.wait_ready(pids)
                    process.send_signal(sig)
                    stdout, stderr = process.communicate(timeout=5)
                    self.assertEqual(process.returncode, 128+sig, (stdout, stderr))
                    report = json.loads((output/"results.json").read_text())
                    self.assertTrue(report["cancelled"])
                    self.assertEqual(len(report["cases"]), 1)
                    case = report["cases"][0]
                    self.assertEqual(case["status"], "cancelled")
                    self.assertEqual(case["runner_status"]["cancel_signal"], sig)
                    self.assertTrue(case["runner_status"]["termination"]["group_empty"])
                    self.assert_gone(pids)
                finally:
                    if process.poll() is None:
                        process.terminate()
                        process.communicate(timeout=10)


if __name__ == "__main__":
    unittest.main()
