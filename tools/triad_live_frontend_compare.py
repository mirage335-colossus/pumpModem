#!/usr/bin/env python3
"""Paired old/wide and explicitly narrowed receiver on real cable captures.

Both public candidates and the full declared search are retained. Each frontend
has a distinct independently frozen model noise scale and empirical null gate.
No hardware access, payload fitting, or Gaussian physical-admission assertion.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import time

import numpy as np
import scipy
import triad_live_margins as campaign
import triad_live_model as model
import triad_live_narrowband as narrow
from triad_live_receiver import EmpiricalReceiver,empirical_decision,evaluate


def validate_experiment_receipt(plan,receipt,count):
    """This qualification covers flat baseline/edge geometry and bound PCM.

Additional Doppler/channel sidebands need a separately justified passband;
never silently use this nominal-band comparison for those configurations.
    """
    if not plan.get('cases') or set(plan['cases'])-{'baseline','edge'}:
        raise ValueError('frontend comparison is qualified only for flat baseline/edge cases')
    campaign.validate_capture(receipt,count)
    if receipt.get('input_sha256')!=plan.get('stimulus_sha256'):
        raise ValueError('capture receipt does not bind this exact transmitted stimulus')


def make_receivers(fixtures_dir, fixtures, profile):
    receivers, operators, details = {}, {}, {}
    source_ids={Path(p).name:campaign.digest(p) for p in (__file__,narrow.__file__,campaign.__file__)}
    for name,f in fixtures.items():
        basis=np.load(fixtures_dir/name/'basis.npy')
        _,valid,_=campaign.convert(np.zeros(f['payload_audio_samples']),f)
        op=narrow.NarrowbandFilter(f['receiver_sample_rate'],f['bandwidth'],precision='fp32')
        nb,nv,nm=op.filter_basis(basis,valid,max_lag=max(f['lags']))
        # Frozen independently seeded null, generated without any actual data.
        raw_noise=np.random.default_rng(912018).normal(0.,f['noise_rms'],f['payload_audio_samples']).astype(np.float32)
        predicted=model.simulate_audio_profile(raw_noise,profile,seed=912019,production_s16=True)
        nz,_,_=campaign.convert(predicted,f)
        new_reference=float(np.mean(abs(op.filter_observation(nz)[nv])**2))
        for mode,b,v,variance in [('wide',basis,valid,f['reference_variance']),('narrow',nb,nv,new_reference)]:
            identity=json.dumps(dict(fixture=f['preprocessing_identity'],frontend=mode,
                sources=source_ids,preprocessing=campaign.preprocessing_sources(),
                numpy=np.__version__,scipy=scipy.__version__),sort_keys=True)
            key=f'{name}/{mode}'
            receivers[key]=EmpiricalReceiver(b,f['receiver_sample_rate'],np.asarray(f['lags']),
                key=f['public_key'],valid=v,reference_variance=variance,
                frequency_offsets=tuple(f['frequency_offsets_hz']),precision='fp32',preprocessing_identity=identity)
            details[key]=dict(receiver_id=receivers[key].receiver_id,reference_variance=variance,
                valid_samples=int(v.sum()),frontend=nm if mode=='narrow' else 'original fixed DDC',
                interpretation='empirical working likelihood; new gate required for each frontend')
        operators[name]=op
    return receivers,operators,details


def analyze(args):
    start=time.perf_counter();src=Path(args.prepared);out=campaign.fresh(args.output)
    plan=campaign.read_json(src/'plan.json');profile=campaign.read_json(src/'profile.json')
    for path,want in [(src/'stimulus.f32',plan['stimulus_sha256']),
                      (src/'profile.json',plan['profile_copy_sha256'])]:
        if campaign.digest(path)!=want:raise ValueError('frozen input changed')
    tx=np.memmap(src/'stimulus.f32',dtype='<f4',mode='r');rx=np.memmap(args.capture,dtype='<f4',mode='r')
    receipt=campaign.read_json(args.metadata);validate_experiment_receipt(plan,receipt,len(rx))
    if np.max(abs(rx))>=.999:raise ValueError('clipped physical capture')
    fixtures_dir=Path(plan['fixtures_directory'])
    fixtures={n:campaign.read_json(fixtures_dir/n/'fixture.json') for n in plan['cases']}
    for n,f in fixtures.items():
        campaign.verify_fixture(fixtures_dir/n,f)
        if campaign.digest(fixtures_dir/n/'fixture.json')!=plan['fixture_json_sha256'][n]:
            raise ValueError('fixture metadata changed')
        if f['profile_sha256']!=plan['profile_sha256'] or f['noise_rms']!=plan['noise_rms']:
            raise ValueError('fixture profile/noise condition differs')
    receivers,operators,details=make_receivers(fixtures_dir,fixtures,profile)
    campaign.write_json(out/'receiver-specifications.json',details)
    state=model.AudioModelState(profile);rows=[];alignments=[]
    first=plan['records'][0]
    delay=model._marker_delay(np.asarray(tx[:first['samples']]),np.asarray(rx[:first['samples']+2*campaign.FS]),
        campaign.FS,first['markers']['markers'][0])['delay_samples']
    for rec in plan['records']:
        off,n=rec['offset'],rec['samples'];chunk=np.asarray(tx[off:off+n])
        if hashlib.sha256(chunk.tobytes()).hexdigest()!=rec['stimulus_sha256']:
            raise ValueError('record input changed')
        a=max(0,int(off+delay-campaign.FS));b=min(len(rx),int(off+delay+n+campaign.FS))
        aligned,mask,fit=model.align_capture(chunk,np.asarray(rx[a:b]),campaign.FS,rec['markers']['markers'])
        delay=a+fit['offset_samples']-off;alignments.append(dict(index=rec['index'],global_delay_samples=delay,**fit))
        predicted=state.process(model.quantize_production_playback(chunk))
        predicted+=campaign.fixed_noise(profile,n,rec['noise_seed']+991000)
        lo=rec['markers']['payload']['start']
        for name in ([rec['case']] if rec['case'] else plan['cases']):
            f=fixtures[name];hi=lo+f['payload_audio_samples']
            if not mask[lo:hi].all():raise ValueError('incomplete aligned physical payload')
            actual,_,_=campaign.convert(aligned[lo:hi],f);simulated,_,_=campaign.convert(predicted[lo:hi],f)
            for mode in ('wide','narrow'):
                key=f'{name}/{mode}';receiver=receivers[key]
                pair={}
                for kind,y in [('physical',actual),('simulated',simulated)]:
                    if mode=='narrow':y=operators[name].filter_observation(y)
                    score=receiver.score(y)
                    row=dict(index=rec['index'],case=name,frontend=mode,gate_key=key,kind=kind,
                        truth=rec['truth'],gain=rec['gain'],cn0_db_hz=rec['cn0_db_hz'],
                        calibration_record=rec['calibration'],**score)
                    rows.append(row);pair[kind]=row
                campaign.write_json(out/f"record-{rec['index']:03d}-{name}-{mode}.json",pair)
        print(f"scored {rec['index']+1}/{len(plan['records'])}",flush=True)
    gates=campaign.read_json(Path(args.calibration)/'gates.json') if args.calibration else {}
    for key,receiver in receivers.items():
        scores=[r['score'] for r in rows if r['gate_key']==key and r['kind']=='physical' and r['calibration_record']]
        if scores:gates[key]=receiver.freeze_calibration(scores,alpha=.05)
    campaign.write_json(out/'gates.json',gates)
    for row in rows:
        if not row['calibration_record']:
            if row['gate_key'] not in gates:raise ValueError('independent frozen calibration is required')
            row['decision']=empirical_decision(row,gates[row['gate_key']])
            row['classification']=evaluate(row['decision'],row['truth'])
    summaries=[]
    for key in receivers:
        for kind in ('physical','simulated'):
            selected=[r for r in rows if r['gate_key']==key and r['kind']==kind and not r['calibration_record']]
            levels=sorted(set(r['cn0_db_hz'] for r in selected if r['truth'] is not None))
            for level in [None,*levels]:
                subset=[r for r in selected if r['cn0_db_hz']==level]
                if not subset:continue
                counts={c:sum(r['classification']==c for r in subset) for c in
                    ('correct_admission','wrong_admission','miss','false_admission','correct_rejection')}
                summaries.append(dict(gate_key=key,kind=kind,cn0_db_hz=level,trials=len(subset),counts=counts,
                    pointwise_intervals_95={c:campaign.interval(k,len(subset)) for c,k in counts.items()}))
    result=dict(schema='triad-live-frontend-comparison-v1',rows=rows,summaries=summaries,alignments=alignments,
        source_sha256={Path(p).name:campaign.digest(p) for p in (__file__,narrow.__file__,campaign.__file__)},
        profile_sha256=campaign.digest(src/'profile.json'),plan_sha256=campaign.digest(src/'plan.json'),
        capture_sha256=campaign.digest(args.capture),metadata_sha256=campaign.digest(args.metadata),
        gates_sha256=campaign.digest(out/'gates.json'),wall_seconds=time.perf_counter()-start,
        synthetic_only=receipt.get('synthetic_only',False),
        scope='full declared bit/timing/CFO bank, marker-conditioned offline capture; separately calibrated frontends, same physical/model data; per-record empirical gate, not operational daily FAP')
    campaign.write_json(out/'results.json',result)
    print(json.dumps(dict(wall_seconds=result['wall_seconds'],summaries=summaries)))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('prepared','capture','metadata','output'):p.add_argument('--'+name,required=True)
    p.add_argument('--calibration')
    analyze(p.parse_args())


if __name__=='__main__':main()
