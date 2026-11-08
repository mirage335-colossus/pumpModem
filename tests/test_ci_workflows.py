#!/usr/bin/env python3
"""Cheap checks for local workflow wiring and the bounded Robust CI boundary."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS = ROOT / '.github/workflows'


class Workflows(unittest.TestCase):
    def test_reusable_workflows_exist(self):
        for path in WORKFLOWS.glob('*.yml'):
            for name in re.findall(r'uses:\s*\./\.github/workflows/([^\s]+)', path.read_text()):
                with self.subTest(workflow=path.name, dependency=name):
                    self.assertTrue((WORKFLOWS / name).is_file())

    def test_retired_robust_jobs_cannot_be_dispatched(self):
        obsolete = ('triad_perf_guard', 'triad_perf_complete', 'triad_perf_bootstrap',
                    'test_differential_receiver_probability', 'test_scheduled_parallel_preparation',
                    'ci-preparation-plan.py', 'ci-test-plan.py', 'test_live_profiles',
                    'DATAPUMP_CALIBRATION_SECTION')
        for path in WORKFLOWS.glob('*.yml'):
            for token in obsolete:
                with self.subTest(workflow=path.name, token=token):
                    self.assertNotIn(token, path.read_text())

    def test_abbreviated_ci_uses_current_source_without_full_mode(self):
        source = (WORKFLOWS / 'triad-abbreviated.yml').read_text()
        self.assertIn('inputs.candidate_ref || github.sha', source)
        self.assertIn('tools/triad_abbreviated.py --output', source)
        self.assertIn('./build.sh test triad --cli', source)
        self.assertIn('test_triad_abbreviated.py', (ROOT / 'CMakeLists.txt').read_text())
        self.assertIn('timeout-minutes: 10', source)
        self.assertNotRegex(source, r'--(?:full|sustained)|reference_ref|--qualify')
        for caller in ('ci.yml', 'release.yml'):
            self.assertIn('./.github/workflows/triad-abbreviated.yml',
                          (WORKFLOWS / caller).read_text())


if __name__ == '__main__':
    unittest.main()
