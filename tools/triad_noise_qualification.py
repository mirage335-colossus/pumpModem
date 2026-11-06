#!/usr/bin/env python3
"""Short digital/cable qualification of fresh AES background noise, not a receiver campaign.

Every generation invokes the native tool anew. Captures contain no stored keys.
Digital flatness means the represented Nyquist band; analog filtering is measured,
not compensated. --live additionally opens the explicitly named raw ALSA route.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

import numpy as np
from scipy import signal


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def spectrum(x, fs):
    return signal.welch(x, fs=fs, nperseg=4096, noverlap=2048, detrend=False)


def digital(tool, root):
    rows = []
    cases = [("bits-0-float", ["--bits", "0"], 1/3, "f32", 48000, 0.)]
    for bits in (4, 10, 15, 16):
        cases.append((f"bits-{bits}", ["--bits", str(bits)], 2.**(-2*bits)/3, "s16", 48000, .1))
    cases += [("obscured-6", ["--obscured-bits", "6"], 2.**(-20)/3, "s16", 48000, .1),
              ("db-90", ["--db", "90"], .5e-9, "s16", 48000, .1)]
    for watts in (4., 40.):
        cases.append((f"mw-of-{watts:g}w", ["--noise-watts", ".001", "--reference-watts", str(watts),
                     "--reference-rms", ".1"], .01*.001/watts, "s16", 48000, .1))
    for fs in (12000, 96000):
        cases.append((f"rate-{fs}", ["--bits", "10"], 2.**(-20)/3, "s16", fs, .1))
    for name, flags, expected, fmt, fs, amplitude in cases:
        n = fs*8
        source = (amplitude*np.sin(2*np.pi*1737*np.arange(n)/fs)).astype('<f4')
        input_path, output_path = root/(name+'.f32'), root/(name+'.'+fmt)
        if output_path == input_path:
            output_path = root/(name+'-out.'+fmt)
        source.tofile(input_path)
        metadata = root/(name+'.json')
        command = [str(tool), '--input', str(input_path), '--output', str(output_path),
                   '--format', 'f32', '--output-format', fmt, '--sample-rate', str(fs),
                   '--metadata', str(metadata), *flags]
        start = time.perf_counter()
        result = subprocess.run(command, capture_output=True, text=True, check=True)
        wall = time.perf_counter()-start
        if fmt == 's16':
            baseline = np.trunc(source*np.float32(32767)).astype(np.int16).astype(float)/32767
            actual = np.fromfile(output_path, '<i2').astype(float)/32767
        else:
            baseline, actual = source, np.fromfile(output_path, '<f4').astype(float)
        added = actual-baseline
        f, psd = spectrum(added, fs)
        bands = [float(np.mean(psd[(f >= lo*fs/2) & (f < hi*fs/2)]))
                 for lo, hi in zip(np.linspace(.01, .99, 17)[:-1], np.linspace(.01, .99, 17)[1:])]
        variance = float(np.mean(added**2))
        error_db = float(10*np.log10(variance/expected))
        flat_db = (10*np.log10(np.asarray(bands)/(2*expected/fs))).tolist()
        lag_one = float(np.dot(added[1:], added[:-1])/np.dot(added, added))
        rows.append(dict(name=name, expected_variance=expected, measured_variance=variance,
            error_db=error_db, full_nyquist_band_errors_db=flat_db, lag_one_correlation=lag_one,
            sample_rate=fs, samples=n, wall_seconds=wall, seconds_per_analog_second=wall/8,
            output_sha256=sha(output_path), command=command, native_metadata=json.loads(metadata.read_text()),
            passed=abs(error_db)<.15 and max(abs(x) for x in flat_db)<.65 and abs(lag_one)<.015))
    return rows


def live(root, device, tool):
    from triad_live_alsa import parser, run
    fs, seconds = 48000, 6
    x=(.05*np.sin(2*np.pi*1737*np.arange(fs*seconds)/fs)).astype('<f4')
    path=root/'cable-source.f32'; x.tofile(path)
    captures={}
    for name, flags, expected in [('off', [], 0.), ('db40', ['--background-noise-db', '40'], .5e-4),
                                  ('db60', ['--background-noise-db', '60'], .5e-6),
                                  ('bits10', ['--background-noise-bits', '10'], 2.**(-20)/3)]:
        output=root/(name+'-capture.f32'); metadata=root/(name+'-capture.json')
        args=['--live','--exclusive','--input',str(path),'--output',str(output),'--metadata',str(metadata),
              '--rate',str(fs),'--capture-device',device,'--playback-device',device,'--max-seconds','30',
              '--background-noise-tool',str(tool),*flags]
        result=run(parser().parse_args(args))
        if result['status'] != 'complete':
            raise RuntimeError(f"cable capture {name} incomplete: {result.get('failure')}")
        # Select a central four seconds. Recording begins before the TX; native
        # preroll and sub-second process scheduling leave a conservative interior.
        y=np.fromfile(output,'<f4').astype(float)[2*fs:6*fs]
        f,p=spectrum(y,fs)
        # The known tone measures actual current passband gain independently of
        # the added noise. Integrate its Welch main lobe; finite noise negligible.
        tone=(f>1690)&(f<1785)
        gain=float(np.sqrt(np.sum(p[tone])*(f[1]-f[0])/(.05**2/2)))
        captures[name]=dict(expected_variance=expected, frequency=f, psd=p, gain=gain,
                            output_sha256=sha(output), metadata=json.loads(metadata.read_text()))
    off=captures['off']; rows=[]
    for name,c in captures.items():
        if name=='off':continue
        f=c['frequency']; added=c['psd']-off['psd']
        band=(f>1400)&(f<2100)&~((f>1650)&(f<1820))
        measured=float(np.mean(added[band])); predicted=2*c['expected_variance']/fs*off['gain']**2
        ratios=[]
        for lo,hi in ((100,500),(500,1400),(2100,6000),(6000,12000),(12000,20000),(20000,23500)):
            mask=(f>=lo)&(f<hi)
            ratios.append(dict(band_hz=[lo,hi], added_psd=float(np.mean(added[mask]))))
        error_db=float(10*np.log10(measured/predicted))
        rows.append(dict(name=name,expected_dac_variance=c['expected_variance'],
            measured_added_passband_psd=measured,predicted_added_passband_psd=predicted,
            passband_error_db=error_db, passed=abs(error_db)<=.5,
            tone_gain=off['gain'],capture_tone_gain=c['gain'], other_bands=ratios,
            output_sha256=c['output_sha256'], metadata=c['metadata']))
    return dict(rows=rows, baseline_gain=off['gain'], acceptance_error_db=.5,
        limitation='Six-second cable observations; passband prediction uses same-session baseline tone gain. '
                   'Other bands retain analog response; this does not measure RF output watts or prove physical whiteness past analog filters.')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tool',type=Path,default=Path('build/dev-cli/background_noise'))
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--live',action='store_true');p.add_argument('--device',default='hw:1,0')
    p.add_argument('--live-only',action='store_true')
    a=p.parse_args()
    if a.live_only and not a.live:p.error('--live-only requires --live')
    a.output.mkdir(parents=True,exist_ok=False)
    report=dict(schema='triad-background-noise-qualification-v1',source_sha256=sha(__file__),
                tool_sha256=sha(a.tool),digital=[],live=None)
    if not a.live_only:report['digital']=digital(a.tool.resolve(),a.output)
    if a.live:report['live']=live(a.output,a.device,a.tool.resolve())
    (a.output/'report.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    print(json.dumps(dict(digital_cases=len(report['digital']),
        digital_passed=sum(r['passed'] for r in report['digital']),
        cable_errors_db=[r['passband_error_db'] for r in report['live']['rows']] if report['live'] else [])))
    return 0 if (all(r['passed'] for r in report['digital']) and
                 (report['live'] is None or all(r['passed'] for r in report['live']['rows']))) else 1


if __name__=='__main__':raise SystemExit(main())
