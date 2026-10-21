#!/usr/bin/env python3
"""Run one bounded original-versus-instrumented GUI stop diagnostic wave.

This is evidence collection only. It preserves original GUI assertions and
never treats instrumented behavior as qualification or as a matched waveform.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import sys
import time
import xml.etree.ElementTree as ET


SOURCE_COMMIT = "8c54277ca9ec7bf35dee65126ec0d78f3a484f74"
TARGETS = ("test_gui_controller", "test_gui_application", "test_gui_link_planner")
CONFIG = {
    "CMAKE_BUILD_TYPE": "Debug",
    "DATAPUMP_SANITIZERS": "ON",
    "DATAPUMP_PORTABLE": "OFF",
    "BUILD_TESTING": "ON",
    "CMAKE_EXPORT_COMPILE_COMMANDS": "ON",
    "DATAPUMP_CI_HOSTED_CLOCK_FUNCTIONAL": "OFF",
}


def sha_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha_file(path: Path) -> str:
    return sha_bytes(path.read_bytes())


def dump(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n")


def git(root: Path, *args: str) -> str:
    return subprocess.check_output(
        ["git", "-c", "safe.directory=" + str(root), *args], cwd=root, text=True
    ).strip()


def command(argv: list[str], cwd: Path, env: dict[str, str], log: Path,
            timeout: int | None = None) -> dict:
    log.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    with log.open("wb") as stream:
        try:
            proc = subprocess.Popen(argv, cwd=cwd, env=env, stdout=stream,
                                    stderr=subprocess.STDOUT, start_new_session=True)
            try:
                code = proc.wait(timeout=timeout)
                timed_out = False
            except subprocess.TimeoutExpired:
                timed_out = True
                try:
                    os.killpg(proc.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    code = proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    try:
                        os.killpg(proc.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    code = proc.wait()
        except OSError as error:
            raise RuntimeError(f"could not start {argv[0]}: {error}") from error
    return {"argv": argv, "cwd": str(cwd), "exit_code": code,
            "timed_out": timed_out, "reaped": proc.poll() is not None,
            "wall_seconds": time.monotonic() - started, "log": str(log),
            "log_sha256": sha_file(log)}


def source_facts(source: Path, helper: Path, output: Path) -> dict:
    head = git(source, "rev-parse", "HEAD")
    if head != SOURCE_COMMIT:
        raise ValueError(f"source HEAD {head} does not match pinned {SOURCE_COMMIT}")
    dirty = git(source, "status", "--porcelain=v1", "--untracked-files=all")
    if dirty:
        raise ValueError("source checkout must be clean: " + dirty[:4000])
    tracked = git(source, "ls-files", "-z").split("\0")
    files = {name: sha_file(source / name) for name in tracked if name}
    allowed_env = ("PATH", "CC", "CXX", "CFLAGS", "CXXFLAGS", "LDFLAGS",
                   "ASAN_OPTIONS", "UBSAN_OPTIONS", "DATAPUMP_SANITIZERS",
                   "DATAPUMP_JOBS", "CMAKE_BUILD_PARALLEL_LEVEL")
    try:
        affinity = sorted(os.sched_getaffinity(0))
    except (AttributeError, OSError):
        affinity = None
    try:
        nice = os.getpriority(os.PRIO_PROCESS, 0)
    except OSError:
        nice = None
    meminfo = Path("/proc/meminfo")
    memory = meminfo.read_text() if meminfo.is_file() else None
    facts = {
        "schema": 1, "source_commit": head, "source_tree": git(source, "rev-parse", "HEAD^{tree}"),
        "source_files_sha256": files, "source_file_count": len(files),
        "helper_commit": git(helper, "rev-parse", "HEAD"),
        "helper_dirty": git(helper, "status", "--porcelain=v1", "--untracked-files=all"),
        "runtime": {"python": sys.version, "platform": platform.platform(),
                    "uname": list(platform.uname()), "pid": os.getpid()},
        "environment": {key: os.environ[key] for key in allowed_env if key in os.environ},
        "affinity_cpu_ids": affinity, "nice": nice, "proc_meminfo": memory,
        "configuration": CONFIG,
        "interpretation": "diagnostic only; original and traced random waveforms are not matched",
    }
    dump(output / "source-and-runtime.json", facts)
    return facts


def cmake_configure(source: Path, build: Path, env: dict[str, str], output: Path) -> dict:
    build.mkdir(parents=True, exist_ok=False)
    argv = ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
            "-DCMAKE_C_COMPILER=gcc-11", "-DCMAKE_CXX_COMPILER=g++-11"]
    argv.extend(f"-D{name}={value}" for name, value in CONFIG.items())
    argv.extend(("-DDATAPUMP_BUILD_TUI=OFF", "-DDATAPUMP_BUILD_FB=OFF",
                 "-DDATAPUMP_BUILD_WEB_WORKER=OFF", "-DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE",
                 "-DDART_TESTING_TIMEOUT=3600"))
    result = command(argv, source, env, output / "configure.log", timeout=900)
    if result["exit_code"] != 0 or result["timed_out"]:
        raise RuntimeError("CMake configure failed; see " + str(output / "configure.log"))
    return result


def build_targets(source: Path, build: Path, env: dict[str, str], output: Path) -> dict:
    argv = ["cmake", "--build", str(build), "--target", *TARGETS, "--parallel", "2"]
    result = command(argv, source, env, output / "build.log", timeout=1800)
    if result["exit_code"] != 0 or result["timed_out"]:
        raise RuntimeError("GUI target build failed; see " + str(output / "build.log"))
    return result


def retain_build(source: Path, build: Path, output: Path) -> dict:
    elf_dir = output / "retained-elf"
    elf_dir.mkdir(parents=True, exist_ok=False)
    rows = {}
    for target in TARGETS:
        binary = build / target
        if not binary.is_file() or not os.access(binary, os.X_OK):
            raise FileNotFoundError(f"expected built executable missing: {binary}")
        retained = elf_dir / target
        shutil.copy2(binary, retained)
        if sha_file(binary) != sha_file(retained):
            raise RuntimeError("retained ELF hash mismatch: " + target)
        version = subprocess.check_output(["readelf", "-n", str(binary)], text=True,
                                          stderr=subprocess.STDOUT)
        deps = subprocess.check_output(["ldd", str(binary)], text=True,
                                       stderr=subprocess.STDOUT)
        dependencies={}
        for token in deps.split():
            if token.startswith("/") and Path(token).is_file():
                library=Path(token).resolve()
                dependencies[str(library)]={"sha256":sha_file(library),"bytes":library.stat().st_size}
        rows[target] = {"path": str(retained), "sha256": sha_file(retained),
                        "bytes": retained.stat().st_size,
                        "readelf_notes": version, "ldd": deps,"runtime_libraries":dependencies}
    cache = build / "CMakeCache.txt"
    compile_commands = build / "compile_commands.json"
    if not cache.is_file() or not compile_commands.is_file():
        raise FileNotFoundError("build cache/compile_commands.json missing")
    shutil.copy2(cache, output / "CMakeCache.txt")
    shutil.copy2(compile_commands, output / "compile_commands.json")
    subprocess.run(["ctest", "--test-dir", str(build), "--show-only=json-v1"],
                   stdout=(output / "test-inventory.json").open("w"), check=True)
    evidence = {"build_dir": str(build), "CMakeCache_sha256": sha_file(cache),
                "compile_commands_sha256": sha_file(compile_commands), "targets": rows}
    dump(output / "runtime-manifests.json", evidence)
    return evidence


def overlay_manifest(helper: Path) -> tuple[Path, dict]:
    overlay = helper / "tools" / "gui-stop-interval"
    manifest_path = overlay / "manifest.json"
    if not manifest_path.is_file():
        raise FileNotFoundError("required reviewed overlay manifest missing: " + str(manifest_path))
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("schema") != 1 or manifest.get("source_sha") != SOURCE_COMMIT:
        raise ValueError("overlay manifest schema/source pin mismatch")
    if not isinstance(manifest.get("files"), list) or not isinstance(manifest.get("new_files"), list):
        raise ValueError("overlay manifest must contain files and new_files arrays")
    required = {"src/live.cpp", "tests/test_gui_controller.cpp"}
    paths = {row.get("path") for row in manifest["files"] if isinstance(row, dict)}
    new_paths = {row.get("path") for row in manifest["new_files"] if isinstance(row, dict)}
    if paths != required or new_paths != {"src/gui_stop_interval.hpp"}:
        raise ValueError("overlay must list live.cpp, test_gui_controller.cpp and new gui_stop_interval.hpp")
    return overlay, manifest


def apply_overlay(original: Path, traced: Path, helper: Path, output: Path) -> dict:
    overlay, manifest = overlay_manifest(helper)
    if original != traced:
        shutil.copytree(original, traced, symlinks=True, ignore=shutil.ignore_patterns(".git"))
    changes = {}
    entries = list(manifest["files"])
    for entry in manifest["new_files"]:
        payload = entry.get("payload")
        entry["overlay_path"] = payload
        entry["pre_sha256"] = None
        entry["post_sha256"] = entry.get("sha256")
        entry["replacements"] = []
        entries.append(entry)
    for entry in entries:
        relative = entry["path"]
        dest = traced / relative
        before = dest.read_bytes() if dest.exists() else None
        before_hash = sha_bytes(before) if before is not None else None
        expected_pre = entry.get("pre_sha256")
        if before_hash != expected_pre:
            raise ValueError(f"overlay preimage mismatch for {relative}: {before_hash}")
        if before is None:
            overlay_file = overlay / entry.get("overlay_path", relative)
            if not overlay_file.is_file():
                raise FileNotFoundError("overlay header missing: " + str(overlay_file))
            after = overlay_file.read_bytes()
        else:
            text = before.decode("utf-8")
            replacements = entry.get("replacements")
            if not isinstance(replacements, list) or not replacements:
                raise ValueError("source overlay needs literal replacements: " + relative)
            for replacement in replacements:
                old, new = replacement.get("old"), replacement.get("new")
                if not isinstance(old, str) or not isinstance(new, str) or not old:
                    raise ValueError("replacement must contain nonempty old and string new: " + relative)
                if text.count(old) != 1:
                    raise ValueError(f"overlay anchor for {relative} must occur once: {old[:100]!r}")
                text = text.replace(old, new, 1)
            after = text.encode("utf-8")
        after_hash = sha_bytes(after)
        if after_hash != entry.get("post_sha256"):
            raise ValueError(f"overlay postimage mismatch for {relative}: {after_hash}")
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(after)
        changes[relative] = {"pre_sha256": before_hash, "post_sha256": after_hash,
                             "replacement_count": len(entry.get("replacements", []))}
    dump(output / "overlay-application.json", {"manifest_sha256": sha_file(overlay / "manifest.json"),
                                               "manifest": manifest, "applied": changes,
                                               "qualification": False})
    return changes


def trial(env: dict[str, str], binary: Path, workdir: Path, output: Path,
          label: str, timeout: int, peer: Path | None = None,
          observer: Path | None = None, source: str = SOURCE_COMMIT, retained: Path | None = None) -> dict:
    output.mkdir(parents=True, exist_ok=False)
    if observer is None:
        if peer is None:
            result = command([str(binary)], workdir, env, output / "stdout-stderr.log", timeout=timeout)
            return {"label": label, "controller_sha256": sha_file(binary),
                    "peer_sha256": None, **result}
        driver_command = [sys.executable, str(Path(__file__).resolve()), "driver",
                          "--timeout", str(timeout), "--peer-timeout", "900",
                          "--cwd", str(workdir), "--binary", str(binary), "--peer", str(peer),
                          "--result", str(output / "driver-result.json")]
        result = command(driver_command, workdir, env, output / "stdout-stderr.log", timeout=930)
        details_path = output / "driver-result.json"
        details = json.loads(details_path.read_text()) if details_path.is_file() else {}
        result.update({"controller_sha256": sha_file(binary), "peer_sha256": sha_file(peer),
                       "component_exit_codes": details.get("exit_codes"),
                       "component_reaped": details.get("reaped", False)})
        result["reaped"] = result["reaped"] and details.get("reaped", False)
        return {"label": label, **result}
    if not observer.is_file():
        raise FileNotFoundError("corrected process observer missing: " + str(observer))
    observer_dir = output / "observer"
    observed = command([sys.executable, str(observer), "--binary", str(binary),
                        *(["--retained-binary",str(retained)] if retained else []),
                        "--output", str(observer_dir), "--source", source,
                        "--", sys.executable, str(Path(__file__).resolve()), "driver",
                        "--timeout", str(timeout), "--peer-timeout", "900", "--cwd", str(workdir),
                        "--binary", str(binary), "--result", str(output / "driver-result.json"),
                        *( ["--peer", str(peer)] if peer else [])],
                       workdir, env, output / "observer-launch.log", timeout=max(timeout, 900) + 30)
    if observed["timed_out"]:
        raise RuntimeError(f"process observer timed out for {label}; see {observer_dir}")
    observation_path = observer_dir / "observation.json"
    if not observation_path.is_file():
        raise RuntimeError("observer did not write observation.json for " + label)
    observation = json.loads(observation_path.read_text())
    if not observation.get("diagnostics_complete") or not observation.get("driver_reaped"):
        raise RuntimeError("observer capture incomplete for " + label)
    trace_dir = Path(env.get("DATAPUMP_GUI_STOP_INTERVAL_DIR", ""))
    trace_artifacts = {}
    for name in ("stop-interval.jsonl", "stop-interval-summary.json", "receiver-input.f32"):
        path = trace_dir / name
        if not path.is_file() or path.stat().st_size == 0:
            raise RuntimeError("incomplete stop interval capture: " + str(path))
        trace_artifacts[name] = {"sha256": sha_file(path), "bytes": path.stat().st_size}
    summary = json.loads((trace_dir / "stop-interval-summary.json").read_text())
    if any(summary.get(key) != 0 for key in ("dropped", "unpublished", "clock_errors", "pcm_omitted")) or summary.get("files_ok") != 1:
        raise RuntimeError("stop interval recorder incomplete: " + str(summary))
    details = json.loads((output / "driver-result.json").read_text())
    if details.get("timed_out") or not details.get("reaped"):
        raise RuntimeError("diagnostic child timeout/closure failure")
    return {"label": label, "controller_sha256": sha_file(binary),
            "component_exit_codes": details["exit_codes"], "summary": summary,
            "peer_sha256": sha_file(peer) if peer else None,
            "observer_exit_code": observed["exit_code"], "observer": str(observer_dir),
            "driver_exit_code": observation.get("driver_exit_code"),
            "wall_seconds": observation.get("wall_seconds"),
            "observer_cpu_seconds": observation.get("observer_cpu_seconds"),
            "capture_complete": True, "stop_interval_capture": trace_artifacts}


def driver(args: argparse.Namespace) -> int:
    env = dict(os.environ)
    if args.peer is None:
        commands = [[args.binary]]
    else:
        commands = [[args.binary], [args.peer]]
    processes = []
    per_process_timeout = []
    def interrupted(signum,frame):
        raise InterruptedError("diagnostic driver interrupted by signal "+str(signum))
    signal.signal(signal.SIGTERM,interrupted)
    try:
        for index, argv in enumerate(commands):
            processes.append(subprocess.Popen(argv, cwd=args.cwd, env=env,
                                               start_new_session=True))
            per_process_timeout.append(args.timeout if index == 0 else args.peer_timeout)
        started = [time.monotonic()] * len(processes)
        remaining = set(range(len(processes)))
        timed_out_indexes = set()
        while remaining:
            now = time.monotonic()
            for index in list(remaining):
                if processes[index].poll() is not None:
                    remaining.remove(index)
                elif now - started[index] >= per_process_timeout[index]:
                    remaining.remove(index)
                    timed_out_indexes.add(index)
            if remaining:
                time.sleep(0.05)
        timed_out = bool(timed_out_indexes)
        if timed_out:
            for index in timed_out_indexes:
                proc = processes[index]
                try:
                    os.killpg(proc.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
            stop_at = time.monotonic() + 10
            while time.monotonic() < stop_at and any(processes[i].poll() is None for i in timed_out_indexes):
                time.sleep(0.05)
            for index in timed_out_indexes:
                proc = processes[index]
                if proc.poll() is not None:
                    continue
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        codes = [proc.wait() for proc in processes]
        result = {"commands": commands, "exit_codes": codes,
                  "timed_out_indexes": sorted(timed_out_indexes), "timed_out": timed_out,
                  "reaped": all(proc.poll() is not None for proc in processes)}
        Path(args.result).write_text(json.dumps(result, indent=2) + "\n")
        return 124 if timed_out else (0 if all(code == 0 for code in codes) else 1)
    except BaseException:
        for proc in processes:
            if proc.poll() is None:
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        for proc in processes:
            proc.wait()
        raise


def run(args: argparse.Namespace) -> int:
    source=args.source_root.resolve(strict=True)
    helper=args.helper_root.resolve(strict=True)
    output=args.output.resolve()
    if output.exists():raise FileExistsError("fresh output required: "+str(output))
    output.mkdir(parents=True)
    facts=source_facts(source,helper,output)
    env=dict(os.environ)
    env.pop("DATAPUMP_GUI_STOP_INTERVAL_DIR",None)
    env.update({"CC":"gcc-11","CXX":"g++-11","DATAPUMP_SANITIZERS":"ON",
                "ASAN_OPTIONS":"detect_leaks=0:halt_on_error=1",
                "UBSAN_OPTIONS":"halt_on_error=1:print_stacktrace=1"})
    dump(output/"compiler.json",{name:subprocess.check_output([name,"--version"],text=True) for name in ("gcc-11","g++-11","cmake")})
    work=output/"working-source"
    shutil.copytree(source,work,symlinks=True,ignore=shutil.ignore_patterns(".git"))
    build=output/"build-tree"
    configured=cmake_configure(work,build,env,output/"configure")
    built=build_targets(work,build,env,output/"original-build-step")
    runtime=retain_build(work,build,output/"original-build-evidence")
    dump(output/"original-build-result.json",dict(configure=configured,build=built,runtime=runtime))
    cases=[]
    wave_start=time.monotonic()
    # Exact original registered test pair, unchanged no-argument executables.
    for label,jobs in (("strict-serial",1),("strict-parallel",2)):
        result=command(["ctest","--test-dir",str(build),"--output-on-failure",
                        "--timeout","600","--output-junit",str(output/(label+".xml")),
                        "-R","^gui_(controller|application)$","-j",str(jobs)],
                       work,env,output/(label+".log"),timeout=1230)
        shutil.copytree(build/"Testing",output/(label+"-testing"))
        dump(output/(label+"-result.json"),result)
        xml=ET.parse(output/(label+".xml"))
        names=sorted(t.attrib["name"] for t in xml.findall(".//testcase"))
        if names != ["gui_application","gui_controller"]:
            raise RuntimeError("strict GUI scope inventory mismatch: "+str(names))
        cases.append(dict(label=label,**result))
    # Retained original ELFs are immutable; overlay only the private working copy.
    overlay=apply_overlay(work,work,helper,output)
    traced_build=build_targets(work,build,env,output/"traced-build-step")
    traced_runtime=retain_build(work,build,output/"traced-build-evidence")
    dump(output/"traced-build-result.json",dict(build=traced_build,runtime=traced_runtime,overlay=overlay))
    for label,peer in (("traced-alone",None),("traced-with-planner",Path(runtime["targets"]["test_gui_link_planner"]["path"]))):
        if time.monotonic()-wave_start>=1800:raise TimeoutError("post-build workload cap reached")
        trial_env=dict(env)
        trial_env["DATAPUMP_GUI_STOP_INTERVAL_DIR"]=str(output/label/"capture")
        cases.append(trial(trial_env,build/"test_gui_controller",work,output/label,label,600,
                           peer=peer,observer=helper/"tools/observe-native-test.py",
                           retained=Path(traced_runtime["targets"]["test_gui_controller"]["path"])))
        dump(output/"cases-so-far.json",cases)
    original_pass=all(r["exit_code"]==0 and not r["timed_out"] and r["reaped"] for r in cases[:2])
    captures_complete=all(r.get("capture_complete") for r in cases[2:])
    diagnostics_pass=all(all(code==0 for code in r["component_exit_codes"]) for r in cases[2:])
    result=dict(schema=1,source_commit=facts["source_commit"],cases=cases,
                original_strict_pass=original_pass,traced_capture_complete=captures_complete,
                diagnostic_applications_pass=diagnostics_pass,qualification=False,
                waveform_match_claim=False,wall_after_original_build_seconds=time.monotonic()-wave_start)
    dump(output/"wave-result.json",result)
    return 0 if original_pass and captures_complete and diagnostics_pass else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="mode", required=True)
    run_parser = sub.add_parser("run")
    run_parser.add_argument("--source-root", type=Path, required=True)
    run_parser.add_argument("--helper-root", type=Path, required=True)
    run_parser.add_argument("--output", type=Path, required=True)
    driver_parser = sub.add_parser("driver")
    driver_parser.add_argument("--timeout", type=int, required=True)
    driver_parser.add_argument("--peer-timeout", type=int, default=900)
    driver_parser.add_argument("--cwd", required=True)
    driver_parser.add_argument("--binary", required=True)
    driver_parser.add_argument("--result", type=Path, required=True)
    driver_parser.add_argument("--peer")
    args = parser.parse_args()
    if args.mode == "driver":
        return driver(args)
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
