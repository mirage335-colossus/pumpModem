"""Public whole-bit fixture -> bounded independent analysis windows.

The source/propagation/frontends continue across the train. This adapter only
selects public per-slot key templates and receiver analysis windows. It does
not infer a message length from received data or implement ordinary framing.
"""
import copy
import math
from pathlib import Path
import json

import numpy as np


def load_slots(directory, *, pre_helper_seconds=1., templates=None):
    directory=Path(directory)
    original=json.loads((directory/'manifest.json').read_text())
    if original['schema']!='triad-helper-whole-bit-public-v1':
        raise ValueError('public whole-bit source calendar required')
    spec=original['templates'];shape=tuple(spec['shape'])
    slots=original['whole_bit_calendar']['slots']
    if shape[:3]!=(len(slots),13,2) or len(shape)!=4 or shape[-1]<1:
        raise ValueError('complete public slot/epoch/bit templates required')
    if templates is None:
        if not spec['enabled']:raise ValueError('complete public slot/epoch/bit templates required')
        path=directory/spec['file']
        if path.stat().st_size!=math.prod(shape)*16:raise ValueError('template file size differs')
        templates=np.memmap(path,dtype='<c16',mode='r',shape=shape)
    else:
        templates=np.asarray(templates)
        if templates.shape!=shape or templates.dtype!=np.dtype(np.complex128):
            raise ValueError('supplied templates must match the public complex128 bank shape')
        if not np.isfinite(templates).all():raise ValueError('supplied templates must be finite')
    fs=original['sample_rate_hz'];cal=original['calendar']
    rows=int(cal['capture_samples'])
    if not math.isfinite(pre_helper_seconds) or pre_helper_seconds<0:
        raise ValueError('finite nonnegative pre-Helper analysis support required')
    prefix=math.ceil(pre_helper_seconds*fs)
    starts=[int(s['helper_begin_sample'])-prefix for s in slots]
    if min(starts)<0:raise ValueError('requested pre-Helper support leaves capture')
    if starts!=sorted(starts) or len(set(starts))!=len(starts):raise ValueError('ordered unique slot starts required')
    # Last source's complete native capture bounds all relative slot windows.
    length=rows-starts[-1]
    if length<=0:raise ValueError('last slot lies outside capture')
    result=[]
    for index,(slot,start)in enumerate(zip(slots,starts)):
        m=copy.deepcopy(original);m['schema']='triad-helper-public-v1';m.pop('whole_bit_calendar')
        m['public_epoch_seconds']=int(slot['key_epoch_seconds'])
        m['format']['symbol_ordinal']=int(slot['symbol_ordinal'])
        c=m['calendar'];c.update(capture_samples=length,
            data_sample_offset=int(slot['support_begin_sample'])-start,
            data_calendar_start_seconds=(slot['pattern_begin_sample']-start)/fs,
            helper_start_seconds=(slot['helper_begin_sample']-start)/fs,
            helper_end_seconds=(slot['helper_end_sample']-start)/fs)
        # Moving the nominal time origin does not cancel unknown sample-clock
        # scale. Cover the extra origin displacement without using its truth.
        drift=(start/fs)*float(m['channel']['sample_clock_pair_ppm'])*float(m['channel']['spread_margin'])*1e-6
        lo,hi=cal['arrival_prior_seconds'];c['arrival_prior_seconds']=[lo-drift,hi+drift]
        m['templates']=dict(spec,shape=list(shape[1:]),frame='per-slot public DATA source')
        m['observation']=dict(original['observation'],shape=[length])
        m['validity']=dict(original['validity'],shape=[length])
        m['stream_window']=dict(slot=index,absolute_start_sample=start,
            absolute_stop_sample=start+length,origin_clock_allowance_seconds=drift,
            pre_helper_analysis_seconds=prefix/fs,
            scope='Public scheduled source window; stream/channel/filter state not reset; no truth input.')
        result.append((m,templates[index],tuple(spec['epoch_offsets_seconds'])))
    return original,result
