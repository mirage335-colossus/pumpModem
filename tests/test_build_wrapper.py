#!/usr/bin/env python3
"""Exercise build orchestration without compiling or downloading dependencies."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


@unittest.skipIf(os.name == "nt", "build.sh is a POSIX entry point; use CMake presets on Windows")
class BuildWrapperTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="datapump wrapper ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source with spaces"
        self.source.mkdir()
        shutil.copy2(Path(__file__).resolve().parents[1] / "build.sh", self.source)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.log = self.root / "calls.jsonl"
        mock = "#!" + sys.executable + "\n" + '''import json, os, pathlib, sys
with open(os.environ["WRAPPER_LOG"], "a", encoding="utf-8") as log:
    log.write(json.dumps([pathlib.Path(sys.argv[0]).name, *sys.argv[1:]]) + "\\n")
if os.environ.get("FAIL_STEP") == ("build" if "--build" in sys.argv else "configure"):
    sys.exit(1)
'''
        for name in ("cmake", "ctest", "ninja"):
            tool = self.bin / name
            tool.write_text(mock, encoding="utf-8")
            tool.chmod(0o755)
        self.env = os.environ.copy()
        for name in ("CC", "CXX", "CMAKE_GENERATOR", "DATAPUMP_JOBS",
                     "CMAKE_BUILD_PARALLEL_LEVEL", "DATAPUMP_MAX_GLIBC"):
            self.env.pop(name, None)
        self.env.update(PATH=str(self.bin) + os.pathsep + self.env["PATH"],
                        WRAPPER_LOG=str(self.log))

    def run_wrapper(self, *args, success=True, **environment):
        result = subprocess.run([str(self.source / "build.sh"), *args],
                                cwd=self.root, env=dict(self.env, **environment),
                                text=True, capture_output=True, check=False)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        calls = [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []
        return result, calls

    def test_default_builds_only_applications(self):
        _, calls = self.run_wrapper()
        self.assertEqual(len(calls), 2)
        self.assertIn("dev", calls[0])
        self.assertIn(str(self.source / "build/dev"), calls[0])
        self.assertIn("Ninja", calls[0])
        self.assertIn("datapump-apps", calls[1])
        self.assertNotIn("ctest", [call[0] for call in calls])

    def test_test_group_is_built_before_ctest(self):
        _, calls = self.run_wrapper("test", "fast", "--jobs", "3")
        self.assertIn("datapump-tests-fast", calls[1])
        self.assertEqual(calls[2][0], "ctest")
        self.assertIn("^fast$", calls[2])
        self.assertIn("--no-tests=error", calls[2])
        self.assertEqual(calls[2][calls[2].index("--parallel") + 1], "3")

    def test_all_tests_exclude_native_display(self):
        _, calls = self.run_wrapper("test", "all")
        self.assertIn("-DDATAPUMP_TEST_NATIVE_GUI=OFF", calls[0])
        self.assertIn("datapump-tests", calls[1])
        self.assertEqual(calls[2][-2:], ["-LE", "native_gui"])

    def test_native_tests_enable_adapter(self):
        _, calls = self.run_wrapper("test", "native")
        self.assertIn("-DDATAPUMP_TEST_NATIVE_GUI=ON", calls[0])
        self.assertIn("datapump-tests-native", calls[1])
        self.assertIn("^native_gui$", calls[2])

    def test_sanitizers_default_to_headless_contract(self):
        _, calls = self.run_wrapper("sanitize")
        self.assertIn("sanitize", calls[0])
        self.assertIn("-DDATAPUMP_BUILD_GUI=OFF", calls[0])
        self.assertIn("datapump-tests-contract", calls[1])
        self.assertIn("Debug", calls[1])
        self.assertIn("^contract$", calls[2])

    def test_cmake_arguments_remain_single_arguments(self):
        value = "-DDATAPUMP_DEPENDENCY_PREFIX=/SDK path/with spaces;and punctuation"
        _, calls = self.run_wrapper("--cli", "--build-dir", "my output", "--", value)
        self.assertIn(str(self.root / "my output"), calls[0])
        self.assertIn(value, calls[0])
        self.assertIn("-DDATAPUMP_BUILD_GUI=OFF", calls[0])

    def test_explicit_generator_is_not_replaced(self):
        _, calls = self.run_wrapper("--", "-G", "Unix Makefiles")
        self.assertEqual(calls[0].count("-G"), 1)
        self.assertIn("Unix Makefiles", calls[0])
        self.assertNotIn("Ninja", calls[0])

    def test_existing_generator_is_retained(self):
        output = self.source / "build/dev"
        output.mkdir(parents=True)
        (output / "CMakeCache.txt").write_text("CMAKE_GENERATOR:INTERNAL=Unix Makefiles\n")
        _, calls = self.run_wrapper()
        self.assertNotIn("-G", calls[0])

    def test_multiconfiguration_output_path_includes_configuration(self):
        output = self.source / "build/dev"
        output.mkdir(parents=True)
        (output / "CMakeCache.txt").write_text("CMAKE_CONFIGURATION_TYPES:STRING=Debug;Release\n")
        result, _ = self.run_wrapper()
        self.assertIn(str(output / "Release/datapump-gui"), result.stdout)

    def test_make_fallback_when_ninja_is_unavailable(self):
        (self.bin / "ninja").unlink()
        (self.bin / "dirname").symlink_to(shutil.which("dirname"))
        _, calls = self.run_wrapper(PATH=str(self.bin))
        self.assertIn("Unix Makefiles", calls[0])

    def test_cached_compiler_change_is_rejected(self):
        output = self.source / "build/dev"
        output.mkdir(parents=True)
        (output / "CMakeCache.txt").write_text("CMAKE_C_COMPILER:FILEPATH=/old/compiler\n")
        result, calls = self.run_wrapper(success=False, CC="/new/compiler")
        self.assertIn("use --build-dir", result.stderr)
        self.assertEqual(calls, [])

    def compiler_cache(self, entries):
        output = self.source / "build/dev"
        output.mkdir(parents=True, exist_ok=True)
        lines = []
        for language, name, arguments in entries:
            compiler = self.bin / name
            compiler.write_text("#!/bin/sh\nexit 0\n")
            compiler.chmod(0o755)
            lines.extend((f"CMAKE_{language}_COMPILER:FILEPATH={compiler}\n",
                          f"CMAKE_{language}_COMPILER_ARG1:STRING={arguments}\n"))
        (output / "CMakeCache.txt").write_text("".join(lines))

    def test_unchanged_compiler_environment_arguments_are_accepted(self):
        self.compiler_cache((("C", "fakecc", " -m64"), ("CXX", "fakecxx", " -m64 -pipe")))
        _, calls = self.run_wrapper(CC="fakecc -m64", CXX="fakecxx -m64 -pipe")
        self.assertEqual(len(calls), 2)

    def test_changed_or_removed_compiler_arguments_are_rejected(self):
        self.compiler_cache((("C", "fakecc", " -m64"),))
        for requested in ("fakecc -m32", "fakecc", "fakecc -m64 -pipe"):
            with self.subTest(requested=requested):
                result, calls = self.run_wrapper(success=False, CC=requested)
                self.assertIn("compiler arguments differ", result.stderr)
                self.assertIn("use --build-dir", result.stderr)
                self.assertEqual(calls, [])

    def test_compiler_paths_with_spaces_and_quoted_names_are_supported(self):
        compiler = self.bin / "compiler with spaces"
        for quotation, arguments in (("", ""), ('"', " -m64"), ("'", " -m64")):
            with self.subTest(quotation=quotation, arguments=arguments):
                self.compiler_cache((("C", compiler.name, arguments),))
                self.log.unlink(missing_ok=True)
                _, calls = self.run_wrapper(CC=f"{quotation}{compiler}{quotation}{arguments}")
                self.assertEqual(len(calls), 2)

    def test_compiler_argument_text_is_never_evaluated(self):
        marker = self.root / "must not be created"
        arguments = f' -DVALUE="$(touch \'{marker}\')" -DOTHER=`false`'
        self.compiler_cache((("C", "fakecc", arguments),))
        _, calls = self.run_wrapper(CC="fakecc" + arguments)
        self.assertEqual(len(calls), 2)
        self.assertFalse(marker.exists())

    def test_cmake_compiler_path_override_preserves_spaces_and_existing_arguments(self):
        compiler = self.bin / "compiler with spaces"
        self.compiler_cache((("C", compiler.name, " -m64"),))
        _, calls = self.run_wrapper("--", f"-DCMAKE_C_COMPILER={compiler}")
        self.assertIn(f"-DCMAKE_C_COMPILER={compiler}", calls[0])

    def test_unsupported_compiler_quoting_fails_before_first_configuration(self):
        for requested in ('"fakecc', '"fake"cc -m64', "fake\\cc -m64"):
            with self.subTest(requested=requested):
                result, calls = self.run_wrapper(success=False, CC=requested)
                self.assertIn("compiler-name quot", result.stderr)
                self.assertEqual(calls, [])

    def test_build_failure_does_not_run_stale_tests(self):
        _, calls = self.run_wrapper("test", "contract", success=False, FAIL_STEP="build")
        self.assertEqual(len(calls), 2)
        self.assertNotIn("ctest", [call[0] for call in calls])

    def test_configuration_failure_stops_before_build(self):
        result, calls = self.run_wrapper(success=False, FAIL_STEP="configure")
        self.assertEqual(len(calls), 1)
        self.assertIn("configuration failed", result.stderr)

    def test_package_verifies_without_inventing_abi_floor(self):
        _, calls = self.run_wrapper("package")
        self.assertIn("release", calls[0])
        self.assertIn("-DDATAPUMP_PORTABLE=ON", calls[0])
        self.assertIn("package", calls[1])
        self.assertTrue(calls[2][-1].endswith("/tools/verify-native-archives.cmake"))
        self.assertFalse(any(arg.startswith("-DMAX_GLIBC=") for arg in calls[2]))

    def test_package_accepts_explicit_abi_ceiling(self):
        _, calls = self.run_wrapper("package", DATAPUMP_MAX_GLIBC="2.35")
        self.assertIn("-DMAX_GLIBC=2.35", calls[2])

    def test_packaging_tests_use_a_separate_static_openssl_tree(self):
        _, calls = self.run_wrapper("test", "packaging")
        self.assertIn(str(self.source / "build/package-tests"), calls[0])
        self.assertIn("-DOPENSSL_USE_STATIC_LIBS=ON", calls[0])
        self.assertIn("datapump-tests-packaging", calls[1])

    def test_gui_group_excludes_native_display(self):
        _, calls = self.run_wrapper("test", "gui")
        self.assertIn("^gui$", calls[2])
        self.assertEqual(calls[2][-2:], ["-LE", "native_gui"])

    def test_invalid_commands_do_not_invoke_build_tools(self):
        for args in (("test",), ("test", "unknown"), ("--jobs", "0"),
                     ("--jobs", "two"), ("--backend", "unknown"),
                     ("--jobs", "00"),
                     ("test", "native", "--cli"), ("--", "-B", "wrong")):
            with self.subTest(args=args):
                _, calls = self.run_wrapper(*args, success=False)
                self.assertEqual(calls, [])


if __name__ == "__main__":
    unittest.main()
