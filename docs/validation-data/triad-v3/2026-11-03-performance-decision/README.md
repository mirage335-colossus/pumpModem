# Per-test confidence stopping

This follow-up changes the performance measurement policy, not the modem DSP,
waveform, native binaries or workload definitions. It supersedes the routine
5% **runtime half-width** stopping target. The earlier
[full-suite evidence](../2026-11-03-performance-complete/README.md), including
failed models and unresolved coverage/qualification boundaries, remains intact.

## Decision and reuse contract

Each routine test uses a one-sided 95% confidence procedure to rule out more
than 30% slowdown in its declared full-workload mean-runtime estimand. For
paired native/FFT/family measurements the estimand remains a geometric mean
ratio; staged arithmetic means remain additive. The fixed looks are 5, 9,
17 and 33. Each direction spends 0.05 across those looks; unrelated test cases
and absolute-runtime diagnostics do not consume that per-test error budget.
The displayed lower/upper pair is not a joint 95% two-sided interval, nor a
95% simultaneous guarantee for the whole suite. The assumptions remain
independent, stationary, approximately normal outer log timings (arithmetic
stage means use their stated Student-t component assumptions).

Stop when the slowdown decision is supported. Absolute-runtime width remains
a diagnostic; it cannot demand repetitions after the decision resolves.
Conversely, a narrow interval that overlaps the decision boundary does not
justify a pass. A resource/sample stop remains inconclusive. There is no
retry-until-pass rule.

Modeled routines still need applicable independently qualified history and a
fresh full endpoint before an accepted decision. Frozen historical engineering
allowances are separate from statistical timing confidence; they are not
proven future prediction-error bounds. If the final frozen interval cannot
support a decision, or its uncertainty makes the endpoint guard impossible,
skip that non-decisive endpoint and leave the result unqualified. A full-only
slowdown still has a chance to falsify every would-be modeled pass.

Historical qualification keeps its original precision/model-equivalence
stopping rules and raw proof replay. It is not retroactively promoted. The
identical-FFT route permits an independently reviewed exact four-file hash
projection for inference/reporting changes, with 22 unchanged measurement and
proof bodies plus all other DSP/runtime/input/control/geometry identities.
The original anchor is preserved. The current/reference implementations must
still match before deduplication; no paired observations are invented.

## Focused decision evidence

The harness checks passed: 294 tests in the initial 295-test discovery, with
the one environment-gated native-fixture test then run explicitly and passing.
The final paired/staged endpoint-skip delta has its separate focused checks.
These are functional tests, not runtime-prediction qualification.

The [5,000-trial decision simulation](controls/README.md) calls the maintained
fixed-look gate. With stationary lognormal jitter, log standard deviation
0.04 and paired correlation 0.6, all 1,000 unchanged cases passed at five
observations and all 1,000 60%-slower cases failed at five. Only 11 unchanged
cases met the obsolete 5% timing-width diagnostic. At a true 31% slowdown,
8/1,000 falsely passed (pointwise 95% exact interval 0.346–1.570%). Cases at
29%, 30% and 31% often remain inconclusive; 33 observations do not reliably
distinguish a one-percentage-point difference near the threshold. These are
conditional simulation frequencies, not future CI error guarantees.

Offline replay of the preceding CPU campaign covers 20 directly observed
paired full-workload cases. Nineteen pass at look 5 and grouped-multitile at
look 9. The sum of retained operation timers falls from 2,350.324 to 436.485
seconds, a reduction of 1,913.840 seconds (81.43%). Different timer scopes
exclude different setup/orchestration costs; this is a replay calculation,
not measured campaign acceleration. The two modeled cases and unpaired
free-crystal comparison are excluded from that paired replay.

## Prospective complete CPU run

One run on the same Ryzen 5 PRO 5650U, affinity 2,4, two numerical threads per Python worker
and one native thread took **741.035 seconds (12 minutes 21 seconds)**. All
23 warm-workload gates passed, as did all 16 fresh functional commands. The
source/dependency-matched native and Python coverage receipts remain applicable.
The hardware screen observed no flagged degradation; its single five-second
blocks are not independent-replicate confidence tests. No case was retried.

| Serialized work | Previous width-driven run | Decision-driven run |
|---|---:|---:|
| Functional commands | 55.817 s | 47.193 s |
| Native/FFT component, including hardware screens | 699.499 s | 214.905 s |
| Separate identical-source free-crystal check | 120.954 s | 39.215 s |
| Nine sampled receiver families | 2,121.931 s | 439.072 s |
| Complete wall time, including orchestration | 2,999.858 s | 741.035 s |

Observed complete-run wall time decreased by 2,258.823 seconds, **75.30%**.
These are two measured campaigns on the same allocation and workloads, not
a confidence-qualified bound on future speedup. The older policy demanded
additional width precision and stronger cross-case adjustment; the new policy
implements the user's per-test decision requirement. Neither campaign is a
matched all-direct comparison establishing the cost of abbreviation itself.

All nine sampled families used five full paired observations. The native VLF
forecast and narrow-HF FFT case needed nine; the other legacy cases stopped at
five. The free-crystal route used five current forecasts and its required full
endpoint, with an engineering-adjusted upper absolute slowdown bound of 1.063.
The 22 paired cases missed the old diagnostic width target; the separate
identical route did not request that diagnostic. Neither condition vetoes the
new supported decisions.

This run exceeds the ten-minute engineering target by 141.035 seconds. The
439.072-second sampled component is now the largest cost, particularly CFO
batch/tail (92.344 s) and phase-optimized (87.962 s). CPU models that previously
failed independent validation are still not used to claim cheaper predictions.
An independently checked [three-pair shadow](n3-shadow/README.md) omitted
87.099 seconds of current operation timers, below its predeclared adoption
criterion. It is not a new wall-time measurement and the default stays at five.

The wrapper correctly exits **2 / incomplete** for the broader requested scope,
not for a measured slowdown or a diagnostic width miss. Remaining blockers are:

- Unqualified cold startup/loading/first-use prediction.
- Missing standalone planner/statistics timing representation.
- No qualified forecast across a representative resource-regime boundary.
- No independently validated mixed-family campaign-throughput forecast.

Warm target decisions are complete for this CPU manifest; entire-study or
release qualification is not established. The [routine archive](routine/README.md)
preserves the raw run, exact source identities, test logs and reproduction command.

## Reproduction and limitations

Use the [maintained guide](../../../triad-performance.md) and
[coverage matrix](../../../triad-performance-matrix.md). Routine defaults to
`--stopping-rule decision`; `--stopping-rule precision` is an explicit
diagnostic/qualification choice. Do not requalify unaffected models merely
because the routine decision resolved with a runtime interval wider than 5%.
Do not reuse an old forecast across changed timed code, arithmetic, geometry,
memory regime or environment without its applicability checks.

No new GPU kernel, precision or DSP work is introduced. Existing H200 evidence
is retained; the decision-policy simulations and CPU run are not new GPU
performance measurements. Ordinary Robust receiver integration and regression
testing remain outside this study.
