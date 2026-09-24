#!/usr/bin/env python3
"""Run native smoke strictly, with one opt-in incomplete computation outcome."""
import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys


BUDGET_PREFIX = 'INCOMPLETE GUI_SMOKE_BUDGET:'
# This is the typed application's complete diagnostic, not a generic timeout
# message. Keep the fixed fields aligned with SmokeBudgetExhausted::what().
BUDGET_DIAGNOSTIC = re.compile(
    r'INCOMPLETE GUI_SMOKE_BUDGET: phase=([0-9]+) '
    r'elapsed=([0-9]+\.[0-9]{6}) budget=([0-9]+\.[0-9]{6}) '
    r'tx_id=([0-9]+) fraction=([0-9]+\.[0-9]{6}) '
    r'media_seconds=([0-9]+\.[0-9]{6}) samples=([0-9]+) '
    r'tail=([01]) progress_age=([0-9]+\.[0-9]{6}) result=incomplete')
SANITIZER_DIAGNOSTIC = re.compile(
    r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|'
    r'ThreadSanitizer|MemorySanitizer|HWAddressSanitizer|'
    r'\bruntime error:', re.IGNORECASE)


class Output:
    """Bounded classification state; the complete bytes are retained on disk."""
    def __init__(self):
        self.nonempty_lines = 0
        self.first_line = ''
        self.sanitizer = False
        self.budget_marker_seen = False

    def observe(self, line):
        self.sanitizer = self.sanitizer or bool(SANITIZER_DIAGNOSTIC.search(line))
        self.budget_marker_seen = self.budget_marker_seen or BUDGET_PREFIX in line
        # Accept a CRLF line ending, but never normalize diagnostic whitespace.
        line = line.rstrip('\r\n')
        if not line.strip():
            return
        self.nonempty_lines += 1
        if self.nonempty_lines == 1:
            self.first_line = line if len(line) <= 4096 else ''


def valid_budget_diagnostic(line):
    if len(line) > 4096:
        return False
    match = BUDGET_DIAGNOSTIC.fullmatch(line)
    if not match:
        return False
    phase, elapsed, budget, identity, fraction, media, samples, tail, age = match.groups()
    # Reject malformed numeric evidence without turning this runner into a
    # second simulation-progress implementation. The application owns that
    # policy and emits this diagnostic only for recent same-transmission work.
    numbers = [float(value) for value in (elapsed, budget, fraction, media, age)]
    if not all(math.isfinite(value) for value in numbers):
        return False
    elapsed, budget, fraction, media, age = numbers
    return (0 <= int(phase) <= 23 and 10 <= budget <= 600 and elapsed >= budget
            and int(identity) > 0 and 0 <= fraction <= 1 and media >= 0
            and int(samples) >= 0 and tail in ('0', '1') and 0 <= age <= 30)


def classify(returncode, output, allow_budget_exhaustion=False):
    if output.sanitizer:
        raise ValueError('sanitizer diagnostic reported; see the retained smoke log')
    if returncode == 0:
        # A malformed or duplicated incomplete diagnostic must not become a
        # pass merely because a wrapper swallowed the application's exit code.
        if output.budget_marker_seen:
            raise ValueError('incomplete budget diagnostic accompanied exit 0')
        return 'passed'
    if (allow_budget_exhaustion and returncode == 75
            and output.nonempty_lines == 1
            and valid_budget_diagnostic(output.first_line)):
        return 'incomplete'
    raise ValueError(f'native smoke failed (exit {returncode}); see the retained smoke log')


def workflow_text(value):
    return value.replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')


def record_summary(path, status, log_path, detail):
    if path:
        with path.open('a', encoding='utf-8') as stream:
            stream.write(f'### Native GUI smoke: {status}\n\n{detail}\n\n'
                         f'Full per-run log: `{log_path}`.\n\n')


def run(command, log_path, summary_path=None, allow_budget_exhaustion=False):
    log_path.parent.mkdir(parents=True, exist_ok=True)
    output = Output()
    metadata = {'command': command, 'allow_budget_exhaustion': allow_budget_exhaustion,
                'started_utc': datetime.now(timezone.utc).isoformat(),
                'source_sha': os.environ.get('GITHUB_SHA', ''),
                'run_id': os.environ.get('GITHUB_RUN_ID', ''),
                'run_attempt': os.environ.get('GITHUB_RUN_ATTEMPT', '')}
    # Never overwrite evidence from another attempt. CI supplies a unique path
    # for every job/attempt and uploads it even when this runner reports failure.
    with log_path.open('xb') as log:
        log.write(('# GUI smoke invocation ' + json.dumps(metadata) + '\n').encode('utf-8'))
        log.flush()
        try:
            with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  stdin=subprocess.DEVNULL) as process:
                for line in process.stdout:
                    log.write(line)
                    log.flush()
                    sys.stdout.buffer.write(line)
                    sys.stdout.buffer.flush()
                    output.observe(line.decode('utf-8', errors='replace'))
                returncode = process.wait()
            status = classify(returncode, output, allow_budget_exhaustion)
            if status == 'incomplete':
                detail = ('The instrumented native smoke exhausted its overall budget while '
                          'sampled simulation was advancing. Remaining workflow coverage is '
                          'incomplete; this is not a smoke pass. ' + output.first_line)
            else:
                detail = 'The native smoke completed successfully.'
        except (OSError, ValueError) as error:
            status, detail = 'failed', str(error)
        log.write(f'\n# GUI smoke result: {status}\n# {detail}\n'.encode('utf-8'))
    record_summary(summary_path, status, log_path, detail)
    if status == 'incomplete':
        print('::warning title=Incomplete instrumented GUI smoke::' + workflow_text(detail))
    elif status == 'failed':
        print(f'Native GUI smoke failed: {detail}', file=sys.stderr)
    return 1 if status == 'failed' else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--summary', type=Path, default=os.environ.get('GITHUB_STEP_SUMMARY'))
    parser.add_argument('--allow-budget-exhaustion', action='store_true',
                        help='Accept only the typed progressing-computation exit as incomplete')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('a native smoke command is required after --')
    try:
        return run(command, args.log, args.summary, args.allow_budget_exhaustion)
    except OSError as error:
        parser.exit(1, f'Native GUI smoke runner: {error}\n')


if __name__ == '__main__':
    raise SystemExit(main())
