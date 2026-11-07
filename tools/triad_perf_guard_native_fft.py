#!/usr/bin/env python3
"""Direct full-target native/FFT measurements for the reject-only guard.

This adapter does not extrapolate, qualify historical models, or make a
statistical decision. The immutable sampled FFT fixtures deliberately remain
identical across fresh runtime observations. Native observations use fresh seeds.
"""
from __future__ import annotations

import copy
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import time

import triad_perf_cases as fixtures
import triad_perf_native as native
import triad_perf_physical_bits as physical


NATIVE_BOUNDARY = "external complete native CLI including generation, channel, preparation, receiver, startup and output"
FFT_BOUNDARY = "resident worker: reload immutable sampled input, prepare full workspace, score full CFO/lag bank, finalize and IPC"
_GEOMETRY = ("sample_count", "sample_rate", "nfft", "lags", "lag_first_sample",
             "lag_last_sample", "lag_step_samples", "group_tile", "precision", "device",
             "array_bound_bytes", "memory_limit_bytes")
_PUBLIC_GEOMETRY = ("sample_count", "sample_rate", "lags", "lag_first_sample", "lag_last_sample",
                    "lag_step_samples", "precision", "device")
_RECEIVER_MODULES = ("triad_fft_acquire", "triad_group_acquire", "triad_gaussian_window",
                     "triad_noise_likelihood", "triad_gaussian_state", "triad_state_workspace")


def _api():
    name = "_triad_guard_measurement_api"
    if name not in sys.modules:
        spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name("triad-performance.py"))
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
    return sys.modules[name]


def _positive(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
        raise ValueError(label + " must be finite and positive")
    return float(value)


class NativeFFTWorkloads:
    """Two persistent receiver processes; one invocation per source/outer pair.

    ``deadline`` is a required finite absolute monotonic time. Call ``close`` even after a failed
    measurement. ``lifecycle`` contains profile costs excluded from individual
    target timers; they must still count toward the runner's total wall budget.
    """

    def __init__(self, context):
        self.context = dict(context)
        self.api = _api()
        self.roots = {name: Path(context[name + "_root"]).resolve() for name in ("reference", "candidate")}
        self.binaries = {"reference": Path(context["reference_binary"]).resolve(),
                         "candidate": Path(context["binary"]).resolve()}
        self.threads = context.get("threads", 1)
        self.native_threads = context.get("native_threads", self.threads)
        self.physical_bit_alignment = context.get("physical_bit_alignment", False)
        if type(self.physical_bit_alignment) is not bool:
            raise ValueError("physical_bit_alignment must be boolean")
        if any(type(v) is not int or v < 1 for v in (self.threads, self.native_threads)):
            raise ValueError("thread counts must be positive integers")
        self.device = context.get("device", "cpu")
        self.precision_mode = context.get("precision", "fp32")
        if self.precision_mode not in ("fp32", "fp64"):
            raise ValueError("precision must be fp32 (minimum) or fp64 (force)")
        self.deadline = context.get("deadline")
        if (isinstance(self.deadline, bool) or not isinstance(self.deadline, (int, float)) or
                not math.isfinite(self.deadline)):
            raise ValueError("deadline must be an explicit finite monotonic timestamp")
        self.workers, self.fft_cases, self.precisions, self.work_identities = {}, {}, {}, {}
        self.fft_recipes = fixtures.scenarios("ci")
        self.native_cases = {row["id"]: row for row in native.scenarios("ci")}
        # Maximal Added Symbols is the one additional maintained native branch.
        for row in native.scenarios("sustain"):
            if row["id"] == "native-added-max":
                self.native_cases[row["id"]] = row
        self.lifecycle = {"schema": "triad-guard-native-fft-lifecycle-v1", "events": [],
                          "closed": False, "prediction_or_historical_lookup_used": False}
        self._descriptors = None
        self._used_run_ids = set()
        self._source_paths = {}
        self._source_roots = {}
        self._worker_context_sequence = 0
        self._worker_context = None
        for name, root in self.roots.items():
            for relative in self.api.SOURCE_FILES:
                self._source_paths[name + ":" + relative] = root / relative
                self._source_roots[name + ":" + relative] = root
            self._source_paths[name + ":binary"] = self.binaries[name]
        for relative in ("triad-performance.py", "triad_perf_native.py", "triad_perf_cases.py",
                         "triad_perf_receiver.py", "triad_perf_physical_bits.py", "triad-performance-native-cases.json",
                         "triad-performance-cases.json", Path(__file__).name):
            self._source_paths["adapter:" + relative] = Path(__file__).with_name(relative)
        config = fixtures._config()
        for recipe in self.fft_recipes:
            plan_path = (fixtures.CONFIG_PATH.parent / config["source_plans"][recipe["source_plan"]]).resolve()
            self._source_paths["plan:" + recipe["source_plan"]] = plan_path
        self.sources = self._hash_paths(self._source_paths, self._source_roots)
        self.lifecycle["source_and_build_hashes"] = self.sources
        self.lifecycle["build_binding_scope"] = "source and executable bytes bound independently; compilation provenance belongs to runner build receipt"

    def _remaining(self):
        if self.lifecycle["closed"]:
            raise RuntimeError("native/FFT workload adapter is closed")
        if time.monotonic() >= self.deadline:
            raise TimeoutError("native/FFT guard deadline exhausted")

    @staticmethod
    def _hash_paths(paths, source_roots=None):
        result = {}
        for name, path in paths.items():
            root = (source_roots or {}).get(name)
            if root is not None:
                # Check only components inside the selected source root. The
                # root itself was resolved at setup; host parents may use links.
                relative = path.relative_to(root)
                component = root
                for part in relative.parts:
                    component = component / part
                    if component.is_symlink():
                        raise ValueError("missing or symlinked measurement input: " + str(component))
            # Older reference revisions predate the Serpent implementation.
            # Bind that absence so an appearing file invalidates the run.
            if (name in {"reference:" + relative for relative in fixtures.OPTIONAL_HISTORICAL_FILES}
                    and not path.exists() and not path.is_symlink()):
                result[name] = fixtures.ABSENT_SOURCE
                continue
            if path.is_symlink() or not path.is_file():
                raise ValueError("missing or symlinked measurement input: " + str(path))
            result[name] = native.file_sha(path)
        return result

    def _check_sources(self):
        if self._hash_paths(self._source_paths, self._source_roots) != self.sources:
            raise ValueError("source, adapter, configuration or binary changed during guard")

    def _event(self, kind, started, **details):
        self.lifecycle["events"].append({"kind": kind, "wall_seconds": time.perf_counter() - started, **details})

    def _fixtures(self):
        if self.fft_cases:
            return
        self._remaining()
        started = time.perf_counter()
        try:
            cases = fixtures.prepare_cases(self.binaries["candidate"], self.context["fixture_cache"],
                profile="ci", deadline=self.deadline, source_root=self.roots["candidate"])
        except subprocess.TimeoutExpired as exc:
            raise TimeoutError("sampled fixture preparation exceeded budget") from exc
        except fixtures.FixtureError as exc:
            raise RuntimeError("sampled fixture preparation failed: " + str(exc)) from exc
        if {row["id"] for row in cases} != {row["id"] for row in fixtures.scenarios("ci")}:
            raise ValueError("incomplete maintained FFT case inventory")
        self.fft_cases = {row["id"]: row for row in cases}
        self._event("immutable_fixture_preparation", started,
                    cases={row["id"]: row["source_bound_metadata"] for row in cases})
        self._check_sources()

    def descriptors(self):
        self._remaining()
        self._check_sources()
        if self._descriptors is None:
            identities = {name: {key: value for key, value in self.sources.items()
                                 if key.startswith(name + ":")} for name in self.roots}
            rows = []
            for case in self.native_cases.values():
                rows.append({"id": case["id"], "kind": "native", "boundary": NATIVE_BOUNDARY,
                    "geometry": {"target_seconds": case["target_seconds"], "batch_trials": case.get("batch_trials", 1),
                                 "arguments": case["args"], "native_threads": self.native_threads},
                    "case_sha256": native.digest(case), "source_identities": identities,
                    "physical_bit_alignment": self.physical_bit_alignment,
                    "arithmetic": "native-fp64", "scope": case["scope"], "full_target": True})
            for recipe in self.fft_recipes:
                rows.append({"id": "fft:" + recipe["id"], "kind": "fft", "boundary": FFT_BOUNDARY,
                    "geometry": {"recipe": recipe, "threads": self.threads, "device": self.device,
                                 "resolution": "complete source-bound native fixture and full CFO/lag grid resolved lazily before screen"},
                    "recipe_sha256": native.digest(recipe),
                    "source_plan_sha256": self.sources["plan:" + recipe["source_plan"]],
                    "source_identities": identities,
                    "arithmetic": "FP32 minimum eligible pilot or FP64 force, frozen before screen",
                    "requested_precision": self.precision_mode,
                    "full_target": True, "fixture_generation_charged_to_profile": True})
            self._descriptors = rows
        return copy.deepcopy(self._descriptors)

    def _start_workers(self, order=("reference", "candidate"), *, confirmation=False):
        if len(self.workers) == 2:
            return
        self._remaining()
        self._worker_context_sequence += 1
        context_id = f"fft-context-{self._worker_context_sequence:04d}"
        context_started = time.perf_counter()
        context_events = {"id": context_id, "creation_order": list(order),
                          "confirmation": confirmation, "workers": {}}
        for name in order:
            if name in self.workers:
                continue
            started = time.perf_counter()
            log_directory = Path(self.context["output"]) / "native-fft-workers" / context_id
            worker = self.api.ReceiverWorker(self.roots[name], threads=self.threads,
                deadline=self.deadline,
                log_directory=log_directory, name=name)
            self.workers[name] = worker
            expected = {module + ".py": self.sources[name + ":tools/" + module + ".py"]
                        for module in _RECEIVER_MODULES}
            if worker.identity.get("source_hashes") != expected or worker.identity.get("threads") != self.threads:
                raise ValueError("receiver worker loaded unexpected source or thread allocation")
            if worker.identity.get("tf32_allowed") is not False:
                raise ValueError("receiver worker did not preserve FP32-minimum arithmetic")
            worker_event = {"source": name, "pid": getattr(getattr(worker, "process", None), "pid", None),
                            "worker_instance": id(worker), "log_directory": str(log_directory),
                            "startup_wall_seconds": time.perf_counter() - started,
                            "external_startup_seconds": getattr(worker, "external_startup_seconds", None),
                            "identity": worker.identity}
            context_events["workers"][name] = worker_event
            self._event("worker_startup", started, source=name, context_id=context_id,
                        pid=worker_event["pid"], log_directory=str(log_directory), identity=worker.identity)
        context_events["setup_wall_seconds"] = time.perf_counter() - context_started
        self._worker_context = context_events

    def _close_worker_context(self, reason):
        context = self._worker_context
        if not self.workers:
            self._worker_context = None
            return {"reason": reason, "workers": {}, "teardown_wall_seconds": 0.}
        start = time.perf_counter()
        records = {}
        errors = []
        for name, worker in list(self.workers.items()):
            one = time.perf_counter()
            process = getattr(worker, "process", None)
            close_ack = False
            close_error = None
            close_timeout = False
            pre_cleanup_exit = process.poll() if process is not None else None
            if process is not None and pre_cleanup_exit is None:
                try:
                    response = worker.request({"action": "close"})
                    close_ack = response.get("closed") is True
                    if not close_ack:
                        raise RuntimeError("worker close protocol did not acknowledge closure")
                    process.stdin.close()
                    process.wait(timeout=2.)
                except Exception as exc:
                    close_error = f"{type(exc).__name__}: {exc}"
                    close_timeout = isinstance(exc, (TimeoutError, subprocess.TimeoutExpired))
                    if not close_timeout:
                        errors.append(name + ": close protocol failure: " + close_error)
            # Our close request may already have closed stdin. Suppress the
            # shared facade's second protocol request, while retaining its
            # bounded terminate/kill/reap and stream/thread cleanup finally.
            # This worker is retired permanently; the suite deadline is intact.
            if process is not None:
                worker.deadline = time.monotonic()-1.
            try:
                worker.close()
            except Exception as exc:
                errors.append(name + ": " + str(exc))
            exit_code = process.poll() if process is not None else None
            if process is not None and exit_code is None:
                errors.append(name + ": receiver process remained alive after close")
            event = {"pid": getattr(getattr(worker, "process", None), "pid", None),
                     "worker_instance": id(worker), "teardown_wall_seconds": time.perf_counter() - one,
                     "close_protocol_acknowledged": close_ack, "close_protocol_error": close_error,
                     "exit_code": exit_code, "pre_cleanup_exit": pre_cleanup_exit,
                     "cleanup_verified": (process is None or (exit_code is not None and
                         process.stdin.closed and process.stdout.closed and worker.log.closed and
                         not worker.reader.is_alive())),
                     "cleanup_mode": ("test_double_unobservable" if process is None else
                         ("graceful_exit" if close_ack and exit_code == 0 else
                          "owned_forced_after_timeout" if close_timeout else "forced_or_abnormal_exit"))}
            if not event["cleanup_verified"]:
                errors.append(name + ": receiver streams or reader remained alive after close")
            if process is not None and not close_timeout and exit_code not in (0, None):
                errors.append(name + ": worker exited abnormally outside owned timeout cleanup")
            records[name] = event
            self._event("worker_context_shutdown", one, source=name,
                        context_id=(context or {}).get("id"), pid=event["pid"], reason=reason,
                        close_acknowledged=close_ack, exit_code=exit_code,
                        cleanup_verified=event["cleanup_verified"], cleanup_mode=event["cleanup_mode"])
        self.workers.clear()
        self._worker_context = None
        result = {"id": (context or {}).get("id"), "reason": reason,
                  "workers": records, "teardown_wall_seconds": time.perf_counter() - start,
                  "cleanup_errors": errors}
        self.lifecycle.setdefault("context_cleanup", []).append(copy.deepcopy(result))
        if errors:
            raise RuntimeError("receiver worker context cleanup failed: " + "; ".join(errors))
        return result

    def _fixture_hashes(self, case):
        hashes = self._hash_paths({name: Path(case["input_directory"]) / name for name in fixtures.PUBLIC_FILES})
        if hashes != case["fixture_hashes"]:
            raise ValueError(case["id"] + ": immutable fixture bytes changed")
        return hashes

    def _freeze_work(self, case_id, work):
        if case_id not in self.work_identities:
            self.work_identities[case_id] = copy.deepcopy(work)
        elif work != self.work_identities[case_id]:
            raise ValueError(case_id + ": full work regime changed across observations")

    def _native_pair(self, case, seed, order):
        results = {}
        alignment = (physical.prepare_pair(self.binaries, [*case["args"], "--seconds", str(case["target_seconds"])],
            seed, case.get("batch_trials", 1), self.deadline,
            output=Path(self.context["output"])/"physical-inputs", order=order,
            input_bits=[int(native._option(case["args"],"--signal-bit",0))]*case.get("batch_trials",1))
            if self.physical_bit_alignment else None)
        complete_inputs = None
        if alignment is not None:
            self.lifecycle.setdefault("physical_source_maps", []).append(copy.deepcopy(alignment))
            complete_inputs = physical.complete_native_inputs(self.binaries,
                [*case["args"], "--seconds", str(case["target_seconds"])], seed, alignment, self.deadline,
                output=Path(self.context["output"])/"physical-inputs")
            self.lifecycle.setdefault("complete_input_preflights", []).append(copy.deepcopy(complete_inputs))
        for name in order:
            self._remaining()
            try:
                selected_case = (dict(case, args=[*case["args"], *physical.arguments(alignment, name)])
                                 if alignment is not None else case)
                results[name] = native.run_native(self.binaries[name], selected_case, case["target_seconds"], seed,
                    threads=self.native_threads, deadline=self.deadline,
                    timeout=max(1., self.deadline-time.monotonic()))
            except subprocess.TimeoutExpired as exc:
                raise TimeoutError("native full target exceeded guard budget") from exc
            except native.NativePerformanceError as exc:
                raise RuntimeError("native full target failed: " + str(exc)) from exc
        raw_parent=Path(self.context["output"])/"native-observations"
        raw_parent.mkdir(parents=True,exist_ok=True)
        raw_directory=Path(tempfile.mkdtemp(prefix=case["id"]+"-",dir=raw_parent))
        for name in order:
            (raw_directory/(name+"-raw.json")).write_text(json.dumps(results[name],indent=2,allow_nan=False)+"\n")
        if alignment is not None:
            for name in order:
                physical.validate_native_receipt(results[name], alignment, name)
                physical.validate_native_input_binding(results[name], complete_inputs, name)
        try:
            agreement = native.compare_native_outputs(results["reference"], results["candidate"])
        except native.NativePerformanceError as exc:
            raise ValueError("native paired correctness/work mismatch: " + str(exc) + "; raw=" + str(raw_directory)) from exc
        work = {}
        for name, result in results.items():
            if result["requested_seconds"] != case["target_seconds"] or result["batch_trials"] != case.get("batch_trials", 1):
                raise ValueError("native invocation shortened the declared full target")
            _positive(result["full_test_wall_seconds"], "native complete process wall")
            work[name] = {"regime": result["regime"], "total_rows": result["total_rows"],
                          "basis_values": result["counter_summary"]["basis_values"],
                          "blocks": result["counter_summary"]["blocks"],
                          "channel_samples": result["counter_summary"]["channel_samples"]}
        if any(work["reference"][key] != work["candidate"][key]
               for key in ("total_rows", "basis_values", "blocks", "channel_samples")):
            raise ValueError("native full target work differs across sources")
        self._freeze_work(case["id"], work)
        return {name: row["full_test_wall_seconds"] for name, row in results.items()}, {
            "boundary": NATIVE_BOUNDARY, "raw": results, "agreement": agreement,
            "work_identity_sha256": native.digest(work), "seed_applied_to_simulation": True,
            "physical_source_alignment": alignment, "complete_input_preflight": complete_inputs,
            "raw_observation_directory":str(raw_directory)}

    def _warm_fft_context(self, case, precision):
        """Warm one complete target on each newly created confirmation worker."""
        context = self._worker_context
        if context is None:
            raise RuntimeError("FFT worker context identity missing")
        target = len(case["frequency_offsets_hz"])
        expected_count = target * case["configuration"]["lag_group_count"]
        for name in context["creation_order"]:
            self._remaining()
            worker = self.workers[name]
            started = time.perf_counter()
            prepared = worker.request(self.api.prepare_request(case, precision, self.device))
            scored = worker.request(self.api.score_request(case, target))
            elapsed = _positive(time.perf_counter() - started, "FFT full-case warmup wall")
            if (prepared.get("geometry", {}).get("sample_count") != case["configuration"]["sample_count"] or
                    prepared.get("geometry", {}).get("precision") != precision or
                    prepared.get("geometry", {}).get("device") != self.device or
                    scored.get("measured_group_count") != expected_count or
                    scored.get("geometry", {}).get("group_count") != expected_count):
                raise ValueError("FFT warmup did not execute the complete frozen target geometry")
            worker_event = context["workers"][name]
            worker_event["warmup"] = {"wall_seconds": elapsed,
                "prepare_seconds": prepared.get("wall_seconds"),
                "score_seconds_including_finish": scored.get("wall_seconds_including_finish"),
                "measured_group_count": scored["measured_group_count"],
                "precision": precision, "device": self.device}
            self._event("full_target_warmup", started, source=name, context_id=context["id"],
                        pid=worker_event["pid"], case=case["id"], groups=expected_count)

    def _fft_pair(self, case, order, *, confirmation=False):
        self._start_workers(order, confirmation=confirmation)
        hashes = self._fixture_hashes(case)
        if case["id"] not in self.precisions:
            started = time.perf_counter()
            pilot_raw = {name: [] for name in self.workers}
            class RecordingWorker:
                def __init__(self, name, worker):
                    self.name, self.worker = name, worker

                def request(self, request):
                    result = self.worker.request(request)
                    pilot_raw[self.name].append({"request": copy.deepcopy(request), "result": copy.deepcopy(result)})
                    return result
            try:
                recorded_workers = {name: RecordingWorker(name, worker) for name, worker in self.workers.items()}
                if self.precision_mode == "fp64":
                    count = min(3, len(case["frequency_offsets_hz"]))
                    scores, times = {}, {}
                    for name, worker in recorded_workers.items():
                        prep = worker.request(self.api.prepare_request(case, "fp64", self.device))
                        scores[name] = worker.request(self.api.score_request(case, count))
                        times[name] = prep["wall_seconds"] + scores[name]["wall_seconds_including_finish"]
                    agreement = self.api.numeric_agreement(scores["reference"], scores["candidate"])
                    if not agreement["eligible"]:
                        raise ValueError("FFT forced FP64 pilot disagrees with pinned receiver")
                    selection = {"selected": "fp64", "alternatives": {"fp64": {"eligible": True, "checks": [agreement]}},
                        "tuning_total_seconds": {"fp64": times}, "qualification_cfo_count": count,
                        "selection_method": "forced_eligible_fp64", "selection_frozen_before_prediction_measurements": True}
                else:
                    selection = self.api.qualify_fft(case, recorded_workers, self.device)
            except self.api.SuiteError as exc:
                if "candidate FP64 disagrees" in str(exc):
                    raise ValueError("FFT arithmetic qualification failed: " + str(exc)) from exc
                raise RuntimeError("FFT arithmetic pilot execution failed: " + str(exc)) from exc
            selection["raw_pilot_measurements"] = pilot_raw
            selection["requested_precision"] = self.precision_mode
            selection["precision_semantics"] = "FP32 minimum permits explicitly selected FP64; FP64 force permits only FP64"
            selection["higher_precision_selected"] = self.precision_mode == "fp32" and selection["selected"] == "fp64"
            selection["fallback"] = selection["higher_precision_selected"] and not selection["alternatives"]["fp32"]["eligible"]
            self.precisions[case["id"]] = selection
            self._event("fft_numerical_precision_pilot", started, case=case["id"], selection=selection)
        selection = self.precisions[case["id"]]
        precision = selection["selected"]
        if self._worker_context is not None and self._worker_context.get("confirmation"):
            self._warm_fft_context(case, precision)
        target = len(case["frequency_offsets_hz"])
        expected_count = target * case["configuration"]["lag_group_count"]
        results, seconds = {}, {}
        for name in order:
            self._remaining()
            started = time.perf_counter()
            prepared = self.workers[name].request(self.api.prepare_request(case, precision, self.device))
            scored = self.workers[name].request(self.api.score_request(case, target))
            seconds[name] = _positive(time.perf_counter()-started, "FFT full external wall")
            _positive(prepared["wall_seconds"], "FFT preparation")
            _positive(scored["wall_seconds_including_finish"], "FFT full bank scoring")
            if prepared.get("input_hashes") != hashes:
                raise ValueError("FFT prepared different fixture bytes")
            if prepared.get("source_hashes") != self.workers[name].identity["source_hashes"]:
                raise ValueError("FFT prepared unexpected receiver source")
            geometry = prepared["geometry"]
            if (geometry["sample_count"] != case["configuration"]["sample_count"] or
                    geometry["lags"] != case["configuration"]["lag_group_count"] or
                    geometry["precision"] != precision or geometry["device"] != self.device or
                    geometry["lag_first_sample"] != 0 or geometry["lag_step_samples"] != case.get("lag_step_samples", 1) or
                    geometry["group_tile"] != case["group_tile"] or
                    geometry["memory_limit_bytes"] != case["memory_mib"] * 1024 * 1024):
                raise ValueError("FFT preparation changed full target geometry or arithmetic")
            scored_geometry = scored["geometry"]
            ranks = scored.get("rank_histogram", [])
            arithmetic = scored.get("arithmetic", {})
            if (scored.get("measured_group_count") != expected_count or
                    scored_geometry.get("group_count") != expected_count or
                    scored_geometry.get("base_group_count") != geometry["lags"] or
                    scored_geometry.get("sample_count") != geometry["sample_count"] or
                    scored_geometry.get("sample_rate") != geometry["sample_rate"] or
                    scored_geometry.get("nfft") != geometry["nfft"] or
                    scored.get("full_group_count_for_reporting") != expected_count or
                    scored_geometry.get("frequency_offsets_hz") != case["frequency_offsets_hz"] or
                    not ranks or any(type(v) is not int or v < 0 for v in ranks) or sum(ranks) != expected_count):
                raise ValueError("FFT score omitted full frequency/lag work or rank accounting")
            if (arithmetic.get("requested_correlation_precision") != precision or
                    arithmetic.get("observation_fft_device") != self.device or
                    arithmetic.get("likelihood_bulk") != precision or arithmetic.get("tf32_allowed") is not False or
                    arithmetic.get("implicit_fallback") is not False):
                raise ValueError("FFT scoring changed declared arithmetic or used an implicit fallback")
            results[name] = {"prepare": prepared, "score": scored, "external_seconds": seconds[name]}
        work = {name: {"prepare": {key: result["prepare"]["geometry"][key] for key in _GEOMETRY},
                       "score": result["score"]["geometry"]} for name, result in results.items()}
        if any(work["reference"]["prepare"][key] != work["candidate"]["prepare"][key]
               for key in _PUBLIC_GEOMETRY):
            raise ValueError("FFT paired public target geometry differs")
        agreement = self.api.numeric_agreement(results["reference"]["score"], results["candidate"]["score"])
        if not agreement["eligible"]:
            raise ValueError("FFT full bank numerical disagreement: " + str(agreement))
        self._freeze_work(case["id"], work)
        self._fixture_hashes(case)
        context = self._worker_context
        confirmation_context = None
        if confirmation:
            confirmation_context = {
                "protocol": "fresh-source-context-pair-v1",
                "identity_kind": "source_isolated_receiver_process",
                "context_id": context["id"],
                "creation_order": list(context["creation_order"]),
                "context_ids": {name: f"{context['id']}:{name}:pid-{row['pid']}"
                                for name, row in context["workers"].items()},
                "workers": copy.deepcopy(context["workers"]),
                "isolation_evidence": {name: {
                    "context_id": f"{context['id']}:{name}:pid-{row['pid']}",
                    "pid": row["pid"], "worker_instance": row["worker_instance"],
                    "setup": {key: row[key] for key in ("startup_wall_seconds", "external_startup_seconds", "log_directory")},
                    "warmup": copy.deepcopy(row.get("warmup")), "teardown": None}
                    for name, row in context["workers"].items()},
                "setup_wall_seconds": context["setup_wall_seconds"],
                "target_boundary_preserved": True,
                "warmup_policy": "full-target-outside-timer",
                "warmup_description": "one complete full CFO/lag target per worker outside measured target timer",
                "setup_warmup_costs_charged_to_campaign": True,
            }
        return seconds, {"boundary": FFT_BOUNDARY, "raw": results, "agreement": agreement,
            "precision_selection": selection, "work_identity_sha256": native.digest(work),
            "fixture_hashes": hashes, "seed_applied_to_simulation": False,
            "input_scope": "identical source-bound fixture; fresh complete runtime observation, no synthetic shortened data",
            "profile_costs_excluded_from_case_timer": ["fixture generation/cache validation", "worker startup", "full-target worker warmup", "precision pilot", "worker shutdown"],
            **({"confirmation_context": confirmation_context} if confirmation else {})}

    def measure_pair(self, case_id, run_id, seed, order=("reference", "candidate"), *, phase="screen"):
        self._remaining()
        if phase not in ("screen", "confirmation") or tuple(sorted(order)) != ("candidate", "reference") or len(order) != 2:
            raise ValueError("invalid guard phase or paired source order")
        if not isinstance(run_id, str) or not run_id.strip() or run_id in self._used_run_ids:
            raise ValueError("outer run_id must be nonempty and globally fresh within adapter")
        if type(seed) is not int or seed < 0:
            raise ValueError("seed must be a nonnegative integer")
        self._check_sources()
        if case_id in self.native_cases:
            case, measure = self.native_cases[case_id], self._native_pair
        else:
            if not isinstance(case_id, str) or not case_id.startswith("fft:") or case_id[4:] not in {c["id"] for c in self.fft_recipes}:
                raise ValueError("unknown native/FFT case: " + str(case_id))
            self._fixtures()
            case, measure = self.fft_cases[case_id[4:]], self._fft_pair
        self._used_run_ids.add(run_id)
        confirmation_context = None
        if measure == self._native_pair:
            seconds, details = measure(case, seed, order)
            if phase == "confirmation":
                # run_native owns one fresh subprocess per source, including
                # its startup, all batch trials, output and completed exit.
                # subprocess.run does not expose a PID, so retain its real
                # launch receipt under an invocation-owned unique directory.
                run_dir = Path(self.context["output"]) / "native-contexts" / native.digest(run_id)[:24]
                run_dir.mkdir(parents=True, exist_ok=False)
                for name in order:
                    (run_dir / (name + "-raw.json")).write_text(json.dumps(details["raw"][name], indent=2, allow_nan=False)+"\n")
                details["confirmation_context"] = {
                    "protocol": "fresh-source-context-pair-v1", "creation_order": list(order),
                    "context_ids": {name: str(run_dir / (name + "-raw.json")) for name in order},
                    "identity_kind": "invocation-owned-fresh-subprocess",
                    "isolation_evidence": {name: {"raw_launch_receipt": str(run_dir / (name + "-raw.json")),
                        "argv": details["raw"][name].get("argv"),
                        "launch_api": "subprocess.run; complete batch exit before next source"} for name in order},
                    "target_boundary_preserved": True,
                    "warmup_policy": "fresh-process-target-includes-startup",
                    "setup_seconds": 0., "warmup_seconds": 0., "teardown_seconds": 0.,
                    "cost_boundary": "all startup and exit costs already included in each native CLI target"}
        else:
            if phase == "confirmation" and self.workers:
                self._close_worker_context("replace_persistent_screen_workers")
            teardown = None
            try:
                seconds, details = measure(case, order, confirmation=(phase == "confirmation"))
            finally:
                if phase == "confirmation":
                    teardown = self._close_worker_context("confirmation_period_complete")
            if phase == "confirmation":
                confirmation_context = details.get("confirmation_context", {})
                confirmation_context["teardown"] = teardown
                if confirmation_context:
                    for name in order:
                        confirmation_context["isolation_evidence"][name]["teardown"] = \
                            copy.deepcopy(teardown["workers"].get(name))
        self._check_sources()
        return {"reference_seconds": seconds["reference"], "candidate_seconds": seconds["candidate"],
                "run_id": run_id, "details": {**details, "case_id": case_id, "seed": seed,
                    "order": list(order), "phase": phase, "full_target": True,
                    **({"confirmation_context": confirmation_context} if phase == "confirmation" and measure != self._native_pair else {}),
                    "source_and_build_identity_sha256": native.digest(self.sources)}}

    def close(self):
        if self.lifecycle["closed"]:
            return copy.deepcopy(self.lifecycle)
        errors = []
        try:
            self._close_worker_context("adapter_close")
        except Exception as exc:
            errors.append(str(exc))
        self.lifecycle["closed"] = True
        self.lifecycle["cleanup_errors"] = errors
        if errors:
            raise RuntimeError("receiver worker cleanup failed: " + "; ".join(errors))
        return copy.deepcopy(self.lifecycle)

    def summary(self):
        return copy.deepcopy({**self.lifecycle, "measured_work_identities": self.work_identities,
                              "precision_selections": self.precisions})

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
