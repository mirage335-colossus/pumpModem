import json
import inspect
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools import triad_perf_cases as cases


class PerformanceCaseTests(unittest.TestCase):
    def test_selected_fixture_is_filtered_before_plan_or_native_work(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(cases, "_plan", side_effect=RuntimeError("before native work")) as plan:
                with self.assertRaisesRegex(RuntimeError, "before native work"):
                    cases.prepare_cases("/bin/true", temporary, case_ids=["free-crystal"])
                self.assertEqual(plan.call_args.args[0]["id"], "free-crystal")
                plan.assert_called_once()
            with patch.object(cases, "_plan") as plan:
                self.assertEqual(cases.prepare_cases("/bin/true", temporary, case_ids=[]), [])
                plan.assert_not_called()

    def test_profiles_are_diverse_and_long_case_is_real_native_point_model(self):
        selected = cases.scenarios("ci")
        self.assertEqual(len(selected), 5)
        self.assertEqual(selected[-1]["id"], "vlf-850-day")
        self.assertEqual(selected[2]["source_case"], "16-miss-middle-scattered")
        self.assertIn("850-day", selected[-1]["scope"])
        with self.assertRaises(ValueError):
            cases.scenarios("unknown")

    def test_broad_prior_preserves_public_lower_anchor(self):
        broad = next(c for c in cases.scenarios("ci") if c["id"] == "broad-hf-timing")
        args = broad["native_overrides"]
        start, prior = args.index("--start"), args.index("--arrival-prior-seconds")
        self.assertEqual(args[start+1:start+4], ["-7.035", "-7.035", "1"])
        self.assertEqual(args[prior+1:prior+3], ["-7.035", "7.035"])

    def test_frequency_grid_covers_bound_at_declared_loss(self):
        limits = {"frequency_loss_db": 0.5, "max_frequency_offsets": 4096}
        grid = cases._frequency_grid(
            {"deterministic_oscillator_cfo_bound_hz": 0.5,
             "duration": {"elapsed_source_calendar_seconds": 8.0}},
            sample_count=100_000, sample_rate=1000, limits=limits)
        self.assertEqual(grid[0], -0.5)
        self.assertAlmostEqual(grid[-1], 0.5)
        self.assertGreater(len(grid), 1)
        bound = 10 ** (-0.5 / 10)
        mismatch = (grid[1] - grid[0]) / 2
        ratio = (abs(__import__("math").sin(__import__("math").pi * mismatch * 8) /
                     (__import__("math").pi * mismatch * 8))) ** 2
        self.assertGreaterEqual(ratio + 1e-12, bound)

    def test_frequency_grid_rejects_oversized_physical_bank(self):
        with self.assertRaisesRegex(cases.FixtureError, "refusing to shrink"):
            cases._frequency_grid(
                {"deterministic_oscillator_cfo_bound_hz": 100.0,
                 "duration": {"elapsed_source_calendar_seconds": 100.0}},
                sample_count=1000, sample_rate=1,
                limits={"frequency_loss_db": 0.5, "max_frequency_offsets": 4096})

    def test_cache_hashes_all_four_public_files_and_manifest_sizes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            workspace = root / "workspace"
            workspace.mkdir()
            blobs = {"basis.c128": b"basis", "observation.c128": b"observation", "validity.u8": b"mask"}
            for name, content in blobs.items():
                (workspace / name).write_bytes(content)
            manifest = {
                "format": "triad-sampled-waveforms-v1",
                "sample_count": len(blobs["validity.u8"]),
                "channel_samples": len(blobs["validity.u8"]),
                "channel": "none",
                "file_sizes": {name: len(content) for name, content in blobs.items()},
            }
            (workspace / "manifest.json").write_text(json.dumps(manifest))
            hashes = cases._workspace_hashes(workspace)
            (root / "fixture.json").write_text(json.dumps({
                "cache_key": "key", "source_hashes": {"source": "hash"},
                "fixture_hashes": hashes,
            }))
            self.assertEqual(cases._valid_cache(root, "key", {"source": "hash"}), hashes)
            (workspace / "observation.c128").write_bytes(b"modified")
            self.assertIsNone(cases._valid_cache(root, "key", {"source": "hash"}))

    def test_export_args_keep_native_impairments_and_canonicalize_output(self):
        fixture_case = {"id": "wide", "native_overrides": ["--arrival-prior-seconds", "-7.035", "7.035"]}
        source = {"trial": {"export_argv": ["{BINARY}", "--mode", "export-sampled", "--output", "old"]}}
        argv = cases._native_argv(Path("/bin/true"), fixture_case, source, Path("/tmp/new"),
                                  "export-sampled", {"memory_mib": 1024, "export_max_mib": 512,
                                                     "max_samples": 1_000_000})
        self.assertIn("--arrival-prior-seconds", argv)
        self.assertEqual(argv[argv.index("--output") + 1], "/tmp/new")
        self.assertEqual(argv[argv.index("--export-max-samples") + 1], "1000000")
        self.assertEqual(cases._normalize_args(argv)[argv.index("--output") + 1], "{WORKSPACE}")

    def test_source_root_selects_candidate_hashes_without_moving_input_plan(self):
        with tempfile.TemporaryDirectory() as temporary:
            root_a = Path(temporary) / "candidate-a"
            root_b = Path(temporary) / "candidate-b"
            for root in (root_a, root_b):
                for relative in cases.SOURCE_FILES:
                    path = root / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text("same candidate source\n")
            changed = root_b / cases.SOURCE_FILES[0]
            changed.write_text("different candidate source\n")
            plan = Path(temporary) / "frozen-plan.json"
            plan.write_text("{}\n")
            hashes_a = cases._source_hashes(root_a, plan)
            hashes_b = cases._source_hashes(root_b, plan)
            self.assertNotEqual(hashes_a[cases.SOURCE_FILES[0]], hashes_b[cases.SOURCE_FILES[0]])
            self.assertEqual(hashes_a["plan"], hashes_b["plan"])
            self.assertEqual(inspect.signature(cases.prepare_cases).parameters["source_root"].default, None)


if __name__ == "__main__":
    unittest.main()
