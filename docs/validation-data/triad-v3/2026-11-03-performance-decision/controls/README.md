# Per-test decision-stopping controls

This is new evidence for the corrected decision policy, separate from the
[earlier precision-stopping controls](../../2026-11-03-performance-complete/controls/README.md).
The experiment tests the maintained decision helper using simulated runtimes.
It does not measure receiver speed or qualify the complete CI dispatch.

The [frozen plan](decision-policy-1000.plan.json) preceded the computation.
The [complete raw result](decision-policy-1000.json) retains all counts,
pointwise exact binomial intervals, stopping looks, runtime versions and source
identities. The 5,000 replications completed in **4.145 seconds** on CPU cores 6
and 8. This elapsed time is for the decision simulation, not for a modem workload.

## Policy and assumptions

- Each individual test uses a 30% allowed slowdown, one-sided alpha 0.05 in
  each decision direction, and fixed looks at 5, 9, 17 and 33 paired timings.
  The maintained helper allocates alpha across those four looks. There is no
  additional adjustment across workload families.
- Stop at the first look establishing pass or regression; otherwise stop
  inconclusive at 33. Reaching the legacy runtime-precision target is not a
  stopping requirement.
- The estimand is the geometric mean of paired complete-runtime ratios.
  Runtimes are stationary paired lognormal draws, with log standard deviation
  0.04 and within-pair correlation 0.6. Pairs and replications are independent.
  Each slowdown uses its own predeclared seed, starting at 1913041.
- These controls call the actual `summarize` helper at every visited look.
  The result is conditional on this generator, with no abbreviation-model
  error. Serial dependence, heavy tails, changing load and prediction error
  can change behavior. The optional empirical replay mode was not exercised
  in this new run.

## Results

Each row contains 1,000 independent simulated tests. Regression at exactly
1.30 is counted as a false regression because that boundary is allowed.

| True runtime ratio | Pass | Regression | Inconclusive | Mean timing-pair count |
|---|---:|---:|---:|---:|
| 1.00 | 1000 | 0 | 0 | 5.000 |
| 1.29 | 218 | 9 | 773 | 30.288 |
| 1.30 | 31 | 31 | 938 | 31.928 |
| 1.31 | 8 | 219 | 773 | 30.092 |
| 1.60 | 0 | 1000 | 0 | 5.000 |

Pointwise 95% Clopper–Pearson intervals for false regression are 0.412–1.702%
at 1.29 and 2.116–4.372% at 1.30. The false-pass interval at 1.31 is
0.346–1.570%. Zero failures in 1,000 trials gives a two-sided 95% upper endpoint
of 0.368%, not zero risk. These intervals concern independent simulations;
they are neither simultaneous across all reported outcomes nor bounds on
future CI behavior.

At ratios 1.00 and 1.60, every replication stopped at the first look. Only
11 and 15 respectively met the separate legacy timing-precision diagnostic.
The 33-pair cap leaves most cases within one percentage point of the threshold
inconclusive under this noise model. This is an explicitly measured detection
limitation, not evidence that those cases pass.

Separate deterministic controls exercise actual policy, applicability,
endpoint-calibration and hardware-screen helpers with disclosed synthetic
inputs:

- Adding outside-receiver work changes the complete-runtime gate to regression
  while an incorrectly receiver-only timer passes.
- An unchanged abbreviation passes despite a 1.60 full-target ratio; the
  separate full-target sentinel blocks model qualification.
- A changed work-regime key fails the actual applicability validator.
- A common 1.60 environment slowdown cancels in the paired source ratio, while
  a synthetic hardware profile triggers the separate raw hardware warning.

These are decision controls, not actual slowdown injections or hardware
measurements. The earlier archive retains the separate real adapter delay
experiment. Eight focused unit tests also passed, including exact-boundary
roundoff checks and stopping before unnecessary timing precision; their scope
does not substitute for the simulation results above.

## Reproduction and provenance

From a compatible repository checkout, using NumPy and SciPy versions recorded
in the plan:

```sh
python tools/triad_perf_decision_policy_controls.py \
  --output local-evidence/decision-controls-new.json --trials 1000
python -m unittest discover -s tests -p test_triad_perf_decision_policy_controls.py
```

The output and its `.plan.json` must not already exist. The driver checkpoints
after each completed slowdown factor and preserves failure/incomplete state.
Runtime can vary; seeded numerical outcomes should reproduce with the matching
sources and compatible numerical libraries.

[matching-source-and-test.tar.gz](matching-source-and-test.tar.gz) preserves
all five source files declared by the plan, plus the eight-test source snapshot.
It is a compact source receipt, not a full repository or dependency lock.
All five declared source hashes matched both during the run and preservation.
[preservation.json](preservation.json) records member hashes, the original
command and the distinction between the prior test result and retained raw
simulation evidence. [SHA256SUMS](SHA256SUMS) covers all other files here.

```sh
cd docs/validation-data/triad-v3/2026-11-03-performance-decision/controls
sha256sum -c SHA256SUMS
```
