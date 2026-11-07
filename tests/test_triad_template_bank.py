from pathlib import Path
import copy
import json
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from triad_template_bank import generate_template_bank
from triad_stream_slots import load_slots


def manifest():
    return dict(schema='triad-helper-whole-bit-public-v1',sample_rate_hz=1000,
        key_seed=3,public_epoch_seconds=100,encrypted_source=True,
        templates=dict(enabled=False,file='not-written',shape=[1,13,2,7],
            epoch_offsets_seconds=list(range(-6,7)),bits=[0,1],source_support_samples=5),
        whole_bit_calendar=dict(slots=[dict(symbol_ordinal=3,key_epoch_seconds=100,
            helper_begin_sample=2000,support_begin_sample=4000,pattern_begin_sample=4002,
            helper_end_sample=5000)]),
        format=dict(pattern_revision='independent-apsk-v2',added_amplitude_depth=.25,carrier_spacing_hz=75,
            inner_rate_hz=36,added_symbols=10,fine_chip_rate_hz=60,occupied_bandwidth_hz=225,
            quiet_seconds=0,ramp_seconds=.02,margin_seconds=.005,pulse_shape='rrc-025-span8-c2-tail2',
            tile_seconds=11.6,tile_count=1,fine_coefficients=660,inner_coefficients=66,
            fhss_envelope_only=False,fhss_center_aligned=False,bit_seconds=11.6,
            added_interleave=True,physical_rf_retuning=False,dwell_seconds=.4),
        power=dict(template_data_gain=1.2),calendar=dict(capture_samples=6000,arrival_prior_seconds=[-1,1],
            data_calendar_origin_sample=2),
        channel=dict(sample_clock_pair_ppm=1,spread_margin=3),observation=dict(shape=[6000]),validity=dict(shape=[6000]))


ARGV=['native','--epoch','100','--key-seed','3','--power-reference','whole-active']


class BankTests(unittest.TestCase):
    def factory(self, **alter):
        owner=self
        class Source:
            def __init__(self,*args,**kwargs):
                self.closed=False;self.calls=[];owner.source=self
                self.descriptor=dict(sample_rate_hz=1000,source_samples=5,epoch=100,symbol_ordinal=3,
                    pattern_revision='independent-apsk-v2',added_amplitude_depth=.25,carrier_spacing_hz=75,
                    template_data_gain=1.2,power=dict(data_power_reference_policy='whole-active'))
                self.descriptor.update(alter)
                f=manifest()['format'];self.plan=dict(format={k:v for k,v in f.items()})
                self.plan['format'].update(fhss=f['fhss_envelope_only'],center_aligned=f['fhss_center_aligned'],
                    elapsed_bit_seconds=f['bit_seconds'],symbol_ordinal=3)
                self.plan['geometry']=dict(padded_source_samples=5,source_calendar_origin_sample=2)
            def basis(self,first,count,rows):
                self.calls.append((first,count,rows))
                return (np.arange(26)[:,None]*100+first+np.arange(count)[None,:]).astype(np.complex128)
            def close(self):self.closed=True
        return Source

    def test_chunk_order_complete_bank_and_no_file_export(self):
        bank,receipt=generate_template_bank(manifest(),ARGV,chunk_samples=3,source_factory=self.factory())
        self.assertTrue(self.source.closed);self.assertEqual(bank.shape,(1,13,2,7))
        self.assertEqual([x[:2]for x in self.source.calls],[(0,3),(3,3),(6,1)])
        expected=np.arange(26)[:,None]*100+np.arange(7)[None,:]
        np.testing.assert_array_equal(bank.reshape(26,7),expected)
        self.assertEqual(self.source.calls[0][2][0],(0,-6,0.,0.,0.,0.,0.))
        self.assertEqual(self.source.calls[0][2][-1],(1,6,0.,0.,0.,0.,0.))
        self.assertEqual(receipt['bytes'],26*7*16)
        with tempfile.TemporaryDirectory()as tmp:
            root=Path(tmp);m=manifest();(root/'manifest.json').write_text(json.dumps(m))
            original,slots=load_slots(root,templates=bank)
            self.assertFalse(original['templates']['enabled']);self.assertEqual(len(slots),1)
            np.testing.assert_array_equal(slots[0][1],bank[0])
            with self.assertRaises(ValueError):load_slots(root,templates=bank.astype(np.complex64))
            with self.assertRaises(ValueError):load_slots(root,templates=bank[0])

    def test_identity_failure_closes_owned_process(self):
        with self.assertRaisesRegex(ValueError,'symbol_ordinal'):
            generate_template_bank(manifest(),ARGV,source_factory=self.factory(symbol_ordinal=4))
        self.assertTrue(self.source.closed);self.assertEqual(self.source.calls,[])

    def test_input_and_memory_guards_precede_source(self):
        def forbidden(*args,**kwargs):raise AssertionError('must not launch source')
        for argv in (ARGV+['--templates'],ARGV+['--output','x'],ARGV[:-2],
                     ARGV[:-1]+['bounded-tiles'],ARGV+['--stream-server'],ARGV+['--key-file','unused'],
                     ARGV+['--now']):
            with self.assertRaises(ValueError):generate_template_bank(manifest(),argv,source_factory=forbidden)
        for chunk in (0,True,1.5,16385):
            with self.assertRaises(ValueError):generate_template_bank(manifest(),ARGV,chunk_samples=chunk,source_factory=forbidden)
        with self.assertRaises(MemoryError):generate_template_bank(manifest(),ARGV,memory_limit_bytes=100,source_factory=forbidden)
        m=manifest();m['templates']['epoch_offsets_seconds']=[0]*13
        with self.assertRaises(ValueError):generate_template_bank(m,ARGV,source_factory=forbidden)

    def test_key_and_origin_mismatch_rejected_before_launch(self):
        def forbidden(*args,**kwargs):raise AssertionError('must not launch source')
        for flag in ('--key-seed','--epoch'):
            argv=list(ARGV);argv[argv.index(flag)+1]='101'
            with self.assertRaisesRegex(ValueError,flag):
                generate_template_bank(manifest(),argv,source_factory=forbidden)

    def test_memory_guard_covers_four_response_copies_at_boundary(self):
        budget=26*7*16+4*26*3*16
        def forbidden(*args,**kwargs):raise AssertionError('must not launch source')
        with self.assertRaises(MemoryError):
            generate_template_bank(manifest(),ARGV,chunk_samples=3,
                memory_limit_bytes=budget-1,source_factory=forbidden)
        bank,receipt=generate_template_bank(manifest(),ARGV,chunk_samples=3,
            memory_limit_bytes=budget,source_factory=self.factory())
        self.assertEqual(receipt['parent_bulk_budget_bytes'],budget)
        np.testing.assert_array_equal(bank.reshape(26,7),np.arange(26)[:,None]*100+np.arange(7)[None,:])
        self.assertTrue(self.source.closed)

    def test_same_geometry_different_source_rejected_before_basis(self):
        for name,value in (('pulse_shape','other'),('ramp_seconds',.04),('added_symbols',1),
                           ('fhss',True),('symbol_ordinal',4)):
            source=self.factory();original=source.__init__
            def init(obj,*args,_name=name,_value=value,**kwargs):
                original(obj,*args,**kwargs);obj.plan['format'][_name]=_value
            source.__init__=init
            with self.assertRaisesRegex(ValueError,'format.'):
                generate_template_bank(manifest(),ARGV,source_factory=source)
            self.assertTrue(self.source.closed);self.assertEqual(self.source.calls,[])

    def test_source_calendar_and_fixed_exporter_contracts(self):
        source=self.factory();original=source.__init__
        def init(obj,*args,**kwargs):
            original(obj,*args,**kwargs);obj.plan['geometry']['source_calendar_origin_sample']=3
        source.__init__=init
        with self.assertRaisesRegex(ValueError,'source_calendar_origin_sample'):
            generate_template_bank(manifest(),ARGV,source_factory=source)
        self.assertTrue(self.source.closed);self.assertEqual(self.source.calls,[])
        for section,name,value in (('format','added_interleave',False),('format','dwell_seconds',.5),
                                   ('format','physical_rf_retuning',True),(None,'encrypted_source',False)):
            m=manifest();(m if section is None else m[section])[name]=value
            with self.assertRaisesRegex(ValueError,name):
                generate_template_bank(m,ARGV,source_factory=self.factory())
            self.assertTrue(self.source.closed);self.assertEqual(self.source.calls,[])

    def test_response_error_closes_owned_process(self):
        source=self.factory()
        source.basis=lambda *args:np.zeros((1,2),np.complex128)
        with self.assertRaisesRegex(ValueError,'basis response'):
            generate_template_bank(manifest(),ARGV,source_factory=source)
        self.assertTrue(self.source.closed)


if __name__=='__main__':unittest.main()
