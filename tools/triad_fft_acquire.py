#!/usr/bin/env python3
"""Actual sampled, whole-observation two-ray acquisition on a fixed timing lattice.

Linear FFT correlations reuse four public shaped columns across every integer
arrival lag. The likelihood, complete-column residual whitening and operational
budgets are the existing grouped receiver's static-channel special case. This
path fixes the public epoch and sample-clock hypothesis. It is not a general
phase tracker, a physical retuning simulation, or a source of conditional redraws.
"""
from __future__ import annotations

import argparse
from decimal import Decimal
import hashlib
import json
import math
import os
from pathlib import Path
import sys
import time

import numpy as np
from scipy import fft
from scipy.special import logsumexp
import torch

from triad_group_acquire import (AcquisitionConfig, WorkspaceError,
    _integer, _number, _normalize_scientific_arguments, decision,
    frequency_offset_grid, prepare_group_tile, sha256)
from triad_gaussian_window import PreparedWindowedGaussian

PUBLIC_FILES = ("manifest.json", "basis.c128", "observation.c128", "validity.u8")
MAX_GROUPS = 1_000_000
MAX_SUPPORT_OPERATIONS = 200_000_000
MAX_REFINEMENT_ROWS = 2_000_000
OBSERVABLE_RELATIVE_TOLERANCE = 1e-10
SYNC_FIELDS = ("arrival_seconds", "frequency_hz", "clock_ppm",
               "epoch_offset_seconds", "frequency_drift_hz_per_second")


def phase(frequency, times, drift=0.):
    """FP64 phasors after long-double physical-time cycle reduction."""
    cycles = np.remainder(np.longdouble(frequency)*times +
        np.longdouble(.5)*np.longdouble(drift)*times*times, np.longdouble(1))
    return np.exp(2j*math.pi*np.asarray(cycles, dtype=np.float64))


class SampledWorkspace:
    """Strict public-only reader for one native sampled template group."""
    def __init__(self, directory):
        self.directory = Path(directory)
        path = self.directory/"manifest.json"
        try:
            if path.stat().st_size > 16*1024*1024:
                raise WorkspaceError("sampled manifest exceeds 16 MiB")
            self.manifest = json.loads(path.read_text())
        except (OSError, ValueError) as error:
            raise WorkspaceError(f"cannot read sampled manifest: {error}") from error
        m = self.manifest
        if not isinstance(m, dict) or (m.get("format"),m.get("layout"),m.get("byte_order"),m.get("complex_type"),m.get("integer_type")) != (
                "triad-sampled-waveforms-v1","candidate,sample,column","little","complex128","uint8"):
            raise WorkspaceError("unsupported sampled representation")
        if (m.get("gaussian_projection_supported") is not True or
                m.get("sampled_frontend",{}).get("active") is not False or
                m.get("frontend_report",{}).get("gaussian_projection_supported") is not True):
            raise WorkspaceError("FFT calibration requires an explicitly Gaussian sampled frontend")
        self.samples = _integer(m.get("sample_count"), "sample_count")
        self.sample_rate = _number(m.get("sample_rate"), "sample_rate", positive=True)
        if self.samples > 128_000_000:
            raise WorkspaceError("sampled extent exceeds 128000000 rows")
        candidates = m.get("candidates")
        if not isinstance(candidates,list) or len(candidates) != m.get("candidate_count") or not 1 <= len(candidates) <= 10000:
            raise WorkspaceError("invalid sampled candidates")
        columns = m.get("basis_shape")
        if columns != [len(candidates),self.samples,2]:
            raise WorkspaceError("FFT acquisition requires native two-column sampled candidates")
        selected = {}
        keys = set()
        for index,c in enumerate(candidates):
            if not isinstance(c,dict) or isinstance(c.get("bit"),bool) or c.get("bit") not in (0,1):
                raise WorkspaceError("invalid candidate bit")
            key = []
            for name in SYNC_FIELDS:
                value = c.get(name)
                if name == "epoch_offset_seconds":
                    if isinstance(value,bool) or not isinstance(value,int) or abs(value) > 2**63-1:
                        raise WorkspaceError("epoch offset must remain an exact signed integer")
                else:
                    value = _number(value,name)
                key.append(value)
            keys.add(tuple(key))
            taps = c.get("taps")
            if not isinstance(taps,list) or c.get("tap_count") != len(taps) or len(taps) not in (1,2):
                raise WorkspaceError("only one/two-tap public sampled candidates are supported")
            for tap in taps:
                if not isinstance(tap,dict):
                    raise WorkspaceError("invalid public tap")
                _number(tap.get("delay_seconds"),"tap delay")
                _number(tap.get("doppler_hz"),"tap Doppler")
            if len(taps) == 2:
                if c["bit"] in selected:
                    raise WorkspaceError("need exactly one two-tap template per bit")
                selected[c["bit"]] = index
        if len(keys) != 1 or set(selected) != {0,1}:
            raise WorkspaceError("export exactly one fixed public synchronization group, with both two-tap bits")
        self.key = dict(zip(SYNC_FIELDS,next(iter(keys))))
        self.selected = [selected[0],selected[1]]
        self.taps = candidates[self.selected[0]]["taps"]
        if candidates[self.selected[1]]["taps"] != self.taps:
            raise WorkspaceError("both bits must use the same public two-tap basis")
        self.frequencies = np.asarray([self.key["frequency_hz"]+tap["doppler_hz"]
            for _ in (0,1) for tap in self.taps],dtype=np.float64)
        self._spec = {
            "basis.c128": ([len(candidates),self.samples,2],"<c16",len(candidates)*self.samples*2*16),
            "observation.c128": ([self.samples],"<c16",self.samples*16),
            "validity.u8": ([self.samples],"u1",self.samples),
        }
        if (m.get("basis_file"),m.get("observation_file"),m.get("validity_file")) != tuple(self._spec):
            raise WorkspaceError("unexpected public sampled filenames")
        if m.get("observation_shape") != [self.samples] or m.get("validity_shape") != [self.samples]:
            raise WorkspaceError("sampled observation/mask shape mismatch")
        self._stats = {}
        self.input_bytes = 0
        for name,(_,_,size) in self._spec.items():
            stat = (self.directory/name).stat()
            if stat.st_size != size or m.get("file_sizes",{}).get(name) != size:
                raise WorkspaceError(f"sampled size mismatch: {name}")
            self.input_bytes += size
            self._stats[name] = (stat.st_size,stat.st_mtime_ns,stat.st_ino)
        stat = path.stat()
        self._stats["manifest.json"] = (stat.st_size,stat.st_mtime_ns,stat.st_ino)

    def array(self,name):
        shape,dtype,_ = self._spec[name]
        return np.memmap(self.directory/name,dtype=dtype,mode="r",shape=tuple(shape))

    def verify_unchanged(self):
        for name,wanted in self._stats.items():
            stat = (self.directory/name).stat()
            if (stat.st_size,stat.st_mtime_ns,stat.st_ino) != wanted:
                raise WorkspaceError("sampled workspace changed while scoring")


def lag_grid(maximum,step):
    _integer(maximum,"maximum lag",0)
    _integer(step,"lag step")
    if (maximum+step-1)//step+1 > MAX_GROUPS:
        raise ValueError("arrival lattice exceeds 1000000 groups")
    values = np.arange(0,maximum+1,step,dtype=np.int64)
    if values[-1] != maximum:
        values = np.append(values,maximum)
    return values


class FFTWorkspace:
    """Reusable public templates and mask-dependent Gram bank; no truth data."""
    def __init__(self,sampled,lags,*,precision="fp64",device="cpu",memory_limit_bytes=512*1024*1024):
        start = time.perf_counter()
        self.sampled = sampled
        self.lags = np.asarray(lags)
        if self.lags.ndim != 1 or self.lags.dtype.kind not in "iu" or not len(self.lags) or len(self.lags)>MAX_GROUPS or self.lags[0] < 0 or np.any(np.diff(self.lags)<=0):
            raise ValueError("unique increasing nonnegative integer timing lags are required")
        self.lags = np.asarray([_integer(int(x),"integer arrival lag",0) for x in self.lags],dtype=np.int64)
        if np.any(np.diff(self.lags)<=0):
            raise ValueError("arrival lags must increase without unsigned wraparound")
        if self.lags[-1] >= sampled.samples:
            raise ValueError("arrival lag reaches beyond the sampled capture")
        if precision not in ("fp32","fp64") or (device != "cpu" and not device.startswith("cuda")):
            raise ValueError("unsupported correlation precision/device")
        if device.startswith("cuda") and not torch.cuda.is_available():
            raise ValueError("CUDA unavailable; no implicit CPU fallback")
        self.precision,self.device = precision,device
        self.memory_limit_bytes = _integer(memory_limit_bytes,"memory limit")
        self.nfft = fft.next_fast_len(2*sampled.samples-1)
        self.mapping = np.asarray([[0,1],[2,3]],dtype=np.int64)
        # Includes mapped input pages, public columns/caches, conservative FFT
        # products/work reserves and the complete 4x4 Gram bank. The Python/
        # Torch runtimes and external allocator internals remain outside this
        # explicit-array estimate; it is not a process-RSS promise.
        self.array_bound = (sampled.input_bytes+sampled.samples*16*20+
            self.nfft*16*20+len(self.lags)*16*20+65536)
        if device.startswith("cuda"):
            self.array_bound += self.nfft*16*12+sampled.samples*16*6
        if self.array_bound >= self.memory_limit_bytes:
            raise MemoryError("FFT public templates, Gram bank and work reserve exceed explicit memory limit")
        self.times = np.arange(sampled.samples,dtype=np.longdouble)/np.longdouble(sampled.sample_rate)
        basis = sampled.array("basis.c128")
        a = np.asarray(basis[sampled.selected]).transpose(0,2,1).reshape(4,sampled.samples).copy()
        if not np.isfinite(a).all():
            raise WorkspaceError("nonfinite public template")
        self.valid = np.array(sampled.array("validity.u8"))
        if np.any(self.valid > 1) or int(self.valid.sum()) <= 4:
            raise WorkspaceError("invalid/insufficient sampled validity mask")
        self.observation = np.array(sampled.array("observation.c128"))
        if not np.isfinite(self.observation).all():
            raise WorkspaceError("nonfinite actual sampled observation")
        self.energy = math.fsum(map(float,abs(self.observation[self.valid != 0])**2))
        self.valid_count = int(self.valid.sum())
        for j,frequency in enumerate(sampled.frequencies):
            a[j] *= phase(-frequency,self.times,-sampled.key["frequency_drift_hz_per_second"])
        # A finite template must be available at every shifted coordinate. No
        # circular extension, clipped support or guessed zero tails are used.
        support = [np.flatnonzero(row != 0) for row in a]
        if any(not len(indices) or int(indices[-1])+int(self.lags[-1]) >= sampled.samples for indices in support):
            raise ValueError("complete public template support leaves the capture at the largest lag; export a longer public arrival prior/calendar")
        self.columns = a
        self.support = [(int(indices[0]),int(indices[-1])+1) for indices in support]
        self.preparation_seconds = time.perf_counter()-start
        mark = time.perf_counter()
        self.gram = self._gram_bank()
        self.gram_fft_seconds = time.perf_counter()-mark
        self._resolve_masked_edges()
        mark = time.perf_counter()
        dtype = np.complex64 if precision == "fp32" else np.complex128
        self.correlation_dtype = dtype
        if device == "cpu":
            self.template_fft = fft.fft(np.asarray(a,dtype=dtype),n=self.nfft,axis=-1,workers=1).conj()
        else:
            torch.backends.cuda.matmul.allow_tf32=False
            cdtype = torch.complex64 if precision == "fp32" else torch.complex128
            source = torch.as_tensor(a,dtype=cdtype,device=device)
            self.template_fft = torch.fft.fft(source,n=self.nfft,dim=-1).conj()
            torch.cuda.synchronize(device)
        self.template_fft_seconds = time.perf_counter()-mark
        self.preparation_total_seconds = time.perf_counter()-start
        sampled.verify_unchanged()

    def _gram_bank(self):
        result = np.empty((len(self.lags),4,4),dtype=np.complex128)
        spectra = {}
        for i in range(4):
            for j in range(i,4):
                difference = float(self.sampled.frequencies[j]-self.sampled.frequencies[i])
                if difference not in spectra:
                    weight = self.valid*phase(difference,self.times)
                    spectra[difference] = fft.fft(weight,n=self.nfft,workers=1)
                kernel = np.conj(self.columns[i])*self.columns[j]
                right = fft.fft(np.conj(kernel),n=self.nfft,workers=1).conj()
                values = fft.ifft(spectra[difference]*right,workers=1)[self.lags]
                if i == j:
                    values = values.real
                result[:,i,j] = values
                result[:,j,i] = np.conj(values)
        if not np.isfinite(result).all():
            raise ValueError("nonfinite FFT Gram bank")
        return result

    def _resolve_masked_edges(self):
        """Exact support zeros and bounded FP64 repair of unresolved FFT edges.

        The selection uses only public templates and the fixed erasure mask.
        It never changes the observable-rank tolerance or selects noisy winners.
        """
        mark = time.perf_counter()
        prefix = np.empty(self.sampled.samples+1,dtype=np.int64)
        prefix[0]=0
        np.cumsum(self.valid,dtype=np.int64,out=prefix[1:])
        unique = {}
        runs = []
        operations=0
        for column in self.columns:
            support=column != 0
            identity=np.packbits(support).tobytes()
            if identity not in unique:
                edges=np.flatnonzero(np.diff(np.pad(support.astype(np.int8),(1,1))))
                intervals=edges.reshape(-1,2)
                operations += len(intervals)*len(self.lags)
                if operations > MAX_SUPPORT_OPERATIONS:
                    raise ValueError("exact public support overlap exceeds bounded run/lag work limit")
                counts=np.zeros(len(self.lags),dtype=np.int64)
                for first,stop in intervals:
                    counts += prefix[stop+self.lags]-prefix[first+self.lags]
                unique[identity]=counts
            runs.append(unique[identity])
        self.support_operations=operations
        self.support_counts=np.stack(runs,axis=-1)
        self.zero_columns=self.support_counts == 0
        for j in range(4):
            self.gram[self.zero_columns[:,j],j,:]=0
            self.gram[self.zero_columns[:,j],:,j]=0
        self.support_seconds=time.perf_counter()-mark
        # This is a numerical-resolution trigger, not a claim of a rigorous
        # implementation-independent FFT error bound. Unresolved public groups
        # are recomputed with the original FP64 dot model, never thresholded
        # into artificial Gram modes or given a noise/variance floor.
        full_energy=np.sum(abs(self.columns)**2,axis=1)
        if not np.isfinite(full_energy).all():
            raise ValueError("public template energy is nonfinite")
        self.fft_resolution_scale=64*np.finfo(np.float64).eps*max(1.,math.log2(self.nfft))
        # FFT error must resolve the downstream relative observation modes,
        # not merely the total diagonal energy. Weak masked overlaps can
        # amplify a globally tiny error before the absolute FFT floor is hit.
        # This changes refinement selection, never rank_tolerance or noise.
        self.relative_resolution_trigger=self.fft_resolution_scale/OBSERVABLE_RELATIVE_TOLERANCE
        diagonal=self.gram.diagonal(axis1=-2,axis2=-1).real
        uncertain=np.any((~self.zero_columns)&(diagonal <= self.relative_resolution_trigger*full_energy[None,:]),axis=1)
        groups=np.flatnonzero(uncertain)
        self.refined_rows={}
        self.refinement_examined_rows=0
        self.refinement_retained_rows=0
        self.direct_gram_refinement_seconds=0.
        if not len(groups):
            return
        valid_indices=np.flatnonzero(self.valid)
        low=min(x[0] for x in self.support)
        high=max(x[1] for x in self.support)
        first=np.searchsorted(valid_indices,self.lags[groups]+low)
        stop=np.searchsorted(valid_indices,self.lags[groups]+high)
        spans=stop-first
        examined=int(spans.sum())
        if examined > MAX_REFINEMENT_ROWS:
            raise ValueError("public near-edge FFT refinement exceeds bounded examined-row work limit")
        self.refinement_examined_rows=examined
        reserve=examined*8+int(spans.max(initial=0))*256+len(groups)*256
        if self.array_bound+reserve >= self.memory_limit_bytes:
            raise MemoryError("public near-edge refinement exceeds explicit combined array budget")
        self.array_bound += reserve
        mark=time.perf_counter()
        for gi,a,b in zip(groups,first,stop):
            indices=valid_indices[a:b]
            observed=np.any(self.columns[:,indices-self.lags[gi]] != 0,axis=0)
            indices=np.array(indices[observed],copy=True)
            self.refined_rows[int(gi)]=indices
            self.refinement_retained_rows += len(indices)
            A=self._compact_basis(int(self.lags[gi]),indices)
            self.gram[gi]=A.conj().T@A
        self.direct_gram_refinement_seconds=time.perf_counter()-mark

    def _compact_basis(self,lag,indices):
        values=self.columns[:,indices-lag].T.copy()
        times=self.times[indices]
        for j,frequency in enumerate(self.sampled.frequencies):
            values[:,j] *= phase(frequency,times,self.sampled.key["frequency_drift_hz_per_second"])
        return values

    def correlations(self,frequency_offset=0.,drift_offset=0.):
        _number(frequency_offset,"frequency offset")
        _number(drift_offset,"drift offset")
        r = np.empty((len(self.lags),4),dtype=np.complex128)
        drift = self.sampled.key["frequency_drift_hz_per_second"]+drift_offset
        transformed_by_frequency = {}
        for j,base_frequency in enumerate(self.sampled.frequencies):
            key = float(base_frequency+frequency_offset)
            if key not in transformed_by_frequency:
                z = np.asarray(self.valid*self.observation*phase(-key,self.times,-drift),dtype=self.correlation_dtype)
                if self.device == "cpu":
                    transformed_by_frequency[key] = fft.fft(z,n=self.nfft,workers=1)
                else:
                    source = torch.as_tensor(z,device=self.device)
                    transformed_by_frequency[key] = torch.fft.fft(source,n=self.nfft)
            transformed = transformed_by_frequency[key]
            if self.device == "cpu":
                correlation = fft.ifft(transformed*self.template_fft[j],workers=1)
                r[:,j] = correlation[self.lags]
            else:
                correlation = torch.fft.ifft(transformed*self.template_fft[j])
                indices = torch.as_tensor(self.lags,device=self.device)
                r[:,j] = correlation[indices].detach().cpu().numpy()
        r[self.zero_columns]=0.
        mark=time.perf_counter()
        for gi,indices in self.refined_rows.items():
            A=self._compact_basis(int(self.lags[gi]),indices)
            y=self.observation[indices]*phase(-frequency_offset,self.times[indices],-drift_offset)
            r[gi]=A.conj().T@y
        self.last_direct_correlation_refinement_seconds=time.perf_counter()-mark
        return r

    def direct_statistics(self,lag,frequency_offset=0.,drift_offset=0.):
        """FP64 raw-dot diagnostic on the same public lattice; never resimulates."""
        lag = int(lag)
        if lag < 0 or lag >= self.sampled.samples:
            raise ValueError("invalid direct-reference lag")
        A = np.zeros((self.sampled.samples,4),dtype=np.complex128)
        for j,frequency in enumerate(self.sampled.frequencies):
            A[lag:,j] = self.columns[j,:self.sampled.samples-lag]*phase(
                frequency+frequency_offset,self.times[lag:],
                self.sampled.key["frequency_drift_hz_per_second"]+drift_offset)
        W = A[self.valid != 0]
        y = self.observation[self.valid != 0]
        return W.conj().T@W,W.conj().T@y


def score_workspace(workspace,config,*,drift_offsets=(0.,),retain_group_scores=True):
    """Fixed complete-bank likelihood; tiles never select or discard groups."""
    if config.doppler_width_hz != 0 or config.window_blocks != 1 or config.reblock != 1 or config.window_seconds is not None:
        raise ValueError("FFT path supports one whole-observation static two-tap channel window")
    if config.device != workspace.device or config.precision != workspace.precision:
        raise ValueError("score and correlation arithmetic/device differ")
    if not isinstance(drift_offsets,tuple) or not drift_offsets or len(drift_offsets)>4096 or len(set(drift_offsets)) != len(drift_offsets):
        raise ValueError("unique fixed drift offsets are required")
    for drift in drift_offsets:
        _number(drift,"drift offset")
    count = len(workspace.lags)*len(config.frequency_offsets_hz)*len(drift_offsets)
    if count > MAX_GROUPS:
        raise ValueError("timing/frequency/drift bank exceeds 1000000 effective groups")
    strict,relaxed = config.budgets()
    delta = math.nextafter(strict.delta_per_segment/count,0.)
    if delta <= 0:
        raise ValueError("bank-wide residual calibration allocation underflows")
    tile = min(config.group_tile,len(workspace.lags))
    # Gaussian small factors/scores plus optional fixed full-bank result arrays.
    reserve = tile*128*1024 + (count*2*8 if retain_group_scores else 0)
    if workspace.array_bound+reserve >= config.memory_limit_bytes:
        raise MemoryError("FFT score tiles exceed explicit combined memory budget")
    started = time.perf_counter()
    mixture = np.full(2,-np.inf)
    scores_all = np.empty((count,2)) if retain_group_scores else None
    best_scores = np.full(2,-np.inf)
    best_indices = np.zeros(2,dtype=np.int64)
    variance_min,variance_max = math.inf,0.
    timing = dict(observation_fft_seconds=0.,direct_correlation_refinement_seconds=0.,residual_and_likelihood_seconds=0.)
    ranks = np.zeros(5,dtype=np.int64)
    index = 0
    for drift in drift_offsets:
        for frequency in config.frequency_offsets_hz:
            mark = time.perf_counter()
            r = workspace.correlations(frequency,drift)
            timing["observation_fft_seconds"] += time.perf_counter()-mark-workspace.last_direct_correlation_refinement_seconds
            timing["direct_correlation_refinement_seconds"] += workspace.last_direct_correlation_refinement_seconds
            for first in range(0,len(workspace.lags),tile):
                stop = min(first+tile,len(workspace.lags))
                mark = time.perf_counter()
                h = stop-first
                G = workspace.gram[None,first:stop]
                rr = r[None,first:stop]
                mapping = np.broadcast_to(workspace.mapping,(h,2,2))
                correlations,grams,upper,residual,dof,histogram = prepare_group_tile(
                    G,rr,np.asarray([workspace.energy]),np.asarray([workspace.valid_count],dtype=np.uint64),mapping,
                    reference=config.reference_variance,delta=delta,projection_tile_blocks=1)
                chosen = upper if config.known_noise_variance is None else np.full(h,config.known_noise_variance/config.reference_variance)
                plan = PreparedWindowedGaussian(grams.reshape(1,h*2,2,2),
                    np.asarray([workspace.sampled.samples/(2*workspace.sampled.sample_rate)]),
                    doppler_width_hz=0.,window_blocks=1,stationary_power=config.stationary_power,
                    memory_limit_bytes=config.memory_limit_bytes-workspace.array_bound-reserve)
                scores = plan.score(correlations.reshape(1,h*2,2),variance_scale=np.repeat(chosen,2)[None,:],
                    device=config.device,precision=config.precision).detach().cpu().numpy().reshape(h,2)
                if not np.isfinite(scores).all():
                    raise ValueError("nonfinite fixed-bank likelihood")
                offset = index+first
                if retain_group_scores:
                    scores_all[offset:offset+h] = scores
                mixture = np.logaddexp(mixture,logsumexp(scores,axis=0))
                for bit in (0,1):
                    local = int(np.argmax(scores[:,bit]))
                    if scores[local,bit] > best_scores[bit]:
                        best_scores[bit],best_indices[bit] = scores[local,bit],offset+local
                variance_min = min(variance_min,float(np.min(upper)))
                variance_max = max(variance_max,float(np.max(upper)))
                ranks += histogram.sum(axis=0)
                timing["residual_and_likelihood_seconds"] += time.perf_counter()-mark
            index += len(workspace.lags)
    mixture -= math.log(count)
    workspace.sampled.verify_unchanged()
    timing["total_receiver_seconds"] = time.perf_counter()-started
    winner = int(np.argmax(mixture))
    best = int(best_indices[winner])
    di,remainder = divmod(best,len(config.frequency_offsets_hz)*len(workspace.lags))
    fi,li = divmod(remainder,len(workspace.lags))
    key = dict(workspace.sampled.key)
    key["arrival_seconds"] += int(workspace.lags[li])/workspace.sampled.sample_rate
    key["frequency_hz"] += config.frequency_offsets_hz[fi]
    key["frequency_drift_hz_per_second"] += drift_offsets[di]
    return dict(format="triad-fft-actual-acquisition-v1",mode="actual-noisy",
        method="whole-observation-coupled-two-ray-gaussian-ucb" if config.known_noise_variance is None else "whole-observation-coupled-two-ray-known-noise-diagnostic",
        bit_log_likelihoods=mixture.tolist(),group_bit_log_likelihoods=scores_all.tolist() if retain_group_scores else None,
        strict=decision(mixture,strict.log_threshold),relaxed=decision(mixture,relaxed.log_threshold),
        best_scoring_group_for_winning_bit=best,best_group_public_parameters=key,
        best_group_scope="diagnostic only; all fixed groups enter the uniform mixture",
        geometry=dict(group_count=count,base_group_count=len(workspace.lags),dimension=4,
            sample_count=workspace.sampled.samples,sample_rate=workspace.sampled.sample_rate,
            fixed_epoch_offset_seconds=workspace.sampled.key["epoch_offset_seconds"],fixed_clock_ppm=workspace.sampled.key["clock_ppm"],
            base_arrival_seconds=workspace.sampled.key["arrival_seconds"],lag_samples=workspace.lags.tolist(),
            frequency_offsets_hz=list(config.frequency_offsets_hz),drift_offsets_hz_per_second=list(drift_offsets),
            group_order="drift major, frequency middle, integer arrival lag minor",nfft=workspace.nfft,
            mixture="uniform over every declared timing/frequency/drift group; no pruning"),
        calibration=dict(scope="stationary proper-complex white Gaussian noise; one complete-column union residual per group",
            effective_groups=count,delta_per_group=delta,strict=strict.diagnostics(),relaxed=relaxed.diagnostics(),
            variance_upper_over_reference_range=[variance_min,variance_max],rank_histogram=ranks.tolist(),
            inter_group_independence_assumed=False,known_noise_variance_diagnostic=config.known_noise_variance),
        arithmetic=dict(requested_correlation_precision=config.precision,observation_fft_device=config.device,
            observation_fft_and_products="complex64" if config.precision=="fp32" else "complex128",
            gram_fft="CPU complex128; substantial bulk work, cached across frequency/drift alternatives",
            phase_preparation="CPU longdouble cycle reduction, float64 exponential",
            preparation_and_finish="CPU complex128/float64 residual whitening, ranks, small SVD, budgets and bit mixture",
            likelihood_whitening_and_evidence_reductions=f"complex128/float64 on selected device ({config.device}); FP32 selection applies to bounded mode GEMM after whitening",
            likelihood_bulk=config.precision,tf32_allowed=False,implicit_fallback=False,
            public_near_edge_refinement="explicit FP64 raw-dot exception for fixed public groups whose masked Gram is unresolved by FFT"),
        numerical_refinement=dict(exact_zero_column_groups=int(workspace.zero_columns.sum()),
            exact_zero_whole_groups=int(np.all(workspace.zero_columns,axis=1).sum()),
            support_run_lag_operations=workspace.support_operations,support_work_limit=MAX_SUPPORT_OPERATIONS,
            direct_refined_groups=len(workspace.refined_rows),examined_rows=workspace.refinement_examined_rows,
            retained_rows=workspace.refinement_retained_rows,examined_row_work_limit=MAX_REFINEMENT_ROWS,
            fft_resolution_trigger_scale=workspace.fft_resolution_scale,
            observable_relative_tolerance=OBSERVABLE_RELATIVE_TOLERANCE,
            masked_energy_refinement_trigger=workspace.relative_resolution_trigger,
            selection="public support/Gram only; fixed rank tolerance and full family allocation retained"),
        timing=dict(public_template_preparation_seconds=workspace.preparation_seconds,gram_fft_seconds=workspace.gram_fft_seconds,
            template_fft_seconds=workspace.template_fft_seconds,exact_support_seconds=workspace.support_seconds,
            direct_gram_refinement_seconds=workspace.direct_gram_refinement_seconds,total_preparation_seconds=workspace.preparation_total_seconds,**timing),
        resources=dict(explicit_array_bound_bytes=workspace.array_bound+reserve,memory_limit_bytes=config.memory_limit_bytes,
            group_tile=tile,retained_all_group_scores=retain_group_scores,
            scope="includes input mappings/FFT reserves; excludes Python/Torch runtime and external allocator internals; not process RSS"),
        limits=["one fixed public epoch and sample-clock hypothesis; one fractional base-arrival branch",
            "arrival search uses a nonnegative integer sample lattice; continuous/fractional timing loss is unqualified",
            "one whole-observation static two-ray channel; not a phase tracker or renewed-channel likelihood",
            "Gaussian-noise calibration does not establish correct-bit or wrong-key rejection",
            "sampled channel impairments and full masks/tails are retained; physical RF retuning is not added"])


def normalize_arguments(arguments):
    result=[]
    drift_list=False
    terminated=False
    for token in _normalize_scientific_arguments(arguments):
        if token == "--":
            terminated=True
        if not terminated and token == "--drift-offsets-hz-per-second":
            drift_list=True
        elif not terminated and drift_list:
            try:
                value=float(token)
            except ValueError:
                if token.startswith("-"):
                    drift_list=False
            else:
                if math.isfinite(value) and token.startswith("-") and "e" in token.lower():
                    token=format(Decimal.from_float(value),"f")
        result.append(token)
    return result


def parse_args(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("input",type=Path)
    p.add_argument("--lag-max-seconds",type=float,required=True)
    p.add_argument("--lag-step-samples",type=int,default=1)
    p.add_argument("--frequency-offsets-hz",type=float,nargs="+")
    p.add_argument("--frequency-offset-grid",type=float,nargs=3)
    p.add_argument("--drift-offsets-hz-per-second",type=float,nargs="+",default=[0.])
    p.add_argument("--group-tile",type=int,default=128)
    p.add_argument("--memory-mib",type=float,default=512.)
    p.add_argument("--device",default="cpu")
    p.add_argument("--precision",choices=("fp32","fp64"),default="fp64",
        help="observation FFT/likelihood bulk only; Gram FFTs and bounded public near-edge raw-dot refinement remain explicitly CPU FP64")
    p.add_argument("--reference-variance",type=float,default=1.)
    p.add_argument("--tap-powers",type=float,nargs=2,default=(.5,.5))
    p.add_argument("--attempts-per-day",type=int,default=1440)
    p.add_argument("--duration-alternatives",type=int,default=8)
    p.add_argument("--selection-methods",type=int,default=16)
    p.add_argument("--known-noise-variance",type=float)
    p.add_argument("--omit-group-scores",action="store_true")
    p.add_argument("--evaluate-simulation",action="store_true")
    p.add_argument("--output",type=Path,required=True)
    args = p.parse_args(normalize_arguments(sys.argv[1:] if argv is None else argv))
    if args.frequency_offsets_hz is not None and args.frequency_offset_grid is not None:
        p.error("choose a frequency list or grid")
    args.frequency_offsets_hz = tuple(args.frequency_offsets_hz or (frequency_offset_grid(*args.frequency_offset_grid) if args.frequency_offset_grid else (0.,)))
    if not math.isfinite(args.lag_max_seconds) or args.lag_max_seconds < 0 or not math.isfinite(args.memory_mib) or args.memory_mib <= 0:
        p.error("finite nonnegative lag and positive memory budget required")
    return args


def main(argv=None):
    args = parse_args(argv)
    torch.set_num_threads(1)
    if args.output.exists():
        raise ValueError("output exists; refusing overwrite")
    sampled = SampledWorkspace(args.input)
    input_hashes = {name:sha256(args.input/name) for name in PUBLIC_FILES}
    scaled = np.longdouble(args.lag_max_seconds)*np.longdouble(sampled.sample_rate)
    if not np.isfinite(scaled) or scaled >= sampled.samples:
        raise ValueError("arrival-lag extent reaches beyond the sampled capture")
    maximum = int(np.ceil(scaled))
    lags = lag_grid(maximum,args.lag_step_samples)
    capacity = int(args.memory_mib*1024*1024)
    work = FFTWorkspace(sampled,lags,precision=args.precision,device=args.device,memory_limit_bytes=capacity)
    config = AcquisitionConfig(0.,window_blocks=1,reference_variance=args.reference_variance,
        stationary_power=tuple(args.tap_powers),group_tile=args.group_tile,memory_limit_bytes=capacity,
        device=args.device,precision=args.precision,attempts_per_day=args.attempts_per_day,
        duration_alternatives=args.duration_alternatives,model_alternatives=args.selection_methods,
        known_noise_variance=args.known_noise_variance,frequency_offsets_hz=args.frequency_offsets_hz)
    report = score_workspace(work,config,drift_offsets=tuple(args.drift_offsets_hz_per_second),retain_group_scores=not args.omit_group_scores)
    report["input_sha256"] = input_hashes
    report["receiver_source_sha256"] = sha256(Path(__file__))
    report["dependency_sha256"] = {name:sha256(Path(__file__).with_name(name)) for name in (
        "triad_group_acquire.py","triad_gaussian_window.py","triad_noise_likelihood.py","triad_gaussian_state.py","triad_state_workspace.py")}
    if args.evaluate_simulation:
        control,truth = sampled.manifest.get("control"),sampled.manifest.get("signal_bit_simulation_truth")
        if control not in ("signal","null","wrong-key") or truth not in (0,1):
            raise ValueError("invalid simulation-only evaluation metadata")
        outcome = {}
        for gate in ("strict","relaxed"):
            accepted,bit = report[gate]["accepted"],report[gate]["bit"]
            outcome[gate] = (dict(correct_admitted=int(accepted and bit==truth),wrong_admitted=int(accepted and bit!=truth),missed=int(not accepted))
                if control=="signal" else dict(false_admitted=int(accepted),not_admitted=int(not accepted)))
        report["simulation_evaluation_only"] = dict(control=control,signal_bit=truth,outcomes=outcome)
    sampled.verify_unchanged()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with args.output.open("x") as out:
        json.dump(report,out,indent=2,allow_nan=False)
        out.write("\n")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError,OSError,MemoryError) as error:
        print(f"triad_fft_acquire: {error}",file=sys.stderr)
        raise SystemExit(2)
