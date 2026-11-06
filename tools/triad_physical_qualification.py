#!/usr/bin/env python3
"""Short, explicitly requested cable qualification of a frozen ADC profile.

The spec fixes receiver arguments, source arguments, playback gain, public
start delay and full-DAC noise before input. Truth selects only TX/evaluation;
the receiver sees both keyed alternatives. No mixer or host-clock changes.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import resource
import subprocess
import threading
import time

import numpy as np

from triad_audio_mapping import MappedCaptureReader
from triad_continuous import NativeStream, carrier_phase, parser as receiver_parser
from triad_physical_run import prepare, read_receipt


def playback_receipt(path, *, frames, sample_rate, noise_db, signal_peak, hardware_rates):
    if (not isinstance(hardware_rates, list) or not hardware_rates or
        any(isinstance(x, bool) or not isinstance(x, int) or x <= 0 for x in hardware_rates)):
        raise ValueError('profile must declare allowed_noise_dac_rates_hz for duplex qualification')
    records = []
    for line in Path(path).read_text().splitlines():
        try: item = json.loads(line)
        except ValueError: continue
        if item.get('schema') == 'triad-audio-stream-v1' and item.get('mode') == 'play':
            records.append(item)
    if len(records) != 1:
        raise ValueError('exactly one successful playback receipt required')
    r = records[0]
    noise = r.get('background_noise', {})
    if (r.get('complete') is not True or r.get('requested_frames') != frames or
        r.get('completed_frames') != frames or r.get('sample_rate_hz') != sample_rate or
        r.get('clipped_samples') != 0 or r.get('clock_discipline', {}).get('enabled') is not True or
        noise.get('enabled') is not True or noise.get('active_samples', 0) <= 0 or
        noise.get('sample_rate_hz') not in hardware_rates or
        not math.isclose(noise.get('expected_variance', -1), .5*10**(-noise_db/10), rel_tol=1e-10) or
        not math.isfinite(noise.get('peak', math.inf)) or signal_peak+noise['peak'] >= .5):
        raise ValueError('playback/noise configuration or conservative headroom check failed')
    return r


def run(spec_path, output, bit):
    spec = json.loads(Path(spec_path).read_text())
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    a = receiver_parser().parse_args([*spec['receiver_arguments'], '--mode', 'receive',
        '--report', str(output/'result.json')])
    a.follow_system_clock = 'continuous'
    native_args = spec['native_arguments']
    if '--bit' in native_args:
        raise ValueError('source truth belongs only to the separate transmitter')
    delay = float(spec.get('play_after_seconds', 1.))
    gain = float(spec['transmit_gain'])
    noise_db = float(spec['background_noise_db'])
    if not 0 < delay < 10 or not 0 < gain < 1 or not 0 <= noise_db <= 200:
        raise ValueError('invalid bounded cable settings')
    (output/'protocol.json').write_text(json.dumps(dict(spec=spec,
        spec_sha256=hashlib.sha256(Path(spec_path).read_bytes()).hexdigest(),
        expected_bit_evaluation_only=bit,
        receiver_uses_transmit_truth=False), indent=2)+'\n')
    receiver = tx = capture = playback = thread = None
    failures = []
    sent = 0
    signal_energy = 0.
    signal_peak = 0.
    child_before = resource.getrusage(resource.RUSAGE_CHILDREN)
    started, cpu = time.perf_counter(), time.process_time()
    result = dict(complete=False, admitted=False, status='failed')
    with (output/'source.log').open('x') as source_log, (output/'capture.log').open('x') as capture_log, \
         (output/'playback.log').open('x') as play_log, (output/'adc.f32').open('xb') as raw:
        try:
            receiver = prepare(a, native_args, source_log)
            tx = NativeStream(a.native, [*native_args, '--bit', str(0 if bit is None else bit)],
                kind='emitted', log=source_log)
            d = tx.descriptor
            fs, total = int(d['sample_rate_hz']), int(d['total_samples'])
            if total/fs > 300 or receiver.frames/receiver.sample_rate > 310:
                raise ValueError('short cable qualification is limited to five-minute transmissions')
            trigger = round(delay*receiver.sample_rate)
            def send():
                nonlocal sent, signal_energy, signal_peak
                try:
                    while sent < total:
                        values, _ = tx.next(min(4096, total-sent))
                        wave = np.sqrt(2)*gain*(values*np.exp(1j*carrier_phase(
                            sent, len(values), d['data_if_hz'], fs))).real
                        if bit is None:
                            wave[:] = 0
                        signal_energy += float(wave@wave)/fs
                        signal_peak = max(signal_peak, float(abs(wave).max()))
                        playback.stdin.write(wave.astype('<f4').tobytes())
                        sent += len(values)
                    playback.stdin.close()
                except BaseException as exc:
                    failures.append(repr(exc))
            command = [str(a.audio_native), '--mode', 'capture', '--sample-rate', str(receiver.sample_rate),
                '--frames', str(receiver.frames), '--device', a.device, '--exclusive',
                '--capture-format', 'adc-v1', '--clock-discipline', 'continuous',
                '--clock-max-ppm', str(a.clock_max_ppm), '--clock-phase-max-ppm', str(a.clock_phase_max_ppm),
                '--clock-slew-ppm-per-second', str(a.clock_slew_ppm_per_second),
                '--clock-phase-seconds', str(a.clock_phase_seconds)]
            (output/'capture-command.json').write_text(json.dumps(command, indent=2)+'\n')
            capture = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=capture_log)
            reader = MappedCaptureReader(capture.stdout)
            result['preparation_wall_seconds'] = time.perf_counter()-started
            while receiver.next_sample < receiver.frames:
                first = receiver.next_sample
                count = min(a.chunk_samples, receiver.frames-first)
                if first < trigger:
                    count = min(count, trigger-first)
                pcm, origin, offsets, rate = reader.read(count)
                raw.write(pcm.astype('<f4', copy=False).tobytes())
                receiver.push(first, pcm, origin, offsets, rate)
                if playback is None and receiver.next_sample >= trigger:
                    play_command = [str(a.audio_native), '--mode', 'play', '--sample-rate', str(fs),
                        '--frames', str(total), '--device', a.device, '--exclusive',
                        '--background-noise-db', str(noise_db)]
                    (output/'playback-command.json').write_text(json.dumps(play_command, indent=2)+'\n')
                    playback = subprocess.Popen(play_command, stdin=subprocess.PIPE, stderr=play_log)
                    thread = threading.Thread(target=send)
                    thread.start()
            if thread:
                thread.join(15)
                if thread.is_alive():
                    raise RuntimeError('playback generation did not finish')
            for proc in (capture, playback):
                if proc is None or proc.wait(timeout=15):
                    raise RuntimeError('native audio did not finish cleanly')
            if failures:
                raise RuntimeError('; '.join(failures))
            capture_log.flush()
            play_log.flush()
            played = playback_receipt(output/'playback.log', frames=total, sample_rate=fs,
                noise_db=noise_db, signal_peak=signal_peak,
                hardware_rates=receiver.binding.get('allowed_noise_dac_rates_hz'))
            preparation = result['preparation_wall_seconds']
            result = receiver.finish(read_receipt(output/'capture.log'))
            result['playback_receipt'] = played
            result['preparation_wall_seconds'] = preparation
        except BaseException as exc:
            failures.append(repr(exc))
            raise
        finally:
            for proc in (capture, playback):
                if proc and proc.poll() is None:
                    proc.terminate()
                    try: proc.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        proc.kill(); proc.wait()
            if thread: thread.join(3)
            if tx: tx.close()
            if receiver: receiver.close()
            child_after = resource.getrusage(resource.RUSAGE_CHILDREN)
            result.update(wall_seconds=time.perf_counter()-started, python_cpu_seconds=time.process_time()-cpu,
                child_cpu_seconds=child_after.ru_utime+child_after.ru_stime-child_before.ru_utime-child_before.ru_stime,
                errors=failures, expected_bit_evaluation_only=bit, transmitted_samples=sent,
                transmitted_signal_peak=signal_peak,
                transmitted_signal_energy_fs_squared_seconds=signal_energy)
            if result.get('processed_seconds'):
                result['all_process_cpu_seconds_per_input_second'] = (
                    result['python_cpu_seconds']+result['child_cpu_seconds'])/result['processed_seconds']
            (output/'result.json').write_text(json.dumps(result, indent=2, allow_nan=False)+'\n')
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--spec', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--bit', choices=('0', '1', 'null'), required=True)
    a = p.parse_args()
    r = run(a.spec, a.output, None if a.bit == 'null' else int(a.bit))
    print(json.dumps({k:r.get(k) for k in ('complete','admitted','candidate_bit','log_e',
        'wall_seconds','all_process_cpu_seconds_per_input_second')}))


if __name__ == '__main__': main()
