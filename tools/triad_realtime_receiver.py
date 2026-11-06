#!/usr/bin/env python3
"""Bounded Helper/FFT acquisition followed by sparse dynamic DATA inference.

An isolated CPU experiment, not the ordinary Robust receiver. Screening is a
declared static-channel approximation; the final likelihood uses original I/Q.
Every adaptive stage belongs to the empirical admission procedure. No historical
gate or Gaussian false-alarm guarantee transfers to this selected-candidate path.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import time

import numpy as np
import scipy
import torch

from triad_dynamic_campaign import public_geometry
from triad_dynamic_joint import load_public
from triad_dynamic_variants import ScanJointReceiver, core_projection_bands
from triad_realtime_helper import estimate_helper
from triad_realtime_fft import FFTScreen


def _digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, allow_nan=False).encode()).hexdigest()


def _timed(function):
    cpu, wall = time.process_time(), time.perf_counter()
    result = function()
    return result, dict(cpu_seconds=time.process_time()-cpu,
                        wall_seconds=time.perf_counter()-wall)


def refinement_coordinates(candidates, manifest, *, clock_half_step_ppm,
                           timing_step_seconds, maximum_candidates=64):
    """Preserve DATA-midpoint alignment when refining correlated clock/arrival.

    The Cartesian product of unrelated candidate coordinates is intentionally
    never constructed. Both coarse bit lists must be supplied by the caller.
    """
    prior = manifest['calendar']['arrival_prior_seconds']
    channel, fmt = manifest['channel'], manifest['format']
    limit = float(channel['sample_clock_pair_ppm'])*float(channel.get('spread_margin', 1))
    center = (float(manifest['calendar']['data_sample_offset'])/manifest['sample_rate_hz']
              +float(fmt['bit_seconds'])/2)
    result, seen = [], set()
    for candidate in candidates:
        epoch = int(candidate['epoch'])
        arrival = float(candidate['arrival'])
        frequency = float(candidate['frequency'])
        old_q = float(candidate['clock_ppm'])
        old_scale = 1+old_q*1e-6
        midpoint = arrival+center/old_scale
        for delta_q in (-clock_half_step_ppm, 0., clock_half_step_ppm):
            q = min(limit, max(-limit, old_q+delta_q))
            adjusted = midpoint-center/(1+q*1e-6)
            for delta_t in (-timing_step_seconds, 0., timing_step_seconds):
                a = min(float(prior[1]), max(float(prior[0]), adjusted+delta_t))
                coordinate = (epoch, a, frequency, q)
                token = (epoch, round(a, 12), round(frequency, 12), round(q, 9))
                if token not in seen:
                    seen.add(token)
                    result.append(coordinate)
                    if len(result) > maximum_candidates:
                        raise ValueError('sparse refinement exceeds configured work budget')
    if not result:
        raise ValueError('no coarse candidates to refine')
    return tuple(result)


class RealtimeReceiver:
    """One public geometry/key; caches coarse templates, never received decisions."""
    def __init__(self, manifest, templates, epoch_offsets, *, precision='fp32',
                 output_sample_rate=1000, top_per_bit=2, clock_points=3,
                 maximum_refinement_candidates=64, helper_config=None,
                 memory_limit_bytes=2*1024**3):
        cpu, wall = time.process_time(), time.perf_counter()
        if precision not in ('fp32', 'fp64'):
            raise ValueError('precision must be fp32 or fp64')
        if clock_points < 1 or clock_points % 2 != 1 or top_per_bit < 1:
            raise ValueError('positive top count and odd positive clock count required')
        self.manifest = copy.deepcopy(manifest)
        self.templates, self.epochs = templates, tuple(epoch_offsets)
        self.precision, self.output_rate = precision, int(output_sample_rate)
        self.top_per_bit = int(top_per_bit)
        self.maximum_refinement_candidates = int(maximum_refinement_candidates)
        self.memory_limit_bytes = int(memory_limit_bytes)
        channel = manifest['channel']
        self.cfo_bound = float(channel['deterministic_cfo_bound_hz'])*float(channel['spread_margin'])
        self.clock_bound = float(channel['sample_clock_pair_ppm'])*float(channel['spread_margin'])
        self.clocks = np.linspace(-self.clock_bound, self.clock_bound, clock_points) if self.clock_bound else np.array([0.])
        if clock_points == 1:
            self.clocks = np.array([0.])
        self.clock_half_step = (float(self.clocks[1]-self.clocks[0])/2
                                if len(self.clocks)>1 else self.clock_bound)
        self.original_frequencies = np.linspace(-self.cfo_bound, self.cfo_bound,
            max(3, 2*math.ceil(self.cfo_bound/.5)+1)) if self.cfo_bound else np.array([0.])
        self.helper_config = copy.deepcopy(helper_config)
        self.screen = FFTScreen(manifest, templates, self.epochs, clock_ppm=self.clocks,
            output_sample_rate=self.output_rate, precision=precision,
            memory_limit_bytes=self.memory_limit_bytes)
        self.required, self.excluded = core_projection_bands(manifest, pilot_use='none',
            frequencies_hz=(-self.cfo_bound, self.cfo_bound),
            clock_ppm=(-self.clock_bound, self.clock_bound))
        names = ('triad_realtime_receiver.py', 'triad_realtime_helper.py',
                 'triad_realtime_fft.py', 'triad_dynamic_frequency.py',
                 'triad_dynamic_joint.py', 'triad_dynamic_variants.py',
                 'triad_dynamic_scan.py', 'triad_gaussian_prepared.py',
                 'triad_gaussian_state.py', 'triad_helper_receiver.py')
        self.sources = {name: hashlib.sha256((Path(__file__).parent/name).read_bytes()).hexdigest()
                        for name in names}
        self.policy = dict(schema='triad-realtime-procedure-v1', precision=precision,
            output_rate=self.output_rate, top_per_bit=self.top_per_bit,
            clock_points=clock_points, clock_half_step_ppm=self.clock_half_step,
            maximum_refinement_candidates=self.maximum_refinement_candidates,
            memory_limit_bytes=self.memory_limit_bytes,
            helper_config=self.helper_config, block_seconds=.25,
            coherence_seconds=['0.5', 'inf'], pilot_phase_use='none',
            numeric_versions=dict(numpy=np.__version__, scipy=scipy.__version__,
                                  torch=torch.__version__),
            projection='fixed original-prior DATA core; explicitly lossy AM projection',
            failure_policy='unresolved; never silently truncate a search or use truth',
            sources=self.sources)
        self.procedure_id = _digest(dict(policy=self.policy,
            geometry=public_geometry(manifest), epochs=self.epochs,
            coarse_procedure_id=self.screen.procedure_id,
            templates_sha256=hashlib.sha256(np.ascontiguousarray(templates).view(np.uint8)).hexdigest()))
        self.initialization = dict(cpu_seconds=time.process_time()-cpu,
                                   wall_seconds=time.perf_counter()-wall)

    def score(self, raw, valid=None):
        cpu, wall = time.process_time(), time.perf_counter()
        result = dict(schema='triad-realtime-result-v1', procedure_id=self.procedure_id,
            status='unresolved', bit=None, score=None, margin=None, timing={},
            initialization=self.initialization, policy=self.policy,
            captured_seconds=len(raw)/self.manifest['sample_rate_hz'],
            admission='requires a matching frozen empirical whole-procedure gate')

        def finish(reason):
            result['reason'] = reason
            result['timing']['total'] = dict(cpu_seconds=time.process_time()-cpu,
                                             wall_seconds=time.perf_counter()-wall)
            return result

        helper, elapsed = _timed(lambda: estimate_helper(raw, self.manifest,
            self.original_frequencies, valid=valid, config=self.helper_config))
        result['helper'], result['timing']['helper'] = helper, elapsed
        frequency = helper.get('frequency', helper.get('frequency_result')) or {}
        if frequency.get('status') != 'narrowed':
            return finish('helper_acquisition_unresolved')
        intervals = frequency['expanded_intervals_hz']
        try:
            coarse, elapsed = _timed(lambda: self.screen.score(raw,
                frequency_intervals=intervals, valid=valid, top_per_bit=self.top_per_bit))
        except MemoryError:
            return finish('coarse_memory_limit')
        result['coarse'], result['timing']['coarse'] = coarse, elapsed
        if not coarse.get('candidates'):
            return finish('coarse_acquisition_unresolved')
        candidates = coarse['candidates']
        if {int(c['bit']) for c in candidates} != {0, 1}:
            raise RuntimeError('coarse screen must retain both keyed bit alternatives')
        try:
            coordinates = refinement_coordinates(candidates, self.manifest,
                clock_half_step_ppm=self.clock_half_step, timing_step_seconds=1/self.output_rate,
                maximum_candidates=self.maximum_refinement_candidates)
        except ValueError as error:
            if 'work budget' not in str(error):
                raise
            return finish('refinement_work_limit')
        try:
            receiver, elapsed = _timed(lambda: ScanJointReceiver(self.manifest, self.templates,
                self.epochs, candidate_coordinates=coordinates, block_seconds=.25,
                coherence_seconds=(.5, math.inf), pilot_use='none',
                required_bands_hz=self.required, auxiliary_exclusion_hz=self.excluded,
                valid=valid, device='cpu', precision=self.precision,
                memory_limit_bytes=self.memory_limit_bytes))
        except MemoryError:
            return finish('refinement_memory_limit')
        result['timing']['refinement_preparation'] = elapsed
        refined, elapsed = _timed(lambda: receiver.score(raw))
        result['timing']['refinement_scoring'] = elapsed
        scores = np.asarray(refined['bit_scores']).reshape(-1, 2)
        if scores.shape != (1, 2) or not np.isfinite(scores).all():
            raise RuntimeError('invalid final dynamic bit likelihoods')
        bit = int(np.argmax(scores[0]))
        result.update(status='scored', bit=bit, score=float(scores[0, bit]),
            margin=float(scores[0, bit]-scores[0, 1-bit]), bit_scores=scores[0].tolist(),
            refinement=dict(candidates=[list(c) for c in coordinates], work=receiver.work(),
                procedure_id=receiver.procedure_id, scan_backend=refined.get('scan_backend')))
        return finish('working_likelihood_requires_calibrated_admission')


def freeze_gate(results, *, alpha=.05):
    """Exchangeable-null per-capture rank gate, including unresolved outcomes."""
    rows = list(results)
    if not rows or not 0 < alpha < 1:
        raise ValueError('nonempty null calibration and alpha in (0,1) required')
    identities = {row['procedure_id'] for row in rows}
    if len(identities) != 1:
        raise ValueError('calibration procedures differ')
    if 1/(len(rows)+1) > alpha+1e-15:
        raise ValueError('too few null captures for the requested rank resolution')
    return dict(schema='triad-realtime-rank-gate-v1', procedure_id=identities.pop(),
        alpha=float(alpha), null_scores=[row['score'] if row['status']=='scored' else None for row in rows],
        scope='per-capture exchangeable null rank; no daily/continuous or conditional guarantee')


def admit(result, gate):
    if gate['procedure_id'] != result['procedure_id']:
        raise ValueError('admission gate procedure mismatch')
    if result['status'] != 'scored':
        return dict(accepted=False, bit=None, p_value=1., reason=result['reason'])
    score = result['score']
    nulls = gate['null_scores']
    p = (1+sum(x is not None and x>=score for x in nulls))/(len(nulls)+1)
    accepted = p <= gate['alpha']
    return dict(accepted=accepted, bit=result['bit'] if accepted else None,
                p_value=p, reason='rank_gate_accepted' if accepted else 'rank_gate_rejected')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--templates', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--threads', type=int, default=2)
    parser.add_argument('--precision', choices=('fp32','fp64'), default='fp32')
    parser.add_argument('--gate', type=Path)
    args = parser.parse_args()
    import torch
    torch.set_num_threads(args.threads)
    torch.set_float32_matmul_precision('highest')
    m, templates, epochs, raw, valid = load_public(args.capture, args.templates)
    receiver = RealtimeReceiver(m, templates, epochs, precision=args.precision)
    result = receiver.score(raw, valid)
    if args.gate:
        result['decision'] = admit(result, json.loads(args.gate.read_text()))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n')
    print(json.dumps({key:result[key] for key in ('status','reason','bit','score','margin','timing')}))


if __name__ == '__main__':
    main()
