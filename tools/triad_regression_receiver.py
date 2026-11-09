"""Fixed synthetic fine-receiver workload for relative regression checks.

This fixture exercises the canonical acquisition-to-fine receiver on one
deterministic baseband stream. It measures matched degradation only; it cannot
qualify native-channel fidelity, absolute CPU headroom, message completion, or
decoded-message success.
"""
from __future__ import annotations

import copy
import hashlib
import json
import math

import numpy as np

from triad_cpu import detect_cpu_budget
from triad_dynamic_joint import _sample_cubic
from triad_operational_admission import DEFAULT_COHERENCES
from triad_realtime_fft import FFTScreen
from triad_receiver import CompleteBitReceiver


SCHEMA = "triad-fine-receiver-regression-v1"
FS = 400
SAMPLES = 2000
SOURCE_SAMPLES = 200
MEMORY_BYTES = 256 * 1024**2


def _fixture():
    t = np.arange(SOURCE_SAMPLES, dtype=np.float64) / FS
    envelope = np.sin(np.pi*np.arange(SOURCE_SAMPLES)/(SOURCE_SAMPLES-1))**2
    source = np.array([
        [envelope*np.exp(2j*np.pi*(4*t+8*t*t)),
         envelope*np.exp(2j*np.pi*(-7*t-9*t*t))],
        [envelope*np.exp(2j*np.pi*(10*t+3*t*t)),
         envelope*np.exp(2j*np.pi*(-12*t+8*t*t))],
    ], dtype=np.complex128)
    manifest = dict(sample_rate_hz=FS,
        calendar=dict(capture_samples=SAMPLES, capture_lead_seconds=1.,
            data_sample_offset=400, arrival_prior_seconds=[-.1, .1],
            physical_origin_sample=0),
        channel=dict(spread_delay_ms=17.5, spread_doppler_hz=13.,
            deterministic_cfo_bound_hz=.4, spread_margin=1.,
            sample_clock_pair_ppm=0., nominal_conversion_if_hz=0.),
        format=dict(occupied_bandwidth_hz=40., fine_chip_rate_hz=30.,
            bit_seconds=SOURCE_SAMPLES/FS),
        power=dict(complex_noise_variance=.1), helper_enabled=False,
        helper_plan=dict(low_hz=150., high_hz=160., auxiliary_hz=170.),
        key_seed=1, public_epoch_seconds=1000)
    coarse_manifest = copy.deepcopy(manifest)
    coarse_manifest['power']['complex_noise_variance'] = 1.
    screen = FFTScreen(coarse_manifest, source, (-1, 1), clock_ppm=(0.,),
        output_sample_rate=200, pass_edge_hz=60., stop_edge_hz=90.,
        precision='fp64', workers=1, correlation_method='auto')
    return manifest, source, screen


def _basis_factory(source):
    def factory(_native, coordinates, taps, offset, timing, audio, **kwargs):
        source_offset = float(kwargs.get('source_offset_seconds', 0.))
        def render(states, anchor, first, stop, origin, offsets):
            times = (origin+offsets)/FS
            output = np.zeros((3, len(states), len(taps), stop-first), dtype=np.complex128)
            for row, state in enumerate(states):
                epoch, arrival, frequency, clock = coordinates[state.group]
                scale = 1+clock*1e-6+state.rate_index*timing.rate_step_ppm*1e-6
                displacement = state.delay_index*timing.delay_step_seconds
                epoch_index = (-1, 1).index(epoch)
                for bit in (0, 1):
                    for tap_index, (delay, doppler) in enumerate(taps):
                        position = ((times-arrival-delay-offset)*scale-source_offset+
                                    displacement)*FS
                        output[bit+1, row, tap_index] = _sample_cubic(
                            source[epoch_index, bit], position)*np.exp(
                            2j*np.pi*(frequency+doppler)*times)
            return output
        render.cache_report = lambda: dict(method='fixed synthetic canonical-basis regression fixture',
            cache_bytes=0, representative_work=True)
        return render
    return factory


def prepare_fine_receiver(*, workers=None, memory_limit_bytes=MEMORY_BYTES):
    """Build one reusable synthetic same-stream complete-bit regression case.

    ``workers`` defaults to the complete process-visible logical CPU budget.
    All numerical pools are limited to one thread per receiver worker.
    """
    manifest, source, screen = _fixture()
    capacity = detect_cpu_budget()
    allocation = capacity['available_cpus'] if workers is None else workers
    if isinstance(allocation, bool) or not isinstance(allocation, int) or allocation < 1:
        raise ValueError('receiver workers must be a positive integer')
    if allocation > capacity['available_cpus']:
        raise ValueError('receiver workers exceed the current process-visible CPU budget')
    from triad_cpu import configure_numerical_threads
    configure_numerical_threads(dict(threads=1, linear_algebra_threads=1,
                                     workers=allocation))
    receiver = CompleteBitReceiver(manifest, screen, None, workers=allocation,
        memory_limit_bytes=memory_limit_bytes, coherences=DEFAULT_COHERENCES,
        basis_factory=_basis_factory(source),
        policy=dict(precision='fp64', cache_bytes=0, chunk_samples=800,
            block_seconds=.25, segment_seconds=.5, maximum_states=16,
            maximum_proposals=48, maximum_fine_candidates=16,
            residual_rate_step_ppm=25., maximum_acceleration_ppm_per_second=20.))
    rng = np.random.default_rng(87621)
    truth_bit = 1
    arrival, frequency, clock = .035, .2, 0.
    physical = np.arange(SAMPLES, dtype=np.float64)/FS-manifest['calendar']['capture_lead_seconds']
    source_offset = manifest['calendar']['data_sample_offset']/FS
    scale = 1+clock*1e-6
    clean = np.zeros(SAMPLES, dtype=np.complex128)
    for gain, delay, doppler in ((.8+.2j, 0., 0.),
            (.3-.6j, manifest['channel']['spread_delay_ms']*.001,
             manifest['channel']['spread_doppler_hz'])):
        positions = ((physical-arrival-delay)*scale-source_offset)*FS
        clean += gain*_sample_cubic(source[0, truth_bit], positions)*np.exp(2j*np.pi*doppler*physical)
    raw = 20*clean + math.sqrt(.1/2)*(rng.normal(size=SAMPLES)+1j*rng.normal(size=SAMPLES))
    raw = np.asarray(raw, dtype=np.complex128)
    valid = np.ones(SAMPLES, dtype=np.uint8)
    geometry = dict(schema=SCHEMA, sample_rate_hz=FS, capture_samples=SAMPLES,
        source_samples=SOURCE_SAMPLES, epoch_hypotheses=[-1, 1], public_bit_hypotheses=[0, 1],
        public_arrival_seconds=[-.1, .1], public_cfo_bound_hz=.4,
        spread_delay_ms=17.5, spread_doppler_hz=13., source_clock_bound_ppm=0.,
        coherences_seconds=list(DEFAULT_COHERENCES), worker_allocation=allocation,
        cpu_budget=capacity, memory_limit_bytes=memory_limit_bytes,
        precision='fp64', numerical_threads_per_worker=1, seed=87621,
        input_sha256=hashlib.sha256(raw.tobytes()+valid.tobytes()).hexdigest(),
        scope='Deterministic synthetic public native-equivalent basis; complete canonical coarse selection, fine lattice, continuous two-lane tracking, null and DATA decisions on one fixed received stream. No native channel, message boundary or codec.')
    geometry['geometry_sha256'] = hashlib.sha256(json.dumps(geometry, sort_keys=True,
        separators=(',', ':')).encode()).hexdigest()

    def run():
        result = receiver.receive(raw, valid)
        tracked = result.get('tracking', {})
        native = tracked.get('native_template_work', {})
        work = tracked.get('work_counts', {})
        if (result.get('schema') != 'triad-complete-bit-receiver-v1' or
                result.get('status') != 'scored' or
                result.get('slot_classification') != 'DATA' or
                result.get('decision_bit') != truth_bit or
                result.get('coverage', {}).get('consumed_samples') != SAMPLES or
                result.get('coverage', {}).get('zero_h0_verified') is not True or
                not result.get('coverage', {}).get('coarse_evidence_used_only_for_selection') or
                not result.get('coverage', {}).get('global_timing_pruning_once') or
                not result.get('coverage', {}).get('complete_observation') or
                not native.get('representative_work') or
                not work.get('candidate_block_updates') or
                not work.get('basis_rows')):
            raise RuntimeError('fine receiver regression workload did not complete its declared DATA/null path')
        return result

    return dict(call=run, close=receiver.close, geometry=geometry,
        worker_allocation=allocation, receiver=receiver,
        scope='One synthetic deterministic DATA stream through canonical full public acquisition and fine tracking. Relative performance regression only; not absolute CPU capacity or native/message qualification.')
