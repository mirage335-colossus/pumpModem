#!/usr/bin/env python3
"""Compile tiny ELF fixtures to exercise SDK build-tree runtime isolation."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOLS = {
    "cmake": shutil.which("cmake"),
    "cc": os.environ.get("DATAPUMP_TEST_C_COMPILER") or shutil.which("cc"),
    "c++": os.environ.get("DATAPUMP_TEST_CXX_COMPILER") or shutil.which("c++"),
    "objdump": os.environ.get("DATAPUMP_TEST_OBJDUMP") or shutil.which("objdump"),
}


@unittest.skipUnless(sys.platform.startswith("linux") and all(TOOLS.values()),
                     "SDK ELF fixtures need Linux, CMake, C/C++ compilers and objdump")
class SdkBuildTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="datapump sdk runtime ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.build = self.root / "build with spaces"
        self.sdk = self.root / "sdk"
        self.sysroot = self.sdk / "sysroot"
        self.libs = self.sysroot / "usr/lib"
        self.libs.mkdir(parents=True)
        self.host = self.root / "host libraries"
        self.host.mkdir()
        self.env = os.environ.copy()
        for name in ("LD_LIBRARY_PATH", "LD_PRELOAD", "CC", "CXX",
                     "CFLAGS", "CXXFLAGS", "LDFLAGS", "CMAKE_GENERATOR"):
            self.env.pop(name, None)
        self.compiler_sysroot = os.environ.get("DATAPUMP_TEST_SYSROOT")
        if not self.compiler_sysroot:
            # A Buildroot compiler wrapper has its own real libc sysroot. Keep
            # using it for compilation while the tiny fixture sysroot controls
            # only runtime dependency selection. Native GCC normally prints an
            # empty value; its equivalent root is /.
            self.compiler_sysroot = self.command(
                TOOLS["c++"], "-print-sysroot").stdout.strip() or "/"

    def command(self, *args, success=True, env=None):
        result = subprocess.run([str(arg) for arg in args], env=env or self.env,
                                text=True, capture_output=True, check=False)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def prepare_chain(self):
        leaf = self.source / "leaf.c"
        leaf.write_text("int fixture_leaf(void) { return 73; }\n")
        host_leaf = self.source / "host-leaf.c"
        host_leaf.write_text("int fixture_leaf(void) { return 19; }\n")
        middle = self.source / "middle.c"
        middle.write_text("extern int fixture_leaf(void);\n"
                          "int fixture_middle(void) { return fixture_leaf(); }\n")
        for source, directory in ((leaf, self.libs), (host_leaf, self.host)):
            self.command(TOOLS["cc"], "-shared", "-fPIC", source,
                         "-Wl,-soname,libdatapump_sdk_test_leaf.so.1",
                         "-o", directory / "libdatapump_sdk_test_leaf.so.1.0")
            (directory / "libdatapump_sdk_test_leaf.so.1").symlink_to(
                "libdatapump_sdk_test_leaf.so.1.0")
        self.command(TOOLS["cc"], "-shared", "-fPIC", middle,
                     self.libs / "libdatapump_sdk_test_leaf.so.1.0",
                     "-Wl,-soname,libdatapump_sdk_test_middle.so.1",
                     f"-Wl,-rpath,{self.host}",
                     "-o", self.libs / "libdatapump_sdk_test_middle.so.1.0")
        (self.libs / "libdatapump_sdk_test_middle.so.1").symlink_to(
            "libdatapump_sdk_test_middle.so.1.0")
        (self.source / "main.cpp").write_text(
            '#include <string>\nextern "C" int fixture_middle(void);\n'
            'int main(int argc, char **argv) {\n'
            '  std::string argument(argv[0]);\n'
            '  return !argument.empty() && argc == 1 && fixture_middle() == 73 ? 0 : 1;\n'
            '}\n')

    def configure(self, *, sdk=True, chain=True, sanitizers=False, success=True):
        # The fixture is deliberately not a full libc SDK. These compile/link
        # overrides use the selected toolchain while dependency collection still
        # sees only the fake target sysroot, after project() has probed compilers.
        policy = (
            f'set(DATAPUMP_SDK_ROOT [==[{self.sdk}]==])\n'
            f'set(CMAKE_SYSROOT [==[{self.sysroot}]==])\n'
            f'set(CMAKE_SYSROOT_COMPILE [==[{self.compiler_sysroot}]==])\n'
            f'set(CMAKE_SYSROOT_LINK [==[{self.compiler_sysroot}]==])\n'
        ) if sdk else ''
        if not chain:
            (self.source / "main.cpp").write_text('int main() { return 0; }\n')
        links = (
            f'target_link_libraries(fixture PRIVATE '
            f'[==[{self.libs / "libdatapump_sdk_test_middle.so.1.0"}]==])\n'
            f'target_link_options(fixture PRIVATE [==[-Wl,-rpath-link,{self.libs}]==])\n'
        ) if chain else ''
        (self.source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.21)\n'
            'project(SdkBuildFixture LANGUAGES C CXX)\n'
            + policy
            + f'set(DATAPUMP_SANITIZERS {"ON" if sanitizers else "OFF"})\n'
            'set(OPENSSL_USE_STATIC_LIBS OFF)\n'
            f'include([==[{ROOT / "cmake/SdkBuild.cmake"}]==])\n'
            'add_executable(fixture main.cpp)\n'
            + links
            + 'file(WRITE "${CMAKE_BINARY_DIR}/openssl-policy.txt" "${OPENSSL_USE_STATIC_LIBS}")\n')
        return self.command(TOOLS["cmake"], "-S", self.source, "-B", self.build,
                            f'-DCMAKE_C_COMPILER={TOOLS["cc"]}',
                            f'-DCMAKE_CXX_COMPILER={TOOLS["c++"]}',
                            f'-DCMAKE_OBJDUMP={TOOLS["objdump"]}', success=success)

    def build_fixture(self, *, success=True):
        return self.command(TOOLS["cmake"], "--build", self.build,
                            "--parallel", "2", success=success)

    def test_sdk_stages_transitive_libraries_and_runs_after_sysroot_removal(self):
        self.prepare_chain()
        self.configure()
        self.build_fixture()
        self.assertEqual((self.build / "openssl-policy.txt").read_text(), "ON")
        runtime = self.build / "sdk-runtime"
        self.assertEqual((runtime / "libdatapump_sdk_test_leaf.so.1").read_bytes(),
                         (self.libs / "libdatapump_sdk_test_leaf.so.1.0").read_bytes())
        self.assertTrue((runtime / "libdatapump_sdk_test_middle.so.1").is_file())
        self.assertFalse((runtime / "libc.so.6").exists())
        headers = self.command(TOOLS["objdump"], "-p", self.build / "fixture").stdout
        self.assertRegex(headers, r"\bRPATH\s+[^\n]*\$ORIGIN/sdk-runtime")
        self.assertNotRegex(headers, r"\bRUNPATH\s")
        for runtime_path in re.findall(r"^\s*(?:RPATH|RUNPATH)\s+(.+)$", headers, re.M):
            self.assertNotIn(str(self.sysroot), runtime_path)
            self.assertNotIn(str(self.sdk), runtime_path)
        self.assertNotRegex(headers, r"\bNEEDED\s+lib(?:stdc\+\+|gcc_s)\.")
        self.sysroot.rename(self.root / "hidden sysroot")
        # DT_RPATH must work for the indirect dependency, even with an available
        # same-SONAME host library and an inherited LD_LIBRARY_PATH.
        relocated = self.root / "relocated executable"
        relocated.mkdir()
        shutil.copy2(self.build / "fixture", relocated)
        shutil.copytree(runtime, relocated / "sdk-runtime")
        self.command(relocated / "fixture", env=dict(self.env, LD_LIBRARY_PATH=str(self.host)))

    def test_sdk_missing_transitive_library_is_not_taken_from_host(self):
        self.prepare_chain()
        self.configure()
        # Keep the original library available to the linker through rpath-link;
        # hide it from sysroot collection only after the executable was linked.
        self.build_fixture()
        (self.libs / "libdatapump_sdk_test_leaf.so.1").unlink()
        (self.libs / "libdatapump_sdk_test_leaf.so.1.0").unlink()
        result = self.command(TOOLS["cmake"], f"-DSDK_SYSROOT={self.sysroot}",
                              f'-DOBJDUMP={TOOLS["objdump"]}',
                              f"-DEXECUTABLE={self.build / 'fixture'}",
                              f"-DDESTINATION={self.build / 'sdk-runtime'}", "-P",
                              ROOT / "tools/stage-sdk-runtime.cmake", success=False,
                              env=dict(self.env, LD_LIBRARY_PATH=str(self.host)))
        self.assertIn("Missing SDK runtime dependency libdatapump_sdk_test_leaf.so.1",
                      result.stdout + result.stderr)

    def test_sdk_runtime_rejects_target_library_symlink_to_host(self):
        self.prepare_chain()
        self.configure()
        self.build_fixture()
        leaf = self.libs / "libdatapump_sdk_test_leaf.so.1"
        leaf.unlink()
        leaf.symlink_to(self.host / "libdatapump_sdk_test_leaf.so.1.0")
        result = self.command(TOOLS["cmake"], f"-DSDK_SYSROOT={self.sysroot}",
                              f'-DOBJDUMP={TOOLS["objdump"]}',
                              f"-DEXECUTABLE={self.build / 'fixture'}",
                              f"-DDESTINATION={self.build / 'sdk-runtime'}", "-P",
                              ROOT / "tools/stage-sdk-runtime.cmake", success=False)
        self.assertIn("SDK path escapes its root", result.stdout + result.stderr)

    def test_project_audio_stub_precedes_same_soname_sdk_library(self):
        sdk_alsa = self.source / "sdk-alsa.c"
        sdk_alsa.write_text("int fixture_audio(void) { return 19; }\n")
        self.command(TOOLS["cc"], "-shared", "-fPIC", sdk_alsa,
                     "-Wl,-soname,libasound.so.2", "-o", self.libs / "libasound.so.2")
        self.configure(chain=False)
        self.build_fixture()
        # Other executables legitimately stage real ALSA in the shared runtime
        # directory. The audio contract must still select its own linked stub.
        self.assertTrue((self.build / "sdk-runtime/libasound.so.2").is_file())
        (self.source / "stub.c").write_text("int fixture_audio(void) { return 73; }\n")
        (self.source / "bridge.c").write_text(
            "extern int fixture_audio(void);\nint fixture_bridge(void) { return fixture_audio(); }\n")
        (self.source / "main.cpp").write_text(
            'extern "C" int fixture_bridge(void);\nint main() { return fixture_bridge() != 73; }\n')
        with (self.source / "CMakeLists.txt").open("a") as cmake:
            cmake.write('add_library(audio_stub SHARED stub.c)\n'
                        'set_target_properties(audio_stub PROPERTIES OUTPUT_NAME asound SOVERSION 2\n'
                        '  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/audio-test-lib")\n'
                        'add_library(audio_bridge STATIC bridge.c)\n'
                        'target_link_libraries(audio_bridge PRIVATE audio_stub)\n'
                        'target_link_libraries(fixture PRIVATE audio_bridge)\n')
        self.command(TOOLS["cmake"], "-S", self.source, "-B", self.build)
        self.build_fixture()
        self.command(self.build / "fixture")
        headers = self.command(TOOLS["objdump"], "-p", self.build / "fixture").stdout
        rpath = re.search(r"\bRPATH\s+([^\n]+)", headers).group(1)
        self.assertLess(rpath.index("audio-test-lib"), rpath.index("$ORIGIN/sdk-runtime"))
        self.assertNotIn(str(self.sysroot), rpath)

    def test_sdk_rejects_unavailable_target_sanitizer_runtime(self):
        result = self.configure(chain=False, sanitizers=True, success=False)
        self.assertRegex(result.stdout + result.stderr, re.compile(r"sanitiz|libasan", re.I))

    def test_native_build_keeps_its_runtime_and_openssl_policy(self):
        self.configure(sdk=False, chain=False)
        self.build_fixture()
        self.command(self.build / "fixture")
        self.assertEqual((self.build / "openssl-policy.txt").read_text(), "OFF")
        self.assertFalse((self.build / "sdk-runtime").exists())


if __name__ == "__main__":
    unittest.main()
