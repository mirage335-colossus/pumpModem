#!/usr/bin/env python3
"""Fast integer-geometry checks for long native plan-only requests.

No waveform, template bank, channel, power calibration or output samples are
created. Pass the built triad_helper_export executable as the sole argument.
"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def invoke(binary, seconds, *extra, success=True, output=None):
    args = [str(binary), '--case', 'hf', '--sample-rate', '6000', '--rate', '36',
            '--added-symbols', '10', '--fhss', 'off', '--spread-preset', 'none',
            '--selective-perturbation', 'off', '--base-oscillator', 'gpsdo-ocxo',
            '--shift-oscillator', 'gpsdo-ocxo', '--seconds', str(seconds), *extra]
    if output is not None:
        args += ['--output', str(output)]
    result = subprocess.run(args, check=False, capture_output=True, text=True, timeout=20)
    if success and result.returncode:
        raise AssertionError(f'command failed: {args}\n{result.stderr}')
    if not success and not result.returncode:
        raise AssertionError(f'command unexpectedly succeeded: {args}')
    return result


def main():
    if len(sys.argv) != 2:
        raise SystemExit('usage: test_triad_helper_plan_long.py /path/to/triad_helper_export')
    binary = Path(sys.argv[1]).resolve()
    if not binary.is_file():
        raise SystemExit(f'exporter not found: {binary}')

    durations = [3600, 86400, 38 * 86400, 365 * 86400, 10 * 365.25 * 86400]
    plans = []
    for seconds in durations:
        result = invoke(binary, seconds, '--plan-only')
        plan = json.loads(result.stdout)
        assert plan['geometry_feasible']
        assert plan['geometry']['status'] == 'checked-integer-geometry-only'
        assert plan['geometry']['allocates_samples_or_templates'] is False
        assert plan['export_constraints']['maximum_sampled_active_seconds'] == 3600
        assert 'no sampled-duration cap' in plan['export_constraints']['plan_only_duration_limit']
        fmt, geometry = plan['format'], plan['geometry']
        assert geometry['active_tiles'] == fmt['tile_count'] > 0
        assert geometry['active_fine_coefficients'] == fmt['fine_coefficients']
        assert geometry['elapsed_data_seconds'] == fmt['elapsed_bit_seconds']
        assert abs(geometry['elapsed_dwells'] - geometry['elapsed_data_seconds'] / .4) <= 1
        assert abs(geometry['elapsed_fine_cells'] -
                   geometry['elapsed_data_seconds'] * fmt['fine_chip_rate_hz']) <= 1
        assert abs(geometry['elapsed_data_seconds'] - seconds) <= fmt['tile_seconds']
        assert geometry['padded_source_samples'] >= int(geometry['elapsed_data_seconds'] * 6000)
        assert geometry['bounded_source_workspace_bytes'] < 2_000_000
        plans.append(plan)

    # Plan-only verifies a long burst calendar while separately reporting DATA
    # energy time and wall-calendar time. The count remains constant-space.
    burst = json.loads(invoke(binary, 86400, '--plan-only', '--burst-wait-tiles', '3').stdout)
    bg = burst['geometry']
    assert bg['burst_wait_tiles_between_active_tiles'] == 3
    assert bg['physical_tiles_including_waits'] == bg['active_tiles'] + (bg['active_tiles'] - 1) * 3
    assert bg['elapsed_data_seconds'] > bg['active_data_seconds']
    assert bg['active_fine_coefficients'] == burst['format']['fine_coefficients']
    assert bg['elapsed_fine_cells'] > bg['active_fine_coefficients']

    with tempfile.TemporaryDirectory() as temp:
        sample = Path(temp) / 'long-sample'
        # The same numerical duration remains rejected on execution paths that
        # would generate/calibrate samples; these failures occur before output.
        invoke(binary, 3601, success=False, output=sample)
        assert not sample.exists()
        invoke(binary, 86400, '--power-plan-only', success=False)

    # The native geometry planner may encode overflow as an explicit
    # geometry_feasible=false JSON classification, or reject it at the CLI.
    # Either outcome must name the representability failure and allocate no
    # sample/template output.
    overflow_args = [str(binary), '--case', 'hf', '--sample-rate', '6000', '--rate', '36',
        '--added-symbols', '10', '--fhss', 'off', '--spread-preset', 'none',
        '--selective-perturbation', 'off', '--base-oscillator', 'gpsdo-ocxo',
        '--shift-oscillator', 'gpsdo-ocxo', '--seconds', '1e90', '--plan-only']
    overflow = subprocess.run(overflow_args, check=False, capture_output=True, text=True, timeout=20)
    if overflow.returncode == 0:
        result = json.loads(overflow.stdout)
        assert result['geometry_feasible'] is False
        reason = (result.get('reason') or '').lower()
        assert 'overflow' in reason or 'extent' in reason
    else:
        reason = overflow.stderr.lower()
        assert 'overflow' in reason or 'extent' in reason

    print(json.dumps({'status': 'passed', 'plan_only_durations_seconds': durations,
                      'long_burst_active_seconds': bg['active_data_seconds'],
                      'long_burst_elapsed_seconds': bg['elapsed_data_seconds'],
                      'sampled_export_limit_seconds': 3600,
                      'plan_only_shapes_or_allocates_waveforms': False}))


if __name__ == '__main__':
    main()
