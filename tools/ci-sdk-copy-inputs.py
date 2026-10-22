#!/usr/bin/env python3
"""Validate and identify immutable SDK packages for copied-only recovery."""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

BACKENDS = ("fltk", "rev")
ARTIFACTS = {
    backend: f"DataPump-SDK-{backend}-Linux-x86_64-glibc-2.36"
    for backend in BACKENDS
}
PACKAGE_JOBS = {
    "fltk": "SDK packages, fltk / SDK packages / fltk / debian:bookworm-slim",
    "rev": "SDK packages, rev / SDK packages / rev / ubuntu:24.04",
}


def positive_run_id(value: str) -> int:
    if not re.fullmatch(r"[1-9][0-9]*", value):
        raise ValueError("copy_run_id must be a positive decimal Actions run ID")
    return int(value)


def validate_metadata(repository: str, run_id: int, expected_sha: str,
                      run: dict, jobs: list[dict], artifacts: list[dict]) -> dict:
    if not re.fullmatch(r"[0-9a-fA-F]{40}", expected_sha):
        raise ValueError("copy_source_sha must be exactly 40 hexadecimal characters")
    if run.get("id") != run_id:
        raise ValueError("source run ID does not match copy_run_id")
    if run.get("repository", {}).get("full_name") != repository:
        raise ValueError("source workflow run must belong to this repository")
    if run.get("head_repository", {}).get("full_name") != repository:
        raise ValueError("source run must belong to this repository; fork runs are rejected")
    if run.get("head_sha", "").lower() != expected_sha.lower():
        raise ValueError("source run head SHA does not match copy_source_sha")
    if run.get("path") != ".github/workflows/sdk.yml" or run.get("event") != "workflow_dispatch":
        raise ValueError("source must be a manually dispatched .github/workflows/sdk.yml run")

    selected = {}
    for backend in BACKENDS:
        job_name = PACKAGE_JOBS[backend]
        matches = [job for job in jobs if job.get("name") == job_name]
        if len(matches) != 1:
            raise ValueError(f"required package producer is missing or duplicated: {job_name}")
        job = matches[0]
        if job.get("status") != "completed" or job.get("conclusion") != "success":
            raise ValueError(f"required package producer did not succeed: {job_name}")
        if (type(job.get("id")) is not int or job["id"] <= 0 or
                job.get("run_id") != run_id or job.get("head_sha", "").lower() != expected_sha.lower()):
            raise ValueError(f"package producer is not bound to the validated source run: {job_name}")
        artifact_name = ARTIFACTS[backend]
        matches = [artifact for artifact in artifacts if artifact.get("name") == artifact_name]
        if len(matches) != 1:
            raise ValueError(f"required immutable artifact is missing or duplicated: {artifact_name}")
        artifact = matches[0]
        if artifact.get("expired") is not False:
            raise ValueError(f"required immutable artifact is expired: {artifact_name}")
        workflow_run = artifact.get("workflow_run") or {}
        if workflow_run.get("id") != run_id or workflow_run.get("head_sha", "").lower() != expected_sha.lower():
            raise ValueError(f"artifact is not bound to the validated source run: {artifact_name}")
        if type(artifact.get("id")) is not int or artifact["id"] <= 0:
            raise ValueError(f"artifact has no valid immutable ID: {artifact_name}")
        if type(artifact.get("size_in_bytes")) is not int or artifact["size_in_bytes"] <= 0:
            raise ValueError(f"artifact has no positive immutable size: {artifact_name}")
        digest = artifact.get("digest")
        if not isinstance(digest, str) or not re.fullmatch(r"sha256:[0-9a-fA-F]{64}", digest):
            raise ValueError(f"artifact has no SHA-256 digest: {artifact_name}")
        selected[backend] = {
            "id": artifact["id"], "name": artifact_name,
            "digest": digest, "size_in_bytes": artifact["size_in_bytes"],
            "producer_job_id": job["id"], "producer_job_name": job_name,
        }
    return selected


def api_json(url: str, token: str) -> dict:
    request = urllib.request.Request(url, headers={
        "Accept": "application/vnd.github+json",
        "Authorization": f"Bearer {token}",
        "X-GitHub-Api-Version": "2022-11-28",
    })
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def paginated(url: str, token: str, key: str) -> list[dict]:
    results = []
    identifiers = set()
    expected_total = None
    for page in range(1, 21):
        separator = "&" if "?" in url else "?"
        data = api_json(f"{url}{separator}per_page=100&page={page}", token)
        batch = data.get(key)
        total = data.get("total_count")
        if not isinstance(batch, list) or type(total) is not int or not 0 <= total <= 2000:
            raise ValueError(f"GitHub API response omitted {key}")
        if expected_total is not None and total != expected_total:
            raise ValueError(f"GitHub API {key} inventory changed during pagination")
        expected_total = total
        for item in batch:
            identifier = item.get("id")
            if type(identifier) is not int or identifier <= 0 or identifier in identifiers:
                raise ValueError(f"GitHub API {key} inventory contains invalid or duplicate IDs")
            identifiers.add(identifier)
        results.extend(batch)
        if len(results) == total:
            return results
        if len(results) > total:
            raise ValueError(f"GitHub API {key} inventory exceeds its declared total")
        if len(batch) < 100:
            raise ValueError(f"GitHub API {key} inventory is truncated")
    raise ValueError(f"GitHub API {key} inventory exceeds the bounded page count")


def main() -> None:
    event = os.environ.get("GITHUB_EVENT_NAME", "")
    devfast = os.environ.get("INPUT_DEVFAST", "").lower() == "true"
    if event != "workflow_dispatch" or not devfast:
        raise ValueError("copied-only recovery requires manual dispatch with devfast=true")
    run_id = positive_run_id(os.environ.get("INPUT_COPY_RUN_ID", ""))
    source_sha = os.environ.get("INPUT_COPY_SOURCE_SHA", "").lower()
    if not re.fullmatch(r"[0-9a-f]{40}", source_sha):
        raise ValueError("copy_source_sha must be exactly 40 hexadecimal characters")
    repository = os.environ["GITHUB_REPOSITORY"]
    token = os.environ["GITHUB_TOKEN"]
    base = f"https://api.github.com/repos/{urllib.parse.quote(repository, safe='/')}/actions"
    run = api_json(f"{base}/runs/{run_id}", token)
    jobs = paginated(f"{base}/runs/{run_id}/jobs", token, "jobs")
    artifacts = paginated(f"{base}/runs/{run_id}/artifacts", token, "artifacts")
    selected = validate_metadata(repository, run_id, source_sha, run, jobs, artifacts)
    compact = {backend: selected[backend]["id"] for backend in BACKENDS}
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
        output.write(f"artifact_ids={json.dumps(compact, separators=(',', ':'))}\n")
        output.write(f"source_sha={source_sha}\nsource_run_id={run_id}\n")
    receipt = {
        "mode": "copied-runtime-only",
        "repository": repository,
        "run_id": run_id,
        "source_sha": source_sha.lower(),
        "workflow": run["path"],
        "event": run["event"],
        "artifacts": selected,
        "tooling_sha": os.environ.get("GITHUB_SHA"),
    }
    receipt_path = Path(os.environ.get("COPY_RECEIPT_PATH", "build/sdk-copy-inputs.json"))
    receipt_path.parent.mkdir(parents=True, exist_ok=True)
    with receipt_path.open("x", encoding="utf-8") as output:
        json.dump(receipt, output, indent=2, sort_keys=True)
        output.write("\n")
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as output:
            output.write("## SDK copied-runtime recovery\n\n")
            output.write("This run qualifies copied-runtime coverage only; it does not qualify the full SDK workflow or release eligibility.\n\n")
            output.write("```json\n" + json.dumps(receipt, indent=2, sort_keys=True) + "\n```\n")
    print(json.dumps(receipt, sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (KeyError, ValueError, urllib.error.URLError) as error:
        print(f"::error::{error}", file=sys.stderr)
        raise SystemExit(1)
