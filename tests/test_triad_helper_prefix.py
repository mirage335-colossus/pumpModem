"""Boundaries of the optional, independently checked Helper-prefix transport.

These are deterministic transport and verification gates, not DSP throughput
or reception-probability tests.
"""
from copy import deepcopy
import hashlib
import io
import itertools
from pathlib import Path
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest.mock import MagicMock, patch

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import triad_helper_prefix as prefix
import triad_replay_packet as packet
import triad_replay_estimate as estimator
import triad_replay_cpu as replay_cpu
import triad_nonfine_calibration as calibration
from test_triad_replay_packet import acquisition, public, sample
import test_triad_replay_producer as producer_fixtures
import triad_replay_producer as producer
from triad_case_capacity import _work_digest
from triad_receiver import _public_metrics


def digest(data):
    return hashlib.sha256(data).hexdigest()


def fixture():
    fine_iq, fine_valid = sample()
    payload = fine_iq.tobytes() + fine_valid.tobytes()
    fine = packet.encode_packet(public(), acquisition(payload), payload)
    iq = np.asarray([1 + 2j, -3 + 4j], dtype='<c16').tobytes()
    valid = bytes([1, 0])
    attestation = dict(helper_prefix=dict(schema='triad-helper-prefix-v1',
        sample_count=2, sample_rate_hz=6000, origin_sample=0,
        iq_sha256=digest(iq), validity_sha256=digest(valid),
        bound_full_iq_sha256='7'*64, bound_full_validity_sha256='8'*64))
    return fine, iq, valid, attestation


class HelperEnvelopeTests(unittest.TestCase):
    def setUp(self):
        self.fine, self.iq, self.valid, self.attestation = fixture()
        self.envelope = prefix.encode_helper_envelope(
            self.fine, self.iq, self.valid, self.attestation)

    def decode(self, value=None, *, count=2, rate=6000, attestation=None):
        return prefix.decode_helper_envelope(self.envelope if value is None else value,
            expected_sample_count=count, expected_sample_rate_hz=rate,
            trusted_attestation=self.attestation if attestation is None else attestation)

    def test_round_trip_preserves_fine_packet_and_prefix(self):
        fine, iq, valid = self.decode()
        self.assertEqual(fine, self.fine)
        np.testing.assert_array_equal(np.asarray(iq), np.frombuffer(self.iq, dtype='<c16'))
        np.testing.assert_array_equal(np.asarray(valid), np.frombuffer(self.valid, dtype='u1'))

    def test_truncation_trailing_and_corruption_are_rejected(self):
        for data in (self.envelope[:-1], self.envelope + b'\0',
                     self.envelope[:1], self.envelope[:-1] + bytes([self.envelope[-1] ^ 1])):
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                self.decode(data)

    def test_public_geometry_and_independently_trusted_attestation_required(self):
        for kwargs in (dict(count=1), dict(count=3), dict(count=1_000_001),
                       dict(count=True), dict(rate=6001), dict(rate=0), dict(rate=True)):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                self.decode(**kwargs)
        for key, value in (('origin_sample', 1), ('sample_count', 3),
                ('sample_rate_hz', 5999), ('iq_sha256', '9'*64),
                ('validity_sha256', '9'*64), ('bound_full_iq_sha256', '9'*64),
                ('bound_full_validity_sha256', '9'*64), ('schema', 'unknown')):
            trusted = deepcopy(self.attestation)
            trusted['helper_prefix'][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.decode(attestation=trusted)

    def test_encoder_rejects_wrong_prefix_geometry_and_contents(self):
        for iq, valid in ((self.iq[:-1], self.valid), (self.iq, self.valid[:-1]),
                          (self.iq + b'\0', self.valid), (self.iq, self.valid + b'\0'),
                          (self.iq[:-1] + bytes([self.iq[-1] ^ 1]), self.valid),
                          (self.iq, bytes([1, 2]))):
            with self.subTest(iq_length=len(iq), valid=valid), self.assertRaises(ValueError):
                prefix.encode_helper_envelope(self.fine, iq, valid, self.attestation)


class HelperEstimatorGateTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.exporter = Path(self.tmp.name) / 'exporter'
        self.exporter.write_bytes(b'fixture executable identity')
        self.source_hash = 'a'*64
        self.case = dict(index=3, configuration_hash='b'*64, native_args=[])
        self.case_list = Path(self.tmp.name) / 'cases.md'
        self.case_list.write_bytes(b'fixture manual list')
        self.parsed = dict(source_sha256=digest(self.case_list.read_bytes()), source_path=self.case_list)
        self.signal = dict(acquisition=dict(receiver_workers=6))
        self.acquisition = dict(coordinates=[[0, 0., 0., 0.]])
        self.public = dict(origin=0)
        self.geometry = dict(memory_limit_bytes=4096, search_samples=2, sample_rate_hz=6000,
            public_geometry=dict(public_calendar=dict()))
        self.runtime = dict(status='available', identity={'test':'runtime'}, identity_sha256='d'*64,
            observation=dict(ended_ns=100, started_ns=50))
        self.replay = dict(schema=estimator.REPLAY_SCHEMA, status='measured_detached_fine',
            case_index=3, configuration_sha256=self.case['configuration_hash'],
            manual_list_sha256=self.parsed['source_sha256'],
            native_exporter_sha256=digest(self.exporter.read_bytes()),
            source_identity_sha256=self.source_hash, signal_work=self.signal,
            signal_work_sha256=estimator.canonical_digest(self.signal),
            public=self.public, acquisition=self.acquisition, workers=6, bank_reservation_bytes=100,
            public_calibration_geometry=self.geometry, identity_before=self.runtime)
        self.calibration = dict(measurement=dict(signal_work=deepcopy(self.signal),
            helper_prefix_anchor={'test': 'anchored'}))
        self.holdout = deepcopy(self.calibration)
        self.attestation = dict(public=deepcopy(self.public), acquisition=deepcopy(self.acquisition),
            signal_work=deepcopy(self.signal), signal_work_sha256=estimator.canonical_digest(self.signal))
        self.evidence = dict(envelope=b'bounded-envelope', attestation=self.attestation)
        self.verified = dict(schema='triad-local-helper-prefix-verification-v1', status='verified')

    def verify(self, **changes):
        args = dict(case=self.case, parsed=self.parsed, exporter=self.exporter,
            replay=self.replay, calibration=self.calibration, holdout=self.holdout,
            evidence=self.evidence)
        args.update(changes)
        return estimator._verify_local_helper_evidence(**args)

    def test_local_verifier_is_invoked_and_no_receipt_is_accepted_as_evidence(self):
        source = MagicMock()
        source.descriptor = dict(fixed_workspace_bytes=10, workspace_budget_bytes=10)
        grid = MagicMock()
        after = dict(self.runtime, observation=dict(started_ns=400, ended_ns=500))
        with patch.object(estimator, 'source_identity_sha256', return_value=self.source_hash), \
             patch.object(estimator, 'validate_signal_acquisition_work', return_value=True), \
             patch.object(estimator, 'machine_resources', return_value={'memory_budget_bytes':4096}), \
             patch('triad_case_suite.native_plan', return_value={}), \
             patch('triad_case_replay._clean_options', return_value=['exporter']), \
             patch('triad_continuous.NativeStream', return_value=source), \
             patch('triad_calibration_identity.capture_calibration_identity',
                   side_effect=itertools.cycle([self.runtime, after])), \
             patch('triad_calibration_identity.compare_calibration_identities', return_value={'status':'matched'}) as comparison, \
             patch('triad_replay_cpu.replay_geometry', return_value=({}, grid, None, None, self.geometry)), \
             patch('triad_replay_producer.acquisition_procedure_sha256', return_value='procedure'), \
             patch('triad_replay_producer.replay_public_metadata', return_value=self.public), \
             patch.object(prefix, 'helper_prefix_sample_count', return_value=2), \
             patch.object(prefix, 'helper_envelope_bound', return_value=1000), \
             patch.object(prefix, 'helper_verification_workspace', return_value=1000) as workspace, \
             patch('time.monotonic_ns', side_effect=itertools.cycle([200, 300])) as clock, \
             patch.object(prefix, 'verify_helper_prefix', return_value=(
                 dict(acquisition=self.acquisition), self.verified)) as verify:
            self.assertEqual(self.verify(), self.verified)
            verify.assert_called_once()
            self.assertEqual(verify.call_args.kwargs['local_prefix_anchor'],
                self.calibration['measurement']['helper_prefix_anchor'])
            with self.assertRaises(ValueError):
                self.verify(evidence=self.verified)
            with self.assertRaises(ValueError):
                self.verify(evidence=dict(self.evidence, verification=self.verified))
            verify.return_value = (dict(acquisition=self.acquisition), {})
            with self.assertRaises(ValueError):
                self.verify()
            verify.side_effect = ValueError('canonical verification failed')
            with self.assertRaises(ValueError):
                self.verify()
            verify.side_effect = None
            verify.return_value = (dict(acquisition=self.acquisition), self.verified)
            comparison.return_value = {'status':'unresolved'}
            with self.assertRaisesRegex(ValueError, 'identity or work interval'):
                self.verify()
            comparison.return_value = {'status':'matched'}
            clock.side_effect = itertools.cycle([200, 500])
            with self.assertRaisesRegex(ValueError, 'identity or work interval'):
                self.verify()
            clock.side_effect = itertools.cycle([200, 300])
            workspace.return_value = 5000
            with self.assertRaises(MemoryError):
                self.verify()
            workspace.return_value = 1000
            oversized = dict(self.replay, bank_reservation_bytes=4096)
            with self.assertRaises(MemoryError):
                self.verify(replay=oversized)

    def test_mismatched_identities_fit_anchor_and_original_work_are_rejected(self):
        with patch.object(estimator, 'source_identity_sha256', return_value=self.source_hash), \
             patch.object(estimator, 'validate_signal_acquisition_work', return_value=True):
            for key, value in (('configuration_sha256','9'*64), ('manual_list_sha256','9'*64),
                    ('source_identity_sha256','9'*64), ('native_exporter_sha256','9'*64),
                    ('signal_work_sha256','9'*64)):
                changed = deepcopy(self.replay)
                changed[key] = value
                with self.subTest(key=key), self.assertRaises(ValueError):
                    self.verify(replay=changed)
            for key in ('public', 'acquisition', 'signal_work', 'signal_work_sha256'):
                changed = deepcopy(self.evidence)
                changed['attestation'][key] = None
                with self.subTest(attestation=key), self.assertRaises(ValueError):
                    self.verify(evidence=changed)
            changed = deepcopy(self.holdout)
            changed['measurement']['signal_work']['acquisition']['receiver_workers'] = 12
            with self.assertRaises(ValueError):
                self.verify(holdout=changed)
            changed = deepcopy(self.calibration)
            del changed['measurement']['helper_prefix_anchor']
            with self.assertRaises(ValueError):
                self.verify(calibration=changed)

    def test_estimator_requires_matching_replay_and_holdout_verification(self):
        signal = dict(sample_count=2, fine_slice_sha256='e'*64,
            acquisition=dict(receiver_workers=6, coordinates=[[0, 0., 0., 0.]],
                             helper_result_sha256='f'*64))
        local = deepcopy(signal)
        local['acquisition']['helper_result_sha256'] = '1'*64
        replay = deepcopy(self.replay)
        replay.update(signal_work=signal, signal_work_sha256=estimator.canonical_digest(signal),
            acquisition=dict(input_sha256='e'*64, coordinates=signal['acquisition']['coordinates']),
            identity_after={}, work_started_ns=200, work_ended_ns=300,
            fine=dict(fine_component={}), local_helper_prefix_verification=self.verified)
        geometry = replay['public_calibration_geometry']
        geometry.update(unit_samples=1)
        measurement = dict(signal_work=local, inference_allocation_mode='pilot_selected',
            nominal_acquisition=dict(samples=[{}, {}, {}], actual_work_match_count=3),
            nominal_service=dict(components={}), selected_workers=6, inference_workers=6)
        fit = dict(case_index=3, measurement=deepcopy(measurement))
        holdout = dict(case_index=3, measurement=deepcopy(measurement),
            local_helper_predeclaration=self.verified)
        parsed = dict(self.parsed, cases=[self.case])
        match = dict(status='locally_matched_unqualified', resident_fine={},
            resident_bank=dict(work=dict(fft_workspace_bytes=10)),
            workspace=dict(status='within_budget', observed_peak_bytes=10, conservative_peak_bytes=20),
            components=dict(nominal_service={}))
        combined = dict(complete_work_covered=True, all_component_models_supported=True,
            ratio_processing_seconds_per_baseband_second=.5,
            ratio_processing_seconds_per_baseband_second_interval=[.4,.6])
        with patch.object(estimator, 'source_identity_sha256', return_value=self.source_hash), \
             patch.object(estimator, 'measurement_policy', return_value={}), \
             patch.object(estimator, '_independent_work', return_value=True), \
             patch.object(estimator, 'validate_holdout_declaration', return_value=True), \
             patch.object(estimator, 'machine_resources', return_value={'memory_budget_bytes':4096}), \
             patch.object(estimator, '_current_runtime_matches', return_value=True), \
             patch.object(estimator, 'compare_nonfine_calibration', return_value=match), \
             patch.object(estimator, 'validate_signal_acquisition_work', return_value=True), \
             patch.object(estimator, 'combine_components', return_value=combined), \
             patch.object(estimator, '_same_fine_work', return_value=True), \
             patch.object(estimator, '_qualified_nominal', return_value={
                 'status':'validated_conditional_nominal_service', 'holdout':{}}), \
             patch.object(estimator, '_verify_local_helper_evidence', return_value=self.verified) as verify:
            def assess(**changes):
                args = dict(case=self.case, parsed=parsed, exporter=self.exporter, replay=replay,
                    calibration=fit, holdout=holdout, helper_evidence=self.evidence)
                args.update(changes)
                return estimator.assess_nominal_replay(**args)
            self.assertEqual(assess(helper_evidence=None)['reason'], 'calibration_complete_actual_work_unmatched')
            self.assertEqual(verify.call_count, 0)
            for key in ('replay', 'holdout'):
                modified = deepcopy(replay if key == 'replay' else holdout)
                modified.pop('local_helper_prefix_verification' if key == 'replay'
                             else 'local_helper_predeclaration')
                self.assertEqual(assess(**{key:modified})['reason'], 'local_helper_prefix_predeclaration_unmatched')
            verify.side_effect = ValueError('local recomputation failed')
            self.assertEqual(assess()['reason'], 'local_helper_prefix_verification_failed')
            verify.side_effect = None
            result = assess()
            self.assertEqual(result['status'], 'validated_conditional_nominal_service')
            self.assertEqual(result['local_helper_prefix_verification'], self.verified)
            # When all work already matches, the legacy exact path never calls
            # or relies on supplemental Helper evidence.
            exact_fit, exact_holdout = deepcopy(fit), deepcopy(holdout)
            exact_fit['measurement']['signal_work'] = signal
            exact_holdout['measurement']['signal_work'] = signal
            verify.reset_mock()
            self.assertEqual(assess(calibration=exact_fit, holdout=exact_holdout,
                                    helper_evidence=None)['status'], 'validated_conditional_nominal_service')
            verify.assert_not_called()
            self.assertEqual(assess(calibration=exact_fit, holdout=exact_holdout,
                                    helper_evidence=self.evidence)['status'], 'validated_conditional_nominal_service')
            verify.assert_called_once()


class LocalHelperVerificationTests(unittest.TestCase):
    def setUp(self):
        self.fine, self.iq, self.valid, self.attestation = fixture()
        self.anchor = deepcopy(self.attestation['helper_prefix'])
        self.remote = dict(sample_count=2, full_iq_sha256='7'*64,
            full_validity_sha256='8'*64, fine_slice_sha256='4'*64,
            acquisition=dict(helper_result_sha256='5'*64, coordinates=[[0, 0., 0., 0.]],
                             receiver_workers=6, helper_reason='same-search'))
        self.local = deepcopy(self.remote)
        self.local['acquisition']['helper_result_sha256'] = '6'*64
        self.attestation.update(signal_work=self.remote,
            signal_work_sha256=estimator.canonical_digest(self.remote))
        self.envelope = prefix.encode_helper_envelope(self.fine, self.iq, self.valid, self.attestation)
        self.manifest = dict(helper_enabled=True, sample_rate_hz=6000,
            calendar=dict(capture_lead_seconds=0., data_sample_offset=0, capture_samples=2))
        self.grid = MagicMock(arrival_upper=0., frequency_bound=0.)
        self.decoded = dict(acquisition=dict(input_sha256='4'*64, coordinates=[[0, 0., 0., 0.]]))

    def verify(self, **overrides):
        args = dict(envelope=self.envelope, expected_public=public(), validate_grid=lambda _: True,
            trusted_attestation=self.attestation, local_signal_work=self.local,
            local_prefix_anchor=self.anchor, manifest=self.manifest, grid=self.grid,
            search_samples=2, memory_limit_bytes=20_000_000, runtime_identity={'fixture':'runtime'})
        args.update(overrides)
        return prefix.verify_helper_prefix(**args)

    def test_recomputes_local_helper_and_preserves_remote_attestation(self):
        before = deepcopy(self.attestation)
        with patch('triad_case_capacity.validate_signal_acquisition_work', return_value=True), \
             patch('triad_case_replay.helper_workspace_estimate', return_value=1024), \
             patch.object(prefix, 'decode_packet', return_value=self.decoded), \
             patch('triad_weak_helper.estimate_weak_helper', return_value={}) as estimate, \
             patch('triad_receiver._public_metrics', return_value={}), \
             patch('triad_case_capacity._work_digest', return_value='6'*64):
            decoded, receipt = self.verify()
            self.assertEqual(decoded, self.decoded)
            self.assertEqual(receipt['remote_helper_sha256'], '5'*64)
            self.assertEqual(receipt['local_helper_sha256'], '6'*64)
            self.assertEqual(receipt['status'], 'verified')
            estimate.assert_called_once()
            self.assertEqual(estimate.call_args.args[1]['calendar']['capture_samples'], 2)
            self.assertEqual(self.attestation, before)
            for change in ('full_iq_sha256', 'full_validity_sha256', 'fine_slice_sha256'):
                altered = deepcopy(self.local)
                altered[change] = '9'*64
                with self.subTest(change=change), self.assertRaises(ValueError):
                    self.verify(local_signal_work=altered)
            altered = deepcopy(self.local)
            altered['acquisition']['helper_reason'] = 'changed-search'
            with self.assertRaises(ValueError):
                self.verify(local_signal_work=altered)
            altered = deepcopy(self.anchor)
            altered['origin_sample'] = 1
            with self.assertRaises(ValueError):
                self.verify(local_prefix_anchor=altered)
            altered = deepcopy(self.anchor)
            altered['iq_sha256'] = '9'*64
            with self.assertRaises(ValueError):
                self.verify(local_prefix_anchor=altered)
            disabled = dict(self.manifest, helper_enabled=False)
            with self.assertRaises(ValueError):
                self.verify(manifest=disabled)
            with self.assertRaises(MemoryError):
                self.verify(memory_limit_bytes=1)
            with self.assertRaises(ValueError):
                self.verify(envelope=self.envelope[:-1])
            with patch('triad_case_capacity._work_digest', return_value='9'*64):
                with self.assertRaises(ValueError):
                    self.verify()

    def test_prefix_extent_is_derived_and_bounded(self):
        self.assertEqual(prefix.helper_prefix_sample_count(self.manifest, self.grid, 2), 2)
        for count in (0, -1, True):
            with self.subTest(count=count), self.assertRaises(ValueError):
                prefix.helper_prefix_sample_count(self.manifest, self.grid, count)
        huge = deepcopy(self.manifest)
        huge['calendar']['capture_lead_seconds'] = 1000
        with self.assertRaises(ValueError):
            prefix.helper_prefix_sample_count(huge, self.grid, 1_000_001)
        nonfinite = deepcopy(self.manifest)
        nonfinite['calendar']['capture_lead_seconds'] = float('nan')
        with self.assertRaises(ValueError):
            prefix.helper_prefix_sample_count(nonfinite, self.grid, 2)

    def test_snapshot_binds_prefix_to_immutable_complete_observation(self):
        receiver = MagicMock()
        receiver.manifest = deepcopy(self.manifest)
        receiver.grid = self.grid
        receiver.samples = 2
        receiver.sample_rate = 6000
        receiver.capture_origin = 0
        receiver.helper_peak_bytes = 100
        prepared = MagicMock()
        prepared.memory_limit_bytes = 4096
        prepared.observation_first_sample = 0
        prepared.raw = np.frombuffer(self.iq, dtype='<c16').copy()
        prepared.valid = np.frombuffer(self.valid, dtype='u1').copy()
        signal = dict(sample_count=2, full_iq_sha256=digest(self.iq),
            full_validity_sha256=digest(self.valid))
        with patch('triad_case_capacity.validate_signal_acquisition_work', return_value=True), \
             patch('triad_case_replay.helper_workspace_estimate', return_value=1):
            iq, valid, anchor = prefix.snapshot_helper_prefix(prepared, receiver, signal)
            self.assertEqual((iq, valid), (self.iq, self.valid))
            self.assertEqual(anchor['bound_full_iq_sha256'], signal['full_iq_sha256'])
            self.assertEqual(anchor['bound_full_validity_sha256'], signal['full_validity_sha256'])
            prepared.observation_first_sample = 1
            with self.assertRaises(ValueError):
                prefix.snapshot_helper_prefix(prepared, receiver, signal)
            prepared.observation_first_sample = 0
            prepared.memory_limit_bytes = 110
            with self.assertRaises(MemoryError):
                prefix.snapshot_helper_prefix(prepared, receiver, signal)
            prepared.memory_limit_bytes = 4096
            receiver.capture_origin = 1
            with self.assertRaises(ValueError):
                prefix.snapshot_helper_prefix(prepared, receiver, signal)
            receiver.capture_origin = 0
            prepared.raw[0] += 1
            with self.assertRaises(ValueError):
                prefix.snapshot_helper_prefix(prepared, receiver, signal)
            prepared.raw = np.frombuffer(self.iq, dtype='<c16').copy()
            prepared.valid[0] = 2
            with self.assertRaises(ValueError):
                prefix.snapshot_helper_prefix(prepared, receiver, signal)

    def test_real_packet_and_work_validation_before_local_helper_comparison(self):
        prepared, case = producer_fixtures.fixture()
        receiver = producer_fixtures.Receiver()
        with patch.object(producer, 'public_screen_geometry', return_value=dict(
                grid=receiver.grid, output_fs=100)):
            fine, expected_public, acquisition_receipt = producer.make_packet(prepared, receiver,
                case=case, list_sha256='b'*64, exporter_sha256='c'*64, source_hash='d'*64)
        iq = prepared.raw[:6000].astype('<c16').tobytes()
        valid = prepared.valid[:6000].tobytes()
        helper_hash = _work_digest(_public_metrics({'test': 1}))
        acquisition_work = dict(status='acquired', complete_public_search=True, work_only=False,
            receiver_workers=2, helper_status='narrowed', helper_reason='test', helper_result_sha256='f'*64,
            coarse_reason='complete_public_workspace_search',
            coarse_candidates=[dict(bit=bit, epoch=0, arrival=0., frequency=0., clock_ppm=0., rank=1)
                for bit in (0, 1)],
            coarse_work=dict(workspaces=2, workers=2, frequency_points=2, arrival_points=10,
                fft_products=4, correlation_method='frequency'),
            searched_frequency_intervals_hz=[[0.,0.]], refinement_candidates=1,
            coordinates=acquisition_receipt['coordinates'])
        remote = dict(schema='triad-complete-public-signal-work-v1', sample_count=60000,
            full_iq_sha256=digest(prepared.raw.tobytes()), full_validity_sha256=digest(prepared.valid.tobytes()),
            fine_slice_sha256=acquisition_receipt['input_sha256'], acquisition=acquisition_work)
        local = deepcopy(remote)
        local['acquisition']['helper_result_sha256'] = helper_hash
        anchor = dict(schema=prefix.PREFIX_SCHEMA, sample_count=6000, sample_rate_hz=6000,
            origin_sample=0, iq_sha256=digest(iq), validity_sha256=digest(valid),
            bound_full_iq_sha256=remote['full_iq_sha256'], bound_full_validity_sha256=remote['full_validity_sha256'])
        attestation = dict(schema=producer.ATTESTATION_SCHEMA, status='complete_public_acquisition',
            source_unchanged=True, public=expected_public, acquisition=acquisition_receipt,
            signal_work=remote, signal_work_sha256=producer.canonical_digest(remote), helper_prefix=anchor)
        envelope = prefix.encode_helper_envelope(fine, iq, valid, attestation)
        manifest = dict(helper_enabled=True, sample_rate_hz=6000,
            calendar=dict(capture_lead_seconds=0., data_sample_offset=0, capture_samples=60000))
        grid = MagicMock(arrival_upper=0., frequency_bound=0.)
        kwargs = dict(expected_public=expected_public,
            validate_grid=lambda coordinates: coordinates == ((0, 0., 0., 0.),),
            trusted_attestation=attestation, local_signal_work=local, local_prefix_anchor=anchor,
            manifest=manifest, grid=grid, search_samples=60000,
            memory_limit_bytes=512*1024**2, runtime_identity={'fixture':'runtime'})
        with patch('triad_weak_helper.estimate_weak_helper', return_value={'test':1}) as helper:
            decoded, receipt = prefix.verify_helper_prefix(envelope, **kwargs)
            self.assertEqual(decoded['acquisition'], acquisition_receipt)
            self.assertEqual(receipt['local_helper_sha256'], helper_hash)
            helper.assert_called_once()
            tampered = bytearray(envelope)
            tampered[-1] ^= 1
            with self.assertRaises(ValueError):
                prefix.verify_helper_prefix(bytes(tampered), **kwargs)
            changed = deepcopy(local)
            changed['acquisition']['coarse_work']['fft_products'] += 1
            with self.assertRaises(ValueError):
                prefix.verify_helper_prefix(envelope, **dict(kwargs, local_signal_work=changed))


class HelperHoldoutCliTests(unittest.TestCase):
    def test_missing_or_unpaired_helper_evidence_never_starts_native_work(self):
        base = ['--case-index', '3', '--exporter', '/unused', '--output', '/unused']
        scenarios = [
            ['--helper-envelope', '/unused'],
            ['--helper-attestation', '/unused'],
            ['--helper-envelope', '/unused', '--helper-attestation', '/unused'],
            ['--holdout-fit', '/unused', '--helper-envelope', '/unused', '--helper-attestation', '/unused'],
            ['--holdout-fit', '/unused', '--holdout-replay', '/unused', '--helper-envelope', '/unused'],
        ]
        for options in scenarios:
            with self.subTest(options=options), redirect_stdout(io.StringIO()), \
                 patch('triad_continuous.NativeStream', side_effect=AssertionError('must fail before native work')):
                self.assertEqual(calibration.main(base + options), 2)

    def test_actual_holdout_mismatch_cannot_reuse_predeclaration(self):
        fit = dict(full_iq_sha256='a'*64, acquisition=dict(helper_result_sha256='b'*64))
        verification = dict(schema=prefix.VERIFICATION_SCHEMA, status='verified')
        good = dict(status='recorded_unqualified', measurement=dict(signal_work=deepcopy(fit)))
        actual = calibration._record_helper_predeclaration(good, fit, verification)
        self.assertEqual(actual['status'], 'recorded_unqualified')
        self.assertEqual(actual['local_helper_predeclaration'], verification)
        changed = deepcopy(good)
        changed['measurement']['signal_work']['full_iq_sha256'] = 'c'*64
        actual = calibration._record_helper_predeclaration(changed, fit, verification)
        self.assertEqual(actual['status'], 'unresolved')
        self.assertEqual(actual['reason'], 'held_out_local_signal_work_differs')
        no_verification = dict(status='recorded_unqualified', measurement=dict(signal_work={}))
        self.assertEqual(calibration._record_helper_predeclaration(no_verification, fit, None), no_verification)


class HelperReplayFitTests(unittest.TestCase):
    def test_stale_fit_is_rejected_before_packet_read_or_fine_processing(self):
        with tempfile.TemporaryDirectory() as root:
            executable = Path(root) / 'exporter'
            executable.write_bytes(b'fixture exporter')
            case = dict(index=3, configuration_hash='a'*64, native_args=[])
            parsed = dict(source_sha256='b'*64, cases=[case])
            public_geometry = dict(search_window_samples=36001, sample_rate_hz=6000,
                public_calendar={})
            geometry = dict(public_geometry=public_geometry, search_samples=36001)
            fit = dict(status='recorded_unqualified', case_index=3, public_geometry=geometry,
                measurement=dict(inference_workers=6))
            source = MagicMock()
            source.descriptor = dict(fixed_workspace_bytes=1, workspace_budget_bytes=1)
            grid = MagicMock()
            scenarios = [dict(status='unresolved'), dict(case_index=4),
                         dict(public_geometry={}), dict(measurement=dict(inference_workers=4))]
            with patch.object(replay_cpu, 'load_cases', return_value=parsed), \
                 patch.object(replay_cpu, 'select_cases', return_value=[case]), \
                 patch.object(replay_cpu, 'machine_resources', return_value={
                     'memory_budget_bytes':100_000_000, 'cpu':{'fixture':'cpu'}}), \
                 patch.object(replay_cpu, 'resolve_cpu_budget', return_value={'explicit_overcommit':False}), \
                 patch.object(replay_cpu, 'configure_numerical_threads'), \
                 patch.object(replay_cpu, 'source_identity_sha256', return_value='c'*64), \
                 patch.object(replay_cpu, 'native_plan', return_value={}), \
                 patch.object(replay_cpu, '_clean_options', return_value=['exporter']), \
                 patch.object(replay_cpu, 'NativeStream', return_value=source), \
                 patch.object(replay_cpu, 'replay_geometry', return_value=({}, grid, {}, 0, geometry)), \
                 patch.object(replay_cpu, 'acquisition_procedure_sha256', return_value='procedure'), \
                 patch.object(replay_cpu, 'replay_public_metadata', return_value={}), \
                 patch.object(replay_cpu, 'read_bounded_file', side_effect=AssertionError('packet was read')), \
                 patch('triad_nonfine_calibration.read_local_receipt') as read_fit:
                for changes in scenarios:
                    read_fit.return_value = dict(fit, **changes)
                    with self.subTest(changes=changes), self.assertRaisesRegex(ValueError, 'matching local fit'):
                        replay_cpu.measure_local_fine_replay(executable=executable, case_list='unused',
                            case_index=3, packet='unused', attestation={'diagnostics':{'acquisition_workers':6}},
                            acquisition_workers=6, workers=6, bank_reservation_bytes=5000,
                            memory_limit_bytes=100_000_000, helper_prefix_fit='unused')


if __name__ == '__main__':
    unittest.main()
