#!/usr/bin/env python3
"""Pilot-aware onset proposals for the isolated weak-signal Helper receiver.

The guaranteed DATA-free part of the public Helper lead supplies a preliminary
spectral core fit.  A continuous causal narrowband FIR then measures onset from
those cores, instead of total wideband energy.  Only after retaining an onset
region is the existing 36-second frequency estimator called.  Neither working
score nor its region is a calibrated confidence bound or a DATA admission.
"""
from __future__ import annotations

import copy
import hashlib
import json
import math
from pathlib import Path
import time

import numpy as np
from scipy import fft, signal, optimize

from triad_dynamic_frequency import (estimate_dynamic_frequency, frequency_policy,
                                     _whittle_profile, _regions)


_DEFAULT = dict(minimum_coarse_seconds=8., coarse_guard_seconds=.1,
    coarse_presence_score=18., coarse_peak_ratio=4., coarse_profile_iterations=20,
    filter_half_support_seconds=.3, filter_half_width_hz=4.,
    filter_output_rate_hz=100., onset_step_seconds=.02,
    onset_score_drop=6., onset_padding_seconds=.15,
    maximum_onset_width_seconds=1.25, maximum_onset_apertures=8, minimum_level_ratio=1.25,
    minimum_noise_seconds=.25, maximum_samples=8_000_000)


def weak_helper_policy(config=None):
    if config is not None and not isinstance(config, dict):
        raise ValueError('config must be a dictionary')
    unknown = set(config or ())-set(_DEFAULT)-{'frequency_config'}
    if unknown:
        raise ValueError('unknown weak Helper configuration: '+', '.join(sorted(unknown)))
    p = dict(_DEFAULT)
    p.update({k:v for k,v in (config or {}).items() if k != 'frequency_config'})
    for name, value in p.items():
        if isinstance(value, (bool, np.bool_)) or not isinstance(value, (int,float,np.number)) or not math.isfinite(value) or value <= 0:
            raise ValueError(name+' must be finite and positive')
    for name in ('maximum_samples','coarse_profile_iterations','maximum_onset_apertures'):
        if int(p[name]) != p[name]:
            raise ValueError(name+' must be integral')
        p[name] = int(p[name])
    fc = dict(modulation_width_allowance=0., require_data_free=True)
    fc.update((config or {}).get('frequency_config', {}))
    if fc['require_data_free'] is not True:
        raise ValueError('final frequency aperture must be DATA-free')
    return dict(schema='triad-weak-helper-policy-v2', **p,
        frequency_config=fc, frequency_policy=frequency_policy(fc),
        core_scope='Preliminary persistent/Gaussian ray cores, not full AM-spectrum likelihood; full observed waveform remains input.',
        onset_scope='Independent persistent-core complex means only for zero random ray width/motion; otherwise filtered-energy change point. Covariance-scale correction and padding are working approximations, not confidence bounds.',
        persistent_frequency_refinement='FP64 bounded scalar Hann complex-correlation maximum inside one coarse FFT bin, absolute tolerance 1/(1000*aperture duration), maximum24 evaluations.',
        phase_correction=False, clock_narrowing=False,
        aperture_policy='Partition entire retained onset hull into bounded cells; union every conditional 36-second frequency proposal. Any fallback preserves full-bank fallback.',
        calibration='All adaptive spectral/onset/frequency selection requires complete-procedure admission calibration.')


def estimate_weak_helper(raw, manifest, original_frequencies, *, valid=None, config=None):
    start_cpu, start_wall = time.process_time(), time.perf_counter()
    p = weak_helper_policy(config)
    bank = np.asarray(original_frequencies, dtype=float)
    if bank.ndim != 1 or not len(bank) or not np.isfinite(bank).all() or len(np.unique(bank)) != len(bank):
        raise ValueError('finite nonempty unique frequency bank required')
    result = dict(schema='triad-weak-helper-v1', status='fallback', reason='',
        original_frequencies=bank.tolist(), original_count=len(bank),
        selected_frequencies=bank.tolist(), selected_count=len(bank),
        expanded_intervals_hz=[], onset_intervals_seconds=[], frequency=None,
        phase_reference_usable=False, clock_status='unchanged', policy=p)
    names = ('triad_weak_helper.py','triad_dynamic_frequency.py')
    sources = {n:hashlib.sha256((Path(__file__).parent/n).read_bytes()).hexdigest() for n in names}
    geometry = {k:manifest.get(k) for k in ('sample_rate_hz','helper_enabled','format','channel','calendar','helper_plan')}
    result['procedure_id'] = hashlib.sha256(json.dumps(dict(policy=p, geometry=geometry,
        frequencies=bank.tolist(), sources=sources),sort_keys=True,allow_nan=False).encode()).hexdigest()
    def finish(reason):
        result['reason'] = reason
        result['timing'] = dict(cpu_seconds=time.process_time()-start_cpu,
                                wall_seconds=time.perf_counter()-start_wall)
        return result
    if manifest.get('helper_enabled', True) is not True:
        return finish('helper_disabled')
    cal, ch, hp = manifest['calendar'], manifest['channel'], manifest['helper_plan']
    if hp.get('pilot_dwell_gating',False) or manifest.get('format',{}).get('physical_rf_retuning',False):
        return finish('gated_or_retuned_pilots_unsupported')
    fs = float(manifest['sample_rate_hz'])
    if not math.isfinite(fs) or fs <= 0:
        raise ValueError('positive sample rate required')
    y = np.asarray(raw)
    if y.ndim != 1 or not np.iscomplexobj(y) or len(y) != cal['capture_samples'] or not np.isfinite(y).all():
        raise ValueError('finite complex capture matching public length required')
    if len(y) > p['maximum_samples']:
        return finish('helper_work_limit')
    mask = np.ones(len(y),bool) if valid is None else np.asarray(valid)
    if mask.shape != y.shape or not np.isin(mask,(0,1)).all():
        raise ValueError('valid must be a binary capture mask')
    required = ('sample_clock_pair_ppm','endpoint_uniform_half_bound_ppm','spread_margin',
        'spread_delay_ms','delay_excursion_seconds','random_doppler_width_hz','spread_doppler_hz',
        'nominal_conversion_if_hz')
    if any(n not in ch for n in required):
        return finish('public_uncertainty_metadata_missing')
    prior = np.asarray(cal['arrival_prior_seconds'],float)
    if prior.shape != (2,) or not np.isfinite(prior).all() or prior[0] > prior[1]:
        raise ValueError('ordered finite arrival prior required')
    margin = float(ch['spread_margin'])
    endpoint = float(ch['endpoint_uniform_half_bound_ppm'])*margin*1e-6
    audio = float(ch['sample_clock_pair_ppm'])*margin*1e-6
    if max(endpoint,audio) >= .1:
        return finish('clock_envelope_unsupported')
    lead = float(cal['capture_lead_seconds'])
    hstart,hend = float(cal.get('helper_start_seconds',0.)),float(cal['helper_end_seconds'])
    source_start = float(cal['data_sample_offset'])/fs
    delay = float(ch['spread_delay_ms'])*.001+float(ch['delay_excursion_seconds'])
    settling = max(delay,float(cal.get('fixed_capture_tail_bound_seconds',delay)))
    guard = p['coarse_guard_seconds']
    begin = (prior[1]+hstart/(1-endpoint)+settling+guard)*(1+endpoint)
    end = min((prior[0]+source_start/(1+endpoint)-guard)*(1-endpoint),
              (prior[0]+hend/(1+endpoint)-guard)*(1-endpoint))
    first,last = max(0,math.ceil((begin+lead)*fs)),min(len(y),math.floor((end+lead)*fs))
    if (last-first)/fs < p['minimum_coarse_seconds']:
        return finish('insufficient_guaranteed_coarse_support')
    if not mask[:last].all():
        return finish('known_erasure_in_helper_support')
    duration = (last-first)/fs
    width = float(ch['random_doppler_width_hz'])
    separation = float(ch['spread_doppler_hz'])
    motion = float(ch.get('delay_motion_doppler_bound_hz',0.))
    sigma = math.hypot(width/2,.75/duration)
    taper = np.hanning(last-first)
    spectrum = fft.fft(y[first:last].astype(np.complex64)*taper.astype(np.float32),workers=1)
    power = np.abs(spectrum.astype(np.complex128))**2/np.dot(taper,taper)
    freqs = fft.fftfreq(last-first,1/fs)
    order = np.argsort(freqs); freqs,power = freqs[order],power[order]
    diagnostics, targets = {}, []
    profile = dict(p['frequency_policy']); profile['profile_iterations'] = p['coarse_profile_iterations']
    for name in ('low','high'):
        center = float(hp[name+'_hz'])-float(ch['nominal_conversion_if_hz'])
        clock_allowance = abs(center)*audio
        lo,hi = float(bank.min()-clock_allowance),float(bank.max()+clock_allowance)
        step = max(1/duration,sigma/4)
        points = max(2,math.ceil((hi-lo)/step)+1)
        if points > profile['maximum_profile_points']:
            return finish('coarse_profile_work_limit')
        grid = np.linspace(lo,hi,points)
        pad = 6*sigma+3/duration
        fitlo,fithi = center+lo-pad,center+hi+separation+pad
        if fitlo <= -fs/2 or fithi >= fs/2:
            diagnostics[name] = dict(usable=False,reason='pilot_support_outside_capture')
            continue
        keep = (freqs >= fitlo)&(freqs <= fithi)
        scores,peaks = _whittle_profile(power[keep],freqs[keep]-center,grid,separation,sigma,profile)
        best = int(np.argmax(scores))
        usable = bool(scores[best] >= p['coarse_presence_score'] and peaks[best] >= p['coarse_peak_ratio'])
        diagnostics[name] = dict(usable=usable,best_shift_hz=float(grid[best]),
            presence_score=float(scores[best]),peak_background_ratio=float(peaks[best]),
            competitive_regions_hz=_regions(grid,scores,profile['competitive_score_drop']),
            fit_window_hz=[fitlo,fithi], profile_points=points)
        if usable:
            # Both physical rays are retained; no common pilot phase is fitted.
            for doppler in (0.,separation):
                target = center+float(grid[best])+doppler
                if not any(abs(target-old) < 1e-9 for old in targets):
                    targets.append(target)
    result['coarse_frequency'] = dict(pilot_diagnostics=diagnostics,
        aperture_seconds=[first/fs-lead,last/fs-lead],observation_seconds=duration,
        overlaps_possible_data=False,targets_hz=targets,
        scope='Preliminary spectral proposal used only for onset filtering, not final frequency assistance.')
    if not targets:
        return finish('no_coarse_pilot')
    if width == 0. and motion == 0.:
        # A coarse spectral-grid error can rotate a fitted mean by half a cycle
        # when extrapolated to onset. Refine each ray/pilot independently from
        # the guaranteed Helper-only aperture before using complex evidence.
        # No observed onset or true clock enters this local frequency fit.
        fit_time=(np.arange(last-first,dtype=float)-(last-first-1)/2)/fs
        weighted=y[first:last].astype(np.complex128)*taper
        refinements=[]
        for index,target in enumerate(targets):
            radius=1/duration
            def objective(frequency):
                correlation=np.dot(weighted,np.exp(-2j*np.pi*np.remainder(frequency*fit_time,1.)))
                return -float(abs(correlation)**2)
            fitted=optimize.minimize_scalar(objective,bounds=(target-radius,target+radius),
                method='bounded',options=dict(xatol=1/(1000*duration),maxiter=24))
            refinements.append(dict(original_hz=target,refined_hz=float(fitted.x),
                bracket_hz=[target-radius,target+radius],success=bool(fitted.success),
                evaluations=int(fitted.nfev),objective=float(fitted.fun)))
            if not fitted.success:
                result['coarse_frequency']['core_refinements']=refinements
                return finish('coarse_frequency_refinement_failed')
            targets[index]=float(fitted.x)
        result['coarse_frequency']['core_refinements']=refinements
        result['coarse_frequency']['targets_hz']=targets
    half_width = max(p['filter_half_width_hz'],3*width+motion)
    decimation = max(1,int(fs/p['filter_output_rate_hz']))
    output_fs = fs/decimation
    if half_width >= output_fs*.4:
        return finish('onset_filter_bandwidth_unsupported')
    half_support = math.ceil(p['filter_half_support_seconds']*fs)
    taps = signal.firwin(2*half_support+1,half_width,window=('kaiser',8.),fs=fs)
    # Full causal convolution, including tail, with explicitly delay-corrected
    # output timestamps. The filter is never restarted at an onset hypothesis.
    count = min(len(y),math.ceil((end+lead+2*p['filter_half_support_seconds'])*fs))
    sample_time = np.arange(count,dtype=float)/fs-lead
    energy, branches = None, []
    for target in targets:
        mixed = y[:count]*np.exp(-2j*np.pi*np.remainder(target*sample_time,1.))
        filtered = signal.oaconvolve(mixed,taps,mode='full')[::decimation]
        branches.append(filtered)
        branch = np.abs(filtered)**2
        energy = branch if energy is None else energy+branch
    dates = (np.arange(len(energy))*decimation-half_support)/fs-lead
    earliest = (prior[0]+hstart/(1+endpoint))*(1-endpoint)
    latest = (prior[1]+hstart/(1-endpoint))*(1+endpoint)
    noise_sel = (dates >= -lead+p['filter_half_support_seconds'])&(dates < earliest-p['filter_half_support_seconds']-guard)
    signal_sel = (dates >= latest+settling+p['filter_half_support_seconds']+guard)&(dates < end-p['filter_half_support_seconds'])
    if min(noise_sel.sum(),signal_sel.sum())/output_fs < p['minimum_noise_seconds']:
        return finish('insufficient_filtered_level_support')
    v0,v1 = float(energy[noise_sel].mean()),float(energy[signal_sel].mean())
    if min(v0,v1) <= 0:
        return finish('unusable_filtered_levels')
    ratio = v1/v0
    result['onset'] = dict(noise_level=v0,helper_level=v1,helper_to_noise_ratio=ratio,
        targets_hz=targets,filter_half_width_hz=half_width,filter_taps=len(taps),
        filter_group_delay_seconds=half_support/fs,output_sample_rate_hz=output_fs)
    if ratio < p['minimum_level_ratio']:
        return finish('weak_filtered_helper_onset')
    step = max(1/output_fs,p['onset_step_seconds'])
    grid = np.linspace(earliest,latest,max(1,math.ceil((latest-earliest)/step))+1)
    indices = np.searchsorted(dates,grid)
    a,b = int(indices.min()),int(indices.max())
    prefix = np.r_[0.,np.cumsum(energy[a:b],dtype=float)]
    counts = indices-a
    # FIR outputs are correlated. This noise-bandwidth scale is a working
    # correction, not a proof for fading/nonstationary or overlapping branches.
    enbw = fs*float(np.dot(taps,taps))
    scale = min(1.,enbw/output_fs)
    if width == 0. and motion == 0.:
        # With persistent ray cores, complex averaging preserves weak-signal
        # information lost by the power-only change point. Each observed pilot/
        # ray branch has its own complex mean; no cross-frequency phase is used.
        # Mean fitting and change-point selection are one adaptive procedure.
        branch_array=np.asarray(branches)
        means=branch_array[:,signal_sel].mean(axis=1)
        branch_variance=v0/len(branches)
        evidence=(2*np.real(means.conj()[:,None]*branch_array)-np.abs(means[:,None])**2).sum(axis=0)/branch_variance
        evidence_prefix=np.r_[0.,np.cumsum(evidence[a:b],dtype=float)]
        scores=-evidence_prefix[counts]*scale
        onset_method='independent persistent pilot/ray complex means'
        result['onset']['mean_coherent_fraction']=float(np.sum(np.abs(means)**2)/v1)
    else:
        scores = (counts*math.log(ratio)+prefix[counts]*(1/v1-1/v0))*scale
        onset_method='renewing-ray filtered energy change point'
    scores -= scores.max()
    keep = scores >= -p['onset_score_drop']
    starts=np.flatnonzero(keep&np.r_[True,~keep[:-1]])
    ends=np.flatnonzero(keep&np.r_[~keep[1:],True])
    clock_pad = (max(abs(earliest),abs(latest))+abs(hstart))*max(audio,endpoint)
    padding = p['onset_padding_seconds']+step+clock_pad
    intervals=[]
    for i,j in zip(starts,ends):
        interval=[max(float(prior[0]),float(grid[i]-hstart-padding)),
                  min(float(prior[1]),float(grid[j]-hstart+padding))]
        if intervals and interval[0] <= intervals[-1][1]:intervals[-1][1]=max(intervals[-1][1],interval[1])
        else:intervals.append(interval)
    hull=[intervals[0][0],intervals[-1][1]]
    result['onset_intervals_seconds']=intervals
    result['onset'].update(best_arrival_seconds=float(grid[np.argmax(scores)]-hstart),
        padded_arrival_hull_seconds=hull,noise_bandwidth_hz=enbw,score_scale=scale,
        grid_seconds=grid.tolist(),relative_log_scores=scores.tolist(),
        method=onset_method,
        uncertainty_scope='Working spectral/complex-mean or filtered-energy proposal; no coverage guarantee.')
    cells=max(1,math.ceil((hull[1]-hull[0])/p['maximum_onset_width_seconds']))
    if cells > p['maximum_onset_apertures']:
        return finish('onset_aperture_work_limit')
    edges=np.linspace(*hull,cells+1)
    result['aperture_conditioning']=dict(original_arrival_prior_seconds=prior.tolist(),
        acquired_arrival_hull_seconds=hull,scope='Only frequency-aperture conditioning; DATA timing prior is unchanged. Only an onset cell containing the true arrival justifies its DATA-free aperture.',
        branches=[])
    branches=result['aperture_conditioning']['branches']
    for left,right in zip(edges[:-1],edges[1:]):
        conditioned=copy.deepcopy(manifest)
        conditioned['calendar']['arrival_prior_seconds']=[float(left),float(right)]
        f=estimate_dynamic_frequency(y,conditioned,bank,valid=mask,config=p['frequency_config'])
        branches.append(dict(onset_cell_seconds=[float(left),float(right)],frequency=f,
            data_free_condition='Conditional on true onset lying in this cell and selected physical envelopes.'))
    # A failed plausible branch must never be silently discarded: it could
    # contain the actual onset. Returning full-bank fallback preserves that
    # uncertainty; the outer receiver may report an explicit work limit.
    if any(b['frequency']['status']!='narrowed' for b in branches):
        result['frequency']=dict(status='fallback',reason='onset_branch_frequency_fallback',
            selected_frequencies=bank.tolist(),selected_count=len(bank),expanded_intervals_hz=[])
        return finish('onset_branch_frequency_fallback')
    if len(branches)==1:
        frequency=branches[0]['frequency']
    else:
        regions=sorted(r for b in branches for r in b['frequency']['expanded_intervals_hz'])
        merged=[]
        for left,right in regions:
            if merged and left<=merged[-1][1]:merged[-1][1]=max(merged[-1][1],right)
            else:merged.append([left,right])
        selected=sorted(set(f for b in branches for f in b['frequency']['selected_frequencies']))
        frequency=dict(status='narrowed',reason='conditional_onset_aperture_union',
            selected_frequencies=selected,selected_count=len(selected),expanded_intervals_hz=merged,
            original_count=len(bank),observation_seconds=min(b['frequency']['observation_seconds'] for b in branches),
            overlaps_possible_data=None,cleanliness_scope='Branch-conditional; wrong onset cells can include DATA.',
            aperture_count=len(branches))
    result['frequency']=frequency
    if frequency['status'] != 'narrowed':
        return finish('frequency_'+frequency['reason'])
    result.update(status='narrowed',selected_frequencies=frequency['selected_frequencies'],
        selected_count=frequency['selected_count'],expanded_intervals_hz=frequency['expanded_intervals_hz'])
    return finish('pilot_aware_onset_relative_frequency_proposal')
