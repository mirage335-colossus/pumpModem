"""Pilot-aware/segmented acquisition with fixed-grid dynamic refinement.

Isolated one-bit adapter. No operational physical-null guarantee is inferred
from its working Gaussian likelihood; see the explicit admission receipt.
"""
import copy
import hashlib
import json
import math
from pathlib import Path
import time

import numpy as np

from triad_dynamic_campaign import public_geometry
from triad_dynamic_variants import ScanJointReceiver, core_projection_bands
from triad_realtime_fft import FFTScreen
from triad_realtime_receiver import refinement_coordinates
from triad_segmented_fft import SegmentedFFTScreen
from triad_weak_helper import estimate_weak_helper
from triad_operational_admission import PublicGrid, operational_decision


def _timed(f):
    c,w=time.process_time(),time.perf_counter();x=f()
    return x,dict(cpu_seconds=time.process_time()-c,wall_seconds=time.perf_counter()-w)


class WeakReceiver:
    def __init__(self, manifest, templates, epochs, *, precision='fp32',
                 block_seconds=.5, screen_mode='auto', noise_inflation=1.,
                 top_per_bit=2, coherences=(.5,2.,math.inf),
                 memory_limit_bytes=2*1024**3):
        c,w=time.process_time(),time.perf_counter()
        if not math.isfinite(noise_inflation) or noise_inflation<1:
            raise ValueError('noise inflation must be finite and at least one')
        if screen_mode not in ('auto','whole','segmented'):
            raise ValueError('unsupported acquisition screen')
        self.manifest=copy.deepcopy(manifest)
        self.manifest['power']['complex_noise_variance']*=noise_inflation
        self.templates,self.epochs=templates,tuple(epochs)
        self.top_per_bit,self.coherences=int(top_per_bit),tuple(coherences)
        self.precision,self.memory_limit=precision,int(memory_limit_bytes)
        ch=manifest['channel'];margin=float(ch['spread_margin'])
        self.cfo_bound=float(ch['deterministic_cfo_bound_hz'])*margin
        self.clock_bound=float(ch['sample_clock_pair_ppm'])*margin
        self.frequencies=np.linspace(-self.cfo_bound,self.cfo_bound,
            2*math.ceil(self.cfo_bound/.5)+1) if self.cfo_bound else np.array([0.])
        self.clocks=np.array([-self.clock_bound,0.,self.clock_bound]) if self.clock_bound else np.array([0.])
        renewing=(float(ch.get('random_doppler_width_hz',0))>0 or
                  float(ch.get('delay_motion_doppler_bound_hz',0))>0)
        self.screen_mode=('segmented' if renewing else 'whole') if screen_mode=='auto' else screen_mode
        common=dict(clock_ppm=self.clocks,precision=precision,memory_limit_bytes=memory_limit_bytes)
        self.screen=(SegmentedFFTScreen(self.manifest,templates,epochs,block_seconds=block_seconds,**common)
                     if self.screen_mode=='segmented' else FFTScreen(self.manifest,templates,epochs,**common))
        self.required,self.excluded=core_projection_bands(self.manifest,pilot_use='none',
            frequencies_hz=(-self.cfo_bound,self.cfo_bound),clock_ppm=(-self.clock_bound,self.clock_bound))
        lo,hi=manifest['calendar']['arrival_prior_seconds']
        self.grid=PublicGrid(self.epochs,lo,hi,.001,self.cfo_bound,.01,self.clock_bound)
        names=('triad_weak_receiver.py','triad_weak_helper.py','triad_segmented_fft.py',
            'triad_operational_admission.py','triad_realtime_receiver.py','triad_realtime_fft.py',
            'triad_dynamic_joint.py','triad_dynamic_frequency.py','triad_dynamic_variants.py',
            'triad_dynamic_scan.py','triad_gaussian_prepared.py','triad_gaussian_state.py',
            'triad_helper_receiver.py','triad_dynamic_campaign.py')
        self.policy=dict(schema='triad-weak-procedure-v1',precision=precision,
            screen_mode=self.screen_mode,block_seconds=block_seconds,top_per_bit=top_per_bit,
            coherences=[str(x)for x in self.coherences],noise_inflation=noise_inflation,
            fixed_projection='original-prior DATA core; lossy receiver projection',
            final_grid=self.grid.__dict__,memory_limit_bytes=memory_limit_bytes,
            sources={n:hashlib.sha256((Path(__file__).parent/n).read_bytes()).hexdigest()for n in names})
        self.procedure_id=hashlib.sha256(json.dumps(dict(policy=self.policy,
            geometry=public_geometry(self.manifest),screen=self.screen.procedure_id,
            templates=hashlib.sha256(np.ascontiguousarray(templates).view(np.uint8)).hexdigest()),
            sort_keys=True,allow_nan=False).encode()).hexdigest()
        self.initialization=dict(cpu_seconds=time.process_time()-c,wall_seconds=time.perf_counter()-w)

    def score(self, raw, valid=None):
        c,w=time.process_time(),time.perf_counter()
        r=dict(schema='triad-weak-result-v1',procedure_id=self.procedure_id,
            policy=self.policy,initialization=self.initialization,status='unresolved',
            bit=None,score=None,margin=None,timing={})
        def finish(reason):
            r['reason']=reason;r['timing']['total']=dict(cpu_seconds=time.process_time()-c,wall_seconds=time.perf_counter()-w)
            return r
        helper,t=_timed(lambda:estimate_weak_helper(raw,self.manifest,self.frequencies,valid=valid))
        r['helper'],r['timing']['helper']=helper,t
        frequency=helper.get('frequency') or {}
        if frequency.get('status')!='narrowed':return finish('helper_acquisition_unresolved')
        try:
            coarse,t=_timed(lambda:self.screen.score(raw,
                frequency_intervals=frequency['expanded_intervals_hz'],valid=valid,top_per_bit=self.top_per_bit))
        except MemoryError:return finish('coarse_memory_limit')
        r['coarse'],r['timing']['coarse']=coarse,t
        if not coarse.get('candidates'):return finish('coarse_acquisition_unresolved')
        candidates=coarse['candidates']
        if {x['bit']for x in candidates}!={0,1}:raise RuntimeError('both bit alternatives must reach refinement')
        try:
            coordinates=refinement_coordinates(candidates,self.manifest,
                clock_half_step_ppm=self.clock_bound/2,timing_step_seconds=.001,maximum_candidates=64)
        except ValueError as error:
            if 'budget' not in str(error):raise
            return finish('refinement_work_limit')
        # Fixed BEFORE seeing observations: arbitrary adaptive coordinates are
        # not valid finite-bank likelihood atoms. Deduplicate after snapping.
        coordinates=tuple(dict.fromkeys(self.grid.snap(x)for x in coordinates))
        self.grid.validate_selected(coordinates)
        try:
            receiver,t=_timed(lambda:ScanJointReceiver(self.manifest,self.templates,self.epochs,
                candidate_coordinates=coordinates,block_seconds=.25,coherence_seconds=self.coherences,
                pilot_use='none',required_bands_hz=self.required,auxiliary_exclusion_hz=self.excluded,
                valid=valid,device='cpu',precision=self.precision,memory_limit_bytes=self.memory_limit))
        except MemoryError:return finish('refinement_memory_limit')
        r['timing']['refinement_preparation']=t
        refined,t=_timed(lambda:receiver.score(raw));r['timing']['refinement_scoring']=t
        evidence=np.asarray(refined['bit_scores']).reshape(-1,2)
        if evidence.shape!=(1,2)or not np.isfinite(evidence).all():raise RuntimeError('invalid bit evidence')
        h0=np.asarray(refined['hypothesis_log_evidence'])[...,0]
        zero_h0=bool(np.max(np.abs(h0))<1e-7)
        if not zero_h0:raise RuntimeError('working null is not the required zero-signal hypothesis')
        decision=operational_decision(evidence[0],coordinates,self.grid,zero_h0_verified=zero_h0)
        bit=int(np.argmax(evidence[0]))
        r.update(status='scored',bit=bit,score=float(evidence[0,bit]),
            margin=float(evidence[0,bit]-evidence[0,1-bit]),bit_scores=evidence[0].tolist(),
            operational=decision,refinement=dict(candidates=coordinates,work=receiver.work(),
                procedure_id=receiver.procedure_id,zero_h0_verified=zero_h0))
        return finish('conditional_model_admission_physical_null_unqualified')
