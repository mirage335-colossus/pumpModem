#!/usr/bin/env python3
"""Prepare repeated absolute-level cable probes; this tool never opens audio.

The prepared composite uses the production float-to-S16 playback conversion
after signal and noise are combined. Two independent captures of one fixed
playlist measure physical repeat scatter. The resulting plan is a diagnostic
input, not calibrated receiver admission data.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

import numpy as np

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))
import triad_live_margins as campaign
import triad_live_model as model

FS = 48000
DEFAULT_NOISE_RMS = (0.1, 0.03, 0.001, 0.0001)
CASE_CNO_DB_HZ = {"baseline": -1.0, "edge": 1.0}


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")


def _digest(path: Path) -> str:
    return campaign.digest(path)


def _load_case(fixtures: Path, name: str) -> tuple[Path, dict, np.ndarray, np.ndarray]:
    directory = fixtures / name
    fixture = campaign.read_json(directory / "fixture.json")
    if fixture.get("schema") != "triad-live-margin-fixture-v1" or fixture.get("id") != name:
        raise ValueError(f"invalid frozen {name} fixture")
    campaign.verify_fixture(directory, fixture, require_complete=True)
    waves = []
    for bit in (0, 1):
        wave = np.fromfile(directory / f"signal-{bit}.f32", dtype="<f4")
        payload_samples = fixture.get("payload_audio_samples")
        declared = fixture.get("source_waveform_samples")
        expected = declared[bit] if isinstance(declared, list) and len(declared) == 2 else len(wave)
        if (not isinstance(expected, int) or expected < 1 or len(wave) != expected
                or expected > payload_samples or not np.all(np.isfinite(wave))):
            raise ValueError(f"invalid complete {name} bit-{bit} source waveform")
        # Export files contain only native source support. Payload records
        # include the fixture's declared padding/tail interval as zero source.
        waves.append(np.pad(wave, (0, payload_samples - len(wave))))
    return directory, fixture, waves[0], waves[1]


def prepare(args) -> dict:
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=False)
    fixture_root = Path(args.fixtures).resolve()
    profile_path = Path(args.profile).resolve()
    profile = campaign.read_json(profile_path)
    if profile.get("schema") != "triad-live-audio-profile-v1" or profile.get("sample_rate") != FS:
        raise ValueError("the frozen corrected 48-kHz physical audio profile is required")
    profile_bytes = profile_path.read_bytes()
    profile_hash = _sha(profile_bytes)
    cases = {name: _load_case(fixture_root, name) for name in CASE_CNO_DB_HZ}
    source_profile_hashes = {f.get("profile_sha256") for _, f, _, _ in cases.values()}
    if profile_hash in source_profile_hashes:
        raise ValueError("level probes require the separately frozen corrected forward profile")
    source_preprocessing = {json.dumps(f.get("preprocessing_sources"), sort_keys=True)
                            for _, f, _, _ in cases.values()}
    if len(source_preprocessing) != 1:
        raise ValueError("the baseline and edge fixtures must use matching source preprocessing")

    levels = tuple(float(value) for value in args.noise_levels.split(","))
    if (not levels or any(not math.isfinite(value) or value <= 0 or value > 0.1 for value in levels)
            or len(set(levels)) != len(levels)):
        raise ValueError("noise levels must be unique finite RMS values in (0, 0.1]")
    signals = {name: (f, a, b) for name, (_, f, a, b) in cases.items()}
    max_samples = max(f["payload_audio_samples"] for f, _, _ in signals.values())
    rng = np.random.default_rng(args.seed)
    records: list[dict] = []
    for noise_rms in levels:
        for name, (fixture, signal0, signal1) in signals.items():
            cno = CASE_CNO_DB_HZ[name]
            gain = math.sqrt(2.0 * noise_rms**2 / FS * 10.0 ** (cno / 10.0))
            for bit, source in enumerate((signal0, signal1)):
                records.append(dict(case=name, kind="signal", truth=bit, gain=gain,
                    cn0_db_hz=cno, noise_rms=noise_rms, calibration=False,
                    source_samples=len(source), fixture=fixture["id"]))
        records.append(dict(case=None, kind="noise", truth=None, gain=0., cn0_db_hz=None,
            noise_rms=noise_rms, calibration=False, source_samples=max_samples, fixture=None))
    for quiet_index in range(2):
        records.append(dict(case=None, kind="silence", truth=None, gain=0., cn0_db_hz=None,
            noise_rms=0., calibration=False, source_samples=max_samples, fixture=None,
            quiet_index=quiet_index))
    rng.shuffle(records)

    by_case = {name: (fixture, signals[name][1:]) for name, (_, fixture, *_rest) in cases.items()}
    offset = 0
    peak = 0.0
    stimulus_path = output / "stimulus.f32"
    with stimulus_path.open("xb") as stream:
        for index, record in enumerate(records):
            record["index"] = index
            record["noise_seed"] = int(args.seed * 10000 + index)
            size = record["source_samples"]
            noise = np.random.default_rng(record["noise_seed"]).normal(
                0., record["noise_rms"], size).astype(np.float32)
            clean = np.zeros(size, dtype=np.float32)
            source = None
            if record["kind"] == "signal":
                fixture, waves = by_case[record["case"]]
                source = waves[record["truth"]]
                if fixture["profile_sha256"] not in source_profile_hashes:
                    raise ValueError("signal source and frozen fixture identity diverged")
                clean[:len(source)] = np.float32(record["gain"]) * source
            composite = noise + clean
            if not np.all(np.isfinite(composite)) or float(np.max(np.abs(composite))) >= .8:
                raise ValueError("probe composite reached the declared 0.8 full-scale headroom limit")
            # Diagnostics compare complete composites with and without the
            # signal; no receiver ever gets the clean waveform or these labels.
            q_composite = model.quantize_production_playback(composite)
            q_noise = model.quantize_production_playback(noise)
            composite_codes = np.rint(q_composite * 32768.).astype(np.int32)
            noise_codes = np.rint(q_noise * 32768.).astype(np.int32)
            delta_codes = composite_codes - noise_codes
            signal_energy = float(np.sum(np.asarray(clean, dtype=np.float64) ** 2))
            code_energy = float(np.sum(delta_codes.astype(np.float64) ** 2))
            record["quantization"] = dict(
                composite_peak=float(np.max(np.abs(composite))),
                composite_nonzero_codes=int(np.count_nonzero(composite_codes)),
                composite_distinct_codes=int(np.unique(composite_codes).size),
                isolated_signal_nonzero_codes=int(np.count_nonzero(
                    np.rint(model.quantize_production_playback(clean) * 32768.))),
                signal_induced_code_changes=int(np.count_nonzero(delta_codes)),
                signal_induced_quantization_code_energy=code_energy,
                signal_induced_code_energy_fs_squared=code_energy / 32768.0**2,
                clean_signal_energy=signal_energy,
                quantized_delta_to_clean_energy_ratio=(code_energy / (32768.0**2 * signal_energy)
                                                       if signal_energy else 0.),
                meaning="complete-composite S16 diagnostic; noise dithers signal codes; not an ADC ENOB estimate")
            tx, markers = model.wrap_with_markers(composite, FS,
                seed=args.seed + 100 + index * 2)
            if float(np.max(np.abs(tx))) >= .8:
                raise ValueError("wrapped probe exceeds the declared 0.8 full-scale headroom limit")
            tx_bytes = tx.astype("<f4").tobytes()
            stream.write(tx_bytes)
            record.update(offset=offset, samples=len(tx), markers=markers,
                          stimulus_sha256=_sha(tx_bytes))
            peak = max(peak, float(np.max(np.abs(tx))))
            offset += len(tx)
    _json(output / "profile.json", profile)
    fixture_hashes = {name: _digest(directory / "fixture.json") for name, (directory, *_rest) in cases.items()}
    plan = dict(schema="triad-live-level-probe-v1", phase="level_probe",
        fixtures_directory=str(fixture_root), profile_sha256=profile_hash,
        profile_copy_sha256=_digest(output / "profile.json"),
        source_profile_sha256=sorted(source_profile_hashes),
        fixture_json_sha256=fixture_hashes, source_sha256=_digest(Path(__file__)),
        model_source_sha256=_digest(Path(model.__file__)), seed=args.seed, rate=FS,
        noise_levels_rms=list(levels), cases=CASE_CNO_DB_HZ,
        records=records, samples=offset, seconds=offset / FS, peak=peak,
        stimulus_sha256=_digest(stimulus_path),
        repeat_passes=2,
        noise_convention="one-sided pre-DAC real noise PSD is 2*RMS^2/48000; C/N0 is nominal total native signal energy per second over this noise density; not ADC-referred C/N0",
        source_waveforms="reused complete keyed baseline/edge fixtures; their original noise/reference-variance values are not reused as signal or likelihood parameters",
        profile_scope="corrected frozen forward profile; templates/bases remain the frozen original fixtures",
        physical_action="record this exact playlist twice in separate raw/native passes; inspect continuity receipts before scoring",
        admission_status="diagnostic only; no calibrated gate, receiver admissions, or FAP claim")
    _json(output / "plan.json", plan)
    return plan


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True)
    parser.add_argument("--fixtures", required=True)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--seed", type=int, default=96101)
    parser.add_argument("--noise-levels", default=",".join(map(str, DEFAULT_NOISE_RMS)))
    args = parser.parse_args()
    if args.seed < 0:
        parser.error("seed must be nonnegative")
    plan = prepare(args)
    print(json.dumps(dict(phase=plan["phase"], seconds=plan["seconds"],
                          records=len(plan["records"]), peak=plan["peak"],
                          repeat_passes=plan["repeat_passes"])))


if __name__ == "__main__":
    main()
