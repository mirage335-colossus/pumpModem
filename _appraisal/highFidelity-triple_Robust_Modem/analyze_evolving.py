"""Decision-window energy references from the existing native evolving-HF run.

No waveform or receiver calculation is performed here. The private reference
knows the exact clean signal and uses one fixed single-hypothesis false-alarm
constraint. It is not a bit-error prediction or blind receiver qualification.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

import run
from analyze_vector_presence import ALPHA, known_mean_miss

APP = Path(__file__).resolve().parent


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def decision_references(receipt: dict) -> list[dict]:
    plan, channel = receipt["plan"], receipt["channel"]
    noise = receipt["scenario"]["channel"]["noise"]
    frontend = receipt["scenario"]["channel"]["frontend"]
    variance = plan["noise_variance"]
    if (plan["channel_renderer"] != "hf_vector_oblique" or plan["helper_enabled"] or
            not math.isfinite(variance) or variance <= 0 or variance != noise["white_variance"] or
            any(noise[k] != 0 for k in ("colored_variance", "impulse_rate_hz", "impulse_rms", "tone_amplitude")) or
            any(frontend[k] != 0 for k in ("adc_bits", "agc_target", "clip_magnitude", "lowpass_hz")) or
            receipt["scenario"]["channel"]["transmitter"]["enabled"]):
        raise ValueError("reference requires direct propagation and additive proper-complex white noise")
    energies = channel["private_signal_energy_observations"]
    public, scored = plan["public_slots"], receipt["slots"]
    if not energies or len(energies) != len(public) or len(scored) != len(public):
        raise ValueError("missing complete per-observation energy or scoring")
    physical = channel.get("vector_oblique", {})
    event = physical.get("evolution")
    event_start = event_stop = 0
    if event is not None:
        fs = plan["sample_rate_hz"]
        event_start = math.ceil(event["start_seconds"] * fs + physical["modelling_delay_samples"])
        event_stop = math.ceil((event["start_seconds"] + event["duration_seconds"]) * fs +
                               physical["modelling_delay_samples"])
    total, last_stop, rows = 0., 0, []
    for i, (energy, slot, score) in enumerate(zip(energies, public, scored)):
        first, stop = energy["first_sample"], energy["stop_sample"]
        value = energy["energy_sample_sum"]
        if (type(first) is not int or type(stop) is not int or
                not last_stop <= first < stop <= plan["horizon_samples"] or
                energy["public_slot"] != i or slot["index"] != i or score["index"] != i or
                slot["observation"] != {"first": first, "stop": stop} or
                score["first_sample"] != first or score["stop_sample"] != stop or
                score["complete"] is not True or score["all_valid"] is not True or
                energy["samples"] != stop-first or not math.isfinite(value) or value < 0):
            raise ValueError("energy observations differ from complete disjoint public decision windows")
        last_stop = stop
        total += value
        gamma = value / variance
        event_samples = max(0, min(stop, event_stop) - max(first, event_start))
        rows.append({"public_slot": i, "first_sample": first, "stop_sample": stop,
            "evaluator_transmitting": slot["evaluator_transmitting"],
            "intrinsic_event_overlap_samples": event_samples,
            "intrinsic_event_overlap_fraction": event_samples / (stop-first),
            "clean_energy_sample_sum": value, "energy_over_noise_variance": gamma,
            "conditional_known_mean_presence_miss": known_mean_miss(gamma),
            "native_decision": score["decision"], "native_bit": score.get("bit")})
    full_energy = channel["signal_energy_sample_sum"]
    if (not math.isfinite(full_energy) or full_energy < 0 or
            total > full_energy + 1e-10 * max(1., full_energy)):
        raise ValueError("disjoint window energy exceeds the complete clean energy")
    return rows


def analyze(directory: Path) -> dict:
    directory = directory.resolve(strict=True)
    if not directory.is_relative_to(APP):
        raise ValueError("evidence must remain under the appraisal directory")
    record_path, frozen_path = directory / "run.json", directory / "frozen-input.json"
    record, frozen = json.loads(record_path.read_text()), json.loads(frozen_path.read_text())
    if (record["campaign_sha256"] != frozen["campaign_sha256"] or
            frozen["campaign_sha256"] != hashlib.sha256(run._json_bytes(frozen["campaign"])).hexdigest() or
            record["probe_sha256"] != frozen["probe_sha256"]):
        raise ValueError("run and frozen campaign/executable bindings differ")
    rows, identity = [], None
    for trial in record["trials"]:
        episode = trial["episode"]
        if episode["control"] != "wanted":
            continue
        if (trial["status"] != "completed" or trial["timed_out"] or trial["return_code"] != 0 or
                episode["mode"] != "receive" or episode["scenario"] not in {
                    "hf-vector-evolving", "hf-vector-evolving-rigid", "hf-vector-evolving-holdout",
                    "hf-vector-evolving-holdout-rigid"}):
            raise ValueError("analysis requires completed direct wanted evolving-field observations")
        if [e for e in frozen["campaign"]["episodes"] if e["id"] == episode["id"]] != [episode]:
            raise ValueError("episode differs from its frozen campaign")
        path = (directory / trial["raw_stdout"]).resolve(strict=True)
        if not path.is_relative_to(directory):
            raise ValueError("receipt escapes the declared run directory")
        receipt = run._parse_receipt(path)
        run._validate_completed_receipt(receipt, episode)
        if identity is not None and identity != receipt["source_identity"]:
            raise ValueError("one analysis cannot combine different native source identities")
        identity = receipt["source_identity"]
        channel, plan = receipt["channel"], receipt["plan"]
        windows = decision_references(receipt)
        rows.append({"episode": episode, "receipt_path": str(path.relative_to(APP)),
            "receipt_sha256": sha(path), "source_identity": identity,
            "input_sha256": channel["input_sha256"], "table_sha256": channel["vector_oblique"]["table_sha256"],
            "actual_bit_seconds": plan["actual_bit_seconds"], "horizon_samples": plan["horizon_samples"],
            "sample_rate_hz": plan["sample_rate_hz"], "noise_variance": plan["noise_variance"],
            "incident_energy_over_noise": channel["input_energy_sample_sum"] / plan["noise_variance"],
            "selected_energy_over_noise": channel["signal_energy_sample_sum"] / plan["noise_variance"],
            "selected_energy_outside_observations_over_noise":
                channel["private_signal_energy_outside_observations_sample_sum"] / plan["noise_variance"],
            "correct_complete": receipt["outcomes"]["correct_complete"],
            "wall_seconds": receipt["timing"]["wall_seconds"], "observations": windows})
    if not rows:
        raise ValueError("no wanted observations")
    return {"kind": "evolving_vector_decision_window_presence_reference", "schema_version": 1,
        "analysis_source_sha256": sha(Path(__file__)),
        "reference_source_sha256": sha(APP / "analyze_vector_presence.py"),
        "run_sha256": sha(record_path), "frozen_input_sha256": sha(frozen_path),
        "probe_sha256": frozen["probe_sha256"], "reference_false_alarm_per_known_hypothesis": ALPHA,
        "scope": "Exact-window clean received energy before white noise, generated privately in the same native render. Conditional known-waveform/phase/amplitude presence reference; not native false-alarm calibration, bit error, physical completion or a natural-condition success rate.",
        "rows": rows}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-directory", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    destination = args.output.resolve()
    if not destination.is_relative_to(APP):
        parser.error("output must remain under the appraisal directory")
    result = analyze(args.run_directory)
    destination.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"output": str(destination), "wanted_rows": len(result["rows"])}))
