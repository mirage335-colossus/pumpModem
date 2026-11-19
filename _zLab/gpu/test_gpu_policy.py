#!/usr/bin/env python3
"""Verify that a CPU-only native worker refuses the lab GPU policy.

Run with the path to a known CPU-only datapump-worker as the first argument.
This check does not establish GPU execution or exercise a CUDA build.
"""
import subprocess
import sys


def main(worker):
    process = subprocess.Popen([worker, '--simulation', '--require-gpu'],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    output, error = process.communicate(timeout=10)
    assert process.returncode != 0, 'CPU-only worker accepted required GPU'
    assert not output, 'CPU-only worker presented a running UI'
    assert b'GPU reception is unavailable' in error, error.decode('utf-8', 'replace')
    print('CPU-only worker refused required GPU')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: test_gpu_policy.py CPU_ONLY_DATAPUMP_WORKER')
    main(sys.argv[1])
