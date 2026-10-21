#!/usr/bin/env python3
"""Matched original/probed Release drain captures; never qualification."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def compatible_sha(path):
    """The matched Ubuntu 22.04 container provides Python 3.10."""
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--report', required=True, type=Path)
    args = parser.parse_args()
    root, report = args.root.resolve(), args.report.resolve()
    tool = Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location('capture', root / 'tools/native-copy-smoke-capture.py')
    capture = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(capture)
    # Keep the existing capture/closure implementation; adapt only its digest
    # entry point for the original runner's Python, without changing that tool.
    capture.sha = compatible_sha
    manifest = json.loads((tool / 'manifest.json').read_text())
    git = ['git', '-c', 'safe.directory=' + str(root), '-C', str(root)]
    def git_read(*arguments):
        return subprocess.check_output(git + list(arguments), text=True).strip()
    capture.require(report.parent == root / 'build/symbol-burst-live-qualification', 'unexpected report parent')
    report.mkdir(parents=True)
    capture.require(not any(report.iterdir()), 'report must be new and empty')
    for path in ['src', 'include', 'tests', 'cmake', 'CMakeLists.txt']:
        capture.require(git_read('rev-parse', 'HEAD:' + path) ==
                        git_read('rev-parse', manifest['application_source'] + ':' + path),
                        'compiled application inputs differ from pinned source: ' + path)
    capture.require(git_read('diff', '--name-only', '--', 'src', 'include', 'tests', 'cmake', 'CMakeLists.txt') == '',
                    'application must begin unchanged')
    cache = (root / 'build/CMakeCache.txt').read_text()
    capture.require('CMAKE_BUILD_TYPE:STRING=Release' in cache and 'DATAPUMP_SANITIZERS:BOOL=OFF' in cache,
                    'this wave requires original uninstrumented Release configuration')
    originals = {}
    for entry in manifest['files']:
        path = root / entry['path']
        capture.require(capture.sha(path) == entry['preimage_sha256'], 'source preimage mismatch')
        capture.require(capture.sha(tool / entry['payload']) == entry['postimage_sha256'], 'probe postimage mismatch')
        originals[entry['path']] = path.read_bytes()
    capture.save(report / 'plan.json', {
        'classification': manifest['classification'], 'tool_source': git_read('rev-parse', 'HEAD'),
        'application_source': manifest['application_source'], 'runtime_source': manifest['runtime_source'],
        'run_id': os.environ.get('GITHUB_RUN_ID'), 'job': os.environ.get('GITHUB_JOB'),
        'fixture': 'original no-argument test_symbol_burst_live; unchanged 25-second per-case functional budget',
        'order': ['original', 'control', 'probe', 'probe', 'control'], 'cpus': 2, 'nice': 10,
        'expected_minutes': '10-20 including native build; run phase 2-5',
        'decision_rule': {
            'cpu': 'continuous worker CPU plus advancing frontier: locate expensive gate/cohort work',
            'waiting': 'frontier stalled without worker CPU: inspect wait/scheduling boundary',
            'cohort': 'resume ingress/admission sample divergence predicts extra work: inspect that boundary',
            'unaccounted': 'no faithful reproduction or inconsistent trace: do not patch; improve capture'},
        'probe_manifest': manifest})
    parent = report.parent
    (parent / 'source.txt').write_text(git_read('rev-parse', 'HEAD') + '\n')
    (parent / 'source-tree.txt').write_text(git_read('rev-parse', 'HEAD^{tree}') + '\n')
    shutil.copyfile(root / 'build/CMakeCache.txt', parent / 'CMakeCache.txt')
    def inventory():
        names = subprocess.check_output(git + ['ls-files', '-z', 'src', 'include', 'tests', 'cmake', 'CMakeLists.txt']).decode().split('\0')
        capture.save(parent / 'source-inventory.json', {name: capture.record(root / name) for name in names if name})
    def watch():
        mem = next(int(line.split()[1]) * 1024 for line in Path('/proc/meminfo').read_text().splitlines()
                   if line.startswith('MemAvailable:'))
        capture.require(mem >= 1536 * 1024 ** 2, 'memory below 1.5GiB stop guard')
        capture.require(capture.bounded_tree(report) <= 768 * 1024 ** 2, 'capture exceeds 768MiB')
        for path in report.glob('trial-*/receiver.trace.jsonl'):
            capture.require(path.stat().st_size <= 16 * 1024 ** 2, 'trace exceeds 16MiB bound')
    def retain(label):
        inventory()
        result, _, _ = capture.run([sys.executable, str(root / 'tools/retain-paced-diagnostic.py'),
                                   '--root', str(root), '--report', str(report / label),
                                   '--runtime-probe', 'off'], report, 'retain-' + label, 180, watch=watch)
        capture.require(capture.successful(result), 'ELF provenance retention failed')
        return report / label / 'test_symbol_burst_live'
    trials = []
    all_passed = False
    try:
        original = retain('original-reproducer')
        for entry in manifest['files']:
            (root / entry['path']).write_bytes((tool / entry['payload']).read_bytes())
        result, _, _ = capture.run(['cmake', '--build', 'build', '--target', 'test_symbol_burst_live', '--parallel', '2'],
                                   report, 'probe-build', 1200, watch=watch)
        capture.require(capture.successful(result), 'diagnostic overlay compilation failed')
        probe = retain('probe-reproducer')
        for number, variant in enumerate(['original', 'control', 'probe', 'probe', 'control'], 1):
            binary = original if variant == 'original' else probe
            label = 'trial-' + str(number) + '-' + variant
            command = [sys.executable, str(tool / 'observe.py'), str(binary), str(report / label)]
            if variant != 'original':
                command += ['--input-capture']
            if variant == 'probe':
                command += ['--probe']
            command += ['original']
            result, _, stderr = capture.run(command, report, label, 600, watch=watch)
            observation = report / label / 'observation.json'
            row = {'label': label, 'variant': variant, 'binary_sha256': capture.sha(binary), **result}
            row['observation'] = json.loads(observation.read_text()) if observation.is_file() else None
            row['inputs'] = [capture.record(p) for p in sorted((report / label).glob('input-*.f32le'))]
            if variant == 'probe':
                traces = list((report / label).glob('receiver.trace.jsonl*'))
                row['traces'] = [capture.record(p) for p in sorted(traces)]
                trace = [json.loads(line) for line in (report / label / 'receiver.trace.jsonl').read_text().splitlines()]
                begin, end = trace[0], trace[-1]
                seals = [json.loads(line) for line in stderr.read_text().splitlines()
                         if line.startswith('{') and '"event":"trace_finalization"' in line]
                row['trace_complete'] = (begin['event'] == 'trace_begin' and end['event'] == 'trace_end' and
                    len(seals) == 1 and seals[0]['token'] == begin['token'] == end['token'] and
                    seals[0]['result'] == 'complete' and end['dropped'] == 0 and not end['size_limited'] and
                    end['attempted'] == len(trace) - 1 and
                    sorted(record['sequence'] for record in trace[:-1]) == list(range(1, len(trace))))
                markers = [json.loads(line.removeprefix('LIVE_DRAIN_PROBE '))
                           for line in stderr.read_text().splitlines() if line.startswith('LIVE_DRAIN_PROBE ')]
                entries = sum(record['event'] == 'hardware_case_entry' for record in markers)
                terminals = [record for record in markers if record['event'] == 'terminal']
                row['marker_stream_closed'] = entries > 0 and len(terminals) == entries and all(
                    record['dropped'] == 0 for record in terminals)
                row['case_terminals'] = terminals
            trials.append(row)
            capture.save(report / 'results.json', trials)
            print(json.dumps({'label': label, 'exit_code': result['exit_code'],
                              'wall_seconds': row['observation'] and row['observation']['wall_seconds']}), flush=True)
            capture.require(result['capture_error'] is None and result['process_group_closed'], 'capture/closure failed')
            capture.require(row['observation'] and row['observation']['observation_error'] is None, 'observer setup failed')
            if variant == 'probe':
                capture.require(row['trace_complete'] and row['marker_stream_closed'], 'incomplete diagnostic evidence')
            if result['exit_code'] != 0:
                capture.require('capture did not drain' in stderr.read_text(errors='replace'),
                                'unexpected assertion/setup failure: preserve and stop wave')
        input_hashes = {}
        for row in trials:
            if row['variant'] == 'original':
                continue
            for record in row['inputs']:
                name = Path(record['path']).name
                if name in input_hashes:
                    capture.require(input_hashes[name] == record['sha256'], 'matched generated PCM differs: ' + name)
                input_hashes[name] = record['sha256']
        capture.save(report / 'matched-inputs.json', {'generated_pcm_sha256': input_hashes,
            'all_observed_control_probe_inputs_equal': True,
            'original_unpatched_input_not_directly_captured': True,
            'control_probe_use_the_same_elf': True})
        all_passed = all(capture.successful(row) for row in trials)
    finally:
        for name, content in originals.items():
            (root / name).write_bytes(content)
        restored = all(capture.sha(root / entry['path']) == entry['preimage_sha256'] for entry in manifest['files'])
        capture.save(report / 'restoration.json', {'preimages_restored': restored,
                                                 'application_diff': git_read('diff', '--name-only', '--', 'src', 'include', 'tests'),
                                                 'probe_elf_is_diagnostic_only': True})
        capture.require(restored, 'source restoration failed')
    files = [capture.record(path) for path in sorted(report.rglob('*')) if path.is_file()]
    capture.save(report / 'capture-manifest.json', {'files': files, 'trials': len(trials),
                                                  'all_original_assertions_passed': all_passed,
                                                  'qualification': False})
    return 0 if all_passed else 1


if __name__ == '__main__':
    sys.exit(main())
