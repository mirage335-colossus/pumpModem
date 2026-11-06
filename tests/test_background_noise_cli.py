"""Fast native PCM boundary checks; no NumPy, device access or receiver campaign."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1)).resolve()


class PcmNoise(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def invoke(self, raw, *, fmt='s16', name='out', flags=(), success=True):
        source=self.root/(name+'.input'); source.write_bytes(raw)
        output=self.root/name; meta=self.root/(name+'.json')
        command=[str(BINARY),'--input',str(source),'--output',str(output),
                 '--metadata',str(meta),'--format',fmt,*flags]
        r=subprocess.run(command,capture_output=True,text=True,timeout=10)
        self.assertEqual(r.returncode==0,success,r.stderr)
        return output,meta,command

    def test_off_preserves_all_s16_codes(self):
        raw=struct.pack('<65536h',*range(-32768,32768))
        output,meta,_=self.invoke(raw)
        self.assertEqual(output.read_bytes(),raw)
        self.assertEqual(json.loads(meta.read_text())['measured_noise_variance'],0)

    def test_fresh_noise_and_sub_lsb_survival(self):
        first,meta,_=self.invoke(bytes(16384),flags=['--bits','16'])
        second,_,_=self.invoke(bytes(16384),name='second',flags=['--bits','16'])
        self.assertNotEqual(first.read_bytes(),bytes(16384))
        self.assertNotEqual(first.read_bytes(),second.read_bytes())
        self.assertEqual(json.loads(meta.read_text())['clipped_samples'],0)
        self.assertGreaterEqual(json.loads(meta.read_text())['noise_peak_bound'],1/32767)

    def test_headroom_is_checked_before_output(self):
        output,meta,_=self.invoke(struct.pack('<h',32767),flags=['--bits','10'],success=False)
        self.assertFalse(output.exists());self.assertFalse(meta.exists())

    def test_no_secret_seed_interface(self):
        for i,flag in enumerate(('--seed','--key','--iv')):
            self.invoke(bytes(16),name=str(i),flags=[flag,'1'],success=False)
        output,_,_=self.invoke(bytes(16),name='unresolvable',flags=['--db','250'],success=False)
        self.assertFalse(output.exists())

    def test_json_control_characters_and_exclusive_output(self):
        output,meta,command=self.invoke(bytes(16),name='line\nwith\ttab')
        self.assertEqual(json.loads(meta.read_text())['output'],str(output))
        repeat=subprocess.run(command,capture_output=True,text=True,timeout=10)
        self.assertNotEqual(repeat.returncode,0)
        self.assertEqual(output.read_bytes(),bytes(16))

    def test_stereo_and_negative_reference_rejected(self):
        self.invoke(struct.pack('<hh',1,2),flags=['--channels','2','--bits','10'],success=False)
        self.invoke(bytes(16),name='ref',flags=['--noise-watts','.001','--reference-watts','40',
                    '--reference-rms','-.1'],success=False)

    def test_float_receipt_measures_final_output_not_invisible_perturbation(self):
        _,meta,_=self.invoke(struct.pack('<f',.5)*64,fmt='f32',flags=['--db','250'])
        receipt=json.loads(meta.read_text())
        self.assertGreater(receipt['expected_noise_variance'],0)
        self.assertEqual(receipt['measured_noise_variance'],0)


if __name__=='__main__':unittest.main()
