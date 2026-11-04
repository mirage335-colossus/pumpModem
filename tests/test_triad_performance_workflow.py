#!/usr/bin/env python3
"""Keep the isolated Triad performance gate source-bound and bounded."""

from pathlib import Path
import re
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


def job_section(text, name):
    match = re.search(
        rf"(?ms)^  {re.escape(name)}:\n(.*?)(?=^  [A-Za-z0-9_-]+:\n|\Z)", text)
    if not match:
        raise AssertionError(f"workflow job {name!r} is missing")
    return match.group(1)


class TriadPerformanceWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ci = (ROOT / ".github/workflows/ci.yml").read_text()
        cls.release = (ROOT / ".github/workflows/release.yml").read_text()
        cls.apt_release = (ROOT / ".github/workflows/apt-release.yml").read_text()
        cls.performance = (ROOT / ".github/workflows/triad-performance.yml").read_text()

    def test_default_ci_runs_but_devfast_skips_performance(self):
        job = job_section(self.ci, "triad-performance")
        self.assertIn("if: ${{ !inputs.devfast }}", job)
        self.assertIn("uses: ./.github/workflows/triad-performance.yml", job)
        self.assertIn("candidate_ref: ${{ github.sha }}", job)
        self.assertIn("tests/test_triad_performance_workflow.py", self.ci)

    def test_manual_and_reusable_workflow_uses_combined_routine_without_qualification(self):
        self.assertIn("workflow_call:", self.performance)
        self.assertIn("workflow_dispatch:", self.performance)
        self.assertIn("CANDIDATE_REF: ${{ inputs.candidate_ref || github.sha }}", self.performance)
        self.assertIn("242ea15fa032ee117b9113a7f3cd07cc6e9f0254", self.performance)
        self.assertEqual(self.performance.count("default: 600"), 2)
        self.assertIn('0 < budget <= 1800', self.performance)
        self.assertIn('--budget-seconds "$PERFORMANCE_BUDGET_SECONDS"', self.performance)
        self.assertIn("timeout-minutes: 50", self.performance)
        self.assertIn("five-second hardware arithmetic probes", self.performance)
        self.assertIn("--threads 2", self.performance)
        self.assertIn("python tools/triad_perf_complete.py", self.performance)
        self.assertIn("--functional-defaults", self.performance)
        self.assertIn("--family-route staged", self.performance)
        self.assertIn("--family-mode reuse", self.performance)
        self.assertNotIn("--validation-mode auto", self.performance)
        self.assertNotIn("--mode qualify", self.performance)
        self.assertNotIn("--validation-mode qualify", self.performance)
        self.assertIn("--anchor-directory build/triad-performance/anchors", self.performance)
        self.assertIn("--baseline-ref \"$BASELINE_REF\"", self.performance)
        self.assertIn("--candidate-root candidate", self.performance)
        self.assertIn("--reference-root reference", self.performance)
        self.assertIn("candidate/build/dev-cli/triad_long", self.performance)
        self.assertIn("reference/build/dev-cli/triad_long", self.performance)
        self.assertIn("Checkout immutable receiver reference source", self.performance)
        self.assertIn("baseline=$(git -C reference rev-parse HEAD)", self.performance)
        self.assertIn("Build baseline native simulation and receiver", self.performance)
        self.assertIn("TRIAD_LONG_BINARY:", self.performance)
        self.assertIn("TRIAD_SOURCE_ROOT:", self.performance)
        self.assertIn("build/triad-performance/**/*.json", self.performance)
        self.assertNotIn("--DBUILD_TESTING", self.performance)
        self.assertIn("-DBUILD_TESTING=ON", self.performance)
        self.assertIn("if: always()", self.performance)
        self.assertNotIn("continue-on-error: true", self.performance)
        self.assertIn("torch==2.8.0", self.performance)
        self.assertIn("numpy==2.3.5", self.performance)
        self.assertIn("scipy==1.16.3", self.performance)
        self.assertIn("coverage==7.17.1", self.performance)
        for target in ("test_triad_statistics", "test_triad_v3_source", "test_triad_v3_channel", "test_triad_v3_receiver"):
            self.assertIn(target, self.performance)
        self.assertIn("--fixture-cache build/triad-performance/triad-performance-fixtures", self.performance)
        self.assertIn("build/triad-performance/**/*.jsonl", self.performance)

    def test_ci_exercises_portable_dispatch_without_claiming_cpu_emulation(self):
        for control in ("ATEN_CPU_CAPABILITY: avx2", "OPENBLAS_CORETYPE: HASWELL",
                        "MKL_ENABLE_INSTRUCTIONS: AVX2", "ONEDNN_MAX_CPU_ISA: AVX2"):
            self.assertIn(control, self.performance)
        self.assertIn("NPY_DISABLE_CPU_FEATURES:", self.performance)
        self.assertIn("physically capable x86 AVX2/FMA CPU", self.performance)

    def test_models_restore_before_use_and_ancillary_caches_have_independent_guards(self):
        cache_pin = "55cc8345863c7cc4c66a329aec7e433d2d1c52a9"
        restore_anchor = self.performance.index("name: Restore receiver performance anchors")
        run_suite = self.performance.index("name: Compare complete isolated workload scope")
        save_anchor = self.performance.index("name: Save runner-bound anchors")
        restore_fixtures = self.performance.index("name: Restore deterministic sampled waveform fixtures")
        save_fixtures = self.performance.index("name: Save validated sampled fixtures")
        self.assertLess(restore_anchor, run_suite)
        self.assertLess(restore_fixtures, run_suite)
        self.assertGreater(save_anchor, run_suite)
        self.assertGreater(save_fixtures, run_suite)
        self.assertEqual(self.performance.count(f"actions/cache/restore@{cache_pin}"), 5)
        self.assertEqual(self.performance.count(f"actions/cache/save@{cache_pin}"), 3)
        trusted_save_guard = "github.ref == format('refs/heads/{0}', github.event.repository.default_branch) && (github.event_name == 'push' || github.event_name == 'workflow_dispatch')"
        self.assertEqual(self.performance.count(trusted_save_guard), 4)
        self.assertIn("Fingerprint runner profile for anchor cache", self.performance)
        self.assertIn("steps.runner-profile.outputs.key", self.performance)
        self.assertIn("steps.binary-key.outputs.candidate", self.performance)
        self.assertIn("steps.anchor-cache-ready.outputs.ready == 'true'", self.performance)
        self.assertIn("name: Check whether validated receiver anchors are available to cache", self.performance)
        self.assertIn("triad-performance/anchors", self.performance)
        self.assertIn("triad-performance-fixtures/triad-physical-fixtures", self.performance)
        self.assertEqual(self.performance.count("restore-keys:"), 4)
        self.assertIn("**/*anchor.json", self.performance)
        self.assertIn("-name '*anchor.json'", self.performance)
        self.assertIn("${{ github.run_id }}-${{ github.run_attempt }}", self.performance)
        anchor_key_lines = [line for line in self.performance.splitlines()
                            if "key: triad-anchors-v2-" in line]
        self.assertEqual(len(anchor_key_lines), 2)
        self.assertTrue(all("steps.binary-key.outputs.candidate" in line and
                            "steps.binary-key.outputs.reference" in line and
                            "tools/triad_perf_" not in line for line in anchor_key_lines))
        # Cache data is intentionally separate from diagnostic logs/reports;
        # validators still decide whether cached evidence applies.
        self.assertNotIn("report.json\n          key:", self.performance)
        self.assertIn("steps.fixture-cache-ready.outputs.ready == 'true'", self.performance)
        self.assertIn("fixtures._valid_cache(directory, key, source_hashes)", self.performance)
        self.assertIn("fixtures._source_hashes(candidate, selected['path'])", self.performance)
        self.assertIn("'binary_sha256': fixtures._sha256(binary)", self.performance)
        self.assertIn("Restore independently qualified sampled-family anchors", self.performance)
        self.assertIn("Restore source-bound instrumented coverage receipts", self.performance)
        self.assertIn("_valid_cache", (ROOT / "tools/triad_perf_cases.py").read_text())
        self.assertIn("anchor_valid", (ROOT / "tools/triad_perf_fft_trace.py").read_text())
        self.assertIn("historical_anchor_proof", (ROOT / "tools/triad_perf_native.py").read_text())

    def test_compatible_artifact_discovery_keeps_hardware_and_binary_boundaries(self):
        for name, reference_required in (("Restore independently qualified sampled-family anchors", True),
                                         ("Restore source-bound instrumented coverage receipts", False)):
            block = re.search(rf"(?ms)^      - name: {re.escape(name)}\n(.*?)(?=^      - name:|\Z)", self.performance).group(1)
            key = re.search(r"(?m)^          key: (.+)$", block).group(1)
            prefixes = re.search(r"(?ms)^          restore-keys: \|\n((?:            [^\n]+\n)+)", block).group(1).splitlines()
            self.assertEqual(len(prefixes), 1)
            prefix = prefixes[0].strip()
            self.assertEqual(prefix, key.split("${{ hashFiles(", 1)[0])
            for required in ("runner.os", "runner.arch", "steps.runner-profile.outputs.key", "steps.binary-key.outputs.candidate"):
                self.assertIn(required, prefix)
            self.assertEqual("steps.binary-key.outputs.reference" in prefix, reference_required)
            self.assertNotIn("hashFiles", prefix)
            self.assertNotIn("actions/cache/save", block)
        self.assertIn("--family-mode reuse", self.performance)
        self.assertNotIn("--mode qualify", self.performance)

    def test_first_hardware_profile_is_restored_and_never_automatically_replaced(self):
        restore = self.performance.index("name: Restore first-observed hardware performance profile")
        compare = self.performance.index("name: Compare complete isolated workload scope")
        capture = self.performance.index("name: Capture first complete hardware reference")
        save = self.performance.index("name: Save immutable first-observed hardware reference")
        self.assertLess(restore, compare)
        self.assertLess(compare, capture)
        self.assertLess(capture, save)
        self.assertIn("steps.runner-profile.outputs.hardware_key", self.performance)
        self.assertIn("cpu_model", self.performance)
        self.assertIn("affinity_topology", self.performance)
        self.assertIn("quota", self.performance)
        self.assertIn("dispatch_environment", self.performance)
        self.assertIn("--hardware-baseline build/triad-performance/hardware-reference.json", self.performance)
        self.assertIn("complete/native-fft.json", self.performance)
        self.assertIn("triad-first-hardware-reference-v1", self.performance)
        self.assertIn("destination.open('x')", self.performance)
        self.assertEqual(self.performance.count("steps.hardware-baseline.outputs.cache-hit != 'true'"), 2)
        self.assertIn("id: hardware-baseline", self.performance)
        self.assertIn("key: triad-hardware-reference-v1-${{ runner.os }}-${{ runner.arch }}-${{ steps.runner-profile.outputs.hardware_key }}", self.performance)
        self.assertIn("opt_func_info()", self.performance)
        self.assertIn("dispatch.json", self.performance)
        self.assertIn("not a complete ISA quarantine", self.performance)
        self.assertIn("steps.captured-hardware-reference.outputs.ready == 'true'", self.performance)
        self.assertIn("'receiver_qualification_claim': False", self.performance)
        self.assertIn("profile.get('status') != 'complete'", self.performance)

    def test_combined_summary_keeps_scope_blockers_and_all_outcomes(self):
        self.assertIn("REPORT: build/triad-performance/complete/summary.json", self.performance)
        self.assertIn("complete_scope_qualification", self.performance)
        self.assertIn("warm_target_decisions_complete", self.performance)
        self.assertIn("value.get('outcomes', {})", self.performance)
        self.assertIn("value.get('scope_blockers', [])", self.performance)
        self.assertIn("Unresolved combined scope blocks this CI/release gate", self.performance)

    def test_embedded_python_blocks_compile_without_running_workloads(self):
        blocks = re.findall(r"(?ms)^ {10}python(?:3)? - <<'([A-Z]+)'\n(.*?)^ {10}\1\s*$", self.performance)
        self.assertGreaterEqual(len(blocks), 6)
        for marker, body in blocks:
            compile(textwrap.dedent(body), f"triad-performance.yml:{marker}", "exec")

    def test_direct_release_builds_wait_for_the_exact_source_performance_gate(self):
        metadata = job_section(self.release, "metadata")
        gate = job_section(self.release, "triad-performance")
        self.assertIn("source_sha: ${{ steps.meta.outputs.source_sha }}", metadata)
        self.assertIn("if: github.event_name == 'workflow_dispatch' && !inputs.source_release && inputs.package_check == 'none'", gate)
        self.assertIn("needs: metadata", gate)
        self.assertIn("candidate_ref: ${{ needs.metadata.outputs.source_sha }}", gate)
        for producer in ("linux", "windows", "wasm"):
            self.assertIn("needs: [metadata, triad-performance]", job_section(self.release, producer))
        self.assertIn("needs: [metadata, triad-performance, linux, windows, wasm]",
                      job_section(self.release, "publish"))
        self.assertIn("tests/test_triad_performance_workflow.py", self.release)

    def test_separate_harness_and_candidate_checkout_reject_mutable_source_refs(self):
        self.assertIn("Checkout performance harness", self.performance)
        self.assertIn("Checkout immutable candidate application source", self.performance)
        self.assertIn("fetch-depth: 0", self.performance)
        self.assertIn("candidate_ref must be a complete immutable Git object ID", self.performance)
        self.assertIn("git -C candidate rev-parse HEAD", self.performance)
        self.assertIn("--candidate-root candidate", self.performance)

    def test_repackage_summary_disclaims_performance_and_reports_verified_source_identity(self):
        package = job_section(self.apt_release, "package")
        self.assertIn("source_sha: ${{ steps.package.outputs.source_sha }}", package)
        self.assertIn("value: ${{ jobs.package.outputs.source_sha }}", self.apt_release)
        self.assertIn("SOURCE_SHA: ${{ steps.package.outputs.source_sha }}", self.apt_release)
        self.assertIn("The verified application archives are reused byte-for-byte", self.apt_release)
        self.assertIn("Triad receiver performance was not evaluated in this repackage run", self.apt_release)
        self.assertIn("Application source commit recorded in the verified source release", self.apt_release)


if __name__ == "__main__":
    unittest.main()
