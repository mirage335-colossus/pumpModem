"""Focused gate for actual signal work identity."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))

from triad_case_capacity import validate_signal_acquisition_work


class ReplayWorkTests(unittest.TestCase):
    def test_signal_work_needs_actual_branch_and_path_counts(self):
        h = 'a'*64
        coordinates = [[0, .5, .001, 0.], [-1, -.5, .002, 0.]]
        candidates = [dict(bit=bit, epoch=0, arrival=.5, frequency=.001, clock_ppm=0., rank=1)
            for bit in (0, 1)]
        receipt = dict(schema='triad-complete-public-signal-work-v1', sample_count=36000,
            full_iq_sha256=h, full_validity_sha256=h, fine_slice_sha256=h,
            acquisition=dict(status='acquired', complete_public_search=True, work_only=False,
                receiver_workers=2, helper_status='fallback', helper_reason='helper_disabled',
                helper_result_sha256=h, coarse_reason='complete_public_workspace_search',
                coarse_candidates=candidates,
                coarse_work=dict(workspaces=78, workers=2, frequency_points=2,
                    arrival_points=8443, fft_products=312, correlation_method='frequency'),
                searched_frequency_intervals_hz=[[-.01, .01]], refinement_candidates=2,
                coordinates=coordinates))
        self.assertTrue(validate_signal_acquisition_work(receipt))
        for field in ('helper_result_sha256', 'coordinates', 'coarse_work'):
            invalid = copy.deepcopy(receipt)
            del invalid['acquisition'][field]
            self.assertFalse(validate_signal_acquisition_work(invalid))
        for changed in (dict(work_only=True), dict(refinement_candidates=1),
                        dict(complete_public_search=False), dict(receiver_workers=1)):
            invalid = copy.deepcopy(receipt)
            invalid['acquisition'].update(changed)
            self.assertFalse(validate_signal_acquisition_work(invalid))


if __name__ == '__main__':
    unittest.main()
