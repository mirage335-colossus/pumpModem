"""CLI integration for the fixed interval format and physical end gate."""
import base64
import json
import math
import pathlib
import subprocess
import sys
import tempfile
import unittest
import wave

PUMP = str(pathlib.Path(sys.argv.pop(1)).resolve())
AUDIO = ('--sample-rate','8000','--bw','1000','--carrier','1500','--spreading','16','--time','1800000000','--search-seconds','0')

class StreamCLI(unittest.TestCase):
    def run_pump(self,*args,data=None,ok=True,timeout=90):
        p=subprocess.run([PUMP,*map(str,args)],input=data,capture_output=True,timeout=timeout)
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
    def test_shannon_capacity_estimate(self):
        for bandwidth,target,expected in ((1000,30,1000), (2000,30,1169.9250014423124),
                                          (1000,60,9967.226258835993), (30000000,-200,1.4426950408889634e-20)):
            # Force a finite symbol at the weakest target, beyond automatic timing's range.
            pattern=('--pattern','pattern-16') if target==-200 else ()
            value=json.loads(self.run_pump('estimate','--text','e','--bw',bandwidth,
                                          '--target-snr',target,*pattern).stdout)
            self.assertTrue(math.isclose(value['shannon_capacity_bps'],expected,rel_tol=1e-12))
            self.assertEqual(value['wire_bits'],3)
            self.assertIn('bit_rate',value)
        manual=json.loads(self.run_pump('estimate','--text','e',*AUDIO).stdout)
        self.assertTrue(math.isclose(manual['shannon_capacity_bps'],1370.1046697509862,rel_tol=1e-12))
        extreme=json.loads(self.run_pump('estimate','--text','e','--bw','1000','--target-snr','1e308').stdout)
        self.assertIsNone(extreme['shannon_capacity_bps'])
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
        self.assertEqual(value['recovery']['state'],'none')
        self.assertEqual(value['recovery']['attempts'],0)
    def test_lpi_advisory_estimates(self):
        public=json.loads(self.run_pump('estimate','--text','e',*AUDIO).stdout)
        self.assertEqual(public['lpi']['status'],'public_waveform')
        self.assertIsNone(public['lpi']['equivalent_wire_symbols'])
        with tempfile.TemporaryDirectory() as directory:
            key=pathlib.Path(directory)/'lpi.key'
            self.run_pump('keygen','--output',key)
            geometry=('--bw','100','--target-snr','-3','--keyfile',key)
            value=json.loads(self.run_pump('estimate','--text','e',*geometry).stdout)
            model=value['lpi']
            self.assertEqual(value['wire_bits'],3)
            self.assertEqual(model['status'],'available')
            self.assertEqual(model['cn0_basis'],'assumed_tx_target')
            self.assertFalse(model['safe_traffic_limit'])
            self.assertEqual(model['detection_probability'],.9)
            self.assertEqual(model['false_alarm_probability_per_window'],.01)
            self.assertTrue(math.isclose(model['detection_seconds'],3257.3126172241587,rel_tol=1e-12))
            self.assertTrue(math.isclose(model['equivalent_wire_symbols'],19.881058454737296,rel_tol=1e-12))
            self.assertTrue(math.isclose(model['burst_exposure_ratio'],value['total_seconds']/model['detection_seconds'],rel_tol=1e-12))
            # Manual keyed transfer automatically selects private Scrambler patterns.
            manual=json.loads(self.run_pump('analyze-link','--bits','001',*AUDIO,
                '--keyfile',key,'--simulation','3dBm -170dB','--trials','10').stdout)
            self.assertEqual(manual['lpi']['status'],'available')
            analysis=json.loads(self.run_pump('analyze-link','--bits','001',*geometry,
                '--simulation','3dBm -170dB','--trials','10').stdout)
            self.assertEqual(analysis['transmission']['wire_bits'],3)
            self.assertEqual(analysis['lpi']['cn0_basis'],'simulated_link')
            self.assertEqual(analysis['lpi']['cn0_db_hz'],-3)
            self.assertEqual(analysis['lpi']['detection_seconds'],model['detection_seconds'])
            strong=json.loads(self.run_pump('estimate','--text','e','--keyfile',key,*AUDIO).stdout)
            self.assertEqual(strong['lpi']['status'],'outside_weak_signal_model')
            self.assertIsNone(strong['lpi']['detection_seconds'])
    def test_sub_hertz_estimate(self):
        for bandwidth in ('0.01', '0.1Hz', '0.5'):
            numeric=float(bandwidth.removesuffix('Hz'))
            value=json.loads(self.run_pump('estimate','--text','a','--bw',bandwidth,
                                          '--target-snr','-3').stdout)
            self.assertEqual(value['wire_bits'],3)
            self.assertEqual(value['sample_rate'],6000)
            self.assertEqual(value['carrier_hz'],1500)
            self.assertTrue(math.isclose(value['symbol_seconds'],128/numeric,rel_tol=1e-12))
            self.assertTrue(math.isclose(value['bit_rate'],numeric/128,rel_tol=1e-12))
    def test_link_analysis_exact_draft_and_airtime(self):
        geometry=('--bw','100','--target-snr','-36','--time','1800000000')
        link=('--tx-dbm','3','--attenuation-db','-200','--trials','2000')
        for draft,count in ((('--bits','0'),1),(('--bits','001'),3),(('--text','a'),3)):
            value=json.loads(self.run_pump('analyze-link',*draft,*geometry,*link,timeout=10).stdout)
            self.assertEqual(value['transmission']['wire_bits'],count)
            self.assertTrue(value['transmission']['raw_wire_path'])
            self.assertFalse(value['production_decoder_run'])
            self.assertFalse(value['pcm_generated'])
            self.assertTrue(value['conditional_on_matched_timing_and_clock'])
            self.assertTrue(value['prescribed_template_correlation_not_measured'])
            self.assertEqual(value['receive_profile_assumption'],'matching_transmit_profile')
            self.assertTrue(value['current_receiver']['profile_matches'])
            self.assertEqual(value['transmission']['symbol_duration_source'],'configured_tx_plan')
            self.assertNotIn('stream_complete',value)
            self.assertNotIn('raw_bits',value)
            self.assertNotIn('data_base64',value)
            self.assertEqual(value['link']['received_power_dbm'],-197)
            self.assertEqual(value['link']['cn0_db_hz'],-33)
            self.assertEqual(value['link']['snr_100hz_db'],-53)
            self.assertTrue(math.isclose(value['link']['ideal_18db_symbol_seconds'],10**5.1,rel_tol=1e-12))
            self.assertTrue(math.isclose(value['link']['zero_residual_coherent_energy_asymptote_linear'],
                                        4*10**(-3.3)/math.radians(0.5)**2,rel_tol=1e-12))
            self.assertIsNone(value['current_receiver']['success_probability'])
            self.assertFalse(value['current_receiver']['carrier_in_search'])
            self.assertEqual(value['current_receiver']['clock_error_ppm'],100)
            self.assertEqual(value['current_receiver']['frequency_offset_hz'],0)
            self.assertTrue(value['current_receiver']['gpu_hypothetical'])
            self.assertIn('i9-13900H',value['current_receiver']['cpu_reference'])
            self.assertIn('4090 Laptop GPU',value['current_receiver']['gpu_reference'])
            self.assertGreaterEqual(value['current_receiver']['tracking_seconds'],0)
            self.assertGreaterEqual(value['current_receiver']['cpu_seconds'],value['current_receiver']['tracking_seconds'])
            self.assertGreaterEqual(value['current_receiver']['gpu_seconds'],value['current_receiver']['tracking_seconds'])
            self.assertEqual(value['current_receiver']['tracking_symbol_windows'],count)
            self.assertLessEqual(value['current_receiver']['frequency_hypotheses'],4097)
            self.assertGreater(value['current_receiver']['full_200ppm_frequency_hypotheses'],4097)
            for experiment in value['experiments'].values():
                self.assertEqual(experiment['trials'],2000)
                self.assertGreater(experiment['chips_per_segment'],0)
                self.assertLessEqual(experiment['correct_detection_probability_low'],experiment['correct_detection_probability'])
                self.assertGreaterEqual(experiment['correct_detection_probability_high'],experiment['correct_detection_probability'])
            self.assertFalse(value['experiments']['ideal_coherent']['phase_mean_energy_approximation'])
            self.assertTrue(value['experiments']['coherent_phase_model']['phase_mean_energy_approximation'])
        exact=json.loads(self.run_pump('estimate','--text','a',*geometry).stdout)
        for field in ('wire_bits','coded_bytes','content_bytes','coded_seconds','content_seconds'):
            self.assertEqual(value['transmission'][field],exact[field])
        self.assertEqual(value['transmission']['waveform_seconds'],exact['total_seconds'])
        self.assertGreater(value['transmission']['simulated_seconds'],exact['total_seconds'])
    def test_link_analysis_bounded_extreme_duration(self):
        # A billion-second symbol must stay an O(trials) statistical job;
        # generating or searching even its first PCM symbol would time out.
        args=('analyze-link','--bits','001','--tx-dbm','3','--attenuation-db','-200',
              '--bw','100','--symbol-seconds','1e9','--trials','2000','--seed','17')
        value=json.loads(self.run_pump(*args,timeout=10).stdout)
        self.assertEqual(value['transmission']['symbol_seconds'],1e9)
        self.assertEqual(value['transmission']['symbol_duration_source'],'explicit_override')
        self.assertEqual(value['transmission']['wire_bits'],3)
        self.assertGreater(value['transmission']['waveform_samples'],10**12)
        segmented=value['experiments']['segmented_phase_model']
        self.assertEqual(segmented['segments'],math.ceil(1e9/3600))
        self.assertTrue(math.isclose(segmented['segments']*segmented['segment_seconds'],1e9,rel_tol=1e-12))
        self.assertFalse(value['current_receiver']['confidence_available'])
        self.assertIsNone(value['current_receiver']['success_probability'])
        self.assertTrue(math.isclose(value['reference_assumptions']['noise_pair_union_bound'],2e-12,rel_tol=1e-12))
        repeat=json.loads(self.run_pump(*args,timeout=10).stdout)
        self.assertEqual(value['experiments'],repeat['experiments'])
    def test_link_analysis_independent_targets_channel_and_reference(self):
        args=('analyze-link','--text','a','--bw','100','--trials','100',
              '--tx-dbm','3','--attenuation-db','-200')
        fast=json.loads(self.run_pump(*args,'--target-snr','32',timeout=10).stdout)
        slow=json.loads(self.run_pump(*args,'--target-snr','-36',timeout=10).stdout)
        self.assertEqual(fast['link'],slow['link'])
        self.assertGreater(slow['transmission']['symbol_seconds'],fast['transmission']['symbol_seconds'])
        profile=json.loads(self.run_pump(*args,'--target-snr','-36','--receive-targets','32',timeout=10).stdout)
        self.assertEqual(profile['receive_profile_assumption'],'explicit_receive_targets')
        self.assertFalse(profile['current_receiver']['profile_matches'])
        self.assertIsNone(profile['current_receiver']['success_probability'])
        ideal=json.loads(self.run_pump(*args,'--phase-noise','0','--clock-error-ppm','0',
            '--frequency-offset','0.125','--residual-frequency-hz','0.001',
            '--symbol-seconds','10000','--template-correlation','0.25','--noise-figure-db','4',timeout=10).stdout)
        self.assertEqual(ideal['link']['cn0_db_hz'],-27)
        self.assertIsNone(ideal['link']['zero_residual_coherent_energy_asymptote_linear'])
        self.assertEqual(ideal['current_receiver']['clock_error_ppm'],0)
        self.assertEqual(ideal['current_receiver']['frequency_offset_hz'],0.125)
        self.assertEqual(ideal['transmission']['symbol_duration_source'],'explicit_override')
        self.assertEqual(ideal['current_receiver']['actual_carrier_offset_hz'],0.125)
        self.assertEqual(ideal['reference_assumptions']['residual_frequency_hz'],0.001)
        self.assertEqual(ideal['experiments']['segmented_phase_model']['template_correlation'],0.25)
        preset=json.loads(self.run_pump('analyze-link','--input','-','--simulation','3dBm -200dB',
            '--trials','100','--bw','100','--target-snr','-36',data=b'a',timeout=10).stdout)
        self.assertEqual(preset['link'],slow['link'])
        self.assertEqual(preset['transmission']['wire_bits'],3)
    def test_link_analysis_rejects_invalid_and_misplaced_options(self):
        args=('analyze-link','--text','a','--tx-dbm','3','--attenuation-db','-200','--trials','10')
        for extra in (('--symbol-seconds','0'),('--symbol-seconds','-1'),('--symbol-seconds','1e308'),
                      ('--coherent-seconds','0'),('--coherent-seconds','1e19'),
                      ('--hypotheses','1'),('--false-alarm','0'),('--false-alarm','1'),
                      ('--residual-frequency-hz','nan'),('--template-correlation','2'),
                      ('--noise-figure-db','-1'),('--output','unused.wav'),('--snr','20')):
            self.run_pump(*args,*extra,ok=False,timeout=10)
        for trials in ('0','1000001','1.5','-1'):
            self.run_pump(*args[:-2],'--trials',trials,ok=False,timeout=10)
        for draft in ((),('--bits',''),('--bits','01x'),('--bits','0','--text','a'),
                      ('--bits','0','--input','-'),('--bits','0','--callsign','CQ'),
                      ('--bits','0','--repeatable')):
            self.run_pump('analyze-link',*draft,'--tx-dbm','3','--attenuation-db','-200',ok=False,timeout=10)
        for power in ((),('--tx-dbm','3'),('--attenuation-db','-200'),
                      ('--tx-dbm','3','--attenuation-db','200'),
                      ('--simulation','off'),('--simulation','3dBm -170dB','--tx-dbm','3'),
                      ('--simulation','3dBm -170dB','--attenuation-db','-200')):
            self.run_pump('analyze-link','--text','a',*power,ok=False,timeout=10)
        for flag in ('tx-dbm','attenuation-db','noise-figure-db','symbol-seconds','coherent-seconds',
                     'trials','hypotheses','false-alarm','residual-frequency-hz','template-correlation'):
            self.run_pump('estimate','--text','a','--'+flag,'1',ok=False)
    def test_oscillator_models_and_overrides(self):
        args=('analyze-link','--text','a','--bw','100','--symbol-seconds','10000',
              '--simulation','3dBm -200dB','--trials','100')
        for name,clock,phase in (('crystal',100,.5),('gpsdo-xo',.1,.5),
                                 ('gpsdo-tcxo',.01,.05),('gpsdo-ocxo',.0001,.005)):
            value=json.loads(self.run_pump(*args,'--oscillator',name).stdout)
            self.assertEqual(value['oscillator_model'],{'preset':name,'illustrative':True,'overridden':False})
            self.assertEqual(value['current_receiver']['clock_error_ppm'],clock)
            self.assertEqual(value['reference_assumptions']['phase_noise_degrees_per_sqrt_second'],phase)
            explicit=json.loads(self.run_pump(*args,'--clock-error-ppm',clock,'--phase-noise',phase).stdout)
            self.assertEqual(value['current_receiver'],explicit['current_receiver'])
            self.assertEqual(value['experiments'],explicit['experiments'])
        override=json.loads(self.run_pump(*args,'--oscillator','gpsdo-ocxo','--phase-noise','2').stdout)
        self.assertTrue(override['oscillator_model']['overridden'])
        self.assertEqual(override['current_receiver']['clock_error_ppm'],.0001)
        self.assertEqual(override['reference_assumptions']['phase_noise_degrees_per_sqrt_second'],2)
        # Custom negative transmit powers remain valid, but are no longer presets.
        negative=json.loads(self.run_pump('analyze-link','--bits','0','--tx-dbm','-3',
                                         '--attenuation-db','-200','--trials','10').stdout)
        self.assertEqual(negative['link']['received_power_dbm'],-203)
        self.run_pump('analyze-link','--bits','0','--simulation','-3dBm -200dB',ok=False)
        for command in ('simulate','listen','analyze-link'):
            self.run_pump(command,'--text','a','--simulation','3dBm -120dB',
                          '--oscillator','missing',ok=False)
        self.run_pump('estimate','--text','a','--oscillator','gpsdo-xo',ok=False)
    def test_oscillator_preset_applies_to_sampled_waveform(self):
        with tempfile.TemporaryDirectory() as directory:
            args=('simulate','--text','a',*AUDIO,'--snr','30','--seed','713')
            for name,clock,phase in (('gpsdo-xo',.1,.5),('gpsdo-ocxo',.0001,.005)):
                preset=pathlib.Path(directory)/(name+'-preset.wav')
                explicit=pathlib.Path(directory)/(name+'-explicit.wav')
                self.run_pump(*args,'--oscillator',name,'--output',preset)
                self.run_pump(*args,'--clock-error-ppm',clock,'--phase-noise',phase,'--output',explicit)
                self.assertEqual(preset.read_bytes(),explicit.read_bytes())
    def test_sub_hertz_sampled_reception(self):
        # A reduced internal clock exercises the actual 3.56-hour symbol
        # geometry with a bounded test recording, without waiting in real time.
        # This is a zero-drift control, not a hardware sensitivity result.
        value=json.loads(self.run_pump('simulate','--text','a','--json','--bw','0.01',
            '--sample-rate','64','--carrier','16','--spreading','64',
            '--clock-error-ppm','0','--phase-noise','0','--snr','30',
            '--search-seconds','0','--time','1800000000').stdout)
        self.assertTrue(value['stream_complete'])
        self.assertEqual(value['raw_bits'],'011')
        self.assertEqual(value['missing_symbols'],0)
        self.assertEqual(base64.b64decode(value['data_base64']),b'a')
    def test_short_shaped_carrier_at_passband_edge(self):
        # The shaped waveform fits exactly above DC. Default offset search
        # must retain its valid center when no negative pair fits the passband.
        geometry=('--bw','3600','--sample-rate','14400','--carrier','1125','--spreading','16')
        estimate=json.loads(self.run_pump('estimate','--text','a',*geometry).stdout)
        self.assertEqual(estimate['wire_bits'],3)
        value=json.loads(self.run_pump('simulate','--text','a','--json',*geometry,
            '--snr','30','--clock-error-ppm','0','--phase-noise','0',
            '--time','1800000000','--search-seconds','0').stdout)
        self.assertTrue(value['stream_complete'])
        self.assertEqual(value['raw_bits'],'011')
        self.assertEqual(value['missing_symbols'],0)
        self.assertEqual(base64.b64decode(value['data_base64']),b'a')
    def test_recovery_settings(self):
        helptext=self.run_pump('--help').stdout
        for option in ('--recovery-seconds','--recovery-threads','--recovery-bits','--recovery-errors'):
            self.assertIn(option.encode(),helptext)
        # Recovery is a local receive policy and never changes the tiny wire path.
        value=json.loads(self.run_pump('simulate','--text','e','--json','--snr','30',
            '--clock-error-ppm','0','--phase-noise','0','--recovery-seconds','0',
            '--recovery-threads','1','--recovery-bits','1024','--recovery-errors','0',*AUDIO).stdout)
        self.assertTrue(value['stream_complete'])
        self.assertEqual(value['raw_bits'],'001')
        self.assertEqual(value['recovery']['state'],'none')
        self.assertEqual(value['recovery']['attempts'],0)
        for extra in (('--recovery-seconds','-1'),('--recovery-seconds','86401'),
                      ('--recovery-threads','1025'),('--recovery-threads','1.5'),
                      ('--recovery-bits','1023'),('--recovery-bits','1048577'),
                      ('--recovery-errors','25'),('--recovery-errors','-1')):
            self.run_pump('simulate','--text','e',*extra,*AUDIO,ok=False)
        for command in ('tx','estimate','status-rx'):
            self.run_pump(command,'--recovery-seconds','0',*AUDIO,ok=False)
    def test_live_json_reception_identity(self):
        result=self.run_pump('listen','--text','quick brown','--json','--seconds','8',
            '--simulation','3dBm -90dB','--bw','3600','--pattern','auto-pattern',
            '--target-snr','32','--receive-targets','55,32','--time','1800000000',
            '--search-seconds','0','--clock-error-ppm','0','--phase-noise','0')
        events=[json.loads(line) for line in result.stdout.splitlines()]
        pending=[event for event in events if event.get('event')=='raw_bits' and not event['complete']]
        completed=[event for event in events if event.get('stream_complete')]
        self.assertTrue(pending,'Live JSON lost the pending raw-bit events')
        self.assertEqual(len(completed),1,'Competing profiles emitted duplicate completed payloads')
        payload=completed[0]
        self.assertEqual(base64.b64decode(payload['data_base64']),b'quick brown')
        updates=[event for event in events if event.get('event')=='reception_update']
        self.assertTrue(updates,'Completed interpretation did not publish its live reception identity')
        self.assertEqual(updates[-1]['reception_id'],payload['id'])
        self.assertEqual(updates[-1]['signal_id'],payload['signal_id'])
        self.assertEqual(updates[-1]['revision'],payload['revision'])
        revisions={}
        for event in events:
            if 'signal_id' not in event:
                continue
            self.assertIsInstance(event['revision'],int)
            self.assertIsInstance(event['superseded_ids'],list)
            if event.get('event') in ('raw_bits','reception_update'):
                self.assertEqual(event['recovery']['state'],'none')
                self.assertEqual(event['recovery']['attempts'],0)
            self.assertGreaterEqual(event['revision'],revisions.get(event['signal_id'],0))
            revisions[event['signal_id']]=event['revision']
        self.assertTrue(any(event['signal_id']==payload['signal_id'] for event in pending),
                        'Pending raw bits and completed payload have unrelated identities')
    def test_automatic_profile_short_text_and_binary_dictionary(self):
        defaults=('--bw','3600','--pattern','auto-pattern',
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
            self.assertEqual(pending['recovery']['state'],'none')
            self.assertEqual(pending['recovery']['attempts'],0)
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
            self.run_pump('status-tx','--bits','001','--output',path,'--no-mono',*AUDIO)
            value=json.loads(self.run_pump('status-rx','--bits','001','--input',path,*AUDIO).stdout)
            self.assertEqual(value['raw_bits'],'001')
            self.assertTrue(value['known_bits_match'])
            self.assertFalse(value['content_validated'])
            self.assertTrue(value['stream_complete'])
            with wave.open(str(path),'rb') as f:
                params=f.getparams();pcm=f.readframes(f.getnframes())
            self.assertEqual(params.nchannels,1,'Live stereo routing must not change WAV framing')
            trim=6*params.framerate*params.sampwidth*params.nchannels
            with wave.open(str(path),'wb') as f:
                f.setparams(params);f.writeframes(pcm[:-trim])
            incomplete=json.loads(self.run_pump('status-rx','--bits','001','--input',path,*AUDIO).stdout)
            self.assertFalse(incomplete['stream_complete'])
            self.assertFalse(incomplete['known_bits_match'])
    def test_invalid_input_is_bounded(self):
        for extra in (('--fec','99'),('--cache-mb','0'),('--bw','0'),('--bw','0.009'),('--nonsense','x')):
            self.run_pump('estimate','--text','x',*extra,ok=False)
        self.run_pump('tx','--text','x',ok=False)

if __name__=='__main__': unittest.main()
