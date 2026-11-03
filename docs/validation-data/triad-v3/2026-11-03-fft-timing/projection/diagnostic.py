#!/usr/bin/env python3
"""Postdecision clean projection audit; no receiver decisions or new hypotheses."""
import argparse,hashlib,json,math,os,subprocess,time
from pathlib import Path
for name in ('OPENBLAS_NUM_THREADS','OMP_NUM_THREADS','MKL_NUM_THREADS'):os.environ[name]='1'
import numpy as np

def sha(path):
 h=hashlib.sha256()
 with Path(path).open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()

def projection(G,r):
 v,U=np.linalg.eigh((G+G.conj().T)/2);scale=max(0.,float(v[-1]));keep=v>1e-10*scale
 if v[0]<-1e-10*max(scale,np.finfo(float).tiny):raise ValueError('indefinite Gram')
 z=U[:,keep].conj().T@r
 return float(np.sum(abs(z)**2/v[keep])),int(keep.sum())

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('mode',choices=('export','analyze','sanity'))
 p.add_argument('--plan',type=Path,default=Path(__file__).with_name('plan.json'));p.add_argument('--case');p.add_argument('--binary',type=Path)
 p.add_argument('--output-root',type=Path);p.add_argument('--original-root',type=Path);p.add_argument('--output',type=Path)
 a=p.parse_args()
 if a.mode=='sanity':
  energy,rank=projection(np.eye(2),np.array([1+2j,3-1j]));np.testing.assert_allclose(energy,15.,rtol=0,atol=1e-14);assert rank==2
  assert projection(np.zeros((2,2)),np.zeros(2))==(0.,0)
  print('identity and zero sanity passed');return
 plan=json.loads(a.plan.read_text());jobs=[j for j in plan['jobs'] if a.case is None or j['id']==a.case]
 if not jobs or a.output_root is None:raise ValueError('select a planned case and output root')
 if a.mode=='export':
  if a.binary is None or sha(a.binary)!=plan['native_binary_sha256']:raise ValueError('native binary identity mismatch')
  a.output_root.mkdir(parents=True,exist_ok=True)
  for job in jobs:
   out=a.output_root/job['id']
   if out.exists():raise ValueError('fresh grouped output required')
   argv=[x.replace('{binary}',str(a.binary)).replace('{output_root}',str(a.output_root)) for x in job['argv']]
   started=time.perf_counter();r=subprocess.run(argv,capture_output=True,text=True,timeout=plan['timeout_seconds'])
   receipt=dict(argv=argv,exit_code=r.returncode,elapsed_seconds=time.perf_counter()-started,stdout=r.stdout,stderr=r.stderr)
   (a.output_root/(job['id']+'-export.json')).write_text(json.dumps(receipt,indent=2)+'\n')
   if r.returncode:raise RuntimeError(r.stderr)
   if sha(a.binary)!=plan['native_binary_sha256']:raise ValueError('native binary changed')
   print(json.dumps(dict(id=job['id'],status='exported',seconds=receipt['elapsed_seconds'])),flush=True)
  return
 if a.original_root is None or a.output is None or a.output.exists():raise ValueError('original root and fresh analysis output required')
 reports=[]
 for job in jobs:
  original=a.original_root/job['id'];new=a.output_root/job['id'];old=json.loads((original/'manifest.json').read_text());m=json.loads((new/'manifest.json').read_text())
  for f in job['original_input_files']:
   if sha(original/f['name'])!=f['sha256']:raise ValueError('original sampled file identity mismatch: '+f['name'])
  if m.get('group_count')!=1 or m.get('stats_noisy') is not True or m.get('grouped_export_execution',{}).get('channel_pass_mode')!='paired':raise ValueError('one public group and native paired clean/noisy pass required')
  keys=('sample_count','sample_rate','source_samples','requested_active_seconds','actual_elapsed_seconds','channel_samples','channel_tail_samples','key_seed','channel_seed','epoch','cn0_db_hz','control','endpoint_truth','candidates')
  for key in keys:
   if old.get(key)!=m.get(key):raise ValueError('changed physical/calendar/basis metadata: '+key)
  for key,value in old['oscillator_model'].items():
   if m['oscillator_model'].get(key)!=value:raise ValueError('oscillator law differs')
  if m['sample_count']!=job['expected_sample_count'] or m['sample_rate']!=job['expected_sample_rate']:raise ValueError('original calendar differs from plan')
  N=m['sample_count'];B=m['block_count'];D=m['dimension'];group=m['groups'][0]
  mask=np.memmap(original/'validity.u8',dtype='u1',mode='r',shape=(N,))
  if not np.all(mask==1):raise ValueError('this no-erasure diagnostic requires all original validity rows')
  intervals=np.fromfile(new/'intervals.u64',dtype='<u8').reshape(B,2);valid=np.fromfile(new/'valid_rows.u64',dtype='<u8')
  if intervals[0,0]!=0 or intervals[-1,1]!=N or np.any(intervals[1:,0]!=intervals[:-1,1]) or not np.array_equal(valid,intervals[:,1]-intervals[:,0]):raise ValueError('grouped calendar/mask differs')
  G=np.fromfile(new/'group_gram.c128',dtype='<c16').reshape(B,1,D,D).sum(axis=0)[0]
  clean=np.fromfile(new/'group_mean.c128',dtype='<c16').reshape(B,1,D).sum(axis=0)[0]
  noisy=np.fromfile(new/'group_observation.c128',dtype='<c16').reshape(B,1,D).sum(axis=0)[0]
  E=math.fsum(np.fromfile(new/'signal_energy.f64',dtype='<f8'));observed=math.fsum(np.fromfile(new/'received_energy.f64',dtype='<f8'))
  if not math.isclose(E,old['channel_signal_energy_samples'],rel_tol=2e-10):raise ValueError('clean energy changed between native passes')
  noise=m['noise_variance']
  if noise!=old['noise_variance_pre_frontend']:raise ValueError('noise variance differs')
  basis=np.memmap(original/'basis.c128',dtype='<c16',mode='r',shape=tuple(old['basis_shape']))
  y=np.memmap(original/'observation.c128',dtype='<c16',mode='r',shape=(N,));raw_energy=float(np.vdot(y,y).real)
  if not math.isclose(raw_energy,observed,rel_tol=2e-10):raise ValueError('actual noisy energy differs')
  bits=[]
  for bit in (0,1):
   ci=next(i for i,c in enumerate(old['candidates']) if c['bit']==bit and c['tap_count']==2)
   columns=group['column_mapping'][group['candidate_indices'].index(ci)];A=np.asarray(basis[ci]);gd=A.conj().T@A;rd=A.conj().T@y
   gg=G[np.ix_(columns,columns)];rn=noisy[columns];rc=clean[columns]
   gerr=float(np.max(abs(gd-gg)))/max(float(np.max(abs(gd))),np.finfo(float).tiny)
   rerr=float(np.max(abs(rd-rn)/np.sqrt(np.maximum(gd.diagonal().real*raw_energy,np.finfo(float).tiny))))
   if max(gerr,rerr)>2e-10:raise ValueError('raw original/noisy paired projection mismatch')
   pe,rank=projection(gg,rc)
   if pe>E*(1+1e-8):raise ValueError('projected energy exceeds total')
   bits.append(dict(bit=bit,projected_energy=pe,retained_fraction=pe/E,projected_rho=pe/noise,representation_loss_db=-10*math.log10(pe/E) if pe else None,rank=rank,original_vs_grouped_gram_relative_error=gerr,original_vs_grouped_noisy_correlation_normalized_error=rerr))
  reports.append(dict(id=job['id'],sample_count=N,sample_rate=m['sample_rate'],calendar_and_physical_truth_match=True,native_pass=m['grouped_export_execution'],total_clean_energy=E,total_rho=E/noise,noisy_energy_relative_error=abs(raw_energy-observed)/raw_energy,per_bit=bits,true_bit_simulation_only=job['true_bit'],original_fft_scores=job['original_bit_log_likelihoods'],original_gates=job['original_gates'],original_outcomes=job['original_outcomes'],base_public_group=group['sync_key'],grouped_input_sha256={x.name:sha(x) for x in new.iterdir() if x.is_file()},original_input_hashes=job['original_input_files']))
 result=dict(schema='triad-fft-clean-base-projection-v1',plan_sha256=sha(a.plan),diagnostic_sha256=sha(__file__),receiver_sha256=plan['source_file_sha256']['tools/triad_fft_acquire.py'],native_binary_sha256=plan['native_binary_sha256'],cases=reports,limits=['Postdecision simulation evaluation only; no receiver rescore, new candidate or pruning.','One original public base group, not the complete six-hypothesis FFT bank; small CFO and fractional timing effects are not corrected here.','Projection energy is a representation diagnostic, not an achievable sensitivity or detection probability.','Raw original noisy projections, energy, calendar and endpoint parameters are checked against the native paired pass; clean raw samples are not retained.'])
 a.output.parent.mkdir(parents=True,exist_ok=True)
 with a.output.open('x') as f:json.dump(result,f,indent=2,allow_nan=False);f.write('\n')
 print(json.dumps(dict(output=str(a.output),cases=len(reports))))
if __name__=='__main__':main()
