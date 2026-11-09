"""Cheap stream adapter guards; waveform/likelihood checks live separately."""
import io
import gc
import math
from pathlib import Path
import sys
import tempfile
import unittest
import weakref
from unittest import mock
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from triad_continuous import (carrier_phase, endpoint_decision, read_exact,
                              receiver_noise_variance, validate_stream_identity,
                              disciplined_capture_extent, make_timing_forward, tracking_source_basis,
                              tracking_receiver_class, NativeStream)
from triad_timing_forward import TimingState


class ContinuousTests(unittest.TestCase):

    def test_public_basis_address_checks_ack_and_preserves_simulator_descriptor(self):
        epoch, ordinal = 1793850600, 10
        def source(receipt):
            value = NativeStream.__new__(NativeStream)
            value.descriptor = dict(basis_address_supported=True, epoch=1793850000,
                                    symbol_ordinal=0, source_samples=1, total_samples=2)
            value.next_sample = 2
            value._request = mock.Mock(return_value=receipt)
            value.close = mock.Mock()
            return value
        receipt = dict(basis_epoch=epoch, basis_symbol_ordinal=ordinal,
                       template_cache_cleared=True, generation=1)
        current = source(receipt)
        descriptor = dict(current.descriptor)
        self.assertEqual(current.set_basis_address(epoch, ordinal), receipt)
        current._request.assert_called_once_with(f'basis-address {epoch} {ordinal}\n')
        self.assertEqual(current.basis_address, (epoch, ordinal))
        self.assertEqual(current.basis_address_generation, 1)
        self.assertEqual(current.descriptor, descriptor)
        self.assertEqual(current.next_sample, 2)
        current.close.assert_not_called()
        for e, o in ((True, 0), (5, 0), (4294967290, 0), (epoch, True),
                     (epoch, -1), (epoch, 1 << 64), (float(epoch), 0)):
            with self.subTest(epoch=e, ordinal=o), self.assertRaises(ValueError):
                current.set_basis_address(e, o)
        self.assertEqual(current._request.call_count, 1)
        for changes in ({'basis_epoch': epoch + 1}, {'basis_epoch': float(epoch)},
                        {'basis_symbol_ordinal': True}, {'template_cache_cleared': False},
                        {'generation': True}, {'generation': 0}, {'generation': 1.0}):
            broken = source(dict(receipt, **changes))
            with self.subTest(changes=changes), self.assertRaisesRegex(ValueError, 'acknowledgement'):
                broken.set_basis_address(epoch, ordinal)
            broken.close.assert_called_once()
            self.assertFalse(hasattr(broken, 'basis_address'))
        with self.assertRaisesRegex(ValueError, 'acknowledgement'):
            current.set_basis_address(epoch, ordinal)  # repeated/stale generation
        current.close.assert_called_once()
        missing = source(receipt)
        missing.descriptor = {}
        with self.assertRaisesRegex(ValueError, 'lacks public basis-address support'):
            missing.set_basis_address(epoch, ordinal)
        missing._request.assert_not_called()

    def test_public_slot_plan_checks_geometry_without_using_payload_extent(self):
        receipt = dict(schema='triad-public-slot-plan-v1', index=1, sample_rate_hz=6000,
            pattern_epoch=1793850060, symbol_ordinal=1,
            source_begin_samples=360000, next_source_begin_samples=720000, source_samples=48000,
            source_calendar_origin_sample=480, helper_begin_samples=360000,
            pattern_begin_samples=360480, pattern_end_samples=407520,
            source_end_samples=408000, helper_end_samples=408000,
            minimum_source_step_samples=360000, receiver_window_first_sample=300000,
            receiver_window_stop_sample=450000, receiver_window_physical_origin_sample=300000,
            receiver_window_samples=150000, receiver_data_first_sample=360000,
            receiver_data_stop_sample=408000, receiver_previous_signal_stop_sample=350000,
            receiver_next_signal_first_sample=720000, quiet_aperture_first_sample=360000,
            quiet_aperture_stop_sample=408000, quiet_aperture_available=True,
            quiet_aperture_truncated=False,
            capture_lead_seconds=8., window_capture_lead_seconds=-42.)
        def source(response):
            value = NativeStream.__new__(NativeStream)
            value.descriptor = dict(slot_plan_supported=True, sample_rate_hz=6000, total_samples=1)
            value.next_sample = 1
            value._request = mock.Mock(return_value=response)
            value.close = mock.Mock()
            return value
        current = source(receipt)
        descriptor = dict(current.descriptor)
        self.assertEqual(current.slot_plan(1), receipt)
        current._request.assert_called_once_with('slot-plan 1\n')
        self.assertEqual(current.next_sample, 1)
        self.assertEqual(current.descriptor, descriptor)
        current.close.assert_not_called()
        # Overlapping public envelopes can leave no quiet-energy aperture.
        # A plan still describes valid geometry; the receiver must classify
        # this empty intersection as ambiguous rather than fabricate silence.
        overlapping = source(dict(receipt, receiver_previous_signal_stop_sample=420000,
                                  receiver_next_signal_first_sample=350000,
                                  quiet_aperture_first_sample=420000, quiet_aperture_stop_sample=420000,
                                  quiet_aperture_available=False, quiet_aperture_truncated=True))
        self.assertEqual(overlapping.slot_plan(1)['receiver_previous_signal_stop_sample'], 420000)
        overlapping.close.assert_not_called()
        for index in (True, -1, 1024, 1.0):
            with self.subTest(index=index), self.assertRaises(ValueError):
                current.slot_plan(index)
        self.assertEqual(current._request.call_count, 1)
        for changes in ({'index': True}, {'index': 0}, {'sample_rate_hz': 5},
                        {'source_calendar_origin_sample': 481}, {'receiver_window_samples': 149999},
                        {'receiver_window_physical_origin_sample': 300001},
                        {'window_capture_lead_seconds': math.nan}, {'window_capture_lead_seconds': -41.},
                        {'helper_begin_samples': 360001}, {'receiver_data_first_sample': 299999},
                        {'receiver_data_stop_sample': 450001}, {'receiver_previous_signal_stop_sample': True},
                        {'quiet_aperture_first_sample': 350001}, {'quiet_aperture_stop_sample': 720000},
                        {'quiet_aperture_available': False}, {'quiet_aperture_truncated': True}):
            broken = source(dict(receipt, **changes))
            with self.subTest(changes=changes), self.assertRaisesRegex(ValueError, 'slot plan'):
                broken.slot_plan(1)
            broken.close.assert_called_once()


    def test_slow_envelope_cadence_and_audio_phase_are_separate(self):
        bank = dict(coordinates=[[0, 0, 0, 0]])
        for width, center, expected in ((.000625, .0005, 4096.), (.0625, .05, 500.),
                                        (.000625, 1725., 4.), (62.5, .05, 4.)):
            _, seconds = make_timing_forward(bank, dict(nominal_triplet_bandwidth_hz=width,
                data_if_hz=center), 1)
            self.assertAlmostEqual(seconds, expected)
        _, seconds = make_timing_forward(dict(bank, tracking=dict(segment_seconds=64)),
            dict(nominal_triplet_bandwidth_hz=.000625, data_if_hz=.0005), 1)
        self.assertEqual(seconds, 64)
        _, seconds = make_timing_forward(bank,
            dict(nominal_triplet_bandwidth_hz=.000625, data_if_hz=.5), 1)
        self.assertLess(seconds, 224.)
        self.assertLessEqual((.5+.000625/2)*20e-6*seconds**2/8, .0625+1e-12)
        forward, seconds = make_timing_forward(bank, dict(nominal_triplet_bandwidth_hz=.000625,
            data_if_hz=.0005, sample_rate_hz=6000), 1)
        self.assertLessEqual(round(seconds*6000), 1_048_576)
        self.assertEqual(forward.rate_step_ppm, 200.)
        # Explicit lattice indices retain their previous physical meaning.
        forward, _ = make_timing_forward(dict(bank, tracking=dict(initial_rate_indices=[1])),
            dict(nominal_triplet_bandwidth_hz=.000625, data_if_hz=.0005), 1)
        self.assertEqual(forward.rate_step_ppm, 50.)

    def test_mapped_basis_composes_local_coordinates_remote_rate_and_if_once(self):
        calls = []
        class Source:
            def basis_mapped(self, origin, offsets, rows, center):
                calls.append((origin, offsets.copy(), rows, center))
                return np.ones((len(rows), len(offsets)), complex)
        class Forward:
            rate_step_ppm = 50
            delay_step_seconds = .001
        clock, arrival, frequency, center, anchor = 75., .7, 1.5, 1725., 20.
        render = tracking_source_basis(Source(), [[0, arrival, frequency, clock]], [[.01, .2]],
            38., Forward(), center, template_backend='direct')
        result = render([TimingState(0, 31, -4)], anchor, 0, 3, 100000,
            np.array([.25, 1.2501, 2.2502]))
        self.assertEqual(result.shape, (3, 1, 1, 3))
        self.assertTrue(np.all(result[0] == 0))
        origin, offsets, rows, fc = calls[0]
        self.assertEqual((origin, fc), (100000, center))
        b, epoch, mapped_arrival, cfo, ppm, delay, doppler = rows[0]
        t = (origin+offsets)/6000
        base, velocity = 1+clock*1e-6, -200e-6
        actual_source = (t-mapped_arrival-delay)*(1+ppm*1e-6)
        expected_source = base*(t-arrival-38.)+.031+velocity*(t-anchor)-delay*(base+velocity)
        np.testing.assert_allclose(actual_source, expected_source, atol=2e-14, rtol=0)
        # Baseband clock already contributes to the old acquisition CFO.
        self.assertAlmostEqual(cfo+center*(base-1), frequency)

    def test_scheduled_source_offset_stays_outside_receiver_affine_clock_warp(self):
        calls = []
        class Source:
            def basis_mapped(self, origin, offsets, rows, center):
                calls.append((origin, offsets.copy(), rows))
                return np.ones((len(rows), len(offsets)), complex)
        class Forward:
            rate_step_ppm = 50
            delay_step_seconds = .001
        clock, arrival, source_offset, anchor = 75., .7, 120.125, 20.
        render = tracking_source_basis(Source(), [[0, arrival, 1.5, clock]], [[.01, .2]],
            38., Forward(), 1725., template_backend='direct', source_offset_seconds=source_offset)
        render([TimingState(0, 31, -4)], anchor, 0, 3, 100000,
               np.array([.25, 1.2501, 2.2502]))
        origin, offsets, rows = calls[0]
        _, _, mapped_arrival, _, ppm, delay, _ = rows[0]
        t = (origin+offsets)/6000
        base, velocity = 1+clock*1e-6, -200e-6
        actual_source = (t-mapped_arrival-delay)*(1+ppm*1e-6)
        expected_source = (base*(t-arrival-38.)-source_offset+.031+
                           velocity*(t-anchor)-delay*(base+velocity))
        np.testing.assert_allclose(actual_source, expected_source, atol=2e-13, rtol=0)
        for bad in (True, math.nan, math.inf, object()):
            with self.assertRaisesRegex(ValueError, 'source-clock offset'):
                tracking_source_basis(Source(), [[0, 0, 0, 0]], [[0, 0]],
                    0., Forward(), 0., template_backend='direct', source_offset_seconds=bad)

    def test_fixed_affine_residual_clock_retains_acquired_total_cfo(self):
        class Source:
            def basis_mapped(self, origin, offsets, rows, center):
                time=(origin+offsets)/6000.
                output=[]
                for bit,epoch,arrival,frequency,clock,delay,doppler in rows:
                    scale=1+clock*1e-6
                    position=(time-arrival-delay)*scale
                    envelope=(1+.1*np.sin(.1*position))*np.exp(
                        2j*np.pi*(.01+.02*bit+.001*epoch)*position)
                    phase=(frequency+doppler)*time+center*(position-time)
                    output.append(envelope*np.exp(2j*np.pi*np.remainder(phase,1.)))
                return np.asarray(output)
        class Forward:
            rate_step_ppm=50.
            delay_step_seconds=.001
        arrival,clock,frequency,center,anchor=.7,75.,1.5,1725.,20.
        offset,source_offset=38.,120.125
        state=TimingState(0,31,-4)
        base,velocity=1+clock*1e-6,-200e-6
        scale=base+velocity
        refined_arrival=(base*(arrival+offset)-.031+velocity*anchor)/scale-offset
        taps=((.01,.2),(.05,-.3))
        render=tracking_source_basis(Source(),[(0,arrival,frequency,clock)],taps,
            offset,Forward(),center,template_backend='direct',source_offset_seconds=source_offset)
        explicit=tracking_source_basis(Source(),[(0,refined_arrival,frequency,(scale-1)*1e6)],taps,
            offset,Forward(),center,template_backend='direct',source_offset_seconds=source_offset)
        actual,expected,dynamic=[],[],[]
        for origin in (20*6000,6720*6000):
            args=(anchor,0,8,origin,np.arange(8,dtype=float))
            actual.append(render.initial_affine((state,),*args))
            expected.append(explicit((TimingState(0,0,0),),*args))
            dynamic.append(render((state,),*args))
        actual=np.concatenate(actual,axis=-1)
        expected=np.concatenate(expected,axis=-1)
        np.testing.assert_allclose(actual,expected,rtol=2e-7,atol=2e-7)
        # The original dynamic convention intentionally retains IF*velocity;
        # it cannot stand in for the fixed affine total-CFO alternative.
        ratio=np.concatenate(dynamic,axis=-1)[1,0,0]/actual[1,0,0]
        self.assertGreater(abs(ratio[0]-ratio[-1]),1.)
        from triad_incremental_receiver import IncrementalReceiver
        rng=np.random.default_rng(4731)
        raw=rng.normal(size=16)+1j*rng.normal(size=16)
        evidence=[]
        for basis in (actual,expected):
            receiver=IncrementalReceiver(sample_rate=6000.,samples=16,block_samples=8,
                basis=lambda lo,hi,b=basis:b[...,lo:hi],required_bands=((-2900.,2900.),),
                candidate_count=1,stationary_power=(.5,.5),coherence_seconds=(math.inf,),
                precision='fp64',projection_mode='valid-rows')
            receiver.push(0,raw)
            evidence.append(receiver.finish()['bit_scores'])
        np.testing.assert_allclose(evidence[0],evidence[1],rtol=1e-7,atol=1e-7)

    def test_canonical_basis_wrappers_release_source_without_cyclic_gc(self):
        if gc.isenabled():
            gc.disable()
            self.addCleanup(gc.enable)
        class Source:
            def basis_mapped(self,origin,offsets,rows,center):
                return np.ones((len(rows),len(offsets)),complex)
        class Forward:
            rate_step_ppm=50.
            delay_step_seconds=.001
        source=Source()
        source_reference=weakref.ref(source)
        render=tracking_source_basis(source,[(0,0.,0.,0.)],[(0.,0.)],
            0.,Forward(),1725.,template_backend='direct')
        reference=weakref.ref(render)
        affine_reference=weakref.ref(render.initial_affine)
        del source
        self.assertIsNotNone(source_reference())
        del render
        self.assertIsNone(reference())
        self.assertIsNone(affine_reference())
        self.assertIsNone(source_reference())

    def test_full_audio_capture_endpoint_covers_both_clocks_and_public_delays(self):
        frames = 1_000_000*6000
        result = disciplined_capture_extent(frames, 6000, .001, [[0, 0, 0, 0]], [[0, 0]])
        self.assertEqual(result, 6_012_006_003)
        self.assertEqual(disciplined_capture_extent(6000, 6000, .001,
            [[0, .1, 0, 100]], [[.01, 0]]), 6676)
        self.assertGreater(disciplined_capture_extent(2**54+1, 6000, .001,
            [[0, 0, 0, 0]], [[0, 0]]), 2**54+1)
        with self.assertRaisesRegex(ValueError, 'sample counter'):
            disciplined_capture_extent(2**64-1, 6000, .001, [[0, 0, 0, 0]], [[0, 0]])



    def test_live_stream_requires_matching_explicit_pattern_identity(self):
        for revision in ('independent-apsk-v2',):
            identity = dict(pattern_revision=revision, added_amplitude_depth=.25)
            self.assertEqual(validate_stream_identity({'format': identity}, identity), identity)
            for changed in ({**identity, 'pattern_revision': 'future-unknown'},
                            {**identity, 'pattern_revision': []},
                            {**identity, 'pattern_revision': 'retired-pattern'},
                            {**identity, 'added_amplitude_depth': .5},
                            {**identity, 'added_amplitude_depth': math.nan},
                            {**identity, 'added_amplitude_depth': True},
                            {**identity, 'added_amplitude_depth': 0}, {}):
                with self.subTest(revision=revision, changed=changed):
                    with self.assertRaises(ValueError):
                        validate_stream_identity({'format': identity}, changed)
        with self.assertRaises(ValueError): validate_stream_identity({}, {})

    def test_declared_variance_cannot_underestimate_known_simulation_noise(self):
        self.assertEqual(receiver_noise_variance(None, 3., simulated=True), 3.)
        self.assertEqual(receiver_noise_variance(4., 3., simulated=True), 4.)
        for variance in (2., 0., -1., math.nan, math.inf):
            with self.assertRaises(ValueError):
                receiver_noise_variance(variance, 3., simulated=True)
        self.assertEqual(receiver_noise_variance(1., 0., simulated=False), 1.)

    def test_endpoint_and_null_scope_cannot_be_overridden_by_large_scores(self):
        for complete, simulated, helper in ((False,True,False),(True,False,False),(True,True,True)):
            result=endpoint_decision([100,0],complete=complete,simulated=simulated,
                helper_enabled=helper,log_threshold=22.)
            self.assertFalse(result['admitted'])
        self.assertTrue(endpoint_decision([100,0],complete=True,simulated=True,
            helper_enabled=False,log_threshold=22.)['admitted'])

    def test_large_integer_phase_origin_and_arbitrary_chunk_boundaries(self):
        start=2**54+137
        complete=carrier_phase(start,100,1725.125,6000)
        pieces=np.concatenate([carrier_phase(start,37,1725.125,6000),
            carrier_phase(start+37,63,1725.125,6000)])
        np.testing.assert_allclose(np.exp(1j*complete),np.exp(1j*pieces),atol=4e-14,rtol=0)
        n,d=1725.125.as_integer_ratio()
        self.assertAlmostEqual(complete[0]/(2*math.pi), ((start*n)%(d*6000))/(d*6000))

    def test_truncated_binary_payload_is_not_silently_completed(self):
        self.assertEqual(read_exact(io.BytesIO(b'1234'),4),b'1234')
        with self.assertRaises(EOFError):read_exact(io.BytesIO(b'123'),4)


if __name__=='__main__':unittest.main()
