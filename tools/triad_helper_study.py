#!/usr/bin/env python3
"""Actual sampled Helper-to-three-carrier acquisition comparison.

The receiver consumes public manifests, keyed templates and noisy I/Q only.
Evaluator truth is read by the CLI only after both receiver outcomes exist.
Empirical admission must calibrate this entire adaptive procedure, not each
selected frequency grid separately.  No operational admission claim is made.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import time

import numpy as np

from triad_helper_assistance import estimate_frequency
from triad_helper_receiver import HelperDataReceiver, data_fir_taps


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def public_geometry(m):
    """Exclude realization, on/off controls, timing costs and file locations."""
    return {"rate": m['sample_rate_hz'], "format": {k: v for k, v in m['format'].items()
             if k != 'requested_bit_seconds'}, "calendar": m['calendar'],
            "helper_plan": m['helper_plan'], "channel": m['channel'],
            "key_seed": m['key_seed'], "epoch": m['public_epoch_seconds'],
            "data_gain": m['power']['template_data_gain']}


def frequency_grid(bound, duration):
    if not math.isfinite(bound) or bound <= 0 or not math.isfinite(duration) or duration <= 0:
        raise ValueError('positive finite public frequency bound/duration required')
    half = max(1, math.ceil(bound * 4 * duration))
    if half > 2047:
        raise ValueError('frequency bank exceeds bounded study work')
    return np.linspace(-bound, bound, 2 * half + 1)


class HelperComparison:
    def __init__(self, fixture_directory, template_directory=None, *, device='cpu',
                 precision='fp32', memory_limit_bytes=2 * 1024**3):
        start = time.perf_counter()
        folder = Path(fixture_directory)
        tfolder = Path(template_directory or folder)
        self.manifest = json.loads((folder / 'manifest.json').read_text())
        tm = json.loads((tfolder / 'manifest.json').read_text())
        m = self.manifest
        if m['schema'] != 'triad-helper-public-v1' or tm['schema'] != m['schema']:
            raise ValueError('unsupported native composite manifest')
        if m['format']['physical_rf_retuning']:
            raise ValueError('physical RF retuning is not qualified in this comparison')
        # Public templates are independent of the selected channel/noise. Their
        # source geometry and fixed calibration must match exactly.
        for key in ('format', 'key_seed', 'epoch', 'data_gain'):
            if public_geometry(m)[key] != public_geometry(tm)[key]:
                raise ValueError(f'template source mismatch: {key}')
        if not tm['templates']['enabled']:
            raise ValueError('explicit complete public source templates required')
        shape = tuple(tm['templates']['shape'])
        path = tfolder / tm['templates']['file']
        if path.stat().st_size != math.prod(shape) * 16:
            raise ValueError('public source template file size mismatch')
        self.template_hash = digest(path)
        templates = np.memmap(path, dtype='<c16', mode='r', shape=shape)
        self.original_bound = m['channel']['deterministic_cfo_bound_hz'] * m['channel']['spread_margin']
        self.frequencies = frequency_grid(self.original_bound, m['format']['bit_seconds'])
        c = m['calendar']
        self.receiver = HelperDataReceiver(
            templates, tm['templates']['epoch_offsets_seconds'],
            source_offset_seconds=c['data_sample_offset'] / m['sample_rate_hz'],
            capture_lead_seconds=c['capture_lead_seconds'], capture_rows=c['capture_samples'],
            arrival_bounds_seconds=c['arrival_prior_seconds'],
            delay_seconds=m['channel']['spread_delay_ms'] * .001,
            doppler_hz=m['channel']['spread_doppler_hz'],
            reference_variance=m['power']['complex_noise_variance'] * float(np.sum(data_fir_taps()**2)),
            frequency_offsets=self.frequencies, precision=precision, device=device,
            memory_limit_bytes=memory_limit_bytes)
        self.sources = {name: digest(Path(__file__).parent / name) for name in (
            'triad_helper_study.py', 'triad_helper_assistance.py', 'triad_helper_receiver.py')}
        identity = dict(geometry=public_geometry(m), template_sha256=self.template_hash,
                        raw_noise_variance=m['power']['complex_noise_variance'],
                        receiver_id=self.receiver.receiver_id, source_hashes=self.sources,
                        policy='rectangular-comb-public-36s-window-floor-.01Hz-v1')
        self.procedure_id = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
        self.preparation_seconds = time.perf_counter() - start

    def score(self, directory):
        start = time.perf_counter()
        directory = Path(directory)
        m = json.loads((directory / 'manifest.json').read_text())
        if public_geometry(m) != public_geometry(self.manifest):
            raise ValueError('capture geometry changed; prepare a new public comparison')
        if m['power']['complex_noise_variance'] != self.manifest['power']['complex_noise_variance']:
            raise ValueError('noise reference changed; new calibration required')
        shape = tuple(m['observation']['shape'])
        path = directory / m['observation']['file']
        if path.stat().st_size != math.prod(shape) * 16:
            raise ValueError('capture file size mismatch')
        raw = np.memmap(path, dtype='<c16', mode='r', shape=shape)
        valid = np.memmap(directory / m['validity']['file'], dtype='u1', mode='r')
        if valid.shape != raw.shape or np.any(valid != 1):
            raise ValueError('complete observation required; no silent erasure replacement')
        fs = m['sample_rate_hz']
        c, h, ch = m['calendar'], m['helper_plan'], m['channel']
        # Fixed public window: every allowed source has started. Its delayed
        # availability is explicitly reported, never relabeled a 36-s frontier.
        begin_seconds = c['capture_lead_seconds'] + c['arrival_prior_seconds'][1] + .4
        first, rows = round(begin_seconds * fs), round(36 * fs)
        earliest_end = c['capture_lead_seconds'] + c['helper_end_seconds'] + c['arrival_prior_seconds'][0]
        if first + rows > len(raw) or (first + rows) / fs >= earliest_end - .02:
            raise ValueError('public timing uncertainty leaves insufficient guaranteed Helper support')
        assistance = estimate_frequency(raw[first:first + rows], sample_rate=fs,
            data_if_hz=ch['nominal_conversion_if_hz'], low_pilot_hz=h['low_hz'],
            high_pilot_hz=h['high_hz'], original_frequencies=self.frequencies,
            prior_half_width_hz=self.original_bound,
            doppler_hz=ch['spread_doppler_hz'], slow_depth=ch['selective_slow_depth'],
            fast_depth=ch['selective_fast_depth'], noise_variance=m['power']['complex_noise_variance'],
            first_sample_seconds=first / fs - c['capture_lead_seconds'],
            phase_diffusion_radians_per_sqrt_second=ch['relative_phase_diffusion_rad_sqrt_s'],
            audio_error_bound_ppm=ch['relative_pair_ppm_bound'],
            pilot_dwell_gating=h['pilot_dwell_gating'])
        prepared = self.receiver.prepare_observation(raw, valid=valid)
        baseline = self.receiver.score_prepared(prepared)
        narrowed = assistance['selected_frequencies']
        if list(narrowed) == list(self.frequencies):
            assisted = dict(baseline)
            assisted['score_reused_from_baseline'] = True
            added_scoring_seconds = 0.
        else:
            mark = time.perf_counter()
            assisted = self.receiver.with_frequency_offsets(narrowed).score_prepared(prepared)
            added_scoring_seconds = time.perf_counter() - mark
            assisted['score_reused_from_baseline'] = False
        # Stable identity is the entire adaptive procedure, including its fixed
        # selection rule; individual bank identities remain in each outcome.
        return dict(schema='triad-helper-comparison-v1', procedure_id=self.procedure_id,
            input_sha256=digest(path), template_sha256=self.template_hash,
            source_hashes=self.sources, assistance=assistance,
            baseline=baseline, assisted=assisted,
            timing=dict(public_preparation_seconds=self.preparation_seconds,
                        helper_seconds=assistance['wall_seconds'],
                        additional_assisted_scoring_seconds=added_scoring_seconds,
                        command_score_seconds=time.perf_counter() - start),
            limits=['Entire adaptive procedure needs independent Helper-present/DATA-absent calibration.',
                    'Static two-ray DATA receiver omits additional selective-AM structure.',
                    'Default arrival and thirteen epoch banks retained; fractional timing/clock not exhaustive.',
                    'No operational daily false-admission, radio-retune or phase-sensitivity qualification.'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fixture', required=True, type=Path)
    p.add_argument('--templates', type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--device', default='cpu')
    p.add_argument('--precision', choices=('fp32', 'fp64'), default='fp32')
    p.add_argument('--evaluate', action='store_true')
    args = p.parse_args()
    if args.output.exists():
        p.error('output already exists')
    study = HelperComparison(args.fixture, args.templates, device=args.device, precision=args.precision)
    result = study.score(args.fixture)
    if args.evaluate:
        result['evaluator_truth'] = json.loads((args.fixture / 'evaluator-truth.json').read_text())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')
    print(json.dumps({'output': str(args.output), 'timing': result['timing'],
                      'assistance': result['assistance']['status']}))


if __name__ == '__main__':
    main()
