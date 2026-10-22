#!/usr/bin/env python3
"""Pure fixtures for the SDK burst diagnostic's scope guards."""

import importlib.util
import subprocess
import sys
from unittest import mock
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "sdk_burst_scheduling_diagnostic", ROOT / "tools/sdk-burst-scheduling-diagnostic.py")
diagnostic = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(diagnostic)


class DiagnosticScopeTests(unittest.TestCase):
    @unittest.skipUnless(Path("/proc").is_dir(), "Linux process groups required")
    def test_outer_cutoff_closes_owned_process_group(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with (root / "driver.log").open("wb") as output:
                with mock.patch.object(diagnostic, "TRIAL_WALL_SECONDS", .2):
                    result = diagnostic.execute([sys.executable, "-c", "import time; time.sleep(30)"], root, output)
            self.assertTrue(result["outer_timed_out"])
            self.assertTrue(result["driver_reaped"])
            self.assertEqual(result["remaining_after_cleanup"], [])

    @unittest.skipUnless(Path("/proc").is_dir(), "Linux process groups required")
    def test_interrupted_wait_closes_owned_process_group(self):
        spawned = []
        real_popen = subprocess.Popen
        def spawn(*args, **kwargs):
            child = real_popen(*args, **kwargs)
            spawned.append(child)
            real_wait = child.wait
            calls = 0
            def wait(*args, **kwargs):
                nonlocal calls
                calls += 1
                if calls == 1:
                    raise KeyboardInterrupt("fixture signal")
                return real_wait(*args, **kwargs)
            child.wait = wait
            return child
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with (root / "driver.log").open("wb") as output:
                with mock.patch.object(diagnostic.subprocess, "Popen", side_effect=spawn):
                    with self.assertRaises(KeyboardInterrupt):
                        diagnostic.execute([sys.executable, "-c", "import time; time.sleep(30)"], root, output)
        self.assertIsNotNone(spawned[0].poll())
        self.assertEqual(diagnostic.group_members(spawned[0].pid), [])

    def test_trial_matrix_has_three_original_runs_per_variant(self):
        alone = [trial for trial in diagnostic.TRIALS if trial[2] == 1]
        paired = [trial for trial in diagnostic.TRIALS if trial[2] == 2]
        self.assertEqual(len(alone), 3)
        self.assertEqual(len(paired), 3)
        self.assertEqual(sum(trial[3] for trial in alone), 2)
        self.assertEqual(sum(trial[3] for trial in paired), 2)
        self.assertEqual({trial[1] for trial in alone}, {"^(symbol_burst_live)$"})
        self.assertEqual({trial[1] for trial in paired}, {"^(symbol_burst_live|weak_signal)$"})

    def test_inventory_requires_exact_no_argument_registered_elves_and_timeout(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            data = {"tests": [
                {"name": name, "command": [str(build / binary)],
                 "properties": [{"name": "TIMEOUT", "value": "3600"}]}
                for name, binary in (("symbol_burst_live", "test_symbol_burst_live"),
                                     ("weak_signal", "test_weak_signal"))]}
            self.assertIs(diagnostic.validate_inventory(data, build), data)
            data_without_explicit_timeouts = {"tests": [
                {"name": item["name"], "command": item["command"], "properties": []}
                for item in data["tests"]]}
            self.assertIs(diagnostic.validate_inventory(data_without_explicit_timeouts, build),
                          data_without_explicit_timeouts)
            malformed = {"tests": [dict(item) for item in data["tests"]]}
            malformed["tests"][0]["command"] = [str(build / "test_symbol_burst_live"), "--changed"]
            with self.assertRaises(RuntimeError):
                diagnostic.validate_inventory(malformed, build)
            malformed = {"tests": [dict(item) for item in data["tests"]]}
            malformed["tests"][1]["properties"] = [{"name": "TIMEOUT", "value": "25"}]
            with self.assertRaises(RuntimeError):
                diagnostic.validate_inventory(malformed, build)

    def test_profile_rejects_nonportable_or_non_sdk_build(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory) / "sdk"
            compilers = {kind: sdk / f"bin/{kind.lower()}++" for kind in ("C", "CXX")}
            compilers["C"] = sdk / "bin/gcc"
            compilers["C"].parent.mkdir(parents=True)
            for compiler in compilers.values():
                compiler.touch()
            cache = {"CMAKE_BUILD_TYPE": "Release", "DATAPUMP_GUI_BACKEND": "fltk",
                     "DATAPUMP_PORTABLE": "ON", "DATAPUMP_CI_HOSTED_CLOCK_FUNCTIONAL": "ON",
                     "DATAPUMP_SANITIZERS": "OFF", "DATAPUMP_BUILD_TUI": "ON",
                     "DATAPUMP_BUILD_FB": "ON", "DATAPUMP_TEST_NATIVE_GUI": "OFF",
                     "CMAKE_C_COMPILER": str(compilers["C"]),
                     "CMAKE_CXX_COMPILER": str(compilers["CXX"])}
            expected, actual = diagnostic.validate_profile(cache, sdk)
            self.assertEqual(expected["DATAPUMP_PORTABLE"], "ON")
            self.assertEqual(actual, {kind: compiler.resolve() for kind, compiler in compilers.items()})
            cache["DATAPUMP_CI_HOSTED_CLOCK_FUNCTIONAL"] = "OFF"
            with self.assertRaises(RuntimeError):
                diagnostic.validate_profile(cache, sdk)


if __name__ == "__main__":
    unittest.main()
