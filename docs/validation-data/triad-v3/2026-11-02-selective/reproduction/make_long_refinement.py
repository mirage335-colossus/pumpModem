#!/usr/bin/env python3
"""Freeze a paired 4096-second block refinement; launch no experiments.

Derive from the frozen long-control plans, retaining the first four physical
seed addresses per mode. This is an adaptive diagnostic, not a new holdout.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path


PARENT_HASHES = {
    "long-export-plan.json": "c7a8557773a72bbb2d126c4d7955646eefeceaec99c6fe2868486a93911435a6",
    "long-score-plan.json": "7f36837cfe8f31b4b079f02d073c620df65e993d6792f6b2cc992511a59868ea",
}
SOURCE_ROOT = "/workspace/triad"
PATHS = 4


def read_parent(name: str) -> dict:
    path = Path(__file__).with_name(name)
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != PARENT_HASHES[name]:
        raise ValueError(f"frozen parent plan changed: {name}")
    return json.loads(data)


def replace_option(argv: list[str], option: str, value: str) -> None:
    if argv.count(option) != 1:
        raise ValueError(f"expected one {option}")
    argv[argv.index(option) + 1] = value


def retarget(value: str, root: Path) -> str:
    if value == SOURCE_ROOT or value.startswith(SOURCE_ROOT + "/"):
        value = str(root) + value[len(SOURCE_ROOT):]
    for old, new in (("/local-evidence/selective/long/", "/local-evidence/selective/long-refinement/"),
                     ("/local-evidence/selective/long-scores/", "/local-evidence/selective/long-refinement-scores/"),
                     ("/local-evidence/selective/long-retention/", "/local-evidence/selective/long-refinement-retention/")):
        value = value.replace(old, new)
    return value


def make_plans(root: Path, python: str, binary: Path) -> tuple[dict, dict]:
    parents = [read_parent("long-export-plan.json"), read_parent("long-score-plan.json")]
    plans = copy.deepcopy(parents)
    for plan in plans:
        plan["schema"] = "triad-selective-stable850-block-refinement-plan-v1"
        plan["stage"] = "paired adaptive block-duration diagnostic; not independent holdout qualification"
        plan["selection"] = "Selected after inspecting the 8192-second parent retention diagnostic; reuse exactly its first four seed addresses per mode."
        plan["sampled_export_count"] = 8
        plan["source_defaults"]["paths_per_mode"] = PATHS
        plan["geometry"].update(block_seconds=4096, block_samples=1024,
                                selective_slow_cycles_per_block=20.48,
                                selective_fast_cycles_per_block=204.8)
        plan["parent_plans"] = [
            {"relative_source": name, "sha256": digest}
            for name, digest in PARENT_HASHES.items()
        ]
        plan["paired_parent_inputs"] = [
            {"mode": mode,
             "parent": [str(root / "local-evidence/selective/long" / f"stable850-{mode}-m60" / f"path-{i:05d}")
                        for i in range(80000, 80004)],
             "refinement": [str(root / "local-evidence/selective/long-refinement" / f"stable850-{mode}-m60" / f"path-{i:05d}")
                            for i in range(80000, 80004)]}
            for mode in ("off", "each-65")
        ]
        plan["interpretation_limits"] = [
            item.replace("An 8192-second block", "A 4096-second block").replace(
                "The 16 observed paths per mode, eight per source bit", "The four reused paths per mode, two per source bit")
            for item in plan["interpretation_limits"]
        ] + [
            "The only physical-export command changes from the parent are block duration, output directory and path count. The split, index, bit, source epoch, seeds, sampling rate and all modeled impairments remain unchanged.",
            "Within each modulation mode the intended raw sampled signal/noise trajectory is identical to the parent; this plan checks seed/configuration identity, while completed evidence must verify the actual sample calendar and energy before claiming exact realization parity.",
            "Reducing a projection block does not remove the selective filter or its state. Compare retained energy and actual receiver outcomes separately; four paths do not establish a sensitivity boundary.",
        ]
        for job in plan["jobs"] + plan.get("retention_jobs", []):
            job["argv"] = [retarget(value, root) for value in job["argv"]]
            job["argv"][0] = python
    for job in plans[0]["jobs"]:
        replace_option(job["argv"], "--binary", str(binary))
        replace_option(job["argv"], "--paths", str(PATHS))
        replace_option(job["argv"], "--block-seconds", "4096")
    for job in plans[1]["jobs"]:
        argv = job["argv"]
        output_index = argv.index("--output")
        if output_index != 18:  # Python, tool, sixteen parent workspaces.
            raise ValueError("unexpected parent score input layout")
        job["argv"] = argv[:2 + PATHS] + argv[output_index:]
    return plans[0], plans[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(SOURCE_ROOT))
    parser.add_argument("--python", default="/workspace/triad-env/bin/python")
    parser.add_argument("--binary", type=Path, default=Path("/workspace/triad/build/dev-cli/triad_long"))
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    plans = make_plans(args.root.resolve(), args.python, args.binary.resolve())
    args.output.mkdir(parents=True, exist_ok=True)
    for name, plan in zip(("long-refinement-export-plan.json", "long-refinement-score-plan.json"), plans):
        (args.output / name).write_text(json.dumps(plan, indent=2, allow_nan=False) + "\n")


if __name__ == "__main__":
    main()
