#!/usr/bin/env python3
"""Generate eight fixed-observation precision jobs, or summarize their receipts.

Never launches jobs. Use an explicit bounded serial executor AFTER the main
campaign; freeze source hashes and record host/GPU/software first. The emitted
receipt_protocol defines exact command, source, artifact and completion bindings.
Use --summarize PLAN --receipts RESULTS --output SUMMARY after execution.
Two replayed captures are not Monte Carlo trials or a sensitivity qualification.
FP32 selects correlation products; CPU FP64 preparation/reductions remain and
must be charged even when dominant. CUDA score time includes transfers/finish.
"""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]
VARIANTS = (("cpu", "fp32"), ("cpu", "fp64"), ("cuda:0", "fp32"), ("cuda:0", "fp64"))

def read(path):
    return json.loads(Path(path).read_text())

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def require(condition, message):
    if not condition:
        raise ValueError(message)

def canonical_sha(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()).hexdigest()

def positive_elapsed(value):
    return type(value) in (int, float) and math.isfinite(value) and value > 0

def option(argv, flag):
    return argv[argv.index(flag) + 1]

def generate(campaign, directory):
    frozen = read(campaign)
    directory = directory or Path(frozen['root']) / 'local-evidence/triad-acquisition-improvement/precision-benchmark'
    jobs = []
    for index in (120002, 120003):
        name = f'paired-high30-each-65-cn8-p{index}-b16-signal-r1'
        matches = [j for j in frozen['scoring_jobs'] if j['name'] == name]
        require(len(matches) == 1, 'missing or duplicate frozen score job: ' + name)
        original = matches[0]
        expected = {'--mode':'actual-noisy', '--window-seconds':'.25', '--reblock':'1', '--group-tile':'4', '--memory-mib':'1024', '--threads':'1', '--attempts-per-day':'1440', '--duration-alternatives':'8', '--selection-methods':'16', '--calibration-segments':'2'}
        require(all(option(original['argv'], k) == v for k, v in expected.items()), 'frozen geometry/budget changed')
        for device, precision in VARIANTS:
            job = copy.deepcopy(original)
            job['name'] = name + '-' + device.replace(':', '-') + '-' + precision
            report = directory / (job['name'] + '.json')
            changes = {'--device':device, '--precision':precision, '--output':str(report), '--scores-output':str(report.with_suffix('.scores.npz'))}
            for flag, value in changes.items():
                job['argv'][job['argv'].index(flag) + 1] = value
            job['argv'] = ['/usr/bin/env', 'OMP_NUM_THREADS=1', 'OPENBLAS_NUM_THREADS=1', 'MKL_NUM_THREADS=1', 'NUMEXPR_NUM_THREADS=1', *job['argv']]
            job.update(timeout_seconds=600, input_workspace=original['argv'][2], capture=name, variant=device + '/' + precision, report=str(report), scores=str(report.with_suffix('.scores.npz')), frozen_score_job_sha256=hashlib.sha256(json.dumps(original, sort_keys=True, separators=(',', ':')).encode()).hexdigest())
            jobs.append(job)
    return {'schema':'triad-fixed-observation-precision-plan-v1', 'jobs':jobs, 'campaign':str(campaign), 'campaign_sha256':sha(campaign), 'generator_sha256':sha(__file__), 'expected_source_file_sha256':frozen['source_file_sha256'], 'source_file_inventory_sha256':frozen['source_file_inventory_sha256'], 'receipt_protocol':{'top_level':['plan_sha256', 'source_file_sha256', 'results'], 'per_job':['name', 'argv', 'argv_sha256', 'returncode', 'elapsed_seconds', 'runner', 'report_sha256', 'scores_npz_sha256'], 'argv_sha256':'SHA256 UTF-8 json.dumps(argv,sort_keys=True,separators=(comma,colon),allow_nan=False); default ensure_ascii=True', 'source_file_sha256':'Actual checked source inventory, exactly equal to expected_source_file_sha256', 'runner':'Unmodified triad_bounded_run.py status: completed, exit_code=0, cancelled=false, timed_out=false, termination.group_empty=true; finite positive elapsed_seconds', 'scope':'Exactly eight unique planned jobs, serial; elapsed_seconds includes wrapper end-to-end time; artifact hashes bind job report and score archive'}, 'execution':{'workers':1, 'after_main_campaign':True, 'timeout_seconds_per_job':600, 'tf32_allowed':False, 'source_hash_check_required_before_execution':True, 'record_host_gpu_torch_cuda_versions':True}, 'scope':'Two fixed actual-noisy captures, four paired arithmetic/device replays each; no noise resampling or independent-trial claim; all H timing groups and calibration budgets unchanged.'}

def summarize(plan_path, receipts_path, report_dir):
    import numpy as np
    plan, receipts = read(plan_path), read(receipts_path)
    require(plan['schema'] == 'triad-fixed-observation-precision-plan-v1', 'wrong benchmark plan')
    require(receipts['plan_sha256'] == sha(plan_path), 'receipt plan hash mismatch')
    require(receipts['source_file_sha256'] == plan['expected_source_file_sha256'], 'receipt source inventory mismatch')
    receipt_map = {r['name']:r for r in receipts['results']}
    require(len(plan['jobs']) == len(receipts['results']) == len(receipt_map) == 8 and set(receipt_map) == {j['name'] for j in plan['jobs']}, 'require exactly eight unique planned receipts')
    results, comparisons, speedups, failures = {}, [], [], []
    for job in plan['jobs']:
        receipt = receipt_map[job['name']]
        require(receipt['argv'] == job['argv'] and receipt['argv_sha256'] == canonical_sha(job['argv']), 'receipt command binding mismatch')
        runner = receipt['runner']
        require(type(receipt['returncode']) is int and receipt['returncode'] == 0 and runner['outcome'] == 'completed' and runner['exit_code'] == 0 and runner['cancelled'] is False and runner['timed_out'] is False and runner['termination']['group_empty'] is True, 'job did not complete cleanly')
        require(positive_elapsed(receipt['elapsed_seconds']) and positive_elapsed(runner['elapsed_seconds']), 'invalid execution elapsed time')
        path = (report_dir / Path(job['report']).name) if report_dir else Path(job['report'])
        require(sha(path) == receipt['report_sha256'], 'receipt report hash mismatch')
        payload = read(path)
        require(payload['receiver_source_sha256'] == plan['expected_source_file_sha256']['tools/triad_group_acquire.py'], 'receiver source hash mismatch')
        require(len(payload['captures']) == 1, 'expected exactly one fixed capture')
        report = payload['captures'][0]
        scores = (report_dir / Path(job['scores']).name) if report_dir else Path(job['scores'])
        require(sha(scores) == payload['scores_npz_sha256'] == receipt['scores_npz_sha256'], 'score archive hash mismatch')
        require(report['input'] == job['input_workspace'], 'report workspace differs from fixed job')
        with np.load(scores, allow_pickle=False) as archive:
            group, bit = archive['group_scores_0'].copy(), archive['bit_scores_0'].copy()
        require(group.shape == (109,2) and bit.shape == (2,) and np.isfinite(group).all() and np.isfinite(bit).all(), 'invalid complete-bank scores')
        require(np.array_equal(group, report['group_bit_log_likelihoods']) and np.array_equal(bit, report['bit_log_likelihoods']), 'JSON/NPZ scores differ')
        results[job['capture'], job['variant']] = (report, group, bit, receipt)
    for capture in sorted({j['capture'] for j in plan['jobs']}):
        reference = results.get((capture, 'cpu/fp64'))
        if reference is None:
            failures.append({'capture':capture, 'error':'missing CPU FP64 reference'}); continue
        ref, rg, rb, _ = reference
        for device, precision in VARIANTS:
            variant = device + '/' + precision
            item = results.get((capture, variant))
            if item is None:
                continue
            report, group, bit, receipt = item
            require(report['input_sha256'] == ref['input_sha256'], 'variants used different input bytes')
            require(all(report[k] == ref[k] for k in ('geometry','calibration','resources','mode','method','limits')), 'variants changed geometry, calibration or model')
            require(report['geometry']['reblock'] == 1 and report['geometry']['nominal_window_seconds'] == .25 and report['geometry']['base_block_samples'] == 16 and report['geometry']['group_count'] == 109, 'report geometry differs from fixed job')
            require(report['resources']['group_tile'] == 4 and report['resources']['memory_limit_bytes'] == 1024*1024*1024, 'report resource configuration differs from fixed job')
            require(report['arithmetic']['bulk'] == precision and report['arithmetic']['device'] == device and report['arithmetic']['tf32_allowed'] is False, 'reported arithmetic differs from planned variant')
            delta = lambda a,b: {'max_absolute':float(np.max(abs(a-b))), 'max_relative_floor_1e_12':float(np.max(abs(a-b)/np.maximum(abs(b),1e-12)))}
            gaps, refgaps = group[:,None,:]-group[None,:,:], rg[:,None,:]-rg[None,:,:]
            inverted = (gaps*refgaps < 0) & np.triu(np.ones((109,109),bool),1)[:,:,None]
            comparisons.append({'capture':capture, 'variant':variant, 'group_scores':delta(group,rg), 'bit_scores':delta(bit,rb), 'group_order_identical':bool(np.array_equal(np.argsort(group,axis=0,kind='stable'),np.argsort(rg,axis=0,kind='stable'))), 'inverted_group_pairs':int(inverted.sum()), 'inverted_pairs_reference_gap_over_1e_4_nats':int((inverted & (abs(refgaps)>1e-4)).sum()), 'top_groups':np.argmax(group,axis=0).tolist(), 'reference_top_groups':np.argmax(rg,axis=0).tolist(), 'reference_gap_at_variant_top_nats':[float(rg[:,b].max()-rg[int(np.argmax(group[:,b])),b]) for b in range(2)], 'signed_bit1_minus_bit0_nats':float(bit[1]-bit[0]), 'reference_signed_bit_margin':float(rb[1]-rb[0]), 'strict':report['strict'], 'relaxed':report['relaxed'], 'reference_strict':ref['strict'], 'reference_relaxed':ref['relaxed'], 'component_seconds':report['timing'], 'runner_end_to_end_seconds':receipt['elapsed_seconds'], 'bounded_runner_elapsed_seconds':receipt.get('runner',{}).get('elapsed_seconds'), 'input_sha256':report['input_sha256']})
        for precision in ('fp32','fp64'):
            cpu, gpu = results.get((capture,'cpu/'+precision)), results.get((capture,'cuda:0/'+precision))
            if cpu is None or gpu is None:
                continue
            ct, gt = cpu[0]['timing'], gpu[0]['timing']
            speedups.append({'capture':capture, 'precision':precision, 'cpu_over_cuda_component_ratios':{k:ct[k]/gt[k] if gt[k]>0 else None for k in ct}, 'cpu_over_cuda_runner_end_to_end_ratio':cpu[3]['elapsed_seconds']/gpu[3]['elapsed_seconds']})
    return {'schema':'triad-fixed-observation-precision-summary-v1', 'plan_sha256':sha(plan_path), 'receipts_sha256':sha(receipts_path), 'complete':not failures and len(comparisons)==8, 'failures':failures, 'comparisons_to_cpu_fp64':comparisons, 'acceleration':speedups, 'limits':['Single serial execution per variant; no timing confidence interval or independent reception trials.', 'FP64 CPU preparation and residual calibration remain; full-pipeline acceleration is distinct from score-stage acceleration.', 'Score-stage timing includes transfers and FP64 evidence finish; no CUDA-kernel-only timing claim.', 'Ranking near-tie diagnostic uses a descriptive 1e-4-nat reference gap, not an admission tolerance.']}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--campaign',type=Path,default=ROOT/'local-evidence/triad-acquisition-improvement/campaign-plan/campaign-plan.json')
    parser.add_argument('--benchmark-dir',type=Path)
    parser.add_argument('--summarize',type=Path,help='read an executed benchmark plan instead of generating jobs')
    parser.add_argument('--receipts',type=Path,help='explicit bounded serial executor receipt JSON matching the emitted protocol')
    parser.add_argument('--report-dir',type=Path,help='optional directory of downloaded JSON/NPZ outputs')
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    if args.summarize and not args.receipts:
        parser.error('--summarize requires --receipts')
    result = summarize(args.summarize,args.receipts,args.report_dir) if args.summarize else generate(args.campaign,args.benchmark_dir)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with args.output.open('x') as stream:
        json.dump(result,stream,indent=2,allow_nan=False); stream.write('\n')

if __name__ == '__main__':
    main()
