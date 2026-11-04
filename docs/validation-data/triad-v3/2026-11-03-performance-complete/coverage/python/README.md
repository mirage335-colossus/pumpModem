# Focused CPU Python runtime coverage

## Result and interpretation

### Fresh dependency-closed capture

The latest capture is [`attempt-04-dependency-closed`](attempt-04-dependency-closed/summary.json). It ran coverage.py 7.17.1 with Python 3.13.5, Torch 2.8.0+cpu, and CPUs 6 and 8. All **16/16 focused test files passed** within the 60-second per-file limit; the full instrumented invocation took 53.43 seconds. All 16 expected Python source rows were present.

Before and after the instrumented run, the collector hashed the frozen coverage matrix, every runtime/helper source in the matrix-derived 19-path union, all selected and matrix-declared test sources, the declared study fixture, and the collector itself. The matrix SHA256 was unchanged at `1860b045b4078da56db5b8928cbb3b71660844a94d0c5da3ddb1eab5382e5a33`; dependency and test hashes were stable. The report records 41 dependency hashes for the matrix union plus declared tests/helpers and retains pre/post maps. The exact matrix snapshot, dependencies, 21 test source snapshots, runtime rows, raw coverage data, and per-file logs are archived beside the report.

The performance wrapper's `check_coverage` accepted this receipt: all expected source rows matched, no declared dependencies were missing or changed, and `declared_dependency_status` was `matched`. The matrix hash is stored as capture provenance, separate from `dependency_sha256`, so later evidence/status-only edits to the matrix do not invalidate the captured DSP dependency set. This hash set covers the declared matrix union and identified tests/helpers; it is **not a claim of full dynamic import closure**.

The previous attempt history remains below. Attempt-03 preserves the split reconciliation after the original ray-frequency failure. Attempt-04 is a new single 16-file passing invocation and does not rewrite that history.

The earlier run used the same coverage.py 7.17.1/Python 3.13.5/CPU-only environment and instrumented 16 maintained isolated-triad runtime Python files while running each selected test file as `__main__`.

The original 16-file invocation **failed**: 15 test files passed, while `test_triad_ray_frequency.py` had one numerical assertion failure. The failure text is retained in `logs/test_triad_ray_frequency.py.log`. Independent diagnosis found a mismatch between static scoring's recorded noise inflation and ray scoring's original noise model. The accepted test-only correction added an independent dense-oracle comparison and preserved strict tolerances. A focused rerun then passed 7/7 tests in 0.407 seconds; its output is in `logs/followup-test_triad_ray_frequency.py.log`.

This is split-run evidence: 15 unchanged prior passing files plus the corrected ray test. It is not one clean 16-file invocation. The attempt-02 raw `.coverage` database was amended by the focused append before the attempt-03 provenance copy was made. The original failure is preserved in the normalized test record and log; attempt-03 copies the amended raw database and JSON report.

The follow-up's exact test source SHA256 is `982473b034caa7bc550e1acee29dc2d9c8e33c6131e25683abd11bc95fbb89b3`; its runtime source SHA256 is `ea3ca2acde8aea75538066e46e3ccae2e36612afac753db6b2d048bd6ff87dd2`. All 16 runtime source snapshots in `attempt-03-reconciled/sources/tools/` were checked against their report hashes. The original test-source hashes were not captured: the 15 original test files and the pre-correction ray test have no source hash binding. No original test hash is inferred here. The copied ray test snapshot is the corrected follow-up version only.

| Runtime source | Covered / executable arcs | SHA256 |
|---|---:|---|
| `triad_gaussian_state.py` | 40 / 56 | `679035e50b27fe919644775460ef25670d95433c8886b39b4474f7f95ecfe7ba` |
| `triad_state_workspace.py` | 126 / 182 | `88887f23264e00bd432a626a987a3a86592a5b8f674aba25194b8569e0df9923` |
| `triad_prepared_observation.py` | 24 / 36 | `13d37977620519aaeb457d4ed1163c80d68dca96c8319480de914cae8a6aaec8` |
| `triad_gaussian_window.py` | 64 / 90 | `b22fe002864922e44bc7e10000672a163353818f610c4882bae5d62cfe1cf883` |
| `triad_noise_likelihood.py` | 75 / 108 | `6964eee7372e0d3dbd590fb7f78a6c2b9c6d133dc012b488cce566f2d2447110` |
| `triad_rbpf.py` | 73 / 98 | `d4b64c3a830acd3bd15433421da0e003ba1aa9810bf1dd904c57082d94c3bd54` |
| `triad_phase_frequency.py` | 82 / 128 | `48ee569b1d467b2d7b6fd55088764bd28dd11e0bfecb5974622a318fbb408585` |
| `triad_phase_optimized.py` | 15 / 54 | `759b46d536f2daa1d1de2336190ab19153376aca3eeca32847cba5289d2d63ed` |
| `triad_fft_acquire.py` | 136 / 180 | `d4a3c2cc39e0068e6aa6aab27d7ee8eeff6cc7db72a790868fe166c3f1d65843` |
| `triad_group_acquire.py` | 166 / 220 | `1e060eb53a411513598a01bd84cc204133056cabdab366c255b66d6b6123b104` |
| `triad_gaussian_prepared.py` | 44 / 102 | `c88afdf459521393958c49d801517d79cd158cbe2291e4a48c3eff6931e42875` |
| `triad-state-study.py` | 7 / 76 | `3fa7491ae93031ff6bfadc4c9b792659336999effc0508a71cf734ada9250402` |
| `triad-rbpf-study.py` | 72 / 92 | `34a9797ed7119dcf6545b6be759b8f506cbdaf6ac9ece886b82fa699cd5d085f` |
| `triad-static-frequency-study.py` | 73 / 106 | `d1b75e9b49de21041558e97ee1daff2ba07a1fd18a123bc8a6cb928f1052e11b` |
| `triad-cfo-batch-study.py` | 43 / 62 | `5c7fb6f8b0ad8cadeaae8591d9fb0e3d489fb5eb4c14962faf800ad46a661665` |
| `triad-ray-frequency-study.py` | 48 / 76 | `ea3ca2acde8aea75538066e46e3ccae2e36612afac753db6b2d048bd6ff87dd2` |

These are Python control-flow arcs. They do not establish semantic branch completeness, numerical correctness across all inputs, or paths inside C/C++ extensions, NumPy, SciPy, Torch, or CUDA. CUDA was unavailable in this CPU environment.

## Reproduction record

The original command was:

```sh
taskset -c 6,8 /tmp/triad-weak-env/bin/python \
  tools/triad_perf_python_coverage.py \
  --raw-dir local-evidence/triad-performance-complete/python-coverage/attempt-02-main/raw \
  --output local-evidence/triad-performance-complete/python-coverage/attempt-02-main/summary.json
```

That command was run before the ray test correction; the pre-correction test source was not snapshotted or hashed, so the original failure is not byte-for-byte reproducible from the archived test source. The focused corrected follow-up used coverage.py 7.17.1, `--branch --append`, the exact 16 runtime source includes, and `tests/test_triad_ray_frequency.py` on CPUs 6 and 8. Its raw appended database is retained. The corrected test source and runtime source snapshots are archived under `attempt-03-reconciled/sources/`.

`SHA256SUMS` covers the archived summary, raw database/JSON, logs, and available source snapshots.
