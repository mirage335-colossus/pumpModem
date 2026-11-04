# GPU phase-family functional coverage

This is a separate CUDA functional and Python source-arc measurement from the CPU runtime coverage report. The four selected test files all passed with `--device cuda:0`; this is functional coverage, not timing, reliability qualification, or CUDA instruction coverage.

The recorded runner used `/workspace/triad-env/bin/python`, coverage.py branch instrumentation, and `cuda:0`. The archived companion environment extract identifies Python 3.12.3, Torch 2.8.0+cu128, CUDA build 12.8, and an NVIDIA H200 (compute capability 9.0). The functional wrapper explicitly set `OMP_NUM_THREADS=2`, `OPENBLAS_NUM_THREADS=2`, `MKL_NUM_THREADS=2`, and `COVERAGE_FILE`; no `CUDA_VISIBLE_DEVICES` or additional GPU selection flag was recorded. These four test files all passed and totaled 67.60 seconds. Their logs retain detailed numerical, precision, recurrence, and CUDA-graph checks.

| Runtime source | Covered / executable arcs | SHA256 |
|---|---:|---|
| `triad_phase_frequency.py` | 85 / 128 | `48ee569b1d467b2d7b6fd55088764bd28dd11e0bfecb5974622a318fbb408585` |
| `triad_phase_optimized.py` | 34 / 54 | `759b46d536f2daa1d1de2336190ab19153376aca3eeca32847cba5289d2d63ed` |
| `triad_phase_reduction.py` | 10 / 62 | `b2f762320fbff5a66fa88123711483f9c42721e1765e406b7699c434048ded98` |
| `triad_phase_tensor.py` | 54 / 94 | `3b1a674ac7ace90ddce72c2773c2d1543bcfe8307d7b65ea965ad03a6525ffda` |

The source and test files in `gpu-functional-coverage-001/sources/` match the hashes in the report. Raw `.coverage`, coverage JSON, four test logs, the run wrapper, and the environment extract are retained beside the summary. The exact per-file commands and test-source hashes are recorded in `summary.json`.

The original run command form was:

```sh
/workspace/triad-env/bin/python -m coverage run --branch --append \
  --include='tools/*' tests/<selected-test>.py --device cuda:0
```

This GPU result exercises selected CUDA paths on the recorded H200. It does not prove all GPU branches or instruction dispatches, and it makes no end-to-end modem speed claim. `SHA256SUMS` covers every archived artifact, log, and matching source snapshot.
