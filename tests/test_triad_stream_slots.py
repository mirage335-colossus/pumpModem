import json
from pathlib import Path
import sys,tempfile,unittest
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from triad_stream_slots import load_slots

class SlotTests(unittest.TestCase):
    def test_public_rebase_and_clock_envelope(self):
        with tempfile.TemporaryDirectory()as d:
            p=Path(d);np.zeros((2,13,2,4),complex).tofile(p/'templates.c128')
            slots=[dict(helper_begin_sample=x,support_begin_sample=x+38_000,
                pattern_begin_sample=x+38_100,helper_end_sample=x+62_000,
                key_epoch_seconds=100+x//1000,symbol_ordinal=i)for i,x in enumerate((20000,110000))]
            m=dict(schema='triad-helper-whole-bit-public-v1',sample_rate_hz=1000,public_epoch_seconds=100,
                templates=dict(enabled=True,file='templates.c128',shape=[2,13,2,4],epoch_offsets_seconds=list(range(-6,7))),
                whole_bit_calendar=dict(slots=slots),format={},calendar=dict(capture_samples=200000,arrival_prior_seconds=[-7,7]),
                channel=dict(sample_clock_pair_ppm=100,spread_margin=3),observation=dict(shape=[200000]),validity=dict(shape=[200000]))
            (p/'manifest.json').write_text(json.dumps(m));original,views=load_slots(p)
            self.assertIn('whole_bit_calendar',original)
            for i,(view,templates,epochs)in enumerate(views):
                self.assertNotIn('whole_bit_calendar',view)
                self.assertEqual(view['calendar']['data_sample_offset'],39000)
                self.assertEqual(view['calendar']['capture_samples'],91000)
                self.assertEqual(view['format']['symbol_ordinal'],i)
                self.assertEqual(templates.shape,(13,2,4))
                self.assertLess(view['calendar']['arrival_prior_seconds'][0],-7)
                self.assertGreater(view['calendar']['arrival_prior_seconds'][1],7)
                self.assertEqual(epochs,tuple(range(-6,7)))

if __name__=='__main__':unittest.main()
