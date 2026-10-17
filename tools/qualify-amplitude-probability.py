#!/usr/bin/env python3
"""Frozen, opt-in standalone amplitude capture study. This does not qualify Live."""
import argparse
import concurrent.futures
import hashlib
import json
import math
from pathlib import Path
import random
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def lower_bound(successes, count, alpha):
    """One-sided exact binomial lower confidence bound."""
    if not successes:
        return 0.0
    if successes == count:
        return alpha ** (1 / count)
    lo, hi = 0.0, 1.0
    for _ in range(80):
        p = (lo + hi) / 2
        terms = [math.lgamma(count + 1) - math.lgamma(k + 1) - math.lgamma(count - k + 1)
                 + k * math.log(p) + (count - k) * math.log1p(-p)
                 for k in range(successes, count + 1)]
        peak = max(terms)
        tail = math.exp(peak) * sum(math.exp(t - peak) for t in terms)
        if tail < alpha:
            lo = p
        else:
            hi = p
    return lo


def assess(directory, frozen):
    rows = []
    complete = True
    for batch in frozen['batches']:
        log = directory / batch['log']
        status = directory / batch['status']
        if not log.exists() or not status.exists():
            complete = False
            continue
        complete &= json.loads(status.read_text())['returncode'] == 0
        for line in log.read_text().splitlines():
            if line.startswith('amplitude_capture '):
                rows.append(dict(part.split('=', 1) for part in line.split()[1:]))
    expected = sorted(s for batch in frozen['batches'] for s in batch['seeds'])
    actual = sorted(int(row['seed']) for row in rows)
    complete &= actual == expected
    complete &= all(int(row.get('seconds', '0')) == frozen['seconds'] and
                    float(row.get('watts', 'nan')) == frozen['watts'] and
                    row.get('bits') == '1' and row.get('extra_keys') == '0' for row in rows)
    successes = sum(all(row.get(k) == value for k, value in {
        'correct': '1', 'observed': '1', 'streams': '1', 'extras': '0',
        'complete': '1', 'early': '0', 'extra_keys': '0'}.items()) for row in rows)
    low = lower_bound(successes, len(rows), frozen['alpha']) if complete and rows else 0
    result = dict(complete=bool(complete), captures=len(rows), successes=successes,
                  failures=len(rows) - successes, one_sided_confidence=1-frozen['alpha'],
                  probability_lower=low, passed=bool(complete and low > .99),
                  scope=frozen['scope'], freeze_sha256=digest(directory / 'freeze.json'))
    print(json.dumps(result, indent=2))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['prepare', 'run', 'assess'])
    parser.add_argument('--study', type=Path, required=True)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--seconds', type=int, default=512)
    parser.add_argument('--watts', type=float, default=15000)
    parser.add_argument('--captures', type=int, default=299)
    parser.add_argument('--workers', type=int, default=6)
    parser.add_argument('--alpha', type=float, default=.05)
    args = parser.parse_args()
    directory = args.study.resolve()
    if args.mode == 'prepare':
        if not args.binary or not (16 <= args.seconds <= 86400 and args.watts > 0 and
                                   math.isfinite(args.watts) and 0 < args.alpha < 1 and
                                   1 <= args.workers <= args.captures <= 100000):
            parser.error('prepare requires a binary and valid capture configuration')
        directory.mkdir(parents=True, exist_ok=False)
        binary = args.binary.resolve(strict=True)
        seeds = random.SystemRandom().sample(range(1000000000, 2**32), args.captures)
        batches = []
        for i in range(args.workers):
            batch = seeds[i::args.workers]
            seed_file = directory / f'seeds-{i}.txt'
            seed_file.write_text(''.join(f'{s}\n' for s in batch))
            batches.append(dict(seeds=batch, seed_file=seed_file.name,
                                seed_sha256=digest(seed_file), log=f'captures-{i}.log',
                                status=f'status-{i}.json'))
        frozen = dict(binary=str(binary), binary_sha256=digest(binary), seconds=args.seconds,
                      watts=args.watts, alpha=args.alpha, batches=batches,
                      scope='Single public first raw bit, amplitude-only PatternReceiver; '
                            'EME oscillator, actual mean source power, seeded AWGN/arrival ensemble. '
                            'No interpolation between conditions; no Live rolling banks, extra keys, '
                            'multi-bit messages, radio hardware or actual EME propagation qualification.')
        (directory / 'freeze.json').write_text(json.dumps(frozen, indent=2) + '\n')
        print(directory / 'freeze.json')
        return 0
    frozen = json.loads((directory / 'freeze.json').read_text())
    if args.mode == 'run':
        binary = Path(frozen['binary'])
        if digest(binary) != frozen['binary_sha256']:
            parser.error('binary differs from frozen study')
        for batch in frozen['batches']:
            if digest(directory / batch['seed_file']) != batch['seed_sha256']:
                parser.error('seed list differs from frozen study')
            if (directory / batch['log']).exists() or (directory / batch['status']).exists():
                parser.error('run outputs already exist; assess them or prepare a new study')
        def capture(batch):
            command = [str(binary), '--native-capture', str(frozen['seconds']), str(frozen['watts']),
                       '0', str(len(batch['seeds'])), '1', '0', str(directory / batch['seed_file'])]
            with (directory / batch['log']).open('x') as log:
                process = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)
            (directory / batch['status']).write_text(json.dumps({'returncode': process.returncode}) + '\n')
        with concurrent.futures.ThreadPoolExecutor(max_workers=len(frozen['batches'])) as pool:
            list(pool.map(capture, frozen['batches']))
        if digest(binary) != frozen['binary_sha256']:
            parser.error('binary changed during captures; study is invalid')
    result = assess(directory, frozen)
    if args.mode == 'run':
        (directory / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
