#!/usr/bin/env python3
"""Summarize retained cable evidence without audio I/O, DSP, or large PCM reads.

Run from any directory; --repo locates the retained local-evidence inputs.
Only completed captures are summarized. This packages evidence, not acceptance.
Raw PCM identities come from retained receipts; --verify-pcm rehashes them.
"""
from __future__ import annotations

import argparse
from collections import Counter
import gzip
import hashlib
import io
import json
from pathlib import Path
import tarfile


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def read(path, default=None):
    return json.loads(path.read_text()) if path.is_file() else default


def selected(obj, keys):
    return {key: obj[key] for key in keys if key in obj}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[4])
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--verify-pcm", action="store_true")
    args = parser.parse_args()
    repo, output = args.repo.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    local = repo / "local-evidence"
    names = {
        "plan": "triad-live-full-plan-001",
        "input": "triad-live-full-input-001",
        "original": "triad-live-full-capture-001",
        "reanalysis": "triad-live-full-analysis-002",
        "continuation": "triad-live-full-continuation-001",
    }
    roots = {key: local / name for key, name in names.items()}
    plan = read(roots["plan"] / "plan.json")
    prepared = read(roots["input"] / "prepared.json")
    if not plan or not prepared:
        parser.error("retained original plan/prepared inputs are required")

    artifacts = {}

    def artifact(path, alias=None):
        if not path.is_file():
            return None
        label = alias or str(path.relative_to(repo))
        # The byte payload is frozen now, avoiding a hash/read race at packaging.
        data = path.read_bytes()
        artifacts[label] = data
        return {"path": label, "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}

    def json_artifact(path):
        return {"identity": artifact(path), "value": read(path)} if path.is_file() else None

    summaries = {key: read(root / "summary.json", {}) for key, root in roots.items()}
    rows = []
    for case in plan["cases"]:
        cid = case["id"]
        row = {
            "id": cid,
            "configuration": selected(case, ["kind", "rate", "added", "carrier_hz", "gain", "spread", "modulation", "margin", "fhss", "burst_wait_tiles", "partial", "native_sample_rate", "requested_seconds", "signal_seconds", "window_seconds", "duration_seconds", "frequencies_hz", "seed", "audio_geometry"]),
            "operational_admission": None,
            "false_admission_probability": None,
        }
        if case.get("status") == "unsupported_audio_geometry":
            row.update(status="unsupported_audio_geometry", evidence_kind=case["evidence_kind"], capture=None)
            rows.append(row)
            continue
        # Continuation symlinks are reused evidence, not new physical repetitions.
        resumed = roots["continuation"] / cid
        capture_root = resumed if resumed.is_dir() and not resumed.is_symlink() else roots["original"] / cid
        receipt_path = capture_root / "capture.json"
        receipt = read(receipt_path, {})
        identity = read(capture_root / "capture-identity.json", {})
        row["capture"] = {
            "receipt": artifact(receipt_path),
            "identity_receipt": artifact(capture_root / "capture-identity.json"),
            "recorded_identity": identity,
            "status": receipt.get("status", "missing"),
            "backend": receipt.get("backend"),
            "actual_format": receipt.get("audio_format"),
            "elapsed_seconds": receipt.get("elapsed_seconds"),
            "discontinuity": receipt.get("discontinuity"),
            "failure": receipt.get("failure"),
            "data": receipt.get("capture"),
            "input_frames": receipt.get("input_frames"),
            "independent_hardware_observations": int(receipt.get("status") == "complete"),
        }
        row["status"] = "captured" if receipt.get("status") == "complete" else "incomplete_capture"
        row["input"] = {
            "stimulus_metadata": artifact(roots["input"] / cid / "stimulus.json"),
            "native_manifest": artifact(roots["input"] / cid / "native" / "manifest.json"),
            "prepared_file_sha256": {key: value for key, value in prepared["files_sha256"].items() if key.startswith(cid + "/")},
        }
        if args.verify_pcm and row["status"] == "captured":
            raw_path = capture_root / "capture.f32"
            actual_hash = digest(raw_path)
            if actual_hash != identity.get("capture_sha256"):
                raise ValueError(f"PCM identity mismatch: {raw_path}")
            row["capture"]["pcm_rehashed_equal_to_receipt"] = True
            stereo_hash = digest(capture_root / "capture.f32.stereo.s16le")
            if stereo_hash != receipt["capture"]["stereo_sha256"]:
                raise ValueError(f"stereo PCM identity mismatch: {capture_root}")
            stimulus_hash = digest(roots["input"] / cid / "stimulus.f32")
            if stimulus_hash != identity.get("stimulus_sha256"):
                raise ValueError(f"stimulus identity mismatch: {cid}")
            row["capture"]["stereo_and_stimulus_rehashed_equal_to_receipts"] = True
        # First 18 captures use payload-only reanalysis; the training profile is
        # a fit, not a held-out analysis. New continuation captures already have
        # payload-only analysis. Never overwrite the original FHSS zero-bank result.
        analysis_path = roots["reanalysis"] / f"{cid}-analysis.json"
        if not analysis_path.is_file():
            analysis_path = roots["continuation"] / f"{cid}-analysis.json"
        report = read(analysis_path, {})
        row["analysis"] = artifact(analysis_path)
        row["evidence_kind"] = report.get("evidence_kind", "training fit" if cid == "training" else "unassessed")
        row["profile_sha256"] = report.get("profile_sha256", read(roots["original"] / "profile-freeze.json", {}).get("profile_sha256"))
        for key in ["payload_energy_and_model_residual", "payload_interior_energy_and_model_residual", "alignment", "capture_receipt_verification", "component_only_reason", "training_model_flags", "heldout_quiet_noise", "channel_energy"]:
            if key in report:
                row[key] = report[key]
        for key in ["real_projection", "predicted_projection"]:
            if key in report:
                row[key] = selected(report[key], ["chosen_bit", "bit_scores", "best_retained_observation_fraction", "candidate_ranks", "valid_samples", "rank_policy", "limits", "identifiability"])
        if "conditional_noise_margin" in report:
            row["conditional_noise_margin"] = selected(report["conditional_noise_margin"], ["status", "target_correct", "conditional_noise_trials", "truth_bit", "grid_brackets", "cn0_definition", "noise_model", "confidence_scope", "pointwise_confidence", "independent_hardware_observations", "fixed_capture_residual_is_reused", "reference_power", "reference_duration_seconds", "valid_observation_seconds", "sampled_noise_crosscheck"])
        if cid == "fhss-burst3":
            row["nominal_bank_limitation"] = "Retained original zero-only timing bank omits the declared +/-90 ms arrival prior; hardware marker alignment does not remove simulated source arrival. Separate public bounded timing diagnostic must not replace this failure."
        if cid.startswith("partial-"):
            row["component_limit"] = "Actual-rate 24 s source window, not a complete bit. Window-edge transients are included; near-DC payload energy is not steady-state carrier gain."
        if cid == "low-level-quantization":
            row["noise_limit"] = "Noiseless deterministic profile cannot predict this capture's noise-dominated total energy. Bit selection is conditional on this single capture; no hardware-error probability established."
        rows.append(row)

    # Package compact, original receipts and recipes only. Do not package sampled
    # PCM/native workspaces, and do not duplicate continuation symlinked captures.
    extra_names = [
        "triad-live-smoke-analysis-003", "triad-live-smoke-capture-001",
        "triad-live-low-refinement-001", "triad-live-low-four-pole-001",
        "triad-live-lowfreq-diagnosis", "triad-live-fhss-timing-001",
        "triad-live-duplex/pilot-route-001",
        "triad-live-duplex/native-training-001",
        "triad-live-duplex/alsa-training-001",
        "triad-live-final-checks-001",
    ]
    source_roots = list(roots.values()) + [local / name for name in extra_names]
    for root in source_roots:
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*")):
            if any(parent.is_symlink() for parent in path.parents if parent != root):
                continue
            if path.is_file() and (path.suffix in {".json", ".log", ".py"} or path.name == "executed-source.tar.gz"):
                artifact(path)
    bridge = Path("/tmp/triad-live-bridge-2swq1rz8/final-module-result.json")
    bridge_identity = artifact(bridge, "external-bridge-anchor/final-module-result.json") if bridge.exists() else None
    if bridge.parent.is_dir():
        for path in sorted(bridge.parent.rglob("*.json")):
            artifact(path, "external-bridge-anchor/" + str(path.relative_to(bridge.parent)))

    integration_paths = [
        "CMakeLists.txt", "cmake/TestGroups.cmake", "tools/triad-long.cpp",
        "tools/triad_audio_capture.cpp", "tools/triad_live_alsa.py",
        "tools/triad_live_duplex.py", "tools/triad_live_model.py",
        "tools/triad_live_waveforms.py", "tests/test_triad_audio_capture.py",
        "tests/test_triad_live_alsa.py", "tests/test_triad_live_duplex.py",
        "tests/test_triad_live_model.py", "tests/test_triad_live_source.py",
        "tests/test_triad_live_waveforms.py", "tests/test_triad_configuration.py",
    ]
    final_checks = read(local / "triad-live-final-checks-001" / "result.json")
    integration = {
        "scope": "Packaging-time implementation/test snapshot, distinct from physically executed versions retained in each campaign's provenance and source archives. This is not a retroactive physical execution claim.",
        "files": {name: artifact(repo / name, "final-integration-source/" + name) for name in integration_paths},
        "focused_check_receipt": json_artifact(local / "triad-live-final-checks-001" / "result.json"),
    }
    if final_checks:
        checked = final_checks.get("source_sha256", {})
        mismatches = [name for name, expected in checked.items() if integration["files"].get(name, {}).get("sha256") != expected]
        if mismatches:
            raise ValueError(f"final integration changed after focused checks: {mismatches}")
        integration["focused_receipt_bound_sources_match"] = True
        integration["focused_receipt_bound_source_count"] = len(checked)

    lf = {}
    for label in ["low-refinement", "low-four-pole"]:
        root = local / f"triad-live-{label}-001"
        result = read(root / "validation-result.json")
        receipt = read(root / "validation" / "capture.json", {})
        # An active capture or absent analysis is explicitly pending.
        lf[label] = {
            "profile_freeze": json_artifact(root / "profile-freeze.json"),
            "status": result.get("status") if result and receipt.get("status") == "complete" else "pending",
            "validation_capture": json_artifact(root / "validation" / "capture.json") if receipt.get("status") == "complete" else None,
            "result": json_artifact(root / "validation-result.json") if result and receipt.get("status") == "complete" else None,
        }
        if args.verify_pcm and receipt.get("status") == "complete":
            validation_hash = digest(root / "validation" / "rx.f32")
            expected = (result or {}).get("identities", {}).get("validation_capture_sha256")
            if expected and expected != validation_hash:
                raise ValueError(f"validation PCM identity mismatch: {root}")
            stereo_hash = digest(root / "validation" / "rx.f32.stereo.s16le")
            if stereo_hash != receipt["capture"]["stereo_sha256"]:
                raise ValueError(f"validation stereo identity mismatch: {root}")
            lf[label]["validation_pcm_rehash"] = {
                "sha256": validation_hash,
                "compared_with_prior_selected_channel_hash": expected is not None,
                "stereo_matches_capture_receipt": True,
                "scope": "Current bytes compared with retained hashes where available; this does not create a historical freeze.",
            }

    base_time = prepared["planning_wall_seconds"] + prepared["preparation_wall_seconds"]
    original_time = summaries["original"].get("whole_command_wall_seconds", 0)
    continuation_time = summaries["continuation"].get("whole_command_wall_seconds", 0)
    refinement_times = {}
    for label, phase in [("low-refinement", "training"), ("low-refinement", "validation"), ("low-four-pole", "validation")]:
        receipt = read(local / f"triad-live-{label}-001" / phase / "capture.json", {})
        refinement_times[f"{label}/{phase}"] = receipt.get("elapsed_seconds") if receipt.get("status") == "complete" else None
    summary = {
        "schema": "triad-live-retained-evidence-matrix-v1",
        "status": "evidence inventory; not a suite acceptance certificate",
        "scope": "one combined DAC/cable/ADC route; hardware-free aggregation of frozen measurements",
        "source_head_at_plan": plan["provenance"]["git_head"],
        "plan_provenance": plan["provenance"],
        "final_integration_source_snapshot": integration,
        "execution_identities": {key: json_artifact(roots[key] / "input-identity.json") for key in ["original", "continuation", "reanalysis"]},
        "original_executed_sources": json_artifact(roots["original"] / "executed-source.json"),
        "profile_freeze": json_artifact(roots["original"] / "profile-freeze.json"),
        "provenance_limits": [
            "Original capture receipts did not bind all metadata hashes at capture time; later consistency checks are not retroactive bindings.",
            "The first payload reanalysis command stopped on the missing interrupted capture; completed individual reports are retained without calling that command passed.",
            "Raw PCM is retained in ignored local-evidence, not in the compact archive. Reproducing DSP requires those identified raw files or fresh physical captures.",
            "Receipt PCM hashes are not freshly recomputed unless --verify-pcm is used; packaged metadata bytes are SHA-256 bound at packaging.",
        ],
        "limits": plan["limitations"],
        "counts": dict(Counter(row["status"] for row in rows)),
        "evidence_counts": dict(Counter(row["evidence_kind"] for row in rows)),
        "capture_health": {
            "completed_captures": sum(row.get("capture", {}).get("status") == "complete" for row in rows if row.get("capture")),
            "reported_discontinuities": sum(any(row["capture"].get("discontinuity", {}).values()) for row in rows if row.get("capture")),
            "reported_stereo_near_fullscale_samples": sum(sum((row["capture"].get("data") or {}).get("stereo_near_fullscale", [])) for row in rows if row.get("capture")),
            "scope": "retained ALSA complete-capture receipts; no guarantee about arbitrary future routing or sub-fullscale analog nonlinearity",
        },
        "census_counts": dict(Counter(row.get("evidence", "unspecified") for row in plan["census"])),
        "timing": {
            "planning_and_preparation_seconds": base_time,
            "original_capture_analysis_run_seconds": original_time,
            "continuation_capture_analysis_run_seconds": continuation_time,
            "combined_original_campaign_active_seconds": base_time + original_time + continuation_time,
            "continuation_reported_hierarchy_seconds": summaries["continuation"].get("result", {}).get("hierarchy_wall_seconds"),
            "human_pause_seconds_estimate": summaries["continuation"].get("result", {}).get("human_pause_seconds_estimate"),
            "uninterrupted_full_campaign_demonstrated": False,
            "additional_payload_reanalysis_seconds": summaries["reanalysis"].get("whole_command_wall_seconds"),
            "refinement_capture_seconds": refinement_times,
            "scope": "Command wall times include capture and inline analysis. Added reanalysis/refinement and human pause remain separate; pilot and software-development time excluded. Simulated source duration is not wall time.",
        },
        "original_run_failure": summaries["original"].get("result"),
        "reanalysis_command_failure": summaries["reanalysis"].get("result"),
        "cases": rows,
        "low_frequency_refinement": lf,
        "independent_bridge_anchor": {"identity": bridge_identity, "value": read(bridge)} if bridge_identity else {"status": "original /tmp bridge anchor unavailable"},
        "fhss_timing_followup": [artifact(p) for p in sorted((local / "triad-live-fhss-timing-001").glob("*.json"))],
    }
    encoded = (json.dumps(summary, indent=2, sort_keys=True, allow_nan=False) + "\n").encode()
    (output / "evidence-matrix.json").write_bytes(encoded)
    artifacts["evidence-matrix.json"] = encoded
    artifact(Path(__file__).resolve(), "package_evidence.py")
    manifest = {name: {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)} for name, data in sorted(artifacts.items())}
    manifest_bytes = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    (output / "artifact-manifest.json").write_bytes(manifest_bytes)
    artifacts["artifact-manifest.json"] = manifest_bytes
    archive = output / "compact-evidence.tar.gz"
    with archive.open("wb") as raw, gzip.GzipFile(fileobj=raw, mode="wb", mtime=0, filename="") as gz, tarfile.open(fileobj=gz, mode="w") as tar:
        for name, data in sorted(artifacts.items()):
            info = tarfile.TarInfo(name)
            info.size, info.mtime, info.mode = len(data), 0, 0o644
            tar.addfile(info, io.BytesIO(data))
    print(json.dumps({"cases": summary["counts"], "evidence": summary["evidence_counts"], "active_seconds": summary["timing"]["combined_original_campaign_active_seconds"], "archive_bytes": archive.stat().st_size, "archive_sha256": digest(archive), "pcm_rehashed": args.verify_pcm}, indent=2))


if __name__ == "__main__":
    main()
