"""Local replay must bind acquisition independently of the replay worker budget."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))

from triad_replay_cpu import measure_local_fine_replay


class ReplayCpuTests(unittest.TestCase):
    def test_attestation_worker_mismatch_is_rejected_before_preparation(self):
        for declared in (1, True, '2', None):
            with self.subTest(declared=declared), patch('triad_replay_cpu.machine_resources') as machine:
                with self.assertRaisesRegex(ValueError, 'attested acquisition allocation'):
                    measure_local_fine_replay(executable='unused', case_list='unused', case_index=7,
                        packet='unused', attestation={'diagnostics': {'acquisition_workers': declared}},
                        acquisition_workers=2, workers=6, bank_reservation_bytes=1)
                machine.assert_not_called()


if __name__ == '__main__':
    unittest.main()
