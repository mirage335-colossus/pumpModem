"""Configuration-driven, bounded Robust simulation and capacity estimates.

The editable source is read on every invocation. CPU service is expressed only
as prepared processing seconds per received baseband second. Measurements of
public subwindows never certify whole messages or an integrated receiver.
"""
from __future__ import annotations

import copy
import hashlib
import json
import math
import statistics
from pathlib import Path
import subprocess
import sys
import time

from triad_cases import DEFAULT_CASES, ROOT, load_cases
from triad_case_metrics import BATCHES, LOOKS, arithmetic_interval, machine_resources, success_interval, summarize_service, workspace_summary

SCHEMA = "triad-manual-cases-v1"


def identity(executable):
    paths = sorted(set([*ROOT.glob("tools/triad*.*"), *ROOT.glob("src/triad*.*"),
                        *ROOT.glob("include/datapump/triad*.*"),
                        ROOT / "src/compression_short.cpp", ROOT / "src/tuning_presets.cpp",
                        ROOT / "src/helper_carrier.cpp"]))
    return {
        "revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "dirty_paths": subprocess.check_output(["git", "status", "--short"], cwd=ROOT, text=True).splitlines(),
        "sources": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in paths if p.is_file()},
        "native_binary": str(executable),
        "native_binary_sha256": hashlib.sha256(Path(executable).read_bytes()).hexdigest(),
    }


def native_plan(executable, native_args):
    command = [str(executable), *native_args, "--seconds", ".001", "--sample-rate", "6000", "--data-if", "1725",
               "--epoch", "1793850000", "--key-seed", "72001", "--short-message", "fox", "--plan-only"]
    return json.loads(subprocess.check_output(command, text=True, timeout=30))


def target_snr(plan):
    """Invert the existing explicitly provisional true-atom duration model.

    This selects a target for future complete-bit trials, not a simulated
    reception threshold. Acquisition/Helper/wrong-bit allocations remain open.
    """
    from triad_duration_model import plan_from_native
    if not plan.get("geometry_feasible"):
        return {"status": "unavailable", "reason": "native geometry infeasible"}
    duration = float(plan["geometry"]["active_data_seconds"])
    bits = int(plan["short_message"]["encoded_bits"])
    requested = .91

    def prediction(cn0, message_bits):
        return plan_from_native(plan, cn0, correct_probability=requested,
                                message_bits=message_bits, manual_seconds=duration,
                                max_active_seconds=max(duration * 2, 1), max_evaluations=4)

    estimates = {}
    for count, name in ((1, "single_bit"), (bits, "fox")):
        low, high = -100., 100.
        upper = prediction(high, count)
        if not (upper.get("selected") or {}).get("meets_detection_allocation", False):
            return {"status": "unavailable", "reason": "duration model target exceeds declared SNR bracket",
                    "searched_bracket_db_hz": [low, high], "source_bits": count}
        for _ in range(24):
            mid = (low + high) / 2
            estimate = prediction(mid, count)
            if not estimate.get("selected"):
                return {"status": "unavailable", "reason": "duration model did not produce a selected duration",
                        "searched_bracket_db_hz": [low, high], "source_bits": count}
            if estimate["selected"]["meets_detection_allocation"]:
                high = mid
            else:
                low = mid
        estimate = prediction(high, count)
        estimates[name] = {"target_cn0_db_hz": high, "bracket_db_hz": [low, high],
                           "source_bits": count,
                           "modeled_true_atom_detection_lower_bound": estimate["selected"]["modeled_detection_lower_bound"],
                           "unmodeled_effects": estimate["unmodeled_selected_effects"],
                           "assumptions": estimate["assumptions"]}
    return {"status": "provisional_model_target", "requested_message_success_probability": requested,
            "whole_bit_or_message_probability_demonstrated": False,
            "scope": "Candidate target from retained duration model; acquisition retention, Helper and wrong-bit errors are not qualified. Not a measured >90% threshold.",
            "data_duration_per_bit_seconds": duration, **estimates}


def suite_policy(case_count, measurement_seconds):
    return {
        "coverage": "Every current manual entry; identical normalized configurations reuse one explicitly identified measurement.",
        "strata": "Selected native waveform, channel, encryption, Helper, oscillator, spread, selective amplitude and synthetic FHSS; public acquisition and subsequent tracking subwindows.",
        "looks": list(LOOKS), "batch_units": list(BATCHES),
        "maximum_measurements_per_unique_case": LOOKS[-1] * len(BATCHES),
        "maximum_baseband_units_per_unique_case": LOOKS[-1] * sum(BATCHES),
        "unit_baseband_seconds": measurement_seconds, "family_size": max(1, case_count * 2),
        "family_alpha": .05, "maximum_holdout_error_fraction": .30,
        "relative_precision": .15,
        "stopping": "First declared look resolving service decision with supported holdout; point >=1 fails immediately at a look; otherwise stop at9. No heavy fallback.",
        "cpu_metric": "Prepared single-stream elapsed busy-service seconds per second of received baseband. No sum of thread seconds, core-count division, or independent-trial throughput substitution.",
        "utilization": "Process CPU counters are internal utilization diagnostics only. Lack of allocation saturation limits a machine CPU-ceiling claim; measured single-stream service remains visible.",
        "workspace": "Peak live receiver/native workspace estimate plus observed process peak when available, compared with50% of current OS/cgroup available RAM; not an allocation certificate.",
        "success": "Repeated fixed-input timing samples are not independent reception trials. Whole-bit/message success remains unresolved until measured with appropriate independent seeds and full public search.",
        "preparation_warning_seconds": 5,
        "forecast_limit": "Only this fixed geometry and recorded state regime; no projection across sample rate, search bank, changing geometry or unmeasured receiver stages.",
    }


def measure_prepared(prepared, *, family_size):
    rows = {name: [] for name in ("acquisition", "tracking")}
    summaries, representative, utilizations = {}, {}, {name: [] for name in rows}
    coverage = {name: {"invalid_samples": 0, "workspace_bytes": 0,
                       "rss_sampled_peak_sum_process_bytes": 0} for name in rows}
    outcomes = []
    for look in LOOKS:
        while len(outcomes) < look:
            row = {name: [None] * len(BATCHES) for name in rows}
            for index in ((len(outcomes) + offset) % len(BATCHES) for offset in range(len(BATCHES))):
                sample = prepared.measure(units=BATCHES[index])
                for name, stage in sample["stages"].items():
                    if name not in rows:
                        continue
                    representative[name] = stage
                    if (stage.get("status") not in ("ok", "complete", "scored", "proposed") or
                            stage.get("representative_work") is False):
                        coverage[name]["invalid_samples"] += 1
                    for key in ("workspace_bytes", "rss_sampled_peak_sum_process_bytes"):
                        coverage[name][key] = max(coverage[name][key], stage.get(key) or 0)
                    ratio = stage.get("ratio_processing_seconds_per_baseband_second")
                    if isinstance(ratio, (float, int)) and math.isfinite(ratio) and ratio > 0:
                        row[name][index] = ratio
                    utilization = stage.get("observed_cpu_utilization_fraction")
                    if utilization is not None:
                        utilizations[name].append(utilization)
                if index == 0:
                    outcome = sample.get("outcome", {})
            for name in rows:
                rows[name].append(row[name])
            outcomes.append(outcome)
        for name in rows:
            if all(all(value is not None for value in row) for row in rows[name]):
                if name == "acquisition":
                    point, interval = arithmetic_interval([statistics.fmean(row) for row in rows[name]],
                        alpha=.05 / (family_size * len(LOOKS) * 3))
                    status = "fail" if point >= 1 else "pass" if interval[1] < 1 else "unresolved"
                    summaries[name] = dict(outer_trials=len(rows[name]),
                        ratio_processing_seconds_per_baseband_second=point,
                        ratio_processing_seconds_per_baseband_second_interval=interval,
                        margin_fraction=1-point, margin_interval=[1-interval[1], 1-interval[0]],
                        realtime_status=status, stop=status != "unresolved" or look == LOOKS[-1],
                        model_status="fixed_observation_no_duration_extrapolation",
                        interval_assumption="Conditional Student-t arithmetic mean interval; IID approximately normal outer timing samples with unchanged geometry.",
                        scope="Repeated fixed public acquisition attempt; units do not change its geometry. No variable-duration holdout claim.")
                else:
                    summaries[name] = summarize_service(rows[name], family_size=family_size)
            else:
                summaries[name] = {"realtime_status": "unresolved", "stop": True,
                    "reason": representative.get(name, {}).get("reason", "stage did not produce complete timing observations")}
        if all(summary["stop"] for summary in summaries.values()):
            break
    for name, summary in summaries.items():
        summary["stage_coverage"] = dict(representative.get(name, {}), **coverage[name])
        summary["stage_coverage"]["peak_scope"] = "Maximum observed across every included batch and look; validity also covers every sample. Other diagnostic fields describe the last sample."
        if coverage[name]["invalid_samples"] or name not in representative:
            summary["service_only_realtime_status"] = summary["realtime_status"]
            summary["realtime_status"] = "unresolved"
        summary["observed_cpu_utilization_fraction_range"] = (
            [min(utilizations[name]), max(utilizations[name])] if utilizations[name] else None)
        summary["allocation_high_utilization_observed"] = bool(utilizations[name] and min(utilizations[name]) >= .8)
        summary["whole_receiver_cpu_ceiling_demonstrated"] = False
    return {"stages": summaries, "fixed_input_diagnostics": outcomes,
            "independent_success_trials": 0,
            "whole_message_realtime_status": "unresolved",
            "whole_message_scope": "Two staged public subwindow measurements, not an integrated complete receiver."}


def write_result(path, result):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n")
    temporary.replace(path)


def measure_projection_service(screen, *, threads, family_size):
    """Abbreviate the same streaming FIR at its actual fixed chunk geometry."""
    import numpy as np
    from triad_realtime_fft import _fir_decimate_chunks
    from triad_case_receiver import _timing
    chunk = screen.template_chunk_samples
    chunks_per_unit = 4  # Amortize finite startup/tail work without a long run.
    rng = np.random.default_rng(83001)
    values = (rng.standard_normal(chunk) + 1j*rng.standard_normal(chunk))/math.sqrt(2)
    rows, utilization = [], []

    def measure(units):
        wall, cpu = time.perf_counter(), time.process_time()
        count = units*chunks_per_unit
        projected = _fir_decimate_chunks((values for _ in range(count)),
            screen.taps, screen.decimation, count*chunk, screen.dtype, chunk)
        timing = _timing(wall, cpu, count*chunk/screen.fs, threads)
        if not np.isfinite(projected).all():
            raise ValueError("streaming projection produced nonfinite simulated samples")
        return timing

    measure(1)  # Excluded numerical-library warmup, not a reception trial.
    for look in LOOKS:
        while len(rows) < look:
            row = []
            for units in BATCHES:
                sample = measure(units)
                row.append(sample["ratio_processing_seconds_per_baseband_second"])
                utilization.append(sample["observed_cpu_utilization_fraction"])
            rows.append(row)
        summary = summarize_service(rows, family_size=family_size)
        if summary["stop"]:
            break
    summary.update(observed_cpu_utilization_fraction_range=[min(utilization), max(utilization)],
        allocation_high_utilization_observed=min(utilization) >= .8,
        chunk_samples=chunk, chunks_per_unit=chunks_per_unit,
        source="Fixed proper-complex synthetic input; exact same canonical FIR/taps/decimation/chunk algorithm. Computation does not select branches by sample values.",
        scope="Measured brief streaming-filter service with complete finite tails; extrapolation to the full bit is conditional on the supported1/2/4 chunk holdout and unchanged geometry. Native simulation and independent-trial throughput are excluded.")
    if summary['model_status'] == 'unresolved':
        direct = measure_finite_projection_service(screen, threads=threads, family_size=family_size)
        if direct['model_status'] == 'fixed_extent_measured_no_extrapolation':
            direct['abbreviated_attempt'] = summary
            return direct
        summary['finite_extent_attempt'] = direct
    return summary


def measure_finite_projection_service(screen, *, threads, family_size):
    """One cheap fixed-extent FIR check, never a full channel/receiver fallback."""
    import numpy as np
    from triad_realtime_fft import _fir_decimate_chunks
    from triad_case_receiver import _timing
    samples, chunk = screen.samples, screen.template_chunk_samples
    result = dict(model_status='unresolved', raw_samples=samples, maximum_raw_samples=4_000_000,
        maximum_elapsed_seconds=5, planned_repeats=3,
        scope='Same canonical FIR, taps, dtype, chunking and complete finite tail at the actual input extent; fixed synthetic values do not change this sample-independent algorithm. One excluded warmup, exactly three repeats, no native/channel/FFT-search campaign or duration extrapolation. Elapsed cap checked between invocations.')
    if not 0 < samples <= result['maximum_raw_samples']:
        return dict(result, reason='finite FIR extent exceeds bounded raw-sample cap')
    workspace = int(32*math.ceil((samples+len(screen.taps))/screen.decimation)+
        256*(chunk+len(screen.taps)))
    result['estimated_extra_workspace_bytes'] = workspace
    if workspace > screen.memory_limit_bytes-screen.resident_estimate:
        return dict(result, reason='finite FIR workspace exceeds remaining memory budget')
    rng = np.random.default_rng(83001)
    values = (rng.normal(size=chunk)+1j*rng.normal(size=chunk))/math.sqrt(2)
    result['geometry'] = dict(sample_rate_hz=screen.fs, downsample=screen.decimation,
        chunk_samples=chunk, dtype=np.dtype(screen.dtype).str,
        fir_taps_count=len(screen.taps), fir_taps_sha256=hashlib.sha256(screen.taps.tobytes()).hexdigest())

    def once():
        wall, cpu = time.perf_counter(), time.process_time()
        output = _fir_decimate_chunks((values[:min(chunk, samples-first)]
            for first in range(0, samples, chunk)), screen.taps, screen.decimation,
            samples, screen.dtype, chunk)
        measurement = _timing(wall, cpu, samples/screen.fs, threads)
        if not np.isfinite(output).all():
            raise ValueError('finite FIR produced nonfinite simulated samples')
        return measurement

    began = time.perf_counter()
    once()
    observations = []
    for _ in range(3):
        if time.perf_counter()-began > 5:
            return dict(result, reason='finite FIR elapsed budget exhausted', observations=observations)
        observations.append(once())
    if time.perf_counter()-began > 5:
        return dict(result, reason='finite FIR elapsed budget exhausted', observations=observations)
    ratios = [r['ratio_processing_seconds_per_baseband_second'] for r in observations]
    point, interval = arithmetic_interval(ratios, alpha=.05/(family_size*6))
    utilization = [r['observed_cpu_utilization_fraction'] for r in observations]
    return dict(result, model_status='fixed_extent_measured_no_extrapolation', outer_trials=3,
        ratio_processing_seconds_per_baseband_second=point,
        ratio_processing_seconds_per_baseband_second_interval=interval,
        margin_fraction=1-point, margin_interval=[1-interval[1], 1-interval[0]],
        realtime_status='fail' if point >= 1 else 'pass' if interval[1] < 1 else 'unresolved',
        observations=observations, allocation_high_utilization_observed=min(utilization) >= .8,
        observed_cpu_utilization_fraction_range=[min(utilization), max(utilization)],
        interval_assumption='Conditional fixed-point arithmetic-mean Student-t interval from three fresh prepared repeats, alpha=.05/(family_size*6); no distribution-free claim or pooling with the unresolved abbreviated attempt.')


def coarse_capacity(prepared, pilot, nominal, *, family_size):
    """Conservative sequential filter + worst-bit nominal scoring estimate."""
    if not hasattr(prepared, "screen") or nominal is None:
        return None
    row = next((row for row in pilot if row["cn0_db_hz"] == nominal), None)
    if row is None:
        return None
    filtering = measure_projection_service(prepared.screen, threads=prepared.threads,
        family_size=family_size)
    scoring, utilization = [], []
    for stratum in row["strata"]:
        outcomes = stratum["outcomes"]
        point, interval = arithmetic_interval(
            [trial["ratio_processing_seconds_per_baseband_second"] for trial in outcomes],
            alpha=.05/(family_size*2))
        scoring.append(dict(bit=stratum["bit"], ratio_processing_seconds_per_baseband_second=point,
            ratio_processing_seconds_per_baseband_second_interval=interval))
        utilization.extend(trial["observed_cpu_utilization_fraction"] for trial in outcomes)
    point = filtering["ratio_processing_seconds_per_baseband_second"] + max(
        row["ratio_processing_seconds_per_baseband_second"] for row in scoring)
    interval = [filtering["ratio_processing_seconds_per_baseband_second_interval"][i] + max(
        row["ratio_processing_seconds_per_baseband_second_interval"][i] for row in scoring) for i in (0, 1)]
    qualified = filtering["model_status"] in ("abbreviated_holdout_supported", "fixed_extent_measured_no_extrapolation")
    complete = all(trial.get("candidate_status") == "proposed"
        for stratum in row["strata"] for trial in stratum["outcomes"])
    status = "fail" if point >= 1 else "pass" if qualified and interval[1] < 1 else "unresolved"
    if not complete:
        status = "unresolved"
    high_utilization = filtering["allocation_high_utilization_observed"] and min(utilization) >= .8
    if not high_utilization:
        status = "unresolved"
    return dict(scope="Estimated complete coarse procedure: same streaming DATA FIR plus worst-bit full public Helper/FFT service at the configured nominal SNR. Sequential-stage sum; includes all coarse receiver stages, not PracticalTracking, framing, operational admission, GUI, or deployment.",
        filtering=filtering, scoring=scoring,
        ratio_processing_seconds_per_baseband_second=point,
        ratio_processing_seconds_per_baseband_second_interval=interval,
        margin_fraction=1-point, margin_interval=[1-interval[1], 1-interval[0]],
        realtime_status=status,
        allocation_high_utilization_observed=high_utilization,
        observed_scoring_cpu_utilization_fraction_range=[min(utilization), max(utilization)],
        interval_assumption="Conditional arithmetic-mean t intervals for fixed per-bit geometry, with filter holdout discrepancy retained; no distribution-free or channel-population claim.")


def confirmation_at_point(receive, cn0, *, draws=42, alpha=.0125, noise_seed=82100, progress=None):
    """Independent fixed-point holdout; first error makes this bound unattainable.

    With42 draws and alpha=.0125 (or36/.025), even one failure prevents a
    lower bound above.9. Early futility reports no optional-stop interval.
    """
    if (draws, alpha) not in ((42, .0125), (36, .025)):
        raise ValueError("confirmation requires a declared fixed budget/tail pair")
    result = dict(cn0_db_hz=cn0, draws_per_bit=draws, tail_alpha=alpha, strata=[], supported=False)
    for bit in (0, 1):
        trials = []
        for j in range(draws):
            row = receive(bit, cn0, noise_seed+j)
            if row.get("status") == "unsupported":
                result.update(reason=row["reason"], unsupported_trial=row)
                return result
            trials.append(row)
            if progress and ((j+1) % 8 == 0 or j+1 == draws or not row['correct_bit']):
                progress(f"confirmation {cn0:.3f} dB-Hz, bit {bit}: {j+1}/{draws} draws")
            if not row["correct_bit"]:
                break
        successes = sum(row["correct_bit"] for row in trials)
        complete = len(trials) == draws
        result["strata"].append(dict(bit=bit, successes=successes, trials=len(trials),
            complete_fixed_budget=complete, probability_estimate=successes/len(trials),
            exact_interval=success_interval(successes, len(trials), alpha=alpha) if complete else None,
            outcomes=trials))
    result["supported"] = all(row["complete_fixed_budget"] and row["exact_interval"][0] > .9
        for row in result["strata"])
    return result


def paired_confirmation_at_point(receive, cn0, *, noise_seed=87100, progress=None):
    """One lower-confidence point:22 independent AND outcomes, not44 trials.

    Each pair uses the same fresh noise seed for bits0/1. A successful pair
    requires BOTH correct. The paired-event probability bounds both marginal
    bit probabilities, but is not the success probability of a real message.
    """
    if isinstance(cn0, bool) or not isinstance(cn0, (float, int)) or not math.isfinite(cn0):
        raise ValueError('paired confirmation requires a fixed finite C/N0')
    pairs = []
    result = dict(cn0_db_hz=cn0, planned_pairs=22, tail_alpha=.1,
        one_sided_confidence=.9, maximum_receive_calls=44, pairs=pairs,
        scope='One fixed point, channel and coarse procedure; independent common-noise pairs, each successful only if BOTH bits are correct. Lower bound on the paired event also bounds each bit marginal. Not a message-success or standard95% suite claim.',
        stopping='First failed pair or22 pairs; no second candidate or extension.')
    for j in range(22):
        outcomes = []
        for bit in (0, 1):
            trial = receive(bit, cn0, noise_seed+j)
            outcomes.append(trial)
            if not trial.get('correct_bit', False):
                break
        success = len(outcomes) == 2 and all(row['correct_bit'] for row in outcomes)
        pairs.append(dict(noise_seed=noise_seed+j, both_bits_correct=success, outcomes=outcomes))
        if progress:
            progress(f"paired point {cn0:.3f} dB-Hz: {j+1}/22 pairs, {'both correct' if success else 'failed'}")
        if not success:
            break
    successes = sum(row['both_bits_correct'] for row in pairs)
    complete = len(pairs) == 22
    lower = success_interval(successes, len(pairs), alpha=.1)[0] if complete else None
    result.update(successful_pairs=successes, observed_pairs=len(pairs),
        complete_fixed_budget=complete, exact_one_sided_lower_bound=lower,
        supported=bool(complete and lower > .9))
    return result


def measure_sensitivity(prepared, target, *, family_size=1, progress=None, paired_followup=False):
    """Fixed paired-noise pilot, then independent conditional confirmation.

    Each bit is a separate fixed-channel binomial stratum. Common noise across
    pilot SNRs is paired evidence, never more independent channel realizations.
    """
    policy = dict(channel_realizations=1, transmitted_bits=[0, 1],
        initial_gate_trials_per_bit=1, pilot_offsets_db=[-6, 0, 6],
        pilot_noise_draws_per_bit=4, confirmation_draws_per_bit=42,
        maximum_confirmation_candidates=2, confirmation_early_futility_failures=1,
        maximum_forecast_confirmation_receiver_service_seconds_per_candidate=600,
        optional_paired_followup=bool(paired_followup),
        paired_followup_policy='Opt-in only when standard confirmation exceeds600s and44-receive paired forecast fits600s. One lowest eligible pilot point,22 fresh independent common-noise pairs,90% one-sided paired-event bound, first failed pair stops. Standard95% result remains unresolved; no second point.',
        maximum_receive_calls=202,
        success="Unique highest coarse full-bit score has the transmitted bit; invalid, empty or tied decisions fail.",
        probability_scope="Conditional on a transmission, this single native channel seed, and this coarse receiver procedure. Not an operational admission or physical channel population guarantee.",
        interval="Confirmation exact lower bounds each98.75% one-sided; Bonferroni covers both bits at both predeclared candidate rounds jointly at least95%. No cross-case simultaneous claim. Pilot intervals are exploratory95% equal-tail.",
        stopping="Initial gate: noiseless without Helper; with Helper use finite noise at max(nominal, model+18dB), avoiding a zero-noise level-estimator degeneracy. Then fixed model±6/0 plus nominal pilot(at most4 points). Forecast one84-receive confirmation from the largest observed gate/pilot receiver service; if above600s retain a provisional model/pilot and stop. Otherwise confirm at most the two lowest pilot SNRs with4/4 each bit, on fresh noise per round. Stop each confirmation bit at its first failure or42 draws. Stop after the first supported point; no campaign extension.")
    result = dict(status="unresolved", policy=policy, preparation=prepared.preparation,
        coverage=prepared.coverage, workspace=prepared.workspace,
        pilot=[], confirmation=[], earlier_confirmation_rounds=[], approximate_target_cn0_db_hz=None)
    from numpy.linalg import LinAlgError
    def receive(bit, cn0, seed):
        try:
            return prepared.receive(bit, cn0, noise_seed=seed)
        except (ValueError, RuntimeError, MemoryError, EOFError, OSError, LinAlgError, subprocess.SubprocessError) as error:
            return dict(status="unsupported", correct_bit=False, decision_bit=None, reason=str(error))

    nominal = getattr(prepared, "plan", {}).get("physics", {}).get("physical_data_cn0_db_hz")
    center = target.get("single_bit", {}).get("target_cn0_db_hz")
    gate_cn0 = (max(nominal, center+18) if prepared.coverage.get("helper_enabled") and
                nominal is not None and center is not None else None)
    policy["initial_gate_cn0_db_hz"] = gate_cn0
    gate = []
    for bit in (0, 1):
        gate.append(receive(bit, gate_cn0, 81101))
        if gate[-1].get("status") == "unsupported":
            break
    result["noiseless_gate" if gate_cn0 is None else "high_snr_gate"] = gate
    if not all(row["correct_bit"] for row in gate):
        incomplete = any(row.get("status") == "unsupported" or
            row.get("candidate_status") not in (None, "proposed") for row in gate)
        result.update(status="unresolved" if incomplete else "failed_noiseless_gate" if gate_cn0 is None else "failed_high_snr_gate",
            reason=("Complete public initial search was not available: " +
                    "; ".join(str(row.get("reason") or row.get("candidate_reason")) for row in gate)
                    if incomplete else "This coarse procedure cannot identify both bits at the declared initial gate on the sampled channel."))
        return result
    if target.get("status") != "provisional_model_target":
        result["reason"] = "No finite provisional target for the bounded pilot bracket."
        return result
    center = float(target["single_bit"]["target_cn0_db_hz"])
    snrs = [center + offset for offset in policy["pilot_offsets_db"]]
    if nominal is not None and nominal not in snrs:
        snrs.append(float(nominal))
    policy["pilot_cn0_db_hz"] = sorted(snrs)
    result["configured_nominal_cn0_db_hz"] = nominal
    eligible = []
    for snr in sorted(snrs):
        if progress:
            progress(f"pilot {snr:.3f} dB-Hz: four draws per bit")
        strata = []
        for bit in (0, 1):
            trials = []
            for j in range(4):
                trials.append(receive(bit, snr, 81200+j))
                if trials[-1].get("status") == "unsupported":
                    break
            unsupported = next((row for row in trials if row.get("status") == "unsupported"), None)
            if unsupported:
                result.update(reason=unsupported["reason"], unsupported_trial=unsupported)
                return result
            successes = sum(row["correct_bit"] for row in trials)
            strata.append(dict(bit=bit, successes=successes, trials=len(trials),
                probability_estimate=successes/len(trials),
                exact_interval=success_interval(successes, len(trials), alpha=.025), outcomes=trials))
        result["pilot"].append(dict(cn0_db_hz=snr, strata=strata))
        if all(row["successes"] == 4 for row in strata):
            eligible.append(snr)
    result["coarse_receiver_capacity"] = coarse_capacity(prepared, result["pilot"], nominal,
        family_size=family_size)
    if not eligible:
        result["reason"] = "No tested SNR passed the tiny paired-noise pilot; threshold unresolved within declared bracket."
        return result
    timings = [row.get('ratio_processing_seconds_per_baseband_second', 0)*row.get('represented_baseband_seconds', 0)
        for row in gate + [trial for point in result['pilot'] for stratum in point['strata'] for trial in stratum['outcomes']]]
    forecast = 84*max(timings, default=0)
    result['confirmation_cost_admission'] = dict(forecast_receiver_service_seconds=forecast,
        budget_seconds=600, scope='Largest observed same-geometry gate/pilot service times84; a conditional cost forecast, not a runtime guarantee. Simulation work is separate.')
    if forecast > 600:
        result.update(provisional_pilot_target_cn0_db_hz=eligible[0],
            reason='Standard95% confirmation receiver-service forecast exceeds the declared600s per-candidate budget; its target remains unresolved. The model and small pilot alone do not establish >90% success.')
        paired_forecast = 44*max(timings, default=0)
        result['paired_confirmation_forecast_test_seconds'] = paired_forecast
        if paired_followup and paired_forecast <= 600:
            result['paired_point_confirmation'] = paired_confirmation_at_point(receive, eligible[0], progress=progress)
        return result
    for index, selected in enumerate(eligible[:2]):
        if result["confirmation"]:
            result["earlier_confirmation_rounds"].append(dict(
                cn0_db_hz=result["tested_candidate_cn0_db_hz"], strata=result["confirmation"]))
        holdout = confirmation_at_point(receive, selected, noise_seed=82100+1000*index, progress=progress)
        result["tested_candidate_cn0_db_hz"] = selected
        result["confirmation"] = holdout["strata"]
        if holdout.get("unsupported_trial"):
            result.update(reason=holdout["reason"], unsupported_trial=holdout["unsupported_trial"])
            return result
        if holdout["supported"]:
            result.update(status="conditional_coarse_target_supported", approximate_target_cn0_db_hz=selected)
            return result
    result["reason"] = "Neither predeclared candidate established >90% for each bit within the fixed noise budget."
    return result


def render_report(result):
    def number(value, digits=3):
        return "—" if value is None else f"{value:.{digits}g}"

    lines = ["# Robust manual-case abbreviated simulation", "",
             f"Input SHA256: `{result['input']['source_sha256']}`.",
             f"CPU: {result['machine']['cpu_model']}. Receiver CPU units: processing seconds / received baseband second.",
             "Positive margins describe the measured stage only. A ratio of 1 or greater fails realtime. Preparation is separate.", "",
             "| Case | Nominal link-budget DATA / Helper C/N0 (dB-Hz) | CPU coarse / tracking probe (s/s) | Estimated coarse-test DSP headroom | Bit / fox transmit seconds |",
             "| --- | --- | --- | --- | --- |"]
    if not (result['input'].get('unchanged_at_completion', False) and
            result['provenance'].get('unchanged_at_completion', False)):
        lines[2:2] = ["**INVALIDATED: input, source, or binary changed during this run, or completion identity was not checked. These are not current-source results.**", ""]
    if result.get("postprocessing"):
        lines[2:2] = ["This report preserves the original measurement identities and labels later follow-up measurements/accounting separately. Rendering changes do not relabel the original measurements as runs of later source.", ""]
    if result.get("selection", {}).get("complete_current_list") is False:
        lines[2:2] = ["**Partial rerun:** current-file rows " +
            ", ".join(map(str, result["selection"]["indices"])) + "; other rows are not covered by this run.", ""]
    for item in result["cases"]:
        plan = item.get("native_plan", {})
        p = plan.get("physics", {})
        helper = p.get('effective_helper_cn0_db_hz')
        snr = f"{number(p.get('physical_data_cn0_db_hz'), 5)} / {number(helper, 5) if helper is not None else 'off'}"
        stages = item.get("measurement", {}).get("stages", {})
        sensitivity = item.get('coarse_sensitivity', {})
        coarse = sensitivity.get('coarse_receiver_capacity') or {}
        ratios = " / ".join(f"{stage['ratio_processing_seconds_per_baseband_second']:.4g}"
            if stage.get('ratio_processing_seconds_per_baseband_second') is not None else 'unresolved'
            for stage in (coarse, stages.get('tracking', {})))
        ws = sensitivity.get('workspace_margin', {})
        headroom = f"{ws['headroom_bytes']/2**20:.1f} MiB" if "headroom_bytes" in ws else "unresolved"
        calendar = plan.get("transmit_calendar", {})
        times = f"{number(calendar.get('single_bit_seconds'), 7)} / {number(calendar.get('short_message_seconds'), 7)}"
        lines.append(f"| {item['case']['index']}. {item['case']['heading']} | {snr} | {ratios} | {headroom} | {times} |")
    lines.extend(["", "Coarse and tracking CPU entries are separate measured endpoints, not an integrated receiver total. DSP headroom covers the bounded coarse test's estimated workspace; an unresolved search does not establish full-search capacity.", "", "C/N0 values are nominal received link-budget references in a 1 Hz noise bandwidth, before the sampled fading and selective perturbations; they are not measured realized component powers. Simulated target values use this same nominal injected-noise reference on the selected channel.", "", "Each fox payload is literal `fox`, encoded by the canonical short-message dictionary. Native calendar values include the complete scheduled support and Helper behavior; message reception is not implemented by this test.", ""])
    for item in result["cases"]:
        lines.append(f"## {item['case']['index']}. {item['case']['heading']}")
        lines.append("")
        if "reused_measurement_from" in item:
            lines.append(f"Identical normalized input; reuses `{item['reused_measurement_from']}`.")
        if item.get("measurement_phase"):
            lines.append(f"Measurement phase: `{item['measurement_phase']}`; exact phase identity and policy are retained in JSON.")
        if item.get("error"):
            lines.append("Unresolved: " + item["error"])
        target = item.get("target_snr", {})
        if target.get("status") == "provisional_model_target":
            lines.append(f"Provisional DATA target for >90%: single bit {target['single_bit']['target_cn0_db_hz']:.2f} dB-Hz; fox {target['fox']['target_cn0_db_hz']:.2f} dB-Hz. This is an unqualified model target, not a demonstrated reception threshold.")
        sensitivity = item.get("coarse_sensitivity", {})
        if sensitivity.get("approximate_target_cn0_db_hz") is not None:
            lines.append(f"Simulated coarse correct-bit target: **{sensitivity['approximate_target_cn0_db_hz']:.2f} dB-Hz**, with independent noise confirmation for both bits. This is conditional on one sampled channel and a transmission being present; it does not qualify operational admission or a 22-bit message.")
            physics = item.get('native_plan', {}).get('physics', {})
            if physics.get('effective_helper_cn0_db_hz') is not None:
                helper_target = physics['effective_helper_cn0_db_hz'] + sensitivity['approximate_target_cn0_db_hz'] - physics['physical_data_cn0_db_hz']
                lines.append(f"At that simulated target, Helper C/N0 is {helper_target:.2f} dB-Hz with the configured Helper/DATA power ratio.")
        elif sensitivity:
            lines.append("Simulated coarse correct-bit threshold: **unresolved** — " + sensitivity.get("reason", sensitivity.get("status", "unknown")))
            if sensitivity.get('provisional_pilot_target_cn0_db_hz') is not None:
                lines.append(f"Lowest exploratory pilot point with 4/4 correct for each bit: {sensitivity['provisional_pilot_target_cn0_db_hz']:.2f} dB-Hz. Four draws do not establish a >90% probability bound.")
        followup = item.get("fixed_point_confirmation_followup")
        if followup:
            holdout = followup['confirmation']
            counts = ', '.join(f"bit {s['bit']}: {s['successes']}/{s['trials']}" for s in holdout['strata'])
            lines.append(f"Separate fixed-point follow-up at **{holdout['cn0_db_hz']:.2f} dB-Hz**: {counts}; " +
                ("supports >90% for each bit" if holdout['supported'] else "does not establish >90%") +
                ". This point-specific confidence claim uses fresh noise and its own recorded source/policy; it is not an extra candidate within the earlier search's confidence family.")
        paired = item.get('paired_point_followup', {}).get('confirmation') or sensitivity.get('paired_point_confirmation')
        if paired:
            lines.append(f"Separate lower-confidence fixed point at **{paired['cn0_db_hz']:.2f} dB-Hz**: "
                f"{paired['successful_pairs']}/{paired['observed_pairs']} common-noise pairs had BOTH bits correct. " +
                (f"The **90% one-sided confidence lower bound is {paired['exact_one_sided_lower_bound']:.5f}** for the paired event and therefore each bit marginal." if paired['supported'] else
                 "The bounded paired check did not establish >90% success.") +
                " The declared budget is 22 independent pairs (at most 44 receives), not 44 independent trials or a real-message test. Early futility reports no fixed-budget bound. The standard 95% suite criterion remains unresolved.")
        coarse_prep = sensitivity.get("preparation", {})
        if coarse_prep.get("receiver_template_preparation_seconds") is not None:
            readiness = coarse_prep["receiver_template_preparation_seconds"]
            lines.append(f"Full coarse receiver preparation: **{readiness:.2f} s**" +
                (" — **exceeds the five-second readiness target**." if readiness > 5 else ".") +
                " Includes public template/profile preparation; excludes simulator work. No unavoidable minimum is established. " +
                f"Separate native simulation generation: {number(coarse_prep.get('simulator_channel_generation_seconds'))} s.")
        nominal = sensitivity.get("configured_nominal_cn0_db_hz")
        nominal_pilot = next((row for row in sensitivity.get("pilot", []) if row["cn0_db_hz"] == nominal), None)
        if nominal_pilot:
            counts = ", ".join(f"bit {row['bit']}: {row['successes']}/{row['trials']}" for row in nominal_pilot["strata"])
            lines.append("At the configured nominal SNR, the small paired-noise pilot gave " + counts + ". These are separate bit strata on one channel seed, not a message-success certificate.")
        capacity = sensitivity.get("coarse_receiver_capacity")
        if capacity:
            ci = capacity["ratio_processing_seconds_per_baseband_second_interval"]
            lines.append(f"Complete coarse receiver CPU estimate: **{capacity['ratio_processing_seconds_per_baseband_second']:.4g} s/s**, "
                f"interval {ci[0]:.4g}–{ci[1]:.4g}, margin {100*capacity['margin_fraction']:.2f}%; status **{capacity['realtime_status']}**. "
                "Includes the measured streaming FIR and full Helper/FFT search; excludes the separate PracticalTracking/product receiver.")
            if capacity.get('phase_note'):
                lines.append(capacity['phase_note'])
        coarse_workspace = sensitivity.get("workspace_margin", {})
        if coarse_workspace.get("estimated_peak_bytes") is not None:
            lines.append(f"Coarse replay workspace: approximately {coarse_workspace['estimated_peak_bytes']/2**20:.1f} MiB "
                f"against {coarse_workspace['budget_bytes']/2**20:.1f} MiB, estimated headroom {100*coarse_workspace['estimated_margin_fraction']:.2f}%. "
                "Includes retained simulation arrays; conservative accounting, not an allocation certificate.")
        prep = item.get("preparation", {})
        if prep:
            lines.append(f"Receiver readiness: {number(prep.get('receiver_readiness_seconds'))} s, including excluded warmup. Simulator generation: {number(prep.get('simulation_generation_seconds'))} s. " +
                ("Readiness exceeds the five-second target; no unavoidable lower bound is established." if prep.get("exceeds_five_second_readiness_target") else
                 "Readiness is within the five-second target for this probe."))
        for name, stage in item.get("measurement", {}).get("stages", {}).items():
            interval = stage.get("ratio_processing_seconds_per_baseband_second_interval")
            margin = stage.get("margin_fraction")
            if interval is not None:
                lines.append(f"{name.capitalize()}: {number(stage.get('ratio_processing_seconds_per_baseband_second'), 4)} s/s "
                    f"(interval {number(interval[0], 4)}–{number(interval[1], 4)}), nominal margin {number(100*margin, 4)}%; "
                    f"stage realtime status **{stage['realtime_status']}**. "
                    + ("Allocated CPU utilization was high." if stage.get("allocation_high_utilization_observed") else
                       "High utilization of the allocated CPUs was not demonstrated."))
        lines.append("These public subwindows do not establish whole-bit acquisition, message reception, or a whole-receiver CPU ceiling. Detailed coverage and memory accounting remain in the JSON.")
        lines.append("")
    lines.append("Raw JSON retains exact configuration, source/binary identity, intervals, model holdout results, utilization, workspace accounting and unresolved coverage. No physical-link or GUI qualification is claimed.")
    return "\n".join(lines) + "\n"


def select_cases(parsed, indices=None):
    """Select explicit current-file row numbers; the default always covers all."""
    if indices is None:
        return parsed["cases"]
    wanted = set(indices)
    available = {case["index"] for case in parsed["cases"]}
    if not wanted or any(isinstance(i, bool) or not isinstance(i, int) for i in wanted) or not wanted <= available:
        raise ValueError("selected case indices must exist in the freshly parsed manual list")
    return [case for case in parsed["cases"] if case["index"] in wanted]


def run_cases(*, executable, output, case_list=DEFAULT_CASES, measurement_seconds=1., threads=None,
              case_indices=None, paired_followup=False):
    parsed = load_cases(case_list)  # Strictly before native/numerical setup.
    selected = select_cases(parsed, case_indices)
    if not math.isfinite(measurement_seconds) or not .1 <= measurement_seconds <= 2:
        raise ValueError("probe seconds must be in[0.1,2]; no long observation mode")
    executable = Path(executable).resolve()
    machine = machine_resources()
    if machine["cpu"] is None or machine["memory_budget_bytes"] is None:
        raise RuntimeError("local CPU/memory allocation unavailable; cannot estimate capacity")
    # The same-stream small-matrix comparison on the local Ryzen5650U found
    # one numerical thread about9x faster than12, with identical bit scores.
    # Preserve explicit overrides and report unused machine capacity; do not
    # divide elapsed service by the available logical CPU count.
    threads = 1 if threads is None else threads
    if isinstance(threads, bool) or not isinstance(threads, int) or not 1 <= threads <= machine["cpu"]["available_cpus"]:
        raise ValueError("threads must fit this machine's detected CPU allocation")
    policy = suite_policy(len(parsed["cases"]), measurement_seconds)
    result = {"schema": SCHEMA, "status": "running", "input": {k: v for k, v in parsed.items() if k != "cases"},
              "machine": machine, "threads": threads, "policy": policy, "provenance": identity(executable), "cases": []}
    result["selection"] = dict(indices=[case["index"] for case in selected],
        complete_current_list=len(selected) == len(parsed["cases"]),
        current_list_entries=len(parsed["cases"]))
    result["thread_policy"] = "One numerical thread by default, based on measured small-matrix overhead; --threads is an explicit override. This is measured single-stream service, not a claim to saturate all available logical CPUs."
    write_result(output, result)
    from triad_cpu import configure_numerical_threads, resolve_cpu_budget
    allocation = resolve_cpu_budget(jobs=1, workers=1, threads=threads)
    result["numerical_threads"] = configure_numerical_threads(allocation)
    from triad_case_receiver import prepare_case
    measured = {}
    for case in selected:
        print(f"case {case['index']}/{len(parsed['cases'])}: {case['heading']}", flush=True)
        if case["configuration_hash"] in measured:
            item = copy.deepcopy(measured[case["configuration_hash"]])
            item["reused_measurement_from"] = item["case"]["id"]
            item["case"] = case
        else:
            item = {"case": case, "status": "unresolved"}
            try:
                plan = native_plan(executable, case["native_args"])
                item["native_plan"] = plan
                item["target_snr"] = target_snr(plan)
                if not plan.get("geometry_feasible"):
                    raise ValueError("native geometry is infeasible: " + json.dumps(plan))
                args = [*case["native_args"], "--seconds", ".001", "--sample-rate", "6000", "--data-if", "1725", "--epoch", "1793850000", "--key-seed", "72001", "--bit", "0"]
                prepared = prepare_case(executable, args, measurement_seconds=measurement_seconds,
                    max_probe_samples=16384, memory_limit_bytes=machine["memory_budget_bytes"], threads=threads, seed=73019)
                try:
                    item["coverage"] = prepared.coverage
                    item["preparation"] = {"total_setup_seconds": prepared.preparation_seconds,
                        "receiver_preparation_seconds": prepared.receiver_preparation_seconds,
                        "simulation_generation_seconds": prepared.simulation_generation_seconds,
                        "native_source_setup_seconds": prepared.native_source_setup_seconds,
                        "long_preparation_unavoidable": None,
                        "scope": "Observed preparation for this implementation; no proof a faster receiver preparation is impossible. Simulation generation is not receiver readiness."}
                    item["workspace"] = prepared.workspace
                    warmup_start = time.perf_counter()
                    prepared.measure(units=1)
                    item["preparation"]["excluded_receiver_warmup_seconds"] = time.perf_counter()-warmup_start
                    readiness = prepared.receiver_preparation_seconds + item["preparation"]["excluded_receiver_warmup_seconds"]
                    item["preparation"]["receiver_readiness_seconds"] = readiness
                    item["preparation"]["exceeds_five_second_readiness_target"] = readiness > policy["preparation_warning_seconds"]
                    item["measurement"] = measure_prepared(prepared, family_size=policy["family_size"])
                    workspace = item["workspace"]
                    estimate = sum(float(workspace.get(key, 0)) for key in
                        ("capture_bytes", "template_bytes", "fft_resident_estimate_bytes", "native_fixed_workspace_bytes"))
                    estimate += max((float(stage["stage_coverage"].get("workspace_bytes", 0))
                        for stage in item["measurement"]["stages"].values()), default=0)
                    observed = max((float(stage["stage_coverage"].get("rss_sampled_peak_sum_process_bytes") or 0)
                        for stage in item["measurement"]["stages"].values()), default=0)
                    item["workspace_margin"] = workspace_summary(estimate, machine["memory_budget_bytes"], observed or None)
                    item["workspace_margin"]["scope"] = "Conservative sum of explicit probe arrays, FFT resident, native fixed and peak stage workspace estimates; overlapping terms may overcount. Whole-bit search and unobserved scratch are not certified."
                    item["status"] = "measured_subwindows"
                finally:
                    prepared.close()
                del prepared
                try:
                    from triad_case_replay import prepare_replay
                    replay = prepare_replay(executable, args,
                        memory_limit_bytes=machine["memory_budget_bytes"], threads=threads, seed=74021)
                    try:
                        item["coarse_sensitivity"] = measure_sensitivity(replay, item["target_snr"],
                            family_size=policy["family_size"], progress=lambda message: print(message, flush=True),
                            paired_followup=paired_followup)
                        sensitivity = item["coarse_sensitivity"]
                        outcomes = list(sensitivity.get("noiseless_gate", [])) + list(sensitivity.get("high_snr_gate", []))
                        for point in sensitivity.get("pilot", []):
                            for stratum in point["strata"]:
                                outcomes.extend(stratum["outcomes"])
                        for stratum in sensitivity.get("confirmation", []):
                            outcomes.extend(stratum["outcomes"])
                        for point in sensitivity.get("earlier_confirmation_rounds", []):
                            for stratum in point["strata"]:
                                outcomes.extend(stratum["outcomes"])
                        for pair in sensitivity.get('paired_point_confirmation', {}).get('pairs', []):
                            outcomes.extend(pair['outcomes'])
                        peak = max((row.get("workspace_bytes", 0) for row in outcomes), default=0)
                        if peak:
                            sensitivity["workspace_margin"] = workspace_summary(peak, machine["memory_budget_bytes"])
                    finally:
                        replay.close()
                    del replay
                except (ValueError, RuntimeError, MemoryError, EOFError, OSError, subprocess.SubprocessError) as error:
                    item["coarse_sensitivity"] = dict(status="unresolved", reason=str(error))
            except (ValueError, RuntimeError, MemoryError, EOFError, OSError, subprocess.SubprocessError) as error:
                item["error"] = str(error)
            measured[case["configuration_hash"]] = item
        result["cases"].append(item)
        write_result(output, result)
    result["input"]["unchanged_at_completion"] = hashlib.sha256(Path(case_list).read_bytes()).hexdigest() == parsed["source_sha256"]
    final_identity = identity(executable)
    result["provenance"]["unchanged_at_completion"] = all(
        result["provenance"][key] == final_identity[key]
        for key in ("revision", "sources", "native_binary_sha256"))
    result["status"] = ("measured_with_unresolved_receiver_coverage" if any(
        item.get("status") == "measured_subwindows" for item in result["cases"]) else "unresolved_no_measurements")
    if not (result["input"]["unchanged_at_completion"] and result["provenance"]["unchanged_at_completion"]):
        result["status"] = "invalidated_source_or_configuration_changed"
    write_result(output, result)
    report = Path(output).with_suffix(".md")
    report.write_text(render_report(result))
    print(json.dumps({"status": result["status"], "output": str(output), "report": str(report), "cases": len(result["cases"])}))
    return result
