#!/usr/bin/env python3
"""Frozen physical-parameter selection and descriptive HF coverage bookkeeping.

This selects manifest parameters only. It renders no signal, imports no receiver
template bank, and never changes the design in response to trial outcomes.
Bin and pair counts describe this constructed coordinate box, not natural-day
probabilities or a guarantee over its continuous interior.
"""
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
import hashlib
from itertools import combinations
import json
import math
from pathlib import Path
from typing import Any


HERE = Path(__file__).resolve().parent
DESIGN_VERSION = "hf-domain-v1"
DESIGN_SEED = 20261114
POINTS = 8
SCENARIO = "hf-ensemble-design"
SCREEN_NAME = "hf-domain-screen"
RECEIVER_NAME = "hf-domain-receiver"
NATIVE_STATUSES = {"completed", "resource_limit", "time_limit", "unavailable", "error"}
TRIAL_STATUSES = NATIVE_STATUSES | {"launch_error", "timed_out", "receipt_input_mismatch",
                                  "channel_empty", "receipt_exit_mismatch", "invalid_receipt", "probe_changed"}
SCOPE = ("Descriptive coverage of a frozen constructed parameter box only; "
         "not a natural-condition probability, a continuous worst-case guarantee, "
         "or a communications/capacity qualification.")


@dataclass(frozen=True)
class Axis:
    name: str
    low: float
    high: float
    scale: str = "linear"

    def value(self, unit: float) -> float:
        if not math.isfinite(unit) or not 0 <= unit <= 1:
            raise ValueError("unit coordinate must lie in [0, 1]")
        if unit == 0:
            return self.low
        if unit == 1:
            return self.high
        if self.scale == "log":
            return self.low * math.exp(math.log(self.high / self.low) * unit)
        return self.low + (self.high - self.low) * unit


AXES = (
    Axis("hf_primary_amplitude_hz", 0, 20000),
    Axis("hf_primary_period_seconds", 110, 480, "log"),
    Axis("hf_primary_phase_radians", -math.pi, math.pi),
    Axis("hf_secondary_amplitude_hz", 0, 20000),
    Axis("hf_secondary_period_seconds", 480, 1400, "log"),
    Axis("hf_secondary_phase_radians", -math.pi, math.pi),
    Axis("hf_knot_amplitude_hz", 0, 20000),
    Axis("hf_knot_seconds", 70, 200, "log"),
    Axis("hf_trajectory_offset_seconds", 0, 1200),
)
PHYSICAL_FIELDS = tuple(axis.name for axis in AXES[:-1])


def _digest(label: str) -> bytes:
    return hashlib.sha256(label.encode("ascii")).digest()


def permutation(axis: str) -> tuple[int, ...]:
    """Versioned SHA ordering avoids dependency on a language PRNG schedule."""
    return tuple(sorted(range(POINTS), key=lambda cell:
                        (_digest(f"{DESIGN_VERSION}:{DESIGN_SEED}:{axis}:{cell}"), cell)))


def latin_points() -> list[dict[str, float]]:
    orders = {axis.name: permutation(axis.name) for axis in AXES}
    return [{axis.name: axis.value((orders[axis.name][row] + .5) / POINTS)
             for axis in AXES} for row in range(POINTS)]


def holdout_point() -> dict[str, float]:
    # Odd numerators over 2**53 give strict interior values, distinct from the
    # core design's unit-space midpoint grid. No trial result participates.
    point = {}
    for axis in AXES:
        integer = int.from_bytes(_digest(f"{DESIGN_VERSION}:holdout:{DESIGN_SEED}:{axis.name}")[:7], "big") >> 4
        point[axis.name] = axis.value(math.ldexp(integer + .5, -52))
    return point


def _anchor(amplitude: float) -> dict[str, float]:
    return {axis.name: (amplitude if "amplitude" in axis.name else
                       0. if "phase" in axis.name or "offset" in axis.name else axis.low)
            for axis in AXES}


def _screen_episode(identity: str, point: dict[str, float], trajectory_seed: int,
                    stratum: str) -> dict[str, Any]:
    return {"id": identity, "scenario": SCENARIO, "mode": "hf-search", "waveform": "reference",
            "seed": 60101, "noise_seed": 70101, "hf_trajectory_seed": trajectory_seed,
            "bit_seconds": 128, "cn0_db_hz": 0, "control": "wanted", "search_slot": 1,
            "search_timing": "selected", "hf_phase_refinement": True, "stratum": stratum, **point}


def _receiver_episode(screen: dict[str, Any], identity: str) -> dict[str, Any]:
    episode = {key: value for key, value in screen.items() if key not in {"search_slot", "search_timing"}}
    episode.update(id=identity, mode="receive", hf_public_phase_receiver=True, capture_digest_slot=1,
                   stratum="frozen-message")
    return episode


def campaigns() -> dict[str, dict[str, Any]]:
    screens = [_screen_episode(f"domain-lhs-{index:02d}", point, 80101 + index, "space-filling-screen")
               for index, point in enumerate(latin_points())]
    screens += [_screen_episode("domain-static-anchor", _anchor(0), 80201, "static-anchor"),
                _screen_episode("domain-max-edge", _anchor(20000), 80202, "declared-domain-edge")]
    screen = {"schema_version": 1, "name": SCREEN_NAME,
              "purpose": "Frozen eight-point nine-axis Latin-hypercube midpoint design plus static and maximum-rate anchors. " + SCOPE,
              "max_wall_seconds": 900, "per_trial_wall_seconds": 100, "max_trials": 10,
              "max_samples": 8000000, "episodes": screens}
    receives = [_receiver_episode(screens[index], f"domain-lhs-{index:02d}-message") for index in (0, 3, 7)]
    holdout = _receiver_episode(_screen_episode("unused", holdout_point(), 90101, "unused"),
                                "domain-fresh-holdout-message")
    holdout.update(seed=90301, noise_seed=90201, message="A", capture_digest_slot=3,
                   stratum="parameter-placement-holdout")
    receives.append(holdout)
    for control in ("noise", "wrong-key"):
        episode = _receiver_episode(screens[0], "domain-" + control + "-message")
        episode.update(control=control, stratum="null-control")
        receives.append(episode)
    receiver = {"schema_version": 1, "name": RECEIVER_NAME,
                "purpose": "Frozen three design messages, fresh physical-parameter/placement/message holdout, and paired null controls. " + SCOPE,
                "max_wall_seconds": 1080, "per_trial_wall_seconds": 180, "max_trials": 6,
                "max_samples": 8000000, "episodes": receives}
    return {SCREEN_NAME: screen, RECEIVER_NAME: receiver}


def design_audit() -> dict[str, Any]:
    orders = {axis.name: permutation(axis.name) for axis in AXES}
    return {"design_version": DESIGN_VERSION, "design_seed": DESIGN_SEED, "scope": SCOPE,
            "selection": "SHA-256 ordered midpoint strata; no receiver template or outcome input",
            "core_points": POINTS, "anchor_points": 2,
            "axes": [{"name": axis.name, "low": axis.low, "high": axis.high, "scale": axis.scale,
                      "ordered_core_bins": list(orders[axis.name]),
                      "core_bin_counts": [orders[axis.name].count(index) for index in range(POINTS)]}
                     for axis in AXES],
            "pair_coverage": [{"axes": [first.name, second.name],
                               "occupied_core_bin_pairs": len(set(zip(orders[first.name], orders[second.name]))),
                               "possible_bin_pairs": POINTS**2}
                              for first, second in combinations(AXES, 2)],
            "holdout": holdout_point(),
            "holdout_scope": "Separate hash domain and interior coordinates; not a sampled ionospheric day"}


def _canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True) + "\n").encode()


def audit_run(campaign: dict[str, Any], run: dict[str, Any]) -> dict[str, Any]:
    """Keep every declared row, including censored, unavailable and absent work.

    This audits runner metadata; it does not replace its native-receipt
    validation or infer any native bit/message result from a shadow.
    """
    if run.get("campaign_sha256") != hashlib.sha256(_canonical_bytes(campaign)).hexdigest():
        raise ValueError("run does not bind the frozen generated campaign")
    episodes = {episode["id"]: episode for episode in campaign["episodes"]}
    trials = run.get("trials")
    if not isinstance(trials, list):
        raise ValueError("run trials must be a list")
    by_id = {}
    for trial in trials:
        if not isinstance(trial, dict) or not isinstance(trial.get("episode"), dict):
            raise ValueError("invalid trial metadata")
        identity = trial["episode"].get("id")
        if not isinstance(identity, str) or identity not in episodes or identity in by_id or trial["episode"] != episodes[identity]:
            raise ValueError("unexpected, duplicate or mutated trial identity")
        if trial.get("status") not in TRIAL_STATUSES:
            raise ValueError("unknown trial status")
        native_status = trial.get("receipt_status")
        if native_status is not None and native_status not in NATIVE_STATUSES:
            raise ValueError("unknown native receipt status")
        if trial["status"] == "completed" and native_status != "completed":
            raise ValueError("completed trial lacks a completed native receipt status")
        by_id[identity] = trial
    rows = []
    for identity, episode in episodes.items():
        trial = by_id.get(identity)
        rows.append({"id": identity, "mode": episode["mode"], "stratum": episode["stratum"],
                     "control": episode["control"],
                     "trial_status": trial["status"] if trial else "not_executed",
                     "native_status": trial.get("receipt_status") if trial else None,
                     "endpoint_scope": "complete native receiver" if episode["mode"] == "receive" else "component only"})
    return {"campaign": campaign["name"], "scope": SCOPE,
            "audit_kind": "frozen design and runner metadata; not native communication-result validation",
            "status_counts": dict(Counter(row["trial_status"] for row in rows)), "rows": rows}


def manifest_text(campaign: dict[str, Any]) -> str:
    return json.dumps(campaign, indent=2, ensure_ascii=True) + "\n"


def check_frozen() -> None:
    for name, campaign in campaigns().items():
        path = HERE / "campaigns" / f"{name}.json"
        if path.read_text(encoding="utf-8") != manifest_text(campaign):
            raise ValueError(f"frozen manifest differs from {DESIGN_VERSION}: {path.name}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--check", action="store_true", help="verify both frozen manifests (default)")
    action.add_argument("--write", action="store_true", help="create both manifests only if neither exists")
    parser.add_argument("--audit-run", type=Path, action="append", default=[], help="append descriptive runner-status coverage")
    args = parser.parse_args()
    generated = campaigns()
    if args.write:
        destinations = {name: HERE / "campaigns" / f"{name}.json" for name in generated}
        if any(path.exists() for path in destinations.values()):
            raise FileExistsError("refusing to overwrite either frozen manifest; use --check")
        for name, path in destinations.items():
            with path.open("x", encoding="utf-8") as stream:
                stream.write(manifest_text(generated[name]))
    check_frozen()
    report = design_audit()
    if args.audit_run:
        report["runs"] = []
        hashes = {hashlib.sha256(_canonical_bytes(campaign)).hexdigest(): campaign for campaign in generated.values()}
        for path in args.audit_run:
            run = json.loads(path.read_text(encoding="utf-8"))
            campaign = hashes.get(run.get("campaign_sha256"))
            if campaign is None:
                raise ValueError("run is not bound to either frozen domain campaign")
            report["runs"].append(audit_run(campaign, run))
    print(json.dumps(report, indent=2, ensure_ascii=True))


if __name__ == "__main__":
    main()
