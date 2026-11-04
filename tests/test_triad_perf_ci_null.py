from __future__ import annotations
import copy
from contextlib import redirect_stdout
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
import triad_perf_ci_null as n


class NullCampaignTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.a=self.root/"candidate";self.b=self.root/"reference"
        files={}
        for side in [self.a,self.b]:
            for name in n.RUNTIME_FILES:
                p=side/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text("frozen "+name)
                files[name]=n.file_sha(p)
            for name in n.BINARY_FILES:
                p=side/"build/dev-cli"/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text("binary "+name)
        self.spec=n.default_spec(self.a,"a"*40,source_manifest={"files":files},archive_sha256="b"*64)
        self.bindings={"candidate":str(self.a),"reference":str(self.b),"python":sys.executable,
            "binary":str(self.a/"build/dev-cli/triad_long"),"reference_binary":str(self.b/"build/dev-cli/triad_long"),
            "planner_binary":str(self.a/"build/dev-cli/triad_planner_perf"),"planner_reference_binary":str(self.b/"build/dev-cli/triad_planner_perf"),
            "bounds_workspace":str(self.a/"bounds"),"fixture_cache":str(self.root/"cache")}
        self.manifest=n.make_manifest(self.spec,2,2)
        self.calls=[];self.unit_failed=False;self.guard_stat=False;self.bad_artifact=False

    def hardware(self):
        return {"observed_usable_vcpus":16,"avx2_flag_reported":True,"fma_flag_reported":True,"physical_host_identity_collected":False}

    def stage(self,stage,bindings,output,env,deadline):
        self.calls.append((stage["id"],dict(env)))
        out=output/(stage["id"]+".stdout");err=output/(stage["id"]+".stderr");err.write_text("")
        code=0
        if stage["id"]=="unit":
            out.write_text("Ran 2 tests in 0.01s\n\nFAILED (failures=1)\nnoise\nRan 519 tests in 0.2s\n\n"+("FAILED (failures=1)\n" if self.unit_failed else "OK (skipped=3)\n"))
            code=int(self.unit_failed)
        else:
            out.write_text("guard\n");code=2 if self.guard_stat else 0
            self.guard(output,code)
        values={**bindings,"output":str(output)}
        return {"id":stage["id"],"argv":[x.format_map(values) for x in stage["argv"]],"cwd":stage["cwd"].format_map(values),"status":"passed" if not code else "failed","exit_code":code,"stdout":out.name,"stderr":err.name,"wall_seconds":.2}

    def guard(self,output,code):
        source={}
        for side,root in [("candidate",self.a),("reference",self.b)]:
            source.update({side+":"+name:n.file_sha(root/name) for name in n.RUNTIME_FILES})
        source.update({"binary:candidate":n.file_sha(self.bindings["binary"]),"binary:reference":n.file_sha(self.bindings["reference_binary"]),
            "binary:candidate_planner":n.file_sha(self.bindings["planner_binary"]),"binary:reference_planner":n.file_sha(self.bindings["planner_reference_binary"])})
        metadata={"threads":2,"native_threads":1,"precision":"fp32","functional_defaults_requested":True,
            "randomization_inference_eligible":True,"case_selection":"all declared applicable workload families"}
        plan=n.seal({"schema":"triad-performance-reject-only-guard-v3.plan","metadata":metadata,
            "policy":{"suite_alpha":.01,"workflow_job_count":1},"threshold":1.7})
        screen=n.seal({"schema":"screen","hypothesis_id":"case","nominate_confirmation":self.guard_stat})
        decision=n.seal({"schema":"triad-performance-reject-only-guard-v3.report","plan_sha256":plan["record_sha256"],
            "screens":[screen],"confirmations":[],"futility_records":[],"futile_confirmations":[],"missing_screens":[],
            "unconfirmed_nominations":[],"confirmation_effort_limited":[],"confirmed_regressions":["case"] if self.guard_stat else [],
            "functional_failures":[],"exit_code":code})
        summary={"guard_decision":decision,"plan_sha256":plan["record_sha256"],"profile":"ci","device":"cpu","budget_seconds":600.,
            "hypothesis_count":33,"source_integrity":{"status":"passed"},"source_identity":{"sha256":source},"exit_code":code,"wall_seconds":100.,"status":"blocked" if code else "no_confirmed_regression"}
        if self.bad_artifact:summary["guard_decision"]["record_sha256"]="wrong"
        n.write(output/"guard/plan.json",plan);n.write(output/"guard/summary.json",summary)

    def execute(self,repeat=0,phase="validation"):
        row=self.manifest["schedule"][repeat]
        return n.run_workflow(self.spec,self.bindings,self.root/("out-"+str(len(self.calls))),phase=phase,
            run_id=row["run_id"] if phase=="validation" else "baseline-001",cluster_id=row["cluster_id"],repeat=row["repeat"],
            manifest_sha256=self.manifest["record_sha256"] if phase=="validation" else None,
            stage_runner=self.stage,hardware_reader=self.hardware,version_reader=lambda *args:dict(n.EXPECTED_STACK))

    def synthetic(self,manifest,row,block=False):
        bindings=dict(self.bindings);output=str(self.root/row["run_id"])
        closure=n._source_check(n.verify(self.spec),bindings)
        stages=[{"id":stage["id"],"argv":[x.format_map({**bindings,"output":output}) for x in stage["argv"]],
                 "cwd":stage["cwd"].format_map(bindings),"status":"passed","exit_code":0} for stage in n.verify(self.spec)["stages"]]
        return n.seal({"schema":n.SCHEMA+".observation","phase":"validation","run_id":row["run_id"],"cluster_id":row["cluster_id"],"repeat":row["repeat"],
            "manifest_sha256":manifest["record_sha256"],"spec_sha256":self.spec["record_sha256"],"status":"complete",
            "started_utc":"2099-01-01T00:00:00+00:00","python_stack":dict(n.EXPECTED_STACK),"stages":stages,
            "path_bindings":bindings,"output_root":output,"unit_test_count":519,"hardware":self.hardware(),
            "source_before":closure,"source_after":closure,"fixture_cache_before":{"exists":False,"file_count":0},
            "controlled_environment":{k:v.format_map(bindings) for k,v in self.spec["environment"].items()},
            "outcomes":{**{key:False for key in n.CATEGORIES},"all_cause_block":block,"infrastructure_incomplete":block,"observed_exit_block":False},
            "guard":{"reported":not block,"statistical_rejection":False,"artifact_sha256":{"summary":"a"*64,"plan":"b"*64}}})

    def test_baseline_is_retained_but_excluded_and_schedule_is_fixed(self):
        baseline=self.execute(phase="baseline")
        manifest=n.make_manifest(self.spec,2,2,baselines=[baseline])
        self.assertEqual(n.validate_manifest(manifest)["scheduled_workflows"],4)
        result=n.analyze(manifest,[baseline])
        self.assertEqual(result["counts"]["all_cause_block"],4)
        self.assertEqual(result["excluded_baseline_ids"],["baseline-001"])
        changed=copy.deepcopy(manifest);changed["schedule"].pop();changed.pop("record_sha256")
        with self.assertRaises(ValueError):n.validate_manifest(n.seal(changed))

    def test_spec_cli_freezes_explicit_positive_unit_count(self):
        source_manifest=self.root/"source-files.json"
        n.write(source_manifest,{"files":self.spec["source_file_sha256"]})
        output=self.root/"future-spec.json"
        argv=["spec","--source-root",str(self.a),"--suite-commit","a"*40,
              "--source-manifest",str(source_manifest),"--source-archive-sha256","b"*64,
              "--expected-unit-tests","533","--output",str(output)]
        with redirect_stdout(io.StringIO()):self.assertEqual(n.main(argv),0)
        self.assertEqual(n.verify(n.read(output),n.SCHEMA+".spec")["expected_unit_tests"],533)
        self.assertEqual(self.spec["expected_unit_tests"],519)
        for invalid in (0,-1,True,1.5):
            with self.subTest(invalid=invalid),self.assertRaisesRegex(ValueError,"positive integer"):
                n.default_spec(self.a,"a"*40,expected_unit_tests=invalid)
        argv[argv.index("533")]="0"
        with self.assertRaisesRegex(ValueError,"positive integer"):n.main(argv)

    def test_complete_workflow_counts_final_unit_summary_and_controlled_environment(self):
        with patch.dict(os.environ,{"OMP_NUM_THREADS":"64","MKL_NUM_THREADS":"64"}):record=self.execute()
        self.assertFalse(record["outcomes"]["all_cause_block"],record)
        self.assertEqual(record["unit_test_count"],519)
        self.assertEqual([x[0] for x in self.calls],["unit","guard"])
        self.assertNotIn("OMP_NUM_THREADS",self.calls[0][1]);self.assertEqual(self.calls[0][1]["ATEN_CPU_CAPABILITY"],"avx2")
        self.assertIn("AVX512_SPR",self.calls[0][1]["NPY_DISABLE_CPU_FEATURES"])

    def test_unit_failure_is_an_all_cause_block_and_guard_does_not_run(self):
        self.unit_failed=True;record=self.execute()
        self.assertTrue(record["outcomes"]["all_cause_block"])
        self.assertTrue(record["outcomes"]["functional_integrity_failure"])
        self.assertFalse(record["guard"]["reported"])
        self.assertEqual([x[0] for x in self.calls],["unit"])

    def test_statistical_rejection_is_counted_separately(self):
        self.guard_stat=True;record=self.execute()
        self.assertTrue(record["outcomes"]["statistical_rejection"])
        self.assertTrue(record["outcomes"]["all_cause_block"])
        self.assertFalse(record["outcomes"]["functional_integrity_failure"])
        self.assertEqual(record["guard"]["nomination_ids"],["case"])

    def test_invalid_guard_artifact_is_unknown_and_conservatively_failing(self):
        self.bad_artifact=True;record=self.execute()
        self.assertFalse(record["guard"]["reported"])
        self.assertTrue(record["outcomes"]["all_cause_block"])
        self.assertTrue(record["outcomes"]["infrastructure_incomplete"])

    def test_equal_but_drifted_sources_or_binaries_are_not_a_clean_null(self):
        for side in [self.a,self.b]:(side/n.RUNTIME_FILES[0]).write_text("both changed")
        record=self.execute()
        self.assertTrue(record["outcomes"]["all_cause_block"])
        self.assertFalse(self.calls)

    def test_minimum_allocation_and_python_stack_are_enforced(self):
        info=self.hardware();info["observed_usable_vcpus"]=4
        record=n.run_workflow(self.spec,self.bindings,self.root/"bad-hardware",phase="baseline",run_id="baseline-small",
            stage_runner=self.stage,hardware_reader=lambda:info,version_reader=lambda *args:{"python_minor":"3.13"})
        self.assertTrue(record["outcomes"]["all_cause_block"])
        self.assertFalse(self.calls)

    def test_zero_event_upper_bounds_use_clusters_not_repeated_workflows(self):
        manifest=n.make_manifest(self.spec,100,1)
        records=[self.synthetic(manifest,row) for row in manifest["schedule"]]
        result=n.analyze(manifest,records)
        self.assertTrue(result["campaign_complete"])
        self.assertAlmostEqual(result["primary_allocation_cluster"]["upper"],1-.05**.01)
        self.assertTrue(result["primary_allocation_cluster"]["below_target"])
        repeated=n.make_manifest(self.spec,12,8)
        records=[self.synthetic(repeated,row) for row in repeated["schedule"]]
        result=n.analyze(repeated,records)
        self.assertGreater(result["primary_allocation_cluster"]["upper"],.2)
        self.assertLess(result["workflow_iid_diagnostic"]["upper"],.05)
        self.assertFalse(result["known_placement_conservative"]["tool_verified_physical_placement"])
        self.assertEqual(result["known_placement_conservative"]["groups"],1)

    def test_missing_and_conflicting_results_cannot_be_dropped(self):
        rows=self.manifest["schedule"];first=self.synthetic(self.manifest,rows[0]);bad=self.synthetic(self.manifest,rows[0],True)
        result=n.analyze(self.manifest,[first,bad])
        self.assertEqual(result["missing_or_conflicting_workflows"],4)
        self.assertEqual(result["counts"]["all_cause_block"],4)
        self.assertFalse(result["campaign_complete"])

    def test_identical_fetch_duplicates_do_not_create_observations(self):
        first=self.synthetic(self.manifest,self.manifest["schedule"][0])
        result=n.analyze(self.manifest,[first,first])
        self.assertEqual(result["retained_scheduled_observations"],1)
        self.assertEqual(result["missing_or_conflicting_workflows"],3)

    def test_resealed_contradictory_outcomes_are_not_clean(self):
        value=self.synthetic(self.manifest,self.manifest["schedule"][0]);value.pop("record_sha256")
        value["outcomes"]["statistical_rejection"]=True
        result=n.analyze(self.manifest,[n.seal(value)])
        self.assertEqual(result["counts"]["all_cause_block"],4)
        self.assertTrue(result["invalid_inputs"])

    def test_empty_or_missing_success_proof_is_conservatively_invalid(self):
        for field,value in [("stages",[]),("hardware",{}),("source_before",{}),("unit_test_count",None),("controlled_environment",{})]:
            record=self.synthetic(self.manifest,self.manifest["schedule"][0]);record.pop("record_sha256");record[field]=value
            result=n.analyze(self.manifest,[n.seal(record)])
            self.assertEqual(result["counts"]["all_cause_block"],4,(field,result))
            self.assertTrue(result["invalid_inputs"])

    def test_first_validation_nonempty_fixture_cache_is_not_silently_cleared(self):
        cache=Path(self.bindings["fixture_cache"]);cache.mkdir();(cache/"old").write_text("retained")
        record=self.execute()
        self.assertTrue(record["outcomes"]["all_cause_block"])
        self.assertFalse(self.calls)
        self.assertTrue((cache/"old").exists())

    def test_strata_and_provisioning_counts_do_not_assert_physical_independence(self):
        records=[self.synthetic(self.manifest,row) for row in self.manifest["schedule"]]
        records[0].pop("record_sha256");records[0]["stratum"]="dc-a";records[0]=n.seal(records[0])
        result=n.analyze(self.manifest,records,provisioning={"attempts":3,"available":2,"unavailable":1},
            placement={"verified_physical_group_by_cluster":{"cluster-001":"group-a","cluster-002":"group-b"}})
        self.assertEqual(result["datacenter_stratum_counts"]["dc-a"]["workflows"],1)
        self.assertFalse(result["known_placement_conservative"]["operator_reported_attestation_grouping_used"])
        self.assertFalse(result["unconditional_future_failure_bound_established"])


if __name__=="__main__":unittest.main()
