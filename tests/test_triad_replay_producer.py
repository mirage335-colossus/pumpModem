"""Producer integrity and explicit remote-trust boundaries."""
import copy
from contextlib import closing
import hashlib
import os
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import triad_replay_producer as producer
import triad_replay_packet as packet


class Grid:
    def validate_selected(self, coordinates):
        if coordinates != ((0, 0., 0., 0.),):
            raise ValueError('coordinates not in public grid')
    def identity(self):
        return 'e' * 64


class Receiver:
    grid = Grid()
    procedure_id = 'f' * 64
    policy = dict(producer._DEFAULT, operational_evidence='fixed_initial_affine')
    workers = 2
    def __init__(self, partial=False):
        self.partial = partial
        self.received = None
    def acquire(self, raw, valid, *, coarse_unit_limit):
        self.received = coarse_unit_limit
        if self.partial:
            return dict(status='partial_acquisition', coarse={}, coordinates=())
        return dict(status='acquired', coarse=dict(status='proposed'),
            coordinates=((0, 0., 0., 0.),))


def fixture():
    descriptor = dict(sample_rate_hz=6000, total_samples=60000,
        capture_lead_seconds=0., data_offset_samples=0, source_samples=12000)
    plan = dict(geometry=dict(source_calendar_origin_sample=0),
        format=dict(tile_seconds=2., elapsed_bit_seconds=2.), physics={})
    slot = dict(index=0, receiver_window_first_sample=0,
        receiver_window_stop_sample=60000, receiver_window_physical_origin_sample=0,
        receiver_window_samples=60000, pattern_epoch=9, symbol_ordinal=0)
    calendar = dict(plans=[dict(slot, index=i) for i in range(24)])
    values = np.arange(60000, dtype=np.float64)
    raw = (values*1e-6+1j*values*2e-6).astype(np.complex128)
    prepared = SimpleNamespace(plan=plan, bank=SimpleNamespace(descriptor=descriptor),
        calendar=calendar, raw=raw, valid=np.ones(60000, np.uint8))
    case = dict(index=7, configuration_hash='a'*64)
    return prepared, case


class ProducerTests(unittest.TestCase):
    def setUp(self):
        self.geometry_patch = patch.object(producer, 'public_screen_geometry',
            return_value=dict(grid=Grid(), output_fs=100))
        self.geometry_patch.start()
        self.addCleanup(self.geometry_patch.stop)

    def make(self, receiver=None):
        prepared, case = fixture()
        receiver = receiver or Receiver()
        blob, public, receipt = producer.make_packet(prepared, receiver, case=case,
            list_sha256='b'*64, exporter_sha256='c'*64, source_hash='d'*64)
        attestation = dict(schema=producer.ATTESTATION_SCHEMA, status='complete_public_acquisition',
            source_unchanged=True, public=public, acquisition=receipt)
        return prepared, case, receiver, blob, public, receipt, attestation

    def test_complete_search_snapshot_and_independent_authenticated_receipt(self):
        prepared, case, receiver, blob, public, receipt, attestation = self.make()
        self.assertEqual(receiver.received, 0)
        self.assertEqual(public['sample_count'], 36000)
        result = packet.decode_packet(blob, expected_public=public,
            validate_grid=lambda coords: coords == ((0, 0., 0., 0.),),
            validate_acquisition_receipt=lambda proof, metadata, digest:
                producer.verify_authenticated_receipt(proof, metadata, digest, attestation))
        start = public['origin_sample']
        np.testing.assert_array_equal(result['samples'], prepared.raw[start:start+36000])
        np.testing.assert_array_equal(result['validity'], prepared.valid[start:start+36000])
        self.assertEqual(hashlib.sha256(result['samples'].astype('<c16').tobytes()+
            result['validity'].tobytes()).hexdigest(), receipt['input_sha256'])
        prepared.raw[start] = 999
        self.assertNotEqual(result['samples'][0], prepared.raw[start])
        self.assertNotIn('transmitted_bit', public)
        self.assertNotIn('transmitted_bit', receipt)

    def test_partial_or_untrusted_acquisition_cannot_produce_packet(self):
        prepared, case = fixture()
        with self.assertRaisesRegex(ValueError, 'complete bounded'):
            producer.make_packet(prepared, Receiver(partial=True), case=case,
                list_sha256='b'*64, exporter_sha256='c'*64, source_hash='d'*64)
        prepared, case, _, blob, public, receipt, attestation = self.make()
        changes = [None, {}, dict(attestation, source_unchanged=False),
            dict(attestation, public=dict(public, case_index=1)),
            dict(attestation, acquisition=dict(receipt, input_sha256='0'*64))]
        for bad in changes:
            with self.subTest(attestation=bad), self.assertRaises(packet.ReplayPacketError):
                packet.decode_packet(blob, expected_public=public, validate_grid=lambda _: True,
                    validate_acquisition_receipt=lambda proof, meta, digest:
                        producer.verify_authenticated_receipt(proof, meta, digest, bad))
        forged = copy.deepcopy(attestation)
        forged['acquisition']['receipt_sha256'] = '0'*64
        self.assertFalse(producer.verify_authenticated_receipt(forged['acquisition'], public,
            forged['acquisition']['input_sha256'], forged))

    def test_acquisition_identity_binds_actual_policy_worker_and_source(self):
        prepared, case = fixture()
        kwargs = dict(case=case, exporter_sha256='c'*64, source_identity_sha256='d'*64,
            plan=prepared.plan, descriptor=prepared.bank.descriptor, calendar=prepared.calendar,
            grid=Grid(), workers=2)
        baseline = producer.acquisition_procedure_sha256(**kwargs)
        for key, changed in (('workers', 3), ('source_identity_sha256', '0'*64),
                             ('exporter_sha256', '0'*64)):
            self.assertNotEqual(baseline, producer.acquisition_procedure_sha256(**dict(kwargs, **{key:changed})))
        with self.assertRaises(ValueError):
            producer.acquisition_procedure_sha256(**dict(kwargs, workers=True))
        receiver = Receiver(); receiver.policy = dict(receiver.policy, top_per_bit=1)
        with self.assertRaisesRegex(ValueError, 'canonical default'):
            producer.make_packet(prepared, receiver, case=case,
                list_sha256='b'*64, exporter_sha256='c'*64, source_hash='d'*64)

    def test_nonzero_slot_start_does_not_rebase_calibrated_capture(self):
        prepared, case = fixture()
        slot = prepared.calendar['plans'][0]
        slot.update(receiver_window_first_sample=9000, receiver_window_physical_origin_sample=9000,
            receiver_window_samples=51000)
        blob, public, receipt = producer.make_packet(prepared, Receiver(), case=case,
            list_sha256='b'*64, exporter_sha256='c'*64, source_hash='d'*64)
        self.assertEqual((public['window_first_sample'],public['window_stop_sample']),(0,60000))
        self.assertEqual(public['calendar_sha256'],producer.canonical_digest(prepared.calendar))
        result = packet.decode_packet(blob, expected_public=public,
            validate_grid=lambda _:True,validate_acquisition_receipt=lambda *args:True)
        at = public['origin_sample']
        np.testing.assert_array_equal(result['samples'],prepared.raw[at:at+36000])
        for change in (dict(receiver_window_first_sample=-1),dict(receiver_window_stop_sample=60001),
                       dict(receiver_window_samples=51001),dict(receiver_window_physical_origin_sample=0)):
            with self.subTest(change=change), patch.dict(slot,change), self.assertRaisesRegex(
                    ValueError,'slot zero window is inconsistent'):
                producer.make_packet(prepared,Receiver(),case=case,list_sha256='b'*64,
                    exporter_sha256='c'*64,source_hash='d'*64)

    def test_local_metadata_rederives_geometry_and_rejects_coordinate_change(self):
        prepared, case, receiver, _, public, receipt, _ = self.make()
        kw = dict(case=case, list_sha256='b'*64, exporter_sha256='c'*64,
            source_identity_sha256='d'*64, plan=prepared.plan, descriptor=prepared.bank.descriptor,
            calendar=prepared.calendar, grid=receiver.grid,
            acquisition_procedure_sha256=receipt['procedure_sha256'],
            coordinates=receipt['coordinates'])
        self.assertEqual(producer.replay_public_metadata(**kw), public)
        kw['case'] = dict(case, configuration_hash='0'*64)
        self.assertNotEqual(producer.replay_public_metadata(**kw)['work_identity_sha256'], public['work_identity_sha256'])
        kw['coordinates'] = [[0, 50., 0., 0.]]
        with self.assertRaises(ValueError): producer.replay_public_metadata(**kw)


class NativeCalendarTests(unittest.TestCase):
    def test_native_groundwave_helper_preroll_preserves_capture_coordinates(self):
        exporter = os.getenv('DATAPUMP_TRIAD_REPLAY_EXPORTER')
        if exporter is None:
            self.skipTest('native exporter provided by the named triad test')
        from triad_case_replay import _clean_options
        from triad_case_suite import native_plan
        from triad_continuous import NativeStream
        from triad_replay_cpu import replay_geometry

        # Fixed public fixture independent of the manually edited case list.
        args = ['--case','hf','--helper','on','--encrypt','on','--helper-power-w','40',
            '--data-power-w','4','--path-loss-db','180','--timing-policy','broad',
            '--base-oscillator','crystal','--shift-oscillator','ic-7100','--spread-preset','hf-best',
            '--selective-perturbation','each-1','--margin','1','--fhss','off',
            '--burst-wait-tiles','0','--rate','1','--rf-hz','500000',
            '--added-symbols','1','--added-interleave','off']
        plan = native_plan(exporter,args)
        command = _clean_options(exporter,args,bit=0,seed=74021)
        with closing(NativeStream(exporter,command[1:],kind='channel')) as source:
            _,grid,_,_,calibration = replay_geometry(source,plan,4*2**30)
            calendar = calibration['public_geometry']['public_calendar']
            slot = calendar['plans'][0]
            self.assertGreater(slot['receiver_window_first_sample'],0)
            # Geometry test only. No acquisition result is manufactured or
            # admitted, and the native service consumes no observation IQ.
            coords = (grid.snap((0,0.,0.,0.)),)
            public = producer.replay_public_metadata(case=dict(index=3,configuration_hash='a'*64),
                list_sha256='b'*64,exporter_sha256='c'*64,source_identity_sha256='d'*64,
                plan=plan,descriptor=source.descriptor,calendar=calendar,grid=grid,
                acquisition_procedure_sha256='e'*64,coordinates=coords)
            self.assertEqual(public['window_first_sample'],0)
            self.assertEqual(public['window_stop_sample'],source.descriptor['total_samples'])
            self.assertEqual(public['calendar_sha256'],producer.canonical_digest(calendar))


if __name__ == '__main__':
    unittest.main()
