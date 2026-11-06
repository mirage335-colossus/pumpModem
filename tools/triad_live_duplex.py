#!/usr/bin/env python3
"""Bounded, opt-in cable qualification of the isolated three-carrier study.

No ordinary Robust receiver is loaded.  Public markers establish alignment;
native keyed source patterns and simulated propagation are replayed unchanged.
The reported bit rankings are conditional diagnostics, never admissions.

Prepared environment workflow (each output directory must be new)::

    python tools/triad_live_duplex.py plan --output local-evidence/live-plan
    python tools/triad_live_duplex.py prepare --plan local-evidence/live-plan/plan.json --output local-evidence/live-input
    python tools/triad_live_duplex.py run --prepared local-evidence/live-input --output local-evidence/live-capture --live
    python tools/triad_live_duplex.py analyze --prepared local-evidence/live-input --capture local-evidence/live-capture --output local-evidence/live-analysis

Use ``plan --profile smoke`` for a route diagnostic, not suite qualification.
Preparation is reported separately; the run's whole-command budget includes
route discovery, input verification, capture, model fitting and analysis.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import resource
import shutil
import signal
import subprocess
import sys
import time

PROCESS_START = time.monotonic()

import numpy as np
import scipy
from scipy import signal as scipy_signal

import triad_live_model as model
import triad_live_waveforms as wave
from triad_background_noise import add_arguments as add_background_noise_arguments
from triad_background_noise import enabled as background_noise_enabled
from triad_background_noise import forwarded_arguments as background_noise_arguments
from triad_background_noise import validate as validate_background_noise


ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "triad-live-campaign-v1"
DEFAULT_BUDGET = 2900.
FS = 48000
GAIN = .04
CN0_GRID = [-60., -50., -40.] + list(range(-30, 21, 2))
LIMITATIONS = [
    "Cable fixture only: no radio, antenna, absolute RF power, noise figure, independent RF oscillator, or physical FHSS retuning is measured.",
    "Known public-marker alignment and a fixed public candidate bank; no blind acquisition or operational admission probability is established.",
    "Noise-added curves condition on one retained hardware observation; generated draws are not independent hardware repetitions.",
    "Low-rate source windows are actual-rate components, not shortened full bits; multi-day stability and very-low-frequency transfer may remain unidentified.",
    "A frozen linear audio model cannot explain every nonlinear, adaptive or time-varying device; mismatches remain reported rather than refitted on holdouts.",
]


class BudgetExceeded(RuntimeError):
    pass


class Deadline:
    def __init__(self, seconds=DEFAULT_BUDGET, clock=time.monotonic):
        if not math.isfinite(seconds) or seconds <= 0:
            raise ValueError("budget must be finite and positive")
        self.clock, self.start, self.seconds = clock, clock(), float(seconds)

    def remaining(self):
        remaining = self.seconds - (self.clock() - self.start)
        if remaining <= 0:
            raise BudgetExceeded("whole-command monotonic deadline exhausted")
        return remaining

    def timeout(self, maximum):
        return min(float(maximum), self.remaining())


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def json_write(path, value):
    """Exclusive creation: retained measurements are never overwritten."""
    with Path(path).open("x", encoding="utf-8") as f:
        json.dump(value, f, indent=2, sort_keys=True, allow_nan=False)
        f.write("\n")


def json_read(path):
    return json.loads(Path(path).read_text())


def fresh_directory(path):
    p = Path(path).resolve()
    p.mkdir(parents=True, exist_ok=False)
    return p


def bytes_under(path):
    return sum(p.stat().st_size for p in Path(path).rglob("*") if p.is_file())


def cpu_seconds():
    own, child = resource.getrusage(resource.RUSAGE_SELF), resource.getrusage(resource.RUSAGE_CHILDREN)
    return own.ru_utime + own.ru_stime + child.ru_utime + child.ru_stime


class Stages:
    """One immutable stage record per attempted operation, including failures."""
    def __init__(self, output, deadline):
        self.output, self.deadline, self.records = Path(output), deadline, []
        (self.output / "stages").mkdir()

    @contextmanager
    def stage(self, name):
        self.deadline.remaining()
        wall, cpu, size = time.monotonic(), cpu_seconds(), bytes_under(self.output)
        row = dict(name=name, status="complete", started_utc_unix_seconds=time.time(),
                   started_monotonic_seconds=wall,
                   hierarchy_elapsed_at_start_seconds=wall-self.deadline.start)
        try:
            yield row
            self.deadline.remaining()
        except BaseException as exc:
            row.update(status="failed", error=f"{type(exc).__name__}: {exc}")
            raise
        finally:
            row.update(wall_seconds=time.monotonic() - wall, cpu_seconds=cpu_seconds() - cpu,
                       output_bytes=max(0, bytes_under(self.output) - size))
            self.records.append(row)
            json_write(self.output / "stages" / f"{len(self.records):03d}.json", row)


def execute(argv, deadline, *, timeout=60., log=None):
    bounded_timeout = deadline.timeout(timeout)
    process = subprocess.Popen([str(a) for a in argv], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, start_new_session=True)
    try:
        stdout, stderr = process.communicate(timeout=bounded_timeout)
    except BaseException:
        # Both native helpers and Python ALSA adapters can own descendants.
        # Contain the entire process group on timeout, alarm or interruption.
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.communicate(timeout=.5)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.communicate(timeout=1.)
        raise
    if log is not None:
        json_write(log, dict(argv=list(map(str, argv)), exit_code=process.returncode,
                            stdout=stdout, stderr=stderr))
    if process.returncode:
        raise RuntimeError(f"{Path(argv[0]).name} exited {process.returncode}: {stderr[-2000:]}")
    return stdout


def provenance(binary, deadline):
    files = ["tools/triad_live_duplex.py", "tools/triad_live_model.py",
             "tools/triad_live_waveforms.py", "tools/triad_audio_capture.cpp",
             "tools/triad_live_alsa.py", "tools/triad_background_noise.py",
             "tools/triad-long.cpp", "src/triad_modem_v3.cpp",
             "src/triad_simulation.cpp", "src/channel_perturbation.hpp",
             "src/channel_spread.hpp", "src/audio.cpp"]
    return dict(git_head=execute(["git", "-C", ROOT, "rev-parse", "HEAD"], deadline).strip(),
                source_sha256={p: digest(ROOT / p) for p in files if (ROOT / p).exists()},
                native_binary=str(Path(binary).resolve()), native_binary_sha256=digest(binary),
                python=sys.version, numpy=np.__version__, scipy=scipy.__version__,
                platform=platform.platform(), machine=platform.machine())


def case_definitions(profile="full"):
    if profile not in ("full", "smoke"):
        raise ValueError("unknown profile")
    cases = [dict(id="training", kind="training", seed=93001),
             dict(id="holdout", kind="probe", seed=94001)]
    if profile == "full":
        cases[1:1] = [dict(id="training-low", kind="training_low", seed=93011,
                          probe_options=dict(training=True, level=.20)),
                      dict(id="validation-low", kind="validation_low", seed=94011,
                          probe_options=dict(training=False, level=.14, validation_variant=1))]
    def native(name, rate, added, carrier, **kwargs):
        return dict(id=name, kind="native", rate=rate, added=added,
                    carrier_hz=carrier, seed=82001, requested_seconds=8.,
                    partial=False, fhss=False, burst_wait_tiles=0, spread="none",
                    modulation="off", margin=1., gain=GAIN, **kwargs)
    if profile == "smoke":
        cases.extend([native("rate3600", 3600, 1, 1725)])
        return cases
    cases.append(dict(id="low-frequency", kind="low_frequency", seed=95001,
                      duration_seconds=420., frequencies_hz=[.0005, .005, .05, 1., 997.]))
    for rate in (.001, .01, .1):
        for suffix, center in (("half", rate / 2), ("audio", 1725.)):
            c = native(f"partial-{rate:g}-{suffix}", rate, 1, center)
            c.update(partial=True, window_seconds=24.)
            cases.append(c)
    for added in (100, 1000):
        c = native(f"partial-0.1-added{added}", .1, added, 1725.)
        c.update(partial=True, window_seconds=24.)
        cases.append(c)
    for rate, center in ((3600, 1725), (12000, 6000), (18000, 9000),
                         (24000, 12000), (24000, 15500), (24000, 16000)):
        cases.append(native(f"rate{rate}-carrier{center}", rate, 1, center))
    for rate, added in ((3.6, 1000), (1., 1000), (10., 100)):
        cases.append(native(f"rate{rate:g}-added{added}", rate, added, 1725.))
    c = native("fhss-burst3", 1200, 1, 1725.)
    c.update(fhss=True, burst_wait_tiles=3, requested_seconds=40.)
    cases.append(c)
    for name, rate, center, spread, depth, fs in (
            ("hf-each65", 3600, 1725, "hf-worst", "each-65", 4800),
            ("hf-each25", 3600, 1725, "hf-worst", "each-25", 4800),
            ("aurora-each65", 18000, 9000, "aurora-worst", "each-65", 48000)):
        c = native(name, rate, 1, center)
        c.update(spread=spread, modulation=depth, margin=3., native_fs=fs)
        cases.append(c)
    c = native("low-level-quantization", 3600, 1, 1725.)
    c["gain"] = .00004  # Explicit independent level; never normalization.
    cases.append(c)
    return cases


def native_args(case, epoch):
    args = ["--seconds", str(case["requested_seconds"]), "--channel", "none",
            "--phase-diffusion", "0", "--rf-hz", "0", "--nominal-if-hz", "0",
            "--basis", "flat", "--threads", "1", "--seed", str(case["seed"]),
            "--key-seed", "82001", "--epoch", str(epoch), "--signal-bit", "1",
            "--rate", str(case["rate"]), "--added", str(case["added"]),
            "--margin", str(case["margin"])]
    if "native_fs" in case:
        args += ["--sample-rate", str(case["native_fs"])]
    if case["spread"] != "none":
        args += ["--spread-preset", case["spread"], "--selective-perturbation", case["modulation"]]
    if case["fhss"]:
        args += ["--fhss", "--burst-wait-tiles", str(case["burst_wait_tiles"])]
    return args


def preflight_budget(durations, budget, reserve, *, per_capture_overhead=8.):
    values = list(map(float, durations))
    if any(not math.isfinite(v) or v <= 0 for v in values) or reserve < 0:
        raise ValueError("invalid planned stimulus duration or reserve")
    stimulus = sum(values)
    required = stimulus + len(values) * per_capture_overhead + reserve
    if required > budget:
        raise BudgetExceeded(f"planned stimuli {stimulus:.3f}s + capture overhead "
                             f"{len(values)*per_capture_overhead:.3f}s + reserve {reserve:.3f}s "
                             f"exceed whole-command budget {budget:.3f}s")
    return dict(stimulus_seconds=stimulus, capture_overhead_seconds=len(values) * per_capture_overhead,
                preparation_analysis_reserve_seconds=reserve, planned_total_seconds=required,
                whole_command_budget_seconds=budget)


def make_plan(binary, profile, deadline, budget, reserve, epoch=None, include_census=True):
    epoch_from_clock = epoch is None
    epoch = int(time.time()) if epoch_from_clock else int(epoch)
    cases = case_definitions(profile)
    for case in cases:
        deadline.remaining()
        if case["kind"] in ("training", "probe"):
            options = dict(period_seconds=1., levels=(.03, .1)) if profile == "smoke" else {}
            if case["kind"] == "probe":
                options = dict(period_seconds=2., levels=(.05, .18))
                if profile == "smoke":
                    options["period_seconds"] = 1.
            x, manifest = model.generate_probe(FS, case["seed"], **options)
            case.update(probe_options=options, stimulus_seconds=len(x) / FS)
        elif case["kind"] in ("training_low", "validation_low"):
            x, _ = model.generate_low_frequency_probe(FS, case["seed"], **case["probe_options"])
            case["stimulus_seconds"] = len(x)/FS
        elif case["kind"] == "low_frequency":
            x, _ = model.wrap_with_markers(np.zeros(1, np.float32), FS)
            case["stimulus_seconds"] = case["duration_seconds"] + (len(x) - 1) / FS
        else:
            cfg = json.loads(execute([binary, "--mode", "configure", *native_args(case, epoch)], deadline))
            case["native_geometry"] = cfg
            fs = cfg["configuration"]["capture"]["sample_rate_hz"]
            width = cfg["configuration"]["capture"]["nominal_triplet_bandwidth_hz"]
            duration = case["window_seconds"] if case["partial"] else cfg["performance_geometry"]["capture_samples"] / fs
            geometry = wave.audio_geometry(width, case["carrier_hz"], FS)
            if not geometry["supported"]:
                case.update(status="unsupported_audio_geometry", audio_geometry=geometry,
                            native_sample_rate=fs, signal_seconds=duration,
                            evidence_kind="calculated exclusion; no hardware capture")
                continue
            case.update(audio_geometry=geometry, native_sample_rate=fs,
                        signal_seconds=duration, stimulus_seconds=duration + 2.6)
    budget_info = preflight_budget([c["stimulus_seconds"] for c in cases if "stimulus_seconds" in c], budget, reserve)
    census = None
    if include_census:
        deadline.remaining()
        # Geometry only: no waveform or receiver campaign is run for this census.
        census = wave.geometry_census(binary, audio_fs=FS, maximum_full_seconds=budget,
            native_extra_args=("--epoch", str(epoch)), timeout_seconds=min(5., deadline.remaining() / 180))
        deadline.remaining()
    return dict(schema=SCHEMA, profile=profile, scope="isolated triad cable diagnostic hierarchy",
                complete_suite_intended=profile == "full", epoch=epoch,
                epoch_source="system UTC clock at plan freeze" if epoch_from_clock else "explicit fixture",
                public_fixture_seeds=True, operational_secret_material=False, encrypted_native_sources=True,
                calibration_family="broad plus concentrated low-frequency training" if profile=="full" else "broad-only diagnostic smoke model",
                low_frequency_model_order=4 if profile=="full" else None,
                sample_rate=FS, cases=cases, budget=budget_info, census=census,
                provenance=provenance(binary, deadline), limitations=LIMITATIONS,
                calibration_policy="fit declared broad and optional low-frequency training once; freeze combined profile before any holdout; never refit holdouts")


def write_samples(path, x):
    a = np.asarray(x, dtype="<f4")
    if a.ndim != 1 or not a.size or not np.isfinite(a).all() or np.max(abs(a)) > .9:
        raise ValueError("unsafe or invalid fixed-gain stimulus")
    with Path(path).open("xb") as f:
        a.tofile(f)


def prepare(plan_path, output, binary, deadline, stages):
    plan = json_read(plan_path)
    if plan.get("schema") != SCHEMA:
        raise ValueError("unsupported campaign plan")
    if digest(binary) != plan["provenance"]["native_binary_sha256"]:
        raise ValueError("native binary changed after geometry plan; create a new plan")
    json_write(output / "plan.json", plan)
    free = shutil.disk_usage(output).free
    estimated = plan["budget"]["stimulus_seconds"] * FS * 16 + 64 * 1024 ** 2
    if free < estimated:
        raise ValueError(f"preparation needs approximately {estimated/1024**2:.1f} MiB free")
    rows = []
    for case in plan["cases"]:
        if case.get("status") == "unsupported_audio_geometry":
            continue
        with stages.stage("prepare-" + case["id"]):
            directory = output / case["id"]
            directory.mkdir()
            if case["kind"] in ("training", "probe"):
                tx, manifest = model.generate_probe(FS, case["seed"], **case["probe_options"])
                details = {}
            elif case["kind"] in ("training_low", "validation_low"):
                tx, manifest = model.generate_low_frequency_probe(FS, case["seed"], **case["probe_options"])
                details = dict(evidence_kind="low-frequency calibration probe" if case["kind"]=="training_low" else "independent low-frequency validation probe")
            elif case["kind"] == "low_frequency":
                count = round(case["duration_seconds"] * FS)
                payload = np.zeros(count, np.float32)
                phases = np.random.default_rng(case["seed"]).uniform(-np.pi, np.pi, len(case["frequencies_hz"]))
                for first in range(0, count, 65536):
                    end = min(count, first + 65536)
                    t = np.arange(first, end) / FS
                    payload[first:end] = sum(.02 * np.sin(2 * np.pi * f * t + p)
                                             for f, p in zip(case["frequencies_hz"], phases))
                tx, manifest = model.wrap_with_markers(payload, FS, seed=case["seed"])
                details = dict(frequencies_hz=case["frequencies_hz"], phases_radians=phases.tolist(),
                               cycles_observed=[f * case["duration_seconds"] for f in case["frequencies_hz"]],
                               sub_cycle_response_unidentified=True)
            else:
                mode = "export-source-window" if case["partial"] else "export"
                args = [binary, "--mode", mode, *native_args(case, plan["epoch"]), "--output", directory / "native"]
                if case["partial"]:
                    args += ["--source-window-seconds", str(case["window_seconds"]), "--source-window-center"]
                execute(args, deadline, timeout=180., log=directory / "export-command.json")
                native = wave.load_export(directory / "native")
                m = native["manifest"]
                if not m.get("encrypted"):
                    raise ValueError("native fixture encryption unexpectedly disabled")
                origin = m.get("first_sample", 0) / native["sample_rate"]
                payload, bridge = wave.to_audio(native["signal"], native["sample_rate"], FS,
                    case["carrier_hz"], occupied_bandwidth_hz=m["occupied_bandwidth_hz"],
                    fixed_gain=case["gain"], source_start_seconds=origin, max_peak=.9)
                tx, manifest = model.wrap_with_markers(payload, FS, seed=case["seed"] + 77)
                details = dict(bridge=bridge, native_manifest_sha256=native["manifest_sha256"],
                               source_origin_seconds=origin,
                               evidence_kind="actual-rate component" if case["partial"] else
                                   ("hardware replay of simulated propagation" if case["spread"] != "none" else "complete physical modem waveform"))
            write_samples(directory / "stimulus.f32", tx)
            json_write(directory / "stimulus.json", dict(case=case, markers=manifest, details=details,
                sample_rate=FS, samples=len(tx), stimulus_seconds=len(tx)/FS,
                stimulus_sha256=digest(directory / "stimulus.f32")))
            rows.append(dict(id=case["id"], stimulus_seconds=len(tx)/FS,
                             stimulus_sha256=digest(directory / "stimulus.f32")))
    actual_budget = preflight_budget([r["stimulus_seconds"] for r in rows],
        plan["budget"]["whole_command_budget_seconds"], plan["budget"]["preparation_analysis_reserve_seconds"])
    inventory = {str(p.relative_to(output)): digest(p) for p in output.rglob("*")
                 if p.is_file() and "stages" not in p.relative_to(output).parts}
    prepared = dict(schema=SCHEMA, plan_sha256=digest(output / "plan.json"), cases=rows,
                    files_sha256=inventory, budget=actual_budget,
                    preparation_wall_seconds=time.monotonic() - deadline.start,
                    planning_wall_seconds=plan.get("planning_wall_seconds", 0.),
                    preparation_cpu_seconds=sum(r["cpu_seconds"] for r in stages.records))
    json_write(output / "prepared.json", prepared)
    return prepared


def verify_prepared(path, deadline):
    path = Path(path).resolve()
    info, plan = json_read(path / "prepared.json"), json_read(path / "plan.json")
    if (plan.get("schema") != SCHEMA or not plan.get("cases")
            or plan["cases"][0].get("kind") != "training"
            or sum(c.get("kind") == "training" for c in plan["cases"]) != 1):
        raise ValueError("exactly one first training case is required before holdouts")
    low_indices = [i for i,c in enumerate(plan["cases"]) if c.get("kind") == "training_low"]
    if len(low_indices)>1 or (low_indices and low_indices[0] != 1):
        raise ValueError("optional low-frequency training must occur once immediately after broad training")
    for name, expected in info["files_sha256"].items():
        deadline.remaining()
        p = (path / name).resolve()
        if not p.is_relative_to(path) or digest(p) != expected:
            raise ValueError(f"prepared input identity failed: {name}")
    return info, plan


def _port_name(item):
    port = item.get("active_port")
    return port.get("name", "") if isinstance(port, dict) else str(port or "")


def physical_endpoint(items, name, kind, override=None):
    matches = [i for i in items if i.get("name") == name]
    if len(matches) != 1:
        raise ValueError(f"requested physical {kind} is not uniquely present")
    item = matches[0]
    props = item.get("properties", {})
    if ("monitor" in name.lower() or item.get("monitor_of_sink") not in (None, "", 4294967295, "4294967295")
            or props.get("device.class") == "monitor"):
        raise ValueError("monitor sources are not a physical cable capture")
    card = props.get("alsa.card", props.get("api.alsa.pcm.card"))
    device = props.get("alsa.device", props.get("api.alsa.pcm.device"))
    if str(card).isdigit() is False or str(device).isdigit() is False:
        raise ValueError("physical ALSA card/device metadata is required; no virtual/default fallback")
    port = _port_name(item)
    expected = ("headphone", "headset", "lineout", "line-out") if kind == "playback" else ("mic", "line", "headset")
    if not port or not any(s in port.lower() for s in expected):
        raise ValueError(f"active {kind} port {port!r} is not a recognized cable endpoint")
    device_id = f"hw:{int(card)},{int(device)}"
    if override is not None and override != device_id:
        raise ValueError("explicit ALSA device does not match the selected physical route metadata")
    return dict(name=name, device=device_id, active_port=port, volume=item.get("volume"),
                mute=item.get("mute"), sample_specification=item.get("sample_specification"),
                properties={k: v for k, v in props.items() if k.startswith(("alsa.", "api.alsa.", "device."))})


def discover_route(deadline, *, capture_source=None, playback_sink=None,
                   capture_device=None, playback_device=None):
    capture_source = capture_source or execute(["pactl", "get-default-source"], deadline, timeout=10.).strip()
    playback_sink = playback_sink or execute(["pactl", "get-default-sink"], deadline, timeout=10.).strip()
    sources = json.loads(execute(["pactl", "--format=json", "list", "sources"], deadline, timeout=10.))
    sinks = json.loads(execute(["pactl", "--format=json", "list", "sinks"], deadline, timeout=10.))
    return dict(capture=physical_endpoint(sources, capture_source, "capture", capture_device),
                playback=physical_endpoint(sinks, playback_sink, "playback", playback_device),
                mixer_adjustments=[], server_suspensions=[], restore_required=False,
                route_scope="active physical ALSA route discovered from desktop metadata; actual native stream formats follow in each capture")


def validate_capture(metadata):
    if metadata.get("schema") != "triad-audio-capture-v1" or metadata.get("status") != "complete":
        raise ValueError("native audio capture is incomplete or unrecognized")
    if metadata.get("discontinuity", {}).get("capture", True) or metadata.get("discontinuity", {}).get("playback", True):
        raise ValueError("native capture/playback discontinuity invalidates continuous evidence")
    if not metadata.get("audio_format", {}).get("capture") or not metadata.get("audio_format", {}).get("playback"):
        raise ValueError("actual stream-format metadata is missing")


def capture_one(case, prepared, output, audio_binary, route, deadline, *, backend="native",
                background_noise_args=None):
    src, dest = prepared / case["id"], output / case["id"]
    dest.mkdir()
    stimulus = json_read(src / "stimulus.json")
    cap = stimulus["stimulus_seconds"] + 12.
    # Fail before opening a device if this capture cannot complete in budget.
    if cap > deadline.remaining():
        raise BudgetExceeded("remaining whole-command budget cannot fit the next complete capture")
    command = ([sys.executable, str(ROOT / "tools/triad_live_alsa.py")] if backend == "alsa-tools" else [audio_binary])
    argv = [*command, "--live", "--exclusive", "--input", src / "stimulus.f32",
             "--output", dest / "capture.f32", "--raw-output", dest / "provider.f32",
             "--metadata", dest / "capture.json", "--rate", FS,
             "--capture-device", route["capture"]["device"],
             "--playback-device", route["playback"]["device"],
             "--preroll-seconds", .75, "--postroll-seconds", 1.,
             "--max-seconds", cap]
    if backend == "alsa-tools" and background_noise_args is not None:
        argv.extend(background_noise_arguments(background_noise_args))
    execute(argv, deadline, timeout=cap + 2., log=dest / "command.json")
    metadata = json_read(dest / "capture.json")
    validate_capture(metadata)
    json_write(dest / "capture-identity.json", dict(stimulus_sha256=stimulus["stimulus_sha256"],
                capture_sha256=digest(dest / "capture.f32"),
                provider_sha256=digest(dest / "provider.f32"),
                stimulus_metadata_sha256=digest(src / "stimulus.json"),
                capture_metadata_sha256=digest(dest / "capture.json"),
                command_sha256=digest(dest / "command.json")))
    return metadata


def verify_capture_receipt(src, dest):
    """Validate available receipts without inventing missing historical hashes."""
    src, dest = Path(src), Path(dest)
    meta = json_read(src / "stimulus.json")
    identity = json_read(dest / "capture-identity.json")
    input_hash, capture_hash = digest(src / "stimulus.f32"), digest(dest / "capture.f32")
    if (identity.get("stimulus_sha256") != input_hash or meta.get("stimulus_sha256") != input_hash
            or identity.get("capture_sha256") != capture_hash):
        raise ValueError("capture receipt does not match frozen stimulus and raw capture")
    missing_hashes = []
    for key, path in (("provider_sha256", dest / "provider.f32"),
                      ("stimulus_metadata_sha256", src / "stimulus.json"),
                      ("capture_metadata_sha256", dest / "capture.json"),
                      ("command_sha256", dest / "command.json")):
        if key in identity:
            if digest(path) != identity[key]:
                raise ValueError(f"capture receipt hash mismatch: {key}")
        else:
            missing_hashes.append(key)
    capture = json_read(dest / "capture.json")
    validate_capture(capture)
    command = json_read(dest / "command.json")
    if command.get("exit_code") != 0:
        raise ValueError("retained capture command did not complete successfully")
    argv = command.get("argv", [])
    def argument(name):
        if argv.count(name) != 1 or argv.index(name)+1 >= len(argv):
            raise ValueError(f"capture command lacks unique {name}")
        return argv[argv.index(name)+1]
    requested = capture.get("requested", {})
    rate = meta.get("sample_rate")
    for option, field in (("--capture-device", "capture_device"), ("--playback-device", "playback_device")):
        if argument(option) != requested.get(field):
            raise ValueError("capture metadata/device command mismatch")
    if (int(argument("--rate")) != rate or requested.get("rate") != rate
            or "--live" not in argv or "--exclusive" not in argv or requested.get("exclusive") is not True):
        raise ValueError("capture command/rate/exclusive metadata mismatch")
    for option, expected in (("--input", src / "stimulus.f32"), ("--output", dest / "capture.f32"),
                             ("--metadata", dest / "capture.json")):
        # A relocated evidence directory retains its original absolute argv.
        # Compare the immutable case/file suffix rather than forbid relocation.
        if Path(argument(option)).parts[-2:] != expected.parts[-2:]:
            raise ValueError(f"capture command references a different case: {option}")
    size = (dest / "capture.f32").stat().st_size
    count = size // 4
    if (not count or size % 4 or capture.get("capture", {}).get("logical_samples") != count
            or capture.get("capture", {}).get("logical_rate") != rate
            or meta.get("samples") != (src / "stimulus.f32").stat().st_size // 4):
        raise ValueError("capture/input sample-count metadata mismatch")
    if "input_sha256" in capture and capture["input_sha256"] != input_hash:
        raise ValueError("audio adapter recorded a different stimulus hash")
    raw_stereo_verified = False
    if capture.get("backend") == "alsa-utils-raw-stereo-v1":
        import triad_live_alsa as alsa
        for kind, key in (("capture", "capture_log"), ("playback", "playback_log")):
            name = Path(capture.get("files", {}).get(key, "")).name
            expected = "capture.f32." + ("arecord" if kind == "capture" else "aplay") + ".stderr.log"
            if name != expected:
                raise ValueError("ALSA receipt refers to an unexpected negotiated-format log")
            log = (dest / name).read_text()
            if re.search(r"\b(?:overrun|underrun|xrun)\b", log, re.I):
                raise ValueError("retained ALSA log reports a discontinuity")
            if alsa.parse_hw_format(log, rate, kind.upper()) != capture["audio_format"][kind]:
                raise ValueError("retained ALSA negotiated format differs from metadata")
        stereo = dest / "capture.f32.stereo.s16le"
        if stereo.stat().st_size != count*4 or digest(stereo) != capture["capture"].get("stereo_sha256"):
            raise ValueError("raw stereo capture differs from the adapter receipt")
        raw_stereo_verified = True
    return dict(stimulus_sha256=input_hash, capture_sha256=capture_hash,
                available_receipt_hashes_verified=True, metadata_consistency_verified=True,
                raw_stereo_hash_verified=raw_stereo_verified,
                missing_historical_receipt_hashes=missing_hashes,
                metadata_hash_bound_at_capture="capture_metadata_sha256" not in missing_hashes,
                scope="retained measurement receipts, not cryptographic authentication or a retroactive metadata hash")


def declared_low_frequency_model_order(prepared):
    path = Path(prepared) / "plan.json"
    order = json_read(path).get("low_frequency_model_order", 3) if path.exists() else 3
    if type(order) is not int or order not in (3,4):
        raise ValueError("declared low-frequency model order must be 3 or 4")
    return order


def fit_training(prepared, capture, output, *, posthoc=False):
    src, dest = prepared / "training", capture / "training"
    verification = verify_capture_receipt(src, dest)
    meta = json_read(src / "stimulus.json")
    tx = np.fromfile(src / "stimulus.f32", dtype="<f4")
    rx = np.fromfile(dest / "capture.f32", dtype="<f4")
    kwargs, low_identity = {}, None
    low_source, low_capture = prepared / "training-low", capture / "training-low"
    if low_source.exists():
        low_verification = verify_capture_receipt(low_source, low_capture)
        low_meta = json_read(low_source / "stimulus.json")
        low_tx = np.fromfile(low_source / "stimulus.f32", dtype="<f4")
        low_rx = np.fromfile(low_capture / "capture.f32", dtype="<f4")
        order = declared_low_frequency_model_order(prepared)
        section = model.fit_low_frequency_section(low_tx, low_rx, FS, low_meta["markers"], input_pcm="s16-native", model_order=order)
        kwargs["low_frequency_section"] = section
        low_identity = dict(stimulus_sha256=low_verification["stimulus_sha256"],
                            capture_sha256=low_verification["capture_sha256"], fitting_uses_holdouts=False,
                            model_order=order)
        json_write(output / "low-frequency-section.json", section)
    profile = model.fit_audio_profile(tx, rx, FS, meta["markers"], input_pcm="s16-native", **kwargs)
    profile["campaign_training_identity"] = dict(stimulus_sha256=digest(src / "stimulus.f32"),
        capture_sha256=digest(dest / "capture.f32"), fitting_uses_holdouts=False,
        frozen_utc_unix_seconds=time.time(), posthoc_training_only_recovery=posthoc)
    if low_identity is not None:
        profile["campaign_training_identity"]["low_frequency_training"] = low_identity
    json_write(output / "audio-profile.json", profile)
    json_write(output / "profile-freeze.json", dict(profile_sha256=digest(output / "audio-profile.json"),
        frozen_utc_unix_seconds=profile["campaign_training_identity"]["frozen_utc_unix_seconds"],
        holdout_captures_used=0, posthoc_training_only_recovery=posthoc,
        chronological_validation=not posthoc, training_receipt_verification=verification))
    return profile


def verify_profile_receipt(prepared, capture, profile):
    training = verify_capture_receipt(prepared / "training", capture / "training")
    identity = profile.get("campaign_training_identity", {})
    if (identity.get("stimulus_sha256") != training["stimulus_sha256"]
            or identity.get("capture_sha256") != training["capture_sha256"]
            or identity.get("fitting_uses_holdouts") is not False):
        raise ValueError("profile identity does not match training raw capture and stimulus")
    freeze_path = capture / "profile-freeze.json"
    report = dict(training=training, profile_sha256=digest(capture / "audio-profile.json"),
                  chronological_validation=False, freeze_receipt_present=freeze_path.exists())
    if (prepared / "training-low").exists() or "low_frequency_training" in identity:
        low_verification = verify_capture_receipt(prepared / "training-low", capture / "training-low")
        low_identity = identity.get("low_frequency_training", {})
        if (low_identity.get("stimulus_sha256") != low_verification["stimulus_sha256"]
                or low_identity.get("capture_sha256") != low_verification["capture_sha256"]
                or low_identity.get("fitting_uses_holdouts") is not False
                or low_identity.get("model_order",3) != declared_low_frequency_model_order(prepared)):
            raise ValueError("combined profile does not match low-frequency training capture and stimulus")
        report["low_frequency_training"] = low_verification
    if not freeze_path.exists():
        report["status"] = "posthoc_unwitnessed_profile_freeze"
        return report
    freeze = json_read(freeze_path)
    if freeze.get("profile_sha256") != report["profile_sha256"] or freeze.get("holdout_captures_used") != 0:
        raise ValueError("frozen profile hash/holdout receipt mismatch")
    frozen = freeze.get("frozen_utc_unix_seconds")
    if not isinstance(frozen, (int, float)) or not math.isfinite(frozen) or frozen != identity.get("frozen_utc_unix_seconds"):
        raise ValueError("profile freeze timestamp differs from training identity")
    starts = []
    for path in sorted((capture / "stages").glob("*.json")):
        row = json_read(path)
        if row.get("name", "").startswith("capture-") and row["name"] not in ("capture-training", "capture-training-low"):
            if "started_utc_unix_seconds" in row:
                starts.append(row["started_utc_unix_seconds"])
    if freeze.get("posthoc_training_only_recovery") or identity.get("posthoc_training_only_recovery"):
        report["status"] = "posthoc_training_only_recovery"
    elif starts:
        if frozen > min(starts):
            raise ValueError("profile freeze occurred after a held-out capture started")
        report.update(status="profile_frozen_before_retained_holdout_starts", chronological_validation=True,
                      first_holdout_started_utc_unix_seconds=min(starts))
    else:
        report["status"] = "freeze_hash_verified_chronology_unwitnessed"
    report["frozen_utc_unix_seconds"] = frozen
    return report


def carrier_response_report(profile, carrier_hz, spacing_hz, bandwidth_hz):
    frequencies = [carrier_hz-spacing_hz, carrier_hz, carrier_hz+spacing_hz]
    response = model.spectral_response(profile, frequencies)
    support = profile.get("measured_frequency_support_hz")
    if support is not None:
        support = [max(support[0], profile.get("model_extrapolation_below_hz", support[0])), support[1]]
    rows = []
    for frequency, h in zip(frequencies, response):
        inside = support is not None and support[0] <= frequency <= support[1]
        rows.append(dict(frequency_hz=float(frequency), model_response_real=float(h.real),
            model_response_imag=float(h.imag), model_amplitude=float(abs(h)),
            model_phase_radians=float(np.angle(h)), inside_measured_frequency_extent=inside,
            evidence="frozen training-model prediction" if inside else "unidentified extrapolation outside measured frequency extent"))
    return dict(carriers=rows, measured_frequency_extent_hz=support,
        complete_nominal_band_inside_measured_extent=bool(support is not None and
            carrier_hz-bandwidth_hz/2 >= support[0] and carrier_hz+bandwidth_hz/2 <= support[1]),
        independent_carrier_measurements=False, coherence_contiguous_band_guarantee=False,
        phase_reference="public-marker aligned causal model including retained alignment guard; not calibrated absolute analog delay",
        scope="model frequency samples do not replace independent measured per-carrier transfer experiments")


def low_frequency_diagnostic(tx, aligned, mask, frequencies, fs):
    """Cheap deterministic line fits, with no white-noise confidence fiction."""
    step = max(1, int(fs / 100))
    count = min(len(tx), len(aligned)) // step
    t = (np.arange(count) * step + (step - 1) / 2) / fs
    x = np.asarray(tx[:count*step]).reshape(count, step).mean(axis=1)
    y = np.asarray(aligned[:count*step]).reshape(count, step).mean(axis=1)
    valid = np.asarray(mask[:count*step]).reshape(count, step).all(axis=1)
    freq = [f for f in frequencies if f < 10]
    cols = [np.ones(count), t / max(t[-1], 1.)]
    for f in freq:
        cols.extend([np.cos(2*np.pi*f*t), np.sin(2*np.pi*f*t)])
    a = np.array(cols).T[valid]
    xx, yy = np.linalg.lstsq(a, x[valid], rcond=None)[0], np.linalg.lstsq(a, y[valid], rcond=None)[0]
    residual = y[valid] - a @ yy
    rows = []
    duration = len(tx) / fs
    for i, f in enumerate(freq):
        j = 2 + 2*i
        gx, gy = complex(xx[j], -xx[j+1]), complex(yy[j], -yy[j+1])
        gain = gy/gx if abs(gx) > 1e-20 else 0j
        guard = []
        for gf in (max(1/duration, f-1.5/duration), f+1.5/duration):
            g = np.column_stack((np.cos(2*np.pi*gf*t[valid]), np.sin(2*np.pi*gf*t[valid])))
            coeff = np.linalg.lstsq(g, residual, rcond=None)[0]
            guard.append(float(np.linalg.norm(coeff)))
        floor = max(guard)
        rows.append(dict(frequency_hz=f, observed_cycles=f*duration,
                         independently_resolved=f*duration >= 2 and abs(gy) > 3*floor,
                         response_real=gain.real, response_imag=gain.imag,
                         response_amplitude=abs(gain), response_phase_radians=float(np.angle(gain)),
                         output_component_amplitude=abs(gy), neighboring_residual_amplitude=floor,
                         amplitude_plus_residual_envelope=(abs(gy)+3*floor)/max(abs(gx),1e-30),
                         envelope_is_confidence_bound=False,
                         interpretation="component line fit" if f*duration>=2 else "unidentified sub-cycle extrapolation; no transfer claim"))
    return dict(rows=rows, joint_design_condition=float(np.linalg.cond(a)),
                block_average_samples=step, confidence_interval=None,
                uncertainty_scope="one fixed capture with colored residual and correlated low-frequency regressors; no independent-repeat confidence claim",
                reference_frequency_hz=max(frequencies),
                reference_scope="high-frequency reference retained in raw PCM and broad-band comparison; excluded from low-frequency line fit")


def quiet_noise_check(tx, aligned, predicted, valid, manifest, profile, fs):
    """Held-out quiet PSD check; finite records do not establish Gaussianity."""
    pieces, actuals, spans = [], [], []
    for segment in manifest.get("segments", []):
        if segment.get("kind") != "quiet":
            continue
        first, count = int(segment["start"]), int(segment["samples"])
        if first < 0 or count <= 0 or first+count > len(tx):
            raise ValueError("quiet segment has invalid positive input support")
        if np.any(np.asarray(tx[first:first+count]) != 0):
            raise ValueError("declared quiet segment contains nonzero transmitted samples")
        # Avoid the immediate boundary while retaining continuous model tails.
        trim = min(round(.05*fs), count//4)
        a, b = first+trim, first+count-trim
        if b-a < 256 or not np.all(valid[a:b]):
            continue
        pieces.append(np.asarray(aligned[a:b])-np.asarray(predicted[a:b]))
        actuals.append(np.asarray(aligned[a:b]))
        spans.append(dict(start=a, samples=b-a))
    if not pieces:
        return dict(status="no_complete_explicit_quiet_segments", scope="no implicit payload quietness assumption")
    nperseg = min(4096, min(map(len, pieces)))
    nperseg = 2**int(math.floor(math.log2(nperseg)))
    accumulated, weight = None, 0
    for x in pieces:
        f, p = scipy_signal.welch(x, fs=fs, window="hann", nperseg=nperseg,
                                 noverlap=nperseg//2, detrend="constant", scaling="density")
        windows = 1+(len(x)-nperseg)//(nperseg//2)
        accumulated = p*windows if accumulated is None else accumulated+p*windows
        weight += windows
    measured = accumulated/weight
    expected = np.interp(f, profile["noise"]["frequency_hz"], profile["noise"]["psd_fs_squared_per_hz"])
    df = float(f[1]-f[0])
    bands = []
    edges = [(0.,100.), (100.,1000.), (1000.,10000.), (10000.,21000.), (21000.,fs/2)]
    for lo, hi in edges:
        hi = min(hi, fs/2)
        if hi <= lo:
            continue
        select = (f >= lo) & ((f <= hi) if hi == fs/2 else (f < hi))
        observed_power, model_power = float(measured[select].sum()*df), float(expected[select].sum()*df)
        bands.append(dict(band_hz=[lo,hi], observed_residual_power=observed_power,
            frozen_training_noise_power=model_power,
            observed_to_training_db=10*math.log10(max(observed_power,1e-300)/max(model_power,1e-300)),
            frequency_bins=int(select.sum())))
    actual, residual = np.concatenate(actuals), np.concatenate(pieces)
    return dict(status="heldout_quiet_diagnostic", explicit_quiet_spans=spans,
                quiet_seconds=len(actual)/fs, quiet_mean=float(actual.mean()),
                quiet_rms=float(np.sqrt(np.mean(actual**2))), residual_mean=float(residual.mean()),
                residual_rms=float(np.sqrt(np.mean(residual**2))),
                psd_resolution_hz=df, welch_window_samples=nperseg, overlapping_welch_windows=weight,
                bands=bands, independent_hardware_observations=1, confidence_interval=None,
                scope="explicit zero-input heldout segments; full continuous deterministic prediction subtracted before PSD; band powers compare one capture with training",
                uncertainty="overlapping Welch windows and frequency bins are not independent hardware trials; no Gaussianity, long-term stationarity or false-admission claim")


def public_timing_diagnostics(real, predicted, bank, sample_rate, manifest, valid, candidate_bits):
    """Supplement the nominal bank using only its declared public prior."""
    configuration = manifest.get("resolved_configuration", {})
    bounds = configuration.get("initial_arrival_bounds_seconds")
    if bounds is None:
        return dict(status="public_prior_unavailable", operational_admission=None)
    if (not isinstance(bounds, list) or len(bounds)!=2 or not all(math.isfinite(x) for x in bounds)
            or bounds[0]>bounds[1]):
        raise ValueError("malformed declared public timing prior")
    if bounds == [0,0]:
        return dict(status="nominal_zero_prior", arrival_bounds_seconds=bounds, operational_admission=None)
    # A deliberately bounded supplement, not implicit permission to introduce
    # an arbitrarily expensive exhaustive acquisition campaign during replay.
    count = math.ceil((bounds[1]-bounds[0])*sample_rate*4)+3
    if bank.shape[2] != 1 or count > 4096:
        return dict(status="unsupported_public_timing_regime", arrival_bounds_seconds=bounds,
            estimated_grid_positions=count, maximum_grid_positions=4096,
            reason="this diagnostic supports one-column banks and at most 4096 public timing positions",
            operational_admission=None)
    kwargs=dict(fractional_phases=4, mask=valid, candidate_bits=candidate_bits)
    try:
        real_result=wave.bounded_timing_projection(real,bank,sample_rate,bounds,**kwargs)
        predicted_result=wave.bounded_timing_projection(predicted,bank,sample_rate,bounds,**kwargs)
    except (ValueError, NotImplementedError) as exc:
        return dict(status="public_timing_diagnostic_unsupported", arrival_bounds_seconds=bounds,
                    reason=str(exc), original_nominal_metrics_preserved=True, operational_admission=None)
    return dict(status="public_timing_diagnostic", arrival_bounds_seconds=bounds,
        prior_source="native manifest resolved_configuration.initial_arrival_bounds_seconds",
        truth_alignment_used=False, original_nominal_metrics_preserved=True,
        operational_admission=None, false_admission_probability=None,
        calibration="search maximum is uncalibrated; bit ranking is neither authentication nor an admission",
        real=real_result, predicted=predicted_result)


def analyze_case(case, prepared, capture, output, profile, deadline, *, trials=256):
    src, dest = prepared / case["id"], capture / case["id"]
    verification = verify_capture_receipt(src, dest)
    meta = json_read(src / "stimulus.json")
    tx = np.fromfile(src / "stimulus.f32", dtype="<f4")
    rx = np.fromfile(dest / "capture.f32", dtype="<f4")
    aligned, valid, alignment = model.align_capture(tx, rx, FS, meta["markers"]["markers"])
    deadline.remaining()
    pcm_tx = model.quantize_production_playback(tx)
    predicted = model.simulate_audio_profile(tx, profile, include_noise=False, production_s16=True)
    report = dict(id=case["id"], kind=case["kind"], alignment=alignment,
        evidence_kind=meta["details"].get("evidence_kind", "independent physical probe"),
        profile_sha256=digest(output / "audio-profile.json"),
        capture_receipt_verification=verification,
        energy_and_model_residual={**model.assess_prediction(pcm_tx, aligned, profile, valid, prediction=predicted),
                                  "scope":"entire valid wrapped stimulus including public markers; not a payload-only metric"},
        operational_admission=None, false_admission_probability=None,
        independent_hardware_observations=1,
        causal_alignment_guard_preserved=True,
        training_model_checks_pass=profile.get("linear_training_checks_pass"),
        training_model_flags=profile.get("training_flags", []),
        quantization="composite TX uses exact native S16 truncation; candidate channel operator stays linear")
    p = meta["markers"]["payload"]
    a, b = p["start"], p["start"] + p["samples"]
    if not 0 <= a < b <= len(tx):
        raise ValueError("payload extent is outside the wrapped stimulus")
    payload_valid = np.zeros(len(valid), dtype=bool)
    payload_valid[a:b] = valid[a:b]
    report["payload_energy_and_model_residual"] = {
        **model.assess_prediction(pcm_tx, aligned, profile, payload_valid, prediction=predicted),
        "scope":"payload support only, using the same full continuous prediction; synchronization markers excluded, no filter reset or refit",
        "payload_start":a, "payload_samples":b-a}
    report["heldout_quiet_noise"] = quiet_noise_check(pcm_tx, aligned, predicted, valid, meta["markers"], profile, FS)
    if case["kind"] == "native":
        m = json_read(src / "native/manifest.json")
        report["carrier_frequency_response"] = carrier_response_report(profile, case["carrier_hz"],
            m["carrier_spacing_hz"], m["occupied_bandwidth_hz"])
    if case["kind"] == "low_frequency":
        report["components"] = low_frequency_diagnostic(pcm_tx[a:b], aligned[a:b], valid[a:b], case["frequencies_hz"], FS)
    elif case["kind"] == "validation_low":
        report["low_frequency_validation"] = model.assess_low_frequency_validation(
            tx, aligned, FS, meta["markers"], profile, valid, input_pcm="s16-native")
    elif case["kind"] == "native" and not case["partial"]:
        native = wave.load_export(src / "native")
        m, nfs = native["manifest"], native["sample_rate"]
        bridge = meta["details"]["bridge"]
        kwargs = dict(occupied_bandwidth_hz=m["occupied_bandwidth_hz"],
                      source_start_seconds=bridge["source_start_seconds"],
                      expected_samples=len(native["signal"]), undo_fixed_gain=case["gain"])
        real, rv, ddc = wave.from_audio(aligned[a:b], FS, nfs, case["carrier_hz"], **kwargs)
        pred, pv, _ = wave.from_audio(predicted[a:b], FS, nfs, case["carrier_hz"], **kwargs)
        # Apply one continuous real-audio model to each *public* column.  No
        # payload-fitted gain or carrier-label-dependent attenuation is used.
        bank = np.empty_like(native["basis"], dtype=np.complex64)
        for ci in range(bank.shape[0]):
            for col in range(bank.shape[2]):
                deadline.remaining()
                audio, _ = wave.to_audio(native["basis"][ci, :, col], nfs, FS,
                    case["carrier_hz"], occupied_bandwidth_hz=m["occupied_bandwidth_hz"],
                    fixed_gain=case["gain"], source_start_seconds=bridge["source_start_seconds"], max_peak=.9)
                padded = np.zeros(len(tx), np.float32)
                padded[a:a+len(audio)] = audio
                filtered = model.apply_audio_profile(padded, profile)
                bank[ci, :, col], _, _ = wave.from_audio(filtered[a:b], FS, nfs, case["carrier_hz"], **kwargs)
        # The physical-validity mask is conservatively sampled on the public
        # grid; from_audio independently masks its filter edge support.
        indices = np.minimum(len(valid[a:b])-1, np.floor(np.arange(len(real))*FS/nfs).astype(int))
        score_valid = rv & pv & valid[a:b][indices] & native["validity"]
        edge = math.ceil((bridge["resample_half_support_seconds"] + ddc["resample_half_support_seconds"]) * nfs)
        if edge:
            score_valid[:edge] = False
            score_valid[-edge:] = False
        report["direct_conversion"] = ddc
        report["real_projection"] = wave.project_diagnostic(real, bank, score_valid, candidate_bits=native["candidate_bits"])
        report["predicted_projection"] = wave.project_diagnostic(pred, bank, score_valid, candidate_bits=native["candidate_bits"])
        report["public_timing_diagnostic"] = public_timing_diagnostics(
            real,pred,bank,nfs,m,score_valid,native["candidate_bits"])
        report["conditional_noise_margin"] = wave.noise_margin_comparison(real, pred, bank,
            CN0_GRID, case["seed"] + 601, trials=trials,
            sample_rate=nfs, duration_seconds=m["nominal_payload_energy_seconds_lower"],
            reference_power=m.get("noise_reference_power", 1.), truth_bit=m["signal_bit"],
            candidate_bits=native["candidate_bits"], mask=score_valid, sampled_validation_trials=1)
        report["channel_energy"] = {k: v for k, v in m.items() if "energy" in k}
    elif case["kind"] == "native":
        report["full_bit_margin"] = None
        report["component_only_reason"] = "partial actual-rate native source window; minimum distinguishable full pattern is not transmitted"
        guard = round(5*FS)
        if b-a > 2*guard+32:
            interior = np.zeros(len(valid), dtype=bool)
            interior[a+guard:b-guard] = valid[a+guard:b-guard]
            report["interior_component_energy_and_model_residual"] = {
                **model.assess_prediction(pcm_tx, aligned, profile, interior, prediction=predicted),
                "edge_guard_seconds_each_side":5., "interior_seconds":(b-a-2*guard)/FS,
                "center_carrier_cycles":case["carrier_hz"]*(b-a-2*guard)/FS,
                "scope":"interior finite component window; five-second guard is not a mathematical settling guarantee, and sub-cycle support does not measure steady-state transfer"}
    deadline.remaining()
    json_write(output / (case["id"] + "-analysis.json"), report)
    return report


def copy_exact(src, dest):
    with Path(src).open("rb") as source, Path(dest).open("xb") as target:
        shutil.copyfileobj(source, target)


def prepare_resume(prepared, origin, output, plan, deadline):
    """Reuse verified completed evidence without reopening or rewriting it."""
    prepared, origin, output = map(lambda p: Path(p).resolve(), (prepared, origin, output))
    if origin == output or output.is_relative_to(origin):
        raise ValueError("continuation output must be separate from original capture evidence")
    original_identity = json_read(origin / "input-identity.json")
    if (original_identity.get("prepared_sha256") != digest(prepared / "prepared.json")
            or original_identity.get("plan_sha256") != digest(prepared / "plan.json")):
        raise ValueError("continuation prepared-input identity differs from the original run")
    original_summary = json_read(origin / "summary.json")
    wall = original_summary.get("whole_command_wall_seconds")
    if original_summary.get("status") not in ("failed", "complete") or not isinstance(wall, (int,float)) or not math.isfinite(wall) or wall < 0:
        raise ValueError("continuation requires a completed original command summary and active wall time")
    profile = json_read(origin / "audio-profile.json")
    verification = verify_profile_receipt(prepared, origin, profile)
    if not verification["freeze_receipt_present"] or not verification["chronological_validation"]:
        raise ValueError("continuation requires a verified profile frozen before original holdouts")
    previous = json_read(origin / "continuation.json") if (origin / "continuation.json").exists() else {}
    prior_active = float(previous.get("prior_capture_run_active_seconds", 0.)) + wall
    if not math.isfinite(prior_active) or prior_active < 0:
        raise ValueError("invalid previous active wall-time chain")
    rows, reused = [], set()
    for case in plan["cases"]:
        deadline.remaining()
        if case.get("status") == "unsupported_audio_geometry":
            continue
        name = case["id"]
        directory = origin / name
        if not (directory / "capture.json").exists():
            rows.append(dict(id=name,status="missing_capture_will_run_fresh"))
            continue
        metadata = json_read(directory / "capture.json")
        if metadata.get("status") != "complete" or not (directory / "capture-identity.json").exists():
            rows.append(dict(id=name,status="incomplete_or_unsealed_capture_will_run_fresh",
                             original_failure=metadata.get("failure"), original_metadata_sha256=digest(directory / "capture.json")))
            continue
        receipt = verify_capture_receipt(prepared / name, directory)
        os.symlink(str(directory.resolve()), output / name, target_is_directory=True)
        reused.add(name)
        rows.append(dict(id=name,status="verified_capture_reused_without_recapture_or_reanalysis",
                         origin=str(directory.resolve()), receipt_verification=receipt))
    training_ids = {c["id"] for c in plan["cases"] if c["kind"] in ("training", "training_low")}
    if not training_ids.issubset(reused):
        raise ValueError("continuation must reuse every original calibration capture")
    copy_exact(origin / "audio-profile.json", output / "audio-profile.json")
    copy_exact(origin / "profile-freeze.json", output / "profile-freeze.json")
    if (origin / "low-frequency-section.json").exists():
        copy_exact(origin / "low-frequency-section.json", output / "low-frequency-section.json")
    # UTC estimate is descriptive; only measured monotonic active durations
    # consume the budget. Human waiting cannot masquerade as processor time.
    starts = [json_read(p) for p in sorted((origin / "stages").glob("*.json"))]
    first = next((r for r in starts if "started_utc_unix_seconds" in r and "hierarchy_elapsed_at_start_seconds" in r), None)
    if first is not None:
        finished_utc = first["started_utc_unix_seconds"] - first["hierarchy_elapsed_at_start_seconds"] + wall
        finish_basis = "first stage UTC minus initial elapsed, plus measured command wall time"
    else:
        finished_utc = (origin / "summary.json").stat().st_mtime
        finish_basis = "summary filesystem modification time; descriptive fallback only"
    pause = max(0., time.time()-finished_utc)
    receipt = dict(schema="triad-live-continuation-v1", origin=str(origin),
        original_summary_sha256=digest(origin / "summary.json"),
        original_input_identity_sha256=digest(origin / "input-identity.json"),
        original_status=original_summary["status"], original_failure=original_summary.get("result", {}).get("error"),
        prior_capture_run_active_seconds=prior_active, immediate_prior_command_wall_seconds=wall,
        human_pause_seconds_estimate=pause, pause_estimate_basis=finish_basis,
        cumulative_human_pause_seconds_estimate=pause+float(previous.get("cumulative_human_pause_seconds_estimate",0.)),
        profile_verification=verification, cases=rows, capture_backend=original_identity.get("capture_backend"),
        uninterrupted_command=False, reuse_access="runner reads reused symlink targets only; symlinks do not enforce filesystem write protection")
    json_write(output / "continuation.json", receipt)
    return profile, reused, receipt


def run_campaign(args, output, deadline, stages):
    if not args.live:
        raise ValueError("--live is required; no audio hardware has been opened")
    prepared = Path(args.prepared).resolve()
    resume, reused, profile = None, set(), None
    with stages.stage("verify-prepared"):
        info, plan = verify_prepared(prepared, deadline)
        if args.resume_capture is not None:
            profile, reused, resume = prepare_resume(prepared, args.resume_capture, output, plan, deadline)
            if resume["capture_backend"] != args.capture_backend:
                raise ValueError("continuation capture backend differs from the frozen original route")
        prior_wall = info.get("preparation_wall_seconds", 0.) + info.get("planning_wall_seconds", 0.)
        prior_wall += resume["prior_capture_run_active_seconds"] if resume is not None else 0.
        # A campaign bound is not reset by splitting preparation into commands.
        # Repeated offline analysis is explicitly outside physical-run evidence.
        deadline.start -= prior_wall
        if getattr(deadline, "alarm_armed", False):
            signal.setitimer(signal.ITIMER_REAL, deadline.remaining())
        preflight_budget([c["stimulus_seconds"] for c in info["cases"] if c["id"] not in reused], deadline.remaining(),
                         plan["budget"]["preparation_analysis_reserve_seconds"])
        json_write(output / "input-identity.json", dict(prepared=str(prepared), prepared_sha256=digest(prepared / "prepared.json"),
                    plan_sha256=digest(prepared / "plan.json"), provenance=provenance(args.binary, deadline),
                    capture_backend=args.capture_backend,
                    audio_binary_sha256=digest(args.audio_binary) if args.capture_backend == "native" else None,
                    alsa_adapter_sha256=digest(ROOT / "tools/triad_live_alsa.py") if args.capture_backend == "alsa-tools" else None,
                    background_noise_tool_sha256=(digest(args.background_noise_tool)
                        if args.capture_backend == "alsa-tools" and background_noise_enabled(args) else None)))
    with stages.stage("route-discovery"):
        route = discover_route(deadline, capture_source=args.capture_source, playback_sink=args.playback_sink,
                               capture_device=args.capture_device, playback_device=args.playback_device)
        if resume is not None:
            original_route = json_read(Path(args.resume_capture) / "route-before.json")
            for side in ("capture", "playback"):
                for field in ("device", "active_port", "volume", "mute"):
                    if route[side].get(field) != original_route[side].get(field):
                        raise ValueError("continuation physical route/mixer differs from original frozen calibration")
        json_write(output / "route-before.json", route)
    reports = []
    has_low_training = any(c["kind"]=="training_low" for c in plan["cases"])
    try:
        for case in plan["cases"]:
            if case["id"] in reused:
                reports.append(dict(id=case["id"],status="verified_capture_reused_not_reanalyzed"))
                continue
            if case.get("status") == "unsupported_audio_geometry":
                reports.append(dict(id=case["id"], status=case["status"], reasons=case["audio_geometry"]["reasons"]))
                continue
            if case["kind"] not in ("training", "training_low") and profile is None:
                raise ValueError("combined declared training profile must freeze before any holdout capture")
            with stages.stage("capture-" + case["id"]):
                capture_one(case, prepared, output, args.audio_binary, route, deadline,
                            backend=args.capture_backend, background_noise_args=args)
            if case["kind"] == "training" and has_low_training:
                continue
            if case["kind"] in ("training", "training_low"):
                with stages.stage("freeze-training-profile"):
                    profile = fit_training(prepared, output, output)
            else:
                if profile is None:
                    raise ValueError("frozen training profile must precede holdout capture")
                try:
                    with stages.stage("analyze-" + case["id"]):
                        reports.append(analyze_case(case, prepared, output, output, profile, deadline, trials=args.trials))
                except ValueError as exc:
                    # An explicit diagnostic limitation does not discard later
                    # physical regimes.  Capture failures themselves still stop.
                    report = dict(id=case["id"], status="analysis_failed", error=str(exc),
                                  operational_admission=None, offline_analysis_available=True)
                    reports.append(report)
                    json_write(output / (case["id"] + "-analysis-failure.json"), report)
    finally:
        # This program never writes mixer or server settings, so it must not
        # overwrite concurrent user changes under the name of restoration.
        if deadline.seconds - (deadline.clock() - deadline.start) > 1:
            try:
                after = discover_route(deadline, capture_source=route["capture"]["name"],
                                       playback_sink=route["playback"]["name"])
                json_write(output / "route-after.json", after)
                json_write(output / "route-preservation.json", dict(unchanged=after == route,
                    adjustments_by_suite=[], restore_actions=[],
                    externally_changed_settings_are_not_overwritten=True))
            except Exception as exc:
                json_write(output / "route-after-error.json", dict(error=str(exc), adjustments_by_suite=[]))
    return dict(status="diagnostics_complete", profile=plan["profile"],
                complete_suite_intended=plan["profile"] == "full", reports=[r["id"] for r in reports],
                diagnostic_outcomes=[dict(id=r["id"], status=r.get("status", "analyzed")) for r in reports],
                stimulus_seconds=info["budget"]["stimulus_seconds"],
                preparation_wall_seconds=info["preparation_wall_seconds"],
                planning_wall_seconds=info.get("planning_wall_seconds", 0.),
                hierarchy_wall_seconds=deadline.clock() - deadline.start,
                uninterrupted_command=resume is None,
                prior_capture_run_active_seconds=resume["prior_capture_run_active_seconds"] if resume else 0.,
                human_pause_seconds_estimate=resume["cumulative_human_pause_seconds_estimate"] if resume else 0.,
                preparation_outside_this_prepared_environment_run=True, limitations=LIMITATIONS,
                qualification="diagnostic measurements retained; interpretation and model validity are separate from command completion")


def analyze_campaign(args, output, deadline, stages):
    prepared, capture = Path(args.prepared).resolve(), Path(args.capture).resolve()
    _, plan = verify_prepared(prepared, deadline)
    json_write(output / "input-identity.json",dict(prepared=str(prepared),capture=str(capture),
        prepared_sha256=digest(prepared / "prepared.json"), plan_sha256=digest(prepared / "plan.json"),
        capture_summary_sha256=digest(capture / "summary.json") if (capture / "summary.json").exists() else None,
        provenance=provenance(args.binary,deadline), scope="offline analysis version; no new physical capture"))
    if (capture / "audio-profile.json").exists():
        profile = json_read(capture / "audio-profile.json")
        profile_verification = verify_profile_receipt(prepared, capture, profile)
        json_write(output / "audio-profile.json", profile)
        if (capture / "profile-freeze.json").exists():
            json_write(output / "profile-freeze.json", json_read(capture / "profile-freeze.json"))
    else:
        with stages.stage("recover-training-only-profile"):
            profile = fit_training(prepared, capture, output, posthoc=True)
        profile_verification = dict(status="posthoc_training_only_recovery", chronological_validation=False,
                                    training=verify_capture_receipt(prepared / "training", capture / "training"))
    json_write(output / "profile-verification.json", profile_verification)
    rows = []
    for case in plan["cases"]:
        if case["kind"] in ("training", "training_low"):
            continue
        if not (capture / case["id"] / "capture.json").exists():
            rows.append(dict(id=case["id"], status="not_captured"))
            continue
        metadata=json_read(capture / case["id"] / "capture.json")
        if metadata.get("status") != "complete" or not (capture / case["id"] / "capture-identity.json").exists():
            rows.append(dict(id=case["id"],status="incomplete_or_unsealed_capture_not_verified",failure=metadata.get("failure")))
            continue
        try:
            with stages.stage("analyze-" + case["id"]):
                analyze_case(case, prepared, capture, output, profile, deadline, trials=args.trials)
            rows.append(dict(id=case["id"], status="analyzed"))
        except (ValueError, RuntimeError, OSError) as exc:
            if isinstance(exc, BudgetExceeded):
                raise
            rows.append(dict(id=case["id"], status="failed", error=str(exc)))
    return dict(status="analysis_complete_with_explicit_limits", cases=rows,
                profile_verification=profile_verification, limitations=LIMITATIONS)


def parser():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("command", choices=("plan", "prepare", "run", "analyze"))
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--binary", type=Path, default=ROOT / "build/dev-cli/triad_long")
    p.add_argument("--audio-binary", type=Path, default=ROOT / "build/dev-cli/triad_audio_capture")
    p.add_argument("--capture-backend", choices=("alsa-tools", "native"), default="alsa-tools")
    p.add_argument("--budget-seconds", type=float, default=DEFAULT_BUDGET)
    p.add_argument("--reserve-seconds", type=float, default=600.)
    p.add_argument("--profile", choices=("full", "smoke"), default="full")
    p.add_argument("--epoch", type=int)
    p.add_argument("--skip-census", action="store_true", help="mark geometry census omitted; useful for an explicitly diagnostic smoke plan")
    p.add_argument("--plan", type=Path)
    p.add_argument("--prepared", type=Path)
    p.add_argument("--capture", type=Path)
    p.add_argument("--resume-capture", type=Path, help="run only unfinished cases using verified prior captures and the exact frozen profile")
    p.add_argument("--trials", type=int, default=256)
    p.add_argument("--live", action="store_true")
    p.add_argument("--capture-source")
    p.add_argument("--playback-sink")
    p.add_argument("--capture-device")
    p.add_argument("--playback-device")
    add_background_noise_arguments(p)
    return p


def main(argv=None):
    start = PROCESS_START if argv is None else time.monotonic()
    args = parser().parse_args(argv)
    validate_background_noise(args)
    if args.command != "run" and background_noise_enabled(args):
        raise ValueError("background noise is a transmit-time option for run, not plan/prepare/analyze")
    if args.command == "run" and args.capture_backend == "native" and background_noise_enabled(args):
        raise ValueError("background noise is currently supported by the ALSA-tools backend; native audio capture has no TX PCM mixer hook")
    deadline = Deadline(args.budget_seconds)
    deadline.start = start
    if not 1 <= args.trials <= 1000000:
        raise ValueError("noise trials must be in 1..1000000")
    if args.command == "prepare" and args.plan is None:
        raise ValueError("prepare requires --plan")
    if args.command in ("run", "analyze") and args.prepared is None:
        raise ValueError("run/analyze requires --prepared")
    if args.command == "analyze" and args.capture is None:
        raise ValueError("analyze requires --capture")
    if args.resume_capture is not None and args.command != "run":
        raise ValueError("--resume-capture is supported only by run")
    if args.command == "run" and not args.live:
        raise ValueError("--live is required; no audio hardware has been opened")
    output = fresh_directory(args.output)
    stages = Stages(output, deadline)
    status, result = "failed", {}
    old_handler = signal.getsignal(signal.SIGALRM)
    def expired(signum, frame):
        raise BudgetExceeded("whole-command wall deadline exceeded")
    signal.signal(signal.SIGALRM, expired)
    signal.setitimer(signal.ITIMER_REAL, deadline.remaining())
    deadline.alarm_armed = True
    try:
        if args.command == "plan":
            reserve = min(args.reserve_seconds, 60.) if args.profile == "smoke" else args.reserve_seconds
            budget = min(args.budget_seconds, 180.) if args.profile == "smoke" else args.budget_seconds
            with stages.stage("plan-and-geometry"):
                result = make_plan(args.binary, args.profile, deadline, budget, reserve,
                                   epoch=args.epoch, include_census=not args.skip_census)
                result["census_status"] = "omitted-explicitly" if args.skip_census else "computed-not-reception-proof"
                result["planning_wall_seconds"] = time.monotonic() - start
                json_write(output / "plan.json", result)
        elif args.command == "prepare":
            result = prepare(args.plan, output, args.binary, deadline, stages)
        elif args.command == "run":
            result = run_campaign(args, output, deadline, stages)
        else:
            result = analyze_campaign(args, output, deadline, stages)
        status = "complete"
        return 0
    except Exception as exc:
        result = dict(error=f"{type(exc).__name__}: {exc}", retry_performed=False,
                      retained_output=str(output), offline_analysis_available=True)
        print(result["error"], file=sys.stderr)
        return 1
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, old_handler)
        summary = dict(schema=SCHEMA, command=args.command, status=status, result=result,
                       whole_command_wall_seconds=time.monotonic()-start,
                       cpu_seconds=cpu_seconds(), output_bytes=bytes_under(output),
                       stages=stages.records, limitations=LIMITATIONS)
        json_write(output / "summary.json", summary)
        print(json.dumps(dict(status=status, output=str(output), wall_seconds=summary["whole_command_wall_seconds"])))


if __name__ == "__main__":
    raise SystemExit(main())
