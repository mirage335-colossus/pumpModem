#!/usr/bin/env python3
"""Run matched census-off/on Live diagnostics against one built test_live ELF.

This is diagnostic evidence only.  Both runs execute test_live's unchanged
--ci-functional-clock assertions through --ci-functional-clock-trace; the
per-case deadlines remain owned by the test.  The external 600 second limit
only bounds a hung child and never enlarges an internal allowance.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import resource
import signal
import shutil
import subprocess
import time

EXTERNAL_TIMEOUT_SECONDS = 600
MAX_LOG_BYTES = 64 * 1024 * 1024
MAX_TRACE_BYTES = 128 * 1024 * 1024
SAFE_ENV = (
    "PATH", "LANG", "LC_ALL", "TZ", "ASAN_OPTIONS", "UBSAN_OPTIONS",
    "RUNNER_OS", "RUNNER_ARCH", "DATAPUMP_RX_CPU_PROFILE",
    "DATAPUMP_FFT_EXECUTION_INNER_CPU", "DATAPUMP_CONTINUATION_INNER_CPU",
)


def sha_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def inventory(root: Path) -> dict[str, str]:
    return {str(path.relative_to(root)): sha_file(path)
            for path in sorted(root.rglob("*")) if path.is_file()}


def trace_summary(path: Path) -> dict[str, object]:
    line_count = 0
    trailing_newline = False
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            line_count += block.count(b"\n")
            if block:
                trailing_newline = block.endswith(b"\n")
    return {"line_count": line_count, "ends_with_newline": trailing_newline}


def terminate_group(process):
    # A reaped leader can still leave descendants in its owned process group.
    try: os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError: pass
    try: process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try: os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError: pass
        process.wait(timeout=10)
    # Close surviving descendants even when the leader exited by itself.
    try: os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError: pass


def sealed_file(path, limit):
    if not path.is_file(): return {"exists": False}
    observed=path.stat().st_size; digest=sha_file(path)
    if observed>limit:
        with path.open("r+b") as stream: stream.truncate(limit)
    return {"exists":True,"bytes_observed":observed,"sha256_observed":digest,
            "bytes_saved":path.stat().st_size,"sha256_saved":sha_file(path),"truncated":observed>limit}


def run_case(binary, root, name, census, report):
    case_dir=report/name;case_dir.mkdir(parents=True,exist_ok=False)
    trace=case_dir/"media-clock-trace.jsonl";out=case_dir/"stdout.log";err=case_dir/"stderr.log"
    argv=[str(binary),"--ci-functional-clock-trace",str(trace)]
    env=os.environ.copy();env["DATAPUMP_FIRST_TRANSITION_CENSUS"]="1" if census else "0"
    before=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.monotonic()
    record={"case":name,"observer":"on" if census else "off","argv":argv,"working_directory":str(root),
        "environment":{"census":env["DATAPUMP_FIRST_TRANSITION_CENSUS"],"safe_values":{k:env[k] for k in SAFE_ENV if k in env},
        "full_environment_sha256":sha_bytes(b"\0".join(k.encode()+b"="+v.encode(errors="surrogateescape") for k,v in sorted(env.items()))),"secret_values_recorded":False},
        "started_utc":datetime.now(timezone.utc).isoformat(),"external_timeout_seconds":EXTERNAL_TIMEOUT_SECONDS,
        "file_hard_limit_bytes":MAX_TRACE_BYTES,"termination_reason":None,"launch_error":None}
    process=None
    def setup():
        # Hard cap is an emergency resource boundary, independent of verdicts.
        resource.setrlimit(resource.RLIMIT_FSIZE,(MAX_TRACE_BYTES,MAX_TRACE_BYTES))
    try:
        with out.open("xb") as stdout,err.open("xb") as stderr:
            process=subprocess.Popen(argv,cwd=root,env=env,stdout=stdout,stderr=stderr,start_new_session=True,preexec_fn=setup)
            record["pid"]=process.pid
            while process.poll() is None:
                if time.monotonic()-start>=EXTERNAL_TIMEOUT_SECONDS:record["termination_reason"]="external_timeout"
                for path,limit in ((out,MAX_LOG_BYTES),(err,MAX_LOG_BYTES),(trace,MAX_TRACE_BYTES)):
                    try:
                        if path.stat().st_size>limit:record["termination_reason"]="output_limit:"+path.name
                    except FileNotFoundError:pass
                if record["termination_reason"]:break
                time.sleep(.1)
    except (OSError,subprocess.SubprocessError) as error:
        record["launch_error"]=str(error)
    finally:
        if process is not None:terminate_group(process)
    after=resource.getrusage(resource.RUSAGE_CHILDREN)
    actual=process.returncode if process is not None else None
    record.update(finished_utc=datetime.now(timezone.utc).isoformat(),wall_seconds=time.monotonic()-start,
        child_cpu_seconds={"user":after.ru_utime-before.ru_utime,"system":after.ru_stime-before.ru_stime},
        actual_exit_code=actual,signal=-actual if actual is not None and actual<0 else None)
    record["stdout"]={"path":str(out.relative_to(report)),**sealed_file(out,MAX_LOG_BYTES)}
    record["stderr"]={"path":str(err.relative_to(report)),**sealed_file(err,MAX_LOG_BYTES)}
    record["trace"]={"path":str(trace.relative_to(report)),**sealed_file(trace,MAX_TRACE_BYTES)}
    if record["trace"]["exists"]:record["trace"].update(trace_summary(trace))
    record["log_limit_exceeded"]=any(record[k].get("truncated",False) for k in ("stdout","stderr","trace"))
    record["process_result"]="passed" if actual==0 and not(record["termination_reason"] or record["launch_error"] or record["log_limit_exceeded"]) else "failed_or_incomplete"
    write_json(case_dir/"result.json",record)
    return record


def capture(root: Path, source_root: Path, compiled_source: Path, adapter: Path,
            binary: Path, report: Path) -> int:
    root = root.resolve(strict=True)
    source_root = source_root.resolve(strict=True)
    compiled_source = compiled_source.resolve(strict=True)
    adapter = adapter.resolve(strict=True)
    binary = binary.resolve(strict=True)
    report.mkdir(parents=True, exist_ok=False)
    manifest_path=compiled_source/"evidence"/"manifest.json"
    manifest=json.loads(manifest_path.read_text())
    if manifest.get("schema")!=1 or manifest.get("baseline_source_commit")!="5e36891bba8677dc4687488f36961d4ed2b77b03" or manifest.get("fixed_origin")!=1792561396.3857498 or manifest.get("applied") is not True:
        raise ValueError("adapter manifest/source/origin not bound")
    source_before = inventory(source_root)
    cache = binary.parent / "CMakeCache.txt"
    if not cache.is_file():
        raise ValueError(f"CMake cache missing beside binary: {cache}")
    cache_text=cache.read_text()
    homes=[line.split("=",1)[1] for line in cache_text.splitlines() if line.startswith("CMAKE_HOME_DIRECTORY:")]
    if len(homes)!=1 or Path(homes[0]).resolve()!= (compiled_source/"source").resolve():
        raise ValueError("configured source tree differs from diagnostic source")
    for key,wanted in (("CMAKE_BUILD_TYPE","Debug"),("DATAPUMP_SANITIZERS","ON"),("DATAPUMP_PORTABLE","OFF")):
        values=[line.split("=",1)[1] for line in cache_text.splitlines() if line.startswith(key+":")]
        if values!=[wanted]:raise ValueError("unexpected diagnostic cache setting:"+key)
    cache_sha = sha_file(cache)
    binary_sha = sha_file(binary)
    prepared_source = compiled_source / "source"
    if not prepared_source.is_dir():
        raise ValueError("adapter did not create prepared-source/source")
    for relative, row in manifest["files"].items():
        if sha_file(prepared_source/relative)!=row["after_sha256"]:
            raise ValueError("adapter postimage mismatch:"+relative)
    compiled_source_before = inventory(prepared_source)
    retained_elf=report/"test_live"
    shutil.copyfile(binary,retained_elf)
    if sha_file(retained_elf)!=binary_sha:raise ValueError("retained ELF differs")
    runs = []
    for name, census in (("baseline", False), ("probe", True)):
        runs.append(run_case(binary, root, name, census, report))
    source_after = inventory(source_root)
    compiled_source_after = inventory(prepared_source)
    record = {
        "schema": 1,
        "workflow_run":{"id":os.environ.get("GITHUB_RUN_ID"),"attempt":os.environ.get("GITHUB_RUN_ATTEMPT"),"workflow_source":os.environ.get("GITHUB_SHA"),"runner_os":os.environ.get("RUNNER_OS"),"runner_arch":os.environ.get("RUNNER_ARCH")},
        "baseline_application_source":"5e36891bba8677dc4687488f36961d4ed2b77b03",
        "scope": "matched census-off/on first-transition diagnostic using current 4e904874 source",
        "classification": "diagnostic variant; never qualification",
        "input_origin": {"expected": "1792561396.3857498", "adapter_manifest_verified": True,
                         "manifest_sha256":sha_file(manifest_path)},
        "fixture_boundary": "production Live test_live simulation path via --ci-functional-clock-trace; same built ELF and original per-case assertions/deadlines in both cases",
        "source": {"archive_root": str(source_root), "archive_files_sha256": source_before,
                   "unchanged_after": source_before == source_after,
                   "archive_after_sha256": source_after,
                   "prepared_source_files_sha256": compiled_source_before,
                   "prepared_source_unchanged_after": compiled_source_before == compiled_source_after,
                   "prepared_source_after_sha256": compiled_source_after,
                   "adapter": str(adapter), "adapter_sha256": sha_file(adapter)},
        "binary": {"path": str(binary), "retained_path":str(retained_elf), "sha256_before": binary_sha,
                   "sha256_after": sha_file(binary), "unchanged": binary_sha == sha_file(binary)},
        "configuration": {"cache_path": str(cache), "cache_sha256_before": cache_sha,
                           "cache_sha256_after": sha_file(cache)},
        "cases": runs,
        "matched_cases": len(runs) == 2 and [run["observer"] for run in runs] == ["off", "on"],
        "diagnostic_status": "complete" if all(run["process_result"] == "passed" for run in runs)
           and source_before==source_after and compiled_source_before==compiled_source_after
           and binary_sha==sha_file(binary) and cache_sha==sha_file(cache) else "failed_or_incomplete",
        "qualification_claim": False,
    }
    write_json(report / "capture-report.json", record)
    return 0 if record["diagnostic_status"] == "complete" else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path, help="diagnostic checkout root")
    parser.add_argument("--source-root", required=True, type=Path, help="exact archived current source")
    parser.add_argument("--compiled-source", required=True, type=Path,
                        help="adapter output used as the CMake source tree")
    parser.add_argument("--adapter", required=True, type=Path, help="Sol prepare.py adapter")
    parser.add_argument("--binary", required=True, type=Path, help="built test_live executable")
    parser.add_argument("--report", required=True, type=Path, help="new evidence directory")
    args = parser.parse_args()
    return capture(args.root, args.source_root, args.compiled_source, args.adapter,
                   args.binary, args.report)


if __name__ == "__main__":
    raise SystemExit(main())
