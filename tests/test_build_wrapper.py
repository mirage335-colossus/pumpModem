#!/usr/bin/env python3
"""Exercise build orchestration without compiling or downloading dependencies."""
import json
import os
from pathlib import Path
import hashlib
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
                     "CMAKE_BUILD_PARALLEL_LEVEL", "DATAPUMP_MAX_GLIBC", "CMAKE_TOOLCHAIN_FILE",
                     "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "OBJC_INCLUDE_PATH",
                     "LIBRARY_PATH", "GCC_EXEC_PREFIX", "COMPILER_PATH"):
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

    def prepare_sdk(self):
        sdk = self.root / "SDK with spaces"
        metadata = sdk / "share/datapump-sdk"
        metadata.mkdir(parents=True)
        (metadata / "manifest.json").write_text('{}')
        (metadata / "relocated-root.txt").write_text(str(sdk) + '\n')
        return sdk

    def test_sdk_uses_separate_tree_and_its_available_host_tools(self):
        sdk = self.prepare_sdk()
        (sdk / 'bin').mkdir()
        for name in ('cmake', 'ctest', 'ninja'):
            tool = sdk / 'bin' / name
            tool.write_text((self.bin / name).read_text().replace(
                'pathlib.Path(sys.argv[0]).name', '"sdk/" + pathlib.Path(sys.argv[0]).name'))
            tool.chmod(0o755)
        _, calls = self.run_wrapper('test', 'build', '--sdk', sdk.name)
        self.assertEqual([call[0] for call in calls], ['sdk/cmake', 'sdk/cmake', 'sdk/ctest'])
        self.assertIn(str(self.source / 'build/dev-sdk'), calls[0])
        self.assertIn(f'-DDATAPUMP_SDK_ROOT={sdk}', calls[0])
        self.assertIn(f'-DCMAKE_TOOLCHAIN_FILE={self.source}/cmake/toolchains/source-sdk.cmake', calls[0])

    def test_sdk_refuses_unrelocated_directory_before_invoking_tools(self):
        sdk = self.prepare_sdk()
        (sdk / 'share/datapump-sdk/relocated-root.txt').write_text('/previous/location\n')
        result, calls = self.run_wrapper('--sdk', str(sdk), success=False)
        self.assertIn('installation/relocation', result.stderr)
        self.assertEqual(calls, [])

    def test_sdk_refuses_competing_compilers_and_search_path_environment(self):
        sdk = self.prepare_sdk()
        for variable in ('CC', 'CXX', 'CMAKE_TOOLCHAIN_FILE', 'CPATH', 'LIBRARY_PATH'):
            with self.subTest(variable=variable):
                _, calls = self.run_wrapper('--sdk', str(sdk), success=False,
                                            **{variable: '/host/override'})
                self.assertEqual(calls, [])

    def test_sdk_refuses_competing_cmake_toolchain_arguments(self):
        sdk = self.prepare_sdk()
        for option in ('-DCMAKE_C_COMPILER:FILEPATH=/host/cc',
                       '-DCMAKE_CXX_COMPILER=/host/cxx', '-DCMAKE_SYSROOT=/host',
                       '-DDATAPUMP_DEPENDENCY_PREFIX=/host',
                       '-DCMAKE_TOOLCHAIN_FILE=/host/toolchain', '--toolchain=/host/toolchain'):
            with self.subTest(option=option):
                result, calls = self.run_wrapper('--sdk', str(sdk), '--', option, success=False)
                self.assertIn('competing CMake option', result.stderr)
                self.assertEqual(calls, [])
        result, calls = self.run_wrapper('--sdk', str(sdk), '--', '-D',
                                         'DATAPUMP_SDK_ROOT=/other/sdk', success=False)
        self.assertIn('competing CMake option', result.stderr)
        self.assertEqual(calls, [])

    def test_sdk_and_native_cache_cannot_be_mixed(self):
        sdk = self.prepare_sdk()
        output = self.root / 'existing build'
        output.mkdir()
        cache = output / 'CMakeCache.txt'
        for requested, cached in ((sdk, ''), (sdk, '/other/sdk'), ('', str(sdk))):
            with self.subTest(requested=requested, cached=cached):
                cache.write_text(f'DATAPUMP_CONFIGURED_SDK_ROOT:INTERNAL={cached}\n')
                args = ['--build-dir', str(output)]
                if requested:
                    args.extend(['--sdk', str(requested)])
                result, calls = self.run_wrapper(*args, success=False)
                self.assertIn('SDK differs', result.stderr)
                self.assertEqual(calls, [])

    def test_sdk_package_enforces_manifest_glibc_ceiling(self):
        sdk = self.prepare_sdk()
        output = self.source / 'build/release-sdk'
        output.mkdir(parents=True)
        (output / 'CMakeCache.txt').write_text(
            f'DATAPUMP_CONFIGURED_SDK_ROOT:INTERNAL={sdk}\nDATAPUMP_SDK_GLIBC_MAX:INTERNAL=2.36\n')
        _, calls = self.run_wrapper('package', '--sdk', str(sdk))
        self.assertIn('-DMAX_GLIBC=2.36', calls[2])

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
                     ("--jobs", "00"), ("--sdk", ""),
                     ("test", "native", "--cli"), ("--", "-B", "wrong")):
            with self.subTest(args=args):
                _, calls = self.run_wrapper(*args, success=False)
                self.assertEqual(calls, [])


@unittest.skipUnless(shutil.which('cmake'), 'CMake is required for SDK discovery checks')
class SourceSdkToolchainTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='datapump toolchain ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sdk = self.root / 'sdk'
        self.metadata = self.sdk / 'share/datapump-sdk'
        self.metadata.mkdir(parents=True)
        self.sysroot = self.sdk / 'target/sysroot'
        (self.sysroot / 'usr/lib').mkdir(parents=True)
        (self.sysroot / 'usr/include').mkdir(parents=True)
        (self.sdk / 'bin').mkdir()
        for name in ('target-gcc', 'target-g++'):
            (self.sdk / 'bin' / name).write_text('fixture')
        self.manifest = {'schema_version': 1, 'id': 'test-sdk', 'baseline': {'glibc': '2.36'},
                         'target': {'triple': 'target', 'processor': 'x86_64',
                                    'sysroot': 'target/sysroot', 'c_compiler': 'bin/target-gcc',
                                    'cxx_compiler': 'bin/target-g++'}}
        (self.metadata / 'manifest.json').write_text(json.dumps(self.manifest))
        (self.metadata / 'relocated-root.txt').write_text(str(self.sdk) + '\n')
        self.toolchain = Path(__file__).resolve().parents[1] / 'cmake/toolchains/source-sdk.cmake'

    def run_cmake(self, script='', *options, success=True, **environment):
        check = self.root / 'check.cmake'
        check.write_text('cmake_minimum_required(VERSION 3.21)\n'
                         f'include([==[{self.toolchain}]==])\n' + script)
        result = subprocess.run(['cmake', f'-DDATAPUMP_SDK_ROOT={self.sdk}', *options,
                                 '-P', str(check)], capture_output=True, text=True,
                                env=dict(os.environ, **environment))
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        return result

    def test_target_discovery_excludes_host_and_pkgconfig_environment_is_isolated(self):
        host = self.root / 'host'
        (host / 'lib').mkdir(parents=True)
        (host / 'include').mkdir()
        (host / 'lib/libonlyhost.a').write_text('host library')
        (host / 'include/onlyhost.h').write_text('host header')
        (self.sysroot / 'usr/lib/libtarget.a').write_text('target library')
        (self.sysroot / 'usr/include/target.h').write_text('target header')
        self.run_cmake(f'''
find_library(target NAMES target PATHS /usr/lib NO_DEFAULT_PATH)
find_path(header NAMES target.h PATHS /usr/include NO_DEFAULT_PATH)
find_library(host_library NAMES onlyhost PATHS [==[{host}/lib]==] NO_DEFAULT_PATH)
find_path(host_header NAMES onlyhost.h PATHS [==[{host}/include]==] NO_DEFAULT_PATH)
find_program(host_python NAMES python3 PATHS [==[{Path(sys.executable).parent}]==] NO_DEFAULT_PATH)
if(NOT target STREQUAL "${{CMAKE_SYSROOT}}/usr/lib/libtarget.a" OR NOT header STREQUAL "${{CMAKE_SYSROOT}}/usr/include")
  message(FATAL_ERROR "Target discovery failed")
endif()
if(host_library OR host_header OR NOT host_python)
  message(FATAL_ERROR "Host library leaked or host executable unavailable")
endif()
if(NOT "$ENV{{PKG_CONFIG_PATH}}" STREQUAL "" OR NOT "$ENV{{PKG_CONFIG_SYSROOT_DIR}}" STREQUAL "${{CMAKE_SYSROOT}}")
  message(FATAL_ERROR "pkg-config environment leaked")
endif()
if(NOT "$ENV{{PKG_CONFIG_LIBDIR}}" STREQUAL "${{CMAKE_SYSROOT}}/usr/lib/pkgconfig:${{CMAKE_SYSROOT}}/usr/share/pkgconfig:${{CMAKE_SYSROOT}}/lib/pkgconfig")
  message(FATAL_ERROR "pkg-config metadata escaped SDK")
endif()
''', PKG_CONFIG_PATH='/host/pkgconfig', PKG_CONFIG_LIBDIR='/host/pkgconfig')

    def test_sdk_identity_changes_and_native_prefix_are_rejected(self):
        for option, diagnostic in (
            ('-DDATAPUMP_CONFIGURED_SDK_ROOT=/other/sdk', 'SDK root changed'),
            ('-DDATAPUMP_CONFIGURED_SDK_MANIFEST=old-hash', 'SDK manifest changed'),
            ('-DDATAPUMP_DEPENDENCY_PREFIX=/host', 'cannot be combined'),
            ('-DCMAKE_C_COMPILER=/host/gcc', 'owns the C compiler'),
        ):
            with self.subTest(option=option):
                result = self.run_cmake('', option, success=False)
                self.assertIn(diagnostic, result.stderr)

    def test_manifest_cannot_escape_sdk_with_relative_paths_or_symlinks(self):
        host = self.root / 'host-compiler'
        host.write_text('compiler')
        for relative in ('../host-compiler', 'bin/escape'):
            with self.subTest(relative=relative):
                if relative == 'bin/escape':
                    (self.sdk / relative).symlink_to(host)
                self.manifest['target']['c_compiler'] = relative
                (self.metadata / 'manifest.json').write_text(json.dumps(self.manifest))
                result = self.run_cmake(success=False)
                self.assertRegex(result.stderr, 'Invalid SDK target|escapes its root')

    def test_unchanged_manifest_can_be_reused(self):
        fingerprint = hashlib.sha256((self.metadata / 'manifest.json').read_bytes()).hexdigest()
        self.run_cmake('', f'-DDATAPUMP_CONFIGURED_SDK_ROOT={self.sdk}',
                       f'-DDATAPUMP_CONFIGURED_SDK_MANIFEST={fingerprint}')

    def test_sdk_advertised_backend_support_is_enforced(self):
        self.manifest['supported_backends'] = ['fltk']
        (self.metadata / 'manifest.json').write_text(json.dumps(self.manifest))
        result = self.run_cmake('', '-DDATAPUMP_BUILD_GUI=ON', '-DDATAPUMP_GUI_BACKEND=rev', success=False)
        self.assertIn('does not support the rev GUI backend', result.stderr)
        self.run_cmake('', '-DDATAPUMP_BUILD_GUI=ON', '-DDATAPUMP_GUI_BACKEND=fltk')
        self.run_cmake('', '-DDATAPUMP_BUILD_GUI=OFF', '-DDATAPUMP_GUI_BACKEND=rev')


if __name__ == "__main__":
    unittest.main()
