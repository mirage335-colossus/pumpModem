import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from triad_stream_frontend import StreamingAudioDDC
from triad_stream_run import StreamSession,public_audio_windows,replay_pcm,prepare_pcm


class StreamRunTests(unittest.TestCase):
    def fixtures(self):
        original=dict(sample_rate_hz=6000,channel=dict(nominal_conversion_if_hz=1725))
        slots=[]
        for i in range(2):
            m=dict(calendar=dict(capture_lead_seconds=8.,capture_samples=600,
                arrival_prior_seconds=[-7.035,7.035]),stream_window=dict(absolute_start_sample=200+800*i))
            slots.append((m,np.zeros((1,2,8),np.complex128),(0,)))
        return original,slots

    def test_public_delay_alignment_preserves_unknown_arrival_prior(self):
        _,slots=self.fixtures();before=copy.deepcopy(slots[0][0])
        f=StreamingAudioDDC();adjusted,requests=public_audio_windows(slots,f,103)
        shift,remainder=divmod(103+f.group_delay_samples,8)
        self.assertEqual(requests[0].start_sample,200+shift)
        self.assertEqual(adjusted[0][0]['calendar']['capture_lead_seconds'],8+remainder/48000)
        self.assertEqual(adjusted[0][0]['calendar']['arrival_prior_seconds'],[-7.035,7.035])
        self.assertEqual(slots[0][0],before)
        self.assertAlmostEqual(requests[0].start_sample/6000-(103+f.group_delay_samples)/48000,
            200/6000-remainder/48000)

    def test_chunked_session_matches_continuous_frontend_and_writes_scores(self):
        original,slots=self.fixtures();seen=[]
        class Receiver:
            def score(self,raw,valid):
                seen.append(raw.copy())
                return dict(status='scored',bit=1,score=100.,margin=90.,timing={},
                    operational=dict(passes_conditional_bound=True))
        pcm=np.repeat((np.sin(np.arange(16000)*.217)*1000).astype('<i2')[:,None],2,axis=1)
        with tempfile.TemporaryDirectory()as tmp:
            session=StreamSession(original,slots,tmp,fixed_gain=.002,analog_gain=.84,
                playback_offset_frames=0,receiver_factory=lambda *args:Receiver())
            f=StreamingAudioDDC(undo_fixed_gain=.002*.84)
            expected,mask,_=f.process(pcm[:,0].astype(np.float32)/32768.)
            for start in range(0,len(pcm),137):session.feed_stereo_s16(pcm[start:start+137].tobytes())
            result=session.finish(1)
            self.assertTrue(result['schedule']['drained']);self.assertFalse(result['message_completed'])
            self.assertFalse(result['physical_null_qualified'])
            self.assertEqual(len(seen),2)
            for i,request in enumerate(session.scheduler.requests):
                np.testing.assert_allclose(seen[i],expected[request.start_sample:request.end_sample],atol=3e-5)
                saved=json.loads((Path(tmp)/f'slot-{i}-receiver.json').read_text())
                self.assertEqual(saved['bit'],1)
            self.assertEqual(result['capture']['frames'],16000)
            self.assertEqual([r['status']for r in result['schedule']['records']],['scored','scored'])

    def test_replay_stereo_frame_boundary_and_pre_post_roll(self):
        class Fake:
            def __init__(self):self.blocks=[]
            def feed_stereo_s16(self,x):self.blocks.append(x)
        with tempfile.TemporaryDirectory()as tmp:
            path=Path(tmp)/'pcm';source=np.arange(26,dtype='<i2').tobytes();path.write_bytes(source)
            session=Fake();r=replay_pcm(session,path,preroll_frames=3,postroll_frames=5,chunk_frames=4)
            self.assertEqual(b''.join(session.blocks),bytes(12)+source+bytes(20))
            self.assertEqual(r['frames'],21)
        original,slots=self.fixtures()
        with tempfile.TemporaryDirectory()as tmp:
            session=StreamSession(original,slots,tmp,receiver_factory=lambda *args:None)
            try:
                with self.assertRaisesRegex(ValueError,'partial'):session.feed_stereo_s16(b'x')
            finally:r=session.finish(1)
            self.assertTrue(all(x['status']=='unresolved_incomplete'for x in r['schedule']['records']))

    def test_observation_override_does_not_change_public_source(self):
        with tempfile.TemporaryDirectory()as tmp:
            root=Path(tmp);n=300
            source=np.zeros(n,dtype='<c16');source.tofile(root/'original.iq')
            override=(.01*np.exp(2j*np.pi*np.arange(n)/37)).astype('<c16');override.tofile(root/'override.iq')
            np.ones(n,np.uint8).tofile(root/'valid')
            original=dict(sample_rate_hz=6000,channel=dict(nominal_conversion_if_hz=1725),
                observation=dict(file='original.iq',shape=[n]),validity=dict(file='valid'))
            before=copy.deepcopy(original)
            receipt=prepare_pcm(root,original,root/'tx.pcm',fixed_gain=.002,observation_file=root/'override.iq')
            self.assertEqual(original,before)
            self.assertEqual(receipt['observation_file'],str(root/'override.iq'))
            self.assertEqual((root/'tx.pcm').stat().st_size,n*8*4)


if __name__=='__main__':unittest.main()
