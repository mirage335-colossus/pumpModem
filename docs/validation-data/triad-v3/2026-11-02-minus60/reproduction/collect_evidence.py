#!/usr/bin/env python3
"""Collect only experiment scientific roots; hardlink arrays for on-host audit.
Provider identifiers, account data and source-tree historical reports excluded.
"""
import pathlib, shutil, os, argparse, tarfile, hashlib, json
p=argparse.ArgumentParser();p.add_argument('--workspace',type=pathlib.Path,required=True);p.add_argument('--destination',type=pathlib.Path,required=True);p.add_argument('--plans',type=pathlib.Path,required=True);p.add_argument('--archive',type=pathlib.Path)
a=p.parse_args()
a.workspace=a.workspace.resolve();a.destination=a.destination.resolve();a.plans=a.plans.resolve()
if a.destination==a.workspace:raise ValueError('destination must be separate from workspace')
if a.plans==a.destination/'reproduction' or a.plans.is_relative_to(a.destination) or a.destination.is_relative_to(a.plans):raise ValueError('plan source must be outside destination')
for source in a.workspace.glob('minus60-*'):
 if source.is_dir() and source.resolve()!=a.destination and (a.destination.is_relative_to(source.resolve()) or source.resolve().is_relative_to(a.destination)):
  raise ValueError('source/destination overlap')
if a.archive:
 selected=set(); extra=set(); weakest={}; oversized=set(); workspace_sizes={}
 for report in a.destination.rglob('*.json'):
  if 'scores' not in str(report.parent) and report.parent.name not in ('minus60-static','minus60-grid','minus60-rbpf','minus60-ray'):continue
  d=json.loads(report.read_text()); candidates=[]
  for c in d.get('captures',[]):
   if c.get('status')!='completed':continue
   methods=c.get('methods',{}); truth=c.get('truth_bit_simulation_only')
   if truth not in (0,1):continue
   scores=[v.get('bit_log_scores',[float('inf')]*2)[truth] for v in methods.values()]
   if scores:candidates.append((min(scores),c['input']))
   if 'minus60-holdout/' in c['input'] and any(v.get('counts',{}).get('missed',0) or v.get('counts',{}).get('wrong_accepted',0) for v in methods.values()):extra.add(c['input'])
  for value,capture in candidates:
   cohort=str(pathlib.Path(capture).parent)
   if cohort not in weakest or value<weakest[cohort][0]:weakest[cohort]=(value,capture)
 extra.update(capture for _,capture in weakest.values())
 extra={str(pathlib.Path(x).relative_to(a.workspace)) for x in extra}
 for d in a.destination.glob('minus60-*'):
  if not d.is_dir():continue
  for f in d.rglob('*'):
   if not f.is_file():continue
   raw_selected=str(f.parent.relative_to(a.destination)) in extra or f.parent.name=='path-00000' or (d.name=='minus60-holdout' and f.parent.name=='path-00001')
   if raw_selected:
    if f.parent not in workspace_sizes:workspace_sizes[f.parent]=sum(x.stat().st_size for x in f.parent.iterdir() if x.is_file())
    if workspace_sizes[f.parent]>16*1024*1024:oversized.add(str(f.parent.relative_to(a.destination)));raw_selected=False
   if f.suffix in ('.json','.npz','.log') or raw_selected:selected.add(f)
 for f in (a.destination/'reproduction').glob('*'):
  if f.is_file():selected.add(f)
 for name in ('analysis-full.json','build-validation.log','environment.json'):
  f=a.destination/name
  if f.is_file():selected.add(f)
 inventory=[]
 with tarfile.open(a.archive,'w:gz',compresslevel=6) as t:
  for f in sorted(selected):
   rel=str(f.relative_to(a.destination));t.add(f,arcname=rel,recursive=False)
   inventory.append({'path':rel,'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()})
 a.archive.with_suffix('.inventory.json').write_text(json.dumps({'scope':'All scientific JSON, score NPZ, logs; first raw workspace per case, second per holdout, minimum-score workspace per cohort and holdout misses. Selected workspaces above16MiB are metadata-only. Other arrays intentionally omitted; full on-host analysis verifies before pruning.','oversized_workspace_arrays_omitted':sorted(oversized),'files':inventory},indent=2)+'\n')
 print(json.dumps({'files':len(inventory),'archive_bytes':a.archive.stat().st_size}))
else:
 a.destination.mkdir(parents=True,exist_ok=True)
 for d in sorted(a.workspace.glob('minus60-*')):
  if not d.is_dir() or d.resolve()==a.destination.resolve():continue
  shutil.copytree(d,a.destination/d.name,copy_function=os.link,dirs_exist_ok=True)
 shutil.copytree(a.plans,a.destination/'reproduction',dirs_exist_ok=True)
