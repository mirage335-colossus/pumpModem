"""Bounded orchestration receipts never become a reception claim."""
from pathlib import Path
import sys
import unittest
from unittest import mock
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from triad_orchestration_cost import _push_units, _full_window_assembly, _message_state_fixture
from triad_stream_receiver import native_short_decoder


class FakeSource:
    descriptor={'fixed_workspace_bytes':0,'workspace_budget_bytes':1024}
    def slot_plan(self,index):
        return dict(index=index,sample_rate_hz=10,
          receiver_window_first_sample=index*40,
          receiver_window_stop_sample=index*40+100,
          receiver_window_samples=100,
          receiver_window_physical_origin_sample=index*40,
          pattern_epoch=1793850000,symbol_ordinal=index,
          source_begin_samples=index*40,next_source_begin_samples=(index+1)*40,
          helper_begin_samples=index*40,helper_end_samples=index*40+100,
          window_capture_lead_seconds=10.,source_samples=100)


class Prepared:
    fs=10;memory_limit_bytes=8*1024**2;executable='/unused'
    observation_first_sample=0
    manifest={'sample_rate_hz':10,'calendar':{},'format':{},'channel':{},'power':{}}
    basis_source=FakeSource();screen=type('Screen',(),{'resident_estimate':512})()
    public_geometry={'sample_rate_hz':10,'search_window_samples':100}
    raw=np.zeros(100,dtype=np.complex128);valid=np.ones(100,dtype=np.uint8)


class WorkOnlyFixtureTests(unittest.TestCase):
    def test_nonzero_public_window_gets_real_prefix_and_correct_local_samples(self):
        class LaterSource(FakeSource):
            def slot_plan(self,index):
                plan=super().slot_plan(index)
                for key in ('receiver_window_first_sample','receiver_window_stop_sample',
                            'receiver_window_physical_origin_sample'):
                    plan[key]+=20000
                return plan
        prepared=Prepared();prepared.basis_source=LaterSource()
        prepared.raw=np.arange(20100,dtype=float).astype(np.complex128)
        prepared.valid=np.ones(20100,dtype=np.uint8)
        from triad_orchestration_cost import _WindowReceiptOnlyBackend
        original=_WindowReceiptOnlyBackend.receive
        seen=[]
        def receive(owner,values,valid):
            seen.append(values.copy())
            return original(owner,values,valid)
        with mock.patch.object(_WindowReceiptOnlyBackend,'receive',receive):
            result=_full_window_assembly(prepared)
        np.testing.assert_array_equal(seen[0],prepared.raw[20000:20100])
        self.assertEqual(result['actual_prefix_samples'],20000)
        self.assertEqual(result['physical_ingested_samples'],20100)
        self.assertEqual(result['represented_baseband_seconds'],2010.)
        self.assertEqual(result['exact_window_seconds'],10.)
        self.assertEqual(_push_units(prepared,1,chunk_samples=4)['status'],'receiving')
        prepared.observation_first_sample=20000
        with self.assertRaises(ValueError):_full_window_assembly(prepared)

    def test_chunk_ingest_stays_before_public_window_and_never_scores(self):
        prepared=Prepared()
        sample=_push_units(prepared,1,chunk_samples=4)
        self.assertEqual(sample['samples'],4)
        self.assertEqual(sample['status'],'receiving')
        self.assertGreater(sample['peak_overlap_buffer_bytes'],0)

    def test_recurring_assembly_ingests_gap_and_dispatches_the_whole_later_window(self):
        class GappedSource(FakeSource):
            def slot_plan(self, index):
                row = super().slot_plan(index)
                for key in ('receiver_window_first_sample', 'receiver_window_stop_sample',
                            'receiver_window_physical_origin_sample'):
                    row[key] += 85*index
                return row

        prepared = Prepared()
        prepared.basis_source = GappedSource()
        prepared.calendar = {'plans': [prepared.basis_source.slot_plan(i) for i in range(4)]}
        prepared.raw = np.arange(100, dtype=np.complex128)
        from triad_orchestration_cost import _WindowReceiptOnlyBackend
        original = _WindowReceiptOnlyBackend.receive
        seen = []
        def receive(owner, values, valid):
            seen.append(values.copy())
            return original(owner, values, valid)
        with mock.patch.object(_WindowReceiptOnlyBackend, 'receive', receive):
            result = _full_window_assembly(prepared, recurring_slot=2, resident_bank_bytes=512)
        np.testing.assert_array_equal(seen[0], prepared.raw)
        self.assertEqual(result['public_slot_index'], 2)
        self.assertEqual(result['native_window_first_sample'], 250)
        self.assertEqual(result['actual_prefix_samples'], 25)
        self.assertEqual(result['physical_ingested_samples'], 125)
        self.assertEqual(result['exact_window_samples'], 100)

    def test_full_public_window_runs_assembly_dispatch_but_not_success(self):
        prepared=Prepared()
        prepared.raw=np.arange(100,dtype=float).astype(np.complex128)
        prepared.valid=np.ones(100,dtype=np.uint8)
        prepared.memory_limit_bytes=8*1024**2
        with mock.patch('triad_orchestration_cost._public_plan',return_value=FakeSource().slot_plan(0)):
            result=_full_window_assembly(prepared)
        self.assertTrue(result['ingest_copy_assembly_dispatch_including_one_state_call'])
        self.assertFalse(result['complete_receiver_work_covered'])
        self.assertEqual(result['slot_receipts'],1)
        self.assertEqual(result['signal_receipt'].split()[0],'AMBIGUOUS')
        self.assertGreater(result['state_machine_elapsed_seconds_per_baseband_second'],0.)
        self.assertGreater(result['public_slot_planning_seconds_per_baseband_second'],0.)
        self.assertAlmostEqual(result['ratio_processing_seconds_per_baseband_second']-
            result['state_machine_elapsed_seconds_per_baseband_second']-
            result['public_slot_planning_seconds_per_baseband_second'],
            result['nonoverlap_stream_orchestration_seconds_per_baseband_second'])

    def test_decoder_adapter_checks_native_ack_and_only_returns_filtered_text(self):
        process=type('P',(),{'stdout':b'{"schema":"triad-short-codec-v1","operation":"decode","codec":"short-prefix","received_bits":3,"decoded_bytes":1,"text":"e"}'})()
        with mock.patch('triad_stream_receiver.subprocess.run',return_value=process) as run:
            result=native_short_decoder('/tmp/exporter')((0,0,1))
        self.assertEqual(result,{'decoded_bytes':1,'filtered_text':'e'})
        self.assertEqual(run.call_args.args[0],['/tmp/exporter','--short-decode','001'])

    def test_fox_work_fixture_needs_two_quiet_receipts_and_canonical_codec(self):
        prepared=Prepared()
        encoded=type('Encoded',(),{'stdout':b'{"bits":"0000000000000000000000"}'})()
        with mock.patch('subprocess.run',return_value=encoded), mock.patch(
                'triad_orchestration_cost._public_plan',side_effect=lambda source,index:FakeSource().slot_plan(index)):
            with mock.patch('triad_orchestration_cost.native_short_decoder',
                             return_value=lambda bits:{'decoded_bytes':3,'filtered_text':'fox'}):
                result=_message_state_fixture(prepared,family_size=1)
        self.assertTrue(result['work_only'])
        self.assertFalse(result.get('reception_success_established',False))
        self.assertEqual(result['physical_slots'],24)
        self.assertEqual(result['decoder_invocations'],9)
        self.assertIn('synthetic',result['scope'])

if __name__=='__main__':unittest.main()
