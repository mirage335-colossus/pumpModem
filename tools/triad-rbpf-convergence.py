#!/usr/bin/env python3
"""Particle-count/algorithm-randomness diagnostics on one actual observation.

Replicas reuse identical observations and differ only in predeclared prior-path
randomness. They are not independent physical validation trials. This tool does
not admit a bit, select the best seed or stop adaptively. It reports normalized
mean likelihoods and their finite-replica variation, not qualified sensitivity.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import math
from pathlib import Path
import platform
import sys
import time

import numpy as np
import torch

from triad_rbpf import RBPhaseFilter
from triad_state_workspace import BlockWorkspace, WorkspaceError

_SPEC = importlib.util.spec_from_file_location("triad_rbpf_adapter", Path(__file__).with_name("triad-rbpf-study.py"))
adapter = importlib.util.module_from_spec(_SPEC)
assert _SPEC.loader is not None
_SPEC.loader.exec_module(adapter)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--particle-counts", type=int, nargs="+", default=[32, 128, 512, 2048])
    parser.add_argument("--algorithm-replicates", type=int, default=8)
    parser.add_argument("--frequency-initializations", choices=("iid", "stratified"), nargs="+", default=["iid"])
    extra, remaining = parser.parse_known_args(sys.argv[1:] if argv is None else argv)
    args = adapter.parse_args(remaining)
    if len(args.inputs) != 1:
        parser.error("convergence diagnostic requires exactly one actual workspace")
    if args.resampling != "none":
        parser.error("initial convergence diagnostic requires --resampling none")
    if not 1 <= extra.algorithm_replicates <= 1024:
        parser.error("algorithm replicas must be in 1..1024")
    if len(extra.particle_counts) != len(set(extra.particle_counts)) or not all(1 <= n <= 1_048_576 for n in extra.particle_counts):
        parser.error("particle counts must be distinct and in 1..1048576")
    if len(extra.frequency_initializations) != len(set(extra.frequency_initializations)):
        parser.error("frequency initialization modes must be distinct")
    args.frequency_initializations = extra.frequency_initializations
    args.particle_counts = extra.particle_counts
    args.algorithm_replicates = extra.algorithm_replicates
    return args


def memory_preflight(blocks, candidates, taps, dimension, methods, particles, replicas, precision):
    """Conservative live-array bound including CPU+device caches and one state."""
    bulk_bytes = 8 if precision == "fp32" else 16
    retained = methods*blocks*candidates*(taps+taps*taps)*16
    # CPU cache, device cache, construction/transfers, candidate output buffers.
    caches = 3*retained + blocks*8*3
    # Mirror the core's conservative live-array contract: eight particle/tap
    # arrays and twenty-four scalar FP64/int64 arrays, even without resampling.
    # The test compares this pre-cache estimate against the initialized core.
    state = replicas*candidates*(particles*(8*taps*bulk_bytes+24*8)+16*16*taps*taps+128)
    preparation = 12*dimension*dimension*16+8*dimension*16+65536
    return {"retained_cpu_arrays_bytes": retained+blocks*8,
            "cache_and_conversion_bound_bytes": caches, "core_array_bound_bytes": state,
            "full_bank_preparation_bound_bytes": preparation,
            "total_array_bound_bytes": caches+state+preparation}


def prepare(workspace, args, mapping, reference):
    b, c, k = workspace.block_count, *mapping.shape
    retained = {method: (np.empty((b,c,k), np.complex128), np.empty((b,c,k,k), np.complex128)) for method in args.methods}
    elapsed = np.empty(b, np.float64)
    upper = calibration = None
    if "rbpf-ucb" in args.methods:
        upper, calibration = adapter.calibrate(workspace, reference, args.budget.delta_per_segment)
    for j, (r, G, energy, valid, dt) in enumerate(adapter.actual_blocks(workspace, reference)):
        result = adapter.whole_bank_residual(torch.from_numpy(r[None]), torch.from_numpy(G), energy, valid)
        elapsed[j] = dt
        for method, (rr, gg) in retained.items():
            source_r, source_G, scale = (result.projected_correlations.numpy()[0], result.projected_gram.numpy(), upper) if method == "rbpf-ucb" else (r, G, 1.)
            rr[j] = source_r[mapping]/scale
            gg[j] = source_G[mapping[:,:,None], mapping[:,None,:]]/scale
    return retained, elapsed, calibration


def summarize_scores(candidate_scores, labels):
    """Likelihood averaging is performed after normalized candidate mixing."""
    labels = torch.as_tensor(labels, dtype=torch.long, device=candidate_scores.device)
    if candidate_scores.ndim != 2 or candidate_scores.shape[1] != len(labels):
        raise ValueError("scores must be [algorithm_replica,candidate]")
    bit_scores = torch.stack([torch.logsumexp(candidate_scores[:,labels == bit], dim=1)-math.log(int((labels == bit).sum())) for bit in (0,1)], dim=1)
    if not bool(torch.isfinite(bit_scores).all()):
        raise ValueError("nonfinite convergence scores")
    aggregate = torch.logsumexp(bit_scores, dim=0)-math.log(len(bit_scores))
    maximum = bit_scores.max(dim=0).values
    scaled_likelihood = torch.exp(bit_scores-maximum)
    mean = scaled_likelihood.mean(dim=0)
    sem_fraction = scaled_likelihood.std(dim=0, correction=1)/(math.sqrt(len(bit_scores))*mean) if len(bit_scores)>1 else None
    result = {"bit_log_scores_per_algorithm_replica": bit_scores.cpu().tolist(),
              "log_mean_likelihood_by_bit": aggregate.cpu().tolist(),
              "log_score_minimum_by_bit": bit_scores.min(dim=0).values.cpu().tolist(),
              "log_score_maximum_by_bit": maximum.cpu().tolist(),
              "log_score_population_sd_by_bit": bit_scores.std(dim=0, correction=0).cpu().tolist(),
              "log_score_population_variance_by_bit": bit_scores.var(dim=0, correction=0).cpu().tolist(),
              "likelihood_mean_scaled_by_bit": mean.cpu().tolist(),
              "likelihood_scale_log_by_bit": maximum.cpu().tolist(),
              "mean_likelihood_by_bit": [math.exp(float(v)) if float(v) <= math.log(sys.float_info.max) else None for v in aggregate],
              "mean_likelihood_representation": "scaled mean times exp(log scale); raw mean is null if above float64 range",
              "likelihood_relative_standard_error_by_bit": None if sem_fraction is None else sem_fraction.cpu().tolist(),
              "relative_error_scope": "finite-replica descriptive standard error; rare likelihood tails may be unresolved, no convergence guarantee"}
    return result


def run_count(r, G, elapsed, labels, args, particles, seed, state_limit, frequency_initialization="iid"):
    replicas, c, k = args.algorithm_replicates, r.shape[1], r.shape[2]
    dtype = torch.complex64 if args.precision == "fp32" else torch.complex128
    state = RBPhaseFilter(args.tap_prior, args.phase_prior, particles=particles, seed=seed,
                          dtype=dtype, resampling="none", memory_limit_bytes=state_limit,
                          frequency_initialization=frequency_initialization)
    if args.device.startswith("cuda"): torch.cuda.synchronize(args.device)
    started = time.perf_counter()
    # Device caches retain all selected linear information once per method;
    # replication is a broadcast view, not copies of the physical observation.
    rr = torch.as_tensor(r, device=args.device)
    gg = torch.as_tensor(G, device=args.device)
    if args.device.startswith("cuda"): torch.cuda.synchronize(args.device)
    transfer_seconds = time.perf_counter()-started
    started = time.perf_counter()
    for j, dt in enumerate(elapsed):
        state.update(rr[j].unsqueeze(0).expand(replicas,c,k), gg[j].unsqueeze(0), float(dt))
    scores = state.finish()
    result = summarize_scores(scores, labels)
    diagnostics = state.diagnostics()
    result["minimum_effective_particles_by_replica_candidate"] = state._minimum_ess.reshape(replicas,c).cpu().tolist()
    result["maximum_particle_weight_by_replica_candidate"] = state._maximum_weight.reshape(replicas,c).cpu().tolist()
    if args.device.startswith("cuda"): torch.cuda.synchronize(args.device)
    scores_by_replica = np.asarray(result["bit_log_scores_per_algorithm_replica"])
    winners = scores_by_replica.argmax(axis=1)
    result["diagnostic_decisions_per_algorithm_replica"] = [
        {"replica_index": j, "master_seed": seed, "winning_bit": int(bit),
         "winning_log_score": float(scores_by_replica[j, bit]),
         "exceeds_reference_gate": bool(scores_by_replica[j, bit] >= args.budget.log_threshold)}
        for j, bit in enumerate(winners)]
    result["diagnostic_reference_log_gate"] = args.budget.log_threshold
    result["diagnostic_decision_scope"] = "fixed reference-gate comparison only; additional initialization/seed selection is not qualified admission"
    result.update(particles=particles, algorithm_replicates=replicas, master_seed=seed,
                  frequency_initialization=frequency_initialization,
                  diagnostics=diagnostics, transfer_seconds=transfer_seconds,
                  inference_seconds=time.perf_counter()-started)
    return result


def paired_comparison(reference_rows, alternative_rows, reference_mode, alternative_mode):
    """Paired descriptive contrasts; neither mode nor replica is selected."""
    comparisons = []
    for left, right in zip(reference_rows, alternative_rows):
        if (left["particles"], left["master_seed"], left["algorithm_replicates"]) != (right["particles"], right["master_seed"], right["algorithm_replicates"]):
            raise ValueError("initialization comparison requires paired counts, seeds and replicas")
        a = np.asarray(left["bit_log_scores_per_algorithm_replica"], dtype=np.float64)
        b = np.asarray(right["bit_log_scores_per_algorithm_replica"], dtype=np.float64)
        if a.shape != b.shape: raise ValueError("initialization replica shape mismatch")
        difference = b-a
        scale = np.maximum(a.max(axis=0), b.max(axis=0))
        likelihood_difference = np.exp(b-scale)-np.exp(a-scale)
        comparisons.append({"particles": left["particles"], "master_seed": left["master_seed"],
            "reference_initialization": reference_mode, "alternative_initialization": alternative_mode,
            "paired_log_score_differences_by_replica_bit": difference.tolist(),
            "mean_paired_log_score_difference_by_bit": difference.mean(axis=0).tolist(),
            "paired_log_score_difference_population_variance_by_bit": difference.var(axis=0).tolist(),
            "likelihood_difference_scale_log_by_bit": scale.tolist(),
            "paired_scaled_likelihood_differences_by_replica_bit": likelihood_difference.tolist(),
            "mean_scaled_likelihood_difference_by_bit": likelihood_difference.mean(axis=0).tolist(),
            "log_mean_likelihood_difference_by_bit": (np.asarray(right["log_mean_likelihood_by_bit"])-np.asarray(left["log_mean_likelihood_by_bit"])).tolist(),
            "reference_gate_decision_disagreements": sum(x["winning_bit"] != y["winning_bit"] or x["exceeds_reference_gate"] != y["exceeds_reference_gate"] for x,y in zip(left["diagnostic_decisions_per_algorithm_replica"], right["diagnostic_decisions_per_algorithm_replica"]))})
    return comparisons


def run(args):
    torch.set_num_threads(args.threads); torch.set_float32_matmul_precision("highest")
    if args.device.startswith("cuda"):
        if not torch.cuda.is_available(): raise ValueError("CUDA unavailable")
        torch.cuda.init(); torch.cuda.set_device(args.device)
        torch.backends.cuda.matmul.allow_tf32 = False
        torch.cuda.reset_peak_memory_stats(args.device)
    started = time.perf_counter()
    ws = BlockWorkspace(args.inputs[0], max_tile_blocks=1, memory_limit_bytes=args.memory_limit_bytes)
    if not ws.has_observation or not ws.has_received_energy:
        raise WorkspaceError("actual observations and received energy required")
    if not ws.gaussian_projection_supported: raise WorkspaceError("sampled frontend does not support Gaussian null")
    indices, mapping, labels = adapter.candidate_mapping(ws, args.taps)
    bound = memory_preflight(ws.block_count, len(mapping), args.taps, ws.dimension, len(args.methods), max(args.particle_counts), args.algorithm_replicates, args.precision)
    if bound["total_array_bound_bytes"] > args.memory_limit_bytes:
        raise MemoryError(f"convergence arrays {bound['total_array_bound_bytes']} exceed limit {args.memory_limit_bytes}")
    reference = args.reference_variance
    if reference is None:
        try: reference = ws.sample_rate/10**(args.cn0/10)
        except (OverflowError, ZeroDivisionError) as error: raise ValueError("reference variance is unresolved") from error
    if not math.isfinite(reference) or reference <= 0: raise ValueError("reference variance is unresolved")
    t0 = time.perf_counter(); retained, elapsed, calibration = prepare(ws, args, mapping, reference)
    preparation_seconds = time.perf_counter()-t0
    initialization_results = {}
    state_limit = args.memory_limit_bytes-bound["cache_and_conversion_bound_bytes"]-bound["full_bank_preparation_bound_bytes"]
    for initialization in args.frequency_initializations:
        initialization_results[initialization] = {}
        for method in args.methods:
            rows = initialization_results[initialization][method] = []
            for index, particles in enumerate(args.particle_counts):
                seed = (args.seed+index) % 2**63
                rows.append(run_count(*retained[method], elapsed, labels, args, particles, seed, state_limit, initialization))
    # Keep the original analyzer's methods layout. The compatibility view is
    # chosen before scoring (IID when requested), never from observed outcomes.
    reference_mode = "iid" if "iid" in initialization_results else args.frequency_initializations[0]
    methods = initialization_results[reference_mode]
    paired = {mode: {method: paired_comparison(methods[method], initialization_results[mode][method], reference_mode, mode)
                     for method in args.methods} for mode in args.frequency_initializations if mode != reference_mode}
    # Evaluation labels are recorded only after all algorithm outputs are fixed.
    truth = ws.manifest.get("signal_bit_simulation_truth", ws.manifest.get("signal_bit", None))
    configuration = {k:v for k,v in vars(args).items() if k not in ("inputs","output","tap_prior","phase_prior","budget")}
    configuration["coherence"] = "infinity" if math.isinf(args.coherence) else args.coherence
    configuration["tap_powers"] = list(args.tap_prior.stationary_power)
    result = {"schema":"triad-rbpf-convergence-v1", "source_sha256":adapter.sha256(__file__),
        "dependencies_sha256":{name:adapter.sha256(Path(__file__).with_name(name)) for name in ("triad-rbpf-study.py","triad_rbpf.py","triad_gaussian_state.py","triad_noise_likelihood.py","triad_state_workspace.py")},
        "configuration":configuration,"methods":methods,"reference_variance":reference,"calibration":calibration,
        "methods_compatibility_initialization":reference_mode,"frequency_initialization_results":initialization_results,
        "paired_initialization_comparisons":paired,
        "calibration_family_only":args.budget.diagnostics(),"memory":bound,
        "input":str(ws.directory),"input_hashes":{name:adapter.sha256(ws.directory/name) for name in adapter.INPUT_FILES},
        "block_count":ws.block_count,"sampled_seconds":ws.sample_count/ws.sample_rate,
        "selected_candidate_indices":indices,"candidate_count_by_bit":[int((labels==bit).sum()) for bit in (0,1)],
        "truth_bit_simulation_only":truth,"control_simulation_only":ws.manifest.get("control","signal"),
        "preparation_seconds":preparation_seconds,"wall_seconds":time.perf_counter()-started,
        "host":{"python":platform.python_version(),"numpy":np.__version__,"torch":torch.__version__,"device":args.device},
        "limitations":["Identical observation is reused across algorithm replicas and particle counts; this is not independent physical validation.",
            "No operational admission, best-seed/mode selection, sequential stopping or qualified sensitivity estimate is produced; per-replica reference-gate decisions are diagnostics only.",
            "Initialization modes reuse identical observations, master seeds and replica-index RNG rows; equal draw counts preserve paired downstream phase randomness. No independent per-replica seed integer is fabricated.",
            "Replicas have independent prior-path random draws under one predeclared master seed per particle count; devices need not generate identical random trajectories.",
            "Log mean likelihood is normalized across replicas; min/max log scores are descriptive spread only and are never substituted for that mean.",
            "Finite-bank phase likelihood is block-constant; static-frequency FFT priors or grids are different experiments, so a score difference alone is not a sensitivity gain.",
            "UCB uses one whole-capture stationary white proper-complex noise bound; calibration family values do not qualify particle convergence or physical detection.",
            "FP32 keeps bulk particle products at complex64; covariance, phase/frequency and evidence remain FP64. TF32 disabled.",
            "Retained selected observations grow with duration within the explicit memory preflight; full-bank calibration streams without duration-sized storage."]}
    if args.device.startswith("cuda"):
        result["cuda_memory"]={"maximum_allocated_bytes":torch.cuda.max_memory_allocated(args.device),"maximum_reserved_bytes":torch.cuda.max_memory_reserved(args.device)}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+"\n")
    return result


def main(argv=None):
    args=parse_args(argv); result=run(args)
    print(json.dumps({"output":str(args.output),"particle_counts":args.particle_counts,"algorithm_replicates":args.algorithm_replicates,"wall_seconds":result["wall_seconds"]}))
    return 0


if __name__ == "__main__": raise SystemExit(main())
