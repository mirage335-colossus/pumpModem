#!/usr/bin/env python3
"""Bounded native-source / Gaussian-state streaming experiment.

The bank is fixed before receiving any samples. This is scheduled acquisition,
not automatic blind tracking. Only the declared endpoint permits admission.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import resource
import subprocess
import sys
import time

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
    supported = {'legacy-v1', 'independent-apsk-v2'}
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

    def clock_target(self, seconds):
        if not math.isfinite(seconds):
            raise ValueError('finite clock correction required')
        return self._request(f'clock {seconds:.17g}\n')

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


def load_bank(path):
    bank = json.loads(Path(path).read_text())
    coordinates = bank['coordinates']
    taps = bank.get('taps', [[0., 0.]])
    if not coordinates or len(coordinates)*len(taps)*2 > 128 or len(taps) > 16:
        raise ValueError('bank exceeds bounded 128 basis columns / 16 taps')
    if len({tuple(x) for x in coordinates}) != len(coordinates):
        raise ValueError('duplicate bank coordinates')
    for row in coordinates:
        if len(row) != 4 or isinstance(row[0], bool) or int(row[0]) != row[0] or abs(row[0]) > 6:
            raise ValueError('bank coordinates are [integer epoch offset, arrival seconds, frequency Hz, clock ppm]')
        if not all(math.isfinite(x) for x in row) or 1+row[3]*1e-6 <= 0:
            raise ValueError('invalid finite bank coordinate')
    if any(len(t) != 2 or not all(math.isfinite(x) for x in t) or t[0] < 0 for t in taps):
        raise ValueError('taps are [nonnegative delay seconds, Doppler Hz]')
    return bank, coordinates, taps


def source_basis(source, coordinates, taps, offset):
    rows = [(bit, epoch, arrival+offset, frequency, clock, delay, doppler)
        for bit in (0, 1) for epoch, arrival, frequency, clock in coordinates
        for delay, doppler in taps]
    def render(first, stop):
        count = stop-first
        data = source.basis(first, count, rows)
        result = np.zeros((3, len(coordinates), len(taps), count), np.complex128)
        result[1:] = data.reshape(2, len(coordinates), len(taps), count)
        return result
    return render


class BoundedClockControl:
    """OS corrections steer only the explicitly eligible DATA source.

    Control observations are not DAC timestamps or estimates of remote clock
    motion. Never change the epoch or give this private TX trace to the RX bank.
    """
    def __init__(self, clock):
        self.clock = clock
        if not clock.available:
            raise ValueError('system clock association unavailable at stream start')
        self.origin = clock.snapshot
        self.last_target = 0.
        self.report = dict(updates=0, outside_envelope=0, unavailable_polls=0,
                           last_target_seconds=0., last_response=None, status='following')

    def _status(self, value):
        if value != self.report['status']:
            print(f'triad_continuous: bounded clock-step following: {value}; '
                  'the established source trajectory and epoch are retained', file=sys.stderr)
            self.report['status'] = value

    def poll(self, source):
        snapshot = self.clock.poll()
        if snapshot is None:
            self.report['unavailable_polls'] += 1
            self._status('clock association unavailable')
            return
        target = (snapshot.offset_ns-self.origin.offset_ns)/1e9
        uncertainty = (snapshot.uncertainty_ns+self.origin.uncertainty_ns)/1e9
        if abs(target) > 2+uncertainty:
            self.report['outside_envelope'] += 1
            self._status('correction outside original +/-2-second envelope')
            return  # Keep the continuous established trajectory; never ratchet.
        self._status('following')
        target = max(-2., min(2., target))
        if abs(target-self.last_target) < max(.001, uncertainty):
            return  # Clock-read jitter must not continually requeue the servo.
        response = source.clock_target(target)
        if not response.get('accepted'):
            raise RuntimeError('native source rejected eligible clock correction')
        self.last_target = target
        self.report.update(updates=self.report['updates']+1,
                           last_target_seconds=target, last_response=response)


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mode', choices=('simulate', 'transmit', 'receive'), default='simulate')
    p.add_argument('--native', type=Path, default=ROOT/'build/dev-cli/triad_helper_export')
    p.add_argument('--audio-native', type=Path, default=ROOT/'build/dev-cli/triad_audio_stream')
    p.add_argument('--bank', type=Path, help='fixed public candidate bank JSON, required for reception')
    p.add_argument('--report', type=Path, required=True)
    p.add_argument('--block-seconds', type=float, default=.25)
    p.add_argument('--chunk-samples', type=int, default=4096)
    p.add_argument('--coherences', default='0.5,2,inf')
    p.add_argument('--precision', choices=('fp32', 'fp64'), default='fp32')
    p.add_argument('--threads', type=int, default=2)
    p.add_argument('--memory-mib', type=int, default=256)
    p.add_argument('--alpha-per-day', type=float, default=1e-4)
    p.add_argument('--noise-variance', type=float, help='fixed receiver variance; required for physical capture')
    p.add_argument('--limit-seconds', type=float, help='diagnostic prefix only; cannot admit an incomplete bit')
    p.add_argument('--device', default='default')
    p.add_argument('--fixed-gain', type=float, default=.002)
    p.add_argument('--audio-arguments', default='[]', help='JSON array of extra native audio options (e.g. full-rate background noise)')
    p.add_argument('--follow-system-clock', choices=('off', 'bounded-data'), default='off',
        help='bounded slow DATA calendar; TX follows +/-2s corrections, RX keeps nominal physical-time templates; no Helper/FHSS/burst')
    return p


def execute(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    split = argv.index('--') if '--' in argv else len(argv)
    a = parser().parse_args(argv[:split]); native_args = argv[split+1:]
    if a.report.exists(): raise FileExistsError(a.report)
    if not 1 <= a.chunk_samples <= 8192 or not 1 <= a.threads <= 64:
        raise ValueError('chunk must be 1..8192 and threads 1..64')
    if not math.isfinite(a.block_seconds) or a.block_seconds <= 0:
        raise ValueError('positive finite block duration required')
    if not math.isfinite(a.fixed_gain) or a.fixed_gain <= 0:
        raise ValueError('fixed audio gain must be finite and positive')
    if a.limit_seconds is not None and (not math.isfinite(a.limit_seconds) or a.limit_seconds <= 0):
        raise ValueError('positive finite prefix duration required')
    if a.mode != 'transmit' and a.bank is None: raise ValueError('reception requires --bank')
    if a.follow_system_clock != 'off' and a.mode == 'simulate':
        raise ValueError('bounded clock profile currently supports emitted TX/RX; use the injected native probe for simulated correction traces')
    if a.mode == 'receive' and (a.noise_variance is None or a.noise_variance <= 0):
        raise ValueError('physical reception requires a declared conservative --noise-variance')
    audio_arguments = json.loads(a.audio_arguments)
    if not isinstance(audio_arguments, list) or any(not isinstance(x, str) for x in audio_arguments):
        raise ValueError('audio-arguments must be a JSON string array')
    # Do not let convenience flags rewrite the pipe format or bounded calendar.
    allowed_audio = ('--background-noise-', '--exclusive')
    for i, item in enumerate(audio_arguments):
        if item.startswith('--') and not item.startswith(allowed_audio):
            raise ValueError('audio extra flags restricted to background noise and exclusive access')
    a.report.parent.mkdir(parents=True, exist_ok=True)
    result = dict(schema='triad-continuous-v1', mode=a.mode, complete=False,
        admitted=False, physical_null_qualified=False, acquisition='fixed public bank; no blind tracking',
        multi_day_hardware_uptime_qualified=False,
        identities=[file_identity(p) for p in (a.native, Path(__file__),
            ROOT/'tools/triad_incremental_receiver.py', ROOT/'tools/triad_gaussian_state.py')])
    if a.mode != 'simulate': result['identities'].append(file_identity(a.audio_native))
    child_before = resource.getrusage(resource.RUSAGE_CHILDREN)
    started, cpu = time.perf_counter(), time.process_time()
    source = audio = None
    clock_control = None
    if a.follow_system_clock == 'bounded-data':
        native_args += ['--clock-follow-bounded']
        result['identities'].append(file_identity(ROOT/'tools/triad_control_clock.py'))
        if a.mode == 'transmit':
            from triad_control_clock import ControlClock
            clock_control = BoundedClockControl(ControlClock())
    log_path = a.report.with_suffix('.stderr.log')
    with log_path.open('x') as log:
        try:
            source = NativeStream(a.native, native_args,
                kind='channel' if a.mode == 'simulate' else 'emitted', log=log)
            d = source.descriptor; fs = int(d['sample_rate_hz']); total = int(d['total_samples'])
            result.update(source=d, plan=source.plan, command=source.command)
            if a.follow_system_clock == 'bounded-data':
                result['clock_profile'] = dict(name='bounded-data',
                    local_corrections_steer_source=a.mode == 'transmit',
                    receiver_uses_private_transmitter_trace=False,
                    receiver_time='physical sample index; nominal affine bank unchanged')
            block_samples = max(1, round(a.block_seconds*fs))
            if 2*block_samples > 16384: raise ValueError('merged final block exceeds native bounded basis request')
            requested = total if a.limit_seconds is None else min(total, math.ceil(a.limit_seconds*fs))
            receiver = None; first = 0
            if a.mode != 'transmit':
                import torch
                from triad_incremental_receiver import IncrementalReceiver
                from triad_operational_admission import operational_threshold
                torch.set_num_threads(a.threads)
                bank, coordinates, taps = load_bank(a.bank)
                variance = receiver_noise_variance(a.noise_variance, float(d['noise_variance']),
                    simulated=a.mode == 'simulate')
                result['receiver_noise_variance'] = variance
                offset = float(d['data_offset_samples'])/fs
                if a.mode == 'simulate': offset += float(d['capture_lead_seconds'])
                projection = bank.get('projection', {})
                if d['helper_enabled'] and not projection.get('required_bands_hz'):
                    raise ValueError('Helper reception requires explicit public DATA projection bands in bank JSON')
                required = projection.get('required_bands_hz', [[-fs*.49, fs*.49]])
                excluded = projection.get('excluded_bands_hz', [])
                projection_mode = 'dft' if d['helper_enabled'] else 'valid-rows'
                # Physical capture uses a fixed quadrature mixer and declared
                # disjoint-block DFT projection. The public bank owns analog
                # latency; neither a payload lag nor noise variance is fitted.
                if a.mode == 'receive' and not projection.get('required_bands_hz'):
                    raise ValueError('real PCM capture requires explicit analytic-sideband projection bands')
                if a.mode == 'receive': projection_mode = 'dft'
                receiver = IncrementalReceiver(sample_rate=fs, samples=total,
                    block_samples=block_samples, basis=source_basis(source, coordinates, taps, offset),
                    required_bands=required, excluded_bands=excluded, noise_variance=variance,
                    stationary_power=tuple(bank.get('stationary_power', [1/len(taps)]*len(taps))),
                    coherence_seconds=tuple(float(x) for x in a.coherences.split(',')),
                    candidate_count=len(coordinates), projection_mode=projection_mode,
                    precision=a.precision, memory_limit_bytes=a.memory_mib*1024**2)
                result.update(bank=bank, bank_sha256=hashlib.sha256(a.bank.read_bytes()).hexdigest(),
                    admission_policy=operational_threshold(alpha_per_day=a.alpha_per_day),
                    helper_null_scope='Fixed DATA projection retains finite-window Helper leakage; physical/Helper null unqualified.')
            if a.mode in ('transmit', 'receive'):
                args = [str(a.audio_native), '--mode', 'play' if a.mode == 'transmit' else 'capture',
                    '--sample-rate', str(fs), '--frames', str(requested), '--device', a.device, *audio_arguments]
                audio = subprocess.Popen(args, stdin=subprocess.PIPE if a.mode == 'transmit' else subprocess.DEVNULL,
                    stdout=subprocess.PIPE if a.mode == 'receive' else subprocess.DEVNULL, stderr=log)
                result['audio_command'] = args
            result['preparation_wall_seconds'] = time.perf_counter()-started
            while first < requested:
                n = min(a.chunk_samples, requested-first)
                if clock_control is not None:
                    clock_control.poll(source)
                if a.mode == 'receive':
                    pcm = np.frombuffer(read_exact(audio.stdout, n*4), dtype='<f4')
                    phase = carrier_phase(first, n, float(d['data_if_hz']), fs)
                    values = pcm * (math.sqrt(2)/a.fixed_gain) * np.exp(-1j*phase)
                    valid = np.ones(n, np.uint8)
                else:
                    values, valid = source.next(n)
                    if len(values) != n: raise EOFError('native source ended before the public calendar')
                if a.mode == 'transmit':
                    phase = carrier_phase(first, n, float(d['data_if_hz']), fs)
                    pcm = (math.sqrt(2)*a.fixed_gain*np.real(values*np.exp(1j*phase))).astype('<f4')
                    if np.max(np.abs(pcm)) >= 1: raise ValueError('fixed playback gain clips; choose a lower gain')
                    audio.stdin.write(pcm.tobytes())
                else:
                    receiver.push(first, values, valid)
                first += n
            if audio is not None:
                if audio.stdin: audio.stdin.close()
                if audio.wait(timeout=30): raise RuntimeError('native audio stream failed; see stderr log')
            result.update(processed_samples=first, processed_seconds=first/fs,
                          complete=first == total)
            if receiver is not None:
                r = receiver.finish()
                result['receiver'] = r
                if result['complete'] and r['complete_observation']:
                    scores = np.asarray(r['bit_scores'], dtype=float).reshape(2)
                    result.update(endpoint_decision(scores, complete=True,
                        simulated=a.mode == 'simulate' and d.get('conditional_gaussian_null_supported', False),
                        helper_enabled=d['helper_enabled'],
                        log_threshold=result['admission_policy']['log_e_threshold']))
            result['status'] = 'complete' if result['complete'] else 'diagnostic_prefix_incomplete'
        except BaseException as error:
            result.update(status='failed', error=type(error).__name__+': '+str(error))
            raise
        finally:
            if clock_control is not None:
                result['system_clock_follow'] = dict(clock_control.report,
                    association=clock_control.clock.report(),
                    scope='Bounded slow DATA source coordinate; fixed epoch, physical audio NCO and nominal receiver basis. No remote timing inference.')
            if audio is not None and audio.poll() is None:
                audio.terminate()
                try: audio.wait(timeout=3)
                except subprocess.TimeoutExpired: audio.kill(); audio.wait()
            if source is not None: result['source_completion'] = source.close()
            usage = resource.getrusage(resource.RUSAGE_CHILDREN)
            result.update(wall_seconds=time.perf_counter()-started,
                python_cpu_seconds=time.process_time()-cpu,
                child_cpu_seconds=usage.ru_utime+usage.ru_stime-child_before.ru_utime-child_before.ru_stime,
                peak_python_rss_kib=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
                peak_child_rss_kib=usage.ru_maxrss)
            seconds = result.get('processed_seconds', 0)
            if seconds: result['all_process_cpu_seconds_per_input_second'] = (result['python_cpu_seconds']+result['child_cpu_seconds'])/seconds
            a.report.write_text(json.dumps(result, indent=2, allow_nan=False, default=lambda v: v.tolist() if isinstance(v,np.ndarray) else str(v))+'\n')
    return result


def main():
    try:
        r = execute()
        print(json.dumps({k:r.get(k) for k in ('status','complete','admitted','candidate_bit','processed_seconds','wall_seconds','all_process_cpu_seconds_per_input_second')}))
        return 0
    except (ValueError, OSError, RuntimeError, EOFError) as error:
        print('triad_continuous: '+str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
