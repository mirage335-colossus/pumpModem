#!/usr/bin/env python3
"""Run retained CTest scopes with exact result accounting and failure-preserving reports."""
import argparse
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

SCOPES = ('core', 'fast', 'legacy', 'frontends', 'web', 'gui', 'build', 'packaging', 'triad', 'native')
REALTIME = {'fast_session', 'gui_fast_live'}


def properties(test):
    return {item['name']: item['value'] for item in test.get('properties', [])}


def scope_for(test):
    labels = properties(test).get('LABELS', [])
    if 'native_gui' in labels:
        return 'native'
    for label in ('web', 'frontends', 'fast', 'legacy', 'triad', 'gui', 'packaging', 'build'):
        if label in labels:
            return label
    return 'core'


def select_tests(tests, scope, sanitizers=False, sanitizer_realtime=False):
    names = [test['name'] for test in tests]
    if len(set(names)) != len(names):
        raise ValueError('CTest inventory contains duplicate test names')
    partitions = {name: [test for test in tests if scope_for(test) == name] for name in SCOPES}
    if scope == 'contract':
        candidates = [test for test in tests if 'contract' in properties(test).get('LABELS', [])
                      and 'native_gui' not in properties(test).get('LABELS', [])]
    else:
        candidates = partitions[scope]
    omitted = [test['name'] for test in candidates
               if sanitizers and not sanitizer_realtime and test['name'] in REALTIME]
    selected = [test for test in candidates if test['name'] not in omitted]
    if not selected:
        raise ValueError(f'CTest scope {scope} selected no tests')
    return selected, omitted, {name: len(values) for name, values in partitions.items()}

class WorkloadWarningCollector:
    """Bind complete verbose CTest workload records to this process's tests."""
    _start = re.compile(r'^[ \t]*Start[ \t]+(\d+): (.+)$')
    _warning = re.compile(r'^[ \t]*(\d+): (TEST_WORKLOAD_BUDGET:.*)$')
    _nested_warning = re.compile(r'^[ \t]*\d+:[ \t]+\d+:[ \t]+TEST_WORKLOAD_BUDGET:')
    _malformed_warning = re.compile(r'^[ \t]*\d+:[ \t]+TEST_WORKLOAD_BUDGET(?:\s|$)')
    _raw_warning = re.compile(r'^[ \t]*TEST_WORKLOAD_BUDGET:')
    _start_prefix = re.compile(r'^[ \t]*Start[ \t]+\d')
    _completion = re.compile(r'^[ \t]*\d+/\d+[ \t]+Test[ \t]+#(\d+):(.*)$')
    _completion_prefix = re.compile(r'^[ \t]*\d+/\d+[ \t]+Test[ \t]+#\d+')

    def __init__(self, selected):
        self.selected = tuple(test['name'] for test in selected)
        self.starts = {}
        self.names = {}
        self.warnings = {}
        self.errors = []
        self.replaying_output = False
        self.completed = set()

    def _error(self, message):
        if len(self.errors) < 20:
            self.errors.append(message)

    def feed(self, line):
        if isinstance(line, bytes):
            complete = line.endswith(b'\n')
            text = line.decode('utf-8', 'replace').rstrip('\r\n')
        else:
            complete = line.endswith('\n')
            text = line.rstrip('\r\n')

        # CTest may replay failed test output after its summary without the
        # live per-test framing. Keep collection limited to the original stream.
        if text.strip().startswith(('The following tests FAILED:', 'The following tests passed:')):
            self.replaying_output = True
            return
        if self.replaying_output:
            return
        completion = self._completion.fullmatch(text)
        if completion or self._completion_prefix.match(text):
            if not complete or not completion:
                self._error('incomplete CTest completion identity')
                return
            number = int(completion.group(1))
            if number not in self.starts:
                self._error(f'completion lacks CTest Start identity {number}')
            elif number in self.completed:
                self._error(f'repeated CTest completion identity {number}')
            elif not re.match(r'^\s*' + re.escape(self.starts[number]) + r'\s+\.+(?=\s|\*)',
                              completion.group(2)):
                self._error(f'completion conflicts with CTest Start identity {number}')
            else:
                self.completed.add(number)
            return

        start = self._start.fullmatch(text)
        if start or self._start_prefix.match(text):
            if not complete:
                self._error('incomplete CTest Start identity')
                return
            if not start:
                self._error('malformed CTest Start identity')
                return
            number, name = int(start.group(1)), start.group(2)
            if number in self.starts:
                self._error(f'repeated CTest test number {number}')
            elif name in self.names:
                self._error(f'repeated CTest test identity {name!r}')
            else:
                self.starts[number] = name
                self.names[name] = number
            return

        warning = self._warning.fullmatch(text)
        if warning:
            if not complete:
                self._error('incomplete workload warning line')
                return
            number, message = int(warning.group(1)), warning.group(2)
            if number in self.completed:
                return
            detail = message[len('TEST_WORKLOAD_BUDGET:'):].strip()
            if not detail:
                self._error(f'incomplete workload warning for CTest test {number}')
                return
            if number not in self.starts:
                self._error(f'workload warning precedes or lacks CTest test number {number}')
                return
            records = self.warnings.setdefault(number, [])
            if message not in records:
                records.append(message)
            return

        # Metadata such as "N: Test command: ...marker text..." and nested
        # child prefixes are not test-output records. Reject only malformed
        # direct marker frames, while ignoring unrelated text that mentions it.
        if self._nested_warning.match(text):
            return
        if self._malformed_warning.match(text):
            self._error('workload warning lacks complete CTest test framing')
        elif self._raw_warning.match(text) and not self.completed:
            self._error('workload warning lacks complete CTest test framing')

    def finish(self, *, require_all=True):
        expected = set(self.selected)
        if len(expected) != len(self.selected):
            self._error('selected CTest inventory contains duplicate names')
        actual = set(self.names)
        extra = actual - expected
        missing = expected - actual
        if extra:
            self._error('CTest Start identities are outside selected inventory: '
                        f'extra={sorted(extra)}')
        if require_all and missing:
            self._error('CTest Start identities omit selected tests: '
                        f'missing={sorted(missing)}')
        for number in self.warnings:
            if number not in self.starts:
                self._error(f'workload warning references unknown CTest test number {number}')
        if self.errors:
            raise ValueError('Invalid verbose CTest workload output: ' + '; '.join(self.errors))
        return {name: list(self.warnings.get(self.names[name], ())) if name in self.names else []
                for name in self.selected}


def read_results(path, selected, default_timeout, warn_fraction, *, workload_warnings=None):
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
        props = properties(inventory[name])
        timeout = float(props.get('TIMEOUT', default_timeout))
        # CTest uses status="fail" for some process outcomes. Never turn a
        # failure/skip into success just because it ran slowly.
        passed = (case.get('status', 'run') == 'run' and
                  not any(case.find(tag) is not None for tag in ('failure', 'error', 'skipped')))
        skips = case.findall('skipped')
        declared_omission = (
            type(props.get('SKIP_RETURN_CODE')) is int
            and props['SKIP_RETURN_CODE'] == 77
            and props.get('DISABLED', False) is False
            and case.get('status') == 'notrun'
            and len(skips) == 1
            and skips[0].get('message') == 'SKIP_RETURN_CODE=77'
            and case.find('failure') is None
            and case.find('error') is None
        )
        if workload_warnings is None:
            case_warnings = list(dict.fromkeys(
                line for output in case.findall('system-out')
                for line in (output.text or '').splitlines()
                if line.startswith('TEST_WORKLOAD_BUDGET:')))
        else:
            case_warnings = list(dict.fromkeys(workload_warnings.get(name, ())))
        result = {'name': name,
                  'status': 'passed' if passed else 'omitted' if declared_omission else 'incomplete_or_failed',
                  'seconds': seconds, 'timeout_seconds': timeout,
                  'workload_warnings': case_warnings,
                  'near_timeout': passed and timeout > 0 and seconds >= timeout * warn_fraction}
        if declared_omission:
            result.update(omission_reason='declared_skip_return_code', skip_return_code=77)
        results.append(result)
    return results


def annotation(message, title='CI test timing'):
    escaped = message.replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
    print(f'::warning title={title}::{escaped}', flush=True)


def write_summary(path, report):
    if not path:
        return
    with Path(path).open('a', encoding='utf-8') as stream:
        stream.write(f"### {report['scope']} tests: {report['status']}\n\n")
        stream.write(f"Selected {len(report['selected'])} tests; elapsed {report['elapsed_seconds']:.1f}s.\n\n")
        for field, title in [('omitted', 'Omitted instrumented real-time cases'),
                             ('runtime_omissions', 'Declared runtime omissions')]:
            if report.get(field):
                stream.write(title + ' (not passes): ' + ', '.join(report[field]) + '.\n\n')
        for result in report['tests']:
            for warning in result.get('workload_warnings', []):
                stream.write(f"- `{result['name']}` ({result['status']}): {warning}\n")
        if report.get('error'):
            stream.write('Runner error: ' + report['error'] + '\n')


def run(args):
    started = time.monotonic()
    report_path = args.report.resolve()
    report_path.parent.mkdir(parents=True, exist_ok=True)
    log_path, junit_path = report_path.with_suffix('.log'), report_path.with_suffix('.xml')
    inventory_path = report_path.with_suffix('.inventory.json')
    report = {'scope': args.scope, 'status': 'failed', 'selected': [], 'omitted': [],
              'runtime_omissions': [], 'tests': [], 'result_counts': None,
              'source_sha': os.environ.get('GITHUB_SHA', ''), 'ctest_returncode': None}
    returncode = 1
    try:
        base = [args.ctest, '--test-dir', str(args.build_dir.resolve())]
        if args.config:
            base += ['--build-config', args.config]
        inventory_path.unlink(missing_ok=True)
        junit_path.unlink(missing_ok=True)
        listing = subprocess.run(base + ['--show-only=json-v1'], check=True, capture_output=True)
        inventory_path.write_bytes(listing.stdout)
        report['inventory_sha256'] = hashlib.sha256(listing.stdout).hexdigest()
        report['inventory_file'] = inventory_path.name
        selected, omitted, counts = select_tests(json.loads(listing.stdout)['tests'], args.scope,
                                                 args.sanitizers, args.sanitizer_realtime)
        report.update(selected=[test['name'] for test in selected], omitted=omitted, partitions=counts)
        pattern = '^(' + '|'.join(re.escape(test['name']) for test in selected) + ')$'
        command = base + ['--output-on-failure', '--verbose', '--no-tests=error', '--parallel', str(args.jobs),
                          '--timeout', str(args.timeout), '--output-junit', str(junit_path), '-R', pattern]
        report['command'] = command
        collector = WorkloadWarningCollector(selected)
        with log_path.open('w', encoding='utf-8') as log:
            with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  text=True, errors='replace') as process:
                for line in process.stdout:
                    print(line, end='', flush=True)
                    log.write(line)
                    collector.feed(line)
                returncode = process.wait()
        report['ctest_returncode'] = returncode
        warnings = collector.finish(require_all=returncode == 0)
        report['tests'] = read_results(junit_path, selected, args.timeout, args.warn_fraction,
                                      workload_warnings=warnings)
        report['result_counts'] = {
            status: sum(test['status'] == status for test in report['tests'])
            for status in ('passed', 'omitted', 'incomplete_or_failed')}
        report['runtime_omissions'] = [test['name'] for test in report['tests'] if test['status'] == 'omitted']
        if returncode == 0 and not report['result_counts']['incomplete_or_failed'] and report['result_counts']['passed']:
            report['status'] = 'passed_with_omissions' if report['runtime_omissions'] else 'passed'
        else:
            returncode = returncode or 1
            if not report['result_counts']['passed']:
                report['error'] = 'No selected test passed'
        for result in report['tests']:
            if result['status'] == 'passed':
                for warning in result['workload_warnings']:
                    annotation(f"{result['name']} passed all assertions. {warning}", title='CI test workload')
                if result['near_timeout']:
                    annotation(f"{result['name']} passed near its time allowance; review test cost.")
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
    parser.add_argument('--scope', choices=(*SCOPES, 'contract'), required=True)
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
