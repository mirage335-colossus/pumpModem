"""Physical topology reporting checks; receiver allocation remains native."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import triad_cpu


class TriadCpuTests(unittest.TestCase):
    def _write_topology(self, root, rows):
        for cpu, package, core in rows:
            topology = root / ('cpu' + str(cpu)) / 'topology'
            topology.mkdir(parents=True, exist_ok=True)
            (topology / 'physical_package_id').write_text(str(package))
            (topology / 'core_id').write_text(str(core))

    def test_topology_counts_distinct_package_core_pairs_within_affinity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            # CPUs 2 and 7 are SMT siblings; core 1 on package 1 is a distinct core.
            self._write_topology(root, [(2, 0, 1), (7, 0, 1), (9, 1, 1)])
            receipt = triad_cpu.detect_cpu_topology(
                logical_cpu_ids={2, 7, 9}, sysfs_root=root, linux=True)
        self.assertEqual(receipt['status'], 'complete')
        self.assertEqual(receipt['logical_cpu_count'], 3)
        self.assertEqual(receipt['physical_core_count'], 2)
        self.assertEqual(receipt['physical_cores'], [
            dict(package_id=0, core_id=1, logical_cpu_ids=[2, 7]),
            dict(package_id=1, core_id=1, logical_cpu_ids=[9])])

    def test_topology_uses_all_packages_and_does_not_guess_from_partial_sysfs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._write_topology(root, [(0, 0, 0), (1, 0, 1)])
            complete = triad_cpu.detect_cpu_topology(
                logical_cpu_ids={0, 1}, sysfs_root=root, linux=True)
            incomplete = triad_cpu.detect_cpu_topology(
                logical_cpu_ids={0, 1, 2}, sysfs_root=root, linux=True)
        self.assertEqual(complete['physical_core_count'], 2)
        self.assertEqual(incomplete['status'], 'partial')
        self.assertIsNone(incomplete['physical_core_count'])
        self.assertEqual(incomplete['incomplete_logical_cpus'][0]['logical_cpu'], 2)

    def test_unknown_platform_and_invalid_affinity_keep_physical_count_unknown(self):
        portable = triad_cpu.detect_cpu_topology(linux=False)
        self.assertEqual(portable['status'], 'unavailable')
        self.assertIsNone(portable['physical_core_count'])
        with tempfile.TemporaryDirectory() as temporary:
            invalid = triad_cpu.detect_cpu_topology(
                logical_cpu_ids={-1}, sysfs_root=Path(temporary), linux=True)
        self.assertEqual(invalid['status'], 'unavailable')
        self.assertIsNone(invalid['physical_core_count'])

    def test_unavailable_affinity_is_not_inferred_from_host_cpu_count(self):
        with patch.object(triad_cpu.os, 'sched_getaffinity', None, create=True):
            receipt = triad_cpu.detect_cpu_topology(linux=True)
        self.assertEqual(receipt['status'], 'unavailable')
        self.assertIsNone(receipt['physical_core_count'])
        with patch.object(triad_cpu.os, 'sched_getaffinity', side_effect=OSError('denied'), create=True):
            receipt = triad_cpu.detect_cpu_topology(linux=True)
        self.assertEqual(receipt['status'], 'unavailable')
        self.assertIn('denied', receipt['reason'])

    def test_empty_and_invalid_affinity_do_not_report_usable_cores(self):
        for ids in ([], [True], [1.5], ['2']):
            with self.subTest(ids=ids):
                receipt = triad_cpu.detect_cpu_topology(logical_cpu_ids=ids, linux=True)
                self.assertEqual(receipt['status'], 'unavailable')
                self.assertIsNone(receipt['physical_core_count'])


if __name__ == '__main__':
    unittest.main()
