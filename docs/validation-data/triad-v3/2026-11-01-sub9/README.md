# Sub-9-kHz, low-rate and month-span study

The completed fixed-size holdout obtained **600/600 correct accepted bits at
−46 dB-Hz with a requested 30-day pattern**, using `static-frequency-ucb` on
independently generated sampled waveforms. There were no wrong accepted bits or
misses. Its pointwise one-sided 95% lower bound on balanced-bit correct reception
is **99.502%**. This meets the 99% reception target for this particular prescribed
channel, oscillator, timing and noise model. It does not qualify a physical
sub-9-kHz link or all channels having the same Doppler spread.

The independent eight-day OU holdout at −36 dB-Hz obtained **300/300 correct
accepted bits** with `rbpf-ucb`, giving a **99.006%** pointwise lower bound on
balanced-bit reception. At −48 dB-Hz, the 38-day static receiver obtained
**296/300** at the strict gate and **300/300** at the relaxed gate; the strict
result does not qualify 99% reception.

All 1,264 planned holdout/control waveform captures completed. Corrected
wrong-key controls produced no false admissions in 32 captures for each of four
methods. The study remains separate from the ordinary Robust receiver. The maintained implementation
contract is in [triad-v3.md](../../../triad-v3.md), with the sampled-channel
contract in [channel-models.md](../../../channel-models.md). The
[evidence archive](evidence.tar.gz) contains the `evidence/` paths cited below;
[analysis.json](analysis.json) is the compact machine-readable recount.
It contains 130 receiver reports, 1,297 scored inputs (including the separate
screen, pilot and day diagnostics), and 4,634 input/method scores, with no
duplicate scores or count disagreements. The 64 failed control-analysis attempts
remain recorded separately and are excluded from scored denominators.

## Physical meaning and observation duration

The user's **1 mW is radiated power**. With 200 dB complete path loss, received
power is −200 dBm. Antenna input power is outside this link budget. Receiver noise
density is an independent assumption, not determined by that path loss:

| Assumed receiver noise density | Total-triplet C/N₀ | Nominal E/N₀ over 38 days |
|---|---:|---:|
| −164 dBm/Hz | −36 dB-Hz | 29.163 dB |
| −154 dBm/Hz | −46 dB-Hz | 19.163 dB |
| −144 dBm/Hz | −56 dB-Hz | 9.163 dB |

These figures use `E/N0 = 10^(C/N0_dBHz/10) × T` and complete power allocation to
one bit. They are available nominal energy, not reception predictions. Actual
signal energy changes with rounding, outages and fading; elapsed time, protected
payload allocation and actual propagated signal energy are recorded separately.
The link-budget evidence (`evidence/link-budget.json`) retains the assumptions.

The full-waveform month pilot generated **3,298,758 complex samples at 1 sample/s**.
Its requested duration was 3,283,200 s (38 days), rounded source duration was
3,296,082.4 s, and capture duration including padding and complete tails was
3,298,758 s. It advanced the sampled oscillator, channel, encryption and shaping
state through that physical span. This is a simulated 38-day observation, **not
38 days of wall-clock laboratory observation**, nor a short observation whose
energy was merely multiplied. The pilot manifest (`evidence/month-pilot/manifest.json`)
and static receiver result (`evidence/month-pilot-static.json`) preserve the case;
the single pilot passed both static and RBPF gates but is not a reliability
estimate.

The implementation streams bounded chunks with 64-bit coordinates. The
epoch/key-addressed encrypted three-carrier pattern and interleaving remain in
use. Fixtures use explicit reproducible epochs; operational `--now` derives the
epoch from the system clock once. This study adds no public waveform structure.
A requested maximum of 38 days
allows source rounding and channel/filter tails beyond that duration. Low sample
rates are admitted only when the occupied waveform fits the sampled bandwidth.
Effects requiring the existing faster impairment path are rejected below its
supported sample rate rather than silently omitted.

## Frozen receiver and channel conditions

The [holdout plan](reproduction/holdout_plan.json) was fixed after the development
screen. Its principal configuration is:

| Setting | Value and scope |
|---|---|
| RF shift | 8,270 Hz |
| GUI-style rate / fine-symbol rate | 0.036 Hz / 0.006 symbols/s/carrier |
| Carrier spacing / nominal triplet bandwidth | 0.0075 Hz / 0.0225 Hz |
| Sample rate / correlation block | 1 sample/s / 1,024 s |
| Added Symbols, FHSS, Symbol Burst | Off for these principal holdouts |
| Base and shift oscillator | Prescribed `gpsdo-ocxo` model, margin 1 |
| Clock / region / offset / audio uncertainty | 10 ms / 1 ms / 0 ms / 70 ms |
| Public timing / epoch bank | One nominal timing branch; true epoch known |
| Shared physical two-ray model | Equal nominal ray powers, 0.005 ms delay, 0.001 Hz Doppler separation |
| Receiver ray basis | Explicit `--tap-delay-ms .005 --tap-doppler-hz .001` |
| Residual common-frequency prior | ±0.000001 Hz |
| Relative phase diffusion | 0.00012341341494884352 rad/√s |

The physical preset does **not** automatically choose the receiver tap basis.
The explicit receiver flags above are essential. The principal holdouts retain
the physical arrival uncertainty (bound ±90.5 ms); they do not inject the true
arrival into a fine timing search. At these rates it is small compared with a
symbol. This still is not qualification of blind timing/epoch acquisition or a
months-long clock-acquisition campaign. The separate two-path acquisition screen
uses three timing branches and three epoch alternatives.

“Static” here means fixed ray gains with the prescribed two-ray Doppler beating;
it does not mean an unchanging received waveform. Optional OU cases use
independent stationary proper-complex Gaussian ray gains with unit mean power
and a separately declared coherence time. That coherence is **not inferred from
0.001 Hz Doppler separation**. The screen also includes a 0.5-radian daily phase
excursion, a day-10 to day-15 outage with one-hour ramps, and 1% slow plus 1% fast
frequency-selective amplitude perturbation. These are specified software models,
not measured VLF propagation, receiver noise or month-long GPSDO behavior.

## Completed independent holdouts

| Frozen cohort | Requested duration | C/N₀ | Primary method | Correct / wrong accepted / missed | Status |
|---|---:|---:|---|---|---|
| `static30-cn46` | 30 days | −46 dB-Hz | `static-frequency-ucb` | **600 / 0 / 0** of 600 | Complete |
| Paired subset of `static30-cn46` | 30 days | −46 dB-Hz | `rbpf-ucb` | **64 / 0 / 0** of 64 | Complete; same physical paths |
| `static38-cn48` | 38 days | −48 dB-Hz | `static-frequency-ucb` | **296 / 0 / 4** of 300 | Complete, strict gate |
| Paired subset of `static38-cn48` | 38 days | −48 dB-Hz | `rbpf-ucb` | **64 / 0 / 0** of 64 | Complete; same physical paths |
| `ou8-cn36`, OU coherence 100,000 s | 8 days | −36 dB-Hz | `rbpf-ucb`, 256 particles | **300 / 0 / 0** of 300 | Complete |
| `null38` | 38 days | −46 dB-Hz reference | Noise-only control, four methods | **0 / 32 false admissions** each | Complete |
| `wrong-key38` | 38 days | −46 dB-Hz | Wrong-key control, four methods | **0 / 32 false admissions** each | Complete after adapter repair |

The completed 600 paths contain 300 of each source bit, with independent physical
seed roots. The balanced all-success bound is `0.05^(1/600)`; it permits different
success probabilities between the two fixed bit strata. The paired 64-path RBPF
bound is only **95.427%**, so that comparison does not independently demonstrate
99% reception. Known-variance versions of both methods also accepted every one
of these paths. Counts are preserved in the static cohort (`evidence/sub9-holdout/static30-cn46/campaign.json`)
and its receiver invocation record (`evidence/sub9-holdout-analysis/static30-cn46/runs.json`).

At −48 dB-Hz all 300 raw winning bits were correct, but four failed the strict
admission gate. Its conservative balanced-bit lower bound is **95.267%**, not
99%. Re-gating the same scores at the relaxed threshold gives 300/300 and a
99.006% balanced-bit lower bound. On the eight-day OU paths, `gaussian-ou`,
`residual-ucb` and `angular-gaussian` also obtained 300/300 strict receptions.
These are paired analyses of the same observations, not extra independent
trials. `static-frequency-ucb` obtained 254/300 strict and 269/300 relaxed, with
300/300 raw correct bits: its channel model loses admission evidence even when
the winning bit remains correct.

The 99.006% bound describes the equally weighted mean of the two bit-stratum
success probabilities. For the OU cohort's 150 trials per bit, each separate
one-sided lower bound is 98.023%. Even the 30-day cohort's separate 300-trial
per-bit bounds of 99.006% do not jointly establish both bits above 99%; applying
a two-bit Bonferroni correction gives 98.778% per bit. Balanced-mean coverage and
simultaneous guarantees for every source bit are different requirements.

The actual propagated energy and total sampled span further distinguish these
cohorts from nominal-duration estimates:

| Cohort | Sampled capture seconds | Nominal linear E/N₀ | Actual linear E/N₀ range |
|---|---:|---:|---:|
| `static30-cn46` | 2,594,740 | 65.1081 | 64.9586–65.1798 |
| `static38-cn48` | 3,298,758 | 52.0352 | 52.1247–52.2835 |
| `ou8-cn36` | 706,693 | 173.6216 | 90.1719–371.0199 |

Nominal energy multiplies the requested duration by C/N₀. Actual energy includes
the realized channel and rounded waveform; it is evaluation-only truth and is
not supplied to the candidate decision. The wide OU range is part of the fading
experiment, not a constant-energy substitute for it.

These are pointwise bounds for fixed-size cohorts, not simultaneous confidence
claims over all methods and configurations. Development screens, pilot runs and
paired receiver evaluations are not pooled into additional independent trials.
The four null methods (`static-frequency`, `static-frequency-ucb`, `rbpf`,
`rbpf-ucb`) each produced 0/32 false admissions at both gates. This leaves an
approximately 8.94% one-sided upper bound per completed capture and cannot
empirically establish a 10⁻⁴ daily false-admission probability. The same four
methods produced 0/32 false admissions on the separate wrong-key controls; this
is not a noise-only daily-rate calibration or an additional 128 physical trials.

## Admission accounting

The declared family contains 11 method alternatives, four duration alternatives,
two candidate bits and one scheduled acquisition attempt per 60 seconds. Half
the false-admission budget is allocated to scores and half to calibration
failure. The resulting log-likelihood gates are:

| Operational target | Scheduled attempts | Log gate |
|---|---:|---:|
| At most 10⁻⁴ probability per 24 hours | 1,440 | 21.6532227596 |
| At most 1% probability per 12 hours | 720 | 16.3549053930 |

The relaxed comparison re-gates the existing scores while retaining the stricter
noise calibration; it does not refit a more permissive calibration bound. The
expanded 11-method/four-duration family must not be substituted into older
nine-method/three-duration evidence without recalculating its gate.

These are mathematical family bounds under the specified likelihood, proper
Gaussian noise and independent residual-calibration assumptions. They cover the
predeclared schedule and alternatives, not arbitrary adaptive monitoring or
post-selection of promising particle seeds. Known-variance scores and residual
upper-confidence-bound (`ucb`) scores have different assumptions. A normalized
fixed trajectory-bank likelihood has a conditional null interpretation; a
randomized particle likelihood estimator has its corresponding joint scope over
observation and algorithm randomness. An unbiased likelihood estimate does not
make its log unbiased, and does not justify selecting the best random run.
The [analyzer](analyze.py) records these conventions in [analysis.json](analysis.json).

## Development screen and failures near the edge

The screen used two independent physical paths per configuration, each with a
requested 38-day duration. The entries below are **strict / relaxed correct
accepted counts out of two**. They locate failures and guide frozen validation;
they cannot support reception-rate claims.

| Configuration | Gaussian OU | RBPF UCB | Static-frequency UCB |
|---|---:|---:|---:|
| Fixed rays, −36, −46 or −48 dB-Hz | 2 / 2 each | 2 / 2 each | 2 / 2 each |
| Fixed rays, −50 dB-Hz | 0 / 1 | 1 / 1 | 1 / 1 |
| Fixed rays, −56 dB-Hz | 0 / 0 | 0 / 0 | 0 / 0 |
| Rates 0.0036, 0.36 or 3.6 Hz at −46 dB-Hz | 2 / 2 each | 2 / 2 each | 2 / 2 each |
| Each-1% perturbation or acquisition bank | 2 / 2 each | 2 / 2 each | 2 / 2 each |
| Daily phase excursion or five-day outage | 1 / 2 each | 2 / 2 each | 2 / 2 each |
| OU coherence 10,000 s, −46 dB-Hz | 0 / 0 | 0 / 0 | 0 / 0 |
| OU coherence 100,000 s, −46 dB-Hz | 0 / 1 | 0 / 1 | 0 / 0 |
| OU coherence 1,000,000 s, −46 dB-Hz | 1 / 1 | 1 / 1 | 0 / 0 |

There were no wrong accepted bits in these completed screen cases. The failures
under OU evolution are substantial: success with fixed ray coefficients does not
establish comparable performance when the channel repeatedly loses coherence.
The screen plan (`evidence/sub9-screen/plan.json`), generation results (`evidence/sub9-screen/results.json`)
and analysis records (`evidence/sub9-screen-analysis/analysis-runs.json`) retain
individual cases, including additional angular and residual methods.

The first 15 Gaussian analysis invocations failed while serializing an infinite
coherence value, with the exact notice
`triad-noise-study failed: ValueError: Out of range float values are not JSON compliant: inf`.
The original failures are retained. All 15 corrected invocations completed in the
rerun record (`evidence/sub9-screen-analysis/gaussian-reruns.json`). These were
report-generation failures, not reception misses, and were not counted as
successful analyses before the corrected runs.

Three initial wrong-key receiver invocations, covering 64 path-evaluation
attempts across adapters, rejected the control label before scoring. The
adapter repair passed its 11- and nine-check focused suites. An initial repair
launcher also failed with a quoting-related `SyntaxError` before any receiver
analysis executed. The corrected uploaded launcher completed the receiver rerun
in 71.186 s after 20/20 focused checks. These setup failures and successful
reruns are retained separately from receiver misses or false admissions.

The four strict misses at −48 dB-Hz were also examined after the holdout with
eight particle replicas at 512 and 8,192 particles. The 8,192-particle stratified
`rbpf-ucb` log mean likelihoods for their true bits were 21.627, 18.014, 21.224 and
21.502, below the 21.653 gate; the first lies within algorithm-seed variability
of the gate. These selected-case diagnostics (`evidence/sub9-miss-path-*.json`)
do not demonstrate a rescue or modify the original correct-reception count.

An earlier day-span negative control used the sub-9-kHz physical channel but the
old 1.5 ms / 0.3 Hz receiver basis. Its correct-bit block projection retained only
50.55% of signal energy. The [diagnostic](theory/day-oracle-diagnostic.json) and
mismatched workspace (`evidence/day-static/manifest.json`) are preserved alongside
the matched replacement (`evidence/day-matched/manifest.json`). This demonstrates
loss before the final receiver likelihood: a later phase algorithm cannot
recover evidence already projected out by the wrong channel basis.

## Benchmarks and remaining sensitivity gaps

The theoretical diagnostics distinguish presence detection from correct-bit
reception. At the strict gate, for a fixed-energy signal with the stated ideal
alignment/noise assumptions, the 99% **presence** requirements are:

| Ideal reference | Required linear E/N₀ |
|---|---:|
| Known-channel Neyman–Pearson detector | 35.900 |
| Known-channel fixed likelihood gate | 43.303 |
| Unknown rank-two channel, Gamma-tail detector | 42.304 |
| Rank-two Gaussian-prior receiver, fixed-energy signal, fixed likelihood gate | 48.052 |

The last row would require 22.141 days at −46 dB-Hz before waveform, acquisition
or model mismatch losses. The successful requested 30-day holdout has nominal
energy 1.319 dB above that reference, 1.873 dB above the rank-two Gamma-tail
reference and 2.585 dB above the known-channel Neyman–Pearson reference. The
38-day −48 dB-Hz allocation is respectively 0.346, 0.899 and 1.612 dB above
those presence references, but its strict holdout has four misses. These are
energy comparisons to simpler presence problems, **not a demonstrated universal
gap to optimal correct-bit reception**.
The observed threshold has not been bracketed by a large independent duration
sweep. The [day oracle derivation](theory/day_oracle_diagnostic.py) and
[results](theory/day-oracle-diagnostic.json) give assumptions and 90% alternatives.

Random fading changes these comparisons. The ideal known-phase/CFO covariance
oracle at −36 dB-Hz predicts 99% presence at about 8.155, 6.190 and 9.831 days
for OU coherence times 10,000, 100,000 and 1,000,000 seconds respectively. An
independent 100,000-draw validation with a finer eigenmode grid obtained 98,982
presence admissions for the 100,000-second case at its fitted 6.190-day point.
This validates that particular oracle calculation, not the competing-bit sampled
receiver. The completed eight-day OU holdout allocates 29.3% more duration
(1.114 dB nominal energy) than the 6.190-day presence reference. Its 300/300
correct receptions demonstrate this selected sampled case, not the minimum
duration or a general 1.114 dB loss bound.
See the [OU model calculation](theory/ou-bound-diagnostic.json) and
[independent validation](theory/ou-bound-validation.json).

At −56 dB-Hz, the link-budget calculation gives only **1.85% strict or 11.86%
relaxed presence probability after 38 days**, even for the completely known
waveform, phase, timing and channel. Its inverse calculation requires about
**165.42 days for 99% strict presence** or **98.08 days for 90% relaxed presence**.
These optimistic known-waveform references use this study's conservative
per-hypothesis false-tail allocation. They are not universal operational
impossibility bounds. Calibrating the correlated repeated windows more tightly,
or declaring fewer scheduled acquisition attempts, could reduce that allocation
penalty. The current fixed-window union bound does not demonstrate near-optimal
continuous monitoring.

The optional 0.22 dB energy derating and finite-frequency-mixture penalty in that
diagnostic is explicitly hypothetical. It is not a proven continuous-CFO loss
bound for stochastic OU channels. Likewise, the [day frequency diagnostic](theory/day-frequency-diagnostic.json)
finds that outward-rounded FFT nodes represent a different finite prior from a
continuous uniform prior. Normalization preserves the corresponding null
likelihood interpretation, but does not remove approximation loss or establish
an exact physical prior.

## Sampling, particle convergence and measured acceleration

The sampling refinement (`evidence/sampling-refinement/main/results.json`) compares
1, 4 and 64 samples/s at rates 0.036 and 0.36 Hz for baseline, daily phase and
outage cases; a separate pair (`evidence/sampling-refinement/rate-3p6/results.json`)
compares 4 and 64 samples/s at rate 3.6 Hz. These are deterministic, noiseless
checks. At rate 0.036, compensating the known payload-origin phase difference
gives a relative channel-basis Gram difference of about 4.39×10⁻⁶ between 1 and
64 samples/s. The uncompensated waveform difference includes the changed ray
phase at the shifted origin. The finer sampled case is a refinement reference,
not analog truth; these checks do not qualify stochastic noise or detection
equivalence across sample rates.

The fixed-ray (`evidence/sub9-convergence-static.json`) and
OU (`evidence/sub9-convergence-ou.json`) particle studies use 32–8,192 particles and
eight algorithm replicas per size on each held-fixed physical observation.
Likelihoods are averaged before taking logs; the best seed is not selected.
At the fixed-ray −50 dB-Hz edge, 32-particle true-pattern scores span approximately
21.24–24.62 across replicas, crossing the strict gate. This is a measurable
particle-resolution issue, not new independent reception evidence. More
particles and explicit convergence checks remain necessary near a decision
boundary; these studies do not establish extreme null-tail calibration.

The optional stratified frequency-initialization comparison subsequently
completed on GPU, with **16 algorithm replicas at each of 32, 128, 512, 2,048 and
8,192 particles**. It reuses one fixed-ray −50 dB-Hz screen input and one
−46 dB-Hz OU input with 1,000,000-second coherence; these are not new independent
physical trials. Independent uniform (`iid`) initialization remains the default;
stratification is an explicit alternative. For the true-bit `rbpf-ucb` log score:

| Held-fixed input | Particles | IID population SD | Stratified population SD |
|---|---:|---:|---:|
| Fixed rays, −50 dB-Hz | 128 | 0.605110 | 0.287881 |
| Fixed rays, −50 dB-Hz | 8,192 | 0.052495 | 0.024580 |
| OU, −46 dB-Hz | 128 | 0.121813 | 0.038317 |

At 32 particles on the fixed-ray input, 13/16 IID replicas and 16/16 stratified
replicas exceeded the reference gate. Those are **diagnostic gate comparisons**,
not admissions or sensitivity estimates: choosing initialization or a seed after
seeing the scores is not qualified reception. Every tested mode and particle
count on the OU input remained below the strict gate; its log mean likelihood
stayed around 15.4 versus the 21.653 gate. The variance reduction is conditional
on these two observations and does not establish a systematic sensitivity gain.

The retained results are `evidence/sub9-stratified-static-cn50.json` and
`evidence/sub9-stratified-ou-1e6.json`; their wall times were 71.87 and 77.16 s.
An initial launcher setup failure omitted required `cwd`/`max_seconds` arguments;
its nonzero exit is retained in `evidence/sub9-stratified-launch-failure.json`.
The corrected jobs completed. This setup failure is separate from physical
receiver performance.

The CPU/GPU benchmark (`evidence/rbpf-benchmark.json`) uses the same predeclared
phase bank on CPU and an RTX PRO 6000 Blackwell Server GPU. For 256 candidates,
2,048 particles, 64 blocks and two taps, FP32 bulk processing achieved **26.46×
resident speedup** and **3.78× single-use speedup**, including preparation and
transfer once in the latter. The small 32-candidate/128-particle case was slower
on GPU: 0.62× resident and 0.69× single-use speedup. CPU used two threads.

Covariance preparation and sensitive phase/log reductions retain FP64; TF32 is
disabled. The large-case FP32 maximum log-score difference from the CPU FP64
reference was about 2.83×10⁻⁵, with no candidate-ranking or benchmark-gate changes.
The closest reference score was 0.035 from that benchmark gate, so this is not a
rare-tail numerical qualification. These timings measure likelihood batching,
not complete waveform generation, export and acquisition throughput.

## Reproduction and check status

The [screen launcher](reproduction/screen.py), [screen analysis](reproduction/analyze_screen.py),
[Gaussian rerun](reproduction/rerun_gaussian.py), [holdout scorer](reproduction/score_holdout.py)
and [follow-up launcher](reproduction/followup_holdouts.py) preserve actual
commands and frozen choices. Their original execution paths must be mapped to
the local checkout and evidence directory when replayed. Source/binary and
workspace hashes are recorded where available; retained manifests alone do not
reconstruct omitted sampled arrays. No transmitter provenance is inferred from
a mathematical diagnostic's source hash.

Recount the retained evidence without changing it:

```sh
study_dir=docs/validation-data/triad-v3/2026-11-01-sub9
unpack_dir=$(mktemp -d /tmp/triad-sub9.XXXXXX)
tar -xzf "$study_dir/evidence.tar.gz" -C "$unpack_dir"
python3 "$study_dir/analyze.py" \
  --input "$unpack_dir/evidence" \
  --output "$unpack_dir/analysis.json"
```

The [theory reproduction record](theory/reproduction.json) supplies dependencies,
commands and hashes. Both day diagnostics accept a workspace path, allowing the
retained `evidence/day-static` workspace to replace their original local path.
The [source manifest](source-provenance.json) binds the final implementation and
focused test sources. Each measured run retains its own source/binary hashes;
the initial month pilot predates the captured binary hash and is not claimed
to have that exact binary binding.

The separate [retained-array check](checks/retained-array-hashes.json) recomputed
1,297 manifest hashes and 46 retained array hashes without a mismatch. It
explicitly identifies 7,099 omitted referenced files and ten retained oracle
files lacking report hash references. Those files are not claimed verified.
Run [its verifier](checks/verify_retained_arrays.py) with the same extracted
`--input` directory and a separate `--output` JSON file. This does not reconstruct
omitted waveforms or turn the software study into a physical measurement.
An [archive replay](checks/archive-replay.json) extracted the final compressed
evidence into a fresh directory and reproduced both analysis JSON files byte
for byte. [SHA256SUMS](SHA256SUMS) covers the retained study artifacts.

The focused validation status is **14 triad tests and 78 Python checks passed**.
Both optional paired stratified-GPU convergence jobs completed, with the
diagnostic scope described above. Corrected wrong-key controls also completed.
The ordinary Robust receiver regression campaign is outside this isolated study.

Remaining practical limits include empirical VLF noise and oscillator models,
full blind epoch/timing acquisition, tighter calibration of correlated continuous
monitoring, continuous field operation, channel-model
selection across unpredictable long-term changes, and near-threshold sensitivity
of the finite particle and frequency approximations. Fixed-ray success at
−46 dB-Hz does not resolve the OU failures at the same C/N₀, nor the −56 dB-Hz
38-day failures. These are quantitative obstacles for subsequent development,
not reasons to extrapolate the completed success to untested links.
