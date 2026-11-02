#!/usr/bin/env python3
"""Fail-closed summaries for paired selective-channel plans and outputs.

This is a diagnostic analyzer, not a field qualification tool. Actual-noisy
paths are independent trial units; conditional draws remain clustered by their
sampled physical path. It checks plans against manifests and saved score arrays
before reporting reception counts, pointwise intervals, paired geometry, and
simulation-only energy/projection diagnostics.

Typical use:
  python analyze.py --plan PLAN.json --evidence-root /path/to/checkout \
      --output summary.json [--verify-arrays]

The evidence root maps absolute paths recorded in the portable plan by their
common source prefix. Re-run the plan with paths inside the evidence root when
possible; --source-root makes deliberate relocation explicit.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys
import tempfile

import numpy as np
from scipy.stats import beta


HERE = Path(__file__).resolve().parent
_SPREAD_PATH = HERE.parents[1] / "2026-11-02-spread" / "analyze.py"
_RETENTION_PATH = HERE / "retention_batch.py"


def _load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load analysis dependency: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


REFERENCE = _load("selective_correction_reference", _SPREAD_PATH)
RETENTION = _load("selective_correction_retention", _RETENTION_PATH)


def require(value, message):
    if not value:
        raise ValueError(message)


def read_json(path: Path):
    def reject(value):
        raise ValueError(f"non-finite JSON value {value} in {path}")
    value = json.loads(path.read_text(), parse_constant=reject)
    require(isinstance(value, dict), f"expected JSON object: {path}")
    return value


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def cp(successes: int, trials: int, alpha: float = .05) -> dict:
    require(type(successes) is int and type(trials) is int and 0 <= successes <= trials,
            "invalid binomial count")
    require(math.isfinite(alpha) and 0 < alpha < 1, "invalid binomial confidence alpha")
    lower = float(beta.ppf(alpha, successes, trials-successes+1)) if successes else 0.
    upper = float(beta.ppf(1-alpha, successes+1, trials-successes)) if successes < trials else 1.
    result = {
        "successes": successes, "trials": trials,
        "fraction": successes / trials if trials else None,
        "lower_one_sided": lower, "upper_one_sided": upper,
        "one_sided_alpha": alpha,
        "scope": "pointwise fixed-size binomial interval; independence applies only to the stated actual path trial unit",
    }
    if alpha == .05:
        result.update(lower_one_sided_95=lower, upper_one_sided_95=upper)
    return result


class Paths:
    def __init__(self, evidence_root: Path, source_root: Path | None, plan: dict):
        self.evidence_root = evidence_root.resolve()
        listed = []
        for job in plan.get("jobs", []):
            listed.append(self._option(job["argv"], "--output"))
        for job in plan.get("scoring_jobs", []):
            argv = job["argv"]
            output = self._option(argv, "--output")
            inputs = argv[2:argv.index("--output")]
            listed.extend(inputs)
            listed.append(output)
        require(listed and all(Path(x).is_absolute() and ".." not in Path(x).parts for x in listed),
                "plan paths must be absolute and canonical")
        # Plans normally point into the same source checkout that owns this
        # analyzer. For a relocated result bundle, pass the source prefix
        # explicitly; never guess by matching basenames.
        self.source_root = source_root.resolve() if source_root else self.evidence_root
        require(self.source_root != Path("/"), "ambiguous path root; supply --source-root")

    @staticmethod
    def _option(argv, option):
        require(isinstance(argv, list) and option in argv, f"planned command lacks {option}")
        index = argv.index(option)
        require(index + 1 < len(argv), f"planned {option} has no value")
        return argv[index+1]

    def local(self, recorded: str) -> Path:
        path = Path(recorded)
        require(path.is_absolute() and ".." not in path.parts, f"unsafe planned path: {recorded}")
        local = (self.evidence_root / path.relative_to(self.source_root)).resolve()
        require(local.is_relative_to(self.evidence_root), "mapped path escapes evidence root")
        return local


def _seed_roots(manifest: dict) -> dict | None:
    roots = manifest.get("random_seed_roots", {})
    if not isinstance(roots, dict):
        return None
    result = {name: roots.get(name, manifest.get(name + "_seed"))
              for name in ("channel", "oscillator", "noise", "frontend", "path")}
    key = manifest.get("key_seed")
    if type(key) is not int or any(type(value) is not int for value in result.values()):
        return None
    return {"key": key, **result}


def _source_path(job):
    argv = job["argv"]
    return Paths._option(argv, "--output")


def _campaign_identity(job):
    expected = job.get("simulation_only_expected")
    require(isinstance(expected, dict), f"plan job {job.get('name')} lacks expected identity")
    return expected


def _check_manifest(job, directory: Path, plan: dict, expected_profile: dict | None = None) -> dict:
    manifest_path = directory / "manifest.json"
    require(manifest_path.is_file(), f"missing sampled workspace: {directory}")
    manifest = read_json(manifest_path)
    require(manifest.get("command_argv") == job["argv"],
            "manifest generation command differs from frozen export argv")
    require(manifest.get("format") == "triad-block-stats-v1", "unsupported sampled-statistics format")
    expected = _campaign_identity(job)
    truth = manifest.get("signal_bit")
    require(truth == expected.get("source_bit"), "manifest source bit differs from frozen campaign identity")
    require(manifest.get("epoch") == expected.get("epoch"), "manifest epoch differs from frozen campaign identity")
    roots = _seed_roots(manifest)
    required_roots = {"key": expected.get("key_seed"), **{name: expected.get(name + "_seed")
                      for name in ("channel", "oscillator", "noise", "frontend", "path")}}
    require(roots == required_roots, "manifest seed roots differ from six frozen campaign roots")
    argv = job["argv"]
    planned_cn0 = float(Paths._option(argv, "--cn0"))
    require(manifest.get("cn0_db_hz") == planned_cn0,
            "manifest C/N0 differs from frozen transmitted/noise configuration")
    planned_fs = float(Paths._option(argv, "--sample-rate")) / float(
        Paths._option(argv, "--sample-rate-denominator"))
    require(manifest.get("sample_rate") == planned_fs and math.isclose(
            float(manifest.get("noise_variance", math.nan)), planned_fs / 10**(planned_cn0/10),
            rel_tol=1e-12, abs_tol=0), "sampled rate or noise power differs from frozen C/N0")
    model = argv[argv.index("--selective-model") + 1]
    modulation = argv[argv.index("--selective-perturbation") + 1]
    selective = manifest.get("path_truth_simulation_only", {}).get("selective_perturbation")
    require(isinstance(selective, dict), "manifest lacks actual selective-model instrumentation")
    if modulation == "off":
        require(selective.get("enabled") is False, "Off control did not bypass the modulation stage")
    else:
        model_names = {"legacy-allpass": "legacy-allpass-v1", "dispersive-dft": "dispersive-dft-v1"}
        require(selective.get("enabled") is True and selective.get("model") == model_names.get(model),
                "manifest selective model differs from frozen plan")
        kind, raw_depth = modulation.rsplit("-", 1)
        depth = int(raw_depth) / 100
        expected_depths = [depth if kind != "fast" else 0., depth if kind != "slow" else 0.]
        require(selective.get("component_depths") == expected_depths,
                "measured selective component depths differ from frozen mode")
        if model == "dispersive-dft":
            scale_arg = float(argv[argv.index("--selective-scale-hz") + 1])
            center_arg = float(argv[argv.index("--selective-center-hz") + 1])
            source_format = manifest.get("source_format", {})
            expected_scale = scale_arg if scale_arg > 0 else float(source_format.get("carrier_spacing_hz", math.nan))
            require(math.isclose(float(selective.get("correlation_spacing_hz", math.nan)),
                                 expected_scale, rel_tol=1e-12, abs_tol=1e-15),
                    "measured selectivity scale differs from frozen plan")
            require(math.isclose(float(selective.get("center_hz", math.nan)), center_arg,
                                 rel_tol=0, abs_tol=1e-15),
                    "measured fixed channel center differs from frozen plan")
        floor_arg = float(argv[argv.index("--selective-tail-floor-seconds") + 1])
        fs_actual = float(manifest.get("sample_rate", math.nan))
        require(manifest.get("channel_tail_samples", 0) + 1 >= floor_arg * fs_actual * (1-1e-6),
                "reported channel tail is shorter than the frozen tail floor")
    require(manifest.get("control") == expected.get("control", "signal"),
            "manifest control differs from frozen campaign identity")
    require(manifest.get("stats_noisy") is True, "workspace is not a sampled-noisy export")
    fmt = manifest.get("source_format", {})
    require(fmt.get("encrypted") is True, "planned source encryption is not established")
    require(fmt.get("added_symbols") == 1 and fmt.get("added_interleave") is True,
            "manifest Added Symbols format differs from the planned baseline")
    if expected_profile:
        requested = float(argv[argv.index("--seconds") + 1])
        require(math.isclose(float(manifest.get("requested_active_seconds", math.nan)), requested,
                             rel_tol=0, abs_tol=1e-9), "manifest requested duration differs from planned profile")
        rate = expected_profile["rate"]
        require(math.isclose(float(fmt.get("inner_rate_hz", math.nan)), rate, rel_tol=0, abs_tol=1e-12),
                "manifest rate differs from planned profile")
        for field, expected_value in (("spread_delay_ms", expected_profile["delay_ms"]),
                                      ("spread_doppler_hz", expected_profile["doppler"])):
            require(math.isclose(float(manifest.get("channel_model", {}).get(field, math.nan)),
                                 expected_value, rel_tol=1e-12, abs_tol=1e-15),
                    f"manifest {field} differs from frozen physical profile")
        require(math.isclose(float(manifest.get("channel_model", {}).get("path_evolution", {}).get(
                    "gaussian_doppler_width_hz", math.nan)), expected_profile["width"],
                    rel_tol=1e-12, abs_tol=1e-15), "manifest Gaussian width differs from frozen profile")
        require(manifest.get("sample_rate_numerator") == expected_profile["fs"] and
                manifest.get("sample_rate_denominator") == expected_profile["denominator"],
                "manifest physical sampling rational differs from frozen profile")
        require(manifest.get("block_samples") == math.ceil(expected_profile["block"] *
                expected_profile["fs"] / expected_profile["denominator"]),
                "manifest block grid differs from frozen profile")
    samples = manifest.get("sample_count")
    blocks = manifest.get("block_count")
    block_samples = manifest.get("block_samples")
    require(all(type(x) is int and x > 0 for x in (samples, blocks, block_samples)),
            "invalid sampled calendar in manifest")
    require(blocks == (samples + block_samples - 1) // block_samples,
            "manifest block count does not cover exact sampled calendar")
    require(manifest.get("channel_samples") == samples,
            "export calendar did not read the complete channel output")
    endpoint = manifest.get("observation_endpoint_report", {})
    require(endpoint.get("processed_samples") == samples and
            type(endpoint.get("natural_complete_samples")) is int and
            type(endpoint.get("public_calendar_padding_samples")) is int and
            endpoint["natural_complete_samples"] + endpoint["public_calendar_padding_samples"] == samples,
            "sampled calendar does not contain the complete natural channel endpoint")
    # These plans intentionally score one nominal arrival/frequency/clock,
    # rather than searching the whole independently sampled physical prior.
    candidates = manifest.get("candidates", [])
    require(candidates and all(c.get("arrival_seconds") == manifest.get("capture_lead_seconds") and
            c.get("frequency_hz") == 0 and c.get("clock_ppm") == 0 and
            c.get("frequency_drift_hz_per_second") == 0 and c.get("epoch_offset_seconds") == 0
            for c in candidates), "campaign is not the declared nominal public candidate bank")
    file_hashes = job.get("file_hashes")
    if file_hashes is not None:
        require(isinstance(file_hashes, dict) and file_hashes.get("manifest.json") == sha256(manifest_path),
                "plan manifest hash receipt mismatch")
    return manifest


def _retention(directory: Path) -> dict:
    report, manifest = RETENTION.analyze_workspace(directory)
    energy = report.get("selective_stage_energy_simulation_only", {})
    truth = manifest.get("signal_bit")
    truth_epoch_offset = manifest.get("truth_epoch_offset_seconds", 0)

    def nominal_candidate(bit):
        selected = [item for item in report.get("candidate_projections", [])
                    if (item.get("public_candidate", {}).get("bit") == bit and
                        item.get("public_candidate", {}).get("epoch_offset_seconds") == truth_epoch_offset and
                        item.get("public_candidate", {}).get("arrival_seconds") == manifest.get("capture_lead_seconds") and
                        item.get("public_candidate", {}).get("frequency_hz") == 0 and
                        item.get("public_candidate", {}).get("clock_ppm") == 0 and
                        item.get("public_candidate", {}).get("frequency_drift_hz_per_second") == 0)]
        # When the public bank exports both a direct-only and the configured
        # tapped channel, use the richest exact public basis. This is still a
        # noiseless upper diagnostic, never a receiver success claim.
        if selected:
            max_taps = max(item["public_candidate"].get("tap_count", 0) for item in selected)
            selected = [item for item in selected if item["public_candidate"].get("tap_count", 0) == max_taps]
        require(len(selected) == 1,
                f"could not isolate one exact epoch/timing/frequency candidate for bit {bit}")
        return selected[0]["projection_energy_samples"]

    exact_correct = nominal_candidate(truth)
    exact_wrong = nominal_candidate(1-truth)
    sampled_energy = report.get("signal_energy_samples")
    result = {
        "status": report.get("status"),
        "sample_count": report.get("sample_count"),
        "capture_seconds": report.get("capture_seconds"),
        "sampled_signal_energy": report.get("signal_energy_samples"),
        "signal_projection_fraction": report.get("full_bank_retained_fraction"),
        "best_fixed_correct_label_projection_fraction": report.get("correct_label_retained_fraction"),
        "best_fixed_wrong_label_projection_fraction": report.get("wrong_label_retained_fraction"),
        "nominal_true_epoch_correct_label_projection_energy_samples": exact_correct,
        "nominal_true_epoch_wrong_label_projection_energy_samples": exact_wrong,
        "nominal_correct_projection_over_sampled_energy": exact_correct / sampled_energy if sampled_energy else None,
        "nominal_wrong_projection_over_sampled_energy": exact_wrong / sampled_energy if sampled_energy else None,
        "selective_stage_energy": energy,
    }
    if energy.get("available"):
        post = energy.get("post_selective_energy_samples")
        pre = energy.get("pre_selective_energy_samples")
        result["nominal_correct_projection_over_post_energy"] = exact_correct / post if post else None
        result["nominal_correct_projection_over_pre_energy"] = exact_correct / pre if pre else None
        result["nominal_wrong_projection_over_post_energy"] = exact_wrong / post if post else None
        result["nominal_wrong_projection_over_pre_energy"] = exact_wrong / pre if pre else None
    return result


def _paired_noise_projection(legacy_dir: Path, corrected_dir: Path,
                             legacy_manifest: dict, corrected_manifest: dict) -> dict:
    """Check identical channel-independent projected thermal noise by index."""
    require(legacy_manifest["sample_count"] == corrected_manifest["sample_count"] and
            legacy_manifest["block_count"] == corrected_manifest["block_count"] and
            legacy_manifest["dimension"] == corrected_manifest["dimension"],
            "noise comparison requires identical sampled-stat dimensions")
    equal_files = {}
    for name in ("gram.c128", "intervals.u64", "valid_rows.u64"):
        left = legacy_dir / name
        right = corrected_dir / name
        require(left.is_file() and right.is_file(), f"paired public input is missing: {name}")
        equal_files[name] = sha256(left) == sha256(right)
        require(equal_files[name], f"paired model exports have different {name} despite identical public templates")
    blocks = legacy_manifest["block_count"]
    dimension = legacy_manifest["dimension"]
    shape = (blocks, dimension)
    left_mean = np.memmap(legacy_dir / legacy_manifest["mean_file"], mode="r", dtype="<c16", shape=shape)
    right_mean = np.memmap(corrected_dir / corrected_manifest["mean_file"], mode="r", dtype="<c16", shape=shape)
    left_observation = np.memmap(legacy_dir / legacy_manifest["observation_file"], mode="r", dtype="<c16", shape=shape)
    right_observation = np.memmap(corrected_dir / corrected_manifest["observation_file"], mode="r", dtype="<c16", shape=shape)
    # The receiver's noisy projection is observation minus its matching signal
    # projection. Floating addition/subtraction leaves roundoff, so compare a
    # strict scale-relative tolerance and report the measured maximum.
    max_difference = 0.
    max_noise_projection = 0.
    chunk = max(1, min(blocks, 2048))
    for first in range(0, blocks, chunk):
        stop = min(first+chunk, blocks)
        left_noise = np.asarray(left_observation[first:stop] - left_mean[first:stop])
        right_noise = np.asarray(right_observation[first:stop] - right_mean[first:stop])
        max_difference = max(max_difference, float(np.max(np.abs(left_noise-right_noise), initial=0)))
        max_noise_projection = max(max_noise_projection,
                                   float(np.max(np.abs(left_noise), initial=0)),
                                   float(np.max(np.abs(right_noise), initial=0)))
    require(max_difference <= 1e-10 * max(1., max_noise_projection),
            "sample-indexed projected noise differs across paired model exports")
    return {"basis_gram_intervals_validity_exactly_equal": equal_files,
            "observation_minus_mean_projected_noise_max_abs_difference": max_difference,
            "maximum_projected_noise_magnitude": max_noise_projection,
            "relative_tolerance": 1e-10,
            "scope": "Same sample-indexed seeded Gaussian thermal noise after subtracting each model's own noiseless projected mean; proves coupling for retained linear projections, not equality of post-channel waveforms."}


_SCORERS = {}
_SCORE_DEPENDENCIES = ("triad-noise-study.py", "triad-rbpf-study.py", "triad-cfo-batch-study.py",
    "triad_gaussian_window.py", "triad-static-frequency-study.py", "triad_gaussian_state.py",
    "triad_rbpf.py", "triad_noise_likelihood.py", "triad_state_workspace.py")


def _score_plan_configuration(score_job, paths: Paths, report: dict):
    """Use the actual scorer parser, including its defaults and duplicate-option
    resolution. Parsing allocates no observation or GPU workspace and launches
    nothing. Bind its source/dependencies so a changed parser cannot silently
    reinterpret an old score report.
    """
    argv = score_job["argv"]
    source = paths.local(argv[1])
    require(source.name == "triad-conditional-sweep.py" and source.is_file(),
            "frozen scorer source is unavailable for configuration binding")
    require(report.get("source_sha256") == sha256(source), "scorer source hash differs from score report")
    require(report.get("dependency_sha256") == {name: sha256(source.with_name(name))
                for name in _SCORE_DEPENDENCIES}, "scorer dependency hashes differ from score report")
    if source not in _SCORERS:
        sys.path.insert(0, str(source.parent))
        try:
            _SCORERS[source] = _load("selective_correction_scorer_" + sha256(source)[:16], source)
        finally:
            sys.path.pop(0)
    parse_argv = list(argv[2:])
    # The execution parser refuses existing output files. Its output path is
    # excluded from configuration and bound separately above, so parse with a
    # fresh scratch destination without running or overwriting the scorer.
    with tempfile.TemporaryDirectory(prefix="triad-score-parse-") as scratch:
        parse_argv[parse_argv.index("--output")+1] = str(Path(scratch) / "parse-only.json")
        args = _SCORERS[source].parse_args(parse_argv)
    configuration = {key: value for key, value in vars(args).items()
                     if key not in ("inputs", "output", "budget", "tap_prior", "phase_prior")}
    configuration["coherence"] = "infinity" if math.isinf(args.coherence) else args.coherence
    configuration["tap_powers"] = list(args.tap_prior.stationary_power)
    require(report.get("configuration") == configuration,
            "score report configuration differs from parsed frozen scorer argv")
    require(report.get("family") == args.budget.diagnostics(),
            "score report family calibration differs from parsed frozen scorer argv")
    require(report.get("observation_mode") == args.mode,
            "score report observation mode differs from frozen scorer argv")
    arithmetic = report.get("arithmetic", {})
    require(arithmetic.get("bulk_fft_and_state") == args.precision and
            arithmetic.get("tf32_allowed") is False,
            "score arithmetic differs from frozen precision policy")
    return args


def _independent_roots(sources, manifests):
    roots = [_seed_roots(manifests[source]) for source in sources]
    require(all(root is not None for root in roots), "actual trial lacks its six seed-domain roots")
    for domain in ("key", "channel", "oscillator", "noise", "frontend", "path"):
        require(len({root[domain] for root in roots}) == len(roots),
                f"actual trial roots repeat within {domain} domain")
    return {"verified": True, "domains": list(roots[0]), "trial_unit_count": len(roots),
            "scope": "Distinct frozen pseudorandom roots in every domain within this cell; model/depth pairs intentionally reuse roots across cells."}


def _stratified_intervals(per_bit, success_field, outcome_fields):
    """Fixed bit allocation is not an IID Bernoulli mixture. Report individual
    strata and conservative equal-bit mixture bounds instead of pooled CP.
    """
    if set(per_bit) != {0, 1} or any(not per_bit[bit].get("trials") for bit in (0, 1)):
        return None
    intervals = {}
    for field in (success_field, *outcome_fields):
        bounds = [cp(int(per_bit[bit].get(field, 0)), int(per_bit[bit]["trials"]), .025)
                  for bit in (0, 1)]
        intervals[field] = {
            "lower_one_sided_95": sum(item["lower_one_sided"] for item in bounds) / 2,
            "upper_one_sided_95": sum(item["upper_one_sided"] for item in bounds) / 2,
            "estimate": sum(per_bit[bit].get(field, 0) / per_bit[bit]["trials"] for bit in (0, 1)) / 2,
        }
    return {"outcomes": intervals, "weights": {"0": .5, "1": .5},
            "component_one_sided_alpha": .025,
            "scope": "Each direction is a pointwise 95% bound for an equal-probability bit population via Bonferroni over two 97.5% per-bit CP bounds; no simultaneous coverage across outcomes/cells/methods/gates."}


def _score_receipt(score_job, paths: Paths, expected_sources: list[str],
                   manifest_by_source: dict[str, dict], verify_inputs: bool):
    argv = score_job["argv"]
    output_index = argv.index("--output")
    input_sources = argv[2:output_index]
    require(input_sources == expected_sources,
            f"score job {score_job.get('name')} input order/set differs from frozen export paths")
    report_path = paths.local(argv[output_index+1])
    require(report_path.is_file(), f"missing receiver score report: {report_path}")
    report = read_json(report_path)
    require(report.get("schema") == "triad-conditional-sweep-v1", "unsupported receiver score report")
    expected_args = _score_plan_configuration(score_job, paths, report)
    independence = _independent_roots(expected_sources, manifest_by_source) if expected_args.mode == "actual-noisy" else None
    require(report.get("scores_file") == report_path.with_suffix(".scores.npz").name,
            "score archive name differs from frozen receiver output")
    npz_path = report_path.parent / report["scores_file"]
    require(npz_path.is_file() and sha256(npz_path) == report.get("scores_sha256"),
            "score array archive missing or hash mismatch")
    config = report.get("configuration", {})
    gates = REFERENCE.gates(config)
    expected_methods = config.get("receiver_methods")
    require(isinstance(expected_methods, list) and expected_methods, "receiver method list missing")
    records = report.get("paths")
    require(isinstance(records, list), "score report path records missing")
    got = [record.get("input") for record in records]
    require(got == expected_sources, "receiver report path order differs from frozen inputs")
    counts = defaultdict(Counter)
    bit_counts = defaultdict(lambda: defaultdict(Counter))
    per_path = []
    failures = []
    with np.load(npz_path, allow_pickle=False) as archive:
        used_arrays = set()
        for record, source in zip(records, expected_sources):
            workspace = paths.local(source)
            require(source in manifest_by_source, "score path has no previously validated planned export")
            manifest = manifest_by_source[source]
            require(record.get("observation_mode") == expected_args.mode,
                    "score path mode differs from frozen observation mode")
            require(workspace == paths.local(source), "score path maps to a different sampled workspace")
            require(record.get("truth_bit_simulation_only") in (None, manifest.get("signal_bit")),
                    "score truth label differs from validated manifest")
            # The record's committed input hashes are mandatory for an actual-noisy
            # decision. Rehash them when requested, always rehash manifest + score NPZ.
            hashes = record.get("input_hashes", record.get("sampled_input_hashes"))
            if record.get("status") == "completed":
                if expected_args.mode == "actual-noisy":
                    require(record.get("truth_bit_simulation_only") == (manifest["signal_bit"] if
                        manifest.get("control") == "signal" else None),
                        "actual score evaluation truth differs from its sampled control")
                expected_hash_files = {"manifest.json", "gram.c128", "intervals.u64", "valid_rows.u64"} | (
                    {"observation.c128", "received_energy.f64"} if expected_args.mode == "actual-noisy" else
                    {"mean.c128", "signal_energy.f64"})
                require(isinstance(hashes, dict) and set(hashes) == expected_hash_files and
                        all(isinstance(value, str) and len(value) == 64 and
                            all(c in "0123456789abcdef" for c in value) for value in hashes.values()),
                        "completed score record lacks sampled-input hash binding")
                require(hashes["manifest.json"] == sha256(workspace / "manifest.json"),
                        "score record manifest hash mismatch")
            else:
                require(hashes is None or isinstance(hashes, dict),
                        "failed score record has malformed input-hash data")
            if verify_inputs and hashes is not None:
                for filename, digest in hashes.items():
                    path = workspace / filename
                    require(path.is_file() and sha256(path) == digest,
                            f"sampled-input hash mismatch: {path}")
            control = manifest.get("control")
            truth = manifest.get("signal_bit")
            require(record.get("control") == control, "score path control differs from sampled workspace")
            if record.get("status") != "completed":
                failures.append({"input": source, "control": control,
                                 "status": record.get("status"), "truth_bit": truth,
                                 "error": record.get("error")})
                if control == "signal":
                    for method in expected_methods:
                        failed_cn0 = manifest.get("cn0_db_hz") if report["observation_mode"] == "actual-noisy" else None
                        for failed_gate in ("strict", "relaxed"):
                            counts[(method, control, failed_cn0, failed_gate)].update({"trials": 1, "missed": 1})
                            bit_counts[(method, control, failed_cn0, failed_gate)][truth].update({"trials": 1, "missed": 1})
                continue
            condition_list = record.get("conditions", [])
            require(condition_list, "completed path has no scored conditions")
            if expected_args.mode == "actual-noisy":
                require(len(condition_list) == 1 and condition_list[0].get("actual_observations") == 1 and
                        condition_list[0].get("noise_trials") == 1 and condition_list[0].get("cn0_db_hz") is None,
                        "actual path must have exactly one observed condition/trial without injected C/N0")
            else:
                require([(c.get("cn0_db_hz"), c.get("control")) for c in condition_list] ==
                        [(cn0, control) for cn0 in expected_args.cn0_grid for control in expected_args.controls],
                        "conditional conditions differ from frozen C/N0/control grid")
            for condition in condition_list:
                condition_control = condition.get("control")
                if report["observation_mode"] == "actual-noisy":
                    require(condition_control == control, "actual score condition and manifest control differ")
                else:
                    require(condition_control in ("signal", "null", "wrong-key"),
                            "invalid conditional control")
                actual_cn0 = manifest.get("cn0_db_hz") if report["observation_mode"] == "actual-noisy" else condition.get("cn0_db_hz")
                require(set(condition.get("methods", {})) == set(expected_methods),
                        "condition receiver methods differ from frozen family")
                for method in expected_methods:
                    item = condition.get("methods", {}).get(method)
                    require(isinstance(item, dict), f"score path omitted declared method {method}")
                    key = item.get("score_array")
                    require(isinstance(key, str) and key in archive and key not in used_arrays,
                            "score array missing or reused")
                    used_arrays.add(key)
                    raw_scores = archive[key]
                    require(raw_scores.dtype.kind == "f" and raw_scores.dtype.itemsize in (4, 8),
                            "score arrays must be real FP32/FP64, without implicit complex/integer conversion")
                    scores = np.asarray(raw_scores, dtype=np.float64)
                    require(scores.ndim == 2 and scores.shape[1] == 2 and np.isfinite(scores).all(),
                            "malformed score array")
                    require(scores.shape[0] == condition.get("noise_trials"),
                            "score rows differ from declared trial count")
                    require(scores.shape[0] == expected_args.noise_trials,
                            "score trial count differs from frozen scorer plan")
                    require(math.isclose(float(item.get("threshold", math.nan)),
                                         gates["strict"], rel_tol=0, abs_tol=1e-10),
                            "receiver report threshold differs from family recalibration")
                    require(item.get("counts") == REFERENCE.recount(scores,
                            truth if condition_control == "signal" else None,
                            condition_control, gates["strict"], "trials"),
                            "score report counts differ from independently recounted archived scores")
                    for gate_name in ("strict", "relaxed"):
                        recounted = REFERENCE.recount(scores, truth if condition_control == "signal" else None,
                                                      condition_control, gates[gate_name], "trials")
                        counts[(method, condition_control, actual_cn0, gate_name)].update(recounted)
                        bit_counts[(method, condition_control, actual_cn0, gate_name)][truth].update(recounted)
                    per_path.append({"input": source, "control": condition_control,
                                     "truth_bit": truth, "cn0_db_hz": actual_cn0,
                                     "method": method, "trials": scores.shape[0],
                                     "strict": REFERENCE.recount(scores, truth if condition_control == "signal" else None,
                                         condition_control, gates["strict"], "trials"),
                                     "relaxed": REFERENCE.recount(scores, truth if condition_control == "signal" else None,
                                         condition_control, gates["relaxed"], "trials")})
    require(len(used_arrays) == len(archive.files), "unreferenced score arrays in NPZ")
    output = []
    for (method, control, cn0, gate_name), tally in sorted(counts.items(), key=lambda item: str(item[0])):
        tally = dict(tally)
        denominator = tally.get("trials", 0)
        success = tally.get("correct_accepted", 0) if control == "signal" else tally.get("false_admitted", 0)
        actual_independent = report["observation_mode"] == "actual-noisy"
        failed_control = any(failure.get("control") in (control, None) for failure in failures)
        interval_allowed = actual_independent and (control == "signal" or not failed_control)
        # A predetermined allocation of bit-0/bit-1 paths is stratified. Do not
        # silently treat their potentially different success rates as IID.
        strata = bit_counts[(method, control, cn0, gate_name)]
        tally["pointwise_interval"] = None
        outcome_fields = ("correct_accepted", "wrong_accepted", "missed") if control == "signal" else ("false_admitted", "not_admitted")
        tally["outcome_intervals"] = None
        tally["per_bit"] = ({str(bit): {"counts": dict(values),
            "pointwise_interval": cp(int(values.get("correct_accepted" if control == "signal" else "false_admitted", 0)),
                                      int(values["trials"])),
            "outcome_intervals": {field: cp(int(values.get(field, 0)), int(values["trials"]))
                                  for field in outcome_fields}}
            for bit, values in sorted(strata.items())} if interval_allowed else None)
        tally["balanced_bit_mixture_interval"] = (_stratified_intervals(strata,
            "correct_accepted" if control == "signal" else "false_admitted", outcome_fields)
            if interval_allowed else None)
        tally["pooled_interval_scope"] = "Pooled CP suppressed for fixed bit allocation; use per-bit bounds and the explicit equal-bit population mixture bound."
        tally["interval_suppressed_due_to_failed_control_paths"] = failed_control
        output.append({"method": method, "control": control, "cn0_db_hz": cn0,
                       "gate": gate_name, "counts": tally,
                       "independent_unit": "actual physical path" if report["observation_mode"] == "actual-noisy" else "none pooled; conditional trials are clustered by physical path"})
    return {"report": str(report_path), "report_sha256": sha256(report_path),
            "score_archive_sha256": sha256(npz_path), "observation_mode": report["observation_mode"],
            "all_input_hash_receipts_reverified": bool(verify_inputs),
            "completed_physical_paths": len(records)-len(failures), "failed_physical_paths": failures,
            "actual_trial_root_independence": independence,
            "counts": output, "per_path": per_path,
            "limits": ["Per-bit CP intervals for actual-noisy observations count distinct six-root physical paths; fixed bit mixtures use a conservative stratified bound. Conditional draws are not pooled as independent paths.",
                       "A model-family-calibrated score is not empirical evidence at rare operational false-admission probabilities."]}


def _runner_receipts(export_jobs: list[dict], score_jobs: list[dict],
                     receipt_paths: list[Path]) -> list[dict]:
    ordered = [*export_jobs, *score_jobs]
    planned = {job["name"]: job["argv"] for job in ordered}
    require(len(planned) == len(ordered),
            "duplicate job name in frozen plan")
    receipts = {}
    evidence = []
    for path in receipt_paths:
        doc = read_json(path)
        rows = doc.get("results")
        require(isinstance(rows, list), f"runner results list missing: {path}")
        require(doc.get("cancelled") is False, f"runner canceled: {path}")
        for row in rows:
            name = row.get("name")
            require(name in planned, f"unplanned runner job: {name}")
            require(name not in receipts, f"duplicate runner receipt for {name}")
            require(row.get("command") == planned[name], f"runner argv differs from frozen plan: {name}")
            require(row.get("returncode") == 0, f"planned job failed: {name}")
            receipts[name] = row
        evidence.append({"path": str(path), "sha256": sha256(path), "jobs": len(rows)})
    if receipt_paths:
        missing = sorted(set(planned) - set(receipts))
        require(not missing, f"runner receipts omit frozen jobs: {missing[:5]}")
    return evidence


def run(plan_path: Path, evidence_root: Path, output: Path, source_root: Path | None,
        verify_inputs: bool, runner_results: list[Path] | None = None,
        score_plan_path: Path | None = None):
    plan = read_json(plan_path)
    require(plan.get("schema") == "triad-selective-model-paired-plan-v1", "unsupported paired campaign plan")
    comparison_scope = plan.get("comparison_scope", "paired-legacy-corrected-model-comparison")
    require(comparison_scope in ("paired-legacy-corrected-model-comparison",
                                 "independent-corrected-model-validation"),
            "unsupported declared channel-model comparison scope")
    independent_corrected = comparison_scope == "independent-corrected-model-validation"
    score_plan = read_json(score_plan_path) if score_plan_path else plan
    score_jobs = score_plan.get("scoring_jobs", [])
    require(isinstance(score_jobs, list), "score plan has no scoring_jobs array")
    combined_plan = {"jobs": plan.get("jobs", []), "scoring_jobs": score_jobs}
    paths = Paths(evidence_root, source_root, combined_plan)
    runner_evidence = _runner_receipts(plan.get("jobs", []), score_jobs, runner_results or [])
    profile_by_name = {profile["name"]: profile for profile in plan.get("profiles", [])}
    export_jobs = plan.get("jobs", [])
    source_to_cell = {}
    for cell in plan.get("cells", []):
        require(cell.get("profile") in profile_by_name, "cell profile is absent from frozen profile table")
        for source in cell.get("sources", []):
            require(source not in source_to_cell, "sampled source occurs in multiple frozen cells")
            source_to_cell[source] = cell
    job_by_source = {}
    manifests = {}
    for job in export_jobs:
        source = _source_path(job)
        require(source not in job_by_source, "duplicate planned sampled workspace")
        job_by_source[source] = job
        require(source in source_to_cell, "export job is absent from frozen cells")
        # Cell membership is authoritative. A name such as g001-fastam must
        # never resolve to the shorter g001 profile through prefix matching.
        profile = profile_by_name[source_to_cell[source]["profile"]]
        cell = source_to_cell[source]
        selected_model = Paths._option(job["argv"], "--selective-model")
        selected_modulation = Paths._option(job["argv"], "--selective-perturbation")
        require(cell.get("modulation") == selected_modulation and
                (cell.get("selective_model") == selected_model or
                 (cell.get("selective_model") == "shared-off-bypass" and selected_modulation == "off")),
                "frozen cell model/modulation label differs from generation command")
        if independent_corrected:
            require(cell.get("selective_model") in ("dispersive-dft", "shared-off-bypass") and
                    (selected_model == "dispersive-dft" or selected_modulation == "off"),
                    "independent corrected-model validation contains another active model")
        manifests[source] = _check_manifest(job, paths.local(source), plan, profile)
    require(set(source_to_cell) == set(job_by_source), "cell/export source inventories differ")

    cells = []
    for cell in plan.get("cells", []):
        sources = cell.get("sources", [])
        require(sources, "paired plan cell has no source paths")
        if "paths" in cell:
            require(cell["paths"] == len(sources), "frozen cell path count differs from source list")
        records = [manifests[source] for source in sources]
        samples = {r["sample_count"] for r in records}
        capture_seconds = {r["sample_count"] / r["sample_rate"] for r in records}
        seeds = [_seed_roots(r) for r in records]
        if "balanced_bits" in cell:
            require(cell["balanced_bits"] == {str(bit): sum(record["signal_bit"] == bit for record in records)
                    for bit in (0, 1)}, "frozen cell bit allocation differs from sampled manifests")
        cells.append({"profile": cell.get("profile"), "model": cell.get("selective_model"),
                      "modulation": cell.get("modulation"), "source_count": len(records),
                      "capture_rows_equal_within_cell": len(samples) == 1,
                      "capture_seconds_equal_within_cell": len(capture_seconds) == 1,
                      "distinct_seed_roots": len({json.dumps(x, sort_keys=True) for x in seeds}) == len(seeds),
                      "sample_counts": sorted(samples),
                      "capture_seconds": sorted(capture_seconds),
                      "seed_identity_by_source": dict(zip(sources, seeds)),
                      "energy_and_projection": [
                          {"source": source, **_retention(paths.local(source))}
                          for source in sources]})

    paired = []
    by_key = defaultdict(dict)
    for cell in plan.get("cells", []):
        # Independent validation has no model partner to infer or manufacture.
        # Controls/C/N0 may legitimately reuse an index in different cells;
        # each score cell's seed-domain checks remain mandatory below.
        if independent_corrected:
            continue
        model = cell.get("selective_model")
        if model not in ("legacy-allpass", "dispersive-dft"):
            continue
        for source in cell.get("sources", []):
            manifest = manifests[source]
            candidate = next((j for j in export_jobs if _source_path(j) == source), None)
            require(candidate is not None, "cell source is absent from export jobs")
            identity = _campaign_identity(candidate)
            key = (cell.get("profile"), cell.get("modulation"), identity["index"])
            require(model not in by_key[key], "duplicate paired model export identity")
            by_key[key][model] = (source, manifest, identity)
    for key, models in sorted(by_key.items(), key=lambda item: str(item[0])):
        require(set(models) == {"legacy-allpass", "dispersive-dft"},
                "paired campaign is missing one selected channel-model export")
        (legacy_source, legacy, legacy_id), (new_source, new, new_id) = (
            models["legacy-allpass"], models["dispersive-dft"])
        require(legacy_id == new_id, "paired source bit/epoch/seed identity differs")
        require(legacy["sample_rate"] == new["sample_rate"] and
                legacy["sample_count"] == new["sample_count"] and
                legacy["block_samples"] == new["block_samples"],
                "paired models do not have the same complete sampled calendar")
        require(legacy.get("channel_seed") == new.get("channel_seed") and
                legacy.get("key_seed") == new.get("key_seed"),
                "paired model waveform/key seeds differ")
        legacy_ret = _retention(paths.local(legacy_source))
        new_ret = _retention(paths.local(new_source))
        paired_noise = _paired_noise_projection(paths.local(legacy_source), paths.local(new_source),
                                                legacy, new)
        paired.append({"profile": key[0], "modulation": key[1], "path_index": key[2],
                       "source_bit": legacy_id["source_bit"], "epoch": legacy_id["epoch"],
                       "seed_roots": _seed_roots(legacy), "equal_calendar": True,
                       "sample_count": legacy["sample_count"],
                       "sampled_signal_energy_ratio_new_over_legacy": (
                           new_ret["sampled_signal_energy"] / legacy_ret["sampled_signal_energy"]
                           if legacy_ret.get("sampled_signal_energy") else None),
                       "paired_noise_projection": paired_noise,
                       "legacy_energy_and_projection": legacy_ret,
                       "corrected_energy_and_projection": new_ret})

    score_summaries = []
    cell_sources = {tuple(cell.get("sources", [])): cell for cell in plan.get("cells", [])}
    for score_job in score_jobs:
        argv = score_job["argv"]
        output_path = Paths._option(argv, "--output")
        source_paths = argv[2:argv.index("--output")]
        # Resolve exactly one frozen cell by the scorer's input set and order.
        matching = [cell for cell in plan.get("cells", []) if cell.get("sources") == source_paths]
        require(len(matching) == 1, f"scoring job {score_job.get('name')} is not bound to one frozen cell")
        cell = matching[0]
        actual_noisy = "actual-noisy" in argv
        expected_mode = "actual-noisy" if plan.get("stage") in ("holdout", "long-holdout", "long-replay") else "conditional"
        if score_plan_path and score_plan.get("stage", "").endswith("actual-noisy"):
            expected_mode = "actual-noisy"
        if score_plan_path and score_plan.get("stage", "").endswith("conditional"):
            expected_mode = "conditional"
        require(actual_noisy == (expected_mode == "actual-noisy"), "score mode differs from frozen stage")
        receipt = _score_receipt(score_job, paths, source_paths, manifests, verify_inputs)
        score_summaries.append({"name": score_job.get("name"), "profile": cell.get("profile"),
                                "model": cell.get("selective_model"), "modulation": cell.get("modulation"),
                                **receipt})

    report = {
        "schema": "triad-selective-model-correction-analysis-v1",
        "analyzer_sha256": sha256(Path(__file__)),
        "retention_source_bindings": RETENTION.source_bindings(),
        "plan": str(plan_path.resolve()), "plan_sha256": sha256(plan_path),
        "score_plan": str(score_plan_path.resolve()) if score_plan_path else str(plan_path.resolve()),
        "score_plan_sha256": sha256(score_plan_path) if score_plan_path else sha256(plan_path),
        "stage": plan.get("stage"), "plan_source": plan.get("source"),
        "comparison_scope": comparison_scope,
        "workspace_count": len(manifests), "paired_path_count": len(paired),
        "cells": cells, "paired_model_comparisons": paired,
        "receiver_score_summaries": score_summaries,
        "runner_receipts": runner_evidence,
        "execution_receipts_complete": bool(runner_results),
        "source_binary_provenance_scope": "Generation argv and scorer source/dependencies are checked here; compiler/source-bundle/binary execution identity remains an external provenance binding, not proved by a manifest command string or Git revision.",
        "interpretation": [
            "Measured pre/post selective energy is kept separate from candidate-subspace projection and receiver admission.",
            "Projection diagnostics are noiseless simulation-only quantities; they are not decoder sensitivity or receiver inputs.",
            "Pairing requires identical source bit, epoch, key/channel seed roots, sample rate, block grid, and complete sample count. Matching seed labels alone is insufficient.",
            "Pointwise Clopper-Pearson bounds do not cover selection across methods, C/N0, modulation, model, cells, or gates.",
            "No result here establishes a continuous-operation false-admission tail or a physical propagation calibration.",
        ],
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--score-plan", type=Path,
                        help="separate frozen receiver plan when CPU/GPU scoring was planned independently")
    parser.add_argument("--evidence-root", type=Path, required=True)
    parser.add_argument("--source-root", type=Path,
                        help="absolute root prefix used in frozen job paths when evidence was relocated (defaults to --evidence-root)")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runner-results", type=Path, action="append", default=[],
                        help="bounded-runner result JSON; supply all files covering frozen exports and scores")
    parser.add_argument("--verify-arrays", action="store_true",
                        help="rehash every sampled input file, not only manifests and NPZ archives")
    args = parser.parse_args(argv)
    result = run(args.plan, args.evidence_root, args.output, args.source_root,
                 args.verify_arrays, args.runner_results, args.score_plan)
    print(json.dumps({"output": str(args.output), "workspaces": result["workspace_count"],
                      "paired_paths": result["paired_path_count"],
                      "score_reports": len(result["receiver_score_summaries"])}))


if __name__ == "__main__":
    main()
