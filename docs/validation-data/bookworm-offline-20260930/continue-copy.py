import datetime, hashlib, json, os, subprocess, sys, time
from pathlib import Path
ROOT=Path('/srv/recovery');OUT=ROOT/'outputs';SDK=ROOT/'native-sdk'
os.chdir(ROOT/'source')
report=json.loads((OUT/'serial-retries.json').read_text())
assert not report.get('finished')
assert [s['name'] for s in report['steps']]==['native-live-node18-idle-retry','native-live-node24-same-worker','native-web-node24-full-idle','wasm-web-full-idle']
assert not subprocess.check_output(['ip','route'],text=True).strip()
os.environ.update(HOME='/home/builder',PATH=str(SDK/'bin')+':'+os.environ['PATH'],PYTHON=str(SDK/'bin/python3'),PYTHONDONTWRITEBYTECODE='1',LC_ALL='C.UTF-8')
package=ROOT/'build-fltk/releases/verification-83c064b972a1/DataPump-1.0.0-Linux-x86_64-native-fltk.tar.gz/offline destination with spaces/DataPump-1.0.0-Linux-x86_64-native-fltk'
assert (package/'manifest.sha256').is_file()
report['harness_interruption']='Original retry driver stopped after four completed steps because its scratch-directory glob also selected the earlier non-GUI verification copy. No runtime test was launched by the failed precondition. Resume only pending copied test using exact original fatal-failure root.'
report['copied_root']=str(package)
report['copied_manifest_sha256']=hashlib.sha256((package/'manifest.sha256').read_bytes()).hexdigest()
name='copied-fltk-tgz-full-idle';(OUT/'serial-stage').write_text(name+'\n');start=time.monotonic();stamp=datetime.datetime.now(datetime.timezone.utc).isoformat()
args=['xvfb-run','-a','-s','-screen 0 2400x1800x24 -dpi 96',str(SDK/'bin/cmake'),'-DPACKAGE_ROOT='+str(package),'-DGUI_SMOKE=ON','-DGUI_SMOKE_TIMEOUT=600','-DREQUIRE_MANUALS=ON','-DREQUIRE_FRONTENDS=ON','-P','tools/verify-native-package.cmake']
with (OUT/(name+'.log')).open('w') as log:
    log.write('COMMAND '+json.dumps(args)+'\n');log.flush();subprocess.run(['vmstat','1','2'],stdout=log,stderr=subprocess.STDOUT)
    rc=subprocess.call(args,stdout=log,stderr=subprocess.STDOUT)
report['steps'].append({'name':name,'started':stamp,'exit_code':rc,'seconds':round(time.monotonic()-start,2)})
report['worker_sha256_after']=hashlib.sha256((ROOT/'build-fltk/datapump-worker').read_bytes()).hexdigest()
assert report['worker_sha256']==report['worker_sha256_after']
report['finished']=datetime.datetime.now(datetime.timezone.utc).isoformat()
(OUT/'serial-retries.json').write_text(json.dumps(report,indent=2)+'\n');(OUT/'serial-stage').write_text('completed\n');print(json.dumps(report['steps'][-1]),flush=True)
sys.exit(any(s['exit_code'] for s in report['steps']))
