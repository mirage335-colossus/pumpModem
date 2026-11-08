#!/usr/bin/env python3
"""Run the native UI smoke strictly and retain per-run evidence."""
import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import threading


SANITIZER_DIAGNOSTIC = re.compile(
    r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|'
    r'ThreadSanitizer|MemorySanitizer|HWAddressSanitizer|'
    r'\bruntime error:', re.IGNORECASE)


class Output:
    """Bounded error classification; complete output stays in the run log."""
    def __init__(self):
        self.sanitizer = False
        self.incomplete = False

    def observe(self, line):
        self.sanitizer |= bool(SANITIZER_DIAGNOSTIC.search(line))
        self.incomplete |= line.startswith('INCOMPLETE ')


def classify(returncode, output):
    if output.sanitizer:
        raise ValueError('sanitizer diagnostic reported; see the retained smoke log')
    if returncode != 0 or output.incomplete:
        raise ValueError(f'native smoke failed (exit {returncode}); see the retained smoke log')
    return 'passed'


def record_summary(path, status, log_path, detail):
    if path:
        with path.open('a', encoding='utf-8') as stream:
            stream.write(f'### Native GUI smoke: {status}\n\n{detail}\n\n'
                         f'Full per-run log: `{log_path}`.\n\n')


def run(command, log_path, summary_path=None, report_path=None, timeout=None, cwd=None, env=None):
    if timeout is not None and (not math.isfinite(timeout) or timeout <= 0):
        raise ValueError('process timeout must be finite and positive')
    if report_path:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        if report_path.exists() or report_path.resolve() == log_path.resolve():
            raise FileExistsError('smoke report must be a new path distinct from its log')
    log_path.parent.mkdir(parents=True, exist_ok=True)
    output = Output()
    identity = os.environ if env is None else env
    metadata = {'command': command,
                'started_utc': datetime.now(timezone.utc).isoformat(),
                'source_sha': identity.get('DATAPUMP_SMOKE_SOURCE_SHA') or identity.get('GITHUB_SHA', ''),
                'run_id': identity.get('GITHUB_RUN_ID', ''),
                'run_attempt': identity.get('GITHUB_RUN_ATTEMPT', '')}
    # Never overwrite evidence from another attempt. CI supplies a unique path
    # for every job/attempt and uploads it even when this runner reports failure.
    with log_path.open('xb') as log:
        log.write(('# GUI smoke invocation ' + json.dumps(metadata) + '\n').encode('utf-8'))
        log.flush()
        try:
            expired = threading.Event()
            with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  stdin=subprocess.DEVNULL, cwd=cwd, env=env,
                                  start_new_session=os.name != 'nt') as process:
                def expire():
                    expired.set()
                    try:
                        if os.name == 'nt':
                            # Registered commands can wrap the GUI with CMake's
                            # environment launcher. Kill its descendants too so
                            # an inherited stdout pipe cannot outlive the cap.
                            try:
                                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                               timeout=5, check=False)
                            except (OSError, subprocess.SubprocessError):
                                pass
                            if process.poll() is None:
                                process.kill()
                        else:
                            os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                timer = threading.Timer(timeout, expire) if timeout is not None else None
                if timer:
                    timer.start()
                try:
                    for line in process.stdout:
                        log.write(line)
                        log.flush()
                        sys.stdout.buffer.write(line)
                        sys.stdout.buffer.flush()
                        output.observe(line.decode('utf-8', errors='replace'))
                    returncode = process.wait()
                finally:
                    if timer:
                        timer.cancel()
                        timer.join()
            if expired.is_set():
                raise ValueError(f'native smoke exceeded its {timeout:g}s process timeout')
            status = classify(returncode, output)
            detail = 'The native UI smoke completed successfully.'
        except (OSError, ValueError) as error:
            status, detail = 'failed', str(error)
        log.write(f'\n# GUI smoke result: {status}\n# {detail}\n'.encode('utf-8'))
    record_summary(summary_path, status, log_path, detail)
    if report_path:
        report = dict(metadata, status=status, detail=detail, log=str(log_path))
        with report_path.open('x', encoding='utf-8') as stream:
            json.dump(report, stream, indent=2)
            stream.write('\n')
    if status == 'failed':
        print(f'Native GUI smoke failed: {detail}', file=sys.stderr)
    return 1 if status == 'failed' else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--summary', type=Path, default=os.environ.get('GITHUB_STEP_SUMMARY') or None)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--timeout', type=float, help='External process deadline; exhaustion is always fatal')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('a native smoke command is required after --')
    try:
        return run(command, args.log, args.summary, args.report, args.timeout)
    except (OSError, ValueError) as error:
        parser.exit(1, f'Native GUI smoke runner: {error}\n')


if __name__ == '__main__':
    raise SystemExit(main())
