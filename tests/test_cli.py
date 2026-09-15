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
    def test_fixed_geometry_and_no_small_message_switch(self):
        one=json.loads(self.run_pump('estimate','--text','e',*AUDIO).stdout)
        longer=json.loads(self.run_pump('estimate','--text','fixed intervals',*AUDIO).stdout)
        self.assertEqual(one['coded_bytes'],128)
        self.assertEqual(one['wire_bits'],1216)
        self.assertEqual(one['wire_bits'],longer['wire_bits'])
        self.assertNotIn('packet_bytes',one)
    def test_sampled_stream_roundtrip(self):
        source=b'fixed intervals\x00\x00'
        value=json.loads(self.run_pump('simulate','--input','-','--json','--snr','30',
            '--clock-error-ppm','0','--phase-noise','0',*AUDIO,data=source).stdout)
        self.assertTrue(value['stream_complete'])
        self.assertTrue(value['content_validated'])
        self.assertFalse(value['authenticated'])
        self.assertEqual(base64.b64decode(value['data_base64']),source)
        self.assertNotIn('packet_validated',value)
    def test_uncompressed_exact_binary(self):
        source=bytes(range(32))+b'\x00\x00'
        value=json.loads(self.run_pump('simulate','--input','-','--no-compression','--json',
            '--snr','30','--clock-error-ppm','0','--phase-noise','0',*AUDIO,data=source).stdout)
        self.assertTrue(value['content_validated'])
        self.assertEqual(base64.b64decode(value['data_base64']),source)
    def test_wav_end_and_eof_are_distinct(self):
        with tempfile.TemporaryDirectory() as directory:
            path=pathlib.Path(directory)/'source.wav'
            self.run_pump('tx','--text','physical end','--output',path,*AUDIO)
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
