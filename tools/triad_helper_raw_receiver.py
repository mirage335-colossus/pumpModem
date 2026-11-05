#!/usr/bin/env python3
"""Bounded raw-sample DATA likelihood for known ADC gaps in the isolated study.

Uses the maintained batched FFT/Gram receiver at 18 kHz, without a lossy FIR
support exclusion. Noise remains independent on the retained raw samples. The
unknown channel and unremoved Helper remain working-model limitations; this is
not an operational admission rule. A matching Helper-present null calibration
is required, separately from filtered receiver calibration.
"""
from __future__ import annotations
import copy
from collections import OrderedDict
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import threading
import time
import numpy as np
from scipy import fft
from scipy.special import logsumexp
from triad_fft_acquire import lag_grid,phase
from triad_helper_fft import BatchedFFTReceiver
from triad_helper_receiver import fractional_delay

RAW_RATE=18000
TIMING_RATE=900
MAX_CFO_HZ=.6


def number(value,name,positive=False):
    if isinstance(value,(bool,np.bool_)) or not isinstance(value,(int,float,np.integer,np.floating)):
        raise ValueError(name+' must be a finite number')
    result=float(value)
    if not math.isfinite(result) or (positive and result<=0):raise ValueError(name+' must be finite'+(' and positive' if positive else ''))
    return result


def integer(value,name,minimum=1):
    if isinstance(value,(bool,np.bool_)) or not isinstance(value,(int,np.integer)) or value<minimum:raise ValueError(name+' must be an integer >= '+str(minimum))
    return int(value)


@dataclass(frozen=True)
class PreparedRawObservation:
    samples: np.ndarray
    preprocessing_id: str
    filtering_seconds: float
    validity_id: str


class RawHelperDataReceiver:
    """Full public epoch/timing/CFO search with exact fixed raw erasure masks.

    Initial scope is nominal singleton audio/RX clocks and |CFO| <= .6 Hz.
    ``lag_step`` counts 900-Hz timing samples, not raw 18-kHz samples. Supplied
    templates and capture retain complete finite source/propagation support.
    ``reference_variance`` is the ORIGINAL per-complex-raw-sample variance.
    There is no filter, interpolation of received gaps or estimated whitening.
    """
    def __init__(self,raw_templates,epoch_offsets,*,source_offset_seconds,capture_lead_seconds,capture_rows,
                 arrival_bounds_seconds,delay_seconds,doppler_hz,reference_variance,
                 frequency_offsets=(0.,),sample_rate=RAW_RATE,output_sample_rate=RAW_RATE,lag_step=1,
                 precision='fp32',device='cpu',memory_limit_bytes=2*1024**3,
                 fft_backend='batched',frontend_profile='raw',clock_ppm_values=(0.,),doppler_scale_values=(1.,),
                 frequency_batch_size=8,max_cached_hypotheses=1,capture_validity=None,
                 source_half_bandwidth_hz=112.5,channel_line_half_width_hz=None):
        started=time.perf_counter()
        if sample_rate!=RAW_RATE or output_sample_rate!=RAW_RATE or fft_backend!='batched':
            raise ValueError('raw prototype requires 18000-Hz input/output and batched FFT')
        if tuple(clock_ppm_values)!=(0.,) or tuple(doppler_scale_values)!=(1.,):
            raise ValueError('raw prototype currently supports singleton nominal audio/RX clocks only')
        if frontend_profile not in ('raw','legacy','wide'):raise ValueError('unknown frontend request')
        self.fft_backend='batched';self.frontend_profile='raw';self.clock_ppm_values=(0.,);self.doppler_scale_values=(1.,)
        self.capture_rows=integer(capture_rows,'capture_rows');self.memory_limit_bytes=integer(memory_limit_bytes,'memory_limit_bytes')
        self._batch_size=integer(frequency_batch_size,'frequency_batch_size');self._requested_capacity=integer(max_cached_hypotheses,'max_cached_hypotheses')
        self._precision,self._device=precision,device
        if precision not in ('fp32','fp64') or (device!='cpu' and not device.startswith('cuda')):raise ValueError('unsupported precision/device')
        source=np.asarray(raw_templates);epochs=tuple(epoch_offsets)
        if source.ndim!=3 or source.shape[1]!=2 or source.shape[2]<=4 or source.dtype.kind!='c' or not np.isfinite(source).all():raise ValueError('finite complex [epochs,2,N>4] templates required')
        if len(epochs)!=len(source) or not 1<=len(epochs)<=13 or len(set(epochs))!=len(epochs) or any(isinstance(x,(bool,np.bool_)) or not isinstance(x,(int,np.integer)) or abs(x)>6 for x in epochs):raise ValueError('unique integer epochs in [-6,6] required')
        self.epoch_offsets=tuple(map(int,epochs));self._capacity=min(self._requested_capacity,len(epochs))
        if source.size*16>=self.memory_limit_bytes:raise MemoryError('source templates exceed raw receiver memory limit')
        self._source=np.array(source,np.complex128,copy=True);self._source.flags.writeable=False
        self._source_hash=hashlib.sha256(memoryview(self._source).cast('B')).hexdigest()
        self.source_offset_seconds=number(source_offset_seconds,'source_offset_seconds');self.capture_lead_seconds=number(capture_lead_seconds,'capture_lead_seconds')
        self.delay_seconds=number(delay_seconds,'delay_seconds');self.doppler_hz=number(doppler_hz,'doppler_hz')
        self.reference_variance=number(reference_variance,'reference_variance',True)
        self.source_half_bandwidth_hz=number(source_half_bandwidth_hz,'source_half_bandwidth_hz',True)
        self.channel_line_half_width_hz=(2*abs(self.doppler_hz) if channel_line_half_width_hz is None else number(channel_line_half_width_hz,'channel_line_half_width_hz'))
        if min(self.capture_lead_seconds,self.delay_seconds)<0 or abs(self.doppler_hz)>=RAW_RATE/2 or self.channel_line_half_width_hz<abs(self.doppler_hz):raise ValueError('invalid public raw channel geometry')
        if len(arrival_bounds_seconds)!=2:raise ValueError('two arrival endpoints required')
        self.arrival_bounds_seconds=tuple(number(x,'arrival bound')for x in arrival_bounds_seconds);lower,upper=self.arrival_bounds_seconds
        earliest=self.capture_lead_seconds+self.source_offset_seconds+lower
        if upper<lower or earliest<0:raise ValueError('invalid public raw arrival interval')
        # Four raw samples conservatively retain the source interpolation halo.
        self.crop_start_raw=max(0,math.floor(earliest*RAW_RATE)-4);self.samples=self.capture_rows-self.crop_start_raw
        self.lags=lag_grid(math.ceil((upper-lower)*TIMING_RATE),integer(lag_step,'lag_step'))*(RAW_RATE//TIMING_RATE)
        anchor=earliest*RAW_RATE-self.crop_start_raw
        support_stop=math.ceil(anchor+self.delay_seconds*RAW_RATE)+source.shape[2]+3+int(self.lags[-1])
        if self.samples<=4 or support_stop>=self.samples:raise ValueError('complete public raw source/channel support leaves capture')
        self.capture_validity=None;self.output_validity=None;self.validity_id='all-valid'
        if capture_validity is not None:
            valid=np.asarray(capture_validity)
            if valid.shape!=(self.capture_rows,) or valid.dtype.kind not in 'bui' or np.any((valid!=0)&(valid!=1)):raise ValueError('binary capture validity required')
            if np.any(valid==0):
                self.capture_validity=np.array(valid,np.uint8,copy=True);self.capture_validity.flags.writeable=False
                self.output_validity=self.capture_validity[self.crop_start_raw:];self.validity_id=hashlib.sha256(self.capture_validity.tobytes()).hexdigest()
                if int(self.output_validity.sum())<=4:raise ValueError('insufficient retained raw samples')
        self.preprocessing=dict(format='triad-helper-raw-frontend-v1',profile='raw',input_sample_rate=RAW_RATE,output_sample_rate=RAW_RATE,
            decimation=1,filter='none',noise_variance_gain=1.,crop_start_raw_sample=self.crop_start_raw,crop_output_samples=self.samples,
            capture_rows=self.capture_rows,source_offset_seconds=self.source_offset_seconds,capture_lead_seconds=self.capture_lead_seconds,
            arrival_bounds_seconds=self.arrival_bounds_seconds,delay_seconds=self.delay_seconds,doppler_hz=self.doppler_hz,
            source_half_bandwidth_hz=self.source_half_bandwidth_hz,channel_line_half_width_hz=self.channel_line_half_width_hz,
            timing_lattice_hz=TIMING_RATE,raw_history_guard_samples=4,validity_id=self.validity_id,
            raw_missing_rows=0 if self.capture_validity is None else int((self.capture_validity==0).sum()),
            retained_raw_rows=self.samples if self.output_validity is None else int(self.output_validity.sum()),
            observation_handling='Original raw samples; discard known missing ADC rows only, no FIR support dilation.',
            noise_model='Raw CN(0,reference_variance) remains white on retained rows; no invented colored-noise whitening.',
            helper_model='Helper remains in observed samples and residual normalizer; unknown Helper/selected-AM mismatch requires matching full-procedure calibration.')
        self.preprocessing_id=hashlib.sha256(json.dumps(self.preprocessing,sort_keys=True).encode()).hexdigest()
        self._minimum_fft_size=4*source.shape[2];self._nfft=fft.next_fast_len(max(2*self.samples-1,self._minimum_fft_size));self.cfo_grid_step_hz=RAW_RATE/self._nfft
        self._workspace_bound=self.samples*(81+320)+self._nfft*320+len(self.lags)*320+65536
        if device.startswith('cuda'):self._workspace_bound+=self._nfft*192+self.samples*96
        factors=len(self.lags)*4096+self._nfft*64+self.samples*48
        self._transient=self._batch_size*(self._nfft*80+self.samples*48+len(self.lags)*2048)
        self.frontend_array_bound_bytes=self._source.nbytes+self.capture_rows*32+self.samples*80+(0 if self.capture_validity is None else self.capture_validity.nbytes)
        self._base_bound=self.frontend_array_bound_bytes+self._capacity*(self._workspace_bound+factors)+self._transient
        self.frequency_offsets=self._frequencies(frequency_offsets);self._validate_bank()
        self._cache=OrderedDict();self._cache_lock=threading.RLock();self.lags.flags.writeable=False
        self.template_energy=[];self.preparation_seconds=time.perf_counter()-started;self._bind_identity()

    def _frequencies(self,values):
        result=tuple(number(x,'frequency offset')for x in values)
        if not result or len(result)>4096 or len(set(result))!=len(result) or max(map(abs,result))>MAX_CFO_HZ:raise ValueError('raw prototype needs unique <=4096 CFOs within +/-0.6 Hz')
        if self.source_half_bandwidth_hz+self.channel_line_half_width_hz+max(map(abs,result))>=RAW_RATE/2:raise ValueError('public raw DATA/channel/CFO band reaches Nyquist')
        return result

    def _validate_bank(self):
        self.effective_group_count=len(self.epoch_offsets)*len(self.lags)*len(self.frequency_offsets)
        if self.effective_group_count>2**53:raise ValueError('raw group count exceeds exact representation')
        self.explicit_array_bound_bytes=self._base_bound+128*len(self.frequency_offsets)+len(self.epoch_offsets)*8192
        if self.explicit_array_bound_bytes>=self.memory_limit_bytes:raise MemoryError('raw epoch cache/FFT batch exceeds explicit memory limit')
        self._child_limit=(self.memory_limit_bytes-self.frontend_array_bound_bytes-128*len(self.frequency_offsets)-len(self.epoch_offsets)*8192-self._transient)//self._capacity+self._transient

    def _bind_identity(self):
        source_names=('triad_helper_raw_receiver.py','triad_helper_receiver.py','triad_helper_fft.py','triad_live_receiver.py','triad_fft_acquire.py','triad_group_acquire.py','triad_gaussian_window.py',
                      'triad_noise_likelihood.py','triad_gaussian_state.py','triad_state_workspace.py')
        sources={name:hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()for name in source_names}
        self.receiver_id=hashlib.sha256(json.dumps(dict(preprocessing=self.preprocessing_id,sources=sources,source=self._source_hash,
            epochs=self.epoch_offsets,lags=self.lags.tolist(),frequencies=self.frequency_offsets,precision=self._precision,device=self._device,
            reference_variance=self.reference_variance,batch_size=self._batch_size),sort_keys=True).encode()).hexdigest()

    def with_frequency_offsets(self,values):
        result=copy.copy(self);result.frequency_offsets=self._frequencies(values);result._validate_bank();result._bind_identity();return result

    def with_hypotheses(self,*,frequency_offsets=None,clock_ppm_values=None,doppler_scale_values=None):
        if (clock_ppm_values is not None and tuple(clock_ppm_values)!=(0.,)) or (doppler_scale_values is not None and tuple(doppler_scale_values)!=(1.,)):
            raise ValueError('raw prototype supports singleton nominal clocks only')
        return self.with_frequency_offsets(self.frequency_offsets if frequency_offsets is None else frequency_offsets)

    def aligned_frequency_offsets(self,bound):
        bound=number(bound,'prior half width',True)
        if bound>MAX_CFO_HZ:raise ValueError('raw prototype prior exceeds +/-0.6 Hz')
        half=math.floor(bound/self.cfo_grid_step_hz);values=np.arange(-half,half+1)*self.cfo_grid_step_hz
        if bound-half*self.cfo_grid_step_hz>1e-12:values=np.r_[-bound,values,bound]
        return self._frequencies(values)

    def prepare_observation(self,raw_observation,*,valid=None):
        started=time.perf_counter();raw=np.asarray(raw_observation)
        if raw.shape!=(self.capture_rows,) or raw.dtype.kind!='c':raise ValueError('complete complex raw capture required')
        if valid is not None:
            mask=np.asarray(valid)
            if mask.shape!=raw.shape or mask.dtype.kind not in 'bui' or np.any((mask!=0)&(mask!=1)) or (self.capture_validity is None and np.any(mask!=1)) or (self.capture_validity is not None and not np.array_equal(mask,self.capture_validity)):
                raise ValueError('incoming validity differs from fixed raw capture mask')
        values=np.array(raw[self.crop_start_raw:],np.complex128,copy=True)
        if self.output_validity is not None:values[self.output_validity==0]=0
        if not np.isfinite(values).all():raise ValueError('valid raw samples must be finite')
        values.flags.writeable=False
        return PreparedRawObservation(values,self.preprocessing_id,time.perf_counter()-started,self.validity_id)

    def _child(self,index):
        if index in self._cache:self._cache.move_to_end(index);return self._cache[index],0.
        while len(self._cache)>=self._capacity:self._cache.popitem(last=False)
        started=time.perf_counter();lower=self.arrival_bounds_seconds[0]
        anchor=(self.capture_lead_seconds+self.source_offset_seconds+lower)*RAW_RATE-self.crop_start_raw
        basis=np.zeros((2,self.samples,2),np.complex128)
        for bit in (0,1):
            for ray,offset in enumerate((anchor,anchor+self.delay_seconds*RAW_RATE)):
                wide=fractional_delay(self._source[index,bit],offset)
                if len(wide)>self.samples:raise ValueError('raw public source support leaves capture')
                if ray:
                    times=(np.arange(len(wide),dtype=np.longdouble)+self.crop_start_raw)/RAW_RATE-self.capture_lead_seconds
                    wide*=phase(self.doppler_hz,times)
                basis[bit,:len(wide),ray]=wide
        child=BatchedFFTReceiver(basis,RAW_RATE,self.lags,key=dict(arrival_seconds=lower,epoch_offset_seconds=self.epoch_offsets[index]),
            frequencies=[0.,self.doppler_hz,0.,self.doppler_hz],reference_variance=self.reference_variance,precision=self._precision,
            device=self._device,memory_limit_bytes=self._child_limit,preprocessing_identity=self.preprocessing_id,
            frequency_batch_size=self._batch_size,minimum_fft_size=self._minimum_fft_size,valid=self.output_validity)
        self._cache[index]=child
        return child,time.perf_counter()-started

    def score(self,raw_observation,*,valid=None):return self.score_prepared(self.prepare_observation(raw_observation,valid=valid))

    def score_prepared(self,prepared):
        if not isinstance(prepared,PreparedRawObservation) or prepared.preprocessing_id!=self.preprocessing_id or prepared.validity_id!=self.validity_id or prepared.samples.shape!=(self.samples,) or prepared.samples.flags.writeable:
            raise ValueError('prepared observation has a different raw preprocessing/mask identity')
        started=time.perf_counter();outcomes=[];preparation=0.
        with self._cache_lock:
            for index,epoch in enumerate(self.epoch_offsets):
                child,elapsed=self._child(index);preparation+=elapsed
                result=child.score(prepared.samples,self.frequency_offsets,family_groups=self.effective_group_count)
                outcomes.append(dict(epoch=epoch,result=result));del child
        scores=np.asarray([x['result']['bit_log_likelihoods']for x in outcomes]);mixture=logsumexp(scores,axis=0)-math.log(len(outcomes));bit=int(np.argmax(mixture));best=outcomes[int(np.argmax(scores[:,bit]))]
        parameters=best['result']['best_group_public_parameters'] if best['result']['bit']==bit else None
        if parameters is not None:parameters=dict(parameters,doppler_scale=1.,arrival_physical_seconds=parameters['arrival_seconds'])
        return dict(format='triad-helper-raw-working-likelihood-v1',receiver_id=self.receiver_id,gate_id=self.receiver_id,bit=bit,
            score=float(mixture[bit]),working_presence=float(mixture[bit]),bit_log_likelihoods=mixture.tolist(),opposite_bit_margin=float(mixture[bit]-mixture[1-bit]),
            epoch_bit_log_likelihoods=scores.tolist(),best_epoch_offset_seconds=best['epoch'],best_group_public_parameters=parameters,
            best_group_scope='Diagnostic winner inside strongest winning-bit epoch mixture; never used to prune the full bank.',
            geometry=dict(epoch_offsets=list(self.epoch_offsets),lag_samples=self.lags.tolist(),timing_lattice_hz=TIMING_RATE,
                frequency_offsets_hz=list(self.frequency_offsets),clock_ppm_values=[0.],doppler_scale_values=[1.],samples=self.samples,
                sample_rate_hz=RAW_RATE,effective_group_count=self.effective_group_count,mixture='Uniform over every declared epoch/timing/CFO.'),
            preprocessing=self.preprocessing,arithmetic=best['result']['arithmetic'],
            hypothesis_bit_log_likelihoods=[dict(epoch=x['epoch'],bit_log_likelihoods=x['result']['bit_log_likelihoods'])for x in outcomes],
            calibration=dict(analytic_admission_validated=False,empirical_gate_required=True,residual_family_groups=self.effective_group_count,
                scope='Raw noise remains white on known valid rows; unmodeled Helper and selective AM require matching Helper-present null calibration.'),
            resources=dict(explicit_array_bound_bytes=self.explicit_array_bound_bytes,memory_limit_bytes=self.memory_limit_bytes,
                maximum_cached_hypotheses=self._capacity,retained_group_scores=False,scope='Explicit arrays/cache/FFT batches; excludes process and device allocator reserve.'),
            timing=dict(total_preparation_seconds=self.preparation_seconds,hypothesis_preparation_seconds=preparation,
                whole_capture_filter_seconds=0.,raw_capture_preparation_seconds=prepared.filtering_seconds,
                observation_fft_seconds=sum(x['result']['timing']['observation_fft_seconds']for x in outcomes),
                residual_and_likelihood_seconds=sum(x['result']['timing']['residual_and_likelihood_seconds']for x in outcomes),
                total_score_prepared_seconds=time.perf_counter()-started),
            limits=['Experimental bounded raw path: nominal singleton clocks and +/-0.6-Hz CFO only.',
                'Static two-ray DATA model; no Helper subtraction or selected-AM nuisance fit.',
                'No analytic/operational admissions, physical completion or ordinary receiver integration claim.'])
