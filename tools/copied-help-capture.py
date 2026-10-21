#!/usr/bin/env python3
"""Bounded source37 copied Help causal capture; never confer qualification."""
import argparse
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import tarfile

MIB = 1024 * 1024
BASE = '37f4315c553b9fc3807f2f173d9a0fae3f239ebc'
LIMIT = 600 * MIB
TRACE_LIMIT = 64 * MIB
CANDIDATE_LIMIT = 8 * MIB
PCM_LIMIT = 16 * MIB


def require(condition, message):
    if not condition:
        raise ValueError(message)


def helper(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), Path(__file__).with_name(name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def jsonl(path, unique):
    records = []
    with path.open(encoding='utf-8') as stream:
        for line in stream:
            require(line.endswith('\n') and len(line.encode()) <= MIB, 'partial/oversized JSONL record: ' + str(path))
            records.append(unique(line))
    return records


def candidate_closure(path, logs, unique):
    require(path.is_file() and 0 < path.stat().st_size <= CANDIDATE_LIMIT, 'candidate journal missing/empty/oversized')
    items = jsonl(path, unique)
    require(len(items) >= 3 and items[0].get('type') == 'header' and items[0].get('schema') == 1 and
            items[-1].get('type') == 'seal', 'candidate journal header/seal missing')
    records, seal = items[1:-1], items[-1]
    require(all(item.get('type') == 'candidate' for item in records), 'unexpected candidate record')
    require(seal.get('complete') is True and seal.get('write_error') is False and
            seal.get('zero_candidates_inconclusive') is False, 'candidate seal incomplete/zero')
    require(all(type(seal.get(key)) is int and seal[key] == 0 for key in
                ('dropped', 'capped_records', 'missing_publications', 'unpublished', 'missing_presentations')), 'candidate records lost/capped')
    require(all(type(seal.get(key)) is int and seal[key] == len(records) for key in
                ('candidates_seen', 'reserved', 'records')), 'candidate journal count mismatch')
    require([item.get('slot') for item in records] == list(range(len(records))), 'candidate slot sequence incomplete')
    require(sorted(item.get('ordinal', 0) for item in records) == list(range(1, len(records) + 1)),
            'candidate ordinal sequence incomplete')
    require(not any('COPIED_HELP_CANDIDATE_TRACE_ERROR' in log.read_text(encoding='utf-8', errors='replace')
                    for log in logs), 'candidate writer reported open/write/flush/close error')
    return {'valid': True, 'records': len(records), 'seal': seal}


def boundary_closure(records, shared):
    result = shared.boundary_capture_report([json.dumps(item) for item in records])
    post = next(item for item in records if item.get('boundary') == 'phase21_after_replay_before_help_evaluation')
    require(post.get('terminal_history_incomplete') is False and isinstance(post.get('terminal_history'), list),
            'terminal poll history missing/incomplete')
    history = post['terminal_history']
    require(post.get('terminal_history_records') == len(history), 'terminal poll history count mismatch')
    for item in history:
        require(item.get('event') == 'gui_smoke_binary_boundary_capture' and
                item.get('boundary') == 'phase21_terminal_delivery_poll' and item.get('capture_incomplete') is False and
                'terminal_history' not in item, 'terminal poll history contains incomplete/recursive record')
    result.update(terminal_polls=len(history), target_transmission_id=post['snapshot_transmission_id'],
                  target_transmit_trace_revision=post['snapshot']['transmit_trace'].get('revision'))
    return result


REQUIRED_CONFIG_FIELDS = ['admitted_clock_half_width_ppm', 'admitted_frequency_half_width_hz', 'allow_local_clock_fallback', 'amplitude_channel_limit', 'amplitude_epoch_stride_seconds', 'amplitude_lookahead_epochs', 'amplitude_receiver_bank_count', 'amplitude_start_offset_seconds', 'amplitude_start_uncertainty_seconds', 'amplitude_start_window', 'amplitude_strategy_workspace_bytes', 'bandwidth_hz', 'bit_limit', 'burst_random_limit', 'burst_wait_symbols', 'candidate_limit', 'carrier_hz', 'chunk_bits', 'clock_errors_ppm', 'compact_clock_search', 'complex_clock_coverage', 'complex_frequency_coverage', 'component_qualified_search', 'config_replay_supported', 'constellation_bits', 'couple_clock_to_carrier', 'data_key_present', 'differential_window_seconds', 'drift_tolerant', 'dsss', 'dsss_factor', 'early_qualified_fft', 'expand_clock_search', 'fake_hopping_present', 'false_alarm_probability', 'frequency_offsets_hz', 'frequency_rate_competition', 'frequency_search_multiplier', 'initial_stream_symbols', 'input_sample_origin', 'integration_seconds', 'memory_limit', 'oscillator_search_present', 'outer_dsss_version', 'outer_presence_guard', 'partitioned_timing_search', 'pattern_symbols', 'prefer_streamed_templates', 'pulse_shaping', 'qualified_start_window', 'receiver_bank_count', 'receiver_cpu_budget', 'receiver_schedule_key_count', 'receiver_strategy', 'receiver_strategy_workspace_bytes', 'release_idle_fft_scratch', 'restricted_direct_search', 'restricted_timing_search', 'retain_score', 'reuse_deferred_templates', 'sample_rate', 'scheduled_capture_present', 'scheduled_hopping_present', 'scramble', 'search_arithmetic', 'search_stream_phases', 'spreading_factor', 'spreading_mode', 'start_offset_seconds', 'start_uncertainty_seconds', 'stream_epoch', 'stream_phase_samples', 'track_limit', 'training_seconds', 'uncoupled_frequency_count', 'worker_threads']


def target_inventory(events, feeds, banks, records):
    post = next(item for item in records if item.get('boundary') == 'phase21_after_replay_before_help_evaluation')
    tx = post['snapshot_transmission_id']
    require(type(tx) is int and tx > 0, 'target TX identity unavailable')
    def identity(item):
        return tuple(item.get(key) for key in ('session', 'bank', 'generation', 'revision'))
    contexts = [item for item in events if item.get('event') == 'source_context' and item.get('transmission_id') == tx]
    matches = {identity(item) for item in contexts}
    require(len(matches) == 1, 'actual TX has missing/ambiguous PCM source bank')
    target = next(iter(matches))
    require(all(type(value) is int and value > 0 for value in target), 'target source identity incomplete')
    rows = [item for item in banks if identity(item) == target]
    require(len(rows) == 1 and all(rows[0].get(key) is True for key in
            ('continuous_sample_zero_prefix', 'config_complete', 'context_seen')), 'target prefix/config/context incomplete')
    selected = [item for item in feeds if identity(item) == target]
    require(selected, 'target feeds absent')
    end = 0
    for item in selected:
        require(item['status'] == 1 and item['first'] == end, 'target has uncommitted/gapped PCM attempts')
        end = item['end']
    require(end > 0 and rows[0].get('committed_end') == end, 'target committed end mismatched')
    scope = [item for item in events if identity(item) == target]
    constructors = {}
    for item in scope:
        if item.get('event') == 'receiver_constructed':
            rid = item.get('receiver_id')
            constructors.setdefault(rid, []).append(item)
    needed = {}
    for item in scope:
        if item.get('event') == 'receiver_admitted':
            needed.setdefault(item.get('receiver_id'), []).append(item.get('call'))
        if item.get('event') == 'feed_attempt':
            for rid in item.get('resident_receiver_ids_before_refresh', []):
                needed.setdefault(rid, []).append(item.get('ordinal'))
    require(needed, 'target has no admitted/resident receiver inventory')
    for rid, calls in needed.items():
        found = constructors.get(rid, [])
        require(len(found) == 1 and all(type(call) is int and found[0].get('call', 0) < call for call in calls),
                'target resident/admitted receiver lacks one prior constructor')
        item = found[0]
        require(all(key in item for key in REQUIRED_CONFIG_FIELDS), 'target constructor Config/PatternSearch fields missing')
        require(item.get('config_replay_supported') is True and item.get('scheduled_capture_present') is False and
                item.get('fake_hopping_present') is False and item.get('scheduled_hopping_present') is False,
                'target constructor configuration unsupported')
        require('seed_derivation' in item if item.get('data_key_present') else
                'spreading_seed' in item and 'dsss_seed' in item, 'target seed reconstruction inputs missing')
        require(type(item.get('actual_constructor_grant')) is int and item['actual_constructor_grant'] > 0 and
                isinstance(item.get('key_tag_hmac_dp_rx_bank'), str), 'target constructor grant/key identity missing')
    attempts = [item for item in scope if item.get('event') == 'feed_attempt']
    require(len(attempts) == len(selected), 'target feed-attempt event inventory mismatch')
    for feed in selected:
        matches = [item for item in attempts if item.get('ordinal') == feed['ordinal']]
        require(len(matches) == 1 and all(matches[0].get(key) == feed[key] for key in
                ('first', 'count', 'pcm_offset')), 'target feed descriptor/event mismatch')
    return {'valid': True, 'transmission_id': tx, 'session_bank_generation_revision': target,
            'committed_samples': end, 'receiver_constructors_checked': len(needed)}


def pcm_closure(directory, records, shared):
    files = {name: directory / name for name in ('input.f32', 'feeds.jsonl', 'events.jsonl', 'seal.json')}
    require(directory.is_dir() and set(p.name for p in directory.iterdir()) == set(files), 'PCM inventory incomplete/unexpected')
    require(all(p.is_file() and not p.is_symlink() for p in files.values()), 'PCM inventory contains nonregular files')
    seal = shared.unique_json(files['seal.json'].read_text())
    require(seal.get('schema') == 1, 'PCM seal schema missing')
    require(all(seal.get(key) is True for key in ('writers_closed', 'data_files_ok',
                                                'float_little_endian', 'float_iec559')), 'PCM writers/data/format incomplete')
    require(all(seal.get(key) is False for key in ('sample_overflow', 'descriptor_overflow', 'metadata_overflow',
              'identity_overflow')), 'PCM overflow/config/input incomplete')
    require(seal.get('float_bytes') == 4 and type(seal.get('samples')) is int and
            0 < seal['samples'] <= 1048576 and files['input.f32'].stat().st_size == seal['samples'] * 4,
            'PCM byte/sample count mismatch')
    for name, key in (('input.f32', 'pcm_sha256'), ('events.jsonl', 'events_sha256'), ('feeds.jsonl', 'feeds_sha256')):
        require(shared.digest(files[name]) == seal.get(key), 'PCM file hash mismatch: ' + name)
    feeds = jsonl(files['feeds.jsonl'], shared.unique_json)
    events = jsonl(files['events.jsonl'], shared.unique_json)
    require(feeds and len(feeds) == seal.get('attempts') and len(feeds) <= 8192 and
            len(events) == seal.get('records') and files['events.jsonl'].stat().st_size == seal.get('metadata_bytes'),
            'PCM descriptor/event count mismatch')
    offset, ends = 0, {}
    for feed in feeds:
        require(all(type(feed.get(key)) is int and feed[key] >= 0 for key in
                    ('ordinal', 'session', 'bank', 'generation', 'revision', 'first', 'count', 'pcm_offset', 'end', 'final_call', 'status')),
                'invalid PCM descriptor integer')
        bank = tuple(feed[key] for key in ('session', 'bank', 'generation', 'revision'))
        require(feed['status'] in (1, 2) and feed['final_call'] > feed['ordinal'] and feed['pcm_offset'] == offset and
                (feed['status'] != 1 or feed['end'] == feed['first'] + feed['count']),
                'PCM invalid attempt descriptor or partition mismatch')
        offset += feed['count']
        ends[bank] = feed['end']
    require(offset == seal['samples'], 'PCM descriptors do not cover retained samples')
    banks = seal.get('banks')
    require(isinstance(banks, list) and banks and len(banks) == len(ends), 'PCM bank inventory mismatch')
    for bank in banks:
        identity = tuple(bank.get(key) for key in ('session', 'bank', 'generation', 'revision'))
        require(identity in ends, 'PCM bank missing from descriptor inventory')
    final = [item for item in records if item.get('event') == 'copied_help_pcm_finalization']
    require(len(final) == 1 and final[0].get('complete') is seal.get('complete') and final[0].get('seal_close_ok') is True,
            'PCM external close seal incomplete')
    target = target_inventory(events, feeds, banks, records)
    return {'valid': True, 'seal': seal, 'feeds': len(feeds), 'events': len(events),
            'causal_complete': True, 'target_binding': target,
            'limits': 'complete retained target input/config inventory; full Session settings and projected complex bytes are not captured; replay/causal attribution and C++ runtime remain unverified'}


def extract_archive(archive, destination, shared, evidence):
    require(archive.is_file() and archive.name.endswith('.tar.gz') and archive.stat().st_size <= 80 * MIB,
            'expected one bounded original portable Rev TGZ')
    destination.mkdir(parents=True, exist_ok=False)
    with tarfile.open(archive, 'r:gz') as package:
        members = package.getmembers()
        require(0 < len(members) <= 4096, 'TGZ member count exceeds bound')
        names, total = set(), 0
        for item in members:
            path = PurePosixPath(item.name)
            require(item.name not in names and '\\' not in item.name and not path.is_absolute() and
                    all(part not in ('', '.', '..') for part in item.name.rstrip('/').split('/')) and
                    (item.isfile() or item.isdir()), 'unsafe/duplicate/nonregular TGZ member')
            names.add(item.name)
            total += item.size
            require(0 <= item.size <= 256 * MIB and total <= 512 * MIB, 'TGZ extraction exceeds bound')
        gui_members = [item for item in members if item.isfile() and item.name.endswith('/bin/datapump-gui')]
        require(len(gui_members) == 1, 'TGZ must contain one GUI')
        package.extractall(destination, filter='data')
    gui = destination / gui_members[0].name
    require(gui.is_file() and os.access(gui, os.X_OK), 'copied GUI is not executable')
    root = gui.parent.parent
    manifest = root / 'manifest.sha256'
    require(manifest.is_file() and manifest.stat().st_size <= MIB, 'embedded package manifest missing/oversized')
    require(manifest.read_text().splitlines().count(shared.digest(gui) + '  bin/datapump-gui') == 1,
            'embedded package manifest does not bind copied ELF')
    shutil.copyfile(manifest, evidence / 'package-manifest.sha256')
    shared.atomic_json(evidence / 'package-inventory.json', [{'name': item.name, 'size_bytes': item.size} for item in members])
    return gui


def capture(args):
    shared, native = helper('sdk-rev-smoke-capture'), helper('native-copy-smoke-capture')
    evidence, artifact = Path(args.evidence).resolve(), Path(args.artifact_root).resolve()
    scratch, archive = Path(args.scratch).resolve(), Path(args.archive).resolve()
    require(args.base_commit == BASE and args.attempt in (1, 2), 'only source37/two independent captures are supported')
    require(artifact in evidence.parents and scratch != artifact and artifact not in scratch.parents and
            scratch not in artifact.parents, 'evidence must be under artifact root and scratch disjoint')
    evidence.mkdir(parents=True, exist_ok=False)
    report = {'purpose': 'enriched C failure capture; no qualification or timing comparison',
              'base_commit': args.base_commit, 'tool_commit': args.tool_commit, 'attempt': args.attempt,
              'status': 'capture_incomplete', 'capture_complete': False, 'original_exit': None,
              'smoke_timeout_seconds': 600, 'artifact_limit_bytes': LIMIT,
              'target_binding_complete': False}
    try:
        require(native.bounded_tree(artifact) + 220 * MIB < LIMIT, 'cannot reserve complete fixture/probe/trace/log evidence')
        report['source_manifest'] = shared.file_record(args.source_manifest)
        report['tooling'] = [shared.file_record(Path(__file__)), shared.file_record(Path(native.__file__)), shared.file_record(Path(shared.__file__))]
        gui = extract_archive(archive, scratch, shared, evidence)
        report.update(archive=shared.file_record(archive), binary=shared.file_record(gui))
        smoke, trace = evidence / 'smoke-dir', evidence / 'verbose.jsonl'
        candidate, pcm = evidence / 'candidate.jsonl', evidence / 'pcm'
        require(not candidate.exists() and not pcm.exists(), 'probe outputs must be new')
        def watch():
            require(native.bounded_tree(artifact) <= LIMIT - 2 * MIB, 'evidence reached artifact bound')
            require(not trace.exists() or trace.stat().st_size <= TRACE_LIMIT, 'trace exceeds 64 MiB bound')
            require(not candidate.exists() or candidate.stat().st_size <= CANDIDATE_LIMIT, 'candidate journal exceeds bound')
            require(not pcm.exists() or native.bounded_tree(pcm) <= PCM_LIMIT, 'PCM evidence exceeds bound')
            if smoke.exists():
                paths = list(smoke.iterdir())
                require(len(paths) <= 8 and all(p.is_file() and not p.is_symlink() and p.stat().st_size <= native.KEY_BYTES for p in paths)
                        and sum(p.stat().st_size for p in paths) <= native.FIXTURE_LIMIT, 'synthetic fixture exceeds bound')
        env = {'DATAPUMP_SMOKE_BINARY_CAPTURE': '1', 'DATAPUMP_COPIED_HELP_CANDIDATE_TRACE': str(candidate),
               'DATAPUMP_COPIED_HELP_PCM_TRACE': str(pcm)}
        command = ['xvfb-run', '-a', '-s', '-screen 0 2400x1800x24 -dpi 96', 'env',
                   'PATH=', 'PYTHONHOME=/nonexistent', 'PYTHONPATH=/nonexistent', 'LD_LIBRARY_PATH=']
        command += [key + '=' + value for key, value in env.items()]
        command += [str(gui), '--smoke-test', '--smoke-timeout', '600', '--smoke-dir', str(smoke), '--verbose', '--verbose-file', str(trace)]
        report['environment'] = dict(env, **{key: os.environ.get(key) for key in ('LP_NUM_THREADS', 'LIBGL_ALWAYS_SOFTWARE')})
        result, stdout, stderr = native.run(command, evidence, 'smoke', 630, watch)
        report['smoke'] = result
        report['original_exit'] = result['exit_code']
        require(result['process_group_closed'], 'writers still live: cannot seal capture')
        records = []
        for log in (stdout, stderr):
            for line in log.read_text(encoding='utf-8', errors='strict').splitlines():
                if line.startswith('{'):
                    records.append(shared.unique_json(line))
        capture_jsonl = evidence / 'capture.jsonl'
        capture_jsonl.write_text(''.join(json.dumps(item, separators=(',', ':')) + '\n' for item in records))
        for name, callback in (
                ('boundary_closure', lambda: boundary_closure(records, shared)),
                ('trace_closure', lambda: native.trace_closure(trace, (stdout, stderr), shared.unique_json)),
                ('fixtures', lambda: native.fixtures(smoke)),
                ('candidate_closure', lambda: candidate_closure(candidate, (stdout, stderr), shared.unique_json)),
                ('pcm_closure', lambda: pcm_closure(pcm, records, shared))):
            try:
                report[name] = callback()
            except (OSError, ValueError, KeyError, TypeError) as exc:
                report[name] = {'valid': False, 'error': type(exc).__name__ + ': ' + str(exc)}
        if report['pcm_closure'].get('valid') and report['candidate_closure'].get('valid'):
            target = report['pcm_closure']['target_binding']['session_bank_generation_revision']
            candidates = [item for item in jsonl(candidate, shared.unique_json) if item.get('type') == 'candidate' and
                          item.get('generation') == target[2] and item.get('input_revision') == target[3] and
                          item.get('bank_identity') == hex(target[1])]
            if not candidates:
                report['candidate_closure'] = {'valid': False, 'error': 'zero candidates for actual target bank/generation/revision'}
            else:
                report['candidate_closure']['target_records'] = len(candidates)
        watch()
        report['retention_complete'] = result['capture_error'] is None and all(report[name]['valid'] for name in
                ('boundary_closure', 'trace_closure', 'fixtures', 'candidate_closure', 'pcm_closure'))
        report['target_binding_complete'] = report['pcm_closure'].get('causal_complete') is True
        report['capture_complete'] = report['retention_complete'] and report['target_binding_complete']
        report['status'] = ('captured_assertion_pass' if result['exit_code'] == 0 else 'captured_assertion_failure') if report['capture_complete'] else 'capture_incomplete'
        require(result['exit_code'] == 0, 'original copied GUI exit remains fatal: ' + str(result['exit_code']))
        require(report['retention_complete'], 'required capture missing/zero/partial; no retention gate pass')
    except Exception as exc:
        report['error'] = type(exc).__name__ + ': ' + str(exc)
        raise
    finally:
        # run() closes the complete process group before returning/raising. Keep
        # package, all synthetic fixtures, raw logs and partial probes on failure.
        shared.atomic_json(evidence / 'run.json', report)
        process_path = evidence / 'smoke.process.json'
        closed = not process_path.exists() or json.loads(process_path.read_text()).get('process_group_closed') is True
        if closed:
            shared.atomic_json(evidence / 'manifest.json', {'files': [shared.file_record(p) for p in sorted(evidence.rglob('*'))
                 if p.is_file() and not p.is_symlink() and p != evidence / 'manifest.json'],
                 'capture_complete': report['capture_complete'], 'target_binding_complete': report['target_binding_complete'], 'qualification': False})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('archive', 'evidence', 'artifact-root', 'scratch', 'base-commit', 'tool-commit', 'source-manifest'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--attempt', type=int, required=True)
    capture(parser.parse_args())


if __name__ == '__main__':
    main()
