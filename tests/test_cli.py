"""CLI integration for the fixed interval format and physical end gate."""
import base64
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
import wave

PUMP = str(pathlib.Path(sys.argv.pop(1)).resolve())
AUDIO = ('--sample-rate','8000','--bw','1000','--carrier','1500','--spreading','16','--time','1800000000','--search-seconds','0')

class StreamCLI(unittest.TestCase):
    def run_pump(self,*args,data=None,ok=True):
        p=subprocess.run([PUMP,*map(str,args)],input=data,capture_output=True,timeout=90)
        self.assertEqual(p.returncode,0 if ok else 2,p.stderr.decode(errors='replace'))
        return p
    def test_removed_commands(self):
        helptext=self.run_pump('--help').stdout
        self.assertNotIn(b'pack/unpack',helptext)
        for command in ('pack','unpack'):
            self.run_pump(command,'--text','x',ok=False)
    def test_short_text_dictionary_threshold(self):
        for size in (1,15,16,17):
            for fec in ('off','20','60'):
                value=json.loads(self.run_pump('estimate','--text','e'*size,'--fec',fec,*AUDIO).stdout)
                self.assertEqual(value['coded_bytes'],(3*size+7)//8 if size<=16 else 128)
                self.assertEqual(value['wire_bits'],3*size if size<=16 else 1216)
                self.assertNotIn('packet_bytes',value)
    def test_short_dictionary_text_received_with_exact_bits(self):
        source=b'e\x00'
        value=json.loads(self.run_pump('simulate','--input','-','--json','--snr','30',
            '--clock-error-ppm','0','--phase-noise','0',*AUDIO,data=source).stdout)
        self.assertTrue(value['stream_complete'])
        self.assertFalse(value['content_validated'])
        self.assertFalse(value['authenticated'])
        self.assertTrue(value['short_text_decoded'])
        self.assertEqual(base64.b64decode(value['data_base64']),source)
        self.assertEqual(value['raw_bits'],'001'+'11111'+'00000000')
        self.assertEqual(value['raw_bit_count'],16)
        self.assertEqual(value['filename'],'')
    def test_automatic_profile_short_text_and_binary_dictionary(self):
        defaults=('--bw','3600','--target-snr','80','--receive-targets','80','--pattern','auto-pattern',
                  '--time','1800000000','--search-seconds','0')
        for source,expected_bits in (('quick brown',70),('quick brown fox ',98),('e'*16,48)):
            estimate=json.loads(self.run_pump('estimate','--text',source,*defaults).stdout)
            self.assertEqual(estimate['wire_bits'],expected_bits)
            value=json.loads(self.run_pump('simulate','--text',source,'--json','--snr','30',
                '--clock-error-ppm','0','--phase-noise','0',*defaults).stdout)
            self.assertTrue(value['stream_complete'])
            self.assertTrue(value['short_text_decoded'])
            self.assertFalse(value['content_validated'])
            self.assertEqual(value['observed_bit_count'],expected_bits)
            self.assertEqual(base64.b64decode(value['data_base64']),source.encode())
        with tempfile.TemporaryDirectory() as directory:
            path=pathlib.Path(directory)/'raw.wav'
            self.run_pump('status-tx','--bits','010','--output',path,*defaults)
            value=json.loads(self.run_pump('rx','--input',path,'--json',*defaults).stdout)
            self.assertEqual(value['raw_bits'],'010')
            self.assertEqual(base64.b64decode(value['data_base64']),b't')
            self.assertTrue(value['short_text_decoded'])
    def test_three_bit_dictionary_text_and_physical_end(self):
        with tempfile.TemporaryDirectory() as directory:
            path=pathlib.Path(directory)/'letter.wav'
            self.run_pump('tx','--text','e','--output',path,*AUDIO)
            value=json.loads(self.run_pump('rx','--input',path,'--json',*AUDIO).stdout)
            self.assertTrue(value['stream_complete'])
            self.assertTrue(value['short_text_decoded'])
            self.assertFalse(value['content_validated'])
            self.assertEqual(value['raw_bits'],'001')
            self.assertEqual(base64.b64decode(value['data_base64']),b'e')
            output=pathlib.Path(directory)/'letter.txt'
            self.run_pump('rx','--input',path,'--save',output,*AUDIO)
            self.assertEqual(output.read_bytes(),b'e')
            with wave.open(str(path),'rb') as f:
                params=f.getparams();pcm=f.readframes(f.getnframes())
            trim=6*params.framerate*params.sampwidth*params.nchannels
            with wave.open(str(path),'wb') as f:
                f.setparams(params);f.writeframes(pcm[:-trim])
            pending=json.loads(self.run_pump('rx','--input',path,'--json',*AUDIO).stdout)
            self.assertFalse(pending['stream_complete'])
            self.assertFalse(pending['short_text_decoded'])
            self.assertEqual(pending['data_base64'],'')
    def test_sampled_stream_roundtrip(self):
        source=b'fixed intervals\x00\x00'
        value=json.loads(self.run_pump('simulate','--input','-','--json','--snr','30',
            '--clock-error-ppm','0','--phase-noise','0',*AUDIO,data=source).stdout)
        self.assertTrue(value['stream_complete'])
        self.assertTrue(value['content_validated'])
        self.assertFalse(value['authenticated'])
        self.assertEqual(base64.b64decode(value['data_base64']),source)
        self.assertNotIn('packet_validated',value)
        self.assertEqual(value['kind'],'text')
        self.assertEqual(value['filename'],'')
        self.assertEqual(value['fec_repairs']['data']['repaired_bytes'],0)
        self.assertGreater(value['pre_fec_accuracy']['received_data_bits'],0)
    def test_uncompressed_exact_binary(self):
        source=bytes(range(32))+b'\x00\x00'
        value=json.loads(self.run_pump('simulate','--input','-','--no-compression','--json',
            '--snr','30','--clock-error-ppm','0','--phase-noise','0',*AUDIO,data=source).stdout)
        self.assertTrue(value['content_validated'])
        self.assertEqual(base64.b64decode(value['data_base64']),source)
        self.assertEqual(value['kind'],'text')
        self.assertEqual(value['filename'],'')
    def test_explicit_attachment(self):
        source=bytes(range(32))+b'\x00\x00'
        with tempfile.TemporaryDirectory() as directory:
            path=pathlib.Path(directory)/'payload.bin'
            path.write_bytes(source)
            value=json.loads(self.run_pump('simulate','--input',path,'--repeatable','--json',
                '--snr','30','--clock-error-ppm','0','--phase-noise','0',*AUDIO).stdout)
            self.assertTrue(value['content_validated'])
            self.assertEqual(value['kind'],'file')
            self.assertEqual(value['filename'],path.name)
            self.assertFalse(value['repeatable'])
            self.assertEqual(base64.b64decode(value['data_base64']),source)
    def test_wav_end_and_eof_are_distinct(self):
        with tempfile.TemporaryDirectory() as directory:
            path=pathlib.Path(directory)/'source.wav'
            self.run_pump('tx','--text','physical stream end','--output',path,*AUDIO)
            full=json.loads(self.run_pump('rx','--input',path,'--json',*AUDIO).stdout)
            self.assertTrue(full['content_validated'])
            self.assertTrue(full['stream_complete'])
            with wave.open(str(path),'rb') as f:
                params=f.getparams(); pcm=f.readframes(f.getnframes())
            # Keep less than one second of the transmitter's quiet tail.
            trim=6*params.framerate*params.sampwidth*params.nchannels
            with wave.open(str(path),'wb') as f:
                f.setparams(params);f.writeframes(pcm[:-trim])
            incomplete=json.loads(self.run_pump('rx','--input',path,'--json',*AUDIO).stdout)
            self.assertFalse(incomplete['stream_complete'])
            self.assertFalse(incomplete['content_validated'])
            self.assertEqual(incomplete['data_base64'],'')
            output=pathlib.Path(directory)/'incomplete.bin'
            self.run_pump('rx','--input',path,'--save',output,*AUDIO,ok=False)
            self.assertFalse(output.exists())
    def test_simulate_output_retains_observed_absence(self):
        source=b'saved simulated stream\x00\x00'
        with tempfile.TemporaryDirectory() as directory:
            path=pathlib.Path(directory)/'simulated.wav'
            simulated=json.loads(self.run_pump('simulate','--input','-','--output',path,'--json',
                '--snr','30','--clock-error-ppm','0','--phase-noise','0',*AUDIO,data=source).stdout)
            recorded=json.loads(self.run_pump('rx','--input',path,'--json',*AUDIO).stdout)
            for value in (simulated,recorded):
                self.assertTrue(value['stream_complete'])
                self.assertTrue(value['content_validated'])
                self.assertEqual(base64.b64decode(value['data_base64']),source)
    def test_raw_bits_same_physical_rule(self):
        with tempfile.TemporaryDirectory() as directory:
            path=pathlib.Path(directory)/'raw.wav'
            self.run_pump('status-tx','--bits','001','--output',path,*AUDIO)
            value=json.loads(self.run_pump('status-rx','--bits','001','--input',path,*AUDIO).stdout)
            self.assertEqual(value['raw_bits'],'001')
            self.assertTrue(value['known_bits_match'])
            self.assertFalse(value['content_validated'])
            self.assertTrue(value['stream_complete'])
            with wave.open(str(path),'rb') as f:
                params=f.getparams();pcm=f.readframes(f.getnframes())
            trim=6*params.framerate*params.sampwidth*params.nchannels
            with wave.open(str(path),'wb') as f:
                f.setparams(params);f.writeframes(pcm[:-trim])
            incomplete=json.loads(self.run_pump('status-rx','--bits','001','--input',path,*AUDIO).stdout)
            self.assertFalse(incomplete['stream_complete'])
            self.assertFalse(incomplete['known_bits_match'])
    def test_invalid_input_is_bounded(self):
        for extra in (('--fec','99'),('--cache-mb','0'),('--bw','0'),('--nonsense','x')):
            self.run_pump('estimate','--text','x',*extra,ok=False)
        self.run_pump('tx','--text','x',ok=False)

if __name__=='__main__': unittest.main()
