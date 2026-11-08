#!/usr/bin/env python3
"""Bounded native source and fixed-bank or evolving timing-state receiver.

Initial acquisition hypotheses and timing dynamics are public before input.
Only the declared observation endpoint permits a diagnostic decision.
"""
from __future__ import annotations

import hashlib
import json
import math
from fractions import Fraction
import platform
from pathlib import Path
import subprocess

import numpy as np
from scipy.special import logsumexp

ROOT = Path(__file__).resolve().parents[1]


def file_identity(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        while block := stream.read(1 << 20): digest.update(block)
    return dict(path=str(path), sha256=digest.hexdigest())


def read_exact(stream, count):
    chunks = bytearray()
    while len(chunks) < count:
        block = stream.read(count-len(chunks))
        if not block:
            raise EOFError('stream ended before the declared payload')
        chunks.extend(block)
    return bytes(chunks)


def carrier_phase(first, count, frequency, sample_rate):
    """Reduce the integer sample origin before converting to binary64."""
    numerator, denominator = float(frequency).as_integer_ratio()
    period = denominator*sample_rate
    initial = (int(first)*numerator) % period
    return 2*np.pi*np.remainder(initial/period + np.arange(count)*(frequency/sample_rate), 1.)


def endpoint_decision(scores, *, complete, simulated, helper_enabled, log_threshold):
    values = np.asarray(scores, dtype=float)
    if values.shape != (2,) or not np.isfinite(values).all():
        raise ValueError('two finite bit scores required')
    log_e = float(logsumexp(values)-math.log(2))
    model_pass = bool(complete and log_e >= log_threshold)
    return dict(candidate_bit=int(np.argmax(values)), log_e=log_e,
        conditional_model_pass=model_pass,
        admitted=model_pass and simulated and not helper_enabled,
        admission_scope='Conditional Gaussian model, fixed bank and endpoint only; neither correctness nor Helper/physical null qualified.')


def receiver_noise_variance(override, native_variance, *, simulated):
    variance = float(native_variance if override is None else override)
    if not math.isfinite(variance) or variance <= 0:
        raise ValueError('receiver noise variance must be finite and positive')
    if simulated and (not math.isfinite(native_variance) or native_variance <= 0 or
                      variance < native_variance):
        raise ValueError('simulation receiver variance must not underestimate the known native thermal noise')
    return variance


def validate_stream_identity(plan, descriptor):
    """Reject a live source whose advertised template identity is ambiguous.

    Historical self-contained captures do not use this handshake. A live
    service must identify both its transmitted source and on-demand basis.
    """
    supported = {'independent-apsk-v2'}
    format_info = plan.get('format')
    if not isinstance(format_info, dict):
        raise ValueError('native plan is missing its waveform format')
    identities = []
    for info in (format_info, descriptor):
        revision = info.get('pattern_revision')
        depth = info.get('added_amplitude_depth')
        if not isinstance(revision, str) or revision not in supported:
            raise ValueError('native stream has a missing or unsupported pattern revision')
        if isinstance(depth, bool) or not isinstance(depth, (int, float)) or not math.isfinite(depth) or not 0 < depth < 1:
            raise ValueError('native stream has an invalid Added Symbols amplitude depth')
        identities.append((revision, float(depth)))
    if identities[0] != identities[1]:
        raise ValueError('native stream pattern identity disagrees with its source plan')
    return dict(pattern_revision=identities[0][0], added_amplitude_depth=identities[0][1])


class NativeStream:
    """One owner, bounded requests; binary output never contains diagnostics."""
    def __init__(self, executable, arguments, *, kind='channel', log=None):
        self.command = [str(executable), *arguments, '--stream-server', '--stream-kind', kind]
        self.process = subprocess.Popen(self.command, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=log)
        try:
            self.plan = self._header()
            self.descriptor = self._header()
            self.waveform_identity = validate_stream_identity(self.plan, self.descriptor)
        except BaseException:
            self.close()
            raise
        self.next_sample = 0

    def _header(self):
        line = self.process.stdout.readline(1 << 20)
        if not line or not line.endswith(b'\n'):
            raise RuntimeError('missing or oversized native stream header')
        result = json.loads(line)
        if not isinstance(result, dict):
            raise ValueError('native stream header must be an object')
        return result

    def _request(self, text):
        self.process.stdin.write(text.encode('ascii'))
        self.process.stdin.flush()
        return self._header()

    def next(self, count):
        if not isinstance(count, int) or not 1 <= count <= 16384:
            raise ValueError('bounded positive native chunk required')
        r = self._request(f'next {count}\n')
        n = int(r['count'])
        if not 0 <= n <= count or r['first_sample'] != self.next_sample:
            raise ValueError('native stream count or calendar changed')
        values = np.frombuffer(read_exact(self.process.stdout, n*16), dtype='<c16')
        valid = np.frombuffer(read_exact(self.process.stdout, n), dtype='u1')
        self.next_sample += n
        return values, valid

    def basis(self, first, count, rows):
        if first < 0 or not 1 <= count <= 16384 or not 1 <= len(rows) <= 128:
            raise ValueError('bounded basis coordinates required')
        lines = [f'basis {first} {count} {len(rows)}']
        for bit, epoch, arrival, frequency, clock, delay, doppler in rows:
            lines.append(f'{int(bit)} {int(epoch)} {arrival:.17g} {frequency:.17g} {clock:.17g} {delay:.17g} {doppler:.17g}')
        r = self._request('\n'.join(lines)+'\n')
        if int(r['count']) != count:
            raise ValueError('native basis response count changed')
        return np.frombuffer(read_exact(self.process.stdout, len(rows)*count*16),
            dtype='<c16').reshape(len(rows), count)

    def basis_mapped(self, first, offsets, rows, audio_center_hz):
        offsets = np.asarray(offsets, dtype='<f8')
        count = len(offsets)
        if (not isinstance(first, int) or not 0 <= first < (1 << 64) or
            not 1 <= count <= 16384 or not 1 <= len(rows) <= 128 or
            offsets.ndim != 1 or not np.isfinite(offsets).all() or offsets[0] < 0 or
            np.any(np.diff(offsets) <= 0) or offsets[-1] > 1048576 or
            not math.isfinite(audio_center_hz)):
            raise ValueError('bounded mapped basis coordinates required')
        if not self.descriptor.get('mapped_basis_supported', False):
            raise ValueError('native source lacks mapped-basis support; rebuild the isolated exporter')
        lines = [f'basis-map {first} {count} {len(rows)} {audio_center_hz:.17g}']
        for bit, epoch, arrival, frequency, clock, delay, doppler in rows:
            lines.append(f'{int(bit)} {int(epoch)} {arrival:.17g} {frequency:.17g} {clock:.17g} {delay:.17g} {doppler:.17g}')
        self.process.stdin.write(('\n'.join(lines)+'\n').encode('ascii'))
        self.process.stdin.write(offsets.tobytes())
        self.process.stdin.flush()
        response = self._header()
        if int(response['count']) != count:
            raise ValueError('native mapped basis response count changed')
        return np.frombuffer(read_exact(self.process.stdout, len(rows)*count*16),
            dtype='<c16').reshape(len(rows), count)

    def close(self):
        process = getattr(self, 'process', None)
        if process is None:
            return
        receipt = None
        if process.poll() is None:
            try:
                process.stdin.write(b'quit\n'); process.stdin.flush()
                receipt = self._header()
                process.wait(timeout=3)
            except (BrokenPipeError, OSError, ValueError, RuntimeError, subprocess.TimeoutExpired):
                process.terminate()
                try: process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()
        for stream in (process.stdin, process.stdout):
            if stream: stream.close()
        self.process = None
        return receipt


def disciplined_capture_extent(samples, sample_rate, maximum_correction, coordinates, taps):
    """Public endpoint envelope, not an inferred physical end or tracking proof.

    Each endpoint's logical/native rate is in [1/(1+c),1+c].
    Include both software maps, declared affine-clock candidates and positive
    arrival/echo extent. Rational arithmetic keeps large integer endpoints.
    """
    c = Fraction(str(maximum_correction))
    clock = Fraction(str(max(abs(row[3]) for row in coordinates))) / 1_000_000
    if not 0 < c <= Fraction(1, 1000) or not 0 <= clock < 1:
        raise ValueError('invalid public continuous-clock capture envelope')
    arrival = max(Fraction(0), Fraction(str(max(row[1] for row in coordinates))) +
                  Fraction(str(max(row[0] for row in taps))))
    extent = (samples + arrival*sample_rate) * (1+c)**2 / (1-clock)
    # Outward guard for decimal CLI/binary-rate conversions and the small
    # number of floating point operations defining the native rate bounds.
    # Keep the final ceil/integer arithmetic exact even at large coordinates.
    extent = extent * (1 + Fraction(64, 1 << 52)) + 2
    frames = (extent.numerator + extent.denominator - 1)//extent.denominator
    if not samples <= frames < (1 << 64):
        raise ValueError('continuous-clock capture endpoint exceeds sample counter')
    return int(frames)


def tracking_source_basis(source, coordinates, taps, offset, timing_forward, audio_center_hz,
                          *, template_backend='auto', cache_bytes=32*1024**2):
    """Physical ADC coordinates + public remote source-offset/rate states.

    Existing acquisition CFO includes the base affine-clock IF contribution.
    Subtract that contribution before mapped-basis adds the complete IF map,
    so the old CFO convention is retained rather than counted twice.
    """
    if template_backend not in ('auto', 'direct'):
        raise ValueError('tracking templates must use auto or direct')
    template_source = source
    if template_backend == 'auto':
        from triad_tracking_basis import CachedMappedBasis
        template_source = CachedMappedBasis(source, cache_bytes=cache_bytes)
    def render(states, anchor_seconds, first, stop, physical_origin, physical_offsets):
        count = stop-first
        rows = []
        for bit in (0, 1):
            for state in states:
                epoch, arrival, frequency, clock = coordinates[state.group]
                base = 1+clock*1e-6
                velocity = state.rate_index*timing_forward.rate_step_ppm*1e-6
                displacement = state.delay_index*timing_forward.delay_step_seconds
                scale = base+velocity
                if scale <= 0: raise ValueError('tracking source rate is nonpositive')
                mapped_arrival = (base*(arrival+offset)-displacement+velocity*anchor_seconds)/scale
                for delay, doppler in taps:
                    rows.append((bit, epoch, mapped_arrival,
                        frequency-audio_center_hz*(base-1), (scale-1)*1e6, delay, doppler))
        data = np.empty((len(rows), count), np.complex128)
        positions = np.asarray(physical_offsets, dtype=np.float64)
        for lo in range(0, count, 16384):
            hi = min(count, lo+16384)
            shift = math.floor(float(positions[lo]))
            for col in range(0, len(rows), 128):
                data[col:col+128, lo:hi] = template_source.basis_mapped(int(physical_origin)+shift,
                    positions[lo:hi]-shift, rows[col:col+128], audio_center_hz)
        result = np.zeros((3, len(states), len(taps), count), np.complex128)
        result[1:] = data.reshape(2, len(states), len(taps), count)
        return result
    render.cache_report = (template_source.report if template_backend == 'auto'
                           else lambda: dict(method='direct-native-mapped-reference', cache_limit_bytes=0))
    return render


def make_timing_forward(bank, descriptor, model_count):
    from triad_timing_forward import SparseTimingForward, TimingState
    raw = bank.get('tracking', {})
    if not isinstance(raw, dict):
        raise ValueError('public tracking configuration must be an object')
    config = dict(raw)
    rates = config.pop('initial_rate_indices', [0])
    delays = config.pop('initial_delay_indices', [0])
    segment_seconds = config.pop('segment_seconds', None)
    bandwidth = float(descriptor.get('nominal_triplet_bandwidth_hz', 0))
    center = abs(float(descriptor.get('data_if_hz', 0)))
    if not math.isfinite(bandwidth) or bandwidth <= 0 or not math.isfinite(center):
        raise ValueError('tracking requires finite resolved native waveform bandwidth and IF metadata')
    if segment_seconds is None:
        segment_seconds = 4.
        # Slow complex-baseband patterns need not branch every four seconds.
        # Bound within-segment envelope motion to a quarter delay cell and
        # acceleration curvature to the same scale. These are geometric design
        # budgets, not measured waveform-loss or arbitrary-trajectory promises.
        # Keep the existing short cadence for audio IF and broad/spread chips.
        if bandwidth < 1. and center < 1.:
            delay_cell = float(config.get('delay_step_seconds', 1/(4*bandwidth)))
            rate = float(config.get('maximum_rate_ppm', 2000.))*1e-6
            acceleration = float(config.get('maximum_acceleration_ppm_per_second', 20.))*1e-6
            rate_cell = float(config.get('rate_step_ppm', 50.))*1e-6
            limits = [4096.]
            if rate > 0: limits.append(.25*delay_cell/rate)
            if acceleration > 0:
                limits.append(math.sqrt(max(0., 2*delay_cell/acceleration)))
                # Envelope slowness alone does not bound common IF phase.
                # At most 1/16 cycle curvature for a local affine template.
                limits.append(math.sqrt(.5/((center+bandwidth/2)*acceleration)))
            if rate_cell > 0: limits.append(.25/((center+bandwidth/2)*rate_cell))
            segment_seconds = max(4., min(limits))
        # Oversampled low-rate input must not turn the slow update cadence into
        # an unbounded segment allocation. Explicit user durations still fail
        # preflight when they exceed the receiver's storage contract.
        fs = float(descriptor.get('sample_rate_hz', 0.))
        if fs:
            if not math.isfinite(fs) or fs <= 0:
                raise ValueError('finite positive tracking sample rate required')
            segment_seconds = min(segment_seconds, 1_048_576/fs)
        # Preserve explicit lattice indices and steps. For the automatic slow
        # envelope grid, cap midpoint frequency mismatch at 1/8 cycle (the
        # rectangular-integration design proxy is approximately 0.22 dB).
        if (bandwidth < 1. and center < 1. and 'rate_step_ppm' not in config and
            'initial_rate_indices' not in raw):
            config['rate_step_ppm'] = min(200., .25e6/((center+bandwidth/2)*segment_seconds))
    if (isinstance(segment_seconds, bool) or not isinstance(segment_seconds, (int, float)) or
        not math.isfinite(segment_seconds) or segment_seconds <= 0):
        raise ValueError('positive finite tracking segment duration required')
    if (not isinstance(rates, list) or not isinstance(delays, list) or not rates or not delays or
        any(isinstance(i, bool) or not isinstance(i, int) for i in [*rates, *delays]) or
        len(set(rates)) != len(rates) or len(set(delays)) != len(delays)):
        raise ValueError('distinct integer initial timing-state indices required')
    allowed = {'model_probabilities', 'delay_step_seconds', 'rate_step_ppm',
        'maximum_rate_ppm', 'maximum_acceleration_ppm_per_second', 'maximum_states', 'maximum_proposals'}
    if set(config)-allowed: raise ValueError('unknown public tracking configuration fields')
    config.setdefault('delay_step_seconds', 1/(4*bandwidth))
    proposal_cap = config.get('maximum_proposals', 256)
    if (isinstance(proposal_cap, bool) or not isinstance(proposal_cap, int) or
        not 1 <= proposal_cap <= 16384 or
        len(bank['coordinates'])*len(delays)*len(rates) > proposal_cap):
        raise ValueError('public initial timing grid must fit the bounded proposal cap')
    states = [TimingState(group, delay, rate) for group in range(len(bank['coordinates']))
              for delay in delays for rate in rates]
    tracker = SparseTimingForward(states, model_count=model_count, **config)
    if (tracker.maximum_acceleration_ppm_per_second > 0 and
        tracker.rate_index_bound > 0 and
        tracker.rate_step_ppm > tracker.maximum_acceleration_ppm_per_second*segment_seconds):
        raise ValueError('tracking segment is too short for a rate-grid transition; reduce rate_step_ppm or increase segment_seconds')
    return tracker, segment_seconds


def tracking_receiver_class():
    """The canonical tracker retains nominal and adaptive channel evidence."""
    from triad_tracking_receiver import PracticalTrackingReceiver
    return PracticalTrackingReceiver


def tracking_resource_report(receiver, descriptor, *, template_bytes=0,
                             template_backend='auto', memory_limit_bytes=None):
    """Source/hardware-bound work accounting, without inventing CPU precision."""
    import torch
    from triad_tracking_budget import count_tracking_work
    forward = receiver.timing
    if forward.observations:
        raise ValueError('tracking work must be frozen before receiving samples')
    args = receiver.receiver_arguments
    work = count_tracking_work(samples=receiver.samples, sample_rate_hz=receiver.sample_rate,
        block_samples=receiver.block_samples, segment_samples=receiver.segment_samples,
        initial_states=len(forward.states), maximum_states=forward.maximum_states,
        maximum_proposals=forward.maximum_proposals, rate_index_bound=forward.rate_index_bound,
        models=forward.model_count, taps=len(args['stationary_power']),
        precision=receiver.precision, projection_mode=args['projection_mode'],
        workspace_bytes=receiver.owned_bytes_estimate+template_bytes,
        fine_symbol_rate_hz=float(descriptor.get('fine_chip_rate_hz', 0.)),
        data_if_hz=abs(float(descriptor.get('data_if_hz', 0.))),
        maximum_relative_rate_ppm=forward.maximum_rate_ppm)
    nominal_plan = getattr(receiver, 'nominal_work_plan', None)
    if nominal_plan:
        nominal = count_tracking_work(samples=receiver.samples, sample_rate_hz=receiver.sample_rate,
            block_samples=receiver.block_samples, segment_samples=receiver.segment_samples,
            initial_states=nominal_plan['initial_states'], maximum_states=nominal_plan['maximum_states'],
            maximum_proposals=nominal_plan['maximum_proposals'], rate_index_bound=0,
            models=nominal_plan['models'], taps=nominal_plan['taps'], precision=receiver.precision,
            projection_mode=args['projection_mode'], workspace_bytes=nominal_plan['workspace_bytes'])
        # Fixed affine states retain their original anchor and execute one
        # identity transition each. Input/calendar counts remain shared; DSP
        # basis/likelihood work is performed separately and must be added.
        nominal['work']['transition_edges'] = nominal_plan['initial_states']*max(0, nominal['receiver']['segments']-1)
        work['lanes'] = dict(adaptive=dict(work['work']), initial_affine=nominal['work'])
        for key in ('basis_row_samples', 'gaussian_updates', 'transition_edges'):
            work['work'][key] += nominal['work'][key]
        work['work']['bound_scope'] += ' Includes both fixed-weight lanes; sample rows and segment calendar are shared counts.'
    cpu_model = platform.processor() or platform.machine()
    if Path('/proc/cpuinfo').exists():
        for line in Path('/proc/cpuinfo').read_text().splitlines():
            if line.startswith('model name'):
                cpu_model = line.split(':', 1)[-1].strip(); break
    identity = dict(source={name: file_identity(ROOT/'tools'/name)['sha256'] for name in
        ('triad_continuous.py', 'triad_tracking_receiver.py', 'triad_incremental_receiver.py',
         'triad_gaussian_state.py', 'triad_timing_forward.py', 'triad_tracking_basis.py',
         'triad_tracking_budget.py')}, hardware=dict(cpu=cpu_model, architecture=platform.machine(),
         torch_version=torch.__version__, threads=torch.get_num_threads()),
        regime=dict(receiver=type(receiver).__name__, sample_rate_hz=receiver.sample_rate,
            block_samples=receiver.block_samples,
            segment_samples=receiver.segment_samples, models=forward.model_count,
            taps=len(args['stationary_power']), precision=receiver.precision,
            coherence_seconds=[str(value) for value in args['coherence_seconds']],
            stationary_power=list(args['stationary_power']),
            required_bands=[list(band) for band in args['required_bands']],
            excluded_bands=[list(band) for band in args['excluded_bands']],
            waveform_descriptor={key: descriptor.get(key) for key in
                ('nominal_triplet_bandwidth_hz', 'fine_chip_rate_hz', 'data_if_hz')},
            delay_step_seconds=forward.delay_step_seconds, rate_step_ppm=forward.rate_step_ppm,
            maximum_rate_ppm=forward.maximum_rate_ppm,
            maximum_acceleration_ppm_per_second=forward.maximum_acceleration_ppm_per_second,
            projection=args['projection_mode'], template_backend=template_backend,
            template_bytes=template_bytes, maximum_states=forward.maximum_states,
            maximum_proposals=forward.maximum_proposals,
            nominal_plan=({key: nominal_plan[key] for key in
                ('initial_states', 'maximum_states', 'maximum_proposals', 'models', 'taps')}
                if nominal_plan else None)))
    work['memory_forecast'] = dict(available=memory_limit_bytes is not None,
        ceiling_bytes=memory_limit_bytes, estimated_bytes=work['workspace']['receiver_estimate_bytes'],
        status=('unknown' if memory_limit_bytes is None else
                'exceeded' if work['workspace']['receiver_estimate_bytes'] > memory_limit_bytes else 'within'),
        confidence='Receiver workspace estimate; allocator/RSS and external workspaces are separate.')
    work['identity'] = identity
    work['preparation_scope'] = 'Exact work counts only. Use hierarchical abbreviated tests for runtime prediction; no end-to-end CPU estimate follows from counts alone.'
    return work


def tracking_preflight(bank, descriptor, *, samples, sample_rate, block_seconds=.25,
                       coherences=(.5, 2., math.inf), precision='fp32', memory_mib=512,
                       chunk_samples=4096, projection_mode='valid-rows',
                       template_backend='auto'):
    """Bounded no-waveform preflight for the isolated duration/link planner."""
    descriptor = dict(descriptor, sample_rate_hz=sample_rate)
    forward, segment_seconds = make_timing_forward(bank, descriptor, len(coherences))
    memory = memory_mib*1024**2
    template_bytes = min(32*1024**2, memory//8) if template_backend == 'auto' else 0
    taps = bank.get('taps', [[0., 0.]])
    projection = bank.get('projection', {})
    receiver = tracking_receiver_class()(sample_rate=sample_rate, samples=samples,
        block_samples=max(1, round(block_seconds*sample_rate)),
        segment_samples=max(1, round(segment_seconds*sample_rate)),
        basis_provider=lambda *args: None, timing_forward=forward,
        required_bands=projection.get('required_bands_hz', [[-.49*sample_rate, .49*sample_rate]]),
        excluded_bands=projection.get('excluded_bands_hz', []),
        stationary_power=tuple(bank.get('stationary_power', [1/len(taps)]*len(taps))),
        coherence_seconds=coherences, precision=precision, projection_mode=projection_mode,
        memory_limit_bytes=memory-template_bytes, maximum_input_chunk_samples=chunk_samples,
        audio_center_hz=float(descriptor.get('data_if_hz', 0.)))
    return tracking_resource_report(receiver, descriptor, template_bytes=template_bytes,
        template_backend=template_backend, memory_limit_bytes=memory)
