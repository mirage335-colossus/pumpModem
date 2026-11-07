"""Source-map/input-binding checks, independent of numerical receiver outcomes."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import triad_perf_physical_bits as physical


def cells(mask=0):
    result=[]
    for i in range(8):
        a=[[float(i+1),0.],[0.,1.],[1.,0.]]
        b=[[float(i+2),1.],[1.,0.],[0.,1.]]
        result.append(dict(cell=i,clear0=a,clear1=b,encrypted0=b if mask else a,encrypted1=a if mask else b))
    result.append(dict(cell=20,**{k:[[0.,0.]]*3 for k in ('clear0','clear1','encrypted0','encrypted1')}))
    return result


def row(mask=0,trial=0):
    return dict(schema=physical.MAP_SCHEMA,trial=trial,key_seed=123+trial,epoch=1793404800,
                encrypted=True,tx=cells(mask),rx=[dict(epoch_offset_seconds=0,cells=cells(mask))])


class SourceBitMapTests(unittest.TestCase):
    def test_both_permutations_require_eight_distinguishing_raw_cells(self):
        for mask in (0,1):
            result=physical.coefficient_map(cells(mask))
            self.assertEqual(result['mask'],mask)
            self.assertEqual(result['distinct_cells'],8)
        self.assertEqual(physical.coefficient_map(cells(0))['clear_coefficients_sha256'],
                         physical.coefficient_map(cells(1))['clear_coefficients_sha256'])
        with self.assertRaisesRegex(ValueError,'eight'):
            physical.coefficient_map(cells()[:-2])

    def test_inconsistent_map_changed_cover_nonfinite_and_duplicate_cells_rejected(self):
        for change in ('inconsistent','cover','nonfinite','duplicate'):
            value=cells()
            if change=='inconsistent':value[0]=cells(1)[0]
            elif change=='cover':value[-1]['encrypted0']=[[1.,0.]]*3
            elif change=='nonfinite':value[0]['clear0'][0][0]=float('nan')
            else:value[1]['cell']=value[0]['cell']
            with self.assertRaises(ValueError,msg=change):physical.coefficient_map(value)

    def test_pair_uses_only_source_probe_and_keeps_four_trial_maps(self):
        with tempfile.TemporaryDirectory() as tmp:
            binaries={name:Path(tmp)/name for name in ('reference','candidate')}
            for binary in binaries.values():binary.write_bytes(b'test-binary')
            calls=[]
            def invoke(argv,**kwargs):
                calls.append(argv)
                side=Path(argv[0]).name
                values=[row((i if side=='reference' else i+1)%2,i) for i in range(4)]
                return subprocess.CompletedProcess(argv,0,'\n'.join(json.dumps(x) for x in values),'')
            with mock.patch.object(physical.subprocess,'run',side_effect=invoke):
                receipt=physical.prepare_pair(binaries,['--seconds','262.4','--rate','36'],123,4,time.monotonic()+10)
            self.assertEqual(len(calls),2)
            self.assertTrue(all(x[x.index('--mode')+1]=='source-bit-map' for x in calls))
            self.assertEqual(physical.arguments(receipt,'reference'),
                             ['--performance-tx-masks','0101','--performance-rx-masks','0101','--performance-signal-bits','0101'])
            self.assertEqual(physical.arguments(receipt,'candidate',2),
                             ['--performance-tx-masks','1','--performance-rx-masks','1','--performance-signal-bits','0'])
            self.assertEqual(len(receipt['sources']['candidate']['rows']),4)

    def test_preflight_order_and_original_reference_plaintext_are_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            binaries={name:Path(tmp)/name for name in ('reference','candidate')}
            for p in binaries.values():p.write_bytes(b'x')
            calls=[]
            def invoke(argv,**kwargs):
                calls.append(Path(argv[0]).name)
                mask=0 if Path(argv[0]).name=='reference' else 1
                return subprocess.CompletedProcess(argv,0,'\n'.join(json.dumps(row((i+mask)%2,i)) for i in range(4)),'')
            with mock.patch.object(physical.subprocess,'run',side_effect=invoke):
                r=physical.prepare_pair(binaries,[],123,4,time.monotonic()+10,
                                        input_bits=[0,0,0,0],order=('candidate','reference'))
            self.assertEqual(calls,['candidate','reference'])
            self.assertEqual(r['physical_signal_bits'],[0,1,0,1])
            self.assertEqual([a^b for a,b in zip(r['physical_signal_bits'],r['sources']['reference']['tx_masks'])],[0,0,0,0])
            with self.assertRaisesRegex(ValueError,'per-capture key-seed'):
                physical.prepare_pair(binaries,['--key-seed','9'],123,4,time.monotonic()+10,separate_captures=True)

    def test_changed_clear_source_or_nonzero_epoch_fails_before_sampled_inputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            binaries={name:Path(tmp)/name for name in ('reference','candidate')}
            for p in binaries.values():p.write_bytes(b'x')
            def invoke(argv,**kwargs):
                r=row()
                if Path(argv[0]).name=='candidate':
                    r['tx'][0]['clear0'][0][0]+=5
                    r['tx'][0]['encrypted0'][0][0]+=5
                return subprocess.CompletedProcess(argv,0,json.dumps(r),'')
            with mock.patch.object(physical.subprocess,'run',side_effect=invoke),self.assertRaisesRegex(ValueError,'clear source'):
                physical.prepare_pair(binaries,[],123,1,time.monotonic()+10)
        r=row();r['rx'][0]['epoch_offset_seconds']=1
        with self.assertRaisesRegex(ValueError,'zero-epoch'):physical.validate_rows([r],1)

    def test_timed_native_receipt_preserves_raw_plaintext_and_map_binding(self):
        receipt=dict(schema=physical.SCHEMA,trials=1,physical_signal_bits=[0],sources={'candidate':dict(tx_masks=[1],rx_masks=[0])})
        value=dict(native_results=[dict(truth_bit=0,decoded_bit=1,receiver_plaintext_bit=1,
            performance_physical_input=dict(schema='triad-performance-physical-bits-v1',tx_mask=1,rx_mask=0,signal_physical_bit=0,signal_plaintext_bit=1))])
        physical.validate_native_receipt(value,receipt,'candidate')
        value['native_results'][0]['receiver_plaintext_bit']=0
        with self.assertRaisesRegex(ValueError,'frozen'):physical.validate_native_receipt(value,receipt,'candidate')

    def test_complete_preflight_compares_all_samples_and_retains_failed_raw_command(self):
        with tempfile.TemporaryDirectory() as tmp:
            binaries={name:Path(tmp)/name for name in ('reference','candidate')}
            for p in binaries.values():p.write_bytes(b'x')
            alignment=dict(schema=physical.SCHEMA,trials=1,physical_signal_bits=[0],order=['reference','candidate'],sources={
                name:dict(tx_masks=[0],rx_masks=[0]) for name in binaries})
            def invoke(argv,**kwargs):
                r=dict(schema='triad-performance-complete-input-v1',trial=0,seed=123,key_seed=123,
                    channel_seed=123,capture_samples=100,source_samples=90,chunk_samples=10,complete=True,
                    sha256=('a' if Path(argv[0]).name=='reference' else 'b')*64,
                    performance_physical_input=dict(tx_mask=0,rx_mask=0))
                return subprocess.CompletedProcess(argv,0,json.dumps(r),'')
            evidence=Path(tmp)/'evidence'
            with mock.patch.object(physical.subprocess,'run',side_effect=invoke),self.assertRaisesRegex(ValueError,'received inputs differ'):
                physical.complete_native_inputs(binaries,[],123,alignment,time.monotonic()+10,output=evidence)
            paths=sorted(evidence.glob('*/*-raw.json'))
            self.assertEqual(len(paths),2)
            self.assertTrue(all(json.loads(p.read_text())['returncode']==0 for p in paths))
            with (mock.patch.object(physical.subprocess,'run',return_value=subprocess.CompletedProcess([],2,'','bad setup')),
                  self.assertRaisesRegex(ValueError,'map command failed')):
                physical.prepare_pair(binaries,[],123,1,time.monotonic()+10,output=evidence)
            failures=[json.loads(p.read_text()) for p in evidence.glob('*/*-raw.json') if json.loads(p.read_text())['returncode']==2]
            self.assertEqual(len(failures),1);self.assertEqual(failures[0]['stderr'],'bad setup')

    def test_all_binary_fixture_inputs_remain_exact_manifest_labels_are_retained(self):
        entry=dict(sha256={'manifest.json':'old-labels','observation.c128':'same','gram.c128':'same2'})
        pair={name:dict(fixture=[copy.deepcopy(entry)]) for name in ('reference','candidate')}
        pair['candidate']['fixture'][0]['sha256']['manifest.json']='new-labels'
        result=physical.validate_fixture_inputs(pair)
        self.assertIn('observation.c128',result['binary_input_sha256'][0])
        pair['candidate']['fixture'][0]['sha256']['observation.c128']='changed'
        with self.assertRaisesRegex(ValueError,'input bytes'):physical.validate_fixture_inputs(pair)

    def test_complete_input_binding_does_not_accept_different_seed_or_work(self):
        r=dict(trial=0,seed=123,key_seed=123,channel_seed=123,capture_samples=100,source_samples=90,
               source_format={'encrypted':True},random_seed_roots={'noise':123},sample_rate_numerator=64,
               sample_rate_denominator=1,performance_physical_input={'tx_mask':1,'rx_mask':0})
        complete=dict(sources={'reference':dict(rows=[copy.deepcopy(r)])})
        result=dict(native_results=[copy.deepcopy(r)])
        physical.validate_native_input_binding(result,complete,'reference')
        for field in ('seed','capture_samples'):
            bad=copy.deepcopy(result);bad['native_results'][0][field]+=1
            with self.assertRaisesRegex(ValueError,'binding'):physical.validate_native_input_binding(bad,complete,'reference')


if __name__=='__main__':unittest.main()
