#!/usr/bin/env python3
"""Exhaustive sampled-waveform correlation and phase/frequency campaign.

This runner deliberately retains every CFO x candidate x lag statistic and
passes that full family to the existing phase/tap likelihood. Native FP4 and
FP32 correlation modes are experimental comparisons; FP64 remains the oracle.
Observation quantization is performed by the selected native backend on the
actual sampled IQ windows, so those scores have no inherited Gaussian tail
calibration. Repeated timing scans reuse the same capture and are not
independent trials.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import subprocess
import time

import numpy as np
import torch

from triad_exhaustive_correlation import ExhaustiveCorrelationBank, direct_fp64_reference
from triad_phase_frequency import PhaseFrequencyLikelihood, PhaseFrequencyModel


FORMAT = "triad-sampled-waveforms-v1"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def generation_provenance(directory: Path, input_hashes: dict):
    """Bind the actual producer separately from the currently installed scorer."""
    path = directory.parent / (directory.name + ".run.json")
    if not path.exists():
        return dict(status="unavailable; current source hashes do not identify the input producer")
    record = json.loads(path.read_text())
    expected = dict(record.get("exported_file_hashes", {}))
    if record.get("manifest_sha256"):
        expected["manifest.json"] = record["manifest_sha256"]
    if record.get("returncode") != 0 or record.get("status", "complete") != "complete":
        raise ValueError("input generation record does not report successful generation")
    if set(expected) != set(input_hashes) or any(input_hashes[name] != value for name, value in expected.items()):
        raise ValueError("input generation record does not match sampled payload hashes")
    return dict(status="verified payload binding", record_sha256=sha256(path), record=record)


def _need_positive_int(value, name):
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise ValueError(f"manifest {name} must be a positive integer")
    return value


def load_capture(directory: Path, *, trim_zero_suffix=False):
    """Load and validate one strict complex128 sampled export."""
    manifest_path = directory / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("format") != FORMAT:
        raise ValueError(f"{directory}: expected {FORMAT} manifest")
    if manifest.get("complex_type") != "complex128" or manifest.get("byte_order") != "little":
        raise ValueError("campaign requires little-endian complex128 source files")
    variance = manifest.get("noise_variance", manifest.get("noise_variance_pre_frontend"))
    if variance is None or not math.isfinite(float(variance)) or float(variance) <= 0:
        raise ValueError("sampled export requires an explicit positive noise variance")
    basis_shape = manifest.get("basis_shape")
    observation_shape = manifest.get("observation_shape")
    if (not isinstance(basis_shape, list) or len(basis_shape) != 3 or
            not isinstance(observation_shape, list) or len(observation_shape) != 1):
        # Also accept explicitly named dimensions from future extended exports.
        candidates = _need_positive_int(manifest.get("candidate_count"), "candidate_count")
        samples = _need_positive_int(manifest.get("template_sample_count",
                                                   manifest.get("sample_count")),
                                     "template_sample_count")
        observed_samples = _need_positive_int(manifest.get("observation_sample_count",
                                                            manifest.get("sample_count")),
                                              "observation_sample_count")
        taps = _need_positive_int(manifest.get("tap_count"), "tap_count")
    else:
        candidates, samples, taps = (_need_positive_int(x, "basis_shape entry") for x in basis_shape)
        observed_samples = _need_positive_int(observation_shape[0], "observation_shape entry")
        if manifest.get("candidate_count", candidates) != candidates:
            raise ValueError("candidate count disagrees with basis shape")
    declared_samples = manifest.get("sample_count")
    if declared_samples is not None and declared_samples != observed_samples:
        raise ValueError("observation sample count disagrees with sample_count")
    if taps not in (1, 2):
        raise ValueError("phase/frequency receiver supports one or two coupled taps")
    metadata = manifest.get("candidates")
    if not isinstance(metadata, list) or len(metadata) != candidates:
        raise ValueError("candidate metadata count is inconsistent")
    labels = np.asarray([row.get("bit") for row in metadata])
    if not np.isin(labels, [0, 1]).all() or not np.any(labels == 0) or not np.any(labels == 1):
        raise ValueError("candidate metadata must include both bit labels")
    if int(np.sum(labels == 0)) != int(np.sum(labels == 1)):
        raise ValueError("bit candidate groups must have equal size for the fixed mixture")
    basis_path, observation_path = directory / manifest.get("basis_file", "basis.c128"), directory / manifest.get("observation_file", "observation.c128")
    basis_bytes = candidates * samples * taps * 16
    observation_bytes = observed_samples * 16
    if basis_path.stat().st_size != basis_bytes or observation_path.stat().st_size != observation_bytes:
        raise ValueError("sampled waveform files do not match declared complex128 shapes")
    basis = np.memmap(basis_path, mode="r", dtype="<c16", shape=(candidates, samples, taps))
    for index, candidate in enumerate(metadata):
        count = candidate.get("tap_count", manifest.get("tap_count"))
        if count not in (1, 2) or count > taps:
            raise ValueError("each candidate requires a supported declared tap_count")
        if count < taps and np.any(basis[index, :, count:] != 0):
            raise ValueError("unused candidate tap columns must be exactly zero")
    retained_samples = samples
    if trim_zero_suffix:
        active = np.any(basis != 0, axis=(0, 2))
        nonzero = np.flatnonzero(active)
        if not nonzero.size:
            raise ValueError("cannot derive template extent from an all-zero candidate bank")
        retained_samples = int(nonzero[-1]) + 1
        if retained_samples == samples:
            raise ValueError("requested zero-suffix trim, but basis has no common all-zero suffix")
        basis = np.asarray(basis[:, :retained_samples, :]).copy()
    observation = np.array(np.memmap(observation_path, mode="r", dtype="<c16", shape=(observed_samples,)),
                           copy=True)[None, :]
    validity_path = directory / manifest.get("validity_file", "validity.u8")
    if "validity_file" in manifest and not validity_path.exists():
        raise ValueError("declared validity mask is missing")
    if validity_path.exists():
        if validity_path.stat().st_size != observed_samples:
            raise ValueError("validity mask does not match observation sample count")
        validity = np.memmap(validity_path, mode="r", dtype=np.uint8, shape=(observed_samples,))
        if not np.isin(validity, [0, 1]).all():
            raise ValueError("validity mask must contain only zero or one")
        if np.any(validity == 0):
            raise ValueError("this runner requires an all-valid capture; erasure masks need lag-specific Gram handling")
        extra_path = (validity_path,)
    else:
        extra_path = ()
    return dict(directory=directory, manifest=manifest, metadata=metadata,
                labels=labels.astype(np.int64), basis=basis, observation=observation,
                original_template_samples=samples, template_samples=retained_samples,
                trimmed_template_samples=samples-retained_samples,
                paths=(manifest_path, basis_path, observation_path, *extra_path))


def _sync(device):
    if device.type == "cuda":
        torch.cuda.synchronize(device)


def _new_receiver(batch, candidates, taps, device, inference_precision, inference_implementation, args):
    dtype = torch.float64 if inference_precision == "fp64" else torch.float32
    model = PhaseFrequencyModel(
        phase_states=args.phase_states,
        relative_phase_states=args.relative_phase_states,
        frequency_bound_hz=args.residual_frequency_bound,
        nominal_dt_seconds=args.block_samples / args.sample_rate,
        frequency_std_hz=args.residual_frequency_std,
        frequency_correlation_seconds=args.frequency_correlation,
        phase_diffusion_rad_per_sqrt_second=args.phase_diffusion)
    phase_type = PhaseFrequencyLikelihood
    if inference_implementation == "graph-fp64":
        if device.type != "cuda" or dtype != torch.float32:
            raise ValueError("graph-fp64 inference requires CUDA and FP32 bulk state")
        from triad_phase_reduction import Fp64ReductionGraphLikelihood
        phase_type = Fp64ReductionGraphLikelihood
    receiver = phase_type(batch, candidates, taps, model, device=device,
                          dtype=dtype, validate_inputs=False,
                          memory_limit_bytes=getattr(args, "receiver_memory_mib", 4096)*1024**2)
    return receiver


def _per_bit_scores(scores, labels, trials, frequencies, lags):
    # Candidate dimension uses the predeclared equal-weight epoch/candidate
    # mixture already used by the study. External CFO and timing are exhaustive
    # acquisition alternatives, retained as a maximum and counted in the
    # family. No coarse detector or top-K reduction is involved.
    matrix = scores.reshape(trials, frequencies, lags, len(labels))
    bits = []
    for bit in (0, 1):
        group = torch.logsumexp(matrix[..., torch.as_tensor(labels == bit, device=matrix.device)], dim=-1)
        group = group - math.log(int(np.sum(labels == bit)))
        bits.append(group.amax(dim=(1, 2)))
    return torch.stack(bits, dim=1), matrix.permute(0, 1, 3, 2).contiguous()


class GroupedLikelihood:
    """Retain each declared channel model's own gain prior and all candidates."""
    def __init__(self, batch, capture, device, precision, implementation, args):
        self.batch, self.count, self.device = batch, len(capture["labels"]), device
        self.complex_dtype = torch.complex128 if precision == "fp64" else torch.complex64
        counts = [c.get("tap_count", capture["manifest"].get("tap_count"))
                  for c in capture["metadata"]]
        self.groups = []
        for taps in sorted(set(counts)):
            indices = torch.tensor([i for i, count in enumerate(counts) if count == taps], device=device)
            receiver = _new_receiver(batch, len(indices), taps, device, precision, implementation, args)
            self.groups.append((indices, taps, receiver, receiver.weights.clone()))

    def update(self, r, gram, dt):
        for indices, taps, receiver, _ in self.groups:
            receiver.update(r[:, indices, :taps].to(self.complex_dtype),
                            gram[indices, :taps, :taps].to(self.complex_dtype), dt)

    def scores(self):
        result = torch.empty((self.batch, self.count), dtype=torch.float64, device=self.device)
        for indices, _, receiver, _ in self.groups:
            result[:, indices] = receiver.scores()
        return result

    @torch.inference_mode()
    def reset(self):
        for _, _, receiver, initial in self.groups:
            if hasattr(receiver, "reset"):
                receiver.reset()
            else:
                receiver.weights.copy_(initial)
                receiver._scores.zero_()
                receiver.updates = 0
                receiver.elapsed_seconds = 0.

    def diagnostics(self):
        return {"channel_groups": [dict(candidate_indices=indices.cpu().tolist(), taps=taps,
                                         receiver=receiver.diagnostics())
                                   for indices, taps, receiver, _ in self.groups],
                "mixture_weights": "unchanged equal candidate weights after scattering groups"}


def score_capture(capture, args, precision, backend=None, inference_precision="fp64",
                  repeats=1, strict_all=False, *, inference_implementation="reference",
                  correlation_only=False):
    """Run a cold scan and warmed timing repeats on identical sampled input."""
    device = torch.device(args.device)
    if device.type == "cuda":
        torch.cuda.set_device(device)
    event_timing = device.type == "cuda" and getattr(args, "instrumented_timing", False)
    if args.sample_rate != capture["manifest"]["sample_rate"]:
        raise ValueError("sample rate differs from sampled waveform manifest")
    frequencies = np.asarray(args.cfo_hz, dtype=np.float64)
    lags = np.arange(args.lag_start, args.lag_stop + 1, args.lag_step, dtype=np.int64)
    if len(lags) == 0 or lags[-1] != args.lag_stop:
        raise ValueError("lag endpoint must be exactly reachable by lag-step")
    if args.lag_stop + capture["basis"].shape[1] > capture["observation"].shape[1]:
        raise ValueError("full template support leaves sampled capture; no trimming is implicit")
    repeat_observations = getattr(args, "repeat_observations", 1)
    if repeat_observations < 1:
        raise ValueError("repeat_observations must be positive")
    repeat_prepare_start = time.perf_counter()
    observations_cpu = np.repeat(capture["observation"], repeat_observations, axis=0)
    repeat_prepare_seconds = time.perf_counter() - repeat_prepare_start
    transfer_event = None
    transfer_elapsed = 0.
    if device.type == "cuda":
        transfer_start_event = torch.cuda.Event(enable_timing=True)
        transfer_event = torch.cuda.Event(enable_timing=True)
        transfer_start_event.record()
    transfer_wall_start = time.perf_counter()
    observations = torch.as_tensor(observations_cpu, dtype=torch.complex128, device=device)
    transfer_wall_seconds = time.perf_counter() - transfer_wall_start
    if device.type == "cuda":
        transfer_event.record()
    run_precisions = [precision]
    if strict_all and precision != "fp64":
        run_precisions.append("fp64")
    report_runs = []
    final_scores = None
    checksum_values = []
    for run_index, run_precision in enumerate(run_precisions):
        run_backend = None if run_precision == "fp64" else backend
        bank_start = time.perf_counter()
        bank = ExhaustiveCorrelationBank(
            capture["basis"], args.sample_rate, frequencies, lags, args.block_samples,
            device=device, precision=run_precision, backend=run_backend,
            candidate_tile=args.candidate_tile, frequency_tile=args.frequency_tile,
            column_tile=args.column_tile, template_cache_bytes=args.template_cache_mib * 1024**2,
            gram_cache_bytes=getattr(args, "gram_cache_mib", 16) * 1024**2,
            fused_reconstruction=getattr(args, "fused_reconstruction", False))
        bank_construction_seconds = time.perf_counter() - bank_start
        scaled_noise = float(capture["manifest"].get(
            "noise_variance", capture["manifest"].get("noise_variance_pre_frontend")))
        if scaled_noise <= 0 or not math.isfinite(scaled_noise):
            raise ValueError("sampled manifest noise variance must be positive and finite")
        # Input observations are the sampled samples after all declared
        # physical effects. The backend may quantize them per window; no
        # projection-space Gaussian shortcut is used.
        scans = repeats if run_index == 0 else 1
        # First scan is cold; remaining scans reuse the same bank's packed A
        # cache and the exact same physical capture, solely for timing.
        receiver = None
        for scan in range(scans):
            if device.type == "cuda" and getattr(args, "nvtx", False):
                torch.cuda.nvtx.range_push(f"correlation={run_precision};scan={scan};copies={repeat_observations}")
            scan_wall_start = time.perf_counter()
            bank.reset_counters()
            infer_start = time.perf_counter()
            if not correlation_only:
                if receiver is None:
                    receiver = GroupedLikelihood(len(observations) * len(frequencies) * len(lags),
                                                 capture, device, inference_precision,
                                                 inference_implementation, args)
                else:
                    receiver.reset()
            init_seconds = time.perf_counter() - infer_start
            correlation_wall_enqueue_seconds = 0.
            update_wall_enqueue_seconds = 0.
            correlation_events, update_events, finish_events = [], [], []
            with torch.inference_mode():
                blocks = iter(bank.iter_blocks(
                    observations, scaled_noise,
                    time_origin_seconds=float(capture["manifest"].get("time_origin_seconds", 0.)),
                    max_output_bytes=args.max_block_output_mib * 1024**2))
                while True:
                    stage_wall_start = time.perf_counter()
                    if event_timing:
                        event_start = torch.cuda.Event(enable_timing=True)
                        event_stop = torch.cuda.Event(enable_timing=True)
                        event_start.record()
                    try:
                        block = next(blocks)
                    except StopIteration:
                        break
                    if event_timing:
                        event_stop.record()
                        correlation_events.append((event_start, event_stop))
                    correlation_wall_enqueue_seconds += time.perf_counter() - stage_wall_start
                    if correlation_only:
                        checksum_values.append(block.r.abs().sum())
                        continue
                    stage_wall_start = time.perf_counter()
                    if event_timing:
                        event_start = torch.cuda.Event(enable_timing=True)
                        event_stop = torch.cuda.Event(enable_timing=True)
                        event_start.record()
                    # Exhaustive correlation layout [trial,F,C,L,tap] ->
                    # receiver [trial*F*L,C,tap]. The coupled tap Gram is
                    # shared by timing/CFO because every window has full support.
                    r = block.r.permute(0, 1, 3, 2, 4).reshape(
                        len(observations) * len(frequencies) * len(lags),
                        len(capture["labels"]), capture["basis"].shape[2])
                    r = r.to(receiver.complex_dtype)
                    gram = block.gram.to(receiver.complex_dtype)
                    receiver.update(r, gram, block.dt)
                    if event_timing:
                        event_stop.record()
                        update_events.append((event_start, event_stop))
                    update_wall_enqueue_seconds += time.perf_counter() - stage_wall_start
            if not correlation_only:
                finish_start = time.perf_counter()
                if event_timing:
                    event_start = torch.cuda.Event(enable_timing=True)
                    event_stop = torch.cuda.Event(enable_timing=True)
                    event_start.record()
                scores = receiver.scores()
                per_bit, all_scores = _per_bit_scores(scores, capture["labels"], len(observations),
                                                       len(frequencies), len(lags))
                if event_timing:
                    event_stop.record()
                    finish_events.append((event_start, event_stop))
                finish_wall_enqueue_seconds = time.perf_counter() - finish_start
            else:
                all_scores = per_bit = None
                finish_wall_enqueue_seconds = 0.
            # All device timing is collected at this whole-capture barrier.
            # There is no host synchronization between block correlations and updates.
            _sync(device)
            if device.type == "cuda":
                if transfer_event is not None and run_index == 0 and scan == 0:
                    transfer_elapsed = transfer_start_event.elapsed_time(transfer_event)/1000.
                if event_timing:
                    correlation_device_seconds = sum(a.elapsed_time(b) for a, b in correlation_events)/1000.
                    update_device_seconds = sum(a.elapsed_time(b) for a, b in update_events)/1000.
                    finish_device_seconds = sum(a.elapsed_time(b) for a, b in finish_events)/1000.
                else:
                    correlation_device_seconds = update_device_seconds = finish_device_seconds = None
            else:
                correlation_device_seconds = correlation_wall_enqueue_seconds
                update_device_seconds = update_wall_enqueue_seconds
                finish_device_seconds = finish_wall_enqueue_seconds
            if correlation_only:
                current_checksums = checksum_values[-bank.block_count:]
                if current_checksums:
                    _ = torch.stack(current_checksums).sum().item()
                final_scores = None
            else:
                final_scores = all_scores.detach().cpu().numpy()
            scan_wall_seconds = time.perf_counter() - scan_wall_start
            first_scan = run_index == 0 and scan == 0
            transfer_seconds = transfer_elapsed if device.type == "cuda" and first_scan else (
                transfer_wall_seconds if first_scan else 0.)
            if first_scan:
                # One real wall interval captures repeated-column preparation,
                # H2D transfer, bank initialization, inference and completion sync.
                resident_seconds = time.perf_counter() - repeat_prepare_start
            elif scan == 0:
                resident_seconds = bank_construction_seconds + scan_wall_seconds
            else:
                resident_seconds = scan_wall_seconds
            hypotheses = len(frequencies) * len(capture["labels"]) * len(lags)
            capture_seconds = capture["basis"].shape[1] / args.sample_rate
            report_runs.append(dict(run_index=run_index, timing_scan=scan,
                                    precision=run_precision,
                                    correlation_seconds=correlation_device_seconds,
                                    cuda_event_timing=event_timing,
                                    cuda_event_timing_is_instrumented=event_timing,
                                    correlation_device_event_seconds=correlation_device_seconds if event_timing else None,
                                    correlation_host_enqueue_seconds=correlation_wall_enqueue_seconds,
                                    inference_init_seconds=init_seconds,
                                    inference_implementation="none" if correlation_only else inference_implementation,
                                    inference_precision="none" if correlation_only else inference_precision,
                                    inference_update_seconds=update_device_seconds,
                                    inference_update_device_event_seconds=update_device_seconds if event_timing else None,
                                    inference_update_host_enqueue_seconds=update_wall_enqueue_seconds,
                                    inference_finish_seconds=finish_device_seconds,
                                    inference_finish_host_enqueue_seconds=finish_wall_enqueue_seconds,
                                    verification_seconds=0.,
                                    complete_resident_seconds=resident_seconds,
                                    batch_latency_seconds=resident_seconds,
                                    single_capture_latency_seconds=resident_seconds if repeat_observations == 1 else None,
                                    capture_duration_seconds=capture_seconds * repeat_observations,
                                    streaming_realtime_factor=(capture_seconds * repeat_observations / resident_seconds
                                                               if resident_seconds > 0 else None),
                                    exhaustive_hypotheses_processed=hypotheses * repeat_observations,
                                    exhaustive_hypotheses_per_second=hypotheses * repeat_observations / resident_seconds
                                        if resident_seconds > 0 else None,
                                    block_hypothesis_products=hypotheses * bank.block_count * repeat_observations,
                                    block_hypothesis_products_per_second=hypotheses * bank.block_count * repeat_observations /
                                        correlation_device_seconds if correlation_device_seconds is not None and correlation_device_seconds > 0 else None,
                                    bank_construction_seconds=bank_construction_seconds if scan == 0 else 0.,
                                    observation_transfer_wall_seconds=(transfer_wall_seconds
                                        if run_index == 0 and scan == 0 else 0.),
                                    observation_transfer_device_seconds=(transfer_elapsed
                                        if device.type == "cuda" and run_index == 0 and scan == 0 else None),
                                    timing_observation_copies=repeat_observations,
                                    observation_batch_prepare_seconds=repeat_prepare_seconds
                                        if run_index == 0 and scan == 0 else 0.,
                                    timing_copies_are_independent_trials=False,
                                    streaming_scope="aggregate repeated-capture processing, not continuous-stream validation",
                                    correlation_only=correlation_only,
                                    bank=bank.accounting(),
                                    inference={} if correlation_only else receiver.diagnostics(),
                                    bit_scores=[] if correlation_only else per_bit.detach().cpu().numpy().tolist(),
                                    score_matrix=final_scores))
            if device.type == "cuda" and getattr(args, "nvtx", False):
                torch.cuda.nvtx.range_pop()
        # cold/warm scan distinction is explicit in scan index; do not label
        # repeats as independent waveform trials.
    verification = None
    if strict_all and precision != "fp64" and not correlation_only:
        verification_start = time.perf_counter()
        strict_scores = np.asarray(report_runs[-1]["score_matrix"])
        approximate = [row for row in report_runs if row["precision"] != "fp64"]
        verification = []
        strict_winner = strict_scores.reshape(strict_scores.shape[0], -1).argmax(axis=1)
        for row in approximate:
            scores = np.asarray(row["score_matrix"])
            winner = scores.reshape(scores.shape[0], -1).argmax(axis=1)
            verification.append(dict(precision=row["precision"], timing_scan=row["timing_scan"],
                                     max_absolute_likelihood_error=float(np.max(np.abs(scores-strict_scores))),
                                     rms_likelihood_error=float(np.sqrt(np.mean((scores-strict_scores)**2))),
                                     full_hypothesis_winner_changes=int(np.sum(winner != strict_winner)),
                                     compared_values=int(scores.size)))
        verification_seconds = time.perf_counter()-verification_start
        for row in report_runs:
            row["verification_seconds"] = verification_seconds / len(approximate)
    return report_runs, final_scores, verification


def _threshold(alpha, horizon_seconds, attempt_spacing, duration_alternatives,
               model_alternatives, hypotheses):
    attempts = math.ceil(horizon_seconds / attempt_spacing)
    family = attempts * duration_alternatives * model_alternatives * hypotheses
    return dict(attempts=attempts, hypotheses_per_attempt=hypotheses,
                duration_alternatives=duration_alternatives,
                receiver_model_alternatives=model_alternatives,
                family_size=family, allocated_probability=alpha,
                log_likelihood_threshold=math.log(family / alpha))


def _source_revision(repository_root, override=None):
    if override:
        return str(override)
    try:
        return subprocess.run(["git", "rev-parse", "HEAD"], cwd=repository_root,
                              check=True, text=True, stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL).stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "unavailable; source hashes identify files"


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--precision", choices=["fp64", "fp32", "native-fp4", "diagnostic-fp64"], default="fp64")
    parser.add_argument("--representation-mode", choices=["strict", "template-only", "observation-only", "both"], default="both")
    parser.add_argument("--fused-reconstruction", action="store_true",
                        help="experimental one-kernel FP64 correlation reconstruction; CUDA only")
    parser.add_argument("--nvtx", action="store_true", help="annotate complete scans for Nsight Systems")
    parser.add_argument("--inference-precision", choices=["fp64", "fp32"], default=None)
    parser.add_argument("--inference-implementation", choices=["reference", "graph-fp64"], default=None)
    parser.add_argument("--source-revision", default=None,
                        help="optional external revision identity for remote bundles without .git")
    parser.add_argument("--native-format", choices=["nvfp4", "mxfp4"], default="nvfp4")
    parser.add_argument("--strict-all", action="store_true",
                        help="also run the full FP64 correlation bank using the same declared inference implementation")
    parser.add_argument("--correlation-only", action="store_true",
                        help="measure all block correlations and checksum outputs without receiver inference")
    parser.add_argument("--instrumented-timing", action="store_true",
                        help="add per-block CUDA timing events; resulting wall throughput is instrumentation-affected")
    parser.add_argument("--trim-zero-suffix", action="store_true",
                        help="remove only a verified exact all-candidate zero suffix to permit positive lags")
    parser.add_argument("--cfo-hz", type=float, nargs="+", required=True)
    parser.add_argument("--lag-start", type=int, default=0)
    parser.add_argument("--lag-stop", type=int, default=0)
    parser.add_argument("--lag-step", type=int, default=1)
    parser.add_argument("--sample-rate", type=float, required=True)
    parser.add_argument("--block-samples", type=int, required=True)
    parser.add_argument("--candidate-tile", type=int, default=16)
    parser.add_argument("--frequency-tile", type=int, default=4)
    parser.add_argument("--column-tile", type=int, default=128)
    parser.add_argument("--gram-cache-mib", type=int, default=16)
    parser.add_argument("--receiver-memory-mib", type=int, default=4096,
                        help="explicit state/graph memory bound per declared channel group")
    parser.add_argument("--template-cache-mib", type=int, default=256)
    parser.add_argument("--max-block-output-mib", type=int, default=1024)
    parser.add_argument("--repeats", type=int, default=2,
                        help="same-input timing scans: one cold then repeats warmed")
    parser.add_argument("--repeat-observations", type=int, default=1,
                        help="batch this many exact copies of the sampled capture; copies are not independent trials")
    parser.add_argument("--phase-states", type=int, default=128)
    parser.add_argument("--relative-phase-states", type=int, default=8)
    parser.add_argument("--residual-frequency-bound", type=float, default=.02)
    parser.add_argument("--residual-frequency-std", type=float, default=.003)
    parser.add_argument("--frequency-correlation", type=float, default=1000.)
    parser.add_argument("--phase-diffusion", type=float, default=.00872665)
    parser.add_argument("--attempt-spacing", type=float, default=60.)
    parser.add_argument("--duration-alternatives", type=int, default=1)
    parser.add_argument("--model-alternatives", type=int, default=1)
    parser.add_argument("--save-scores", action="store_true")
    args = parser.parse_args(argv)
    if args.output.exists():
        parser.error("output must not exist")
    if args.save_scores and args.output.with_suffix(".npz").exists():
        parser.error("score archive output must not exist")
    if args.precision == "native-fp4" and args.device == "cpu":
        parser.error("native-fp4 requires an explicitly selected GPU")
    if args.lag_start < 0 or args.lag_stop < args.lag_start or args.lag_step < 1:
        parser.error("invalid lag range")
    if args.lag_stop % args.lag_step != args.lag_start % args.lag_step:
        parser.error("lag endpoint must be reachable by lag-step")
    if args.repeats < 1 or args.repeat_observations < 1 or args.sample_rate <= 0 or args.block_samples < 1:
        parser.error("repeats, sample rate and block size must be positive")
    if min(args.template_cache_mib, args.max_block_output_mib) < 1 or args.gram_cache_mib < 0:
        parser.error("memory bounds must be positive MiB values")
    if min(args.duration_alternatives, args.model_alternatives) < 1 or args.attempt_spacing <= 0:
        parser.error("family-accounting values must be positive")
    if not np.isfinite(args.cfo_hz).all():
        parser.error("CFO grid must be finite")
    return args


def run(args, native_backend=None):
    wall_start = time.perf_counter()
    if str(args.device).startswith("cuda"):
        # This standalone runner owns its CUDA process. Explicitly select strict
        # IEEE FP32 for any downstream FP32 work; the library module itself
        # continues to reject callers that leave reduced products enabled.
        torch.backends.cuda.matmul.allow_tf32 = False
        torch.backends.cudnn.allow_tf32 = False
        torch.set_float32_matmul_precision("highest")
    capture = load_capture(args.capture, trim_zero_suffix=args.trim_zero_suffix)
    if args.precision in ("native-fp4", "diagnostic-fp64") and native_backend is None:
        from triad_fp4_native import NativeFp4Backend
        native_backend = NativeFp4Backend(format=args.native_format)
        if args.precision == "diagnostic-fp64":
            from triad_fp4_representation import Fp4RepresentationDiagnostic
            native_backend = Fp4RepresentationDiagnostic(native_backend, mode=args.representation_mode, output_dtype="fp64")
    if args.inference_implementation is None:
        args.inference_implementation = "graph-fp64" if str(args.device).startswith("cuda") else "reference"
    if args.inference_precision is None:
        args.inference_precision = "fp32" if args.inference_implementation == "graph-fp64" else "fp64"
    if args.inference_implementation == "graph-fp64" and not str(args.device).startswith("cuda"):
        raise ValueError("graph-fp64 inference requires CUDA; CPU reference fallback is prohibited")
    if args.inference_implementation == "graph-fp64" and args.inference_precision != "fp32":
        raise ValueError("graph-fp64 inference requires FP32 bulk state with FP64 reductions")
    if args.correlation_only:
        inference_implementation, inference_precision = "none", "none"
    else:
        inference_implementation, inference_precision = (args.inference_implementation,
                                                          args.inference_precision)
    # The exporter already generated the keystream/template bank and sampled
    # IQ. Hash/load time is measured here; manifest simulation and template
    # preparation are reported separately and never called receiver DSP time.
    input_hashes = {path.name: sha256(path) for path in capture["paths"]}
    input_generation = generation_provenance(capture["directory"], input_hashes)
    load_seconds = time.perf_counter() - wall_start
    runs, scores, verification = score_capture(capture, args, args.precision, native_backend,
                                               inference_precision, args.repeats, args.strict_all,
                                               inference_implementation=inference_implementation,
                                               correlation_only=args.correlation_only)
    manifest = capture["manifest"]
    simulation = dict(sampled_observation_wall_seconds=manifest.get("sampled_observation_wall_seconds"),
                      basis_generation_and_write_wall_seconds=manifest.get("basis_generation_and_write_wall_seconds"))
    hypotheses = len(args.cfo_hz) * len(capture["labels"]) * len(
        range(args.lag_start, args.lag_stop + 1, args.lag_step))
    default_gate = _threshold(1e-4, 86400., args.attempt_spacing,
                              args.duration_alternatives, args.model_alternatives, hypotheses)
    relaxed_gate = _threshold(.01, 43200., args.attempt_spacing,
                              args.duration_alternatives, args.model_alternatives, hypotheses)
    truth = capture["manifest"].get("truth", {})
    control = manifest.get("control", "signal")
    if manifest.get("signal_enabled") is False or control in ("null", "wrong-key"):
        truth = {}
    elif not truth and manifest.get("signal_enabled"):
        truth = {"bit": manifest.get("signal_bit_simulation_truth")}
    truth_bit = truth.get("bit") if isinstance(truth, dict) else None
    if truth_bit not in (0, 1):
        truth_bit = None
    decisions = []
    for gate_name, gate in (("default_99pct_1e-4_day", default_gate),
                            ("relaxed_90pct_1pct_12h", relaxed_gate)):
        threshold = gate["log_likelihood_threshold"]
        for row in runs:
            if args.correlation_only:
                continue
            bit_scores = np.asarray(row["bit_scores"][:1])
            best = bit_scores.argmax(axis=1)
            accepted = bit_scores.max(axis=1) >= threshold
            decisions.append(dict(gate=gate_name, threshold=threshold,
                                  run_index=row["run_index"], timing_scan=row["timing_scan"],
                                  correlation_precision=row["precision"],
                                  inference_implementation=row["inference_implementation"],
                                  inference_precision=row["inference_precision"],
                                  admitted=int(np.sum(accepted)),
                                  noise_admitted=int(np.sum(accepted)) if control == "null" or manifest.get("signal_enabled") is False else None,
                                  wrong_key_admitted=int(np.sum(accepted)) if control == "wrong-key" else None,
                                  control=control,
                                  correct_accepted=None if truth_bit is None else int(np.sum(accepted & (best == truth_bit))),
                                  wrong_accepted=None if truth_bit is None else int(np.sum(accepted & (best != truth_bit))),
                                  missed=None if truth_bit is None else int(np.sum(~accepted)),
                                  trial_denominator=1,
                                  timing_copies=row["timing_observation_copies"],
                                  classification="one physical capture; duplicate columns only time batching; no operational calibration claim"))
    if args.strict_all and not args.correlation_only and args.precision != "fp64":
        reference_run = next(row for row in reversed(runs) if row["precision"] == "fp64")
        final_decisions = [row for row in decisions if row["run_index"] == reference_run["run_index"]]
        approximate_diagnostics = [row for row in decisions if row["run_index"] != reference_run["run_index"]]
    else:
        final_decisions, approximate_diagnostics = decisions, []
    repository_root = Path(__file__).resolve().parents[1]
    revision = _source_revision(repository_root, getattr(args, "source_revision", None))
    source_names = ["triad-fp4-campaign.py", "triad_exhaustive_correlation.py",
                    "triad_correlation_fusion.py",
                    "triad_phase_frequency.py", "triad_phase_optimized.py",
                    "triad_phase_reduction.py", "triad-long.cpp"]
    if Path(__file__).with_name("triad_fp4_native.py").exists():
        source_names.append("triad_fp4_native.py")
    if Path(__file__).with_name("triad_fp4_representation.py").exists():
        source_names.append("triad_fp4_representation.py")
    output = dict(format="triad-fp4-campaign-v1", source_revision=revision,
                  configuration={key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()},
                  source_hashes={name: sha256(Path(__file__).with_name(name))
                                 for name in source_names
                                 if Path(__file__).with_name(name).exists()},
                  environment=dict(python=platform.python_version(), numpy=np.__version__, torch=torch.__version__,
                                  cuda=torch.version.cuda, device=args.device,
                                  cuda_tf32_allowed=torch.backends.cuda.matmul.allow_tf32,
                                  float32_matmul_precision=torch.get_float32_matmul_precision(),
                                  gpu=torch.cuda.get_device_name(args.device) if str(args.device).startswith("cuda") else "CPU"),
                  input=str(capture["directory"]), input_hashes=input_hashes,
                  input_generation=input_generation,
                  sampled_manifest=capture["manifest"],
                  search=dict(candidates=len(capture["labels"]), frequencies_hz=list(args.cfo_hz),
                              lags_samples=list(range(args.lag_start,args.lag_stop+1,args.lag_step)),
                              blocks=math.ceil(capture["basis"].shape[1]/args.block_samples),
                              original_template_samples=capture["original_template_samples"],
                              retained_template_samples=capture["template_samples"],
                              verified_exact_zero_suffix_samples=capture["trimmed_template_samples"],
                              hypotheses_per_capture=hypotheses, no_pruning=True),
                  timing=dict(exported_simulation_and_preparation_seconds=simulation,
                              load_hash_seconds=load_seconds,
                              campaign_pre_output_wall_seconds=time.perf_counter()-wall_start),
                  thresholds=dict(default=default_gate, relaxed=relaxed_gate),
                  precision=dict(correlation=args.precision, inference=inference_precision,
                                 inference_implementation=inference_implementation,
                                 final_decision=dict(correlation="FP64" if args.strict_all and args.precision != "fp64"
                                                     else args.precision,
                                                     inference=inference_implementation,
                                                     inference_precision=inference_precision),
                                 admission_calibration=("not established for quantized sampled observations"
                                     if args.precision == "native-fp4" else "not established by this campaign")),
                  decisions=dict(final=final_decisions, approximate_diagnostics=approximate_diagnostics),
                  scans=runs,
                  strict_reference_verification=verification)
    if args.precision == "diagnostic-fp64":
        output["representation_diagnostics"] = native_backend.diagnostics()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.save_scores:
        score_path = args.output.with_suffix(".npz")
        arrays = {f"score_{index}": np.asarray(row["score_matrix"])
                  for index, row in enumerate(runs)}
        np.savez_compressed(score_path, **arrays)
        output["score_file"] = score_path.name
        output["score_sha256"] = sha256(score_path)
        for row in runs:
            row["score_matrix"] = None
    else:
        for row in runs:
            row["score_matrix"] = None
    output["timing"]["artifact_write_seconds"] = None
    output["timing"]["within_process_seconds"] = None
    output["timing"]["timing_scope"] = "run entry after imports through first JSON write; external parent wall required for complete process"
    write_start = time.perf_counter()
    args.output.write_text(json.dumps(output, indent=2, allow_nan=False) + "\n")
    output["timing"]["artifact_write_seconds"] = time.perf_counter() - write_start
    output["timing"]["within_process_seconds"] = time.perf_counter() - wall_start
    args.output.write_text(json.dumps(output, indent=2, allow_nan=False) + "\n")
    return output


def main(argv=None):
    args = parse_args(argv)
    run(args)


if __name__ == "__main__":
    main()
