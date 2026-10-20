#!/usr/bin/env python3
"""Fail-closed preparation and bounded evidence capture for the SDK Rev GUI smoke."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import time

MIB = 1024 * 1024
ARTIFACT_LIMIT = 600 * MIB
TRACE_LIMIT = 32 * MIB
STDERR_LIMIT = 2 * MIB
STDOUT_LIMIT = 2 * MIB

def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

def file_record(path):
    path = Path(path)
    return {'path': str(path), 'size_bytes': path.stat().st_size, 'sha256': digest(path)}

def atomic_json(path, value):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + '.partial')
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    temporary.replace(path)

def tree_size(path):
    total = 0
    for item in Path(path).rglob('*'):
        if item.is_file() and not item.is_symlink():
            total += item.stat().st_size
    return total

def overlay(args):
    base = Path(args.base).resolve()
    original = base / 'src/gui/gui_smoke.cpp'
    postimage = Path(args.postimage).resolve()
    if args.expected_preimage.startswith('ROOT_BIND_') or args.expected_postimage.startswith('ROOT_BIND_'):
        raise SystemExit('root must bind accepted gui_smoke.cpp preimage and postimage SHA256 values')
    if not original.is_file() or not postimage.is_file():
        raise SystemExit('required base source or accepted postimage file is missing')
    actual_commit = subprocess.check_output(['git', '-c', f'safe.directory={base}', '-C', str(base), 'rev-parse', 'HEAD'], text=True).strip()
    if actual_commit != args.base_commit:
        raise SystemExit(f'base source commit mismatch: got {actual_commit}')
    before = digest(original)
    proposed = digest(postimage)
    if before != args.expected_preimage:
        raise SystemExit(f'gui_smoke.cpp preimage mismatch: got {before}')
    if proposed != args.expected_postimage:
        raise SystemExit(f'gui_smoke.cpp postimage mismatch: got {proposed}')
    shutil.copyfile(postimage, original)
    after = digest(original)
    if after != args.expected_postimage:
        raise SystemExit(f'gui_smoke.cpp overlay verification failed: got {after}')
    atomic_json(args.record, {'base_commit': args.base_commit, 'path': 'src/gui/gui_smoke.cpp',
        'preimage_sha256': before, 'postimage_sha256': after, 'postimage_path': str(postimage)})

def unique_json(line):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('duplicate JSON key: ' + key)
            result[key] = value
        return result
    item = json.loads(line, object_pairs_hook=pairs,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError(value)))
    if not isinstance(item, dict):
        raise ValueError('JSONL item is not an object')
    return item

def trace_report(trace_path, stderr_path):
    result = {'valid': False}
    lines = Path(trace_path).read_text(encoding='utf-8').splitlines()
    events = [unique_json(line) for line in lines if line]
    begins = [event for event in events if event.get('event') == 'trace_begin']
    ends = [event for event in events if event.get('event') == 'trace_end']
    if len(begins) != 1 or len(ends) != 1:
        raise ValueError('trace must contain one begin and one end footer')
    begin, end = begins[0], ends[0]
    if begin.get('schema') != 2 or not isinstance(begin.get('token'), str):
        raise ValueError('trace begin schema/token invalid')
    token = begin['token']
    if end.get('token') != token or end.get('finalization_required') is not True:
        raise ValueError('trace footer token/finalization marker mismatch')
    if end.get('dropped') != 0 or end.get('size_limited') is not False or end.get('prior_io_failed') is not False:
        raise ValueError('trace footer reports dropped, limited, or failed writes')
    records = [event for event in events if event.get('event') not in ('trace_end',)]
    sequences = [event.get('sequence') for event in records]
    attempted = end.get('attempted')
    if (not all(type(sequence) is int for sequence in sequences) or
            sorted(sequences) != list(range(1, attempted + 1))):
        raise ValueError('trace record sequence does not match attempted count')
    seals = [unique_json(line) for line in Path(stderr_path).read_text(encoding='utf-8').splitlines()
             if line.startswith('{') and '"event":"trace_finalization"' in line]
    if len(seals) != 1:
        raise ValueError('missing or duplicate external trace finalization seal')
    seal = seals[0]
    if (seal.get('event') != 'trace_finalization' or seal.get('token') != token or
            Path(seal.get('path', '')).resolve() != Path(trace_path).resolve() or
            seal.get('attempted') != attempted or seal.get('dropped') != 0 or
            seal.get('size_limited') is not False or seal.get('prior_io_failed') is not False or
            seal.get('flush_ok') is not True or seal.get('close_ok') is not True or
            seal.get('result') != 'complete'):
        raise ValueError('external trace finalization seal is incomplete or mismatched')
    result.update(valid=True, token=token, records=len(records), attempted=attempted,
                  footer=end, external_finalization=seal)
    return result

def boundary_capture_report(lines):
    records = [unique_json(line) for line in lines]
    captures = [r for r in records if r.get('event') == 'gui_smoke_binary_boundary_capture']
    metrics = [r for r in records if r.get('event') == 'gui_smoke_binary_boundary_capture_emission']
    expected = {'phase20_before_binary_transmit', 'phase21_after_replay_before_help_evaluation'}
    if len(captures) != 2 or {r.get('boundary') for r in captures} != expected or len(metrics) != 1:
        raise ValueError('expected exactly two distinct binary boundary records and one emission metric')
    for item in captures:
        if item.get('capture_incomplete') is not False:
            raise ValueError('binary boundary capture is incomplete')
        for field in ('snapshot_transmission_id', 'snapshot_sequence', 'pre_tx_signal_count', 'snapshot_signal_count', 'gui_line_count'):
            if type(item.get(field)) is not int or item[field] < 0:
                raise ValueError('invalid integer boundary field: ' + field)
        for field in ('collection_start_monotonic_seconds', 'collection_end_monotonic_seconds', 'collection_wall_seconds'):
            if type(item.get(field)) not in (int, float) or not math.isfinite(item[field]) or item[field] < 0:
                raise ValueError('invalid collection timing: ' + field)
        if item['collection_end_monotonic_seconds'] < item['collection_start_monotonic_seconds']:
            raise ValueError('collection monotonic time moved backwards')
        if item.get('cpu_scope') != 'process_std_clock':
            raise ValueError('process CPU measurement scope missing')
        snapshot = item.get('snapshot')
        if not isinstance(snapshot, dict) or not isinstance(snapshot.get('transmit_trace'), dict):
            raise ValueError('snapshot/actual transmit trace missing')
        for field in ('transmitting', 'simulation_replay', 'transmission_finished', 'simulation_receiving_tail', 'transmission_failed', 'transmission_cancelled'):
            if type(snapshot.get(field)) is not bool:
                raise ValueError('invalid lifecycle field: ' + field)
        for field, count in (('snapshot_signals', 'snapshot_signal_count'), ('gui_lines', 'gui_line_count'), ('pre_tx_signal_ids', 'pre_tx_signal_count')):
            if not isinstance(item.get(field), list) or len(item[field]) > 128 or len(item[field]) != item[count]:
                raise ValueError('boundary array count/cap mismatch: ' + field)
        if not isinstance(item.get('fixture_settings'), dict):
            raise ValueError('fixture settings missing')
    if type(metrics[0].get('wall_seconds')) not in (int, float) or not math.isfinite(metrics[0]['wall_seconds']) or metrics[0]['wall_seconds'] < 0:
        raise ValueError('emission timing missing')
    return {'valid': True, 'boundaries': sorted(expected), 'records': len(captures), 'emission_metrics': len(metrics)}

def capture(args):
    archive = Path(args.archive).resolve()
    evidence = Path(args.evidence).resolve()
    if args.attempt not in (1, 2):
        raise SystemExit('attempt must be 1 or 2')
    if not archive.is_file() or archive.suffixes[-2:] != ['.tar', '.gz']:
        raise SystemExit('expected one built native TGZ archive')
    evidence.mkdir(parents=True, exist_ok=False)
    # Reserve the known 128 MiB fixture plus a bounded trace/log allowance before launch.
    used = tree_size(args.artifact_root)
    if used + 160 * MIB > ARTIFACT_LIMIT:
        raise SystemExit('artifact budget cannot reserve this smoke fixture and trace; refusing launch')
    extract = evidence.parent.parent.parent / f'.sdk-rev-smoke-package-{args.attempt}' / 'offline destination with spaces'
    extract.mkdir(parents=True, exist_ok=False)
    with tarfile.open(archive, 'r:gz') as package:
        members = package.getmembers()
        files = [m for m in members if m.isfile() and m.name.endswith('/bin/datapump-gui')]
        if len(files) != 1 or any(m.issym() or m.islnk() or m.isdev() for m in members):
            raise SystemExit('TGZ inventory lacks one regular Rev GUI or contains links/devices')
        package.extractall(extract, filter='data')
        gui = extract / files[0].name
    if not gui.is_file() or not os.access(gui, os.X_OK):
        raise SystemExit('extracted copied GUI is missing or not executable')
    smoke_dir = evidence / 'smoke-dir'
    trace = evidence / 'verbose.jsonl'
    stdout = evidence / 'stdout.log'
    stderr = evidence / 'stderr.log'
    command = ['xvfb-run', '-a', '-s', '-screen 0 2400x1800x24 -dpi 96',
        'timeout', '--signal=TERM', '--kill-after=10', '630', 'env', 'PATH=', 'PYTHONHOME=/nonexistent',
        'PYTHONPATH=/nonexistent', 'LD_LIBRARY_PATH=', str(gui), '--smoke-test',
        '--smoke-timeout', '600', '--smoke-dir', str(smoke_dir), '--verbose', '--verbose-file', str(trace)]
    env = os.environ.copy()
    env['DATAPUMP_SMOKE_BINARY_CAPTURE'] = '1'
    start = time.monotonic_ns()
    launch_error = None
    returncode = None
    try:
        with stdout.open('wb') as out, stderr.open('wb') as err:
            returncode = subprocess.run(command, env=env, stdout=out, stderr=err, check=False).returncode
    except OSError as exc:
        launch_error = f'{type(exc).__name__}: {exc}'
    end = time.monotonic_ns()
    stderr_size = stderr.stat().st_size
    trace_size = trace.stat().st_size if trace.exists() else 0
    capture_jsonl = evidence / 'capture.jsonl'
    capture_error = None
    json_lines = []
    if stderr_size > STDERR_LIMIT:
        capture_error = f'stderr exceeds {STDERR_LIMIT} byte capture bound'
    if stdout.stat().st_size > STDOUT_LIMIT:
        capture_error = f'stdout exceeds {STDOUT_LIMIT} byte capture bound'
    if capture_error is None:
        for line in stderr.read_text(encoding='utf-8', errors='replace').splitlines():
            if line.startswith('{'):
                try:
                    unique_json(line)
                    json_lines.append(line)
                except Exception as exc:
                    capture_error = f'invalid JSON stderr line: {exc}'
                    break
    if not json_lines and capture_error is None:
        capture_error = 'capture mode emitted no JSON lines to stderr'
    if json_lines:
        capture_jsonl.write_text('\n'.join(json_lines) + '\n', encoding='utf-8')
    boundary_closure = {'valid': False}
    if capture_error is None:
        try:
            boundary_closure = boundary_capture_report(json_lines)
        except Exception as exc:
            capture_error = f'binary boundary capture invalid: {exc}'
    trace_closure = {'valid': False}
    if trace.exists() and trace_size <= TRACE_LIMIT and stderr.exists():
        try:
            trace_closure = trace_report(trace, stderr)
        except Exception as exc:
            trace_closure = {'valid': False, 'error': f'{type(exc).__name__}: {exc}'}
    elif trace_size > TRACE_LIMIT:
        trace_closure = {'valid': False, 'error': f'trace exceeds {TRACE_LIMIT} byte evidence bound'}
    else:
        trace_closure = {'valid': False, 'error': 'verbose trace missing'}
    smoke_files = []
    if smoke_dir.exists():
        for item in sorted(smoke_dir.rglob('*')):
            if item.is_file() and not item.is_symlink():
                smoke_files.append(file_record(item))
    if not stdout.exists(): stdout.write_bytes(b'')
    if not stderr.exists(): stderr.write_bytes(b'')
    closure_path = evidence / 'trace-closure.json'
    atomic_json(closure_path, trace_closure)
    seal_path = evidence / 'trace-finalization.json'
    if trace_closure.get('external_finalization'):
        atomic_json(seal_path, trace_closure['external_finalization'])
    package_record = file_record(archive)
    binary_record = file_record(gui)
    shutil.rmtree(extract)
    run_record = {'attempt': args.attempt, 'base_commit': args.base_commit,
        'tool_commit': args.tool_commit, 'source_manifest': str(Path(args.source_manifest).resolve()),
        'command': command, 'exit_code': returncode, 'launch_error': launch_error, 'monotonic_start_ns': start,
        'monotonic_end_ns': end, 'monotonic_elapsed_ns': end - start,
        'environment': {'DATAPUMP_SMOKE_BINARY_CAPTURE': '1', 'LP_NUM_THREADS': os.environ.get('LP_NUM_THREADS'),
                        'LIBGL_ALWAYS_SOFTWARE': os.environ.get('LIBGL_ALWAYS_SOFTWARE')},
        'archive': package_record, 'binary': binary_record, 'settings': file_record(args.settings),
        'source_manifest_sha256': digest(args.source_manifest), 'stdout': file_record(stdout),
        'stderr': file_record(stderr), 'capture_jsonl': file_record(capture_jsonl) if capture_jsonl.exists() else None,
        'trace': file_record(trace) if trace.exists() else None, 'trace_closure': trace_closure,
        'synthetic_smoke_files': smoke_files, 'capture_error': capture_error, 'boundary_closure': boundary_closure,
        'smoke_test_is_assertion_result': True}
    atomic_json(evidence / 'run.json', run_record)
    atomic_json(evidence / 'manifest.json', {'files': [file_record(p) for p in evidence.rglob('*')
        if p.is_file() and p != evidence / 'manifest.json'], 'run': str((evidence / 'run.json').resolve())})
    used = tree_size(args.artifact_root)
    if used > ARTIFACT_LIMIT:
        raise SystemExit(f'critical evidence exceeded 600 MiB cap ({used} bytes); refusing upload')
    if launch_error:
        raise SystemExit(f'original copied-TGZ smoke could not launch: {launch_error}; evidence retained')
    if returncode != 0:
        raise SystemExit(f'original copied-TGZ smoke failed with exit {returncode}; evidence retained')
    if capture_error or not trace_closure.get('valid'):
        raise SystemExit('smoke exited successfully but required capture/trace closure is incomplete')

parser = argparse.ArgumentParser()
sub = parser.add_subparsers(dest='action', required=True)
p = sub.add_parser('overlay')
p.add_argument('--base', required=True); p.add_argument('--base-commit', required=True)
p.add_argument('--postimage', required=True); p.add_argument('--expected-preimage', required=True)
p.add_argument('--expected-postimage', required=True); p.add_argument('--record', required=True)
p.set_defaults(func=overlay)
p = sub.add_parser('capture')
p.add_argument('--archive', required=True); p.add_argument('--evidence', required=True)
p.add_argument('--artifact-root', required=True); p.add_argument('--attempt', type=int, required=True)
p.add_argument('--base-commit', required=True); p.add_argument('--tool-commit', required=True)
p.add_argument('--source-manifest', required=True); p.add_argument('--settings', required=True)
p.set_defaults(func=capture)
args = parser.parse_args()
args.func(args)
