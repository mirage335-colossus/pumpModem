#!/usr/bin/env python3
"""Bounded paired actual IncrementalReceiver check; flat source-window only."""
import hashlib
import json
import math
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
import numpy as np
import torch
from scipy.special import logsumexp
from triad_incremental_receiver import IncrementalReceiver
from triad_operational_admission import operational_threshold
from triad_clock_diagnostic import binomial_result

torch.set_num_threads(1)
torch.set_num_interop_threads(1)
BASE = Path(__file__).resolve().parent
OUTPUT = BASE/'incremental-validation.json'
TRIALS = 16
EN0_DB = 14.
BLOCK = 4096
SEED = 85001

def identity(path):
    path = Path(path)
    return {'path':str(path.relative_to(ROOT)), 'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}

def load(name):
    directory = BASE/name
    manifest = json.loads((directory/'manifest.json').read_text())
    data = np.memmap(directory/'composite.c128', dtype='<c16', mode='r', shape=(2,manifest['rows']))
    return manifest, data, [identity(directory/'manifest.json'), identity(directory/'composite.c128')]

def score(values, basis, fs, precision):
    def basis_block(first, stop):
        out = np.zeros((3,1,1,stop-first), dtype=np.complex128)
        out[1:,0,0] = basis[:,first:stop]
        return out
    receiver = IncrementalReceiver(sample_rate=fs, samples=values.size, block_samples=BLOCK,
        basis=basis_block, required_bands=((-fs*.49,fs*.49),), noise_variance=1.,
        stationary_power=(1.,), coherence_seconds=(math.inf,), precision=precision,
        candidate_count=1, projection_mode='valid-rows', memory_limit_bytes=32*1024**2)
    for first in range(0,values.size,3001):
        receiver.push(first,values[first:first+3001])
    result = receiver.finish()
    scores = np.asarray(result['bit_scores'], dtype=float).reshape(2)
    return dict(bit_scores=scores.tolist(), log_e=float(logsumexp(scores)-math.log(2)),
        candidate_bit=int(np.argmax(scores)), complete_window=bool(result['complete_observation']),
        blocks=result['blocks'], processed_rows=result['processed_rows'],
        push_cpu_seconds=result['push_process_cpu_seconds'],
        workspace_bytes=result['working_bytes_estimate'])

def main():
    if OUTPUT.exists():
        raise RuntimeError('output exists; do not overwrite retained evidence')
    wall, cpu = time.perf_counter(), time.process_time()
    policy = operational_threshold(alpha_per_day=1e-4)
    output = dict(schema='triad-clock-incremental-validation-v1', trials=TRIALS,
        setup_failure='Initial wrapper import failed because repository ROOT used parents[3]; fixed to parents[2]. No receiver computations occurred before this correction.',
        nominal_en0_db=EN0_DB, seed=SEED, threads=1, block_samples=BLOCK,
        arithmetic='FP32 bulk, maintained sensitive reductions; selected FP64 comparison',
        policy=policy, fixtures=[],
        source_identities=[identity(ROOT/'tools'/name) for name in
            ('triad_incremental_receiver.py','triad_gaussian_state.py','triad_dynamic_joint.py','triad_operational_admission.py')],
        script_identity=identity(__file__),
        scope='Actual maintained IncrementalReceiver on full 1,000,000-second sampled payload window; complete pulse halo is excluded. Flat AWGN, constant true complex gain1, known noise variance, no oscillator/channel/spread/selectivity/Helper effects. Fixed nominal bank receives no TX correction trace. Oracle basis is a separate diagnostic. No physical or continuous operational qualification.',
        physical_time='Unchanged input sample indices and elapsed block centers for every comparison.',
        amplitude_policy='One fixed amplitude per fixture from mean nominal codeword energy; warped signal is not renormalized.')
    for fixture_index,(nominal_name,warped_name) in enumerate((
        ('low-base-74001','low-plus-74001'),('edge-base-74002','edge-minus-74002'))):
        manifest,nominal,ids = load(nominal_name)
        warped_manifest,warped,warped_ids = load(warped_name)
        assert manifest['rows']==warped_manifest['rows'] and manifest['sample_rate_hz']==warped_manifest['sample_rate_hz']
        fs=manifest['sample_rate_hz']; n=manifest['rows']
        amplitude = math.sqrt(10**(EN0_DB/10)/float(np.mean(np.sum(abs(nominal)**2,axis=1))))
        nb=np.asarray(nominal)*amplitude; wb=np.asarray(warped)*amplitude
        fixture=dict(nominal=nominal_name,warped=warped_name,sample_rate_hz=fs,
            samples=n, observation_seconds=n/fs, amplitude=amplitude,
            actual_nominal_en0_db=(10*np.log10(np.sum(abs(nb)**2,axis=1))).tolist(),
            actual_warped_en0_db=(10*np.log10(np.sum(abs(wb)**2,axis=1))).tolist(),
            input_identities=ids+warped_ids,rows=[], fp64_comparison=[])
        outcomes={name:dict(correct_admissions=0,wrong_admissions=0,misses=0,false_admissions=0)
            for name in ('baseline','warped_nominal','warped_oracle')}
        for trial in range(TRIALS):
            rng=np.random.default_rng(SEED+fixture_index*1000+trial)
            bit=trial%2
            noise=(rng.standard_normal(n)+1j*rng.standard_normal(n))/math.sqrt(2)
            rows={}
            configurations=(('baseline',nb[bit]+noise,nb),('warped_nominal',wb[bit]+noise,nb),
                            ('warped_oracle',wb[bit]+noise,wb))
            for name,values,basis in configurations:
                r=score(values,basis,fs,'fp32'); r['conditional_model_pass']=r['log_e']>=policy['log_e_threshold']
                outcome='misses' if not r['conditional_model_pass'] else 'correct_admissions' if r['candidate_bit']==bit else 'wrong_admissions'
                outcomes[name][outcome]+=1; rows[name]=r
                if trial==0:
                    reference=score(values,basis,fs,'fp64')
                    fixture['fp64_comparison'].append(dict(name=name, fp64=reference,
                        maximum_bit_score_difference=float(np.max(np.abs(np.asarray(r['bit_scores'])-reference['bit_scores'])))))
            null=score(noise,nb,fs,'fp32')
            null['conditional_model_pass']=null['log_e']>=policy['log_e_threshold']
            outcomes['baseline']['false_admissions']+=int(null['conditional_model_pass'])
            outcomes['warped_nominal']['false_admissions']+=int(null['conditional_model_pass'])
            oracle_null=score(noise,wb,fs,'fp32')
            outcomes['warped_oracle']['false_admissions']+=int(oracle_null['log_e']>=policy['log_e_threshold'])
            fixture['rows'].append(dict(trial=trial,seed=SEED+fixture_index*1000+trial,bit=bit,
                results=rows,noise_nominal=null,noise_oracle=oracle_null))
            if time.process_time()-cpu>110:
                raise RuntimeError('bounded CPU work exceeded; incomplete results not promoted')
        fixture['outcomes']={name:{key:binomial_result(count,TRIALS) for key,count in counts.items()}
                             for name,counts in outcomes.items()}
        fixture['maximum_warped_minus_baseline_log_e_difference']=max(abs(row['results']['warped_nominal']['log_e']-row['results']['baseline']['log_e']) for row in fixture['rows'])
        fixture['maximum_warped_minus_oracle_log_e_difference']=max(abs(row['results']['warped_nominal']['log_e']-row['results']['warped_oracle']['log_e']) for row in fixture['rows'])
        output['fixtures'].append(fixture)
    output.update(wall_seconds=time.perf_counter()-wall,process_cpu_seconds=time.process_time()-cpu)
    OUTPUT.write_text(json.dumps(output,indent=2,allow_nan=False)+'\n')
    print(json.dumps({key:output[key] for key in ('wall_seconds','process_cpu_seconds')}))
    for f in output['fixtures']:
        print(json.dumps({'fixture':f['nominal'],'outcomes':{name:{k:v['count'] for k,v in counts.items()} for name,counts in f['outcomes'].items()},'max_score_difference':f['maximum_warped_minus_baseline_log_e_difference']}))

if __name__=='__main__':main()
