#!/usr/bin/env python3
"""Capture one unchanged, retained FLTK ZIP smoke; never confer qualification.

The caller checkout supplies tooling only. The application is extracted from the
pinned original producer artifact, with no rebuild, overlay, or GUI test changes.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import selectors
import signal
import stat
import subprocess
import time
import zipfile

MIB = 1024 * 1024
ARTIFACT_LIMIT = 600 * MIB
TRACE_LIMIT = 128 * MIB
LOG_LIMIT = 2 * MIB
FIXTURE_LIMIT = 150 * MIB
KEY_BYTES = 134_218_123
EXTRACT_LIMIT = 512 * MIB


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def record(path):
    path = Path(path)
    return {'path': str(path), 'size_bytes': path.stat().st_size, 'sha256': sha(path)}


def save(path, value):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + '.partial')
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    temporary.replace(path)


def bounded_tree(root):
    total = 0
    for path in root.rglob('*'):
        require(not path.is_symlink(), 'evidence contains a symlink: ' + str(path))
        if path.is_file():
            total += path.stat().st_size
    return total


def group_members(group):
    members = []
    for path in Path('/proc').glob('[0-9]*/stat'):
        try:
            value = path.read_text()
            fields = value.rsplit(')', 1)[1].split()
            if int(fields[2]) == group and fields[0] != 'Z':
                members.append({'pid': int(path.parent.name), 'state': fields[0],
                                'name': value.split('(', 1)[1].rsplit(')', 1)[0][:80]})
        except (OSError, ValueError, IndexError):
            continue
    return members


def group_alive(group):
    return bool(group_members(group))


def signal_group(group, value):
    try:
        os.killpg(group, value)
    except ProcessLookupError:
        pass


def run(command, directory, label, seconds, watch=None, stdout_limit=LOG_LIMIT):
    """Bound both streams while running; TERM then KILL the whole new session.

    Smoke uses the existing 630-second outer TERM and 10-second KILL grace.
    Evidence-limit failures stop capture and remain fatal, never a smoke pass.
    """
    out_path, err_path = directory / (label + '.stdout.log'), directory / (label + '.stderr.log')
    result = {'command': command, 'term_after_seconds': seconds, 'kill_grace_seconds': 10,
              'started_monotonic_ns': time.monotonic_ns(), 'exit_code': None, 'capture_error': None,
              'writer_exit_grace_seconds': 2}
    process = None
    try:
        with out_path.open('xb') as out, err_path.open('xb') as err:
            process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, start_new_session=True)
            result['pid'] = process.pid
            streams = selectors.DefaultSelector()
            streams.register(process.stdout, selectors.EVENT_READ, (out, stdout_limit))
            streams.register(process.stderr, selectors.EVENT_READ, (err, LOG_LIMIT))
            counts = {out: 0, err: 0}
            started = time.monotonic()
            terminated = None
            launcher_exited = None
            while streams.get_map() or process.poll() is None or group_alive(process.pid):
                now = time.monotonic()
                if terminated is None:
                    try:
                        if watch:
                            watch()
                        require(now - started < seconds, 'outer process deadline reached')
                    except (OSError, ValueError) as exc:
                        result['capture_error'] = str(exc)
                        terminated = now
                        signal_group(process.pid, signal.SIGTERM)
                if terminated is not None and now - terminated >= 10:
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                if terminated is not None and now - terminated >= 12:
                    result['capture_error'] = result['capture_error'] or 'writers did not close after KILL grace'
                    for key in list(streams.get_map().values()):
                        streams.unregister(key.fileobj)
                        key.fileobj.close()
                    break
                for key, _ in streams.select(0.1):
                    block = os.read(key.fileobj.fileno(), 64 * 1024)
                    target, limit = key.data
                    if not block:
                        streams.unregister(key.fileobj)
                        key.fileobj.close()
                        continue
                    remaining = max(0, limit - counts[target])
                    target.write(block[:remaining])
                    counts[target] += len(block)
                    if counts[target] > limit and terminated is None:
                        result['capture_error'] = 'bounded output exceeded ' + str(limit) + ' bytes'
                        terminated = now
                        signal_group(process.pid, signal.SIGTERM)
                # Launchers such as xvfb-run can return while their server is
                # completing normal shutdown. Observe a bounded exit grace;
                # any writer still alive afterwards remains a fatal orphan.
                if process.poll() is not None and terminated is None and group_alive(process.pid):
                    if launcher_exited is None:
                        launcher_exited = time.monotonic()
                        result['launcher_exit_seen_monotonic_ns'] = time.monotonic_ns()
                        result['post_exit_members'] = group_members(process.pid)[:16]
                    if time.monotonic() - launcher_exited >= 2:
                        result['capture_error'] = 'launcher exited with surviving process-group members after bounded grace'
                        terminated = time.monotonic()
                        signal_group(process.pid, signal.SIGTERM)
            result['exit_code'] = process.wait(timeout=2)
            streams.close()
    finally:
        if process is not None:
            if group_alive(process.pid):
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                deadline = time.monotonic() + 2
                while group_alive(process.pid) and time.monotonic() < deadline:
                    time.sleep(0.02)
            result['process_group_closed'] = not group_alive(process.pid)
            if not result['process_group_closed']:
                result['capture_error'] = 'process group did not close; capture incomplete'
        else:
            result['process_group_closed'] = True
        result['ended_monotonic_ns'] = time.monotonic_ns()
        save(directory / (label + '.process.json'), result)
    return result, out_path, err_path


def successful(result):
    return result['exit_code'] == 0 and result['capture_error'] is None and result['process_group_closed']


def api(endpoint, evidence, label, limit=1 * MIB):
    result, output, _ = run(['gh', 'api', endpoint], evidence, label, 180, stdout_limit=limit)
    require(successful(result), 'GitHub API request failed: ' + label)
    return output


def zip_inventory(package):
    result = []
    names = set()
    total = 0
    for info in package.infolist():
        name = info.filename
        path = PurePosixPath(name)
        require(name and '\\' not in name and not path.is_absolute() and
                not any(part in ('..', '.', '') for part in name.rstrip('/').split('/')) and
                not re.match(r'^[A-Za-z]:', name) and '\x00' not in name,
                'unsafe ZIP path: ' + repr(name))
        require(name not in names, 'duplicate ZIP path: ' + name)
        names.add(name)
        mode = info.external_attr >> 16
        kind = stat.S_IFMT(mode)
        require(kind in (0, stat.S_IFREG, stat.S_IFDIR) and not (info.flag_bits & 1),
                'ZIP contains a link, special file, or encrypted entry: ' + name)
        require(info.file_size <= 256 * MIB, 'ZIP member exceeds size bound')
        total += info.file_size
        require(total <= EXTRACT_LIMIT and len(names) <= 4096, 'ZIP inventory exceeds extraction bound')
        result.append({'name': name, 'size_bytes': info.file_size, 'crc32': info.CRC,
                       'compressed_bytes': info.compress_size, 'external_attr': info.external_attr})
    require(package.testzip() is None, 'ZIP CRC validation failed')
    return sorted(result, key=lambda item: item['name'])


def prepare(pins, evidence, scratch):
    repo = os.environ['GITHUB_REPOSITORY']
    base = 'repos/' + repo + '/actions/'
    artifact_path = api(base + 'artifacts/' + str(pins['artifact_id']), evidence, 'artifact-api')
    artifact = json.loads(artifact_path.read_text())
    require(artifact['id'] == pins['artifact_id'] and artifact['name'] == pins['artifact_name'] and
            artifact['size_in_bytes'] == pins['artifact_size_bytes'] and artifact['expired'] is False and
            artifact['digest'] == 'sha256:' + pins['artifact_sha256'] and
            artifact['workflow_run']['id'] == pins['run_id'] and
            artifact['workflow_run']['head_sha'] == pins['source_commit'], 'artifact API identity mismatch')
    run_data = json.loads(api(base + 'runs/' + str(pins['run_id']), evidence, 'producer-run-api').read_text())
    # The producer passed, while this full run contains the copied-smoke failure.
    require(run_data['id'] == pins['run_id'] and run_data['head_sha'] == pins['source_commit'] and
            run_data['path'] == pins['workflow_path'],
            'producer workflow identity mismatch')
    job = json.loads(api(base + 'jobs/' + str(pins['producer_job_id']), evidence, 'producer-job-api').read_text())
    require(job['run_id'] == pins['run_id'] and job['head_sha'] == pins['source_commit'] and
            job['conclusion'] == 'success' and job['status'] == 'completed', 'producer job did not pass')
    outer = api(base + 'artifacts/' + str(pins['artifact_id']) + '/zip', scratch, 'retained-artifact',
                pins['artifact_size_bytes'])
    require(outer.stat().st_size == pins['artifact_size_bytes'] and sha(outer) == pins['artifact_sha256'],
            'downloaded artifact digest/size mismatch')
    products = evidence / 'products'
    products.mkdir()
    with zipfile.ZipFile(outer) as package:
        inventory = zip_inventory(package)
        save(evidence / 'outer-zip-inventory.json', inventory)
        require([{'name': item['name'], 'bytes': item['size_bytes'], 'crc32': item['crc32']} for item in inventory] ==
                sorted(pins['outer_members'], key=lambda item: item['name']), 'outer ZIP inventory mismatch')
        for member in (pins['product_member'], pins['checksums_member']):
            require(member in package.namelist(), 'pinned artifact member missing: ' + member)
            target = products / PurePosixPath(member).name
            with package.open(member) as source, target.open('xb') as destination:
                while block := source.read(MIB):
                    destination.write(block)
    product = products / PurePosixPath(pins['product_member']).name
    sums = products / PurePosixPath(pins['checksums_member']).name
    require(product.stat().st_size == pins['product_bytes'] and sums.stat().st_size == pins['checksums_bytes'] and
            sha(product) == pins['product_sha256'] and sha(sums) == pins['checksums_sha256'],
            'product or SHA256SUMS pin mismatch')
    entries = {}
    for line in sums.read_text().splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  ([^/\\]+)', line)
        require(match is not None and match[2] not in entries, 'malformed/duplicate SHA256SUMS entry')
        entries[match[2]] = match[1]
    require(entries.get(product.name) == sha(product), 'product is not bound by original SHA256SUMS')
    extract = scratch / 'offline destination with spaces'
    extract.mkdir()
    with zipfile.ZipFile(product) as package:
        inventory = zip_inventory(package)
        save(evidence / 'product-zip-inventory.json', inventory)
        for info in package.infolist():
            target = extract / info.filename
            if info.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with package.open(info) as source, target.open('xb') as destination:
                    while block := source.read(MIB):
                        destination.write(block)
                target.chmod((info.external_attr >> 16) & 0o777)
    manifests = list(extract.glob('*/manifest.sha256'))
    require(len(manifests) == 1 and manifests[0].stat().st_size == pins['manifest_bytes'] and sha(manifests[0]) == pins['manifest_sha256'], 'embedded manifest pin mismatch')
    root = manifests[0].parent
    gui = root / 'bin/datapump-gui'
    require(gui.is_file() and os.access(gui, os.X_OK) and gui.stat().st_size == pins['gui_bytes'] and sha(gui) == pins['gui_sha256'], 'GUI binary pin mismatch')
    manifest_entries = manifests[0].read_text().splitlines()
    require(manifest_entries.count(pins['gui_sha256'] + '  bin/datapump-gui') == 1,
            'embedded manifest does not bind the original GUI')
    build_info = (root / 'share/doc/datapump/build-info.txt').read_text()
    require(('Source commit: ' + pins['source_commit'] + '\n') in build_info and 'GUI: ON (fltk)\n' in build_info,
            'copied binary source/backend does not match original producer')
    return product, root, gui


def trace_closure(trace, logs, unique_json):
    require(trace.is_file() and trace.stat().st_size <= TRACE_LIMIT, 'trace missing or exceeds 128 MiB')
    seen, begin, end = set(), None, None
    with trace.open(encoding='utf-8') as stream:
        for line in stream:
            require(len(line.encode()) <= MIB, 'trace record exceeds bounded line size')
            require(line.endswith('\n') and end is None, 'trace is truncated or has data after footer')
            item = unique_json(line)
            if item.get('event') == 'trace_end':
                end = item
                continue
            sequence = item.get('sequence')
            require(type(sequence) is int and sequence >= 1 and sequence not in seen, 'duplicate/invalid trace sequence')
            if not seen:
                require(item.get('event') == 'trace_begin', 'trace does not start with begin record')
            seen.add(sequence)
            if item.get('event') == 'trace_begin':
                require(begin is None and item.get('schema') == 2 and item.get('event_filter') is not True and
                        isinstance(item.get('token'), str) and item['token'],
                        'duplicate/invalid trace begin')
                begin = item
    require(begin is not None and end is not None, 'trace begin/footer missing')
    attempted = end.get('attempted')
    require(type(attempted) is int and attempted >= 1 and len(seen) == attempted and
            min(seen) == 1 and max(seen) == attempted, 'trace sequences are not complete, unique, contiguous')
    require(end.get('token') == begin['token'] and end.get('finalization_required') is True and
            type(end.get('dropped')) is int and end['dropped'] == 0 and end.get('size_limited') is False and
            end.get('prior_io_failed') is False, 'trace footer is incomplete')
    seals = []
    for log in logs:
        for line in log.read_text(encoding='utf-8', errors='strict').splitlines():
            if line.startswith('{'):
                item = unique_json(line)
                if item.get('event') == 'trace_finalization':
                    seals.append(item)
    require(len(seals) == 1, 'missing/duplicate external finalization seal across stdout/stderr')
    seal = seals[0]
    require(seal.get('token') == begin['token'] and Path(seal.get('path', '')).resolve() == trace.resolve() and
            seal.get('attempted') == attempted and type(seal.get('attempted')) is int and
            type(seal.get('dropped')) is int and seal['dropped'] == 0 and seal.get('size_limited') is False and
            seal.get('prior_io_failed') is False and seal.get('flush_ok') is True and seal.get('close_ok') is True and
            seal.get('result') == 'complete', 'external finalization seal is mismatched/incomplete')
    return {'valid': True, 'records': attempted, 'footer': end, 'external_finalization': seal}


def fixtures(smoke):
    files = list(smoke.iterdir()) if smoke.is_dir() else []
    require(1 <= len(files) <= 8, 'synthetic fixture count outside 1..8')
    total = 0
    for path in files:
        require(not path.is_symlink() and path.is_file() and path.stat().st_size <= KEY_BYTES,
                'fixture is not a bounded direct regular file')
        total += path.stat().st_size
    require(total <= FIXTURE_LIMIT, 'synthetic fixtures exceed 150 MiB')
    keys = [path for path in files if path.name.startswith('generated keys café-') and path.suffix == '.key']
    require(len(keys) == 1 and keys[0].stat().st_size == KEY_BYTES, 'complete original synthetic key missing')
    return {'valid': True, 'key': record(keys[0]), 'files': [record(path) for path in sorted(files)]}


def capture(args):
    evidence, scratch = Path(args.evidence).resolve(), Path(args.scratch).resolve()
    require(evidence != scratch and evidence not in scratch.parents and scratch not in evidence.parents,
            'evidence and scratch must be disjoint')
    evidence.mkdir(parents=True, exist_ok=False)
    scratch.mkdir(parents=True, exist_ok=False)
    pins = json.loads(Path(args.pins).read_text())
    report = {'purpose': 'one independent synthetic capture; no performance comparison or qualification',
              'tool_commit': args.tool_commit, 'pins': pins, 'boundaries': 'ordinary GUI preimage; no typed binary boundary records',
              'smoke_timeout_seconds': 600, 'trace_capture_limit_bytes': TRACE_LIMIT,
              'application_default_trace_limit_bytes': 256 * MIB, 'artifact_limit_bytes': ARTIFACT_LIMIT,
              'status': 'capture_incomplete', 'trace_closure': {'valid': False}, 'fixtures': {'valid': False}}
    original_exit = None
    report['tooling'] = [record(Path(__file__).resolve()), record(Path(args.pins).resolve())]
    report['environment'] = {key: os.environ.get(key) for key in ('LP_NUM_THREADS', 'LIBGL_ALWAYS_SOFTWARE', 'GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT')}
    try:
        require(not any(isinstance(value, str) and value.startswith('ROOT_BIND') for value in pins.values()),
                'root must resolve all retained product/inventory pins before dispatch')
        product, root, gui = prepare(pins, evidence, scratch)
        report.update(archive=record(product), binary=record(gui), package_manifest=record(root / 'manifest.sha256'))
        for label, command in (
            ('package-verification', ['xvfb-run', '-a', '-s', '-screen 0 2400x1800x24 -dpi 96', 'cmake', '-DPACKAGE_ROOT=' + str(root), '-DGUI_SMOKE=OFF',
             '-DREQUIRE_MANUALS=ON', '-DREQUIRE_FRONTENDS=ON', '-P', 'tools/verify-native-package.cmake']),
            ('abi-verification', ['cmake', '-DPACKAGE_ROOT=' + str(root), '-DMAX_GLIBC=2.35', '-P', 'tools/verify-linux-abi.cmake'])):
            result, _, _ = run(command, evidence, label, 480)
            require(successful(result), label + ' failed; no smoke launched')
        require(bounded_tree(evidence) + TRACE_LIMIT + FIXTURE_LIMIT + 8 * MIB < ARTIFACT_LIMIT,
                'cannot reserve full trace/fixture/log budget before smoke')
        smoke, trace = evidence / 'smoke-dir', evidence / 'verbose.jsonl'
        def watch():
            require(not trace.exists() or trace.stat().st_size <= TRACE_LIMIT, 'trace exceeds 128 MiB capture bound')
            require(bounded_tree(evidence) <= ARTIFACT_LIMIT - MIB, 'evidence exceeds artifact budget')
            if smoke.exists():
                paths = list(smoke.iterdir())
                require(len(paths) <= 8 and all(p.is_file() and not p.is_symlink() and p.stat().st_size <= KEY_BYTES for p in paths),
                        'synthetic fixtures exceed file count/type/size bound')
                require(sum(p.stat().st_size for p in paths) <= FIXTURE_LIMIT, 'synthetic fixtures exceed total bound')
        command = ['xvfb-run', '-a', '-s', '-screen 0 2400x1800x24 -dpi 96', 'env',
                   'PATH=', 'PYTHONHOME=/nonexistent', 'PYTHONPATH=/nonexistent', 'LD_LIBRARY_PATH=',
                   str(gui), '--smoke-test', '--smoke-timeout', '600', '--smoke-dir', str(smoke),
                   '--verbose', '--verbose-file', str(trace)]
        result, stdout, stderr = run(command, evidence, 'smoke', 630, watch)
        original_exit = result['exit_code']
        report['smoke'] = result
        # Both closures are diagnostic evidence, independent of assertion outcome.
        module_spec = importlib.util.spec_from_file_location('rev_capture', Path(__file__).with_name('sdk-rev-smoke-capture.py'))
        shared = importlib.util.module_from_spec(module_spec)
        module_spec.loader.exec_module(shared)
        for name, callback in [('trace_closure', lambda: trace_closure(trace, (stdout, stderr), shared.unique_json)),
                               ('fixtures', lambda: fixtures(smoke))]:
            try:
                report[name] = callback()
            except (OSError, ValueError, KeyError) as exc:
                report[name] = {'valid': False, 'error': str(exc)}
        report['status'] = ('assertion_pass_capture_pending' if original_exit == 0 else 'assertion_failure_capture_pending')
        require(result['capture_error'] is None and result['process_group_closed'] and
                report['trace_closure']['valid'] and report['fixtures']['valid'], 'required capture closure incomplete')
        report['capture_complete'] = True
        report['status'] = ('captured_assertion_pass' if original_exit == 0 else 'captured_assertion_failure')
        require(original_exit == 0, 'original GUI assertion/process exit remains fatal: ' + str(original_exit))
    except Exception as exc:
        report['error'] = type(exc).__name__ + ': ' + str(exc)
        report.setdefault('capture_complete', False)
        raise
    finally:
        # Keep raw logs, key, trace, partial files and package on all failures.
        save(evidence / 'run.json', report)
        save(evidence / 'manifest.json', {'files': [record(path) for path in sorted(evidence.rglob('*'))
             if path.is_file() and not path.is_symlink() and path.name != 'manifest.json']})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pins', required=True)
    parser.add_argument('--evidence', required=True)
    parser.add_argument('--scratch', required=True)
    parser.add_argument('--tool-commit', required=True)
    args = parser.parse_args()
    capture(args)


if __name__ == '__main__':
    main()
