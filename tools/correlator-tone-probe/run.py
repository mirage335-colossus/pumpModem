#!/usr/bin/env python3
"""Retain a matched original/observed numeric diagnostic; never qualification."""
from pathlib import Path
import argparse, hashlib, json, os, resource, shutil, signal, subprocess, time

def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()

def save(path, data):
    path.write_text(json.dumps(data, indent=2) + '\n')

def command(argv, output, name, timeout, env=None):
    stdout, stderr = output / (name + '.stdout.log'), output / (name + '.stderr.log')
    started = time.monotonic_ns()
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    timed_out, process = False, None
    with stdout.open('xb') as out, stderr.open('xb') as err:
        def limits():
            resource.setrlimit(resource.RLIMIT_FSIZE, (128 * 1024 * 1024, 128 * 1024 * 1024))
        process = subprocess.Popen(argv, stdout=out, stderr=err, env=env,
                                   start_new_session=True, preexec_fn=limits)
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            os.killpg(process.pid, signal.SIGTERM)
            try:
                code = process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                code = process.wait(timeout=10)
        finally:
            # Also close any descendants surviving a normally exited leader.
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait(timeout=10)
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    return {'command': argv, 'exit_code': code, 'timed_out': timed_out,
            'wall_ns': time.monotonic_ns() - started,
            'whole_child_cpu_seconds': after.ru_utime + after.ru_stime - before.ru_utime - before.ru_stime,
            'stdout': {'path': str(stdout), 'sha256': digest(stdout), 'bytes': stdout.stat().st_size},
            'stderr': {'path': str(stderr), 'sha256': digest(stderr), 'bytes': stderr.stat().st_size}}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True)
    parser.add_argument('--jobs', type=int, required=True)
    args = parser.parse_args()
    root = Path.cwd().resolve()
    git = ['git', '-c', 'safe.directory=' + str(root)]
    build = Path(args.build_dir).resolve()
    output = build / 'correlator-tone-probe'
    output.mkdir(exist_ok=False)
    binary = build / 'test_pattern_correlator'
    tools = root / 'tools/correlator-tone-probe'
    manifest = json.loads((tools / 'manifest.json').read_text())
    result = {'classification': 'diagnostic only; exact assertions remain fatal',
              'tool_source': subprocess.check_output(git + ['rev-parse', 'HEAD'], text=True).strip(),
              'baseline_code': '95cc9b0514d7555103de56723ae7a4adf406bb35',
              'fixture': 'parallel_long_tiles_preserve_boundaries; unchanged seed529/001/input polls',
              'manifest_sha256': digest(tools / 'manifest.json'), 'restored': False}
    originals, changed = {}, []
    try:
        subprocess.run(git + ['diff', '--exit-code', result['baseline_code'], '--', 'src', 'include', 'tests', 'CMakeLists.txt', 'cmake'], check=True)
        for name in ('CMakeCache.txt', 'compile_commands.json'):
            if (build / name).is_file():
                shutil.copy2(build / name, output / name)
        cache = (build / 'CMakeCache.txt').read_text()
        assert 'CMAKE_BUILD_TYPE:STRING=Release\n' in cache
        assert 'DATAPUMP_SANITIZERS:BOOL=OFF\n' in cache
        compiler_entries = [line.split('=', 1)[1] for line in cache.splitlines()
                            if line.startswith(('CMAKE_CXX_COMPILER:FILEPATH=', 'CMAKE_CXX_COMPILER:STRING='))]
        assert len(compiler_entries) == 1 and Path(compiler_entries[0]).is_file(), compiler_entries
        result['compiler_path'] = compiler_entries[0]
        result['compiler'] = subprocess.check_output([compiler_entries[0], '--version'], text=True, timeout=35).splitlines()[0]
        result['manifest'] = manifest
        # Schema is bound after independent review of the private adapter.
        for entry in manifest['files']:
            path = root / entry['path']
            originals[entry['path']] = path.read_bytes() if path.exists() else None
            actual = digest(path) if path.exists() else None
            assert actual == entry['pre_sha256'], (entry['path'], actual)
            proposed = tools / entry['payload']
            assert digest(proposed) == entry['post_sha256'], entry['path']
            target = output / 'preimage' / entry['path']
            target.parent.mkdir(parents=True, exist_ok=True)
            if path.exists():
                shutil.copy2(path, target)
            post = output / 'postimage' / entry['path']
            post.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(proposed, post)
        binaries = output / 'bin'
        binaries.mkdir()
        original_binary = binaries / 'baseline-test_pattern_correlator'
        shutil.copy2(binary, original_binary)
        result['baseline_binary_sha256'] = digest(original_binary)
        environment = dict(os.environ)
        environment.pop('DATAPUMP_CORRELATOR_TONE_TRACE', None)
        result['baseline'] = command([str(original_binary), 'parallel_long_tiles_preserve_boundaries'], output, 'baseline', 3600, environment)
        save(output / 'result.json', result)
        for entry in manifest['files']:
            path = root / entry['path']
            path.parent.mkdir(parents=True, exist_ok=True)
            changed.append(entry['path'])
            path.write_bytes((tools / entry['payload']).read_bytes())
        result['rebuild'] = command(['cmake', '--build', str(build), '--target', 'test_pattern_correlator', '--parallel', str(args.jobs)], output, 'rebuild', 1200)
        save(output / 'result.json', result)
        if result['rebuild']['exit_code'] != 0:
            result['outcome'] = 'diagnostic build setup failed; no observed application run'
            return 1
        observed_binary = binaries / 'observed-test_pattern_correlator'
        shutil.copy2(binary, observed_binary)
        result['observed_binary_sha256'] = digest(observed_binary)
        environment['DATAPUMP_CORRELATOR_TONE_TRACE'] = str(output / 'numeric.jsonl')
        result['observed'] = command([str(observed_binary), 'parallel_long_tiles_preserve_boundaries'], output, 'observed', 3600, environment)
        trace = output / 'numeric.jsonl'
        if trace.is_file():
            result['trace'] = {'path': str(trace), 'bytes': trace.stat().st_size, 'sha256': digest(trace)}
        result['outcome'] = 'retained actual exits; numeric completeness and first divergence require independent readback'
        # Diagnostic variants never convert the original assertion to a pass.
        if result['baseline']['exit_code'] or result['observed']['exit_code']:
            return 1
        return 0
    except Exception as error:
        result['setup_error'] = repr(error)
        raise
    finally:
        for name in reversed(changed):
            path = root / name
            original = originals[name]
            if original is None:
                path.unlink()
            else:
                path.write_bytes(original)
        result['restored'] = all((not (root / name).exists()) if data is None else (root / name).read_bytes() == data for name, data in originals.items())
        save(output / 'result.json', result)
        if not result['restored']:
            raise RuntimeError('diagnostic sources were not restored; retained result is not accepted')

if __name__ == '__main__':
    raise SystemExit(main())
