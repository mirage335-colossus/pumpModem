#!/usr/bin/env python3
"""Keep the isolated Triad performance gate source-bound and bounded."""

from pathlib import Path
import fnmatch
import json
import os
import subprocess
import sys
import tempfile
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


def step_section(text, name):
    match = re.search(
        rf"(?ms)^      - name: {re.escape(name)}\n(.*?)(?=^      - name:|\Z)", text)
    if not match:
        raise AssertionError(f"workflow step {name!r} is missing")
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

    def test_manual_and_reusable_workflow_uses_fast_guard_without_qualification(self):
        self.assertIn("workflow_call:", self.performance)
        self.assertIn("workflow_dispatch:", self.performance)
        self.assertIn("CANDIDATE_REF: ${{ inputs.candidate_ref || github.sha }}", self.performance)
        self.assertIn("242ea15fa032ee117b9113a7f3cd07cc6e9f0254", self.performance)
        self.assertEqual(self.performance.count("default: 300"), 2)
        self.assertIn('0 < budget <= 1800', self.performance)
        self.assertIn('--budget-seconds "$PERFORMANCE_BUDGET_SECONDS"', self.performance)
        self.assertIn("--stopping-rule decision", self.performance)
        self.assertIn("--regression-profile normal", self.performance)
        self.assertIn("Requested regression profile:", self.performance)
        self.assertIn("timeout-minutes: ${{ inputs.qualify && 145 || 50 }}", self.performance)
        self.assertIn("five-second hardware arithmetic probes", self.performance)
        self.assertIn("--threads 2", self.performance)
        self.assertIn("python tools/triad_perf_guard.py", self.performance)
        self.assertIn("python tools/triad_perf_complete.py", self.performance)
        self.assertIn("--functional-defaults", self.performance)
        self.assertIn("--family-route paired", self.performance)
        self.assertIn("--family-mode direct", self.performance)
        self.assertEqual(self.performance.count("--native-direct-case native-vlf-low-cn0"), 2)
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

    def test_normal_guard_is_reject_only_and_does_not_run_qualification_work(self):
        guard = step_section(self.performance, "Guard isolated receiver performance against confirmed large slowdowns")
        self.assertIn("if: ${{ inputs.qualify != true }}", guard)
        self.assertIn("set -euo pipefail", guard)
        self.assertIn("--profile ci --suite-alpha 0.01 --workflow-job-count 1", guard)
        self.assertIn("--device cpu --precision fp32 --threads 2 --native-threads 1", guard)
        self.assertIn('--budget-seconds "$PERFORMANCE_BUDGET_SECONDS"', guard)
        self.assertIn("--functional-defaults", guard)
        self.assertIn("--hardware-seconds 5", guard)
        self.assertIn("--output build/triad-performance/guard 2>&1 | tee", guard)
        for forbidden in ("triad_perf_complete", "triad_perf_bootstrap", "--anchor-directory",
                          "--lifecycle", "--family-mode", "--native-direct-case", "--skip-hardware-probes"):
            self.assertNotIn(forbidden, guard)
        for restore in ("Restore receiver performance anchors", "Restore qualified lifecycle campaign anchors"):
            self.assertIn("if: ${{ inputs.qualify == true }}", step_section(self.performance, restore))
        complete = step_section(self.performance, "Compare complete isolated workload scope with the fixed baseline")
        self.assertIn("inputs.qualify == true && !cancelled()", complete)
        self.assertIn("--regression-profile normal", complete)
        self.assertIn('--budget-seconds "$QUALIFICATION_BUDGET_SECONDS"', complete)
        self.assertIn("set -euo pipefail", complete)
        self.assertNotIn("continue-on-error", self.performance)
        self.assertEqual(len(re.findall(r"(?m)^  [A-Za-z0-9_-]+:\n    name:", self.performance)), 1)
        self.assertNotIn("matrix:", self.performance)

    def test_one_harness_discovery_includes_guard_checks_before_any_timings(self):
        name = "Check native performance trace and harness contracts"
        block = step_section(self.performance, name)
        pattern = re.search(r"python -m unittest discover -s tests -p '([^']+)'", block).group(1)
        guard_tests = list((ROOT / "tests").glob("test_triad_perf_guard*.py"))
        self.assertTrue(guard_tests, "guard policy and adapter tests must exist")
        self.assertTrue(all(fnmatch.fnmatch(test.name, pattern) for test in guard_tests))
        self.assertEqual(self.performance.count("python -m unittest discover"), 1)
        check_position = self.performance.index("name: " + name)
        self.assertLess(self.performance.index("name: Install pinned CPU numerical stack"), check_position)
        self.assertLess(check_position, self.performance.index("name: Guard isolated receiver performance"))
        self.assertLess(check_position, self.performance.index("name: Explicit one-time qualification producer"))

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
        save_anchor = self.performance.index("name: Save independently validated native and FFT anchors")
        restore_fixtures = self.performance.index("name: Restore deterministic sampled waveform fixtures")
        save_fixtures = self.performance.index("name: Save validated sampled fixtures")
        self.assertLess(restore_anchor, run_suite)
        self.assertLess(restore_fixtures, run_suite)
        # Qualification artifacts have their own proof gate and survive a
        # subsequent unresolved combined scope; normal CI cannot produce them.
        self.assertLess(save_anchor, run_suite)
        self.assertGreater(save_fixtures, run_suite)
        self.assertEqual(self.performance.count(f"actions/cache/restore@{cache_pin}"), 5)
        self.assertEqual(self.performance.count(f"actions/cache/save@{cache_pin}"), 5)
        trusted_save_guard = "github.ref == format('refs/heads/{0}', github.event.repository.default_branch) && (github.event_name == 'push' || github.event_name == 'workflow_dispatch')"
        self.assertEqual(self.performance.count(trusted_save_guard), 3)
        self.assertIn("Fingerprint runner profile for anchor cache", self.performance)
        self.assertIn("steps.runner-profile.outputs.key", self.performance)
        self.assertIn("steps.binary-key.outputs.candidate", self.performance)
        self.assertIn("steps.bootstrap-cache-ready.outputs.anchors_ready == 'true'", self.performance)
        self.assertIn("name: Validate exact bootstrap publication index", self.performance)
        self.assertIn("triad-performance/anchors", self.performance)
        self.assertIn("triad-performance-fixtures/triad-physical-fixtures", self.performance)
        self.assertEqual(self.performance.count("restore-keys:"), 4)
        self.assertIn("prepare_publication_cache", self.performance)
        self.assertNotIn("-name '*anchor.json'", self.performance)
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
        self.assertNotIn("Restore independently qualified sampled-family anchors", self.performance)
        self.assertIn("Restore source-bound instrumented coverage receipts", self.performance)
        self.assertIn("_valid_cache", (ROOT / "tools/triad_perf_cases.py").read_text())
        self.assertIn("anchor_valid", (ROOT / "tools/triad_perf_fft_trace.py").read_text())
        self.assertIn("historical_anchor_proof", (ROOT / "tools/triad_perf_native.py").read_text())

    def test_compatible_artifact_discovery_keeps_hardware_and_binary_boundaries(self):
        for name, reference_required in (("Restore source-bound instrumented coverage receipts", False),
                                         ("Restore qualified lifecycle campaign anchors", True)):
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
        self.assertIn("--family-mode direct", self.performance)
        self.assertNotIn("--mode qualify", self.performance)

    def test_qualification_is_explicit_trusted_and_independently_published(self):
        call = self.performance.split("  workflow_call:\n", 1)[1].split("  workflow_dispatch:\n", 1)[0]
        dispatch = self.performance.split("  workflow_dispatch:\n", 1)[1].split("\npermissions:", 1)[0]
        self.assertNotIn("qualify:", call)
        self.assertRegex(dispatch, r"(?s)qualify:.*?type: boolean.*?default: false")
        self.assertIn("default: 1800", dispatch)
        self.assertIn("bootstrap and complete each receive this duration", dispatch)
        self.assertIn("0 < qualification_budget <= 3600", self.performance)
        qualify = self.performance.index("name: Explicit one-time qualification producer")
        index = self.performance.index("name: Validate exact bootstrap publication index")
        routine = self.performance.index("name: Compare complete isolated workload scope")
        self.assertLess(qualify, index)
        self.assertLess(index, routine)
        self.assertIn("--budget-seconds \"$QUALIFICATION_BUDGET_SECONDS\"", self.performance)
        for name, category, restore in (("native and FFT anchors", "anchors", "Restore receiver performance anchors"),
                                       ("coverage receipts", "coverage", "Restore source-bound instrumented coverage receipts"),
                                       ("lifecycle anchors", "lifecycle", "Restore qualified lifecycle campaign anchors")):
            block = re.search(rf"(?ms)^      - name: Save independently validated {name}\n(.*?)(?=^      - name:|\Z)", self.performance).group(1)
            self.assertIn("always() && !cancelled()", block)
            self.assertIn("github.event_name == 'workflow_dispatch' && inputs.qualify", block)
            self.assertIn("github.ref == format('refs/heads/{0}', github.event.repository.default_branch)", block)
            self.assertIn(f"steps.bootstrap-cache-ready.outputs.{category}_ready == 'true'", block)
            restored = re.search(rf"(?ms)^      - name: {restore}\n(.*?)(?=^      - name:|\Z)", self.performance).group(1)
            self.assertEqual(re.search(r"(?m)^          path: (.+)$", block).group(1),
                             re.search(r"(?m)^          path: (.+)$", restored).group(1))
            self.assertNotIn("combined-routine", block)
        self.assertIn("from triad_perf_bootstrap import prepare_publication_cache", self.performance)
        self.assertIn("steps.qualification.outcome == 'failure'", self.performance)
        self.assertIn("Qualification artifacts:", self.performance)
        self.assertNotIn("continue-on-error: true", self.performance)

    def test_extensions_use_provenanced_test_driver_and_restore_only_proofs(self):
        baseline = self.performance.index("name: Build baseline native simulation and receiver")
        overlay = self.performance.index("name: Prepare identical planner test driver against immutable reference production")
        planner_build = self.performance.index("name: Build only the new baseline planner test target")
        verify = self.performance.index("name: Verify reference receiver binary was preserved")
        compare = self.performance.index("name: Compare complete isolated workload scope")
        self.assertLess(baseline, overlay)
        self.assertLess(overlay, planner_build)
        self.assertLess(planner_build, verify)
        self.assertLess(verify, compare)
        self.assertIn("prepare_reference_driver(Path('candidate'), Path('reference'))", self.performance)
        self.assertIn("receipt['production_unchanged']", self.performance)
        self.assertIn("reference_native_sha256", self.performance)
        self.assertIn("--target triad_planner_perf --parallel 2", self.performance)
        self.assertIn("--planner-binary candidate/build/dev-cli/triad_planner_perf", self.performance)
        self.assertIn("--planner-reference-binary reference/build/dev-cli/triad_planner_perf", self.performance)
        self.assertIn("--lifecycle-mixed-method direct", self.performance)
        self.assertIn("--lifecycle-direct-cycles auto", self.performance)
        self.assertNotIn("--qualify-lifecycle-model", self.performance)
        self.assertLess(self.performance.index("name: Restore qualified lifecycle campaign anchors"), compare)
        self.assertIn("--resource-direct", self.performance)
        self.assertNotIn("--resource-anchor", self.performance)
        self.assertNotIn("triad-resource-v1", self.performance)
        fixture = "docs/validation-data/triad-v3/2026-11-03-performance-complete/controls/actual-delay-001/sampled-stats"
        self.assertIn("--planner-bounds-workspace " + fixture, self.performance)
        self.assertTrue((ROOT/fixture/"manifest.json").is_file())
        self.assertNotIn("triad-bounds-workspace-v1", self.performance)
        self.assertIn("Extension {name}:", self.performance)

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
        self.assertIn("guard/summary.json", self.performance)
        capture_block = step_section(self.performance, "Capture first complete hardware reference")
        self.assertIn("os.environ['QUALIFICATION_REQUESTED'] == 'true'", capture_block)
        self.assertIn(".get('hardware_profile')", capture_block)
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

    def test_summary_distinguishes_guard_assurance_from_full_qualification(self):
        self.assertIn("build/triad-performance/complete/summary.json", self.performance)
        self.assertIn("build/triad-performance/guard/summary.json", self.performance)
        self.assertIn("complete_scope_qualification", self.performance)
        self.assertIn("warm_target_decisions_complete", self.performance)
        self.assertIn("maintained_workload_scope_complete", self.performance)
        self.assertIn("release_review_requirements", self.performance)
        self.assertIn("value.get('outcomes', {})", self.performance)
        self.assertIn("value.get('scope_blockers', [])", self.performance)
        self.assertIn("Unresolved combined scope blocks explicit qualification", self.performance)
        for field in ("suite_alpha", "workflow_job_count", "hypothesis_count", "limited_assurance",
                      "assurance_limitations", "functional_failures", "confirmed_regressions"):
            self.assertIn(f"value.get('{field}'", self.performance)
        self.assertIn("A green CI guard means no confirmed regression", self.performance)
        self.assertIn("does not cover deterministic test or infrastructure failures", self.performance)
        self.assertIn("conditional on independent confirmation pairs", self.performance)

    def render_summary(self, payload, qualification=False):
        body = re.search(r"(?ms)^ {10}python - <<'PYSUMMARY'\n(.*?)^ {10}PYSUMMARY\s*$", self.performance).group(1)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            report = root / "report.json"
            report.write_text(json.dumps(payload))
            result = subprocess.run([sys.executable, "-c", textwrap.dedent(body)], cwd=root,
                                    env={**os.environ, "REPORT": str(report), "GITHUB_STEP_SUMMARY": str(root / "summary.md"),
                                         "CANDIDATE_REF": "1" * 40, "QUALIFICATION_REQUESTED": str(qualification).lower()},
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((root / "summary.md").read_text().strip().replace("\n\n", "\n"), result.stdout.strip())
            return result.stdout

    def test_advisory_guard_summary_does_not_claim_qualification_or_block_scope(self):
        rendered = self.render_summary({"status": "no_confirmed_regression", "performance_status": "no_confirmed_regression",
                                        "limited_assurance": True, "qualification_status": "not_evaluated_by_fast_guard",
                                        "suite_alpha": 0.01, "workflow_job_count": 1, "hypothesis_count": 23,
                                        "assurance_limitations": ["Missing source-bound coverage"], "wall_seconds": 103.4,
                                        "guard_decision": {"blocking_estimand": "median of paired full-target candidate/reference runtime ratios",
                                                           "blocking_test": "exact one-sided binomial sign test",
                                                           "statistical_assumptions": ["independent confirmation signs"]}})
        self.assertIn("Limited assurance: True", rendered)
        self.assertIn("false-block budget: 0.01; jobs: 1; frozen hypotheses: 23", rendered)
        self.assertIn("Assurance limitation (advisory): Missing source-bound coverage", rendered)
        self.assertIn("does not establish complete qualification", rendered)
        self.assertIn("Blocking estimand: median of paired full-target candidate/reference runtime ratios", rendered)
        self.assertIn("exact one-sided binomial sign test", rendered)
        self.assertIn("Statistical assumption: independent confirmation signs", rendered)
        self.assertIn("conditional on independent confirmation pairs", rendered)
        self.assertNotIn("Blocking scope:", rendered)
        self.assertNotIn("Unresolved combined scope blocks", rendered)

    def test_guard_failures_and_explicit_qualification_failures_remain_visible(self):
        rendered = self.render_summary({"status": "blocked", "confirmed_regressions": ["native-vlf slowdown"],
                                        "functional_failures": ["invalid source identity"]})
        self.assertIn("Confirmed regression: native-vlf slowdown", rendered)
        self.assertIn("Functional/integrity failure: invalid source identity", rendered)
        rendered = self.render_summary({"status": "incomplete", "complete_scope_qualification": False,
                                        "scope_blockers": ["historical model missing"],
                                        "outcomes": {"coverage": {"status": "incomplete"}},
                                        "extension_outcomes": {"resource": {"status": "failed", "gap_ids": []}}}, qualification=True)
        self.assertIn("Blocking scope: historical model missing", rendered)
        self.assertIn("coverage: incomplete", rendered)
        self.assertIn("Extension resource: failed", rendered)
        self.assertIn("Unresolved combined scope blocks explicit qualification", rendered)
        self.assertNotIn("A green CI guard", rendered)

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
