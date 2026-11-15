"""Scalar, full-horizon known-message presence reference; no DSP or receiver execution.

The three preselected low-energy characterization rows use direct vector
propagation and additive proper-complex white Gaussian noise. This reference
knows the whole transmitted waveform, channel, phase, amplitude and timing.
It is neither a decoding probability nor a practical receiver qualification.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from statistics import NormalDist

import run

APP = Path(__file__).resolve().parent
EPISODES = ("primary-A-0", "holdout-B-0", "primary-A-double-duration")
ALPHA = 1e-9  # Explicit single-hypothesis reference, not the native search threshold.


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def known_mean_miss(gamma: float) -> float:
    if not math.isfinite(gamma) or gamma < 0:
        raise ValueError("energy/noise ratio must be finite and nonnegative")
    threshold_z = -NormalDist().inv_cdf(ALPHA)
    # erfc avoids destructive cancellation in the small miss tail.
    return .5 * math.erfc((math.sqrt(2 * gamma) - threshold_z) / math.sqrt(2))


def observation_bound(plan: dict, slots: list, gamma: float) -> dict:
    """Pigeonhole bound; no clean per-slot energy is observed or inferred."""
    active = [slot for slot in plan["public_slots"] if slot["evaluator_transmitting"]]
    if not active or len(slots) != len(plan["public_slots"]):
        raise ValueError("complete public-slot observations are required")
    by_index = {slot["index"]: slot for slot in slots}
    if len(by_index) != len(slots):
        raise ValueError("duplicated scored public slot")
    intervals = []
    for public in active:
        scored = by_index[public["index"]]
        start, stop = scored["first_sample"], scored["stop_sample"]
        if (type(start) is not int or type(stop) is not int or
                not 0 <= start < stop <= plan["horizon_samples"] or
                public["observation"] != {"first": start, "stop": stop} or
                scored["complete"] is not True or scored["all_valid"] is not True):
            raise ValueError("active observation is invalid or differs from its public plan")
        intervals.append((start, stop))
    intervals.sort()
    if any(left[1] > right[0] for left, right in zip(intervals, intervals[1:])):
        raise ValueError("active observations overlap; energy partition bound is unavailable")
    bound = gamma / len(intervals)
    return {"active_observation_count": len(intervals), "pairwise_disjoint": True,
        "active_observation_intervals": intervals,
        "at_least_one_observation_energy_over_noise_upper_bound": bound,
        "at_least_one_known_mean_observation_miss_probability_lower_bound": known_mean_miss(bound),
        "scope": "Conditional observation-scale bound from nonnegative whole-horizon energy and disjoint apertures. Not measured per-slot energy, the native error probability, or a universal message-decoder bound."}


def analyze(directory: Path) -> dict:
    directory = directory.resolve(strict=True)
    if not directory.is_relative_to(APP):
        raise ValueError("evidence must remain under the contained appraisal")
    record_path = directory / "run.json"
    record = json.loads(record_path.read_text())
    frozen_path = directory / "frozen-input.json"
    frozen = json.loads(frozen_path.read_text())
    if (record["campaign_sha256"] != frozen["campaign_sha256"] or
            frozen["campaign_sha256"] != hashlib.sha256(run._json_bytes(frozen["campaign"])).hexdigest() or
            record["probe_sha256"] != frozen["probe_sha256"]):
        raise ValueError("run and frozen executable/campaign bindings differ")
    rows = []
    for name in EPISODES:
        matches = [trial for trial in record["trials"] if trial["episode"]["id"] == name]
        if len(matches) != 1:
            raise ValueError(f"missing or duplicated selected row: {name}")
        trial = matches[0]
        episode = trial["episode"]
        if [ep for ep in frozen["campaign"]["episodes"] if ep["id"] == name] != [episode]:
            raise ValueError("selected episode differs from the frozen campaign")
        if (trial["status"] != "completed" or trial["timed_out"] or trial["return_code"] != 0 or
                episode["mode"] != "receive" or episode.get("control", "wanted") != "wanted" or
                episode["scenario"] not in {"hf-vector-3d", "hf-vector-3d-holdout"}):
            raise ValueError("reference requires completed direct wanted vector reception")
        path = (directory / trial["raw_stdout"]).resolve(strict=True)
        if not path.is_relative_to(directory):
            raise ValueError("raw receipt escapes its declared run directory")
        receipt = run._parse_receipt(path)
        run._validate_completed_receipt(receipt, episode)
        plan, channel = receipt["plan"], receipt["channel"]
        configured = receipt["scenario"]["channel"]
        noise, frontend = configured["noise"], configured["frontend"]
        variance = plan["noise_variance"]
        if (variance != noise["white_variance"] or not math.isfinite(variance) or variance <= 0 or
                any(noise[key] != 0 for key in ("colored_variance", "impulse_rate_hz", "impulse_rms", "tone_amplitude")) or
                any(frontend[key] != 0 for key in ("adc_bits", "agc_target", "clip_magnitude", "lowpass_hz")) or
                configured["transmitter"]["enabled"] or plan["helper_enabled"]):
            raise ValueError("reference requires the declared pure additive white-noise plane")
        energy = channel["vector_oblique"]["selected_energy_sample_sum"]
        horizon = plan["horizon_samples"]
        if (not math.isfinite(energy) or energy < 0 or energy != channel["signal_energy_sample_sum"] or
                horizon != channel["samples"] or horizon != receipt["outcomes"]["accepted_samples"]):
            raise ValueError("clean energy and complete receive horizon do not match")
        gamma = energy / variance
        miss = known_mean_miss(gamma)
        rows.append({"episode_id": name, "receipt_path": str(path.relative_to(APP)),
            "receipt_sha256": digest(path), "source_identity": receipt["source_identity"],
            "input_sha256": channel["input_sha256"], "table_sha256": channel["vector_oblique"]["table_sha256"],
            "horizon_samples": horizon, "sample_rate_hz": plan["sample_rate_hz"],
            "incident_cn0_db_hz": plan["cn0_db_hz"], "clean_selected_energy_sample_sum": energy,
            "receiver_complex_noise_variance": variance, "whole_horizon_energy_over_noise_variance": gamma,
            "conditional_known_message_miss_probability": miss,
            "log10_miss_probability": math.log10(miss) if miss else None,
            "binary64_miss_underflow": miss == 0.,
            "disjoint_observation_bound": observation_bound(plan, receipt["slots"], gamma),
            "native_correct_complete": receipt["outcomes"]["correct_complete"]})
    return {"kind": "conditional_full_horizon_known_message_presence_reference", "schema_version": 1,
        "analysis_source_sha256": digest(Path(__file__)), "run_sha256": digest(record_path),
        "frozen_input_sha256": digest(frozen_path), "probe_sha256": frozen["probe_sha256"],
        "false_alarm_probability_per_known_hypothesis": ALPHA,
        "formula": "Pmiss = Phi(Phi^-1(1-alpha) - sqrt(2*E/nu)); nu=E[|n[k]|^2] per complex sample",
        "hypotheses": "H0: white noise; H1: one exactly known full-message clean received mean plus the same noise law",
        "scope": "Optimistic known payload/channel/timing/phase/amplitude characterization. Not blind reception, BER, physical completion, an unknown-candidate search, or an environmental success rate.",
        "rows": rows}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-directory", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(APP):
        parser.error("output must remain under the contained appraisal")
    result = analyze(args.run_directory)
    output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"output": str(output), "rows": len(result["rows"])}))
