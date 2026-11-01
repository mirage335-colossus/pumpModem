#!/usr/bin/env python3
"""Check offline consumer analyzer portability, summaries and plot outputs."""
from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import math
from pathlib import Path
import platform
import re
import statistics
import tarfile
from typing import Any


ROOT = Path(__file__).resolve().parents[4]
STUDY = ROOT / "docs/validation-data/triad-v3/2026-11-01-consumer-gpu"
PORTABLE_STUDY_REL = Path("triad-consumer-gpu-evidence/study")
EXPECTED_PORTABLE_PLOTS = {
    "fused-pack-paths.png", "holdout-detection.png",
    "illustrative-cost-per-observed-correct.png", "null-admissions.png",
    "paired-score-error.png", "partial-accumulation.png", "probe-products.png",
    "resident-throughput.png", "sustained-replay-throughput.png",
    "tile-sweep-microbenchmarks.png", "transfer-probe.png",
    "workspace-replay-speedup.png",
}
EXTRA_LOCAL_PLOT = "resident-seconds-per-correct-capture.png"
PATH_DIFFERENCE = re.compile(r"^/(?:indexes/\d+/path|gpu_profiles/\d+/trace)$")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def version(name: str) -> str | None:
    try:
        return importlib.metadata.version(name)
    except importlib.metadata.PackageNotFoundError:
        return None


def json_file(path: Path) -> Any:
    return json.loads(path.read_text())


def _join(pointer: str, part: str | int) -> str:
    return f"{pointer}/{part}"


def stage_result_suffix(value: Any) -> str | None:
    if not isinstance(value, str):
        return None
    marker = "/stage5/consumer-results/"
    if marker not in value:
        return None
    return value.rsplit(marker, 1)[1]


def compare_summaries(repo: Any, portable: Any) -> dict[str, Any]:
    differences: list[dict[str, str]] = []
    failures: list[str] = []

    def visit(a: Any, b: Any, pointer: str = "") -> None:
        if type(a) is not type(b):
            if PATH_DIFFERENCE.fullmatch(pointer):
                failures.append(f"allowed path field has changed JSON type: {pointer}")
            else:
                failures.append(f"JSON type differs at {pointer or '/'}")
            return
        if isinstance(a, dict):
            if a.keys() != b.keys():
                missing = sorted(a.keys()-b.keys())
                extra = sorted(b.keys()-a.keys())
                failures.append(f"JSON keys differ at {pointer or '/'}; missing={missing[:6]}, extra={extra[:6]}")
                return
            for key in sorted(a):
                visit(a[key], b[key], _join(pointer, key))
        elif isinstance(a, list):
            if len(a) != len(b):
                failures.append(f"JSON list length differs at {pointer or '/'}")
                return
            for index, (left, right) in enumerate(zip(a, b)):
                visit(left, right, _join(pointer, index))
        elif a != b:
            if pointer == "/source_root":
                if not (isinstance(a, str) and isinstance(b, str) and
                        a.endswith("/stage5/consumer-results") and
                        b.endswith("/stage5/consumer-results")):
                    failures.append("source_root relocation does not point to stage5/consumer-results")
                    return
            elif PATH_DIFFERENCE.fullmatch(pointer):
                if stage_result_suffix(a) is None or stage_result_suffix(a) != stage_result_suffix(b):
                    failures.append(f"path relocation changes indexed artifact identity at {pointer}")
                    return
            else:
                failures.append(f"unexpected analyzer summary difference at {pointer or '/'}")
                return
            differences.append({"path": pointer, "kind": "relocated_source_path"})

    visit(repo, portable)
    expected_indexes = len(repo.get("indexes", [])) if isinstance(repo, dict) else 0
    expected_profiles = len(repo.get("gpu_profiles", [])) if isinstance(repo, dict) else 0
    actual_index_diffs = sum(row["path"].startswith("/indexes/") for row in differences)
    actual_profile_diffs = sum(row["path"].startswith("/gpu_profiles/") for row in differences)
    if actual_index_diffs != expected_indexes:
        failures.append(f"expected {expected_indexes} relocated index paths, found {actual_index_diffs}")
    if actual_profile_diffs != expected_profiles:
        failures.append(f"expected {expected_profiles} relocated profile trace paths, found {actual_profile_diffs}")
    if len(differences) != 1 + expected_indexes + expected_profiles:
        failures.append("summary differences do not match the permitted source-root and file-path relocations")
    return {"status": "passed" if not failures else "failed",
            "difference_count": len(differences),
            "difference_paths": [row["path"] for row in differences],
            "allowed_path_differences_only": not failures,
            "numerical_and_count_fields_exact": not failures,
            "failures": failures}


def compare_plots(repo_plots: Path, portable_plots: Path) -> dict[str, Any]:
    repo = {path.name: path for path in repo_plots.glob("*.png") if path.is_file()}
    portable = {path.name: path for path in portable_plots.glob("*.png") if path.is_file()}
    common = sorted(repo.keys() & portable.keys())
    mismatches = [name for name in common if sha256(repo[name]) != sha256(portable[name])]
    expected_repo = EXPECTED_PORTABLE_PLOTS | {EXTRA_LOCAL_PLOT}
    failures = []
    if set(portable) != EXPECTED_PORTABLE_PLOTS:
        failures.append("portable plot filename set differs from the expected twelve analyzer outputs")
    if set(repo) != expected_repo:
        failures.append("repository plot filename set differs from twelve shared outputs plus the derived sibling plot")
    if mismatches:
        failures.append("one or more shared PNGs differ byte-for-byte")
    if EXTRA_LOCAL_PLOT not in repo or EXTRA_LOCAL_PLOT in portable:
        failures.append("expected 13th derived plot is not isolated from original portable analyzer outputs")
    return {"status": "passed" if not failures else "failed",
            "repository_plot_count": len(repo), "portable_plot_count": len(portable),
            "shared_plot_count": len(common), "byte_identical_shared_plot_count": len(common)-len(mismatches),
            "shared_plots": [{"name": name, "sha256": sha256(repo[name]),
                              "bytes": repo[name].stat().st_size}
                             for name in common if name not in mismatches],
            "mismatched_shared_plots": mismatches,
            "repository_only_plots": sorted(repo.keys()-portable.keys()),
            "portable_only_plots": sorted(portable.keys()-repo.keys()),
            "failures": failures}


def archive_binding(root: Path, archive_path: Path, member_name: str,
                    portable_analyzer: Path) -> dict[str, Any]:
    if not archive_path.is_file():
        raise FileNotFoundError("cataloged consumer evidence archive is missing")
    with tarfile.open(archive_path, mode="r:*") as archive:
        names = [member for member in archive.getmembers() if member.name.endswith("/MANIFEST.json")]
        if len(names) != 1:
            raise ValueError("consumer evidence archive must contain exactly one MANIFEST.json")
        stream = archive.extractfile(names[0])
        if stream is None:
            raise ValueError("consumer evidence MANIFEST.json could not be read")
        manifest_bytes = stream.read()
        manifest = json.loads(manifest_bytes)
    rows = [row for row in manifest.get("members", []) if row.get("member") == member_name]
    if len(rows) != 1:
        raise ValueError("analyzer source member is missing or ambiguous in archive manifest")
    actual = sha256(portable_analyzer)
    row = rows[0]
    if actual != row.get("sha256") or portable_analyzer.stat().st_size != row.get("size_bytes"):
        raise ValueError("extracted portable analyzer differs from the archived member binding")
    return {"archive_sha256": sha256(archive_path), "archive_bytes": archive_path.stat().st_size,
            "archive_repo_path": archive_path.relative_to(root).as_posix(),
            "manifest_member": names[0].name,
            "manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest(),
            "analyzer_member": member_name, "analyzer_member_sha256": row["sha256"],
            "analyzer_member_bytes": row["size_bytes"]}


def check_derived_points(summary: dict, derived: dict) -> dict:
    """Recompute signal-only statistics without importing the plotting code."""
    fields = ("variant", "policy", "gate", "stratum", "cn0_db_hz")
    outcomes = {tuple(row[key] for key in fields): row
                for row in summary["holdout_pointwise"]
                if row["variant"] == "holdout-verified" and
                row["stratum"] in ("boundary", "target")}
    seen = set()
    errors = []
    for point in derived["points"]:
        key = tuple(point[field] for field in fields)
        if key in seen or key not in outcomes:
            errors.append(f"duplicate or unexpected point: {key}")
            continue
        seen.add(key)
        outcome = outcomes[key]
        rows = [row for row in summary["runtime"]
                if row["stage"] == "screening-holdout" and
                row["variant"] == point["variant"] and
                row["policy"] == point["policy"] and
                row["cn0_db_hz"] == point["cn0_db_hz"] and
                row["input"].startswith(point["stratum"] + "-")]
        if (not rows or len(rows) != outcome["trials"] or
                len({row["input"] for row in rows}) != outcome["distinct_inputs"]):
            errors.append(f"signal timing/outcome count mismatch: {key}")
            continue
        median = statistics.median(row["complete_resident_s"] for row in rows)
        rate = outcome["successes_or_null_admissions"] / outcome["trials"]
        low, high = outcome["ci95_low"], outcome["ci95_high"]
        expected = {
            "trials": outcome["trials"], "successes": outcome["successes_or_null_admissions"],
            "resident_timing_count": len(rows), "point_success_fraction": rate,
            "median_complete_resident_s_per_capture": median,
            "time_per_observed_correct_admission_s": median / rate if rate else None,
            "time_interval_low_s": median / high if high else None,
            "time_interval_high_s": median / low if low else None,
        }
        factor = derived["hourly_rate_usd_assumption"] / 3600
        for cost, time in (("illustrative_compute_cost_usd_per_observed_correct_admission", "time_per_observed_correct_admission_s"),
                           ("illustrative_compute_cost_interval_low_usd", "time_interval_low_s"),
                           ("illustrative_compute_cost_interval_high_usd", "time_interval_high_s")):
            expected[cost] = expected[time] * factor if expected[time] is not None else None
        for name, value in expected.items():
            actual = point.get(name)
            matches = actual is None if value is None else (
                isinstance(actual, (int, float)) and math.isclose(actual, value, rel_tol=1e-14, abs_tol=0))
            if not matches:
                errors.append(f"derived field mismatch: {key}: {name}")
        if point["upper_interval_unbounded"] != (low == 0):
            errors.append(f"unbounded interval flag mismatch: {key}")
    if seen != set(outcomes) or derived["point_count"] != len(seen):
        errors.append("derived point set does not cover exactly the signal outcome groups")
    return {"status": "passed" if not errors else "failed", "points_checked": len(seen),
            "method": "independent standard-library recomputation from signal-stratum runtime rows and archived outcome groups; 1e-14 relative numeric tolerance",
            "scope": "derived medians, counts, reception fractions, interval transforms and illustrative cost; no waveform or receiver execution",
            "errors": errors}


def run(root: Path, portable_root: Path, archive_path: Path) -> dict[str, Any]:
    study = root / "docs/validation-data/triad-v3/2026-11-01-consumer-gpu"
    original_summary = study / "analysis/summary.json"
    portable_study = portable_root / PORTABLE_STUDY_REL
    portable_summary = portable_study / "analysis/summary.json"
    original_analyzer = study / "analyze.py"
    portable_analyzer = portable_study / "analyze.py"
    repo_plots = study / "plots"
    portable_plots = portable_root / "plots"
    required = (original_summary, portable_summary, original_analyzer,
                portable_analyzer, repo_plots, portable_plots)
    if any(not path.exists() for path in required):
        raise FileNotFoundError("one or more portability comparison inputs are missing")

    repo_data = json_file(original_summary)
    portable_data = json_file(portable_summary)
    summary_check = compare_summaries(repo_data, portable_data)
    plot_check = compare_plots(repo_plots, portable_plots)
    analyzer_archive_name = "triad-consumer-gpu-evidence/study/analyze.py"
    archive = archive_binding(root, archive_path, analyzer_archive_name, portable_analyzer)
    repo_analyzer_hash = sha256(original_analyzer)
    portable_analyzer_hash = sha256(portable_analyzer)
    if repo_analyzer_hash != portable_analyzer_hash:
        summary_check["status"] = "failed"
        summary_check["failures"].append("portable analyzer bytes differ from the repository analyzer")

    controls = root / "docs/validation-data/triad-v3/precision-controls"
    derived_plot = controls / EXTRA_LOCAL_PLOT
    corrected_cost_plot = controls / "illustrative-cost-per-observed-correct.png"
    derived_renderer = controls / "plot-resident-sensitivity.py"
    derived_points_path = controls / "resident-sensitivity-points.json"
    derived_points = json_file(derived_points_path)
    point_check = check_derived_points(repo_data, derived_points)
    expected_analyzer_absent = (EXTRA_LOCAL_PLOT not in original_analyzer.read_text() and
                                EXTRA_LOCAL_PLOT not in portable_analyzer.read_text())
    remedy_exists = (derived_plot.is_file() and corrected_cost_plot.is_file() and
                     derived_renderer.is_file() and derived_points_path.is_file())
    renderer_note = derived_points.get("renderer_note")
    derived_bindings = derived_points.get("source_input_sha256", {})
    binding_values = set(derived_bindings.values()) if isinstance(derived_bindings, dict) else set()
    remedy_bindings_match = {sha256(original_summary), sha256(derived_renderer),
                             sha256(original_analyzer)}.issubset(binding_values)
    renderer_note_explicit = (isinstance(renderer_note, str) and
                              "original PNG renderer is unknown" in renderer_note and
                              "not claimed to be recreated" in renderer_note)
    remedy_record = {
        "status": "passed" if expected_analyzer_absent and remedy_exists and
                  derived_points.get("artifact_kind") == "derived_plot_points_not_raw_measurements" and
                  remedy_bindings_match and renderer_note_explicit else "failed",
        "13th_plot": EXTRA_LOCAL_PLOT,
        "original_analyzer_has_renderer_reference": not expected_analyzer_absent,
        "original_renderer_unknown": True,
        "sibling_renderer": derived_renderer.relative_to(root).as_posix(),
        "sibling_renderer_sha256": sha256(derived_renderer) if derived_renderer.is_file() else None,
        "derived_points": derived_points_path.relative_to(root).as_posix(),
        "derived_points_sha256": sha256(derived_points_path) if derived_points_path.is_file() else None,
        "derived_point_count": derived_points.get("point_count"),
        "derived_source_bindings_match": remedy_bindings_match,
        "renderer_note_explicitly_limits_recreation_claim": renderer_note_explicit,
        "derived_png_sha256": sha256(derived_plot) if derived_plot.is_file() else None,
        "corrected_cost_png_sha256": sha256(corrected_cost_plot) if corrected_cost_plot.is_file() else None,
        "historical_cost_aggregation_limit": "Original analyzer pools signal and null timings at -10 dB-Hz; the new derived views group signal strata separately. Historical results stay unchanged.",
        "renderer_note": renderer_note,
        "interpretation": "The sibling renderer creates a separately labeled derived plot from archived summary values; it does not claim to reproduce the unknown original renderer.",
    }
    hashes = {
        "portability_checker_sha256": sha256(Path(__file__).resolve()),
        "repository_analyzer_sha256": repo_analyzer_hash,
        "portable_analyzer_sha256": portable_analyzer_hash,
        "repository_summary_sha256": sha256(original_summary),
        "portable_summary_sha256": sha256(portable_summary),
        "repository_analyzer_plot_source_binding_sha256": archive["analyzer_member_sha256"],
        "derived_renderer_sha256": remedy_record["sibling_renderer_sha256"],
        "derived_points_sha256": remedy_record["derived_points_sha256"],
    }
    status = "passed" if all(check["status"] == "passed" for check in
                              (summary_check, plot_check, remedy_record, point_check)) else "failed"
    return {
        "schema": "triad-consumer-analysis-portability-v1",
        "status": status,
        "scope": "offline archived analyzer rerun summary and plot byte comparison",
        "qualification_claim": "none; this is an artifact portability check, not receiver qualification",
        "versions": {"python": platform.python_version(),
                     "numpy": version("numpy"), "matplotlib": version("matplotlib")},
        "archive_binding": archive,
        "hashes": hashes,
        "summary_comparison": summary_check,
        "plot_comparison": plot_check,
        "derived_plot_remedy": remedy_record,
        "derived_point_check": point_check,
        "analyzer_rerun_by_checker": False,
        "gpu_used": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--portable-root", type=Path,
                        default=Path("/tmp/triad-precision-portable-consumer"))
    parser.add_argument("--archive", type=Path, default=None)
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()
    root = args.root.resolve()
    portable_root = args.portable_root.resolve()
    archive = (args.archive if args.archive else
               root / "docs/validation-data/triad-v3/2026-11-01-consumer-gpu/technical-evidence.tar.gz").resolve()
    output = (args.output if args.output else
              root / "docs/validation-data/triad-v3/precision-controls/portability-check.json").resolve()
    if output.exists():
        parser.error(f"refusing to overwrite portability record: {output}")
    try:
        output.relative_to(root)
    except ValueError:
        parser.error("--output must be inside --root")
    try:
        result = run(root, portable_root, archive)
    except (OSError, ValueError, json.JSONDecodeError, tarfile.TarError) as error:
        result = {"schema": "triad-consumer-analysis-portability-v1", "status": "failed",
                  "scope": "offline archived analyzer rerun summary and plot byte comparison",
                  "qualification_claim": "none", "error": f"{type(error).__name__}: {error}",
                  "gpu_used": False}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"status": result["status"], "summary": result.get("summary_comparison", {}).get("difference_count"),
                      "shared_plots_identical": result.get("plot_comparison", {}).get("byte_identical_shared_plot_count"),
                      "output": output.relative_to(root).as_posix()}, sort_keys=True))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
