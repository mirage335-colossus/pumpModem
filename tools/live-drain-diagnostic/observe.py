#!/usr/bin/env python3
"""Observe one retained ELF without changing its fixture or internal budgets."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import selectors
import signal
import subprocess
import sys
import time


def available():
    for line in Path('/proc/meminfo').read_text().splitlines():
        if line.startswith('MemAvailable:'):
            return int(line.split()[1]) * 1024
    raise RuntimeError('MemAvailable unavailable')


def stat_record(path):
    text = path.read_text()
    fields = text.rsplit(')', 1)[1].split()
    return {'state': fields[0], 'user_ticks': int(fields[11]),
            'system_ticks': int(fields[12]), 'start_ticks': int(fields[19])}


def process_sample(pid):
    base = Path('/proc') / str(pid)
    value = stat_record(base / 'stat')
    value['threads'] = []
    tasks = sorted((base / 'task').iterdir(), key=lambda p: int(p.name))
    if len(tasks) > 128:
        raise RuntimeError('thread inventory exceeds observation cap')
    for task in tasks:
        try:
            item = {'tid': int(task.name), **stat_record(task / 'stat')}
            try:
                item['wchan'] = (task / 'wchan').read_text().strip()[:120]
            except OSError as exc:
                item['wchan_unavailable'] = type(exc).__name__
            value['threads'].append(item)
        except FileNotFoundError:
            value.setdefault('departed_during_sample', []).append(int(task.name))
    for line in (base / 'status').read_text().splitlines():
        if line.startswith('VmRSS:'):
            value['rss_kib'] = int(line.split()[1])
    return value


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('selector', choices=['--timestamps', '--private', 'original'])
    parser.add_argument('--probe', action='store_true')
    parser.add_argument('--no-sampling', action='store_true')
    parser.add_argument('--input-capture', action='store_true')
    args = parser.parse_args()
    args.output.mkdir()
    memory = available()
    if memory <= 4 * 1024 ** 3:
        raise RuntimeError('fresh MemAvailable below 4GiB application launch guard')
    affinity = sorted(os.sched_getaffinity(0))[:2]
    if len(affinity) != 2:
        raise RuntimeError('two CPUs unavailable')
    os.sched_setaffinity(0, affinity)
    os.nice(10)
    env = os.environ.copy()
    for name in list(env):
        if name.startswith('LD_'):
            del env[name]
    env['LD_LIBRARY_PATH'] = str(args.binary.parent / 'runtime')
    env['ASAN_OPTIONS'] = 'detect_leaks=0:halt_on_error=1'
    env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    env.pop('DATAPUMP_LIVE_DRAIN_PROBE', None)
    env.pop('DATAPUMP_LIVE_DRAIN_INPUT_DIR', None)
    if args.probe:
        env['DATAPUMP_LIVE_DRAIN_PROBE'] = '1'
    if args.input_capture:
        env['DATAPUMP_LIVE_DRAIN_INPUT_DIR'] = str(args.output)
    start = time.monotonic_ns()
    usage_start = resource.getrusage(resource.RUSAGE_SELF)
    command = [str(args.binary)] + ([] if args.selector == 'original' else [args.selector])
    if args.probe:
        command.append('--trace=' + str(args.output / 'receiver.trace.jsonl'))
    result = {'command': command, 'probe': args.probe, 'sampling': not args.no_sampling, 'affinity': affinity,
              'nice': os.getpriority(os.PRIO_PROCESS, 0), 'memory_available_before': memory,
              'start_monotonic_ns': start, 'sample_period_ns': 100000000,
              'clock_ticks_per_second': os.sysconf('SC_CLK_TCK'),
              'environment': {k: env[k] for k in ['LD_LIBRARY_PATH', 'ASAN_OPTIONS', 'UBSAN_OPTIONS']},
              'observation_error': None, 'sample_count': 0, 'event_count': 0}
    child = None
    total_bytes = 0
    def interrupted(signum, frame):
        raise KeyboardInterrupt('signal ' + str(signum))
    signal.signal(signal.SIGTERM, interrupted)
    try:
        with (args.output / 'samples.jsonl').open('xb') as samples, (args.output / 'events.jsonl').open('xb') as events:
            child = subprocess.Popen(result['command'], stdin=subprocess.DEVNULL,
                                     stdout=None, stderr=subprocess.PIPE, env=env,
                                     cwd=args.output)
            result['pid'] = child.pid
            streams = selectors.DefaultSelector()
            streams.register(child.stderr, selectors.EVENT_READ)
            pending = b''
            next_sample = time.monotonic()
            while streams.get_map() or child.poll() is None:
                now = time.monotonic()
                if now >= next_sample and child.poll() is None:
                    memory = available()
                    if memory < 1536 * 1024 ** 2:
                        raise RuntimeError('MemAvailable fell below 1.5GiB stop guard')
                    try:
                        record = {'elapsed_ns': time.monotonic_ns() - start,
                                  'memory_available': memory, **({} if args.no_sampling else process_sample(child.pid))}
                        encoded = json.dumps(record, separators=(',', ':')).encode() + b'\n'
                        total_bytes += len(encoded)
                        if total_bytes > 8 * 1024 ** 2:
                            raise RuntimeError('bounded observations exceeded 8MiB')
                        samples.write(encoded)
                        result['sample_count'] += 1
                    except FileNotFoundError:
                        result['exit_during_sample'] = True
                    next_sample = now + 0.1
                for key, _ in streams.select(0.025):
                    block = os.read(key.fileobj.fileno(), 65536)
                    if not block:
                        streams.unregister(key.fileobj)
                        key.fileobj.close()
                        continue
                    sys.stderr.buffer.write(block)
                    sys.stderr.buffer.flush()
                    pending += block
                    if len(pending) > 1024 ** 2:
                        raise RuntimeError('unterminated stderr line exceeded bound')
                    while b'\n' in pending:
                        line, pending = pending.split(b'\n', 1)
                        record = {'elapsed_ns': time.monotonic_ns() - start,
                                  'line': line.decode('utf-8', 'replace')}
                        try:
                            record['process'] = stat_record(Path('/proc') / str(child.pid) / 'stat')
                        except FileNotFoundError:
                            record['process_already_exited'] = True
                        encoded = json.dumps(record, separators=(',', ':')).encode() + b'\n'
                        total_bytes += len(encoded)
                        if total_bytes > 8 * 1024 ** 2:
                            raise RuntimeError('bounded observations exceeded 8MiB')
                        events.write(encoded)
                        result['event_count'] += 1
            if pending:
                events.write(json.dumps({'elapsed_ns': time.monotonic_ns() - start,
                                         'unterminated_line': pending.decode('utf-8', 'replace')}).encode() + b'\n')
            streams.close()
            result['exit_code'] = child.wait(timeout=2)
    except (Exception, KeyboardInterrupt) as exc:
        result['observation_error'] = str(exc)
        if child is not None and child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=2)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=2)
        result['exit_code'] = None if child is None else child.returncode
    finally:
        used = resource.getrusage(resource.RUSAGE_CHILDREN)
        own = resource.getrusage(resource.RUSAGE_SELF)
        result.update({'wall_seconds': (time.monotonic_ns() - start) / 1e9,
                       'child_user_seconds': used.ru_utime, 'child_system_seconds': used.ru_stime,
                       'child_max_rss_kib': used.ru_maxrss,
                       'observer_cpu_seconds': own.ru_utime + own.ru_stime - usage_start.ru_utime - usage_start.ru_stime,
                       'sample_bytes': total_bytes,
                       'child_reaped': child is not None and child.poll() is not None})
        (args.output / 'observation.json').write_text(json.dumps(result, indent=2) + '\n')
    return result['exit_code'] if result['exit_code'] in range(256) and not result['observation_error'] else 2


if __name__ == '__main__':
    sys.exit(main())
