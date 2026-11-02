#!/usr/bin/env python3
"""Record scientific build identity; exclude provider/account metadata."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tarfile

import numpy
import scipy
import torch


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--bundle', type=Path, required=True)
    p.add_argument('--bundle-sha256', required=True)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    root = a.root.resolve()
    if digest(a.bundle) != a.bundle_sha256:
        raise ValueError('source bundle digest mismatch')
    files = {}
    with tarfile.open(a.bundle, 'r:gz') as archive:
        for member in archive:
            if not member.isfile():
                raise ValueError('expected regular source members only')
            target = (root / member.name).resolve()
            if not target.is_relative_to(root) or member.name in files:
                raise ValueError('unsafe or duplicate source member')
            expected = hashlib.sha256(archive.extractfile(member).read()).hexdigest()
            actual = digest(target)
            if expected != actual:
                raise ValueError('extracted source differs: ' + member.name)
            files[member.name] = actual
    gpu = subprocess.run(['nvidia-smi', '--query-gpu=name,driver_version,memory.total',
                          '--format=csv,noheader'], check=True, capture_output=True,
                         text=True).stdout.strip()
    result = {'schema': 'triad-selective-build-provenance-v1',
              'base_revision': 'f13e3bc937f5b45949d7fd0adaedbe689f1b71b7',
              'source_bundle_sha256': a.bundle_sha256,
              'source_bundle_bytes': a.bundle.stat().st_size,
              'all_extracted_source_files_match_bundle': True,
              'source_file_sha256': files,
              'binary_sha256': digest(a.binary),
              'binary_bytes': a.binary.stat().st_size,
              'python': platform.python_version(), 'numpy': numpy.__version__,
              'scipy': scipy.__version__, 'torch': torch.__version__,
              'torch_cuda': torch.version.cuda, 'gpu': gpu,
              'build_command': './build.sh test triad --cli --jobs 2 --build-jobs 12',
              'execution_environments': {
                  'screen_and_highspread': 'inherited image environment; --threads 1',
                  'holdout_long_alternatives': 'OMP_NUM_THREADS=OPENBLAS_NUM_THREADS=MKL_NUM_THREADS=1; --threads 1'},
              'scope': 'Source and executable identity, not device or receiver certification.'}
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with a.output.open('x') as stream:
        json.dump(result, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write('\n')
    print(json.dumps({'source_files': len(files), 'binary_sha256': result['binary_sha256']}))


if __name__ == '__main__':
    main()
