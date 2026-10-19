#!/usr/bin/env python3
"""Strict additive reader; invokes unchanged A4 semantic oracle separately."""
from __future__ import annotations
import argparse,dataclasses,hashlib,importlib.util,json,math
from pathlib import Path
ROOT=Path(__file__).resolve().parent
PREFIX='fixed_prefix_storage_'
NATIVE_REQUIRED=set('history peaks points ring work spectrum product reference energy_prefix packed_phases drift_scratch differential_scratch drift_scores templates template_energy template_square cached_templates scoring tracking_reference completed bursts tracks latest.bits'.split())
def require(ok,message):
 if not ok:raise ValueError(message)
def unique(pairs):
 out={}
 for k,v in pairs:
  require(k not in out,'duplicate JSON field '+k);out[k]=v
 return out
def load(path):return json.loads(Path(path).read_text(),object_pairs_hook=unique)
def integer(row,key):
 v=row[key];require(type(v) is int and v>=0,'unsigned integer '+key);return v
def flag(row,key):
 v=row[key];require(type(v) in (int,bool) and v in (0,1),'boolean '+key);return bool(v)
@dataclasses.dataclass(frozen=True)
class Component:
 owner:int;name:str;index:int;size:int;capacity:int;element:int;group:int|None
 @classmethod
 def parse(cls,r):
  o=cls(integer(r,'owner'),r['component'],integer(r,'index'),integer(r,'size'),integer(r,'capacity'),integer(r,'element_bytes'),integer(r,'exact_group') if flag(r,'equality_available') else None)
  require(o.size<=o.capacity and o.element>0,'component extent');require(r['size_bytes']==o.size*o.element and r['capacity_bytes']==o.capacity*o.element,'component byte equation')
  require(flag(r,'empty')==(o.size==0) and flag(r,'allocated')==(o.capacity>0),'empty versus allocated distinction')
  require(o.group is None or o.group>0,'nonzero exact group');return o
def read(trace,seal,source_manifest,original_reader,oracle,manifest=None):
 m=manifest or load(ROOT/'manifest.json');source=load(source_manifest);actual=source['compiled_overlay_sha256']
 require(hashlib.sha256(Path(original_reader).read_bytes()).hexdigest()==m['a4_reader_sha256'],'original A4 reader source hash')
 require(hashlib.sha256(Path(oracle).read_bytes()).hexdigest()==m['a3_oracle_sha256'],'original A3 oracle hash')
 # Require the complete source/include read closure and all changed postimages.
 expected={**m['read_pins'],**m['postimages']}
 for name,digest in expected.items():require(actual.get(name)==digest,'compiled source missing/mismatched '+name)
 rows=[]
 with Path(trace).open() as f:
  for i,line in enumerate(f,1):
   require(line.endswith('\n'),'truncated trace line');require(len(line.encode())<=16384,'oversize trace record')
   row=json.loads(line,object_pairs_hook=unique);require(type(row) is dict,'trace record object');rows.append(row)
 require(rows and rows[0]['event']=='trace_begin' and rows[-1]['event']=='trace_end','trace boundary/footer')
 require(sum(r['event']=='trace_begin' for r in rows)==1 and sum(r['event']=='trace_end' for r in rows)==1,'one trace scope')
 token=rows[0]['token'];end=rows[-1];require(end['token']==token,'footer token')
 require(not flag(end,'size_limited') and integer(end,'dropped')==0 and not flag(end,'prior_io_failed'),'trace footer complete')
 sequences=[integer(r,'sequence') for r in rows[:-1]];require(sorted(sequences)==list(range(1,integer(end,'attempted')+1)),'trace sequence completeness')
 seals=[]
 for line in Path(seal).read_text().splitlines():
  if line.startswith('{'):
   try:r=json.loads(line,object_pairs_hook=unique)
   except ValueError:continue
   if r.get('event')=='trace_finalization' and r.get('token')==token:seals.append(r)
 require(len(seals)==1,'exactly one matching external seal');s=seals[0]
 require(s.get('result')=='complete' and flag(s,'flush_ok') and flag(s,'close_ok') and not flag(s,'prior_io_failed') and not flag(s,'size_limited') and integer(s,'dropped')==0,'external seal complete')
 require(s['attempted']==end['attempted'],'seal attempt count')
 new=[r for r in rows if r['event'].startswith(PREFIX)];require(new,'storage events missing/filtered')
 require(all(r.get('storage_schema')==1 for r in new),'storage schema')
 allowed={'begin','summary','owner','state','track','component','diagnostics','content','content_state','caller'}
 require(all(r['event'][len(PREFIX):] in allowed for r in new),'unknown/incomplete storage event')
 def one(suffix):
  a=[r for r in new if r['event']==PREFIX+suffix];require(len(a)==1,'one '+suffix);return a[0]
 b=one('begin');t=one('summary');require(new[0] is b and new[-1] is t,'census terminal ordering')
 require(b['overlay_identity']==m['overlay_identity'],'trace overlay identity')
 require(b['sample']==b['expected_sample']==154038 and b['whole_partitions']==233 and b['unfed_suffix_samples']==638,'fixed prefix boundary')
 require(not flag(b,'eof_injected') and not flag(b,'absence_injected'),'no physical-end injection')
 require(b['owners']==269 and b['stored']==166926318 and b['recomputed']==b['stored'],'A3 frozen end ledger')
 require(integer(b,'dsp_budget')==536870912 and b['stored']<=integer(b,'bank_budget')<=b['dsp_budget'],'configured fixture/bank budgets')
 require(b['ledger_sample']==b['sample'] and b['ledger_owners']==b['owners'] and flag(b,'journal_complete'),'current ledger and journal')
 require(integer(b,'observer_owned_bytes')<=integer(b,'observer_extra_limit_bytes')<=64*1024**2,'observer bounded storage')
 shared=sum(integer(b,k) for k in ('base','shared_capture','deferred','source','exact_samples','exact_points','permutations','temporal'))
 require(shared+integer(b,'receiver_sum')==b['stored'],'bank ledger equation')
 owners=[r for r in new if r['event']==PREFIX+'owner'];states=[r for r in new if r['event']==PREFIX+'state'];tracks=[r for r in new if r['event']==PREFIX+'track'];parts=[Component.parse(r) for r in new if r['event']==PREFIX+'component']
 require({r['owner'] for r in owners}==set(range(269)) and len(owners)==269,'owner identity/count')
 require({r['owner'] for r in states}==set(range(269)) and len(states)==269,'native state identity/count')
 require(all(flag(r,'native_available') for r in states),'unavailable native backend')
 require(sum(integer(r,'charge') for r in owners)==b['receiver_sum']==t['receiver_sum'],'per-owner ledger reconciliation')
 for r in owners:
  require(sum(integer(r,k) for k in ('modem','control','content','burst_struct','burst_bits','history_ordinals','presentation_estimate','source_family'))==r['charge'],'owner ledger equation')
  require(flag(r,'native_inspected') and flag(r,'journal_match') and flag(r,'journal_current'),'owner inspection/retention provenance')
  require(not flag(r,'decision_retire'),'retired owner retained')
  require(not flag(r,'content_present') or flag(r,'content_inspected'),'missing content inspection')
 keys={(p.owner,p.name,p.index) for p in parts};require(len(keys)==len(parts),'unique component rows')
 for r in states:
  owner=r['owner'];have={p.name.removeprefix('native.') for p in parts if p.owner==owner and p.name.startswith('native.')}
  require(NATIVE_REQUIRED<=have,'native inventory coverage '+str(owner))
  require(sum(x['owner']==owner for x in tracks)==r['tracks'],'track inventory count')
  for i in range(r['tracks']):require((owner,'native.track.bits',i) in keys and (owner,'native.track.frequency_scores',i) in keys,'track nested vectors')
  o=next(x for x in owners if x['owner']==owner)
  if o['content_present']:
   c=[x for x in new if x['event']==PREFIX+'content' and x['owner']==owner];require(len(c)==1,'content summary')
   for i in range(c[0]['states']):
    require(sum(x['event']==PREFIX+'content_state' and x['owner']==owner and x['index']==i for x in new)==1,'content state inventory')
    require(sum(x['event']==PREFIX+'diagnostics' and x['owner']==owner and x['index']==i for x in new)==1,'retained Diagnostics inventory')
    for name in ('diagnostics.constellation','diagnostics.waveform','content.raw_bits','content.source','content.recovery_bits','content.recovery_starts','content.message.data','content.error'):
     require((owner,name,i) in keys,'content component inventory')
 require(t['owners']==t['owner_total']==269 and t['states']==269 and t['tracks']==len(tracks) and t['components']==len(parts),'summary inventory')
 require(t['rows_before_summary']==len(new)-1,'census row completeness')
 require(not flag(t,'limited') and not flag(t,'queue_timeout') and integer(t,'errors')==0,'observer limited/errors')
 require(flag(t,'cpu_valid'),'observer calling thread CPU unavailable')
 for k in ('calling_thread_cpu_seconds','monotonic_wall_seconds'):require(type(t[k]) in (int,float) and math.isfinite(t[k]) and t[k]>=0,'observer clock')
 # Existing A4 behavior/work/health/selection/physical-output oracle remains authoritative.
 spec=importlib.util.spec_from_file_location('original_a4_reader',original_reader);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 original=module.semantic_readback(Path(trace),Path(oracle));require(original['passed'],'unchanged original A4 semantic gate failed: '+str(original.get('errors')))
 groups={}
 for p in parts:
  if p.group is not None:
   key=p.group;old=groups.setdefault(key,(p.size,p.element));require(old==(p.size,p.element),'group component extent mismatch')
 aggregate={}
 for p in parts:
  a=aggregate.setdefault(p.name,dict(arrays=0,empty_arrays=0,allocated_empty_arrays=0,size_bytes=0,capacity_bytes=0,exact_groups=set(),equality_unavailable=0))
  a['arrays']+=1;a['empty_arrays']+=p.size==0;a['allocated_empty_arrays']+=p.size==0 and p.capacity>0;a['size_bytes']+=p.size*p.element;a['capacity_bytes']+=p.capacity*p.element
  if p.group is None:a['equality_unavailable']+=1
  else:a['exact_groups'].add(p.group)
 for a in aggregate.values():a['exact_groups']=len(a['exact_groups'])
 return dict(status='passed',diagnostic_only=True,trace_sha256=hashlib.sha256(Path(trace).read_bytes()).hexdigest(),source_manifest_sha256=hashlib.sha256(Path(source_manifest).read_bytes()).hexdigest(),overlay_identity=m['overlay_identity'],owners=269,components=len(parts),global_exact_groups=len(groups),aggregate=aggregate,original_a4=original,observer=dict(calling_thread_cpu_seconds=t['calling_thread_cpu_seconds'],monotonic_wall_seconds=t['monotonic_wall_seconds'],cpu_nonadditive=True),limits=['exact content groups do not establish safe owner/track merging','capacity totals include nested object and SSO representations; not allocator/RSS accounting','unavailable internal/caller components remain explicitly unavailable'])
def main():
 p=argparse.ArgumentParser();p.add_argument('--trace',required=True);p.add_argument('--seal',required=True);p.add_argument('--source-manifest',required=True);p.add_argument('--original-reader',required=True);p.add_argument('--oracle',required=True);a=p.parse_args()
 print(json.dumps(read(a.trace,a.seal,a.source_manifest,a.original_reader,a.oracle),indent=2))
if __name__=='__main__':main()
