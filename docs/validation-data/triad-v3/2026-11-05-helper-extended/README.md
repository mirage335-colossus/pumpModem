# Extended Helper-assisted qualification

This study extends the isolated three-carrier experiment beyond the previous
18-kHz input / 900-Hz DATA fixture. It does **not** qualify the entire requested
Cartesian product or multi-day Helper-assisted reception. Those boundaries are
quantified below; successful execution, native planning and received bits remain
separate evidence. Ordinary Robust integration and its regression campaign are
outside scope.

The base revision is `388d0749bc3fae9c529a5d298946ab2d6ee19791`. Candidate source
identities, frozen protocols, failures and compact sampled results accompany
this report. The primary maintained entry points are the
[receiver guide](../../../triad-helper.md), [coverage map](../../../triad-helper-coverage.md)
and [performance matrix](../../../triad-performance-matrix.md).

## Implementation changes

- The exporter accepts explicit integer sample rates of at least 6 kHz,
  divisible by five; its default remains 18 kHz. Four-hundred-millisecond dwell
  timing, encrypted patterns, Added Symbols/interleaving, Helper waveform and
  lead, continuous shared propagation and complete tails are preserved.
- The opt-in `rate-aware` DATA frontend chooses a decimation rate and FIR from
  the public DATA bandwidth, oscillator bounds, path/modulation sidebands and
  actual Helper pilot/auxiliary locations. It rejects overlapping declared
  pass/stop requirements instead of quietly filtering part of the signal.
  These finite engineering envelopes are not compact-support guarantees.
- Whole-active source-power reference version 2 sums every integer source
  sample for rich patterns or failed quadrature convergence. It uses one
  public bit-0 reference gain for all bits, epochs and controls. This repairs
  high-rate quadrature failures without normalizing received fading energy.
  The bounded fallback rejects more than 4,194,304 source samples explicitly.
- Long rate-aware FIRs use overlap-add when at least 257 taps and four input
  rows per tap justify that regime. Complete tails and the original decimation
  phase are preserved. A paired 524,288-row / 3,273-tap diagnostic took
  1.32525 s with direct filtering and 0.043046 s with the selected path:
  **30.8× for that filter stage**, relative waveform error `1.99e-15`.
  This is neither a whole-receiver speedup nor a confidence-qualified CI model.

Historical `legacy` and `wide` frontend taps, prepared samples and receiver
results matched exactly in the independent compatibility comparison. Strict
FP32 bulk arithmetic remains distinct from wider preparation and likelihood
reductions; TF32 is disabled. These additions do not make the isolated Python
receiver an ordinary receiver or an audio-device backend.

## Frozen sampled campaign

The protocol freezes twelve cohorts before validation: rates 3.6/12/18/24 kHz,
Added ×100/×1000, selected HF/EME/Sub9 channels with Each65 modulation,
free-crystal/IC-7100 clocks, FHSS envelopes, within-bit wait tiles and known
10-ms ADC gaps. It requests 740 captures: 228 calibration captures, 336 signal
captures and 176 held-out DATA-absent controls. The low-rate C/N₀ cases are
−10 and −16 dB-Hz, not −60 dB-Hz. Requested duration and actual tile-rounded
bit duration are recorded separately.

The source key is fixed by the protocol; fresh channel/noise seeds do not
constitute a qualification across all keys. These C/N₀ values are injected
noise settings, not the GUI planning-target label. In particular, the HF8-dB-Hz cases deliberately weaken the nominal
4-W/180-dB link, whose configured noise density would otherwise give
20.0206 dB-Hz. The EME10.0206-dB-Hz cases use the corresponding physical
40-W/200-dB link. Actual propagation and useful-energy changes remain separate.

Each cohort freezes a 19-capture Helper-present/DATA-absent rank gate at
`alpha=.05`, then scores separate signal and null seeds. Some configurations
share seeds intentionally for paired comparisons. Results must not be pooled
as 740 independent reliability trials. A five-percent per-capture gate is a
sensitivity diagnostic, **not** either requested operational daily
false-admission target. Sixteen null trials are far too few to establish those
targets. The campaign scores the actual emitted waveform with its selected
oscillator, two-ray delay/Doppler and continuous dispersive selective modulation;
the DATA receiver fits its documented static two-ray model, not the complete
selective-modulation law.

All cases use the bounded arrival prior, thirteen candidate epochs and the
full integer timing grid at the selected DATA output rate. The separate older
[broad study](../2026-11-05-helper-broad/README.md) retains default multi-second
arrival cases. Reusing those observations does not qualify every interaction
between the new rates and that larger search. The high-rate native placements
require higher DATA/pilot frequencies; the familiar 1725/1775-Hz audio placement
cannot accommodate arbitrary bandwidth.

All twelve cohorts completed. Independent recount verified all 740 manifests,
truth files and paired erasure/input/validity receipts, the frozen protocol,
nineteen source hashes and the actual remote native binary. Raw arrays were
hashed before deletion; the recount validates the retained receipts, not those
deleted bytes a second time. Two diagnostic re-exports separately matched
selected original I/Q hashes exactly.

| Sampled case | Actual bit duration | Correct / wrong / missed | DATA-absent admissions | Equal-bit mean 95% lower bound |
| --- | ---: | ---: | ---: | ---: |
| 3.6 kHz, HF worst ×3, Each65, 8 dB-Hz | 8.8 s | 32 / 0 / 0 | 1 / 16 | 91.1% |
| 12 kHz, same HF settings | 8.8 s | 32 / 0 / 0 | 3 / 16 | 91.1% |
| 18 kHz, same HF settings | 8.8 s | 32 / 0 / 0 | 1 / 16 | 91.1% |
| 24 kHz, same HF settings | 8.8 s | 32 / 0 / 0 | 0 / 16 | 91.1% |
| 24 kHz, paired flat/AM-off control | 8.8 s | 32 / 0 / 0 | 2 / 16 | 91.1% |
| 3.6 kHz, EME, Each65, FHSS, 10.02 dB-Hz | 8.8 s | 32 / 0 / 0 | 0 / 16 | 91.1% |
| 1 Hz, Added ×1000, Sub9 moderate, Each65, −10 dB-Hz | 384.4 s | 27 / 0 / 5 | 1 / 16 | 58.0% |
| 10 Hz, Added ×100, Sub9 moderate, Each65, −10 dB-Hz | 388 s | 28 / 0 / 4 | 0 / 16 | 61.7% |
| 1 Hz, Added ×1000, paired −16 dB-Hz | 384.4 s | 5 / 0 / 27 | 1 / 16 | 2.8% |
| 3.6 kHz, HF, free crystal/IC-7100, margin1, 8 dB-Hz | 8.8 s | 8 / 0 / 0 | 0 / 8 | 68.8% |
| 3.6 kHz, EME/FHSS, within-bit waits3 | 79.2 s | 8 / 0 / 0 | 0 / 8 | 68.8% |
| 3.6 kHz, EME/FHSS, known 10-ms ADC gaps | 8.8 s | 32 / 0 / 0 | 0 / 16 | 91.1% |

The pointwise confidence bounds assume independent representative channel/noise
trials within each fixed-bit stratum and the frozen gate. For balanced all-
success cohorts the AM-GM bound is `0.05^(1/N)`; other rows use the mean of two
97.5% one-sided Clopper–Pearson bounds. They are not simultaneous guarantees
across the table, and do not establish 99% correct reception. Null uncertainty
is also broad: even 0/16 has a one-sided 95% upper bound of 17.1%; 0/8 gives
31.2%, and 3/16 gives 41.7%. The low-rate rows had 1, 2 and 9 raw candidate-bit
errors respectively; their admission gates rejected those wrong candidates.
No wrong bit was admitted in any cohort.

Helper narrowed in four cohorts: the three EME/FHSS variants and the free-
crystal/IC-7100 case. The latter reduced 2,149 public CFO candidates to 3–4;
this is a measured bank reduction of about 537–716×, **not** an end-to-end
receiver speedup, because that cohort did not execute an unassisted baseline.
The basic EME/FHSS rows reduced 43 candidates to 2–3; the wait case reduced
369 to 10–11. All high-rate OCXO HF rows retained their already-small three-bin
bank (`no_bank_shrink`). All low-rate rows used the fallback after a flat or
insufficient Helper profile. Successful reception in those rows must not be
credited to Helper narrowing.

Tile rounding is material: Added ×1000 requested eight seconds but emitted a
384.4-second bit; Added ×100 requested 384 seconds and emitted 388; the wait
case requested 24 active seconds and occupied 79.2 seconds. Its within-bit
waits remain different from whole-bit Symbol Burst3. The paired ADC-gap case
retained 32/32 correct admissions for these seeds; it is not a physical missed-
hop or RF-retuning result.

The complete command took **6,922.723 seconds (115.38 minutes)**. Recorded
native export stages totaled 5,808.355 seconds; prepared receiver scoring
summed to 683.630 seconds. These timers have different boundaries; imports,
setup, Helper/FIR preparation, writes, pauses and other overhead belong to the
full command. Native generation dominates this research workload. This is an
occasional qualification campaign, not a proposed routine regression duration.


## Paired interference diagnostics

These compare three actual native captures with identical source/channel/noise
seeds: composite, DATA-only and Helper-only. C/N₀ is deliberately set to
200 dB-Hz to expose leakage without dominant thermal noise. The complete causal
FIR and native channel tails are retained. Integrated leakage is measured over
a public DATA window including arrival uncertainty and tails; it is not a
matched-filter error probability.

| Case | DATA energy after/before frontend | Helper/DATA energy in expanded DATA window | DATA change in actual raw 36-s Helper observation |
| --- | ---: | ---: | ---: |
| Rate 3.6 kHz, strong EME Helper, FHSS, Each65 | 0.999957791 | 0.000420577 (−33.76 dB) | Exactly zero for this seed |
| Rate 1 Hz, Added ×1000, Sub9-moderate, Each65 | 1.000003543 | 5.05665e−9 (−82.96 dB) | Exactly zero for this seed |

These channels also change energy: emitted/received DATA energies were
6.38595/9.46513 for the FHSS case and 384.4/605.319 for Added ×1000 in normalized
units (received changes +1.709 and +1.972 dB). Nominal C/N₀ is defined before
these selected propagation changes. The retention ratios above compare the
frontend against received energy, so they do not mislabel that channel gain as
receiver sensitivity. FHSS useful energy and elapsed duration are different.

The slight ratio above unity is finite FIR passband ripple. Shared seeded noise
implies `composite − DATA-only − Helper-only = −noise`; measured residual RMS
is 1.00739 and 0.999869 times the predicted filtered thermal-noise RMS. That is
an additive-model check, not a nonlinear-noise experiment. Zero raw difference
means identical inputs to the Helper estimator for these seeds; it does not
establish post-lead phase-reference quality or rare-channel margins.

An earlier Rate 24 kHz diagnostic measured retention 0.999994968 and leakage
2.37820e−5 (−46.24 dB). Its DATA crop was about 0.2 seconds wider than necessary,
and its zero leading difference used an early filtered window rather than the
exact raw Helper window. Those results and the erratum are retained. The final
script corrects source versus capture coordinates, bounds both endpoint clocks,
and uses exactly the receiver's public Helper observation. No earlier artifact
was silently replaced.

## CPU/GPU timing on the same sampled input

An independent Rate 24 kHz HF capture used exactly the same templates, immutable
prepared samples and 13-epoch × 4,345-timing × 3-CFO search (169,455 groups) on
both backends. An AMD EPYC 9355 used two numerical threads; the GPU was an
RTX PRO 6000 Blackwell Server Edition. One cold and three interleaved warm
executions were measured per backend, with CUDA synchronization.

| Timing boundary | CPU | GPU | CPU/GPU ratio |
| --- | ---: | ---: | ---: |
| Warm prepared DATA score, median of 3 | 2.33272 s | 0.430279 s | 5.42× |
| Internal correlation batches, median of 3 | 1.75834 s | 0.0110353 s | 159.34× |
| Composed first receiver use | 9.13932 s | 7.34895 s | 1.24× |

The internal stage includes CFO modulation, FFT/IFFT and correlation indexing;
it excludes the base FFT and observation upload, which remain inside the outer
score timer. The first-use composition includes constructors, shared Helper/FIR
preparation and GPU initialization; native generation, Python imports and
process startup are reported separately. It is not an isolated fresh-process
latency comparison. CPU/native generation remains important to campaign wall
time; no 159× end-to-end speedup is claimed.

All eight scores selected the correct bit 1 and identical winning parameters.
Maximum retained log-likelihood difference was `2.11339e-5`, with no retained
rank changes. The Helper search was already only three bins, so it fell back
with `no_bank_shrink`; this benchmark measures GPU acceleration, not Helper
narrowing. GPU allocator peak was about 0.672 GiB. Three warm repetitions do not
establish a sustained-performance or CI regression confidence interval.
The main campaign was paused for 46.637 seconds and resumed safely for this
benchmark; its wall measurements retain that pause and should not be treated
as uncontended throughput qualification. The 63.98-second oracle and
17.45-second projection diagnostics also used two host threads concurrently
with the campaign; the campaign is a reception experiment, not a sustained
processor-throughput qualification.

## Diagnosing a maximum-spreading miss

The −10 dB-Hz Added ×1000 cohort included one raw bit error (rejected by the
admission gate). A separate diagnostic regenerated that exact capture and a
successful control, verifying byte hashes before comparison. It then generated
both candidate waveforms through the complete, known physical channel and
subtracted the known Helper. This oracle knows arrival, epoch, oscillators,
channel coefficients and the time-varying modulation; the practical receiver
does not. The diagnostic is deliberately outcome-selected, not an independent
estimate of population reliability.

| Selected trial | Actual bit energy/noise | Known-channel bit error probability | Oracle observed decision |
| --- | ---: | ---: | --- |
| Raw-error trial | 9.9143 | 0.0005357 | Correct and admitted |
| Correct practical control | 40.0392 | 4.94e−11 | Correct and admitted |

Both emitted essentially the same DATA energy. Propagation reduced the first
trial by about 5.89 dB relative to emitted energy, while the control gained
about 0.18 dB. The 6.06-dB received-energy difference is real within this channel
model; it must not be attributed to receiver computation. The remaining
oracle/practical gap combines unknown acquisition and channel inference. This
experiment does not isolate their individual contributions or establish that
the practical receiver is near the unknown-channel limit.

For scale only, assigning a 5% false-admission budget by a union bound across
two known waveform tests in each of 7,098 acquisition groups gives a normal
threshold of 4.492. The raw-error trial's own-word projection was 4.207, below
that conservative threshold despite passing the oracle's two-word, known-
alignment threshold of 1.960. This is an engineering comparison, not a lower
bound on an optimal acquisition receiver: correlated hypotheses can permit a
less conservative threshold. It explains why the oracle success alone does not
justify lowering the operational gate.

A second, predeclared diagnostic projected the failed trial's exact noiseless
DATA mean onto the current two-ray raw-source family at known true arrival,
static clock rates and static CFO. It reused the existing template and required
regenerated bytes to match the oracle. A two-column SVD retained **28.5272%**
of the energy: **5.4474 dB representation loss**, leaving projected
`E/N₀=2.82828` from actual `9.91432`. The fit had rank two and normalized
orthogonality and energy-identity errors below `3e−15`. It took 17.445 seconds
including two native exports.

Raw noise is scalar-white, so this comparison avoids a false diagonal
approximation to the FIR output covariance. It is still a raw mean-model
measurement, not the practical filtered likelihood or its blind-search loss.
The residual includes dispersive amplitude/phase modulation, stochastic phase
and native-versus-template interpolation differences; it is not exclusively an
AM penalty. It is also one outcome-selected trial, not a population loss
estimate. This is meaningful remaining headroom, and a near-bound practical
receiver claim is not supported here.

Adding the full dispersive modulation basis would require many correlated
columns and new rank control/calibration, with substantial work and nuisance-
fitting cost. A richer fit may retain more signal while also admitting more
noise. The next useful comparison is physically constrained temporal/channel
inference against this retained oracle, at the same acquisition and false-
admission budgets; raw projected energy alone must not decide the algorithm.
The independent oracle process took 63.98 seconds including eight native
exports. Exact input identities, arithmetic and finite-noise corrections are
retained for both diagnostics.

## Full-domain and multi-day boundaries

The necessary-constraint census counts all **2,500,485,120** requested tuples
using 408 distinct geometry projections. It is exact under its declared
predicates, which apply to the default 18-kHz / at-most 400-Hz DATA frontend.
It is **not** billions of native plans or receiver simulations; the new opt-in
sample-rate/frontend cases lie outside that fixed-fixture census. Its residual
220,631,040 cells are explicitly unqualified. The
[coverage map](../../../triad-helper-coverage.md) gives exclusive and overlapping
failure counts, precedence and remaining native checks.

A separate 68-row native audit relaxes the fixed audio clock/IF constraints by
choosing deliberately generous sample clocks and DATA centers for every
requested rate at Added ×1/×10/×100/×1000. Sixty-six plan-only calls took 4.47
seconds; two candidate sample clocks exceeded uint32 and were recorded without
launching. Fifteen candidates failed the source tile cap, ten the Helper lead,
and seven a necessary 2 GiB raw/template size bound; 34 passed these limited
screens. They are not received configurations or an optimal placement search.
The frozen audit mislabels its full nominal bandwidth W as a half-bandwidth in
one metadata field; the actual placement formula correctly uses W/2. The
original records and an explicit erratum are retained.

Important unresolved boundaries are concrete:

- Helper requires DATA at least 1725 Hz. Half-rate low-frequency placement
  conflicts with that interface. At Rate .001 ×1000 the source pulse halo is
  48 seconds, exceeding the unchanged 38-second lead. Changing a label or an
  acquisition threshold cannot fix this timing conflict.
- At 1 MHz and 30 MHz the existing minimum-duration source would exceed its
  1,000,000-fine-cell tile cap even with Added ×1. Sample-rate support alone does
  not qualify these modes. Audio-device and spectrum constraints are separate.
- The requested whole-bit Symbol Burst3 is not implemented by this single-bit
  Helper fixture. Within-bit wait tiles are explicitly different. Fake FHSS
  does not validate physical RF retuning or synthesizer settling.
- At DATA −60 dB-Hz and the inherited Helper/DATA powers, the fixed 36-second
  Helper energy is only 0.00036 (HF) or 0.0135 (EME) in units of `E/N₀`. For the unit-received-energy-gain reference, even an
  ideal known-waveform/phase one-candidate detector at 1% false admission has
  only about 1.07% or 1.53% detection probability. Longer DATA alone cannot repair
  that fixed initial observation.
- Existing standalone 850-day −60 dB-Hz sampled results remain valid in their
  stated scope. They do not contain Helper. An 850-day 18-kHz Helper capture
  would require 22.47 TB for raw complex samples and validity alone, before
  templates; the current fixture limit is 2 GiB. A validated multirate streaming
  composite/channel/acquisition architecture is still missing.

The [long-duration assessment](long-duration-boundaries.md) derives the favorable
energy and frequency-estimation references, distinguishes bit discrimination
from admission, documents prior 850-day runs, and records the new complete
multiweek channel chunk-equivalence test. No scalar calculation, long simulated
calendar or source hash substitutes for end-to-end multi-day Helper reception.

## Cheap checks and occasional qualification

Routine changes should use the focused Python tests in the performance matrix
and the existing native channel test. Do not add this 740-capture research
campaign to normal CI. The final local check ran 66 focused tests in 2.089 seconds;
the frozen RunPod candidate ran 79 tests in 1.669 seconds, including its available
CUDA path. These times are correctness-check elapsed times, not receiver cost
forecasts. The native channel suite passed in 10.069 seconds, including a full
38-day-requested, tile-rounded low-rate stream compared sample-for-sample across
257- and 4096-row chunks with less than 1 MiB channel workspace. That test has no
Helper or receiver and does not model nonzero oscillator error.

```sh
PYTHONPATH=tools:tests OMP_NUM_THREADS=2 OPENBLAS_NUM_THREADS=2 \
  python -m unittest test_triad_helper_domain test_triad_helper_rate_aware \
  test_triad_helper_study test_triad_helper_campaign \
  test_triad_helper_broad_campaign test_triad_helper_receiver \
  test_triad_helper_batched_receiver

python tools/triad_helper_domain.py --output /tmp/helper-domain.json

# Occasional native rate/power qualification after corresponding source changes:
python tests/test_triad_helper_export.py \
  build/dev-cli/triad_helper_export --rate-qualification
```

Use a compatible numerical environment containing NumPy, SciPy and PyTorch.
Build through `./build.sh` and reuse a compatible `build/` profile. Missing
CUDA hardware is unavailable coverage, not a passed GPU check. Requalification
of waveform power, public frontends or timing policies invalidates the relevant
frozen admission gates. It does not require replaying unrelated ordinary Robust
research or every historical triad campaign.

## Reproducible evidence and resource closure

[`compact-evidence.tar.gz`](compact-evidence.tar.gz) contains frozen protocols,
source/build identities, raw trial measurements, manifests/evaluator records,
independent recounts, accepted and rejected diagnostics, source changes and
reproduction scripts. [`artifact-manifest.json`](artifact-manifest.json) records
each member's digest; [`SHA256SUMS`](SHA256SUMS) binds the archive, manifest and
packaging script. Large I/Q arrays and executables are intentionally excluded:
their identities and exact generation commands are retained. Re-exporting is
an occasional qualification operation, not a routine CI requirement.

The source archive uploaded in 8.018 seconds (19.87 MB). One RTX PRO 6000
Blackwell Server Edition at approximately $2.50/hour was used, then stopped
and terminated after evidence retrieval. No persistent or network volume was
created. Zero ongoing provider spend was verified; the conservative elapsed-
allocation estimate is under $6.40, separate from whole-account invoices.
Private credentials, endpoints and account/billing screenshots are excluded
from the repository evidence.

The study closes this bounded campaign, not the entire requested qualification.
Remaining blockers include multi-day shared Helper/DATA streaming and
acquisition, the measured raw two-ray model gap, operational daily false-
admission calibration, whole-bit Symbol Burst3, physical RF retuning, and the
explicit low/high-rate geometry and workspace boundaries. No ordinary Robust
integration, release certification, universal near-bound sensitivity or full
Cartesian reception qualification is claimed.

