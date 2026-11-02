# Three-carrier reception at −60 dB-Hz

This study extends the isolated three-carrier modem to reciprocal sample rates
and genuinely long sampled observations at total-triplet **C/N₀ = −60 dB-Hz**.
It compares rates, observation durations, fixed and evolving rays, common and
relative frequency inference, channel mismatch and a tighter acquisition bank.
The ordinary Robust receiver is outside this study.

The fixed 850-day, Rate-0.0036-Hz holdout obtained **600/600 correct strict
receptions**, with a pointwise one-sided 95% lower bound of **99.502%** on
balanced-bit reception. The 3,000-day evolving-ray holdout obtained **300/300**
with the Gaussian-tap CFO-grid receiver, giving a **99.006%** lower bound. These
meet the 99% reception target for their specified sampled models and acquisition
conditions. One of the other rate cohorts obtained 299/300 strict receptions;
it qualifies 90%, but does not demonstrate 99% reception.

The [full-data audit](analysis-full.json) verifies all five primary holdout
populations and their generator/receiver bindings, with 12,672 report-bound
input-file hash checks. The [retained-archive audit](analysis-retained.json)
reproduces exactly the five primary cohorts' strict/relaxed counts, bindings and
pointwise qualifications. Benchmark repetitions and other nonprimary receiver
evaluations are explicitly excluded from holdout qualification. These results
do not establish a general radio operating envelope or universal optimality.

The maintained implementation contract is [triad-v3.md](../../../triad-v3.md),
and the shared physical-model contract is [channel-models.md](../../../channel-models.md).
The [earlier sub-9-kHz study](../2026-11-01-sub9/README.md) records the preceding
38-day experiments. Its performance and family budgets are not silently
extended to the present years-long cases.

## Signal, power and duration accounting

At −60 dB-Hz, `C = 10^-6 s^-1` and nominal full-power energy is
`E/N₀ = C × T`. Requested useful time therefore gives:

| Requested duration | Nominal linear E/N₀ | Role |
|---|---:|---|
| 38 days | 3.2832 | Very weak development edge |
| 250 days | 21.6 | Development duration sweep |
| 400 days | 34.56 | Development duration sweep |
| 750 days | 64.8 | Development comparisons |
| 850 days | 73.44 | Frozen fixed-ray holdouts |
| 1,500 days | 129.6 | Longer development cases |
| 3,000 days | 259.2 | Frozen evolving-ray holdout |

These are nominal available energies, not predicted decoder performance.
Requested active duration, rounded source elapsed time, padding/tails and
actual propagated energy must be reported separately. The 850-day request is
73,440,000 seconds; the 3,000-day request is 259,200,000 seconds. The audited
extents and realized energy are:

| Cohort | Rounded source elapsed seconds | Sampled capture seconds | Actual linear E/N₀ range |
|---|---:|---:|---:|
| 850 days, Rate 0.0036 | 73,440,183.6 | 73,467,264 | 73.1538–73.7475 |
| 850 days, Rate 0.036 | 73,441,836 | 73,444,608 | 73.4168–73.4602 |
| 850 days, Rate 0.00036 | 73,600,018.4 | 73,867,136 | 73.4318–73.4420 |
| 850 days, Rate 0.000036 | 80,000,002 | 82,667,136 | 78.3456–78.4082 |
| 3,000 days, OU coherence 10⁷ s | 259,200,648 | 259,227,776 | 190.6008–371.9217 |

The lowest main rate rounds to about 925.93 source days and 956.80 capture days,
with more actual energy than the 850-day nominal allocation. It is therefore
not an equal-energy rate comparison. The OU range includes realized fading.
Signal energy is evaluation-only truth and does not modify receiver scores.

The simulator advances waveform, channel and oscillator state through the
entire sampled physical span. Years of simulated observation are not years of
wall-clock or field operation. Reciprocal sampling reduces the number of samples
needed for an appropriately narrow signal; it does not multiply short-capture
energy or remove propagation dynamics.

The user's 1 mW remains **radiated power**. With 200 dB complete path loss the
received power is −200 dBm; −60 dB-Hz would then correspond to a separately
assumed noise density of −140 dBm/Hz. The earlier −144 dBm/Hz assumption instead
gives −56 dB-Hz. Neither noise density is asserted to be typical at a particular
site or RF band. Receiver noise and environmental interference require separate
physical evidence.

Encryption, three adjacent shaped carriers and whole-cell interleaving are
preserved. These fixtures use reproducible explicit epochs and key-addressed
patterns; operational `--now` reads the system clock once to establish the
message epoch. It does not periodically restart the keystream. No public pilot
or additional repeating waveform structure was added.

## Frozen conditions and acquisition scope

The [holdout plan](reproduction/holdout_plan.json) was fixed before holdout
generation/scoring. It declares independent physical seed splits, balanced
source bits, fixed sample counts and no early-success stopping.

| Setting | Principal configuration |
|---|---|
| RF shift | 8,270 Hz |
| Base and shift oscillator | Prescribed `gpsdo-ocxo` classes, margin 1 |
| Clock / region / offset / audio uncertainty | 10 ms / 1 ms / 0 ms / 70 ms |
| Public timing / epoch bank | One nominal timing branch; known epoch |
| Propagation | Two nominal equal-power rays, 0.005 ms excess delay, 0.001 Hz Doppler separation |
| Public receiver basis | Explicit delay 0.005 ms and Doppler 0.001 Hz |
| Added Symbols, FHSS and Symbol Burst | Off in the principal holdouts |
| Correlation block | 16,384 physical seconds |
| Common CFO receiver prior | ±0.000001 Hz |
| Primary fixed-ray receiver | `static-frequency-ucb` |
| Primary OU receiver | `cfo-grid-ucb`, 1,024 deterministic midpoint CFO points |

The nominal timing branch retains the physical arrival and clock uncertainties;
it is not replaced with simulation truth. At these symbol rates, the declared
initial ±90.5 ms arrival range is small. Known epoch and this small timing range
remain material restrictions: these trials do not qualify blind acquisition
over years of epoch uncertainty, changing clocks or receivers joining midway.
The development acquisition case explicitly adds three timing branches and
three epoch alternatives, rather than extrapolating from the nominal-only bank.

“Fixed rays” retain deterministic two-ray Doppler beating and the prescribed
oscillator impairment. OU cases additionally use independent stationary
proper-complex ray coefficients with declared coherence; that coherence is not
inferred from the 0.001 Hz Doppler separation. The physical oscillator class
uses its prescribed angular phase-noise law. It is not a measured multi-year
GPSDO Allan-deviation or phase-noise record. In particular, preserving the same
angular law while changing RF shift is a controlled software comparison, not
RF-specific oscillator qualification.

The deterministic-CFO receiver assumes no oscillator phase or frequency
diffusion, while the sampled oscillator impairment remains present. That is
explicit receiver-model mismatch. Its Gaussian tap dynamics can describe the
declared OU prior, but do not turn that model into a general phase tracker. The
phase-particle alternative is evaluated separately with its declared oscillator
prior; algorithms are compared on the same observations where available.

## Audited holdout results

| Cohort | Rate | Sample rate | Requested duration | Strict correct / wrong accepted / missed | Balanced-mean 95% lower bound |
|---|---:|---:|---:|---|---:|
| `static850-rate0036` | 0.0036 Hz | 1/64 Hz | 850 days | **600 / 0 / 0** | **99.502%** |
| `static850-rate036` | 0.036 Hz | 1/16 Hz | 850 days | **300 / 0 / 0** | **99.006%** |
| `static850-rate00036` | 0.00036 Hz | 1/64 Hz | 850 days | **299 / 0 / 1** | **96.956%** |
| `static850-rate000036` | 0.000036 Hz | 1/64 Hz | 850 days | **300 / 0 / 0** | **99.006%** |
| `ou3000-tc1e7` | 0.0036 Hz | 1/64 Hz | 3,000 days | **300 / 0 / 0** | **99.006%** |

Each fixed-ray cohort uses its predeclared `static-frequency-ucb` receiver; the
OU cohort uses `cfo-grid-ucb`. Every raw winning bit was correct in all five
cohorts. The single missed Rate-0.00036 bit passes the relaxed gate, giving
300/300 relaxed receptions; all other relaxed counts equal their strict counts.
The 299/300 cohort's conservative lower bound averages simultaneous per-stratum
bounds. Its observed 99.667% success fraction must not be presented as proof of
at least 99% reception.

The [controls plan](reproduction/controls_plan.json) completed 64 noise-only
and 64 paired wrong-key captures. Both static receiver variants produced
**0/64 false admissions** in each control class at both gates. The one-sided
95% upper bound remains 4.573% per capture; these sparse controls do not
empirically qualify a 10⁻⁴ daily rate. Wrong-key signals also have a different
distribution from noise-only calibration.

The four-path extreme-rate development case at Rate 0.0000036 Hz obtained 4/4
correct strict receptions with both static variants. Its requested 1,500 days
became 160,000,000.4 source seconds and 186,667,136 capture seconds, with actual
E/N₀ 143.668–143.864. This supports representation and these four sampled cases,
not a reception-rate qualification or a 1,500-day equal-energy claim.

Pointwise fixed-size confidence bounds must retain the balanced-bit design.
Paired methods, reused physical observations, pilot cases and development
screens are not extra independent trials. A lower bound on equally weighted
mean reception across the two bit strata is not a simultaneous guarantee for
each individual bit or for every reported rate. For a 300/300 cohort, the
balanced-mean lower bound is 99.006%, while each 150-trial bit stratum separately
has a 98.023% lower bound. The audit reports all four outcome categories and
requires full planned populations, independent seed roots and fixed source and
configuration bindings before assigning a primary qualification.

## Development comparisons and receiver alternatives

The [screen plan](reproduction/screen_plan.json) uses eight physical paths per
case. It sweeps 38–1,500 requested days, four rates, OU coherence from 10,000 to
100,000,000 seconds, a daily 0.5-radian phase excursion, 1% slow plus 1% fast
frequency-selective amplitude perturbation, and a day-200 to day-300 outage
with one-day ramps. It also tests a 10⁻⁷ Hz public ray-Doppler error, acquisition
alternatives, sample-rate refinement and 4,096-second correlation blocks.
These small screens identify trends and failures; they are not reliability
qualifications. Counts below are strict / relaxed correct accepted bits out of
eight, with a dash meaning that receiver was not evaluated on that case:

| Development case | Static-frequency UCB | CFO-grid UCB | Phase-particle UCB |
|---|---:|---:|---:|
| Fixed rays, 38 days | 0 / 0 | — | — |
| Fixed rays, 250 days | 0 / 0 | — | — |
| Fixed rays, 400 days | 3 / 5 | — | — |
| Fixed rays, 750 days | 8 / 8 | 8 / 8 | 8 / 8 |
| Fixed rays, 1,000 or 1,500 days | 8 / 8 each | 8 / 8 each | — |
| 750 days, OU coherence 10⁴, 10⁵ or 10⁶ s | 0 / 0 each | 0 / 0 each | — |
| 750 days, OU coherence 10⁷ s | 3 / 3 | 6 / 6 | — |
| 750 days, OU coherence 10⁸ s | 3 / 5 | 5 / 6 | — |
| 1,500 days, OU coherence 10⁷ s | 4 / 7 | 8 / 8 | 8 / 8 |
| Daily phase excursion | 7 / 8 | 7 / 7 | 8 / 8 |
| 100-day outage | 6 / 8 | 6 / 7 | — |
| 10⁻⁷-Hz ray-Doppler error | 4 / 6 | 3 / 6 | 3 / 6 |

There were no wrong accepted bits in these comparisons. Fixed-ray rate changes,
each-1% perturbation, the acquisition bank and both sampling/block refinements
each gave 8/8 strict static receptions. The short-coherence OU cases remain a
clear failure: increasing duration and retaining a Gaussian channel model do
not erase the penalty of repeated coherence loss. At 38 days the static raw
winner was correct only 5/8 times; at 250 days it was correct 8/8 but never passed
admission. Raw discrimination and calibrated reception are separate outcomes.

The [RF plan](reproduction/rf_plan.json) contains four paired 500-kHz-shift
captures, with the same selected sub-9-kHz propagation and angular oscillator
law. They use 1,024-second blocks and a ±50.16-microhertz receiver CFO prior.
Both static variants obtained 4/4 correct strict receptions. Actual source and
capture spans were 64,800,162 and 64,827,264 seconds. This examines changed
frequency-offset scaling under a declared model; it does not establish a
physically representative 500-kHz path, much less 5.8-GHz multi-year performance.

The [5.8-GHz preflight](microwave-preflight.json) is configuration/resource
analysis only. Under the same preset, its deterministic CFO bound is about
±0.58 Hz, compared with ±50.15 microhertz at 500 kHz. It needs blocks shorter
than 0.8621 s for the current block-center frequency search. Illustrative
0.8-second blocks over 750 days produce about 81.1 million blocks; the requested
frequency spacing needs a 2²⁹ FFT, exceeding the current 2²⁶ cap. A 2-Hz raw
capture also exceeds the 128-million-row export cap. No microwave waveform
receiver campaign was run. These are limits of this representation and search,
not proof that the physical link or a different acquisition architecture fails.

The compared algorithms preserve the complex three-carrier evidence:

| Receiver | Retained likelihood and limitation |
|---|---|
| Static frequency | Accumulates original complex block correlations over common CFO, then integrates fixed Gaussian tap coefficients; evolving rays are mismatch |
| Deterministic CFO grid | Integrates a normalized finite CFO prior with Gaussian tap evolution; no oscillator diffusion or adaptive resampling |
| Phase-particle receiver | Integrates conditional Gaussian taps across predeclared oscillator trajectories; finite particle resolution must be checked |
| Joint ray frequency | Searches common CFO and a constant relative ray-frequency correction with its cross-Gram; fixed coefficients, no OU tracking claim |

The [joint-ray plan](reproduction/ray_plan.json) keeps half the relative-frequency
prior mass at zero and distributes the remainder over a finite grid. Its
same-common-grid inequality, `logLR_joint ≥ logLR_zero_relative − ln(2)`, is a
likelihood bound, not a dB sensitivity guarantee. The implemented cross-Gram
uses the positive relative-frequency exponent and remains in original ray
coordinates until after frequency accumulation.

The [paired joint-ray analysis](analysis-ray.json) obtained 8/8 strict receptions
on each of fixed rays, ray-Doppler mismatch, acquisition and each-1% perturbation,
on both CPU FP64 and GPU FP32. The mismatch case improves from 4/8 with the
static receiver to 8/8 with the joint-ray receiver. Their common CFO grids also
differ, so this is not an isolated measurement of relative-Doppler gain. Across
these paired cases, UCB log-score differences were at most 1.78×10⁻⁵, with no
winner or gate disagreement. Eight paths per case cannot qualify 99% reception;
their timings under concurrent work are diagnostic rather than isolated speedup
measurements.

## Measured CPU/GPU execution

The separate [batch benchmark](analysis-benchmark.json) completed all nine jobs.
For the same eight actual 850-day observations and 1,024 CFO points, the medians
over three FP32 repeats were:

| Timing scope | CPU, two threads | GPU | CPU/GPU time ratio |
|---|---:|---:|---:|
| Resident inference | 4.7845 s | 3.0364 s | **1.576×** |
| Tool wall time | 6.1078 s | 4.4100 s | **1.385×** |
| Process wall time | 7.6061 s | 6.2682 s | **1.213×** |

Preparation remained about 1.30 s on either path. Tool time starts after device
initialization; process time includes startup. These figures measure scoring
already-exported observations, not waveform generation or end-to-end modem
acquisition. All repeats are retained; the fastest run was not selected.

Against the CPU FP64 reference, maximum FP32 log-score errors were 9.50×10⁻⁶
on CPU and 9.79×10⁻⁶ on GPU, with no winner/admission changes. The nearest
reference score was 15.046 log units from the gate and the smallest bit margin
was 41.117, so this does not qualify arithmetic at a reception edge. FP32 selects
bulk correlation/particle products; full-bank calibration, Grams, covariance
solves and sensitive evidence reductions retain FP64, with TF32 disabled.

At 8,192 CFO points, GPU resident/tool/process times were 11.587/16.510/18.414 s
for 32 paths; CPU times were 41.819/43.182/44.534 s for eight paths. Unequal
workloads do not supply a direct speedup factor. Their common eight paths had
FP32 CPU/GPU score differences at most 8.05×10⁻⁷ and unchanged decisions, but
there was no 8,192-point FP64 reference. All benchmark inputs are reused
observations, not additional independent reception trials.

The [additional precision diagnostic](analysis-precision-edge.json) completed
four jobs comparing CPU FP64 with GPU FP32 on 11 reused captures: eight 400-day
development cases and three cases selected after the Rate-0.00036 holdout.
All winners and gate decisions agreed. Maximum log-score errors were
1.51×10⁻⁶ and 6.61×10⁻⁶ respectively; the closest reference scores were still
3.139 and 3.118 log units from the gate. This includes both accepted and missed
cases, but it is not a machine-precision boundary or rare-tail qualification.
Post-hoc case selection does not add independent sensitivity evidence.

## Admission budgets and theoretical references

The declared family is **16 receiver/model alternatives × eight duration
alternatives × two bits**, with one scheduled attempt per minute. Half the
false-admission budget is reserved for score tails and half for calibration.

| Target | Scheduled attempts | Log-likelihood gate |
|---|---:|---:|
| 99% correct reception; at most 10⁻⁴ noise admission probability per day | 1,440 | 22.7210633896 |
| 90% correct reception; at most 1% per 12 hours | 720 | 17.4227460230 |

These mathematical noise-family allocations require the declared stationary
proper-complex Gaussian model, independent residual calibration and complete
predeclared alternatives. They are not an empirical continuous-operation test.
Changing duration, receiver or acquisition prior after viewing results requires
additional accounting. The relaxed results re-gate the same scores while
retaining the original stricter UCB calibration.

The [theory implementation](theory/frontier.py) and [numerical results](theory/frontier.json)
separate a known-waveform Gaussian reference from unknown Gaussian-ray models.
With the present minute schedule, the optimistic known-waveform reference needs
**432.072 useful days for 99% presence** under the strict per-component false
tail, or **261.203 days for 90% presence** under the relaxed allocation. Presence
does not include a competing wrong-bit hypothesis or establish correct-bit
reception under unknown synchronization/channel parameters.

The frozen primary target was no more than 3 dB of nominal energy above the
432.072-day presence reference. An 850-day allocation has 73.44 linear E/N₀
(18.659 dB), **2.939 dB above that reference**. The audited 600/600 primary
holdout meets that predeclared target for its selected sampled model. This is
an energy comparison to an easier presence problem, not a measured 2.939 dB gap
to optimal unknown-channel correct-bit reception. It does not establish the
minimum usable duration or universal optimality. Realized energy and capture
padding remain explicit in the earlier table.

For two ideal independently observed orthogonal OU rays of mean power 1/2 each,
the theory uses kernel `K_c(t,s) = (C/2) exp(−|t−s|/τ)`, known waveform,
alignment, deterministic Dopplers, common phase and exact stationary white
noise density. Its asymptotic expected log-likelihood rate is
`C(Cτ)/(sqrt(1+Cτ)+1)^2`. Only when `Cτ << 1` does this become `C²τ/4`, showing
why repeated loss of channel coherence can require much worse duration scaling
than the coherent `1/C` reference. The two ideal observation channels are not
the actual summed sampled waveform; the static Gaussian limit also includes
random total received power, unlike fixed equal-power physical rays.

OU moment/Cantelli bounds in the theory are presence bounds for their stated
likelihood gate. They are not exact optimum-detector probabilities, wrong-bit
guarantees or universal impossibility results. The calculation uses high
precision for moments and FP64 for the resulting bounds, without an interval
arithmetic certificate or empirical rare-tail extrapolation.

Hourly or daily attempt schedules reduce the known-waveform requirements by
restricting operation. That is not an algorithmic sensitivity improvement.
Exact calibration of correlated repeated windows could reduce the conservative
union penalty, but the present campaign does not prove near-optimal continuous
monitoring.

## Implementation and reproduction limits

The new `sample_rate_denominator` supports reciprocal-integer Hz with numerator
one and exact integer-second UTC steps. Effective sample rate is used throughout
source, channel, basis, noise and capture calculations. The requested duration
ceiling is ten Julian years (315,576,000 seconds); private-tile rounding and
tails may extend the elapsed support. Checked 64-bit coordinates and bounded
source/channel workspaces avoid a full-duration waveform allocation. Retained
receiver/export workspaces still have explicit resource limits.

Low sample rates require the complete shaped signal and declared deterministic
frequency envelope to fit capture. Unsupported fast frontend/oscillator
impairments, FHSS and millisecond dropout are rejected below their supported
sample rate. This point-sampled model has not thereby become an independently
validated high-rate integration/decimation or analog radio model. Prescribed
stochastic spectral tails, long-term aging and changing environmental noise
remain important physical limitations.

The generic duration planner is a conditional model calculation. It does not
learn channel retention or wrong-pattern separation from these successes. Its
ordered search evaluates at most 2,000,001 candidate durations and explicitly
throws when unvisited durations remain; it does not assume monotonic evidence
across rank changes. Session checkpoint/restart and field recovery remain
unimplemented.

The [job runner](reproduction/run_jobs.py) and retained generation/scoring plans
specify commands, seeds, family choices and bounded execution. Replay requires
mapping their original workspace paths to the local checkout. The
[receiver/source binding receipt](reproduction/receiver_bindings.json) is
explicitly a post-run receipt of unchanged reviewed sources, not a prior
timestamped hash commitment. The [analyzer](analyze.py) owns the final identity,
duplicate, control, score and confidence checks. The full audit verified all
12,672 available report-bound input-file checks before evidence pruning, with
no failed receiver capture records. Its primary-method population checks are
the basis for the holdout claims; repeated benchmark scores are nonprimary.
Independent review checked all 1,800 primary actual manifests, their six seed
domains and source/binary bindings, and every score row in the 105 primary NPZ
archives, including the 300-path OU cohort. That review accepted the primary
counts and their stated qualification scope.

The retained audit contains 168 receiver reports and 2,374 scored capture
records, versus 164 reports and 2,352 records in the full-data audit. The four
additional reports are the later post-hoc precision comparisons, not additional
primary reception trials. Its 2,392 verified input-file checks and 10,280
explicit missing-array entries reflect intentional archive pruning. The five
primary results and qualifications agree exactly between these audits;
missing arrays are not relabeled as locally verified.

The [scientific evidence archive](minus60-scientific-evidence.tar.gz)
(84,866,723 bytes) retains scientific JSON, score NPZ files, logs and selected
complete workspaces. Its [inventory](minus60-scientific-evidence.tar.inventory.json)
records all **7,511 retained files**, each verified byte-for-byte by SHA-256,
and the three oversized selected workspaces whose
raw arrays were omitted. Other nonselected raw workspaces are also omitted.
The archive SHA-256 is
`d381b8e6688a849b1feef53b8bf35936794d67c2be69fa448df1226ae18a91da`.
The acquisition and RF arrays were hash-verified during the full audit before
pruning; their absence from the retained archive must not be described as a new
local byte verification. Recount the retained material with:

```sh
study_dir=docs/validation-data/triad-v3/2026-11-02-minus60
unpack_dir=$(mktemp -d /tmp/triad-minus60.XXXXXX)
tar -xzf "$study_dir/minus60-scientific-evidence.tar.gz" -C "$unpack_dir"
python3 "$study_dir/analyze.py" \
  --root "$unpack_dir" \
  --plan "$study_dir/reproduction/holdout_plan.json" \
  --output "$unpack_dir/retained-recount.json"
```

This recount must preserve missing-array notices; it cannot recreate the full
pre-pruning byte audit without regenerating those arrays. Reproduction plans
and source hashes remain necessary if regenerating data under a later checkout.

The focused C++ triad group passed **14/14 locally and 14/14 on the compute
host**; the archive retains `build-validation.log`. Focused Python checks passed
for the reciprocal/core work (16), adapter (14), sampling refinement (10), link
budget (7), theory (7), resident batch adapter (8) and joint-ray direct oracle
(7). These checks cover the isolated changes; the ordinary receiver's unrelated
regression campaign was not run.

Initial environment setup failed when global pip encountered an externally
managed Python installation; using an isolated virtual environment resolved it.
An initial lean source bundle omitted `CMakePresets.json`; adding the required
build inputs resolved that before the 14-test build validation. These setup
failures occurred before the receiver experiments and are not reception misses.
Their operational logs are not included in this scientific archive. No runtime
export failure is hidden as a successful capture.
