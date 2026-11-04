# Broader isolated modem performance qualification

This record extends the [earlier decision-policy evidence](../2026-11-03-performance-decision/README.md).
This record includes completed CPU and Blackwell GPU routine campaigns,
independent full-workload qualification, and focused follow-up evidence.
**Broader release qualification remains held**, for the specific outcomes
below. A successful component does not establish complete routine qualification.
Ordinary Robust integration and its regression campaign are excluded.

Compact source-bound evidence is retained by purpose:

- [Qualification, planner, resource and lifecycle evidence](scientific/README.md).
- [Original CPU complete routine and separate derived classification](scientific-cpu-complete-001/README.md).
- [Original Blackwell complete routine](scientific-gpu-complete-002/README.md).
- [Blackwell identical-FFT qualification and matched cost experiment](scientific-blackwell-identical-cost-001/README.md).
- [Separate FHSS CI assessment from retained normal measurements](scientific-fhss-derived-ci-001/README.md).
- [Negative native VLF cost experiment](scientific-savings-native-001/README.md).
- [Focused original-cache FFT follow-ups on CPU and GPU](scientific-focused-broad-001/README.md).

These indexes retain raw measurements, measured source versions, binary/input
identities and checksums. Exact historical source recovered from a retained
patch is explicitly labeled reconstructed and must match its recorded SHA-256;
it is not presented as an original snapshot. Unavailable original invocations
or source versions remain identified provenance gaps.

## Decision policy

The requested confidence is a directional decision-error budget, not a target
runtime interval width. Normal uses a 15% erroneous-pass budget per test at a
1.30 runtime ratio; explicit CI uses 30% at 1.70. Both retain a separate 5%
false-regression budget. Fixed-look spending includes all permitted looks.
There is no simultaneous confidence claim across the entire suite. Historical
prediction qualifications retain their original statistical rules and source
identities. Engineering model allowances are not future-error guarantees.

The [actual-helper decision controls](controls/README.md) retain 5,000
simulations per new profile, including near-threshold cases. Just-above-threshold
false-pass rates were 2.0% normal and 6.1% CI under the declared stationary
lognormal experiment. Boundary false-failure confidence intervals extend
slightly above 5%; this finite campaign is not a universal guarantee for machine
timing. Inconclusive decisions remain distinct from confirmed regressions.

## Completed evidence

- **Planner/statistics:** eight direct full-target workloads completed in
  42.635 seconds on the declared two-core local allocation. All passed both
  complete-process and nested computation gates. The same observations support
  both gates, avoiding additional executions. Six native bundles cover finite
  grids, the 2,000,001-point exhaustion cap, calendar/resource exits, logarithmic
  format geometry and Gamma exact/fallback paths. Two Gaussian design-bound
  jobs cover a retained 39-block, two-candidate, one-identity-pair workspace.
  Larger Gaussian banks and duration scaling are not extrapolated from them.
- **Native instrumentation:** the [fresh atomic capture](coverage/native/native-coverage-001/README.md)
  rebuilt nine counter translation units after its guard. Four focused CTests,
  five CLI modes and seven collector tests passed. All 413 declared dependency
  hashes matched; no negative CFG counters occurred. These are source/CFG
  observations, not semantic completeness or hardware measurements.
- **Python instrumentation:** a new 17-file capture includes the previously
  omitted Gaussian design-bound implementation. All selected tests passed in
  63.194 seconds. The receipt binds 43 declared dependencies. NumPy, SciPy,
  Torch and CUDA internals remain outside Python arc instrumentation.
- **GPU hardware:** RTX PRO 6000 Blackwell Workstation, driver 595.91.07,
  Torch 2.8.0+cu128. Five-second arithmetic/tensor probes took 57.741 seconds
  overall. Eleven paths completed; both FP8 requests were explicitly unsupported
  because compiled products lacked the required matching dense tensor
  instruction. Unsupported dispatch is not a measured performance pass.
- **GPU-host native/FFT qualification:** 14 declared targets completed in
  392.872 seconds with valid direct/predicted timing representations. Its
  original precision warnings and unresolved FHSS comparison are preserved.
  This qualification uses the original strict policy; it is not a complete
  current routine run. All seven retained H200 staged proofs replayed exactly
  on their matching numerical stack after the reviewed reporting-only changes.
  That replay does not transfer H200 timings to the Blackwell GPU.

## Resource-boundary result and routing

The native grouped-statistics exporter really changes execution at its memory
threshold: the tested 5-MiB budget uses paired channels and one basis synthesis;
4 MiB uses sequential channels and two basis syntheses. Both simulate two
channels and retain identical sufficient statistics. The full-target timing
includes process creation, waveform/channel generation, preparation, output
and process completion.

Attempt 001 rejected an incorrect nominal-duration geometry assumption before
qualification. Requested durations are rounded to modem tiles. Attempt 002
used actual rows but its five frozen training observations were too uncertain;
additional holdouts could not resolve the declared model criterion.

One independently reviewed final protocol used 33 fixed balanced training
replicates at actual 32/96-second-request geometry and independent 64-second
target holds at 9/17/33/65. It spent a total 5% qualification-error budget,
retained the ±10% equivalence band, and completed in 175.835 seconds:

| Regime | Actual/forecast mean ratio interval | Result |
| --- | --- | --- |
| Sequential | [0.924981, 1.021512] | Qualified |
| Paired | [0.895145, 1.029346] | Inconclusive |

No observations were discarded and no tolerance was widened. The paired result
is insufficient equivalence evidence, not a demonstrated slowdown. Further
forecast optimization was stopped. The fresh direct routine measured both complete targets in **14.467 seconds**,
with five paired observations per regime. One-sided upper slowdown-ratio bounds
were 1.1382 and 1.0652, below the normal 1.30 threshold. Actual work counters,
shared budgets, byte parity and source identities passed; independent replay
accepted the receipt. This route costs less than model qualification. Direct
measurement requires no duration extrapolation; it must still verify both
binaries select the intended regimes at the same frozen budgets. Direct
coverage must not relabel the cross-duration model as qualified.

## Cold startup and sustained campaigns

On the RTX PRO 6000 Blackwell Workstation, lifecycle qualification 002 completed
in **882.041 seconds**. Persistent-cache and private-empty-Triton-cache
full-shape graph/tensor startup checks passed separately. The frozen complete
mixed campaign used ten warm cycles across all twelve applicable families;
forecasts were 93.813 seconds for reference and 94.409 seconds for candidate.
Three independent full paired campaigns met the unchanged qualification rule
and retained at least 60 seconds of actual warm processing per source. These
are Blackwell measurements, separate from the earlier H200 staged proofs.

CPU qualification 001 completed the cold-process target but ended at its
1,800-second budget. It retained five complete mixed campaign pairs and an
excluded partial sixth attempt. Complete means were 118.439 seconds reference
and 115.984 seconds candidate. The paired actual/frozen-forecast interval
[0.890392, 0.938725] did not fit the original [0.90, 1.10] equivalence band.
Its forecast remains **inconclusive**, not a demonstrated receiver regression.
All ten complete source campaigns retained at least 60 warm seconds. Further
model fitting was stopped; a prospective direct CPU campaign is the selected
routine alternative. It cannot retrospectively qualify this failed prediction.
The partial next look also motivated a prospective budget guard: do not start
another observation when the next permitted decision look cannot fit.

## Complete routine measurements

| Backend | Actual complete routine wall time | Outcomes |
| --- | ---: | --- |
| Ryzen 5 PRO 5650U, two selected cores | 1,684.791 s (28 min 05 s) | Seventeen functional commands and applicable coverage receipts passed; all nine sampled families, planner and resource targets passed. Broad-HF FFT anchor applicability remained unresolved. Hardware and sustained-stability review signals remain separate holds. |
| RTX PRO 6000 Blackwell Workstation | 1,170.758 s (19 min 31 s) | Seventeen functional commands, all twelve sampled families, lifecycle, planner and resource targets passed. Broad-HF/free-crystal FFT anchor applicability and the normal-profile native FHSS decision remained unresolved in the original report. |

Both original reports remain **incomplete** and unchanged. Later component
checks and raw-data reclassification are separately identified; they are not
new complete-suite executions or retroactive edits to the original exit codes.
The CPU run exceeded the 25-minute engineering target by 184.791 seconds. Its
largest enclosing children were lifecycle 755.415 seconds, sampled families
485.296 seconds, native/FFT 263.584 seconds and identical FFT 60.973 seconds;
functional checks took 58.658 seconds, planner 41.932 and resource 15.309.
This is the shortest measured complete broader CPU campaign here, not a proof
that further savings are impossible. The explicit 1,800-second hard budget
allowed it to finish without a timeout. No fresh hosted GitHub Actions run or
public release certification is claimed.

### CPU sustained observation and independent outcomes

The discarded planning pilot froze five complete warm cycles across all nine
families. Three independent full reference/candidate campaign pairs took
104.398/102.029, 105.470/101.014 and 101.309/102.101 seconds. All six warm spans
reached 72.5–75.7 seconds. The full-runtime normal-profile upper ratio bound is
1.032565, below 1.30. This is direct timing, not qualification of the failed
F+6W forecast.

One within-run last/first-quarter ratio was 1.144, above the unchanged 1.10
stability screen. With five cycles, each such quarter contains only one cycle.
The complete outer observations include that variation; the corrected reporting
keeps their mean-runtime comparison separate from the stability review. A
source-bound replay preserves the original erased decision, raw measurements
and original exit 2. Independently valid cold-start evidence is also reported
separately. The stability concern is not relabeled as healthy.

The CPU hardware screen additionally observed ratios 1.356 for stream-copy,
1.314 for complex64 phase work and 1.467 for complex128 phase work against the
retained baseline. These are individual five-second screens, **not statistically
confirmed regressions**. Utilization was high. Paired candidate/reference
timings cannot exclude a slowdown shared by both sources; the environment
review remains a release hold. Repeating until green or replacing the baseline
would not resolve that evidence.

### GPU near-threshold result

The complete native FHSS/selective-modulation/burst case retained 33 full paired
observations. Its normal-profile ratio interval [1.285225, 1.306855] straddles
1.30, so the normal decision remains inconclusive. The retained upper bound is
below the independently predeclared CI threshold 1.70, with a stronger pass
error budget than CI requires. A separate derived assessment therefore supports
the CI decision without another measurement. It does not establish the wall
time of a fresh CI-profile campaign or change the original normal outcome.

## Avoiding unnecessary repeated work

The original FFT applicability problem was traced to regenerated metadata:
the sampled basis, observation and validity files were byte-identical, but
manifests recorded different generation timers and temporary output paths.
Strict manifest hashes remain enforced. Qualification and routine commands now
accept the same explicit retained `--fixture-cache`; they must reuse those
exact artifacts. In broad-HF GPU selection, both formats passed numerical
checks but a 0.26% pilot timing difference selected FP64 instead of the qualified
FP32 format. Routine selection now prefers a currently numerically eligible,
exactly applicable qualified format, using a documented FP32-then-FP64 order.
Qualification still makes its fresh choice. This is a practical stable policy,
not proof of universally optimal arithmetic.

The [focused archive](scientific-focused-broad-001/README.md) also records an
exact-byte assembly of one reusable GPU fixture cache for all five cases. It
combines the original qualified fixtures with the newer free-crystal metadata
required by its strict-identical anchor. All 20 public input files (109.4 MB)
were checked against the selected receipts and anchors. Original caches remain
unchanged; the large arrays stay in ignored local evidence, with compact
metadata, hashes and a file-only reproduction procedure in the archive. This
assembly performed no simulation or receiver work and establishes no new timing
claim.

The independent focused broad-HF follow-ups then **passed on both backends**,
with the exact original cache, eligible qualified FP32 arithmetic, a fresh full
endpoint for each source and unchanged source/fixture/anchor identities:

| Backend | Focused component wall | Model-adjusted full-runtime ratio interval |
| --- | ---: | --- |
| CPU | 206.889 s | [0.882159, 1.148666] |
| Blackwell GPU | 33.772 s | [0.898242, 1.106221] |

Both intervals use the retained engineering model allowance once; this remains
a conditional prediction result, not a proven future-error bound. The CPU
needed all 33 permitted training observations, so this relationship is not
universally inexpensive. These follow-ups close the named applicability issue
without changing the original complete reports. Their costs are **additional**
to those reports: CPU original plus focused follow-up totals 1,891.681 seconds
(31 min 32 s). No complete rerun with the final cache/precision policy has been
measured, and no claim that it meets 25 minutes is made.

The CPU's extra training did **not** chase the old 5% timing-width diagnostic.
Its provisional slowdown upper bound already passed at five observations
(1.27968). The separate historical prediction-readiness guard was still
unsatisfied: at 17 observations, candidate/paired combined timing-and-model
allowances were 20.35%/21.83%, above its unchanged 20% ceiling. At 33 they fell
to 15.02%/16.10%, permitting the independent full endpoint. Training after the
first five added 135.794 seconds of receiver timers. The final result still
has `timing_precision_reached=false` and passes the regression decision.
This explains the measured cost under the existing conditional model contract;
it is not a fundamental minimum or a proven future-error guarantee.

A separate Blackwell **free-crystal strict-identical** qualification took
47.034 seconds and validated its new anchor with independent full holds.
One randomized matched cost pair then measured:

| Route, same full target and normal decision policy | Wall time | Result |
| --- | ---: | --- |
| Direct, five full observations | 61.620 s | Pass, upper ratio 1.007258 |
| Abbreviated, five training observations plus independent full endpoint | 24.258 s | Pass, upper ratio 1.151708 including its model allowance |

Observed savings were 37.362 seconds, or 60.63%. Qualification amortizes after
approximately two such routine uses. The direct arm includes a discarded
full-shape pilot; subtracting it still leaves 52.691 seconds. **One randomized
pair does not establish a confidence-qualified campaign-cost ratio or future
savings guarantee.** The target retains 992 CFO alternatives, 23,876 samples,
47,916 FFT points and 458 lags; it is a resident receiver target, not a complete
radio simulation. H200 evidence was not transferred to Blackwell.

The matched native VLF experiment reached the opposite engineering conclusion:
its first abbreviated arm took 57.82 seconds versus 46.95 seconds direct, and
the second abbreviated arm was model-inconclusive. It stopped without claiming
qualified savings. `--native-direct-case native-vlf-low-cn0` now selects the
same full native workload directly, preserves the historical forecast, and
avoids qualifying or repeatedly validating a model that provided no measured
cost advantage here. Direct resource and planner routes follow the same rule.

Use the [routine and focused qualification commands](../../../triad-performance.md)
and [maintained matrix](../../../triad-performance-matrix.md). Reuse matching
fixtures, build outputs and historical proofs. Changes to measured code, work
regime, arithmetic or environment require the corresponding focused checks;
reviewed reporting-only changes use explicit source projections with protected
measurement bodies. Do not rerun the historical research campaigns merely to
change a reporting policy.

## Remaining qualification holds

- **CPU environment and stability:** retain the measured warnings above. A
  controlled investigation of the affected hardware probes and the warm-cycle
  variation is needed before treating this host as a healthy release reference.
  The direct mean-runtime comparison itself passed; no confirmed regression is
  inferred from the single hardware screens or quarter ratio.
- **Normal GPU FHSS:** the 1.30 decision remains inconclusive. The separately
  supported predeclared CI 1.70 decision does not justify relabeling normal as
  passed. Repeating unchanged trials until one passes is not a remedy.
- **Routine cost and hosted CI:** a repaired complete CPU run within 25 minutes
  is unmeasured, and hosted CI has not been executed. The guide's 35-minute local
  hard cap supplies headroom without changing the 25-minute target or weakening
  gates. Qualification artifacts and source projections do not certify a
  published binary or another machine.

FP8 dispatch rejected by the instruction guard, unmeasured processor
architectures, larger untested state/particle/memory regimes and physical-radio
behavior retain the matrix's explicit limitations. They are not successful
coverage claims and should not trigger an indiscriminate historical campaign.

Cold targets retain the OS page cache. Private compilation tests isolate the
Triton cache, not every system or driver cache. Paired timings can hide an
environmental change that slows both implementations equally; arithmetic
hardware probes do not cover every import or disk slowdown. These limitations
remain separate from functional correctness and prediction validity.
