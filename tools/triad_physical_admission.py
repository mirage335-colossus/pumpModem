#!/usr/bin/env python3
"""Conditional physical admission on uniform, real, pre-ASRC ADC samples.

For a fixed public signal prior, the Gaussian likelihood ratio is a mixture of
mean-shift ratios. Its null expectation is <= 1 whenever the COMPLETE real ADC
noise vector has the declared joint sub-Gaussian proxy v*I. Independence between
scored blocks and Gaussian-looking marginal samples are not required. A measured
profile supports, but cannot prove, that physical assumption for future data.

Positive DFT modes are represented as sqrt(2)*[real,imag]. Excluding DC, Nyquist
and conjugate duplicates makes these real rows orthonormal. Constant input DC
is annihilated exactly. Real Gaussian channel inference reuses the maintained
complex Kalman algebra and halves each model/candidate log LR BEFORE mixing.
No adaptive timing merge, fitted noise, selected trajectory or empirical rank
gate is introduced. This module does not certify an unconditional daily rate.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import time

import numpy as np
from scipy import fft
from scipy.special import logsumexp
import torch

from triad_gaussian_state import GaussianPrior, GaussianStateFilter


SCHEMA = 'triad-physical-noise-profile-v1'
_U64 = (1 << 64)-1
PROFILE_CHECKS = ('no_clipping', 'observed_positive_psd_within_declared_proxy',
    'observed_variance_within_declared_proxy', 'every_observed_block_variance_within_declared_proxy',
    'centered_peak_within_ten_proxy_standard_deviations',
    'block_mean_motion_within_eight_proxy_standard_errors')


def _integer(value, name, low=1, high=_U64):
    if isinstance(value, (bool, np.bool_)) or not isinstance(value, (int, np.integer)) or not low <= int(value) <= high:
        raise ValueError(f'{name} must be an integer in [{low},{high}]')
    return int(value)


def _finite(value, name, *, positive=False):
    if isinstance(value, (bool, np.bool_)):
        raise ValueError(f'{name} must be finite')
    try:
        result = float(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f'{name} must be finite') from exc
    if not math.isfinite(result) or (positive and result <= 0):
        raise ValueError(f'{name} must be finite' + (' and positive' if positive else ''))
    return result


def _sha(value):
    return isinstance(value, str) and len(value) == 64 and all(c in '0123456789abcdef' for c in value)


def source_sha256():
    return hashlib.sha256(Path(__file__).read_bytes()).hexdigest()


def _bands(values, fs):
    try:
        result = tuple(tuple(_finite(x, 'band edge') for x in row) for row in values)
    except TypeError as exc:
        raise ValueError('positive-frequency band pairs required') from exc
    if (not result or any(len(row) != 2 or not 0 < row[0] <= row[1] < fs/2 for row in result)
            or any(max(a,c) <= min(b,d) for i,(a,b) in enumerate(result) for c,d in result[i+1:])):
        raise ValueError('distinct positive bands strictly inside ADC Nyquist required')
    return result


@dataclass(frozen=True)
class ValidatedProfile:
    profile_id: str
    variance_proxy: float
    serialized: str

    def report(self):
        result = json.loads(self.serialized)
        result.update(profile_id=self.profile_id, physical_admission_scope='conditional-on-profile',
            unconditional_daily_rate_certified=False,
            statistical_scope='Joint sub-Gaussian ADC proxy is a physical-model assumption; short held-out measurements do not prove future rare-event rates.')
        return result


def validate_profile(profile, expected_binding, *, sample_rate, block_samples,
                     data_if_hz, required_bands_hz, fixed_gain, capture_seconds):
    """Freeze a compatible successful profile; never infer it from payload data."""
    if not isinstance(profile, dict) or profile.get('schema') != SCHEMA:
        raise ValueError('unsupported physical noise profile')
    binding = profile.get('binding')
    required = {'backend','device','hardware_rate_hz','hardware_channels','selected_channel',
                'mixer_sha256','audio_binary_sha256','receiver_sha256'}
    if (not isinstance(binding, dict) or not required <= binding.keys() or
            not isinstance(expected_binding, dict) or binding != expected_binding):
        raise ValueError('physical noise profile binding mismatch')
    for key in ('mixer_sha256','audio_binary_sha256','receiver_sha256'):
        if not _sha(binding[key]):
            raise ValueError('invalid physical profile source/mixer identity')
    if binding['receiver_sha256'] != source_sha256():
        raise ValueError('physical receiver source differs from frozen profile')
    fs = _integer(sample_rate, 'ADC rate', high=120_000_000)
    block = _integer(block_samples, 'block samples', high=65536)
    channel_count = _integer(binding['hardware_channels'], 'hardware channels', high=32)
    _integer(binding['selected_channel'], 'selected channel', low=0, high=channel_count-1)
    if (binding['hardware_rate_hz'] != fs or not isinstance(binding['backend'], str) or
            not binding['backend'] or not isinstance(binding['device'], str) or not binding['device']):
        raise ValueError('physical ADC identity/rate mismatch')
    bands = _bands(required_bands_hz, fs)
    center = _finite(data_if_hz, 'DATA IF', positive=True)
    gain = _finite(fixed_gain, 'fixed gain', positive=True)
    duration = _finite(capture_seconds, 'capture seconds', positive=True)
    geometry = profile.get('geometry')
    if not isinstance(geometry, dict):
        raise ValueError('physical profile geometry required')
    expected = dict(sample_rate_hz=fs, block_samples=block, data_if_hz=center,
                    required_bands_hz=[list(row) for row in bands], fixed_gain=gain)
    if any(geometry.get(key) != value for key,value in expected.items()):
        raise ValueError('physical profile observation geometry mismatch')
    if duration > _finite(geometry.get('maximum_capture_seconds'), 'profile duration limit', positive=True):
        raise ValueError('physical capture exceeds qualified profile duration')
    model = profile.get('null_model')
    if (not isinstance(model, dict) or model.get('kind') != 'global-real-subgaussian-proxy-v1' or
            model.get('dc_component') != 'constant-only' or model.get('bound_scope') != 'conditional-on-profile' or
            not isinstance(model.get('assumptions'), list) or not model['assumptions'] or
            any(not isinstance(s, str) or not s.strip() for s in model['assumptions'])):
        raise ValueError('explicit global physical null assumptions required')
    variance = _finite(model.get('variance_proxy'), 'global ADC variance proxy', positive=True)
    qualification = profile.get('qualification')
    if (not isinstance(qualification, dict) or qualification.get('passed') is not True or
            not _sha(qualification.get('calibration_sha256')) or not _sha(qualification.get('validation_sha256')) or
            qualification['calibration_sha256'] == qualification['validation_sha256']):
        raise ValueError('distinct successful calibration and validation captures required')
    for key in ('calibration_samples','validation_samples'):
        _integer(qualification.get(key), key, low=2*block)
    checks = qualification.get('checks')
    if (not isinstance(checks, list) or not checks or
            any(not isinstance(x, dict) or not isinstance(x.get('name'), str) or not x['name'].strip() or
                x.get('passed') is not True for x in checks)):
        raise ValueError('all declared physical profile checks must pass')
    names = [x['name'] for x in checks]
    required_checks = {stage+':'+name for stage in ('calibration','validation') for name in PROFILE_CHECKS}
    if len(names)!=len(set(names)) or not required_checks <= set(names):
        raise ValueError('physical profile lacks distinct calibration and validation engineering checks')
    try:
        serialized = json.dumps(profile, sort_keys=True, separators=(',', ':'), allow_nan=False)
    except (TypeError, ValueError) as exc:
        raise ValueError('physical noise profile must be finite JSON') from exc
    identity = hashlib.sha256(serialized.encode()).hexdigest()
    return ValidatedProfile(identity, variance, serialized)


def load_profile(path, expected_binding, **geometry):
    return validate_profile(json.loads(Path(path).read_text()), expected_binding, **geometry)


class PhysicalReceiver:
    """Streaming fixed-bank real-Gaussian likelihood on native ADC coordinates.

    ``basis(first,stop)`` returns [2,candidate,complex_tap,row] full-audio complex
    templates z, including their audio IF. The alternate maintained [3,...]
    layout is accepted only with an exactly zero first hypothesis. Two real
    coefficients per complex tap have stationary powers p/2; the modeled real
    waveform is sqrt(2)*gain*Re(z*h). Public bands and priors are fixed before
    data. This class does not estimate an ADC proxy or decide admission.
    """

    def __init__(self, *, sample_rate, samples, block_samples, basis,
                 candidate_count=1, required_bands_hz, variance_proxy,
                 stationary_power=(1.,), coherence_seconds=(.5,2.,math.inf),
                 candidate_probabilities=None, model_probabilities=None,
                 fixed_gain=1., precision='fp32', memory_limit_bytes=512*1024**2,
                 maximum_input_chunk_samples=8192, maximum_candidates=512):
        started = time.perf_counter()
        self.sample_rate = _integer(sample_rate, 'ADC rate', high=120_000_000)
        self.samples = _integer(samples, 'sample count')
        self.block_samples = _integer(block_samples, 'block samples', high=65536)
        self.maximum_input_chunk_samples = _integer(maximum_input_chunk_samples, 'input chunk limit', high=65536)
        maximum_candidates = _integer(maximum_candidates, 'candidate cap', high=4096)
        self.candidates = _integer(candidate_count, 'candidate count', high=maximum_candidates)
        if not callable(basis):
            raise TypeError('public real-waveform basis provider required')
        self.basis = basis
        self.required_bands = _bands(required_bands_hz, self.sample_rate)
        self.variance = _finite(variance_proxy, 'global ADC variance proxy', positive=True)
        self.fixed_gain = _finite(fixed_gain, 'fixed gain', positive=True)
        powers = tuple(_finite(x, 'tap power') for x in stationary_power)
        if not powers or len(powers) > 8 or any(x < 0 for x in powers) or not any(powers):
            raise ValueError('one to eight nonnegative complex-tap powers, including a positive power, required')
        self.taps = len(powers)
        self.coherences = tuple(float(x) for x in coherence_seconds)
        if not self.coherences or len(self.coherences)>64:
            raise ValueError('one to 64 fixed channel models required')
        self.precision = precision
        if precision not in ('fp32','fp64'):
            raise ValueError('precision must be fp32 or fp64')
        self.real_dtype = np.dtype(np.float32 if precision=='fp32' else np.float64)
        self.torch_real = torch.float32 if precision=='fp32' else torch.float64
        self.torch_complex = torch.complex64 if precision=='fp32' else torch.complex128
        self.candidate_probabilities = self._probabilities(candidate_probabilities,self.candidates)
        self.model_probabilities = self._probabilities(model_probabilities,len(self.coherences))
        self.runtimes = [GaussianStateFilter(GaussianPrior(tuple(x/2 for x in powers for _ in (0,1)),tau),
                            dtype=self.torch_complex) for tau in self.coherences]
        full, tail = divmod(self.samples,self.block_samples)
        self.block_count = max(1,full)
        self.maximum_block_rows = self.samples if not full else self.block_samples+tail
        # Include callback c128 storage, real FFT/projected copies, float torch
        # products, Gaussian scratch/state and input staging. This is an owned
        # workspace estimate, not a bound on Python/scientific-library RSS.
        n,c,k,m = self.maximum_block_rows,self.candidates,self.taps,len(self.coherences)
        item = 8 if precision=='fp32' else 16
        self.owned_bytes_estimate = (192*c*k*n + 64*(n+self.maximum_input_chunk_samples) +
            16*m*2*c*((2*k)**2+2*k)*item)
        limit = _integer(memory_limit_bytes,'workspace bytes')
        if self.owned_bytes_estimate > limit:
            raise MemoryError('physical fixed-bank workspace exceeds declared limit')
        self._pending = np.empty(self.maximum_block_rows,dtype=self.real_dtype)
        self._pending_rows = 0
        self.next_input_sample = self.processed_rows = self.block_updates = 0
        self.projection_rows = self.basis_rows = self.coefficient_updates = 0
        self.peak_pending_rows = 0
        self.complete_observation = False
        self.failed = False
        self.maximum_absolute_input = 0.
        self.push_cpu_seconds = self.maximum_block_cpu_seconds = self.maximum_block_wall_seconds = 0.
        self._previous_center = None
        self.preparation_seconds = time.perf_counter()-started

    @staticmethod
    def _probabilities(values,count):
        p = np.ones(count,np.float64) if values is None else np.asarray(values,np.float64)
        if p.shape != (count,) or not np.isfinite(p).all() or (p<0).any() or not p.max()>0:
            raise ValueError('fixed public mixture probabilities required')
        p = p/p.max()
        return p/p.sum()

    def _block_bounds(self,index):
        first = index*self.block_samples
        return first,self.samples if index==self.block_count-1 else first+self.block_samples

    def _indices(self,n):
        frequencies = fft.rfftfreq(n,1/self.sample_rate)
        keep = np.zeros(len(frequencies),bool)
        for low,high in self.required_bands:
            selected = (frequencies>=low-self.sample_rate/(2*n)) & (frequencies<=high+self.sample_rate/(2*n))
            if not selected.any():
                raise ValueError('physical block cannot resolve required band')
            keep |= selected
        keep[0] = False
        if n%2==0:keep[-1]=False
        indices = np.flatnonzero(keep)
        if not len(indices):
            raise ValueError('positive non-Nyquist ADC Fourier modes required')
        return indices

    @staticmethod
    def project(values,indices):
        """Real orthonormal projection; caller supplies fixed positive indices."""
        values = np.asarray(values)
        if values.dtype.kind != 'f':
            raise ValueError('real floating-point ADC values required')
        transformed = fft.rfft(values,axis=-1,norm='ortho',workers=1)[...,indices]
        return np.concatenate((transformed.real,transformed.imag),axis=-1)*math.sqrt(2.)

    def _score(self,first,stop,values):
        began,cpu = time.perf_counter(),time.process_time()
        indices = self._indices(stop-first)
        y = self.project(values,indices)
        z = np.asarray(self.basis(first,stop))
        if z.shape == (3,self.candidates,self.taps,stop-first):
            if np.any(z[0]!=0):raise ValueError('physical H0 basis must be exactly zero')
            z=z[1:]
        if z.shape != (2,self.candidates,self.taps,stop-first) or z.dtype.kind!='c' or not np.isfinite(z).all():
            raise ValueError('physical basis must be finite complex [2,candidate,tap,row]')
        columns = np.empty((2,self.candidates,2*self.taps,len(y)),dtype=self.real_dtype)
        scale = math.sqrt(2.)*self.fixed_gain
        columns[:,:,0::2,:] = scale*self.project(np.asarray(z.real,dtype=self.real_dtype),indices)
        columns[:,:,1::2,:] = -scale*self.project(np.asarray(z.imag,dtype=self.real_dtype),indices)
        a = torch.as_tensor(columns,dtype=self.torch_real)
        obs = torch.as_tensor(y,dtype=self.torch_real)
        r = (a@obs/self.variance).to(self.torch_complex)
        gram = (a@a.transpose(-2,-1)/self.variance).to(self.torch_complex)
        center = (first+stop-1)/2
        elapsed = 0. if self._previous_center is None else (center-self._previous_center)/self.sample_rate
        for runtime in self.runtimes:runtime.update(r,gram,elapsed)
        self._previous_center = center
        self.processed_rows += stop-first
        self.block_updates += 1
        self.projection_rows += len(y)
        self.basis_rows += 2*self.candidates*self.taps*(stop-first)
        self.coefficient_updates += 2*self.candidates*len(self.runtimes)
        self.maximum_block_cpu_seconds=max(self.maximum_block_cpu_seconds,time.process_time()-cpu)
        self.maximum_block_wall_seconds=max(self.maximum_block_wall_seconds,time.perf_counter()-began)

    def push(self,first_sample,values):
        began=time.process_time()
        if self.failed:raise RuntimeError('physical receiver failed previously')
        first_sample=_integer(first_sample,'first sample',low=0)
        if first_sample!=self.next_input_sample:
            raise ValueError('native ADC input must be contiguous from sample zero')
        values=np.asarray(values)
        if (values.ndim!=1 or values.size>self.maximum_input_chunk_samples or values.dtype.kind not in 'fi' or
                not np.isfinite(values).all() or values.size>self.samples-first_sample):
            raise ValueError('bounded finite contiguous real ADC chunk required')
        if values.size:
            self.maximum_absolute_input=max(self.maximum_absolute_input,float(np.max(np.abs(values))))
        try:
            cursor=0
            while cursor<len(values):
                lo,hi=self._block_bounds(self.block_updates)
                count=min(len(values)-cursor,hi-lo-self._pending_rows)
                self._pending[self._pending_rows:self._pending_rows+count]=values[cursor:cursor+count]
                self._pending_rows+=count;cursor+=count;self.next_input_sample+=count
                self.peak_pending_rows=max(self.peak_pending_rows,self._pending_rows)
                if self._pending_rows==hi-lo:
                    self._score(lo,hi,self._pending[:self._pending_rows])
                    self._pending_rows=0
            self.complete_observation=self.next_input_sample==self.samples and self.processed_rows==self.samples
        except BaseException:
            self.failed=True
            raise
        finally:
            self.push_cpu_seconds+=time.process_time()-began

    def progress(self):
        if not self.block_updates:
            model=np.zeros((len(self.runtimes),2,self.candidates))
        else:
            # This division is inside both fixed mixtures, not outside logsumexp.
            model=np.stack([runtime.finish().detach().cpu().numpy()*.5 for runtime in self.runtimes])
        with np.errstate(divide='ignore'):
            candidates=logsumexp(model+np.log(self.candidate_probabilities)[None,None,:],axis=-1)
            bits=logsumexp(candidates+np.log(self.model_probabilities)[:,None],axis=0)
        return dict(schema='triad-physical-fixed-real-likelihood-v1',bit_scores=bits.tolist(),
            model_bit_scores=candidates.tolist(),complete_observation=self.complete_observation and not self.failed,
            processed_rows=self.processed_rows,consumed_samples=self.next_input_sample,
            block_updates=self.block_updates,working_bytes_estimate=self.owned_bytes_estimate,
            peak_pending_rows=self.peak_pending_rows,maximum_absolute_input=self.maximum_absolute_input,
            preparation_seconds=self.preparation_seconds,push_process_cpu_seconds=self.push_cpu_seconds,
            maximum_block_cpu_seconds=self.maximum_block_cpu_seconds,maximum_block_wall_seconds=self.maximum_block_wall_seconds,
            arithmetic=dict(bulk=self.precision,fft=self.precision,channel_state=self.precision,
                            accumulated_log_likelihood='fp64',complex_algebra_log_ratio_scale=.5),
            work_counts=dict(projection_rows=self.projection_rows,basis_rows=self.basis_rows,coefficient_updates=self.coefficient_updates),
            admission=None,scope='Fixed public real-Gaussian signal mixture under a declared global joint sub-Gaussian ADC proxy; no physical profile is inferred from payload.')

    def finish(self):
        result=self.progress()
        result['status']='failed' if self.failed else 'scored' if result['complete_observation'] else 'incomplete'
        return result


def physical_decision(scores, *, complete, profile, log_threshold, clean_capture=True):
    if not isinstance(profile,ValidatedProfile):
        raise ValueError('validated matching physical noise profile required')
    scores=np.asarray(scores,np.float64)
    if scores.shape!=(2,) or not np.isfinite(scores).all():raise ValueError('two finite physical bit scores required')
    threshold=_finite(log_threshold,'admission threshold')
    log_e=float(logsumexp(scores)-math.log(2.))
    passed=bool(complete and clean_capture and log_e>=threshold)
    return dict(candidate_bit=int(scores.argmax()),log_e=log_e,admitted=passed,
        conditional_model_pass=passed,physical_noise_profile_id=profile.profile_id,
        physical_null_qualified=True,physical_admission_scope='conditional-on-profile',
        unconditional_physical_rate_certified=False,
        admission_receiver='fixed-public-real-Gaussian-bank',
        admission_scope='Conditional on the complete native ADC noise satisfying the frozen global joint sub-Gaussian proxy and matched hardware/profile conditions; short measurements do not prove a future unconditional daily rate.')
