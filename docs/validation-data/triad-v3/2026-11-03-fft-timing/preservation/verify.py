#!/usr/bin/env python3
"""Stream-verify a preserved FFT result archive without extracting payloads."""
from __future__ import annotations
import argparse,contextlib,datetime,hashlib,io,json,os,signal,subprocess,tarfile,tempfile,time
from pathlib import Path,PurePosixPath

CHUNK=1024*1024
MAX_JSON=64*1024*1024
MAX_FILES=100_000

def require(ok,msg):
 if not ok: raise ValueError(msg)
def canonical(name):
 require(isinstance(name,str) and name and len(name)<4096,'invalid member name')
 require('\\' not in name and not any(ord(c)<32 or ord(c)==127 for c in name),'invalid member characters')
 p=PurePosixPath(name)
 require(not p.is_absolute() and all(x not in ('','.','..') for x in name.split('/')),'unsafe member path')
 return name
def file_digest(path):
 h=hashlib.sha256(); n=0
 with path.open('rb') as f:
  while b:=f.read(CHUNK): h.update(b); n+=len(b)
 return n,h.hexdigest()
def read_json(path):
 require(path.stat().st_size<=MAX_JSON,f'JSON too large: {path}')
 raw=path.read_bytes()
 def pairs(items):
  d={}
  for k,v in items:
   require(k not in d,f'duplicate JSON key: {k}'); d[k]=v
  return d
 return raw,json.loads(raw,object_pairs_hook=pairs)
class GuardedTarInfo(tarfile.TarInfo):
 def _proc_member(self,archive):
  allowed=(tarfile.REGTYPE,tarfile.AREGTYPE,tarfile.LNKTYPE,tarfile.XHDTYPE,tarfile.GNUTYPE_LONGNAME)
  require(self.type in allowed,f'unsupported tar type: {self.type!r}')
  if self.type in (tarfile.XHDTYPE,tarfile.GNUTYPE_LONGNAME): require(self.size<=65536,'oversize extension header')
  return super()._proc_member(archive)
 def _proc_gnusparse_00(self,*args): raise ValueError('sparse files unsupported')
 _proc_gnusparse_01=_proc_gnusparse_00
 _proc_gnusparse_10=_proc_gnusparse_00
def inventory_expected(raw):
 inv=json.loads(raw)
 require(inv.get('schema')=='triad-fft-preservation-inventory-v1','inventory schema mismatch')
 files=inv.get('files'); require(isinstance(files,dict) and 0<len(files)<=MAX_FILES,'inventory file count')
 expected={}; total=0
 for rel,item in files.items():
  canonical(rel); require(rel.startswith(('results/','ops/','wide-input/')),'unexpected inventory prefix')
  sz,sha=item.get('size_bytes'),item.get('sha256')
  require(type(sz) is int and sz>=0 and isinstance(sha,str) and len(sha)==64,'invalid size/hash record')
  expected[rel]=(sz,sha); total+=sz
 require(inv.get('file_count')==len(files) and inv.get('logical_bytes')==total,'inventory count/size mismatch')
 expected['metadata/inventory.json']=(len(raw),hashlib.sha256(raw).hexdigest())
 return inv,expected
def verify_stream(stream,expected):
 checked={}; logical=regular=aliases=0
 with tarfile.open(fileobj=stream,mode='r|',bufsize=CHUNK,tarinfo=GuardedTarInfo) as tar:
  for m in tar:
   name=canonical(m.name); require(name in expected,f'unexpected member: {name}')
   require(name not in checked,f'duplicate member: {name}')
   want_size,want_sha=expected[name]
   require(m.sparse is None and not any(x.startswith('GNU.sparse') for x in m.pax_headers),'sparse tar member')
   if m.isreg():
    require(m.size==want_size,f'member size mismatch: {name}')
    src=tar.extractfile(m); require(src is not None,f'missing data: {name}')
    h=hashlib.sha256(); n=0
    with src:
     while b:=src.read(CHUNK):
      n+=len(b); require(n<=want_size,f'member overrun: {name}'); h.update(b)
    require(n==want_size and h.hexdigest()==want_sha,f'member hash/size mismatch: {name}')
    if name!='metadata/inventory.json': regular+=n
   elif m.islnk():
    require(name!='metadata/inventory.json','inventory cannot be hardlinked')
    require(m.size==0,'hardlink payload not empty')
    target=canonical(m.linkname); require(target in checked,f'hardlink target must precede link: {name}')
    require(checked[target]==(want_size,want_sha),f'hardlink target mismatch: {name}')
    aliases+=1
   else: raise ValueError(f'unsupported tar member: {name}')
   checked[name]=(want_size,want_sha)
   if name!='metadata/inventory.json': logical+=want_size
   tar.members.clear()
  while b:=tar.fileobj.read(CHUNK): require(not any(b),'nonzero trailing bytes after tar end')
 require(set(checked)==set(expected),f'missing {len(set(expected)-set(checked))} members')
 return {'members_verified':len(checked),'payload_files_verified':len(checked)-1,'logical_bytes_verified':logical,
         'regular_payload_bytes_verified':regular,'hardlink_aliases_verified':aliases}
@contextlib.contextmanager
def deadline(seconds):
 def alarm(signum,frame): raise TimeoutError(f'verification exceeded {seconds}s limit')
 old=signal.signal(signal.SIGALRM,alarm); timer=signal.setitimer(signal.ITIMER_REAL,seconds)
 try: yield
 finally: signal.setitimer(signal.ITIMER_REAL,*timer); signal.signal(signal.SIGALRM,old)
def verify(archive,inventory_path,metadata_path,max_seconds=300):
 start=time.monotonic()
 with deadline(max_seconds):
  iraw,inv=read_json(inventory_path); mraw,meta=read_json(metadata_path)
  require(meta.get('schema')=='triad-fft-preservation-archive-v1','archive metadata schema mismatch')
  require(meta.get('archive')==archive.name,'archive filename mismatch')
  require(meta.get('inventory_sha256')==hashlib.sha256(iraw).hexdigest(),'inventory external hash mismatch')
  invobj,expected=inventory_expected(iraw)
  for k,ik in [('payload_files','file_count'),('logical_bytes','logical_bytes')]: require(meta.get(k)==invobj.get(ik),f'{k} mismatch')
  before=archive.stat(); require(before.st_size==meta.get('archive_size_bytes'),'archive size mismatch')
  size,sha=file_digest(archive); require(size==before.st_size and sha==meta.get('archive_sha256'),'archive SHA/size mismatch')
  proc=subprocess.Popen(['zstd','-q','-d','-c','--',str(archive)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
  try:
   result=verify_stream(proc.stdout,expected); proc.stdout.close(); code=proc.wait(timeout=10)
   require(code==0,'zstd decompression failed: '+proc.stderr.read(2048).decode(errors='replace'))
  finally:
   if proc.poll() is None:
    with contextlib.suppress(ProcessLookupError): os.killpg(proc.pid,signal.SIGTERM)
    with contextlib.suppress(subprocess.TimeoutExpired): proc.wait(timeout=2)
   if proc.poll() is None:
    with contextlib.suppress(ProcessLookupError): os.killpg(proc.pid,signal.SIGKILL)
    proc.wait()
  after=archive.stat(); require((before.st_dev,before.st_ino,before.st_size,before.st_mtime_ns,before.st_ctime_ns)==(after.st_dev,after.st_ino,after.st_size,after.st_mtime_ns,after.st_ctime_ns),'archive changed while verifying')
  require(result['hardlink_aliases_verified']==meta.get('hardlink_aliases'),'hardlink count mismatch')
  require(result['regular_payload_bytes_verified']==meta.get('unique_content_bytes'),'unique byte count mismatch')
  require(result['logical_bytes_verified']==meta.get('logical_bytes'),'logical byte count mismatch')
 return {'schema':'triad-fft-preservation-verification-v1','status':'verified','archive':str(archive.resolve()),
  'archive_bytes':size,'archive_sha256':sha,'inventory_sha256':hashlib.sha256(iraw).hexdigest(),
  'metadata_sha256':hashlib.sha256(mraw).hexdigest(),'elapsed_seconds':round(time.monotonic()-start,3),
  'extracted_payload_bytes':0,'decompressor_exit_status':0,**result}

def self_test():
 with tempfile.TemporaryDirectory(prefix='triad-fft-verify-selftest-') as d:
  root=Path(d); a=root/'fixture.tar.zst'; invp=root/'fixture.inventory.json'; mp=root/'fixture.archive.json'
  payload=b'compact deterministic preservation fixture\n'; h=hashlib.sha256(payload).hexdigest()
  inv={'schema':'triad-fft-preservation-inventory-v1','file_count':2,'logical_bytes':2*len(payload),
       'files':{'results/a.bin':{'size_bytes':len(payload),'sha256':h},'results/b.bin':{'size_bytes':len(payload),'sha256':h}}}
  raw=(json.dumps(inv,sort_keys=True,indent=2)+'\n').encode(); invp.write_bytes(raw)
  with a.open('xb') as out:
   z=subprocess.Popen(['zstd','-T1','-1','-q','-c'],stdin=subprocess.PIPE,stdout=out)
   with tarfile.open(fileobj=z.stdin,mode='w|') as tar:
    ti=tarfile.TarInfo('results/a.bin'); ti.size=len(payload); tar.addfile(ti,io.BytesIO(payload))
    ti=tarfile.TarInfo('results/b.bin'); ti.type=tarfile.LNKTYPE; ti.linkname='results/a.bin'; ti.size=0; tar.addfile(ti)
    ti=tarfile.TarInfo('metadata/inventory.json'); ti.size=len(raw); tar.addfile(ti,io.BytesIO(raw))
   z.stdin.close(); require(z.wait(timeout=5)==0,'self-test zstd failed')
  size,sha=file_digest(a); meta={'schema':'triad-fft-preservation-archive-v1','archive':a.name,'archive_size_bytes':size,
   'archive_sha256':sha,'inventory_sha256':hashlib.sha256(raw).hexdigest(),'payload_files':2,
   'logical_bytes':2*len(payload),'unique_content_bytes':len(payload),'hardlink_aliases':1}
  mp.write_text(json.dumps(meta)); result=verify(a,invp,mp,10)
  require(result['hardlink_aliases_verified']==1 and result['members_verified']==3,'hardlink verification self-test failed')
  return {'self_test':'passed','members':result['members_verified'],'hardlinks':result['hardlink_aliases_verified'],'archive_bytes':size}
def main():
 ap=argparse.ArgumentParser(); ap.add_argument('--archive',type=Path); ap.add_argument('--inventory',type=Path); ap.add_argument('--metadata',type=Path); ap.add_argument('--timeout',type=int,default=300); ap.add_argument('--self-test',action='store_true'); a=ap.parse_args()
 if a.self_test: print(json.dumps(self_test(),sort_keys=True)); return 0
 if not all((a.archive,a.inventory,a.metadata)): ap.error('archive, inventory, and metadata are required')
 print(json.dumps(verify(a.archive,a.inventory,a.metadata,a.timeout),indent=2)); return 0
if __name__=='__main__':
 try: raise SystemExit(main())
 except Exception as e: print(f'verification failed: {type(e).__name__}: {e}',file=__import__('sys').stderr); raise SystemExit(1)
