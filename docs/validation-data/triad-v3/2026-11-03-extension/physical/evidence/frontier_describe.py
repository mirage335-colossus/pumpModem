#!/usr/bin/env python3
"""Bind two selected SNR followups to the prior same-cohort -16dB-Hz record."""
import argparse,hashlib,json,statistics
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--input',type=Path,required=True,help='full JSON from analyze_physical.py')
p.add_argument('--reports',type=Path,required=True)
p.add_argument('--prior',type=Path,required=True,help='unchanged additional-summary.json containing -16dB-Hz paths')
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();d=json.loads(a.input.read_text());prior=json.loads(a.prior.read_text());scripts=Path(__file__).resolve().parent
if d['counts']!={'plans':2,'captures':36,'variant_method_scores':108,'original_failed_acquisitions':0}:raise ValueError('Requires the original two-plan frontier package.')
def sha(f):return hashlib.sha256(f.read_bytes()).hexdigest()
def stats(v):return dict(min=min(v),median=statistics.median(v),max=max(v),mean=statistics.mean(v))
old={r['id']:r for r in prior['captures'] if r['plan']=='maxspread-validation-m16'}
comparisons=[]
for cap in d['captures']:
 other=old[cap['id']]
 for field in ('seeds','energy_seconds','elapsed_source_seconds','sampled_seconds','fine_rate_hz','control','bit'):assert cap[field]==other[field],(cap['id'],field)
 comparisons.append(dict(id=cap['id'],cn0_db_hz=cap['cn0_db_hz'],all_seed_arguments_equal=True,signal_energy_equal=True,source_and_sampled_calendars_equal=True))
assert not any(r['outcomes'].get('wrong_admitted') or r['outcomes'].get('false_admitted') for r in d['records'])
cost=[];geometry=[]
for planfile in sorted(a.reports.glob('*-plan.json')):
 name=planfile.name[:-10];ex=json.loads((a.reports/(name+'-export/results.json')).read_text());sc=json.loads((a.reports/(name+'-rescore-run/results.json')).read_text())
 cost.append(dict(plan=name,export_job_seconds=stats([j['elapsed_seconds'] for j in ex['jobs']]),export_bytes=stats([j['evaluation_only']['export_bytes'] for j in ex['jobs']]),export_stage_seconds=ex['elapsed_seconds'],export_concurrent_jobs=ex['concurrent_jobs'],score_stage_seconds=sc['elapsed_seconds'],score_concurrent_jobs=sc['concurrent_jobs']))
 for method in ('window','global','global-frequency'):
  rs=[r for r in d['records'] if r['plan']==name and r['method']==method and r['gate']=='strict']
  geometry.append(dict(plan=name,method=method,base_groups=sorted({r['base_groups'] for r in rs}),effective_groups=sorted({r['effective_groups'] for r in rs}),window_seconds=sorted({r['window_seconds'] for r in rs}),allowed_phase_loss_db=sorted({r['phase_loss_allowance_db'] for r in rs}),phase_loss_bound_db=stats([r['phase_loss_bound_db'] for r in rs]),receiver_seconds=stats([r['receiver_seconds'] for r in rs]),signal_raw_bit_errors=sum(r['scores'].index(max(r['scores']))!=r['bit'] for r in rs if r['control']=='signal')))
transitions=[]
for gate in ('strict','relaxed'):
 accepted={cn:{r['capture'] for r in d['records'] if r['cn0_db_hz']==cn and r['method']=='global-frequency' and r['gate']==gate and r['outcomes'].get('correct_admitted')} for cn in (-20,-18)}
 assert accepted[-20]<=accepted[-18]
 baseline=next(s for s in prior['summary'] if s['plan']=='maxspread-validation-m16' and s['method']=='global-frequency' and s['gate']==gate)
 assert baseline['counts']['signal']['correct_admitted']==16
 transitions.append(dict(gate=gate,correct_at_minus20=len(accepted[-20]),additional_correct_at_minus18=len(accepted[-18]-accepted[-20]),additional_correct_at_minus16=16-len(accepted[-18]),lost_between_minus20_and_minus18=[]))
out={k:d[k] for k in ('counts','summary','captures','same_seed_across_cn0','benchmark','original_failures','provenance_sha256')}
out.update(schema='triad-extension-frontier-summary-v1',scope='Selected followup to the prior -16dB-Hz result; the same16 validation paths are reused at -20/-18,not a new independent cohort.',full_analysis_file=a.input.name,full_analysis_sha256=sha(a.input),prior_summary_file=a.prior.name,prior_summary_sha256=sha(a.prior),generator_sha256={f:sha(scripts/f) for f in ('analyze_physical.py','frontier_describe.py')},same_cohort_prior_checks=comparisons,geometry_and_timing=geometry,export_and_stage_cost=cost,paired_admission_transitions=transitions,prior_minus16_cell_summaries=[s for s in prior['summary'] if s['plan']=='maxspread-validation-m16'],cfo_signal_scores=[{k:r[k] for k in ('capture','cn0_db_hz','gate','bit','scores','outcomes','threshold','rho','genie_presence_pd')} for r in d['records'] if r['control']=='signal' and r['method']=='global-frequency'],limitations=['No failed jobs,wrong admissions,null admissions or wrong-key admissions in this selected package; controls are small and reused across SNR.','Same16 physical/key/noise seed roots and signal energies as the prior -16 cohort. New SNR points were selected after that result; no independent or simultaneous confidence qualification is claimed.','Pointwise Clopper-Pearson endpoints assume independent/common-probability Bernoulli trials within one chosen cell; selection and repeated SNR reuse prevent treating them as joint coverage.','All methods retain0.01dB phase-loss allowance. Historical six execution failures and microwave0.02dB arm belong to the first nine-plan package,not this package.','Known-waveform presence benchmarks are conditional favorable references,not unknown-channel reception guarantees or measured duration scaling.','Raw grouped statistics were omitted from this compact package. Available score JSON/NPZ and manifests were checked; raw hashes remain original receipt claims.','No -60dB-Hz or multi-day claim,ordinary-receiver integration,new receiver code or further campaign is included.'])
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(out,indent=2,sort_keys=True,allow_nan=False)+'\n')
print(json.dumps(dict(counts=d['counts'],paired_transitions=transitions,summary_sha256=sha(a.output))))
