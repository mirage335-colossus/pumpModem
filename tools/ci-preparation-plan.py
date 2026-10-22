#!/usr/bin/env python3
"""Capture and aggregate the four native Debug sanitizer preparation parts.

Only capture executes a test binary, and only its non-DSP --ci-inventory mode.
Successful captures are partial coverage; all four are required for aggregation.
"""
import argparse
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('ci_test_plan', ROOT / 'tools/ci-test-plan.py')
planner = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(planner)
runner = planner.runner
TEST = 'scheduled_parallel_preparation'
TARGET = 'test_' + TEST
CACHE_PART = 'DATAPUMP_CI_PREPARATION_PART'
PARTS = ('0', '1', '2', '3')
WORKLOAD_LIMIT_SECONDS = 9000
WORKLOAD_WARNING_SECONDS = 7200
# Independent compatibility inventory: a producer cannot redefine completeness
# by reporting a smaller binary catalog or just the cases it happened to run.
ALL_CASES = ('reservation_policy', 'arithmetic_default', 'arithmetic_fp32',
             'arithmetic_fp64', 'shared', 'small', 'tiled', 'cancellation_and_no_false_end')
EXPECTED_PARTS = {str(index): [name] for index, name in enumerate(ALL_CASES[1:4])}
EXPECTED_PARTS['3'] = [ALL_CASES[0], *ALL_CASES[4:]]


def strict_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'duplicate JSON key: {key}')
        result[key] = value
    return result


def read_json(raw):
    return json.loads(raw, object_pairs_hook=strict_object,
                      parse_constant=lambda value: (_ for _ in ()).throw(
                          ValueError(f'invalid JSON number: {value}')))


def validate_catalog(catalog):
    if (not isinstance(catalog, dict) or set(catalog) != {'schema', 'parts', 'all_cases'}
            or type(catalog['schema']) is not int or catalog['schema'] != 1):
        raise ValueError('invalid preparation binary catalog schema')
    parts, cases = catalog['parts'], catalog['all_cases']
    if (not isinstance(parts, dict) or set(parts) != set(PARTS)
            or not isinstance(cases, list) or cases != list(ALL_CASES)):
        raise ValueError('preparation catalog does not cover the original eight cases')
    if parts != EXPECTED_PARTS:
        raise ValueError('preparation catalog has missing, duplicated or reassigned cases')


def context_identity():
    context = {key: os.environ.get(variable, '') for key, variable in (
        ('source_sha', 'GITHUB_SHA'), ('run_id', 'GITHUB_RUN_ID'),
        ('repository', 'GITHUB_REPOSITORY'), ('run_attempt', 'GITHUB_RUN_ATTEMPT'))}
    if any(not value for value in context.values()):
        raise ValueError('preparation evidence requires SHA, run, repository and attempt context')
    return context


def clean_source(source_root, context):
    identity = planner.source_identity(source_root)
    if (identity['working_tree_sha256'] != planner.digest(b'')
            or identity['untracked_input_sha256'] != planner.digest({})):
        raise ValueError('preparation evidence requires a clean source checkout')
    if identity['revision'] != context['source_sha']:
        raise ValueError('current source revision differs from GITHUB_SHA')
    return identity


def cache_values(path):
    entries = {}
    for line in path.read_text(encoding='utf-8').splitlines():
        if line and not line.startswith(('#', '//')) and '=' in line and ':' in line.split('=', 1)[0]:
            key, value = line.split('=', 1)
            key = key.rsplit(':', 1)[0]
            if key in entries:
                raise ValueError(f'duplicate CMake cache entry: {key}')
            entries[key] = value
    return entries


def configurations(cache, config, part, source_root):
    values = cache_values(cache)
    if values.get(CACHE_PART) != part:
        raise ValueError('configured preparation part differs from requested part')
    if values.get('CMAKE_BUILD_TYPE') != 'Debug' or config not in (None, 'Debug'):
        raise ValueError('preparation partitions require native Debug configuration')
    if values.get('DATAPUMP_SANITIZERS') != 'ON':
        raise ValueError('preparation partitions require sanitizers ON')
    if Path(values.get('CMAKE_HOME_DIRECTORY', '')).resolve() != source_root.resolve():
        raise ValueError('configured CMake source root differs from selected source root')
    raw = planner.config_identity(cache, config, include_cache_values=True)
    common = planner.config_identity(cache, config, ignored_cache_keys=(CACHE_PART,))
    return raw, common


def registration_inventory(tests, registration):
    canonical = planner.canonical_inventory(tests)
    if set(registration) != {test['name'] for test in canonical}:
        raise ValueError('prerequisites do not cover the whole configured CTest inventory')
    selected, omitted, _ = runner.select_tests(tests, 'preparation', sanitizers=True)
    if omitted or [test['name'] for test in selected] != [TEST]:
        raise ValueError('preparation scope must select only the original single test')
    if registration.get(TEST) != [TARGET]:
        raise ValueError('preparation prerequisite differs from the original single target')
    labels = runner.properties(selected[0]).get('LABELS', [])
    if not {'regular', 'contract', 'preparation'}.issubset(labels):
        raise ValueError('preparation registration lost its original scope labels')
    props = runner.properties(selected[0])
    if props.get('DISABLED', False) is not False or 'SKIP_RETURN_CODE' in props:
        raise ValueError('preparation registration permits disabled or omitted coverage')
    return canonical, selected[0]


def case_events(raw, number, expected):
    """Accept markers only in the original selected CTest execution interval."""
    events = []
    started = completed = replay = False
    start = re.compile(r'^\s*Start\s+(\d+): (.+)$')
    completion = re.compile(r'^\s*\d+/\d+\s+Test\s+#(\d+):\s*(\S+)\s+\.+\s+Passed\s+([0-9.]+) sec\s*$')
    marker = re.compile(r'^\s*(\d+): PREPARATION_CASE_(BEGIN|END): ([a-z0-9_]+)$')
    direct_prefix = re.compile(r'^\s*\d+: PREPARATION_CASE_(?:BEGIN|END)(?:\s|:|$)')
    for line in raw.decode('utf-8', 'strict').splitlines(keepends=True):
        text = line.rstrip('\r\n')
        if text.strip().startswith(('The following tests FAILED:', 'The following tests passed:')):
            replay = True
        if replay:
            continue
        first = start.fullmatch(text)
        if first:
            if (not line.endswith('\n') or started or completed
                    or int(first[1]) != number or first[2] != TEST):
                raise ValueError('conflicting or duplicate CTest Start identity')
            started = True
            continue
        if re.match(r'^\s*Start\s+\d', text):
            raise ValueError('malformed CTest Start identity')
        last = completion.fullmatch(text)
        if last:
            if (not line.endswith('\n') or not started or completed
                    or int(last[1]) != number or last[2] != TEST):
                raise ValueError('conflicting or duplicate CTest completion identity')
            completed = True
            continue
        if re.match(r'^\s*\d+/\d+\s+Test\s+#\d+', text):
            raise ValueError('CTest completion did not report the selected test passed')
        event = marker.fullmatch(text)
        if event:
            if (not line.endswith('\n') or int(event[1]) != number
                    or not started or completed):
                raise ValueError('preparation marker lacks an active matching CTest identity')
            events.append({'event': event[2], 'case': event[3]})
        elif direct_prefix.match(text):
            raise ValueError('malformed preparation marker framing')
        # Unprefixed markers, nested child prefixes and command metadata are
        # deliberately not evidence, including any replay after completion.
    wanted = [{'event': event, 'case': name} for name in expected for event in ('BEGIN', 'END')]
    if not started or not completed or events != wanted:
        raise ValueError('preparation case events are incomplete, duplicated or out of order')
    return events


def validate_runner_report(report, source_sha):
    if not isinstance(report, dict):
        raise ValueError('runner report must be a JSON object')
    counts = report.get('result_counts')
    if (report.get('scope') != 'preparation' or report.get('status') != 'passed'
            or report.get('source_sha') != source_sha
            or type(report.get('ctest_returncode')) is not int or report['ctest_returncode'] != 0
            or report.get('selected') != [TEST]
            or counts != {'passed': 1, 'omitted': 0, 'incomplete_or_failed': 0}
            or any(type(value) is not int for value in counts.values())):
        raise ValueError('runner report does not record exactly one successful preparation test')
    for field in ('omitted', 'runtime_omissions', 'coverage_limits'):
        if report.get(field) != []:
            raise ValueError(f'runner report has omitted or incomplete coverage: {field}')
    if report.get('error') or report.get('phases') or report.get('unaccepted_hosted_clock_records'):
        raise ValueError('runner report has an execution error or unexpected phases')
    results = report.get('tests')
    if (not isinstance(results, list) or len(results) != 1 or not isinstance(results[0], dict)
            or results[0].get('name') != TEST
            or results[0].get('status') != 'passed' or results[0].get('workload_warnings') != []
            or results[0].get('coverage_limit')):
        raise ValueError('runner test result has failed, omitted or warning-limited coverage')
    for key in ('seconds', 'timeout_seconds'):
        value = results[0].get(key)
        if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
            raise ValueError(f'runner result has invalid {key}')
    if results[0]['timeout_seconds'] != WORKLOAD_LIMIT_SECONDS:
        raise ValueError('runner preparation timeout differs from the original 9000-second budget')
    if results[0]['seconds'] > WORKLOAD_LIMIT_SECONDS:
        raise ValueError('runner preparation time exceeds the original 9000-second budget')


def seal(record):
    record['capture_sha256'] = planner.digest(record)
    return record


def capture(args):
    build = args.build_dir.resolve()
    source_root = args.source_root.resolve()
    context = context_identity()
    source = clean_source(source_root, context)
    cache = build / 'CMakeCache.txt'
    raw_config, common_config = configurations(cache, args.config, args.part, source_root)
    report_path = args.report.resolve()
    artifacts = {kind: path.read_bytes() for kind, path in (
        ('report', report_path), ('inventory', report_path.with_suffix('.inventory.json')),
        ('junit', report_path.with_suffix('.xml')), ('log', report_path.with_suffix('.log')))}
    report = read_json(artifacts['report'])
    validate_runner_report(report, context['source_sha'])
    if (report.get('inventory_file') != report_path.with_suffix('.inventory.json').name
            or report.get('inventory_sha256') != planner.digest(artifacts['inventory'])):
        raise ValueError('runner inventory sidecar is missing or has a mismatched hash')
    base = [args.ctest, '--test-dir', str(build)]
    if args.config:
        base += ['--build-config', args.config]
    listing = subprocess.run(base + ['--show-only=json-v1'], check=True, capture_output=True).stdout
    if listing != artifacts['inventory']:
        raise ValueError('actual configured CTest inventory differs from the executed inventory')
    tests = read_json(listing)['tests']
    registration = planner.load_registration(build / 'ci-test-prerequisites.json')
    canonical, selected = registration_inventory(tests, registration)
    command = selected.get('command')
    if (not isinstance(command, list) or len(command) != 3
            or command[1:] != ['--ci-part', args.part]):
        raise ValueError('CTest preparation registration must invoke the selected binary part exactly')
    binary = Path(command[0])
    if not binary.is_absolute() or binary.name != TARGET or not binary.is_file():
        raise ValueError('CTest preparation executable is missing or differs from the original binary')
    expected_command = base + ['--output-on-failure', '--verbose', '--no-tests=error', '--parallel', '1',
        '--timeout', '9000', '--output-junit', str(report_path.with_suffix('.xml')),
        '-R', '^(' + TEST + ')$']
    actual_command = report.get('command')
    if not isinstance(actual_command, list) or len(actual_command) != len(expected_command):
        raise ValueError('runner command differs from the serial preparation execution contract')
    timeout_index = expected_command.index('--timeout') + 1
    # The public runner CLI parses timeout as float and records "9000.0".
    # Bind its exact numeric value, retaining the original argv in evidence.
    try:
        timeout_text = actual_command[timeout_index]
        if not isinstance(timeout_text, str) or float(timeout_text) != WORKLOAD_LIMIT_SECONDS:
            raise ValueError('runner timeout differs from the original 9000-second contract')
    except (ValueError, OverflowError) as error:
        raise ValueError('runner timeout differs from the original 9000-second contract') from error
    normalized_command = list(actual_command)
    normalized_command[timeout_index] = '9000'
    if normalized_command != expected_command:
        raise ValueError('runner command differs from the serial preparation execution contract')
    binary_sha = planner.digest(binary.read_bytes())
    catalog_bytes = subprocess.run([str(binary), '--ci-inventory'], check=True,
        capture_output=True, timeout=30).stdout
    catalog = read_json(catalog_bytes)
    validate_catalog(catalog)
    number = next(index for index, test in enumerate(tests, 1) if test['name'] == TEST)
    events = case_events(artifacts['log'], number, catalog['parts'][args.part])
    # Independently read the JUnit instead of trusting the JSON result summary.
    results = runner.read_results(report_path.with_suffix('.xml'), [selected], 9000, .8)
    if (len(results) != 1 or results[0]['status'] != 'passed' or results[0]['workload_warnings']
            or results[0]['seconds'] != report['tests'][0].get('seconds')
            or results[0]['timeout_seconds'] != report['tests'][0].get('timeout_seconds')):
        raise ValueError('actual JUnit evidence differs from the successful runner result')
    record = {'schema': 1, 'kind': 'preparation_part', 'status': 'passed', 'coverage': 'partial',
        'selected_part': args.part, 'cache_part': args.part, 'source_identity': source,
        'ci_context': context, 'raw_config_identity': raw_config, 'config_identity': common_config,
        'ignored_cache_keys': [CACHE_PART], 'catalog': catalog, 'catalog_sha256': planner.digest(catalog),
        'binary_sha256': binary_sha, 'binary_name': binary.name,
        'canonical_inventory': canonical, 'canonical_inventory_sha256': planner.digest(canonical),
        'configured_test_order': [test['name'] for test in tests],
        'registration': registration, 'registration_sha256': planner.digest(registration),
        'selected_command': command, 'ctest_number': number, 'case_events': events,
        'runner_report': report, 'artifact_sha256': {kind: planner.digest(raw) for kind, raw in artifacts.items()}}
    # Stop if any read dependency changed during the capture, including binary
    # replacement or sidecar rewriting by another process.
    if (clean_source(source_root, context) != source
            or configurations(cache, args.config, args.part, source_root) != (raw_config, common_config)
            or planner.digest(binary.read_bytes()) != binary_sha
            or planner.load_registration(build / 'ci-test-prerequisites.json') != registration):
        raise ValueError('preparation inputs changed during evidence capture')
    for kind, path in (('report', report_path), ('inventory', report_path.with_suffix('.inventory.json')),
                       ('junit', report_path.with_suffix('.xml')), ('log', report_path.with_suffix('.log'))):
        if path.read_bytes() != artifacts[kind]:
            raise ValueError('runner artifacts changed during evidence capture')
    seal(record)
    validate_capture(record)
    planner.atomic_json(args.output, record)


def validate_capture(record):
    if (not isinstance(record, dict) or type(record.get('schema')) is not int
            or record['schema'] != 1 or record.get('kind') != 'preparation_part'
            or record.get('status') != 'passed' or record.get('coverage') != 'partial'):
        raise ValueError('preparation capture is not a completed successful partial result')
    payload = {key: value for key, value in record.items() if key != 'capture_sha256'}
    if record.get('capture_sha256') != planner.digest(payload):
        raise ValueError('preparation capture integrity mismatch')
    part = record.get('selected_part')
    if part not in PARTS or record.get('cache_part') != part:
        raise ValueError('preparation capture has invalid or conflicting part selectors')
    validate_catalog(record['catalog'])
    if record.get('catalog_sha256') != planner.digest(record['catalog']):
        raise ValueError('preparation catalog identity mismatch')
    canonical, selected = registration_inventory(record['canonical_inventory'], record['registration'])
    if (canonical != record['canonical_inventory']
            or record.get('canonical_inventory_sha256') != planner.digest(canonical)
            or record.get('registration_sha256') != planner.digest(record['registration'])):
        raise ValueError('preparation CTest or prerequisite identity mismatch')
    validate_runner_report(record['runner_report'], record['ci_context']['source_sha'])
    if record.get('ignored_cache_keys') != [CACHE_PART]:
        raise ValueError('preparation capture normalizes unsupported configuration keys')
    raw_config, common_config = record['raw_config_identity'], record['config_identity']
    values = raw_config.get('cache_values')
    if (not isinstance(values, dict) or values.get(CACHE_PART) != part
            or any(not isinstance(key, str) or not isinstance(value, str) for key, value in values.items())
            or values.get('CMAKE_BUILD_TYPE') != 'Debug' or values.get('DATAPUMP_SANITIZERS') != 'ON'
            or raw_config.get('cache_values_sha256') != planner.digest(values)
            or common_config.get('cache_values_sha256') != planner.digest(
                {key: value for key, value in values.items() if key != CACHE_PART})):
        raise ValueError('preparation raw and normalized cache identities do not match')
    if (raw_config.get('sanitizers') != 'ON' or common_config.get('sanitizers') != 'ON'
            or common_config.get('config') not in ('', 'Debug')
            or {key: value for key, value in raw_config.items() if key not in ('cache_values', 'cache_values_sha256')} !=
               {key: value for key, value in common_config.items() if key != 'cache_values_sha256'}):
        raise ValueError('preparation configuration or sanitizer identity mismatch')
    expected_events = [{'event': event, 'case': name}
                       for name in record['catalog']['parts'][part] for event in ('BEGIN', 'END')]
    if record.get('case_events') != expected_events:
        raise ValueError('preparation capture does not complete every selected case')
    command = record.get('selected_command')
    order = record.get('configured_test_order')
    if (not isinstance(command, list) or len(command) != 3
            or Path(command[0]).name != TARGET or command[1:] != ['--ci-part', part]
            or record.get('binary_name') != TARGET
            or type(record.get('ctest_number')) is not int or record['ctest_number'] < 1):
        raise ValueError('preparation capture registration command or test identity mismatch')
    if (not isinstance(order, list) or len(order) != len(canonical)
            or sorted(order) != [test['name'] for test in canonical]
            or order.index(TEST) + 1 != record['ctest_number']):
        raise ValueError('preparation capture CTest number differs from configured inventory order')
    hashes = record.get('artifact_sha256', {})
    if set(hashes) != {'report', 'inventory', 'junit', 'log'}:
        raise ValueError('preparation capture omits actual runner artifact identities')
    for value in [record.get('binary_sha256'), *hashes.values()]:
        if not isinstance(value, str) or not re.fullmatch('[0-9a-f]{64}', value):
            raise ValueError('preparation capture contains invalid artifact hashes')
    if record['runner_report'].get('inventory_sha256') != hashes['inventory']:
        raise ValueError('preparation capture runner inventory hash mismatch')
    # The report itself is embedded, allowing its bytes to be kept in the
    # producer artifact while its semantic result remains reviewable here.
    return part


def aggregate(args):
    context = context_identity()
    source = clean_source(args.source_root.resolve(), context)
    records, input_hashes = {}, {}
    for path in args.parts:
        raw = path.read_bytes()
        record = read_json(raw)
        part = validate_capture(record)
        if part in records:
            raise ValueError(f'duplicate preparation result part: {part}')
        records[part] = record
        input_hashes[part] = planner.digest(raw)
    if set(records) != set(PARTS):
        raise ValueError('preparation aggregation requires all four unique completed parts')
    first = records['0']
    common_keys = ('config_identity', 'catalog', 'catalog_sha256', 'canonical_inventory',
                   'canonical_inventory_sha256', 'configured_test_order', 'ctest_number',
                   'registration', 'registration_sha256', 'binary_sha256')
    for part, record in records.items():
        if record['ci_context'] != context or record['source_identity'] != source:
            raise ValueError(f'preparation part {part} has mismatched source or workflow context')
        for key in common_keys:
            if record[key] != first[key]:
                raise ValueError(f'preparation part {part} has mismatched {key}')
    completed = [event['case'] for part in PARTS for event in records[part]['case_events']
                 if event['event'] == 'END']
    if len(completed) != len(ALL_CASES) or set(completed) != set(ALL_CASES):
        raise ValueError('completed preparation union differs from the original eight cases')
    timings = {part: records[part]['runner_report']['tests'][0]['seconds'] for part in PARTS}
    total = math.fsum(timings.values())
    if not math.isfinite(total) or total > WORKLOAD_LIMIT_SECONDS:
        raise ValueError(f'combined preparation serial-equivalent workload {total}s exceeds '
                         f'the original {WORKLOAD_LIMIT_SECONDS}s budget')
    result = {'schema': 1, 'kind': 'preparation_aggregate', 'scope': 'preparation',
        'status': 'passed', 'coverage': 'complete', 'completed_parts': list(PARTS),
        'completed_cases': list(ALL_CASES), 'source_identity': source, 'ci_context': context,
        **{key: first[key] for key in common_keys}, 'part_input_sha256': input_hashes,
        'part_test_seconds': timings, 'serial_equivalent_seconds': total,
        'serial_equivalent_limit_seconds': WORKLOAD_LIMIT_SECONDS,
        'serial_equivalent_warning_seconds': WORKLOAD_WARNING_SECONDS,
        'serial_equivalent_near_timeout': total >= WORKLOAD_WARNING_SECONDS,
        'raw_config_identities': {part: records[part]['raw_config_identity'] for part in PARTS},
        'part_capture_sha256': {part: records[part]['capture_sha256'] for part in PARTS}}
    if clean_source(args.source_root.resolve(), context) != source:
        raise ValueError('source changed during preparation aggregation')
    planner.atomic_json(args.output, result)
    if result['serial_equivalent_near_timeout']:
        runner.annotation(f'Complete preparation passed; combined serial-equivalent test workload '
                          f'{total:.2f}s / {WORKLOAD_LIMIT_SECONDS}s approaches its original '
                          'allowance. This is the sum of part test times, not distributed wall elapsed.')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    producer = commands.add_parser('capture')
    producer.add_argument('--source-root', type=Path, default=ROOT)
    producer.add_argument('--build-dir', type=Path, required=True)
    producer.add_argument('--part', choices=PARTS, required=True)
    producer.add_argument('--report', type=Path, required=True)
    producer.add_argument('--output', type=Path, required=True)
    producer.add_argument('--config')
    producer.add_argument('--ctest', default='ctest')
    producer.set_defaults(func=capture)
    consumer = commands.add_parser('aggregate')
    consumer.add_argument('--source-root', type=Path, default=ROOT)
    consumer.add_argument('--parts', type=Path, nargs='+', required=True)
    consumer.add_argument('--output', type=Path, required=True)
    consumer.set_defaults(func=aggregate)
    args = parser.parse_args(argv)
    try:
        args.func(args)
    except (OSError, ValueError, KeyError, TypeError, runner.ET.ParseError, subprocess.SubprocessError) as error:
        print(f'preparation evidence error: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
