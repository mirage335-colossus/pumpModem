#!/usr/bin/env python3
"""Fail-closed preservation of completed FFT campaign outputs (no cloud actions)."""
from __future__ import annotations
import argparse, datetime, hashlib, json, os, signal, subprocess, tarfile, time
from collections import Counter
from pathlib import Path, PurePosixPath

RESULT_DIRS=("physical","long","followup","gpu-comparison")
OPS_REQUIRED=("triad-fft-source.tar.gz","triad-fft-source.manifest.json",
 "triad-fft-wide-pilot-sampled.tar.gz","triad-fft-wide-pilot-sampled.manifest.json",
 "plan.json","long-plan.json","followup-plan.json","run-v1.py","run.py","compare.py","triad_long")
MAX_FILES=100_000
CHUNK=1024*1024

def digest(path:Path):
 h=hashlib.sha256(); n=0
 with path.open('rb') as f:
  while b:=f.read(CHUNK): h.update(b); n+=len(b)
 return n,h.hexdigest()

def canonical(name):
 p=PurePosixPath(name)
 if not name or p.is_absolute() or '\\' in name or any(x in ('','.','..') for x in name.split('/')) or any(ord(c)<32 or ord(c)==127 for c in name):
  raise ValueError(f'unsafe/noncanonical path: {name!r}')
 return name

def read_json(path):
 if path.stat().st_size>64*1024*1024: raise ValueError(f'JSON too large: {path}')
 def pairs(items):
  d={}
  for k,v in items:
   if k in d: raise ValueError(f'duplicate JSON key: {k}')
   d[k]=v
  return d
 return json.loads(path.read_text(),object_pairs_hook=pairs)

def campaign_receipt(root,name):
 p=root/name/'results.json'
 if p.is_symlink() or not p.is_file(): raise ValueError(f'{name}: results.json missing/unsafe')
 d=read_json(p)
 if d.get('schema')!='triad-fft-campaign-results-v1': raise ValueError(f'{name}: campaign receipt schema mismatch')
 selected=d.get('selected_captures'); records=d.get('records'); counts=d.get('counts')
 if type(selected) is not int or selected<=0 or d.get('all_selected_captures_accounted') is not True:
  raise ValueError(f'{name}: selected capture completion flag/count invalid')
 if not isinstance(records,list) or len(records)!=selected or d.get('planned_captures')!=selected or not isinstance(counts,dict):
  raise ValueError(f'{name}: record/count cardinality mismatch')
 if any(type(v) is not int or v<0 for v in counts.values()): raise ValueError(f'{name}: invalid aggregate count values')
 ids=[]
 for r in records:
  if not isinstance(r,dict): raise ValueError(f'{name}: malformed capture record')
  ident=r.get('id')
  if not isinstance(ident,str) or not ident: raise ValueError(f'{name}: capture record lacks unique id')
  ids.append(ident)
  if r.get('status')!='completed':
   raise ValueError(f'{name}: capture {ident} is not complete/accounted')
 if len(set(ids))!=selected: raise ValueError(f'{name}: capture IDs are not unique')
 if counts!=dict(Counter(r['status'] for r in records)): raise ValueError(f'{name}: aggregate counts disagree with records')
 if d.get('source_and_binary_unchanged') is not True or d.get('identity_error') is not None:
  raise ValueError(f'{name}: source/binary change or missing proof')
 return {'path':str(p.relative_to(root)),'schema':d['schema'],'selected_captures':selected,
  'records':len(records),'completed':counts['completed'],'unique_capture_ids':True,'source_and_binary_unchanged':True}

def validate_input_records(root,files):
 checked=[]
 for campaign in ('physical','long','followup'):
  receipt=read_json(root/campaign/'results.json')
  for row in receipt['records']:
   ident=row['id']; paths=row.get('input_files')
   if not isinstance(paths,list) or len(paths)!=4 or {r.get('name',r.get('path')) for r in paths if isinstance(r,dict)}!={'manifest.json','basis.c128','observation.c128','validity.u8'}:
    raise ValueError(f'{campaign}/{ident}: exactly four bound sampled inputs required')
   for rec in paths:
    name=rec.get('name',rec.get('path')); want=rec.get('sha256'); size=rec.get('bytes',rec.get('size_bytes'))
    if not isinstance(name,str) or not isinstance(want,str): raise ValueError(f'{campaign}/{ident}: malformed input hash')
    rel=PurePosixPath(name)
    if rel.is_absolute() or '..' in rel.parts: raise ValueError(f'{campaign}/{ident}: unsafe input path {name}')
    exact=f'results/{campaign}/workspaces/{ident}/{name}'
    actual=files.get(exact)
    if actual is None or actual['sha256'].lower()!=want.lower() or (size is not None and int(size)!=actual['size_bytes']):
     raise ValueError(f'{campaign}/{ident}: exact capture input mismatch: {name}')
    checked.append({'receipt':f'{campaign}/results.json','capture_id':ident,'path':name,'payload_path':exact,**actual})
  names={PurePosixPath(x['path']).name for x in checked if x['receipt']==f'{campaign}/results.json'}
  required={'manifest.json','basis.c128','observation.c128','validity.u8'}
  if not required<=names: raise ValueError(f'{campaign}: required sampled input files absent from capture receipts')
 return checked

def validate_gpu(root,wide_input_root):
 p=root/'gpu-comparison'/'summary.json'
 cp=root/'gpu-comparison'/'campaign.json'
 if not p.is_file() or p.is_symlink(): raise ValueError('GPU comparison summary missing/unsafe')
 if not cp.is_file() or cp.is_symlink(): raise ValueError('GPU comparison campaign provenance missing/unsafe')
 d=read_json(p)
 c=read_json(cp)
 if d.get('source_unchanged_during_campaign') is not True:
  raise ValueError('GPU summary does not prove unchanged source')
 before=c.get('source_closure_before'); after=d.get('source_closure_after')
 if not isinstance(before,dict) or not before or before!=after:
  raise ValueError('GPU source closure before/after differs or is missing')
 jobs=d.get('jobs')
 if not isinstance(jobs,list) or len(jobs)!=6: raise ValueError('GPU summary must account for six jobs')
 status_files=[]
 inputs=c.get('input_files')
 if not isinstance(inputs,dict): raise ValueError('GPU campaign input hash map missing')
 wide_files={}
 for name in ('manifest.json','basis.c128','observation.c128','validity.u8'):
  q=wide_input_root/name
  if q.is_symlink() or not q.is_file(): raise ValueError(f'GPU wide-pilot input missing: {name}')
  sz,sha=digest(q); wide_files[name]={'size_bytes':sz,'sha256':sha}
  if inputs.get(name)!=sha: raise ValueError(f'GPU summary input hash mismatch: {name}')
 if c.get('input_manifest_sha256')!=wide_files['manifest.json']['sha256']:
  raise ValueError('GPU sampled manifest hash mismatch')
 for j in jobs:
  if not isinstance(j,dict) or j.get('status')!='completed': raise ValueError('GPU summary has a failed or malformed job')
  name=j.get('job')
  if not isinstance(name,str) or '/' in name or name in ('.','..'): raise ValueError('unsafe GPU job name')
  sp=root/'gpu-comparison'/f'{name}.status.json'
  if not sp.is_file() or sp.is_symlink(): raise ValueError(f'GPU status missing: {name}')
  sd=read_json(sp)
  if sd.get('job')!=name or sd.get('status')!='completed': raise ValueError(f'GPU status mismatch: {name}')
  size,sha=digest(sp)
  score_path=root/'gpu-comparison'/f'{name}.scores.json'
  if not score_path.is_file() or score_path.is_symlink(): raise ValueError(f'GPU score output missing: {name}')
  score_size,score_sha=digest(score_path)
  if j.get('score_sha256')!=score_sha or sd.get('score_sha256')!=score_sha:
   raise ValueError(f'GPU score sha mismatch: {name}')
  recorded_score=j.get('score_file')
  if isinstance(recorded_score,str) and not recorded_score.endswith('/gpu-comparison/'+score_path.name):
   raise ValueError(f'GPU summary score path mismatch: {name}')
  # Compare summary/status copies of the stable job result identity fields.
  for key in ('job','device','precision','status','returncode','wall_seconds','peak_child_rss_bytes','score_sha256'):
   if key in j and sd.get(key)!=j.get(key): raise ValueError(f'GPU summary/status semantic mismatch {name}:{key}')
  status_files.append({'job':name,'status_path':str(sp.relative_to(root)),'status_size_bytes':size,'status_sha256':sha,
   'score_path':str(score_path.relative_to(root)),'score_size_bytes':score_size,'score_sha256':score_sha,
   'sha_scope':'status/score bytes rehashed during preservation; status hash is a preservation-time audit value, not a contemporaneous external attestation'})
 return {'summary_path':str(p.relative_to(root)),'summary_sha256':digest(p)[1],
         'campaign_path':str(cp.relative_to(root)),'campaign_sha256':digest(cp)[1],'status_files':status_files,
         'wide_input_files':wide_files,'audit_scope':'preservation-time rehash and summary/status consistency; not contemporaneous proof'}

def _assemble(results,ops,outbase,bootstrap_logs_root,wide_input_root):
 if not results.is_dir() or not ops.is_dir() or not bootstrap_logs_root.is_dir() or not wide_input_root.is_dir(): raise ValueError('results/ops/logs/wide-input root missing')
 for d in RESULT_DIRS:
  if not (results/d).is_dir(): raise ValueError(f'required campaign directory missing: {d}')
 if not (results/'environment.json').is_file(): raise ValueError('environment.json missing')
 receipts=[campaign_receipt(results,n) for n in ('physical','long','followup')]
 gpu=validate_gpu(results,wide_input_root)
 files={}; sources={}
 def add(path,arc):
  canonical(arc)
  if path.is_symlink() or not path.is_file(): raise ValueError(f'only regular files are allowed: {path}')
  if arc in files: raise ValueError(f'duplicate logical package path: {arc}')
  size,sha=digest(path); files[arc]={'size_bytes':size,'sha256':sha}; sources[arc]=path
 for p in sorted(results.rglob('*')):
  if p.is_symlink(): raise ValueError(f'symlink in results tree: {p}')
  if p.is_file(): add(p,'results/'+str(p.relative_to(results)))
  elif not p.is_dir(): raise ValueError(f'unsupported result object: {p}')
 for name in OPS_REQUIRED:
  found=[p for p in ops.rglob(name) if p.is_file() and not p.is_symlink()]
  if name=='triad_long' and not found:
   candidate=results.parent/'build'/'dev-cli'/'triad_long'
   if candidate.is_file() and not candidate.is_symlink(): found=[candidate]
  if len(found)!=1: raise ValueError(f'expected one known-safe ops artifact named {name}, found {len(found)}')
  add(found[0],'ops/'+name)
 for name in ('manifest.json','basis.c128','observation.c128','validity.u8'):
  add(wide_input_root/name,'wide-input/'+name)
 logs=sorted([p for p in bootstrap_logs_root.glob('*.log') if p.is_file() and not p.is_symlink()])
 if not logs: raise ValueError('bootstrap build logs missing')
 for p in logs: add(p,'ops/bootstrap-logs/'+p.name)
 inputs=validate_input_records(results,files)
 if len(files)>MAX_FILES: raise ValueError('file-count bound exceeded')
 inventory={'schema':'triad-fft-preservation-inventory-v1','results_root':str(results),'campaign_receipts':receipts,
  'input_file_records_verified':inputs,'gpu_status':gpu,'files':files,'file_count':len(files),
  'source_paths':{arc:str(path) for arc,path in sources.items()},
  'logical_bytes':sum(x['size_bytes'] for x in files.values()),
  'preserved_ops_allowlist':list(OPS_REQUIRED)+[p.name for p in logs],
  'archive_note':'Identical files are represented as tar hardlinks to earlier members. Preserve as immutable scientific evidence.'}
 invraw=(json.dumps(inventory,indent=2,sort_keys=True)+'\n').encode()
 invpath=outbase.with_suffix('.inventory.json'); archive=outbase.with_suffix('.tar.zst'); metapath=outbase.with_suffix('.archive.json')
 if any(p.exists() for p in (invpath,archive,metapath)): raise FileExistsError('refusing to overwrite preservation output')
 invpath.write_bytes(invraw)
 started=time.monotonic(); seen={}; unique=0; aliases=0
 with archive.open('xb') as dst:
  z=subprocess.Popen(['zstd','-T4','-3','-q','-c'],stdin=subprocess.PIPE,stdout=dst,stderr=subprocess.PIPE)
  try:
   with tarfile.open(fileobj=z.stdin,mode='w|') as tar:
    for arc,item in files.items():
     source=sources[arc]
     ti=tar.gettarinfo(str(source),arcname=arc); key=(item['size_bytes'],item['sha256'])
     if key in seen:
      ti.type=tarfile.LNKTYPE; ti.linkname=seen[key]; ti.size=0; tar.addfile(ti); aliases+=1
     else:
      with source.open('rb') as f: tar.addfile(ti,f)
      seen[key]=arc; unique+=item['size_bytes']
    ti=tarfile.TarInfo('metadata/inventory.json'); ti.size=len(invraw)
    import io; tar.addfile(ti,io.BytesIO(invraw))
   z.stdin.close(); err=z.stderr.read(); code=z.wait(timeout=10)
   if code: raise RuntimeError(f'zstd failed ({code}): {err[-1000:].decode(errors="replace")}')
  except BaseException:
   z.kill(); z.wait(); raise
 size,archive_sha=digest(archive)
 metadata={'schema':'triad-fft-preservation-archive-v1','archive':archive.name,'archive_sha256':archive_sha,
  'archive_size_bytes':size,'inventory_sha256':hashlib.sha256(invraw).hexdigest(),'payload_files':len(files),
  'logical_bytes':inventory['logical_bytes'],'unique_content_bytes':unique,'hardlink_aliases':aliases,
  'campaign_receipt_count':len(receipts),'elapsed_seconds':round(time.monotonic()-started,3),
  'completed_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
 metapath.write_text(json.dumps(metadata,indent=2,sort_keys=True)+'\n')
 return metadata

def assemble(results,ops,outbase,bootstrap_logs_root,wide_input_root):
 targets=(outbase.with_suffix('.inventory.json'),outbase.with_suffix('.tar.zst'),outbase.with_suffix('.archive.json'))
 if any(p.exists() for p in targets): raise FileExistsError('refusing to overwrite preservation output')
 started=time.monotonic()
 def timeout(signum,frame): raise TimeoutError('preservation exceeded 300-second total bound')
 old=signal.signal(signal.SIGALRM,timeout); signal.setitimer(signal.ITIMER_REAL,300)
 try:
  result=_assemble(results,ops,outbase,bootstrap_logs_root,wide_input_root)
  result['elapsed_total_seconds']=round(time.monotonic()-started,3)
  metapath=outbase.with_suffix('.archive.json')
  meta=read_json(metapath); meta['elapsed_total_seconds']=result['elapsed_total_seconds']
  metapath.write_text(json.dumps(meta,indent=2,sort_keys=True)+'\n')
  return result
 except BaseException:
  for p in targets:
   if p.exists(): p.unlink()
  raise
 finally:
  signal.setitimer(signal.ITIMER_REAL,0); signal.signal(signal.SIGALRM,old)

def self_test():
 import tempfile
 with tempfile.TemporaryDirectory(prefix='triad-fft-pack-selftest-') as tmp:
  base=Path(tmp); results=base/'results'; ops=base/'ops'; logs=base/'logs'; wide=base/'wide'
  results.mkdir(); ops.mkdir(); logs.mkdir(); wide.mkdir()
  data={'manifest.json':b'{"fixture":1}\n','basis.c128':b'basis','observation.c128':b'observation','validity.u8':b'\x01\x01'}
  for name,b in data.items(): (wide/name).write_bytes(b)
  hashes={n:hashlib.sha256(b).hexdigest() for n,b in data.items()}
  for campaign in ('physical','long','followup'):
   work=results/campaign/'workspaces'/'capture-a'; work.mkdir(parents=True)
   input_files=[]
   for name,b in data.items():
    (work/name).write_bytes(b); input_files.append({'name':name,'bytes':len(b),'sha256':hashes[name]})
   rec={'schema':'triad-fft-campaign-results-v1','selected_captures':1,'planned_captures':1,
        'all_selected_captures_accounted':True,'source_and_binary_unchanged':True,'identity_error':None,
        'counts':{'completed':1},'records':[{'id':'capture-a','status':'completed','input_files':input_files}]}
   (results/campaign/'results.json').write_text(json.dumps(rec))
  (results/'environment.json').write_text('{"fixture":true}\n')
  gd=results/'gpu-comparison'; gd.mkdir()
  jobs=[]
  for i in range(6):
   name=f'fixture-{i}'; score=gd/f'{name}.scores.json'; score.write_text(json.dumps({'score':i})+'\n')
   score_sha=digest(score)[1]
   status={'job':name,'status':'completed','device':'cpu','precision':'fp32','returncode':0,
           'wall_seconds':0.01,'peak_child_rss_bytes':4096,'score_sha256':score_sha}
   sp=gd/f'{name}.status.json'; sp.write_text(json.dumps(status,sort_keys=True)+'\n')
   jobs.append({**status,'score_sha256':score_sha,'status_file_sha256':digest(sp)[1]})
  source_closure={'cli.py':hashlib.sha256(b'cli').hexdigest()}
  campaign={'schema':'triad-fft-cpu-gpu-comparison-campaign-v1','input_files':hashes,
            'input_manifest_sha256':hashes['manifest.json'],'source_closure_before':source_closure}
  summary={'schema':'triad-fft-cpu-gpu-comparison-summary-v1','source_unchanged_during_campaign':True,
           'source_closure_after':source_closure,'jobs':jobs}
  (gd/'campaign.json').write_text(json.dumps(campaign))
  (gd/'summary.json').write_text(json.dumps(summary))
  for name in OPS_REQUIRED: (ops/name).write_bytes(('fixture:'+name).encode())
  (logs/'bootstrap-build.log').write_text('fixture log\n')
  archive_meta=assemble(results,ops,base/'preserved',logs,wide)
  command=[os.environ.get('PYTHON','python3'),str(Path(__file__).with_name('verify.py')),
           '--archive',str(base/'preserved.tar.zst'),'--inventory',str(base/'preserved.inventory.json'),
           '--metadata',str(base/'preserved.archive.json'),'--timeout','15']
  verified=subprocess.run(command,capture_output=True,text=True,timeout=20,check=False)
  if verified.returncode: raise RuntimeError(f'local archive verifier rejected fixture: {verified.stderr.strip()}')
  result=json.loads(verified.stdout)
  if result.get('status')!='verified' or result.get('hardlink_aliases_verified')<4:
   raise RuntimeError('preservation fixture did not verify deduplicated hardlinks')
  return {'self_test':'passed','archive_bytes':archive_meta['archive_size_bytes'],
          'payload_files':archive_meta['payload_files'],'hardlinks':result['hardlink_aliases_verified'],
          'verified_members':result['members_verified']}

def main():
 p=argparse.ArgumentParser(description=__doc__); p.add_argument('--results-root',type=Path,default=Path('/workspace/triad/results'))
 p.add_argument('--ops-root',type=Path,default=Path('/tmp/triad-fft-ops'))
 p.add_argument('--bootstrap-logs-root',type=Path,default=Path('/tmp/triad-fft-logs'))
 p.add_argument('--wide-input-root',type=Path,default=Path('/workspace/triad-input/local-evidence/triad-fft-timing/wide-pilot/sampled'))
 p.add_argument('--outbase',type=Path,default=Path('/workspace/triad/triad-fft-results'))
 p.add_argument('--self-test',action='store_true')
 a=p.parse_args()
 if a.self_test: print(json.dumps(self_test(),indent=2)); return 0
 print(json.dumps(assemble(a.results_root.resolve(),a.ops_root.resolve(),a.outbase.resolve(),a.bootstrap_logs_root.resolve(),a.wide_input_root.resolve()),indent=2))
if __name__=='__main__': main()
