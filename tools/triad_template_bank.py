"""Generate a public keyed DATA bank without simulating an observation.

The native source retains its fixed shared power calibration. Each call owns
and closes a native source process; no received decision chooses the key bank.
Only public fixture key seeds are supported; private key files are not inputs.
For bounded campaigns run the CLI in an externally timed process group; Python
worker cancellation does not interrupt a blocked native protocol read.
"""
from __future__ import annotations

import hashlib
import math
from pathlib import Path
import resource
import time

import numpy as np

from triad_continuous import NativeStream


def child_cpu_seconds():
    r=resource.getrusage(resource.RUSAGE_CHILDREN)
    return r.ru_utime+r.ru_stime


def _digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb')as stream:
        while block:=stream.read(1<<20):h.update(block)
    return h.hexdigest()


def _validate_arguments(argv):
    if not isinstance(argv,(list,tuple))or len(argv)<2 or any(not isinstance(x,str)for x in argv):
        raise ValueError('native argv must include an executable and string arguments')
    for flag in ('--output','--templates','--stream-server','--stream-kind','--plan-only','--key-file','--now'):
        if flag in argv:raise ValueError('template source argv must omit '+flag)
    if argv.count('--power-reference')!=1:
        raise ValueError('explicit whole-active power reference required')
    at=argv.index('--power-reference')
    if at+1==len(argv)or argv[at+1]!='whole-active':
        raise ValueError('explicit whole-active power reference required')
    if argv.count('--epoch')!=1 or argv.count('--key-seed')!=1:
        raise ValueError('explicit public epoch and key seed required')


def _public_integer_argument(argv, flag, expected):
    at=argv.index(flag)
    value=argv[at+1]if at+1<len(argv)else ''
    if not value.isascii()or not value.isdecimal()or int(value)!=expected:
        raise ValueError('native public template identity differs: '+flag)


def _equal(actual, expected, name):
    if isinstance(expected,float):
        same=(isinstance(actual,(int,float))and not isinstance(actual,bool)
              and math.isfinite(actual)and math.isclose(actual,expected,rel_tol=5e-13,abs_tol=5e-15))
    else:same=type(actual)is type(expected)and actual==expected
    if not same:raise ValueError('native public template identity differs: '+name)


def _validate_format(plan, manifest):
    """Match the actual source format, not just nominal carrier geometry."""
    f=manifest['format'];p=plan['format']
    names=('inner_rate_hz','pattern_revision','added_amplitude_depth','added_symbols',
           'fine_chip_rate_hz','occupied_bandwidth_hz','quiet_seconds','ramp_seconds',
           'margin_seconds','pulse_shape','tile_seconds','tile_count',
           'fine_coefficients','inner_coefficients')
    for name in names:_equal(p.get(name),f[name],'format.'+name)
    for native,public in (('fhss','fhss_envelope_only'),('center_aligned','fhss_center_aligned'),
                           ('elapsed_bit_seconds','bit_seconds')):
        _equal(p.get(native),f[public],'format.'+public)
    slot=manifest['whole_bit_calendar']['slots'][0]
    _equal(p.get('symbol_ordinal'),slot['symbol_ordinal'],'format.symbol_ordinal')
    # These are fixed native exporter contracts, not selectable source options.
    for value,expected,name in ((manifest.get('encrypted_source'),True,'encrypted_source'),
                               (f.get('added_interleave'),True,'added_interleave'),
                               (f.get('physical_rf_retuning'),False,'physical_rf_retuning'),
                               (f.get('dwell_seconds'),.4,'dwell_seconds')):
        _equal(value,expected,name)
    geometry=plan['geometry']
    _equal(geometry.get('padded_source_samples'),manifest['templates']['source_support_samples'],
           'geometry.padded_source_samples')
    _equal(geometry.get('source_calendar_origin_sample'),manifest['calendar']['data_calendar_origin_sample'],
           'geometry.source_calendar_origin_sample')


def generate_template_bank(manifest, native_argv, *, chunk_samples=4096,
                           memory_limit_bytes=512*1024**2, source_factory=NativeStream):
    """Return ``[1,13,2,source+tail]`` complex128 samples and a receipt.

    Current scope is one public whole-bit slot. Nonzero/invalid metadata is
    rejected before generation; the caller must not infer source keys from RX.
    Per-call child-CPU deltas may overlap concurrent calls. Only the enclosing
    campaign's single before/after child total is additive in that situation.
    """
    _validate_arguments(native_argv)
    if isinstance(chunk_samples,bool)or not isinstance(chunk_samples,int)or not 1<=chunk_samples<=16384:
        raise ValueError('chunk samples must be an integer in1..16384')
    if isinstance(memory_limit_bytes,bool)or not isinstance(memory_limit_bytes,int)or memory_limit_bytes<=0:
        raise ValueError('positive integer memory limit required')
    if manifest.get('schema')!='triad-helper-whole-bit-public-v1':
        raise ValueError('public whole-bit source manifest required')
    _public_integer_argument(native_argv,'--key-seed',manifest['key_seed'])
    _public_integer_argument(native_argv,'--epoch',manifest['public_epoch_seconds'])
    slots=manifest['whole_bit_calendar']['slots'];spec=manifest['templates'];shape=tuple(spec['shape'])
    if len(slots)!=1 or len(shape)!=4 or shape[:3]!=(1,13,2)or not isinstance(shape[-1],int)or shape[-1]<1:
        raise ValueError('one complete public13-epoch/two-bit slot required')
    epochs=tuple(spec['epoch_offsets_seconds'])
    if epochs!=tuple(range(-6,7))or tuple(spec.get('bits',(0,1)))!=(0,1):
        raise ValueError('complete ordered public epoch/bit bank required')
    bytes_required=math.prod(shape)*16
    # Budget bulk parent storage conservatively: previous response, protocol
    # accumulation/copies and the new response can coexist. This excludes small
    # Python metadata and allocator overhead; native has its own separate guard.
    response_bytes=26*min(chunk_samples,shape[-1])*16
    parent_bulk_budget_bytes=bytes_required+4*response_bytes
    if parent_bulk_budget_bytes>memory_limit_bytes:
        raise MemoryError('public template bank exceeds declared parent memory budget')
    c,w,children=time.process_time(),time.perf_counter(),child_cpu_seconds()
    source=None
    try:
        source=source_factory(native_argv[0],native_argv[1:],kind='emitted')
        d=source.descriptor;f=manifest['format'];slot=slots[0]
        _validate_format(source.plan,manifest)
        checks=(('sample_rate_hz',manifest['sample_rate_hz']),
                ('source_samples',spec['source_support_samples']),
                ('epoch',slot['key_epoch_seconds']),
                ('symbol_ordinal',slot['symbol_ordinal']),
                ('pattern_revision',f['pattern_revision']))
        for name,expected in checks:
            if d.get(name)!=expected:raise ValueError('native public template identity differs: '+name)
        for name,expected in (('carrier_spacing_hz',f['carrier_spacing_hz']),
                              ('added_amplitude_depth',f['added_amplitude_depth']),
                              ('template_data_gain',manifest['power']['template_data_gain'])):
            value=d.get(name)
            if not isinstance(value,(int,float))or not math.isfinite(value)or not math.isclose(value,expected,rel_tol=5e-13,abs_tol=5e-15):
                raise ValueError('native public template identity differs: '+name)
        if d.get('power',{}).get('data_power_reference_policy')!='whole-active':
            raise ValueError('native power reference differs from public whole-active bank')
        bank=np.empty(shape,dtype=np.complex128)
        rows=[(bit,epoch,0.,0.,0.,0.,0.)for epoch in epochs for bit in (0,1)]
        for first in range(0,shape[-1],chunk_samples):
            count=min(chunk_samples,shape[-1]-first)
            values=source.basis(first,count,rows)
            if values.shape!=(26,count)or values.dtype!=np.dtype(np.complex128)or not np.isfinite(values).all():
                raise ValueError('native basis response has invalid shape, arithmetic or samples')
            bank[0,:,:,first:first+count]=values.reshape(13,2,count)
        descriptor=dict(d);source_format=dict(source.plan['format'])
    finally:
        if source is not None:source.close()
    receipt=dict(schema='triad-public-template-bank-v1',native_argv=list(native_argv),
        shape=list(shape),bytes=bytes_required,chunk_samples=chunk_samples,
        parent_bulk_budget_bytes=parent_bulk_budget_bytes,
        waveform_descriptor=descriptor,source_format=source_format,parent_cpu_seconds=time.process_time()-c,
        child_cpu_seconds_observed_delta=child_cpu_seconds()-children,
        child_cpu_scope='Process-wide waited-child delta; overlapping calls must not be summed.',
        wall_seconds=time.perf_counter()-w,
        scope='Fresh public keyed templates only; no channel/observation generation, no Helper or received-data-derived key.')
    return bank,receipt


def native_identity(native_argv):
    _validate_arguments(native_argv)
    return dict(path=native_argv[0],sha256=_digest(native_argv[0]))
