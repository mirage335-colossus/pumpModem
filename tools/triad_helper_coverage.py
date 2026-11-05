#!/usr/bin/env python3
"""Factorized Helper-enabled coverage plan for the isolated triad exporter.

This tool records requested values, selected interaction probes and optional
bounded native plan-only results. It never runs receptions and never treats
the large Cartesian configuration count as sampled coverage.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import time
from typing import Any


RATES_HZ = (.001, .01, .1, 1., 3.6, 10., 36., 100., 360., 1200., 2400.,
            3600., 12000., 18000., 24000., 1_000_000., 30_000_000.)
BASEBAND_CARRIERS = ("1725-hz", "half-rate", "8270-hz")
RF_SHIFTS_HZ = (0., 500_000., 1_000_000., 3_500_000., 7_000_000.,
                10_000_000., 14_000_000., 30_000_000., 5_800_000_000.)
TIMING_POLICIES = ("bounded-10ms-1ms-0ms-audio70ms", "default-plusminus-several-seconds")
BASE_OSCILLATORS = ("crystal", "gpsdo-xo", "gpsdo-tcxo", "gpsdo-ocxo")
SHIFT_OSCILLATORS = ("baseband-clock", "crystal", "cb-ssb", "ic-7100",
                     "gpsdo-xo", "gpsdo-tcxo", "gpsdo-ocxo")
SPREAD_PRESETS = ("none", "sub9-best", "sub9-moderate", "sub9-worst", "hf-best",
                  "hf-moderate", "hf-worst", "aurora-best", "aurora-worst", "eme-focused")
SELECTIVE_MODULATION = ("off", "slow-1", "slow-5", "slow-10", "slow-25", "slow-65",
                        "fast-1", "fast-5", "fast-10", "fast-25", "fast-65",
                        "each-1", "each-5", "each-10", "each-25", "each-65")
CN0_DB_HZ = (200., 140., 120., 100., 80., 60., 55., 40., 32., 20., 6.,
             -6., -10., -16., -20., -23., -26., -30., -60.)
MARGINS = (1., 3.)
ADDED_SYMBOLS = (1, 10, 100, 1000)
FHSS = ("off", "envelope-only")
SYMBOL_BURST = (0, 3)
SAMPLE_BURST_TILE_WAITS = (0, 3)

# Requested axes only. Helper power and missed-hop arrangements are study
# conditions around this grid, not silently multiplied into its count.
AXES: dict[str, tuple[Any, ...]] = {
    "rate_hz": RATES_HZ,
    "data_carrier": BASEBAND_CARRIERS,
    "rf_shift_hz": RF_SHIFTS_HZ,
    "timing_policy": TIMING_POLICIES,
    "base_oscillator": BASE_OSCILLATORS,
    "shift_oscillator": SHIFT_OSCILLATORS,
    "spread_preset": SPREAD_PRESETS,
    "selective_modulation": SELECTIVE_MODULATION,
    "data_cn0_db_hz": CN0_DB_HZ,
    "margin": MARGINS,
    "added_symbols": ADDED_SYMBOLS,
    "fhss": FHSS,
    "symbol_burst": SYMBOL_BURST,
}

RATE_DEFAULT = 36.
PROTOCOL_SCHEMA = "triad-helper-broad-coverage-protocol-v2"
MAX_PLAN_PROBES = 256
MAX_PLAN_RUNTIME_SECONDS = 150.
SAMPLE_RATE_HZ = 18_000.
FRONTEND_HALF_WIDTH_HZ = 400.
HELPER_TONES_HZ = (550., 950., 2400.)


def cartesian_count(axes: dict[str, tuple[Any, ...]] = AXES) -> int:
    return math.prod(len(values) for values in axes.values())


def _base_row() -> dict[str, Any]:
    return {
        # Conservative, supported HF reference for one-factor sweeps. Stress
        # conditions are added explicitly below instead of changing context
        # independently for every marginal value.
        "rate_hz": 36., "data_carrier": "1725-hz", "rf_shift_hz": 30_000_000.,
        "timing_policy": "default-plusminus-several-seconds", "base_oscillator": "gpsdo-xo",
        "shift_oscillator": "gpsdo-xo", "spread_preset": "hf-best",
        "selective_modulation": "off", "data_cn0_db_hz": 20., "margin": 1.,
        "added_symbols": 10, "fhss": "off", "symbol_burst": 0,
    }


def _modulate(base: dict[str, Any], axis: str, value: Any) -> dict[str, Any]:
    row = dict(base)
    row[axis] = value
    return row


def representative_cases() -> list[dict[str, Any]]:
    """One-factor HF strata plus purposeful cross-axis stress interactions.

    Every requested level appears. This economical witness set is not a
    covering array and deliberately makes no claim about every level pair.
    Marginal cases hold every other factor at a single valid HF baseline.
    """
    baseline = _base_row()
    baseline["case_name"] = "valid-hf-one-factor-baseline"
    rows: list[dict[str, Any]] = [baseline]
    for axis, levels in AXES.items():
        for value in levels:
            rows.append(_modulate(_base_row(), axis, value))

    # High requested rates may exceed the selected Added Symbols regime. Keep a
    # second, explicitly named rate sweep at Added Symbols=1 to distinguish a
    # rate ceiling from a rate-times-spreading ceiling.
    for value in RATES_HZ:
        row = _modulate(_base_row(), "rate_hz", value)
        row["added_symbols"] = 1
        row["case_name"] = "rate-at-added-symbols-1"
        rows.append(row)

    def stress(name: str, **updates: Any) -> None:
        row = _base_row()
        row.update(updates)
        row["case_name"] = name
        rows.append(row)

    # Include the requested settings that collide with one another and the
    # most informative algorithms/resource boundaries.
    stress("vlf-maximum-margin", rate_hz=.01, data_carrier="8270-hz", rf_shift_hz=0.,
           timing_policy="bounded-10ms-1ms-0ms-audio70ms", base_oscillator="gpsdo-ocxo",
           shift_oscillator="baseband-clock", spread_preset="sub9-worst",
           selective_modulation="each-65", data_cn0_db_hz=-60., margin=3.,
           added_symbols=1000, fhss="off", symbol_burst=0)
    stress("minimum-spacing-faster-ray", rate_hz=.001, data_carrier="half-rate", rf_shift_hz=500_000.,
           timing_policy="default-plusminus-several-seconds", base_oscillator="crystal",
           shift_oscillator="cb-ssb", spread_preset="sub9-worst", selective_modulation="each-65",
           data_cn0_db_hz=-60., margin=3., added_symbols=1000, fhss="off", symbol_burst=3)
    stress("none-spread-amplitude-modulation-rate-override", rate_hz=36., data_carrier="1725-hz",
           rf_shift_hz=30_000_000., base_oscillator="gpsdo-xo", shift_oscillator="gpsdo-xo",
           spread_preset="none", selective_modulation="each-65", selective_modulation_rate_hz=1.,
           data_cn0_db_hz=6., margin=1., added_symbols=10, fhss="off", symbol_burst=0)
    stress("hf-70ms-arrival-dispersive", rate_hz=36., data_carrier="1725-hz", rf_shift_hz=30_000_000.,
           timing_policy="bounded-10ms-1ms-0ms-audio70ms", base_oscillator="crystal",
           shift_oscillator="ic-7100", spread_preset="hf-worst", selective_modulation="each-65",
           data_cn0_db_hz=8., margin=3., added_symbols=10, fhss="off", symbol_burst=0)
    stress("aurora-each65-fhss-burst", rate_hz=360., data_carrier="1725-hz", rf_shift_hz=30_000_000.,
           timing_policy="default-plusminus-several-seconds", base_oscillator="gpsdo-tcxo",
           shift_oscillator="gpsdo-ocxo", spread_preset="aurora-worst", selective_modulation="each-65",
           data_cn0_db_hz=-30., margin=3., added_symbols=100, fhss="envelope-only", symbol_burst=3)
    rows[-1]["burst_wait_tiles"] = 3
    stress("eme-maximum-modulation", rate_hz=360., data_carrier="1725-hz", rf_shift_hz=5_800_000_000.,
           timing_policy="bounded-10ms-1ms-0ms-audio70ms", base_oscillator="gpsdo-ocxo",
           shift_oscillator="gpsdo-ocxo", spread_preset="eme-focused", selective_modulation="each-65",
           data_cn0_db_hz=1.5, margin=1., added_symbols=1, fhss="envelope-only", symbol_burst=0)
    stress("high-rate-frontend-boundary", rate_hz=3600., data_carrier="1725-hz", rf_shift_hz=14_000_000.,
           timing_policy="bounded-10ms-1ms-0ms-audio70ms", base_oscillator="gpsdo-tcxo",
           shift_oscillator="ic-7100", spread_preset="hf-moderate", selective_modulation="fast-65",
           data_cn0_db_hz=-16., margin=1., added_symbols=1, fhss="envelope-only", symbol_burst=0)
    stress("high-rate-unsupported-ceiling", rate_hz=30_000_000., data_carrier="8270-hz",
           rf_shift_hz=5_800_000_000., timing_policy="default-plusminus-several-seconds",
           base_oscillator="crystal", shift_oscillator="cb-ssb", spread_preset="eme-focused",
           selective_modulation="each-25", data_cn0_db_hz=200., margin=1., added_symbols=1,
           fhss="off", symbol_burst=0)
    return deduplicate(rows)


def deduplicate(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    seen: set[str] = set()
    result = []
    for source in rows:
        row = dict(source)
        signature = json.dumps({k: row[k] for k in AXES} |
                               {k: row[k] for k in ("selective_modulation_rate_hz", "burst_wait_tiles", "case_name") if k in row},
                               sort_keys=True, separators=(",", ":"))
        if signature in seen:
            continue
        seen.add(signature)
        result.append(row)
    return result


def helper_power_conditions(data_cn0_db_hz: float) -> list[dict[str, Any]]:
    """Resolve fixed HF/EME link ratios and the maximum Helper-to-DATA cap."""
    hf_margin_db = 10. * math.log10(40. / 4.)
    eme_margin_db = 10. * math.log10(15000. / 40.)
    maximum_ratio_db = 90.
    return [
        {"id": "fixed-hf-ratio-40w-to-4w", "data_cn0_db_hz": data_cn0_db_hz,
         "data_power_w": 4., "helper_power_w": 40., "helper_minus_data_db": hf_margin_db,
         "helper_cn0_db_hz": data_cn0_db_hz + hf_margin_db,
         "status": "derived-fixed-power-ratio"},
        {"id": "fixed-eme-ratio-15000w-to-40w", "data_cn0_db_hz": data_cn0_db_hz,
         "data_power_w": 40., "helper_power_w": 15000., "helper_minus_data_db": eme_margin_db,
         "helper_cn0_db_hz": data_cn0_db_hz + eme_margin_db,
         "status": "derived-fixed-power-ratio"},
        {"id": "maximum-authorized-helper-to-data-ratio", "data_cn0_db_hz": data_cn0_db_hz,
         "data_power_w": 40., "helper_power_w": 40. * 10 ** (maximum_ratio_db / 10.),
         "helper_minus_data_db": maximum_ratio_db,
         "helper_cn0_db_hz": data_cn0_db_hz + maximum_ratio_db,
         "status": "derived-90db-plan-cap-not-independent-reception"},
    ]


def missed_hop_cases() -> list[dict[str, Any]]:
    return [
        {"id": "none", "dwell_mask": []},
        {"id": "first-hop", "dwell_mask": [0]},
        {"id": "last-hop", "dwell_mask": [-1]},
        {"id": "end-of-cycle-plus-next-start", "dwell_mask": [-1, 0]},
        {"id": "adjacent-middle-pair", "dwell_mask": ["middle", "middle+1"]},
        {"id": "four-dispersed-hops-one-symbol", "dwell_mask": ["seeded-four-spread-positions"]},
        {"id": "central-hop", "dwell_mask": ["middle"]},
    ]


def nominal_bound_diagnostics(row: dict[str, Any]) -> dict[str, Any]:
    """Only apply explicit necessary checks; defer sufficiency to native plan."""
    if row["data_carrier"] == "1725-hz":
        center = 1725.
    elif row["data_carrier"] == "8270-hz":
        center = 8270.
    elif row["data_carrier"] == "half-rate":
        center = float(row["rate_hz"]) / 2.
    else:
        raise ValueError(f"unknown DATA center mode {row['data_carrier']!r}")
    codes = []
    if center < 1725.:
        codes.append("helper-standard-data-center-floor-violation")
    if center >= SAMPLE_RATE_HZ / 2.:
        codes.append("nominal-center-at-or-above-nyquist")
    # The simulation captures complex zero-centered DATA; the configured IF is
    # removed by direct conversion. Check the nominal composite audio support
    # around that IF, without confusing the 900-Hz post-DDC receiver filter
    # with an absolute audio-frequency limit.
    data_edges = (center - 112.5, center + 112.5)
    helper_edges = HELPER_TONES_HZ
    required_audio_edge = max(abs(value) for value in (*data_edges, *helper_edges))
    nominal_capture_support = required_audio_edge <= SAMPLE_RATE_HZ / 2.
    if not nominal_capture_support:
        codes.append("nominal-composite-audio-spectrum-outside-18khz-capture")
    post_ddc_required_halfwidth = max(112.5, *(abs(tone - center) for tone in HELPER_TONES_HZ))
    # GUI rate maps to one-sixth as many fine pattern symbols per second;
    # Added Symbols then multiplies that rate, matching native v3 planning.
    fine_rate = float(row["rate_hz"]) * int(row["added_symbols"]) / 6.
    if fine_rate <= 0 or not math.isfinite(fine_rate):
        raise ValueError("fine rate must be positive and finite")
    if row["fhss"] == "envelope-only" and 8. / fine_rate > .4:
        codes.append("no-complete-eight-chip-pulse-per-dwell")
    if 8. / fine_rate > 38.:
        codes.append("pulse-halo-exceeds-fixed-38s-helper-lead")
    return {
        "data_center_hz": center,
        "fine_rate_hz": fine_rate,
        "pulse_halo_seconds": 8. / fine_rate,
        "nominal_composite_audio_edge_hz": required_audio_edge,
        "nominal_capture_support_necessary_check": nominal_capture_support,
        "post_ddc_required_halfwidth_hz": post_ddc_required_halfwidth,
        "post_ddc_frontend_half_width_hz": FRONTEND_HALF_WIDTH_HZ,
        "necessary_rejection_codes": codes,
        "helper_placement_status": "native-plan-only",
        "capture_cfo_support_status": "native-plan-only",
        "complete_data_pilot_sideband_status": "native-plan-only",
        "necessary_bound_status": "rejected" if codes else "not-rejected-by-this-necessary-check",
        "overall_support_status": ("unsupported-by-at-least-one-necessary-bound" if codes
                                   else "unqualified-needs-native-plan-and-sampled-receiver"),
        "scope": "Necessary nominal center/shape guards only. No channel or Helper-placement sufficiency inferred.",
    }


def projection(row: dict[str, Any], power: dict[str, Any]) -> list[str]:
    center = nominal_bound_diagnostics(row)["data_center_hz"]
    case = "hf" if power["id"].startswith("fixed-hf-") else "eme"
    args = ["--case", case, "--plan-only", "--rate", format(float(row["rate_hz"]), ".17g"),
            "--added-symbols", str(row["added_symbols"]), "--fhss", "on" if row["fhss"] == "envelope-only" else "off",
            "--data-if", format(center, ".17g"), "--rf-hz", format(float(row["rf_shift_hz"]), ".17g"),
            "--base-oscillator", row["base_oscillator"], "--shift-oscillator", row["shift_oscillator"],
            "--spread-preset", row["spread_preset"], "--margin", format(float(row["margin"]), ".17g"),
            "--selective-perturbation", row["selective_modulation"],
            "--timing-policy", ("bounded" if row["timing_policy"].startswith("bounded") else "broad"),
            "--data-power-w", format(float(power["data_power_w"]), ".17g"),
            "--helper-power-w", format(float(power["helper_power_w"]), ".17g")]
    if "selective_modulation_rate_hz" in row:
        args += ["--selective-modulation-rate-hz",
                 format(float(row["selective_modulation_rate_hz"]), ".17g")]
    if "burst_wait_tiles" in row:
        args += ["--burst-wait-tiles", str(int(row["burst_wait_tiles"]))]
    if row["shift_oscillator"] == "baseband-clock":
        args[args.index("--shift-oscillator") + 1] = "baseband-clock"
    return args


def source_digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def build_protocol() -> dict[str, Any]:
    configs = representative_cases()
    baseline = {key: _base_row()[key] for key in AXES}
    return {
        "schema": PROTOCOL_SCHEMA,
        "protocol_definition_status": "fixed-representative-order-before-native-export",
        "requested_cartesian_count": cartesian_count(),
        "requested_cartesian_formula": "17*3*9*2*4*7*10*16*19*2*4*2*2",
        "requested_axes": {key: list(values) for key, values in AXES.items()},
        "factorized_representative_plan": {
            "rows": configs,
            "row_count": len(configs),
            "marginal_coverage": {axis: sorted({str(row[axis]) for row in configs}) for axis in AXES},
            "complete_marginal_values": {axis: set(map(str, levels)).issubset(
                                         set(str(row[axis]) for row in configs))
                                         for axis, levels in AXES.items()},
            "one_factor_design": {
                "baseline_id": "valid-hf-one-factor-baseline",
                "baseline": baseline,
                "rule": "Each requested marginal row changes one axis from baseline; the additional rate-at-added-symbols-1 stratum changes only rate within its explicitly stated Added Symbols=1 context.",
                "supplementary_rate_rows": sum(
                    row.get("case_name") == "rate-at-added-symbols-1" for row in configs),
            },
            "supplemental_values_outside_requested_axes": {
                axis: sorted(set(str(row[axis]) for row in configs) - set(map(str, levels)))
                for axis, levels in AXES.items()
                if set(str(row[axis]) for row in configs) - set(map(str, levels))
            },
            "coverage_claim": "Every requested marginal level plus named interaction probes; no all-pair or Cartesian receiver coverage claim.",
        },
        "helper_power_families": {
            "description": "Three explicitly derived power conditions; power is not another crossed axis.",
            "weakest_cn0_examples": helper_power_conditions(-60.),
        "fixed_ratio_baselines": ["40 W Helper / 4 W DATA, as in HF case",
                                  "15,000 W Helper / 40 W DATA, as in strong EME case"],
            "maximum_ratio_db": 90.,
        },
        "missed_hop_patterns": missed_hop_cases(),
        "separate_tile_wait_probe_values": list(SAMPLE_BURST_TILE_WAITS),
        "scope_partition": {
            "native_plan_only": "geometry, pulse/dwell fit, Helper placement/power acceptance, CFO/capture limits",
            "sampled_waveform": "emitted I/Q, encryption/keyed source, sidebands, filter/channel tails, dwell erasures",
            "receiver": "calibrated presence and bit search; correct/wrong/missed/false admissions separately",
            "reception_status_initial": "untested",
        },
        "known_model_limits": [
            "The 18 kHz fixture has 9 kHz Nyquist; full nominal composite audio support, oscillator allowances and skirts still require native planning. The 900 Hz receive frontend is post-DDC and is not an absolute IF ceiling.",
            "A DATA center below 1725 Hz violates the current Helper standard floor.",
            "Sub-9 kHz compatibility requires the full DATA triplet, high pilot, sidebands and guards to fit; 8270 Hz center alone does not prove it.",
            "Envelope-only FHSS preserves baseband calendar effects; it is not physical RF retune or a missed RF-hop simulation.",
            "The user Symbol Burst 3x axis is recorded as a complete-bit burst request; it must not be represented as three within-symbol wait tiles. Native Helper exporter cannot encode the complete-bit burst request yet.",
            "Spread None bypasses added frequency-selective modulation unless --selective-modulation-rate-hz is explicitly supplied; modulation-only stress is a separate probe condition.",
            "The fixed 38 second Helper lead rejects pulse halo durations above 38 seconds; exporter capture caps and workspace caps are separate implementation constraints.",
            "DSSS is represented by the Added Symbols multiplier; the v3 format retains its whole-symbol interleave.",
            "GPSDO oscillator presets do not establish host clock synchronization.",
            "A plan or configuration probe is not an actual transmission, physical-radio result, or receiver admission.",
        ],
        "runs": [],
        "status": "protocol-frozen-no-native-or-receiver-work-run",
    }


def native_plan_probe(rows: list[dict[str, Any]], exporter: Path, timeout_seconds: float = 20.,
                      maximum_total_seconds: float = MAX_PLAN_RUNTIME_SECONDS) -> dict[str, Any]:
    if len(rows) > MAX_PLAN_PROBES:
        raise ValueError(f"plan probe cap is {MAX_PLAN_PROBES} rows")
    if (not math.isfinite(maximum_total_seconds) or maximum_total_seconds <= 0 or
            maximum_total_seconds > MAX_PLAN_RUNTIME_SECONDS):
        raise ValueError(f"total native plan budget must be in (0,{MAX_PLAN_RUNTIME_SECONDS:g}] seconds")
    binary = exporter.resolve(strict=True)
    if not binary.is_file() or not binary.stat().st_mode & 0o111:
        raise ValueError("native exporter must be an executable file")
    binary_hash = source_digest(binary)
    tool_path = Path(__file__).resolve()
    tool_hash = source_digest(tool_path)
    start = time.perf_counter()
    result_rows = []
    for index, row in enumerate(rows):
        if time.perf_counter() - start >= maximum_total_seconds:
            for pending in range(index, len(rows)):
                result_rows.append({"row": pending, "request": rows[pending],
                                    "status": "not-run-total-runtime-budget-exhausted",
                                    "receiver_status": "untested", "wall_seconds": 0.})
            break
        command = [str(binary), *projection(row, helper_power_conditions(row["data_cn0_db_hz"])[0]),
                   "--cn0", format(float(row["data_cn0_db_hz"]), ".17g")]
        before = time.perf_counter()
        try:
            process = subprocess.run(command, capture_output=True, text=True, timeout=timeout_seconds)
            try:
                native = json.loads(process.stdout)
            except json.JSONDecodeError:
                native = None
            result_rows.append({"row": index, "request": row, "command": command,
                                "returncode": process.returncode, "native_plan": native,
                                "stdout": process.stdout if native is None else None,
                                "stderr": process.stderr,
                                "status": "plan-returned" if process.returncode == 0 and native is not None else "native-plan-rejected-or-invalid",
                                "unrepresented_requested_dimensions": (["symbol_burst"] if row["symbol_burst"] else []),
                                "selective_modulation_bypassed_by_none_spread": (
                                    row["spread_preset"] == "none" and
                                    row["selective_modulation"] != "off" and
                                    "selective_modulation_rate_hz" not in row),
                                "receiver_status": "untested", "wall_seconds": time.perf_counter()-before})
        except subprocess.TimeoutExpired as exc:
            result_rows.append({"row": index, "request": row, "command": command,
                                "status": "native-plan-timeout", "receiver_status": "untested",
                                "unrepresented_requested_dimensions": (["symbol_burst"] if row["symbol_burst"] else []),
                                "selective_modulation_bypassed_by_none_spread": (
                                    row["spread_preset"] == "none" and
                                    row["selective_modulation"] != "off" and
                                    "selective_modulation_rate_hz" not in row),
                                "stdout": exc.stdout, "stderr": exc.stderr,
                                "wall_seconds": time.perf_counter()-before})
    after_hash = source_digest(binary)
    tool_hash_after = source_digest(tool_path)
    def summarize(selected: list[dict[str, Any]]) -> dict[str, Any]:
        planned = [r for r in selected if r.get("native_plan") is not None]
        represented = [r for r in selected if not r.get("unrepresented_requested_dimensions")]
        represented_plans = [r for r in represented if r.get("native_plan") is not None]
        known_rejections = [r for r in represented if
                            r.get("status") == "native-plan-rejected-or-invalid"]
        geometry_yes = [r for r in planned if
                        r["native_plan"].get("geometry_feasible") is True]
        placement_yes = [r for r in planned if
                         r["native_plan"].get("helper_placement", {}).get(
                             "feasible_at_requested_if") is True]
        both_yes = [r for r in planned if
                    r["native_plan"].get("geometry_feasible") is True and
                    r["native_plan"].get("helper_placement", {}).get(
                        "feasible_at_requested_if") is True]
        lead_yes = [r for r in planned if
                    r["native_plan"].get("export_constraints", {}).get(
                        "pulse_halo_fits_lead") is True]
        all_yes = [r for r in represented_plans if
                   r["native_plan"].get("geometry_feasible") is True and
                   r["native_plan"].get("helper_placement", {}).get(
                       "feasible_at_requested_if") is True and
                   r["native_plan"].get("export_constraints", {}).get(
                       "pulse_halo_fits_lead") is True]
        if not represented:
            support_classification = "requested-dimension-unrepresented"
            supported = None
        elif all_yes:
            support_classification = "native-plan-feasible-in-sampled-configuration"
            supported = True
        elif known_rejections:
            support_classification = "exporter-rejected-in-sampled-configuration"
            supported = False
        elif represented_plans:
            support_classification = "native-plan-infeasible-in-sampled-configuration"
            supported = False
        else:
            support_classification = "not-evaluated-no-native-result"
            supported = None
        return {
            "attempted_rows": len(selected),
            "native_plan_returned_rows": len(planned),
            "native_plan_rejected_rows": sum(r["status"] == "native-plan-rejected-or-invalid" for r in selected),
            "native_plan_timeout_rows": sum(r["status"] == "native-plan-timeout" for r in selected),
            "not_run_rows": sum(r["status"] == "not-run-total-runtime-budget-exhausted" for r in selected),
            "represented_requested_rows": len(represented),
            "unrepresented_requested_rows": len(selected) - len(represented),
            "geometry_feasible_rows": len(geometry_yes),
            "helper_placement_feasible_rows": len(placement_yes),
            "pulse_halo_fits_helper_lead_rows": len(lead_yes),
            "geometry_and_helper_feasible_rows": len(both_yes),
            "geometry_helper_and_lead_feasible_rows": len(all_yes),
            "supported_in_sample": supported,
            "support_classification": support_classification,
            "classification_scope": "observed native plan rows only; no unprobed combinations or reception claims",
        }

    # Named interaction cases have different contexts by design. Do not let a
    # passing cross-stress accidentally turn a failed one-factor marginal into
    # an apparent supported level.
    marginal_rows = [r for r in result_rows if
                     "case_name" not in r.get("request", {}) or
                     r["request"].get("case_name") == "valid-hf-one-factor-baseline"]
    marginal_outcomes: dict[str, Any] = {}
    for axis, levels in AXES.items():
        by_value = {}
        for level in levels:
            selected = [r for r in marginal_rows if axis in r.get("request", {}) and
                        r["request"][axis] == level]
            by_value[str(level)] = summarize(selected)
        marginal_outcomes[axis] = by_value
    rate_added1_outcomes = {
        str(rate): summarize([r for r in result_rows if
                              r.get("request", {}).get("case_name") ==
                              "rate-at-added-symbols-1" and
                              r["request"]["rate_hz"] == rate])
        for rate in RATES_HZ
    }
    return {"schema": "triad-helper-native-plan-probes-v2", "binary": str(binary),
            "binary_sha256_before": binary_hash, "binary_sha256_after": after_hash,
            "tool_sha256_before": tool_hash, "tool_sha256_after": tool_hash_after,
            "identity_stable": binary_hash == after_hash and tool_hash == tool_hash_after,
            "comparison_valid": binary_hash == after_hash and tool_hash == tool_hash_after,
            "probe_limit_rows": MAX_PLAN_PROBES,
            "runtime_budget_seconds": maximum_total_seconds,
            "attempted_rows": sum(r["wall_seconds"] > 0 for r in result_rows),
            "not_run_rows": sum(r["status"] == "not-run-total-runtime-budget-exhausted" for r in result_rows),
            "results": result_rows,
            "marginal_outcomes": marginal_outcomes,
            "supplementary_rate_at_added_symbols_1": rate_added1_outcomes,
            "receiver_status": "untested", "total_wall_seconds": sum(r["wall_seconds"] for r in result_rows)}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--exporter", type=Path,
                        help="optional built triad_helper_export for bounded native --plan-only probes")
    parser.add_argument("--plan-count", type=int, default=0,
                        help=f"probe the first N representative rows, maximum {MAX_PLAN_PROBES}; zero performs no native work")
    parser.add_argument("--timeout-seconds", type=float, default=20.)
    parser.add_argument("--maximum-plan-runtime-seconds", type=float,
                        default=MAX_PLAN_RUNTIME_SECONDS)
    args = parser.parse_args(argv)
    if args.output.exists():
        parser.error("output exists; preserve old evidence")
    if not 0 <= args.plan_count <= MAX_PLAN_PROBES:
        parser.error(f"--plan-count must be between 0 and {MAX_PLAN_PROBES}")
    if not math.isfinite(args.timeout_seconds) or args.timeout_seconds <= 0:
        parser.error("--timeout-seconds must be positive and finite")
    if (not math.isfinite(args.maximum_plan_runtime_seconds) or args.maximum_plan_runtime_seconds <= 0 or
            args.maximum_plan_runtime_seconds > MAX_PLAN_RUNTIME_SECONDS):
        parser.error(f"--maximum-plan-runtime-seconds must be in (0,{MAX_PLAN_RUNTIME_SECONDS:g}]")
    if args.plan_count and args.exporter is None:
        parser.error("--exporter is required when --plan-count is nonzero")
    protocol = build_protocol()
    protocol["tool_sha256"] = source_digest(Path(__file__).resolve())
    protocol["test_source_sha256"] = source_digest(Path(__file__).with_name("..").resolve() / "tests" / "test_triad_helper_coverage.py")
    protocol["status"] = "protocol-frozen-before-native-probes"
    if args.plan_count:
        selected = protocol["factorized_representative_plan"]["rows"][:args.plan_count]
        protocol["native_plan_probes"] = native_plan_probe(
            selected, args.exporter, args.timeout_seconds, args.maximum_plan_runtime_seconds)
        protocol["status"] = "bounded-native-plan-only-probes-complete"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(protocol, stream, indent=2, allow_nan=False)
        stream.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
