#!/usr/bin/env python3
"""Bounded full-DAC-rate AES-noise qualification, without a receiver campaign.

The native renderer creates a fresh ephemeral key for every noisy output. The
paired clean output removes the deterministic signal and resampler tails; keys
are never requested or saved. All spectra describe PCM before the analog DAC.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

import numpy as np
from scipy import signal, stats

from triad_stream_frontend import StreamingAudioDDC


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def measure_noise(added, expected, sample_rate):
    """Measure total power and disjoint PSD bands, including above source Nyquist."""
    power = float(np.mean(added * added))
    frequency, psd = signal.welch(added, fs=sample_rate, nperseg=4096,
        noverlap=2048, detrend=False)
    bands = []
    for low, high in ((100, 2000), (4000, 8000), (8000, 14000),
                      (14000, 20000), (20000, 23500)):
        selected = (frequency >= low) & (frequency < high)
        measured = float(np.mean(psd[selected]))
        bands.append(dict(range_hz=[low, high], measured_psd=measured,
            expected_one_sided_psd=2 * expected / sample_rate,
            error_db=float(10 * np.log10(measured / (2 * expected / sample_rate)))))
    # Disjoint blocks give a transparent descriptive interval for mean power.
    # This is not a simultaneous future-performance guarantee or a receiver-null
    # calibration. Acceptance also has an explicit loose engineering envelope.
    block_length = 4096
    block_count = len(added) // block_length
    block_power = np.mean(added[:block_count * block_length].reshape(
        block_count, block_length) ** 2, axis=1)
    center = float(block_power.mean())
    half_width = float(stats.t.ppf(.975, block_count - 1) *
        block_power.std(ddof=1) / np.sqrt(block_count))
    lag_one = float(np.dot(added[1:], added[:-1]) / np.dot(added, added))
    error_db = float(10 * np.log10(power / expected))
    return dict(expected_variance=expected, measured_mean_square=power,
        variance_error_db=error_db, bands=bands,
        descriptive_mean_power_interval_95=[center - half_width, center + half_width],
        interval_method='Student-t of disjoint 4096-sample mean-square blocks; '
                        'approximate, per case, assuming independent AES sample draws',
        lag_one_correlation=lag_one,
        acceptance=dict(total_power_error_db=.15, per_band_error_db=.5,
                        absolute_lag_one=.015),
        passed=abs(error_db) < .15 and all(abs(b['error_db']) < .5 for b in bands)
               and abs(lag_one) < .015)


def ddc_measurement(added, expected):
    frontend = StreamingAudioDDC(48000, 6000, 1725., precision='fp64')
    values = []
    for first in range(0, len(added), 4096):
        projected, valid, _ = frontend.process(added[first:first + 4096])
        values.append(projected[valid])
    output = np.concatenate(values)
    metadata = frontend.metadata()
    predicted = expected * metadata['white_real_pcm_variance_to_complex_output_variance']
    measured = float(np.mean(np.abs(output) ** 2))
    error = float(10 * np.log10(measured / predicted))
    return dict(expected_complex_variance=predicted, measured_complex_variance=measured,
        error_db=error, valid_output_samples=len(output), metadata=metadata,
        assumptions='Independent real sample noise; linear fixed DDC with its actual FIR '
                    'energy, complete startup history excluded; no channel or whitening claim.',
        acceptance_error_db=.25, passed=abs(error) < .25)


def render(tool, root, name, source, input_rate, output_format, flags):
    output = root / (name + '.' + output_format)
    metadata = root / (name + '.json')
    command = [str(tool), '--input', str(source), '--output', str(output),
        '--metadata', str(metadata), '--format', 'f32', '--output-format', output_format,
        '--input-rate', str(input_rate), '--sample-rate', '48000', *flags]
    started = time.perf_counter()
    subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)
    elapsed = time.perf_counter() - started
    receipt = json.loads(metadata.read_text())
    dtype, scale = ('<i2', 32767.) if output_format == 's16' else ('<f4', 1.)
    values = np.fromfile(output, dtype).astype(float) / scale
    return values, dict(command=command, wall_seconds=elapsed, receipt=receipt,
        output_sha256=sha(output))


def qualify(tool, root, seconds=4):
    cases = [
        ('6k-to-48k-bits10-float', 6000, 'f32', 1., ['--bits', '10'], 2. ** -20 / 3),
        ('8k-to-48k-db40-s16', 8000, 's16', 1., ['--db', '40'], .5e-4),
        ('8k-to-48k-fixed-reference-gain', 8000, 'f32', .25,
            ['--noise-watts', '.001', '--reference-watts', '40', '--reference-rms', '.1'], 2.5e-7),
        ('48k-db60-s16', 48000, 's16', 1., ['--db', '60'], .5e-6),
    ]
    rows = []
    negative_control = None
    for name, input_rate, output_format, gain, flags, expected in cases:
        source = root / (name + '-source.f32')
        samples = (.15 * np.sin(2 * np.pi * 1737 *
            np.arange(input_rate * seconds) / input_rate)).astype('<f4')
        samples.tofile(source)
        shared = ['--signal-gain', str(gain)]
        clean, clean_info = render(tool, root, name + '-off', source,
            input_rate, output_format, shared)
        noisy, noise_info = render(tool, root, name + '-on', source,
            input_rate, output_format, [*shared, *flags])
        if clean.shape != noisy.shape:
            raise RuntimeError('Off/On output endpoints differ')
        added = noisy - clean
        measured = measure_noise(added, expected, 48000)
        projected = ddc_measurement(added, expected)
        if negative_control is None:
            # Keep total power correct while deliberately creating the defect
            # this qualification targets: noise confined to source bandwidth.
            # Reuse actual AES output, not a second synthetic random generator.
            confined = signal.sosfilt(signal.butter(8, input_rate / 2,
                fs=48000, output='sos'), added)
            confined *= np.sqrt(expected / np.mean(confined ** 2))
            diagnostic = measure_noise(confined, expected, 48000)
            negative_control = dict(name='correct-power-but-source-band-limited',
                intentional_lowpass_hz=input_rate / 2, measurement=diagnostic,
                detected=not diagnostic['passed'])
        receipt = noise_info['receipt']
        metadata_correct = (receipt['sample_rate_hz'] == 48000 and
            receipt['input_rate_hz'] == input_rate and receipt['samples'] == len(noisy) and
            abs(receipt['expected_noise_variance'] / expected - 1) < 1e-12 and
            receipt['clipped_samples'] == 0)
        rows.append(dict(name=name, input_rate_hz=input_rate, output_rate_hz=48000,
            output_format=output_format, output_samples=len(noisy),
            source_sha256=sha(source), off=clean_info, on=noise_info,
            noise=measured, ddc=projected, metadata_correct=metadata_correct,
            passed=measured['passed'] and projected['passed'] and metadata_correct))

    # Full signed range is deliberately checked only with noise Off. With noise
    # On, no finite PCM range can retain these endpoint codes without headroom.
    codes = np.arange(-32768, 32768, dtype='<i2')
    source = root / 'all-s16-codes.input'; codes.tofile(source)
    output = root / 'all-s16-codes.output'; metadata = root / 'all-s16-codes.json'
    command = [str(tool), '--input', str(source), '--output', str(output),
        '--metadata', str(metadata), '--format', 's16', '--sample-rate', '48000']
    subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)
    exact = dict(name='off-preserves-all-s16-codes', samples=len(codes), command=command,
        output_sha256=sha(output), passed=source.read_bytes() == output.read_bytes())
    return dict(schema='triad-fullrate-noise-qualification-v1', tool_sha256=sha(tool),
        qualification_source_sha256=sha(__file__),
        ddc_source_sha256=sha(Path(__file__).with_name('triad_stream_frontend.py')),
        observation_seconds_per_case=seconds, rows=rows, exact_s16=exact,
        negative_control=negative_control,
        scope='Digital full-rate PCM renderer using the production shared output stage. '
              'Not a physical device, browser runtime, RF model, or receiver admission test.',
        passed=all(row['passed'] for row in rows) and exact['passed']
               and negative_control['detected'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=Path('build/dev-cli/background_noise'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=4)
    args = parser.parse_args()
    if not 2 <= args.seconds <= 30:
        parser.error('--seconds must be between 2 and 30')
    args.output.mkdir(parents=True, exist_ok=False)
    started = time.perf_counter()
    report = qualify(args.tool.resolve(), args.output.resolve(), args.seconds)
    report['wall_seconds'] = time.perf_counter() - started
    (args.output / 'report.json').write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    print(json.dumps(dict(passed=report['passed'], cases=len(report['rows']) + 1,
        wall_seconds=report['wall_seconds'],
        power_errors_db=[row['noise']['variance_error_db'] for row in report['rows']],
        ddc_errors_db=[row['ddc']['error_db'] for row in report['rows']])))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
