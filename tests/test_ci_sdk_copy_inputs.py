import importlib.util
import json
import os
import pathlib
import tempfile
import unittest
from unittest.mock import patch


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "ci_sdk_copy_inputs", ROOT / "tools/ci-sdk-copy-inputs.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


SHA = "efb0ebb972a68614c44ed80bce7eba162de43366"
REPOSITORY = "mirage335-colossus/pumpModem"


def fixtures():
    run = {
        "id": 39819386677,
        "repository": {"full_name": REPOSITORY},
        "head_repository": {"full_name": REPOSITORY},
        "head_sha": SHA,
        "path": ".github/workflows/sdk.yml",
        "event": "workflow_dispatch",
    }
    # These are the actual nested reusable-workflow job names in run39819386677,
    # deliberately independent of the implementation's lookup constants.
    jobs = [{"id": 90 + index, "run_id": run["id"], "head_sha": SHA,
             "status": "completed", "name": name, "conclusion": "success"}
            for index, name in enumerate([
                "SDK packages, fltk / SDK packages / fltk / debian:bookworm-slim",
                "SDK packages, rev / SDK packages / rev / ubuntu:24.04",
            ])]
    artifacts = [
        {
            "id": index + 70,
            "name": MODULE.ARTIFACTS[backend],
            "expired": False,
            "size_in_bytes": 12345,
            "digest": "sha256:" + "a" * 64,
            "workflow_run": {"id": run["id"], "head_sha": SHA},
        }
        for index, backend in enumerate(MODULE.BACKENDS)
    ]
    return run, jobs, artifacts


class CopyInputValidation(unittest.TestCase):
    def test_selects_only_expected_immutable_artifacts(self):
        run, jobs, artifacts = fixtures()
        selected = MODULE.validate_metadata(REPOSITORY, run["id"], SHA, run, jobs, artifacts)
        self.assertEqual(selected["fltk"]["id"], 70)
        self.assertEqual(selected["rev"]["id"], 71)

    def test_rejects_invalid_run_id_and_sha(self):
        for value in ("0", "-1", "1x"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                MODULE.positive_run_id(value)
        run, jobs, artifacts = fixtures()
        with self.assertRaisesRegex(ValueError, "40 hexadecimal"):
            MODULE.validate_metadata(REPOSITORY, run["id"], "abc", run, jobs, artifacts)

    def test_rejects_fork_wrong_workflow_event_or_source_sha(self):
        for field, value in (
            ("repository", {"full_name": "attacker/fork"}),
            ("head_repository", {"full_name": "attacker/fork"}),
            ("path", ".github/workflows/other.yml"),
            ("event", "pull_request"),
            ("head_sha", "b" * 40),
        ):
            run, jobs, artifacts = fixtures()
            run[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                MODULE.validate_metadata(REPOSITORY, run["id"], SHA, run, jobs, artifacts)

    def test_normal_and_recovery_paths_preserve_four_copy_cases(self):
        workflow = (ROOT / ".github/workflows/sdk.yml").read_text()
        import yaml

        parsed = yaml.load(workflow, Loader=yaml.BaseLoader)
        dispatch = parsed["on"]["workflow_dispatch"]["inputs"]
        self.assertEqual(dispatch["copy_run_id"]["type"], "string")
        self.assertEqual(dispatch["copy_source_sha"]["type"], "string")
        self.assertNotIn("copy_run_id", parsed["on"]["workflow_call"].get("inputs", {}))
        jobs = parsed["jobs"]
        self.assertEqual(jobs["packages"]["if"], "${{ !inputs.devfast && !inputs.copy_run_id && !inputs.copy_source_sha }}")
        producer_cases = jobs["packages"]["strategy"]["matrix"]["include"]
        self.assertEqual(
            {(case["backend"], case["image"]) for case in producer_cases},
            {("fltk", "debian:bookworm-slim"), ("rev", "ubuntu:24.04")},
        )
        expected_matrix = "backend: [fltk, rev]\n        image: [debian:bookworm-slim, ubuntu:24.04]"
        self.assertEqual(workflow.count(expected_matrix), 2)
        self.assertIn("if: github.event_name == 'workflow_dispatch' && inputs.devfast && inputs.copy_run_id != '' && inputs.copy_source_sha != ''", workflow)
        self.assertIn("if: github.event_name == 'workflow_dispatch' && (inputs.copy_run_id != '' || inputs.copy_source_sha != '')", workflow)
        verify = "-DGUI_SMOKE_TIMEOUT=600 -DMAX_GLIBC=2.36 -DREQUIRE_MANUALS=ON -DREQUIRE_FRONTENDS=ON"
        self.assertEqual(workflow.count(verify), 2)
        self.assertEqual(workflow.count("export CI_APT_DOWNLOAD_TIMEOUT=180"), 2)

    def test_rejects_missing_or_cancelled_package_producer(self):
        run, jobs, artifacts = fixtures()
        jobs[0]["conclusion"] = "cancelled"
        with self.assertRaisesRegex(ValueError, "did not succeed"):
            MODULE.validate_metadata(REPOSITORY, run["id"], SHA, run, jobs, artifacts)
        run, jobs, artifacts = fixtures()
        jobs.pop()
        with self.assertRaisesRegex(ValueError, "missing or duplicated"):
            MODULE.validate_metadata(REPOSITORY, run["id"], SHA, run, jobs, artifacts)

    def test_rejects_missing_expired_or_cross_run_artifacts(self):
        for mutate, message in (
            (lambda a: a.pop(), "missing or duplicated"),
            (lambda a: a[0].update(expired=True), "expired"),
            (lambda a: a[0]["workflow_run"].update(id=999), "not bound"),
        ):
            run, jobs, artifacts = fixtures()
            mutate(artifacts)
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                MODULE.validate_metadata(REPOSITORY, run["id"], SHA, run, jobs, artifacts)

    def test_paginates_until_short_page(self):
        first = {"total_count": 101, "jobs": [{"id": n + 1} for n in range(100)]}
        second = {"total_count": 101, "jobs": [{"id": 101}]}
        with patch.object(MODULE, "api_json", side_effect=[first, second]) as api:
            values = MODULE.paginated("https://api.example/jobs", "token", "jobs")
        self.assertEqual(len(values), 101)
        self.assertEqual(api.call_count, 2)

    def test_rejects_duplicate_or_mismatched_producer_and_artifact_identities(self):
        for kind in ("job_duplicate", "artifact_duplicate", "job_source", "job_run", "artifact_bool", "size_bool"):
            run, jobs, artifacts = fixtures()
            if kind == "job_duplicate": jobs.append(dict(jobs[0]))
            elif kind == "artifact_duplicate": artifacts.append(dict(artifacts[0]))
            elif kind == "job_source": jobs[0]["head_sha"] = "b" * 40
            elif kind == "job_run": jobs[0]["run_id"] = 999
            elif kind == "artifact_bool": artifacts[0]["id"] = True
            elif kind == "size_bool": artifacts[0]["size_in_bytes"] = True
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                MODULE.validate_metadata(REPOSITORY, run["id"], SHA, run, jobs, artifacts)

    def test_pagination_rejects_truncation_duplicates_and_changed_inventory(self):
        first = {"total_count": 101, "jobs": [{"id": n + 1} for n in range(100)]}
        for pages in ([{"total_count": 2, "jobs": [{"id": 1}]}],
                      [first, {"total_count": 101, "jobs": [{"id": 100}]}],
                      [first, {"total_count": 102, "jobs": [{"id": 101}, {"id": 102}]}]):
            with self.subTest(pages=len(pages)), patch.object(MODULE, "api_json", side_effect=pages):
                with self.assertRaises(ValueError):
                    MODULE.paginated("https://api.example/jobs", "token", "jobs")

    def test_main_emits_canonical_provenance_and_retained_receipt(self):
        run, jobs, artifacts = fixtures()
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            env = {"GITHUB_EVENT_NAME": "workflow_dispatch", "INPUT_DEVFAST": "true",
                   "INPUT_COPY_RUN_ID": str(run["id"]), "INPUT_COPY_SOURCE_SHA": SHA.upper(),
                   "GITHUB_REPOSITORY": REPOSITORY, "GITHUB_TOKEN": "fixture-not-a-real-token",
                   "GITHUB_SHA": "c" * 40, "GITHUB_OUTPUT": str(root / "outputs"),
                   "COPY_RECEIPT_PATH": str(root / "receipt.json")}
            with patch.dict(os.environ, env, clear=True), patch.object(MODULE, "api_json", return_value=run), \
                    patch.object(MODULE, "paginated", side_effect=[jobs, artifacts]):
                MODULE.main()
            outputs = dict(line.split("=", 1) for line in (root / "outputs").read_text().splitlines())
            self.assertEqual(outputs["source_sha"], SHA)
            self.assertEqual(json.loads(outputs["artifact_ids"]), {"fltk": 70, "rev": 71})
            receipt = json.loads((root / "receipt.json").read_text())
            self.assertEqual(receipt["tooling_sha"], "c" * 40)
            self.assertEqual(receipt["source_sha"], SHA)
            self.assertEqual(receipt["artifacts"]["fltk"]["producer_job_id"], 90)
            self.assertNotIn("fixture-not-a-real-token", (root / "receipt.json").read_text())


if __name__ == "__main__":
    unittest.main()
