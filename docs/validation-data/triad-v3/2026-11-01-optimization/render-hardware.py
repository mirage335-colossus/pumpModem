#!/usr/bin/env python3
"""Render conditional hardware comparisons from the preserved raw evidence."""
import argparse, hashlib, json, pathlib, statistics
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=argparse.ArgumentParser();p.add_argument('--evidence',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
sources={}
def read(path):
 path=a.evidence/path;sources[str(path.relative_to(a.evidence))]=hashlib.sha256(path.read_bytes()).hexdigest();return json.loads(path.read_text())
def median(rows,key):return statistics.median(key(r) for r in rows)
latency=[]
for folder,label in [('gpu','5090'),('pro6000','pro6000'),('mig24','mig24')]:
 for b in (1,8,32):
  rows=[read(pathlib.Path(folder)/'results'/f'{label}-cached-hour-graph-b{b}-r{r}.json') for r in range(3)]
  pipeline=median(rows,lambda d:d['timing_seconds']['candidate_scoring_work']['streamed_warm_pipeline_seconds']);samples=rows[0]['workload']['source_sample_count'];duration=rows[0]['workload']['physical_observation_seconds']
  latency.append({'device':label,'batch':b,'median_single_capture_warm_resident_seconds':median(rows,lambda d:d['timing_seconds']['candidate_scoring_work']['single_capture_latency']['pretransferred_resident_seconds']) if b==1 else None,'median_single_capture_warm_streamed_seconds':median(rows,lambda d:d['timing_seconds']['candidate_scoring_work']['single_capture_latency']['streamed_host_block_seconds']) if b==1 else None,'median_single_capture_cold_seconds':median(rows,lambda d:d['timing_seconds']['signal_cold_first_use_capture']['end_to_end_full_first_capture_seconds']) if b==1 else None,'paired_streaming_pipeline_seconds':pipeline,'represented_samples_per_second_streaming_pipeline':2*b*samples/pipeline,'represented_real_time_factor':2*b*duration/pipeline,'scope':'already-exported sufficient statistics; includes simulated observation preparation and streaming host transfers; excludes physical waveform generation, raw sampled-IQ acquisition, calibration and subprocess launch'})
equal=read(pathlib.Path('gpu/results/equal-work-analysis.json'))
scaling=read(pathlib.Path('pro6000/results/scaling-analysis.json'))
screen=[]
for folder,label,bs in [('pro6000','pro6000',(4,8,12,16)),('mig24','mig24',(1,2,4,8))]:
 for b in bs:
  d=read(pathlib.Path(folder)/'results'/f'{label}-screen-graph-fp64-b{b}.json');t=d['timing'];screen.append({'device':label,'batch':b,'observations':d['workload']['paired_observations_scored'],'pipeline_observations_per_second':t['paired_observations_per_second_complete_processing'],'inference_observations_per_second':t['paired_observations_per_second_inference'],'stages':t})
summary={'scope':'conditional screening only; no qualified operational throughput/cost','source_sha256':sources,'latency_streaming':latency,'equal_work':equal,'multi_gpu':scaling,'batch_screen':screen}
(a.output/'hardware-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
with plt.rc_context({'font.size':10,'svg.hashsalt':'triad-hardware-v1'}):
 fig,ax=plt.subplots(figsize=(9,5),layout='constrained');names=['Reference B4','Graph + FP64 sum B4','Graph + FP64 sum B12'];xs=list(range(3));width=.24
 for offset,(key,label) in enumerate([('inference_seconds','Inference'),('processing_seconds','Processing pipeline'),('parent_job_seconds','Complete subprocess job')]):
  ax.bar([x+(offset-1)*width for x in xs],[r[key] for r in equal['rows']],width,label=label)
 ax.set_xticks(xs,names);ax.set_ylabel('Seconds for 192 scored H0/H1 observations');ax.set_title('RTX 5090: equal-work medians, three fresh-noise runs');ax.legend();ax.grid(axis='y',alpha=.2)
 fig.savefig(a.output/'equal-work.svg',metadata={'Date':None});fig.savefig(a.output/'equal-work.png',dpi=150);plt.close(fig)
 fig,ax=plt.subplots(figsize=(9,5),layout='constrained')
 for device in ('pro6000','mig24'):
  rows=[r for r in screen if r['device']==device];ax.plot([r['batch'] for r in rows],[r['pipeline_observations_per_second'] for r in rows],'o-',label=device)
 ax.set(xlabel='Independent noise trials per batch',ylabel='Scored H0/H1 observations per second',title='Combined graph + FP64 sum: eight-batch conditional campaigns');ax.legend();ax.grid(alpha=.2)
 fig.savefig(a.output/'hardware-batches.svg',metadata={'Date':None});fig.savefig(a.output/'hardware-batches.png',dpi=150);plt.close(fig)
print(json.dumps({'sources':len(sources),'latency':latency},indent=2))
