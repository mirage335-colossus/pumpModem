import sys, unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).parents[1]/'tools'))
sys.path.insert(0,str(Path(__file__).parent))
import triad_replay_producer as p
from test_triad_replay_producer import fixture, Receiver, Grid

class SignalIntegration(unittest.TestCase):
    def test_signal_evidence_bound_to_slice_coordinates_and_workers(self):
        prepared,case=fixture(); receiver=Receiver()
        kwargs=dict(case=case,list_sha256='b'*64,exporter_sha256='c'*64,source_hash='d'*64)
        with patch.object(p,'public_screen_geometry',return_value=dict(grid=Grid(), output_fs=100)):
            _,_,acquisition=p.make_packet(prepared,receiver,**kwargs)
            work=dict(fine_slice_sha256=acquisition['input_sha256'],
                acquisition=dict(coordinates=acquisition['coordinates'],receiver_workers=2))
            with patch.object(p,'signal_acquisition_work',return_value=work), patch.object(p,'validate_signal_acquisition_work',return_value=True):
                blob,public,actual,signal=p.make_packet(prepared,receiver,**kwargs,capture_signal_work=True)
                self.assertEqual(actual,acquisition);self.assertEqual(signal,work)
                for key,value in (('fine_slice_sha256','0'*64),('acquisition',dict(coordinates=[[0,3.,0.,0.]],receiver_workers=2)),('acquisition',dict(coordinates=acquisition['coordinates'],receiver_workers=3))):
                    bad=dict(work,**{key:value})
                    with patch.object(p,'signal_acquisition_work',return_value=bad), self.assertRaises(ValueError):
                        p.make_packet(prepared,receiver,**kwargs,capture_signal_work=True)
            with self.assertRaises(ValueError): p.make_packet(prepared,receiver,**kwargs,capture_signal_work=1)
if __name__ == '__main__': unittest.main()
