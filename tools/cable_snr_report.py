"""Offline segmentation/report for cable_snr_live.py captures. Never opens audio."""
import argparse
import json
from pathlib import Path

import numpy as np
from cable_snr_analysis import analyze_tone, analyze_silence, analyze_multitone


def align(capture, transmit, fs):
    sync = transmit[fs:round(1.4*fs)].astype(float)
    # Only the early capture can contain the synchronization chirp.
    early = capture[:6*fs].astype(float)
    n = 1 << (len(early)+len(sync)-1).bit_length()
    corr = np.fft.irfft(np.fft.rfft(early, n)*np.conj(np.fft.rfft(sync, n)), n)
    index = int(np.argmax(np.abs(corr[:4*fs])))
    candidate = early[index:index+len(sync)]
    coherence = float(np.dot(candidate, sync)**2/(np.dot(candidate,candidate)*np.dot(sync,sync)))
    if coherence < .5:
        raise RuntimeError(f"Synchronization chirp not reliably found: {coherence}")
    return index-fs, coherence


def analyze(path, channel="mono", capture_path=None, capture_channels=2):
    meta = json.loads((path/"metadata.json").read_text())
    fs = meta['sample_rate']
    capture_path = capture_path or path/"capture-stereo.f32"
    raw = np.fromfile(capture_path, dtype="<f4").reshape(-1,capture_channels)
    y = raw.astype(float).mean(axis=1) if channel=="mono" else raw[:, {"left":0,"right":1}[channel]].astype(float)
    x = np.fromfile(path/"transmit-mono.f32",dtype="<f4")
    offset, coherence = align(y,x,fs)
    records, psds = [], {}
    for index,s in enumerate(meta['segments']):
        if s['kind'] not in ('tone','silence','multitone'):
            continue
        duration=s['samples']/fs
        if duration < 1:
            continue
        # Discard all fades/transitions. Long tones supply two independent
        # one-second blocks as well as one two-second fit (reported separately).
        start = s['start_sample']+offset+round(.5*fs)
        end = s['start_sample']+offset+s['samples']-round(.5*fs)
        if start<0 or end>len(y) or end-start<round(.4*fs):
            continue
        windows=[('steady',start,end)]
        if s['kind'] in ('tone','multitone') and end-start>=2*fs:
            windows += [('first-second', start, start+fs),('second-second', start+fs,start+2*fs)]
        for window,a,b in windows:
            data=y[a:b]
            if s['kind']=='multitone':
                xa=a-offset;xb=b-offset
                reference=analyze_multitone(x[xa:xb],fs,meta['frequencies'])
                result=analyze_multitone(data,fs,meta['frequencies'],
                    reference_amplitudes=[t['peak_amplitude'] for t in reference['tones']],
                    reference_phases_rad=[t['phase_rad'] for t in reference['tones']])
            elif s['kind']=='tone':
                result=analyze_tone(data,fs,s['frequency_hz'])
            else:
                result=analyze_silence(data,fs)
            spectrum=result.pop('psd')
            key=f"segment{index}-{window}"
            if window=='steady':
                for name,value in spectrum.items():psds[key+'-'+name]=value
            result.update(segment_index=index,window=window,capture_start_sample=a,
                          capture_end_sample=b,tx_segment=s)
            records.append(result)
            if s['kind']=='tone' and window=='steady':
                print(f"{path.name} {channel} A={s['tx_peak_requested']:.4f} f={s['frequency_hz']:.1f} peak={result['levels']['peak']:.4f} " +
                      ' | '.join(f"{z['low_hz']:.0f}-{z['high_hz']:.0f}Hz SNR={z['snr_db']:.2f} SINAD={z['sinad_db']:.2f}" for z in result['bands']),flush=True)
            if s['kind']=='multitone' and window=='steady':
                print(f"{path.name} {channel} A={s['tx_peak_requested']:.5f} multitone " +
                      ' | '.join(f"{z['low_hz']:.0f}-{z['high_hz']:.0f}Hz ratio={z['linear_multitone_residual_ratio_db']:.2f}" for z in result['bands']),flush=True)
    label=channel if capture_channels==2 else 'production-mono'
    out=dict(capture_file=str(capture_path),channel=label,sample_rate=fs,
             alignment_offset_samples=offset,sync_squared_coherence=coherence,
             metadata=meta,measurements=records)
    (path/f"analysis-{label}.json").write_text(json.dumps(out,indent=2,allow_nan=False)+"\n")
    np.savez_compressed(path/f"spectra-{label}.npz",**psds)
    return out


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('paths',type=Path,nargs='+')
    p.add_argument('--channel',choices=['mono','left','right'],default='mono')
    p.add_argument('--capture-file',type=Path)
    p.add_argument('--capture-channels',type=int,choices=[1,2],default=2)
    a=p.parse_args()
    if a.capture_file and len(a.paths)!=1:p.error('capture override needs one stimulus directory')
    for path in a.paths:analyze(path,a.channel,a.capture_file,a.capture_channels)
