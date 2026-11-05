#!/usr/bin/env python3
"""Fast factorized necessary-constraint census for the Helper triad catalog.

The residual class deliberately means *not excluded by this small set of
necessary constraints*. It is not a native plan, sampled waveform, or receive
success. Use the exporter and its receiver campaign for those claims.
"""
from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path
from typing import Any, Iterable

from triad_helper_coverage import AXES, cartesian_count

SAMPLE_RATE_HZ = 18_000.0
NYQUIST_HZ = SAMPLE_RATE_HZ / 2
HELPER_LEAD_SECONDS = 38.0
MAX_ADDED_SYMBOLS = 1000
MAX_RATE_HZ = 30_000_000.0
RESIDUAL = "not-excluded-unqualified"

# Mutually exclusive classification precedence. Secondary rule counts below
# remain nonexclusive so a rejected cell's additional defects are not hidden.
RULES = (
    "whole-bit-symbol-burst-unsupported",
    "data-center-below-helper-floor",
    "data-center-at-or-above-nyquist",
    "nominal-three-carrier-band-exceeds-nyquist",
    "nominal-data-half-bandwidth-exceeds-wide-frontend",
    "sample-rate-too-low-for-pattern",
    "pulse-halo-exceeds-helper-lead",
)


def resolve_center(row: dict[str, Any]) -> float:
    mode = row["data_carrier"]
    if mode == "1725-hz":
        return 1725.0
    if mode == "8270-hz":
        return 8270.0
    if mode == "half-rate":
        return float(row["rate_hz"]) / 2.0
    raise ValueError(f"unsupported data_carrier: {mode!r}")


def geometry_diagnostics(rate: float, center: float, added: int, symbol_burst: int = 0) -> dict[str, Any]:
    """Evaluate the public necessary geometry at arbitrary boundary values."""
    if not math.isfinite(rate) or rate <= 0 or rate > MAX_RATE_HZ:
        raise ValueError("Rate is outside the maintained source format range")
    if not 1 <= added <= MAX_ADDED_SYMBOLS:
        raise ValueError("Added Symbols is outside the maintained source format range")
    if not math.isfinite(center) or center < 0:
        raise ValueError("DATA center must be finite and nonnegative")
    # Match the native evaluation order: inner_rate / 6, then Added Symbols.
    fine = (rate / 6.0) * added
    spacing = 1.25 * fine
    occupied = 3.0 * spacing
    halo = 8.0 / fine
    halo_samples = math.ceil(8.0 * SAMPLE_RATE_HZ / fine) + 2
    codes: list[str] = []
    if symbol_burst != 0:
        codes.append("whole-bit-symbol-burst-unsupported")
    if center < 1725.0:
        codes.append("data-center-below-helper-floor")
    if center >= NYQUIST_HZ:
        codes.append("data-center-at-or-above-nyquist")
    if center + occupied / 2.0 >= NYQUIST_HZ:
        codes.append("nominal-three-carrier-band-exceeds-nyquist")
    if occupied / 2.0 > 400.0:
        codes.append("nominal-data-half-bandwidth-exceeds-wide-frontend")
    if SAMPLE_RATE_HZ < 4.0 * fine or not SAMPLE_RATE_HZ > 1.1 * occupied:
        codes.append("sample-rate-too-low-for-pattern")
    if halo_samples > HELPER_LEAD_SECONDS * SAMPLE_RATE_HZ:
        codes.append("pulse-halo-exceeds-helper-lead")
    primary = next((code for code in RULES if code in codes), RESIDUAL)
    return {
        "classification": primary,
        "secondary_necessary_failures": codes,
        "data_center_hz": center,
        "fine_chip_rate_hz": fine,
        "carrier_spacing_hz": spacing,
        "nominal_occupied_bandwidth_hz": occupied,
        "pulse_halo_seconds": halo,
        "pulse_halo_samples_including_two_sample_guard": halo_samples,
        "fixed_helper_lead_samples": HELPER_LEAD_SECONDS * SAMPLE_RATE_HZ,
        "wide_frontend_pass_edge_hz": 400.0,
        "nominal_data_half_bandwidth_hz": occupied / 2.0,
        "scope": "Fixed 18 kHz Helper exporter; necessary constraints only. Residual is unqualified, not supported or received.",
    }


def diagnose(row: dict[str, Any]) -> dict[str, Any]:
    """Classify one requested tuple using hard necessary constraints only."""
    for axis, choices in AXES.items():
        if axis not in row or row[axis] not in choices:
            raise ValueError(f"{axis} must be one of the requested axis values")
    rate = float(row["rate_hz"])
    added = int(row["added_symbols"])
    if not math.isfinite(rate) or rate <= 0 or rate > MAX_RATE_HZ:
        raise ValueError("Rate is outside the maintained source format range")
    if not 1 <= added <= MAX_ADDED_SYMBOLS:
        raise ValueError("Added Symbols is outside the maintained source format range")
    center = resolve_center(row)
    return geometry_diagnostics(rate, center, added, int(row["symbol_burst"]))


def _checkable_projection_rows(axes: dict[str, tuple[Any, ...]] = AXES) -> Iterable[dict[str, Any]]:
    """Enumerate only axes that affect these predicates; weight all others."""
    relevant = ("rate_hz", "data_carrier", "added_symbols", "symbol_burst")
    varying = tuple(name for name in relevant if name in axes)
    for values in itertools.product(*(axes[name] for name in varying)):
        yield dict(zip(varying, values))


def _weighted_census(axes: dict[str, tuple[Any, ...]] = AXES) -> dict[str, Any]:
    varying_names = ("rate_hz", "data_carrier", "added_symbols", "symbol_burst")
    varying = tuple(name for name in varying_names if name in axes)
    all_count = math.prod(len(values) for values in axes.values())
    projected_count = math.prod(len(axes[a]) for a in varying)
    weight = all_count // projected_count
    exclusive = {name: 0 for name in (*RULES, RESIDUAL)}
    nonexclusive = {name: 0 for name in RULES}
    witnesses: dict[str, dict[str, Any]] = {}
    for row in _checkable_projection_rows(axes):
        result = diagnose(_complete_row(row))
        label = result["classification"]
        exclusive[label] += weight
        for failure in result["secondary_necessary_failures"]:
            nonexclusive[failure] += weight
            witnesses.setdefault(failure, {**row, **result})
    if sum(exclusive.values()) != all_count:
        raise AssertionError("factored census did not conserve the Cartesian total")
    return {
        "requested_cartesian_count": all_count,
        "factorized_projection_count": projected_count,
        "weight_per_projection": weight,
        "mutually_exclusive_cell_counts": exclusive,
        "per_rule_nonexclusive_cell_counts": nonexclusive,
        "necessary_failure_witnesses": witnesses,
    }


def _complete_row(partial: dict[str, Any]) -> dict[str, Any]:
    """Fill classifier-irrelevant axes with canonical declared values."""
    defaults = {axis: values[0] for axis, values in AXES.items()}
    defaults.update(partial)
    # Use FHSS off and the baseline timing/oscillator/context for readable witnesses.
    defaults.update({
        "rf_shift_hz": 30_000_000.0,
        "timing_policy": "default-plusminus-several-seconds",
        "base_oscillator": "gpsdo-xo",
        "shift_oscillator": "gpsdo-xo",
        "spread_preset": "hf-best",
        "selective_modulation": "off",
        "data_cn0_db_hz": 20.0,
        "margin": 1.0,
        "fhss": "off",
    })
    return defaults


def known_waveform_presence_duration(cn0_db_hz: float, required_rho: float) -> float:
    """Ideal one-test known deterministic waveform energy/noise duration.

    rho is signal energy divided by noise spectral density N0, i.e. E/N0.
    This is an optimistic reference only; it includes no acquisition or channel
    uncertainty and does not represent bit discrimination.
    """
    if not math.isfinite(cn0_db_hz) or not math.isfinite(required_rho) or required_rho <= 0:
        raise ValueError("finite C/N0 and positive finite rho required")
    return required_rho / (10.0 ** (cn0_db_hz / 10.0))


def energy_reference(cn0_db_hz: float, data_power_w: float = 4.0,
                     helper_power_w: float = 40.0) -> dict[str, Any]:
    if not math.isfinite(data_power_w) or data_power_w <= 0:
        raise ValueError("DATA power must be positive")
    if not math.isfinite(helper_power_w) or helper_power_w < 0:
        raise ValueError("Helper power must be nonnegative")
    helper_cn0 = (cn0_db_hz + 10 * math.log10(helper_power_w / data_power_w)
                  if helper_power_w else None)
    # Favorable one-hypothesis known-waveform presence examples; repeated
    # searches, unknown channels and bit decisions need different thresholds.
    cases = {"pd90_fa01_rho": 6.5085, "pd99_fa1e-4_rho": 18.2732}
    return {
        "data_cn0_db_hz": cn0_db_hz,
        "helper_cn0_db_hz": helper_cn0,
        "helper_minus_data_db": (10 * math.log10(helper_power_w / data_power_w)
                                  if helper_power_w else None),
        "ideal_known_waveform_presence_seconds": {
            name: known_waveform_presence_duration(cn0_db_hz, rho)
            for name, rho in cases.items()
        },
        "assumptions": "Single known deterministic waveform test in white complex Gaussian noise; no acquisition, channel uncertainty, outages, bit discrimination, or repeated looks.",
    }


def source_digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def build_report() -> dict[str, Any]:
    census = _weighted_census()
    root = Path(__file__).resolve().parents[1]
    source = Path(__file__).with_name("triad_helper_coverage.py")
    return {
        "schema": "triad-helper-domain-census-v1",
        "source_sha256": source_digest(source),
        "classifier_sha256": source_digest(Path(__file__)),
        "native_source_identities": {
            path.relative_to(root).as_posix(): source_digest(path)
            for path in (root / "src/triad_modem_v3.cpp",
                         root / "src/helper_carrier.cpp",
                         root / "tools/triad-helper-export.cpp",
                         root / "tools/triad_helper_receiver.py")
        },
        "axes": {name: list(values) for name, values in AXES.items()},
        **census,
        "classification_precedence": list(RULES) + [RESIDUAL],
        "factorization": {
            "varied_axes": ["rate_hz", "data_carrier", "added_symbols", "symbol_burst"],
            "weighted_axes": [axis for axis in AXES if axis not in
                               ("rate_hz", "data_carrier", "added_symbols", "symbol_burst")],
            "claim": "All declared tuples are counted exactly under predicates depending only on the varied axes. This is not Cartesian native-plan or receiver qualification.",
        },
        "assumptions_and_unmodeled_constraints": [
            "Fixed baseline DATA/audio sample rate 18 kHz (the current exporter default) and the 400 Hz wide complex receive frontend; no implicit sample-rate or IF adjustment. Exporter opt-in sample rates are outside this census.",
            "DATA center modes resolve to 1725 Hz, half the GUI rate, or 8270 Hz. The Helper floor is 1725 Hz.",
            "Nominal occupied width is three carriers at spacing 1.25 times fine-chip rate; compact bandwidth is only a necessary geometry screen, not a spectral-tail or leakage guarantee.",
            "Source constraints mirror v3: fine=(Rate/6)*Added, rate <=30 MHz, Added Symbols 1..1000, Fs >=4*fine, Fs >1.1*occupied, and calendar origin ceil(8*Fs/fine)+2 <=38*Fs. The 400 Hz frontend check rejects nominal DATA half-bandwidth >400 Hz only; oscillator/channel-line allowance is not included. Tile workspace, exact protected cells, auto shaping, Helper placement, oscillator capture and output-size conditions remain native-plan-only.",
            "The requested whole-bit Symbol Burst=3 axis has no matching Helper exporter calendar; the separately implemented within-symbol wait tiles do not satisfy it.",
            "FHSS dimension is envelope-only in this exporter; physical RF retuning and missed RF-hop recovery remain unqualified.",
            "All residual cells require native plan and representative sampled/receiver evidence. Neither favorable channel settings nor high C/N0 change that label.",
        ],
        "status": "necessary-constraint-census-only",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = build_report()
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded)
    else:
        print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
