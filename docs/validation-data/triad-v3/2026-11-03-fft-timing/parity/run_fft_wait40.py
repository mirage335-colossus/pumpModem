"""Run the two receiver paths on the frozen 40-second burst fixture."""
import hashlib,json,os,pathlib,subprocess,time
HERE=pathlib.Path(__file__).resolve().parent
ROOT=HERE.parents[2]
PY=pathlib.Path('/tmp/triad-weak-env/bin/python')
TOOL=ROOT/'tools/triad_fft_acquire.py'
SOURCE_SHA='415d624bbd44dd593dc29bebc2f511d4b3c4df8f716d50256e1f21d89768d77e'
NAME='hf-Each65-FHSS-burst-rate1200-wait40-wait40'
D=HERE/NAME

def sha(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for b in iter(lambda:f.read(1<<20),b''): h.update(b)
 return h.hexdigest()
def run(label,argv):
 log=HERE/(label+'.log'); start=time.monotonic()
 with log.open('xb') as out:
  p=subprocess.run(argv,cwd=ROOT,stdout=out,stderr=subprocess.STDOUT,timeout=60,check=False,
   env={**os.environ,'OMP_NUM_THREADS':'1','OPENBLAS_NUM_THREADS':'1','MKL_NUM_THREADS':'1','NUMEXPR_NUM_THREADS':'1'})
 receipt={'label':label,'argv':list(map(str,argv)),'status':'completed' if p.returncode==0 else 'failed','exit_code':p.returncode,'elapsed_seconds':time.monotonic()-start,'log_sha256':sha(log),'fft_source_sha256':sha(TOOL)}
 (HERE/(label+'.status.json')).write_text(json.dumps(receipt,indent=2)+'\n')
 if receipt['fft_source_sha256']!=SOURCE_SHA: raise RuntimeError('FFT source hash differs from the frozen repair')
 if p.returncode: raise RuntimeError(f'{label} failed; see {log}')
 return receipt

def main():
 if sha(TOOL)!=SOURCE_SHA: raise SystemExit('FFT source differs from the approved source identity')
 sampled=D/'sampled-base'; grouped=D/'direct-grouped'
 r1=run(NAME+'-fft-repair',[str(PY),str(TOOL),str(sampled),'--lag-max-seconds','0.0078125','--lag-step-samples','1','--frequency-offsets-hz','0','--drift-offsets-hz-per-second','0','--group-tile','9','--memory-mib','512','--device','cpu','--precision','fp64','--attempts-per-day','1440','--duration-alternatives','8','--selection-methods','16','--evaluate-simulation','--output',str(D/'fft.json')])
 r2=run(NAME+'-grouped-repair',[str(PY),str(ROOT/'tools/triad_group_acquire.py'),str(grouped),'--doppler-width-hz','0','--window-blocks','1','--reblock','1','--frequency-offsets-hz','0','--tap-powers','0.5','0.5','--group-tile','9','--memory-mib','512','--device','cpu','--precision','fp64','--threads','1','--attempts-per-day','1440','--duration-alternatives','8','--selection-methods','16','--calibration-segments','2','--evaluate-simulation','--output',str(D/'grouped.json'),'--scores-output',str(D/'group-scores.npz')])
 (D/'receiver-run-receipt.json').write_text(json.dumps({'schema':'triad-fft-native-run-v1','source_sha256':SOURCE_SHA,'runs':[r1,r2]},indent=2)+'\n')
 print(json.dumps({'status':'completed','source_sha256':SOURCE_SHA,'fft_seconds':r1['elapsed_seconds'],'grouped_seconds':r2['elapsed_seconds']}))
if __name__=='__main__':main()
