#!/usr/bin/python3
import os,sys,json,subprocess,time,datetime,hashlib
from pathlib import Path
ROOT=Path('/srv/recovery');OUT=ROOT/'outputs';SDK=ROOT/'native-sdk';TREE=ROOT/'build-fltk';WASM=ROOT/'wasm-sdk-with-retained-python';SRC=ROOT/'source';os.chdir(SRC)
main=json.loads((OUT/'results.json').read_text());assert main.get('finished'),'Main test must finish before serial retries'
wr=json.loads((OUT/'wasm-recovery.json').read_text());assert wr.get('finished'),'Other test must finish before serial retries'
os.environ.update(HOME='/home/builder',PYTHONDONTWRITEBYTECODE='1',LC_ALL='C.UTF-8',PYTHON=str(SDK/'bin/python3'),PATH=str(SDK/'bin')+':'+os.environ['PATH'])
for k in ['LD_LIBRARY_PATH','LD_PRELOAD','CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','LIBRARY_PATH','CC','CXX']:os.environ.pop(k,None)
worker=TREE/'datapump-worker';node24=WASM/'node/bin/node'
report={'started':datetime.datetime.now(datetime.timezone.utc).isoformat(),'source_sha':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'worker_sha256':hashlib.sha256(worker.read_bytes()).hexdigest(),'network_namespace':os.readlink('/proc/self/ns/net'),'routes':subprocess.check_output(['ip','route'],text=True),'node18':subprocess.check_output(['/usr/bin/node','--version'],text=True).strip(),'node24':subprocess.check_output([str(node24),'--version'],text=True).strip(),'python':str(SDK/'bin/python3'),'steps':[]};assert not report['routes'].strip()
def persist():(OUT/'serial-retries.json').write_text(json.dumps(report,indent=2)+'\n')
def step(name,args):
 (OUT/'serial-stage').write_text(name+'\n');start=time.monotonic();stamp=datetime.datetime.now(datetime.timezone.utc).isoformat()
 with (OUT/(name+'.log')).open('w') as log:
  log.write('COMMAND '+json.dumps(args)+'\n');log.flush();subprocess.run(['vmstat','1','2'],stdout=log,stderr=subprocess.STDOUT)
  rc=subprocess.call(args,stdout=log,stderr=subprocess.STDOUT)
 result={'name':name,'started':stamp,'exit_code':rc,'seconds':round(time.monotonic()-start,2)};report['steps'].append(result);persist();print(json.dumps(result),flush=True);return rc==0
step('native-live-node18-idle-retry',[str(SDK/'bin/ctest'),'--test-dir',str(TREE),'-C','Release','--output-on-failure','--no-tests=error','--parallel','1','-R','^web_live$'])
step('native-live-node24-same-worker',[str(node24),'tests/test_web_live.mjs','--native',str(worker)])
assert report['worker_sha256']==hashlib.sha256(worker.read_bytes()).hexdigest(),'Worker changed during Node comparison'
nf=['--sdk',str(SDK),'--backend','fltk','--tui','--fb','--web-worker','--build-dir',str(TREE),'--jobs','1','--build-jobs','1','--','-DDATAPUMP_PORTABLE=ON','-DDATAPUMP_NODE_EXECUTABLE='+str(node24)]
step('native-web-node24-full-idle',['./build.sh','test','web']+nf)
step('wasm-web-full-idle',['./build.sh','test','web','--wasm-sdk',str(WASM),'--build-dir',str(ROOT/'build-wasm-recovered'),'--jobs','1','--build-jobs','1'])
roots=list((TREE/'releases').glob('verification-*/DataPump-*-native-fltk.tar.gz/offline destination with spaces/*/manifest.sha256'))
assert len(roots)==1,'Expected exact failed copied FLTK package root'
step('copied-fltk-tgz-full-idle',['xvfb-run','-a','-s','-screen 0 2400x1800x24 -dpi 96',str(SDK/'bin/cmake'),'-DPACKAGE_ROOT='+str(roots[0].parent),'-DGUI_SMOKE=ON','-DGUI_SMOKE_TIMEOUT=600','-DREQUIRE_MANUALS=ON','-DREQUIRE_FRONTENDS=ON','-P','tools/verify-native-package.cmake'])
report['worker_sha256_after']=hashlib.sha256(worker.read_bytes()).hexdigest();report['finished']=datetime.datetime.now(datetime.timezone.utc).isoformat();persist();(OUT/'serial-stage').write_text('completed\n');sys.exit(any(x['exit_code'] for x in report['steps']))
