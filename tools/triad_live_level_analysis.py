#!/usr/bin/env python3
"""Compare repeated cable observations with a frozen forward audio model.

Never opens hardware. Public markers are the only alignment information.
Repeated identical inputs separate repeatable model error from repeat variation;
neither the latter nor Welch bins are independent hardware populations. This is
waveform/noise diagnosis, not a calibrated admission or sensitivity bound.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import time

import numpy as np
from scipy import signal

import triad_live_margins as campaign
import triad_live_model as model

FS = campaign.FS


def repeat_input_identities(paths):
    if len(paths)!=2 or Path(paths[0]).resolve()==Path(paths[1]).resolve():
        raise ValueError('distinct physical repeat captures required')
    hashes=[campaign.digest(p) for p in paths]
    if hashes[0]==hashes[1]:
        raise ValueError('byte-identical captures are not independent physical repeats')
    return hashes


def repeated_metrics(actual, predicted, valid):
    """Two independent repeat errors: bias cross-power and innovation power.

Negative cross-power estimates are retained, not clipped into a bias claim.
The estimates require independent zero-mean repeat innovations; two passes do
not establish that assumption. Mean energy is over the same fixed valid mask.
    """
    actual = np.asarray(actual)
    predicted = np.asarray(predicted)
    valid = np.asarray(valid, bool)
    if actual.shape != (2, len(predicted)) or valid.shape != predicted.shape or not valid.any():
        raise ValueError("two same-length repeats and a nonempty fixed mask required")
    if not np.isfinite(actual).all() or not np.isfinite(predicted).all():
        raise ValueError("finite samples required")
    error = actual[:, valid] - predicted[valid]
    p = float(np.mean(abs(predicted[valid])**2))
    noise = float(np.mean(abs(error[0]-error[1])**2)/2)
    cross = float(np.mean((error[0]*error[1].conj()).real))
    energies = [float(np.mean(abs(a[valid])**2)) for a in actual]
    gains = [np.vdot(predicted[valid],a[valid])/np.sum(abs(predicted[valid])**2)
             for a in actual] if p>0 else []
    return dict(predicted_deterministic_power=p, actual_power=energies,
        residual_power=[float(np.mean(abs(e)**2)) for e in error],
        repeat_difference_power_over_two=noise, residual_cross_power=cross,
        repeat_mean_residual_power=float(np.mean(abs(error.mean(axis=0))**2)),
        energy_ratio_db=[10*math.log10(v/p) if v>0 and p>0 else None for v in energies],
        evaluation_only_complex_projection=[dict(real=float(g.real),imag=float(g.imag),
            magnitude_db=float(20*np.log10(abs(g))) if abs(g)>0 else None,
            phase_degrees=float(np.angle(g,deg=True))) for g in gains],
        fitted_gain_applied=False,
        samples=int(valid.sum()),
        scope="moment estimates, conditional on fixed transmitted samples; independent zero-mean repeat innovations not established")


def align_records(prepared, captures, metadata, output):
    started = time.perf_counter()
    if len(captures) != 2 or len(metadata) != 2:
        raise ValueError("exactly two predeclared physical repeats are required")
    capture_hashes=repeat_input_identities(captures)
    src, out = Path(prepared), campaign.fresh(output)
    plan = campaign.read_json(src/'plan.json')
    profile = campaign.read_json(src/'profile.json')
    if campaign.digest(src/'stimulus.f32') != plan['stimulus_sha256']:
        raise ValueError('prepared input changed')
    if campaign.digest(src/'profile.json') != plan['profile_copy_sha256']:
        raise ValueError('frozen profile changed')
    fixtures_dir = Path(plan['fixtures_directory'])
    fixtures = {name: campaign.read_json(fixtures_dir/name/'fixture.json') for name in ('baseline','edge')}
    for name, f in fixtures.items():
        campaign.verify_fixture(fixtures_dir/name, f)
        if campaign.digest(fixtures_dir/name/'fixture.json') != plan['fixture_json_sha256'][name]:
            raise ValueError('fixture metadata changed')
    tx = np.memmap(src/'stimulus.f32', dtype='<f4', mode='r')
    rx = [np.memmap(p, dtype='<f4', mode='r') for p in captures]
    receipts = [campaign.read_json(p) for p in metadata]
    for samples, receipt in zip(rx, receipts):
        campaign.validate_capture(receipt, len(samples))
        if np.max(abs(samples)) >= .999:
            raise ValueError('clipped capture')
    first = plan['records'][0]
    delays = [model._marker_delay(np.asarray(tx[:first['samples']]),
        np.asarray(r[:first['samples']+2*FS]), FS,
        first['markers']['markers'][0])['delay_samples'] for r in rx]
    state = model.AudioModelState(profile)
    rows, alignment_rows = [], []
    for rec in plan['records']:
        off, n = rec['offset'], rec['samples']
        chunk = np.asarray(tx[off:off+n])
        if hashlib.sha256(chunk.tobytes()).hexdigest() != rec['stimulus_sha256']:
            raise ValueError('record input changed')
        quantized = model.quantize_production_playback(chunk)
        prediction = state.process(quantized)
        residual = campaign.fixed_noise(profile, n, rec['noise_seed']+1991000)
        physical = []
        for i, r in enumerate(rx):
            a=max(0, int(off+delays[i]-FS)); b=min(len(r),int(off+delays[i]+n+FS))
            aligned, mask, fit = model.align_capture(chunk, np.asarray(r[a:b]),FS,rec['markers']['markers'])
            delays[i]=a+fit['offset_samples']-off
            physical.append((aligned,mask))
            alignment_rows.append(dict(index=rec['index'],repeat=i,global_delay_samples=delays[i],**fit))
        names = [rec['case']] if rec['case'] else list(fixtures)
        for name in names:
            f = fixtures[name]; start=rec['markers']['payload']['start']; stop=start+f['payload_audio_samples']
            if not all(mask[start:stop].all() for _,mask in physical):
                raise ValueError('incomplete physical payload')
            actual = np.asarray([campaign.convert(a[start:stop],f)[0] for a,_ in physical])
            pred, valid, _ = campaign.convert(prediction[start:stop], f)
            valid_indices=np.flatnonzero(valid)
            if len(valid_indices)<2 or not np.all(np.diff(valid_indices)==1):
                raise ValueError('spectral diagnosis requires contiguous valid support')
            noise, _, _ = campaign.convert(residual[start:stop], f)
            metrics = repeated_metrics(actual,pred,valid)
            metrics['predicted_residual_power'] = float(np.mean(abs(noise[valid])**2))
            carrier_rows=[]
            # Three physical spectral regions, never carrier-label channel processing.
            # Fine-symbol spacing = 1.25*(rate*Added/6); carrierwidth=spacing.
            spacing=1.25*f['rate']*f['added']/6
            hz, psd=signal.welch((actual[0,valid]-actual[1,valid])/math.sqrt(2),fs=f['receiver_sample_rate'],
                nperseg=min(len(pred),round(f['receiver_sample_rate'])),return_onesided=False)
            _,model_psd=signal.welch(noise[valid],fs=f['receiver_sample_rate'],nperseg=min(len(pred),round(f['receiver_sample_rate'])),return_onesided=False)
            errors=actual[:,valid]-pred[valid]
            split=errors.shape[-1]//2
            metrics['residual_power_early_late']=[[float(np.mean(abs(e[:split])**2)),
                float(np.mean(abs(e[split:])**2))] for e in errors]
            _,error_psd=signal.welch(errors,fs=f['receiver_sample_rate'],
                nperseg=min(len(pred),round(f['receiver_sample_rate'])),return_onesided=False,axis=-1)
            _,error_cross=signal.csd(errors[0],errors[1],fs=f['receiver_sample_rate'],
                nperseg=min(len(pred),round(f['receiver_sample_rate'])),return_onesided=False)
            for center in (-spacing,0.,spacing):
                select=abs(hz-center)<=spacing/2
                carrier_rows.append(dict(offset_hz=center,width_hz=spacing,
                    repeat_noise_density=float(np.mean(psd[select])),
                    model_noise_density=float(np.mean(model_psd[select])),
                    physical_residual_density=[float(np.mean(p[select])) for p in error_psd],
                    residual_cross_density_real=float(np.mean(error_cross[select].real)),
                    bins=int(select.sum())))
            row=dict(index=rec['index'],case=name,truth=rec['truth'],kind=rec['kind'],noise_rms=rec['noise_rms'],
                gain=rec['gain'],cn0_db_hz=rec['cn0_db_hz'],metrics=metrics,carrier_noise=carrier_rows)
            # Reusable immutable linear observations; truth is metadata only.
            filename=f"record-{rec['index']:03d}-{name}.npz"
            np.savez_compressed(out/filename,physical=actual.astype(np.complex64),
                deterministic_prediction=pred.astype(np.complex64),simulated_residual=noise.astype(np.complex64),valid=valid)
            row['workspace']=filename; row['workspace_sha256']=campaign.digest(out/filename)
            rows.append(row)
        print(f"compared {rec['index']+1}/{len(plan['records'])}",flush=True)
    result=dict(schema='triad-live-repeated-level-analysis-v1',rows=rows,alignments=alignment_rows,
        plan_sha256=campaign.digest(src/'plan.json'),profile_sha256=campaign.digest(src/'profile.json'),
        capture_sha256=capture_hashes,metadata_sha256=[campaign.digest(p) for p in metadata],
        source_sha256={Path(p).name:campaign.digest(p) for p in [__file__,campaign.__file__,model.__file__]},
        wall_seconds=time.perf_counter()-started,
        scope='paired repeated physical waveform diagnosis; no operational admission or equivalent-SNR confidence claim',
        synthetic_only=any(r.get('synthetic_only',False) for r in receipts))
    campaign.write_json(out/'results.json',result)
    print(json.dumps({'wall_seconds':result['wall_seconds'],'observations':len(rows)}))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prepared',required=True);p.add_argument('--capture',action='append',required=True)
    p.add_argument('--metadata',action='append',required=True);p.add_argument('--output',required=True)
    a=p.parse_args();align_records(a.prepared,a.capture,a.metadata,a.output)


if __name__=='__main__':
    main()
