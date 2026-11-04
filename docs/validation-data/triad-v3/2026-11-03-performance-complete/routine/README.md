# Complete CPU campaign and remaining cost

`cpu-complete-001` finished in **2,999.857887 seconds (49 min 59.86 s)**,
with exit 2 (`incomplete`), no execution errors, all 23 declared warm workload
decisions passing and all 16 functional commands passing. This is the measured
complete campaign cost on a Ryzen 5 PRO 5650U, affinity CPUs 2,4, two Python
threads and one native thread. Compatible builds and fixtures already existed;
their earlier construction is outside this wall time. The three CUDA-only
sampled families are not claimed as CPU executions.

This run does **not** qualify the entire requested suite or demonstrate a
five-to-ten-minute complete routine. It preserves the failed prediction history
and uses direct full jobs for CPU sampled families. No repetitions were trimmed
or restarted to obtain a pass.

| Serial component | Measured wall seconds |
|---|---:|
| Four native checks plus fifteen Python functional commands | 55.817 |
| Thirteen native/FFT cases, including hardware screens | 699.499 |
| Strict identical-implementation free-crystal route | 120.954 |
| Nine direct sampled-family cases | 2,121.931 |
| Inventory, orchestration and remaining overhead | 1.655 |
| Complete wrapper | 2,999.858 |

The functional commands include one CTest command executing four native tests;
there are sixteen commands, not nineteen independent suites. The sampled
driver's own measured scope is 2,120.456 seconds, inside its enclosing process
wall time above.

| Sampled full target | Case wall seconds | Paired observations | 5% timing target |
|---|---:|---:|---|
| Gaussian hour | 178.674 | 17 | met |
| Phase reference hour | 371.486 | 33 | met |
| Phase optimized hour | 528.684 | 33 | met |
| Grouped multitile | 105.502 | 33 | missed |
| RBPF systematic resampling | 307.073 | 33 | missed |
| CFO batch with tail | 280.435 | 17 | met |
| Static FFT | 184.011 | 33 | met |
| Ray-frequency tiles | 150.654 | 33 | met |
| Small sampled state control | 6.927 | 33 | met |

Nine legacy cases also missed the 5% target; twelve of the 23 overall targets
met it. A passed regression decision is distinct from reaching that tighter
precision goal. All declared target prediction/direct-measurement eligibility
and performance gates passed. A direct full result does not qualify an earlier
failed abbreviated model.

The original wrapper additionally required the 5% goal for its warm-completion
flag, and counted permanent limitations as inventory gaps. Its exact original
summary is retained. The later classification repair changes neither timings
nor model, endpoint or regression gates; any replay under that repair is
explicitly separate from this measured protocol.

## Why this is not a demonstrated fast suite

The earlier 836.952-second routine covered a smaller scope. In the new campaign,
the completed functional/native/identical-FFT components alone cost 876.271
seconds before the additional sampled families. Broad-HF and VLF850 cases
accounted for about 325 seconds of the legacy child. The current expanded
sampled cases required 35.3 minutes under their predeclared sampling policy.
CPU cached-stage models had previously failed independent validation; using
their cheaper numbers would not preserve comparable assurance.

Reaching ten minutes from this actual complete cost would require an 80.0%
reduction; five minutes would require 90.0%. Those are required reductions,
not demonstrated opportunities or physical lower bounds. No matched-assurance
complete-suite savings have been demonstrated. The existing native VLF and FFT
anchors remain useful within their own validated boundaries.

Free-crystal now has a passing historical-conditional check in the strict
equal-implementation route. It does not retrospectively resolve the earlier
paired protocol's rejected endpoint. Its final absolute comparison interval
was approximately [0.777, 1.269]; the existing upper-bound regression gate passed.
That interval must not be described as a universal 20%-width guarantee, nor
confused with its 3.96% current timing precision.

Three five-second hardware screens warned against the retained CPU baseline:
copy throughput gave a 1.347 time-equivalent ratio, complex64 phase reduction
1.417, and complex128 phase reduction 1.474. These are raw health screens,
not replicated evidence of a specific environmental cause or a receiver
regression. The complete result also lacked qualified cold-start prediction
and fully applicable dependency-bound coverage receipts. Subsequent focused
coverage work is separate evidence and does not retime this campaign.

## Reproduce selectively

The exact child argument vectors, numerical environment, source/build/input
identities, work counters and intervals are in
[`cpu-complete-001/summary.json.gz`](cpu-complete-001/summary.json.gz) and its
adjacent reports. Use the [guide](../../../../triad-performance.md) and
[matrix](../../../../triad-performance-matrix.md) to select an affected case.
Do not run this fifty-minute direct campaign for a documentation or reporting
change. The measured complete invocation used:

```sh
taskset -c 2,4 python tools/triad_perf_complete.py \
  --candidate-root . --reference-root REFERENCE_SOURCE \
  --binary build/dev-cli/triad_long --reference-binary REFERENCE_BINARY \
  --anchor-directory LEGACY_ANCHORS \
  --family-route paired --family-mode direct \
  --identical-fft-case free-crystal --identical-anchor-directory IDENTICAL_ANCHORS \
  --fixture-cache FIXTURE_CACHE --hardware-baseline HARDWARE_PROFILE_JSON \
  --native-coverage NATIVE_COVERAGE_JSON --python-coverage PYTHON_COVERAGE_JSON \
  --functional-defaults --device cpu --precision fp32 \
  --threads 2 --native-threads 1 --budget-seconds 3600 --output NEW_OUTPUT
```

The recorded stack is Python 3.13.5, NumPy 2.5.4, SciPy 1.18.1 and PyTorch
2.8.0+cpu. Substitute only applicable source-bound anchors; a different stack
is not an automatic transfer. The recorded report contains the concrete local
paths and baseline revision.

`preservation.json` lists exact uncompressed and compressed hashes for sixty
raw records. `measured-source.tar.gz` preserves 95 measured source/protocol
members. Native binary hashes are recorded; binary bytes and generated I/Q
fixtures are omitted. Later reporting/coverage fixes belong to the separate
final-code snapshot and must not be relabeled as this measured source.

Run `python verify.py` from this directory to verify bytes without a receiver
execution. `python reclassify.py` applies the current wrapper's reporting rules
to the archived timings and writes `classification-replay.json`. The retained
replay marks warm decisions complete and full scope incomplete. It deliberately
retains the original coverage receipts; the separately reviewed fresh coverage
receipts are documented in the parent record. Neither command performs a new
timing experiment or upgrades a failed prediction model.
