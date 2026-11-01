#!/usr/bin/env python3
"""Batch actual independent triad paths through the unchanged phase likelihood.

Only completed actual sampled workspaces are accepted. CPU FP64 preparation
uses the complete public bank for independent stationary residual calibration;
GPU batches contain independent paths, never synthetic channel replications.
"""
from __future__ import annotations

import argparse
from copy import copy
import importlib.util
import json
import math
from pathlib import Path
import platform
import sys
import time

import numpy as np
import torch

_SERIAL_SPEC = importlib.util.spec_from_file_location(
    "triad_serial_noise_study", Path(__file__).with_name("triad-noise-study.py"))
serial = importlib.util.module_from_spec(_SERIAL_SPEC)
assert _SERIAL_SPEC.loader is not None
_SERIAL_SPEC.loader.exec_module(serial)

METHODS = ("phase-frequency", "phase-frequency-ucb")


def _parse_args(argv=None):
    extra = argparse.ArgumentParser(add_help=False)
    extra.add_argument("--batch-paths", type=int, default=16,
                       help="maximum resident independent physical paths per shard")
    extra.add_argument("--transfer-mode", choices=("block", "resident"), default="block",
                       help="stage each block, or cache the selected shard on the device once")
    batch, rest = extra.parse_known_args(sys.argv[1:] if argv is None else argv)
    if batch.batch_paths < 1:
        extra.error("batch-paths must be positive")
    defaults = ["--mode", "actual", "--calibration-scope", "stationary",
                "--selection-methods", "7", "--calibration-alternatives", "2",
                "--duration-alternatives", "3"]
    suffix = [] if "--methods" in rest else ["--methods", *METHODS]
    args = serial._parse_args(defaults + rest + suffix)
    args.batch_paths = batch.batch_paths
    args.transfer_mode = batch.transfer_mode
    if args.mode != "actual" or args.calibration_scope != "stationary":
        extra.error("this adapter requires actual observations and stationary calibration")
    if any(method not in METHODS for method in args.methods):
        extra.error("only phase-frequency and phase-frequency-ucb are supported")
    return args


def _reference(args, workspace):
    value = args.reference_variance or workspace.sample_rate / 10.**(args.cn0/10.)
    if not math.isfinite(value) or value <= 0:
        raise ValueError("reference variance must be positive and finite")
    return value


def _source_hashes(workspace):
    # No noiseless means, noiseless energy or simulator variance is read here.
    names = ("manifest.json", "gram.c128", "observation.c128", "received_energy.f64",
             "intervals.u64", "valid_rows.u64")
    return {name: serial._sha256(workspace.directory/name) for name in names}


def _release_maps(workspace):
    # All retained DSP arrays are owned copies. Release consumed file mappings
    # as well as arrays so resident mapped pages cannot accumulate across shards.
    for value in workspace._maps.values():
        value._mmap.close()
    workspace._maps.clear()


def _error_record(workspace, index, error):
    try:
        hashes = _source_hashes(workspace)
    except OSError:
        hashes = {"manifest.json": serial._sha256(workspace.manifest_path)}
    declared = workspace.manifest.get("control")
    control = declared if declared in ("signal", "null", "wrong-key") else (
        "signal" if workspace.manifest.get("signal_enabled") is True else "null")
    truth = workspace.manifest.get("signal_bit_simulation_truth", workspace.manifest.get("signal_bit"))
    return {"status":"error", "physical_path_index":index,
            "input":str(workspace.directory), "input_hashes":hashes,
            "control":control, "truth_bit_simulation_only":truth if control == "signal" else None,
            "conditional_noise_trials":1, "attempted_captures":1, "completed_captures":0,
            "error_type":type(error).__name__, "error":str(error), "methods":{}}


def _validate_profiles(workspaces):
    paths = [str(w.directory.resolve()) for w in workspaces]
    if len(set(paths)) != len(paths):
        raise serial.WorkspaceError("duplicate physical workspace")
    identities = []
    first = workspaces[0]
    keys = ("sample_rate", "sample_count", "block_samples", "block_count", "dimension",
            "column_mapping", "candidates", "column_descriptors")
    signature = {key: first.manifest.get(key) for key in keys}
    try:
        # Own this small calendar before releasing its file mapping. Profile
        # validation must not accumulate one open descriptor per physical path.
        intervals = np.array(first._map("intervals.u64", first._U64_DTYPE,
                                       (first.block_count, 2)), copy=True)
        _release_maps(first)
        for w in workspaces:
            try:
                if not w.has_observation or not w.has_received_energy:
                    raise serial.WorkspaceError("actual projections and actual received energy are required")
                if {key: w.manifest.get(key) for key in keys} != signature:
                    raise serial.WorkspaceError("physical paths do not share a public bank/calendar")
                other = w._map("intervals.u64", w._U64_DTYPE, (w.block_count, 2))
                if not np.array_equal(other, intervals):
                    raise serial.WorkspaceError("physical paths have different block calendars")
                fields = ("channel_seed", "key_seed", "epoch")
                if all(field in w.manifest for field in fields):
                    identity = tuple(w.manifest[field] for field in fields)
                    if identity in identities:
                        raise serial.WorkspaceError("duplicate physical channel/key/epoch identity")
                    identities.append(identity)
            finally:
                _release_maps(w)
    finally:
        for w in workspaces:
            _release_maps(w)


def _resident_bytes(workspace, selected_count, taps, paths):
    # Original and rank-projected r/G, plus actual E, residual E/DOF, dt/rows.
    return paths * workspace.block_count * (
        2*selected_count*(taps+taps*taps)*16 + 6*8)


def _memory_guard(args, workspace, mapping, paths):
    # Reuse the serial model builder to avoid changing any state/prior geometry.
    probe = copy(args)
    probe.device, probe.phase_implementation = "cpu", "reference"
    probe.methods = ["phase-frequency"]
    state = serial._new_states(probe, workspace, mapping, 1, False)["phase-frequency"][0]
    per_path_bound = state.working_bytes_bound
    graph_entries_bound = state.model.transition_cache_entries
    complex_bytes = 16 if args.precision == "fp64" else 8
    persistent_graph_bytes = paths*(state.weights.numel()*state.weights.element_size()+
        len(mapping)*(args.taps+args.taps*args.taps)*complex_bytes)
    del state
    graph = args.phase_implementation in ("graph", "graph-fp64")
    # Each bounded private graph cache entry can retain one complete working
    # bound, plus a live/capture bound. Use at least six working bounds, and
    # separately charge persistent r/G and initial-prior storage. With the
    # current four-entry cache this exceeds the backend's (entries+1) bound.
    graph_factor = max(6, graph_entries_bound+2) if graph else 1
    hmm_bound = per_path_bound*paths*len(args.methods)*graph_factor
    if graph:
        hmm_bound += persistent_graph_bytes*len(args.methods)
    resident = _resident_bytes(workspace, len(mapping), args.taps, paths)
    # Charge resident storage three times for temporary preparation, CPU/GPU
    # packing and framework copies; add one full public Gram validation tile.
    scratch = min(args.tile_blocks, workspace.block_count)*workspace.dimension**2*16*4 + 2**20
    cache = packing = transfer_scratch = 0
    if args.transfer_mode == "resident":
        elements = paths*workspace.block_count*len(mapping)*(args.taps+args.taps*args.taps)
        cache = elements*complex_bytes*len(args.methods)
        # Pack one method at a time in FP64, including a conservative second
        # host copy and a full FP64 device-conversion staging allowance.
        packing = elements*16*2
        transfer_scratch = elements*16
    bound = 3*resident+hmm_bound+scratch+cache+packing+transfer_scratch
    if bound > args.memory_limit_bytes:
        raise MemoryError(f"batch memory estimate {bound} exceeds limit {args.memory_limit_bytes}; reduce --batch-paths")
    return {"resident_cpu_arrays_bytes": resident,
            "phase_state_working_bound_bytes": hmm_bound,
            "graph_working_factor": graph_factor,
            "graph_cache_entries_bound": graph_entries_bound if graph else 0,
            "persistent_graph_input_state_bytes": persistent_graph_bytes*len(args.methods) if graph else 0,
            "resident_selected_device_cache_bytes": cache,
            "resident_cpu_packing_peak_bytes": packing,
            "resident_device_transfer_scratch_bytes": transfer_scratch,
            "conservative_total_bytes": bound,
            "limit_bytes": args.memory_limit_bytes,
            "policy": "3x resident arrays + aggregate phase working bounds (graph: >=6x plus persistent inputs/prior) + public-Gram scratch + explicit resident transfer/cache allowances; runtime allocator cache excluded"}


def _prepare_path(args, workspace, index, mapping, gate):
    start = time.perf_counter()
    b, c, k = workspace.block_count, *mapping.shape
    ref = _reference(args, workspace)
    r = np.empty((b,c,k), dtype=np.complex128)
    G = np.empty((b,c,k,k), dtype=np.complex128)
    projected_r, projected_G = np.empty_like(r), np.empty_like(G)
    energy = np.empty(b, dtype=np.float64)
    residual_energy = np.empty(b, dtype=np.float64)
    dof = np.empty(b, dtype=np.float64)
    dt = np.empty(b, dtype=np.float64)
    rows = np.empty(b, dtype=np.int64)
    total = correction = 0.
    for block in workspace.iter_received_blocks(reference_variance=ref, tile_blocks=args.tile_blocks):
        j = block.index
        rr = torch.from_numpy(np.asarray(block.r))
        gg = torch.from_numpy(np.asarray(block.gram))
        result = serial.whole_bank_residual(rr, gg, block.received_energy, block.valid_rows)
        r[j] = block.r[0, mapping]
        G[j] = block.gram[mapping[:,:,None], mapping[:,None,:]]
        pr, pg = result.projected_correlations.numpy(), result.projected_gram.numpy()
        projected_r[j] = pr[0, mapping]
        projected_G[j] = pg[mapping[:,:,None], mapping[:,None,:]]
        energy[j], residual_energy[j] = block.received_energy, float(result.energy[0])
        dof[j], dt[j], rows[j] = float(result.complex_dof), block.dt_seconds, block.valid_rows
        adjusted = residual_energy[j]-correction
        updated = total+adjusted
        correction = (updated-total)-adjusted
        total = updated
    total_dof = float(dof.sum())
    upper = None
    if "phase-frequency-ucb" in args.methods:
        upper = float(serial.variance_upper_confidence(
            total, total_dof, gate["phase-frequency-ucb"]["ucb_budget"]["delta_per_segment"]))
    control = serial._controls_for_workspace(args, workspace)[0]
    truth = int(workspace.manifest.get("signal_bit_simulation_truth", workspace.manifest.get("signal_bit", 0)))
    if truth not in (0,1):
        raise serial.WorkspaceError("simulation evaluation bit must be zero or one")
    prep_seconds = time.perf_counter()-start
    start_hash = time.perf_counter()
    hashes = _source_hashes(workspace)
    return {"index": index, "r": r, "G": G, "projected_r": projected_r,
            "projected_G": projected_G, "received_energy": energy,
            "residual_energy": residual_energy, "dof": dof, "dt": dt,
            "valid_rows": rows, "variance_upper": upper, "reference_variance": ref,
            "control": control, "truth_bit": truth, "input_hashes": hashes,
            "prep_seconds": prep_seconds, "hash_seconds": time.perf_counter()-start_hash,
            "pooled_residual_energy": total, "pooled_complex_dof": total_dof,
            "gaussian_projection_supported": workspace.gaussian_projection_supported}


def _infer_shard(args, workspace, paths, mapping, labels):
    device = torch.device(args.device)
    states = serial._new_states(args, workspace, mapping, len(paths), False)
    dtype = torch.complex128 if args.precision == "fp64" else torch.complex64
    transfer_seconds = inference_seconds = packing_seconds = 0.
    upper = np.asarray([p["variance_upper"] for p in paths], dtype=np.float64) if "phase-frequency-ucb" in args.methods else None
    for p in paths[1:]:
        if not np.array_equal(p["dt"], paths[0]["dt"]):
            raise serial.WorkspaceError("batch paths have different physical block spacings")
    cached_bytes = 0
    with torch.inference_mode():
        if args.transfer_mode == "resident":
            resident_inputs = {}
            for method in args.methods:
                start = time.perf_counter()
                if method == "phase-frequency-ucb":
                    rr = np.stack([p["projected_r"] for p in paths],axis=1)
                    gg = np.stack([p["projected_G"] for p in paths],axis=1)
                    # The same FP64 elementwise division as block mode, before
                    # casting any bulk observations to the requested precision.
                    rr /= upper[None,:,None,None]
                    gg /= upper[None,:,None,None,None]
                else:
                    rr = np.stack([p["r"] for p in paths],axis=1)
                    gg = np.stack([p["G"] for p in paths],axis=1)
                packing_seconds += time.perf_counter()-start
                if device.type == "cuda":
                    torch.cuda.synchronize(device)
                start = time.perf_counter()
                resident_inputs[method] = (torch.as_tensor(rr,dtype=dtype,device=device),
                                           torch.as_tensor(gg,dtype=dtype,device=device))
                if device.type == "cuda":
                    torch.cuda.synchronize(device)
                transfer_seconds += time.perf_counter()-start
                cached_bytes += sum(t.numel()*t.element_size() for t in resident_inputs[method])
                del rr, gg
            if device.type == "cuda":
                torch.cuda.synchronize(device)
            start = time.perf_counter()
            # No posterior-dependent host decision exists inside this loop.
            # Views and updates use the same stream; synchronize only at the
            # completed observation boundary to measure all queued work.
            for j in range(workspace.block_count):
                for method,(rr,gg) in resident_inputs.items():
                    states[method][0].update(rr[j],gg[j],float(paths[0]["dt"][j]))
            if device.type == "cuda":
                torch.cuda.synchronize(device)
            inference_seconds = time.perf_counter()-start
        else:
            # Original block-transfer organization retained for paired timing.
            for j in range(workspace.block_count):
                start = time.perf_counter()
                packed = {}
                for method in args.methods:
                    if method == "phase-frequency-ucb":
                        rr = np.stack([p["projected_r"][j] for p in paths])/upper[:,None,None]
                        gg = np.stack([p["projected_G"][j] for p in paths])/upper[:,None,None,None]
                    else:
                        rr = np.stack([p["r"][j] for p in paths])
                        gg = np.stack([p["G"][j] for p in paths])
                    packed[method] = rr, gg
                packing_seconds += time.perf_counter()-start
                if device.type == "cuda":
                    torch.cuda.synchronize(device)
                start = time.perf_counter()
                tensors = {method: (torch.as_tensor(rr,dtype=dtype,device=device),
                                    torch.as_tensor(gg,dtype=dtype,device=device))
                           for method,(rr,gg) in packed.items()}
                if device.type == "cuda":
                    torch.cuda.synchronize(device)
                transfer_seconds += time.perf_counter()-start
                start = time.perf_counter()
                for method, (rr,gg) in tensors.items():
                    states[method][0].update(rr,gg,float(paths[0]["dt"][j]))
                if device.type == "cuda":
                    torch.cuda.synchronize(device)
                inference_seconds += time.perf_counter()-start
    label_tensor = torch.as_tensor(labels, dtype=torch.long, device=device)
    scores, diagnostics = {}, {}
    for method in args.methods:
        scores[method] = serial._bit_mixture(states[method][0].scores(), label_tensor).detach().cpu().numpy()
        if not np.isfinite(scores[method]).all():
            raise ValueError("batch phase scores became nonfinite")
        diagnostics[method] = states[method][0].diagnostics()
    return scores, {"transfer_seconds": transfer_seconds, "inference_seconds": inference_seconds,
                    "packing_seconds": packing_seconds, "receiver_diagnostics": diagnostics,
                    "transfer_mode":args.transfer_mode, "resident_input_tensor_bytes":cached_bytes,
                    "input_tensor_staging_operations":2*len(args.methods)*(1 if args.transfer_mode == "resident" else workspace.block_count),
                    "inference_sync_scope":"whole observation" if args.transfer_mode == "resident" else "each block"}


def _attempt_summary(captures, method, control):
    """Keep processing failures in attempted signal reception probability."""
    completed = [capture for capture in captures
                 if capture.get("status") == "completed" and
                 capture.get("methods", {}).get(method, {}).get("counts")]
    failed = len(captures)-len(completed)
    diagnostic = serial._confidence_summary(completed, method, control, .05)
    if control == "signal":
        # Each path contains exactly one actual capture. A failed processing
        # attempt did not correctly deliver its bit, hence it is a miss in the
        # pipeline's all-attempt probability of correct reception.
        failed_as_misses = []
        for capture in captures:
            if capture in completed:
                failed_as_misses.append(capture)
            else:
                failed_as_misses.append({**capture, "methods":{method:{"counts":{
                    "captures":1, "correct_accepted":0, "wrong_accepted":0,
                    "missed":1, "raw_correct":0}}}})
        result = serial._confidence_summary(failed_as_misses, method, control, .05)
        result["success_probability_scope"] = "all attempted signal paths; processing failures count as misses"
    else:
        # Failed noise processing is not a valid observed no-admission trial.
        # Completed-only intervals are diagnostics when failures exist; they
        # do not qualify the whole attempted null campaign.
        result = dict(diagnostic)
        result["valid_null_qualification_trials"] = len(completed)
        result["null_qualification_complete"] = failed == 0
        result["false_admission_qualification_interval"] = (
            diagnostic["path_mean_fraction_hoeffding_two_sided"] if failed == 0 else None)
        result["success_probability_scope"] = "completed null observations only; failures invalidate whole-campaign qualification"
    result.update({"attempted_physical_paths":len(captures),
                   "completed_physical_paths":len(completed),
                   "failed_physical_paths":failed,
                   "completed_only_diagnostic":diagnostic})
    return result


def run(args):
    if args.threads > 2:
        raise ValueError("CPU preparation is limited to two threads")
    torch.set_num_threads(args.threads)
    torch.set_float32_matmul_precision("highest")
    if args.device.startswith("cuda") and not torch.cuda.is_available():
        raise ValueError("CUDA requested but unavailable")
    device = torch.device(args.device)
    if device.type == "cuda":
        torch.cuda.synchronize(device)
        torch.cuda.reset_peak_memory_stats(device)
    workspaces = [serial.BlockWorkspace(path, max_tile_blocks=args.tile_blocks,
                        check_psd=False, memory_limit_bytes=args.memory_limit_bytes) for path in args.inputs]
    _validate_profiles(workspaces)
    for workspace in workspaces:
        _release_maps(workspace)
    selected, mapping, labels = serial._selected_candidates(workspaces[0],args.taps)
    _, gates = serial._gates(args,workspaces[0].block_count)
    reports, arrays, shards = [], {}, []
    start_all = time.perf_counter()
    for shard_first in range(0,len(workspaces),args.batch_paths):
        shard_indices = list(range(shard_first,min(shard_first+args.batch_paths,len(workspaces))))
        memory = _memory_guard(args,workspaces[0],mapping,len(shard_indices))
        prepared = []
        for index in shard_indices:
            workspace = workspaces[index]
            try:
                prepared.append(_prepare_path(args,workspace,index,mapping,gates))
            except Exception as error:
                reports.append(_error_record(workspace,index,error))
            finally:
                _release_maps(workspace)
        if not prepared:
            continue
        try:
            score, timing = _infer_shard(args,workspaces[0],prepared,mapping,labels)
        except Exception as error:
            for path in prepared:
                reports.append(_error_record(workspaces[path["index"]],path["index"],error))
            del path, prepared
            continue
        shard_id = len(shards)
        shards.append({"shard":shard_id,"physical_path_indices":[p["index"] for p in prepared],
                       "memory":memory,**timing})
        for offset,path in enumerate(prepared):
            index = path["index"]
            workspace = workspaces[index]
            methods = {}
            # Simulator variance is consulted only here, after inference, to
            # describe baseline model mismatch. It never normalizes observations.
            matches = abs(workspace.noise_variance/path["reference_variance"]-1.) <= 1e-12
            for method in args.methods:
                value = score[method][offset:offset+1]
                arrays[f"path{index:05d}_{path['control']}_{method}"] = value
                calibration = ("diagnostic_only_frontend_model_not_gaussian" if not path["gaussian_projection_supported"] else
                    "independent_whole_bank_stationary_ucb_budgeted" if method.endswith("-ucb") else
                    "known_variance_model_matches_reference" if matches else "diagnostic_only_known_variance_mismatch_uncalibrated")
                methods[method] = {"status":"scored","counts":serial._classify(value,path["truth_bit"],gates[method]["threshold_log_likelihood_ratio"],path["control"]),
                                  "calibration_status":calibration,"threshold":gates[method]}
            reports.append({"status":"completed","physical_path_index":index,"shard":shard_id,
                "input":str(workspace.directory),"input_hashes":path["input_hashes"],
                "control":path["control"],"conditional_noise_trials":1,"truth_bit_simulation_only":path["truth_bit"],
                "attempted_captures":1,"completed_captures":1,
                "reference_variance":path["reference_variance"],"selected_candidate_indices":selected,
                "selected_candidate_count":len(selected),"candidate_count":len(workspace.candidates),
                "calibration_full_bank_dimension":workspace.dimension,
                "valid_raw_complex_samples":int(path["valid_rows"].sum()),
                "stationary_residual_pool":{"complex_dof":path["pooled_complex_dof"],"residual_energy":path["pooled_residual_energy"],"variance_upper_to_reference":path["variance_upper"]},
                "preparation_seconds":path["prep_seconds"],"source_hash_seconds":path["hash_seconds"],"methods":methods})
        del path, prepared
    reports.sort(key=lambda c:c["physical_path_index"])
    summary = {}
    for control in ("signal","null"):
        captures = [c for c in reports if c.get("control")==control]
        if captures:
            summary[control] = {method:_attempt_summary(captures,method,control) for method in args.methods}
    output = args.output
    output.parent.mkdir(parents=True,exist_ok=True)
    npz = output.with_suffix(".scores.npz")
    np.savez_compressed(npz,**arrays)
    cuda_memory = None
    if device.type == "cuda":
        torch.cuda.synchronize(device)
        cuda_memory = {"maximum_allocated_bytes":torch.cuda.max_memory_allocated(device),
                       "maximum_reserved_bytes":torch.cuda.max_memory_reserved(device),
                       "device_name":torch.cuda.get_device_name(device),
                       "compute_capability":list(torch.cuda.get_device_capability(device)),
                       "scope":"process peak since reset before preparation; reserved includes allocator cache"}
    report = {"schema":"triad-noise-batch-study-v1","scope":"actual independent physical-path batches; unchanged phase likelihood and stationary whole-bank UCB",
        "source_sha256":serial._sha256(Path(__file__)),"serial_source_sha256":serial._sha256(Path(serial.__file__)),
        "configuration":{k:v for k,v in vars(args).items() if k not in ("inputs","output","memory_limit_bytes")},
        "family":{"false_probability":args.false_probability,"gates":gates,"selection_methods":args.selection_methods,"calibration_alternatives":args.calibration_alternatives},
        "captures":reports,"shards":shards,"summary_by_control":summary,
        "failed_physical_capture_records":sum(c["status"]=="error" for c in reports),
        "attempted_physical_capture_records":len(reports),
        "completed_physical_capture_records":sum(c["status"]=="completed" for c in reports),
        "scores_file":npz.name,"scores_sha256":serial._sha256(npz),"wall_seconds_joint":time.perf_counter()-start_all,
        "stage_seconds":{"cpu_preparation":sum(c.get("preparation_seconds",0.) for c in reports),
            "source_hashing":sum(c.get("source_hash_seconds",0.) for c in reports),
            "cpu_packing":sum(s["packing_seconds"] for s in shards),
            "transfer":sum(s["transfer_seconds"] for s in shards),
            "inference":sum(s["inference_seconds"] for s in shards)},
        "arithmetic":{"cpu_retained_statistics":"complex128", "rank_energy_variance":"FP64",
            "device_observation_storage":"complex128" if args.precision == "fp64" else "complex64",
            "phase_bulk":args.precision, "phase_evidence":"FP64",
            "final_mixtures_thresholds":"FP64", "float32_matmul_precision":"highest; no reduced-input tensor arithmetic"},
        "cuda_memory":cuda_memory,
        "host":{"python":platform.python_version(),"numpy":np.__version__,"torch":torch.__version__,"device":args.device,"threads":torch.get_num_threads()},
        "limitations":["stationary pooling requires one constant unknown white proper-complex noise power within each completed physical-path observation",
            "physical-path independence is supplied by generative seeds; duplicate known identities are rejected",
            "FP64 CPU rank/energy/variance preparation and FP64 evidence coexist with explicitly selected bulk state precision",
            "block transfers synchronize each block; resident transfers stage the selected shard once and synchronize inference at the whole-observation boundary",
            "timing includes preparation, packing and transfer; this adapter retains no state history and buffers at most batch-paths physical observations"]}
    output.write_text(json.dumps(report,indent=2,allow_nan=False)+"\n")
    return report


def main(argv=None):
    args = _parse_args(argv)
    try:
        report = run(args)
    except Exception as error:
        raise SystemExit(f"triad-noise-batch-study failed: {type(error).__name__}: {error}") from error
    print(json.dumps({"output":str(args.output),"captures":len(report["captures"]),
                      "failed_captures":report["failed_physical_capture_records"],"wall_seconds_joint":report["wall_seconds_joint"]}))
    return 0 if not report["failed_physical_capture_records"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
