#!/usr/bin/env python3
"""Run a disjoint CTest scope and retain timing/coverage evidence for CI."""
import argparse
from contextlib import nullcontext
from collections import Counter
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET


SCOPES = ('core', 'preparation', 'fast', 'live', 'calibration', 'frontends', 'web')
REALTIME = {'fast_session', 'gui_fast_live'}
LIVE = {'live', 'live_profiles'}


def properties(test):
    return {item['name']: item['value'] for item in test.get('properties', [])}


def scope_for(test):
    if test['name'] in LIVE:
        return 'live'
    labels = properties(test).get('LABELS', [])
    if 'preparation' in labels:
        return 'preparation'
    if 'web' in labels:
        return 'web'
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
    partitions = {name: [test for test in tests if scope_for(test) == name]
                  for name in SCOPES if name != 'contract'}
    if scope == 'contract':
        candidates = [test for test in tests if 'contract' in properties(test).get('LABELS', [])
                      and 'native_gui' not in properties(test).get('LABELS', [])]
        omitted = []
        if not candidates:
            raise ValueError('CTest contract selection is empty')
        return candidates, omitted, {name: len(values) for name, values in partitions.items()}
    candidates = ([test for test in tests if 'gui' in properties(test).get('LABELS', [])]
                  if scope == 'gui' else partitions[scope])
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


HOSTED_CLOCK_FIELDS = {
    'schema', 'fixture', 'status', 'case', 'normal_budget_seconds', 'limit_seconds',
    'elapsed_seconds', 'transmission_id', 'input_revision', 'samples_received',
    'fraction', 'source_seconds', 'generated_bits', 'total_bits', 'tail',
    'progress_age_seconds', 'standard_functional_coverage',
    'remaining_live_coverage', 'first_samples_received', 'first_fraction',
    'first_source_seconds', 'observation_count',
}
HOSTED_CLOCK_CASES = ('raw', 'short', 'framed')
_HOSTED_CLOCK_MARKER = 'TEST_HOSTED_THROUGHPUT:'


def _strict_json_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError(f'duplicate hosted clock JSON key {key!r}')
        value[key] = item
    return value


def parse_hosted_clock_record(payload):
    """Parse and validate the one narrowly classified compact-clock record."""
    try:
        record = json.loads(payload, object_pairs_hook=_strict_json_object,
                            parse_constant=lambda value: (_ for _ in ()).throw(
                                ValueError(f'invalid JSON number {value}')))
    except (json.JSONDecodeError, ValueError) as error:
        raise ValueError(f'invalid hosted clock JSON: {error}') from error
    if type(record) is not dict or set(record) != HOSTED_CLOCK_FIELDS:
        raise ValueError('hosted clock record has unexpected or missing keys')
    if type(record['schema']) is not int or record['schema'] != 1:
        raise ValueError('hosted clock schema must be integer 1')
    for key, expected in (('fixture', 'default_media_clock_compact'),
                          ('status', 'incomplete_coverage'),
                          ('standard_functional_coverage', 'passed'),
                          ('remaining_live_coverage', 'passed')):
        if type(record[key]) is not str or record[key] != expected:
            raise ValueError(f'hosted clock {key} must be {expected!r}')
    if type(record['case']) is not str or record['case'] not in HOSTED_CLOCK_CASES:
        raise ValueError('hosted clock case must be raw, short, or framed')
    if type(record['normal_budget_seconds']) is not int or record['normal_budget_seconds'] != 40:
        raise ValueError('hosted clock normal budget must be integer 40')
    if type(record['limit_seconds']) is not int or record['limit_seconds'] not in (40, 120):
        raise ValueError('hosted clock limit must be integer 40 or 120')

    def integer(key, minimum):
        value = record[key]
        if type(value) is not int or value < minimum:
            raise ValueError(f'hosted clock {key} must be an integer >= {minimum}')

    def number(key, minimum=None, maximum=None):
        value = record[key]
        if type(value) not in (int, float):
            raise ValueError(f'hosted clock {key} must be a finite number')
        try:
            finite = math.isfinite(value)
        except OverflowError:
            finite = False
        if not finite:
            raise ValueError(f'hosted clock {key} must be a finite number')
        if minimum is not None and value < minimum:
            raise ValueError(f'hosted clock {key} must be >= {minimum}')
        if maximum is not None and value > maximum:
            raise ValueError(f'hosted clock {key} must be <= {maximum}')

    integer('transmission_id', 1)
    integer('input_revision', 1)
    integer('samples_received', 0)
    integer('generated_bits', 0)
    integer('total_bits', 1)
    if record['generated_bits'] > record['total_bits']:
        raise ValueError('hosted clock generated bit count exceeds its source endpoint')
    integer('first_samples_received', 0)
    integer('observation_count', 2)
    number('elapsed_seconds', record['limit_seconds'])
    number('fraction', 0, 1)
    number('source_seconds', 0)
    number('progress_age_seconds', 0, 30)
    number('first_fraction', 0, 1)
    number('first_source_seconds', 0)
    if type(record['tail']) is not bool:
        raise ValueError('hosted clock tail must be a boolean')
    if record['samples_received'] < record['first_samples_received']:
        raise ValueError('hosted clock sample count moved backwards')
    if record['fraction'] < record['first_fraction']:
        raise ValueError('hosted clock fraction moved backwards')
    if record['source_seconds'] < record['first_source_seconds']:
        raise ValueError('hosted clock source time moved backwards')
    advanced = (record['fraction'] > record['first_fraction'] or
                record['source_seconds'] > record['first_source_seconds'])
    sampled_tail_advance = record['tail'] and record['samples_received'] > record['first_samples_received']
    if not (advanced or sampled_tail_advance):
        raise ValueError('hosted clock record has no forward progress evidence')
    return record


def _hosted_clock_eligible(test):
    command = test.get('command', ())
    return (test.get('name') == 'live' and isinstance(command, list) and
            '--ci-functional-clock' in command)


class HostedClockWarningCollector:
    """Bind exactly one strict compact-clock record to an eligible CTest case."""
    _start = WorkloadWarningCollector._start
    _raw_marker = re.compile(r'^[ \t]*TEST_HOSTED_THROUGHPUT(?::|\s|$)')
    _direct_marker = re.compile(r'^[ \t]*\d+:[ \t]+TEST_HOSTED_THROUGHPUT(?::|\s|$)')
    _frame = re.compile(r'^[ \t]*(\d+): (TEST_HOSTED_THROUGHPUT:[ \t]*\{.*\})$')
    _nested = re.compile(r'^[ \t]*\d+:[ \t]+\d+:[ \t]+TEST_HOSTED_THROUGHPUT:')

    def __init__(self, selected):
        self.selected = {item['name']: item for item in selected}
        self.selected_count = len(selected)
        self.starts = {}
        self.names = {}
        self.records = {}
        self.errors = []
        self.replaying_output = False
        self.completed = set()

    def _error(self, message):
        if len(self.errors) < 20:
            self.errors.append(message)

    def feed(self, line):
        complete = line.endswith(b'\n') if isinstance(line, bytes) else line.endswith('\n')
        text = (line.decode('utf-8', 'replace').rstrip('\r\n') if isinstance(line, bytes)
                else line.rstrip('\r\n'))
        if text.strip().startswith(('The following tests FAILED:', 'The following tests passed:')):
            self.replaying_output = True
            return
        if self.replaying_output:
            return
        completion = WorkloadWarningCollector._completion.fullmatch(text)
        if completion or WorkloadWarningCollector._completion_prefix.match(text):
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
        if start or WorkloadWarningCollector._start_prefix.match(text):
            if not complete or not start:
                self._error('incomplete or malformed CTest Start identity')
                return
            number, name = int(start.group(1)), start.group(2)
            if number in self.starts or name in self.names:
                self._error('repeated CTest test identity')
            else:
                self.starts[number] = name
                self.names[name] = number
            return
        # CTest metadata may quote a command that contains the marker. Only
        # direct test-output frames (or malformed versions of them) are records.
        if self._direct_marker.match(text) or self._raw_marker.match(text) or self._nested.match(text):
            if not complete:
                self._error('incomplete hosted clock marker line')
                return
            if self._nested.match(text):
                self._error('nested hosted clock marker is not eligible output')
                return
            frame = self._frame.fullmatch(text)
            if not frame:
                self._error('malformed or unbound hosted clock marker framing')
                return
            number = int(frame.group(1))
            name = self.starts.get(number)
            if name is None:
                self._error(f'hosted clock marker precedes or lacks CTest test number {number}')
                return
            if number in self.completed:
                self._error('hosted clock marker follows test completion')
                return
            test = self.selected.get(name)
            if test is None or not _hosted_clock_eligible(test):
                self._error(f'hosted clock marker is ineligible for CTest test {name!r}')
                return
            if number in self.records:
                self._error(f'duplicate hosted clock marker for CTest test {name!r}')
                return
            try:
                record = parse_hosted_clock_record(frame.group(2)[len(_HOSTED_CLOCK_MARKER):].strip())
            except ValueError as error:
                self._error(str(error))
                return
            record['warning'] = frame.group(2)
            self.records[number] = record
            return

    def finish(self, *, require_all=True):
        expected = set(self.selected)
        actual = set(self.names)
        if len(expected) != self.selected_count:
            self._error('selected CTest inventory contains duplicate names')
        if actual - expected:
            self._error('CTest Start identities are outside selected inventory')
        if require_all and expected - actual:
            self._error('CTest Start identities omit selected tests')
        for number in self.records:
            if number not in self.completed:
                self._error('hosted clock record lacks a CTest completion identity')
        if self.errors:
            raise ValueError('Invalid verbose CTest hosted clock output: ' + '; '.join(self.errors))
        return {self.starts[number]: record for number, record in self.records.items()}


def read_results(path, selected, default_timeout, warn_fraction, *, workload_warnings=None,
                 hosted_clock_warnings=None):
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
        if hosted_clock_warnings and name in hosted_clock_warnings and passed:
            coverage_limit = dict(hosted_clock_warnings[name])
            first_case = HOSTED_CLOCK_CASES.index(coverage_limit['case'])
            coverage_limit['compact_cases_completed'] = list(HOSTED_CLOCK_CASES[:first_case])
            coverage_limit['compact_cases_not_completed'] = [
                *HOSTED_CLOCK_CASES[first_case:], 'clear_after_positive']
            result['coverage_limit'] = coverage_limit
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
        stream.write(f"Selected {len(report.get('selected', []))} tests; "
                     f"elapsed {report['elapsed_seconds']:.1f} seconds.\n\n")
        if report.get('omitted'):
            stream.write('Omitted instrumented real-time coverage (not passes): ' +
                         ', '.join(report['omitted']) + '. Release retains these tests.\n\n')
        if report.get('result_counts') is not None:
            counts = report['result_counts']
            stream.write(f"Results: {counts['passed']} passed, {counts['omitted']} declared omissions, "
                         f"{counts['incomplete_or_failed']} incomplete or failed.\n\n")
        if report.get('runtime_omissions'):
            stream.write('Selected conditional tests omitted by their declared exit 77 (not passes): ' +
                         ', '.join(report['runtime_omissions']) + '.\n\n')
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
            if any(test.get('coverage_limit') for test in workload):
                stream.write('\nMandatory functional assertions passed using additional workload time; '
                             'compact coverage limits are listed separately:\n\n')
            else:
                stream.write('\nThese tests completed all assertions using additional workload time:\n\n')
            for test in workload:
                for warning in test['workload_warnings']:
                    stream.write(f"- `{test['name']}`: {warning}\n")
        coverage_limits = report.get('coverage_limits', [])
        if coverage_limits:
            stream.write('\nCompact default media clock coverage was incomplete; mandatory '
                         'functional coverage passed:\n\n')
            for limit in coverage_limits:
                stream.write(f"- `{limit['test']}`: case `{limit['case']}` reached its "
                             f"{limit['limit_seconds']}s hosted limit; compact cases not completed: "
                             f"{', '.join(limit['compact_cases_not_completed'])}.\n")
            stream.write('\n')
        if report.get('error'):
            stream.write(f"Runner error: {report['error']}\n\n")


def run(args):
    started = time.monotonic()
    report_path = args.report.resolve()
    report_path.parent.mkdir(parents=True, exist_ok=True)
    log_path = report_path.with_suffix('.log')
    junit_path = report_path.with_suffix('.xml')
    inventory_path = report_path.with_suffix('.inventory.json')
    report = {'scope': args.scope, 'status': 'failed', 'selected': [], 'omitted': [],
              'runtime_omissions': [], 'result_counts': None,
              'tests': [], 'coverage_limits': [], 'source_sha': os.environ.get('GITHUB_SHA', ''),
              'inventory_file': None, 'inventory_sha256': None, 'ctest_returncode': None}
    returncode = 1
    try:
        base = [args.ctest, '--test-dir', str(args.build_dir.resolve())]
        if args.config:
            base += ['--build-config', args.config]
        inventory_path.unlink(missing_ok=True)
        listing = subprocess.run(base + ['--show-only=json-v1'], check=True,
                                 capture_output=True)
        inventory_path.write_bytes(listing.stdout)
        report['inventory_sha256'] = hashlib.sha256(listing.stdout).hexdigest()
        report['inventory_file'] = inventory_path.name
        tests = json.loads(listing.stdout)['tests']
        plan = None
        selection_plan_path = getattr(args, 'selection_plan', None)
        if selection_plan_path:
            planner_spec = importlib.util.spec_from_file_location(
                'ci_test_plan', Path(__file__).with_name('ci-test-plan.py'))
            planner = importlib.util.module_from_spec(planner_spec)
            planner_spec.loader.exec_module(planner)
            plan = json.loads(selection_plan_path.read_text(encoding='utf-8'))
            planner.validate_plan(plan)
            if plan['scope'] != args.scope:
                raise ValueError('selection plan scope does not match runner scope')
            if planner.source_identity(plan['source_root']) != plan['source_identity']:
                raise ValueError('source inputs changed since planning')
            if planner.config_identity(args.build_dir / 'CMakeCache.txt', args.config) != plan['config_identity']:
                raise ValueError('compiler/configuration inputs changed since planning')
            if args.sanitizers != (plan['config_identity']['sanitizers'] == 'ON'):
                raise ValueError('sanitizer selection conflicts with configured build')
            canonical = planner.canonical_inventory(tests)
            if canonical != plan.get('canonical_inventory'):
                raise ValueError('configured CTest registration identity changed since planning')
            registration = planner.load_registration(args.build_dir / 'ci-test-prerequisites.json')
            if registration != plan.get('registration'):
                raise ValueError('generated CTest prerequisite identity changed since planning')
            selected_names = plan['selected_tests']
            model = planner.partition_model(tests, args.scope)
            if model != plan['parts'] or model.get(plan['selected_part']) != selected_names:
                raise ValueError('selection plan does not match authoritative runner selection')
            selected_by_name = {test['name']: test for test in tests}
            selected = [selected_by_name[name] for name in selected_names]
            if not selected or any(not test.get('command') for test in selected):
                raise ValueError('selected execution prerequisites are missing')
            omitted = []
            _, _, counts = select_tests(tests, 'core' if args.scope == 'contract' else args.scope,
                                        args.sanitizers, args.sanitizer_realtime)
        else:
            selected, omitted, counts = select_tests(tests, args.scope, args.sanitizers, args.sanitizer_realtime)
        report.update(selected=[test['name'] for test in selected], omitted=omitted, partitions=counts)
        if plan:
            report.update(plan_sha256=plan['plan_sha256'], source_identity=plan['source_identity'],
                          config_identity=plan['config_identity'], selected_part=plan['selected_part'],
                          expected_tests=plan['selected_tests'],
                          canonical_inventory_sha256=plan['canonical_inventory_sha256'],
                          ci_context=plan['ci_context'],
                          selected_commands={test['name']: test['command'] for test in selected})
        print('CTest partition sizes: ' + json.dumps(counts, sort_keys=True), flush=True)
        if omitted:
            print('Omitted instrumented real-time coverage: ' + ', '.join(omitted), flush=True)
        pattern = '^(' + '|'.join(re.escape(test['name']) for test in selected) + ')$'
        gui_names = {'gui_controller', 'gui_application'}
        # Advice has its own short responsiveness assertion. Keep it out of
        # competing DSP processes too, without separating the controller and
        # application pair that must still exercise parallel execution.
        isolated_gui = gui_names if gui_names.issubset(report['selected']) else set()
        if 'gui_timing_advice' in report['selected']:
            isolated_gui = isolated_gui | {'gui_timing_advice'}
        gui_phases = []
        if args.sanitizers and args.scope == 'core':
            gui_phases = [
                ('core', [test for test in selected if test['name'] not in isolated_gui]),
                ('gui_advice', [test for test in selected if test['name'] == 'gui_timing_advice']),
                ('gui', [test for test in selected if test['name'] in gui_names & isolated_gui]),
            ]
            gui_phases = [(name, cases) for name, cases in gui_phases if cases]
        gui_phased = len(gui_phases) > 1
        # The SDK general partition keeps this original wall-budgeted sampled
        # fixture out of the other CPU-intensive CTest processes. It still runs
        # once, with the same registration, assertions and deadlines.
        burst_phased = (plan is not None and args.scope == 'contract' and
                        plan['selected_part'] == 'general' and
                        'symbol_burst_live' in report['selected'] and len(selected) > 1)
        if gui_phased:
            phases = gui_phases
        elif burst_phased:
            phases = [('general', [test for test in selected if test['name'] != 'symbol_burst_live']),
                      ('symbol_burst_live', [test for test in selected if test['name'] == 'symbol_burst_live'])]
        else:
            phases = [('all', selected)]
        phased = gui_phased or burst_phased
        if not phased:
            command = base + ['--output-on-failure', '--verbose', '--no-tests=error', '--parallel', str(args.jobs),
                              '--timeout', str(args.timeout), '--output-junit', str(junit_path), '-R', pattern]
            report['command'] = command
        else:
            report['command'] = None
            report['commands'] = []
            report['phases'] = []
        # Prevent stale evidence from a previous attempt satisfying this run.
        junit_path.unlink(missing_ok=True)
        phase_results = []
        phase_errors = []
        with log_path.open('w', encoding='utf-8') as log:
            for index, (phase_name, phase_tests) in enumerate(phases, 1):
                phase_junit = junit_path if not phased else report_path.with_name(
                    f'{report_path.stem}.phase-{index}.xml')
                phase_log_path = log_path if not phased else report_path.with_name(
                    f'{report_path.stem}.phase-{index}.log')
                phase_pattern = '^(' + '|'.join(re.escape(test['name']) for test in phase_tests) + ')$'
                command = base + ['--output-on-failure', '--verbose', '--no-tests=error', '--parallel', str(args.jobs),
                                  '--timeout', str(args.timeout), '--output-junit', str(phase_junit), '-R', phase_pattern]
                phase_junit.unlink(missing_ok=True)
                if phased:
                    phase_log_path.unlink(missing_ok=True)
                workload_collector = WorkloadWarningCollector(phase_tests)
                hosted_clock_collector = HostedClockWarningCollector(phase_tests)
                phase_started = time.monotonic()
                with (phase_log_path.open('w', encoding='utf-8') if phased else nullcontext()) as phase_log:
                    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                          text=True, errors='replace') as process:
                        if phased:
                            report['commands'].append(command)
                        for line in process.stdout:
                            print(line, end='', flush=True)
                            log.write(line)
                            log.flush()
                            if phase_log is not None:
                                phase_log.write(line)
                                phase_log.flush()
                            workload_collector.feed(line)
                            hosted_clock_collector.feed(line)
                        phase_rc = process.wait()
                phase_elapsed = time.monotonic() - phase_started
                try:
                    workload_warnings = workload_collector.finish(require_all=(phase_rc == 0))
                    hosted_clock_warnings = hosted_clock_collector.finish(require_all=(phase_rc == 0))
                    for record in hosted_clock_warnings.values():
                        if record['limit_seconds'] != (120 if args.sanitizers else 40):
                            raise ValueError('hosted clock limit conflicts with selected sanitizer configuration')
                    phase_results.extend(read_results(
                        phase_junit, phase_tests, args.timeout, args.warn_fraction,
                        workload_warnings=workload_warnings,
                        hosted_clock_warnings=hosted_clock_warnings if phase_rc == 0 else {}))
                    if phase_rc != 0:
                        report.setdefault('unaccepted_hosted_clock_records', {}).update(hosted_clock_warnings)
                except (OSError, ValueError, KeyError, ET.ParseError) as error:
                    phase_errors.append(str(error))
                if phased:
                    report['phases'].append({
                        'name': phase_name, 'selected': [test['name'] for test in phase_tests],
                        'command': command, 'returncode': phase_rc, 'log_file': phase_log_path.name,
                        'junit_file': phase_junit.name, 'elapsed_seconds': phase_elapsed})
        if phased and not phase_errors and len(phase_results) == len(selected):
            aggregate = ET.Element('testsuites')
            for index in range(1, len(phases) + 1):
                aggregate.append(ET.parse(report_path.with_name(f'{report_path.stem}.phase-{index}.xml')).getroot())
            ET.ElementTree(aggregate).write(junit_path, encoding='utf-8', xml_declaration=True)
        if phased:
            report['ctest_returncode'] = next((phase['returncode'] for phase in report['phases']
                                               if phase['returncode'] != 0), 0)
        else:
            report['ctest_returncode'] = phase_rc
        returncode = report['ctest_returncode']
        if phase_errors:
            raise ValueError('; '.join(phase_errors))
        report['tests'] = phase_results
        if plan:
            if planner.source_identity(plan['source_root']) != plan['source_identity']:
                raise ValueError('source inputs changed during execution')
            if planner.config_identity(args.build_dir / 'CMakeCache.txt', args.config) != plan['config_identity']:
                raise ValueError('compiler/configuration inputs changed during execution')
        report['result_counts'] = {
            status: sum(test['status'] == status for test in report['tests'])
            for status in ('passed', 'omitted', 'incomplete_or_failed')
        }
        report['runtime_omissions'] = [test['name'] for test in report['tests']
                                       if test['status'] == 'omitted']
        report['coverage_limits'] = [dict(test=test['name'], **test['coverage_limit'])
                                     for test in report['tests']
                                     if test.get('status') == 'passed' and test.get('coverage_limit')]
        complete = all(test['status'] in ('passed', 'omitted') for test in report['tests'])
        if returncode == 0 and complete and report['result_counts']['passed']:
            if report['coverage_limits']:
                report['status'] = 'passed_with_warnings'
            elif report['runtime_omissions']:
                report['status'] = 'passed_with_omissions'
            else:
                report['status'] = 'passed'
        else:
            if returncode == 0 and complete:
                report['error'] = 'No selected test passed; all selected coverage was omitted'
            returncode = returncode or 1
        for test in report['tests']:
            if test['status'] == 'passed':
                for warning in test['workload_warnings']:
                    if test.get('coverage_limit'):
                        message = (f"{test['name']} passed mandatory functional assertions; compact "
                                   f"default media clock coverage is incomplete. {warning}")
                    else:
                        message = f"{test['name']} passed all assertions. {warning}"
                    annotation(message, title='CI test workload')
            if test.get('status') == 'passed' and test.get('coverage_limit'):
                limit = test['coverage_limit']
                annotation(f"{test['name']} mandatory functional coverage passed; compact default "
                           f"media clock case {limit['case']} is incomplete after the "
                           f"{limit['limit_seconds']}s hosted throughput limit.",
                           title='CI compact clock coverage')
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
    parser.add_argument('--scope', choices=(*SCOPES, 'gui', 'contract'), required=True)
    parser.add_argument('--selection-plan', type=Path)
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
