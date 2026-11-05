#!/usr/bin/env python3
"""Explicit experimental raw receiver adapter; source-bound Helper policy reuse.

`campaign` forwards to the maintained broad orchestrator with this factory.
`replay` reuses retained native controls, verifies the old exact mask/hash and
reports scores only. Neither mode monkeypatches a shared module or inherits a
filtered receiver's procedure identity.
"""
import argparse,json,hashlib,math,sys,time,tempfile
from pathlib import Path
import numpy as np
# Run from the repository root; no assumed installation depth.
REPO=Path.cwd()
if not (REPO/'tools/triad_helper_raw_receiver.py').is_file():raise RuntimeError('run from the matching repository root')
sys.path.insert(0,str(REPO/'tools'))
from triad_helper_study import HelperComparison,public_geometry,frequency_grid,digest
from triad_helper_raw_receiver import RawHelperDataReceiver
from triad_helper_assistance import tone_offsets


def clock_approximation(manifest):
    ch=manifest['channel'];margin=float(ch.get('spread_margin',1.))
    bound=float(ch.get('sample_clock_pair_ppm',ch.get('base_relative_pair_ppm_bound',ch['relative_pair_ppm_bound'])))*margin
    horizon=manifest['calendar']['capture_samples']/manifest['sample_rate_hz']
    phase=2*math.pi*manifest['format']['occupied_bandwidth_hz']*.5*horizon*bound*1e-6
    if not math.isfinite(bound) or bound<0 or bound>.001 or phase>.01:
        raise ValueError('raw prototype requires negligible public GPSDO audio-clock uncertainty (<=0.001ppm and <=0.01rad nominal edge displacement); free-crystal singleton is unsupported')
    return dict(public_pair_ppm_bound_after_margin=bound,nominal_edge_phase_displacement_bound=phase,
                policy='Explicit engineering approximation for tiny GPSDO drift; not a stochastic phase/coherence guarantee.')


class RawHelperComparison(HelperComparison):
    def __init__(self,fixture_directory,template_directory=None,*,device='cpu',precision='fp32',memory_limit_bytes=24*1024**3,
                 fft_backend='batched',frontend_profile='raw',clock_ppm_values=(0.,),doppler_scale_values=(1.,),
                 frequency_batch_size=8,max_cached_hypotheses=1,aligned_fft_grid=True,lag_step=1):
        start=time.perf_counter();folder=Path(fixture_directory);tfolder=Path(template_directory or folder)
        m=json.loads((folder/'manifest.json').read_text());tm=json.loads((tfolder/'manifest.json').read_text());self.manifest=m
        if m['schema']!='triad-helper-public-v1' or tm['schema']!=m['schema'] or m['format']['physical_rf_retuning']:raise ValueError('unsupported native raw fixture')
        approximation=clock_approximation(m)
        for key in ('format','key_seed','epoch','data_gain'):
            if public_geometry(m)[key]!=public_geometry(tm)[key]:raise ValueError('raw template source mismatch: '+key)
        if not tm['templates']['enabled']:raise ValueError('complete public raw templates required')
        self.clock_ppm_values=tuple(map(float,clock_ppm_values));self.doppler_scale_values=tuple(map(float,doppler_scale_values));self.fft_backend='batched'
        if fft_backend!='batched':raise ValueError('raw adapter requires batched FFT')
        meta=tm['templates'];path=tfolder/meta['file'];shape=tuple(meta['shape'])
        if path.stat().st_size!=math.prod(shape)*16:raise ValueError('raw public template file size mismatch')
        self.template_hash=digest(path);templates=np.memmap(path,mode='r',dtype='<c16',shape=shape)
        ch=m['channel'];c=m['calendar'];self.original_bound=ch['deterministic_cfo_bound_hz']*ch['spread_margin']
        self.frequencies=frequency_grid(self.original_bound,m['format']['bit_seconds'],max_points=4095)
        line=float(np.max(np.abs(tone_offsets(ch['spread_doppler_hz'],ch['selective_slow_depth'],ch['selective_fast_depth'],modulation_rate_hz=ch.get('selective_modulation_rate_hz')))))
        self.frontend_support=dict(profile='raw',pass_edge_hz=m['sample_rate_hz']/2,nominal_source_half_width_hz=m['format']['occupied_bandwidth_hz']*.5,
            maximum_selected_line_offset_hz=line,cfo_half_bound_hz=self.original_bound,
            scope='Original sampled capture; no lowpass or raw-mask support dilation. Native capture preflight remains required.',clock_approximation=approximation)
        valid=np.memmap(folder/m['validity']['file'],mode='r',dtype='u1')
        if valid.shape!=(c['capture_samples'],) or np.any(valid>1):raise ValueError('binary raw capture validity required')
        self.capture_validity=np.array(valid,copy=True);self.capture_validity.flags.writeable=False
        self.receiver=RawHelperDataReceiver(templates,meta['epoch_offsets_seconds'],source_offset_seconds=c['data_sample_offset']/m['sample_rate_hz'],
            capture_lead_seconds=c['capture_lead_seconds'],capture_rows=c['capture_samples'],arrival_bounds_seconds=c['arrival_prior_seconds'],
            delay_seconds=ch['spread_delay_ms']*.001,doppler_hz=ch['spread_doppler_hz'],reference_variance=m['power']['complex_noise_variance'],
            frequency_offsets=self.frequencies,precision=precision,device=device,memory_limit_bytes=memory_limit_bytes,
            lag_step=lag_step,frequency_batch_size=frequency_batch_size,max_cached_hypotheses=max_cached_hypotheses,
            clock_ppm_values=self.clock_ppm_values,doppler_scale_values=self.doppler_scale_values,capture_validity=self.capture_validity,
            source_half_bandwidth_hz=m['format']['occupied_bandwidth_hz']*.5,channel_line_half_width_hz=line)
        if aligned_fft_grid:
            self.frequencies=np.asarray(self.receiver.aligned_frequency_offsets(self.original_bound))
            self.receiver=self.receiver.with_hypotheses(frequency_offsets=self.frequencies)
        self.sources={name:digest(REPO/'tools'/name)for name in ('triad_helper_study.py','triad_helper_assistance.py','triad_helper_raw_receiver.py','triad_helper_receiver.py','triad_helper_fft.py')}
        self.sources['raw_study_adapter.py']=digest(__file__)
        identity=dict(geometry=public_geometry(m),receiver_id=self.receiver.receiver_id,template_sha256=self.template_hash,
            raw_noise_variance=m['power']['complex_noise_variance'],validity_sha256=hashlib.sha256(self.capture_validity.tobytes()).hexdigest(),
            source_hashes=self.sources,policy='raw18k-no-FIR-known-mask-Helper-v1',frontend_support=self.frontend_support,
            aligned_fft_grid=bool(aligned_fft_grid),clock_ppm_values=self.clock_ppm_values,doppler_scale_values=self.doppler_scale_values)
        self.procedure_id=hashlib.sha256(json.dumps(identity,sort_keys=True).encode()).hexdigest()
        self.shared_capture_id=hashlib.sha256(json.dumps(dict(geometry=public_geometry(m),preprocessing_id=self.receiver.preprocessing_id,
            frequencies=self.frequencies.tolist(),sources=self.sources,validity=identity['validity_sha256']),sort_keys=True).encode()).hexdigest()
        self.preparation_seconds=time.perf_counter()-start


def replay(argv):
    p=argparse.ArgumentParser();p.add_argument('--controls',type=Path,required=True);p.add_argument('--campaign',type=Path,required=True)
    p.add_argument('--cohort',default='eme-missed-10ms');p.add_argument('--trial',required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--device',default='cuda:0');p.add_argument('--memory-limit-bytes',type=int,default=24*1024**3)
    p.add_argument('--frequency-batch-size',type=int,default=8);p.add_argument('--max-cached-hypotheses',type=int,default=13)
    p.add_argument('--assisted-only',action='store_true');p.add_argument('--replays',type=int,default=1);a=p.parse_args(argv)
    if a.output.exists():p.error('output exists')
    if a.replays<1:p.error('positive replay count required')
    source=next(x for x in map(json.loads,(a.campaign/a.cohort/'trials.jsonl').read_text().splitlines())if x['trial']['id']==a.trial)
    m=json.loads((a.controls/'composite/manifest.json').read_text());path=a.controls/'composite'/m['observation']['file']
    if digest(path)!=source['erasure']['native']['observation_sha256']:raise ValueError('native input hash mismatch')
    raw=np.array(np.memmap(path,mode='r',dtype='<c16',shape=tuple(m['observation']['shape'])));valid=np.ones(len(raw),np.uint8)
    for lo,hi in source['erasure']['intervals_seconds']:valid[round(lo*m['sample_rate_hz']):round(hi*m['sample_rate_hz'])]=0
    raw[valid==0]=0
    if hashlib.sha256(raw.tobytes()).hexdigest()!=source['input_sha256']:raise ValueError('masked input hash mismatch')
    import torch
    torch.backends.cuda.matmul.allow_tf32=False;torch.set_float32_matmul_precision('highest');torch.set_num_threads(2)
    with tempfile.TemporaryDirectory(prefix='raw-replay-')as temporary:
        folder=Path(temporary);(folder/'manifest.json').write_text(json.dumps(m));raw.tofile(folder/m['observation']['file']);valid.tofile(folder/m['validity']['file'])
        comparison=RawHelperComparison(folder,a.controls/'data0',device=a.device,memory_limit_bytes=a.memory_limit_bytes,
            frequency_batch_size=a.frequency_batch_size,max_cached_hypotheses=a.max_cached_hypotheses)
        results=[comparison.score(folder,include_baseline=not a.assisted_only)for _ in range(a.replays)]
    truth=json.loads((a.controls/'composite/evaluator-truth.json').read_text())
    report=dict(schema='triad-helper-raw-exact-replay-v1',true_bit=truth['source_bit'],results=results,
        procedure_id=comparison.procedure_id,source_hashes=comparison.sources,
        scope='Exact original masked observations; raw rankings only. Replays share a workspace and are not independent receptions; fresh matching calibration required for admissions.')
    with a.output.open('x')as stream:json.dump(report,stream,indent=2,allow_nan=False);stream.write('\n')
    print(json.dumps(dict(output=str(a.output),bits=[{arm:result[arm]['bit']for arm in ('baseline','assisted')if result.get(arm)is not None}for result in results])))


def main():
    if len(sys.argv)<2:raise SystemExit('use replay or campaign mode')
    if sys.argv[1]=='replay':replay(sys.argv[2:])
    elif sys.argv[1]=='campaign':
        import triad_helper_broad_campaign as broad
        frozen={name:digest(REPO/'tools'/name)for name in ('triad_helper_raw_receiver.py','triad_helper_receiver.py','triad_helper_fft.py')};frozen['adapter']=digest(__file__)
        def factory(*args,**kwargs):
            current={name:digest(REPO/'tools'/name)for name in frozen if name!='adapter'};current['adapter']=digest(__file__)
            if current!=frozen:raise ValueError('raw adapter/receiver source changed after freeze')
            return RawHelperComparison(*args,**kwargs)
        status=broad.main(sys.argv[2:],comparison_factory=factory)
        output=Path(sys.argv[sys.argv.index('--output')+1]);broad.write_json(output/'raw-adapter-source-bindings.json',frozen)
        raise SystemExit(status)
    else:raise SystemExit('unknown mode')


if __name__=='__main__':main()
