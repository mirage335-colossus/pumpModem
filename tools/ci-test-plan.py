#!/usr/bin/env python3
"""Build and validate deterministic CI test partitions from CTest registration."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('run_ci_tests', ROOT / 'tools/run-ci-tests.py')
runner = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(runner)
CONTRACT_PARTS = ('general', 'preparation', 'live', 'calibration')
CORE_PARTS = ('0', '1')
SCHEMA = 1


def canonical_inventory(tests):
    names = [test.get('name') for test in tests]
    if None in names or len(names) != len(set(names)):
        raise ValueError('CTest discovery has missing or duplicate names')
    records = []
    for test in tests:
        props = runner.properties(test)
        # Commands may be absent before a selected executable is built. All
        # registered behavior (timeouts, environment, serial/resource rules,
        # skip policy and labels) must still agree between producers.
        if 'LABELS' in props:
            labels = props['LABELS']
            props['LABELS'] = sorted(labels.split(';') if isinstance(labels, str) else labels)
        records.append({'name': test['name'], 'properties': [
            {'name': key, 'value': value} for key, value in sorted(props.items())]})
    return sorted(records, key=lambda item: item['name'])


def digest(value):
    raw = value if isinstance(value, bytes) else json.dumps(value, sort_keys=True, separators=(',', ':')).encode()
    return hashlib.sha256(raw).hexdigest()


def contract_partition(tests):
    contract = [test for test in tests if 'contract' in runner.properties(test).get('LABELS', [])
                and 'native_gui' not in runner.properties(test).get('LABELS', [])]
    by_name = {test['name']: test for test in contract}
    live = {'live', 'live_profiles'} & set(by_name)
    preparation = {'scheduled_parallel_preparation'} & set(by_name)
    calibration = {'differential_receiver_probability'} & set(by_name)
    excluded = live | preparation | calibration
    return {
        'general': [test for test in contract if test['name'] not in excluded],
        'preparation': [by_name[name] for name in sorted(preparation)],
        'live': [by_name[name] for name in sorted(live)],
        'calibration': [by_name[name] for name in sorted(calibration)],
    }


def partition_model(tests, scope):
    if scope == 'core':
        selected, omitted, _ = runner.select_tests(tests, 'core')
        if omitted:
            raise ValueError('unsharded core plan cannot omit tests')
        names = sorted(test['name'] for test in selected)
        parts = {name: [] for name in CORE_PARTS}
        # Keep the pair together for the existing sanitizer isolated GUI phase.
        pair = {'gui_controller', 'gui_application'}
        pair = pair if pair.issubset(names) else set()
        if len(pair) == 2:
            parts['0'].extend(sorted(pair))
        remainder = [name for name in names if name not in pair]
        for index, name in enumerate(remainder):
            parts[str(index % 2)].append(name)
        return {key: sorted(value) for key, value in parts.items()}
    if scope == 'contract':
        partitions = contract_partition(tests)
        return {part: sorted(test['name'] for test in partitions[part]) for part in CONTRACT_PARTS}
    raise ValueError(f'unsupported plan scope: {scope}')


def source_identity(source_root):
    root = Path(source_root).resolve()
    # Container checkout actions use a temporary HOME for their safe-directory
    # setting. Trust only this explicitly selected checkout for these reads.
    git = ['git', '-c', 'safe.directory=' + str(root), '-C', str(root)]
    rev = subprocess.run(git + ['rev-parse', 'HEAD'], check=True,
                         capture_output=True, text=True).stdout.strip()
    diff = subprocess.run(git + ['diff', '--binary', 'HEAD', '--'], check=True,
                          capture_output=True).stdout
    untracked = subprocess.run(git + [
        'ls-files', '--others', '--exclude-standard', '-z', '--',
        'src', 'include', 'tests', 'cmake', 'tools', '.github/workflows', 'CMakeLists.txt', 'build.sh'],
        check=True, capture_output=True).stdout
    extra = {name: digest((root / name).read_bytes())
             for name in sorted(untracked.decode().split('\0')) if name}
    return {'revision': rev, 'working_tree_sha256': digest(diff),
            'untracked_input_sha256': digest(extra)}


def config_identity(cache_path, config, *, ignored_cache_keys=(), include_cache_values=False):
    """Hash configured inputs; optional cache detail permits narrow normalization.

    Existing callers retain the same identity and every semantic cache key.
    """
    entries = {}
    for line in Path(cache_path).read_text(encoding='utf-8', errors='replace').splitlines():
        if not line or line.startswith(('#', '//')) or '=' not in line or ':' not in line.split('=', 1)[0]:
            continue
        key, value = line.split('=', 1)
        key = key.rsplit(':', 1)[0]
        # CTest's dashboard SITE defaults to the container hostname. It does
        # not configure compilation or a registered test, and differs between
        # otherwise identical producers. Keep every semantic cache entry and
        # the separately hashed compiler/library inputs in the identity.
        if key in {'CMAKE_CACHEFILE_DIR', 'CMAKE_HOME_DIRECTORY', 'CMAKE_MAKE_PROGRAM', 'SITE'} or key in ignored_cache_keys:
            continue
        entries[key] = value
    if not entries:
        raise ValueError('CMake cache has no semantic configuration entries')
    build_dir = Path(cache_path).parent
    compiler_files = sorted(build_dir.glob('CMakeFiles/*/CMake*Compiler.cmake'))
    compiler_identity = {str(path.relative_to(build_dir)): digest(path.read_bytes())
                         for path in compiler_files}
    # Include the actual compiler and configured native/SDK libraries, not just
    # their install paths. Missing optional/not-found entries are left to CMake.
    inputs = {}
    for key, value in entries.items():
        if key in ('CMAKE_C_COMPILER', 'CMAKE_CXX_COMPILER') or 'LIBRARY' in key:
            for name in value.split(';'):
                path = Path(name)
                if path.is_absolute() and path.is_file():
                    inputs[name] = digest(path.read_bytes())
    result = {'cache_values_sha256': digest(entries), 'config': config or '',
              'compiler_configuration': compiler_identity, 'compiler_library_inputs': inputs,
              'sanitizers': entries.get('DATAPUMP_SANITIZERS', 'OFF')}
    if include_cache_values:
        result['cache_values'] = entries
    return result


def load_registration(path):
    data = json.loads(Path(path).read_text(encoding='utf-8'))
    if data.get('schema') != 1 or not isinstance(data.get('tests'), list):
        raise ValueError('invalid generated CTest prerequisite inventory')
    entries = {}
    for item in data['tests']:
        name, targets = item.get('name'), item.get('targets')
        if not isinstance(name, str) or name in entries or not isinstance(targets, list):
            raise ValueError('duplicate or malformed generated CTest registration')
        if any(not isinstance(target, str) for target in targets):
            raise ValueError(f'malformed prerequisite targets for {name}')
        entries[name] = sorted(set(targets))
    return entries


def atomic_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f'.{path.name}.', dir=path.parent)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except BaseException:
        Path(temporary).unlink(missing_ok=True)
        raise


def prepare(args):
    build_dir = args.build_dir.resolve()
    base = [args.ctest, '--test-dir', str(build_dir)]
    if args.config:
        base += ['--build-config', args.config]
    listing = subprocess.run(base + ['--show-only=json-v1'], check=True, capture_output=True)
    tests = json.loads(listing.stdout)['tests']
    discovered = canonical_inventory(tests)
    registration = load_registration(build_dir / 'ci-test-prerequisites.json')
    if set(registration) != {item['name'] for item in discovered}:
        missing = sorted({item['name'] for item in discovered} - set(registration))
        stale = sorted(set(registration) - {item['name'] for item in discovered})
        raise ValueError(f'CTest registration mismatch (unmapped={missing}, undiscovered={stale})')
    model = partition_model(tests, args.scope)
    all_names = [name for values in model.values() for name in values]
    if len(all_names) != len(set(all_names)):
        raise ValueError('partition model assigns a test more than once')
    selected = model[args.part]
    targets = sorted({target for name in selected for target in registration[name]})
    if args.extra_target:
        targets = sorted(set(targets) | set(args.extra_target))
    cache = build_dir / 'CMakeCache.txt'
    if not cache.is_file():
        raise ValueError('configured CMake cache is missing')
    plan = {
        'schema': SCHEMA, 'scope': args.scope, 'selected_part': args.part,
        'parts': model, 'expected_tests': sorted(all_names), 'selected_tests': selected,
        'build_targets': targets, 'source_identity': source_identity(args.source_root),
        'extra_targets': sorted(set(args.extra_target)),
        'config_identity': config_identity(cache, args.config),
        'registration_sha256': digest(registration), 'registration': registration,
        'canonical_inventory': discovered, 'canonical_inventory_sha256': digest(discovered),
        'configured_inventory_sha256': digest(listing.stdout),
        'source_root': str(args.source_root.resolve()),
        'ci_context': {'run_id': os.environ.get('GITHUB_RUN_ID', ''),
                       'repository': os.environ.get('GITHUB_REPOSITORY', '')},
    }
    plan['plan_sha256'] = model_digest(plan)
    validate_plan(plan)
    atomic_json(args.output, plan)


def model_digest(plan):
    # One common identity for every part. Execution-specific provenance is
    # retained separately and cannot alter the required complete partition.
    local = {'selected_part', 'selected_tests', 'build_targets', 'extra_targets',
             'configured_inventory_sha256', 'source_root', 'plan_sha256'}
    return digest({key: value for key, value in plan.items() if key not in local})


def validate_plan(plan):
    if plan.get('schema') != SCHEMA:
        raise ValueError('unsupported plan schema')
    claimed = plan.get('plan_sha256')
    if claimed != model_digest(plan):
        raise ValueError('plan identity mismatch')
    parts = plan.get('parts')
    expected = plan.get('expected_tests')
    if not isinstance(parts, dict) or not isinstance(expected, list):
        raise ValueError('malformed plan partition model')
    all_tests = [name for names in parts.values() for name in names]
    if len(all_tests) != len(set(all_tests)) or sorted(all_tests) != expected:
        raise ValueError('plan partition union does not match expected inventory')
    part = plan.get('selected_part')
    if part not in parts or plan.get('selected_tests') != parts[part]:
        raise ValueError('selected plan part does not match partition model')
    inventory = plan.get('canonical_inventory')
    if canonical_inventory(inventory) != inventory or digest(inventory) != plan.get('canonical_inventory_sha256'):
        raise ValueError('canonical CTest registration identity mismatch')
    registration = plan.get('registration')
    if not isinstance(registration, dict) or digest(registration) != plan.get('registration_sha256'):
        raise ValueError('prerequisite registration identity mismatch')
    if set(registration) != {test['name'] for test in inventory}:
        raise ValueError('prerequisites do not cover the whole configured inventory')
    # Reconstruct from independent, unpartitioned discovery, never merely from
    # the subset of reports that happened to arrive.
    if partition_model(inventory, plan['scope']) != parts:
        raise ValueError('partition model differs from authoritative complete selection')
    required_targets = {target for name in parts[part] for target in registration[name]}
    extras = plan.get('extra_targets', [])
    if any(target != 'datapump-apps' for target in extras):
        raise ValueError('unsupported explicit non-CTest prerequisite')
    if sorted(required_targets | set(extras)) != plan.get('build_targets'):
        raise ValueError('build targets differ from exact selected prerequisites')


def aggregate(args):
    plan = json.loads(Path(args.plan).read_text(encoding='utf-8'))
    validate_plan(plan)
    if getattr(args, 'scope', None) and args.scope != plan['scope']:
        raise ValueError('aggregate scope differs from required caller scope')
    if getattr(args, 'config', None) and args.config != plan['config_identity']['config']:
        raise ValueError('aggregate configuration differs from required caller configuration')
    reports = {}
    for path in args.parts:
        report = json.loads(Path(path).read_text(encoding='utf-8'))
        part = report.get('selected_part')
        if part in reports:
            raise ValueError(f'duplicate result part: {part}')
        reports[part] = report
    if set(reports) != set(plan['parts']):
        raise ValueError('result parts do not match complete expected part set')
    tests = []
    for part in plan['parts']:
        report = reports[part]
        for key, expected in (('plan_sha256', plan['plan_sha256']),
                              ('source_identity', plan['source_identity']),
                              ('config_identity', plan['config_identity']),
                              ('canonical_inventory_sha256', plan['canonical_inventory_sha256']),
                              ('ci_context', plan['ci_context'])):
            if report.get(key) != expected:
                raise ValueError(f'{part} result has mismatched {key}')
        expected_names = plan['parts'][part]
        if report.get('expected_tests') != expected_names:
            raise ValueError(f'{part} result expected-test list mismatch')
        results = report.get('tests')
        if (not isinstance(results, list) or len(results) != len(expected_names)
                or sorted(item.get('name') for item in results) != expected_names
                or report.get('selected') != expected_names):
            raise ValueError(f'{part} result names do not match expected partition')
        if report.get('ctest_returncode') != 0 or report.get('status') not in ('passed', 'passed_with_warnings', 'passed_with_omissions'):
            raise ValueError(f'{part} result did not pass')
        for result in results:
            if result.get('status') == 'omitted':
                registered = next(test for test in plan['canonical_inventory'] if test['name'] == result['name'])
                props = runner.properties(registered)
                if (type(props.get('SKIP_RETURN_CODE')) is not int or props['SKIP_RETURN_CODE'] != 77
                        or props.get('DISABLED', False) is not False
                        or result.get('omission_reason') != 'declared_skip_return_code'
                        or result.get('skip_return_code') != 77):
                    raise ValueError(f'{part} contains an undeclared omission')
            elif result.get('status') != 'passed':
                raise ValueError(f'{part} contains non-passing test status')
        tests.extend(results)
    counts = {status: sum(test['status'] == status for test in tests)
              for status in ('passed', 'omitted', 'incomplete_or_failed')}
    limits = [dict(test=test['name'], **test['coverage_limit']) for test in tests if test.get('coverage_limit')]
    status = 'passed_with_warnings' if limits else 'passed_with_omissions' if counts['omitted'] else 'passed'
    aggregate_report = {'schema': SCHEMA, 'status': status, 'scope': plan['scope'],
                        'plan_sha256': plan['plan_sha256'], 'source_identity': plan['source_identity'],
                        'config_identity': plan['config_identity'], 'expected_tests': plan['expected_tests'],
                        'tests': sorted(tests, key=lambda test: test['name']), 'parts': sorted(reports),
                        'result_counts': counts, 'coverage_limits': limits,
                        'runtime_omissions': [test['name'] for test in tests if test['status'] == 'omitted']}
    atomic_json(args.output, aggregate_report)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    plan = commands.add_parser('plan')
    plan.add_argument('--build-dir', type=Path, required=True)
    plan.add_argument('--source-root', type=Path, default=ROOT)
    plan.add_argument('--scope', choices=('core', 'contract'), required=True)
    plan.add_argument('--part', required=True)
    plan.add_argument('--config')
    plan.add_argument('--ctest', default='ctest')
    plan.add_argument('--extra-target', action='append', choices=('datapump-apps',), default=[])
    plan.add_argument('--output', type=Path, required=True)
    plan.set_defaults(func=prepare)
    aggregate_parser = commands.add_parser('aggregate')
    aggregate_parser.add_argument('--plan', type=Path, required=True)
    aggregate_parser.add_argument('--scope', choices=('core', 'contract'), required=True)
    aggregate_parser.add_argument('--config', required=True)
    aggregate_parser.add_argument('--parts', nargs='+', type=Path, required=True)
    aggregate_parser.add_argument('--output', type=Path, required=True)
    aggregate_parser.set_defaults(func=aggregate)
    args = parser.parse_args(argv)
    try:
        if args.command == 'plan':
            allowed = CORE_PARTS if args.scope == 'core' else CONTRACT_PARTS
            if args.part not in allowed:
                raise ValueError(f'part {args.part!r} not allowed for {args.scope}')
        elif os.environ.get('GITHUB_RUN_ID'):
            plan = json.loads(args.plan.read_text(encoding='utf-8'))
            if plan['ci_context'] != {'run_id': os.environ['GITHUB_RUN_ID'],
                                      'repository': os.environ.get('GITHUB_REPOSITORY', '')}:
                raise ValueError('aggregate artifacts do not belong to this workflow run')
            if plan['source_identity']['revision'] != os.environ.get('GITHUB_SHA'):
                raise ValueError('aggregate source does not match the required workflow revision')
        args.func(args)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        print(f'CI test planning failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
