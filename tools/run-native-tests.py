#!/usr/bin/env python3
"""Run registered native tests, retaining typed incomplete smoke as explicit evidence."""
import argparse
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time


def load_tool(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


smoke = load_tool('native_smoke', 'run-gui-smoke.py')
ci = load_tool('native_ci_results', 'run-ci-tests.py')


def select_tests(tests, label, exclude_label=None):
    names = [test['name'] for test in tests]
    if len(set(names)) != len(names):
        raise ValueError('CTest inventory contains duplicate names')
    selected = [test for test in tests if label in ci.properties(test).get('LABELS', [])
                and (not exclude_label or exclude_label not in ci.properties(test).get('LABELS', []))]
    if not selected:
        raise ValueError('Selected CTest label contains no tests')
    return selected


def direct_context(test, build_dir):
    properties = ci.properties(test)
    supported = {'LABELS', 'TIMEOUT', 'WORKING_DIRECTORY', 'ENVIRONMENT', 'RUN_SERIAL',
                 'PROCESSORS', 'COST'}
    unsupported = set(properties) - supported
    if unsupported:
        raise ValueError(f"Cannot directly preserve {test['name']} properties: {sorted(unsupported)}")
    command = test.get('command')
    if not isinstance(command, list) or not command or not all(isinstance(x, str) and x for x in command):
        raise ValueError(f"Missing registered command for {test['name']}; build its prerequisites first")
    timeout = float(properties.get('TIMEOUT', 0))
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError(f"Missing finite positive registered TIMEOUT for {test['name']}")
    cwd = Path(properties.get('WORKING_DIRECTORY', build_dir)).resolve()
    if not cwd.is_dir():
        raise ValueError(f"Missing registered working directory for {test['name']}")
    environment = os.environ.copy()
    settings = properties.get('ENVIRONMENT', [])
    if not isinstance(settings, list):
        raise ValueError('CTest ENVIRONMENT must be a list')
    for setting in settings:
        name, separator, value = setting.partition('=')
        if not separator or not name or '\x00' in setting:
            raise ValueError('Malformed CTest ENVIRONMENT entry')
        environment[name] = value
    return command, cwd, environment, timeout


def run(args):
    started = time.monotonic()
    directory = args.report_dir.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    report_path = directory / 'native-tests.json'
    if report_path.exists():
        raise FileExistsError('Native test report already exists')
    report = {'scope': args.label, 'status': 'failed', 'selected': [], 'omitted': [], 'tests': []}
    try:
        base = [args.ctest, '--test-dir', str(args.build_dir.resolve())]
        if args.config:
            base += ['--build-config', args.config]
        listing = subprocess.run(base + ['--show-only=json-v1'], check=True, capture_output=True, text=True)
        selected = select_tests(json.loads(listing.stdout)['tests'], args.label, args.exclude_label)
        report['selected'] = [test['name'] for test in selected]
        direct = {test['name']: test for test in selected if test['name'] == 'gui_workflow'
                  or (test['name'] == 'gui_adapter_conformance' and args.adapter_timeout is not None)}
        if args.adapter_timeout is not None and 'gui_adapter_conformance' not in direct:
            raise ValueError('Adapter allowance requested without the registered adapter test')
        # Validate every directly executed case before starting any test. The
        # complementary CTest cases retain all their own registered properties.
        contexts = {name: direct_context(test, args.build_dir) for name, test in direct.items()}
        remaining = [test for test in selected if test['name'] not in direct]
        if remaining:
            junit = directory / 'native-remaining.xml'
            log_path = directory / 'native-remaining.log'
            if junit.exists():
                raise FileExistsError('Native CTest result already exists')
            pattern = '^(' + '|'.join(re.escape(test['name']) for test in remaining) + ')$'
            command = base + ['--output-on-failure', '--no-tests=error', '--stop-on-failure',
                              '--parallel', '1', '--output-junit', str(junit), '-R', pattern]
            with log_path.open('xb') as log:
                with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as process:
                    for line in process.stdout:
                        log.write(line)
                        log.flush()
                        sys.stdout.buffer.write(line)
                        sys.stdout.buffer.flush()
                    code = process.wait()
            if code:
                raise ValueError(f'Complementary CTest selection failed (exit {code}); see {log_path}')
            results = ci.read_results(junit, remaining, 0, .8)
            if not all(test['status'] == 'passed' for test in results):
                raise ValueError('CTest reported incomplete or failed mandatory cases')
            report['tests'].extend(results)
            for test in results:
                for warning in test['workload_warnings']:
                    ci.annotation(f"{test['name']} completed all assertions. {warning}", 'CI test workload')
                if test['near_timeout']:
                    ci.annotation(f"{test['name']} completed in {test['seconds']:.1f}s near its "
                                  f"{test['timeout_seconds']:.1f}s allowance.")
        if 'gui_workflow' in contexts:
            command, cwd, environment, timeout = contexts['gui_workflow']
            before = time.monotonic()
            smoke_report = directory / 'gui_workflow.json'
            result = smoke.run(command, directory / 'gui_workflow.log', args.summary,
                               args.allow_budget_exhaustion, smoke_report, timeout, cwd, environment)
            outcome = json.loads(smoke_report.read_text())
            report['tests'].append({'name': 'gui_workflow', 'status': outcome['status'],
                                    'seconds': time.monotonic() - before, 'timeout_seconds': timeout,
                                    'near_timeout': False})
            if result:
                raise ValueError('Registered GUI smoke failed; see its retained log')
        if 'gui_adapter_conformance' in contexts:
            command, cwd, environment, _ = contexts['gui_adapter_conformance']
            before = time.monotonic()
            log_path = directory / 'gui_adapter_conformance.log'
            with log_path.open('xb') as log:
                result = subprocess.run(command, cwd=cwd, env=environment, stdout=log,
                                        stderr=subprocess.STDOUT, timeout=args.adapter_timeout)
            elapsed = time.monotonic() - before
            print(log_path.read_text(encoding='utf-8', errors='replace'), end='', flush=True)
            if result.returncode:
                raise ValueError(f'Registered full adapter failed (exit {result.returncode})')
            # Keep sanitizer output fatal even if a recoverable sanitizer exits 0.
            if smoke.SANITIZER_DIAGNOSTIC.search(log_path.read_text(encoding='utf-8', errors='replace')):
                raise ValueError('Adapter emitted a sanitizer diagnostic')
            report['tests'].append({'name': 'gui_adapter_conformance', 'status': 'passed',
                                    'seconds': elapsed, 'timeout_seconds': args.adapter_timeout,
                                    'near_timeout': elapsed >= args.adapter_warn_after})
            if elapsed > args.adapter_warn_after:
                ci.annotation(f'All original adapter assertions completed in {elapsed:.1f}s, above '
                              f'{args.adapter_warn_after:g}s; bounded allowance {args.adapter_timeout:g}s.',
                              'Rev adapter workload')
        report['status'] = ('passed_with_warnings' if any(test['status'] == 'incomplete'
                            for test in report['tests']) else 'passed')
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError, ci.ET.ParseError) as error:
        report['error'] = str(error)
        print(f'Native tests failed: {error}', file=sys.stderr)
    report['elapsed_seconds'] = time.monotonic() - started
    with report_path.open('x', encoding='utf-8') as stream:
        json.dump(report, stream, indent=2)
        stream.write('\n')
    ci.write_summary(args.summary, report)
    print(f"Native test scope: {report['status']}; smoke incomplete outcomes are recorded as incomplete, not passes.")
    return 1 if report['status'] == 'failed' else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--ctest', default='ctest')
    parser.add_argument('--config', default='Release')
    parser.add_argument('--label', default='native_gui')
    parser.add_argument('--exclude-label')
    parser.add_argument('--report-dir', type=Path, required=True)
    parser.add_argument('--summary', type=Path, default=os.environ.get('GITHUB_STEP_SUMMARY') or None)
    parser.add_argument('--allow-budget-exhaustion', action='store_true')
    parser.add_argument('--adapter-timeout', type=float)
    parser.add_argument('--adapter-warn-after', type=float, default=330)
    args = parser.parse_args(argv)
    if args.adapter_timeout is not None and (not math.isfinite(args.adapter_timeout)
            or not 0 < args.adapter_timeout <= 900 or not 0 <= args.adapter_warn_after <= args.adapter_timeout):
        parser.error('adapter allowance must be positive and at most900s, with a bounded warning threshold')
    try:
        return run(args)
    except (OSError, ValueError) as error:
        parser.exit(1, f'Native test runner: {error}\n')


if __name__ == '__main__':
    raise SystemExit(main())
