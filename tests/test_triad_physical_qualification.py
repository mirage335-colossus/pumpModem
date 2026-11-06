"""Reject mismatched/unsafe playback before a physical admission is released."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_physical_qualification import playback_receipt


class PlaybackTests(unittest.TestCase):
    def test_receipt_and_rejections(self):
        good = dict(schema='triad-audio-stream-v1', mode='play', complete=True,
            requested_frames=6000, completed_frames=6000, sample_rate_hz=6000,
            clipped_samples=0, clock_discipline={'enabled':True},
            background_noise=dict(enabled=True, active_samples=48000, sample_rate_hz=48000,
                                  expected_variance=5e-5, peak=.013))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'play.log'
            def check(row, **kw):
                path.write_text('ALSA informational line\n'+json.dumps(row)+'\n')
                return playback_receipt(path, frames=6000, sample_rate=6000,
                    noise_db=40, signal_peak=kw.get('peak', .001), hardware_rates=[48000])
            self.assertEqual(check(good), good)
            for key, value in [('complete',False), ('completed_frames',5999),
                               ('clipped_samples',1), ('sample_rate_hz',48000)]:
                with self.subTest(key=key), self.assertRaises(ValueError):
                    check({**good,key:value})
            for key, value in [('enabled',False), ('active_samples',0),
                               ('expected_variance',5e-7), ('peak',float('nan')), ('sample_rate_hz',44100)]:
                wrong = copy.deepcopy(good); wrong['background_noise'][key] = value
                with self.subTest(key=key), self.assertRaises(ValueError): check(wrong)
            with self.assertRaises(ValueError): check(good, peak=.49)
            path.write_text(json.dumps(good)+'\n'+json.dumps(good)+'\n')
            with self.assertRaises(ValueError):
                playback_receipt(path, frames=6000, sample_rate=6000, noise_db=40, signal_peak=.001,
                    hardware_rates=[48000])
            with self.assertRaises(ValueError):
                playback_receipt(path, frames=6000, sample_rate=6000, noise_db=40, signal_peak=.001,
                    hardware_rates=None)


if __name__ == '__main__': unittest.main()
