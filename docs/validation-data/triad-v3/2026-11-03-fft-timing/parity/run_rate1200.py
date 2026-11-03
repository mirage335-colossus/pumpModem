"""One bounded, explicitly selected rate1200 FHSS fixture after rate36 geometry rejection."""
import hashlib,json,os,pathlib,subprocess,time
HERE=pathlib.Path(__file__).resolve().parent
ROOT=HERE.parents[2]
BIN=ROOT/'build/dev-cli/triad_long'
PY=pathlib.Path('/tmp/triad-weak-env/bin/python')
DEST=HERE/'hf-Each65-FHSS-burst-rate1200'
SEED=4201
PRIOR=(-4/1024,4/1024)
def sha(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for b in iter(lambda:f.read(1<<20),b''): h.update(b)
 return h.hexdigest()
def run(name,argv):
 log=HERE/(name+'.log')
 with log.open('xb') as f:
  p=subprocess.run(argv,cwd=ROOT,stdout=f,stderr=subprocess.STDOUT,timeout=60,check=False,
   env={**os.environ,'OMP_NUM_THREADS':'1','OPENBLAS_NUM_THREADS':'1','MKL_NUM_THREADS':'1','NUMEXPR_NUM_THREADS':'1'})
 r={'name':name,'argv':list(map(str,argv)),'exit_code':p.returncode,'status':'completed' if p.returncode==0 else 'failed','elapsed_seconds':None,'log_sha256':sha(log)}
 (HERE/(name+'.status.json')).write_text(json.dumps(r,indent=2)+'\n')
 if p.returncode: raise RuntimeError(f'{name} failed; see {log}')
 return log
def main():
 if DEST.exists(): raise SystemExit(f'refusing overwrite: {DEST}')
 from runpy import run_path
 m=run_path(str(HERE/'run_parity.py'))
 args=m['common'](SEED,True); args[args.index('--rate')+1]='1200'
 cfglog=run('hf-Each65-FHSS-burst-rate1200-configure-repair',[str(BIN),'--mode','configure',*args])
 cfg=json.loads(cfglog.read_text())['configuration']
 elapsed=cfg['duration']['elapsed_source_calendar_seconds']; total=max(1,int(elapsed/.4))
 missed=sorted({0,total//2,total-1})
 args += ['--missed-dwells',','.join(map(str,missed))]
 low,high=PRIOR; step=1/1024
 entries={'name':'hf-Each65-FHSS-burst-rate1200','rate_hz':1200,'seed':SEED,'missed_dwells':missed,
  'burst_wait_tiles':3,'public_arrival_prior_seconds':[low,high],
  'exception':'rate36,72,360 FHSS rejected for no protected dwell cells; root-selected rate1200 is user-listed and passes configure',
  'control':'HF-best, Each65 at .1Hz, FHSS envelope, burst internal waits; physical two-ray .5ms/.1Hz',
  'binary_sha256':sha(BIN)}
 common=['--memory-mib','512','--chunk','4096','--threads','1','--export-max-mib','512']
 for kind,start in [('sampled-base',(low,low,1.)),('sampled-direct',(low,high,step))]:
  d=DEST/kind
  run('hf-Each65-FHSS-burst-rate1200-'+kind+'-repair',[str(BIN),'--mode','export-sampled',*args,'--start',*map(str,start),*common,'--output',str(d)])
 d=DEST/'direct-grouped'
 run('hf-Each65-FHSS-burst-rate1200-grouped-direct-repair',[str(BIN),'--mode','export-group-stats','--stats-noisy',*args,'--start',str(low),str(high),str(step),'--memory-mib','512','--chunk','4096','--block','65536','--threads','1','--export-max-mib','512','--output',str(d)])
 for d in (DEST/'sampled-base',DEST/'sampled-direct',DEST/'direct-grouped'):
  man=json.loads((d/'manifest.json').read_text()); man['_files_sha256']={p.name:sha(p) for p in d.iterdir() if p.is_file()}
  (DEST/(d.name+'-receipt.json')).write_text(json.dumps(man,indent=2)+'\n')
 for f in ('observation.c128','validity.u8'):
  if sha(DEST/'sampled-base'/f)!=sha(DEST/'sampled-direct'/f): raise RuntimeError('observation/mask bytes differ')
 entries['sample_count']=json.loads((DEST/'sampled-base'/'manifest.json').read_text())['sample_count']
 entries['raw_identity']={'observation_sha256':sha(DEST/'sampled-base'/'observation.c128'),'validity_sha256':sha(DEST/'sampled-base'/'validity.u8')}
 (DEST/'experiment.json').write_text(json.dumps(entries,indent=2)+'\n')
 print(json.dumps({'status':'exports-complete',**entries}))
if __name__=='__main__':main()
