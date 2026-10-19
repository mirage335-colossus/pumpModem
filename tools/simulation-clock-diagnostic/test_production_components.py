from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


TOOL = Path(__file__).with_name("production_components.py")


class ProductionComponentRouteTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name) / "checkout"
        self.root.mkdir()
        self.bin = Path(self.temp.name) / "bin"
        self.bin.mkdir()
        (self.root / "tests").mkdir()
        (self.root / "src").mkdir()
        (self.root / "include").mkdir()
        (self.root / "cmake").mkdir()
        (self.root / "build").mkdir()
        registrations = [
            ('native peak A', 'peak_a'),
            ('native point 3-point', 'point_a'),
            ('native point 3-point second', 'point_b'),
            ('diagnostic history retention', 'history'),
            ('qualified UTC FFT partial EOF threshold', 'eof'),
        ]
        source = "int main(int argc,char** argv) {\n" + "\n".join(
            f' run("{name}",{function});' for name, function in registrations) + "\n}\n"
        (self.root / "tests/test_pattern_receiver.cpp").write_text(source)
        (self.root / "tests/test_stream_receive.cpp").write_text("int main() {}\n")
        (self.root / "tests/test_live.cpp").write_text("int main() {}\n")
        (self.root / "src/pattern_receiver.cpp").write_text("// source\n")
        (self.root / "include/test.hpp").write_text("// header\n")
        (self.root / "cmake/test.cmake").write_text("# cmake\n")
        (self.root / "CMakeLists.txt").write_text("# registration fixture\n")
        self.compiler = self.bin / "g++-11"
        self.compiler.write_text("#!/bin/sh\nprintf 'g++ (fixture) 11.4.0\\n'\n")
        self.compiler.chmod(0o755)
        ctest = self.bin / "ctest"
        ctest.write_text("#!/usr/bin/env python3\nimport json\nprint(json.dumps({'tests':[{'name':'stream_receive','command':['/fixture/test_stream_receive']}]}))\n")
        ctest.chmod(0o755)
        cache = """CMAKE_BUILD_TYPE:STRING=Debug
CMAKE_CXX_COMPILER:FILEPATH={compiler}
CMAKE_CXX_FLAGS_DEBUG:STRING=-g
DATAPUMP_SANITIZERS:BOOL=ON
DATAPUMP_PORTABLE:BOOL=OFF
""".format(compiler=self.compiler)
        (self.root / "build/CMakeCache.txt").write_text(cache)
        pattern = self.root / "build/test_pattern_receiver"
        pattern.write_text("""#!/usr/bin/env python3
import sys
names = {
 'native peak': ['native peak A'],
 'native point': ['native point 3-point', 'native point 3-point second'],
 'diagnostic history retention': ['diagnostic history retention'],
 'qualified UTC FFT partial EOF threshold': ['qualified UTC FFT partial EOF threshold'],
}
for selector, tests in names.items():
 if selector == sys.argv[1]:
  for name in tests: print(name + ': passed')
""")
        pattern.chmod(0o755)
        stream = self.root / "build/test_stream_receive"
        stream.write_text("#!/bin/sh\necho 'stream receive integration passed'\n")
        stream.chmod(0o755)
        planning = self.root / "build/test_live_planning_link"
        planning.write_text("#!/bin/sh\necho 'provider-free Live allocation/planning link regression passed'\n")
        planning.chmod(0o755)
        live = self.root / "build/test_live"
        live.write_text("#!/bin/sh\nexit 0\n")
        live.chmod(0o755)
        subprocess.run(["git", "init", "-q"], cwd=self.root, check=True)
        subprocess.run(["git", "config", "user.email", "fixture@example.invalid"], cwd=self.root, check=True)
        subprocess.run(["git", "config", "user.name", "Fixture"], cwd=self.root, check=True)
        subprocess.run(["git", "add", "."], cwd=self.root, check=True)
        subprocess.run(["git", "commit", "-qm", "fixture"], cwd=self.root, check=True)
        self.head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=self.root, text=True).strip()

    def tearDown(self) -> None:
        self.temp.cleanup()

    def invoke(self, report_name: str) -> subprocess.CompletedProcess[str]:
        env = os.environ.copy()
        env["PATH"] = str(self.bin) + os.pathsep + env["PATH"]
        return subprocess.run([
            sys.executable, str(TOOL), "--root", str(self.root),
            "--report", str(self.root / "build" / report_name),
            "--expected-head", self.head,
        ], env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def test_success_records_exact_registration_coverage_and_source_binding(self) -> None:
        result = self.invoke("component-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads((self.root / "build/component-run/run.json").read_text())
        self.assertTrue(report["all_components_passed"])
        self.assertTrue(report["source_unchanged"])
        self.assertEqual(len(report["pattern_registration_inventory"]["native point"]), 2)
        self.assertEqual(len(report["components"]), 6)
        self.assertIn("child_cpu_seconds", report["components"][0])
        self.assertIn("finished_at_utc", report["components"][0])

    def test_last_selector_failure_does_not_start_stream_suite(self) -> None:
        pattern = self.root / "build/test_pattern_receiver"
        pattern.write_text(pattern.read_text().replace("for selector, tests in names.items():", "names['qualified UTC FFT partial EOF threshold'] = []\nfor selector, tests in names.items():"))
        pattern.chmod(0o755)
        result = self.invoke("last-selector-failed")
        self.assertEqual(result.returncode, 1)
        report = json.loads((self.root / "build/last-selector-failed/run.json").read_text())
        self.assertFalse(report["all_components_passed"])
        self.assertEqual(len(report["components"]), 4)
        self.assertFalse(report["components"][-1]["registration_coverage"])
        self.assertFalse((self.root / "build/last-selector-failed/stream-receive.stdout.log").exists())

    def test_empty_selector_inventory_fails_closed(self) -> None:
        source = self.root / "tests/test_pattern_receiver.cpp"
        text = source.read_text().replace('"native point', '"renamed point')
        self.assertNotIn('"native point', text)
        source.write_text(text)
        result = self.invoke("bad-inventory")
        self.assertEqual(result.returncode, 2)
        failure = json.loads((self.root / "build/bad-inventory/setup-failure.json").read_text())
        self.assertIn("no source registration matches", failure["error"])

    def test_incomplete_selector_output_fails_coverage_gate(self) -> None:
        pattern = self.root / "build/test_pattern_receiver"
        pattern.write_text(pattern.read_text().replace("for name in tests:", "for name in tests[:1]:"))
        pattern.chmod(0o755)
        result = self.invoke("missing-registration-output")
        self.assertEqual(result.returncode, 1)
        report = json.loads((self.root / "build/missing-registration-output/run.json").read_text())
        self.assertFalse(report["all_components_passed"])
        self.assertFalse(report["components"][1]["registration_coverage"])
        self.assertEqual([r["selector"] for r in report["components"]], ["native peak", "native point"])


if __name__ == "__main__":
    unittest.main()
