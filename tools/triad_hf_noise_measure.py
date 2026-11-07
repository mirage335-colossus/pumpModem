#!/usr/bin/env python3
"""Focused full-audio AES-noise HF reception and local CPU measurement.

This is an opt-in single scheduled-bit experiment, not a CI campaign or a
completed-message receiver. The radio channel is already in the source fixture;
the added full-DAC noise is an explicitly post-channel laboratory impairment.
No evaluator truth is read by this program.
"""
from __future__ import annotations

import time
IMPORT_CPU, IMPORT_WALL = time.process_time(), time.perf_counter()
import argparse
import json
import math
import os
from pathlib import Path
import platform
import resource
import subprocess
import sys

import numpy as np
from scipy import signal
import torch

from triad_stream_run import (StreamSession, digest, live_pcm, prepare_pcm,
                              replay_pcm, write_json)
from triad_stream_slots import load_slots
from triad_weak_receiver import WeakReceiver


def cpu_children():
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    return usage.ru_utime + usage.ru_stime


def noise_plan(manifest, target_db_hz, gain):
    """Calibrate PSD, not integrated variance after the narrow DDC filter."""
    if (manifest['power'].get('data_power_reference_policy') != 'whole-active' or
        not math.isclose(float(manifest['power'].get('reference_normalized_data_mean_norm', 0)),
                         1., rel_tol=0., abs_tol=1e-12)):
        raise ValueError('study requires the exported whole-active unit DATA reference')
    fs = float(manifest['sample_rate_hz'])
    native_n0 = float(manifest['power']['complex_noise_variance']) / fs
    nominal_data_power = 1.  # Exporter's whole-active DATA normalization.
    target_n0 = nominal_data_power / 10.**(target_db_hz / 10.)
    added_n0 = target_n0 - native_n0
    if not math.isfinite(added_n0) or added_n0 <= 0:
        raise ValueError('target must require strictly more noise than the physical fixture')
    variance = 48000. / 2 * gain**2 * added_n0
    if 3 * variance >= 1:
        raise ValueError('full-audio noise alone exceeds DAC headroom; choose a lower fixed gain')
    return dict(target_nominal_data_cn0_db_hz=target_db_hz,
        target_nominal_helper_cn0_db_hz=target_db_hz + 10 * math.log10(
            float(manifest['power']['nominal_helper_to_data_ratio'])),
        native_noise_density=native_n0, target_noise_density=target_n0,
        added_noise_density=added_n0, real_dac_added_variance=variance,
        suppression_db=-10 * math.log10(2 * variance),
        receiver_noise_inflation=target_n0/native_n0,
        receiver_preprojection_complex_variance=target_n0*fs,
        fixed_gain=gain, nominal_data_power=nominal_data_power,
        ordering='Complete stochastic HF simulation, real-audio conversion, then shared full-rate AES noise; not pre-HF transmitted noise.',
        power_scope='Added noise is a laboratory impairment, not preservation of the 4W+40W total radiated-power budget.',
        normalization='to_audio uses sqrt(2)*gain*Re(IQ exp(j carrier t)); real one-sided PSD=2*variance/48000; DDC FIR energy is not the raw preprojection noise variance.')


def quiet_noise_metrology(pcm, original, *, gain, analog_gain, offset_frames):
    # Public earliest possible Helper, with an additional one-second guard.
    # The interval is selected without consulting the actual arrival or DATA.
    first_helper = original['whole_bit_calendar']['slots'][0]['helper_begin_sample'] / original['sample_rate_hz']
    quiet_end = first_helper + original['calendar']['arrival_prior_seconds'][0] - 1.
    quiet_begin = 1.
    if quiet_end - quiet_begin < 2:
        raise ValueError('public calendar lacks two seconds of guaranteed quiet metrology')
    data = np.memmap(pcm, dtype='<i2', mode='r').reshape(-1, 2)
    start = offset_frames + math.ceil(quiet_begin*48000)
    stop = offset_frames + math.floor(quiet_end*48000)
    if stop > len(data):
        raise ValueError('quiet metrology interval lies outside retained audio')
    values = data[start:stop, 0].astype(float)/32768.
    f, psd = signal.welch(values, fs=48000, nperseg=8192, noverlap=4096, detrend=False)
    bands = []
    for name, lo, hi in (('DATA', 1450, 2000), ('low-pilot', 450, 650),
                         ('high-pilot', 2300, 2500), ('upper-audio', 8000, 18000)):
        measured = float(np.mean(psd[(f >= lo) & (f < hi)]))
        if not math.isfinite(measured) or measured <= 0:
            raise ValueError('quiet PSD is not finite and positive')
        reference = gain**2 * analog_gain**2
        bands.append(dict(name=name, band_hz=[lo, hi], measured_one_sided_pcm_psd=measured,
            nominal_data_equivalent_cn0_db_hz=10*math.log10(reference/measured)))
    return dict(public_quiet_interval_frames=[start, stop], seconds=len(values)/48000,
        bands=bands, receiver_used_this_estimate=False,
        limitation='Finite quiet-sample PSD diagnostic using independent fixed analog gain. Nominal unit DATA reference; faded DATA energy is evaluated separately. No physical-null bound.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--public', type=Path, required=True)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=('replay', 'live'), default='replay')
    parser.add_argument('--exclusive', action='store_true')
    parser.add_argument('--device', default='hw:1,0')
    parser.add_argument('--target-cn0', type=float, default=8.)
    parser.add_argument('--fixed-gain', type=float, default=.003)
    parser.add_argument('--analog-gain', type=float, default=1.)
    parser.add_argument('--threads', type=int, default=2)
    parser.add_argument('--noise-tool', type=Path, default=Path('build/dev-cli/background_noise'))
    args = parser.parse_args()
    if args.mode == 'live' and not args.exclusive:
        parser.error('live operation requires --exclusive')
    if not all(math.isfinite(x) and x > 0 for x in (args.fixed_gain, args.analog_gain)):
        parser.error('positive fixed gains required')
    if args.mode == 'replay' and args.analog_gain != 1:
        parser.error('software replay has analog gain one')
    if args.threads not in (1, 2, 3, 4, 6):
        parser.error('choose 1,2,3,4 or6 numerical threads')
    args.output.mkdir(parents=True, exist_ok=False)
    source_paths = [Path(__file__).parent/p for p in
        ('triad_hf_noise_measure.py', 'triad_stream_run.py',
         'triad_weak_receiver.py', 'triad_stream_frontend.py')]
    source_hashes = {p.name:digest(p) for p in source_paths}
    source_revision = subprocess.check_output(['git','rev-parse','HEAD'], text=True).strip()
    write_json(args.output/'invocation.json', dict(argv=sys.argv,
        source_revision=source_revision, source_hashes=source_hashes))
    torch.set_num_threads(args.threads)
    torch.set_num_interop_threads(1)
    torch.set_float32_matmul_precision('highest')
    import_timing = dict(cpu_seconds=time.process_time()-IMPORT_CPU,
                         wall_seconds=time.perf_counter()-IMPORT_WALL)
    c, w = time.process_time(), time.perf_counter()
    original, slots = load_slots(args.public)
    if len(slots) != 1:
        raise ValueError('this bounded measurement requires exactly one public slot')
    loading = dict(cpu_seconds=time.process_time()-c, wall_seconds=time.perf_counter()-w)
    plan = noise_plan(original, args.target_cn0, args.fixed_gain)
    write_json(args.output/'noise-plan.json', plan)  # Frozen before noise or scoring.
    clean = args.output/'clean.stereo.s16le'
    bridge = prepare_pcm(args.public, original, clean, fixed_gain=args.fixed_gain,
        observation_file=args.capture/'observation.c128')
    write_json(args.output/'bridge.json', bridge)
    noisy, receipt = args.output/'noisy.stereo.s16le', args.output/'noise.json'
    command = [str(args.noise_tool.resolve()), '--input', str(clean.resolve()),
        '--output', str(noisy.resolve()), '--metadata', str(receipt.resolve()),
        '--format', 's16', '--channels', '2', '--sample-rate', '48000',
        '--db', repr(plan['suppression_db'])]
    write_json(args.output/'noise-command.json', command)
    subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)

    # Allocate and prepare from public configuration only. The per-observation
    # sparse refinement remains inside score(), and is charged to reception.
    c, w = time.process_time(), time.perf_counter()
    session = StreamSession(original, slots, args.output/'receiver',
        fixed_gain=args.fixed_gain, analog_gain=args.analog_gain,
        playback_offset_frames=48000, noise_inflation=plan['receiver_noise_inflation'],
        workers=1, fft_workers=args.threads)
    receiver = WeakReceiver(*session.slots[0], noise_inflation=plan['receiver_noise_inflation'],
        admission_policy=session.slots[0][0].get('weak_admission_policy'), fft_workers=args.threads)
    session.receiver_factory = lambda *unused: receiver
    preparation = dict(cpu_seconds=time.process_time()-c, wall_seconds=time.perf_counter()-w)
    c, w, children = time.process_time(), time.perf_counter(), cpu_children()
    transport, stream, failure = {}, {}, None
    try:
        if args.mode == 'live':
            transport = live_pcm(session, noisy, args.output, device=args.device,
                preroll_frames=48000, postroll_frames=96000, max_seconds=180., record_pcm=True)
        else:
            transport = replay_pcm(session, noisy, preroll_frames=48000,
                postroll_frames=96000, chunk_frames=2400)
    except Exception as error:
        failure = type(error).__name__+': '+str(error)
    finally:
        stream = session.finish(180.)
    receive = dict(cpu_seconds=time.process_time()-c,
        child_cpu_seconds=cpu_children()-children, wall_seconds=time.perf_counter()-w)
    observed = stream['capture']['seconds']
    slot = original['whole_bit_calendar']['slots'][0]
    denominators = dict(observed_audio=observed,
        analysis_window=slots[0][0]['calendar']['capture_samples']/original['sample_rate_hz'],
        transmitted_helper_and_data=(slot['helper_end_sample']-slot['helper_begin_sample'])/original['sample_rate_hz'],
        active_data=original['power']['data_reference_active_seconds'])
    receive['duration_denominators_seconds'] = denominators
    receive['cpu_per_second_by_duration'] = {
        name:dict(prepared=receive['cpu_seconds']/seconds,
            including_preparation=(receive['cpu_seconds']+preparation['cpu_seconds'])/seconds)
        for name, seconds in denominators.items() if seconds > 0}
    if observed > 0:
        receive['cpu_seconds_per_analog_second'] = receive['cpu_seconds']/observed
        receive['cpu_with_audio_children_per_analog_second'] = (receive['cpu_seconds']+receive['child_cpu_seconds'])/observed
        receive['including_preparation_per_analog_second'] = (receive['cpu_seconds']+preparation['cpu_seconds'])/observed
        receive['cold_including_import_load_preparation_per_analog_second'] = (
            receive['cpu_seconds']+preparation['cpu_seconds']+loading['cpu_seconds']+import_timing['cpu_seconds'])/observed
    scheduler_errors = [dict(index=r['index'], status=r['status'], error=r.get('error'))
        for r in stream['schedule']['records'] if r['status'] != 'scored']
    # No receiver decision uses this post-hoc noise estimate.
    measurement_pcm = noisy if args.mode == 'replay' else args.output/'capture.stereo.s16le'
    metrology, metrology_error = None, None
    if measurement_pcm.exists():
        try:
            metrology = quiet_noise_metrology(measurement_pcm, original, gain=args.fixed_gain,
                analog_gain=args.analog_gain, offset_frames=0 if args.mode=='replay' else 48000)
        except Exception as error:
            metrology_error = type(error).__name__+': '+str(error)
    else:
        metrology_error = 'No recorded PCM available for metrology'
    result = dict(schema='triad-hf-full-audio-noise-measurement-v1', mode=args.mode,
        source_revision=source_revision, source_hashes=source_hashes,
        source_identity_unchanged=all(digest(p)==source_hashes[p.name] for p in source_paths),
        public_manifest_sha256=digest(args.public/'manifest.json'),
        capture_manifest_sha256=digest(args.capture/'manifest.json'),
        observation_sha256=digest(args.capture/'observation.c128'),
        playback_sha256=digest(noisy), noise_tool_sha256=digest(args.noise_tool),
        cpu_model=next((line.split(':',1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines()
                       if line.startswith('model name')), platform.processor()),
        numerical_threads=args.threads, load_average=os.getloadavg(),
        imports=import_timing, public_loading=loading, receiver_preparation=preparation,
        reception=receive, noise_plan=plan, noise_receipt=json.loads(receipt.read_text()),
        metrology=metrology, metrology_error=metrology_error, stream=stream, transport=transport,
        measurement_execution_pass=not(failure or transport.get('failure') or scheduler_errors or
            not stream['schedule']['drained'] or observed <= 0),
        scheduler_errors=scheduler_errors,
        failure=failure or transport.get('failure'),
        timing_scope='Reception includes DDC, ingestion, Helper, acquisition, observation-dependent refinement preparation/scoring and drain; excludes TX generation, channel simulation, PCM generation and AES impairment synthesis. Stage process deltas overlap and must not be summed.',
        preparation_scope='Prepared screen/workspace is specific to public epoch/template/calendar/noise/precision. Candidate-dependent refinement preparation remains in reception. Raw keyed-template generation is separately measured.',
        message_completed=False, physical_null_qualified=False)
    write_json(args.output/'report.json', result)
    print(json.dumps(dict(failure=result['failure'], prepared_receive=receive,
        preparation=preparation, bits=stream['pending_candidate_bits'],
        measured_noise=metrology['bands'] if metrology else None), allow_nan=False))
    return int(not result['measurement_execution_pass'] or metrology_error is not None or
               not result['source_identity_unchanged'])


if __name__ == '__main__':
    raise SystemExit(main())
