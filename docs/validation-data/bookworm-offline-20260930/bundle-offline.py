import datetime, hashlib, importlib.util, json, os, shutil, subprocess, time
from pathlib import Path
ROOT=Path('/srv/recovery'); OUT=ROOT/'outputs'; SRC=ROOT/'source'; SDK=ROOT/'native-sdk'
assert json.loads((OUT/'serial-retries.json').read_text()).get('finished')
os.chdir(SRC)
os.environ.update(PATH=str(SDK/'bin')+':'+os.environ['PATH'],HOME='/home/builder',LC_ALL='C.UTF-8',PYTHONDONTWRITEBYTECODE='1')
report={'started':datetime.datetime.now(datetime.timezone.utc).isoformat(),'source_sha':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'network_namespace':os.readlink('/proc/self/ns/net'),'routes':subprocess.check_output(['ip','route'],text=True),'scope':'Existing release-web library assembly and full non-GUI copied-package/ABI checks; no publication and no replacement of original producers. GUI reception evidence remains from original exact native executable bytes.','steps':[]}
assert not report['routes'].strip()
spec=importlib.util.spec_from_file_location('release_web',SRC/'tools/release-web.py')
web=importlib.util.module_from_spec(spec);spec.loader.exec_module(web)
metadata={'source_sha':report['source_sha'],'dependencies':{'wasm-sdk':'e66e98abb90b466cab32'},'web':web.CAPABILITY}
wasm_tree=ROOT/'build-wasm-recovered'
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
tested={'factory':digest(wasm_tree/'datapump-wasm.js'),'wasm':digest(wasm_tree/'datapump-wasm.wasm')}
report['serial_tested_wasm_inputs']=tested
def persist():(OUT/'complete-portable.json').write_text(json.dumps(report,indent=2)+'\n')
def stage_wasm(name):
    # CPack's private _CPack_Packages tree is not a producer asset. Match the
    # ordinary workflow artifact handoff: exact TGZ/ZIP pair plus checksums.
    target=ROOT/name;assert not target.exists();target.mkdir()
    source=wasm_tree/'releases'
    files=[p for p in source.iterdir() if p.is_file()]
    assert len(files)==3 and any(p.name=='SHA256SUMS.txt' for p in files)
    for path in files:shutil.copyfile(path,target/path.name)
    return target
staged=stage_wasm('wasm-producer-input')
browser=web.browser_payload(staged,metadata)
def same_runtime(payload):
    recorded=json.loads(payload[web.WEB+'web-manifest.json'][0])['inputs']
    return all(recorded[key]==value for key,value in tested.items())
if not same_runtime(browser):
    # Preserve the original stock-tool producer before refreshing the packaging
    # from the exact runtime bytes that the serial suite just exercised.
    shutil.copytree(staged,ROOT/'wasm-stock-produced')
    start=time.monotonic()
    args=['./build.sh','package','--wasm-sdk',str(ROOT/'wasm-sdk-with-retained-python'),'--build-dir',str(wasm_tree),'--jobs','1','--build-jobs','3']
    with (OUT/'wasm-repackage-tested-runtime.log').open('w') as log:
        log.write('COMMAND '+json.dumps(args)+'\n');log.flush()
        rc=subprocess.call(args,stdout=log,stderr=subprocess.STDOUT)
    report['steps'].append({'name':'wasm-repackage-tested-runtime','exit_code':rc,'seconds':round(time.monotonic()-start,2)});persist()
    assert rc==0
    assert tested=={'factory':digest(wasm_tree/'datapump-wasm.js'),'wasm':digest(wasm_tree/'datapump-wasm.wasm')},'Packaging changed the tested runtime bytes'
    browser=web.browser_payload(stage_wasm('wasm-tested-producer-input'),metadata)
assert same_runtime(browser),'Browser producer differs from serial-tested runtime bytes'
destination=ROOT/'complete-portable';assert not destination.exists();destination.mkdir()
for backend in ('fltk','rev'):
    start=time.monotonic();target=destination/backend;target.mkdir()
    original=ROOT/('build-'+backend)/'releases'
    archives=sorted(p for p in original.iterdir() if p.is_file() and p.name.endswith(('.tar.gz','.zip')))
    assert len(archives)==2
    entries=[]
    for source in archives:
        _, before=web.archive_files(source)
        output=target/source.name
        web.bundle(source,output,browser)
        web.verify_native(output,metadata,'linux-x86_64-'+backend)
        _, after=web.archive_files(output)
        native=[name for name in before if name.startswith(('bin/','lib/'))]
        assert all(before[name]==after[name] for name in before if name!='manifest.sha256'),'Original file bytes/modes changed during browser bundling'
        entries.append({'name':output.name,'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'bytes':output.stat().st_size,'native_files_unchanged':len(native)})
    (target/'SHA256SUMS.txt').write_text(''.join(e['sha256']+'  '+e['name']+'\n' for e in entries))
    report['steps'].append({'name':'bundle-'+backend,'exit_code':0,'seconds':round(time.monotonic()-start,2),'archives':entries});persist()
    name='verify-complete-'+backend;start=time.monotonic()
    args=[str(SDK/'bin/cmake'),'-DARCHIVE_DIR='+str(target),'-DGUI_SMOKE=OFF','-DMAX_GLIBC=2.36','-DREQUIRE_MANUALS=ON','-DREQUIRE_FRONTENDS=ON','-P','tools/verify-native-archives.cmake']
    with (OUT/(name+'.log')).open('w') as log:
        log.write('COMMAND '+json.dumps(args)+'\n');log.flush()
        rc=subprocess.call(args,stdout=log,stderr=subprocess.STDOUT)
    report['steps'].append({'name':name,'exit_code':rc,'seconds':round(time.monotonic()-start,2)});persist()
    print(json.dumps(report['steps'][-1]),flush=True)
report['finished']=datetime.datetime.now(datetime.timezone.utc).isoformat();persist()
raise SystemExit(any(step['exit_code'] for step in report['steps']))
