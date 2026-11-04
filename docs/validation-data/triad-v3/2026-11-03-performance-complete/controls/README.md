# Performance-decision controls

These records test decision logic under declared timing distributions and verify
one real adapter's enclosing timer. They **do not establish the future false-failure
rate of CI**, receiver sensitivity, hardware slowdown, or complete suite coverage.

The [summary](summary.json) preserves high-level outcomes. The complete measured
control outputs are [64-campaign matrix](decision-empirical-002.json.gz) and
[512-campaign null checks](decision-null-512.json.gz). Their unchanged pre-run
plans and progress logs are alongside them. [Preservation metadata](preservation.json)
contains compressed and uncompressed hashes; `SHA256SUMS` covers the bundle.

## Decision simulations and uncertainty

The matrix completed 36 controls, each with 64 independently seeded campaigns of
14 cases, in **60.775 seconds**. Null-only direct and conditional controls each
used 512 campaigns and completed in **26.889 seconds**. These are computation
costs of the decision simulation, not predicted full receiver times.

Every scenario invokes the maintained adaptive/conditional decision helpers.
Timing inputs are paired outer full-runtime forecasts or direct measurements
from CPU003, separately centered in log space. The block generator resamples
non-overlapping adjacent pairs, preserving short ordering while discarding the
unmatched tail. Inner kernel calls are not independent observations.

The following counts mean **at least one case in the 14-case campaign**. Event
categories can overlap because different cases can have different outcomes.
Intervals are pointwise 95% Clopper–Pearson intervals across simulated campaigns;
they are not simultaneous across table rows or predictions of future hardware.

| Scenario | Imposed factor | Blocking performance failures | Pointwise interval |
|---|---:|---:|---:|
| Direct measurements | 1.00, 1.20, 1.29 separately | 0/64 each | 0–5.60% |
| Direct measurements | 1.31 | 10/64 | 7.76–26.86% |
| Direct measurements | 1.40, 1.60 separately | 64/64 each | 94.40–100% |
| Direct null, larger check | 1.00 | 0/512 | 0–0.718% |
| Conditional null, larger check | 1.00 | 0/512 | 0–0.718% |

The 1.31× direct scenario still had an inconclusive case in every campaign.
Conditional-mode scenarios had at least one unresolved applicability check in
all 64 matrix campaigns, including factor 1.00; this remained 512/512 in the
larger null check. At factor 1.60 they could both detect a performance regression
in one case and leave another case unresolved. Inconclusive is not a pass.

A slowdown present only at the full endpoint left abbreviated estimates
unchanged. The separate **blocking calibration guard**, not a confirmed mean
performance regression, triggered in 10/64 campaigns at factor 1.20, 23/64 at
1.29, 27/64 at 1.31, 61/64 at 1.40 and 64/64 at 1.60. These results expose the
engineering guard's limitations: jitter can block a forecast even when the
imposed full-only slowdown is below 30%. The 512 null conditional campaigns had
0 blocking calibration failures, with the same 0–0.718% pointwise interval.

Important limits:

- Each mode intentionally applies one decision policy to all 14 timing families.
  This is **not a replay of CPU003's actual mixed direct/conditional dispatch**.
  For example, the factor-1.20 full-only calibration blocks arose in native-null,
  native-OU, broad-HF and the Gaussian helper; only broad-HF actually used a
  conditional model in CPU003.
- Conditional history is an idealized zero-bias fixture with the existing 10%
  floor. It is not a replacement for measured full-workload qualification.
  Endpoint jitter uses the same selected empirical timing generator; its
  distribution is **not independently calibrated from actual full endpoints**.
- Resampling is conditional on the finite CPU003 observations. It does not
  account for uncertainty in the underlying jitter distribution or future
  environment changes. Blocks deliberately introduce serial dependence, so
  this is a robustness diagnostic, not validation of independent Student-t
  assumptions.
- Outside-score controls add delay to nominal complete-pipeline time. Their
  exact geometric-mean shift differs slightly from the nominal additive factor
  in noisy cases. Common environmental slowdown cancels in paired timing;
  separately invoked raw hardware-health screens warn above their threshold.
  Those hardware profiles are synthetic and have no confidence-qualified
  hardware false-failure rate.
- Changed-regime controls call the actual conditional applicability validator;
  rejection is distinct from a measured slowdown.

## Real sampled adapter timer check

[Actual result](actual-delay-001/result.json), [frozen plan](actual-delay-001/plan.json),
[reproducer](actual-delay-001/reproduce.py) and
[reproduction receipt](actual-delay-001/reproduction-receipt.json) are preserved,
as are all eight native-exported input files under `actual-delay-001/sampled-stats/`.
The inputs came from the shared native HF channel with an actual noisy
observation, not a synthesized algebraic timing fixture.

The full Gaussian-state adapter processed **39/39 blocks and 4,938 sampled rows**,
including a partial final block. A second execution injected 50 ms after the
adapter returned. Scores, measured work and regime were unchanged. Its enclosing
timer measured **71.817 ms**, versus **21.575 ms** inside the adapter, leaving
**50.241 ms** outside the receiver timer. The sampled export took 83.975 ms.

This verifies that the enclosing boundary captures the injected external cost.
One execution pair does not qualify regression-detection power, a mean runtime
change, hardware utilization, or cache/thermal behavior. The delay is explicitly
reported sleep, not an alleged DSP slowdown.

## Reproduce without another receiver campaign

Run from the repository root with the study's recorded NumPy/SciPy environment.
Use fresh output names. These are optional decision-method qualification checks;
they are not required in every routine receiver run.

```sh
python tools/triad_perf_decision_controls.py \
  --output local-evidence/new-controls.json --campaigns 64 --families 14 \
  --noise empirical-block --budget-seconds 165 \
  --residual-report docs/validation-data/triad-v3/2026-11-03-performance-complete/controls/cpu003-timing-input.json

python tools/triad_perf_decision_controls.py \
  --output local-evidence/new-null-controls.json --campaigns 512 --families 14 \
  --noise empirical-block --budget-seconds 90 --mode direct --mode conditional \
  --factor 1 \
  --residual-report docs/validation-data/triad-v3/2026-11-03-performance-complete/controls/cpu003-timing-input.json
```

The compact timing input retains the **exact original floating-point sample
values**, source identities and original CPU003 report hash, with irrelevant
receiver arrays omitted. Replaying it changes input-file provenance, not the
numerical timing vectors. The archived controls originally read the full CPU003
backend report. `matching-source.tar.gz` preserves the nine exact measured
helper/dependency files, including the historical family adapter source recovered
by hash from an earlier source archive. It is not a freestanding repository or
native binary; use the surrounding source/build evidence and recorded binary
hash when reproducing the actual sampled check.

A previous 512-campaign, 36-control attempt produced only its plan before the
coordinator-reported **300-second external timeout (exit 124)**. Its plan and empty
log are retained as `decision-empirical-001.*`; no completed report exists. The
empty log does not independently prove that exit status. The updated helper now
creates an exclusive initial report, atomically checkpoints every completed
control, and reports pending/incomplete work. Wall-budget-stopped partial
controls do not claim nominal fixed-sample binomial coverage. The failed attempt
was not relabeled as a successful run or used to narrow the reported intervals.
