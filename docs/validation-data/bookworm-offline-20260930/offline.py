#!/usr/bin/python3
import os,sys,json,subprocess,time,socket,errno,datetime,shutil
from pathlib import Path
ROOT=Path('/srv/recovery'); SRC=ROOT/'source'; OUT=ROOT/'outputs'; OUT.mkdir(exist_ok=True)
os.chdir(SRC)
for key in ('CC','CXX','CMAKE_TOOLCHAIN_FILE','CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','LIBRARY_PATH','LD_LIBRARY_PATH','LD_PRELOAD','PYTHONPATH','EM_CONFIG','EM_CACHE'):
 os.environ.pop(key,None)
os.environ.update(HOME='/home/builder',PYTHONDONTWRITEBYTECODE='1',LC_ALL='C.UTF-8',DATAPUMP_MAX_GLIBC='2.36')
report={'source_sha':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'started':datetime.datetime.now(datetime.timezone.utc).isoformat(),'steps':[],'excluded_scopes':['full regression/calibration','physical audio','Windows','ARM64','cold toolchain reconstruction']}
def persist():
 tmp=OUT/'results.tmp';tmp.write_text(json.dumps(report,indent=2)+'\n');tmp.replace(OUT/'results.json')
def step(name,args,env=None):
 print('BEGIN',name,flush=True); (OUT/'current-stage').write_text(name+'\n');start=time.monotonic()
 with (OUT/(name+'.log')).open('w') as log:
  log.write('COMMAND '+json.dumps(args)+'\n');log.flush()
  process=subprocess.Popen(args,stdout=log,stderr=subprocess.STDOUT,env=env)
  (OUT/'active-job.json').write_text(json.dumps({'name':name,'pid':process.pid,'started':datetime.datetime.now(datetime.timezone.utc).isoformat()})+'\n')
  rc=process.wait()
 result={'name':name,'exit_code':rc,'seconds':round(time.monotonic()-start,2)};report['steps'].append(result);persist();print('END',json.dumps(result),flush=True);(OUT/'active-job.json').unlink();return rc==0
report['os_release']=Path('/etc/os-release').read_text();report['network_namespace']=os.readlink('/proc/self/ns/net');report['routes']=subprocess.check_output(['ip','route'],text=True);report['python']=sys.version;report['node']=subprocess.check_output(['node','--version'],text=True).strip()
assert not report['routes'].strip()
s=socket.socket();s.settimeout(2)
try:s.connect(('1.1.1.1',443));raise AssertionError('External networking unexpectedly available')
except OSError as e:assert e.errno==errno.ENETUNREACH;report['internet_probe']='ENETUNREACH'
finally:s.close()
report['ambient_compilers']={c:shutil.which(c) for c in ('cc','gcc','g++','clang','clang++')};assert not any(report['ambient_compilers'].values());persist()
SDK=ROOT/'native-sdk';WASM=ROOT/'wasm-sdk';archive=ROOT/'inputs/native/datapump-sdk-6c4884fdff9c745ab0a0-linux-x86_64.tar.gz'
if not step('native-retained-inputs',['python3','tools/sdk-release.py','verify','--directory',str(ROOT/'inputs/native')]):sys.exit(1)
if not step('native-sdk-install',['python3','tools/build-sdk.py','install','--archive',str(archive),'--destination',str(SDK)]):sys.exit(1)
if not step('native-sdk-verify',['python3','tools/build-sdk.py','verify',str(SDK),'--max-host-glibc','2.36']):sys.exit(1)
def flags(backend):return ['--sdk',str(SDK),'--backend',backend,'--tui','--fb','--web-worker','--build-dir',str(ROOT/('build-'+backend)),'--jobs','1','--build-jobs','3']
for backend in ('fltk','rev'):step('package-'+backend,['./build.sh','package']+flags(backend))
for group in ('frontends','web','packaging'):step('test-'+group,['xvfb-run','-a','-s','-screen 0 2400x1800x24 -dpi 96','./build.sh','test',group]+flags('fltk')+['--','-DDATAPUMP_PORTABLE=ON'])
for backend in ('fltk','rev'):
 step('native-'+backend,['xvfb-run','-a','-s','-screen 0 2400x1800x24 -dpi 96','./build.sh','test','native']+flags(backend)+['--','-DDATAPUMP_PORTABLE=ON','-DGUI_SMOKE_TIMEOUT=600'])
 step('copied-portable-'+backend,['xvfb-run','-a','-s','-screen 0 2400x1800x24 -dpi 96',str(SDK/'bin/cmake'),'-DARCHIVE_DIR='+str(ROOT/('build-'+backend)/'releases'),'-DGUI_SMOKE=ON','-DGUI_SMOKE_TIMEOUT=600','-DMAX_GLIBC=2.36','-DREQUIRE_MANUALS=ON','-DREQUIRE_FRONTENDS=ON','-P','tools/verify-native-archives.cmake'])
step('wasm-retained-inputs',['python3','tools/wasm-sdk-release.py','verify','--directory',str(ROOT/'inputs/wasm')])
if step('wasm-sdk-install-stock-bookworm',['python3','tools/wasm-sdk-release.py','install','--directory',str(ROOT/'inputs/wasm'),'--destination',str(WASM)]):
 wf=['--wasm-sdk',str(WASM),'--build-dir',str(ROOT/'build-wasm-stock'),'--jobs','1','--build-jobs','3']
 ok=step('wasm-package-stock-bookworm',['./build.sh','package']+wf)
 if ok:step('wasm-web-stock-bookworm',['./build.sh','test','web']+wf)
 if not ok:
  env=dict(os.environ);env['PATH']=str(SDK/'bin')+':'+env['PATH'];wf=['--wasm-sdk',str(WASM),'--build-dir',str(ROOT/'build-wasm-integrated'),'--jobs','1','--build-jobs','3']
  if step('wasm-package-integrated-sdk',['./build.sh','package']+wf,env):step('wasm-web-integrated-sdk',['./build.sh','test','web']+wf,env)
report['finished']=datetime.datetime.now(datetime.timezone.utc).isoformat();report['failed']=[x['name'] for x in report['steps'] if x['exit_code']];persist();(OUT/'current-stage').write_text('completed\n');sys.exit(bool(report['failed']))
