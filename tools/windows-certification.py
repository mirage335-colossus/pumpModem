#!/usr/bin/env python3
"""Recognize only the hosted Windows Rev WGL limitation; retain all other errors."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


WGL_MESSAGE = '[NativeWindow] Required WGL ARB extensions not available'
WGL_CODE = 'windows-rev-wgl-unavailable'
WGL_IMPACT = ('The runner cannot create the Rev OpenGL context. Native Windows Rev '
              'GUI tests and published GUI smoke are untested; compilation, headless '
              'GUI/CLI, modem, archive integrity and relocation checks remain mandatory. '
              'A Windows machine with the same graphics limitation cannot open the Rev GUI.')


def warning_record():
    return {'code': WGL_CODE, 'target': 'windows-x86_64-rev',
            'omitted_checks': ['source:gui_workflow', 'source:gui_adapter_conformance',
                               'source:gui_coordinates_1x', 'source:gui_coordinates_2x',
                               'published:gui_smoke'],
            'probe': {'test': 'gui_coordinates_1x', 'exit_code': 1, 'output': WGL_MESSAGE},
            'message': WGL_IMPACT}


def classify(returncode, output, platform):
    if returncode == 0:
        return []
    # An assertion, crash, timeout, extra diagnostic or another platform must
    # never turn green merely because the known text appears somewhere in it.
    if platform == 'win32' and returncode == 1 and output.strip() == WGL_MESSAGE:
        return [warning_record()]
    raise ValueError(f'Rev coordinate probe failed (exit {returncode}): {output.strip()}')


def probe(executable, github_output=None, summary=None):
    if sys.platform != 'win32':
        raise ValueError('The WGL exception is restricted to Windows certification')
    result = subprocess.run([str(executable.resolve(strict=True)), '1'],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            encoding='utf-8', errors='replace', timeout=90, check=False)
    print(result.stdout, end='')
    warnings = classify(result.returncode, result.stdout, sys.platform)
    values = {'gui_smoke': 'OFF' if warnings else 'ON',
              'warnings': json.dumps(warnings, separators=(',', ':'))}
    if github_output:
        with github_output.open('a', encoding='utf-8') as stream:
            for key, value in values.items():
                stream.write(f'{key}={value}\n')
    if warnings:
        print(f'::warning title=Windows Rev graphics coverage unavailable::{WGL_IMPACT}')
        if summary:
            with summary.open('a', encoding='utf-8') as stream:
                stream.write('## Windows Rev graphics warning\n\n' + WGL_IMPACT + '\n\n'
                             'A green job does not qualify Windows Rev desktop graphics.\n')
    return warnings


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--github-output', type=Path,
                        default=os.environ.get('GITHUB_OUTPUT'))
    parser.add_argument('--summary', type=Path,
                        default=os.environ.get('GITHUB_STEP_SUMMARY'))
    args = parser.parse_args(argv)
    try:
        probe(args.executable, args.github_output, args.summary)
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        parser.exit(1, f'Windows certification: {error}\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
