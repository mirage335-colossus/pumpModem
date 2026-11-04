# Fast three-carrier performance guard: decision controls and validation

The new `triad_perf_guard.py` separates a bounded regression guard from the
existing prediction/full-scope qualification campaign. CI prioritizes rare false
blocks and accepts missed regressions; GPU development exercises the applicable
extra GPU families at a lower slowdown threshold. Ordinary Robust integration
is outside this record.

**Status:** the final CPU guard completed in **291.060 seconds** and the GPU
guard in **236.635 seconds**, covering all 33/36 declared workload screens with
no correctness failure or confirmed performance rejection. These are external
command wall times, not entire hosted-workflow times or affirmative mean-runtime
qualification. Both results retain limited assurance. **Serial-dependence
controls still produce severe false-block rates**, so the nominal statistical
budgets are conditional, not unconditional machine guarantees.

The [maintained guide](../../../triad-performance.md) defines commands and timing
boundaries; the [coverage matrix](../../../triad-performance-matrix.md) defines
33 CPU or 36 CUDA workload hypotheses. Existing qualification records and
unresolved holds are unchanged. Hosted GitHub Actions has not been run.

## Actual CPU and GPU guards

| Measurement | CPU `cpu-004` | GPU `gpu-003` |
| --- | ---: | ---: |
| Hardware | Ryzen 5 PRO 5650U, declared local allocation | RTX PRO 6000 Blackwell Workstation, 96 GB |
| External command wall | 291.060227 s | 236.634937 s |
| Internal reported wall | 288.473208 s | 236.081032 s |
| Full-workload screens | 33/33 | 36/36 |
| Functional command receipts | 18 passed | 18 passed |
| Functional elapsed | 54.961 s | 37.864 s |
| Fresh confirmation pairs | 0 | 25 |
| Hardware probes | 10 completed, 50.395 s | 11 completed; 2 FP8 unsupported, 55.705 s |
| Functional/source/work errors | 0 | 0 |
| Confirmed performance rejections | 0 | 0 |

The CPU screen nominated no cases. The GPU screen nominated five: native FHSS,
null, static FFT, ray-frequency and resource-sequential. Their 25 fresh pairs
ended in valid non-rejection/futility, with no missing confirmation. FHSS used
13 pairs with median ratio 1.3181 and range 1.2691–1.3670; each other nominee
used three pairs, with medians 1.1444–1.1817. No run was retried to obtain a
preferred statistical decision. Non-rejection is not proof of an acceptable
arithmetic mean or a narrow uncertainty interval.

All five FFT cases selected eligible FP32 on CPU. GPU `broad-hf-timing`
selected eligible FP64; the other four, including `free-crystal`, selected FP32. The GPU hardware probe
recorded mean device busy 96.94% over 278 samples and memory busy 8.76%; these
are system-wide activity measures, not SM occupancy or tensor-instruction
utilization. CPU GEMM used about 1.99 cores and FFT 1.86–1.88; several other
probes used roughly one core. Low-precision instruction dispatch remains
unverified where the hardware report says so. Neither run supplied a matching
hardware baseline, so neither establishes a hardware-health pass.

Each run supplied 18 functional receipts: the native inventory and 17 selected
functional commands. The separate complete harness unit discovery ran 472 tests
in 18.255 seconds: 469 passed and three optional environment-dependent checks
were skipped. Actual guard workloads exercised the associated planner/export
work. This extra unit stage plus dependency installation and build are outside
the 291-second CPU guard command. The complete hosted performance workflow has
not been timed; no under-five-minute hosted-workflow claim follows.

The original guard reports correctly record omitted native/Python coverage
receipts. A separate file-only [coverage applicability replay](coverage-reuse.json)
subsequently validated unchanged native and Python DSP instrumentation against
457 unique current source/dependency files, without rewriting either report.
This is reusable historical coverage, not fresh instrumentation. New guard
instrumentation ran 83 focused tests in 6.36 seconds plus one actual native-null
CLI check, reaching 1,407/1,662 lines (84.657%) and 494/670 branches (73.731%).
The [coverage receipt](guard-coverage-receipt.json) and [raw coverage](guard-coverage.json)
retain the partial coverage; they do not establish every data-dependent or CUDA
path. Instrumented execution is excluded from performance timings.

Two initial integrations remain preserved. CPU `cpu-003` took 292.339 seconds
and failed resource comparison because the historical reference configuration
lacked the new counter fields. The reviewed adapter now obtains the expected
calendar through the already-qualified direct route and requires strict actual
work from both sides. GPU `gpu-002` took 233.096 seconds and stopped after a
numerical-library inventory order mismatch in RBPF, leaving seven screens
unmeasured; scores were bit-identical. The narrow repair canonicalized that
unordered inventory and retained its contents and other identity checks. The
subsequent full runs validate these integration repairs, not statistical retries.

Five injected actual-pipeline controls completed in 18.482 seconds. Neutral and
common-environment slowdowns had ratios 1.0048 and 1.0023 and did not reject.
A general slowdown and a full-duration-only slowdown each confirmed after twelve
pairs, with median ratios 2.3456 and 2.5326; the short eight-second probe did not
receive the full-duration delay. Changed work failed the functional integrity
check. These demonstrate the named controls, not all possible regressions.

## Cost and reuse

The earlier qualification-oriented CPU/GPU routines took 1,684.791 and
1,170.758 seconds and remained incomplete. The new 291.060/236.635-second guards
are substantially cheaper **under a different assurance objective**: they do not
repeat cold/sustained qualification or prove affirmative non-regression. This
is not a same-assurance savings estimate or GPU-versus-CPU acceleration ratio.

In the earlier sampled-family routines, five retained pairs plus one discarded
pair per case produced 108 CPU receiver calls/132 exports and 144 GPU calls/168
exports. The new no-confirmation screens use 18 CPU or 24 GPU receiver calls and
12 exports. CPU screen generation/receiver time was 21.158/28.329 seconds; GPU
was 11.083/17.881 seconds. Shared generation avoids an estimated 27.882 CPU or
27.417 GPU seconds of repeated generation using the measured stage sums; these
are additive accounting estimates, not matched independent savings campaigns.
GPU confirmations added 12 exports and 12 sampled-family receiver calls.

Earlier estimates of a shared one-pass family screen were 50.955 CPU and 27.379
GPU seconds, excluding startup, JIT and verification. They were planning
inferences, not full-suite forecasts. Persistent workers and fixture reuse avoid
repeated setup without changing the declared full receiver geometry. Direct
measurement remains preferable where an abbreviation costs more than the work.
The historical native/FFT forecasts and their limits remain separately retained.

Temporary GPU compute was deleted after retrieving the evidence. No provider
account or billing artifacts are included in this scientific record.

## Decision and scope

One broad screen per available named workload nominates suspects. Only fresh
full-target paired observations can confirm a performance rejection. The plan
freezes source/work identities, the complete hypothesis family, job count,
randomized execution order and confirmation looks before observing timings.
Correctness, numerical/work parity and source integrity remain separate hard
checks. Missing or inconclusive timing, engineering model warnings, unavailable
coverage and one-block hardware warnings remain explicit limited assurance.

The blocking null is `P(candidate_seconds > threshold × reference_seconds) <=
0.5`. This is the median of paired full-runtime ratios, **not** a ratio of
marginal medians or a bound on the arithmetic/geometric mean runtime. The old
mean-runtime forecasts and qualification procedures retain their rules.

| Profile | Ratio threshold | Workflow rejection budget | Usual fresh confirmation looks |
| --- | ---: | ---: | --- |
| CPU CI | 1.70 | 1% | 12 pairs |
| GPU development | 1.30 | 5% | 12 and 20 pairs |

The exact one-sided binomial/sign test counts ties as nonpositive. Each look
receives `alpha_suite / (hypotheses × jobs × looks)`. The minimum sample count
increases when needed for an attainable exact tail probability. Exact futility
stopping abandons confirmation only when no permitted remaining look could
reject even with all future signs positive. It does not produce a supported
non-regression conclusion or reuse the abandoned alpha.

The nominal workflow allowance assumes independent confirmation-pair signs with
probability at most one half under the null. Different hypotheses may be
dependent. Randomized order and fresh inputs mitigate some timing effects but
do not prove pair independence; persistent source-session regimes can violate
it severely, as measured below. No unconditional real-machine false-block bound
has been established.

The default 300-second CI and 600-second development budgets are not power
guarantees. An expensive case can nominate even a severe slowdown and remain
unconfirmed because twelve or twenty fresh full pairs cannot fit. Reports retain
that nomination and missing confirmation as limited assurance. There is no
censored-timeout inference. An explicit `--budget-seconds 1500` can be appropriate
when the extra confirmation cost is justified.

## Synthetic statistical controls

Two frozen median-policy campaigns evaluated 38 and 16 scenarios, each with
5,000 workflows. They took 12.598 and 3.384 seconds of local computation. The
vectorized policy simulation matched 216 executions through the actual report
aggregator, including confirmation/futility pair counts. These are decision
model checks, not CPU/GPU receiver timings. The synthetic inventories used 36 CI
and 39 development hypotheses, with one- and two-job cases; real runs derive
counts from their frozen inventory.

The intervals in this table are **per-scenario two-sided 95% exact binomial
intervals**. Raw records additionally contain simultaneous Bonferroni-adjusted
intervals across each campaign's scenarios. No interval covers arbitrary future
hardware or an untested timing distribution.

| Scenario | Observed outcome / 5,000 | Rate and 95% interval |
| --- | ---: | --- |
| CI unchanged independent Gaussian scheduling | 0 false blocks | 0%; upper 0.07375% |
| CI unchanged independent bimodal scheduling | 0 false blocks | 0%; upper 0.07375% |
| CI boundary symmetric near-Rademacher, independent cases | 50 false blocks | 1.00% [0.743%, 1.316%] |
| Same boundary, latent cross-case correlation 0.8 | 31 false blocks | 0.62% [0.422%, 0.879%] |
| CI boundary independent lognormal/heavy-lognormal scheduling | 22–25 false blocks | 0.44–0.50%; separate scenario intervals retained |
| One CI case at 1.71×, remaining cases unchanged | 7 true detections | 0.14% [0.0563%, 0.2882%]; 99.86% missed |
| One CI case at 2× or 3×, remaining cases unchanged | 5,000 true detections each | 100%; lower 99.926% |
| One development case at 1.6×, remaining cases unchanged | 5,000 true detections | 100%; lower 99.926% |
| **Unchanged but serially persistent bimodal source sessions**, independent cases | **4,999 false blocks** | **99.98% [99.889%, 99.9995%]** |
| Same serial violation, latent cross-case correlation 0.8 | **3,495 false blocks** | **69.90% [68.607%, 71.169%]** |

The last two rows deliberately violate pair independence while preserving equal
source runtime laws. They show why a nominal 1% allowance must not be described
as a validated unconditional CI false-failure rate. More repetitions within the
same persistent regime do not fix the violation. These controls remain visible
limitations rather than being dropped from acceptance evidence.

The initial five-pair Student-t proposal was rejected after a symmetric bimodal
mean-null control falsely blocked 3,442/5,000 workflows (68.84%), or 21.32% with
latent cross-case correlation 0.8. Its source and raw controls are retained. The
exact sign decision removes that normality assumption. A different rare-fast
bimodal example has a null arithmetic mean but a true median alternative with
90% probability above threshold; its median detections are not falsely counted
as median-null failures. Changing the estimand is explicit, not retrospective
promotion of the rejected mean test.

Near-threshold misses are intentional evidence of the tradeoff. The strong
2×/3× synthetic detection results do not guarantee detection in physical runs,
under omitted/full-size regimes, or when a confirmation cannot fit the budget.
The controls do not synthesize a hardware-health certificate.

## Frozen evidence identities

The [artifact index](artifacts.json) binds 14 compact files (6,456,463 bytes)
and the six runtime modules. [Decision controls](decision-controls.tar.gz)
retain the JSON files and frozen source snapshots listed below. SHA-256 binds
content, not the truth of the experiment or statistical assumptions.

| File | SHA-256 |
| --- | --- |
| `median-policy-001.json` | `35077992878575816db838dc4b7b6e4f960238171df7c84d2ba168268d059520` |
| `median-policy-002.json` | `16cab022fdc4a46f32061c0cde2059b55571f1520f464edc6d67506531a7432a` |
| Rejected `current-policy-001.json` | `6508c0cca4e7e3152a8ae390c26a91e5b4d9f4623dd96c7cb3380db97621d4d5` |
| Accepted `triad_perf_guard_policy.py` | `156a65af2b92c2b6a0f17da7b54f99a5378a13a5a6c26b413693ddb551ffa84b` |
| Accepted `triad_perf_guard_controls.py` | `6b1c12a01452d98315593c05f4f88b9a82a431c1bd24ae687b85247204657079` |

The physical archives retain commands, source/build/input identities, frozen
plans, measurements, lifecycle costs and original reports:

- [CPU initial integration](cpu-003.tar.gz) and [final CPU guard](cpu-004.tar.gz).
- [GPU initial integration](gpu-002.tar.gz) and [final GPU guard](gpu-003.tar.gz).
- [Injected actual-pipeline controls](injected-controls.tar.gz).
- [Initial guard sources](initial-guard-sources.tar.gz), [unit output](all-harness-tests.log),
  [external timer](time-command.py), and the coverage receipts above.

The frozen source directories preserve the accepted median implementation and
the rejected mean proposal. The control module retains per-scenario seeds,
plans, nominal/null classifications, raw event counts, uncertainty, and actual
aggregator parity. Policy tests passed 18 checks; controls tests passed five.
These unit results are not substitute receiver or prediction evidence.

Reproduce the decision simulations on the matching source without rerunning
receiver qualification:

```sh
python3 tools/triad_perf_guard_controls.py --campaign base --trials 5000 \
  --parity-workflows 4 --ci-hypotheses 36 --dev-hypotheses 39 --seed 214231 \
  --output local-evidence/triad-performance-guard/controls/median-base-repeat.json
python3 tools/triad_perf_guard_controls.py --campaign extensions --trials 5000 \
  --parity-workflows 4 --ci-hypotheses 36 --dev-hypotheses 39 --seed 224321 \
  --output local-evidence/triad-performance-guard/controls/median-extra-repeat.json
```

Use the [routine commands](../../../triad-performance.md#fast-guard-commands-and-decision-contract)
for physical guards, and [focused qualification](../../../triad-performance.md#focused-prediction-and-qualification-commands)
only when a changed computational relation needs new full-run evidence. Historical
cold/sustained campaigns remain qualification records, not routine prerequisites.
