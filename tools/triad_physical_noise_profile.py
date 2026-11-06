#!/usr/bin/env python3
"""Freeze an ADC noise envelope, then check a separate physical capture.

The checks are inexpensive diagnostics of a declared physical model. A Welch
maximum, finite tail sample, or passing holdout does NOT prove a joint
sub-Gaussian bound for future observations. The resulting admission profile is
explicitly conditional on that bound and its stated operating conditions.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import time

import numpy as np
from scipy import signal


PREPARED_SCHEMA = 'triad-physical-noise-preparation-v1'
PROFILE_SCHEMA = 'triad-physical-noise-profile-v1'
PSD_CONVENTION = 'global-real-welch-interior-fs-over-two-endpoints-fs-v1'


def sha256(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        while block := stream.read(1 << 20):
            result.update(block)
    return result.hexdigest()


def _positive(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
        raise ValueError(f'{name} must be finite and positive')
    return float(value)


def _receipt(metadata, count, sample_rate, binding):
    """Native receipt integrity, distinct from a statistical noise check."""
    if metadata.get('schema') == 'triad-audio-capture-v1':
        capture = metadata.get('capture', {})
        discontinuity = metadata.get('discontinuity', {})
        if (metadata.get('status') != 'complete' or metadata.get('failure') or
                metadata.get('capture_error') or discontinuity.get('capture') or
                discontinuity.get('playback') or capture.get('raw_samples') != count or
                capture.get('raw_rate') != sample_rate):
            raise ValueError('incomplete/discontinuous native ADC characterization receipt')
        fmt = metadata.get('audio_format', {}).get('capture', {})
        mode = 'pre-ASRC ADC characterization; active clock following not asserted'
    elif metadata.get('schema') == 'triad-audio-stream-v1':
        if (metadata.get('mode') != 'capture' or metadata.get('complete') is not True or
                metadata.get('capture_format') != 'adc-v1' or
                metadata.get('capture_observation') != 'pre-asrc-native-mono' or
                metadata.get('completed_frames') != count or
                metadata.get('requested_frames') != count or
                metadata.get('clipped_samples') != 0 or
                metadata.get('clock_discipline', {}).get('enabled') is not True or
                metadata.get('clock_discipline', {}).get('reports', 0) <= 0 or
                metadata.get('discarded_post_asrc_frames', 0) <= 0):
            raise ValueError('incomplete/unqualified pre-ASRC native stream receipt')
        fmt = metadata.get('capture_stream_format', {})
        mode = 'pre-ASRC ADC with active full-audio clock/ASRC pipeline'
    else:
        raise ValueError('unsupported native ADC receipt')
    if (fmt.get('hardware_rate') != sample_rate or
            fmt.get('hardware_channels') != binding.get('hardware_channels') or
            fmt.get('selected_capture_channel') != binding.get('selected_channel')):
        raise ValueError('native ADC format does not match profile binding')
    return mode


def characterize(path, metadata_path, *, geometry, binding, trim_seconds=2., window_samples=4096):
    """Compact diagnostics; never estimates a payload-dependent receiver scale."""
    fs = int(_positive(geometry['sample_rate_hz'], 'sample rate'))
    block = int(_positive(geometry['block_samples'], 'block samples'))
    if (not isinstance(window_samples, int) or isinstance(window_samples, bool) or
            not 64 <= window_samples <= 65536 or not 0 <= trim_seconds <= 60 or
            not math.isfinite(trim_seconds)):
        raise ValueError('invalid fixed diagnostic window/trim')
    path, metadata_path = Path(path), Path(metadata_path)
    if path.stat().st_size % 4:
        raise ValueError('raw ADC file ends inside a float32 sample')
    raw = np.memmap(path, dtype='<f4', mode='r')
    metadata = json.loads(metadata_path.read_text())
    receipt_scope = _receipt(metadata, len(raw), fs, binding)
    trim = round(trim_seconds*fs)
    stop = len(raw)-trim
    if stop-trim < max(2*block, 8*window_samples):
        raise ValueError('noise capture too short after fixed endpoint trimming')
    # Characterization is short by contract; bounded at 10 minutes to avoid an
    # accidental multi-day allocation. Streaming reception has a separate API.
    if stop-trim > 600*fs:
        raise ValueError('noise characterization is bounded to ten minutes')
    x = np.asarray(raw[trim:stop], dtype=np.float64)
    if not np.isfinite(x).all():
        raise ValueError('nonfinite ADC samples')
    mean = float(np.mean(x))
    centered = x-mean
    frequencies, psd = signal.welch(centered, fs=fs, nperseg=window_samples,
        noverlap=window_samples//2, detrend=False, scaling='density')
    # One-sided interior PSD S has coordinate variance S*Fs/2. scipy leaves
    # DC and (for an even FFT) Nyquist undoubled, so those bins require S*Fs.
    # Removing the one global mean does not remove each Welch segment's DC;
    # slow random variation must remain in a GLOBAL covariance diagnostic.
    # This finite estimator is a diagnostic, not a bound on the true spectrum.
    positive = (frequencies > 0) & (frequencies < fs/2)
    observed_proxy = float(np.max(psd[positive])*fs/2)
    boundary = ~positive
    global_proxy = max(observed_proxy, float(np.max(psd[boundary])*fs))
    nblocks = len(centered)//block
    blocks = centered[:nblocks*block].reshape(nblocks, block)
    means = blocks.mean(axis=1)
    variances = blocks.var(axis=1)
    band_summaries = []
    for lo, hi in geometry['required_bands_hz']:
        mask = (frequencies >= lo) & (frequencies <= hi)
        if not mask.any():
            raise ValueError('diagnostic FFT has no bins in a required band')
        band_summaries.append(dict(band_hz=[lo, hi], mean_psd=float(psd[mask].mean()),
                                   maximum_psd=float(psd[mask].max())))
    return dict(raw_sha256=sha256(path), metadata_sha256=sha256(metadata_path),
        raw_samples=len(raw), selected_samples=len(x), selected_interval_samples=[trim, stop],
        raw_file_mtime_ns=path.stat().st_mtime_ns, receipt_scope=receipt_scope,
        sample_rate_hz=fs, window_samples=window_samples, block_samples=block,
        trim_seconds=trim_seconds, mean=mean, variance=float(centered.var()),
        peak=float(np.max(np.abs(x))), clipping_samples=int(np.sum(np.abs(x) >= .999)),
        centered_peak=float(np.max(np.abs(centered))),
        observed_positive_psd_variance_proxy=observed_proxy,
        observed_global_psd_variance_proxy=global_proxy,
        maximum_block_variance=float(np.max(variances)),
        maximum_centered_block_mean=float(np.max(np.abs(means))),
        block_count=nblocks, bands=band_summaries)


def checks_for(diagnostic, variance_proxy):
    """Fixed engineering rejection checks, not confidence intervals."""
    v = _positive(variance_proxy, 'variance proxy')
    return [
        dict(name='no_clipping', passed=diagnostic['clipping_samples'] == 0),
        # Retain the schema-v1 check name; its stronger global check includes
        # undoubled DC/Nyquist bins as well as the positive-frequency interior.
        dict(name='observed_positive_psd_within_declared_proxy',
             passed=diagnostic['observed_global_psd_variance_proxy'] <= v),
        dict(name='observed_variance_within_declared_proxy', passed=diagnostic['variance'] <= v),
        dict(name='every_observed_block_variance_within_declared_proxy',
             passed=diagnostic['maximum_block_variance'] <= v),
        dict(name='centered_peak_within_ten_proxy_standard_deviations',
             passed=diagnostic['centered_peak'] <= 10*math.sqrt(v)),
        dict(name='block_mean_motion_within_eight_proxy_standard_errors',
             passed=diagnostic['maximum_centered_block_mean'] <=
                    8*math.sqrt(v/diagnostic['block_samples'])),
    ]


def prepare(calibration, metadata, *, binding, geometry, variance_proxy, assumptions,
            trim_seconds=2., window_samples=4096):
    if (not isinstance(assumptions, list) or not assumptions or
            any(not isinstance(x, str) or not x.strip() for x in assumptions)):
        raise ValueError('explicit physical null assumptions required')
    v = _positive(variance_proxy, 'variance proxy')
    diagnostic = characterize(calibration, metadata, geometry=geometry, binding=binding,
        trim_seconds=trim_seconds, window_samples=window_samples)
    checks = checks_for(diagnostic, v)
    if not all(x['passed'] for x in checks):
        raise ValueError('calibration contradicts the declared proxy: '+
                         ', '.join(x['name'] for x in checks if not x['passed']))
    return dict(schema=PREPARED_SCHEMA, prepared_unix_ns=time.time_ns(),
        binding=binding, geometry=geometry,
        null_model=dict(kind='global-real-subgaussian-proxy-v1', variance_proxy=v,
            assumptions=assumptions, dc_component='constant-only', bound_scope='conditional-on-profile'),
        calibration=diagnostic, calibration_checks=checks,
        diagnostic_policy=dict(trim_seconds=trim_seconds, window_samples=window_samples,
            psd_proxy_convention=PSD_CONVENTION,
            statistical_scope='Fixed engineering contradiction checks; no probability coverage or future joint-mgf guarantee.'),
        limitations=['This finite capture does not establish an unconditional daily false-admission rate.',
                    'The joint sub-Gaussian proxy must hold across the complete received observation, including cross-block dependence.',
                    'Nonconstant deterministic interference, clipping, changed noise settings and changed hardware/mixer conditions are outside this profile.'])


def finalize(preparation_path, validation, metadata):
    preparation_path = Path(preparation_path)
    p = json.loads(preparation_path.read_text())
    if p.get('schema') != PREPARED_SCHEMA:
        raise ValueError('unsupported frozen noise preparation')
    if p.get('diagnostic_policy', {}).get('psd_proxy_convention') != PSD_CONVENTION:
        raise ValueError('noise preparation lacks the global DC/Nyquist PSD diagnostic; prepare a new frozen record')
    if Path(validation).stat().st_mtime_ns <= p['prepared_unix_ns']:
        raise ValueError('validation capture must be produced after the frozen preparation')
    diagnostic = characterize(validation, metadata, geometry=p['geometry'], binding=p['binding'],
                             **{k:p['diagnostic_policy'][k] for k in ('trim_seconds','window_samples')})
    if diagnostic['raw_sha256'] == p['calibration']['raw_sha256']:
        raise ValueError('validation must be an independent capture, not calibration replay')
    checks = checks_for(diagnostic, p['null_model']['variance_proxy'])
    combined = [dict(name='calibration:'+x['name'], passed=x['passed']) for x in p['calibration_checks']]
    combined += [dict(name='validation:'+x['name'], passed=x['passed']) for x in checks]
    profile = dict(schema=PROFILE_SCHEMA, binding=p['binding'], geometry=p['geometry'],
        null_model=p['null_model'], qualification=dict(
            passed=all(x['passed'] for x in combined),
            preparation_sha256=sha256(preparation_path),
            calibration_sha256=p['calibration']['raw_sha256'], validation_sha256=diagnostic['raw_sha256'],
            calibration_samples=p['calibration']['selected_samples'], validation_samples=diagnostic['selected_samples'],
            checks=combined, calibration=p['calibration'], validation=diagnostic,
            diagnostic_policy=p['diagnostic_policy'], limitations=p['limitations'],
            independence_scope='Distinct newer capture and hashes checked; acquisition independence is an operator/model assumption, not authenticated by filesystem timestamps.'))
    if profile['qualification']['passed']:
        # Reuse the admission implementation's strict schema/source validator.
        from triad_physical_admission import validate_profile
        g = p['geometry']
        validate_profile(profile, p['binding'], sample_rate=g['sample_rate_hz'],
            block_samples=g['block_samples'], data_if_hz=g['data_if_hz'],
            required_bands_hz=g['required_bands_hz'], fixed_gain=g['fixed_gain'],
            capture_seconds=g['maximum_capture_seconds'])
    return profile


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='operation', required=True)
    p = sub.add_parser('prepare')
    p.add_argument('--calibration', type=Path, required=True)
    p.add_argument('--metadata', type=Path, required=True)
    p.add_argument('--binding', type=Path, required=True)
    p.add_argument('--geometry', type=Path, required=True)
    p.add_argument('--variance-proxy', type=float, required=True)
    p.add_argument('--assumptions', type=Path, required=True, help='JSON list of explicit physical-model assumptions')
    p.add_argument('--trim-seconds', type=float, default=2.)
    p.add_argument('--window-samples', type=int, default=4096)
    p.add_argument('--output', type=Path, required=True)
    p = sub.add_parser('finalize')
    p.add_argument('--preparation', type=Path, required=True)
    p.add_argument('--validation', type=Path, required=True)
    p.add_argument('--metadata', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    if args.output.exists():
        raise FileExistsError(args.output)
    if args.operation == 'prepare':
        result = prepare(args.calibration, args.metadata,
            binding=json.loads(args.binding.read_text()), geometry=json.loads(args.geometry.read_text()),
            variance_proxy=args.variance_proxy, assumptions=json.loads(args.assumptions.read_text()),
            trim_seconds=args.trim_seconds, window_samples=args.window_samples)
    else:
        result = finalize(args.preparation, args.validation, args.metadata)
    with args.output.open('x') as out:
        json.dump(result, out, indent=2, allow_nan=False)
        out.write('\n')
    if args.operation == 'finalize' and not result['qualification']['passed']:
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
