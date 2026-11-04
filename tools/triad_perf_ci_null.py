#!/usr/bin/env python3
"""Preregister, execute and count complete unchanged-source CI workflows.

This operator-side observer is deliberately outside the frozen tested checkout.
It neither provisions cloud resources nor changes the receiver/guard. Anonymous
allocation labels are experimental units, not proof of distinct physical hosts.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import signal
import subprocess
import sys
import time

SCHEMA = "triad-ci-null-v1"
RUNTIME_FILES = (
    "tools/triad_perf_guard.py", "tools/triad_perf_guard_policy.py",
    "tools/triad_perf_guard_native_fft.py", "tools/triad_perf_guard_families.py",
    "tools/triad_perf_guard_extras.py", "tools/triad_perf_complete.py",
    "tools/triad_perf_hardware.py", "tools/triad-performance-coverage.json",
    "tools/triad-performance-families.json", "tools/triad-performance-native-cases.json",
    "tools/triad-performance-cases.json",
)
BINARY_FILES = ("triad_long", "triad_planner_perf", "test_triad_statistics", "test_triad_v3_source", "test_triad_v3_channel", "test_triad_v3_receiver")
EXPECTED_STACK = {"python_minor":"3.12", "numpy":"2.3.5", "scipy":"1.16.3", "torch":"2.8.0+cpu"}
BINDINGS = {"candidate", "reference", "python", "binary", "reference_binary",
            "planner_binary", "planner_reference_binary", "bounds_workspace", "fixture_cache"}
CATEGORIES = ("all_cause_block", "statistical_rejection", "functional_integrity_failure",
              "infrastructure_incomplete", "nomination", "confirmation", "futility", "budget_exhausted")


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()


def seal(value):
    result = json.loads(json.dumps(value, allow_nan=False))
    return {**result, "record_sha256": digest(result)}


def verify(value, schema=None):
    if not isinstance(value, dict):
        raise ValueError("sealed object required")
    body = dict(value); claimed = body.pop("record_sha256", None)
    if claimed != digest(body) or (schema is not None and body.get("schema") != schema):
        raise ValueError("record schema/identity mismatch")
    return body


def read(path):
    return json.loads(Path(path).read_text())


def write(path, value):
    path = Path(path); path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix+".tmp")
    tmp.write_text(json.dumps(value, indent=2, allow_nan=False)+"\n"); tmp.replace(path)


def file_sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024*1024), b""): h.update(block)
    return h.hexdigest()


def label(value):
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_-]{0,63}", value):
        raise ValueError("anonymous experimental label required")
    return value


def default_spec(source_root, suite_commit, *, expected_unit_tests=519, source_manifest=None, archive_sha256=None):
    if type(expected_unit_tests) is not int or expected_unit_tests < 1:
        raise ValueError("expected unit test count must be a positive integer")
    root = Path(source_root)
    if not re.fullmatch(r"[0-9a-f]{40,64}", suite_commit):
        raise ValueError("exact source commit required")
    files = source_manifest.get("files",source_manifest) if source_manifest is not None else None
    if not isinstance(files,dict) or not files or not set(RUNTIME_FILES).issubset(files):
        raise ValueError("complete frozen source archive file manifest required")
    if not isinstance(archive_sha256,str) or not re.fullmatch(r"[0-9a-f]{64}",archive_sha256):
        raise ValueError("expected frozen source archive SHA256 required")
    for name,value in files.items():
        path=Path(name)
        if path.is_absolute() or ".." in path.parts or not re.fullmatch(r"[0-9a-f]{64}",value):
            raise ValueError("invalid source manifest entry")
        if file_sha(root/path)!=value: raise ValueError("source manifest does not match "+name)
    if source_manifest.get("commit",suite_commit)!=suite_commit or source_manifest.get("archive_sha256",archive_sha256)!=archive_sha256:
        raise ValueError("frozen archive/source manifest identity differs")
    environment = dict(ATEN_CPU_CAPABILITY="avx2", OPENBLAS_CORETYPE="HASWELL", MKL_ENABLE_INSTRUCTIONS="AVX2", ONEDNN_MAX_CPU_ISA="AVX2",
        NPY_DISABLE_CPU_FEATURES="AVX512F,AVX512CD,AVX512_KNL,AVX512_KNM,AVX512_SKX,AVX512_CLX,AVX512_CNL,AVX512_ICL,AVX512_SPR",
        PYTHONDONTWRITEBYTECODE="1", TRIAD_SOURCE_ROOT="{candidate}", TRIAD_LONG_BINARY="{binary}")
    guard = ["{python}", "tools/triad_perf_guard.py", "--profile", "ci", "--suite-alpha", "0.01",
        "--workflow-job-count", "1", "--candidate-root", "{candidate}", "--reference-root", "{reference}",
        "--binary", "{binary}", "--reference-binary", "{reference_binary}", "--fixture-cache", "{fixture_cache}",
        "--device", "cpu", "--precision", "fp32", "--threads", "2", "--native-threads", "1",
        "--budget-seconds", "600", "--functional-defaults", "--planner-binary", "{planner_binary}",
        "--planner-reference-binary", "{planner_reference_binary}", "--planner-bounds-workspace", "{bounds_workspace}",
        "--hardware-seconds", "5", "--output", "{output}/guard"]
    return seal({"schema": SCHEMA+".spec", "suite_commit": suite_commit,
        "runtime_source_sha256": {name: file_sha(root/name) for name in RUNTIME_FILES},
        "source_file_sha256":files,"source_archive_sha256":archive_sha256,
        "source_file_manifest_identity_sha256":digest(source_manifest),
        "binary_file_sha256":{str(Path("build/dev-cli")/name):file_sha(root/"build/dev-cli"/name) for name in BINARY_FILES},
        "expected_python_stack":dict(EXPECTED_STACK),
        "expected_unit_tests": expected_unit_tests, "expected_hypothesis_count": 33,
        "minimum_visible_vcpus": 8, "environment": environment, "workflow_timeout_seconds": 900.,
        "unset_environment": ["OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS", "NUMEXPR_NUM_THREADS", "VECLIB_MAXIMUM_THREADS"],
        "unit_thread_policy": "no observer-imposed library thread cap; inherited caps explicitly unset; guard sets its own two library/one native thread controls",
        "stages": [
            {"id": "unit", "argv": ["{python}", "-m", "unittest", "discover", "-s", "tests", "-p", "test_triad_perf*.py"],
             "cwd": "{candidate}", "timeout_seconds": 240.},
            {"id": "guard", "argv": guard, "cwd": "{candidate}", "timeout_seconds": 720.}],
        "timing_boundary": "full performance-harness unittest prestage plus actual CI guard; prebuilt source/binaries; build, dependency installation, provisioning and network excluded",
        "cache_policy": "first validation repeat uses an empty fixture cache; later repeats retain that allocation's cache; OS page cache is uncontrolled",
        "aa_policy": "separate source/worker paths; exact source and native/planner binary equality within each workflow",
        "assignment_policy": "frozen guard chooses fresh OS assignment coins; observer never fixes order entropy"})


def make_manifest(spec, clusters, runs_per_cluster, *, baselines=(), alpha=.05, target=.05, registered_utc=None):
    verify(spec, SCHEMA+".spec")
    if type(clusters) is not int or type(runs_per_cluster) is not int or min(clusters, runs_per_cluster) < 1:
        raise ValueError("positive fixed cluster and repeat counts required")
    if not 0 < alpha < 1 or not 0 < target < 1:
        raise ValueError("probability levels must lie in (0,1)")
    baseline_ids = []
    for record in baselines:
        body = verify(record, SCHEMA+".observation")
        if body["phase"] != "baseline" or body["spec_sha256"] != spec["record_sha256"]:
            raise ValueError("baseline/spec mismatch")
        baseline_ids.append({"id": body["run_id"], "sha256": record["record_sha256"]})
    schedule = [{"cluster_id": f"cluster-{c+1:03d}", "repeat": r,
                 "run_id": f"cluster-{c+1:03d}-run-{r+1:03d}",
                 "fixture_cache_state": "empty-first" if r == 0 else "retained-prior-attempts"}
                for c in range(clusters) for r in range(runs_per_cluster)]
    if {r["id"] for r in baseline_ids} & {r["run_id"] for r in schedule}:
        raise ValueError("baseline identities overlap validation schedule")
    return seal({"schema": SCHEMA+".manifest", "spec": spec, "schedule": schedule,
        "registered_utc": registered_utc or datetime.now(timezone.utc).isoformat(),
        "cluster_count": clusters, "runs_per_cluster": runs_per_cluster,
        "scheduled_workflows": len(schedule), "baseline_records_excluded": baseline_ids,
        "primary_alpha": alpha, "target_block_probability": target,
        "primary_unit": "any conservative all-cause block in one fresh allocation cluster",
        "stopping_rule": "fixed schedule; no early favorable stopping, replacement after outcomes, or removal of failed/missing workflows",
        "placement_note": "allocation labels do not establish physical host independence; allocation-cluster binomial bounds are conditional on independent representative clusters"})


def validate_manifest(manifest):
    body = verify(manifest, SCHEMA+".manifest")
    verify(body["spec"], SCHEMA+".spec")
    expected = make_manifest(body["spec"], body["cluster_count"], body["runs_per_cluster"],
        alpha=body["primary_alpha"], target=body["target_block_probability"], registered_utc=body["registered_utc"])
    original = dict(body); original["baseline_records_excluded"] = []
    other = dict(verify(expected)); other["baseline_records_excluded"] = []
    if original != other:
        raise ValueError("manifest schedule/arithmetic changed")
    if len({row["id"] for row in body["baseline_records_excluded"]}) != len(body["baseline_records_excluded"]):
        raise ValueError("duplicate excluded baseline identity")
    return body


def hardware():
    visible = os.cpu_count() or 0
    affinity = len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else visible
    quota = None
    try:
        q,p = Path("/sys/fs/cgroup/cpu.max").read_text().split()
        if q != "max": quota = int(q)/int(p)
    except (OSError, ValueError): pass
    cpu_model = None; avx2 = fma = False
    try:
        text = Path("/proc/cpuinfo").read_text()
        match = re.search(r"(?m)^model name\s*:\s*(.+)", text)
        cpu_model = match.group(1) if match else None
        flags=set(re.search(r"(?m)^flags\s*:\s*(.+)", text).group(1).split())
        avx2,fma = "avx2" in flags,"fma" in flags
    except (OSError, AttributeError): pass
    return {"os": platform.system(), "kernel": platform.release(), "machine": platform.machine(),
        "python": platform.python_version(), "cpu_model": cpu_model, "visible_cpus": visible,
        "affinity_cpus": affinity, "cgroup_vcpu_limit": quota,
        "observed_usable_vcpus": min(visible, affinity, quota if quota is not None else visible),
        "avx2_flag_reported": avx2, "fma_flag_reported":fma,"physical_host_identity_collected": False}


def _source_check(spec, bindings):
    issues=[];hashes={};expected=spec["source_file_sha256"]
    for side in ("candidate","reference"):
        root=Path(bindings[side]);actual={}
        for name,value in expected.items():
            try: actual[name]=file_sha(root/name)
            except OSError: actual[name]=None
            if actual[name]!=value: issues.append(f"{side} source mismatch: {name}")
        hashes[side]={"file_count":len(actual),"expected_closure_sha256":digest(expected),"actual_closure_sha256":digest(actual)}
        binaries={}
        for name,value in spec["binary_file_sha256"].items():
            try: binaries[name]=file_sha(root/name)
            except OSError: binaries[name]=None
            if binaries[name]!=value: issues.append(f"{side} frozen binary mismatch: {name}")
        hashes[side]["binary_sha256"]=binaries
    for left,right in (("binary","reference_binary"),("planner_binary","planner_reference_binary")):
        try: values=[file_sha(bindings[key]) for key in (left,right)]
        except OSError: values=[None,None]
        hashes[left+"_pair"]=values
        name="triad_long" if left=="binary" else "triad_planner_perf"
        frozen=spec["binary_file_sha256"]["build/dev-cli/"+name]
        if values!=[frozen,frozen]: issues.append(left+" A/A or frozen binary hashes differ")
        if Path(bindings[left]).resolve()==Path(bindings[right]).resolve(): issues.append(left+" A/A paths are not separate")
    if Path(bindings["candidate"]).resolve()==Path(bindings["reference"]).resolve(): issues.append("A/A source paths are not separate")
    return {"issues":issues,"hashes":hashes}


def python_stack(python, environment):
    code="import json,platform,numpy,scipy,torch; print(json.dumps({'python_minor':'.'.join(platform.python_version().split('.')[:2]),'numpy':numpy.__version__,'scipy':scipy.__version__,'torch':torch.__version__}))"
    result=subprocess.run([python,"-c",code],capture_output=True,text=True,timeout=45,env=environment)
    if result.returncode: raise ValueError("tested Python stack probe failed: "+result.stderr[-500:])
    return json.loads(result.stdout.splitlines()[-1])


def _stop(process):
    try: os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError: pass
    try: process.wait(timeout=2.)
    except subprocess.TimeoutExpired:
        try: os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError: pass
        process.wait(timeout=5.)


def _execute_stage(stage, bindings, output, environment, deadline):
    start = time.perf_counter(); values = {**bindings, "output": str(output)}
    argv = [part.format_map(values) for part in stage["argv"]]
    stdout,stderr = output/(stage["id"]+".stdout"),output/(stage["id"]+".stderr")
    row = {"id": stage["id"], "argv": argv, "cwd":stage["cwd"].format_map(values), "stdout": stdout.name, "stderr": stderr.name,
           "status": "running", "exit_code": None}
    remaining = min(stage["timeout_seconds"], deadline-time.monotonic())
    if remaining <= 0: return {**row,"status":"not_started_budget","wall_seconds":0.}
    try:
        with stdout.open("wb") as out, stderr.open("wb") as err:
            process = subprocess.Popen(argv, cwd=stage["cwd"].format_map(values), env=environment,
                stdout=out, stderr=err, start_new_session=True)
            try:
                code = process.wait(timeout=remaining)
                row.update(status="passed" if code == 0 else "failed",exit_code=code)
            except subprocess.TimeoutExpired:
                _stop(process);row.update(status="timeout",exit_code=process.returncode)
            except BaseException:
                _stop(process)
                raise
    except OSError as exc: row.update(status="launch_error",error=type(exc).__name__+": "+str(exc))
    row["wall_seconds"] = time.perf_counter()-start
    return row


def _guard_metrics(spec, output, stage):
    issues = []; metrics = {"reported": False, "statistical_rejection": False,
        "functional_integrity_failure": False, "nomination_ids": [], "confirmation_blocks": {},
        "futility_ids": [], "budget_exhausted": False, "guard_coverage_limited": False}
    if stage is None or stage["status"].startswith("not_started"):
        return metrics,issues
    try:
        guard = read(output/"guard/summary.json"); plan = read(output/"guard/plan.json")
        p = verify(plan, "triad-performance-reject-only-guard-v3.plan")
        decision = guard["guard_decision"]; d = verify(decision,"triad-performance-reject-only-guard-v3.report")
        if guard["plan_sha256"] != plan["record_sha256"] or d["plan_sha256"] != plan["record_sha256"]:
            raise ValueError("guard plan identity differs")
        for field in ("screens","confirmations","futility_records"):
            for record in d[field]: verify(record)
        metadata = p["metadata"]
        if (p.get("policy",{}).get("suite_alpha")!=.01 or p.get("policy",{}).get("workflow_job_count")!=1 or p.get("threshold")!=1.70 or
            guard.get("profile") != "ci" or guard.get("device") != "cpu" or guard.get("budget_seconds") != 600. or
            guard.get("hypothesis_count") != spec["expected_hypothesis_count"] or
            metadata.get("threads") != 2 or metadata.get("native_threads") != 1 or metadata.get("precision") != "fp32" or
            metadata.get("functional_defaults_requested") is not True or metadata.get("randomization_inference_eligible") is not True or
            metadata.get("case_selection") != "all declared applicable workload families"):
            raise ValueError("guard configuration differs from preregistered complete CI profile")
        if guard.get("source_integrity",{}).get("status") != "passed":
            metrics["functional_integrity_failure"] = True
        expected_exit=2 if d["confirmed_regressions"] or d["functional_failures"] else 0
        if guard.get("exit_code") != stage.get("exit_code") or d.get("exit_code") != stage.get("exit_code") or stage.get("exit_code") != expected_exit:
            raise ValueError("guard reported decision/exit differs from process exit")
        identities = guard["source_identity"]["sha256"]
        a = {key.split(":",1)[1]:v for key,v in identities.items() if key.startswith("candidate:")}
        b = {key.split(":",1)[1]:v for key,v in identities.items() if key.startswith("reference:")}
        if not a or a != b: raise ValueError("guard recorded unequal A/A source bytes")
        for left,right in (("binary:candidate","binary:reference"),("binary:candidate_planner","binary:reference_planner")):
            if not identities.get(left) or identities[left] != identities.get(right):
                raise ValueError("guard recorded unequal A/A binary bytes")
        metrics["artifact_sha256"]={"summary":file_sha(output/"guard/summary.json"),"plan":file_sha(output/"guard/plan.json")}
        metrics.update(reported=True, statistical_rejection=bool(d["confirmed_regressions"]),
            functional_integrity_failure=metrics["functional_integrity_failure"] or bool(d["functional_failures"]),
            nomination_ids=[r["hypothesis_id"] for r in d["screens"] if r["nominate_confirmation"]],
            futility_ids=d["futile_confirmations"],budget_exhausted=bool(guard.get("budget_exhausted")),
            guard_coverage_limited=bool(d["missing_screens"] or d["unconfirmed_nominations"] or d["confirmation_effort_limited"]),
            missing_screens=d["missing_screens"],unconfirmed_nominations=d["unconfirmed_nominations"],
            guard_wall_seconds=guard.get("wall_seconds"), guard_status=guard.get("status"))
        for record in d["confirmations"]+d["futility_records"]:
            key=record["hypothesis_id"]
            metrics["confirmation_blocks"][key]=max(metrics["confirmation_blocks"].get(key,0),record["sample_count"])
    except (OSError,ValueError,KeyError,TypeError) as exc:
        metrics["reported"]=False
        issues.append("guard artifact invalid/missing: "+str(exc))
    return metrics,issues


def collect(spec, output, record):
    """Count the observed whole-workflow decision, preserving conservative gaps."""
    issues = list(record.get("setup_issues",[])); rows = record["stages"]
    unit = next((r for r in rows if r["id"]=="unit"),None)
    unit_failure = False; unit_count = None
    if unit and not unit["status"].startswith("not_started"):
        text = "\n".join((output/unit[key]).read_text(errors="replace") if (output/unit[key]).exists() else "" for key in ("stdout","stderr"))
        summaries=list(re.finditer(r"(?m)^Ran\s+(\d+)\s+tests?\s+in\s+[^\n]+\n\s*\n?(OK(?:[^\n]*)|FAILED(?:[^\n]*))",text))
        final=summaries[-1] if summaries else None
        unit_count=int(final.group(1)) if final else None
        unit_failure=bool(final and final.group(2).startswith("FAILED")) or unit["status"]=="failed"
        if unit["status"]=="passed" and (unit_count!=spec["expected_unit_tests"] or final is None or not final.group(2).startswith("OK")):
            issues.append("successful unit prestage lacks the complete frozen test count/OK summary")
    guard_stage = next((r for r in rows if r["id"]=="guard"),None)
    metrics,guard_issues = _guard_metrics(spec,output,guard_stage);issues.extend(guard_issues)
    infra = bool(issues) or any(r["status"] in ("timeout","launch_error","not_started_budget") for r in rows)
    if unit is None or (unit["status"].startswith("not_started") and not issues): infra=True
    if guard_stage is None or (guard_stage["status"].startswith("not_started") and unit and unit["status"]=="passed"): infra=True
    stopped_by_failure = any(r["status"]=="failed" for r in rows)
    incomplete = len(rows)!=len(spec["stages"]) or any(r["status"]=="running" for r in rows)
    infra |= incomplete
    actual_exit_block = stopped_by_failure or any(r["status"] in ("timeout","launch_error") for r in rows)
    all_cause = actual_exit_block or infra or unit_failure or metrics["functional_integrity_failure"] or metrics["statistical_rejection"]
    return {**record, "unit_test_count":unit_count,"guard":metrics,"integrity_issues":issues,
        "outcomes":{"all_cause_block":bool(all_cause),"statistical_rejection":metrics["statistical_rejection"],
            "functional_integrity_failure":unit_failure or metrics["functional_integrity_failure"] or bool(issues),
            "infrastructure_incomplete":bool(infra),"observed_exit_block":bool(actual_exit_block),
            "nomination":bool(metrics["nomination_ids"]),"confirmation":bool(metrics["confirmation_blocks"]),
            "futility":bool(metrics["futility_ids"]),"budget_exhausted":metrics["budget_exhausted"]},
        "status":"complete" if not incomplete else "incomplete"}


def run_workflow(spec, bindings, output, *, phase, run_id, cluster_id=None, repeat=None, manifest_sha256=None,
                 stage_runner=_execute_stage, hardware_reader=hardware, version_reader=python_stack, stratum="unknown"):

    s=verify(spec,SCHEMA+".spec");label(run_id)
    if phase not in ("baseline","validation"): raise ValueError("invalid workflow phase")
    if set(bindings)!=BINDINGS or any(not isinstance(v,str) or not v for v in bindings.values()):
        raise ValueError("exact local-path bindings required")
    output=Path(output).resolve();output.mkdir(parents=True,exist_ok=False)
    started=time.perf_counter(); info=hardware_reader(); check=_source_check(s,bindings)
    issues=list(check["issues"])
    if info["observed_usable_vcpus"] < s["minimum_visible_vcpus"] or not info["avx2_flag_reported"] or not info.get("fma_flag_reported",False):
        issues.append("allocation lacks the preregistered observable >=8-vCPU/AVX2/FMA eligibility")
    cache=Path(bindings["fixture_cache"])
    cache_before={"exists":cache.exists(),"file_count":sum(p.is_file() or p.is_symlink() for p in cache.rglob("*")) if cache.is_dir() else 0}
    if phase=="validation" and repeat==0 and (cache_before["file_count"] or (cache.exists() and not cache.is_dir())):
        issues.append("first validation fixture cache is not empty")
    environment=os.environ.copy()
    for name in s.get("unset_environment",[]): environment.pop(name,None)
    environment.update({k:v.format_map(bindings) for k,v in s["environment"].items()})
    try: stack=version_reader(bindings["python"],environment)
    except (OSError,ValueError,subprocess.SubprocessError) as exc: stack={"error":str(exc)}
    if stack!=s["expected_python_stack"]: issues.append("tested Python/numerical stack differs from preregistered versions")
    record={"schema":SCHEMA+".observation","phase":phase,"run_id":run_id,"cluster_id":cluster_id,
        "repeat":repeat,"stratum":label(stratum),"manifest_sha256":manifest_sha256,"spec_sha256":spec["record_sha256"],
        "started_utc":datetime.now(timezone.utc).isoformat(),"hardware":info,"python_stack":stack,"source_before":check,
        "path_bindings":dict(bindings),"output_root":str(output),"fixture_cache_before":cache_before,
        "controlled_environment":{k:environment[k] for k in s["environment"]},"setup_issues":issues,
        "stages":[],"observer_boundary":"outside frozen tested source; baseline excluded from validation N"}
    write(output/"running.json",seal(record))
    deadline=time.monotonic()+s["workflow_timeout_seconds"]
    workflow_start=time.perf_counter()
    for stage in s["stages"]:
        if issues or any(row["status"]!="passed" for row in record["stages"]):
            row={"id":stage["id"],"status":"not_started_after_failure","exit_code":None,"wall_seconds":0.}
        else: row=stage_runner(stage,bindings,output,environment,deadline)
        record["stages"].append(row);write(output/"running.json",seal(record))
    record["workflow_stage_wall_seconds"]=time.perf_counter()-workflow_start
    after=_source_check(s,bindings);record["source_after"]=after
    record["setup_issues"].extend(after["issues"])
    record["wall_seconds"]=time.perf_counter()-started
    record["ended_utc"]=datetime.now(timezone.utc).isoformat()
    record=collect(s,output,record)
    result=seal(record);write(output/"observation.json",result)
    return result


def upper_bound(events,n,alpha=.05):
    if n==0: return None
    if events==n: return 1.
    from scipy.stats import beta
    return float(beta.ppf(1-alpha,events+1,n-events))


def _clean_proof_issues(record,spec):
    """Minimum retained evidence for counting a whole workflow as clean."""
    issues=[];stages=record.get("stages",[])
    if not isinstance(stages,list) or [r.get("id") for r in stages]!=[s["id"] for s in spec["stages"]]:
        return ["complete unit/guard stage inventory is absent"]
    bindings=record.get("path_bindings",{})
    if set(bindings)!=BINDINGS or not record.get("output_root"):
        return ["retained local-path bindings/output identity are absent"]
    values={**bindings,"output":record["output_root"]}
    for expected,actual in zip(spec["stages"],stages):
        if actual.get("status")!="passed" or actual.get("exit_code")!=0:
            issues.append("clean outcome contradicts stage completion")
        if actual.get("argv")!=[arg.format_map(values) for arg in expected["argv"]] or actual.get("cwd")!=expected["cwd"].format_map(values):
            issues.append("executed stage differs from frozen command")
    if record.get("unit_test_count")!=spec["expected_unit_tests"]:
        issues.append("complete frozen unittest count is absent")
    info=record.get("hardware",{});vcpus=info.get("observed_usable_vcpus")
    if type(vcpus) not in (int,float) or not math.isfinite(vcpus) or vcpus<spec["minimum_visible_vcpus"] or not info.get("avx2_flag_reported") or not info.get("fma_flag_reported"):
        issues.append("minimum CPU/ISA evidence is absent")
    if record.get("python_stack")!=spec["expected_python_stack"]:
        issues.append("tested Python stack identity is absent")
    expected_environment={k:v.format_map(bindings) for k,v in spec["environment"].items()}
    if record.get("controlled_environment")!=expected_environment:
        issues.append("declared dispatch/environment controls are absent")
    closure=digest(spec["source_file_sha256"])
    for field in ("source_before","source_after"):
        source=record.get(field,{})
        if source.get("issues")!=[]: issues.append(field+" source check failed/absent")
        hashes=source.get("hashes",{})
        for side in ("candidate","reference"):
            expected={"file_count":len(spec["source_file_sha256"]),"expected_closure_sha256":closure,
                "actual_closure_sha256":closure,"binary_sha256":spec["binary_file_sha256"]}
            if hashes.get(side)!=expected: issues.append(field+" full frozen source/build proof differs")
        for key,name in (("binary_pair","triad_long"),("planner_binary_pair","triad_planner_perf")):
            if hashes.get(key)!=[spec["binary_file_sha256"]["build/dev-cli/"+name]]*2:
                issues.append(field+" A/A binary proof differs")
    if Path(bindings["candidate"]).resolve()==Path(bindings["reference"]).resolve(): issues.append("source paths are not separate")
    cache=record.get("fixture_cache_before",{})
    if record.get("repeat")==0 and cache.get("file_count")!=0:
        issues.append("first validation empty-cache evidence is absent")
    guard=record.get("guard",{})
    if not guard.get("reported") or set(guard.get("artifact_sha256",{}))!={"summary","plan"}:
        issues.append("retained guard artifact identities are absent")
    return issues


def analyze(manifest, observations, *, provisioning=None, placement=None):
    m=validate_manifest(manifest); expected={r["run_id"]:r for r in m["schedule"]}
    found={};unexpected=[];baseline=[];invalid=[];duplicates=[]
    for observation in observations:
        try: r=verify(observation,SCHEMA+".observation")
        except ValueError as exc: invalid.append(str(exc));continue
        if r["phase"]=="baseline": baseline.append(r["run_id"]);continue
        if r["run_id"] not in expected or r["manifest_sha256"]!=manifest["record_sha256"] or r["spec_sha256"]!=m["spec"]["record_sha256"]:
            unexpected.append(r["run_id"]);continue
        schedule=expected[r["run_id"]]
        if (not isinstance(r.get("outcomes"),dict) or any(type(r["outcomes"].get(k)) is not bool for k in CATEGORIES)
                or r.get("status")!="complete" or r.get("started_utc","") < m["registered_utc"]):
            invalid.append(r["run_id"]+": incomplete/malformed or predates preregistration");continue
        if (r["cluster_id"],r["repeat"])!=(schedule["cluster_id"],schedule["repeat"]):
            invalid.append(r["run_id"]+": schedule identity mismatch");continue
        outcomes=r["outcomes"];guard=r.get("guard",{})
        inferred_block=any(outcomes.get(k,False) for k in ("statistical_rejection","functional_integrity_failure","infrastructure_incomplete","observed_exit_block"))
        bad_stage=any(x.get("status") in ("failed","timeout","launch_error","not_started_budget") for x in r.get("stages",[]))
        source_problem=bool(r.get("setup_issues") or r.get("integrity_issues") or r.get("source_before",{}).get("issues") or r.get("source_after",{}).get("issues"))
        if (outcomes["all_cause_block"]!=inferred_block or (bad_stage and not outcomes["all_cause_block"]) or
            (source_problem and not outcomes["all_cause_block"]) or
            outcomes["statistical_rejection"]!=bool(guard.get("statistical_rejection",False)) or
            (not outcomes["all_cause_block"] and (not guard.get("reported") or r.get("python_stack")!=m["spec"]["expected_python_stack"]))):
            invalid.append(r["run_id"]+": contradictory outcome/provenance");continue
        proof_issues=[] if outcomes["all_cause_block"] else _clean_proof_issues(r,m["spec"])
        if proof_issues:
            invalid.append(r["run_id"]+": "+"; ".join(proof_issues));continue
        old=found.get(r["run_id"])
        if old and old["record_sha256"]!=observation["record_sha256"]:
            duplicates.append(r["run_id"]);found[r["run_id"]]=None
        elif r["run_id"] not in found: found[r["run_id"]]=observation
    rows=[]
    for key,schedule in expected.items():
        value=found.get(key)
        if value is None:
            rows.append({**schedule,"missing_or_conflicting":True,
                "outcomes":{name:name in ("all_cause_block","infrastructure_incomplete") for name in CATEGORIES},"guard":{}})
        else: rows.append({**verify(value),"missing_or_conflicting":False})
    n=len(rows);alpha=m["primary_alpha"]
    counts={name:sum(bool(r["outcomes"].get(name)) for r in rows) for name in CATEGORIES}
    clusters={name:[r for r in rows if r["cluster_id"]==name] for name in sorted({r["cluster_id"] for r in rows})}
    cluster_events=sum(any(r["outcomes"]["all_cause_block"] for r in values) for values in clusters.values())
    cluster_upper=upper_bound(cluster_events,len(clusters),alpha)
    missing=sum(r["missing_or_conflicting"] for r in rows)
    unknown_guard=sum(not r.get("guard",{}).get("reported",False) for r in rows)
    nominal={name:{"events":value,"denominator":n,"rate":value/n,
                   "simultaneous_one_sided_upper":upper_bound(value,n,alpha/len(CATEGORIES))}
             for name,value in counts.items()}
    placement=placement or {}
    known=placement.get("verified_physical_group_by_cluster",{})
    # Any unresolved allocation may share a known host, so complete ignorance
    # conservatively merges all allocations instead of inventing independence.
    if set(known)!=set(clusters) or placement.get("evidence_kind")!="attested-physical-host": groups={"unresolved-placement":list(clusters)};verified=False
    else:
        groups={}
        for key,value in known.items():groups.setdefault(label(value),[]).append(key)
        verified=True
    group_events=sum(any(any(r["outcomes"]["all_cause_block"] for r in clusters[c]) for c in values) for values in groups.values())
    complete=not(missing or invalid or unexpected or duplicates)
    strata={}
    for row in rows:
        key=row.get("stratum","not_observed")
        value=strata.setdefault(key,{"workflows":0,"all_cause_blocks":0,"statistical_rejections":0})
        value["workflows"]+=1
        value["all_cause_blocks"]+=row["outcomes"]["all_cause_block"]
        value["statistical_rejections"]+=row["outcomes"]["statistical_rejection"]
    return seal({"schema":SCHEMA+".analysis","manifest_sha256":manifest["record_sha256"],
        "campaign_complete":complete,"scheduled_workflows":n,"retained_scheduled_observations":n-missing,
        "missing_or_conflicting_workflows":missing,"excluded_baseline_ids":sorted(set(baseline)),
        "invalid_inputs":invalid,"unexpected_validation_ids":unexpected,"conflicting_duplicate_ids":sorted(set(duplicates)),
        "counts":counts,"workflow_rates":nominal,
        "primary_allocation_cluster":{"events":cluster_events,"denominator":len(clusters),
            "one_sided_confidence":1-alpha,"upper":cluster_upper,
            "below_target":bool(complete and cluster_upper<m["target_block_probability"]),
            "assumption":"independent representative allocation-cluster outcomes; physical independence is not established by allocation IDs"},
        "workflow_iid_diagnostic":{"events":counts["all_cause_block"],"denominator":n,
            "upper":upper_bound(counts["all_cause_block"],n,alpha),
            "assumption":"independent representative workflows; repeated workflows on an allocation may violate this"},
        "known_placement_conservative":{"tool_verified_physical_placement":False,"operator_reported_attestation_grouping_used":verified,"groups":len(groups),"events":group_events,
            "upper":upper_bound(group_events,len(groups),alpha),
            "assumption":"independent verified host groups; unknown placement merges all allocations conservatively"},
        "statistical_rejection_identification":{"observed":counts["statistical_rejection"],"unknown_guard_outcomes":unknown_guard,
            "worst_case_events":min(n,counts["statistical_rejection"]+unknown_guard),"denominator":n},
        "provisioning_aggregate":provisioning or {"status":"not_supplied"},
        "datacenter_stratum_counts":strata,
        "stratum_interpretation":"anonymous data-center strata are descriptive; they do not establish physical host independence",
        "baseline_in_primary_denominator":False,"favorable_early_stopping_allowed":False,
        "unconditional_future_failure_bound_established":False,
        "per_workflow":[{"run_id":r["run_id"],"cluster_id":r["cluster_id"],"repeat":r["repeat"],
            "missing_or_conflicting":r["missing_or_conflicting"],"outcomes":r["outcomes"],"guard":r.get("guard",{}),
            "wall_seconds":r.get("wall_seconds")} for r in rows]})


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest="action",required=True)
    s=sub.add_parser("spec");s.add_argument("--source-root",type=Path,required=True);s.add_argument("--suite-commit",required=True);s.add_argument("--source-manifest",type=Path,required=True);s.add_argument("--source-archive-sha256",required=True);s.add_argument("--expected-unit-tests",type=int,default=519);s.add_argument("--output",type=Path,required=True)
    s=sub.add_parser("init");s.add_argument("--spec",type=Path,required=True);s.add_argument("--clusters",type=int,required=True);s.add_argument("--runs-per-cluster",type=int,required=True);s.add_argument("--baseline",type=Path,action="append",default=[]);s.add_argument("--output",type=Path,required=True)
    for action in ("baseline","run"):
        s=sub.add_parser(action);s.add_argument("--stratum",default="unknown");s.add_argument("--spec" if action=="baseline" else "--manifest",type=Path,required=True);s.add_argument("--bindings",type=Path,required=True);s.add_argument("--output",type=Path,required=True)
        if action=="baseline":s.add_argument("--baseline-id",default="baseline-001")
        else:s.add_argument("--cluster",required=True);s.add_argument("--repeat",type=int,required=True)
    s=sub.add_parser("analyze");s.add_argument("--manifest",type=Path,required=True);s.add_argument("--observations",type=Path,nargs="+",required=True);s.add_argument("--provisioning",type=Path);s.add_argument("--placement",type=Path);s.add_argument("--output",type=Path,required=True)
    a=p.parse_args(argv)
    if a.action=="spec":result=default_spec(a.source_root,a.suite_commit,expected_unit_tests=a.expected_unit_tests,source_manifest=read(a.source_manifest),archive_sha256=a.source_archive_sha256)
    elif a.action=="init":result=make_manifest(read(a.spec),a.clusters,a.runs_per_cluster,baselines=[read(x) for x in a.baseline])
    elif a.action in ("baseline","run"):
        if a.action=="baseline":spec=read(a.spec);kwargs=dict(phase="baseline",run_id=label(a.baseline_id))
        else:
            manifest=read(a.manifest);m=validate_manifest(manifest);spec=m["spec"]
            row=next((r for r in m["schedule"] if r["cluster_id"]==a.cluster and r["repeat"]==a.repeat),None)
            if row is None:raise ValueError("workflow is outside frozen schedule")
            kwargs=dict(phase="validation",run_id=row["run_id"],cluster_id=row["cluster_id"],repeat=row["repeat"],manifest_sha256=manifest["record_sha256"])
        result=run_workflow(spec,read(a.bindings),a.output,stratum=a.stratum,**kwargs)
        print(json.dumps({"run_id":result["run_id"],"outcomes":result["outcomes"],"wall_seconds":result["wall_seconds"]}));return 0
    else:
        paths=[]
        for q in a.observations:paths.extend(sorted(q.rglob("observation.json")) if q.is_dir() else [q])
        result=analyze(read(a.manifest),[read(q) for q in paths],provisioning=read(a.provisioning) if a.provisioning else None,placement=read(a.placement) if a.placement else None)
    write(a.output,result);print(json.dumps({"schema":result["schema"],"output":str(a.output)}));return 0


if __name__=="__main__":raise SystemExit(main())
