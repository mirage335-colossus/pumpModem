#!/usr/bin/env python3
"""Run a disjoint CTest scope and retain timing/coverage evidence for CI."""
import argparse
from collections import Counter
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET


SCOPES = ('core', 'fast', 'live', 'calibration', 'frontends')
REALTIME = {'fast_session', 'gui_fast_live'}
LIVE = {'live', 'live_profiles'}


def properties(test):
    return {item['name']: item['value'] for item in test.get('properties', [])}


def scope_for(test):
    if test['name'] in LIVE:
        return 'live'
    labels = properties(test).get('LABELS', [])
    if 'frontends' in labels:
        return 'frontends'
    if 'calibration' in labels:
        return 'calibration'
    if 'fast' in labels:
        return 'fast'
    return 'core'


def select_tests(tests, scope, sanitizers=False, sanitizer_realtime=False):
    names = [test['name'] for test in tests]
    if len(set(names)) != len(names):
        raise ValueError('CTest inventory contains duplicate test names')
    # Every registered test has exactly one owner, including newly added tests
    # without labels. Contract labels overlap functional labels by design.
    partitions = {name: [test for test in tests if scope_for(test) == name] for name in SCOPES}
    candidates = ([test for test in tests if 'gui' in properties(test).get('LABELS', [])]
                  if scope == 'gui' else partitions[scope])
    omitted = [test['name'] for test in candidates
               if sanitizers and not sanitizer_realtime and test['name'] in REALTIME]
    selected = [test for test in candidates if test['name'] not in omitted]
    if not selected:
        raise ValueError(f'CTest scope {scope} selected no tests')
    return selected, omitted, {name: len(values) for name, values in partitions.items()}


def read_results(path, selected, default_timeout, warn_fraction):
    root = ET.parse(path).getroot()
    cases = list(root.iter('testcase'))
    observed = Counter(case.get('name') for case in cases)
    expected = Counter(test['name'] for test in selected)
    if observed != expected:
        raise ValueError('CTest result names do not match the selected inventory')
    inventory = {test['name']: test for test in selected}
    results = []
    for case in cases:
        name = case.get('name')
        seconds = float(case.get('time', '0'))
        timeout = float(properties(inventory[name]).get('TIMEOUT', default_timeout))
        # CTest uses status="fail" for some process outcomes. Never turn a
        # failure/skip into success just because it ran slowly.
        passed = (case.get('status', 'run') == 'run' and
                  not any(case.find(tag) is not None for tag in ('failure', 'error', 'skipped')))
        workload_warnings = list(dict.fromkeys(
            line for output in case.findall('system-out')
            for line in (output.text or '').splitlines()
            if line.startswith('TEST_WORKLOAD_BUDGET:')))
        results.append({'name': name, 'status': 'passed' if passed else 'incomplete_or_failed',
                        'seconds': seconds, 'timeout_seconds': timeout,
                        'workload_warnings': workload_warnings,
                        'near_timeout': passed and timeout > 0 and seconds >= timeout * warn_fraction})
    return results


def annotation(message, title='CI test timing'):
    escaped = message.replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
    print(f'::warning title={title}::{escaped}', flush=True)


def write_summary(path, report):
    if not path:
        return
    with Path(path).open('a', encoding='utf-8') as stream:
        stream.write(f"### {report['scope']} tests: {report['status']}\n\n")
        stream.write(f"Selected {len(report.get('selected', []))} tests; "
                     f"elapsed {report['elapsed_seconds']:.1f} seconds.\n\n")
        if report.get('omitted'):
            stream.write('Omitted instrumented real-time coverage (not passes): ' +
                         ', '.join(report['omitted']) + '. Release retains these tests.\n\n')
        longest = sorted(report.get('tests', []), key=lambda test: test['seconds'], reverse=True)[:5]
        if longest:
            stream.write('Longest tests in this scope:\n\n')
            for test in longest:
                stream.write(f"- `{test['name']}`: {test['seconds']:.1f}s ({test['status']}).\n")
            stream.write('\n')
        slow = [test for test in report.get('tests', []) if test['near_timeout']]
        if slow:
            stream.write('These tests passed but approached their time allowance:\n\n')
            for test in slow:
                stream.write(f"- `{test['name']}`: {test['seconds']:.1f}s / "
                             f"{test['timeout_seconds']:.1f}s.\n")
        workload = [test for test in report.get('tests', [])
                    if test['status'] == 'passed' and test.get('workload_warnings')]
        if workload:
            stream.write('\nThese tests completed all assertions using additional workload time:\n\n')
            for test in workload:
                for warning in test['workload_warnings']:
                    stream.write(f"- `{test['name']}`: {warning}\n")
        if report.get('error'):
            stream.write(f"Runner error: {report['error']}\n\n")


def run(args):
    started = time.monotonic()
    report_path = args.report.resolve()
    report_path.parent.mkdir(parents=True, exist_ok=True)
    log_path = report_path.with_suffix('.log')
    junit_path = report_path.with_suffix('.xml')
    report = {'scope': args.scope, 'status': 'failed', 'selected': [], 'omitted': [],
              'tests': [], 'source_sha': os.environ.get('GITHUB_SHA', '')}
    returncode = 1
    try:
        base = [args.ctest, '--test-dir', str(args.build_dir.resolve())]
        if args.config:
            base += ['--build-config', args.config]
        listing = subprocess.run(base + ['--show-only=json-v1'], check=True,
                                 capture_output=True, text=True)
        tests = json.loads(listing.stdout)['tests']
        selected, omitted, counts = select_tests(tests, args.scope, args.sanitizers, args.sanitizer_realtime)
        report.update(selected=[test['name'] for test in selected], omitted=omitted, partitions=counts)
        print('CTest partition sizes: ' + json.dumps(counts, sort_keys=True), flush=True)
        if omitted:
            print('Omitted instrumented real-time coverage: ' + ', '.join(omitted), flush=True)
        pattern = '^(' + '|'.join(re.escape(test['name']) for test in selected) + ')$'
        command = base + ['--output-on-failure', '--no-tests=error', '--parallel', str(args.jobs),
                          '--timeout', str(args.timeout), '--output-junit', str(junit_path), '-R', pattern]
        report['command'] = command
        # Prevent stale evidence from a previous attempt satisfying this run.
        junit_path.unlink(missing_ok=True)
        with log_path.open('w', encoding='utf-8') as log:
            with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  text=True, errors='replace') as process:
                for line in process.stdout:
                    print(line, end='', flush=True)
                    log.write(line)
                    log.flush()
                returncode = process.wait()
        report['tests'] = read_results(junit_path, selected, args.timeout, args.warn_fraction)
        complete = all(test['status'] == 'passed' for test in report['tests'])
        if returncode == 0 and complete:
            report['status'] = 'passed'
        else:
            returncode = returncode or 1
        for test in report['tests']:
            if test['status'] == 'passed':
                for warning in test['workload_warnings']:
                    annotation(f"{test['name']} passed all assertions. {warning}", title='CI test workload')
            if test['near_timeout']:
                annotation(f"{test['name']} passed in {test['seconds']:.1f}s, "
                           f"near its {test['timeout_seconds']:.1f}s allowance. "
                           'Consider moving or dividing this workload; correctness checks remain enforced.')
    except (OSError, ValueError, KeyError, subprocess.SubprocessError, ET.ParseError) as error:
        report['error'] = str(error)
        print(f'CI test runner failed: {error}', file=sys.stderr)
        returncode = 1
    report['elapsed_seconds'] = time.monotonic() - started
    report_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    write_summary(args.summary, report)
    return 0 if returncode == 0 else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--scope', choices=(*SCOPES, 'gui'), required=True)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--config')
    parser.add_argument('--sanitizers', action='store_true')
    parser.add_argument('--sanitizer-realtime', action='store_true')
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--summary', default=os.environ.get('GITHUB_STEP_SUMMARY'))
    parser.add_argument('--ctest', default='ctest')
    parser.add_argument('--timeout', type=float, default=3600)
    parser.add_argument('--warn-fraction', type=float, default=0.8)
    args = parser.parse_args(argv)
    if (args.jobs < 1 or not math.isfinite(args.timeout) or args.timeout <= 0 or
            not math.isfinite(args.warn_fraction) or not 0 < args.warn_fraction <= 1):
        parser.error('jobs/timeout must be positive and warn-fraction must be in (0, 1]')
    return run(args)


if __name__ == '__main__':
    raise SystemExit(main())
