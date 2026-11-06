#!/usr/bin/env python3
"""Source-only triad carrier-spacing sweep over saved native probe arrays.

This recombines the exact emitted, shaped carrier envelopes at candidate public
spacings. It does not rerender symbols, propagate a channel, or infer reception.
Window extrema are descriptive overlapping measurements, not confidence bounds.
"""
from __future__ import annotations
import argparse
import json
import math
import hashlib
from pathlib import Path
import re
import numpy as np

sys_path = Path(__file__).resolve().parent
import sys
sys.path.insert(0, str(sys_path))
from triad_pattern_metrics import envelope_metrics, spectrum_metrics

MAX_ROWS = 500_000
MAX_RATIOS = 16
MAX_PROBES = 32
MAX_CAMPAIGN_WORK = 16_000_000
WINDOWS = (.08, .09, .10, .11, .12, .15, .20, .40, .80)
TAUS = (.08, .10, .12, .80)
_SAFE_NAME = re.compile(r'^[A-Za-z0-9_.-]+$')


def _array(directory: Path, name, shape, dtype):
    if not isinstance(name, str) or Path(name).name != name or not _SAFE_NAME.fullmatch(name):
        raise ValueError('unsafe probe array filename')
    path = directory / name
    expected = math.prod(shape) * np.dtype(dtype).itemsize
    if path.stat().st_size != expected:
        raise ValueError(f'{name} extent does not match manifest shape')
    result = np.memmap(path, mode='r', dtype=dtype, shape=tuple(shape))
    if not np.isfinite(result).all():
        raise ValueError(f'{name} contains nonfinite samples')
    return result


def validate_manifest(m, directory=None):
    if not isinstance(m, dict):
        raise ValueError('probe manifest must be an object')
    if m.get('schema') != 'triad-pattern-probe-v1' or m.get('source_only') is not True:
        raise ValueError('native triad-pattern-probe-v1 source-only manifest required')
    if m.get('channel_applied') is not False or m.get('dtype') != '<c16' or m.get('envelope_dtype') != '<f8':
        raise ValueError('probe must declare unpropagated <c16> samples and <f8> envelope')
    fmt=m.get('format')
    if not isinstance(fmt,dict): raise ValueError('probe format metadata must be an object')
    pulse = fmt.get('pulse_shape')
    if pulse not in ('rrc-025-span8', 'rrc-025-span8-c2-tail2'):
        raise ValueError('unsupported pulse shape or unknown rolloff; native width formula requires rolloff .25')
    n = m.get('rows'); fs = m.get('sample_rate_hz'); fine = m.get('fine_rate_hz')
    first = m.get('first_sample'); origin = m.get('calendar_origin_sample')
    if not isinstance(n, int) or not 1 <= n <= MAX_ROWS:
        raise ValueError(f'probe rows must be in [1,{MAX_ROWS}]')
    if not all(isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v) and v > 0 for v in (fs, fine)):
        raise ValueError('sample and fine rates must be positive finite numbers')
    if not isinstance(first, int) or isinstance(first,bool) or not isinstance(origin, int) or isinstance(origin,bool) or not (0 <= first < 2**64 and 0 <= origin < 2**64):
        raise ValueError('invalid sample coordinates')
    spacing = m.get('carrier_spacing_hz')
    if not isinstance(spacing, (int,float)) or isinstance(spacing,bool) or not math.isfinite(spacing) or spacing <= 0:
        raise ValueError('baseline carrier spacing must be positive and finite')
    expected_shapes = {'composite_shape': [2,n], 'carrier_shape': [2,3,n], 'envelope_shape': [n]}
    for key, shape in expected_shapes.items():
        if m.get(key) != shape:
            raise ValueError(f'{key} differs from expected probe geometry')
    if fs < 4*fine or 1.1*(2*spacing+1.25*fine) >= fs:
        raise ValueError('native baseline spacing violates sample-rate geometry guards')
    if directory is not None:
        for key,shape in expected_shapes.items():
            name=m.get({'composite_shape':'composite_file','carrier_shape':'carrier_file','envelope_shape':'envelope_file'}[key])
            if not isinstance(name,str) or Path(name).name!=name or not _SAFE_NAME.fullmatch(name):
                raise ValueError('unsafe probe array filename')
            expected=math.prod(shape)*np.dtype('<f8' if key=='envelope_shape' else '<c16').itemsize
            if (Path(directory)/name).stat().st_size!=expected:
                raise ValueError(f'{name} extent does not match manifest shape')
    return expected_shapes

def load_probe(directory):
    directory = Path(directory)
    m = json.loads((directory / 'manifest.json').read_text())
    expected_shapes = validate_manifest(m,directory)
    x = _array(directory, m['composite_file'], expected_shapes['composite_shape'], '<c16')
    c = _array(directory, m['carrier_file'], expected_shapes['carrier_shape'], '<c16')
    e = _array(directory, m['envelope_file'], expected_shapes['envelope_shape'], '<f8')
    if np.any(e < 0):
        raise ValueError('negative shaping envelope')
    return m, x, c, e


def reconstruct(carriers, fs, first_sample, origin_sample, spacing):
    # Subtract integer coordinates before converting: large UTC sample indices
    # otherwise lose fractional-sample phase in float64. Keep cycles extended
    # and reduce modulo one before calling exp, like the native renderer.
    delta = np.arange(carriers.shape[-1], dtype=np.longdouble) + np.longdouble(first_sample-origin_sample)
    t = delta / np.longdouble(fs)
    cycles = np.remainder(np.array([-1.,0.,1.],dtype=np.longdouble)[:,None]*
                          np.longdouble(spacing)*t[None,:], 1.)
    phase = np.exp(2j*np.pi*np.asarray(cycles,dtype=np.float64))
    return np.sum(carriers*phase[None,:,:],axis=1)


def _one_pole(waveform, envelope, fs, tau):
    # scipy's one-pole IIR recurrence preserves the detector state over the
    # supplied continuous probe. The initial state is its first input value.
    from scipy.signal import lfilter
    power = np.abs(waveform)**2
    beta = math.exp(-1. / (fs * tau))
    raw = lfilter([1-beta], [1, -beta], power, zi=[beta*float(power[0])])[0]
    known = lfilter([1-beta], [1, -beta], envelope*envelope,
                    zi=[beta*float(envelope[0]**2)])[0]
    valid = (np.arange(len(power)) >= math.ceil(5*tau*fs)) & (known > .1)
    if not valid.any():
        return dict(measured=False, reason='no active samples after 5 time constants')
    # Correct only the known transmitter gate response, not fading or signal.
    corrected = raw[valid] / known[valid]
    median = float(np.median(corrected))
    if not math.isfinite(median) or median <= 0:
        return dict(measured=False, reason='no positive corrected detector energy')
    db = 10*np.log10(np.maximum(corrected, 1e-300)/median)
    gain = np.clip(np.sqrt(median/np.maximum(raw[valid], 1e-300)), .1, 10.)
    changed = waveform[valid] * gain
    original = waveform[valid]
    overlap = abs(np.vdot(original, changed))**2 / max(float(np.vdot(original, original).real * np.vdot(changed, changed).real), 1e-300)
    overlap=min(1.,max(0.,float(overlap)))
    # This is a high-SNR shape-overlap screen for the illustrative gain law.
    return dict(measured=True, tau_seconds=tau, samples=int(valid.sum()),
                corrected_power_max_abs_db=float(np.max(np.abs(db))),
                corrected_power_quantiles_db=np.quantile(db,[.01,.05,.5,.95,.99]).tolist(),
                raw_detector_power_median=float(np.median(raw[valid])),
                gain_min=float(gain.min()), gain_max=float(gain.max()),
                gain_clamped_fraction=float(np.mean((gain <= .1+1e-12)|(gain >= 10.-1e-12))),
                normalized_matched_template_overlap=float(overlap),
                matched_template_loss_db=float(-10*math.log10(max(overlap,1e-300))) )


def beat_nulls_in_window(spacing, low=.08, high=.12, limit=64):
    if not (math.isfinite(spacing) and spacing>0 and 0<low<=high and limit>=1):
        raise ValueError('invalid spacing or beat-null report bounds')
    first=math.ceil(low*spacing); last=math.floor(high*spacing)
    count=max(0,last-first+1)
    shown=min(count,limit)
    return dict(total_count=count,seconds=[round(k/spacing,12) for k in range(first,first+shown)],
                truncated=count>limit,report_limit=limit)

def _shape_metrics(x, envelope, fs, nominal_width, summed_windows=None):
    step = 10*math.log10(1.6)
    out=[]
    for bit in range(2):
        p=np.abs(x[bit])**2
        windows=[envelope_metrics(p,envelope,fs,w,step) for w in WINDOWS]
        carrier_windows=(summed_windows[bit] if summed_windows is not None else None)
        poles={str(tau):_one_pole(x[bit],envelope,fs,tau) for tau in TAUS}
        out.append(dict(bit=bit, mean_power=float(np.mean(p)), windows=windows,
                        summed_carrier_windows=carrier_windows, one_pole_power_detector=poles,
                        spectrum=spectrum_metrics(x[bit],fs,nominal_width)))
    norms=np.linalg.norm(x,axis=1)
    if np.any(norms <= 0): raise ValueError('empty candidate waveform')
    corr=complex(np.vdot(x[0]/norms[0],x[1]/norms[1]))
    return out, dict(normalized_bit_correlation=[corr.real,corr.imag],
                      coherent_distance_squared=float(np.sum(abs(x[0]/norms[0]-x[1]/norms[1])**2)),
                      unknown_global_phase_distance_squared=float(max(0.,2-2*abs(corr))))


def analyze(directory, ratios):
    ratios=validate_ratios(ratios)
    m,x,c,e=load_probe(directory)
    fs=float(m['sample_rate_hz']); fine=float(m['fine_rate_hz']); base=float(m['carrier_spacing_hz'])
    base_width=2*base+1.25*fine
    baseline=reconstruct(c,fs,m['first_sample'],m['calendar_origin_sample'],base)
    scale=max(float(np.linalg.norm(x)),1e-300)
    err=float(np.linalg.norm(baseline-x)/scale)
    if not math.isfinite(err) or err > 1e-8:
        raise ValueError(f'native carrier envelopes do not reconstruct composite (relative error {err:g})')
    step=10*math.log10(1.6)
    summed_power=np.sum(np.abs(c)**2,axis=1)
    summed_windows=[[envelope_metrics(summed_power[b],e,fs,w,step) for w in WINDOWS] for b in (0,1)]
    base_metrics,base_code=_shape_metrics(x,e,fs,base_width,summed_windows)
    baseline_mean=np.mean(np.abs(x)**2,axis=1)
    candidates=[]
    for ratio in ratios:
        spacing=fine*ratio
        width=2*spacing+1.25*fine
        if width >= fs or fs < 4*fine or 1.1*width >= fs:
            raise ValueError(f'candidate ratio {ratio:g} violates native sample-rate geometry guards')
        y=(np.asarray(x) if spacing == base else reconstruct(c,fs,m['first_sample'],m['calendar_origin_sample'],spacing))
        bit_metrics,code=(base_metrics,base_code) if spacing == base else _shape_metrics(y,e,fs,width,summed_windows)
        notch_windows=beat_nulls_in_window(spacing)
        candidates.append(dict(spacing_ratio=float(ratio),spacing_hz=spacing,
            rectangular_beat_nulls_80_120ms=notch_windows,
            nominal_triplet_width_hz=width, nominal_width_formula='2*spacing + 1.25*fine_rate',
            relative_energy_change_db=(10*np.log10(np.mean(abs(y)**2,axis=1)/baseline_mean)).tolist(),
            bits=bit_metrics,codeword=code))
    manifest_bytes=(Path(directory)/'manifest.json').read_bytes()
    return dict(schema='triad-spacing-diagnostic-v1',
        source=dict(path=str(Path(directory)),manifest_sha256=hashlib.sha256(manifest_bytes).hexdigest(),
            diagnostic_source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),seed=m.get('seed'),pattern_revision=m.get('format',{}).get('pattern_revision'),
            rows=m['rows'],sample_rate_hz=fs,fine_rate_hz=fine,baseline_spacing_hz=base,
            baseline_nominal_width_hz=base_width,first_sample=m['first_sample'],calendar_origin_sample=m['calendar_origin_sample'],
            shaping_policy=m.get('shaping_policy'),pulse_shape=m.get('format',{}).get('pulse_shape'),
            envelope_scope=m.get('envelope_scope')),
        scope='SOURCE-ONLY. Candidate spacing changes only carrier oscillators; emitted shaped envelopes and coefficients are held fixed. No channel, receiver, radio AGC, sensitivity, or operational reception claim; overlapping-window extrema are not confidence bounds.',
        baseline_reconstruction_relative_error=err,
        baseline=dict(bits=base_metrics,codeword=base_code),candidates=candidates,
        interpretation=dict(window_extrema='Overlapping windows are descriptive for this finite key/fixture, not independent samples or confidence bounds.',
            known_envelope_correction='Moving-power metrics divide only by the known mean squared transmit envelope for measurement; the transmitted waveform is unmodified. One-pole metrics filter that envelope and remove only its known gate response.',
            frequency_notch='Spacing can make a rectangular window commensurate with a beat frequency. Inspect the 80–120 ms neighborhood and one-pole measures; do not select from one window alone.',
            summed_carrier_power='This is invariant to carrier spacing and isolates pulse-shaped per-carrier variation from cross-carrier beating.'))


def validate_ratios(values):
    values=[float(x) for x in values]
    if not values or len(values)>MAX_RATIOS or not all(math.isfinite(x) and .25 <= x <= 8 for x in values):
        raise ValueError(f'provide 1..{MAX_RATIOS} finite ratios in [.25,8]')
    if len(set(values)) != len(values): raise ValueError('spacing ratios must be unique')
    return values


def parse_ratios(text):
    try: values=text.split(',')
    except AttributeError as exc: raise ValueError('spacing ratios must be comma-separated numbers') from exc
    return validate_ratios(values)

def campaign_work(manifests, ratio_count):
    work=sum(int(m['rows'])*(1+ratio_count) for m in manifests)
    if work > MAX_CAMPAIGN_WORK:
        raise ValueError(f'campaign work {work} exceeds bounded row budget {MAX_CAMPAIGN_WORK}')
    return work


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('probes',nargs='+',type=Path,help=f'1..{MAX_PROBES} saved native probe directories')
    p.add_argument('--spacing-ratios',default='1.25,1.3,1.375,1.5,1.6')
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if len(a.probes)>MAX_PROBES:p.error(f'at most {MAX_PROBES} probes')
    if a.output.exists():p.error('use a new output path')
    try:
        ratios=parse_ratios(a.spacing_ratios)
        manifests=[]
        for path in a.probes:
            m=json.loads((path/'manifest.json').read_text())
            validate_manifest(m,path)
            manifests.append(m)
        work=campaign_work(manifests,len(ratios))
        result=dict(schema='triad-spacing-campaign-v1',ratios=ratios,estimated_rows=work,
                    cases=[analyze(path,ratios) for path in a.probes])
        a.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    except (OSError,ValueError,KeyError,TypeError,json.JSONDecodeError) as exc:
        p.error(str(exc))

if __name__=='__main__':main()
