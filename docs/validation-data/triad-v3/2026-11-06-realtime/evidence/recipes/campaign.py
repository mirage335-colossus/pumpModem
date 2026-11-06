#!/usr/bin/env python3
"""Focused frozen-procedure native campaign; never use evaluator truth in acquisition."""
import os
os.environ.update(OPENBLAS_NUM_THREADS='1',OMP_NUM_THREADS='1',MKL_NUM_THREADS='1')
import argparse,concurrent.futures,copy,hashlib,json,multiprocessing,subprocess,time,random
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[2]
OUT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'tools'))
import numpy as np
import torch
from triad_dynamic_joint import load_public
from triad_realtime_receiver import RealtimeReceiver,freeze_gate,admit
from scipy.stats import beta
BASE=ROOT/'local-evidence/triad-cpu-ceiling/capture'
NAMES=['triad_realtime_receiver.py','triad_realtime_helper.py','triad_realtime_fft.py','triad_dynamic_joint.py','triad_dynamic_frequency.py','triad_dynamic_scan.py','triad_dynamic_variants.py','triad_helper_receiver.py','triad_gaussian_prepared.py','triad_gaussian_state.py']
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def sources():return {n:sha(ROOT/'tools'/n)for n in NAMES}
def save(path,value):Path(path).write_text(json.dumps(value,indent=2,allow_nan=False)+'\n')
def protocol():
 rows=[]
 randomizer=random.Random(2026110600)
 for kind,count,start in [('calibration',19,2026110700),('signal',16,2026110800),('null',16,2026110900),('noise',4,2026111000),('stress8',4,2026111100),('ray05',2,2026111200)]:
  for i in range(count):
   r=dict(name=f'{kind}-{i:02d}',kind=kind,seed=start+i,bit=i%2,epoch_offset=randomizer.randint(-6,6),ray_width=.5 if kind=='ray05' else 0.,cn0=8. if kind=='stress8' else None)
   if kind=='signal' and i<4:r['arrival']=(-7.035,7.035,-7.034,7.034)[i]
   rows.append(r)
 return dict(schema='triad-realtime-qualification-v1',rows=rows,sources=sources(),native_binary_sha256=sha(ROOT/'build/dev-cli/triad_helper_export'),template_sha256=sha(BASE/'templates.c128'),reference_manifest_sha256=sha(BASE/'manifest.json'),alpha=.05,scope='Per-capture matched Helper-present null rank gate; stress/noise laws reported separately; no daily false-admission guarantee',workers=4,score_threads=1,fft_workers=2,generated_at=time.time())
CACHE={}
def init_worker(frozen):
 global FROZEN
 FROZEN=frozen
 torch.set_num_threads(1);torch.set_num_interop_threads(1);torch.set_float32_matmul_precision('highest')
 assert sources()==frozen['sources'],'source changed before worker init'
def run(row):
 assert sources()==FROZEN['sources'],'source changed before trial'
 directory=OUT/'captures'/row['name'];directory.parent.mkdir(parents=True,exist_ok=True)
 args=[str(ROOT/'build/dev-cli/triad_helper_export'),'--case','hf','--base-oscillator','crystal','--shift-oscillator','ic-7100','--channel-evolution','stochastic','--random-doppler-width-hz',str(row['ray_width']),'--sample-rate','6000','--key-seed','2417','--epoch',str(1793850000+row['epoch_offset']),'--seed',str(row['seed']),'--bit',str(row['bit']),'--output',str(directory)]
 if row['kind'] in ('calibration','null','noise'):args+=['--data','off']
 if row['kind']=='noise':args+=['--helper','off']
 if 'arrival'in row:args+=['--arrival-seconds',str(row['arrival'])]
 if row['cn0']is not None:args+=['--cn0',str(row['cn0'])]
 start=time.perf_counter()
 if not (directory/'manifest.json').exists():
  proc=subprocess.run(args,cwd=ROOT,capture_output=True,text=True,timeout=60)
  if proc.returncode:raise RuntimeError(row['name']+': '+proc.stderr)
 generation_wall=time.perf_counter()-start
 m,templates,epochs,_,_=load_public(BASE)
 cm,_,_,raw,valid=load_public(directory,BASE)
 regime=(row['cn0'],row['ray_width'])
 # The receiver always expects Helper, searches the original epoch/prior and
 # never gets DATA-present labels. Stress noise/channel metadata is public.
 if row['cn0'] is not None:
  m['power']=copy.deepcopy(cm['power']);m['power']['reference_epoch_seconds']=1793850000
 if row['ray_width']:
  m['channel']=copy.deepcopy(cm['channel'])
 if regime not in CACHE:CACHE[regime]=RealtimeReceiver(m,templates,epochs)
 receiver=CACHE[regime]
 result=receiver.score(raw,valid)
 output=OUT/'rows'/(row['name']+'.json');output.parent.mkdir(exist_ok=True)
 save(output,result) # Freeze ALL receiver output before opening truth.
 outcome=dict(trial=row,result_sha256=sha(output),procedure_id=result['procedure_id'],status=result['status'],reason=result['reason'],bit=result['bit'],score=result['score'],margin=result['margin'],timing=result['timing'],generation_wall_seconds=generation_wall,command=args,artifact_sha256={p.name:sha(p)for p in directory.iterdir()if p.is_file()},native_noise_variance=cm['power']['complex_noise_variance'],receiver_noise_variance=m['power']['complex_noise_variance'],receiver_policy=receiver.policy)
 truth=json.loads((directory/cm['evaluator_truth_file']).read_text())
 outcome['evaluator_truth']=truth
 outcome['epoch_retained']=any(c['epoch']==row['epoch_offset']for c in result.get('coarse',{}).get('candidates',[])if c['bit']==row['bit']) if row['kind'] in ('signal','stress8','ray05') else None
 save(OUT/'rows'/(row['name']+'-evaluation.json'),outcome)
 assert sources()==FROZEN['sources'],'source changed during trial'
 return outcome

def summarize(frozen,outcomes):
 by_name={r['trial']['name']:r for r in outcomes}
 cal=[json.loads((OUT/'rows'/(r['name']+'.json')).read_text())for r in frozen['rows']if r['kind']=='calibration']
 gate=freeze_gate(cal,alpha=frozen['alpha']);save(OUT/'gate.json',gate)
 groups={}
 for row in frozen['rows']:
  o=by_name[row['name']];r=json.loads((OUT/'rows'/(row['name']+'.json')).read_text());kind=row['kind']
  g=groups.setdefault(kind,dict(total=0,scored=0,unresolved=0,raw_correct=0,raw_wrong=0,correct_admissions=0,wrong_admissions=0,misses=0,false_admissions=0))
  g['total']+=1;g['scored']+=r['status']=='scored';g['unresolved']+=r['status']!='scored'
  if kind in ('signal','stress8','ray05') and r['status']=='scored':g['raw_correct'if r['bit']==row['bit']else'raw_wrong']+=1
  if kind in ('signal','null','noise'):
   decision=admit(r,gate);o['decision']=decision
   if kind=='signal':
    g['correct_admissions'if decision['accepted']and r['bit']==row['bit']else'wrong_admissions'if decision['accepted']else'misses']+=1
   else:g['false_admissions']+=decision['accepted']
  save(OUT/'rows'/(row['name']+'-evaluation.json'),o)
 for kind,g in groups.items():
  n=g['total']
  if kind=='signal':
   k=g['correct_admissions'];g['binomial_reference_95pct_CP_not_boundary_population_guarantee']=[0. if k==0 else float(beta.ppf(.025,k,n-k+1)),1. if k==n else float(beta.ppf(.975,k+1,n-k))]
  if kind in ('null','noise'):
   k=g['false_admissions'];g['false_probability_one_sided95pct_upper']=1. if k==n else float(beta.ppf(.95,k+1,n-k))
 return dict(groups=groups,gate=gate,scope=frozen['scope'],source_sha256=frozen['sources'])

def main():
 p=argparse.ArgumentParser();p.add_argument('--prepare',action='store_true');p.add_argument('--run',action='store_true');args=p.parse_args()
 file=OUT/'protocol.json'
 if args.prepare:
  if file.exists():raise RuntimeError('refusing to replace a frozen protocol')
  save(file,protocol());print('Prepared',file);return
 frozen=json.loads(file.read_text())
 assert sources()==frozen['sources'],'source changed after protocol freeze'
 if not args.run:return
 start=time.perf_counter();outcomes=[]
 # The procedure is frozen now. Calibration and holdout trials are disjoint;
 # gates are fitted before any holdout outcomes are evaluated for acceptance.
 with concurrent.futures.ProcessPoolExecutor(max_workers=frozen['workers'],mp_context=multiprocessing.get_context('spawn'),initializer=init_worker,initargs=(frozen,))as pool:
  for group in ('calibration','holdout'):
   selected=[r for r in frozen['rows']if (r['kind']=='calibration')==(group=='calibration')]
   for o in pool.map(run,selected,chunksize=1):
    outcomes.append(o);print(o['trial']['name'],o['status'],o['score'],flush=True)
   if group=='calibration':
    cal=[json.loads((OUT/'rows'/(r['name']+'.json')).read_text())for r in selected]
    save(OUT/'gate.json',freeze_gate(cal,alpha=frozen['alpha']))
 summary=summarize(frozen,outcomes);summary['campaign_wall_seconds']=time.perf_counter()-start
 save(OUT/'summary.json',summary);print(json.dumps(summary['groups'],indent=2),flush=True)
if __name__=='__main__':main()
