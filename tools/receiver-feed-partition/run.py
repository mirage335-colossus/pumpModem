#!/usr/bin/env python3
"""One bounded foreground receiver-work capture; never qualification."""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess


SOURCE = "8c54277ca9ec7bf35dee65126ec0d78f3a484f74"
BASELINE_RUN = 39648615451


def load_common(helper: Path):
    spec = importlib.util.spec_from_file_location(
        "gui_stop_interval_common", helper / "tools/gui-stop-interval/run.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def apply_partition(source: Path, payload: Path, common, report: Path) -> dict:
    """Apply the reviewed extra overlay after the existing interval overlay."""
    manifest_path = payload / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("schema") != 1 or manifest.get("source_sha") != SOURCE:
        raise ValueError("partition overlay schema/source mismatch")
    allowed = {"src/live.cpp", "src/pattern_receiver.cpp", "src/gui_stop_interval.hpp"}
    paths = [e["path"] for e in manifest["files"]]
    if set(paths) != allowed or len(paths) != len(allowed):
        raise ValueError("partition overlay source inventory mismatch")
    new = manifest["new_files"]
    if len(new) != 1 or new[0]["path"] != "src/receiver_feed_partition.hpp":
        raise ValueError("partition overlay header inventory mismatch")
    applied = {}
    for entry in manifest["files"]:
        dest = source / entry["path"]
        text = dest.read_text()
        if common.sha_bytes(text.encode()) != entry["pre_sha256"]:
            raise ValueError("partition preimage mismatch: " + entry["path"])
        for change in entry["replacements"]:
            old, replacement = change["old"], change["new"]
            if not old or text.count(old) != 1:
                raise ValueError("partition anchor mismatch: " + entry["path"])
            text = text.replace(old, replacement, 1)
        if common.sha_bytes(text.encode()) != entry["post_sha256"]:
            raise ValueError("partition postimage mismatch: " + entry["path"])
        dest.write_text(text)
        applied[entry["path"]] = {k: entry[k] for k in ("pre_sha256", "post_sha256")}
    entry = new[0]
    dest = source / entry["path"]
    data = (payload / entry["payload"]).read_bytes()
    if dest.exists() or common.sha_bytes(data) != entry["sha256"]:
        raise ValueError("partition new header mismatch")
    dest.write_bytes(data)
    applied[entry["path"]] = {"pre_sha256": None, "post_sha256": entry["sha256"]}
    common.dump(report / "partition-overlay.json", {
        "manifest_sha256": common.sha_file(manifest_path), "manifest": manifest,
        "applied": applied, "qualification": False})
    return applied


def retain_controller(build: Path, report: Path, common) -> Path:
    report.mkdir()
    source = build / "test_gui_controller"
    retained = report / source.name
    shutil.copy2(source, retained)
    if common.sha_file(source) != common.sha_file(retained):
        raise RuntimeError("retained controller hash mismatch")
    dependencies = subprocess.check_output(["ldd", str(source)], text=True)
    libraries = {}
    for token in dependencies.split():
        if token.startswith("/") and Path(token).is_file():
            library = Path(token).resolve()
            libraries[str(library)] = {"sha256": common.sha_file(library),
                                      "bytes": library.stat().st_size}
    for name in ("CMakeCache.txt", "compile_commands.json"):
        shutil.copy2(build / name, report / name)
    with (report / "test-inventory.json").open("w") as stream:
        subprocess.run(["ctest", "--test-dir", str(build), "--show-only=json-v1"],
                       stdout=stream, check=True)
    common.dump(report / "runtime.json", {
        "elf_sha256": common.sha_file(retained), "elf_bytes": retained.stat().st_size,
        "ldd": dependencies, "runtime_libraries": libraries,
        "readelf_notes": subprocess.check_output(["readelf", "-n", str(source)], text=True),
        "CMakeCache_sha256": common.sha_file(build / "CMakeCache.txt"),
        "compile_commands_sha256": common.sha_file(build / "compile_commands.json")})
    return retained


def check_capture(capture: Path, summary: dict) -> dict:
    rows = [json.loads(line) for line in (capture / "receiver-feed.jsonl").read_text().splitlines()]
    blocks = {r["index"]: r for r in rows if r["type"] == "block"}
    owners = [r for r in rows if r["type"] == "receiver"]
    if len(blocks) != summary["blocks"] or len(owners) != summary["owner_rows"] or not blocks:
        raise ValueError("partition row-count mismatch or empty capture")
    if sorted(blocks) != list(range(len(blocks))):
        raise ValueError("partition block identities are not contiguous")
    stage_cpu = {i: 0 for i in blocks}
    stage_wall = {i: 0 for i in blocks}
    for row in rows:
        if row["type"] == "stage":
            stage_cpu[row["block"]] += row["thread_cpu_ns"]
            stage_wall[row["block"]] += row["wall_ns"]
    events = [json.loads(line) for line in (capture / "stop-interval.jsonl").read_text().splitlines()]
    pending, envelopes = {}, []
    for row in events:
        key = row["tid"], row["serial"]
        if row["event"] == "receiver_begin":
            if key in pending:
                raise ValueError("overlapping receiver marker pair")
            pending[key] = row
        elif row["event"] == "receiver_end":
            begin = pending.pop(key)
            envelopes.append((begin, row))
    for index, row in blocks.items():
        if stage_cpu[index] + row["residual_thread_cpu_ns"] != row["total_thread_cpu_ns"]:
            raise ValueError("partition CPU accounting mismatch")
        if stage_wall[index] + row["residual_wall_ns"] != row["total_wall_ns"]:
            raise ValueError("partition wall accounting mismatch")
        matches = [(a, b) for a, b in envelopes
                   if a["tid"] == row["tid"] and a["serial"] == row["serial"]
                   and a["wall_ns"] <= row["begin_wall_ns"] <= row["end_wall_ns"] <= b["wall_ns"]]
        if len(matches) != 1:
            raise ValueError("partition block not enclosed by one receiver marker pair")
        if row["input_samples"] != matches[0][0]["count"]:
            raise ValueError("partition input count differs from receiver marker")
    request = next(r for r in events if r["event"] == "fixture_request")
    flag = next(r for r in events if r["event"] == "request_flag" and r["serial"] == request["serial"])
    result = next(r for r in events if r["event"] == "fixture_result")
    enclosed = [i for i, r in blocks.items() if r["serial"] == request["serial"]
                and flag["wall_ns"] <= r["begin_wall_ns"] <= r["end_wall_ns"] <= result["wall_ns"]]
    if not enclosed:
        raise ValueError("no complete receiver blocks inside original stop window")
    return {"blocks": len(blocks), "owner_rows": len(owners),
            "fully_enclosed_stop_blocks": enclosed, "all_block_cpu_and_wall_reconciled": True,
            "all_blocks_match_original_receiver_markers": True}


def run(args) -> int:
    source = args.source_root.resolve(strict=True)
    helper = args.helper_root.resolve(strict=True)
    output = args.output.resolve()
    if output.exists():
        raise FileExistsError("fresh output required: " + str(output))
    output.mkdir(parents=True)
    common = load_common(helper)
    facts = common.source_facts(source, helper, output)
    common.dump(output / "baseline-reuse.json", {
        "run_id": BASELINE_RUN, "source_sha": SOURCE,
        "original_strict_serial": "2/2 passed", "original_strict_parallel": "2/2 passed",
        "artifact_id": 12369864424,
        "artifact_sha256": "cab431eaf9ce5d9b3ac2fbcdf497e52f2b4fe056672f2bb42593146d6f262e99",
        "interpretation": "finite original passes; not waveform-matched performance evidence",
        "earlier_full_debug_failure_remains": True})
    env = dict(os.environ)
    env.pop("DATAPUMP_GUI_STOP_INTERVAL_DIR", None)
    env.pop("DATAPUMP_RX_CPU_PROFILE", None)
    env.update(CC="gcc-11", CXX="g++-11", DATAPUMP_SANITIZERS="ON",
               ASAN_OPTIONS="detect_leaks=0:halt_on_error=1",
               UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    common.dump(output / "compiler.json", {
        name: subprocess.check_output([name, "--version"], text=True)
        for name in ("gcc-11", "g++-11", "cmake")})
    work = output / "working-source"
    shutil.copytree(source, work, symlinks=True, ignore=shutil.ignore_patterns(".git"))
    common.apply_overlay(work, work, helper, output)
    apply_partition(work, helper / "tools/receiver-feed-partition", common, output)
    build = output / "build-tree"
    configured = common.cmake_configure(work, build, env, output / "configure")
    built = common.command(["cmake", "--build", str(build), "--target",
                            "test_gui_controller", "--parallel", "2"],
                           work, env, output / "build.log", timeout=1800)
    common.dump(output / "build-result.json", {"configure": configured, "build": built})
    if built["exit_code"] != 0 or built["timed_out"] or not built["reaped"]:
        raise RuntimeError("partition target build failed")
    retained = retain_controller(build, output / "retained-runtime", common)
    trial_env = dict(env)
    trial_env["DATAPUMP_GUI_STOP_INTERVAL_DIR"] = str(output / "partitioned/capture")
    trial = common.trial(trial_env, build / "test_gui_controller", work,
                         output / "partitioned", "partitioned", 600,
                         observer=helper / "tools/observe-native-test.py", retained=retained)
    # Preserve application failures as evidence, then fail the wave. Never use
    # teardown completion to qualify the unchanged ten-second stop assertion.
    capture = output / "partitioned/capture"
    files = {}
    for name in ("receiver-feed-summary.json", "receiver-feed.jsonl"):
        path = capture / name
        if not path.is_file() or not path.stat().st_size:
            raise RuntimeError("missing receiver partition capture: " + name)
        files[name] = {"sha256": common.sha_file(path), "bytes": path.stat().st_size}
    summary = json.loads((capture / "receiver-feed-summary.json").read_text())
    output_error = any(marker in (output / "partitioned/observer-launch.log").read_text(errors="replace")
                       for marker in ("RECEIVER_FEED_OUTPUT_ERROR", "STOP_INTERVAL_OUTPUT_ERROR"))
    complete = summary.get("complete") is True and not output_error
    # Retain the capture and original failure even if a teardown interrupted an
    # outer span. Only complete captures receive the independent join check.
    readback = check_capture(capture, summary) if complete else None
    passed = all(code == 0 for code in trial["component_exit_codes"])
    common.dump(output / "wave-result.json", {
        "schema": 1, "source_sha": facts["source_commit"], "trial": trial,
        "partition_files": files, "partition_summary": summary,
        "output_error_marker": output_error, "independent_capture_readback": readback,
        "capture_complete": complete, "application_pass": passed,
        "qualification": False, "waveform_matched_comparison": False})
    return 0 if complete and passed else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--helper-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    raise SystemExit(run(parser.parse_args()))
