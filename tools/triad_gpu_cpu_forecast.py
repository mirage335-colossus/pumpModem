#!/usr/bin/env python3
"""Forecast this host's receiver service from a CUDA work trace and local timing.

The CUDA report supplies work and public geometry. The target CPU supplies
all time coefficients, including fixed orchestration work. A deterministic
last local observation is reserved for an independent timing check.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
from statistics import fmean
import time

from triad_case_capacity import combine_components, _larger_component, unqualified_blind_capacity
from triad_case_suite import identity, same_measured_source, write_result
from triad_cases import DEFAULT_CASES, ROOT, load_cases


RATIO = 'ratio_processing_seconds_per_baseband_second'
INTERVAL = RATIO+'_interval'
COST_LIMIT = .30
_NOMINAL_TIMING = (('recurring_bank_preparation',), ('data_acquisition',), ('nominal_fine',))
_ACQUISITION_TIMING = ('data_acquisition','noise_acquisition','recurring_acquisition')
_FINE_TIMING = ('nominal_fine','terminal','noise_only','maximum_state_transition')
_CEILING_TIMING = (_NOMINAL_TIMING[0], _ACQUISITION_TIMING, _FINE_TIMING)
_HOLDOUT_BUDGETS = {'nominal_service': (.15,.10), 'complete_capacity': (.20,.15)}
ADAPTIVE_WORK_LIMIT = .05
CALLBACK_WORK_LIMIT = .10
CALLBACK_ABSOLUTE_ALLOWANCE = 2
NATIVE_TRANSFER_RATIO_LIMIT = 2.
SCORE_ATOL, SCORE_RTOL = 2e-3, 2e-4
_GEOMETRY = ('sample_rate_hz', 'search_window_samples', 'source_samples',
    'maximum_capacity_window_seconds', 'conservative_public_slot_interval_seconds',
    'fine_unit_samples', 'recurring_window', 'public_calendar')
_TILE = ('frequency_points', 'units', 'frequency_unit_points', 'terminal_unit_frequency_points',
    'full_fft_products', 'full_observation_ffts', 'raw_candidate_row_capacity',
    'template_workspaces', 'arrival_points', 'rays', 'fft_length')
_CACHE = ('method', 'direct_rows', 'cache_misses',
    'native_preparation_calls', 'native_preparation_samples', 'evictions', 'cache_limit_bytes',
    'tile_samples', 'dense_factor', 'large_phase_fallback_rows', 'staging_fallback_rows',
    'fallback_reason')
_NATIVE_FILL = ('cache_misses','native_preparation_calls','native_preparation_samples','evictions')
_ADAPTIVE_REGIME = ('initial_states','state_cap','proposal_cap','attained_state_cap',
                    'attained_proposal_cap','coherence_models')
_FINE_STRATA = ('nominal_continuation','maximum_state_transition','terminal','interior_erasure','noise_only')
_ENDPOINT = ('status','classification','bit','quiet_projection_modes')
_GPU_RUNTIME_LEAVES = ('triad_gpu_workload.py','triad_gpu_bank.py','triad_gpu_fine.py')
_CUDA_REPORT_MATCH = ('runtime','runtime_limits','workers','memory_limit_bytes')
_COARSE_RUNTIME = ('name','cupy','cuda_runtime','workspace_limit_bytes','array_pool_limit_bytes',
                   'fft_plan_cache_limit_bytes','fft_plan_cache_limit_count')
_DISPATCH = frozenset(('hypothesis_update_tasks', 'requested_candidate_shards',
                       'maximum_hypothesis_tasks_per_block'))
_FIXED = ('stream_controller', 'quiet_state_and_canonical_codec', 'public_slot_planning',
          'receiver_worker_lifecycle')


def _require(condition, reason):
    if not condition:
        raise ValueError(reason)


def _select(row, keys):
    return {key: row[key] for key in keys}


def _capacity_metadata(report):
    return dict(capacity_scope=report.get('capacity_scope','historical-unspecified'),
        blind_fresh_start_capacity=copy.deepcopy(report.get(
            'blind_fresh_start_capacity',unqualified_blind_capacity())))


def _useful(row):
    return {key: value for key, value in row.items() if key not in _DISPATCH}


def _fit(published, prediction, holdout, *, work=None):
    """Transfer a separately held-out point and preserve the wider local interval."""
    bounds = published.get(INTERVAL)
    _require(math.isfinite(prediction) and prediction >= 0 and math.isfinite(holdout) and holdout > 0,
             'nonfinite or nonpositive calibration/holdout timing')
    error = prediction/holdout-1
    supported = (published['model_status'] != 'unresolved' and bounds is not None and
                 1/(1+COST_LIMIT) <= prediction/holdout <= 1+COST_LIMIT)
    low, high = work['relative_cost_scale_interval'] if work else (1., 1.)
    point_scale = work.get('relative_point_scale',math.sqrt(low*high)) if work else 1.
    scaled = prediction*point_scale
    return {RATIO: scaled,
        INTERVAL: [min(bounds[0], prediction, holdout)*low,
                   max(bounds[1], prediction, holdout)*high] if bounds else None,
        'model_status': published['model_status'] if supported else 'unresolved',
        'calibrated_point_before_gpu_scaling':prediction,
        'independent_local_holdout': {RATIO: holdout, 'relative_cpu_fit_error': error,
            'relative_gpu_scaled_error':scaled/holdout-1,
            'accepted': abs(math.log(prediction/holdout)) <= math.log1p(COST_LIMIT) if prediction else False},
        **({'gpu_work_discrepancy':work} if work else {})}


def _score_match(cpu, gpu):
    return (cpu.keys()==gpu.keys() and bool(cpu) and all(
        len(a)==len(b)==2 and all(math.isfinite(x) and math.isfinite(y) and
            abs(x-y)<=SCORE_ATOL+SCORE_RTOL*abs(y) for x,y in zip(a,b)) and
        ((a[0]>a[1])-(a[0]<a[1]))==((b[0]>b[1])-(b[0]<b[1]))
        for a,b in ((cpu[name],gpu[name]) for name in cpu)))


def _drivers(row, cache, *, transfer=False):
    drivers = {'adaptive.'+key:value for key,value in _useful(row['lanes']['adaptive']['work_counts']).items()} | {
        'cache.'+key:cache[key] for key in ('calls','interpolated_rows','row_tile_references')}
    if transfer:
        calls = cache['native_preparation_calls']
        _require(cache['cache_misses']==calls and cache['evictions']==0 and cache['direct_rows']==0 and
                 cache['native_preparation_samples']==2*cache['tile_samples']*calls,
                 'cross-case native fills must be the same full two-bit tile kernel without evictions')
        drivers['native.full_two_bit_tile_fills']=calls
    return drivers


def _driver_ratios(cpu, gpu, earlier=None):
    _require(cpu.keys()==gpu.keys() and bool(cpu), 'adaptive counter schema differs')
    ratios = {}
    for name, before in cpu.items():
        after = gpu[name]
        if earlier is not None:
            before -= earlier[0][name]
            after -= earlier[1][name]
        _require(type(before) is int and type(after) is int and before>=0 and after>=0,
                 'nonmonotonic or invalid fine work counter')
        _require(bool(before)==bool(after), 'fine work absent from one calibration domain: '+name)
        ratio = after/before if before else 1.
        if name.startswith('native.'):
            inside = 1/NATIVE_TRANSFER_RATIO_LIMIT<=ratio<=NATIVE_TRANSFER_RATIO_LIMIT
        else:
            allowed = (max(CALLBACK_ABSOLUTE_ALLOWANCE,CALLBACK_WORK_LIMIT*before) if name=='cache.calls'
                       else ADAPTIVE_WORK_LIMIT*before)
            inside = abs(after-before)<=allowed
        _require(inside,'fine work outside declared approximation envelope: '+name)
        ratios[name] = ratio
    return ratios


def _work_envelope(receipts, allow_approximation, *, transfer=False):
    exact = all(row['exact_work'] for row in receipts)
    _require(exact or allow_approximation, 'approximate fine work lacks a supported precision/work reference')
    counters = {}
    for receipt in receipts:
        for name, ratio in receipt['ratios'].items():
            low, high = counters.get(name,(1.,1.))
            counters[name]=[min(low,ratio),max(high,ratio)]
    result = dict(exact_work_match=exact and not transfer, observed_counter_ratio_intervals=counters,
        relative_cost_scale_interval=[min(1.,*(pair[0] for pair in counters.values())),
                                      max(1.,*(pair[1] for pair in counters.values()))],
        scope='Fixed costs plus nonnegative costs for observed work drivers. Unknown cost shares are bounded by the extreme observed work ratios, including 1 for fixed work.')
    if transfer:
        # The clear profile can omit the encrypted donor's one-byte mask
        # computation during native source construction. We have no fitted
        # native wall-time share or lower bound on that saving.
        nonnative = [pair for name,pair in counters.items() if not name.startswith('native.')]
        result['relative_cost_scale_interval'][0] = 0.
        result['relative_point_scale'] = math.sqrt(min(pair[0] for pair in nonnative)*
                                                   max(pair[1] for pair in nonnative))
        result['scope'] += (' The clear target removes one optional per-source mask computation. '
            'Native full-tile multiplicity is charged in the upper bound, and its unknown wall-time '
            'share is omitted from the point adjustment. No positive lower bound is inferred.')
    return result


def _check_fine(local, gpu, local_cache, *, nominal=False, earlier=None, transfer=False):
    """Check exact physical/operational work, then bound diagnostic drift."""
    def lanes(row):
        return {name: {'counts': _useful(lane['work_counts']), 'regime': _useful(lane['state_work_regime']),
            **(_select(lane['timing'] if nominal and 'timing' in lane else lane,
                       ('pruning_events','states_dropped')))}
            for name,lane in row['lanes'].items()}
    keys = ('consumed_samples','blocks','segments','additional_discarded_rows','requested_invalid_rows')
    left, right = lanes(local), lanes(gpu)
    exact_cache = tuple(key for key in _CACHE if not transfer or key not in _NATIVE_FILL)
    _require(set(left)==set(right)=={'initial_affine','adaptive'} and
             _select(local,keys) == _select(gpu,keys) and
             left['initial_affine']==right['initial_affine'] and
             _select(local_cache,exact_cache) == _select(gpu['native_cache'],exact_cache) and
             _select(left['adaptive']['regime'],_ADAPTIVE_REGIME)==
             _select(right['adaptive']['regime'],_ADAPTIVE_REGIME),
             'fine physical, fixed-affine, native cache or public adaptive regime differs')
    exact = left['adaptive']==right['adaptive']
    if nominal:
        _require(local['processed_rows'] == gpu['processed_rows'] and
                 local['retained_states']['initial_affine']==gpu['retained_states']['initial_affine'],
                 'continued fixed-affine retained states differ')
        exact = exact and local['retained_states']['adaptive']==gpu['retained_states']['adaptive']
    else:
        _require(_select(local['probe_endpoint'],_ENDPOINT)==_select(gpu['probe_endpoint'],_ENDPOINT) and
                 (len(local['coordinates'])==len(gpu['coordinates']) if transfer else
                  local['coordinates']==gpu['coordinates']),
                 'fine short endpoint or quiet projection differs')
    drivers = (_drivers(local,local_cache,transfer=transfer),
               _drivers(gpu,gpu['native_cache'],transfer=transfer))
    cumulative = _driver_ratios(*drivers)
    ratios = cumulative if earlier is None else _driver_ratios(*drivers,earlier=earlier)
    return dict(exact_work=exact and all(value==1 for value in cumulative.values()),
                ratios=ratios, drivers=drivers)


def _nominal(cpu, gpu, geometry, *, allow_approximation=False, transfer=False):
    records = gpu['records']
    observations = cpu['observations']
    _require(len(records) == 5 and len(observations) >= 3, 'nominal prefix/continuation holdout missing')
    last = records[-1]['work']
    _require(cpu['public_origin_sample'] == last['physical_first_sample'] and
             cpu['public_probe_samples'] == last['processed_rows'], 'continued physical extent differs')
    work = []
    for observation in observations:
        _require(observation['status'] == 'complete_work_observation' and
                 observation['geometry']['supported'] and observation['continued_cache_boundary_work']['supported'],
                 'nominal CPU state/cache gate did not pass')
        earlier = None
        for phase, record in zip([observation['prefix'], *observation['checkpoints']], records[:4], strict=True):
            local = phase['counters']
            if not transfer:
                _require(_score_match({name:lane['bit_scores'] for name,lane in local['lanes'].items()},
                    record['lane_scores']), 'target CPU and GPU continued scores or bit order differ')
            check = _check_fine(local, record['work'], local['native_templates']['cache'],
                               nominal=True, earlier=earlier, transfer=transfer)
            work.append(check)
            earlier = check['drivers']
        _require(observation['finish_classify_close']['endpoint'] == last['probe_endpoint']['classification'],
                 'nominal short endpoint differs')

    fs, slot, samples = (geometry['sample_rate_hz'],
        geometry['conservative_public_slot_interval_seconds'], geometry['search_window_samples'])
    prefix_rows = records[0]['work']['processed_rows']
    continued_rows = records[3]['work']['processed_rows']-prefix_rows
    _require(prefix_rows == 2*fs and continued_rows == 4*fs and samples >= prefix_rows+continued_rows,
             'GPU work does not cover the declared 2+1/2/4 second fine observation')

    def normalize(row, held_out):
        a,b,c = [phase[RATIO] for phase in row['checkpoints']]
        rate = c if held_out else (a+2*b)/3
        cpu_seconds_per_work_row = rate*4/continued_rows
        return (row['prefix'][RATIO]*prefix_rows/fs+
                cpu_seconds_per_work_row*(samples-prefix_rows))/slot + row['finish_classify_close'][RATIO]

    prediction = fmean(normalize(row,False) for row in observations[:-1])
    out = _fit(cpu['fine_component'], prediction, normalize(observations[-1],True),
               work=_work_envelope(work,allow_approximation,transfer=transfer))
    out['gpu_demand'] = dict(prefix_rows=prefix_rows, continuation_rows=samples-prefix_rows)
    return out


def _branch(cpu, gpu, geometry, *, ceiling=False, allow_approximation=False,
            transfer=False, allow_shifted_probe=False):
    records = gpu['records']
    _require(len(records) == 1 and cpu['all_observations_complete'],
             'fine branch lacks complete bounded work')
    reference = next((row for row in cpu['work_samples'] if row['units'] == 1), None)
    _require(reference is not None, 'fine CPU training unit missing')
    work = records[0]['work']
    match = _check_fine(reference, work, reference['native_template_work']['cache'],transfer=transfer)
    _require(reference['physical_first_sample'] == work['physical_first_sample'] or
             (transfer and allow_shifted_probe and 0<=work['physical_first_sample']<=
              geometry['search_window_samples']-work['processed_rows']),
             'fine branch physical geometry differs')
    if ceiling:
        _require(work['capacity_transition_coverage']['supported'], 'GPU capped transition work missing')
    fs = geometry['sample_rate_hz']
    probe_rows = work['processed_rows']
    _require(probe_rows == reference['work_unit_samples'] and probe_rows > 0,
             'GPU fine branch work unit differs')
    slot = geometry['conservative_public_slot_interval_seconds']
    # Capped work is a two-segment cold unit charged once per actual segment.
    demand = (math.ceil(2*geometry['maximum_capacity_window_seconds'])*probe_rows if ceiling else
              round(fs*geometry['maximum_capacity_window_seconds']))
    multiplier = demand/(fs*slot)
    rows = cpu['ratios']
    _require(len(rows) >= 3, 'fine branch independent CPU holdout missing')
    point = fmean((row[0]+2*row[1])/3 for row in rows[:-1])*multiplier
    bounds = cpu.get(INTERVAL)
    published = dict(cpu, **{INTERVAL:[x*multiplier for x in bounds] if bounds else None})
    out = _fit(published, point, rows[-1][2]*multiplier,
               work=_work_envelope([match],allow_approximation,transfer=transfer))
    out['gpu_demand'] = dict(probe_rows=probe_rows, forecast_work_rows=demand)
    return out


def _acquisition_demand_match(local, remote, *, transfer=False):
    local_work, remote_work = local['acquisition_work_class'], remote['acquisition_work_class']
    if transfer:
        _require(local_work['helper_reason']==remote_work['helper_reason']=='helper_disabled',
                 'cross-case Helper work class not calibrated')
        local_work, remote_work = ({key:value for key,value in row.items() if key!='helper_procedure_id'}
                                  for row in (local_work,remote_work))
    if local_work is not None and local_work==remote_work:
        return None
    _require(local_work is not None and remote_work is not None, 'Helper or coarse demand differs')
    local_shape, remote_shape = (row['coarse'] for row in (local_work,remote_work))
    def without_products(row):
        return dict(row, coarse={key:value for key,value in row['coarse'].items() if key!='fft_products'})
    _require(local_shape.get('correlation_method')==remote_shape.get('correlation_method')=='lag' and
             without_products(local_work)==without_products(remote_work) and
             local['frequency_intervals'] is not None and
             local['frequency_intervals']==remote['frequency_intervals'], 'Helper or coarse demand differs')
    cpu_parts, gpu_parts = (row['work']['workers'] for row in (local,remote))
    workspaces, arrivals = local_shape['workspaces'], local_shape['arrival_points']
    _require(gpu_parts==1 and 1<cpu_parts<=workspaces and arrivals>0,
             'lag FFT workspace partition counts differ from the calibrated domain')
    # Each lag group contributes two products per workspace/ray/arrival.
    # Each multi-frequency group contributes one cached FFT per partition.
    group_unit = 2*workspaces*arrivals
    cpu_bulk, cpu_fixed = divmod(local_shape['fft_products'],group_unit)
    gpu_bulk, gpu_fixed = divmod(remote_shape['fft_products'],group_unit)
    _require(0<gpu_fixed<=gpu_bulk==cpu_bulk and cpu_fixed==gpu_fixed*cpu_parts,
             'lag FFT products differ beyond exact cached-kernel replication')
    return dict(cpu_products=local_shape['fft_products'],gpu_products=remote_shape['fft_products'],
                cpu_partitions=cpu_parts,gpu_partitions=gpu_parts,
                shared_bulk_fft_products=cpu_bulk*group_unit,
                cached_kernel_ffts_per_partition=gpu_fixed,
                replicated_cached_kernel_ffts=cpu_fixed-gpu_fixed)


def _acquisition(cpu, gpu, *, transfer=False):
    coverage = cpu.get('coverage',{})
    local = dict(acquisition_work_class=cpu['acquisition_work_class'],
                 frequency_intervals=coverage.get('searched_frequency_intervals_hz'),
                 work=coverage.get('coarse',{}).get('work'))
    correction = _acquisition_demand_match(local,gpu,transfer=transfer)
    if gpu['status'] == 'acquired':
        _require(cpu['coverage']['status'] == 'acquired' and
                 (transfer or cpu['coverage']['coordinates']==gpu['coordinates']),
                 'complete CPU acquisition or matched public proposals missing')
        rows = [row[RATIO] for row in cpu['samples']]
        _require(len(rows) >= 3, 'independent local acquisition timing missing')
        out = _fit(cpu,fmean(rows[:-1]),rows[-1])
        if not transfer:
            out['measured_complete_component_lower_bound'] = min(rows)
        out['gpu_demand'] = gpu['acquisition_work_class']
        if correction is not None:
            out['local_lag_fft_partition_work'] = correction
        return out
    _require(gpu['status'] == 'partial_acquisition', 'unmeasured GPU acquisition branch')
    plan = gpu['full_plan']
    _require(_select(plan,_TILE) == _select(cpu['work_plan'],_TILE), 'full coarse tiled demand differs')
    needed = min(plan['frequency_points'], min(plan['units'],4)*plan['frequency_unit_points'])
    tiles = gpu['bounded_tiles']
    _require(bool(tiles) and tiles[-1]['cumulative_frequency_points'] >= needed,
             'GPU acquisition did not exercise the declared 1/2/4 frequency tiles')
    parts = cpu['components']
    setup = [row[RATIO] for row in cpu['samples']]
    exact = parts['exact_frequency_tiles']['ratios']
    selection = parts['global_candidate_selection']['samples']
    _require(min(len(setup),len(exact),len(selection)) >= 3, 'local tiled CPU holdout missing')
    # Training rates were normalized to the CPU's complete grid. The exact
    # same GPU grid demand gates reuse, including the nonlinear selection size.
    prediction = fmean(setup[:-1])+fmean((r[0]+2*r[1])/3 for r in exact[:-1])+max(selection[:-1])
    out = _fit(cpu,prediction,setup[-1]+exact[-1][2]+selection[-1])
    out['gpu_demand'] = _select(plan,_TILE)
    return out


def _banks(cpu, gpu):
    samples = cpu['samples']
    _require(len(samples) == len(gpu) == 2, 'two distinct recurring banks required')
    for local,remote in zip(samples,gpu,strict=True):
        work = remote['work']
        _require(local['public_slot_index'] == remote['public_slot_index'] and
                 local['public_address'] == remote['public_address'] and
                 local['native_work_geometry'] == _select(work,local['native_work_geometry']) and
                 local['fft_bank_geometry'] is not None and local['fft_bank_geometry'] == work['fft_bank_geometry'],
                 'recurring public bank work differs')
    out = _fit(cpu,samples[0][RATIO],samples[1][RATIO])
    # Charge at least the direct conservative maximum observed bank service.
    out[RATIO] = max(out[RATIO], cpu[RATIO])
    out['independent_local_holdout']['relative_conservative_charge_error'] = out[RATIO]/samples[1][RATIO]-1
    out['gpu_demand'] = [row['native_work_geometry'] for row in samples]
    return out


def _forecast_row(cpu, gpu, *, allow_approximation=False, transfer=False, target_noise=None):
    geometry = gpu['public_geometry']
    _require(_select(geometry,_GEOMETRY) == _select(cpu['public_geometry'],_GEOMETRY),
             'public slot/calendar geometry differs')
    _require(cpu['calibration_inputs'] == gpu['evaluator'], 'evaluator seeds or source bit differ')
    measurement = cpu['measurement']
    _require(measurement['inference_allocation_mode'] == 'full_logical' and
             measurement['workspace_margin']['status'] == 'within_budget',
             'local full-logical allocation or workspace unavailable')
    _require(measurement['recurring_window']['covers_longest_public_geometry'],
             'local recurring DSP/ingress coverage incomplete')
    cuda = gpu['devices']['cuda']
    fine = cuda['strata']
    rows, missing = {}, {}
    def stage(name, call):
        try:
            rows[name] = call()
        except (KeyError, TypeError, ValueError, OverflowError, IndexError) as error:
            rows[name] = {'model_status':'unresolved'}
            missing[name] = str(error)

    stage('data_acquisition', lambda: _acquisition(measurement['nominal_acquisition'],
        cuda['data_acquisition'],transfer=transfer))
    stage('noise_acquisition', lambda: _acquisition(measurement['noise_only_acquisition'],
        cuda['noise_acquisition'],transfer=transfer))
    stage('recurring_acquisition', lambda: _acquisition(
        measurement['recurring_window']['noise_only_acquisition'],gpu['recurring_acquisition'],transfer=transfer))
    stage('recurring_bank_preparation', lambda: _banks(measurement['recurring_banks'],gpu['recurring_banks']))
    stage('nominal_fine', lambda: _nominal(measurement['fine_active'],fine['nominal_continuation'],geometry,
                                         allow_approximation=allow_approximation,transfer=transfer))
    for name,key in (('terminal','fine_terminal'),('noise_only','noise_only_fine'),
                     ('maximum_state_transition','fine_public_state_ceiling')):
        calibrated_target = name=='noise_only' and target_noise is not None
        stage(name,lambda: _branch(target_noise if calibrated_target else measurement[key],fine[name],geometry,
            ceiling=name=='maximum_state_transition',allow_approximation=allow_approximation,
            transfer=transfer and not calibrated_target,allow_shifted_probe=name=='noise_only'))
    def erasure():
        local = measurement['erasure_branch_holdout']
        _require(local['supported_by_state_ceiling'], 'local interior erasure outside cap allowance')
        work = fine['interior_erasure']['records'][0]['work']
        sample = local['sample']
        match = _work_envelope([_check_fine(sample,work,sample['native_template_work']['cache'],
                                           transfer=transfer)],allow_approximation,transfer=transfer)
        _require(sample['physical_first_sample']==work['physical_first_sample'], 'erasure location differs')
        cap = measurement['fine_public_state_ceiling'][INTERVAL][1]
        _require(sample[RATIO]*match['relative_cost_scale_interval'][1] <= 2*cap,
                 'approximate interior erasure exceeds conservative cap allowance')
        return {'model_status':'covered_by_state_ceiling','gpu_work_discrepancy':match}
    stage('interior_erasure',erasure)
    common = {name:measurement['nominal_service']['components'][name] for name in _FIXED}
    for name in _FIXED:
        _require(common[name][RATIO] is not None, f'fixed local work missing: {name}')
    common['recurring_bank_preparation'] = rows['recurring_bank_preparation']
    nominal = combine_components(dict(common,acquisition=rows['data_acquisition'],
        complete_fine_search=rows['nominal_fine']),complete_work_covered=True)

    def maximum(names):
        return _larger_component(tuple(rows[name] for name in names))

    ceiling = combine_components(dict(common,
        acquisition=maximum(_ACQUISITION_TIMING),complete_fine_search=maximum(_FINE_TIMING)),
        complete_work_covered='interior_erasure' not in missing)
    if nominal['realtime_status']=='fail':
        ceiling.update(realtime_status='fail',reason='nominal_service_failure')
    elif ceiling['realtime_status']=='fail' and ceiling['measured_complete_component_lower_bound']<1:
        ceiling.update(realtime_status='unresolved',reason='conservative_ceiling_point_does_not_prove_nominal_failure')
    scope = _capacity_metadata(measurement)
    nominal['capacity_scope']=ceiling['capacity_scope']=scope['capacity_scope']
    fixed_total = sum(common[name][RATIO] for name in _FIXED)
    nominal_names = tuple(group[0] for group in _NOMINAL_TIMING)
    independent_nominal = [rows[name].get('independent_local_holdout',{}).get(RATIO) for name in nominal_names]
    held = (fixed_total+sum(independent_nominal) if all(x is not None for x in independent_nominal) else None)
    unadjusted = (fixed_total+sum(rows[name]['calibrated_point_before_gpu_scaling'] for name in nominal_names)
                  if held is not None else None)
    return dict(components=rows, missing_or_mismatched=missing, nominal_service=nominal,
        complete_capacity=ceiling, local_workspace=measurement['workspace_margin'], **scope,
        combined_independent_local_nominal_holdout_s_per_s=held,
        combined_nominal_relative_cpu_fit_error=(unadjusted/held-1 if held else None),
        combined_nominal_relative_gpu_forecast_error=(nominal[RATIO]/held-1
            if held and nominal[RATIO] is not None else None),
        scope='Conditional sum of separately held-out CPU components on the observed public work class; no GPU elapsed-time conversion or message-success claim.')


def _transfer_domain(donor, target):
    """Only the encrypted-to-clear variant of the same native work domain."""
    old, new = donor['fields'], target['fields']
    _require(old['Encryption'] is True and new['Encryption'] is False and
             {key:value for key,value in old.items() if key!='Encryption'}==
             {key:value for key,value in new.items() if key!='Encryption'},
             'no calibrated cross-case domain for these manual configuration edits')
    def native_args(case, expected):
        args = case['native_args']
        at = args.index('--encrypt')
        _require(args.count('--encrypt')==1 and args[at+1]==expected,
                 'native encryption switch does not match its manual configuration')
        return args[:at]+args[at+2:]
    _require(native_args(donor,'on')==native_args(target,'off'), 'cross-case native argument domain differs')


def _trace_source_matches(trace, source):
    return (trace['shared_sources']==source['sources'] and
            trace['native_binary_sha256']==source['native_binary_sha256'] and
            set(source['native_artifacts'].values())=={trace['mapped_kernel_sha256']})


def _runtime_leaf_hashes():
    return {name:hashlib.sha256((ROOT/'tools'/name).read_bytes()).hexdigest() for name in _GPU_RUNTIME_LEAVES}


def _validate_cuda_trace(trace, source, leaf_hashes, *, needs_reference):
    allowed = {'bounded_mixed_precision_cost_regime_observed'}
    if not needs_reference:
        allowed.add('bounded_cuda_work_observed')
    _require(trace['schema']=='triad-gpu-workload-v2' and trace['unchanged_at_completion'] and
             trace['device']=='cuda' and trace['coarse_device']=='cuda' and trace['cuda_peak_allocated_bytes']>0 and
             trace['status'] in allowed, 'CUDA work trace has no qualified mixed-precision work reference')
    _require(_trace_source_matches(trace,source), 'GPU trace source or native artifacts differ')
    _require(_select(trace['sources'],leaf_hashes)==leaf_hashes,
             'GPU runtime leaf source differs from the tested backend')


def _same_cuda_reference_environment(trace, reference, indices):
    def policy(report):
        return {key:value for key,value in report['policy'].items() if key!='paired_reference_case_indices'}
    def environment_identity(report):
        identity = _select(report,_CUDA_REPORT_MATCH)
        for section,inventory in (('runtime','numerical_thread_pools'),('runtime_limits','pools')):
            values = identity[section]
            if inventory in values:
                ordered = sorted(values[inventory],key=lambda pool:json.dumps(pool,sort_keys=True))
                identity[section] = dict(values,**{inventory:ordered})
        return identity
    _require(environment_identity(trace)==environment_identity(reference) and
             policy(trace)==policy(reference),
             'cached CUDA reference runtime, precision, worker allocation or workspace differs')
    jit = trace['runtime'].get('cuda_jit',{})
    capability = jit.get('compute_capability',[])
    _require(bool(jit.get('triton_version')) and bool(jit.get('driver_version')) and
             len(capability)==2 and all(type(value) is int and value>=0 for value in capability) and capability[0]>0,
             'cached CUDA reference requires an observed Triton version, NVIDIA driver and GPU capability')
    reference_rows = [row for row in reference['cases'] if row['case']['index'] in indices]
    origin = _select(reference_rows[0]['gpu_coarse'],_COARSE_RUNTIME)
    _require(all(_select(row['gpu_coarse'],_COARSE_RUNTIME)==origin
                 for row in [*reference_rows,*trace['cases']]),
             'cached CUDA reference device or coarse library differs')


def _gpu_work_executed(row):
    coarse = row['gpu_coarse']['work']
    _require(coarse['frequency_batches']+coarse['lag_batches']>0 and
             coarse['mapped_projection_batches']+coarse['integer_projection_columns']>0,
             'CUDA coarse search or template projection did not execute')
    fine = row['devices']['cuda']['strata']['nominal_continuation']['records'][-1]
    cuda = fine['work']['native_cache']['accelerator']
    _require(cuda['mapped_values']>0 and cuda['block_statistics']>0 and
             (cuda.get('dft_values',0)>0 or cuda.get('dc_reduction_input_values',0)>0),
             'CUDA fine mapped basis or DFT projection did not execute')
    return (cuda['arithmetic']['precision_selection'],row['gpu_coarse']['precision']['grid_selection'])


def _reference_coverage(gpu, parsed, expected=()):
    policy = gpu['policy']
    declared = policy['paired_reference_case_indices']
    _require(bool(declared) and len(declared)==len(set(declared)) and
             (not expected or set(expected)==set(declared)) and
             0<=policy['score_atol']<=SCORE_ATOL and 0<=policy['score_rtol']<=SCORE_RTOL and
             policy['adaptive_cost_driver_relative_limit']==ADAPTIVE_WORK_LIMIT,
             'GPU reference coverage or preregistered tolerance differs')
    public = {row['index']:row for row in parsed['cases']}
    observed = {row['case']['index']:row for row in gpu['cases']}
    acquisition = {'status_match','coordinates_match','helper_work_match','candidate_order_match',
                   'candidate_scores_match','bounded_tile_work_match'}
    for index in declared:
        row = observed[index]
        config = ('configuration_hash','fields','native_args')
        _require(_select(row['case'],config)==_select(public[index],config) and 'error' not in row and
                 row['bank_parity']['accepted'] is True, f'GPU reference bank failed for row {index}')
        _gpu_work_executed(row)
        _require(set(row['acquisition_parity'])=={'data_acquisition','noise_acquisition'} and all(
            set(check)==acquisition and all(value is True for value in check.values())
            for check in row['acquisition_parity'].values()), f'GPU reference acquisition failed for row {index}')
        _require(set(row['parity'])==set(row['cost_regime'])==set(_FINE_STRATA) and all(
            row['parity'][name]['scores_match'] is True and row['cost_regime'][name]['accepted'] is True and
            0<=row['cost_regime'][name]['maximum_relative_adaptive_cost_driver_error']<=ADAPTIVE_WORK_LIMIT
            for name in _FINE_STRATA), f'GPU reference fine work or precision failed for row {index}')
    return declared


def _guarded_interval(component, relative_budget):
    interval = component.get(INTERVAL)
    if interval is None:
        return
    low, high = interval
    guarded = [low/(1+relative_budget), high/(1-relative_budget)]
    component.update(local_calibration_interval=interval,
                     margin_interval=[1-guarded[1],1-guarded[0]], **{INTERVAL:guarded})
    if component['realtime_status']=='pass' and guarded[1]>=1:
        component.update(realtime_status='unresolved',reason='holdout_error_allowance_reaches_realtime')


def _holdout_service(residuals, mismatches, fixed, groups, forecast, limits):
    """Use the same sum of maxima as the forecast, without cancellation."""
    required = {name for group in groups for name in group}
    missing = sorted((required-residuals.keys()) | (required & mismatches.keys()))
    if missing:
        return dict(accepted=False, status='partial_coverage', missing=missing)
    actual = fixed+sum(max(residuals[name]['observed_s_per_s'] for name in group) for group in groups)
    predicted = forecast[RATIO]
    _require(math.isfinite(actual) and actual>0 and math.isfinite(predicted) and predicted>=0,
             'invalid held-out whole-service estimate')
    composed = fixed+sum(max(residuals[name]['forecast_s_per_s'] for name in group) for group in groups)
    _require(math.isclose(predicted,composed,rel_tol=1.e-12), 'held-out and forecast cost composition differs')
    stage_errors = {name:abs(residuals[name]['forecast_s_per_s']-residuals[name]['observed_s_per_s'])/actual
                    for name in sorted(required)}
    combined = sum(max(stage_errors[name] for name in group) for group in groups)
    total_budget, stage_budget = limits
    interval = forecast.get(INTERVAL)
    inside = interval is None or interval[0]<=actual<=interval[1]
    accepted = combined<=total_budget and max(stage_errors.values())<=stage_budget and inside
    return dict(status='accepted' if accepted else 'cost_drift',accepted=accepted,
        observed_s_per_s=actual,forecast_s_per_s=predicted,relative_error=predicted/actual-1,
        no_cancellation_relative_error=combined,stage_absolute_error_share_of_observed=stage_errors,
        whole_service_relative_error_budget=total_budget,individual_stage_error_share_budget=stage_budget,
        inside_forecast_interval=inside if interval is not None else None)


def _heldout_acquisition(cpu, gpu, slot):
    """Check a fresh complete acquisition or its declared bounded cost work."""
    _require(cpu['status']==gpu['status'] and cpu['coordinates']==gpu['coordinates'],
             'independent local/GPU target acquisition differs')
    _acquisition_demand_match(cpu,gpu)
    if cpu['status']=='acquired':
        samples = cpu['elapsed_observations_seconds']
        _require(bool(samples) and len(samples)==(3 if samples[0]<=.5 else 1) and
                 all(math.isfinite(x) and x>0 for x in samples) and cpu['elapsed_seconds']==fmean(samples),
                 'held-out acquisition lacks the declared matched timing starts')
        return fmean(samples)/slot
    if cpu['status']!='partial_acquisition' or 'full_selection_holdout' not in cpu:
        return None
    plan, other_plan = cpu['full_plan'], gpu['full_plan']
    _require(_select(plan,_TILE)==_select(other_plan,_TILE),'held-out broad search plan differs')
    keys = ('completed_units','completed_frequency_points','raw_candidate_rows')
    steps = cpu['bounded_tiles']
    _require([_select(step,keys) for step in steps]==[_select(step,keys) for step in gpu['bounded_tiles']],
             'held-out broad search executed different bounded tiles')
    units = sum(step['completed_units'] for step in steps)
    points = sum(step['completed_frequency_points'] for step in steps)
    _require(units==min(4,plan['units']) and units>0 and
             points==min(plan['frequency_points'],units*plan['frequency_unit_points']),
             'held-out broad search lacks its declared four-unit work')
    selection = cpu['full_selection_holdout']
    _require(selection['exact_candidate_rows']==plan['raw_candidate_row_capacity'] and
             selection['score_order_strata']==['monotone','alternating','shuffled'] and
             len(selection['samples'])==3,
             'held-out full-count candidate-selection work differs')
    kernel_seconds = [step['timing']['total']['wall_seconds'] for step in steps]
    setup_seconds = cpu['elapsed_seconds']
    selection_cost = selection['samples']
    _require(all(math.isfinite(x) and x>0 for x in [setup_seconds,*kernel_seconds,*selection_cost]),
             'invalid held-out broad search timing')
    return (setup_seconds+sum(kernel_seconds)*plan['units']/units)/slot + max(selection_cost)


def _verification(forecast, calibration, trace, gpu_trace, *, transfer=False, target_noise=None):
    """Check a separately run CPU work observation against the GPU forecast."""
    input_relation = _holdout_input_relation(trace,gpu_trace)
    if input_relation['status']!='identical_input':
        return dict(status='unresolved',input_relation=input_relation,
                    reason='independent CPU/GPU target IQ bytes differ')
    cpu = trace['devices']['cpu']
    matched = _forecast_row(calibration, dict(trace, devices={'cuda':cpu}),
                            allow_approximation=transfer,transfer=transfer,target_noise=target_noise)
    geometry = trace['public_geometry']
    slot, fs = (geometry['conservative_public_slot_interval_seconds'],geometry['sample_rate_hz'])
    fine = cpu['strata']
    cuda = gpu_trace['devices']['cuda']
    cuda_fine = cuda['strata']
    observed = {}
    for name,local,gpu in (('data_acquisition',cpu['data_acquisition'],cuda['data_acquisition']),
                           ('noise_acquisition',cpu['noise_acquisition'],cuda['noise_acquisition']),
                           ('recurring_acquisition',trace['recurring_acquisition'],gpu_trace['recurring_acquisition'])):
        value = _heldout_acquisition(local,gpu,slot)
        if value is not None:
            observed[name] = value
    for name in _FINE_STRATA:
        earlier = None
        for index,(local,gpu) in enumerate(zip(fine[name]['records'],cuda_fine[name]['records'],strict=True)):
            _require(_score_match(local['lane_scores'],gpu['lane_scores']) and
                     local['work'].get('probe_endpoint')==gpu['work'].get('probe_endpoint'),
                     'independent local/GPU score, bit order or threshold-side mismatch: '+name)
            nominal = name=='nominal_continuation'
            if not nominal or index<4:
                check = _check_fine(local['work'],gpu['work'],local['work']['native_cache'],
                                    nominal=nominal,earlier=earlier)
                earlier = check['drivers']
    phase = fine['nominal_continuation']['phase_timing']
    _require(len(phase)==5 and phase[0]['phase']=='prefix' and
             phase[-1]['phase']=='finish_classify_close' and
             all(row['phase']=='continuation' for row in phase[1:4]) and
             sum(row['represented_baseband_seconds'] for row in phase[1:4])==4,
             'held-out continuation phases incomplete')
    nominal_fine = (phase[0]['elapsed_seconds']+phase[-1]['elapsed_seconds']+
        (geometry['search_window_samples']/fs-2)*sum(row['elapsed_seconds'] for row in phase[1:4])/4)/slot
    observed.update(nominal_fine=nominal_fine,
        recurring_bank_preparation=max(row['elapsed_seconds'] for row in trace['recurring_banks'])/slot)
    for name in ('terminal','noise_only','maximum_state_transition'):
        work = matched['components'][name].get('gpu_demand')
        if work is not None:
            observed[name] = fine[name]['elapsed_seconds']*work['forecast_work_rows']/work['probe_rows']/slot
    erasure = fine['interior_erasure']
    represented = erasure['represented_baseband_seconds']
    _require(represented>0 and math.isfinite(represented) and
             math.isfinite(erasure['elapsed_seconds']) and erasure['elapsed_seconds']>0,
             'invalid held-out erasure timing')
    erasure_service = erasure['elapsed_seconds']*geometry['maximum_capacity_window_seconds']/represented/slot
    cap_service = observed.get('maximum_state_transition')
    erasure_covered = cap_service is not None and erasure_service<=cap_service
    residuals = {}
    for name,actual in observed.items():
        predicted=forecast['components'].get(name,{}).get(RATIO)
        if predicted is not None:
            _require(math.isfinite(actual) and actual>0 and math.isfinite(predicted) and predicted>=0,
                     'invalid independent CPU service timing')
            residuals[name]=dict(forecast_s_per_s=predicted, observed_s_per_s=actual,
                absolute_error_s_per_s=abs(predicted-actual),relative_error=predicted/actual-1)
    fixed = sum(calibration['measurement']['nominal_service']['components'][name][RATIO] for name in _FIXED)
    mismatches = matched['missing_or_mismatched'].copy()
    if not erasure_covered:
        mismatches['interior_erasure'] = 'held-out erasure cost exceeds independently observed capped work'
    nominal = _holdout_service(residuals,mismatches,fixed,_NOMINAL_TIMING,forecast['nominal_service'],
                              _HOLDOUT_BUDGETS['nominal_service'])
    ceiling = _holdout_service(residuals,mismatches,fixed,_CEILING_TIMING,forecast['complete_capacity'],
                              _HOLDOUT_BUDGETS['complete_capacity'])
    if 'interior_erasure' in mismatches:
        ceiling.update(status='partial_coverage',accepted=False,
                       missing=sorted(set(ceiling.get('missing',()))|{'interior_erasure'}))
    missing = sorted(set(forecast['components'])-set(residuals)-{'interior_erasure'})
    status = ('cost_drift' if 'cost_drift' in (nominal['status'],ceiling['status']) else
              'partial_coverage' if missing or mismatches else 'accepted')
    return dict(status=status,input_relation=input_relation,
        nominal_holdout_accepted=nominal['accepted'], nominal_service=nominal,complete_capacity=ceiling,
        components=residuals,actual_nominal_s_per_s=nominal.get('observed_s_per_s'),
        forecast_nominal_s_per_s=nominal.get('forecast_s_per_s'),relative_nominal_error=nominal.get('relative_error'),
        missing_held_out_stage_timings=missing,
        mismatched_held_out_calibration_stages=mismatches,fixed_cpu_calibration_s_per_s=fixed,
        held_out_erasure=dict(observed_s_per_s=erasure_service,observed_state_ceiling_s_per_s=cap_service,
                              covered_by_state_ceiling=erasure_covered),
        scope='Independent bounded CPU DSP work and separately calibrated fixed CPU overhead. Broad search uses actual 1/2/4 tiles and full-count selection, not full-grid elapsed time.')


def _holdout_input_relation(cpu, gpu):
    _require(cpu['evaluator']==gpu['evaluator'], 'independent CPU/GPU target evaluator differs')
    _require(cpu['validity_sha256']==gpu['validity_sha256'], 'independent CPU/GPU target validity differs')
    return dict(status=('identical_input' if cpu['input_sha256']==gpu['input_sha256']
                        else 'same_evaluator_distinct_input_bytes'),
                cpu_input_sha256=cpu['input_sha256'],gpu_input_sha256=gpu['input_sha256'],
                cpu_validity_sha256=cpu['validity_sha256'],gpu_validity_sha256=gpu['validity_sha256'])


def _gate_target_verdicts(row, direct):
    verification = row.get('independent_cpu_trace',{})
    actual_nominal = verification.get('actual_nominal_s_per_s')
    contradictions = {}
    for name in ('nominal_service','complete_capacity'):
        forecast = row.get(name,{})
        verdict = forecast.get('realtime_status')
        target = direct.get(name,{})
        direct_verdict = target.get('realtime_status')
        if name=='nominal_service':
            points = [value for value in (actual_nominal,target.get(RATIO)) if value is not None]
            differs = any((value>=1)!=(verdict=='fail') for value in points)
        else:
            differs = direct_verdict in ('pass','fail') and direct_verdict!=verdict
            differs |= 'nominal_service' in contradictions and forecast.get('reason')=='nominal_service_failure'
            points = []
        if verdict in ('pass','fail') and differs:
            contradictions[name] = dict(forecast_verdict=verdict,direct_target_verdict=direct_verdict,
                                        held_out_target_points_s_per_s=points)
            forecast.update(realtime_status='unresolved',reason='independent_target_timing_contradicts_verdict')
    if contradictions:
        row.update(status='unresolved',held_out_target_contradictions=contradictions)


def _validate_target_noise_report(report, parsed, source, runtime, cpu_budget, workspace_bytes):
    _require(report['schema']=='triad-manual-stage-calibration-v1' and
             report['calibration_stage']=='noise-only-fine' and report['status']=='stage_calibration_measured' and
             report['provenance']['unchanged_at_completion'] and same_measured_source(report['provenance'],source) and
             report['input']['unchanged_at_completion'] and report['input']['source_sha256']==parsed['source_sha256'] and
             report['runtime']['unchanged_at_completion'] and
             {key:value for key,value in report['runtime'].items() if key!='unchanged_at_completion'}==runtime and
             report['machine']['cpu']==cpu_budget and report['worker_allocations']==[cpu_budget['available_cpus']] and
             report['workspace_limit_bytes']==workspace_bytes,
             'target noise-only calibration identity or allocation differs')


def estimate(cpu, gpu, parsed, source, runtime, cpu_budget, *, holdout=None, holdout_indices=(),
             reference_indices=(), gpu_reference=None, transfers=(), target_noise_indices=(),
             target_noise_report=None, case_indices=()):
    start = time.perf_counter()
    try:
        _require(cpu['schema']=='triad-manual-complete-capacity-v3' and gpu['schema']=='triad-gpu-workload-v2',
                 'unsupported CPU calibration or GPU work trace schema')
        _require(cpu['provenance']['unchanged_at_completion'] and cpu['runtime']['unchanged_at_completion']
                 and cpu['input']['unchanged_at_completion'] and cpu['message_gate']['passed'],
                 'local calibration identity or functional message gate did not pass')
        _require(same_measured_source(cpu['provenance'],source), 'current local source or binary differs')
        _require({key:value for key,value in cpu['runtime'].items() if key!='unchanged_at_completion'}==runtime
                 and cpu['machine']['cpu']==cpu_budget, 'local calibration CPU/runtime allocation differs')
        leaf_hashes = _runtime_leaf_hashes()
        _validate_cuda_trace(gpu,source,leaf_hashes,needs_reference=gpu_reference is None)
        _require(gpu['input']['source_sha256']==parsed['source_sha256'],
                 'GPU trace manual list differs from current input')
        same_manual_list = cpu['input']['source_sha256']==parsed['source_sha256']
        _require(gpu['memory_limit_bytes']==cpu['workspace_limit_bytes'],
                 'GPU trace and target CPU workspace caps differ')
        reference = gpu if gpu_reference is None else gpu_reference
        if gpu_reference is not None:
            _validate_cuda_trace(reference,source,leaf_hashes,needs_reference=True)
        qualified_references = _reference_coverage(reference,parsed,reference_indices)
        precision_references = {_gpu_work_executed(row) for row in reference['cases']
                                if row['case']['index'] in qualified_references}
        if gpu_reference is not None:
            _same_cuda_reference_environment(gpu,reference,qualified_references)
        problem = None
    except (IndexError,KeyError,TypeError,ValueError,OSError) as error:
        leaf_hashes = {}
        qualified_references = []
        precision_references = set()
        problem = str(error)
    def ordered(rows):
        indexed = {}
        for row in rows:
            key = row['case']['index']
            _require(key not in indexed,'duplicate result index')
            indexed[key] = row
        return indexed
    try:
        local,remote=ordered(cpu['cases']),ordered(gpu['cases'])
    except (KeyError,TypeError,ValueError) as error:
        local=remote={}
        problem=problem or str(error)
    target_noise_rows = {}
    target_noise_problem = None
    if target_noise_report is not None:
        try:
            _validate_target_noise_report(target_noise_report,parsed,source,runtime,cpu_budget,
                                          cpu['workspace_limit_bytes'])
            target_noise_rows = ordered(target_noise_report['cases'])
            _require(set(target_noise_rows)==set(target_noise_indices),
                     'target noise-only calibration indices differ from declaration')
        except (KeyError,TypeError,ValueError) as error:
            target_noise_problem=str(error)
    independent = {}
    holdout_problem = None
    if holdout is not None:
        try:
            _require(bool(holdout_indices) and holdout['schema']=='triad-gpu-workload-v2' and
                holdout['device']=='cpu' and holdout['status']=='cpu_reference_only' and
                holdout['unchanged_at_completion'] and _trace_source_matches(holdout,source) and
                holdout['runtime']==runtime and holdout['cpu_budget']==cpu_budget and
                holdout['workers']==cpu_budget['available_cpus'] and
                holdout['input']['source_sha256']==parsed['source_sha256'],
                'independent local CPU holdout provenance/allocation differs')
            _require(holdout['memory_limit_bytes']==cpu['workspace_limit_bytes'],
                f"independent local CPU holdout workspace {holdout['memory_limit_bytes']} bytes differs "
                f"from calibrated {cpu['workspace_limit_bytes']} bytes")
            driver = _GPU_RUNTIME_LEAVES[0]
            _require(holdout['sources'][driver]==leaf_hashes[driver],
                     'independent local CPU holdout driver differs')
            independent = ordered(holdout['cases'])
            _require(set(independent)==set(holdout_indices), 'independent CPU holdout indices differ from declaration')
        except (KeyError,TypeError,ValueError) as error:
            independent={}
            holdout_problem=str(error)
    rows=[]
    donors = dict(transfers)
    current = {case['index']:case for case in parsed['cases']}
    for case in parsed['cases']:
        index=case['index']
        if case_indices and index not in case_indices:
            continue
        donor_index=donors.get(index,index)
        transfer=index!=donor_index
        target_noise = None
        row=dict(case_index=index, heading=case['heading'],configuration_hash=case['configuration_hash'],
            **_capacity_metadata(local.get(donor_index,{}).get('measurement',{})))
        if transfer:
            row['cross_case_calibration'] = dict(donor_case_index=donor_index,
                donor_manual_list_sha256=cpu.get('input',{}).get('source_sha256'),
                target_manual_list_sha256=parsed['source_sha256'],
                target_cpu_timing_excluded_from_fit=True,
                target_cpu_timing_excluded_from_nominal_fit=True,
                target_cpu_calibrated_stages=[],
                domain='same public geometry, encryption on to off')
        try:
            _require(problem is None,problem)
            _require(same_manual_list or transfer,
                     'edited manual list requires an explicit validated donor for this row')
            _require(donor_index in local and index in remote,
                     'current row missing from CPU calibration or GPU work trace')
            if transfer:
                _require(index in holdout_indices, 'cross-case target requires a predeclared independent CPU trace')
                _transfer_domain(current[donor_index],case)
            for actual,expected in ((local[donor_index],current[donor_index]),(remote[index],case)):
                _require(actual['case']['configuration_hash']==expected['configuration_hash'] and 'error' not in actual,
                         'matched current case result missing')
            if transfer:
                _require(all(local[donor_index]['case'][key]==current[donor_index][key]
                             for key in ('fields','native_args')), 'calibration donor configuration differs')
            _require(local[donor_index]['measurement']['inference_workers']==cpu_budget['available_cpus'],
                     'CPU calibration did not measure the full logical allocation')
            if index in target_noise_indices:
                target_rows = local if target_noise_report is None else target_noise_rows
                _require(target_noise_problem is None,target_noise_problem)
                _require(transfer and index in target_rows and (target_noise_report is not None or same_manual_list),
                         'noise-only calibration requires a current target CPU report and an explicit donor')
                target = target_rows[index]
                measured = target['measurement']
                _require(target['case']['configuration_hash']==case['configuration_hash'] and
                         all(target['case'][key]==case[key] for key in ('fields','native_args')) and
                         'error' not in target and
                         measured['inference_allocation_mode']=='full_logical' and
                         measured['inference_workers']==cpu_budget['available_cpus'] and
                         measured['workspace_margin']['status']=='within_budget' and
                         target['calibration_inputs']==remote[index]['evaluator'] and
                         _select(target['public_geometry'],_GEOMETRY)==_select(remote[index]['public_geometry'],_GEOMETRY),
                         'target noise-only calibration source, allocation or geometry differs')
                target_noise = measured['noise_only_fine']
                row['cross_case_calibration'].update(target_cpu_timing_excluded_from_fit=False,
                    target_cpu_calibrated_stages=['noise_only'],target_cpu_noise_calibration_source=(
                        'full_current_cpu_report' if target_noise_report is None else 'standalone_noise_only_report'))
            _require(_gpu_work_executed(remote[index]) in precision_references,
                     'CUDA coarse/fine precision class lacks a paired reference')
            row.update(_forecast_row(local[donor_index],remote[index],
                allow_approximation=bool(qualified_references),transfer=transfer,target_noise=target_noise))
            for scope, (relative_budget, _) in _HOLDOUT_BUDGETS.items():
                _guarded_interval(row[scope],relative_budget)
            points = (row['nominal_service'][RATIO] is not None and row['complete_capacity'][RATIO] is not None
                      and not row['missing_or_mismatched'])
            row['status'] = ('conditional_estimate' if points and
                row['complete_capacity']['all_component_models_supported'] else
                'point_estimate_with_unresolved_support' if points else 'unresolved')
        except (KeyError,TypeError,ValueError,IndexError,OverflowError) as error:
            row.update(status='unresolved',reason=str(error))
        if index in holdout_indices:
            try:
                _require(holdout_problem is None and index in independent,
                         holdout_problem or 'independent local holdout missing')
                actual = independent[index]
                _require(actual['case']['configuration_hash']==case['configuration_hash'] and
                         'error' not in actual and 'components' in row, 'held-out current row unavailable')
                row['independent_cpu_trace'] = _verification(row,local[donor_index],actual,remote[index],
                                                            transfer=transfer,target_noise=target_noise)
            except (KeyError,TypeError,ValueError,IndexError,OverflowError) as error:
                row['independent_cpu_trace'] = dict(status='unresolved',reason=str(error))
            trace_verification = row['independent_cpu_trace']
            if trace_verification['status']!='accepted':
                row['status'] = 'unresolved'
                for name in _HOLDOUT_BUDGETS:
                    component = row.get(name,{})
                    if (not trace_verification.get(name,{}).get('accepted') and
                            component.get('realtime_status') in ('pass','fail')):
                        component.update(realtime_status='unresolved',reason='independent_target_holdout_not_accepted')
        if transfer:
            trace_verification = row.get('independent_cpu_trace',{})
            verification = trace_verification.get('status','unresolved')
            row['cross_case_calibration']['target_holdout_status'] = verification
            direct = {}
            if problem is None and index in local and local[index]['case']['configuration_hash']==case['configuration_hash']:
                direct = local[index].get('measurement',{})
                errors = {}
                for name in ('nominal_service','complete_capacity'):
                    expected = direct.get(name,{}).get(RATIO)
                    predicted = row.get(name,{}).get(RATIO)
                    if expected and predicted is not None:
                        errors[name] = dict(direct_target_s_per_s=expected, forecast_s_per_s=predicted,
                                            relative_error=predicted/expected-1)
                key = ('target_comparison_with_noise_calibration' if target_noise is not None
                       else 'withheld_complete_target_comparison')
                row['cross_case_calibration'][key] = errors
            _gate_target_verdicts(row,direct)
        elif index in holdout_indices:
            _gate_target_verdicts(row,{})
        rows.append(row)
    verified = [row['case_index'] for row in rows
                if row.get('independent_cpu_trace',{}).get('status')=='accepted']
    verified_nominal = [row['case_index'] for row in rows
                        if row.get('independent_cpu_trace',{}).get('nominal_holdout_accepted')]
    status = ('unresolved' if not rows or any(row['status']=='unresolved' for row in rows)
              or set(verified)!=set(holdout_indices) else
              'conditional_estimates' if all(row['status']=='conditional_estimate' for row in rows) else
              'complete_point_estimates_with_unresolved_support')
    return dict(schema='triad-gpu-local-cpu-forecast-v2', cases=rows,
        status=status, **_capacity_metadata(cpu),
        nominal_point_estimates=sum(row.get('nominal_service',{}).get(RATIO) is not None for row in rows),
        ceiling_point_estimates=sum(row.get('complete_capacity',{}).get(RATIO) is not None for row in rows),
        source_revision=source['revision'], manual_list_sha256=parsed['source_sha256'],
        cpu_calibration_manual_list_sha256=cpu.get('input',{}).get('source_sha256'),
        forecast_selected_indices=[row['case_index'] for row in rows],
        forecast_selects_complete_current_list=len(rows)==len(parsed['cases']),
        gpu_runtime_leaf_sha256=leaf_hashes,
        qualified_mixed_precision_reference_indices=qualified_references,
        qualified_cuda_precision_classes=[dict(fine=fine,coarse=coarse)
                                          for fine,coarse in sorted(precision_references)],
        separate_cached_cuda_reference_used=gpu_reference is not None and bool(qualified_references),
        approximate_work_policy=dict(adaptive_relative_limit=ADAPTIVE_WORK_LIMIT,
            callback_relative_limit=CALLBACK_WORK_LIMIT,callback_absolute_allowance=CALLBACK_ABSOLUTE_ALLOWANCE,
            cross_case_native_full_tile_ratio_interval=[1/NATIVE_TRANSFER_RATIO_LIMIT,NATIVE_TRANSFER_RATIO_LIMIT]),
        calibration_elapsed_seconds=cpu.get('suite_observed_elapsed_seconds'),
        target_noise_calibration_elapsed_seconds=(None if target_noise_report is None else
                                                  target_noise_report.get('suite_observed_elapsed_seconds')),
        gpu_trace_elapsed_seconds=gpu.get('trace_observed_elapsed_seconds'),
        local_holdout_elapsed_seconds=holdout.get('trace_observed_elapsed_seconds') if holdout else None,
        local_holdout_declared_indices=list(holdout_indices), local_holdout_accepted_indices=verified,
        local_nominal_holdout_accepted_indices=verified_nominal,
        held_out_error_policy={scope:dict(maximum_whole_service_error=total,maximum_stage_contribution=stage)
                               for scope,(total,stage) in _HOLDOUT_BUDGETS.items()},
        estimator_elapsed_seconds=time.perf_counter()-start,
        holdout_rule='Reserve the final local outer observation; use its four-unit result for fine branches. Recurring banks use the second independent ordinal. Fixed stages are measured locally and charged in full.')


def _cpu_operating_conditions():
    """Optional observations; never use instantaneous clocks to scale costs."""
    def read(path):
        try:
            return Path(path).read_text().strip()
        except OSError:
            return None
    temperatures = [value for directory in Path('/sys/class/hwmon').glob('hwmon*')
                    if read(directory/'name') in ('k10temp','coretemp')
                    if (value := read(directory/'temp1_input')) is not None]
    return dict(platform_profile=read('/sys/firmware/acpi/platform_profile'),
        energy_preference=read('/sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference'),
        governor=read('/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor'),
        ac_online=read('/sys/class/power_supply/AC/online'),cpu_sensor_millidegrees_C=temperatures)


def _refresh_cpu_phase(phase, previous, arguments, output, case_list, input_artifacts):
    """Fit one phase, seal its forecast, then time one fresh receiver."""
    from triad_abbreviated import environment
    from triad_case_capacity import (PreparedCapacityCase, build_recurring_public_bank, measure_acquisition,
                                    measure_sustained_fine, noise_only_observation)
    from triad_case_metrics import machine_resources
    from triad_case_suite import native_plan
    from triad_gpu_workload import _acquisition as holdout_acquisition, _continuation_sample
    from triad_receiver import CompleteBitReceiver

    began = time.perf_counter()
    nominal = phase=='nominal-fine'
    scope = 'nominal_service' if nominal else 'complete_capacity'
    prior, = previous['cases']
    independent = prior.get('independent_cpu_trace',{})
    _require(not prior.get('missing_or_mismatched') and
             independent.get('input_relation',{}).get('status')=='identical_input' and
             independent.get(scope,{}).get('observed_s_per_s') is not None,
             'phase refresh requires matching original CPU, GPU and independent stage evidence')
    index = prior['case_index']
    cpu,gpu,holdout = (arguments[name] for name in ('cpu','gpu','holdout'))
    source,runtime,budget = (arguments[name] for name in ('source','runtime','cpu_budget'))
    case = next(row for row in arguments['parsed']['cases'] if row['index']==index)
    local = next(row for row in cpu['cases'] if row['case']['index']==index)
    remote = next(row for row in gpu['cases'] if row['case']['index']==index)
    old_trace, = holdout['cases']
    cap = cpu['workspace_limit_bytes']
    workers = budget['available_cpus']
    resources = machine_resources()
    _require(resources['cpu']==budget and (resources['memory_budget_bytes'] or 0)>=cap,
             'phase refresh requires the exact calibrated CPU allocation and workspace')
    executable = Path(source['native_binary'])
    plan = native_plan(executable,case['native_args'])
    family_size = cpu['policy']['family_size']
    tool_sha = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    before = _cpu_operating_conditions()
    evidence = dict(phase=phase,case_index=index,configuration_hash=case['configuration_hash'],status='prediction_sealed',
        family_size=family_size,workspace_bytes=cap,workers=workers,
        measured_cpu_source=source,runtime=runtime,cpu_budget=budget,
        input_artifacts=input_artifacts,estimator_sha256=tool_sha,operating_conditions_before=before,
        previous_holdout=independent[scope],
        scope='Only the selected CPU phase is refreshed in the recorded operating context. Unchanged independent component observations remain fixed verification evidence from their earlier runs.')
    prepare_began = time.perf_counter()
    with PreparedCapacityCase(executable,case,plan,memory_limit_bytes=cap,bank_workers=workers,
                              noise_only=not nominal) as prepared:
        _require(_select(prepared.public_geometry,_GEOMETRY)==_select(remote['public_geometry'],_GEOMETRY),
                 'fresh phase fixture differs from public GPU geometry')
        if nominal:
            def open_receiver():
                return prepared.receiver(workers)
        else:
            recurring = prepared.recurring_window['index']
            _require(recurring==remote['recurring_acquisition']['public_slot_index']==
                     old_trace['recurring_acquisition']['public_slot_index'], 'largest recurring ordinal differs')
            slot = prepared.calendar['plans'][recurring]
            prepared.screen = prepared.bank.screen = None
            bank = build_recurring_public_bank(prepared,slot,workers=workers)
            noise_only_observation(prepared,seed=remote['evaluator']['recurring_noise_only_seed'],
                                   samples=slot['receiver_window_samples'])
            manifest = dict(bank['manifest'],power=dict(bank['manifest']['power'],
                                                       complex_noise_variance=prepared.noise_variance))
            def open_receiver():
                return CompleteBitReceiver(manifest,bank['screen'],prepared.basis_source,
                    workers=workers,memory_limit_bytes=prepared.receiver_memory_limit_bytes)
            evidence['recurring_public_slot_index'] = recurring
        data_sha = hashlib.sha256(memoryview(prepared.raw).cast('B')).hexdigest()
        mask_sha = hashlib.sha256(memoryview(prepared.valid).cast('B')).hexdigest()
        if nominal:
            _require(all((row['input_sha256'],row['validity_sha256'])==(data_sha,mask_sha) for row in (remote,old_trace)),
                     'fresh nominal fixture differs from GPU or original local IQ')
        evidence['fixture_preparation_seconds'] = time.perf_counter()-prepare_began
        fit_began = time.perf_counter()
        with open_receiver() as receiver:
            if nominal:
                acquired = prepared.acquisition(receiver)['result']
                session = acquired.pop('coarse_session',None)
                if session is not None:
                    session.close()
                coordinates = acquired.get('coordinates')
                _require(acquired['status']=='acquired' and coordinates and
                         json.loads(json.dumps(coordinates))==remote['devices']['cuda']['data_acquisition']['coordinates'],
                         'fresh nominal fixture did not acquire the same public coordinates')
                fit = measure_sustained_fine(prepared,receiver,coordinates,family_size=family_size,maximum_look=9)
                _require(fit['successful_outer_observations'] in (3,5,9),'bounded nominal fine fit incomplete')
                measured = dict(local['measurement'],fine_active=fit)
            else:
                fit = measure_acquisition(prepared,receiver,family_size=family_size)
                window = dict(local['measurement']['recurring_window'],noise_only_acquisition=fit)
                measured = dict(local['measurement'],recurring_window=window)
        evidence['fit_seconds'] = time.perf_counter()-fit_began
        updated = dict(local,measurement=measured)
        prediction = _forecast_row(updated,remote,allow_approximation=True)
        _require(not prediction['missing_or_mismatched'],'fresh CPU phase does not match GPU work')
        for name,(limit,_) in _HOLDOUT_BUDGETS.items():
            _guarded_interval(prediction[name],limit)
        sealed = dict(training_observations=fit,prediction={name:_select(prediction[name],(RATIO,INTERVAL))
                                                           for name in _HOLDOUT_BUDGETS})
        evidence.update(fixture_kind='data' if nominal else 'seeded_recurring_h0',
            input_sha256=data_sha,validity_sha256=mask_sha,pre_external_holdout=sealed,
            pre_external_holdout_sha256=hashlib.sha256(json.dumps(sealed,sort_keys=True,allow_nan=False).encode()).hexdigest(),
            operating_conditions_before_holdout=_cpu_operating_conditions())
        output.parent.mkdir(parents=True,exist_ok=True)
        output.touch(exist_ok=False)
        write_result(output,dict(schema='triad-gpu-local-cpu-phase-refresh-v1',status='prediction_sealed',phase_refresh=evidence))
        holdout_began = time.perf_counter()
        with open_receiver() as receiver:
            if nominal:
                fresh = _continuation_sample(prepared,receiver,coordinates,lambda:None)
            else:
                fresh = holdout_acquisition(prepared,receiver,independent_cost_holdout=True)
                fresh['public_slot_index'] = recurring
        evidence['external_phase_holdout_seconds'] = time.perf_counter()-holdout_began
    after = _cpu_operating_conditions()
    _require(all(before[key]==after[key]==evidence['operating_conditions_before_holdout'][key]
                 for key in ('platform_profile','energy_preference','governor','ac_online')) and
             same_measured_source(source,identity(executable)) and environment('cpu')==runtime and
             machine_resources()['cpu']==budget and load_cases(case_list)['source_sha256']==arguments['parsed']['source_sha256'] and
             gpu['sources']==_runtime_leaf_hashes() and hashlib.sha256(Path(__file__).read_bytes()).hexdigest()==tool_sha,
             'source, runtime, case list or operating profile changed during the phase refresh')
    # Compare a live receipt using the same JSON shapes as archived traces.
    fresh = json.loads(json.dumps(fresh,allow_nan=False))
    if nominal:
        old_cpu = old_trace['devices']['cpu']
        mixed = dict(old_trace,devices={'cpu':dict(old_cpu,strata=dict(old_cpu['strata'],nominal_continuation=fresh))})
    else:
        mixed = dict(old_trace,recurring_acquisition=fresh)
    refreshed_cpu = dict(cpu,cases=[updated if row['case']['index']==index else row for row in cpu['cases']])
    result = estimate(**dict(arguments,cpu=refreshed_cpu,holdout=dict(holdout,cases=[mixed])))
    current, = result['cases']
    _require(all(_select(current[name],(RATIO,INTERVAL))==sealed['prediction'][name] for name in _HOLDOUT_BUDGETS),
             'verified forecast differs from the sealed pre-holdout point or interval')
    accepted = current.get('independent_cpu_trace',{}).get(scope,{}).get('accepted') is True
    model_status = current['components']['nominal_fine' if nominal else 'recurring_acquisition']['model_status']
    evidence.update(status='phase_timing_supported' if accepted else 'phase_timing_unresolved',
        model_status=model_status,external_phase_observation=fresh,operating_conditions_after=after,
        elapsed_seconds=time.perf_counter()-began)
    result['phase_refresh'] = evidence
    return result


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__,allow_abbrev=False)
    parser.add_argument('--cpu-report',type=Path,required=True)
    parser.add_argument('--gpu-report',type=Path,required=True)
    parser.add_argument('--gpu-reference-report',type=Path,
                        help='separate current-runtime qualified paired CUDA references for a targeted trace')
    parser.add_argument('--local-holdout-report',type=Path)
    parser.add_argument('--holdout-case-index',type=int,action='append',default=[])
    parser.add_argument('--reference-case-index',type=int,action='append',default=[])
    parser.add_argument('--case-index',type=int,action='append',default=[],
                        help='forecast a selected current row without implying full-list coverage')
    parser.add_argument('--refresh-cpu-phase',choices=('nominal-fine','recurring-acquisition'),
                        help='refit one same-row CPU phase and measure one fresh independent observation')
    parser.add_argument('--transfer-case',action='append',default=[],metavar='TARGET=DONOR',
                        help='predeclare a same-geometry encrypted donor for a clear target excluded from the fit')
    parser.add_argument('--calibrate-target-noise-case-index',type=int,action='append',default=[],
                        help='use only the target CPU noise-only fit for this explicit transfer')
    parser.add_argument('--target-noise-calibration-report',type=Path,
                        help='optional separate source-bound noise-only stage calibration for those targets')
    parser.add_argument('--case-list',type=Path,default=DEFAULT_CASES)
    parser.add_argument('--exporter',type=Path,default=ROOT/'build/dev/triad_helper_export')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(argv)
    from triad_abbreviated import environment
    from triad_cpu import configure_numerical_threads, detect_cpu_budget, resolve_cpu_budget
    try:
        parsed=load_cases(args.case_list)
        source=identity(args.exporter.resolve())
        cpu_budget=detect_cpu_budget()
        configure_numerical_threads(resolve_cpu_budget(jobs=1,workers=cpu_budget['available_cpus'],threads=1))
        input_artifacts={}
        def load_report(name,path):
            raw=path.read_bytes()
            if args.refresh_cpu_phase:
                input_artifacts[name]=dict(path=str(path),sha256=hashlib.sha256(raw).hexdigest())
            return json.loads(raw)
        cpu=load_report('cpu_calibration',args.cpu_report)
        gpu=load_report('gpu_work',args.gpu_report)
        declared=tuple(args.holdout_case_index)
        indices={case['index'] for case in parsed['cases']}
        requested=tuple(args.case_index)
        selected=set(requested) if requested else indices
        if not selected<=indices or (requested and len(selected)!=len(requested)):
            raise ValueError('selected case indices must be distinct current rows')
        if (bool(declared)!=bool(args.local_holdout_report) or len(declared)!=len(set(declared)) or
                not set(declared)<=selected):
            raise ValueError('supply the independent holdout report and its distinct predeclared selected indices together')
        transfers=[]
        for item in args.transfer_case:
            target,donor=map(int,item.split('='))
            if target==donor or target not in declared or donor not in indices:
                raise ValueError('cross-case transfer requires different current donor and predeclared held-out target rows')
            transfers.append((target,donor))
        if len(transfers)!=len(dict(transfers)) or set(dict(transfers)) & set(dict(transfers).values()):
            raise ValueError('cross-case targets must be distinct and withheld from all donor fits')
        target_noise = args.calibrate_target_noise_case_index
        if len(target_noise)!=len(set(target_noise)) or not set(target_noise)<=dict(transfers).keys():
            raise ValueError('noise-only calibration requires distinct explicit transfer targets')
        if args.target_noise_calibration_report and not target_noise:
            raise ValueError('supply the separate stage report with its declared noise-only target indices')
        holdout=load_report('prior_holdout',args.local_holdout_report) if args.local_holdout_report else None
        gpu_reference=load_report('gpu_reference',args.gpu_reference_report) if args.gpu_reference_report else None
        noise_report=(json.loads(args.target_noise_calibration_report.read_text())
                      if args.target_noise_calibration_report else None)
        if args.refresh_cpu_phase and (len(requested)!=1 or requested!=declared or transfers or args.output.exists()):
            raise ValueError('phase refresh requires one same-row selection, its earlier holdout and a new output path')
        options=dict(cpu=cpu,gpu=gpu,parsed=parsed,source=source,runtime=environment('cpu'),cpu_budget=cpu_budget,
                     holdout=holdout,holdout_indices=declared,reference_indices=args.reference_case_index,
                     gpu_reference=gpu_reference,transfers=transfers,target_noise_indices=target_noise,
                     target_noise_report=noise_report,case_indices=requested)
        result=estimate(**options)
        if args.refresh_cpu_phase:
            result=_refresh_cpu_phase(args.refresh_cpu_phase,result,options,args.output,args.case_list,input_artifacts)
        write_result(args.output,result)
    except (OSError,ValueError,RuntimeError) as error:
        parser.error(str(error))
    summary=dict(status=result['status'],capacity_scope=result['capacity_scope'],
                 blind_fresh_start_capacity=result['blind_fresh_start_capacity']['status'])
    if args.refresh_cpu_phase:
        summary['phase_refresh']=result['phase_refresh']['status']
    print(json.dumps(summary))
    return 0 if result['status']=='conditional_estimates' else 2


if __name__ == '__main__':
    raise SystemExit(main())
