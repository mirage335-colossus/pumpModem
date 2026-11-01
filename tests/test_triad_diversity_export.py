from __future__ import annotations

import importlib.util
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest


REPOSITORY = pathlib.Path(__file__).resolve().parents[1]
DRIVER = REPOSITORY / "tools" / "triad-diversity-export.py"
RUNNER = REPOSITORY / "tools" / "triad_bounded_run.py"
SPEC = importlib.util.spec_from_file_location("triad_diversity_export", DRIVER)
EXPORT = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
SPEC.loader.exec_module(EXPORT)


FAKE_BINARY = r'''#!/usr/bin/env python3
import json, pathlib, sys
args = sys.argv[1:]
values = {}
flags = set()
i = 0
while i < len(args):
    token = args[i]
    if token.startswith("--"):
        if "=" in token:
            key, value = token.split("=", 1)
            values[key] = value
        elif i + 1 < len(args) and not args[i + 1].startswith("--"):
            values[token] = args[i + 1]
            i += 1
        else:
            flags.add(token)
    i += 1
bit = int(values["--signal-bit"])
if "--fail-bit-one" in flags and bit == 1:
    print("intentional fixture failure")
    sys.exit(17)
if "--skip-manifest" in flags:
    sys.exit(0)
seed_names = ("seed", "key-seed", "channel-seed", "oscillator-seed", "noise-seed", "frontend-seed")
roots = {"channel": int(values["--channel-seed"]),
         "oscillator": int(values["--oscillator-seed"]),
         "noise": int(values["--noise-seed"]),
         "frontend": int(values["--frontend-seed"])}
manifest = {"format": "triad-block-stats-v1", "stats_noisy": "--stats-noisy" in flags,
            "command_argv": args, "random_seed_roots": roots}
if "--bad-seed" in flags:
    manifest["random_seed_roots"]["channel"] += 1
out = pathlib.Path(values["--output"])
out.mkdir(parents=True)
(out / "manifest.json").write_text(json.dumps(manifest))
'''


class DiversityExportTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix="triad-export-test-")
        self.root = pathlib.Path(self.temp.name)
        self.binary = self.root / "fake-triad"
        self.binary.write_text(FAKE_BINARY, encoding="utf-8")
        self.binary.chmod(0o755)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def launch(self, output: pathlib.Path, *driver_args: str) -> subprocess.CompletedProcess[str]:
        outer_log = self.root / f"outer-{len(list(self.root.glob('outer-*.log')))}.log"
        status = outer_log.with_suffix(".json")
        command = [sys.executable, str(RUNNER), "--cwd", str(REPOSITORY),
                   "--log", str(outer_log), "--status", str(status),
                   "--timeout", "30", "--max-seconds", "30", "--term-grace", "0.2",
                   "--", sys.executable, str(DRIVER), "--binary", str(self.binary),
                   "--output", str(output), *driver_args]
        return subprocess.run(command, cwd=REPOSITORY, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              timeout=40, check=False)

    def campaign_args(self, *extra: str) -> tuple[str, ...]:
        return ("--paths", "4", "--jobs", "2", "--split", "holdout", "--case", "paired",
                "--", *extra)

    def test_pair_seeds_stable_across_projection_knobs_and_full_campaign_succeeds(self) -> None:
        first = self.root / "first"
        second = self.root / "second"
        a = self.launch(first, *self.campaign_args("--block-seconds", "0.2", "--cn0", "-8"))
        b = self.launch(second, *self.campaign_args("--block-seconds", "1.6", "--cn0", "-20"))
        self.assertEqual(a.returncode, 0, a.stdout)
        self.assertEqual(b.returncode, 0, b.stdout)
        left = json.loads((first / "campaign.json").read_text())
        right = json.loads((second / "campaign.json").read_text())
        self.assertEqual(left["status"], "completed")
        self.assertEqual([p["status"] for p in left["paths"]], ["completed"] * 4)
        for a_path, b_path in zip(left["paths"], right["paths"]):
            for key in ("signal_bit", "epoch", "seeds"):
                self.assertEqual(a_path[key], b_path[key])
            self.assertEqual(a_path["returncode"], 0)
        self.assertEqual([p["signal_bit"] for p in left["paths"]], [0, 1, 0, 1])
        self.assertNotEqual(left["paths"][0]["seeds"]["channel"],
                            left["paths"][0]["seeds"]["noise"])
        first_command = left["paths"][0]["command"]
        self.assertIn("0.2", first_command)
        self.assertIn("-8", first_command)
        self.assertEqual(left["paths"][0]["binary_sha256"], EXPORT.sha256_file(self.binary))
        self.assertEqual(left["paths"][0]["driver_sha256"], EXPORT.sha256_file(DRIVER))

    def test_failed_export_is_not_a_pass_and_remaining_indices_are_missing(self) -> None:
        output = self.root / "failed"
        result = self.launch(output, "--paths", "4", "--jobs", "1", "--split", "development",
                             "--case", "failure", "--", "--fail-bit-one")
        self.assertEqual(result.returncode, 1, result.stdout)
        report = json.loads((output / "campaign.json").read_text())
        self.assertEqual(report["status"], "incomplete")
        self.assertEqual(report["paths"][0]["status"], "completed")
        self.assertEqual(report["paths"][1]["status"], "export_failed")
        self.assertEqual(report["paths"][1]["returncode"], 17)
        self.assertEqual(report["paths"][2]["status"], "not_started_after_failure")
        self.assertEqual(report["paths"][3]["status"], "not_started_after_failure")
        self.assertEqual(report["missing_paths"], [1, 2, 3])

    def test_conflicting_forced_options_rejected_before_output(self) -> None:
        output = self.root / "conflict"
        result = self.launch(output, *self.campaign_args("--seed=42"))
        self.assertEqual(result.returncode, 2, result.stdout)
        outer_log = sorted(self.root.glob("outer-*.log"))[-1]
        self.assertIn("controlled by the campaign driver", outer_log.read_text())
        self.assertFalse(output.exists())

    def test_invalid_path_count_and_jobs_do_not_create_output(self) -> None:
        for suffix, options in (("odd", ("--paths", "3")),
                                ("wide", ("--paths", "4", "--jobs", "33"))):
            output = self.root / suffix
            result = self.launch(output, *options, "--split", "holdout", "--case", "bad")
            self.assertEqual(result.returncode, 2, result.stdout)
            self.assertFalse(output.exists())

    def test_success_without_manifest_is_rejected(self) -> None:
        output = self.root / "no-manifest"
        result = self.launch(output, "--paths", "2", "--jobs", "1", "--split", "holdout",
                             "--case", "bad-manifest", "--", "--skip-manifest")
        self.assertEqual(result.returncode, 1, result.stdout)
        report = json.loads((output / "campaign.json").read_text())
        self.assertEqual(report["paths"][0]["status"], "manifest_error")
        self.assertNotEqual(report["status"], "completed")

    def test_driver_requires_bounded_parent(self) -> None:
        output = self.root / "unbounded"
        command = [sys.executable, str(DRIVER), "--binary", str(self.binary),
                   "--output", str(output), "--paths", "2", "--split", "holdout",
                   "--case", "direct"]
        result = subprocess.run(command, cwd=REPOSITORY, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=10, check=False)
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("directly through triad_bounded_run.py", result.stdout)
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
