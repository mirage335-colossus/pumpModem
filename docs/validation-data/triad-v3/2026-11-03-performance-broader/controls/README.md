# Normal and CI decision-policy controls

These controls exercise the maintained decision helper using simulated timings.
They do not measure receiver throughput, qualify an abbreviation model, or
establish a future hardware error rate. The earlier
[strict-policy evidence](../../2026-11-03-performance-decision/controls/README.md)
is unchanged; `--regression-profile strict` remains the control program's
default.

| Profile | Allowed runtime ratio | Pass-direction alpha | Failure-direction alpha |
|---|---:|---:|---:|
| Normal | 1.30 | 0.15 | 0.05 |
| CI | 1.70 | 0.30 | 0.05 |

Each direction divides its own error budget across the four fixed looks at
5, 9, 17 and 33 paired observations. The actual `summarize` helper runs at every
visited look. Sampling stops on its first pass or regression decision, or
inconclusive at 33. There is no additional family-multiplicity adjustment and
no requirement to meet the legacy runtime-precision target before stopping.

## Frozen experiment and assumptions

Each profile has 1,000 independent replications at each of five runtime ratios,
for **5,000 replications per profile**. Both use stationary paired lognormal
runtimes with log standard deviation 0.04 and within-pair correlation 0.6.
Pairs are independent across time and across replications. The estimand is the
geometric mean of paired complete-runtime ratios; no abbreviation-model error
is injected. Serial dependence, nonstationary load, heavy tails, and model error
can change the observed behavior. Empirical replay was not run in this study.

The program wrote each plan before sampling and checked source identities after
each completed ratio. The raw plans were not edited after the run:

| Profile | Frozen plan | Complete raw result | Initial seed | Control-computation wall time |
|---|---|---|---:|---:|
| Normal | [plan](decision-policy-normal-1000.plan.json) | [result](decision-policy-normal-1000.json) | 2913041 | 6.298 s |
| CI | [plan](decision-policy-ci-1000.plan.json) | [result](decision-policy-ci-1000.json) | 3913041 | 5.585 s |

Each subsequent ratio uses the initial seed plus 1,000 times its zero-based
index. Both runs used CPU cores 6 and 8 and exited successfully. These wall
times measure decision simulations, not full modem workloads or routine-suite
duration. [preservation.json](preservation.json) retains the original commands,
hashes, source-guard results, and original plan-file timestamps. The timestamps
are filesystem modification metadata, not an independent trusted timestamp;
the frozen JSON itself has no timestamp field.

## Results and uncertainty

Every row below has 1,000 trials. A pass at the exact allowed threshold is a
**boundary pass**, not a false pass. A regression at or below the threshold is
a false regression. A pass strictly above it is a false pass.

| Profile | True ratio | Pass | Regression | Inconclusive | Mean paired observations |
|---|---:|---:|---:|---:|---:|
| Normal | 1.00 | 1000 | 0 | 0 | 5.000 |
| Normal | 1.29 | 384 | 3 | 613 | 27.448 |
| Normal | 1.30 | 105 boundary | 38 | 857 | 30.236 |
| Normal | 1.31 | 20 | 209 | 771 | 29.852 |
| Normal | 1.60 | 0 | 1000 | 0 | 5.000 |
| CI | 1.00 | 1000 | 0 | 0 | 5.000 |
| CI | 1.69 | 426 | 12 | 562 | 25.728 |
| CI | 1.70 | 182 boundary | 39 | 779 | 28.916 |
| CI | 1.71 | 61 | 139 | 800 | 29.872 |
| CI | 2.00 | 0 | 1000 | 0 | 5.000 |

The pointwise exact 95% Clopper–Pearson intervals include:

| Profile and event | Observed rate | 95% interval |
|---|---:|---:|
| Normal false regression at 1.29 | 0.3% | 0.062–0.874% |
| Normal false regression at 1.30 | 3.8% | 2.703–5.179% |
| Normal false pass at 1.31 | 2.0% | 1.226–3.072% |
| CI false regression at 1.69 | 1.2% | 0.622–2.087% |
| CI false regression at 1.70 | 3.9% | 2.788–5.293% |
| CI false pass at 1.71 | 6.1% | 4.698–7.767% |

Both boundary false-regression upper endpoints exceed 5%. These finite
simulations alone therefore do not establish a failure rate at most 5%, and
provide no universal future guarantee. The directional error allocation has
its stated justification under independent normal log-ratios and the fixed
look schedule. The reported Monte Carlo intervals are pointwise across
independent replications, not simultaneous across profiles, ratios or events.
Zero events in 1,000 trials has a two-sided 95% upper endpoint of 0.368%.

Both profiles quickly classify unchanged and clearly regressed cases. Most
cases one percentage point either side of their threshold remain inconclusive
under this generator. The looser pass budgets do not remove this practical
limit. [summary.json](summary.json) and the raw reports retain all interval
endpoints and stopping counts, including the independent timing-precision
diagnostic.

## Separate scope controls and tests

Deterministic synthetic stage costs are passed to actual decision helpers:

- Outside-receiver cost produces a complete-runtime regression while a
  receiver-only timer incorrectly passes. The injected fraction is 60% for
  normal and 100% for CI, so each complete runtime exceeds its own threshold.
- An unchanged abbreviation passes despite a full-target ratio of 1.60 or
  2.00; the separate full endpoint blocks model qualification.
- A changed regime key is rejected by the actual applicability validator.
- A common environment slowdown cancels from paired source comparison, while
  a separate synthetic hardware profile triggers a raw hardware warning.

These scope controls are neither real receiver slowdown injections nor real
hardware measurements. Twelve focused unit tests passed in 0.173 seconds,
including profile resolution, actual helper decisions, CLI plan contents,
strict-default compatibility and the distinct CI threshold. Unit tests do not
substitute for the Monte Carlo evidence or full-workload validation.

## Reproduction

Use compatible NumPy and SciPy versions recorded in each plan and matching
source files. Output paths and their `.plan.json` files must be fresh:

```sh
python tools/triad_perf_decision_policy_controls.py \
  --output local-evidence/controls-normal-new.json --trials 1000 \
  --regression-profile normal --seed 2913041
python tools/triad_perf_decision_policy_controls.py \
  --output local-evidence/controls-ci-new.json --trials 1000 \
  --regression-profile ci --seed 3913041
python -m unittest discover -s tests -p test_triad_perf_decision_policy_controls.py
```

[matching-source-and-test.tar.gz](matching-source-and-test.tar.gz) contains all
five sources declared by both plans and the test snapshot. All declared hashes
matched during both runs and at preservation. This compact archive is not a
complete repository or dependency lock. The raw logs are empty because the
program writes results to JSON. [SHA256SUMS](SHA256SUMS) covers all other files:

```sh
cd docs/validation-data/triad-v3/2026-11-03-performance-broader/controls
sha256sum -c SHA256SUMS
```
