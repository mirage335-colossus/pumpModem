"""Cheap orchestration/safety contracts; never open a physical audio device."""
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import triad_live_duplex as d


def endpoint(kind="capture", **extra):
    return dict(name="physical-"+kind, active_port="analog-input-mic" if kind=="capture" else "analog-output-headphones",
                properties={"alsa.card":"1", "alsa.device":"0", "device.class":"sound"},
                mute=False, volume={"front-left":{"value":9830}}, **extra)


def metadata():
    return dict(schema="triad-audio-capture-v1", status="complete",
                discontinuity=dict(capture=False, playback=False),
                audio_format=dict(capture={"format":"S16_LE"}, playback={"format":"S16_LE"}))


def make_capture_fixture(root, name="training", x=None, y=None, *, bind_metadata=False):
    prepared, capture, output = root/"prep", root/"cap", root/"out"
    for p in (prepared/name, capture/name, output):p.mkdir(parents=True,exist_ok=True)
    x=np.arange(128,dtype="<f4")/1000 if x is None else np.asarray(x,dtype="<f4")
    y=x.copy() if y is None else np.asarray(y,dtype="<f4")
    x.tofile(prepared/name/"stimulus.f32")
    y.tofile(capture/name/"capture.f32")
    h=d.digest(prepared/name/"stimulus.f32")
    d.json_write(prepared/name/"stimulus.json",dict(markers={"training":True},stimulus_sha256=h,
                 sample_rate=d.FS,samples=len(x)))
    m=metadata()
    m.update(requested=dict(rate=d.FS,capture_device="hw:1,0",playback_device="hw:1,0",exclusive=True),
             capture=dict(logical_samples=len(y),logical_rate=d.FS))
    d.json_write(capture/name/"capture.json",m)
    argv=["adapter","--live","--exclusive","--input",str(prepared/name/"stimulus.f32"),
          "--output",str(capture/name/"capture.f32"),"--metadata",str(capture/name/"capture.json"),
          "--rate",str(d.FS),"--capture-device","hw:1,0","--playback-device","hw:1,0"]
    d.json_write(capture/name/"command.json",dict(exit_code=0,argv=argv))
    receipt=dict(stimulus_sha256=h,capture_sha256=d.digest(capture/name/"capture.f32"))
    if bind_metadata:receipt["capture_metadata_sha256"]=d.digest(capture/name/"capture.json")
    d.json_write(capture/name/"capture-identity.json",receipt)
    return prepared,capture,output


class CampaignTests(unittest.TestCase):
    def test_budget_counts_actual_stimuli_capture_overhead_and_reserve(self):
        p = d.preflight_budget([10,20],100,50)
        self.assertEqual(p["planned_total_seconds"],96)
        with self.assertRaises(d.BudgetExceeded):
            d.preflight_budget([10,20],95,50)
        with self.assertRaises(ValueError):
            d.preflight_budget([float("nan")],100,0)

    def test_monotonic_deadline_limits_subprocess_budget(self):
        values = iter([100,103,111])
        limit = d.Deadline(10, clock=lambda:next(values))
        self.assertEqual(limit.timeout(90),7)
        with self.assertRaises(d.BudgetExceeded):
            limit.remaining()

    def test_no_live_flag_means_no_output_and_no_device_discovery(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.object(d, "discover_route") as discover:
            with self.assertRaisesRegex(ValueError,"--live"):
                d.main(["run","--prepared",tmp,"--output",str(Path(tmp)/"out")])
            discover.assert_not_called()
            self.assertFalse((Path(tmp)/"out").exists())

    def test_physical_route_preserves_settings_and_rejects_wrong_devices(self):
        x = endpoint()
        before = json.dumps(x,sort_keys=True)
        e = d.physical_endpoint([x],x["name"],"capture","hw:1,0")
        self.assertEqual(e["device"],"hw:1,0")
        self.assertEqual(before,json.dumps(x,sort_keys=True))
        with self.assertRaisesRegex(ValueError,"does not match"):
            d.physical_endpoint([x],x["name"],"capture","hw:0,0")

    def test_monitor_virtual_and_speaker_routes_cannot_silently_fallback(self):
        for item in [endpoint(monitor_of_sink=1),
                     {**endpoint(), "properties":{}},
                     {**endpoint("playback"), "active_port":"analog-output-speaker"}]:
            kind = "playback" if item["name"].endswith("playback") else "capture"
            with self.assertRaises(ValueError):
                d.physical_endpoint([item],item["name"],kind)

    def test_incomplete_capture_xrun_and_missing_formats_are_failures(self):
        d.validate_capture(metadata())
        for change in [dict(status="incomplete"),dict(discontinuity={"capture":True,"playback":False}),dict(audio_format={})]:
            with self.assertRaises(ValueError):
                d.validate_capture({**metadata(), **change})

    def test_exclusive_outputs_and_failed_stage_remain_visible(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            d.json_write(out/"f.json",{"x":1})
            with self.assertRaises(FileExistsError):
                d.json_write(out/"f.json",{"x":2})
            stages = d.Stages(out,d.Deadline(10))
            with self.assertRaisesRegex(ValueError,"fixture"):
                with stages.stage("failed-fixture"):
                    raise ValueError("fixture")
            record = d.json_read(out/"stages/001.json")
            self.assertEqual(record["status"],"failed")
            self.assertGreaterEqual(record["wall_seconds"],0)
            self.assertIn("cpu_seconds",record)
            self.assertIn("started_utc_unix_seconds",record)

    def test_subprocess_timeout_is_bounded_without_retry(self):
        with self.assertRaises(subprocess.TimeoutExpired):
            d.execute([sys.executable,"-c","import time;time.sleep(5)"],d.Deadline(2),timeout=.03)
        self.assertEqual(d.execute([sys.executable,"-c","print('literal $HOME')"],d.Deadline(2)).strip(),"literal $HOME")

    def test_matrix_contains_real_duration_and_boundary_regimes(self):
        cases = d.case_definitions()
        self.assertEqual(cases[0]["kind"],"training")
        self.assertEqual([c["kind"] for c in cases[:4]],["training","training_low","validation_low","probe"])
        self.assertFalse(any(c["kind"]=="training_low" for c in d.case_definitions("smoke")))
        self.assertEqual(sum(c["kind"]=="training" for c in cases),1)
        lows = [c for c in cases if c.get("partial")]
        self.assertEqual(len(lows),8)
        self.assertTrue(all(c["window_seconds"]==24 for c in lows))
        self.assertTrue(any(c.get("rate")==1 and c.get("added")==1000 for c in cases))
        burst = next(c for c in cases if c["id"]=="fhss-burst3")
        self.assertEqual(burst["requested_seconds"],40)
        self.assertEqual(burst["burst_wait_tiles"],3)
        self.assertEqual({c["modulation"] for c in cases if c["kind"]=="native"}, {"off","each-25","each-65"})
        self.assertTrue(any(c.get("carrier_hz")==16000 for c in cases))

    def test_native_args_keep_encryption_epoch_and_real_burst(self):
        c = next(c for c in d.case_definitions() if c["id"]=="fhss-burst3")
        args = d.native_args(c,12345)
        self.assertNotIn("--no-encrypt",args)
        self.assertEqual(args[args.index("--epoch")+1],"12345")
        self.assertIn("--burst-wait-tiles",args)
        self.assertNotIn("--symbol-burst",args)

    def test_frozen_input_tampering_and_training_order_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p/"input").write_bytes(b"original")
            d.json_write(p/"prepared.json",{"files_sha256":{"input":d.digest(p/"input")}})
            d.json_write(p/"plan.json",{"schema":d.SCHEMA,"cases":[{"kind":"training"}]})
            d.verify_prepared(p,d.Deadline(5))
            (p/"input").write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError,"identity"):
                d.verify_prepared(p,d.Deadline(5))
            (p/"plan.json").write_text(json.dumps({"schema":d.SCHEMA,"cases":[{"kind":"probe"}]}))
            with self.assertRaisesRegex(ValueError,"first training"):
                d.verify_prepared(p,d.Deadline(5))

    def test_training_fit_quantizes_native_tx_and_excludes_holdout(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            prepared,capture,output=make_capture_fixture(p)
            with mock.patch.object(d.model,"fit_audio_profile",return_value={}) as fit:
                result = d.fit_training(prepared,capture,output)
            self.assertEqual(fit.call_args.kwargs,{"input_pcm":"s16-native"})
            self.assertFalse(result["campaign_training_identity"]["fitting_uses_holdouts"])
            self.assertTrue((output/"audio-profile.json").exists())
            freeze=d.json_read(output/"profile-freeze.json")
            self.assertEqual(freeze["profile_sha256"],d.digest(output/"audio-profile.json"))
            self.assertEqual(freeze["holdout_captures_used"],0)
            with mock.patch.object(d.model,"fit_audio_profile",return_value={}):
                with self.assertRaises(FileExistsError):d.fit_training(prepared,capture,output)

    def test_low_frequency_subcycle_component_is_never_qualified(self):
        fs, seconds = 100,420
        t = np.arange(fs*seconds)/fs
        x = .02*np.sin(2*np.pi*.0005*t+.2)+.02*np.sin(2*np.pi*.005*t+.7)+.02*np.sin(2*np.pi*.05*t)
        result = d.low_frequency_diagnostic(x,.2*x,np.ones(len(x),bool),[.0005,.005,.05,997],fs)
        self.assertFalse(result["rows"][0]["independently_resolved"])
        self.assertIsNone(result["confidence_interval"])
        self.assertFalse(result["rows"][0]["envelope_is_confidence_bound"])
        self.assertAlmostEqual(result["rows"][1]["response_amplitude"],.2,places=6)

    def test_unsafe_pcm_rejects_without_normalizing(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/"a.f32"
            with self.assertRaises(ValueError):d.write_samples(p,np.array([1.1]))
            self.assertFalse(p.exists())
            d.write_samples(p,np.array([.1,-.2]))
            np.testing.assert_allclose(np.fromfile(p,dtype="<f4"),[.1,-.2])

    def test_carrier_model_outside_training_band_is_unidentified(self):
        profile={"measured_frequency_support_hz":[.25,21000.]}
        with mock.patch.object(d.model,"spectral_response",return_value=np.ones(3,dtype=complex)*.5):
            result=d.carrier_response_report(profile,.005,.002,.006)
        self.assertFalse(result["independent_carrier_measurements"])
        self.assertTrue(all(not row["inside_measured_frequency_extent"] for row in result["carriers"]))
        self.assertFalse(result["complete_nominal_band_inside_measured_extent"])

    def test_receipt_verification_detects_raw_and_metadata_changes(self):
        with tempfile.TemporaryDirectory() as tmp:
            prepared,capture,_=make_capture_fixture(Path(tmp),bind_metadata=True)
            result=d.verify_capture_receipt(prepared/"training",capture/"training")
            self.assertTrue(result["metadata_hash_bound_at_capture"])
            p=capture/"training/capture.json"
            m=d.json_read(p); m["capture"]["logical_samples"]+=1
            p.write_text(json.dumps(m))
            with self.assertRaisesRegex(ValueError,"hash mismatch"):
                d.verify_capture_receipt(prepared/"training",capture/"training")

    def test_legacy_metadata_is_consistency_checked_not_retroactively_bound(self):
        with tempfile.TemporaryDirectory() as tmp:
            prepared,capture,_=make_capture_fixture(Path(tmp))
            result=d.verify_capture_receipt(prepared/"training",capture/"training")
            self.assertFalse(result["metadata_hash_bound_at_capture"])
            p=capture/"training/capture.json"
            m=d.json_read(p); m["capture"]["logical_samples"]+=1
            p.write_text(json.dumps(m))
            with self.assertRaisesRegex(ValueError,"sample-count"):
                d.verify_capture_receipt(prepared/"training",capture/"training")

    def test_freeze_hash_and_training_capture_identity_are_required(self):
        with tempfile.TemporaryDirectory() as tmp:
            prepared,capture,output=make_capture_fixture(Path(tmp))
            with mock.patch.object(d.model,"fit_audio_profile",return_value={}):
                profile=d.fit_training(prepared,capture,capture)
            result=d.verify_profile_receipt(prepared,capture,profile)
            self.assertFalse(result["chronological_validation"])
            (capture/"stages").mkdir()
            frozen=profile["campaign_training_identity"]["frozen_utc_unix_seconds"]
            d.json_write(capture/"stages/001.json",dict(name="capture-holdout",started_utc_unix_seconds=frozen+1))
            self.assertTrue(d.verify_profile_receipt(prepared,capture,profile)["chronological_validation"])
            receipt=d.json_read(capture/"profile-freeze.json");receipt["profile_sha256"]="changed"
            (capture/"profile-freeze.json").write_text(json.dumps(receipt))
            with self.assertRaisesRegex(ValueError,"profile hash"):
                d.verify_profile_receipt(prepared,capture,profile)

    def test_missing_freeze_is_explicitly_posthoc(self):
        with tempfile.TemporaryDirectory() as tmp:
            prepared,capture,_=make_capture_fixture(Path(tmp))
            with mock.patch.object(d.model,"fit_audio_profile",return_value={}):
                profile=d.fit_training(prepared,capture,capture)
            (capture/"profile-freeze.json").unlink()
            report=d.verify_profile_receipt(prepared,capture,profile)
            self.assertEqual(report["status"],"posthoc_unwitnessed_profile_freeze")
            self.assertFalse(report["chronological_validation"])

    def test_combined_training_profile_binds_both_training_captures(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)
            prepared,capture,_=make_capture_fixture(p)
            make_capture_fixture(p,"training-low")
            section={"schema":"fixture-low-frequency-section"}
            with mock.patch.object(d.model,"fit_low_frequency_section",return_value=section) as low, \
                 mock.patch.object(d.model,"fit_audio_profile",return_value={}) as broad:
                profile=d.fit_training(prepared,capture,capture)
            low.assert_called_once()
            self.assertEqual(low.call_args.kwargs["model_order"],3)
            self.assertEqual(broad.call_args.kwargs["low_frequency_section"],section)
            self.assertEqual(profile["campaign_training_identity"]["low_frequency_training"]["capture_sha256"],
                             d.digest(capture/"training-low/capture.f32"))
            result=d.verify_profile_receipt(prepared,capture,profile)
            self.assertIn("low_frequency_training",result)
            (capture/"training-low/capture.f32").write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError,"raw capture"):
                d.verify_profile_receipt(prepared,capture,profile)

    def test_declared_four_pole_model_is_selected_and_legacy_order_stays_three(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);prepared,capture,_=make_capture_fixture(p)
            make_capture_fixture(p,"training-low")
            self.assertEqual(d.declared_low_frequency_model_order(prepared),3)
            d.json_write(prepared/"plan.json",{"low_frequency_model_order":4})
            with mock.patch.object(d.model,"fit_low_frequency_section",return_value={}) as low, \
                 mock.patch.object(d.model,"fit_audio_profile",return_value={}):
                profile=d.fit_training(prepared,capture,capture)
            self.assertEqual(low.call_args.kwargs["model_order"],4)
            self.assertEqual(profile["campaign_training_identity"]["low_frequency_training"]["model_order"],4)
            (prepared/"plan.json").write_text(json.dumps({"low_frequency_model_order":8}))
            with self.assertRaisesRegex(ValueError,"3 or 4"):
                d.declared_low_frequency_model_order(prepared)

    def test_payload_metrics_reuse_full_prediction_and_exclude_markers(self):
        with tempfile.TemporaryDirectory() as tmp:
            x=np.ones(512,dtype=np.float32)*.2;x[128:384]=.001
            y=x.copy();y[128:384]*=.1
            prepared,capture,output=make_capture_fixture(Path(tmp),"holdout",x,y)
            meta=d.json_read(prepared/"holdout/stimulus.json")
            meta.update(details={},markers=dict(markers=[],payload=dict(start=128,samples=256),segments=[]))
            (prepared/"holdout/stimulus.json").write_text(json.dumps(meta))
            profile={"schema":d.model.SCHEMA,"sample_rate":d.FS,"highpass_cutoff_hz":0.,"fir":[1.]}
            d.json_write(output/"audio-profile.json",profile)
            with mock.patch.object(d.model,"align_capture",return_value=(y,np.ones(len(x),bool),{})), \
                 mock.patch.object(d.model,"simulate_audio_profile",return_value=x) as simulate, \
                 mock.patch.object(d.model,"apply_audio_profile",side_effect=AssertionError("must reuse prediction")):
                report=d.analyze_case(dict(id="holdout",kind="probe"),prepared,capture,output,profile,d.Deadline(5))
            simulate.assert_called_once()
            self.assertLess(abs(report["energy_and_model_residual"]["energy_prediction_error_db"]),.01)
            self.assertLess(report["payload_energy_and_model_residual"]["energy_prediction_error_db"],-19.9)
            self.assertEqual(report["payload_energy_and_model_residual"]["samples"],256)

    def test_quiet_psd_checks_declared_zero_input_without_gaussian_claim(self):
        fs=48000;n=fs*2
        rng=np.random.default_rng(91)
        noise=rng.normal(0,.001,n)
        tx=np.zeros(n);prediction=np.zeros(n)
        profile={"noise":{"frequency_hz":[0,fs/2],"psd_fs_squared_per_hz":[2e-6/fs,2e-6/fs]}}
        manifest={"segments":[{"kind":"quiet","start":0,"samples":n}]}
        result=d.quiet_noise_check(tx,noise,prediction,np.ones(n,bool),manifest,profile,fs)
        self.assertEqual(len(result["bands"]),5)
        self.assertIsNone(result["confidence_interval"])
        self.assertLess(abs(result["bands"][2]["observed_to_training_db"]),.4)
        tx[10]=1
        with self.assertRaisesRegex(ValueError,"nonzero"):
            d.quiet_noise_check(tx,noise,prediction,np.ones(n,bool),manifest,profile,fs)

    def test_continuation_reuses_verified_capture_and_leaves_failed_case_fresh(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);prepared,origin,_=make_capture_fixture(p)
            make_capture_fixture(p,"holdout")
            plan={"cases":[{"id":"training","kind":"training"},{"id":"holdout","kind":"probe"},
                           {"id":"failed","kind":"native"},{"id":"missing","kind":"native"}]}
            d.json_write(prepared/"plan.json",plan);d.json_write(prepared/"prepared.json",{})
            d.json_write(origin/"input-identity.json",dict(prepared_sha256=d.digest(prepared/"prepared.json"),
                plan_sha256=d.digest(prepared/"plan.json"),capture_backend="alsa-tools"))
            d.json_write(origin/"summary.json",dict(status="failed",whole_command_wall_seconds=100.,result={"error":"device busy"}))
            with mock.patch.object(d.model,"fit_audio_profile",return_value={}):profile=d.fit_training(prepared,origin,origin)
            (origin/"stages").mkdir()
            frozen=profile["campaign_training_identity"]["frozen_utc_unix_seconds"]
            d.json_write(origin/"stages/001.json",dict(name="capture-holdout",started_utc_unix_seconds=frozen+1))
            (origin/"failed").mkdir();d.json_write(origin/"failed/capture.json",dict(status="incomplete",failure="busy"))
            output=p/"continuation";output.mkdir()
            before=d.digest(origin/"summary.json")
            _,reused,receipt=d.prepare_resume(prepared,origin,output,plan,d.Deadline(10))
            self.assertEqual(reused,{"training","holdout"})
            self.assertTrue((output/"training").is_symlink())
            self.assertFalse((output/"failed").exists())
            self.assertFalse((output/"missing").exists())
            self.assertEqual(receipt["prior_capture_run_active_seconds"],100.)
            self.assertEqual(before,d.digest(origin/"summary.json"))
            self.assertEqual(d.digest(output/"audio-profile.json"),d.digest(origin/"audio-profile.json"))
            (prepared/"prepared.json").write_text("{\"changed\":true}")
            second=p/"second";second.mkdir()
            with self.assertRaisesRegex(ValueError,"prepared-input identity"):
                d.prepare_resume(prepared,origin,second,plan,d.Deadline(10))

    def test_public_timing_diagnostic_uses_declared_prior_not_truth(self):
        y=np.ones(128,dtype=np.complex64);bank=np.ones((2,128,1),dtype=np.complex64)
        manifest={"truth_arrival_seconds":1000.,"resolved_configuration":{"initial_arrival_bounds_seconds":[-.09,.09]}}
        with mock.patch.object(d.wave,"bounded_timing_projection",return_value={"chosen_bit":1},create=True) as method:
            result=d.public_timing_diagnostics(y,y,bank,960.,manifest,np.ones(128,bool),[0,1])
        self.assertEqual(method.call_count,2)
        self.assertEqual(method.call_args.args[3],[-.09,.09])
        self.assertFalse(result["truth_alignment_used"])
        self.assertTrue(result["original_nominal_metrics_preserved"])
        self.assertIsNone(result["operational_admission"])

    def test_zero_and_unbounded_timing_regimes_do_not_dispatch_search(self):
        y=np.ones(16,dtype=np.complex64);bank=np.ones((2,16,1),dtype=np.complex64)
        with mock.patch.object(d.wave,"bounded_timing_projection",create=True) as method:
            zero=d.public_timing_diagnostics(y,y,bank,960.,{"resolved_configuration":{"initial_arrival_bounds_seconds":[0,0]}},np.ones(16,bool),[0,1])
            large=d.public_timing_diagnostics(y,y,bank,48000.,{"resolved_configuration":{"initial_arrival_bounds_seconds":[-7,7]}},np.ones(16,bool),[0,1])
        method.assert_not_called()
        self.assertEqual(zero["status"],"nominal_zero_prior")
        self.assertEqual(large["status"],"unsupported_public_timing_regime")


if __name__=="__main__":unittest.main()
