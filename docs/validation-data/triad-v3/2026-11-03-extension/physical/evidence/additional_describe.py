#!/usr/bin/env python3
"""Describe the separate four-plan schedule/spreading package; no receiver work."""
import argparse,hashlib,json,math,statistics
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--input',type=Path,required=True,help='full JSON from analyze_physical.py')
p.add_argument('--reports',type=Path,required=True,help='additional-physical-reports directory')
p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();d=json.loads(a.input.read_text());a.output_dir.mkdir(parents=True,exist_ok=True)
if d['counts']!={'plans':4,'captures':60,'variant_method_scores':180,'original_failed_acquisitions':0}:raise ValueError('Requires the original four additional plans,60 captures and180 completed scores.')
def sha(f):return hashlib.sha256(f.read_bytes()).hexdigest()
def stats(v):return dict(min=min(v),median=statistics.median(v),max=max(v),mean=statistics.mean(v))
geometry=[];extras={};source=Path(__file__).resolve().parent
for planfile in sorted(a.reports.glob('*-plan.json')):
 plan=json.loads(planfile.read_text());name=planfile.name[:-10]
 receiptfile=a.reports/(name+'-export')/'results.json';ex=json.loads(receiptfile.read_text())
 for cell in plan['cells']:
  jobs=[j for j in ex['jobs'] if j['case']==cell['name']];ms=[]
  for j in jobs:
   mf=a.reports/name/'workspaces'/j['id']/'manifest.json';ms.append(json.loads(mf.read_text()));extras[str(mf.relative_to(a.reports))]=sha(mf)
  rows=[r for r in d['records'] if r['plan']==name and r['case']==cell['name'] and r['gate']=='strict']
  methods={}
  for method in ('window','global','global-frequency'):
   rs=[r for r in rows if r['method']==method]
   methods[method]={'base_groups':sorted({r['base_groups'] for r in rs}),'effective_groups':sorted({r['effective_groups'] for r in rs}),'window_seconds':sorted({r['window_seconds'] for r in rs}),'receiver_seconds':stats([r['receiver_seconds'] for r in rs]),'phase_loss_allowance_db':sorted({r['phase_loss_allowance_db'] for r in rs}),'phase_loss_bound_db':stats([r['phase_loss_bound_db'] for r in rs])}
  g={'plan':name,'case':cell['name'],'suite':plan['suite'],'cn0_db_hz':cell['cn0_db_hz'],'rate_hz':cell['rate_hz'],'added_symbols':cell['added_symbols'],'fine_symbols_per_carrier_hz':cell['rate_hz']*cell['added_symbols']/6,'requested_seconds':cell['requested_seconds'],'source_elapsed_seconds':sorted({m['actual_elapsed_seconds'] for m in ms}),'sampled_seconds':sorted({m['sample_count']/m['sample_rate'] for m in ms}),'sample_rates':sorted({m['sample_rate'] for m in ms}),'sample_counts':sorted({m['sample_count'] for m in ms}),'block_samples':sorted({m['block_samples'] for m in ms}),'block_counts':sorted({m['block_count'] for m in ms}),'export_seconds':stats([j['elapsed_seconds'] for j in jobs]),'export_bytes':stats([j['evaluation_only']['export_bytes'] for j in jobs]),'export_stage_concurrent_jobs':ex['concurrent_jobs'],'export_stage_elapsed_seconds':ex['elapsed_seconds'],'methods':methods}
  geometry.append(g)
wrong=[r for r in d['records'] if r['outcomes'].get('wrong_admitted') or r['outcomes'].get('false_admitted')];assert not wrong
summary={k:d[k] for k in ('counts','summary','captures','same_seed_across_cn0','benchmark','original_failures')}
summary.update(schema='triad-extension-additional-physical-summary-v1',scope='Separate four-plan,60-capture continuation. First nine-plan record is unchanged; no ordinary receiver integration.',full_analysis_file=a.input.name,full_analysis_sha256=sha(a.input),generator_sha256={f:sha(source/f) for f in ('analyze_physical.py','additional_describe.py')},input_plan_and_receipt_sha256={n:h for n,h in d['provenance_sha256'].items() if n.endswith('-plan.json') or n.endswith('/results.json')},geometry_and_cost=geometry,manifest_sha256=extras,exceptional_captures=[r for r in d['records'] if r['control']=='signal' and r['gate']=='strict' and r['method']=='global-frequency' and r['outcomes']['missed']],limitations=['All results refer to the explicitly sampled channel, fixed timing/CFO banks and strict/relaxed gates; clock-rate and epoch alternatives remain unsearched.','Six original execution failures belong to the first nine-plan package; this separate package has no failed jobs. Every method here retains0.01dB original-block phase-loss allowance.','Each independent signal cell has16 paths; repeated methods share observations. The paired−23/−16 arms reuse keys/channel/noise seeds and energy, and are not independent validations.','No null or wrong-key admissions in these small control sets establishes neither a rare operational rate nor general structured-interference rejection.','Raw-statistics bytes are omitted from the compact input package. Available JSON/NPZ and receipts were re-read/hashed; original raw hashes were retained, not independently checked here.','Genie presence detection assumes known complex waveform/channel/bit/synchronization/noise. Reported energy margins are not measured implementation dB gaps or guaranteed duration factors.'])
(a.output_dir/'additional-summary.json').write_text(json.dumps(summary,sort_keys=True,indent=2,allow_nan=False)+'\n')
print(json.dumps({'counts':d['counts'],'summary_sha256':sha(a.output_dir/'additional-summary.json')}))
