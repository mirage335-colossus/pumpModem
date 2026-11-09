"""Bounded confirmation memory and fixed-point identity regressions."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from triad_manual_confirmation import _point_procedures, _slot_geometry, _successful_endpoint
from triad_manual_confirmation import _worker_memory_estimates


_MIB = 1024**2
_ALLOCATION = dict(per_job_native_stream_workspace_bytes=64*_MIB,
                   per_native_process_runtime_allowance_bytes=32*_MIB)


def worker_pair(pid, python_mib, native_mib, **monitor_changes):
    sample = dict(positive_samples=1, scan_failures=0, maximum_concurrent_children_observed=1,
                  sampled_peak_combined_rss_bytes=native_mib*_MIB)
    sample.update(monitor_changes)
    return dict(worker_pid=pid, python_peak_rss_bytes=python_mib*_MIB,
                outcomes=[dict(supported=True, native_rss=sample)])


class ConfirmationMemoryTests(unittest.TestCase):
    def test_postexec_sample_and_workspace_floor_ignore_reaped_fork_high_water(self):
        pair = worker_pair(11, 455, 10)
        # The original run's reaped-child peak matched its Python launcher.
        pair['native_child_peak_rss_bytes'] = pair['python_peak_rss_bytes']
        self.assertEqual(_worker_memory_estimates([pair], _ALLOCATION), {11:583*_MIB})

    def test_high_observed_rss_and_idle_workers_remain_in_estimate(self):
        earlier = worker_pair(11, 455, 150)
        repeated = worker_pair(11, 455, 10)
        latest = worker_pair(12, 460, 10)
        self.assertEqual(_worker_memory_estimates([earlier, repeated, latest], _ALLOCATION),
                         {11:637*_MIB, 12:588*_MIB})

    def test_incomplete_native_monitor_cannot_supply_headroom(self):
        for changes in (dict(positive_samples=0), dict(scan_failures=1),
                        dict(maximum_concurrent_children_observed=3)):
            with self.subTest(changes=changes):
                pair = worker_pair(11, 455, 10, **changes)
                self.assertIsNone(_worker_memory_estimates([pair], _ALLOCATION))


class ConfirmationProcedureTests(unittest.TestCase):
    def test_fingerprint_may_change_from_pilot_but_not_within_confirmation_point(self):
        slots = [dict(index=index, slot_classification=kind, decision_bit=0,
                      procedure_id='a'*64, coverage=dict(all_input_valid=True,
                                                        complete_observation=True))
                 for index, kind in enumerate(('DATA', 'QUIET', 'QUIET'))]
        pilot_geometry = [_slot_geometry(slot) for slot in slots]
        confirmation_slots = [dict(slot, procedure_id='b'*64) for slot in slots]
        outcome = dict(supported=True, success=True, source_bit=0, native_seed=985000,
                       cn0_db_hz=11.11, independent_native_seed=True, physical_complete=True,
                       complete_receiver_work_covered=True, received_bits=[0],
                       trailing_quiet_slots=[1, 2], erasure_slot_indices=[], slots=confirmation_slots)
        self.assertTrue(_successful_endpoint(outcome, 0, 11.11, 985000, pilot_geometry))
        receipt = [dict(geometry=_slot_geometry(slot), procedure_id=slot['procedure_id'])
                   for slot in confirmation_slots]
        pairs = [dict(outcomes=[dict(slots=receipt)])]
        self.assertEqual(_point_procedures(pairs), ['b'*64]*3)
        # A partial unsuccessful receive must still match on every reported slot.
        pairs.append(dict(outcomes=[dict(matched_success=False,
            slots=[dict(receipt[0], procedure_id='c'*64)])]))
        self.assertIsNone(_point_procedures(pairs))


if __name__ == '__main__':
    unittest.main()
