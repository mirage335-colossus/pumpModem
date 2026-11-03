# Traceable full-simulation performance study — 2026-11-03

This study extends the [earlier performance suite](../2026-11-03-performance/README.md)
with native whole-simulation predictions, independent full-target confirmations,
adaptive timing precision, execution witnesses and sustained processing checks.
The maintained contract is [the performance guide](../../../triad-performance.md).
It does not qualify ordinary Robust integration or radio sensitivity.

## First complete campaign: retained diagnostic protocol

The first complete CPU campaign used the fixed-confirmation v2 protocol on an
AMD Ryzen 5 PRO 5650U, with two Python numerical workers and one native worker,
unrestricted 12-logical-CPU affinity. The actual processor is recorded rather
than relabeled as a 5560U. Source identities, individual observations and counts
are retained in [the compact report](cpu-001-summary.json), derived from an
84,383,148-byte raw backend report whose SHA256 is included in that summary.
The [harness snapshot](cpu-001-harness.tar.gz) and
[individual source hashes](cpu-001-harness-sha256.json) preserve that protocol.
The immutable receiver baseline is `242ea15fa032ee117b9113a7f3cd07cc6e9f0254`.

The complete suite took **1,739.77 seconds** including supervision; the backend
reported 1,731.89 seconds. All 14 required cases and eight CPU-applicable
offline path families executed. Three CUDA-only families were explicitly
unsupported locally. There were no hard failures or numerical disagreements.
Eleven case predictions qualified/passed; native flat, native FHSS and the
free-crystal FFT comparison remained unqualified. All cases reached 33 outer
timing samples; only native-null and the resident 850-day FFT case met the
requested 5% precision for all three timing intervals. This was a completed
**warning** result, not an all-case 5% timing-accuracy certificate.

The full native VLF target retained 73,728,051.2 source seconds, about 853 active
days, at a 1/128-Hz point-sampling rate and −60 dB-Hz. Its independent 17-pair
full-target confirmation put mean forecast error intervals at −2.77% to +0.93%
for the reference, −3.13% to −0.66% for the candidate, and −3.29% to +1.40% for
their ratio. These were fixed-look 90% equivalence intervals under the old
protocol. They do not establish correct reception or continuous high-bandwidth
simulation of 853 days. The candidate full-simulation forecast was 3.9642 s.

| Frozen sustained forecast | Measured complete work | Difference | Scope |
| --- | ---: | ---: | --- |
| Native external: 63.3582 s | 62.5540 s | −1.27% | 16 complete VLF simulations; 62.5299 s inside the native pipeline |
| FFT resident: 60.2336 s | 62.1532 s | +3.19% | Repeated full preparation and full CFO banks; 62.6234 s external wall time |

These are two predeclared single sustained consistency observations, not new
confidence intervals. Native last-quarter/first-quarter mean runtime was 1.0352.
The separate long source calendar and sustained processor execution are both
measured, but must not be conflated.

The free-crystal FFT reference and candidate absolute forecasts each passed
their equivalence checks. Its paired mean-error interval was +2.94% to +5.52%,
crossing the ±5% model bound; the joint comparison correctly stayed unresolved.
An interval near that engineering boundary is not a proven implementation
slowdown above 30%.

## Why the next protocol changes

The native flat abbreviation plus full confirmation consumed 28.88 seconds of
timed work. Thirty-three direct full pairs would have cost about 21.16 seconds
using the observed full-call costs. This is a diagnostic estimate, not a second
measured campaign. Native FHSS also showed a worse variance-to-cost tradeoff
for abbreviation. Their next protocol therefore measures the same complete
targets directly.

The original native VLF fit extrapolated two nearby duration siblings, amplifying
their timing errors. The next protocol widens training from T/16 and T/8 to
T/32 and T/4 while retaining the full target T. Its affine weights change from
−14,+15 to −24/7,+31/7. That improves conditioning under comparable fractional
timing noise, but does not remove host clock drift and is not itself measured
proof of more precise prediction.

The first broad-timing FFT case predicted about 23 seconds per full paired
confirmation and preselected 17 pairs from training variance. The next protocol
uses a frozen forecast with cumulative confirmation looks at 3, 5, 9 and 17,
each using the corrected 97.5% interval described in the maintained guide.
It can stop earlier on decisive evidence without a hidden retry or refit.
Preparation-inclusive pilot costs also select direct work when abbreviation
would save less than 20%.

New anchors additionally bind adaptive-protocol hashes and actual CPU masks
and topology. Their raw-stage observations, frozen links, residuals, allowed
look order and stopping decisions are reconstructed before reuse. Old evidence
is retained as old evidence; it is not silently upgraded to the new protocol.

## Second complete campaign: frozen finite-look qualification

[CPU002](cpu-002-summary.json) used the next v3 protocol, pinned to logical CPUs
2 and 4, which are different physical cores on this host. Python numerical work
used two threads, native work one. Its [harness snapshot](cpu-002-harness.tar.gz)
and [file hashes](cpu-002-harness-sha256.json) preserve the measured code.
All 14 cases completed in **1,606.92 seconds** including supervision
(1,600.01 seconds in the backend). Twelve predictions qualified/passed;
narrow-HF and free-crystal FFT models stayed unqualified. Ten of 14 cases met
all three requested 5% timing intervals. No numerical disagreements or hard
failures were reported. CPU affinity and protocol both changed from CPU001,
so the elapsed-time difference is not an isolated optimization measurement.

The VLF full-target qualification needed all 17 predeclared pairs. Its final
97.5% intervals were −1.49% to +1.44% for the reference, −2.41% to +3.11% for
the candidate and −2.67% to +3.46% for their ratio. Current timing precision was
4.65%, 2.27% and 4.00%, respectively. The full candidate forecast was 3.8815 s.
Free-crystal reached its frozen budget cap of nine confirmation pairs: the
paired interval was −5.73% to +1.10%, so it could not establish ±5% equivalence.
Narrow-HF also failed to establish equivalence. Neither result is a confirmed
30% implementation slowdown.

## Prospective routine policy

The following policy was chosen after CPU002; CPU002 does not retrospectively
validate it. Preserve raw full-run proof, use a reviewed measurement contract
for applicability, and remeasure current abbreviated coefficients. A routine
modeled case performs one complete paired endpoint sentinel under a frozen
historical engineering envelope, rather than repeating fresh mean-equivalence
qualification. It reports conditional support separately from fresh qualification.
Subsecond FFT full calls use direct timing. CI restores validated anchors and
sampled fixtures; earlier workflow versions uploaded them without restoring them.
Actual timed-source or binary changes still require focused qualification.

The separately measured [external-delay negative control](external-delay-control-summary.json)
inserted 200 ms after each real native subprocess, outside its internal receiver
clock. Complete-test timing detected it: the paired slowdown interval was
**6.496–8.631×**, and the driver exited 2 as expected. Numerical receiver work
was unchanged. This proves that the native benchmark includes external process
cost; it is not a processor-throughput measurement or a calibrated 30% boundary
experiment.

## Measured routine reuse and the mean/scatter correction

A separate bounded [free-crystal qualification](free-qualification-summary.json)
completed in **488.09 seconds**, stopping after nine independent full pairs.
The 97.5% mean-error intervals were −1.17% to +2.30% (reference), −0.79% to
+3.16% (candidate) and −2.41% to +3.72% (paired). All three current timing
intervals reached 5%. This is new evidence with its own frozen forecast; it
does not retroactively pass CPU002's budget-limited attempt.

The first native reuse diagnostic took 86.79 seconds and stayed inconclusive.
Its full endpoint errors were only −0.032% and −2.91%, but individual historical
residuals up to 16.6% had been included in the **mean** error allowance. Together
with current mean-timing uncertainty, that exceeded the 20% cap. The
[diagnostic record](native-jitter-diagnostic-summary.json) preserves this failed
policy. Conditional contract v2 separates mean-model uncertainty from
single-execution scatter. Original v1 hashes are not reinterpreted; their
schema is explicitly rejected by the v2 validator.

The [complete routine run](cpu-003-summary.json) then finished in **836.95 seconds**
including supervision (830.47 seconds in the backend). All 14 cases and eight
CPU-applicable path families executed. Thirteen cases had supported regression
gates; free-crystal's endpoint check was inconclusive. There were **no hard
failures, numerical disagreements, or raw hardware-health warnings**. Eight of
14 cases met all three 5% timing targets. Six retained precision warnings rather
than silently extending the test campaign. The largest finite timing schedule
was 33 samples; native wrong-key and frontend stopped at 17.

Native VLF and broad-HF FFT obtained **conditional historical support**, with
one current full endpoint pair each. Cheap narrow-HF used direct full timing.
The free-crystal routine forecast itself met the 5% timing target, but its
single paired endpoint residual interval was approximately −10.01% to −2.52%,
partly outside the frozen reciprocal-10% sentinel envelope (−9.09% to +10%).
That overlap prevents conditional support; it is not a demonstrated 30%
regression. The suite preserved the warning and did not repeat the endpoint
until it passed. Its earlier independent full qualification remains recorded
separately.

The measured routine cycle is about half CPU002's qualification wall time, but
**still exceeds the preferred five-to-ten-minute budget on this two-core
allocation**. This is not a proof of globally minimal benchmarking cost.
Hardware probes, direct full cases, adaptive sample ceilings and the remaining
modeled searches account for the measured cost; the JSON retains their work and
sample counts. Broad-HF's adapter time was 166.74 seconds versus 531.27 seconds
in CPU002; free-crystal was 191.42 seconds versus 488.09 seconds for its focused
qualification. These campaigns differed in qualification policy and timing
realizations, so the ratios are measured campaign costs, not isolated kernel
acceleration claims.

A separate [native routine plus sustained check](native-routine-sustained-summary.json)
completed in 147.28 seconds. It allowed up to 65 timing samples, stopped at 33,
and reached timing half-widths 2.17%, 3.37% and 3.20%. Sixteen complete VLF
simulations were then executed under a forecast frozen before that work:
**61.7300 seconds predicted, 61.5445 seconds observed externally (−0.30%)**.
The measured native pipeline lasted 61.5232 seconds, including 35.4438 seconds
in the receiver. Its final/initial quarter mean ratio was 1.0514. This is a
sustained consistency observation, not a new future-coverage guarantee.

The [uniform synthetic slowdown](fft-uniform-delay-summary.json) used the real
broad-HF receiver with an added delay of 60% of each candidate score-call time.
No numerical outputs or work counts changed. The predicted full-time ratio was
1.5753; its model-adjusted interval was **1.3624–1.8215**, wholly above 1.30.
The driver correctly returned regression/exit 2 after 226.23 seconds. Both
sources retained the full sample/timing geometry; abbreviation remained active.
This is a negative-control result, not a measured code or hardware speed change.

The [full-bank-only control](fft-full-only-delay-summary.json) injected exactly
one delay, into the full candidate bank after abbreviated measurements had
finished. The abbreviated full-time ratio was 0.9825, but the endpoint's
candidate discrepancy was +53.8%; the paired log-discrepancy interval was
0.4137–0.4971. Both exceeded their frozen observation envelopes. The model was
rejected, with no envelope update or retry, after 187.47 seconds. This
experiment demonstrates why retaining the full endpoint is necessary.

After these timed runs, a narrow release guard was added so a severe model
contradiction cannot become a nonblocking green warning. It subtracts the
pre-existing historical observation allowance and current timing half-width
from the endpoint's log underprediction, and blocks above `log(1.30)`. The
[deterministic classification audit](endpoint-release-guard-audit.json) applies
that rule to the **unchanged measured records**: the full-only control blocks,
the uniform control remains a confirmed performance-gate failure, and none of
CPU003's ordinary cases gain a new failure. The guard is labeled an engineering
calibration failure with unqualified false-failure rate. It is not a new timing
experiment or a statistical proof of future mean performance. The
[post-measurement delta](post-measurement-release-guard.json) and
[final source snapshot](release-gate-harness.tar.gz) separate it from the
[harness actually timed for CPU003](routine-v2-harness.tar.gz).

The final [FFT sustained check](fft-sustained-summary.json) selected the largest
eligible saved forecast by bank size. Free-crystal was ineligible, so this was
broad-HF: 33 CFOs over 13,509 timing positions, retaining the full 24,978-sample
fixture and 50,000-point FFT. Six complete resident prepare/score calls had a
frozen forecast of **69.3170 seconds** and measured **67.7452 seconds**
(−2.27%); external elapsed time was 67.7630 seconds. The driver took 72.71
seconds including setup. It did not refit the CPU003 forecast or reselect
precision. This provides the second current sustained observation alongside
the native test, without another calibration campaign.

## Reproduction and further development

All **156 focused tests passed** in 10.235 seconds after the final release guard;
independent reviews covered the statistical distinction, historical proof,
FFT/native integration, focused-case selection and release-status propagation.
The tests verify the harness contracts, while the measurements above establish
its demonstrated predictive behavior. Do not replace either with the other.

The source base is `38209d0131de2c90ad9db4075025741198ec168d`; the immutable receiver
reference is `242ea15fa032ee117b9113a7f3cd07cc6e9f0254`. Local timing used the
actual Ryzen 5 PRO 5650U, CPU mask `2,4`, two Python workers and one native worker.
[Reproduction metadata](reproduction.json) lists exact commands, input hashes,
source overlays and reference-build provenance. Restore overlays only into an
isolated checkout of the stated base. Raw focused reports, anchors, commitments,
and experimental drivers are in the 3.77 MB [proof archive](raw-proof.tar.gz),
with a [member manifest](raw-proof-manifest.json). Full CPU reports remain in
ignored local evidence; their exact hashes/sizes and compact derived results
are retained. The archive excludes binaries and sampled waveform caches, which
can be rebuilt from the declared source and plans.

For ordinary use, retain `--anchor-directory` and run the maintained CLI with
`--validation-mode auto`. To investigate only an affected case, use repeatable
`--case ID` selection; it retains the full profile confidence multiplicity and
is explicitly incomplete for normal CI. Source/binary changes in timed receiver
code still require affected-case qualification. Reporting-only compatible
changes do not. Cross-revision model transfer without that qualification,
universal detection of 30% full-size-only regressions from one noisy endpoint,
and sub-ten-minute complete runs on this allocation remain **unestablished**.
No further threshold widening or retries were used to erase the free-crystal
warning or missed precision targets.

## Execution and qualification limits

The first campaign's raw JSON remains under ignored `local-evidence/` with the
hash recorded above. The compact report retains predictions, confirmation
observations, work counters, sampling-call counts, gates, source identities and
sustained results. It omits repeated raw per-trial metadata, so it is an audited
summary rather than a byte-identical replacement of the raw report.

RunPod deployment attempts for RTX 5090, RTX A6000 and RTX PRO 6000 returned
unavailable; automatic-deployment subscriptions were canceled. No Pod was
created or new cloud charge incurred. The account displayed $0/hour active
spend afterward. There is no new GPU-performance claim from this study.

Hosted GitHub Actions and ordinary receiver regression campaigns were not run.
Cross-architecture predictions for unmeasured processors remain unqualified.
