#!/usr/bin/env python3
"""Validate and identify the immutable native Linux package for copied-only recovery."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import re
import sys
import urllib.error
import urllib.parse

HELPER = Path(__file__).with_name("ci-sdk-copy-inputs.py")
_spec = importlib.util.spec_from_file_location("ci_sdk_copy_inputs", HELPER)
if _spec is None or _spec.loader is None:
    raise RuntimeError(f"cannot load shared copy-input validator: {HELPER}")
_shared = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_shared)

PACKAGE_JOBS = {"native": "Linux Release / package / all"}
ARTIFACT_NAMES = {"native": "DataPump-Linux-x86_64-glibc-2.35"}
WORKFLOW_PATH = ".github/workflows/native-linux.yml"


def validate_metadata(repository: str, run_id: int, expected_sha: str,
                      run: dict, jobs: list[dict], artifacts: list[dict]) -> dict:
    return _shared.validate_metadata(
        repository, run_id, expected_sha, run, jobs, artifacts,
        workflow_path=WORKFLOW_PATH,
        package_jobs=PACKAGE_JOBS,
        artifact_names=ARTIFACT_NAMES,
    )


def main() -> None:
    if os.environ.get("GITHUB_EVENT_NAME") != "workflow_dispatch":
        raise ValueError("native copied-package recovery requires manual workflow dispatch")
    run_id = _shared.positive_run_id(os.environ.get("INPUT_SOURCE_RUN_ID", ""))
    source_sha = os.environ.get("INPUT_SOURCE_SHA", "").lower()
    if not re.fullmatch(r"[0-9a-f]{40}", source_sha):
        raise ValueError("source_sha must be exactly 40 hexadecimal characters")
    repository = os.environ["GITHUB_REPOSITORY"]
    token = os.environ["GITHUB_TOKEN"]
    base = f"https://api.github.com/repos/{urllib.parse.quote(repository, safe='/')}/actions"
    run = _shared.api_json(f"{base}/runs/{run_id}", token)
    jobs = _shared.paginated(f"{base}/runs/{run_id}/jobs", token, "jobs")
    artifacts = _shared.paginated(f"{base}/runs/{run_id}/artifacts", token, "artifacts")
    selected = validate_metadata(repository, run_id, source_sha, run, jobs, artifacts)["native"]
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
        output.write(f"artifact_id={selected['id']}\n")
        output.write(f"source_sha={source_sha}\nsource_run_id={run_id}\n")
    receipt = {
        "mode": "copied-native-package-only",
        "repository": repository,
        "source_run_id": run_id,
        "source_sha": source_sha,
        "source_workflow": run["path"],
        "source_event": run["event"],
        "artifact": selected,
        "tooling_sha": os.environ.get("GITHUB_SHA"),
    }
    receipt_path = Path(os.environ.get("COPY_RECEIPT_PATH", "build/native-copy-inputs.json"))
    receipt_path.parent.mkdir(parents=True, exist_ok=True)
    with receipt_path.open("x", encoding="utf-8") as output:
        json.dump(receipt, output, indent=2, sort_keys=True)
        output.write("\n")
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as output:
            output.write("## Native copied-package recovery\n\n")
            output.write("This run checks copied native package compatibility only; it does not qualify full native CI or release eligibility.\n\n")
            output.write("```json\n" + json.dumps(receipt, indent=2, sort_keys=True) + "\n```\n")
    print(json.dumps(receipt, sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (KeyError, ValueError, urllib.error.URLError) as error:
        print(f"::error::{error}", file=sys.stderr)
        raise SystemExit(1)
