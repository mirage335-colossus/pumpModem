"""Device-free framing and exact-origin guards for shared mapped capture."""
import io
import math
from pathlib import Path
import sys
import unittest
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_audio_mapping import HEADER, MAGIC, MappedCaptureReader, mapped_phase, rescale_coordinates

def frame(first, origin, offsets, rate=48000, **kw):
    offsets=np.asarray(offsets,dtype='<f8')
    return HEADER.pack(kw.get('magic',MAGIC),1,len(offsets),first,origin,rate,0)+offsets.tobytes()+np.arange(len(offsets),dtype='<f4').tobytes()

class MappingTests(unittest.TestCase):
    def test_packet_splitting_and_merging_preserve_coordinates(self):
        r=MappedCaptureReader(io.BytesIO(frame(0,10,[.2,8.2,16.2])+frame(3,34,[.2,8.2,16.2])))
        x,o,t,fs=r.read(2); self.assertEqual((o,fs),(10,48000));np.testing.assert_allclose(t,[.2,8.2])
        x,o,t,fs=r.read(4);self.assertEqual(o,26);np.testing.assert_allclose(t,[.2,8.2,16.2,24.2])
        self.assertEqual(r.next_output,6);self.assertEqual(r.packets,2)
    def test_malformed_maps_fail_before_use(self):
        for payload in (frame(1,0,[0]),frame(0,0,[0,0]),frame(0,0,[0,math.nan]),
                        frame(0,0,[1]),frame(0,0,[0],magic=b'BADMAGIC'),frame(0,0,[0])[:-1]):
            with self.subTest(payload=payload):
                with self.assertRaises((ValueError,EOFError)):MappedCaptureReader(io.BytesIO(payload)).read(1)
        r=MappedCaptureReader(io.BytesIO(frame(0,0,[0])+frame(1,0,[0])))
        r.read(1)
        with self.assertRaisesRegex(ValueError,'backward'):r.read(1)
    def test_large_coordinate_phase_and_conversion(self):
        origin=2**54+137;offsets=np.array([.125,.25,1.125])
        phase=mapped_phase(origin,offsets,1725.125,48000)
        num,den=(1725.125).as_integer_ratio()
        expected=2*np.pi*np.remainder((origin*num%(den*48000))/(den*48000)+offsets*1725.125/48000,1.)
        np.testing.assert_allclose(phase,expected,atol=1e-14,rtol=0)
        whole,relative=rescale_coordinates(origin,offsets,48000,6000)
        self.assertEqual(whole,origin//8);np.testing.assert_allclose(relative,origin%8/8+offsets/8)
    def test_rate_change_is_not_silently_rebound(self):
        r=MappedCaptureReader(io.BytesIO(frame(0,0,[0])+frame(1,8,[0],44100)))
        r.read(1)
        with self.assertRaisesRegex(ValueError,'rate changed'):r.read(1)

if __name__=='__main__':unittest.main()
