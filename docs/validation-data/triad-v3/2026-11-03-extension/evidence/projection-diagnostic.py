#!/usr/bin/env python3
"""Evaluation-only clean-signal projection audit of an already scored public bank.

Never supplies simulator means/truth to receiver decisions. No new hypothesis is
created: this diagnoses the representation used by an existing actual-noisy
score report. Optional segmented fits are oracle channel-flexibility diagnostics,
not attainable noisy receiver performance or additional calibrated receivers.
"""
import argparse, hashlib, json, math, os, resource, sys, time
from pathlib import Path
for variable in ('OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS'):
    os.environ[variable]='1'
import numpy as np
EXPECTED_SCORER='1e060eb53a411513598a01bd84cc204133056cabdab366c255b66d6b6123b104'
RANK_TOLERANCE=1e-10

def sha(path):
    digest=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):digest.update(block)
    return digest.hexdigest()

def projected_energy(G,r):
    """Stable observable-eigenspace whitening; never invert a Gram matrix."""
    if not np.isfinite(G).all() or not np.isfinite(r).all():raise ValueError('nonfinite statistics')
    hermitian=(G+G.conj().swapaxes(-1,-2))*.5
    values,vectors=np.linalg.eigh(hermitian)
    scale=np.maximum(values[...,-1],0.)
    if np.any(values[...,0] < -RANK_TOLERANCE*np.maximum(scale,np.finfo(float).tiny)):
        raise ValueError('materially indefinite Gram matrix')
    keep=values>RANK_TOLERANCE*scale[...,None]
    coordinates=np.einsum('...ji,...j->...i',vectors.conj(),r)
    whitened=np.divide(coordinates,np.sqrt(np.maximum(values,0.)),out=np.zeros_like(coordinates),where=keep)
    energy=np.sum(abs(whitened)**2,axis=-1)
    discarded=np.sum(np.where(keep,0.,np.maximum(values,0.)),axis=-1)
    total=np.sum(np.maximum(values,0.),axis=-1)
    fraction=np.divide(discarded,total,out=np.zeros_like(total),where=total>0)
    return energy,keep.sum(axis=-1),fraction

def self_test():
    G=np.array([np.eye(2),np.diag([2.,0.]),np.zeros((2,2))],dtype=np.complex128)
    r=np.array([[1+2j,3-1j],[4,0],[0,0]],dtype=np.complex128)
    e,rank,_=projected_energy(G,r)
    np.testing.assert_allclose(e,[15,8,0],rtol=1e-14,atol=1e-14)
    np.testing.assert_array_equal(rank,[2,1,0])
    print('identity, rank-deficient and zero projection sanity passed')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tools-root',type=Path)
    parser.add_argument('--workspace',type=Path)
    parser.add_argument('--score-report',type=Path)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--window-blocks',type=int,default=0,help='optional oracle/free-gain segmented diagnostic; 0 disables')
    parser.add_argument('--self-test',action='store_true')
    args=parser.parse_args()
    if args.self_test:self_test();return
    if any(x is None for x in (args.tools_root,args.workspace,args.score_report,args.output)):
        parser.error('--tools-root --workspace --score-report --output are required')
    if args.window_blocks<0 or args.output.exists():raise ValueError('invalid window or existing output')
    source=args.tools_root/'triad_group_acquire.py'
    if sha(source)!=EXPECTED_SCORER:raise ValueError('receiver source binding differs from accepted score version')
    sys.path.insert(0,str(args.tools_root.resolve()))
    from triad_group_acquire import GroupedWorkspace, INPUT_FILES, frequency_reuse_bound
    import torch
    torch.set_num_threads(1)
    start=time.perf_counter()
    recorded=json.loads(args.score_report.read_text())
    if recorded.get('receiver_source_sha256')!=EXPECTED_SCORER or len(recorded.get('captures',[]))!=1:
        raise ValueError('expected one completed capture from the bound actual-noisy receiver')
    score=recorded['captures'][0]
    ws=GroupedWorkspace(args.workspace)
    if score.get('mode')!='actual-noisy' or score['geometry']['public_group_parameters']!=ws.sync_keys:
        raise ValueError('score and public synchronization bank differ')
    offsets=tuple(score['geometry'].get('frequency_offsets_hz',[0.]))
    if score['geometry']['group_count']!=ws.groups*len(offsets):raise ValueError('effective bank size mismatch')
    bound=frequency_reuse_bound(offsets,ws.block_samples/ws.sample_rate,.01)
    if score.get('frequency_reuse',{}).get('allowed_coherent_power_loss_db',.01)>.01:
        raise ValueError('this diagnostic retains the default .01 dB approximation budget')
    public_hashes={name:sha(ws.directory/name) for name in INPUT_FILES}
    if public_hashes!=score['input_sha256']:raise ValueError('public score inputs have changed')
    m=ws.manifest
    if (m.get('mean_file'),m.get('mean_shape'),m.get('signal_energy_file'),m.get('signal_energy_shape')) != (
            'group_mean.c128',[ws.blocks,ws.groups,ws.dimension],'signal_energy.f64',[ws.blocks]):
        raise ValueError('evaluation-only mean/energy shape mismatch')
    extra={name:sha(ws.directory/name) for name in ('group_mean.c128','signal_energy.f64')}
    stats={name:(ws.directory/name).stat() for name in extra}
    expected_mean=ws.blocks*ws.groups*ws.dimension*16
    if stats['group_mean.c128'].st_size!=expected_mean or stats['signal_energy.f64'].st_size!=ws.blocks*8:
        raise ValueError('evaluation-only array size mismatch')
    mean=np.memmap(ws.directory/'group_mean.c128',dtype='<c16',mode='r',shape=(ws.blocks,ws.groups,ws.dimension))
    signal_energy=np.fromfile(ws.directory/'signal_energy.f64',dtype='<f8')
    if not np.isfinite(signal_energy).all() or np.any(signal_energy<0):raise ValueError('invalid clean energy')
    total_energy=math.fsum(signal_energy)
    interval,valid,received_energy=ws.calendar(1)
    centers=(interval[:,0].astype(np.longdouble)+interval[:,1].astype(np.longdouble))/(2*np.longdouble(ws.sample_rate))
    phases=[]
    for offset in offsets:
        cycles=np.remainder(np.longdouble(offset)*centers,np.longdouble(1))
        angle=np.asarray(-2*np.longdouble(str(math.pi))*cycles,dtype=np.float64)
        phases.append(np.exp(1j*angle))
    values=np.empty((len(offsets),ws.groups,2))
    segmented=np.empty_like(values) if args.window_blocks else None
    ranks=np.empty_like(values,dtype=np.int64)
    discarded_max=0.
    # Tile the large files; full temporal/state covariance is unnecessary here.
    for first in range(0,ws.groups,8):
        stop=min(first+8,ws.groups)
        G=np.array(ws.array('group_gram.c128')[:,first:stop])
        r=np.array(mean[:,first:stop])
        if not np.isfinite(G).all() or not np.isfinite(r).all():raise ValueError('nonfinite clean statistics')
        global_G=np.sum(G,axis=0)
        if args.window_blocks:
            starts=np.arange(0,ws.blocks,args.window_blocks)
            window_G=np.add.reduceat(G,starts,axis=0)
        for fi,phase in enumerate(phases):
            shifted=r*phase[:,None,None]
            global_r=np.sum(shifted,axis=0)
            if args.window_blocks:window_r=np.add.reduceat(shifted,starts,axis=0)
            for local,group in enumerate(range(first,stop)):
                columns=ws.mapping[group]
                gg=global_G[local][columns[:,:,None],columns[:,None,:]]
                rr=global_r[local][columns]
                energy,rank,discarded=projected_energy(gg,rr)
                values[fi,group]=energy;ranks[fi,group]=rank
                discarded_max=max(discarded_max,float(np.max(discarded)))
                if args.window_blocks:
                    gg=window_G[:,local][:,columns[:,:,None],columns[:,None,:]]
                    rr=window_r[:,local][:,columns]
                    energy,_,discarded=projected_energy(gg,rr)
                    segmented[fi,group]=np.sum(energy,axis=0)
                    discarded_max=max(discarded_max,float(np.max(discarded)))
        del G,r,global_G,shifted,global_r
    if np.any(values>total_energy*(1+1e-8)+1e-12) or (segmented is not None and np.any(segmented>total_energy*(1+1e-8)+1e-12)):
        raise ValueError('projected energy exceeds actual clean energy')
    # All receiver decisions precede these explicitly simulation-only reads.
    truth_bit=m.get('signal_bit_simulation_truth')
    noise=float(m.get('noise_variance',float('nan')))
    if truth_bit not in (0,1) or not math.isfinite(noise) or noise<=0:raise ValueError('invalid evaluation truth')
    def entry(array,bit,selection=None):
        fi,gi=np.unravel_index(int(np.argmax(array[:,:,bit])),array[:,:,bit].shape) if selection is None else selection
        key=dict(ws.sync_keys[gi]);key['frequency_hz']+=offsets[fi]
        energy=float(array[fi,gi,bit])
        result=dict(bit=bit,projected_clean_energy_samples=energy,retained_fraction=energy/total_energy if total_energy else None,
                    projected_energy_over_true_noise=energy/noise,frequency_index=int(fi),base_group_index=int(gi),
                    public_parameters=key)
        for label,field in (('arrival_seconds','selected_truth_arrival_seconds_simulation_only'),
                            ('frequency_hz','selected_truth_frequency_hz_simulation_only'),
                            ('clock_ppm','selected_truth_clock_ppm_simulation_only')):
            if field in m:result[label+'_error_simulation_only']=float(key[label]-m[field])
        return result
    report=dict(format='triad-clean-projection-diagnostic-v1',receiver_source_sha256=EXPECTED_SCORER,
        diagnostic_source_sha256=sha(__file__),score_report_sha256=sha(args.score_report),input_sha256={**public_hashes,**extra},
        scope='evaluation only after fixed receiver decisions; noiseless means and truth never enter acquisition or new hypotheses',
        geometry=dict(groups=ws.groups,blocks=ws.blocks,sample_rate=ws.sample_rate,block_samples=ws.block_samples,
                      sample_count=ws.sample_count,frequency_offsets_hz=offsets),frequency_reuse=bound,
        signal_bit_simulation_truth=truth_bit,total_clean_energy_samples_simulation_only=total_energy,
        true_noise_variance_simulation_only=noise,total_energy_over_true_noise_simulation_only=total_energy/noise,
        total_received_noisy_energy_samples=math.fsum(received_energy),
        observed_receiver=dict(bit_log_likelihoods=score['bit_log_likelihoods'],strict=score['strict'],relaxed=score['relaxed']),
        best_global_clean_projection_per_bit=[entry(values,bit) for bit in (0,1)],
        rank_tolerance=RANK_TOLERANCE,maximum_discarded_gram_trace_fraction=discarded_max,
        projection_rank_range=[int(ranks.min()),int(ranks.max())],
        limits=['Maximizing clean projection is an oracle evaluation, not the operational noisy likelihood or a calibrated detector.',
                'Retained energy is relative to this public shaped two-tap subspace. It does not establish a fundamental channel information limit.',
                'Common block-midpoint CFO rephasing uses exactly the existing finite bank and keeps physical timestamps across erasures.',
                'Projection rank truncation follows the receiver relative Gram tolerance; small discarded modes represent lost modeled information.',
                'This comparison cannot separately identify all timing, phase, modulation and channel-mismatch losses without additional controlled observations.'])
    if 0. in offsets:
        zero=offsets.index(0.)
        report['best_zero_cfo_projection_per_bit']=[entry(values,bit,(zero,int(np.argmax(values[zero,:,bit])))) for bit in (0,1)]
    chosen=int(score['best_scoring_group_for_winning_bit'])
    report['projection_at_actual_receiver_choice_per_bit']=[entry(values,bit,divmod(chosen,ws.groups)) for bit in (0,1)]
    if segmented is not None:
        report['segmented_oracle']=dict(window_blocks=args.window_blocks,nominal_window_seconds=args.window_blocks*ws.block_samples/ws.sample_rate,
            best_clean_projection_per_bit=[entry(segmented,bit) for bit in (0,1)],
            warning='Independent gains per segment can absorb the wrong pattern. This is a flexibility/representation diagnostic, not achievable noisy sensitivity.')
    ws.verify_unchanged()
    for name,initial in stats.items():
        final=(ws.directory/name).stat()
        if (initial.st_size,initial.st_mtime_ns,initial.st_ino)!=(final.st_size,final.st_mtime_ns,final.st_ino):raise ValueError('mean/energy changed')
    report['elapsed_seconds']=time.perf_counter()-start
    report['maximum_rss_kib']=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with args.output.open('x') as stream:json.dump(report,stream,indent=2,allow_nan=False);stream.write('\n')
    print(json.dumps(dict(output=str(args.output),seconds=report['elapsed_seconds'],rho=total_energy/noise,
                         best_retained_fraction=report['best_global_clean_projection_per_bit'][truth_bit]['retained_fraction'],
                         maximum_rss_kib=report['maximum_rss_kib'])))
if __name__=='__main__':main()
