"""Full-window DSP, one-stream scheduling, and physical-absence contracts."""
import copy
import gc
import math
from pathlib import Path
import sys
import unittest
import weakref
from unittest import mock

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_cpu import configure_numerical_threads
from triad_dynamic_joint import _sample_cubic
from triad_receiver import CompleteBitReceiver
from triad_realtime_fft import FFTScreen
from test_triad_realtime_fft import fixture, waveform


class CompleteReceiverTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        configure_numerical_threads({'threads': 1, 'linear_algebra_threads': 1})

    def inputs(self, workers=1, *, window_origin=0, clock_bound=0., helper=False, conversion_if=0.):
        m, source, epochs = fixture()
        m.update(helper_enabled=helper, key_seed=1, public_epoch_seconds=1000)
        m['calendar']['physical_origin_sample'] = window_origin
        m['calendar']['quiet_aperture_first_sample'] = window_origin
        m['calendar']['quiet_aperture_stop_sample'] = window_origin+m['calendar']['capture_samples']
        m['calendar']['capture_lead_seconds'] -= window_origin/m['sample_rate_hz']
        m['calendar']['data_sample_offset'] += window_origin
        m['helper_plan'] = dict(low_hz=150., high_hz=160., auxiliary_hz=170.)
        m['format']['bit_seconds'] = source.shape[-1]/m['sample_rate_hz']
        m['channel'].update(deterministic_cfo_bound_hz=.4, spread_margin=1.,
            sample_clock_pair_ppm=clock_bound, nominal_conversion_if_hz=conversion_if)
        coarse_manifest = copy.deepcopy(m)
        coarse_manifest['power']['complex_noise_variance'] = 1.
        screen = FFTScreen(coarse_manifest, source, epochs,
            clock_ppm=(-clock_bound, 0., clock_bound) if clock_bound else (0.,),
            output_sample_rate=200, pass_edge_hz=60., stop_edge_hz=90.,
            precision='fp64', workers=1, correlation_method='auto')
        def basis_factory(_source, coordinates, taps, offset, timing, audio, **_kwargs):
            source_offset = _kwargs.get('source_offset_seconds', 0.)
            def provider(states, anchor, first, stop, origin, offsets):
                times = (origin+offsets)/m['sample_rate_hz']
                output = np.zeros((3, len(states), len(taps), stop-first), complex)
                for index, state in enumerate(states):
                    epoch, arrival, frequency, clock = coordinates[state.group]
                    scale = 1+clock*1e-6+state.rate_index*timing.rate_step_ppm*1e-6
                    displacement = state.delay_index*timing.delay_step_seconds
                    for bit in (0, 1):
                        for tap, (delay, doppler) in enumerate(taps):
                            position = ((times-arrival-delay-offset)*scale-source_offset+
                                        displacement)*m['sample_rate_hz']
                            output[bit+1, index, tap] = _sample_cubic(
                                source[epochs.index(epoch), bit], position)*np.exp(
                                2j*np.pi*(frequency+doppler)*times)
                return output
            provider.cache_report = lambda: {'method': 'deterministic-test-basis'}
            return provider
        receiver = CompleteBitReceiver(m, screen, None, workers=workers,
            memory_limit_bytes=256*1024**2, basis_factory=basis_factory,
            policy=dict(cache_bytes=0))
        return receiver, m, source, epochs

    def test_complete_unknown_bit_both_alternatives_and_same_stream_parallelism(self):
        for bit in (0, 1):
            answers = []
            for workers in (1, 4):
                receiver, m, source, _ = self.inputs(workers)
                rng = np.random.default_rng(6301+bit)
                raw = 20*waveform(m, source, 1, bit, .035, .2, 0.)
                raw += math.sqrt(.1/2)*(rng.normal(size=len(raw))+1j*rng.normal(size=len(raw)))
                with receiver:
                    result = receiver.receive(raw)
                self.assertEqual(result['status'], 'scored')
                self.assertEqual(result['slot_classification'], 'DATA')
                self.assertEqual(result['decision_bit'], bit)
                self.assertTrue(result['coverage']['complete_observation'])
                self.assertEqual(result['coverage']['consumed_samples'], len(raw))
                self.assertTrue(result['coverage']['coarse_evidence_used_only_for_selection'])
                self.assertTrue(result['coverage']['zero_h0_verified'])
                self.assertLess(result['coverage']['selected_grid_prior_mass'], 1.)
                self.assertTrue(result['operational']['passes_conditional_bound'])
                self.assertFalse(result['operational']['physical_null_qualified'])
                self.assertTrue(result['work']['native_templates']['representative_work'])
                self.assertEqual(result['tracking']['arithmetic']['precision'], 'fp64')
                self.assertEqual(result['tracking']['initial_lattice']['delay_half_width_seconds'],
                                 .5/receiver.screen.output_fs)
                self.assertEqual(set(result['stages']), {'helper', 'coarse',
                    'refinement_selection', 'fine_preparation', 'tracking', 'decision'})
                answers.append(result)
            self.assertEqual(answers[0]['acquisition']['coordinates'], answers[1]['acquisition']['coordinates'])
            np.testing.assert_allclose(answers[0]['bit_scores'], answers[1]['bit_scores'],
                                       rtol=1e-11, atol=1e-8)

    def test_quiet_is_scored_noise_not_failed_admission_or_invalid_data(self):
        receiver, m, _, _ = self.inputs(2)
        rng = np.random.default_rng(6401)
        raw = math.sqrt(.1/2)*(rng.normal(size=receiver.samples)+1j*rng.normal(size=receiver.samples))
        with receiver:
            quiet = receiver.receive(raw)
            self.assertEqual(quiet['slot_classification'], 'QUIET')
            self.assertTrue(quiet['coverage']['valid_noise_statistics'])
            self.assertTrue(quiet['quiet']['noise_consistent'])
            self.assertIsNone(quiet['decision_bit'])
            invalid = np.ones(receiver.samples, np.uint8)
            invalid[7] = 0
            failed = receiver.receive(raw, invalid)
            self.assertEqual(failed['slot_classification'], 'AMBIGUOUS')
            self.assertFalse(failed['coverage']['complete_observation'])
            receiver.screen.maximum_fft_products = 1
            work_limit = receiver.receive(raw)
            self.assertEqual(work_limit['slot_classification'], 'AMBIGUOUS')
            self.assertEqual(work_limit['status'], 'unresolved')

    def test_bounded_fine_probe_keeps_original_physical_source_clock(self):
        receiver, m, source, _ = self.inputs()
        raw = 20*waveform(m, source, 1, 1, 0., 0., 0.)
        with receiver:
            acquisition = receiver.acquire(raw)
            session = receiver.create_fine_session(acquisition['coordinates'],
                samples=80, physical_origin=880)
            session.push(0, raw[880:920])
            self.assertFalse(session.receiver.progress()['complete_observation'])
            session.push(40, raw[920:960])
            result = session.finish()
            self.assertTrue(result['complete_observation'])
            self.assertGreater(result['native_template_work']['energy'], 0.)
            self.assertEqual(result['consumed_samples'], 80)

    def test_fine_basis_and_phase_transport_share_conversion_if(self):
        receiver,_,_,epochs=self.inputs(conversion_if=1725.)
        coordinate=receiver.grid.snap((epochs[0],0.,0.,0.))
        with receiver, mock.patch.object(receiver,'basis_factory',wraps=receiver.basis_factory) as factory:
            fine=receiver.create_fine_session((coordinate,),samples=80)
            self.assertEqual(factory.call_args.args[5],1725.)
            self.assertEqual(fine.receiver.adaptive.audio_center_hz,1725.)
            self.assertEqual(fine.receiver.nominal.audio_center_hz,1725.)

    def test_long_duration_final_frequency_grid_preserves_coarse_resolution(self):
        duration=10_000
        times=np.arange(duration,dtype=float)
        source=np.array([[np.exp(2j*np.pi*.07*times),np.exp(2j*np.pi*.11*times)]])
        manifest=dict(sample_rate_hz=1,
            calendar=dict(capture_samples=duration+20,capture_lead_seconds=2.,
                data_sample_offset=1,arrival_prior_seconds=[-.1,.1]),
            channel=dict(spread_delay_ms=0.,spread_doppler_hz=0.,
                deterministic_cfo_bound_hz=.02,spread_margin=1.,
                sample_clock_pair_ppm=0.,nominal_conversion_if_hz=0.),
            format=dict(occupied_bandwidth_hz=.2,fine_chip_rate_hz=.1,
                bit_seconds=duration),power=dict(complex_noise_variance=1.),
            helper_enabled=False,helper_plan=dict(low_hz=.3,high_hz=.4,auxiliary_hz=.45))
        screen=FFTScreen(manifest,source,(0,),clock_ppm=(0.,),
            output_sample_rate=1,pass_edge_hz=.3,stop_edge_hz=.45,
            precision='fp64',workers=1)
        with CompleteBitReceiver(manifest,screen,None,memory_limit_bytes=64*1024**2) as receiver:
            step=screen.maximum_frequency_step
            self.assertEqual(receiver.grid.frequency_step,step)
            for frequency in (-.01999,-.00731,.00731,.01999):
                snapped=receiver.grid.snap((0,0.,frequency,0.))[2]
                self.assertLessEqual(abs(snapped-frequency),.5*step+1e-12)
            self.assertGreater(receiver.grid.counts[2],5)  # old .01Hz grid

    def test_complete_fine_routes_shared_fixed_affine_provider(self):
        receiver,_,_,epochs=self.inputs()
        original=receiver.basis_factory
        providers=[]
        def factory(*args,**kwargs):
            provider=original(*args,**kwargs)
            provider.initial_affine=mock.Mock(wraps=provider)
            providers.append(provider)
            return provider
        coordinate=receiver.grid.snap((epochs[0],0.,0.,0.))
        with receiver,mock.patch.object(receiver,'basis_factory',side_effect=factory):
            fine=receiver.create_fine_session((coordinate,),samples=80)
            fine.push(0,np.zeros(80,complex))
            result=fine.finish()
        self.assertTrue(result['complete_observation'])
        self.assertGreater(providers[0].initial_affine.call_count,0)
        self.assertGreater(result['native_template_work']['calls'],
                           providers[0].initial_affine.call_count)

    def test_fine_lifecycle_releases_lane_and_cache_without_cyclic_gc(self):
        if gc.isenabled():
            gc.disable()
            self.addCleanup(gc.enable)
        receiver,_,_,epochs=self.inputs()
        coordinate=receiver.grid.snap((epochs[0],0.,0.,0.))
        with receiver:
            fine=receiver.create_fine_session((coordinate,),samples=80)
            lane=weakref.ref(fine.receiver.nominal)
            provider=weakref.ref(fine.provider)
            fine.close()
            self.assertIsNone(lane())
            self.assertIsNone(provider())
            fine.close()  # Idempotent, including error-path cleanup.
            with self.assertRaisesRegex(RuntimeError,'closed'):
                fine.push(0,np.zeros(80,complex))
            fine=receiver.create_fine_session((coordinate,),samples=80)
            reference=weakref.ref(fine)
            lane=weakref.ref(fine.receiver.nominal)
            del fine
            self.assertIsNone(reference())
            self.assertIsNone(lane())

    def test_future_window_keeps_source_offset_outside_clock_scale(self):
        origin = 4_000_000
        receiver, m, source, epochs = self.inputs(window_origin=origin, clock_bound=100.)
        with receiver:
            coordinate = receiver.grid.snap((epochs[1], 0., 0., 50.))
            first, count = 640, 80
            fine = receiver.create_fine_session((coordinate,), samples=count,
                physical_origin=origin+first)
            # Select the zero residual-delay/rate state, independent of order.
            states = tuple(s for s in fine.forward.states if s.delay_index == 0 and s.rate_index == 0)
            result = fine.provider(states, 0., 0, count, origin+first, np.arange(count, dtype=float))
            physical = (origin+first+np.arange(count))/m['sample_rate_hz']
            source_offset = m['calendar']['data_sample_offset']/m['sample_rate_hz']
            expected = _sample_cubic(source[1, 1],
                ((physical-1.)*(1+50e-6)-source_offset)*m['sample_rate_hz'])
            np.testing.assert_allclose(result[2, 0, 0], expected, rtol=1e-8, atol=1e-8)
            self.assertGreater(float(np.vdot(expected, expected).real), 0.)
            wrong = _sample_cubic(source[1, 1],
                (physical-1.-source_offset)*(1+50e-6)*m['sample_rate_hz'])
            self.assertLess(float(np.vdot(wrong, wrong).real), 1e-20)

    def test_work_only_cap_probe_executes_cap_without_admitting_payload(self):
        receiver, m, source, _ = self.inputs(4)
        raw = 20*waveform(m, source, 1, 1, 0., 0., 0.)
        with receiver:
            acquired = receiver.acquire(raw)
            fine = receiver.create_fine_session(acquired['coordinates'], work_probe=True)
            fine.push(0, raw)
            result = fine.finish()
            self.assertTrue(result['complete_observation'])
            self.assertTrue(result['work_probe_only'])
            self.assertEqual(result['initial_lattice']['states'], 256)
            self.assertEqual(result['lanes']['initial_affine']['state_work_regime']['peak_pre_emission_states'], 256)
            self.assertTrue(result['lanes']['adaptive']['state_work_regime']['attained_proposal_cap'])
            self.assertGreater(result['work_counts']['hypothesis_update_tasks'], 0)
            self.assertGreater(result['public_work_ceiling']['candidate_block_updates_upper'], 0)
            classified = receiver.classify(result, acquired['coordinates'], all_input_valid=True)
            self.assertEqual(classified['slot_classification'], 'AMBIGUOUS')
            self.assertFalse(classified['coverage']['complete_observation'])

    def test_helper_transient_is_admitted_before_estimator_allocation(self):
        receiver, _, _, _ = self.inputs(helper=True)
        receiver.memory_limit_bytes = receiver.screen.resident_estimate+receiver.samples*17+1024
        with receiver, mock.patch('triad_receiver.estimate_weak_helper') as helper:
            result = receiver.receive(np.zeros(receiver.samples, complex))
        self.assertEqual(result['slot_classification'], 'AMBIGUOUS')
        self.assertEqual(result['reason'], 'complete_helper_transient_memory_limit')
        helper.assert_not_called()

    def test_only_explicit_noise_scale_may_differ_from_coarse_manifest(self):
        receiver, m, _, _ = self.inputs()
        receiver.close()
        m['calendar']['data_sample_offset'] += 1
        with self.assertRaisesRegex(ValueError, 'only noise normalization'):
            CompleteBitReceiver(m, receiver.screen, None, memory_limit_bytes=256*1024**2)

    def test_broad_prior_partial_tiles_never_supply_coordinates_or_quiet(self):
        receiver, m, source, _ = self.inputs(4)
        raw = 20*waveform(m, source, 1, 1, .035, .2, 0.)
        with receiver:
            receiver.screen.maximum_frequency_points = 2
            partial = receiver.acquire(raw, coarse_unit_limit=0)
            self.assertEqual(partial['status'], 'partial_acquisition')
            self.assertEqual(partial['coordinates'], ())
            session = partial['coarse_session']
            try:
                self.assertFalse(session.complete)
                self.assertEqual(session.finish()['candidates'], [])
                session.step(1)
                self.assertEqual(session.finish()['candidates'], [])
            finally:
                session.close()
            complete = receiver.receive(raw)
            self.assertEqual(complete['status'], 'scored')
            self.assertEqual(complete['slot_classification'], 'DATA')
            self.assertEqual(complete['decision_bit'], 1)
            self.assertTrue(complete['acquisition']['coarse']['frequency_tiles']['complete_public_search'])

    def test_partial_executor_submission_joins_already_started_coarse_jobs(self):
        receiver, _, _, _ = self.inputs(4)
        future = mock.Mock()
        future.result.return_value = {}
        executor = mock.Mock()
        executor.submit.side_effect = [future, RuntimeError('executor stopped')]
        raw = np.zeros(receiver.samples, complex)
        with receiver:
            values, valid = receiver._input(raw, None)
            with mock.patch.object(receiver, 'executor', executor):
                with self.assertRaisesRegex(RuntimeError, 'executor stopped'):
                    receiver._coarse(values, valid, [[-.4, .4]])
        future.result.assert_called_once_with()

    def test_fixed_affine_atoms_are_invariant_to_selected_subset(self):
        receiver, m, source, epochs = self.inputs(4)
        raw = 5*waveform(m, source, 1, 1, 0., 0., 0.)
        first = receiver.grid.snap((epochs[0], 0., 0., 0.))
        second = receiver.grid.snap((epochs[1], 0., 0., 0.))
        values = []
        with receiver:
            receiver._input(raw, None)
            for coordinates in ((first,), (second,), (first, second)):
                fine = receiver.create_fine_session(coordinates)
                fine.push(0, raw)
                tracked = fine.finish()
                values.append(np.asarray(tracked['lanes']['initial_affine']['bit_scores']))
        expected = np.logaddexp(values[0], values[1])-math.log(2.)
        np.testing.assert_allclose(values[2], expected, rtol=1e-11, atol=1e-8)

    def test_adaptive_evidence_neither_admits_nor_changes_affine_bit(self):
        receiver, _, _, epochs = self.inputs()
        coordinates = (receiver.grid.snap((epochs[0], 0., 0., 0.)),)
        tracked = dict(complete_observation=True, processed_rows=receiver.samples,
            consumed_samples=receiver.samples, bit_scores=[1e9, -1e9],
            native_template_work=dict(calls=1, representative_work=True),
            lanes=dict(initial_affine=dict(bit_scores=[-5., 120.],
                observed_projection_modes=20, projected_energy=1000.,
                quiet_observed_projection_modes=20, quiet_projected_energy=1000.)))
        with receiver:
            result = receiver.classify(tracked, coordinates, all_input_valid=True)
            self.assertEqual(result['slot_classification'], 'DATA')
            self.assertEqual(result['decision_bit'], 1)
            self.assertEqual(result['operational']['evidence_source'], 'fixed_initial_affine')
            tracked['lanes']['initial_affine']['bit_scores'] = [-50., -50.]
            result = receiver.classify(tracked, coordinates, all_input_valid=True)
            self.assertEqual(result['slot_classification'], 'AMBIGUOUS')
            self.assertIsNone(result['decision_bit'])
            self.assertFalse(result['operational']['passes_conditional_bound'])

    def test_invalid_input_does_not_reach_templates(self):
        receiver, _, _, _ = self.inputs()
        with receiver:
            with self.assertRaises(ValueError):
                receiver.receive(np.zeros(receiver.samples-1, complex))
            with self.assertRaises(ValueError):
                receiver.receive(np.zeros(receiver.samples, float))

    def test_missing_public_quiet_aperture_cannot_admit_absence(self):
        receiver,m,_,epochs=self.inputs()
        receiver.close()
        for name in ('quiet_aperture_first_sample','quiet_aperture_stop_sample'):
            m['calendar'].pop(name)
        with CompleteBitReceiver(m,receiver.screen,None,memory_limit_bytes=256*1024**2) as bare:
            coordinates=(bare.grid.snap((epochs[0],0.,0.,0.)),)
            tracked=dict(complete_observation=True,processed_rows=bare.samples,
                consumed_samples=bare.samples,native_template_work=dict(calls=1,representative_work=True),
                lanes=dict(initial_affine=dict(bit_scores=[-50.,-50.],
                    quiet_observed_projection_modes=20,quiet_projected_energy=10.)))
            result=bare.classify(tracked,coordinates,all_input_valid=True)
            self.assertEqual(result['slot_classification'],'AMBIGUOUS')
            self.assertFalse(result['coverage']['valid_noise_statistics'])
            self.assertFalse(result['quiet']['public_aperture_declared'])


if __name__ == '__main__':
    unittest.main()
