"""Shared public-manifest and process-accounting helpers for Robust cases.

Receiver measurement now lives in the complete-capacity path. Keep these small
helpers here because replay, message-gate, and capacity tools share them.
"""
from __future__ import annotations

import copy
import os
from pathlib import Path


EPOCHS = tuple(range(-6, 7))


def _option(arguments, flag, value):
    """Replace exactly one public fixture option, rejecting ambiguous argv."""
    count = arguments.count(flag)
    if count > 1:
        raise ValueError('duplicate native option: ' + flag)
    if count:
        at = arguments.index(flag)
        if at + 1 >= len(arguments):
            raise ValueError('missing native option value: ' + flag)
        arguments[at + 1] = str(value)
    else:
        arguments.extend((flag, str(value)))


def _native_cpu_seconds(source):
    """CPU of this adapter's still-running native child, never all children."""
    process = getattr(source, 'process', None)
    if process is None:
        return None
    try:
        fields = Path('/proc/' + str(process.pid) + '/stat').read_text().rsplit(')', 1)[1].split()
        return (int(fields[11]) + int(fields[12])) / os.sysconf('SC_CLK_TCK')
    except (OSError, ValueError, IndexError, AttributeError):
        return None


def _rss_snapshot(source):
    """Sample resident process storage; this is not an allocator peak proof."""
    def resident(pid):
        try:
            for line in Path('/proc/' + str(pid) + '/status').read_text().splitlines():
                if line.startswith('VmRSS:'):
                    return int(line.split()[1]) * 1024
        except (OSError, ValueError, IndexError):
            pass
        return None
    process = getattr(source, 'process', None)
    parent = resident(os.getpid())
    child = resident(process.pid) if process is not None else None
    return dict(parent_process_bytes=parent, native_child_bytes=child,
        sum_process_bytes=None if parent is None or child is None else parent + child,
        scope='Sampled current parent plus owned native-child RSS, including Python/numerical runtime and shared pages; not exclusive DSP storage or proof of transient peak.')


def _manifest(plan, descriptor, samples, source_first, seed):
    physics = copy.deepcopy(plan['physics'])
    physics.setdefault('endpoint_uniform_half_bound_ppm',
        float(physics.get('base_relative_pair_ppm_bound', 0.)) / 2)
    physics['nominal_conversion_if_hz'] = float(descriptor['data_if_hz'])
    fmt = copy.deepcopy(plan['format'])
    fmt['bit_seconds'] = float(fmt['elapsed_bit_seconds'])
    fmt['physical_rf_retuning'] = False
    helper_start = 0.
    helper_end = int(descriptor['emitted_samples']) / descriptor['sample_rate_hz']
    if descriptor['helper_enabled']:
        calendar = plan.get('transmit_calendar', {})
        if 'first_helper_start_seconds' not in calendar or 'first_helper_end_seconds' not in calendar:
            raise ValueError('whole-bit Helper requires the native public transmit calendar')
        helper_start = float(calendar['first_helper_start_seconds'])
        helper_end = float(calendar['first_helper_end_seconds'])
    return dict(schema='triad-public-local-probe-v1',
        sample_rate_hz=int(descriptor['sample_rate_hz']), format=fmt, channel=physics,
        helper_enabled=bool(descriptor['helper_enabled']),
        public_epoch_seconds=int(descriptor['epoch']), key_seed=seed,
        calendar=dict(capture_samples=samples,
            capture_lead_seconds=float(descriptor['capture_lead_seconds']),
            data_sample_offset=int(descriptor['data_offset_samples']) + source_first,
            arrival_prior_seconds=list(physics['arrival_prior_seconds']),
            helper_start_seconds=helper_start,
            helper_end_seconds=helper_end,
            fixed_capture_tail_bound_seconds=float(physics['spread_delay_ms']) * .001),
        helper_plan=dict(low_hz=float(descriptor['helper_low_hz'] or 0.),
            high_hz=float(descriptor['helper_high_hz'] or 0.), auxiliary_hz=950.,
            helper_gain=0., pilot_dwell_gating=bool(descriptor['helper_enabled'] and fmt['fhss'])),
        power=dict(complex_noise_variance=float(descriptor['noise_variance']),
            post_plan_composite_gain=1.))
