import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

root = Path.cwd()
out = root / 'evidence/gpu-functional-coverage-001'
out.mkdir(exist_ok=False)
tests = ['test_triad_phase_frequency.py', 'test_triad_phase_optimized.py',
         'test_triad_phase_reduction.py', 'test_triad_phase_tensor.py']
sources = ['triad_phase_frequency.py', 'triad_phase_optimized.py',
           'triad_phase_reduction.py', 'triad_phase_tensor.py']
hashes = {p: hashlib.sha256((root/'tools'/p).read_bytes()).hexdigest() for p in sources}
env = dict(os.environ, COVERAGE_FILE=str(out/'.coverage'), OMP_NUM_THREADS='2',
           OPENBLAS_NUM_THREADS='2', MKL_NUM_THREADS='2')
rows = []
start = time.perf_counter()
for test in tests:
    command = [sys.executable, '-m', 'coverage', 'run', '--branch', '--append',
               '--include=tools/*', 'tests/'+test, '--device', 'cuda:0']
    with (out/(test+'.log')).open('w') as log:
        before = time.perf_counter()
        try:
            p = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=150)
            code = p.returncode
        except subprocess.TimeoutExpired:
            code = 124
    rows.append(dict(test=test, command=command, returncode=code,
                     wall_seconds=time.perf_counter()-before,
                     test_sha256=hashlib.sha256((root/'tests'/test).read_bytes()).hexdigest()))
    (out/'summary.json').write_text(json.dumps(dict(status='running', rows=rows, sources=hashes), indent=2))
subprocess.run([sys.executable, '-m', 'coverage', 'json', '-o', str(out/'coverage.json')], env=env, check=True)
assert hashes == {p: hashlib.sha256((root/'tools'/p).read_bytes()).hexdigest() for p in sources}
(out/'summary.json').write_text(json.dumps(dict(status='passed' if all(r['returncode']==0 for r in rows) else 'failed',
    rows=rows, sources=hashes, wall_seconds=time.perf_counter()-start,
    scope='CUDA phase family functional invariants and Python source arcs; not timing or CUDA instruction coverage'), indent=2))
sys.exit(0 if all(r['returncode']==0 for r in rows) else 2)
