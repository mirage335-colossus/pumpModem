# Initial sampled FFT physical contrasts and long observations

The initial campaign completed **152 actual sampled captures**: 136 across
17 short/moderate-duration contrasts and 16 across two long-duration cells.
All exports and scores completed. Independent recount finds **63/68 correct
strict admissions and five misses** in the base campaign, or **65/68 correct
and three misses** at the relaxed gate. Neither gate admitted a wrong bit.
The long cells each admitted all four signal captures. Every cell's two null
and two wrong-key controls were rejected at both gates.

These totals describe coverage, **not pooled independent reliability**. The
same four signal seed tuples recur across base cells; a separate four tuples
are paired across the two long cells. Four successes cannot qualify 90% or
99% reception. The long result is a measured synthetic, point-sampled
observation over more than 850 days at −60 dB-Hz, with restrictive public
channel and synchronization assumptions described below.

The [reviewed summary](summary.json) preserves every capture's outcomes,
geometry, energy accounting, durations, costs and provenance. The read-only
[analysis script](analysis.py) reproduces the recount without running any
receiver or simulator. This is the initial 152-capture snapshot; the completed
[half-duration, independent 32-signal and higher-Doppler followups](followup/README.md)
are separate evidence, not silently included here.

## Receiver and search scope

The receiver uses the existing whole-observation, two-ray Gaussian likelihood
with residual noise-variance upper bounds and FFT acquisition. It evaluates
both bit patterns over every declared integer timing/CFO group. The chosen
group is a diagnostic; admission uses the complete uniform mixture. These
runs fix the public epoch, sample-clock hypothesis, frequency drift and
fractional timing branch. The physical preset delay and Doppler are supplied
as the two-ray template hypotheses. They are model-informed channel parameters,
not inferred from simulator truth, but this is **not fully blind channel,
clock or epoch acquisition**.

All eligible base cases use the tight native 10 ms clock / 1 ms region /
0 ms offset / 70 ms station audio-lag policy. The separate several-second
arrival case was blocked by its group cap. CFO grids cover the declared
oscillator envelope, with maximum spacing 1/(4 × observation upper bound).
That spacing is coherent rectangular-integration guidance, not a certificate
for arbitrary shaped multipath likelihood loss. No bank was narrowed after
seeing a received signal. The grid remains exhaustive only on its integer
sample lattice.

Encryption is enabled, with the reproducible clock epoch 1793664000 and all
seven random seed domains explicit. Unit-power C/N₀ normalization controls
the injected thermal noise; these are not independent radiated-power or
antenna-efficiency measurements. Base cells use **12 dB-Hz**, except the
24 kHz cell at **20 dB-Hz**. Channel energy is not normalized after fading or
selective modulation. The corrected `dispersive-dft-v1` selective channel is
a spacing-scaled synthetic stress law, not calibrated field propagation.

CPU FP32 covers observation FFT/products and projected likelihood bulk.
Gram FFTs, residual projection, rank/factor preparation, small observation
whitening and evidence reductions retain explicit FP64/complex128 work.
No CUDA speedup is claimed; the separate [CPU/GPU comparison](../gpu/README.md)
describes that implementation and its measured limits.

## Measured base contrasts

Every row has four signals, two nulls and two wrong-key captures. All controls
reject and all wrong-bit admission counts are zero. Durations are actual
source calendars, not the nominal request. “Strict / relaxed” means correct
admissions out of four.

| Cell | Main configuration | Source calendar | Groups | Strict / relaxed |
| --- | --- | ---: | ---: | ---: |
| 01 anchor | Rate36 ×10, 500 kHz, TCXO/IC-7100, Sub9 moderate, Each1 | 11.6 s | 8,925 | 3 / 3 |
| 03 depth65 | Anchor with Each65 | 11.6 s | 8,925 | 2 / 4 |
| 04 depth65 margin3 | Each65, margin3 | 11.6 s | 25,725 | 2 / 2 |
| 05 HF30 XO | Rate360 ×10, 30 MHz, GPSDO XO/XO, HF worst, Each25 | 9.2 s | 1,374 | 4 / 4 |
| 07 microwave OCXO | 5.8 GHz, GPSDO OCXO/OCXO, Aurora worst, Each25 | 9.2 s | 20,610 | 4 / 4 |
| 08 microwave shared | Same, RF shares baseband clock | 9.2 s | 20,610 | 4 / 4 |
| 09 microwave depth1 | Microwave OCXO with Each1 | 9.2 s | 20,610 | 4 / 4 |
| 10 VLF zero | Rate3.6 ×10, RF0 / IF8270 Hz, TCXO/shared, Sub9 worst, Each25 | 112.4 s | 525 | 4 / 4 |
| 11 VLF margin3 | VLF with margin3 | 112.4 s | 525 | 4 / 4 |
| 12 free/CB | Rate360 ×10, 500 kHz / IF180 Hz, crystal/CB SSB, HF moderate, Fast10 | 9.2 s | 695,702 | 4 / 4 |
| 13 disciplined | Free/CB contrast with GPSDO XO/XO | 9.2 s | 1,374 | 4 / 4 |
| 14 miss first | Rate360 ×10, 500 kHz, HF worst, Each65, FHSS, dwell0 erased | 46.0 s | 1,374 | 4 / 4 |
| 15 miss last | Same, nominal dwell114 erased | 46.0 s | 1,374 | 4 / 4 |
| 16 middle/scattered | Same, Each25 and ten fixed erased dwells | 46.0 s | 1,374 | 4 / 4 |
| 17 internal wait3 | Rate36 ×10, GPSDO XO/XO, Each5, three tile waits | 150.8 s | 525 | 4 / 4 |
| 18 whole-bit burst3 | Same useful allocation, whole-bit burst metadata | 46.4 s | 525 | 4 / 4 |
| 19 rate24k | Rate24k ×1, 30 MHz, OCXO/OCXO, spread None, modulation Off | 8.8 s | 9,126 | 4 / 4 |

In this table, ×10/×1 denotes Added Symbols. Oscillator labels beginning
TCXO/XO/OCXO are the GPSDO presets unless “crystal” or a radio is explicit.
HF worst is 30 Hz/7 ms and Aurora worst is 55 Hz/11 ms before the stated
margin. High deterministic ray separation can be modeled efficiently when
its delayed/Doppler-shifted basis is supplied; these successes do not establish
performance against unknown stochastic channel renewal of the same bandwidth.
The 24 kHz None case has union rank two; all other groups have rank four.
No public-edge direct refinement was invoked.

The two VLF cells benefit from **112.4 seconds**, imposed by minimum pattern
complexity, despite an eight-second request. Their large scores are not an
eight-second sensitivity result. Similarly, missed-hop and burst cases use
a 40-second active request, rounded to 46–46.4 seconds before waits.

## Misses, energy and theoretical comparison

All five strict misses still selected the correct raw bit. The strict gate
is 22.72106339; the relaxed gate is 17.42274602. The two Each65 scores near
18 pass only the relaxed gate.

For an inactive frontend and **no erased samples**, the exported channel
signal energy divided by the thermal complex-sample variance gives the
conditional energy ratio `rho`. A receiver knowing the complete received
waveform, phase, arrival and channel in white Gaussian noise has the
Neyman–Pearson presence benchmark

`Pd = Phi(sqrt(2*rho) - norm.isf(alpha))`, with `alpha = exp(-gate)`.

Using each actual allocated gate, rather than a whole-day probability for
one decision, the required known-waveform energy is **37.331 for the strict
99% target** or **22.568 for the relaxed 90% target**. The summary also retains
an explicitly named 99% comparison under the relaxed gate, requiring 30.133;
that is not the relaxed operational target. These optimistic presence
benchmarks use the current operational allocation, not a universal minimum
for one known completion. They are not correct-bit probabilities,
unknown-channel bounds, or the measured receiver likelihood. They distinguish
available energy from acquisition/model loss.

| Missed capture | `rho` | Strict score | Conditional known-waveform strict Pd |
| --- | ---: | ---: | ---: |
| Anchor trial2 | 31.941 | 12.999 | 0.9534 |
| Each65 trial1 | 71.969 | 18.119 | 0.999999993 |
| Each65 trial2 | 44.637 | 18.548 | 0.99914 |
| Each65 margin3 trial1 | 33.924 | 7.776 | 0.9727 |
| Each65 margin3 trial2 | 19.213 | 2.069 | 0.4540 |

This separates two effects. Some captures lack enough energy for 99% strict
presence detection even with a known waveform. The unscaled Each65 trials
have adequate genie energy but miss the practical strict gate; synchronization,
unknown gain/channel inference and the restricted channel model retain a
measurable gap. Those two captures are respectively **2.85 and 0.78 dB**
above the known-waveform 99% energy requirement. Four paths and one duration
do not establish the C/N₀ or duration cost of closing that gap.

Strong modulation changes physical energy as well as model match. For
example, the four anchor `rho` values span 31.94–341.31, while the same root
tuples with Each65 span 44.64–598.57; the change is not a uniform multiplier.
The sampled paths are paired by roots, not guaranteed identical after every
configuration changes RNG consumption. No post-channel normalization was
introduced, and a higher score must not be labeled sensitivity gain without
accounting for the altered energy.

**The sampled export's `channel_signal_energy_samples` is pre-erasure.**
It cannot supply the received post-mask energy for the three missed-hop
cells. Their summary retains the reported pre-erasure quantity but leaves
`rho` and the post-erasure genie benchmark unset. Raw observations, validity
and templates remain in the separately retained full workspace; the compact
archive does not contain a clean post-erasure signal-energy trace.

## Bursts and missed-hop interpretation

Internal wait3 transmits four 11.6-second useful tiles, with three tile-length
waits between adjacent useful tiles: **46.4 seconds useful allocation,
150.8 seconds source calendar**, 151.072 seconds shaped support and 151.329
seconds observation. It tests actual silent waits inside the captured bit.

Whole-bit burst3 has **46.4 seconds source calendar** and no internal wait.
Its public successor metadata schedules the next support at epoch
1793664210, 210 seconds after the requested origin. This capture contains
only the first bit; it does not demonstrate a multi-bit backlog-processing
cycle or transmission of the successor.

Missed-hop masks are fixed receiver-calendar indices after channel, noise and
frontend processing. Physical state and echo tails continue. The first/last
masks are `[0]` and `[114]`; the middle/scattered mask is
`[17,48,57,58,72,82,86,109,111,113]`, including consecutive middle losses.
The logical useful calendar contains 115 dwells. Timing uncertainty can
shift the overlap with actual transmitted hops, so these are not a proof of
the worst possible first/last transmitter-hop loss or two-cycle wrap loss.
FHSS is envelope-only; physical RF retuning is not newly validated.

## Measured −60 dB-Hz long coordinates

Each25 and Each65 use Rate **0.001 Hz**, Added Symbols ×1, RF0,
conversion IF0.0005 Hz, GPSDO OCXO/baseband-clock, margin1 and Sub9 best
**0.001 Hz/0.005 ms**. All waveform samples are processed over the genuine
long simulation coordinates, with continuous selected channel/oscillator
state; this is not extrapolation from a short run.

| Quantity | Recorded value |
| --- | ---: |
| Requested active duration | 73,440,000 s = 850 days |
| Actual payload/source calendar | 73,728,051.2 s = 853.334 days |
| Complete shaped source support | 73,824,896 s |
| Complete observation | 73,830,400 s = 854.519 days |
| Sample rate/count | 1/128 Hz; 576,800 complex samples |
| Private tiles / fine cells | 128 tiles; 64 fine cells per tile |
| Fine rate / carrier spacing | 0.000166667 / 0.000208333 Hz |
| Receiver bank | Two integer lags × three CFO values = six groups |
| Each25 signal scores | 56.135, 53.919, 38.788, 52.283 |
| Each65 signal scores | 64.876, 59.702, 37.244, 35.066 |

Both cells admit 4/4 signals and reject 2/2 nulls and 2/2 wrong-key captures
at both gates. Known-waveform energy ratios span **64.54–84.25** for Each25
and **68.98–120.22** for Each65: respectively **2.38–3.54 dB** and
**2.67–5.08 dB** above the strict 99% genie requirement. Those margins do
not measure a practical sensitivity penalty or establish a minimum duration;
there is only one duration here. The fixed few-hypothesis receiver and very
low sample rate explain the small computational cost.

The physical ray separation is deterministic and included exactly in the
two-ray basis. Receiver coefficient width is B=0, fitting constant unknown
complex gains, with no phase oracle. Native Wiener phase noise remains
active at **8.726646×10⁻⁵ rad/√s** relative diffusion; its endpoint RMS over
this span is approximately 0.75 rad. White/flicker/random-walk FM and white
PM are zero in this selected low-rate model. These are declared synthetic
oscillator laws, not a validation of multi-year GPSDO behavior.

Samples are **128 seconds apart**. The low-rate implementation preserves
the selected continuous state and fractional propagation, but it is a
point-sampled model: it has not been shown equivalent to high-rate
integration/decimation over this observation. No sampling-refinement result
is supplied here. Millisecond FHSS/dropout and active frontend controls are
unsupported at this rate and are off, not silently simulated. The two timing
lags and tiny public CFO bank do not establish broad synchronization search
at −60 dB-Hz. Unknown Doppler/channel evolution, higher spread, additional
oscillator spectra, general renewed fading and field propagation remain
outside this result. See the [low-rate model contract](../../../../triad-v3.md).

## Uncertainty, blocked configurations and cost

Conditional two-sided 95% Clopper–Pearson intervals for correct reception
are **[0.398,1] for 4/4**, **[0.194,0.994] for 3/4**, and
**[0.0676,0.932] for 2/4**. They assume per-cell Bernoulli sampling and do
not license pooling reused paths across cells. With zero admissions in two
controls, the one-sided 95% upper probability is **77.6%** for that control
type. This is diagnostic control evidence, not a rare-event validation.
Neither 99%/10⁻⁴ per day nor 90%/1% per 12 hours is statistically qualified.

The frozen base plan also preserves six unexecuted configuration outcomes:

- The wide arrival anchor requires **1,445,463 groups**, exceeding the
  one-million group cap. Its prior was not narrowed to obtain a pass.
- HF-worst margin3 fails the deterministic capture-width feasibility check
  at the automatic 2,520 Hz sample rate. This is a capture-configuration
  limit, not a measured decoding failure.
- Rate0.001 and Rate0.01 short-request entries were configure-only. The
  separate measured long cells above do not turn those rows into tests.
- Rates1 MHz and30 MHz fail protected-tile geometry with the preserved
  message: `triad_long: triad v3 protected tile exceeds bounded source workspace`.

The 136 base captures took **97.683 seconds with four workers**; the 16 long
captures took **49.659 seconds with two workers**. These jobs overlapped for
part of execution, so the elapsed times are not isolated throughput
benchmarks. Each child used one CPU thread. Base exports took 0.101–0.831 s;
base scoring took 1.258–16.472 s. The free-crystal/CB SSB case, with 695,702
groups, accounts for the 16.20–16.47 s scores; the disciplined contrast took
1.33–1.63 s. This is a measured computational effect of oscillator-derived
search width, not a universal hardware scaling law.

Long exports took **2.71–3.14 s** and scoring **2.76–3.16 s** per capture.
The explicit long CPU array bound was **608.50 MiB** under the declared
1 GiB array budget. It excludes interpreter/allocator overhead and is not
measured process RSS. Full raw arrays total about 1.266 GB for the base and
1.338 GB for the long campaign; compact manifests and reports permit the
present review without downloading or rereading all those arrays.

## Provenance and reproducing the review

The receiver binds source `f91265b9...` and Gaussian helper `b22fe002...`;
the native executable is SHA-256
`9cdf82f396d9be9a7d11b66b45b254e5f18b3857f27b08aed58430dce6d8ea47`.
All 16 declared source hashes match the frozen plans and the audit-time
local files. Original source/binary before/after checks are retained.
The base plan SHA-256 is `595342c5...`, and the long plan is `a9bd5491...`;
full identities are in the summary. No runtime code was changed for review.

The compact archive is **21,388,520 bytes**, SHA-256
`66c277aa18f639684acdd7cd829561d89b9c7824ef00178c10ec08dc294b2417`.
Review checked every result record, export/score status and log hash, score
report, input-manifest hash, declared bank geometry and decision. The raw
basis/IQ/validity hashes match across export inventories and scores, but those
bytes are absent from this compact archive and **were not independently
rehashed here**. Per-group score tables were omitted in the campaign, so
this review cannot independently reconstruct likelihood mixtures or residual
variance UCBs from compact reports alone. Earlier full-table parity evidence
is separate and does not replace these limitations.

Run this read-only analysis from the repository root with NumPy/SciPy available:

```sh
/tmp/triad-weak-env/bin/python \
  docs/validation-data/triad-v3/2026-11-03-fft-timing/physical/analysis.py \
  --campaign local-evidence/triad-fft-timing/campaign/remote-initial/physical \
    local-evidence/triad-fft-timing/campaign/preflight-003/plan.json \
  --campaign local-evidence/triad-fft-timing/campaign/remote-initial/long \
    local-evidence/triad-fft-timing/campaign/long-preflight-001/plan.json \
  --archive local-evidence/triad-fft-timing/campaign/compact-initial.tar.gz \
  --output local-evidence/triad-fft-timing/physical-review.json
```

The retained long simulation is useful evidence for computational feasibility
under its assumptions. It is not a reason to repeat an 850-day campaign as
routine regression, nor a demonstrated general duration planner. Ordinary
Robust receiver integration and its regression campaign remain outside scope.
