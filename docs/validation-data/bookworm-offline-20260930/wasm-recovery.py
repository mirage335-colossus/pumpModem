#!/usr/bin/python3
import os,sys,json,subprocess,time,datetime
from pathlib import Path
ROOT=Path('/srv/recovery');os.chdir(ROOT/'source');OUT=ROOT/'outputs';SDK=ROOT/'native-sdk';WASM=ROOT/'wasm-sdk-with-retained-python'
os.environ.update(HOME='/home/builder',PYTHONDONTWRITEBYTECODE='1',LC_ALL='C.UTF-8')
report={'started':datetime.datetime.now(datetime.timezone.utc).isoformat(),'source_sha':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'path':'Use retained native SDK Python only for Wasm install, then stock Bookworm host tools for application/package tests','network_namespace':os.readlink('/proc/self/ns/net'),'routes':subprocess.check_output(['ip','route'],text=True),'steps':[]};assert not report['routes'].strip()
def step(name,args):
 (OUT/'wasm-recovery-stage').write_text(name+'\n');start=time.monotonic()
 with (OUT/(name+'.log')).open('w') as log:
  log.write('COMMAND '+json.dumps(args)+'\n');log.flush();rc=subprocess.call(args,stdout=log,stderr=subprocess.STDOUT)
 result={'name':name,'exit_code':rc,'seconds':round(time.monotonic()-start,2)};report['steps'].append(result)
 (OUT/'wasm-recovery.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(result),flush=True);return rc==0
if not step('wasm-install-retained-python',[str(SDK/'bin/python3'),'tools/wasm-sdk-release.py','install','--directory',str(ROOT/'inputs/wasm'),'--destination',str(WASM)]):sys.exit(1)
wf=['--wasm-sdk',str(WASM),'--build-dir',str(ROOT/'build-wasm-recovered'),'--jobs','1','--build-jobs','1']
if step('wasm-package-bookworm-retained-python',['./build.sh','package']+wf):step('wasm-web-bookworm-retained-python',['./build.sh','test','web']+wf)
report['finished']=datetime.datetime.now(datetime.timezone.utc).isoformat();(OUT/'wasm-recovery.json').write_text(json.dumps(report,indent=2)+'\n');(OUT/'wasm-recovery-stage').write_text('completed\n');sys.exit(any(x['exit_code'] for x in report['steps']))
