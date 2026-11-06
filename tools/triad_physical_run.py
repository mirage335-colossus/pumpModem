"""Profile-bound physical admission using original ADC samples.

The full-audio clock controller/ASRC remains active. Only its pre-ASRC monitor
feeds this fixed public likelihood, so the declared null is in native ADC
coordinates. This is an isolated scheduled-bit receiver, not message framing or
an unconditional physical false-admission certificate.
"""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import re
import resource
import subprocess
import time
from types import SimpleNamespace

import numpy as np

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        while part := stream.read(1 << 20):
            h.update(part)
    return h.hexdigest()


def settings(a):
    """Public likelihood choices frozen before the first ADC sample."""
    return dict(precision=a.precision, coherences=a.coherences,
        fixed_gain=a.fixed_gain, block_seconds=a.block_seconds,
        tracking_templates=a.tracking_templates,
        clock_max_ppm=a.clock_max_ppm, clock_phase_max_ppm=a.clock_phase_max_ppm,
        clock_slew_ppm_per_second=a.clock_slew_ppm_per_second,
        clock_phase_seconds=a.clock_phase_seconds)


def collect_binding(a, declared):
    """Refresh observable identity; operating assumptions remain explicit.

    Hardware rate/channel declarations are verified against the native receipt
    after capture. A physical noise condition such as remote injected noise is
    an assumption, never inferred from the payload or fabricated by this call.
    """
    from triad_physical_admission import source_sha256
    match = re.fullmatch(r'hw:(\d+),(\d+)', a.device)
    if match is None:
        raise ValueError('qualified physical capture requires an explicit native ALSA hw:C,D endpoint')
    mixer = subprocess.run(['amixer', '-c', match[1], 'contents'], check=True,
        capture_output=True, timeout=10).stdout
    if not mixer.strip():
        raise ValueError('cannot bind an empty ALSA mixer snapshot')
    binding = dict(declared)
    pipeline = ('triad_physical_run.py', 'triad_continuous.py',
        'triad_audio_mapping.py', 'triad_gaussian_state.py',
        'triad_tracking_basis.py', 'triad_operational_admission.py')
    source_ids = {name: digest(ROOT/'tools'/name) for name in pipeline}
    binding.update(backend='native-alsa-adc-v1', device=a.device,
        mixer_sha256=hashlib.sha256(mixer).hexdigest(),
        audio_binary_sha256=digest(a.audio_native),
        receiver_sha256=source_sha256(), source_binary_sha256=digest(a.native),
        bank_sha256=digest(a.bank),
        pipeline_sha256=hashlib.sha256(json.dumps(source_ids, sort_keys=True).encode()).hexdigest(),
        settings_sha256=hashlib.sha256(json.dumps(settings(a), sort_keys=True,
            allow_nan=False).encode()).hexdigest())
    return binding


def observation_geometry(a, descriptor, bank, adc_rate):
    from triad_continuous import disciplined_capture_extent
    fs = int(descriptor['sample_rate_hz'])
    nominal = int(descriptor['total_samples'])
    if isinstance(adc_rate, bool) or not isinstance(adc_rate, int) or not 1 <= adc_rate <= 120_000_000:
        raise ValueError('integer native ADC rate required')
    endpoint = disciplined_capture_extent(nominal, fs, a.clock_max_ppm*1e-6,
        bank['coordinates'], bank.get('taps', [[0., 0.]]))
    frames = (endpoint*adc_rate+fs-1)//fs
    block = round(a.block_seconds*adc_rate)
    if not 1 <= block <= 32768:
        raise ValueError('physical block must fit bounded 65536-row final merge')
    projection = bank.get('projection', {})
    bands = projection.get('required_bands_hz')
    if not isinstance(bands, list) or not bands:
        raise ValueError('physical fixed bank requires public baseband DATA projection bands')
    if projection.get('excluded_bands_hz'):
        raise ValueError('physical projection uses explicit retained bands; exclusions are not supported')
    center = float(descriptor['data_if_hz'])
    positive = [[center+float(lo), center+float(hi)] for lo, hi in bands]
    if any(not 0 < lo <= hi < adc_rate/2 for lo, hi in positive):
        raise ValueError('public DATA bands must map strictly between ADC DC and Nyquist')
    return dict(sample_rate=adc_rate, samples=frames, block_samples=block,
        data_if_hz=center, required_bands_hz=positive, fixed_gain=a.fixed_gain,
        capture_seconds=frames/adc_rate)


def validate_receipt(receipt, binding, frames):
    if (not isinstance(receipt, dict) or receipt.get('schema') != 'triad-audio-stream-v1' or
            receipt.get('mode') != 'capture' or receipt.get('complete') is not True or
            receipt.get('capture_format') != 'adc-v1' or
            receipt.get('capture_observation') != 'pre-asrc-native-mono' or
            receipt.get('requested_frames') != frames or receipt.get('completed_frames') != frames or
            receipt.get('clipped_samples') != 0 or
            receipt.get('sample_rate_hz') != binding['hardware_rate_hz']):
        raise ValueError('physical admission requires a complete unclipped native ADC receipt')
    fmt = receipt.get('capture_stream_format', {})
    expected = dict(logical_rate=binding['hardware_rate_hz'],
        hardware_rate=binding['hardware_rate_hz'], logical_channels=1,
        hardware_channels=binding['hardware_channels'],
        selected_capture_channel=binding['selected_channel'])
    if fmt != expected:
        raise ValueError('captured ADC format differs from frozen physical profile')
    clock = receipt.get('clock_discipline', {})
    if (clock.get('enabled') is not True or clock.get('reports', 0) <= 0 or
            receipt.get('discarded_post_asrc_frames', 0) <= 0 or
            receipt.get('observed_native_frames', 0) < frames):
        raise ValueError('physical admission requires the active full-audio clock/ASRC witness')
    # Native tool aborts rather than issuing a complete receipt on a capture
    # discontinuity. Retain explicit rejection for future/forwarded receipts.
    if receipt.get('discontinuity') or receipt.get('capture_discontinuity'):
        raise ValueError('discontinuous ADC capture is not qualified')
    return True


def read_receipt(path):
    found = []
    for line in Path(path).read_text().splitlines():
        try:
            item = json.loads(line)
        except ValueError:
            continue
        if isinstance(item, dict) and item.get('schema') == 'triad-audio-stream-v1' and item.get('mode') == 'capture':
            found.append(item)
    if len(found) != 1:
        raise ValueError('exactly one completed native ADC capture receipt required')
    return found[0]


def make_basis(source, coordinates, taps, adc_rate, *, template_backend='auto', cache_bytes=32*1024**2):
    """Render physical complex templates at the original ADC sample times."""
    from triad_audio_mapping import rescale_coordinates
    from triad_continuous import tracking_source_basis, carrier_phase
    from triad_timing_forward import TimingState
    d = source.descriptor
    fs, center = int(d['sample_rate_hz']), float(d['data_if_hz'])
    forward = SimpleNamespace(rate_step_ppm=1., delay_step_seconds=1.)
    states = [TimingState(i, 0, 0) for i in range(len(coordinates))]
    render = tracking_source_basis(source, coordinates, taps,
        d['data_offset_samples']/fs, forward, center,
        template_backend=template_backend, cache_bytes=cache_bytes)
    def basis(first, stop):
        origin, offsets = rescale_coordinates(first,
            np.arange(stop-first, dtype=np.float64), adc_rate, fs)
        z = render(states, 0., first, stop, origin, offsets)
        z *= np.exp(1j*carrier_phase(first, stop-first, center, adc_rate))
        return z
    return basis, render.cache_report


class PreparedPhysical:
    """Public prepared receiver reused by the CLI and short duplex experiments."""
    def __init__(self, a, native_args, log):
        from triad_continuous import NativeStream, load_bank
        from triad_physical_admission import PhysicalReceiver, load_profile
        import torch
        if (a.mode != 'receive' or a.follow_system_clock != 'continuous' or
                a.limit_seconds is not None or a.noise_variance is not None):
            raise ValueError('physical profile requires complete receive mode, continuous clock, and profile-owned noise proxy')
        extras = json.loads(a.audio_arguments)
        if extras not in ([], ['--exclusive']):
            raise ValueError('qualified ADC capture accepts only --exclusive as an extra audio option')
        if '--clock-follow-bounded' in native_args:
            raise ValueError('source-only clock steering cannot qualify full-audio ADC capture')
        self.a = a
        self.source = None
        self.next_sample = 0
        self.closed = False
        self.bank, coordinates, taps = load_bank(a.bank)
        if 'tracking' in self.bank:
            raise ValueError('physical profile lane uses a fixed public bank; adaptive tracking fields must be omitted')
        raw = json.loads(a.physical_noise_profile.read_text())
        self.binding = collect_binding(a, raw.get('binding', {}))
        try:
            self.source = NativeStream(a.native, native_args, kind='emitted', log=log)
            self.descriptor = d = self.source.descriptor
            if d['helper_enabled']:
                raise ValueError('physical ADC profile does not qualify nonzero Helper leakage under H0')
            geometry = observation_geometry(a, d, self.bank, self.binding['hardware_rate_hz'])
            self.sample_rate, self.frames = geometry['sample_rate'], geometry['samples']
            self.profile = load_profile(a.physical_noise_profile, self.binding,
                **{k: v for k, v in geometry.items() if k != 'samples'})
            basis, self.basis_report = make_basis(self.source, coordinates, taps, self.sample_rate,
                template_backend=a.tracking_templates,
                cache_bytes=min(32*1024**2, a.memory_mib*1024**2//8))
            torch.set_num_threads(a.threads)
            self.receiver = PhysicalReceiver(sample_rate=self.sample_rate, samples=self.frames,
                block_samples=geometry['block_samples'], basis=basis,
                candidate_count=len(coordinates), required_bands_hz=geometry['required_bands_hz'],
                variance_proxy=self.profile.variance_proxy, fixed_gain=a.fixed_gain,
                stationary_power=self.bank.get('stationary_power', [1/len(taps)]*len(taps)),
                coherence_seconds=[float(x) for x in a.coherences.split(',')],
                candidate_probabilities=self.bank.get('candidate_probabilities'),
                model_probabilities=self.bank.get('model_probabilities'), precision=a.precision,
                memory_limit_bytes=a.memory_mib*1024**2-min(32*1024**2,a.memory_mib*1024**2//8),
                maximum_input_chunk_samples=a.chunk_samples)
            self.geometry = geometry
        except BaseException:
            self.close()
            raise

    def push(self, first, pcm, origin, offsets, rate):
        offsets = np.asarray(offsets)
        if (first != self.next_sample or rate != self.sample_rate or origin != first or
                offsets.shape != (len(pcm),) or not np.array_equal(offsets, np.arange(len(pcm)))):
            raise ValueError('physical admission requires the uniform original native ADC coordinate grid')
        self.receiver.push(first, pcm)
        self.next_sample += len(pcm)

    def finish(self, receipt):
        from triad_physical_admission import physical_decision
        from triad_operational_admission import operational_threshold
        validate_receipt(receipt, self.binding, self.frames)
        if collect_binding(self.a, self.binding) != self.binding:
            raise ValueError('physical hardware/source/mixer binding changed during capture')
        result = self.receiver.finish()
        complete = self.next_sample == self.frames and result['complete_observation']
        policy = operational_threshold(alpha_per_day=self.a.alpha_per_day)
        decision = physical_decision(result['bit_scores'], complete=complete,
            profile=self.profile, log_threshold=policy['log_e_threshold'])
        return dict(schema='triad-continuous-physical-v1', mode='receive',
            status='complete' if complete else 'incomplete', complete=complete,
            processed_samples=self.next_sample, processed_seconds=self.next_sample/self.sample_rate,
            receiver=result, source=self.descriptor, bank=self.bank,
            physical_profile=self.profile.report(), binding=self.binding,
            observation_geometry=self.geometry, audio_receipt=receipt,
            template_cache=self.basis_report(), admission_policy=policy,
            system_clock_policy='continuous',
            observation='native ADC before active ASRC; fixed public bank',
            receiver_uses_private_transmitter_trace=False,
            multi_day_hardware_uptime_qualified=False, **decision)

    def close(self):
        if self.source is not None:
            self.source.close()
            self.source = None
        self.closed = True


def prepare(a, native_args, log=None):
    return PreparedPhysical(a, native_args, log)


def execute(a, native_args):
    from triad_audio_mapping import MappedCaptureReader
    if a.report.exists():
        raise FileExistsError(a.report)
    if a.physical_save_pcm and a.physical_save_pcm.exists():
        raise FileExistsError(a.physical_save_pcm)
    a.report.parent.mkdir(parents=True, exist_ok=True)
    began, cpu = time.perf_counter(), time.process_time()
    child = resource.getrusage(resource.RUSAGE_CHILDREN)
    result = dict(schema='triad-continuous-physical-v1', complete=False, admitted=False,
        status='failed', physical_null_qualified=False)
    prepared = audio = output = None
    path = a.report.with_suffix('.stderr.log')
    try:
        with path.open('x') as log:
            prepared = prepare(a, native_args, log)
            command = [str(a.audio_native), '--mode', 'capture', '--sample-rate', str(prepared.sample_rate),
                '--frames', str(prepared.frames), '--device', a.device, '--exclusive',
                '--capture-format', 'adc-v1', '--clock-discipline', 'continuous',
                '--clock-max-ppm', str(a.clock_max_ppm), '--clock-phase-max-ppm', str(a.clock_phase_max_ppm),
                '--clock-slew-ppm-per-second', str(a.clock_slew_ppm_per_second),
                '--clock-phase-seconds', str(a.clock_phase_seconds)]
            audio = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=log)
            reader = MappedCaptureReader(audio.stdout)
            if a.physical_save_pcm:
                output = a.physical_save_pcm.open('xb')
            result['preparation_wall_seconds'] = time.perf_counter()-began
            while prepared.next_sample < prepared.frames:
                first = prepared.next_sample
                pcm, origin, offsets, rate = reader.read(min(a.chunk_samples, prepared.frames-first))
                if output is not None:
                    output.write(pcm.astype('<f4', copy=False).tobytes())
                prepared.push(first, pcm, origin, offsets, rate)
            if audio.wait(timeout=10):
                raise RuntimeError('native ADC capture failed; see retained stderr')
            log.flush()
            result.update(prepared.finish(read_receipt(path)), audio_command=command)
    except BaseException as error:
        result.update(status='failed', error=type(error).__name__+': '+str(error), admitted=False)
        raise
    finally:
        if output is not None:
            output.close()
        if audio is not None and audio.poll() is None:
            audio.terminate()
            try: audio.wait(timeout=3)
            except subprocess.TimeoutExpired:
                audio.kill(); audio.wait()
        if prepared is not None:
            prepared.close()
        usage = resource.getrusage(resource.RUSAGE_CHILDREN)
        result.update(wall_seconds=time.perf_counter()-began, python_cpu_seconds=time.process_time()-cpu,
            child_cpu_seconds=usage.ru_utime+usage.ru_stime-child.ru_utime-child.ru_stime)
        if result.get('processed_seconds'):
            result['all_process_cpu_seconds_per_input_second']=(result['python_cpu_seconds']+result['child_cpu_seconds'])/result['processed_seconds']
        a.report.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n')
    return result
