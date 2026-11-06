#!/usr/bin/env python3
"""Conditional blockwise Gaussian-noise trials on complete native waveforms.

Each disjoint sampled block supplies its own two-pattern joint Gram and actual
channel-signal correlations. Noise is independent between blocks and correlated
between patterns within a block. Maintained GaussianStateFilter instances carry
their channel posterior through the entire observation. Finite-coherence trials
therefore never substitute a whole-observation Gram for missing temporal data.

The alternative is a proper complex Gaussian OU/static coefficient process.
It is not an exact Brownian unit-amplitude phase law. Trial confidence intervals
are conditional on these fixed native channel files and public synchronization.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import time

import numpy as np
from scipy.special import logsumexp
import torch

from triad_gaussian_state import GaussianPrior, GaussianStateFilter
from triad_incremental_receiver import IncrementalReceiver
from triad_multiday_likelihood import classify_scores, mix_log_evidence
from triad_phase_frequency import PhaseFrequencyLikelihood, PhaseFrequencyModel


def _hash(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def _source_identities():
    root=Path(__file__).resolve().parent
    return {str(p):_hash(p) for p in (Path(__file__).resolve(),
        *(root/name for name in ('triad_gaussian_state.py','triad_phase_frequency.py',
          'triad_incremental_receiver.py','triad_dynamic_joint.py','triad_multiday_likelihood.py')))}


class NativeBlockInputs:
    """Read only the fixed public nominal bank and separate channel references."""
    def __init__(self, directory, template_ppm=0.):
        self.directory = Path(directory).resolve()
        self.manifest = json.loads((self.directory/'manifest.json').read_text())
        m = self.manifest
        if (m.get('dtype') != '<c8' or m.get('first_sample') != 0 or
            m.get('channel_noise_enabled') is not False or
            not isinstance(m.get('samples'),int) or not 1 <= m['samples'] < (1<<64)):
            raise ValueError('complete noiseless native complex64 source/channel files required')
        self.samples = m['samples'];self.sample_rate = float(m['sample_rate_hz'])
        if not math.isfinite(self.sample_rate) or self.sample_rate <= 0:
            raise ValueError('positive physical sample rate required')
        self.template_ppm=float(template_ppm)
        if not math.isfinite(self.template_ppm):raise ValueError('finite public template ppm required')
        nominal = [i for i,x in enumerate(m['template_ppm']) if float(x)==self.template_ppm]
        if len(nominal) != 1:
            raise ValueError('one predeclared public selected-ppm template per bit required')
        selected=[]
        for role in ('public-affine-basis','channel-signal-oracle'):
            rows=[]
            for bit in (0,1):
                found=[x for x in m['files'] if x.get('role')==role and x.get('bit')==bit and
                    (role!='public-affine-basis' or x.get('template_index')==nominal[0])]
                if len(found)!=1:raise ValueError('missing or ambiguous public/template channel file')
                path=(self.directory/found[0]['path']).resolve()
                if path.parent!=self.directory or path.stat().st_size!=self.samples*8:
                    raise ValueError('native file path or complete sample extent is invalid')
                rows.append(path)
            selected.append(rows)
        self.paths=selected
        self.basis=tuple(np.memmap(p,dtype='<c8',mode='r',shape=(self.samples,)) for p in selected[0])
        self.signals=tuple(np.memmap(p,dtype='<c8',mode='r',shape=(self.samples,)) for p in selected[1])

    def identity(self):
        paths=[self.directory/'manifest.json',*(p for rows in self.paths for p in rows)]
        return {str(p):_hash(p) for p in paths}

    def basis_block(self,lo,hi):
        return np.stack([x[lo:hi] for x in self.basis]).astype(np.complex128)

    def signal_block(self,lo,hi):
        return np.stack([x[lo:hi] for x in self.signals]).astype(np.complex128)


def _calendar(source,block_samples,variance):
    # Reuse the maintained block calendar, including its merged terminal block.
    # No source template or observation is synthesized by this preflight object.
    return IncrementalReceiver(sample_rate=source.sample_rate,samples=source.samples,
        block_samples=block_samples,basis=lambda lo,hi:None,
        required_bands=((-source.sample_rate*.49,source.sample_rate*.49),),
        noise_variance=variance,stationary_power=(1.,),coherence_seconds=(math.inf,),
        precision='fp64',candidate_count=1,projection_mode='valid-rows',
        maximum_input_chunk_samples=block_samples,memory_limit_bytes=64*1024**2)


def _block_statistics(source,lo,hi,variance):
    a=source.basis_block(lo,hi);s=source.signal_block(lo,hi)
    gram=a.conj()@a.T/variance
    gram=(gram+gram.conj().T)*.5
    values,vectors=np.linalg.eigh(gram)
    scale=max(1.,float(values[-1]));minimum=float(values[0])
    if minimum < -2e-10*scale:raise FloatingPointError('block joint Gram is not PSD')
    factor=vectors*np.sqrt(np.maximum(values,0))[None,:]
    if minimum<0:
        gram=factor@factor.conj().T;gram=(gram+gram.conj().T)*.5
    means=(a.conj()@s.T/variance).T # [true bit, correlation bit]
    return gram,factor,means,max(0.,-minimum)/scale


def _dtype(precision,device):
    if precision not in ('fp32','fp64'):raise ValueError('precision must be fp32 or fp64')
    device=torch.device(device)
    if device.type not in ('cpu','cuda'):raise ValueError('CPU or CUDA required')
    if device.type=='cuda':
        if not torch.cuda.is_available():raise RuntimeError('CUDA requested but unavailable')
        if precision=='fp32' and (torch.backends.cuda.matmul.allow_tf32 or torch.get_float32_matmul_precision()!='highest'):
            raise ValueError('FP32 minimum requires TF32 off and highest matmul precision')
    return (torch.complex64 if precision=='fp32' else torch.complex128),device


def _models(coherences,dtype):
    if not coherences or len(coherences)>8 or len(set(coherences))!=len(coherences):
        raise ValueError('one through eight unique predeclared coherence alternatives required')
    return [GaussianStateFilter(GaussianPrior((1.,),float(t)),dtype=dtype,check_inputs=False) for t in coherences]


def _phase_models(states,diffusion,batch,dtype,device,dt):
    if (len(states)>4 or len(set(states))!=len(states) or
        any(isinstance(x,bool) or not isinstance(x,int) or not 2<=x<=4096 for x in states) or
        not math.isfinite(diffusion) or diffusion<0):
        raise ValueError('invalid public phase discretizations or diffusion')
    return [PhaseFrequencyLikelihood(batch,2,1,PhaseFrequencyModel(phase_states=n,
        frequency_bound_hz=0.,frequency_grid_hz=(0.,),frequency_model='static',
        frequency_std_hz=0.,random_walk_hz_per_sqrt_second=0.,
        nominal_dt_seconds=dt,amplitude_nodes=(1.,),amplitude_weights=(1.,),
        relative_phase_states=1,ray_amplitudes=(1.,),
        phase_diffusion_rad_per_sqrt_second=diffusion),device=device,
        dtype=torch.float32 if dtype==torch.complex64 else torch.float64,
        memory_limit_bytes=256*1024**2,validate_inputs=False) for n in states]


def conditional_trials(source,*,cn0_db_hz=-60.,block_samples=256,coherences=(2e6,math.inf),
                       trials=128,batch_trials=128,seed=673031,precision='fp32',device='cpu',
                       log_threshold=math.log(10000),confidence=.95,
                       phase_states=(),phase_diffusion=.001,memory_limit_bytes=1024**3):
    """Independent-noise trials, conditional on the complete actual channel.

    Every model processes exactly the same block r/G realizations. An external
    caller owns the endpoint, repeated-attempt and duration-alternative budget.
    The default threshold only names one predeclared test at alpha=1e-4.
    """
    if (isinstance(trials,bool) or not isinstance(trials,int) or not 1<=trials<=1_000_000 or
        isinstance(batch_trials,bool) or not isinstance(batch_trials,int) or not 1<=batch_trials<=4096 or
        isinstance(block_samples,bool) or not isinstance(block_samples,int) or not 1<=block_samples<=1048576 or
        isinstance(seed,bool) or not isinstance(seed,int) or not 0<=seed<(1<<63) or
        not math.isfinite(cn0_db_hz) or not math.isfinite(log_threshold) or
        isinstance(memory_limit_bytes,bool) or not isinstance(memory_limit_bytes,int) or memory_limit_bytes<1):
        raise ValueError('invalid trial count, block size, seed or C/N0')
    variance=source.sample_rate/10**(.1*cn0_db_hz)
    if not math.isfinite(variance) or variance<=0:raise ValueError('noise variance overflows')
    dtype,device=_dtype(precision,device)
    calendar=_calendar(source,block_samples,variance)
    began,cpu=time.perf_counter(),time.process_time()
    all_scores=[];batches=[];worst_repair=0.;observed_energy=np.zeros(2);basis_energy=np.zeros(2)
    maximum_workspace=0;statistics_cpu=noise_cpu=0.
    for first_trial in range(0,trials,batch_trials):
        n=min(batch_trials,trials-first_trial)
        # Separate seeds make explicitly tiled batches independently reproducible.
        batch_seed=seed+first_trial
        rng=np.random.default_rng(batch_seed)
        filters=_models(coherences,dtype)
        phase_filters=_phase_models(phase_states,phase_diffusion,3*n,dtype,device,
                                    block_samples/source.sample_rate)
        item=8 if dtype==torch.complex64 else 16
        workspace=(sum(f.working_bytes_bound for f in phase_filters)+
            len(filters)*(3*n*2*(item*8+64)+4096)+calendar.maximum_block_rows*512+
            trials*(len(filters)+len(phase_filters))*3*2*8*3+3*n*2*16*8)
        maximum_workspace=max(maximum_workspace,workspace)
        if workspace>memory_limit_bytes:raise MemoryError('aggregate phase/Gaussian/trial-output workspace exceeds declared budget')
        previous_center=None;batch_start=time.perf_counter();blocks=0
        for index in range(calendar.block_count):
            lo,hi=calendar._block_bounds(index)
            stage=time.process_time()
            gram,factor,means,repair=_block_statistics(source,lo,hi,variance)
            worst_repair=max(worst_repair,repair)
            if first_trial==0:
                basis_energy+=gram.diagonal().real*variance/source.sample_rate
                s=source.signal_block(lo,hi)
                observed_energy+=(abs(s)**2).sum(-1)/source.sample_rate
            statistics_cpu+=time.process_time()-stage;stage=time.process_time()
            z=(rng.standard_normal((3,n,2))+1j*rng.standard_normal((3,n,2)))/math.sqrt(2)
            r=z@factor.T
            r[:2]+=means[:,None,:]
            if not np.isfinite(r).all():raise FloatingPointError('block correlations became nonfinite')
            noise_cpu+=time.process_time()-stage
            center=(lo+hi-1)/(2*source.sample_rate)
            dt=0. if previous_center is None else center-previous_center
            rt=torch.as_tensor(r[...,None],dtype=dtype,device=device)
            gt=torch.as_tensor(gram.diagonal().real.copy()[...,None,None],dtype=dtype,device=device)
            for f in filters:f.update(rt,gt,dt)
            for f in phase_filters:f.update(rt.reshape(3*n,2,1),gt,dt)
            previous_center=center;blocks+=1
        score=np.stack([*[f.finish().detach().cpu().numpy() for f in filters],
                        *[f.scores().detach().cpu().numpy().reshape(3,n,2) for f in phase_filters]])
        if not np.isfinite(score).all():raise FloatingPointError('model scores became nonfinite')
        all_scores.append(score)
        batches.append(dict(first_trial=first_trial,trials=n,seed=batch_seed,blocks=blocks,
            wall_seconds=time.perf_counter()-batch_start,
            diagnostics=[f.diagnostics() for f in (*filters,*phase_filters)]))
    scores=np.concatenate(all_scores,axis=2)
    mixed=mix_log_evidence(scores)
    model_names=['static-inf' if math.isinf(float(t)) else f'OU-{float(t):.17g}s' for t in coherences]
    model_names += [f'unit-phase-{n}' for n in phase_states]
    reports={}
    for name,values in [*(zip(model_names,scores)),('fixed-equal-model-mixture',mixed)]:
        reports[name]={label:classify_scores(values[i],log_threshold,true_bit=None if i==2 else i,confidence=confidence)
                      for i,label in enumerate(('bit0','bit1','null'))}
    return dict(schema='triad-multiday-block-conditional-mc-v1',samples=source.samples,
        sample_rate_hz=source.sample_rate,simulated_seconds=source.samples/source.sample_rate,
        public_template_ppm=getattr(source,'template_ppm',0.),
        cn0_db_hz=cn0_db_hz,complex_sample_noise_variance=variance,
        block_samples=block_samples,blocks=calendar.block_count,
        terminal_block_rows=calendar._block_bounds(calendar.block_count-1)[1]-calendar._block_bounds(calendar.block_count-1)[0],
        coherence_seconds=['inf' if math.isinf(float(x)) else float(x) for x in coherences],
        phase_states=list(phase_states),phase_diffusion_rad_sqrt_s=phase_diffusion,
        trials=trials,batch_trials=batch_trials,seed=seed,precision=precision,device=str(device),
        gram_arithmetic='complex128 CPU; 2x2 PSD repair shared by draws/diagonal',
        inference_bulk=str(dtype),evidence_reductions='torch.float64',
        memory_limit_bytes=memory_limit_bytes,aggregate_working_bytes_bound=maximum_workspace,
        memory_scope='Aggregate declared phase/Gaussian/transient/output arrays; excludes NumPy/Torch runtime, allocator and OS mmap/page cache',
        statistics_preparation_cpu_seconds=statistics_cpu,noise_generation_cpu_seconds=noise_cpu,
        maximum_relative_psd_repair=worst_repair,basis_energy_seconds=basis_energy.tolist(),
        actual_channel_energy_seconds=observed_energy.tolist(),batches=batches,reports=reports,
        wall_seconds=time.perf_counter()-began,cpu_seconds=time.process_time()-cpu,
        scope='Conditional independent Gaussian-noise Monte Carlo through every actual native sample block; no fresh channel paths, continuum optimality, physical-noise or anytime qualification'),scores


def validate_sampled(source,*,cn0_db_hz,seed,true_bit=0,block_samples=256,
                     coherences=(2e6,math.inf),input_chunk_samples=256,
                     phase_states=(),phase_diffusion=.001):
    """Independent actual-sample noise versus block-statistic filter replay.

    This reference deliberately uses FP64 and reproduces the maintained input
    chunk RNG order before applying its possibly merged terminal block calendar.
    Only this bounded qualification helper retains the complete noisy vector.
    """
    if true_bit is not None and true_bit not in (0,1):raise ValueError('bit must be zero, one or null')
    if source.samples*16>256*1024**2:raise MemoryError('sampled validation vector exceeds256MiB; use a streaming external reference')
    variance=source.sample_rate/10**(.1*cn0_db_hz)
    rng=np.random.default_rng(seed);y=np.empty(source.samples,np.complex128)
    for lo in range(0,source.samples,input_chunk_samples):
        hi=min(source.samples,lo+input_chunk_samples);n=hi-lo
        y[lo:hi]=(rng.standard_normal(n)+1j*rng.standard_normal(n))*math.sqrt(variance/2)
        if true_bit is not None:y[lo:hi]+=source.signals[true_bit][lo:hi]
    calendar=_calendar(source,block_samples,variance)
    filters=_models(coherences,torch.complex128);previous=None
    phase_filters=_phase_models(phase_states,phase_diffusion,1,torch.complex128,'cpu',
                                block_samples/source.sample_rate)
    for index in range(calendar.block_count):
        lo,hi=calendar._block_bounds(index);a=source.basis_block(lo,hi)
        gram=a.conj()@a.T/variance;r=a.conj()@y[lo:hi]/variance
        center=(lo+hi-1)/(2*source.sample_rate);dt=0. if previous is None else center-previous
        for f in filters:
            f.update(torch.as_tensor(r[...,None]),
                torch.as_tensor(gram.diagonal().real.copy()[...,None,None]).to(torch.complex128),dt)
        for f in phase_filters:
            f.update(torch.as_tensor(r[None,:,None]),
                torch.as_tensor(gram.diagonal().real.copy()[...,None,None]).to(torch.complex128),dt)
        previous=center
    extracted=np.stack([f.finish().detach().numpy() for f in filters])
    def basis(lo,hi):
        result=np.zeros((3,1,1,hi-lo),np.complex128)
        result[1:,0,0]=source.basis_block(lo,hi)
        return result
    receiver=IncrementalReceiver(sample_rate=source.sample_rate,samples=source.samples,
        block_samples=block_samples,basis=basis,required_bands=((-source.sample_rate*.49,source.sample_rate*.49),),
        noise_variance=variance,stationary_power=(1.,),coherence_seconds=coherences,
        precision='fp64',candidate_count=1,projection_mode='valid-rows',maximum_input_chunk_samples=input_chunk_samples)
    for lo in range(0,source.samples,input_chunk_samples):receiver.push(lo,y[lo:lo+input_chunk_samples])
    result=receiver.finish();maintained=np.asarray(result['model_bit_scores'])
    error=float(abs(extracted-maintained).max())
    if error>2e-9:raise AssertionError(f'block statistic versus sampled maintained likelihood error {error}')
    phase_scores=np.stack([f.scores().detach().numpy()[0] for f in phase_filters]) if phase_filters else np.empty((0,2))
    return dict(schema='triad-multiday-block-sampled-reference-v1',seed=seed,true_bit=true_bit,
        cn0_db_hz=cn0_db_hz,blocks=calendar.block_count,input_chunk_samples=input_chunk_samples,
        extracted_bit_scores=extracted.tolist(),maintained_bit_scores=maintained.tolist(),
        maintained_log_e=(logsumexp(maintained,axis=-1)-math.log(2)).tolist(),maximum_absolute_error=error,
        phase_states=list(phase_states),phase_bit_scores=phase_scores.tolist(),
        phase_log_e=(logsumexp(phase_scores,axis=-1)-math.log(2)).tolist(),
        phase_diagnostics=[f.diagnostics() for f in phase_filters],
        scope='Exact same actual sampled noise/complete native observation through maintained FP64 receiver and independently extracted block r/G; fixed channel path')


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--samples',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--cn0',type=float,default=-60);p.add_argument('--block-samples',type=int,default=256)
    p.add_argument('--template-ppm',type=float,default=0.,help='one public affine bank atom chosen before observations, not a fitted clock trajectory')
    p.add_argument('--coherences',default='2000000,inf');p.add_argument('--trials',type=int,default=128)
    p.add_argument('--batch-trials',type=int,default=128);p.add_argument('--seed',type=int,default=673031)
    p.add_argument('--precision',choices=('fp32','fp64'),default='fp32');p.add_argument('--device',default='cpu')
    p.add_argument('--threads',type=int,default=1);p.add_argument('--log-threshold',type=float,default=math.log(10000))
    p.add_argument('--phase-states',default='',help='public phase-grid comparisons, e.g.64,128; all requested branches enter fixed model mixture')
    p.add_argument('--phase-diffusion',type=float,default=.001)
    p.add_argument('--validate-seed',type=int);p.add_argument('--validate-bit',choices=('0','1','null'),default='0')
    a=p.parse_args(argv)
    if a.output.exists():raise FileExistsError(a.output)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    if not 1<=a.threads<=64:raise ValueError('threads must be1..64')
    torch.set_num_threads(a.threads);torch.backends.cuda.matmul.allow_tf32=False;torch.set_float32_matmul_precision('highest')
    source_before=_source_identities()
    source=NativeBlockInputs(a.samples,a.template_ppm);coherences=tuple(float(x) for x in a.coherences.split(','))
    phases=tuple(int(x) for x in a.phase_states.split(',') if x)
    whole,cpu=time.perf_counter(),time.process_time();identities=source.identity()
    report,scores=conditional_trials(source,cn0_db_hz=a.cn0,block_samples=a.block_samples,
        coherences=coherences,trials=a.trials,batch_trials=a.batch_trials,seed=a.seed,
        precision=a.precision,device=a.device,log_threshold=a.log_threshold,
        phase_states=phases,phase_diffusion=a.phase_diffusion)
    if a.validate_seed is not None:
        report['sampled_validation']=validate_sampled(source,cn0_db_hz=a.cn0,seed=a.validate_seed,
            true_bit=None if a.validate_bit=='null' else int(a.validate_bit),block_samples=a.block_samples,
            coherences=coherences,input_chunk_samples=a.block_samples,
            phase_states=phases,phase_diffusion=a.phase_diffusion)
    source_after=_source_identities();inputs_after=source.identity()
    changed=[p for p in source_before if source_before[p]!=source_after[p]]
    changed_inputs=[p for p in identities if identities[p]!=inputs_after[p]]
    report.update(input_identities=identities,input_identities_after=inputs_after,
        source_identities=source_before,source_identities_after=source_after,
        changed_source_files=changed,changed_input_files=changed_inputs,
        qualification_valid=not changed and not changed_inputs,source_sha256=source_before[str(Path(__file__).resolve())],
        complete_wall_seconds=time.perf_counter()-whole,complete_cpu_seconds=time.process_time()-cpu)
    report['complete_timer_scope']='Input hashing + conditional/validation computation + final identity checks; excludes imports, argument parsing, NativeBlockInputs construction and artifact serialization. Use subprocess timing for end-to-end cost.'
    np.savez_compressed(a.output.with_suffix('.npz'),model_case_trial_bit_scores=scores)
    a.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    if changed or changed_inputs:raise RuntimeError('source/input changed during qualification; retained result is invalid')
    print(json.dumps(dict(trials=a.trials,wall_seconds=report['complete_wall_seconds'],
        cpu_seconds=report['complete_cpu_seconds'],reports=report['reports'])))
    return report


if __name__=='__main__':main()
