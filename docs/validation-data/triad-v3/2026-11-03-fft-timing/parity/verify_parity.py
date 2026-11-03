#!/usr/bin/env python3
"""Independent direct raw-sample and full-lattice likelihood parity checks."""
import hashlib,json,sys,pathlib
import numpy as np
ROOT=pathlib.Path(__file__).resolve().parents[3]
HERE=pathlib.Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'tools'))
from triad_fft_acquire import SampledWorkspace,FFTWorkspace
CASES=[('flat-fractional-ray',36),('hf-Each65-FHSS-burst-rate1200',1200),
       ('hf-Each65-FHSS-burst-rate1200-wait40-wait40',1200)]
def sha(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for b in iter(lambda:f.read(1<<20),b''):h.update(b)
 return h.hexdigest()
def main():
 reports=[]
 for name,rate in CASES:
  d=HERE/name; base=SampledWorkspace(d/'sampled-base'); n=base.samples
  lags=np.arange(9,dtype=np.int64)
  fftw=FFTWorkspace(base,lags,precision='fp64',device='cpu',memory_limit_bytes=512*1024*1024)
  Gfft=fftw.gram; rfft=fftw.correlations()
  direct=json.loads((d/'sampled-direct'/'manifest.json').read_text())
  cand=direct['candidates']; basis=np.memmap(d/'sampled-direct'/'basis.c128',dtype='<c16',mode='r',shape=tuple(direct['basis_shape']))
  y=np.memmap(d/'sampled-direct'/'observation.c128',dtype='<c16',mode='r',shape=(n,))
  mask=np.memmap(d/'sampled-direct'/'validity.u8',dtype='u1',mode='r',shape=(n,))!=0
  sample_rate=direct['sample_rate']; base_arrival=direct['candidates'][0]['arrival_seconds']
  max_g=max_r=0.; perlag=[]
  for li,lag in enumerate(lags):
   arrival=base_arrival+int(lag)/sample_rate
   ix={}
   for i,c in enumerate(cand):
    if c['arrival_seconds']==arrival and c['tap_count']==2:
     ix[c['bit']]=i
   if set(ix)!={0,1}: raise RuntimeError(f'{name}: no two-tap direct basis at lag {lag}')
   A=np.column_stack([basis[ix[bit],:,tap] for bit in (0,1) for tap in (0,1)])
   Aw=A[mask]; yw=y[mask]
   G=Aw.conj().T@Aw; r=Aw.conj().T@yw
   ge=float(np.max(np.abs(Gfft[li]-G))); re=float(np.max(np.abs(rfft[li]-r)))
   max_g=max(max_g,ge);max_r=max(max_r,re)
   perlag.append({'lag_samples':int(lag),'arrival_seconds':arrival,'max_gram_abs_error':ge,'max_correlation_abs_error':re,'valid_samples':int(mask.sum())})
  fftres=json.loads((d/'fft.json').read_text())
  grp=json.loads((d/'grouped.json').read_text())['captures'][0]
  a=np.asarray(fftres['group_bit_log_likelihoods']);b=np.asarray(grp['group_bit_log_likelihoods'])
  curves=float(np.max(np.abs(a-b)))
  bits=float(np.max(np.abs(np.asarray(fftres['bit_log_likelihoods'])-np.asarray(grp['bit_log_likelihoods']))))
  checks={'raw_basis_lag_bank':max_g<2e-7 and max_r<2e-7,'likelihood_lattice':curves<2e-8 and bits<2e-8,
   'gate_and_winner':fftres['strict']['bit']==grp['strict']['bit'] and fftres['strict']['accepted']==grp['strict']['accepted'] and fftres['relaxed']['accepted']==grp['relaxed']['accepted']}
  reports.append({'case':name,'rate_hz':rate,'sample_count':n,'lag_count':len(lags),'fft_source_sha256':fftres['receiver_source_sha256'],
   'fft_sampled_input_hashes':fftres['input_sha256'],'raw_basis_lag_bank_max_abs_error_gram':max_g,'raw_basis_lag_bank_max_abs_error_correlations':max_r,
   'per_lag':perlag,'full_lattice_max_abs_error_group_bit_loglikelihood':curves,'full_lattice_max_abs_error_bit_loglikelihood':bits,
   'fft_decision':{k:fftres[k] for k in ('strict','relaxed')},'direct_decision':{k:grp[k] for k in ('strict','relaxed')},'checks':checks})
 if not all(all(x['checks'].values()) for x in reports): raise SystemExit(json.dumps(reports,indent=2))
 out={'schema':'triad-fft-native-sample-parity-v1','binary_sha256':'9cdf82f396d9be9a7d11b66b45b254e5f18b3857f27b08aed58430dce6d8ea47','method':'FFT full bank compared to independently exported native direct-arrival templates, explicit raw masked complex128 dot products per lag; actual noisy output compared against native export-group-stats receiver with same 9 hypotheses, one whole-observation block and likelihood calibration','checks':reports,'status':'passed'}
 (HERE/'parity-result.json').write_text(json.dumps(out,indent=2)+'\n')
 print(json.dumps({'status':out['status'],'cases':len(reports),'max_gram_error':max(x['raw_basis_lag_bank_max_abs_error_gram'] for x in reports),'max_correlation_error':max(x['raw_basis_lag_bank_max_abs_error_correlations'] for x in reports),'max_curve_difference':max(x['full_lattice_max_abs_error_group_bit_loglikelihood'] for x in reports)}))
if __name__=='__main__':main()
