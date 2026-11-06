#!/usr/bin/env python3
"""Bounded, complete-observation qualification of a public static clock bank.

Use triad_tracking_export for the native encrypted source and shared channel.
No private clock coordinates or oracle channel templates enter practical scores.
This is scheduled post-acquisition DATA-only reception, not blind acquisition.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import resource
import time

import numpy as np
import torch
from scipy.special import logsumexp

from triad_multiday_likelihood import StreamingBankStatistics, classify_scores


def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for x in iter(lambda: f.read(1048576), b''):
            h.update(x)
    return h.hexdigest()


def run(a):
    start, cpu = time.perf_counter(), time.process_time()
    sources={p:digest(Path(__file__).with_name(p)) for p in
        ('triad_multiday_study.py','triad_multiday_likelihood.py','triad_gaussian_state.py')}
    torch.set_num_threads(a.threads)
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.set_float32_matmul_precision('highest')
    root = Path(a.input)
    m = json.loads((root/'manifest.json').read_text())
    if m['mode'] != 'continuous-shared-channel':
        raise ValueError('complete continuous shared-channel export required')
    n, fs = m['samples'], m['sample_rate_hz']
    files = m['files']
    bank = sorted((f for f in files if f['role'] == 'public-affine-basis'),
                  key=lambda f: (f['bit'], f['template_index']))
    signals = sorted((f for f in files if f['role'] == 'channel-signal-oracle'), key=lambda f: f['bit'])
    if len(signals) != 2 or len(bank) % 2:
        raise ValueError('both bits and complete public bank required')
    c = len(bank)//2
    arrays = []
    identities = {}
    for f in bank+signals:
        p = root/f['path']
        if p.stat().st_size != n*8:
            raise ValueError('wrong sampled file length')
        identities[f['path']] = digest(p)
        arrays.append(np.memmap(p, mode='r', dtype='<c8', shape=(n,)))
    loading = time.perf_counter()-start
    variance = fs/10**(a.cn0/10)
    # Independent actual sample-noise streams validate the projected MC path.
    # Per-chunk RNG draws are reproducible for the recorded chunk size.
    labels = [0]*a.sampled_trials+[1]*a.sampled_trials+[None]*a.sampled_trials
    rng = np.random.default_rng(a.seed)
    accumulator = StreamingBankStatistics(sample_rate=fs, samples=n,
        candidates=c, noise_variance=variance, observed_streams=len(labels),
        precision=a.precision, device=a.device, maximum_chunk_samples=a.chunk,
        reduction_samples=min(a.chunk, a.reduction_samples),
        memory_limit_bytes=a.memory_mib*1024**2)
    preparation = time.perf_counter()-start
    reset_rows=max(1,round(4*fs))
    if a.chunk % reset_rows:
        raise ValueError('chunk must align with the reset-reference block size')
    reset=np.zeros((len(labels),2*c),np.float64)
    for lo in range(0, n, a.chunk):
        hi = min(n, lo+a.chunk)
        x = np.stack([p[lo:hi] for p in arrays])
        y = (rng.standard_normal((len(labels), hi-lo))+
             1j*rng.standard_normal((len(labels), hi-lo)))*math.sqrt(variance/2)
        for j, label in enumerate(labels):
            if label is not None:
                y[j] += x[2*c+label]
        accumulator.push(lo, x[:2*c].reshape(2,c,-1), x[2*c:], y)
        # Optimistic fixed-clock reference with independently renewed scalar
        # channel priors, exactly the rank-one Gaussian update per segment.
        # At reciprocal Fs this may exceed4s and is reported, never hidden.
        # Tile the independent segment reductions; Python per-segment loops
        # would turn a low-rate long observation into unnecessary overhead.
        reset_tile=max(reset_rows,(1024//reset_rows)*reset_rows)
        for first in range(0,hi-lo,reset_tile):
            stop=min(hi-lo,first+reset_tile)
            blocks=math.ceil((stop-first)/reset_rows)
            b=np.zeros((2*c,blocks*reset_rows),np.complex128)
            z=np.zeros((len(labels),blocks*reset_rows),np.complex128)
            b[:,:stop-first]=x[:2*c,first:stop]
            z[:,:stop-first]=y[:,first:stop]
            b=b.reshape(2*c,blocks,reset_rows)
            z=z.reshape(len(labels),blocks,reset_rows)
            r=np.einsum('wbr,cbr->wbc',z,b.conj(),optimize=True)/variance
            g=np.sum(abs(b)**2,axis=-1).T/variance
            reset+=(abs(r)**2/(1+g)-np.log1p(g)).sum(axis=1)
    statistics = accumulator.finish()
    sampled_scores = statistics.observed_scores()
    threshold = math.log(1/a.alpha)
    reset_scores=logsumexp(reset.reshape(-1,2,c),axis=-1)-math.log(c)
    sampled, conditional, reset_results = {}, {}, {}
    for j, label in enumerate((0, 1, None)):
        name = 'null' if label is None else f'bit{label}'
        sampled[name] = classify_scores(sampled_scores[j*a.sampled_trials:(j+1)*a.sampled_trials],
                                        threshold, true_bit=label)
        reset_results[name]=classify_scores(reset_scores[j*a.sampled_trials:(j+1)*a.sampled_trials],
                                           threshold,true_bit=label)
        practical, oracle, mc_diagnostics = [], [], []
        trials = a.null_trials if label is None else a.trials
        for lo in range(0, trials, a.mc_batch):
            draws = statistics.draw_correlations(trials=min(a.mc_batch, trials-lo),
                seed=a.seed+100000+j*10000+lo, signal_index=label)
            practical.append(statistics.static_scores(draws['bank_correlations']))
            oracle.append(statistics.oracle_scores(draws['oracle_correlations']))
            mc_diagnostics.append(draws['diagnostics'])
        conditional[name] = dict(
            practical=classify_scores(np.concatenate(practical),threshold,true_bit=label),
            known_channel_oracle=classify_scores(np.concatenate(oracle),threshold,true_bit=label),
            monte_carlo_diagnostics=mc_diagnostics)
    diagonal = statistics.gram.diagonal().real
    cross = statistics.gram[:2*c,2*c:]
    overlap = abs(cross)**2/np.maximum(diagonal[:2*c,None]*diagonal[None,2*c:],1e-300)
    best = [float(overlap[b*c:(b+1)*c,b].max()) for b in (0,1)]
    out = dict(schema='triad-multiday-study-v1', configuration=vars(a),
        manifest=m, input_sha256=identities, manifest_sha256=digest(root/'manifest.json'),
        receiver_sha256=sources,
        source_changed_during_run=any(digest(Path(__file__).with_name(p))!=h for p,h in sources.items()),
        statistics_identity=statistics.identity(), statistics=statistics.metadata,
        received_energy_over_n0=statistics.reference_energies.tolist(),
        best_public_same_bit_squared_overlap=best,
        best_public_same_bit_mismatch_db=[-10*math.log10(max(v,1e-300)) for v in best],
        sampled_noise_results=sampled, conditional_sufficient_statistic_results=conditional,
        reset_reference=dict(segment_seconds=reset_rows/fs,results=reset_results,
            scope='Fresh scalar Gaussian prior per segment, fixed public clock bank mixed once. This isolates reset loss; it is not the full timing HMM.'),
        null_accounting=dict(alpha=a.alpha, log_threshold=threshold,
            schedule='At most one fixed endpoint per calendar day, with equal bit prior and one fixed public clock bank. Additional starts/durations/models must share this budget.',
            guarantee='Conditional exact known-variance circular white Gaussian noise and exact likelihood arithmetic: Markov bound <= alpha per endpoint. Empirical counts check implementation, not physical ASRC noise.'),
        timing=dict(loading_and_hashing_wall_seconds=loading, preparation_wall_seconds=preparation,
            total_wall_seconds=time.perf_counter()-start,total_cpu_seconds=time.process_time()-cpu),
        peak_rss_kib=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
        scope='Complete sampled native encrypted DATA/channel/clock observation; public static-affine bank after scheduled acquisition. Oracle separate. MC conditional on this path; no Helper/live/blind-acquisition qualification.')
    output = Path(a.output)
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(out,indent=2)+'\n')
    np.savez_compressed(output.with_suffix('.npz'),gram=statistics.gram, observed_correlations=statistics.observed_correlations,
                        candidate_weights=statistics.candidate_weights)
    print(json.dumps(dict(output=str(output),seconds=n/fs,
        mismatch_db=out['best_public_same_bit_mismatch_db'],
        results={k:v['practical']['counts'] for k,v in conditional.items()},timing=out['timing'])))


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input',required=True); p.add_argument('--output',required=True)
    p.add_argument('--cn0',type=float,required=True); p.add_argument('--alpha',type=float,default=1e-4)
    p.add_argument('--trials',type=int,default=4096); p.add_argument('--null-trials',type=int,default=32768)
    p.add_argument('--sampled-trials',type=int,default=4); p.add_argument('--mc-batch',type=int,default=1024)
    p.add_argument('--seed',type=int,default=71539); p.add_argument('--chunk',type=int,default=16384)
    p.add_argument('--reduction-samples',type=int,default=1024)
    p.add_argument('--memory-mib',type=int,default=256); p.add_argument('--threads',type=int,default=1)
    p.add_argument('--device',default='cpu'); p.add_argument('--precision',choices=['fp32','fp64'],default='fp32')
    a=p.parse_args()
    if not 0<a.alpha<1 or not math.isfinite(a.cn0) or min(a.trials,a.null_trials,a.sampled_trials,a.mc_batch,a.threads)<1:
        p.error('positive trial/thread counts and finite C/N0/alpha required')
    run(a)
