"""Small deterministic tests for the bounded coarse correct-bit endpoint."""
from pathlib import Path
import sys
import unittest
from unittest import mock

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import triad_case_replay as replay_module
from triad_case_replay import (PreparedReplay, build_public_screen, _bounded_basis,
                               _clean_options, _integer_basis, _local_fidelity_manifest)


class NativeBasis:
    def __init__(self):
        self.calls = []

    def basis_mapped(self, first, offsets, rows, _center):
        self.calls.append((first, len(offsets), float(np.max(offsets)-np.min(offsets))))
        return np.asarray([first + offsets + 1j*(epoch+bit) for bit, epoch, *_ in rows])

    def basis(self, first, count, rows):
        if count > 16384:
            raise ValueError('bounded basis coordinates required')
        self.calls.append((first, count, count-1))
        epoch, bit = rows[0][1], rows[0][0]
        return np.asarray([first+np.arange(count)+1j*(epoch+bit)])[...]


class FakeScreen:
    def __init__(self, proposals):
        self.proposals = proposals
        self.crop_rows = 8
        self.samples = 8
        self.taps = np.asarray([1., .25])
        self.resident_estimate = 32
        self.preparation_peak_estimate_bytes = 64

    def projection_receipt(self):
        return {'test': 'same-projection', 'cropped_output_count': self.crop_rows, 'downsample': 1}

    def score_projected(self, data, *, frequency_intervals, projection_receipt, top_per_bit):
        self.seen_data = np.array(data, copy=True)
        self.seen_intervals = frequency_intervals
        assert projection_receipt == self.projection_receipt()
        assert top_per_bit == 2
        if callable(self.proposals):
            return self.proposals(data)
        return self.proposals


def proposals(a, b, status='proposed', reason=None):
    return dict(status=status, reason=reason, frequencies_hz=[0.],
        candidates_by_bit=[[{'score': a}], [{'score': b}]])


def replay_with(screen):
    replay = object.__new__(PreparedReplay)
    replay.threads = 1
    replay.memory_limit = 100000
    replay.screen = screen
    replay.descriptor = {'helper_enabled': False}
    replay.observations = {0: np.ones(8, complex), 1: np.full(8, 2, complex)}
    replay.helper_observations = {0: np.empty(0, complex), 1: np.empty(0, complex)}
    replay.helper_prefix_samples = 0
    replay.helper_status = 'helper_disabled'
    replay.grid = type('Grid', (), {'frequency_bound': .1})()
    replay.template_source = type('Source', (), {'process': type('Process', (), {'pid': -1})()})()
    replay.observation_samples = 8
    replay.fs = 6000
    replay.represented_baseband_seconds = 8/6000
    replay.workspace = {'projection_noise_workspace_bytes': None}
    replay.coverage = {}
    replay.preparation = {}
    replay._noise = None
    replay.ensure_clean_pair = lambda: None
    return replay


class ReplayTests(unittest.TestCase):
    def test_shared_public_screen_builder_binds_address_and_uses_source_extent(self):
        class Source:
            def __init__(self):
                self.addresses = []
                self.descriptor = dict(sample_rate_hz=6000, source_samples=1200,
                    total_samples=1500, emitted_samples=1700, fixed_workspace_bytes=64)
                self.plan = dict(format=dict(occupied_bandwidth_hz=1., inner_rate_hz=1.,
                        fhss=False, elapsed_bit_seconds=.2, symbol_ordinal=4),
                    physics=dict(spread_margin=1., spread_delay_ms=0., spread_doppler_hz=0.,
                        deterministic_cfo_bound_hz=.1, sample_clock_pair_ppm=.0001,
                        arrival_prior_seconds=[-.1, .1]))

            def set_basis_address(self, epoch, ordinal):
                self.addresses.append((epoch, ordinal))
                return dict(basis_epoch=epoch, basis_symbol_ordinal=ordinal,
                            template_cache_cleared=True)

        class Screen:
            crop_rows = 128
            resident_estimate = 1024
            preparation_peak_estimate_bytes = 2048

        source = Source()
        manifest = dict(sample_rate_hz=6000, calendar=dict(capture_samples=1500,
            capture_lead_seconds=8., data_sample_offset=12000, physical_origin_sample=0),
            format=dict(symbol_ordinal=4), channel=dict(spread_delay_ms=0.))
        original_calendar = dict(manifest['calendar'])
        seen = {}

        def screen_factory(screen_manifest, templates, epochs, **kwargs):
            seen.update(manifest=screen_manifest, templates=templates,
                        epochs=epochs, kwargs=kwargs)
            return Screen()

        args = dict(public_address={'pattern_epoch': 1793850001, 'symbol_ordinal': 7},
            source_identity={'case': 'fixture', 'sha256': 'abc'},
            memory_limit_bytes=1_000_000, threads=1, mapped_step=1,
            screen_factory=screen_factory)
        result = build_public_screen(source, manifest, **args)
        self.assertEqual(source.addresses, [(1793850001, 7)])
        self.assertEqual(result['manifest']['calendar'], original_calendar)
        self.assertEqual(result['manifest']['public_epoch_seconds'], 1793850001)
        self.assertEqual(result['manifest']['format']['symbol_ordinal'], 7)
        self.assertEqual(result['work']['raw_source_samples'], 1200)
        self.assertEqual(result['work']['raw_capture_samples'], 1500)
        self.assertEqual(result['work']['native_basis_sample_work_estimate'],
                         1200*13*2*3)
        self.assertEqual(seen['kwargs']['source_samples'], 1200)
        self.assertEqual(seen['kwargs']['template_identity'], result['template_identity'])
        other = build_public_screen(Source(), manifest,
            **{**args, 'public_address': {'pattern_epoch': 1793850001, 'symbol_ordinal': 8}})
        self.assertNotEqual(result['template_identity'], other['template_identity'])

    def test_public_address_is_validated_before_mutating_native_source(self):
        class Source:
            called = False
            def set_basis_address(self, *_args):
                self.called = True
        source = Source()
        with self.assertRaisesRegex(ValueError, 'public address'):
            build_public_screen(source, {}, public_address=(1, 2), source_identity='fixture',
                memory_limit_bytes=1, threads=1)
        self.assertFalse(source.called)

    def test_local_fidelity_calendar_is_normalized_without_changing_full_calendar(self):
        manifest = dict(calendar=dict(capture_lead_seconds=3., data_sample_offset=6000,
                physical_origin_sample=41, capture_samples=90000,
                arrival_prior_seconds=[-7.035, 7.035]),
            channel=dict(spread_delay_ms=21.), format=dict(symbol_ordinal=3),
            public_epoch_seconds=100)
        original = {key: value.copy() if isinstance(value, dict) else value
                    for key, value in manifest.items()}
        local = _local_fidelity_manifest(manifest,
            {'pattern_epoch': 200, 'symbol_ordinal': 9}, [-100., 0., 100.],
            source_window_samples=6000, fs=6000)
        self.assertEqual(manifest, original)
        self.assertEqual(local['calendar']['capture_lead_seconds'], 8.)
        self.assertEqual(local['calendar']['data_sample_offset'], 12000)
        self.assertEqual(local['calendar']['physical_origin_sample'], 0)
        self.assertGreaterEqual(local['calendar']['capture_samples'],
                                (8+2+1+7.035+.021+1)*6000)
        self.assertEqual(local['public_epoch_seconds'], 200)
        self.assertEqual(local['format']['symbol_ordinal'], 9)

    def test_current_mapped_fidelity_gate_runs_through_clean_and_noisy_controls(self):
        class Source:
            plan = dict(format=dict(tile_seconds=.1, elapsed_bit_seconds=.2),
                        geometry=dict(source_calendar_origin_sample=0))
            descriptor = dict(source_samples=9000)
            def basis(self, first, count, rows):
                return np.ones((len(rows), count), dtype=np.complex128)

        class Screen:
            arrivals = np.asarray([0.])
            resident_estimate = 1024
            preparation_peak_estimate_bytes = 2048
            workspaces = [None]*26
            kernel_rows = 32
            rays = [0]
            correlation_method = 'frequency'
            def __init__(self, manifest, *_args, **_kwargs):
                self.manifest = manifest
            def _correlation_plan(self, *_args, **_kwargs):
                return dict(selected=('frequency',),estimated_peak_bytes_by_method=dict(frequency=4096))
            def score(self, _data, _frequencies, *, top_per_bit):
                return {'candidates': [{'bit': 0, 'epoch': 900, 'score': 1.}]}

        def compare(*_args):
            return dict(rank_agreement=True, maximum_projector_distance=0.,
                maximum_stored_scoring_operator_distance=0.,
                maximum_column_relative_norm_error=0., maximum_gram_relative_norm_error=0.,
                minimum_wrong_bit_projector_separation_ratio=1.)

        manifest = dict(sample_rate_hz=6000, calendar=dict(
                capture_lead_seconds=3., data_sample_offset=6000, physical_origin_sample=10,
                capture_samples=20000, arrival_prior_seconds=[-7.035, 7.035]),
            channel=dict(spread_delay_ms=21., spread_doppler_hz=0.),
            format=dict(symbol_ordinal=1))
        with (mock.patch.object(replay_module, 'FFTScreen', Screen),
              mock.patch.object(replay_module, 'compare_template_projectors', compare)):
            result = replay_module._validate_mapped_profile(Source(), manifest,
                [-100., 0., 100.], output_fs=10, pass_edge=.4, stop_edge=4.9,
                public_address={'pattern_epoch': 900, 'symbol_ordinal': 5},
                memory_limit_bytes=128*1024**2, threads=1, sampling_step=60)
            with self.assertRaisesRegex(MemoryError, 'comparison/control peak'):
                replay_module._validate_mapped_profile(Source(), manifest,
                    [-100., 0., 100.], output_fs=10, pass_edge=.4, stop_edge=4.9,
                    public_address={'pattern_epoch': 900, 'symbol_ordinal': 5},
                    memory_limit_bytes=64*1024**2, threads=1, sampling_step=60)
        self.assertTrue(result['accepted'])
        self.assertGreater(result['preparation_peak_estimate_bytes'], 4096)
        self.assertEqual(result['common_clean_noisy_winner_controls'], [True, True])
        self.assertEqual(manifest['calendar']['capture_lead_seconds'], 3.)

    def test_noise_work_cap_stays_default_without_s3_no_helper_probe(self):
        receipt = {'cropped_output_count': 100, 'downsample': 1,
                   'fir_taps_sha256': 'source-geometry'}
        taps = np.ones(1420)
        with mock.patch.object(replay_module, 'ProjectedNoise') as noise:
            for mapped_step, helper_prefix in ((1, 0), (3, 1)):
                limit, record = replay_module.calibrated_noise_limit(
                    receipt, taps, mapped_step=mapped_step,
                    helper_prefix_samples=helper_prefix,
                    memory_limit_bytes=64*1024**2)
                self.assertEqual(limit, 100_000_000)
                self.assertEqual(record['status'], 'default_limit')
            noise.assert_not_called()

    def test_noise_work_cap_hard_ceiling_rejects_before_probe(self):
        receipt = {'cropped_output_count': 8192, 'downsample': 1}
        taps = np.ones(350)
        with mock.patch.object(replay_module, 'ProjectedNoise') as noise:
            with self.assertRaisesRegex(ValueError, '500M'):
                replay_module.calibrated_noise_limit(receipt, taps,
                    mapped_step=3, helper_prefix_samples=0,
                    memory_limit_bytes=2*1024**3)
            noise.assert_not_called()

    def test_slow_noise_probe_keeps_default_limit_and_preserves_geometry(self):
        self._assert_noise_probe_outcome(admitted=False)

    def test_fast_noise_probe_admits_only_within_fixed_cost_forecasts(self):
        self._assert_noise_probe_outcome(admitted=True)

    def _assert_noise_probe_outcome(self, *, admitted):
        receipt = {'schema': 'exact-screen-receipt', 'cropped_output_count': 9000,
                   'downsample': 1, 'first_cropped_convolution_raw_sample': 0,
                   'fir_taps_sha256': 'fixed-tap-hash', 'raw_samples': 100_000}
        original = dict(receipt)
        taps = np.linspace(-.5, .5, 151)
        memory_limit = 321*1024**2
        calls = {}

        class ProbeNoise:
            def __init__(self, probe_receipt, probe_taps, *, memory_limit_bytes):
                calls['receipt'] = probe_receipt
                calls['taps'] = probe_taps
                calls['memory_limit_bytes'] = memory_limit_bytes

            def draw(self, seed):
                calls.setdefault('seeds', []).append(seed)

        if admitted:
            clock = [0., 2., 2., 2.25, 2.25, 2.5, 2.5, 2.75]
        else:
            clock = [0., 10., 10., 10.1, 10.1, 10.2, 10.2, 10.3]
        with (mock.patch.object(replay_module, 'ProjectedNoise', ProbeNoise),
              mock.patch.object(replay_module.time, 'perf_counter', side_effect=clock)):
            limit, record = replay_module.calibrated_noise_limit(receipt, taps,
                mapped_step=3, helper_prefix_samples=0,
                memory_limit_bytes=memory_limit)

        self.assertEqual(receipt, original)
        self.assertEqual(calls['receipt']['cropped_output_count'], 8192)
        self.assertEqual({key: value for key, value in calls['receipt'].items()
                          if key != 'cropped_output_count'},
                         {key: value for key, value in original.items()
                          if key != 'cropped_output_count'})
        np.testing.assert_array_equal(calls['taps'], taps)
        self.assertIs(calls['taps'], taps)
        self.assertEqual(calls['memory_limit_bytes'], memory_limit)
        self.assertEqual(calls['seeds'], [86100, 86101, 86102])
        if admitted:
            self.assertEqual((limit, record['status']), (500_000_000, 'admitted'))
            self.assertLessEqual(record['conservative_factor_forecast_seconds'], 15.)
            self.assertLessEqual(record['conservative_simulator_draw_forecast_seconds'], 2.)
        else:
            self.assertEqual((limit, record['status']), (100_000_000, 'cost_limit'))
            self.assertGreater(record['conservative_factor_forecast_seconds'], 15.)

    def test_s3_selection_requires_heavy_fhss_and_keeps_native_work_cap(self):
        class StopBeforeFullBank(Exception):
            pass
        class Header:
            def __init__(self, *_args, **_kwargs):
                self.plan = dict(format=dict(inner_rate_hz=36., fhss=True,
                        occupied_bandwidth_hz=225., elapsed_bit_seconds=256.8),
                    physics=dict(spread_margin=3., spread_delay_ms=21., spread_doppler_hz=90.,
                        deterministic_cfo_bound_hz=.003, sample_clock_pair_ppm=.0001,
                        arrival_prior_seconds=[-7.035, 7.035]))
                self.descriptor = dict(sample_rate_hz=6000, source_samples=1_542_405,
                    total_samples=1_800_000, capture_lead_seconds=8., data_offset_samples=12000,
                    emitted_samples=1_600_000, data_if_hz=1725., helper_enabled=False,
                    helper_low_hz=None, helper_high_hz=None, epoch=1793850000,
                    symbol_ordinal=0, noise_variance=1., fixed_workspace_bytes=0)
            def set_basis_address(self, epoch, symbol_ordinal):
                return dict(basis_epoch=epoch, basis_symbol_ordinal=symbol_ordinal,
                            template_cache_cleared=True)
            def close(self):
                pass
        seen = {}
        def screen(*_args, **kwargs):
            seen.update(kwargs)
            raise StopBeforeFullBank()
        with (mock.patch.object(replay_module, '_validate_mapped_profile', return_value={'accepted': True}) as validate,
              mock.patch.object(replay_module, '_mapped_identity', return_value='0'*64)):
            with self.assertRaises(StopBeforeFullBank):
                PreparedReplay('fixture', [], memory_limit_bytes=2*1024**3, threads=1,
                    seed=1, native_factory=Header, screen_factory=screen)
            self.assertEqual(seen['template_sampling_step'], 3)
            self.assertEqual(seen['output_sample_rate'], 600)
            self.assertEqual((seen['pass_edge_hz'], seen['stop_edge_hz']), (240., 294.))
            self.assertEqual(validate.call_args.kwargs['sampling_step'], 3)
            self.assertEqual(replay_module._MAX_MAPPED_TEMPLATE_POINTS, 120_000_000)
            def cheap(*args, **kwargs):
                source = Header(*args, **kwargs)
                source.descriptor['source_samples'] = 10_000
                return source
            with self.assertRaises(StopBeforeFullBank):
                PreparedReplay('fixture', [], memory_limit_bytes=2*1024**3, threads=1,
                    seed=1, native_factory=cheap, screen_factory=screen)
            self.assertEqual(seen['template_sampling_step'], 1)
            self.assertIsNone(seen['mapped_template_provider'])
            def oversized(*args, **kwargs):
                source = Header(*args, **kwargs)
                source.descriptor['source_samples'] = 10_000_000
                return source
            with self.assertRaisesRegex(MemoryError, 'bounded native-work'):
                PreparedReplay('fixture', [], memory_limit_bytes=2*1024**3, threads=1,
                    seed=1, native_factory=oversized, screen_factory=screen)
            def non_hopping(*args, **kwargs):
                source = Header(*args, **kwargs)
                source.plan['format']['fhss'] = False
                return source
            with self.assertRaisesRegex(ValueError, 'S=3'):
                PreparedReplay('fixture', [], memory_limit_bytes=2*1024**3, threads=1,
                    seed=1, native_factory=non_hopping, screen_factory=screen, mapped_step=3)

    def test_s60_fresh_profile_selects_5hz_rate_and_fullscreen_budgets(self):
        class StopBeforeFullBank(Exception):
            pass

        def make_header(occupied_bandwidth, doppler, cfo):
            class Header:
                def __init__(self, *_args, **_kwargs):
                    self.plan = dict(format=dict(inner_rate_hz=1., fhss=False,
                            occupied_bandwidth_hz=occupied_bandwidth, elapsed_bit_seconds=576.4),
                        physics=dict(spread_margin=1., spread_delay_ms=0., spread_doppler_hz=doppler,
                            deterministic_cfo_bound_hz=cfo, sample_clock_pair_ppm=.0001,
                            arrival_prior_seconds=[-7.035, 7.035]))
                    self.descriptor = dict(sample_rate_hz=6000, source_samples=4_034_406,
                        total_samples=4_650_000, capture_lead_seconds=8., data_offset_samples=12000,
                        emitted_samples=4_250_000, data_if_hz=1725., helper_enabled=False,
                        helper_low_hz=None, helper_high_hz=None, epoch=1793850000,
                        symbol_ordinal=0, noise_variance=1., fixed_workspace_bytes=0)

                def set_basis_address(self, epoch, symbol_ordinal):
                    return dict(basis_epoch=epoch, basis_symbol_ordinal=symbol_ordinal,
                                template_cache_cleared=True)

                def close(self):
                    pass
            return Header

        def capture_profile(occupied_bandwidth, doppler, cfo, *, accepted=True):
            seen = {}
            def screen(*_args, **kwargs):
                seen.update(kwargs)
                raise StopBeforeFullBank()
            with (mock.patch.object(replay_module, '_validate_mapped_profile',
                                    return_value={'accepted': accepted}) as validate,
                  mock.patch.object(replay_module, '_mapped_identity', return_value='0'*64)):
                with self.assertRaises(StopBeforeFullBank):
                    PreparedReplay('fixture', [], memory_limit_bytes=2*1024**3, threads=1,
                        seed=1, native_factory=make_header(occupied_bandwidth, doppler, cfo),
                        screen_factory=screen)
            return seen, validate

        # required = occupied half-band + ray Doppler + public CFO bound.
        # The narrow profile stays10Hz, this one selects5Hz, and a wider
        # admissible slow waveform retains20Hz.
        for occupied, doppler, cfo, expected_rate in (
                (.2, 0., .1, 10), (1., .1, .2, 5), (4., 0., .5, 20)):
            with self.subTest(expected_rate=expected_rate):
                seen, validate = capture_profile(occupied, doppler, cfo)
                self.assertEqual(validate.call_args.kwargs['output_fs'], expected_rate)
                self.assertEqual(validate.call_args.kwargs['pass_edge'],
                                 .4 if expected_rate == 10 else 2. if expected_rate == 5 else 4.)
                self.assertEqual(validate.call_args.kwargs['stop_edge'],
                                 4.9 if expected_rate == 10 else 2.45 if expected_rate == 5 else 9.8)
                self.assertEqual(validate.call_args.kwargs['sampling_step'], 60)
                self.assertEqual(seen['template_sampling_step'], 60)
                self.assertEqual(seen['output_sample_rate'], expected_rate)
                self.assertEqual(seen['maximum_frequency_points'], 4096)
                self.assertEqual(seen['maximum_fft_products'], 32768)
                self.assertEqual(seen['correlation_method'], 'auto')

        class ValidationRejected(Exception):
            pass
        screen = mock.Mock(side_effect=StopBeforeFullBank())
        with (mock.patch.object(replay_module, '_validate_mapped_profile',
                                side_effect=ValidationRejected('fresh local gate failed')) as validate,
              mock.patch.object(replay_module, '_mapped_identity', return_value='0'*64)):
            with self.assertRaisesRegex(ValidationRejected, 'fresh local gate failed'):
                PreparedReplay('fixture', [], memory_limit_bytes=2*1024**3, threads=1,
                    seed=1, native_factory=make_header(1., .1, .2), screen_factory=screen)
            validate.assert_called_once()
            screen.assert_not_called()

    def test_public_mapped_basis_splits_both_native_coordinate_limits(self):
        source = NativeBasis()
        positions = np.arange(20_100, dtype=float) * 65
        rows = _bounded_basis(source, ((3, 1), (-4, 0)), positions)
        self.assertEqual(rows.shape, (2, len(positions)))
        np.testing.assert_array_equal(rows[0], positions+4j)
        np.testing.assert_array_equal(rows[1], positions-4j)
        self.assertGreater(len(source.calls), 1)
        self.assertTrue(all(count <= 16384 and span <= 1_000_000 for _, count, span in source.calls))

    def test_integer_basis_splits_clock_warped_ranges_at_native_request_limit(self):
        source = NativeBasis()
        values = _integer_basis(source, 5, 1, 123, 40_010)
        self.assertEqual(values.shape, (40_010,))
        np.testing.assert_allclose(values.real, np.arange(123, 123+40_010))
        self.assertTrue(all(count <= 16384 for _, count, _ in source.calls))

    def test_template_and_channel_commands_retain_one_public_profile(self):
        args = ['--case', 'hf', '--seed', '8', '--bit', '0', '--seconds', '.001']
        command0 = _clean_options('/test/native', args, bit=0, seed=77)
        command1 = _clean_options('/test/native', args, bit=1, seed=77)
        for command in (command0, command1):
            self.assertEqual(command.count('--bit'), 1)
            self.assertEqual(command[command.index('--seed')+1], '77')
            self.assertEqual(command[command.index('--thermal-noise')+1], 'off')
            self.assertEqual(command[command.index('--whole-bit-count')+1], '1')
            self.assertIn('--power-reference', command)
        self.assertNotEqual(command0[command0.index('--bit')+1], command1[command1.index('--bit')+1])
        with self.assertRaisesRegex(ValueError, 'omit --stream-server'):
            _clean_options('/test/native', args+['--stream-server'], bit=0, seed=77)

    def test_noiseless_bit_choice_is_scored_before_evaluator_label(self):
        replay = replay_with(FakeScreen(proposals(4., 3.)))
        result = replay.receive(0, cn0_db_hz=None)
        self.assertEqual(result['decision_bit'], 0)
        self.assertTrue(result['correct_bit'])
        self.assertEqual(replay.screen.seen_intervals, [[-.1, .1]])
        other = replay.receive(1, cn0_db_hz=None)
        self.assertEqual(other['decision_bit'], 0)
        self.assertFalse(other['correct_bit'])
        self.assertEqual(replay.screen.seen_data[0], 2+0j)
        self.assertEqual(result['represented_baseband_seconds'], 8/6000)
        self.assertFalse(result['raw_projection_included_in_receiver_ratio'])

    def test_empty_invalid_and_tied_public_decisions_are_failures(self):
        for screen in (FakeScreen(proposals(5., 5.)),
                       FakeScreen(proposals(1., 0., status='unresolved', reason='frequency_work_limit')),
                       FakeScreen(dict(status='proposed', candidates_by_bit=[[], []]))):
            result = replay_with(screen).receive(0, cn0_db_hz=None)
            self.assertIsNone(result['decision_bit'])
            self.assertFalse(result['success'])
            self.assertEqual(result['status'], 'no_decision')

    def test_common_projected_noise_and_helper_prefix_share_the_declared_cn0(self):
        screen = FakeScreen(proposals(9., 1.))
        replay = replay_with(screen)
        replay.descriptor = {'helper_enabled': True, 'emitted_samples': 40}
        replay.memory_limit = 64*1024**2
        replay.helper_prefix_samples = 4
        replay.helper_observations = {0: np.full(4, 3+0j), 1: np.full(4, 4+0j)}
        replay.manifest = {'calendar': {'capture_samples': 8},
                           'power': {'complex_noise_variance': 0.}, 'format': {},
                           'channel': {}, 'helper_enabled': True}
        projected = np.full(8, 2+0j)
        prefix = np.full(4, 5+0j)
        seen = {}

        class Noise:
            def __init__(self, receipt, taps, *, helper_prefix_samples, memory_limit_bytes, maximum_factor_work):
                seen['receipt'] = receipt
                seen['prefix_samples'] = helper_prefix_samples
                seen['memory_limit_bytes'] = memory_limit_bytes
                seen['maximum_factor_work'] = maximum_factor_work
                self.owned_workspace_bytes = 80

            def draw(self, seed):
                seen['noise_seed'] = seed
                return {'projected_noise': projected, 'helper_raw_noise': prefix}

        def helper(values, manifest, frequencies, *, config):
            seen['helper_values'] = np.array(values, copy=True)
            seen['helper_manifest_variance'] = manifest['power']['complex_noise_variance']
            seen['helper_frequencies'] = frequencies
            seen['helper_config'] = config
            return {'status': 'fallback', 'frequency': {'status': 'fallback', 'expanded_intervals_hz': []}}

        with (mock.patch.object(replay_module, 'ProjectedNoise', Noise),
              mock.patch.object(replay_module, 'estimate_weak_helper', helper)):
            result = replay.receive(0, cn0_db_hz=-30., noise_seed=501)

        sigma = np.sqrt(replay.fs*10**3)
        np.testing.assert_allclose(screen.seen_data, np.ones(8)/sigma + projected)
        np.testing.assert_allclose(seen['helper_values'], np.full(4, 3.)/sigma + prefix)
        self.assertEqual(seen['helper_manifest_variance'], 1.)
        self.assertEqual(seen['prefix_samples'], 4)
        self.assertEqual(seen['noise_seed'], 501)
        np.testing.assert_allclose(seen['helper_frequencies'], [-.1, 0., .1])
        self.assertEqual(seen['helper_config']['frequency_config']['selection_geometry'],
                         'continuous_interval')
        self.assertEqual(seen['helper_config']['frequency_config']['future_horizon_seconds'],
                         replay.represented_baseband_seconds)
        self.assertTrue(result['success'])
        self.assertFalse(result['raw_projection_included_in_receiver_ratio'])
        self.assertGreater(result['helper_transient_workspace_estimate_bytes'], 0)
        self.assertGreaterEqual(result['workspace_bytes'],
            screen.resident_estimate+result['helper_transient_workspace_estimate_bytes']+
            result['retained_simulator_bytes'])


if __name__ == '__main__':
    unittest.main()
