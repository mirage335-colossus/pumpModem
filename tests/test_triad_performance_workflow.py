#!/usr/bin/env python3
"""Keep the isolated Triad performance gate source-bound and bounded."""

from pathlib import Path
import re
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

    def test_manual_and_reusable_workflow_uses_fixed_paired_run_contract(self):
        self.assertIn("workflow_call:", self.performance)
        self.assertIn("workflow_dispatch:", self.performance)
        self.assertIn("CANDIDATE_REF: ${{ inputs.candidate_ref || github.sha }}", self.performance)
        self.assertIn("242ea15fa032ee117b9113a7f3cd07cc6e9f0254", self.performance)
        self.assertIn("--budget-seconds 600", self.performance)
        self.assertIn("--stress-seconds 5", self.performance)
        self.assertIn("--threads 2", self.performance)
        self.assertIn("--baseline-ref \"$BASELINE_REF\"", self.performance)
        self.assertIn("--candidate-root candidate", self.performance)
        self.assertIn("candidate/build/dev-cli/triad_long", self.performance)
        self.assertNotIn("--DBUILD_TESTING", self.performance)
        self.assertIn("-DBUILD_TESTING=ON", self.performance)
        self.assertIn("timeout-minutes: 25", self.performance)
        self.assertIn("if: always()", self.performance)
        self.assertNotIn("continue-on-error: true", self.performance)
        self.assertIn("torch==2.8.0", self.performance)
        self.assertIn("numpy==2.3.5", self.performance)
        self.assertIn("scipy==1.16.3", self.performance)

    def test_ci_exercises_portable_dispatch_without_claiming_cpu_emulation(self):
        for control in ("ATEN_CPU_CAPABILITY: avx2", "OPENBLAS_CORETYPE: HASWELL",
                        "MKL_ENABLE_INSTRUCTIONS: AVX2", "ONEDNN_MAX_CPU_ISA: AVX2"):
            self.assertIn(control, self.performance)
        self.assertIn("NPY_DISABLE_CPU_FEATURES:", self.performance)
        self.assertIn("physically capable x86 AVX2/FMA CPU", self.performance)
        self.assertIn("opt_func_info()", self.performance)
        self.assertIn("dispatch.json", self.performance)
        self.assertIn("not a complete ISA quarantine", self.performance)

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
