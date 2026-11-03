import pathlib,json,hashlib,shutil,tarfile,subprocess,time,datetime
root=pathlib.Path('/workspace/triad-results');workspace=root.parent
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
assert json.loads(pathlib.Path('/workspace/triad-fused/local-evidence/triad-group-fusion/benchmark.json').read_text())['state']=='complete'
assert subprocess.run(['pgrep','-x','triad_long'],stdout=subprocess.DEVNULL).returncode==1
provenance=root/'preserved-builds';provenance.mkdir(exist_ok=False)
for n in ['build.log','build-proof.json','binding-smoke.log','binding-smoke.status.json','phase-build-binding.json','fused-build.log','fused-build-proof.json']:
    shutil.copy2(pathlib.Path('/tmp/triad-extension-cloud')/n,provenance/n)
shutil.copy2('/workspace/triad/build/dev-cli/triad_long',provenance/'triad_long-original')
shutil.copy2('/workspace/triad-fused/build/dev-cli/triad_long',provenance/'triad_long-fused')
shutil.copytree('/workspace/triad-fused/local-evidence/triad-group-fusion',root/'exporter-fusion')
sources=root/'preserved-source-packages';sources.mkdir()
for base,names in [('/tmp',['triad-extension-minimal.tar.gz','physical-reviewed.tar.gz','triad-phase-extension-overlay.tar.gz','triad-phase-extension-reviewed-overlay.tar.gz']),('/workspace',['scorer-v2-overlay.tar.gz','triad-phase-cn57-plan-overlay.tar.gz','maxspread-plan-overlay.tar.gz','fused-native-overlay.tar.gz','fused-benchmark-tools.tar.gz'])]:
    for name in names:shutil.copy2(pathlib.Path(base)/name,sources/name)
plans={sha(p):p.name[:-10] for p in root.glob('*-plan.json') if json.loads(p.read_text()).get('schema') in ['triad-extension-physical-plan-v1','triad-flat-tcxo-actual-plan-v1']}
expected={};receipt_count=0
for p in root.rglob('results.json'):
    d=json.loads(p.read_text())
    if d.get('schema')=='triad-extension-physical-stage-v1':base=root/plans[d['plan_sha256']]
    elif d.get('schema')=='triad-extension-source-variant-results-v1':base=root/(plans[d['original_plan_sha256']]+'-rescored')
    elif 'output_root' in d:base=pathlib.Path(d['output_root'])
    else:raise ValueError('unrecognized results receipt: '+str(p))
    assert d['all_selected_jobs_accounted'];receipt_count+=1
    for job in d['jobs']:
        for item in job.get('output_files',[]):
            name=str((base/item['path']).relative_to(root));value=(item['size_bytes'],item['sha256'])
            assert name not in expected or expected[name]==value
            expected[name]=value
files={};started=time.monotonic()
for p in sorted(root.rglob('*')):
    if p.is_file():
        assert not p.is_symlink();name=str(p.relative_to(root));files[name]={'size_bytes':p.stat().st_size,'sha256':sha(p)}
        if name in expected:assert expected[name]==(files[name]['size_bytes'],files[name]['sha256']),name
assert set(expected)<=set(files)
index=dict(schema='triad-extension-preservation-v1',receipt_count=receipt_count,receipt_bound_files=len(expected),receipt_bound_logical_bytes=sum(v[0] for v in expected.values()),payload_files=len(files),logical_bytes=sum(v['size_bytes'] for v in files.values()),files=files,archive_note='Identical-content files are stored as tar hardlinks to prior members; treat extracted evidence as immutable. All receipt-bound bytes were hashed and verified before packing.')
index_path=root/'evidence-inventory.json';index_path.write_text(json.dumps(index,indent=2,sort_keys=True)+'\n')
archive=workspace/'triad-extension-evidence.tar.zst';seen={};unique_bytes=0;aliases=0
with archive.open('xb') as stream:
    zstd=subprocess.Popen(['zstd','-T4','-3','-q','-c'],stdin=subprocess.PIPE,stdout=stream)
    with tarfile.open(fileobj=zstd.stdin,mode='w|') as tar:
        for name,item in files.items():
            p=root/name;arc='triad-results/'+name;info=tar.gettarinfo(str(p),arcname=arc);key=(item['size_bytes'],item['sha256'])
            if key in seen:
                info.type=tarfile.LNKTYPE;info.linkname=seen[key];info.size=0;tar.addfile(info);aliases+=1
            else:
                with p.open('rb') as f:tar.addfile(info,f)
                seen[key]=arc;unique_bytes+=item['size_bytes']
        info=tar.gettarinfo(str(index_path),arcname='triad-results/evidence-inventory.json')
        with index_path.open('rb') as f:tar.addfile(info,f)
    zstd.stdin.close();assert zstd.wait(timeout=120)==0
meta=dict(schema='triad-extension-archive-v1',archive=archive.name,archive_sha256=sha(archive),archive_bytes=archive.stat().st_size,inventory_sha256=sha(index_path),logical_bytes=index['logical_bytes'],payload_files=len(files),unique_content_bytes=unique_bytes,tar_hardlink_aliases=aliases,receipt_bound_files=len(expected),receipt_count=receipt_count,elapsed_seconds=time.monotonic()-started,completed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),generator_sha256=sha(pathlib.Path(__file__)))
(workspace/'triad-extension-evidence.archive.json').write_text(json.dumps(meta,indent=2)+'\n');print(json.dumps(meta),flush=True)
