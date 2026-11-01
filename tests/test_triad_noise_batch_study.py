#!/usr/bin/env python3
"""Serial/batched actual-observation parity for independent triad paths."""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import resource
import sys
import tempfile
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
sys.path.insert(0,str(ROOT))
from tests.test_triad_noise_study import write_workspace

SPEC = importlib.util.spec_from_file_location("triad_noise_batch_study",ROOT/"tools/triad-noise-batch-study.py")
batch = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(batch)


def common(paths, output):
    return [*(str(path) for path in paths),"--output",str(output),"--cn0","0",
            "--reference-variance","1","--taps","1","--phase-states","16",
            "--relative-phase-states","2","--frequency-bound","0",
            "--frequency-std","0","--phase-diffusion",".02","--precision","fp64",
            "--selection-methods","7","--calibration-alternatives","2",
            "--duration-alternatives","3"]


def fixture_paths(directory):
    paths = [Path(directory)/"p0",Path(directory)/"p1"]
    for i,path in enumerate(paths):
        path.mkdir()
        write_workspace(path,seed=52+i,observation=True,block_count=4,valid_rows_count=8-i)
        m=json.loads((path/"manifest.json").read_text())
        m.update(channel_seed=100+i,key_seed=200+i,epoch=300+i)
        (path/"manifest.json").write_text(json.dumps(m))
    return paths


class NoiseBatchTests(unittest.TestCase):
    @unittest.skipUnless(hasattr(resource,"RLIMIT_NOFILE"),"requires POSIX descriptor limits")
    def test_profile_validation_closes_many_calendars_and_mismatch_under_low_fd_limit(self):
        with tempfile.TemporaryDirectory() as directory:
            paths=[]
            for i in range(64):
                path=Path(directory)/f"p{i:03d}"
                path.mkdir()
                write_workspace(path,seed=1000+i,observation=True,block_count=1)
                paths.append(path)
            workspaces=[batch.serial.BlockWorkspace(path) for path in paths]
            old_soft,old_hard=resource.getrlimit(resource.RLIMIT_NOFILE)
            low=min(old_soft,24)
            if old_hard != resource.RLIM_INFINITY:
                low=min(low,old_hard)
            self.assertGreaterEqual(low,16)
            try:
                resource.setrlimit(resource.RLIMIT_NOFILE,(low,old_hard))
                batch._validate_profiles(workspaces)
                self.assertTrue(all(not w._maps for w in workspaces))
                np.array([[1,9]],dtype="<u8").tofile(paths[-1]/"intervals.u64")
                with self.assertRaisesRegex(batch.serial.WorkspaceError,"different block calendars"):
                    batch._validate_profiles(workspaces)
                self.assertTrue(all(not w._maps for w in workspaces))
            finally:
                resource.setrlimit(resource.RLIMIT_NOFILE,(old_soft,old_hard))

    def test_resident_fp64_scores_gates_and_decisions_match_block_transfers(self):
        with tempfile.TemporaryDirectory() as directory:
            paths=fixture_paths(directory)
            reports,values={},{}
            for mode in ("block","resident"):
                output=Path(directory)/(mode+".json")
                args=batch._parse_args([*common(paths,output),"--batch-paths","2",
                    "--transfer-mode",mode,"--frequency-bound",".125",
                    "--frequency-step",".0625","--frequency-model","static",
                    "--frequency-random-walk","0"])
                reports[mode]=batch.run(args)
                self.assertEqual(reports[mode]["failed_physical_capture_records"],0)
                with np.load(output.with_suffix(".scores.npz")) as data:
                    values[mode]={key:data[key].copy() for key in data.files}
            self.assertEqual(set(values["block"]),set(values["resident"]))
            for key in values["block"]:
                np.testing.assert_allclose(values["resident"][key],values["block"][key],atol=2e-12,rtol=2e-12)
            for left,right in zip(reports["block"]["captures"],reports["resident"]["captures"]):
                self.assertEqual(left["methods"],right["methods"])
            left,right=reports["block"]["shards"][0],reports["resident"]["shards"][0]
            self.assertEqual(left["input_tensor_staging_operations"],4*4)
            self.assertEqual(right["input_tensor_staging_operations"],4)
            self.assertEqual(left["resident_input_tensor_bytes"],0)
            self.assertGreater(right["resident_input_tensor_bytes"],0)
            self.assertEqual(right["inference_sync_scope"],"whole observation")

    def test_resident_device_cache_and_packing_are_charged_before_allocation(self):
        with tempfile.TemporaryDirectory() as directory:
            paths=fixture_paths(directory)
            args=batch._parse_args(common(paths,Path(directory)/"resident-memory.json"))
            self.assertEqual(args.transfer_mode,"block")
            workspace=batch.serial.BlockWorkspace(paths[0])
            _,mapping,_=batch.serial._selected_candidates(workspace,1)
            block=batch._memory_guard(args,workspace,mapping,2)
            args.transfer_mode="resident"
            resident=batch._memory_guard(args,workspace,mapping,2)
            extra=sum(resident[key] for key in ("resident_selected_device_cache_bytes",
                "resident_cpu_packing_peak_bytes","resident_device_transfer_scratch_bytes"))
            self.assertGreater(extra,0)
            self.assertEqual(resident["conservative_total_bytes"],block["conservative_total_bytes"]+extra)
            args.memory_limit_bytes=(block["conservative_total_bytes"]+resident["conservative_total_bytes"])//2
            with self.assertRaisesRegex(MemoryError,"memory estimate"):
                batch._memory_guard(args,workspace,mapping,2)

    def test_actual_independent_paths_match_serial_and_shard_sizes(self):
        with tempfile.TemporaryDirectory() as directory:
            paths=fixture_paths(directory)
            serial_args=batch.serial._parse_args([
                "--mode","actual","--calibration-scope","stationary",
                *common(paths,Path(directory)/"serial.json"),
                "--methods","phase-frequency","phase-frequency-ucb"])
            serial_report=batch.serial.run(serial_args)
            self.assertTrue(all(c["status"]=="completed" for c in serial_report["captures"]))
            with np.load(Path(directory)/"serial.scores.npz") as data:
                reference={key:data[key].copy() for key in data.files}
            for shard_size in (1,2):
                output=Path(directory)/f"batch{shard_size}.json"
                args=batch._parse_args([*common(paths,output),"--batch-paths",str(shard_size)])
                report=batch.run(args)
                self.assertEqual(report["failed_physical_capture_records"],0)
                self.assertEqual(len(report["captures"]),2)
                self.assertEqual(len(report["shards"]),2 if shard_size==1 else 1)
                with np.load(output.with_suffix(".scores.npz")) as data:
                    self.assertEqual(set(data.files),set(reference))
                    for key in data.files:
                        np.testing.assert_allclose(data[key],reference[key],atol=2e-12,rtol=2e-12)
                for actual,expected in zip(report["captures"],serial_report["captures"]):
                    for method in batch.METHODS:
                        self.assertEqual(actual["methods"][method]["counts"],expected["methods"][method]["counts"])
                        self.assertEqual(actual["methods"][method]["threshold"],expected["methods"][method]["threshold"])
                    self.assertEqual(len(actual["input_hashes"]),6)

    def test_duplicate_paths_and_memory_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            paths=fixture_paths(directory)
            args=batch._parse_args(common([paths[0],paths[0]],Path(directory)/"duplicate.json"))
            with self.assertRaisesRegex(batch.serial.WorkspaceError,"duplicate physical workspace"):
                batch.run(args)
            args=batch._parse_args([*common(paths,Path(directory)/"memory.json"),"--memory-gib",".001"])
            with self.assertRaisesRegex((MemoryError,ValueError),"memory"):
                batch.run(args)

    def test_one_failed_calibration_path_does_not_hide_or_drop_the_other(self):
        with tempfile.TemporaryDirectory() as directory:
            paths=fixture_paths(directory)
            write_workspace(paths[0],seed=52,observation=True,block_count=4,valid_rows_count=2)
            args=batch._parse_args(common(paths,Path(directory)/"partial.json"))
            report=batch.run(args)
            self.assertEqual(report["failed_physical_capture_records"],1)
            self.assertEqual([c["status"] for c in report["captures"]],["error","completed"])
            self.assertIn("input_hashes",report["captures"][0])
            self.assertEqual(report["captures"][0]["control"],"signal")
            self.assertEqual(report["captures"][0]["truth_bit_simulation_only"],0)
            self.assertEqual(report["captures"][1]["physical_path_index"],1)
            self.assertEqual(report["attempted_physical_capture_records"],2)
            self.assertEqual(report["completed_physical_capture_records"],1)
            for method in batch.METHODS:
                summary=report["summary_by_control"]["signal"][method]
                self.assertEqual(summary["attempted_physical_paths"],2)
                self.assertEqual(summary["completed_physical_paths"],1)
                self.assertEqual(summary["aggregate_counts"]["captures"],2)
                self.assertGreaterEqual(summary["aggregate_counts"]["missed"],1)
                self.assertEqual(summary["completed_only_diagnostic"]["aggregate_counts"]["captures"],1)

    def test_signal_failure_success_bound_and_failed_null_qualification(self):
        method="phase-frequency"
        success={"status":"completed","control":"signal","methods":{method:{"counts":{
            "captures":1,"correct_accepted":1,"wrong_accepted":0,"missed":0,"raw_correct":1}}}}
        failure={"status":"error","control":"signal","methods":{}}
        result=batch._attempt_summary([success,failure],method,"signal")
        self.assertEqual(result["path_mean_fraction"],.5)
        self.assertEqual(result["aggregate_counts"]["missed"],1)
        self.assertEqual(result["completed_only_diagnostic"]["path_mean_fraction"],1.)
        self.assertEqual(result["independent_physical_paths"],2)
        null={"status":"completed","control":"null","methods":{method:{"counts":{
            "captures":1,"false_admitted":0,"not_admitted":1}}}}
        result=batch._attempt_summary([null,{**failure,"control":"null"}],method,"null")
        self.assertEqual(result["attempted_physical_paths"],2)
        self.assertEqual(result["valid_null_qualification_trials"],1)
        self.assertEqual(result["aggregate_counts"]["captures"],1)
        self.assertFalse(result["null_qualification_complete"])
        self.assertIsNone(result["false_admission_qualification_interval"])

    def test_graph_cache_guard_covers_all_working_pools_and_aggregate_resident(self):
        with tempfile.TemporaryDirectory() as directory:
            paths=fixture_paths(directory)
            args=batch._parse_args(common(paths,Path(directory)/"graph-memory.json"))
            workspace=batch.serial.BlockWorkspace(paths[0])
            _,mapping,_=batch.serial._selected_candidates(workspace,1)
            reference=batch._memory_guard(args,workspace,mapping,2)
            args.phase_implementation="graph"
            graph=batch._memory_guard(args,workspace,mapping,2)
            self.assertGreaterEqual(graph["graph_working_factor"],6)
            self.assertEqual(graph["graph_cache_entries_bound"],4)
            self.assertGreater(graph["persistent_graph_input_state_bytes"],0)
            self.assertGreaterEqual(graph["phase_state_working_bound_bytes"],
                                    6*reference["phase_state_working_bound_bytes"]+
                                    graph["persistent_graph_input_state_bytes"])
            old_three_bound=reference["conservative_total_bytes"]+2*reference["phase_state_working_bound_bytes"]
            args.memory_limit_bytes=(old_three_bound+graph["conservative_total_bytes"])//2
            self.assertGreater(args.memory_limit_bytes,old_three_bound)
            with self.assertRaisesRegex(MemoryError,"memory estimate"):
                batch._memory_guard(args,workspace,mapping,2)

    def test_unsupported_modes_and_missing_actual_energy_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            paths=fixture_paths(directory)
            args=batch._parse_args(common(paths,Path(directory)/"missing.json"))
            (paths[0]/"received_energy.f64").unlink()
            with self.assertRaises(batch.serial.WorkspaceError):
                batch.run(args)
            with self.assertRaises(SystemExit):
                batch._parse_args([*common(paths,Path(directory)/"bad.json"),"--mode","projected"])


if __name__=="__main__":
    unittest.main()
