"""Explicit study allocations must not inherit the automatic live CPU default."""
import copy
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import triad_hf_noise_measure as noise_measure
import triad_weak_qualify as qualify


class StopAtReceiver(RuntimeError):
    pass


class ReceiverCpuLimits(unittest.TestCase):
    def test_frozen_qualification_threads_reach_fft_without_rewriting_protocol(self):
        for extra, expected in (({}, 3), ({'fft_workers': 5}, 5)):
            with self.subTest(extra=extra), tempfile.TemporaryDirectory() as directory:
                protocol = dict(protocol_id='test', source_hashes={},
                    receiver=dict(threads_per_process=3, coherences=['.5', 'inf'], **extra),
                    base=dict(public_manifest={}, directory='public'), regimes=[dict(name='test')],
                    exporter=dict(path='unused-native'))
                before = copy.deepcopy(protocol)
                row = dict(id='trial', regime='test', phase='signal')
                manifest = dict(observation=dict(file='iq'), validity=dict(file='valid'))
                with mock.patch.dict(os.environ), \
                     mock.patch.object(qualify, 'verify'), \
                     mock.patch.object(qualify, 'native_arguments', return_value=[]), \
                     mock.patch.object(qualify, 'receiver_manifest', return_value={}), \
                     mock.patch.object(qualify, 'digest', return_value='test-sha'), \
                     mock.patch.object(qualify.subprocess, 'run', return_value=mock.Mock(returncode=0)), \
                     mock.patch('triad_dynamic_joint.load_public',
                                return_value=(manifest, None, [], np.zeros(1), np.ones(1))), \
                     mock.patch('torch.set_num_threads'), mock.patch('torch.set_num_interop_threads'), \
                     mock.patch('torch.set_float32_matmul_precision'), \
                     mock.patch('triad_weak_receiver.WeakReceiver', side_effect=StopAtReceiver) as receiver:
                    result = qualify.trial((protocol, row, directory))
                self.assertEqual(result['status'], 'error')  # Intentional stop before any score/generation.
                self.assertEqual(receiver.call_args.kwargs['fft_workers'], expected)
                self.assertNotIn('threads_per_process', receiver.call_args.kwargs)
                self.assertEqual(protocol, before)

    def test_noise_measure_explicit_limit_reaches_session_and_custom_receiver(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/'run'
            slot = ({}, None, [])
            with mock.patch('sys.argv', ['triad_hf_noise_measure.py', '--public', 'public',
                    '--capture', 'capture', '--output', str(output), '--threads', '3']), \
                 mock.patch.object(noise_measure, 'digest', return_value='test-sha'), \
                 mock.patch.object(noise_measure.subprocess, 'check_output', return_value='test-revision'), \
                 mock.patch.object(noise_measure.subprocess, 'run'), \
                 mock.patch.object(noise_measure, 'load_slots', return_value=({}, [slot])), \
                 mock.patch.object(noise_measure, 'noise_plan',
                                   return_value=dict(suppression_db=20., receiver_noise_inflation=5.)), \
                 mock.patch.object(noise_measure, 'prepare_pcm', return_value={}), \
                 mock.patch.object(noise_measure, 'StreamSession') as session, \
                 mock.patch.object(noise_measure, 'WeakReceiver', side_effect=StopAtReceiver) as receiver, \
                 mock.patch('torch.set_num_threads'), mock.patch('torch.set_num_interop_threads'), \
                 mock.patch('torch.set_float32_matmul_precision'):
                session.return_value.slots = [slot]
                with self.assertRaises(StopAtReceiver):
                    noise_measure.main()
            self.assertEqual(session.call_args.kwargs['workers'], 1)
            self.assertEqual(session.call_args.kwargs['fft_workers'], 3)
            self.assertEqual(receiver.call_args.kwargs['fft_workers'], 3)


if __name__ == '__main__':
    unittest.main()
