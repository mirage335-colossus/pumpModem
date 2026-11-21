"""Invoke the compiled Robust receiver and validate its measurement receipts."""
from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import platform
import subprocess

from triad_cases import ROOT

SCHEMA = "datapump.triad.native-probe.v1"
DEFAULT_PROBE = ROOT / "build/dev/triad_receiver_probe"
BACKENDS = ("cpu", "tensor-cpu", "cuda", "vulkan")
NATIVE_BACKENDS = (*BACKENDS, "gpu")
COMMANDS = frozenset(("describe", "plan", "slot", "continuation", "message", "backlog",
                      "regression", "codec"))
STATUS_CODES = {"complete": 0, "incomplete": 2, "unavailable": 2, "error": 1}
RECEIPT_LIMIT = 16 * 1024 * 1024
# Why a dense OpenCL coarse session handed its search to the FFT session, as
# opencl_coarse_fallback_reasons in src/triad_receiver_bulk.hpp lists them;
# tests/test_triad_abbreviated.py compares the two lists.
OPENCL_COARSE_FALLBACK_REASONS = frozenset((
    "lag-group-route", "mapped-profile", "midpoint-gain", "varying-passband", "fine-cell-limit",
    "description-storage", "device-templates-off", "few-arrivals", "one-ray-unqualified", "dense-cost",
    "keyed-chip-limit", "kernel-extent", "workspace-lease", "host-allocation", "device-allocation",
    "ill-conditioned-template", "unoffered"))
# Fine-stage device families and why a stage stayed native, as
# stage_family_receipt_names and the stage_*_reasons lists in
# src/triad_receiver_bulk.hpp give them; tests/test_triad_abbreviated.py
# compares the lists. A Vulkan receipt names a reason
# vulkan-<family>-<suffix>, with the family's hyphenated name.
VULKAN_STAGE_FAMILIES = ("gain_rank", "drift", "varying")
VULKAN_STAGE_ROUTES = ("automatic", "device", "native")
VULKAN_VARYING_ARITHMETIC = ("automatic", "fp64", "fp32-template")
STAGE_POLICY_REASONS = (
    "native-requested", "fp64-requested", "host-scoring-forced", "shader-fp64-unavailable",
    "unsupported-shape", "unqualified-device", "unimplemented")
STAGE_FALLBACK_REASONS = (
    "workspace", "unified-memory-envelope", "unsupported-transform", "nonfinite-input", "nonfinite-device")
GAIN_RANK_STAGE_REASONS = (
    "verify-mode", "no-direct-rank", "no-unsampled-blocks", "audit-unavailable", "shift-runs", "shift-range",
    "band-violated", "frontier-overflow", "near-tie")
VARYING_STAGE_REASONS = ("diagnostic", "line", "coherent-gain", "projected-tracker")
# The serialized VkFFT plan cache's entries by its byte limit, a receipt's
# host_cache_limit_bytes: coarse_cache_bounds and stage_cache_bounds in
# src/triad_vulkan_core.hpp. Only a backend that can select a fine-stage
# family for the device holds the larger cache.
VULKAN_SERIALIZED_CACHE_ENTRIES = {8 << 20: 16, 64 << 20: 48}


def vulkan_stage_reasons(family):
    """The decline and fallback reasons a Vulkan receipt may name for a family."""
    own = {"gain_rank": GAIN_RANK_STAGE_REASONS, "varying": VARYING_STAGE_REASONS}.get(family, ())
    prefix = "vulkan-" + family.replace("_", "-") + "-"
    return (frozenset(prefix + reason for reason in (*STAGE_POLICY_REASONS, *own)),
            frozenset(prefix + reason for reason in (*STAGE_FALLBACK_REASONS, *own)))


class ProbeError(ValueError):
    """A native command could not provide an applicable measurement receipt."""


def probe_path(executable):
    """Resolve the old exporter option to the native probe next to that exporter."""
    path = Path(executable)
    if path.stem == "triad_helper_export":
        path = path.with_name("triad_receiver_probe" + path.suffix)
    if os.name == "nt" and not path.suffix and not path.is_file():
        path = path.with_suffix(".exe")
    return path.resolve()


def sha256_file(path):
    with Path(path).open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def digest(value):
    data = json.dumps(value, sort_keys=True, ensure_ascii=False, allow_nan=False,
                      separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(data).hexdigest()


def positive_int(value, name, *, allow_zero=False):
    if type(value) is not int or value < (0 if allow_zero else 1):
        raise ProbeError(f"{name} must be a {'nonnegative' if allow_zero else 'positive'} integer")
    return value


def finite_number(value, name, *, nonnegative=False):
    if type(value) not in (int, float) or not math.isfinite(value) or (nonnegative and value < 0):
        raise ProbeError(f"Invalid {name} in native receipt")
    return float(value)


def is_sha256(value):
    return (isinstance(value, str) and len(value) == 64
            and all(character in "0123456789abcdef" for character in value))


def json_loads(data):
    def unique_fields(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ProbeError(f"Duplicate JSON field {key!r}")
            result[key] = value
        return result

    def invalid_constant(value):
        raise ProbeError(f"Nonfinite JSON constant {value}")

    return json.loads(data, object_pairs_hook=unique_fields, parse_constant=invalid_constant)


def read_report(path, *, max_bytes=64 * 1024 * 1024):
    with Path(path).open("rb") as source:
        data = source.read(max_bytes + 1)
    if len(data) > max_bytes:
        raise ProbeError("Measurement report exceeds its size limit")
    return json_loads(data)


def write_result(path, result):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def environment(backend):
    processor = platform.processor()
    try:
        processor = next((line.partition(":")[2].strip()
                          for line in Path("/proc/cpuinfo").read_text().splitlines()
                          if line.startswith("model name")), processor)
    except OSError:
        pass
    affinity = sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None
    return {"host": platform.node(), "system": platform.platform(), "machine": platform.machine(),
            "processor": processor, "cpu_affinity": affinity, "backend": backend}


def runtime_identity(receipt):
    """Build and device attributes that must match a relative comparison."""
    identity = receipt["identity"]
    runtime = {name: identity.get(name) for name in (
        "backend", "compiler", "build_config", "build_options_sha256", "configured_tensor_backend",
        "receiver_arithmetic", "requested_native_coarse_precision",
        "requested_tensor_precision", "requested_tensor_coarse_math",
    )}
    runtime["configured_vulkan_backend"] = identity.get("configured_vulkan_backend", False)
    bulk = identity.get("backend_receipt")
    if bulk is None:
        runtime["bulk"] = None
    elif identity.get("backend") in ("vulkan", "gpu") and bulk.get("vulkan") is True:
        runtime["bulk"] = {name: bulk.get(name) for name in (
            "device", "device_name", "library_version", "vulkan", "vulkan_api_version",
            "vulkan_driver_version", "vulkan_vendor_id", "vulkan_device_id", "vulkan_subgroup_size",
            "vulkan_float16", "vulkan_int8", "vulkan_integer_dot8_accelerated", "unified_memory",
            "numerical_threads", "precision", "workspace_scope", "workspace_limit_bytes", "total_device_bytes",
        )}
        runtime["bulk"]["selection_reason"] = bulk.get("selection_reason")
        if identity.get("backend") == "gpu":
            runtime["selected_accelerator_backend"] = "vulkan"
        # The selected storage can differ by session and is known only after
        # work executes. Bind current reports to their requested policy instead.
        if "requested_vulkan_precision" in identity:
            runtime["requested_vulkan_precision"] = identity["requested_vulkan_precision"]
            del runtime["bulk"]["precision"]
        if "requested_vulkan_fine_projection" in identity:
            runtime["requested_vulkan_fine_projection"] = identity["requested_vulkan_fine_projection"]
        # A stated fine-stage route or arithmetic; automatic receipts stay
        # comparable with those from before the routes existed.
        for key in ("requested_vulkan_gain_rank", "requested_vulkan_drift", "requested_vulkan_varying_fine",
                    "requested_vulkan_varying_arithmetic"):
            if identity.get(key, "automatic") != "automatic":
                runtime[key] = identity[key]
    else:
        runtime["bulk"] = {name: bulk.get(name) for name in (
            "device", "device_name", "library_version", "cuda_runtime_version", "cuda_driver_version",
            "cuda_fft_version",
            "cuda_blas_version", "cuda_compute_major", "cuda_compute_minor",
            "cuda", "numerical_threads", "precision", "requested_coarse_math", "workspace_limit_bytes", "total_device_bytes",
            "fft_plan_cache_limit", "selection_reason",
        )}
        if identity.get("backend") == "gpu":
            runtime["selected_accelerator_backend"] = "cuda" if bulk.get("cuda") is True else "unknown"
    return runtime


def allocation(receipt):
    data = receipt["allocation"]
    hardware = positive_int(data.get("hardware_threads"), "effective hardware threads")
    workers = positive_int(data.get("workers"), "receiver workers")
    owners = positive_int(data.get("slot_owners"), "slot owners")
    memory = positive_int(data.get("memory_limit_bytes"), "memory limit")
    if owners > workers or data.get("receiver_numerical_threads_per_worker") != 1:
        raise ProbeError("Native allocation violates the receiver worker contract")
    return {"hardware_threads": hardware, "workers": workers, "slot_owners": owners,
            "memory_limit_bytes": memory, "receiver_numerical_threads_per_worker": 1}


def named_phases(receipt, name, *, scope=None):
    return [phase for phase in receipt["phases"]
            if phase["name"] == name and (scope is None or phase.get("scope") == scope)]


def phase_once(receipt, name, *, scope=None):
    phases = named_phases(receipt, name, scope=scope)
    if len(phases) != 1:
        raise ProbeError(f"Expected one {name} native phase, got {len(phases)}")
    return phases[0]


def _validate_frequency_admission(value, where):
    if value is None:
        return
    if not isinstance(value, dict):
        raise ProbeError(f"Invalid Vulkan bulk-frequency admission in {where}")
    for key in ("tile_width", "workspace_batch", "device_workspace_bytes", "host_workspace_bytes"):
        if type(value.get(key)) is not int or value[key] < 0:
            raise ProbeError(f"Invalid Vulkan admission {key} in {where}")
    reason = value.get("native_fallback_reason")
    if reason not in ("", "native-vulkan-workspace"):
        raise ProbeError(f"Unrecognized Vulkan admission fallback in {where}")
    if not reason and not all(value[key] > 0 for key in ("tile_width", "workspace_batch", "device_workspace_bytes")):
        raise ProbeError(f"Admitted Vulkan bulk work lacks a positive device allocation in {where}")


def _validate_vulkan_stage_requests(identity, backend, status):
    """A stated fine-stage route names the backend's shape-independent policy
    decline, empty when it can be served; a required device route the
    backend cannot serve is unservable and the work incomplete."""
    for family in VULKAN_STAGE_FAMILIES:
        key = "requested_vulkan_" + ("varying_fine" if family == "varying" else family)
        route = identity.get(key, "automatic")
        requirement = identity.get("vulkan_" + family + "_requirement")
        decline = identity.get("vulkan_" + family + "_route_decline")
        prefix = "vulkan-" + family.replace("_", "-") + "-"
        policy = {prefix + reason for reason in STAGE_POLICY_REASONS}
        selected = isinstance(identity.get("backend_receipt"), dict)
        if (route not in VULKAN_STAGE_ROUTES or (route != "automatic" and backend != "vulkan")
                or requirement not in (None, "required", "not_applicable", "unservable")
                or (requirement is not None and route != "device")
                or (decline is not None and (route == "automatic" or not isinstance(decline, str)
                                             or (decline and decline not in policy)))
                or (route != "automatic" and selected and decline is None)
                or (route == "native" and decline is not None and decline != prefix + "native-requested")
                or (requirement == "unservable" and (not decline or status == "complete"))
                or (requirement in ("required", "not_applicable") and decline)):
            raise ProbeError("Invalid Vulkan fine-stage route request")
    arithmetic = identity.get("requested_vulkan_varying_arithmetic", "automatic")
    if arithmetic not in VULKAN_VARYING_ARITHMETIC or (arithmetic != "automatic" and (
            backend != "vulkan" or identity.get("requested_vulkan_varying_fine") != "device")):
        raise ProbeError("Invalid Vulkan varying arithmetic request")


def _validate_vulkan_stages(bulk, identity):
    """Each fine-stage family of a complete receipt names the reasons of its
    native stages, counts device work only for sessions it admitted, and
    meets a required device route."""
    counters = ("sessions", "forward_ffts", "inverse_ffts", "fft_row_stages", "score_rows",
                "planned_calls", "declined_calls", "native_fallbacks", "upload_bytes")
    for family in VULKAN_STAGE_FAMILIES:
        prefix = "vulkan_" + family + "_"
        requirement = identity.get(prefix + "requirement")
        if prefix + "planned_calls" not in bulk:
            if requirement is not None:
                raise ProbeError("Vulkan receipt lacks the fine-stage work of a required route")
            continue  # a receipt from before the fine-stage routes
        counts = {name: positive_int(bulk.get(prefix + name), f"Vulkan {family} {name}", allow_zero=True)
                  for name in counters}
        declines, fallbacks = vulkan_stage_reasons(family)
        decline = bulk.get(prefix + "decline_reason")
        fallback = bulk.get(prefix + "fallback_reason")
        # Every planned call is a decline, a session or a fallback before
        # any session; a session's own fallbacks add no planned call.
        recorded = counts["planned_calls"] or counts["declined_calls"] or counts["native_fallbacks"]
        if (not isinstance(decline, str) or not isinstance(fallback, str)
                or type(bulk.get(prefix + "qualified_device")) is not bool
                or (decline and decline not in declines) or (fallback and fallback not in fallbacks)
                or bool(decline) != bool(counts["declined_calls"])
                or bool(fallback) != bool(counts["native_fallbacks"])
                or counts["declined_calls"] + counts["sessions"] > counts["planned_calls"]
                or counts["planned_calls"] > counts["declined_calls"] + counts["sessions"] + counts["native_fallbacks"]
                or (not counts["sessions"] and any(counts[name] for name in (
                    "forward_ffts", "inverse_ffts", "fft_row_stages", "score_rows", "upload_bytes")))
                or (requirement == "not_applicable" and recorded)
                or (requirement == "required" and (not recorded or not counts["sessions"] or not counts["score_rows"]
                                                   or counts["declined_calls"] or counts["native_fallbacks"]))
                or requirement == "unservable"):
            raise ProbeError("Vulkan receipt does not identify its fine-stage device work and native stages")


def validate_receipt(receipt, *, command, backend, returncode, expected_require_accelerator=None):
    if not isinstance(receipt, dict) or receipt.get("schema") != SCHEMA:
        raise ProbeError("Expected a current native Robust probe receipt")
    if (receipt.get("command") is None and receipt.get("status") == "error" and returncode == 1):
        raise ProbeError(receipt.get("reason") or "Native probe rejected its command")
    if receipt.get("command") != command:
        raise ProbeError("Native receipt command disagrees with requested work")
    status = receipt.get("status")
    if not isinstance(status, str) or STATUS_CODES.get(status) != returncode:
        raise ProbeError("Native receipt status disagrees with process exit")
    for key in ("identity", "configuration", "allocation", "budget", "result"):
        if not isinstance(receipt.get(key), dict):
            raise ProbeError(f"Missing native receipt object {key}")
    observation = receipt.get("observation")
    if observation is not None and not isinstance(observation, dict):
        raise ProbeError("Invalid native observation receipt")
    identity = receipt["identity"]
    native_precision = identity.get("requested_native_coarse_precision")
    if (native_precision not in ("automatic", "fp32", "fp64", "q15")
            or (backend != "cpu" and native_precision in ("fp32", "q15"))):
        raise ProbeError("Complete native receipt lacks a valid requested native precision policy")
    if identity.get("backend") != backend:
        raise ProbeError("Native receipt backend disagrees with requested backend")
    if identity.get("implementation") != "native-production-receiver":
        raise ProbeError("Measurement did not identify the canonical native receiver")
    _validate_vulkan_stage_requests(identity, backend, status)
    if (backend in ("vulkan", "gpu") and expected_require_accelerator is not None
            and identity.get("require_accelerator") is not expected_require_accelerator):
        raise ProbeError("Native probe did not honor the requested accelerator requirement")
    if not isinstance(receipt.get("phases"), list):
        raise ProbeError("Missing native phase receipts")
    for phase in receipt["phases"]:
        if (not isinstance(phase, dict) or not isinstance(phase.get("name"), str)
                or not phase["name"] or not isinstance(phase.get("work"), dict)):
            raise ProbeError("Invalid native phase receipt")
        finite_number(phase.get("wall_seconds"), "phase wall time", nonnegative=True)
        finite_number(phase.get("represented_seconds"), "represented baseband duration", nonnegative=True)
        if phase.get("process_cpu_seconds") is not None:
            finite_number(phase["process_cpu_seconds"], "phase process CPU time", nonnegative=True)
        work = phase.get("work", {})
        if "bulk_frequency_admission" in work:
            _validate_frequency_admission(work["bulk_frequency_admission"], phase["name"])
        if ("native_fallback_reason" in work and
                work["native_fallback_reason"] not in ("", "native-vulkan-workspace",
                    "native-lag-groups", "compact-tight-clock-fp64")):
            raise ProbeError("Native coarse phase has an unknown fallback reason")
    configuration = receipt.get("configuration") or {}
    full_prior = configuration.get("coarse_full_prior") if isinstance(configuration, dict) else None
    if isinstance(full_prior, dict) and "bulk_frequency_admission" in full_prior:
        _validate_frequency_admission(full_prior["bulk_frequency_admission"], "full-prior plan")
    if status == "complete":
        allocation(receipt)
        if not all(is_sha256(identity.get(key)) for key in ("source_manifest_sha256", "build_options_sha256")):
            raise ProbeError("Complete native receipt lacks build and source identity")
        if not all(isinstance(identity.get(key), str) and identity[key] for key in (
                "compiler", "build_config", "receiver_arithmetic", "requested_tensor_precision",
                "requested_tensor_coarse_math", "requested_native_coarse_precision")):
            raise ProbeError("Complete native receipt lacks compiler or arithmetic identity")
        coarse_math = identity["requested_tensor_coarse_math"]
        if (coarse_math not in ("fft", "fp16", "int8", "fp16-fft")
                or (backend == "cpu" and coarse_math != "fft")
                or (coarse_math == "fp16-fft" and backend != "cuda")
                or (backend == "gpu" and coarse_math != "fft")):
            raise ProbeError("Complete native receipt has unsupported coarse arithmetic")
        if type(identity.get("configured_tensor_backend")) is not bool:
            raise ProbeError("Complete native receipt lacks the build backend configuration")
        if (backend != "vulkan" and "configured_vulkan_backend" in identity
                and type(identity["configured_vulkan_backend"]) is not bool):
            raise ProbeError("Complete native receipt has an invalid Vulkan build configuration")
        if backend == "vulkan" and identity.get("configured_vulkan_backend") is not True:
            raise ProbeError("Complete native receipt lacks the Vulkan build configuration")
        actual_backend = backend
        if backend == "gpu":
            bulk = identity.get("backend_receipt")
            if not isinstance(bulk, dict) or bulk.get("cuda") is bulk.get("vulkan"):
                raise ProbeError("Automatic GPU selection lacks exactly one actual accelerator identity")
            actual_backend = "cuda" if bulk.get("cuda") is True else "vulkan"
            if identity.get("selected_accelerator_backend") != actual_backend:
                raise ProbeError("Automatic GPU selected-backend identity disagrees with executed device")
            if not isinstance(bulk.get("selection_reason"), str) or not bulk["selection_reason"]:
                raise ProbeError("Automatic GPU receipt lacks its selection/fallback reason")
        if actual_backend in ("tensor-cpu", "cuda"):
            bulk = identity.get("backend_receipt")
            if (not isinstance(bulk, dict) or bulk.get("cuda") is not (actual_backend == "cuda") or bulk.get("vulkan") is True
                    or bulk.get("numerical_threads") != 1
                    or not all(isinstance(bulk.get(key), str) and bulk[key] for key in (
                        "device", "device_name", "library_version", "precision"))):
                raise ProbeError("Complete tensor receipt lacks the requested device and library identity")
            if bulk.get("requested_coarse_math") != coarse_math:
                raise ProbeError("Tensor backend coarse arithmetic differs from the requested policy")
            waiters = bulk.get("device_admission_waiters")
            if waiters is not None and (type(waiters) is not int or waiters < 0):
                raise ProbeError("Tensor receipt has an invalid device-admission waiter count")
        if actual_backend == "vulkan":
            bulk = identity.get("backend_receipt")
            if (identity["configured_vulkan_backend"] is not True or not isinstance(bulk, dict)
                    or bulk.get("vulkan") is not True or bulk.get("cuda") is True
                    or bulk.get("numerical_threads") != 1
                    or not all(isinstance(bulk.get(key), str) and bulk[key] for key in (
                        "device", "device_name", "library_version", "precision"))
                    or type(bulk.get("unified_memory")) is not bool):
                raise ProbeError("Vulkan receipt lacks the requested physical-device/runtime identity")
            for key in ("vulkan_api_version", "vulkan_driver_version", "vulkan_vendor_id", "vulkan_device_id"):
                positive_int(bulk.get(key), "Vulkan " + key, allow_zero=key in ("vulkan_vendor_id", "vulkan_device_id"))
            for key in ("vulkan_float16", "vulkan_int8", "vulkan_integer_dot8_accelerated"):
                if type(bulk.get(key)) is not bool:
                    raise ProbeError("Vulkan receipt lacks queried arithmetic features")
            _validate_vulkan_stages(bulk, identity)
            if "requested_vulkan_precision" in identity:
                requested = identity["requested_vulkan_precision"]
                if requested not in ("automatic", "fp32", "fp16-storage",
                                     "direct-fp32", "direct-fp16-storage", "direct-fp16", "nvfp4"):
                    raise ProbeError("Invalid Vulkan storage precision request")
                total = positive_int(bulk.get("score_rows"), "Vulkan scored rows", allow_zero=True)
                host_scored = positive_int(bulk.get("vulkan_host_scored_rows", 0),
                                           "Vulkan host-scored rows", allow_zero=True)
                single = positive_int(bulk.get("vulkan_fp32_score_rows"), "Vulkan FP32 scored rows", allow_zero=True)
                half = positive_int(bulk.get("vulkan_fp16_storage_score_rows"),
                                    "Vulkan FP16-storage scored rows", allow_zero=True)
                # Dense FP4 rows come from the OpenCL Tensor Core search of the
                # same device: every row of an explicit request, or the
                # sessions automatic selection admitted with device templates.
                dense = positive_int(bulk.get("opencl_coarse_score_rows", 0), "OpenCL dense coarse rows",
                                     allow_zero=True)
                automatic_dense = (requested == "automatic" and backend == "gpu"
                                   and identity.get("requested_gpu_fp32_minimum") is not True)
                if host_scored not in (0, total - min(dense, total)):
                    raise ProbeError("Vulkan host-scored rows must cover the complete selected route")
                dense_issued = positive_int(bulk.get("opencl_coarse_issued_products", 0),
                                            "OpenCL dense coarse issued products", allow_zero=True)
                dense_useful = positive_int(bulk.get("opencl_coarse_useful_products", 0),
                                            "OpenCL dense coarse useful products", allow_zero=True)
                dense_rescored = positive_int(bulk.get("opencl_coarse_rescored_peaks", 0),
                                              "OpenCL dense coarse rescored peaks", allow_zero=True)
                dense_synthesized = positive_int(bulk.get("opencl_coarse_synthesized_columns", 0),
                                                 "OpenCL dense coarse synthesized columns", allow_zero=True)
                dense_uploaded = positive_int(bulk.get("opencl_coarse_uploaded_columns", 0),
                                              "OpenCL dense coarse uploaded columns", allow_zero=True)
                if (dense > total or (dense and not (requested == "nvfp4" or automatic_dense))
                        or (dense and automatic_dense and dense_uploaded)
                        or (dense and bulk["vulkan_vendor_id"] != 0x10de)
                        or bool(dense) != bool(dense_useful) or dense_issued < dense_useful
                        or bool(dense) != bool(dense_rescored)
                        or (dense and not (dense_synthesized or dense_uploaded))
                        or (requested == "nvfp4" and total != dense)):
                    raise ProbeError("Vulkan receipt does not identify actual OpenCL dense coarse work")
                if single + half + dense != total or (requested in ("fp32", "direct-fp32") and half):
                    raise ProbeError("Vulkan receipt does not partition actual work by storage precision")
                # Receipts that name the rescoring accumulation partition the
                # rescored peaks between FP32 partial sums and the FP64 control.
                rescore_request = identity.get("requested_opencl_coarse_rescore", "fp64")
                partial_rescore = False
                if "opencl_coarse_rescore_fp32_partial_peaks" in bulk or "opencl_coarse_rescore_fp64_peaks" in bulk:
                    partial = positive_int(bulk.get("opencl_coarse_rescore_fp32_partial_peaks", 0),
                                           "OpenCL FP32 partial rescored peaks", allow_zero=True)
                    control = positive_int(bulk.get("opencl_coarse_rescore_fp64_peaks", 0),
                                           "OpenCL FP64 rescored peaks", allow_zero=True)
                    if (partial + control != dense_rescored
                            or (control if rescore_request == "fp32" else partial)):
                        raise ProbeError("Vulkan receipt does not name the accumulation of its rescored peaks")
                    partial_rescore = bool(partial) and not control
                # FP32 partial sums are the explicit nvfp4 route's and the
                # automatic route's default; other routes keep FP64.
                if rescore_request not in ("fp32", "fp64") or (
                        rescore_request == "fp32" and requested != "nvfp4" and backend != "gpu"):
                    raise ProbeError("Invalid OpenCL coarse rescoring request")
                # One-ray admission is a choice of automatic GPU selection.
                one_ray_request = identity.get("requested_opencl_coarse_one_ray", False)
                if type(one_ray_request) is not bool or (one_ray_request and backend != "gpu"):
                    raise ProbeError("Invalid OpenCL coarse one-ray request")
                # Receipts that name fallback reasons count each fallback once.
                if "opencl_coarse_fallback_counts" in bulk:
                    reasons = bulk["opencl_coarse_fallback_counts"]
                    fallbacks = positive_int(bulk.get("opencl_coarse_frequency_fallbacks", 0),
                                             "OpenCL dense coarse fallbacks", allow_zero=True)
                    if (not isinstance(reasons, dict) or set(reasons) != OPENCL_COARSE_FALLBACK_REASONS
                            or any(type(count) is not int or count < 0 for count in reasons.values())
                            or sum(reasons.values()) != fallbacks):
                        raise ProbeError("Vulkan receipt does not name the reasons of its dense coarse fallbacks")
                direct_single = positive_int(bulk.get("vulkan_direct_fp32_score_rows", 0),
                                             "Vulkan FP32 direct rows", allow_zero=True)
                direct_stored = positive_int(bulk.get("vulkan_direct_fp16_storage_score_rows", 0),
                                             "Vulkan half-storage direct rows", allow_zero=True)
                direct_packed = positive_int(bulk.get("vulkan_direct_fp16_score_rows", 0),
                                             "Vulkan packed FP16 direct rows", allow_zero=True)
                dot_terms = positive_int(bulk.get("vulkan_direct_dot_terms", 0),
                                         "Vulkan direct-dot terms", allow_zero=True)
                direct_total = direct_single + direct_stored + direct_packed
                if (direct_single > single or direct_stored + direct_packed > half
                        or bool(direct_total) != bool(dot_terms) or dot_terms < direct_total
                        or (direct_packed and bulk["vulkan_float16"] is not True)
                        or (requested in ("fp32", "fp16-storage") and direct_total)
                        or (requested == "direct-fp16-storage" and direct_packed)):
                    raise ProbeError("Vulkan receipt does not identify actual direct-dot arithmetic")
                actual = ("nvfp4_dense_coarse_fp32_partial_rescored_peaks_native_fir_and_fine"
                          if dense and dense == total and partial_rescore else
                          "nvfp4_dense_coarse_fp32_rescored_peaks_native_fir_and_fine" if dense and dense == total else
                          "mixed_nvfp4_dense_and_fft_coarse_native_fir_and_fine" if dense else
                          "fp32_direct_dot_fp64_scoring" if direct_single == total else
                          "fp16_storage_fp32_direct_dot_fp64_scoring" if direct_stored == total else
                          "packed_fp16_direct_dot_fp32_reduction_fp64_scoring" if direct_packed == total else
                          "mixed_vulkan_frequency_arithmetic" if direct_total else
                          "mixed_fp16_fp32_fft_storage_fp32_compute" if single and half else
                          "fp16_fft_storage_fp32_fft_and_products" if half else
                          "fp32_fft_fp64_coordinates_and_scores_native_fir_and_fine")
                fine_request = identity.get("requested_vulkan_fine_projection", "automatic")
                batches = positive_int(bulk.get("opencl_nvfp4_fine_batches", 0), "OpenCL NVFP4 batches", allow_zero=True)
                pinned = positive_int(bulk.get("opencl_nvfp4_pinned_batches", 0), "OpenCL NVFP4 pinned batches",
                                      allow_zero=True)
                modes = positive_int(bulk.get("opencl_nvfp4_fine_modes", 0), "OpenCL NVFP4 modes", allow_zero=True)
                native_blocks = positive_int(bulk.get("opencl_nvfp4_native_blocks", 0),
                                             "OpenCL projection native blocks", allow_zero=True)
                resident_batches = positive_int(bulk.get("opencl_resident_fine_batches", 0),
                                                "OpenCL resident batches", allow_zero=True)
                resident_pairs = positive_int(bulk.get("opencl_resident_fine_pairs", 0),
                                               "OpenCL resident pairs", allow_zero=True)
                resident_nvfp4 = positive_int(bulk.get("opencl_resident_nvfp4_modes", 0),
                                               "OpenCL resident NVFP4 modes", allow_zero=True)
                resident_fft = positive_int(bulk.get("opencl_resident_fft_modes", 0),
                                             "OpenCL resident FFT modes", allow_zero=True)
                resident_upload = positive_int(bulk.get("opencl_resident_upload_bytes", 0),
                                                "OpenCL resident upload bytes", allow_zero=True)
                resident_download = positive_int(bulk.get("opencl_resident_download_bytes", 0),
                                                  "OpenCL resident download bytes", allow_zero=True)
                resident_direct = positive_int(bulk.get("opencl_resident_direct_columns", 0),
                                                "OpenCL resident direct columns", allow_zero=True)
                resident_dispatches = positive_int(bulk.get("opencl_resident_dispatches", 0),
                                                    "OpenCL resident dispatches", allow_zero=True)
                resident_map_dispatches = positive_int(bulk.get("opencl_resident_mapping_dispatches", 0),
                                                        "OpenCL resident mapping dispatches", allow_zero=True)
                # Blocking writes, and writes the host does not wait for.
                resident_writes = positive_int(bulk.get("opencl_resident_blocking_writes", 0),
                                                "OpenCL resident blocking writes", allow_zero=True) + positive_int(
                    bulk.get("opencl_resident_async_writes", 0), "OpenCL resident asynchronous writes", allow_zero=True)
                # A batch holds a window of public blocks. Receipts from
                # before windows held one block, or the two of a pair.
                resident_blocks = positive_int(
                    bulk.get("opencl_resident_fine_blocks", resident_batches + resident_pairs),
                    "OpenCL resident blocks", allow_zero=True)
                rendered_tiles = positive_int(bulk.get("opencl_resident_rendered_tiles", 0),
                                              "OpenCL resident rendered tiles", allow_zero=True)
                keyed_lanes = positive_int(bulk.get("opencl_resident_keyed_lanes", 0),
                                           "OpenCL resident keyed lanes", allow_zero=True)
                # The most blocks one batch held, and the windows a refused
                # device lease ran in smaller batches. Absent before windows.
                window_blocks = positive_int(bulk.get("opencl_resident_window_blocks", 0),
                                             "OpenCL resident window blocks", allow_zero=True)
                reduced_windows = positive_int(bulk.get("opencl_resident_reduced_windows", 0),
                                               "OpenCL resident reduced windows", allow_zero=True)
                fine_tiles = identity.get("requested_opencl_fine_tiles", "device")
                # Receipts that name the tile and mapping arithmetic partition
                # mapped columns and rendered tiles between the FP32 kernels
                # and the FP64 control.
                fine_map = identity.get("requested_opencl_fine_map", "fp64")
                if fine_map not in ("fp32", "fp64") or (fine_map == "fp32" and backend != "gpu" and fine_request not in (
                        "resident-nvfp4", "resident-fft")):
                    raise ProbeError("Invalid OpenCL resident fine arithmetic request")
                arithmetic_keys = ("opencl_resident_fp32_mapped_columns", "opencl_resident_fp64_mapped_columns",
                                   "opencl_resident_fp32_rendered_tiles", "opencl_resident_fp64_rendered_tiles")
                if any(key in bulk for key in arithmetic_keys):
                    mapped32, mapped64, rendered32, rendered64 = (
                        positive_int(bulk.get(key, 0), key.replace("_", " "), allow_zero=True)
                        for key in arithmetic_keys)
                    mapped_total = positive_int(bulk.get("mapped_columns", 0), "mapped columns", allow_zero=True)
                    if (mapped32 + mapped64 != mapped_total or rendered32 + rendered64 != rendered_tiles
                            or (mapped64 + rendered64 if fine_map == "fp32" else mapped32 + rendered32)):
                        raise ProbeError("Vulkan receipt does not name its resident fine arithmetic")
                fine_window = identity.get("requested_opencl_fine_window", 0)
                fine_stream = identity.get("requested_opencl_fine_stream", 4)
                # Only a stated "on" times the resident fine session.
                fine_profile = identity.get("requested_opencl_profile")
                resident_seconds = [bulk.get(key, 0) for key in bulk if key.startswith("opencl_resident_")
                                    and key.endswith("_seconds")]
                measured = command not in ("describe", "plan", "codec")
                if (fine_request not in ("automatic", "native", "nvfp4", "resident-nvfp4", "resident-fft")
                        or bool(batches) != bool(modes)
                        or modes < batches or pinned > batches
                        or (modes and (bulk["vulkan_vendor_id"] != 0x10de or fine_request == "native"))
                        or (measured and fine_request == "nvfp4" and not modes)):
                    raise ProbeError("Vulkan receipt does not identify actual OpenCL NVFP4 work")
                resident_modes = resident_nvfp4 + resident_fft
                automatic_resident = fine_request == "automatic" and backend == "gpu"
                fp4_resident_only = resident_nvfp4 or fine_request == "resident-nvfp4"
                if (fine_tiles not in ("device", "native") or type(fine_window) is not int
                        or not (fine_window == 0 or (2 <= fine_window <= 64 and fine_window % 2 == 0))
                        or type(fine_stream) is not int or not 0 <= fine_stream <= 64
                        or fine_profile not in (None, "on", "off", "default")
                        or any(isinstance(value, bool) or not isinstance(value, (int, float))
                               or not math.isfinite(value) or value < 0 for value in resident_seconds)
                        or (fine_profile in ("off", "default") and any(resident_seconds))
                        or ("opencl_resident_window_blocks" in bulk
                            and (bool(window_blocks) != bool(resident_batches) or window_blocks > 64
                                 or (fine_window and window_blocks > fine_window)
                                 or resident_blocks > window_blocks * resident_batches))
                        or reduced_windows > resident_batches
                        or resident_blocks < resident_batches + resident_pairs
                        or resident_blocks > 64 * resident_batches
                        or (fine_window and resident_blocks > fine_window * resident_batches)
                        or bool(rendered_tiles) != bool(keyed_lanes) or keyed_lanes > rendered_tiles
                        or (rendered_tiles and (not resident_batches or fine_tiles == "native"))):
                    raise ProbeError("Vulkan receipt does not identify its resident OpenCL fine batches and tiles")
                if (resident_pairs > resident_batches
                        or (fp4_resident_only and (resident_nvfp4 != modes or resident_batches != batches or pinned))
                        or (resident_nvfp4 and not (automatic_resident or fine_request == "resident-nvfp4"))
                        or (resident_fft and (bulk["vulkan_vendor_id"] != 0x10de or fine_request != "resident-fft"))
                        or (fine_request == "resident-fft" and modes)
                        or bool(resident_batches) != bool(resident_modes)
                        or bool(resident_batches) != bool(resident_upload)
                        or bool(resident_batches) != bool(resident_download)
                        or bool(resident_batches) != bool(resident_dispatches)
                        or bool(resident_batches) != bool(resident_writes)
                        or resident_map_dispatches > resident_dispatches
                        or (resident_direct and not resident_batches)
                        or (measured and fine_request == "resident-nvfp4" and not resident_nvfp4)
                        or (measured and fine_request == "resident-fft" and not resident_fft)):
                    raise ProbeError("Vulkan receipt does not identify actual resident OpenCL fine work")
                if resident_modes:
                    # Four DATA template lanes and 47 retained modes per state.
                    # Readback carries 11 complex double values per state.
                    states, partial = divmod(resident_modes, 4 * 47)
                    blocks = resident_blocks
                    if (partial or not blocks <= states <= 72 * blocks
                            or resident_direct > 4 * states or resident_download != 11 * 16 * states
                            or resident_dispatches < resident_batches * (4 if resident_nvfp4 else 3)
                            or resident_writes < resident_batches
                            or states > positive_int(bulk.get("statistics_blocks"), "OpenCL statistics blocks")
                            or resident_modes > positive_int(bulk.get("projected_values"), "OpenCL projected values")
                            or resident_upload > positive_int(bulk.get("upload_bytes"), "backend uploaded bytes")
                            or resident_download > positive_int(bulk.get("download_bytes"), "backend downloaded bytes")):
                        raise ProbeError("Vulkan receipt does not cover the resident OpenCL fine batch")
                if modes or resident_fft:
                    actual = actual.removesuffix("_and_fine")
                    if resident_nvfp4:
                        actual += "_opencl_resident_nvfp4_fine"
                    if resident_fft:
                        actual += "_opencl_resident_fft_fine"
                    if modes and not resident_nvfp4:
                        actual += "_opencl_nvfp4_fine_projection"
                    if native_blocks or bulk.get("native_fine_sessions", 0):
                        actual += "_mixed_native_fine"
                if total and bulk["precision"] != actual and not (
                        host_scored == total and
                        bulk["precision"] == actual + "_fixed_phase_host_fp64_scoring"):
                    raise ProbeError("Vulkan reported storage precision disagrees with scored rows")
        if command not in ("describe", "plan") and not receipt["phases"]:
            raise ProbeError("Complete native workload contains no measured phases")
        if command not in ("describe", "plan", "codec") and (
                observation is None or not is_sha256(observation.get("input_sha256"))):
            raise ProbeError("Complete native workload lacks the exact observed input identity")
        if actual_backend == "cuda" and command not in ("describe", "plan"):
            accelerator = identity.get("backend_receipt") or {}
            diagnostic = receipt["result"].get("coordinate_scope") == "declared_public_work_coordinate"
            mapped = accelerator.get("mapped_values")
            if backend == "gpu":
                native_fine_sessions = accelerator.get("native_fine_sessions", 0)
                native_fine_reason = accelerator.get("native_fine_fallback_reason", "")
                terminal_gpu_fine = (receipt["result"].get("stratum") == "terminal"
                                     and type(mapped) is int and mapped == 0)
                actual_gpu_fine = (type(accelerator.get("statistics_blocks")) is int
                                   and accelerator["statistics_blocks"] > 0
                                   and (type(mapped) is int and mapped > 0 or terminal_gpu_fine))
                recognized_native_fine = (type(native_fine_sessions) is int and native_fine_sessions > 0
                                          and native_fine_reason == "small-chronological-blocks")
                operations = ("fft_calls", "score_rows")
            elif diagnostic:
                terminal_zero = (receipt["result"].get("stratum") == "terminal"
                                 and type(mapped) is int and mapped == 0)
                operations = ("statistics_blocks",) if terminal_zero else ("mapped_values", "statistics_blocks")
            else:
                operations = ("fft_calls", "score_rows", "mapped_values", "statistics_blocks")
            projected = any(type(accelerator.get(key)) is int and accelerator[key] > 0
                            for key in ("fft_calls", "dc_reduction_values"))
            valid_operations = all(type(accelerator.get(key)) is int and accelerator[key] > 0
                                   for key in operations)
            if (accelerator.get("cuda") is not True or accelerator.get("executed") is not True
                    or not projected or not valid_operations
                    or (backend == "gpu" and not (actual_gpu_fine or recognized_native_fine))):
                raise ProbeError("Complete CUDA receipt does not prove the required coarse and fine accelerator work")
            if backend == "cuda" and coarse_math == "fp16-fft":
                score_rows = positive_int(accelerator.get("score_rows"), "CUDA scored rows")
                half_score_rows = positive_int(accelerator.get("cuda_half_fft_score_rows"),
                                               "CUDA half-FFT scored rows", allow_zero=True)
                forward_ffts = positive_int(accelerator.get("cuda_half_fft_forward_ffts"),
                                            "CUDA half-FFT forward transforms", allow_zero=True)
                inverse_ffts = positive_int(accelerator.get("cuda_half_fft_inverse_ffts"),
                                            "CUDA half-FFT inverse transforms", allow_zero=True)
                positive_int(accelerator.get("cuda_fft_version"), "CUDA cuFFT version")
                positive_int(accelerator.get("cuda_half_fft_padded_ffts"),
                             "CUDA half-FFT padded transforms", allow_zero=True)
                for key, label in (("cuda_half_fft_peak_bank_bytes", "CUDA half-FFT bank peak"),
                                   ("cuda_half_fft_peak_workspace_bytes", "CUDA half-FFT workspace peak"),
                                   ("cuda_half_fft_peak_phase_bytes", "CUDA half-FFT phase peak")):
                    positive_int(accelerator.get(key), label, allow_zero=True)
                plan_seconds = accelerator.get("cuda_half_fft_plan_seconds")
                fallbacks = accelerator.get("half_fft_fallback_counts")
                expected_fallbacks = {"cpu-device", "fp64-stage", "lag-group-route",
                                      "unsupported-plan", "workspace-limit", "fp32-minimum",
                                      "unqualified-hardware", "unqualified-shape",
                                      "ill-conditioned-template", "plan-allocation", "allocator-limit"}
                if (type(score_rows) is not int or score_rows <= 0
                        or half_score_rows != score_rows or forward_ffts <= 0 or inverse_ffts <= 0
                        or not isinstance(plan_seconds, (int, float)) or not math.isfinite(plan_seconds)
                        or plan_seconds < 0
                        or not isinstance(fallbacks, dict) or set(fallbacks) != expected_fallbacks
                        or any(type(count) is not int or count < 0 for count in fallbacks.values())):
                    raise ProbeError("Explicit CUDA fp16-fft receipt lacks complete actual half-transform scores")
                reason_observed = any(
                    isinstance(phase.get("work"), dict)
                    and phase["work"].get("precision_reason") == "cuda-fp16-frequency-fft"
                    and phase["work"].get("fft_precision") == "fp16_fft_fp64_coordinates_and_reductions"
                    and isinstance(phase["work"].get("bulk"), dict)
                    and phase["work"]["bulk"].get("precision") ==
                        "fp16_fft_fp64_coordinates_and_reductions"
                    for phase in receipt["phases"] if phase.get("name") in ("coarse_prepare", "coarse_tiles"))
                if not reason_observed:
                    raise ProbeError("Explicit CUDA fp16-fft receipt lacks its selected coarse precision reason")
        require_accelerator = expected_require_accelerator
        if require_accelerator is None:
            require_accelerator = identity.get("require_accelerator") is True
        if actual_backend == "vulkan" and command not in ("describe", "plan") and require_accelerator:
            accelerator = identity.get("backend_receipt") or {}
            workspace_limit = accelerator.get("workspace_limit_bytes")
            if type(workspace_limit) is not int or workspace_limit <= 0:
                raise ProbeError("Vulkan receipt lacks a positive workspace limit")
            owner_peak = accelerator.get("vulkan_peak_owner_bytes")
            active_sessions = accelerator.get("vulkan_active_sessions")
            aggregate_peak = accelerator.get("allocator_peak_reserved_bytes")
            positive_int(accelerator.get("native_coarse_workspace_sessions", 0),
                         "native coarse workspace fallback sessions", allow_zero=True)
            cache_limit = accelerator.get("host_cache_limit_bytes")
            cache_bytes = accelerator.get("host_cache_bytes")
            cache_entries = accelerator.get("host_cache_entries")
            expected_scope = "backend_shared" if backend == "gpu" else "per_coarse_session"
            # Dense OpenCL sessions hold a quoted device lease and allocate
            # nothing from the Vulkan allocator.
            dense_rows = positive_int(accelerator.get("opencl_coarse_score_rows", 0),
                                      "OpenCL dense coarse rows", allow_zero=True)
            fft_rows = positive_int(accelerator.get("score_rows"), "Vulkan score rows", allow_zero=True) - dense_rows
            owner_lease = accelerator.get("vulkan_peak_owner_lease_bytes", 0)
            if (fft_rows < 0 or (dense_rows and (type(owner_lease) is not int
                                                 or not 0 < owner_lease <= workspace_limit))):
                raise ProbeError("Vulkan receipt does not bound its OpenCL dense coarse sessions")
            if (accelerator.get("workspace_scope") != expected_scope
                    or type(owner_peak) is not int or not (0 if fft_rows == 0 else 1) <= owner_peak <= workspace_limit
                    or type(active_sessions) is not int or active_sessions < 0
                    or type(aggregate_peak) is not int or aggregate_peak < owner_peak
                    or type(cache_limit) is not int or cache_limit not in VULKAN_SERIALIZED_CACHE_ENTRIES
                    or type(cache_bytes) is not int or not 0 <= cache_bytes <= cache_limit
                    or type(cache_entries) is not int
                    or not 0 <= cache_entries <= VULKAN_SERIALIZED_CACHE_ENTRIES[cache_limit]):
                raise ProbeError("Vulkan receipt does not prove bounded device and serialized FFT cache allocation")
            if backend == "gpu":
                shared_limit = positive_int(accelerator.get("vulkan_shared_workspace_limit_bytes"),
                                            "Vulkan shared workspace limit")
                leased_bytes = positive_int(accelerator.get("vulkan_leased_bytes"),
                                            "Vulkan currently leased bytes", allow_zero=True)
                peak_leased = positive_int(accelerator.get("vulkan_peak_leased_bytes"), "Vulkan peak leased bytes")
                peak_owner_lease = positive_int(accelerator.get("vulkan_peak_owner_lease_bytes"),
                                                "Vulkan peak owner lease")
                reserved_bytes = positive_int(accelerator.get("reserved_bytes"),
                                              "Vulkan currently reserved bytes", allow_zero=True)
                positive_int(accelerator.get("vulkan_waiting_sessions"), "Vulkan waiting sessions", allow_zero=True)
                if (workspace_limit != shared_limit
                        or not owner_peak <= peak_owner_lease <= peak_leased <= shared_limit
                        or not reserved_bytes <= leased_bytes <= peak_leased
                        or aggregate_peak > peak_leased):
                    raise ProbeError("Automatic Vulkan receipt exceeds its shared workspace or owner leases")
            frequency_route = any(isinstance(phase.get("work"), dict)
                                  and phase["work"].get("native_fallback_reason", "") == ""
                                  and phase["work"].get("route") == "frequency_tiles"
                                  for phase in receipt["phases"])
            stream = receipt.get("result", {}).get("stream")
            stream_phase = next((phase for phase in receipt["phases"]
                                 if phase.get("name") == "receiver_service"
                                 and isinstance(phase.get("work"), dict)), None)
            stream_work = stream_phase.get("work", {}) if stream_phase else {}
            stream_route = (command in ("message", "backlog")
                            and receipt["result"].get("complete_receiver_service") is True
                            and isinstance(stream, dict) and stream.get("state") == "input_ended"
                            and stream.get("input_closed") is True and stream.get("work_complete") is True
                            and type(stream.get("committed_slots")) is int and stream["committed_slots"] > 0
                            and type(stream.get("input_rows")) is int and stream["input_rows"] > 0
                            and is_sha256(stream.get("verified_input_sha256"))
                            and stream_work.get("committed_slots") == stream["committed_slots"]
                            and stream_work.get("input_rows") == stream["input_rows"]
                            and stream_work.get("verified_input_sha256") == stream["verified_input_sha256"])
            regression_units = receipt["result"].get("units")
            regression_phases = [phase for phase in receipt["phases"] if phase.get("name") == "native_slot"]
            admission_route = (command == "admission" and len(regression_phases) == 1
                               and receipt["result"].get("complete_public_slot") is True
                               and receipt["result"].get("one_native_score_for_all_policies") is True
                               and regression_phases[0]["work"].get("complete") is True
                               and regression_phases[0]["work"].get("coarse_fft_precision") == "bulk"
                               and all(type(regression_phases[0]["work"].get(key)) is int
                                       and regression_phases[0]["work"][key] > 0 for key in (
                                           "coarse_workspaces", "coarse_frequency_points", "coarse_arrival_points")))
            regression_route = (command == "regression"
                                and receipt["result"].get("work_complete") is True
                                and receipt["result"].get("evaluator_expected_decision_observed") is True
                                and type(regression_units) is int and regression_units > 0
                                and len(regression_phases) == regression_units
                                and all(phase["work"].get("complete") is True
                                        and phase["work"].get("all_valid") is True
                                        and phase["work"].get("decision") in ("data", "quiet")
                                        and type(phase["work"].get("coarse_workspaces")) is int
                                        and phase["work"]["coarse_workspaces"] > 0
                                        and type(phase["work"].get("coarse_frequency_points")) is int
                                        and phase["work"]["coarse_frequency_points"] > 0
                                        and type(phase["work"].get("coarse_arrival_points")) is int
                                        and phase["work"]["coarse_arrival_points"] > 0
                                        and is_sha256(phase["work"].get("work_identity_sha256"))
                                        for phase in regression_phases))
            if command in ("message", "backlog"):
                route_proof = stream_route
            elif command == "regression":
                route_proof = regression_route
            elif command == "admission":
                route_proof = admission_route
            else:
                route_proof = frequency_route
            host_scored = positive_int(accelerator.get("vulkan_host_scored_rows", 0),
                                       "Vulkan host scored rows", allow_zero=True)
            score_rows = positive_int(accelerator.get("score_rows"), "Vulkan score rows", allow_zero=True)
            frontier_rows = positive_int(accelerator.get("vulkan_arrival_peak_rows"),
                                         "Vulkan arrival peak rows", allow_zero=True)
            frontier_values = positive_int(accelerator.get("vulkan_arrival_peak_values"),
                                           "Vulkan arrival peak values", allow_zero=True)
            if (host_scored == fft_rows and host_scored > 0
                    and (frontier_rows or frontier_values)):
                raise ProbeError("Host-scored Vulkan rows cannot claim a GPU arrival frontier")
            if host_scored not in (0, fft_rows):
                raise ProbeError("Vulkan host-scored rows must cover the complete selected route")
            frontier_proof = (host_scored == fft_rows and host_scored > 0
                              or host_scored == 0 and frontier_rows > 0 and frontier_values > 0)
            work_counts = ("fft_calls", "vulkan_direct_dot_terms")
            fft_proof = (frontier_proof and type(accelerator.get("vulkan_dispatches")) is int
                         and accelerator["vulkan_dispatches"] > 0
                         and any(type(accelerator.get(key)) is int and accelerator[key] > 0
                                 for key in work_counts))
            dense_proof = all(type(accelerator.get(key)) is int and accelerator[key] > 0 for key in (
                "opencl_coarse_dispatches", "opencl_coarse_rescored_peaks", "opencl_coarse_sessions"))
            if (accelerator.get("vulkan") is not True or accelerator.get("executed") is not True
                    or score_rows <= 0 or (fft_rows and not fft_proof) or (dense_rows and not dense_proof)
                    or not route_proof):
                raise ProbeError("Complete Vulkan receipt does not prove GPU full-frequency scoring")
    return receipt


class NativeProbe:
    """One identified native executable. Each measurement has explicit limits."""

    def __init__(self, executable=DEFAULT_PROBE, *, backend="cpu"):
        if backend not in NATIVE_BACKENDS:
            raise ProbeError("Unsupported native backend")
        self.path = probe_path(executable)
        self.backend = backend
        try:
            with self.path.open("rb") as binary:
                magic = binary.read(4)
            # ELF, Windows PE, or native/universal Mach-O. Refuse an accidentally
            # selected script even if it imitates the JSON protocol.
            if not (magic == b"\x7fELF" or magic.startswith(b"MZ") or magic in (
                    b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe",
                    b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",
                    b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
                    b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca")):
                raise ProbeError(f"Expected a compiled native probe at {self.path}")
            self.executable_sha256 = sha256_file(self.path)
            self._file_state = self._stat()
        except OSError as error:
            raise ProbeError(f"Cannot open native receiver probe {self.path}: {error}") from error

    def _stat(self):
        state = self.path.stat()
        return state.st_dev, state.st_ino, state.st_size, state.st_mtime_ns

    def verify_unchanged(self):
        if self._stat() != self._file_state or sha256_file(self.path) != self.executable_sha256:
            raise ProbeError("Native executable changed during measurement")

    def call(self, command, native_args=(), *, deadline_ms=30_000, max_samples=4_194_304,
             workers=None, slot_owners=None, memory_limit_bytes=None, **options):
        if command not in COMMANDS:
            raise ProbeError(f"Unknown native probe command {command!r}")
        positive_int(deadline_ms, "deadline_ms")
        positive_int(max_samples, "max_samples")
        if deadline_ms > 3_600_000:
            raise ProbeError("Native probe deadline exceeds one hour")
        argv = [str(self.path), command, "--backend", self.backend,
                "--require-accelerator", "on" if self.backend in ("cuda", "vulkan", "gpu") else "off",
                "--deadline-ms", str(deadline_ms), "--max-samples", str(max_samples),
                *native_args]
        limits = {"workers": workers, "slot_owners": slot_owners,
                  "memory_limit_bytes": memory_limit_bytes}
        for name, value in limits.items():
            if value is not None:
                positive_int(value, name)
        for name, value in (limits | options).items():
            if value is not None:
                argv.extend(("--" + name.replace("_", "-"),
                             ("on" if value else "off") if type(value) is bool else str(value)))
        try:
            if self._stat() != self._file_state:
                raise ProbeError("Native executable changed during measurement")
            finished = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                      timeout=deadline_ms / 1000 + 15, check=False)
            if self._stat() != self._file_state:
                raise ProbeError("Native executable changed during measurement")
        except subprocess.TimeoutExpired as error:
            raise ProbeError(f"Native {command} exceeded its bounded process deadline") from error
        except OSError as error:
            raise ProbeError(f"Cannot launch native {command}: {error}") from error
        if len(finished.stdout) > RECEIPT_LIMIT:
            raise ProbeError("Native probe receipt exceeds its size limit")
        try:
            raw = json_loads(finished.stdout)
        except (UnicodeError, json.JSONDecodeError) as error:
            detail = finished.stderr[-1024:].decode("utf-8", errors="replace").strip()
            raise ProbeError(f"Native {command} returned no receipt (exit {finished.returncode})"
                             + (f": {detail}" if detail else "")) from error
        receipt = validate_receipt(raw, command=command, backend=self.backend, returncode=finished.returncode,
                                   expected_require_accelerator=self.backend in ("vulkan", "gpu"))
        if receipt["status"] == "complete":
            if receipt["identity"]["requested_tensor_coarse_math"] != (options.get("tensor_coarse_math") or "fft"):
                raise ProbeError("Native probe altered the requested coarse arithmetic policy")
            if (receipt["identity"]["requested_native_coarse_precision"] !=
                    (options.get("native_coarse_precision") or "automatic")):
                raise ProbeError("Native probe altered the requested native coarse precision policy")
            if self.backend == "vulkan":
                requested = argv[argv.index("--vulkan-precision") + 1] if "--vulkan-precision" in argv else "automatic"
                if receipt["identity"].get("requested_vulkan_precision", "automatic") != requested:
                    raise ProbeError("Native probe altered the requested Vulkan storage precision policy")
            actual = allocation(receipt)
            for key, value in limits.items():
                if value is not None and actual[key] != value:
                    raise ProbeError(f"Native probe altered the explicit {key} allocation")
            if workers is None and actual["workers"] != 2 * actual["hardware_threads"]:
                raise ProbeError("Automatic native receiver allocation must be twice effective hardware threads")
        receipt["identity"]["executable_sha256"] = self.executable_sha256
        return receipt
