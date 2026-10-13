# Frequency-search cost, amplitude evidence and remaining implementation

This local investigation uses the public alphabet at source `c9f02d89` and its
frozen manual GUI/library. Nominal transmitted samples, framing and cryptographic
addressing are unchanged. An overly conservative
clock-lane eligibility check is corrected below. The new envelope and segmented
statistics are **diagnostic prototypes**, not automatic production receivers.
No new reception probability or extended weak-target qualification is claimed.

## The actual example and its first limit

The built-in example requests 4 W, 220 dB path loss, noise density -164 dBm/Hz,
1 Hz rate setting, 1500 Hz audio carrier, 5.8 GHz Shift, independent GPSDO-OCXO
models and margin 3. It selects public patterns with no transmit key. Its saved
Fake FCC preference does not enable hopping: the encryption prerequisite turns
actual FHSS off. Neither antenna gains absent from the entered path loss nor
measured lunar propagation is inferred from the example name.

The resulting C/N0 is **-19.9794 dB-Hz**, whereas the selected design target is
**-3.0721 dB-Hz**. The initial 64 chips last 2 seconds each: 128 seconds per bit,
6000 input samples/s and 12000 samples/chip. Actual Es/N0 is only 1.09 dB. The
18 dB tuning design reference is not a measured receiver threshold; even that
reference needs 6279.7 seconds at this link budget. At rate 1, automatic geometry
rounds to 4096 chips and **8192 seconds (2.28 hours) per bit**.

The RF model's .0001 ppm becomes .58 Hz, or +/-1.74 Hz with the margin. Baseband
sampling-clock uncertainty is only +/-.0003 ppm. Whole-symbol frequency spacing
is at most `.25/T`; a complete 8192-second bank would need about 114035 carrier
positions, before timing or other alternatives. The cap is 4097 frequencies and
8194 frequency/clock pairs. Capped coverage is explicitly unsupported for the
full requested oscillator domain; a small estimate for that capped computation
cannot justify claiming complete reception.

The original independent-clock preliminary check used an *unclipped* RF strip
of about 2320 ppm to decide whether to calculate a more precise clock-cell
radius. At 256 seconds that rejected the calculation, despite the physical
clock interval being just .0006 ppm wide. Three redundant clock lanes consumed
the pair budget. The corrected entry check also uses the bound `4*A`: both a
representative clock and a true clock lie in `[-A,A]`, so their separation is at
most `2*A`, and twice the radius is at most `4*A`. The existing exact extrema,
outward rounding, quarter-chip condition, joint passband checks and frequency
spacing remain authoritative. This does not simply omit oscillator uncertainty.

| Rate 1 symbol | Before: frequency/clock pairs; covered half-width | After: pairs; covered half-width | Full requested domain after |
| --- | --- | --- | --- |
| 128 s | 1783; 1.74000045 Hz | 1783; 1.74000045 Hz | Yes |
| 256 s | 8193; 1.33282863 Hz | 3565; 1.74000045 Hz | Yes |
| 512 s | 8193; .66641431 Hz | 4097; .99986558 Hz | No |
| 8192 s | 8193; .04165601 Hz | 4097; .06249927 Hz | No |

This restores the next weaker automatic profile, down to approximately -6.08
dB-Hz at rate 1, subject to its other workspace/admission requirements. It does
not solve the remaining whole-symbol grid limit. The exact geometry probe also
covers rates .1 and 10: lowering rate alone does not cure frequency coverage;
the minimum 64-chip waveform at .1 already lasts 1280 seconds. At rate 10 some
very weak non-dyadic sample geometries select compact direct processing with
an excessive work/workspace estimate. These are algorithm/resource limits,
not proof that individual phase changes cannot be received.

The coverage constructor is also consulted by the existing hardware UTC timing
controller. Better bank coverage can make that controller eligible in additional
geometries, without changing its numerical rate allowance or audio qualification.
For the 1500 Hz example, steering eligibility is unchanged: 128-second symbols
support the original-plus-expanded union; at 256 seconds that union still exceeds
the 4097-frequency cap, despite the unsteered bank now fitting. With a 10 Hz audio
carrier and otherwise the same 128-second microwave geometry, the corrected union
does fit and existing UTC steering can become available. Dedicated regressions
check both cases, retain every original lane and preserve capped fallback. Thus
unchanged source PCM does not imply identical hardware presentation timing in
every newly supported UTC configuration. The measurements below use fixed PCM
and default clock timing.

## What the receiver already does

The ordinary I/Q match fits unknown complex gain. Individual phase transitions
need not be separately detectable above noise: their evidence accumulates over
the pattern. Four independently phased quarters and, for sufficiently long
patterns, local differential matches provide additional phase tolerance. They
still use known complex pattern structure and the whole-symbol frequency bank.
There is no amplitude-only bit decoder. The new public alphabet makes its two
amplitude profiles distinct, but that does not supply a calibrated new detector.

A raw envelope detector removes phase and a constant frequency offset once the
entire uncertain band is captured. It still needs timing/clock search, both bit
profiles, correct noise statistics and interference rejection. It is not OOK:
public coefficients have continuously varying magnitudes, not prescribed on/off
slots. Frequency-insensitive envelope evidence is useful, but under weak signal
conditions it scales approximately as `r*sqrt(T/B)` rather than coherent `r*T`,
where `r` is linear C/N0 and `B` is the admitted noise bandwidth. With fixed B,
10 dB less power can therefore require about 100 times the duration in this
regime. This is a detector tradeoff, not a universal information limit.

## Bounded numerical comparison

A reproducible local diagnostic samples the actual finite-shaped, limited public
source at original sample coordinates (including .375-sample fractional offset),
then examines 8 independent proper complex Gaussian observations per second.
It models an ideal 8 Hz complex baseband observation; it does **not** implement
or validate downconversion/filtering from real PCM. The central full symbol is
used; its start/end shaping is retained, but additional pulse tails outside the
symbol are omitted. The two alternatives keep their actual energy imbalance,
normalized to equal ensemble mean received energy. All methods use the same
2048 noise seeds and signal samples within each case.

The coherent and disjoint-segment statistics use normalized complex templates;
independent normalized segment energies have a Gamma null distribution. The
amplitude prototype sums `(abs(y)^2-1)` weighted by the shaped squared envelope;
its null threshold uses a conservative Chernoff bound for weighted exponentials.
Each alternative receives a 5e-7 false-alarm budget (union bound 1e-6 for two
alternatives). This is a **known-timing, known-noise, single-alignment** experiment,
not the application's many-trial acquisition or absent-symbol decision. The
amplitude threshold is a bound, not an optimal likelihood threshold, so its
result does not bound every conceivable amplitude decoder. The 8 Hz noise band
is wider than the nominal approximately 4.11 Hz waveform-plus-static-uncertainty
support and is an explicit cost of this prototype.

At 8192 seconds per bit:

| Statistic | Additional C/N0 at 90% detection | Paired bootstrap 95% interval | Additional C/N0 at 99% |
| --- | --- | --- | --- |
| Full complex match, unknown global phase | Reference | — | Reference |
| 512 s coherent segments, sum energies | +2.67 dB | [+2.60,+2.74] | +2.43 dB |
| 256 s coherent segments, sum energies | +3.67 dB | [+3.59,+3.75] | +3.17 dB |
| 128 s coherent segments, sum energies | +4.75 dB | [+4.67,+4.84] | +4.24 dB |
| 32 s coherent segments, sum energies | +7.29 dB | [+7.20,+7.38] | +6.68 dB |
| Amplitude-only weighted energy | +17.80 dB | [+17.70,+17.92] | +17.01 dB |

The 99% difference intervals are respectively [2.28,2.54], [2.96,3.30], [4.04,4.37],
[6.43,6.84] and [16.79,17.16] dB. Grid spacing is .25 dB; intervals are paired
500-resample bootstrap uncertainty only, excluding interpolation, frontend,
noise-estimation, channel and acquisition-model errors. At 128-second symbols,
amplitude-only costs 9.29 dB at 90%, versus 1.09 dB for four 32-second segments.
No earlier amplitude-ablation estimate is treated as a measured gain.

For the 8192-second, 128-second-segment experiment the conditional 90%/99%
C/N0 points are -21.05/-20.17 dB-Hz. These must **not** be entered as application
RX percentages: additional frequency/timing/clock trials, strongest-quarter
rejection, unknown noise, interference and propagation can materially change
them. No rare-event rate was inferred from these trial counts. Diagnostic wall
execution was 16.4/42.9 seconds for the 128/8192-second Monte Carlo workloads;
those times are neither production CPU benchmarks nor modem latency estimates.

## Exact alternatives before discarding phase

An additional native C++ prototype evaluates the frequency dimension together.
The current projected template factorizes, at a fixed exact clock rate r, as
`s_b,r[i] * exp(j*2*pi*f*t_i)`. For each admitted timing start, a frequency-axis
transform of `x[start+i] * conj(s_b,r[i])` supplies the same complex dots across
the carrier bank. The current refined frequency spacing is generally not a
radix-two FFT-bin spacing, so the prototype uses a chirp-z transform (CZT),
implemented with the existing FP64 FFT kernel. Choosing the nearest ordinary
FFT bins would change the searched hypotheses and is not an acceptable shortcut.

At the corrected 256-second EME geometry, the 3565 frequencies have just three
**exact** clock ratios: nominal and the two clipped clock-domain endpoints.
No close-clock interpolation is needed. Four section transforms per bit retain
the original coherent sum, independent-quarter energies, strongest-quarter
removal and detector-choice penalty. Separately caching the three unrotated
clock rows also avoids repeatedly constructing the same shaped coefficients.
That exact cache is compatible with the FP64 diagnostic policy.

Isolated one-worker kernel results (seconds of process CPU):

| Timing starts | Existing timing-axis kernel | Exact clock-row reuse alone | Clock-row reuse plus frequency CZT |
| --- | --- | --- | --- |
| 1 | 3.937 | .440 | .0080 |
| 4 | 3.595 | .523 | .0176 |
| 16 | 7.571 | 4.608 | .0627 |
| 64 | 7.776 | 4.463 | .2404 |
| 1024 | 8.524 | 5.380 | 3.7885 |

The 16- and 1024-start rows are medians of three serial paired repeats; the other
rows are single bounded controls. Paired CZT speedups span 107.4–121.6x and
2.06–2.27x, respectively (median-time ratios 120.7x and 2.25x). These ranges
describe observed execution variability, not confidence intervals on population
performance. They do not establish whole-receiver improvement.

The current kernel already uses direct arithmetic for up to four starts, which
explains the discontinuity between four and sixteen. These runs use identical
projected complex observations, actual finite-shaped public templates, 2049
bins, all 3565 carrier/clock pairs and both bits. Timings include that method's
row construction, transform setup, allocation and score reduction. They exclude
real-PCM preprocessing, receiver scheduling, tracking and progress publication.
They are **kernel results, not full-receiver or live throughput claims**.

Exact clock-row reuse produced bit-identical scores in this fixture. CZT's
maximum absolute score difference was 5.38e-10, with no decision disagreement at
the four inspected thresholds 5, 10, 20 and 40. This is numerical comparison,
not a sensitivity curve or an estimate of rare false accepts. The CZT prototype
uses the uniform mathematical lattice; explicit handling/refinement of the
rounded endpoint coordinates remains necessary before production integration.
Its reusable scratch is 855008 bytes, independent of the number of starts.
The diagnostic additionally retains old/new score matrices for comparison
(58.4 MB each at 1024 starts); a production implementation must stream bounded
tiles into the original ordered collector rather than require those matrices.

The cache-only diagnostic supplies original nonunit-clock samples through the
existing nominal-row hook only after asserting that section boundaries and
extended-window zero masks are identical. A production cache needs an explicit
exact clock identity instead of this test-only adapter. Validity also includes
pattern configuration, key/epoch, symbol, stream phase, pulse geometry and
observation coordinates; changes to any of them invalidate the row.

This strategy can lose when few frequencies share a clock row or when timing is
broad. A 21-frequency, 64-start control took .0423 seconds with the existing
kernel, .0271 with exact reuse and .0326 with CZT. At 1024 starts that same
21-frequency control took .0567, .0418 and .5772 seconds, respectively: CZT was
10.2x slower than the existing kernel. The automatic crossover must
therefore compare complete alternatives rather than universally select CZT.
Moreover, the current public long-symbol receiver does not honor the narrow
qualified-start path: its continuing acquisition uses full 1024-start hops.
The spectacular small-start kernel ratios cannot be credited to that receiver
without implementing and validating its complete admitted timing set.

Other outstanding integration work includes frequency-dependent real-input
Gram terms for sample-fit geometries, differential windows, bounded complete
banks beyond the present cap, tracking/continuation (which also scans carrier
alternatives), cancellation, exact ordering/ties and near-threshold refinement.
The prototype supports none of those by implication. It establishes a valuable
exact optimization to pursue **before** paying the lossy-detector penalties
below; it does not yet supply an automatic new receiver backend.

## Implementation direction for an automatic CPU fallback

After measured exact reuse and transform-orientation choices, an automatic CPU
policy should consider **bounded coherent segments with
noncoherent evidence accumulation**, retaining the existing amplitude and phase
waveform within each segment. Frequency spacing can then follow segment length
L instead of whole-bit duration T. At T=8192, L=128, the same +/-1.74 Hz requires
about 1783 frequency positions instead of 114035: **64 times fewer positions**,
not an established 64-times total speedup. Reuse input transforms and complete
shaped template pieces across the two alternatives and timing cells. Do not
instantiate one full receiver/FFT history per segment or duplicate shifted PCM.
Bound storage independently of the physical symbol duration. In particular,
generalizing the current four-quarter implementation to K sections while still
performing K full-symbol FFTs per frequency would cancel much of the frequency
count saving. Frequency spacing may follow L, but clock spacing must still
bound accumulated timing displacement across the full T. Passing a shortened
modem configuration to the current grid constructor would incorrectly change
clock coverage, pattern coordinates and physical absence.

The earlier [restricted-acquisition measurements](search-compute.md) are an
important constraint on this proposal: in the recorded DSSS1000 fixture,
reducing a six-second prior to one millisecond reduced final-candidate CPU
from 5.988 to 1.829 seconds, not in proportion to the timing-position count.
Forced whole FFT cost 3.929 seconds at one millisecond versus 1.875 seconds
for the automatic mix in a separate single control. The 128-position direct
crossover was slower than the retained 32-position crossover. Those are
different, private DSSS geometries; they do not establish EME costs. A new
segmented backend must compare complete FFT, tiled and direct execution,
including templates, statistics and rolling setup. Shorter timing priors alone
must not be credited with savings while broad transforms/scheduling remain.

### Beyond EME: an execution budget and amplitude-preserving segments

The same problem occurs with ordinary RF references. The current IC-7100 Shift
model uses a 1 ppm relative pair bound, before margin: at 30 MHz and margin 3,
the RF contribution is +/-90 Hz. At T=8192 seconds the uncapped quarter-cycle
grid is about 5.90 million frequencies. At L=256 it is still 184321; at L=2 it
is 1441. These are geometric counts, not execution benchmarks or predictions
of IC-7100 performance. The baseband clock and radio reference remain separate.

There is an important limitation to simply shortening the existing section
fits. Each fit estimates an independent **complex gain**, normalizing away that
section's amplitude. If the section lies entirely inside one held chip, two
alternatives differing only by a complex scalar have identical normalized
projection energy. The existing section branch requires at least 16 complete
chips per section. At rate 1 that implies L>=32 seconds, while fitting +/-90 Hz
within 4097 frequencies requires L<=5.69 seconds. Silently removing the minimum
chip check would not supply an amplitude decoder. Actual finite shaping can
break exact proportionality near boundaries, but does not justify relying on
that small residual distinction.

A genuine amplitude-preserving branch should retain the known relative template
energies across segments and fit a common received scale, with independent
phase nuisance parameters. A weighted quadratic or noncoherent likelihood can
then use amplitude differences even for short coherent pieces. A shared coarse
channelizer is another candidate. Both require their own null/trial bounds and
measured sensitivity; the normalized-segment table above does not qualify them.
The existing DSSS independent-code guard also labels section/differential fits
unqualified; automatic private/DSSS use cannot bypass that guard's validation.

A configurable receiver processing allowance, for example **0.6 processing
seconds per second of audio**, is a reasonable selection policy once the
alternative backend is measured. The policy should:

- Evaluate the complete admitted bank, including timing, frequency, full-symbol
  clock coverage, keys, rolling epochs, preparation, scoring and publication.
  Keep workspace and the user's allowed sensitivity tradeoff as separate limits.
- Define the ratio as processing elapsed time at the selected worker allocation;
  also report total core-CPU seconds. Four core-seconds of parallel work can fit
  inside one elapsed second. Neither ratio alone accounts for burst scheduling
  and background-load stalls, so the allowance must leave headroom and expose
  measured backlog separately.
- Choose a supported backend and coherent span before examining candidate
  outcomes. Keep an acquired track's detector identity stable. A backlog must
  not silently drop hypotheses or downgrade existing absence evidence.
- Plot that branch's own supported RX curve, including discontinuities at
  strategy changes. If its probability model is unqualified, show an explicit
  unavailable/experimental range instead of borrowing the coherent estimate.
- Reject an infeasible requested bank explicitly when no supported alternative
  fits. Never convert a CPU cap into narrower hidden oscillator coverage.

No new budget control is exposed by this checkpoint because the cheaper
production backend and its calibration are not implemented. The current
four-section planner uses an aggregate operation-rate allowance, not a measured
per-host deadline guarantee. Sustained rolling-listening cost is explicitly
unmodeled for this branch; its zero-valued field must not mean free work. The
planner's current supported-profile predicate checks oscillator coverage and
workspace, not a CPU deadline. Merely adding a slider around that model would not
make automatic switching reliable. Shortening coherent spans can substantially
reduce one search dimension, but the complete execution crossover and amplitude
likelihood are still implementation work.

Pure envelope matching remains worth exploring where phase is unusable or the
compute constraint is more extreme. A shared channelizer with coarse overlapping
frequency bands can avoid both an enormous fine-frequency bank and the noise
penalty of one unnecessarily wide envelope channel. Channelization, setup,
filter covariance and every frequency alternative remain part of its cost and
trial count. Do not discard weak coherent candidates using an unqualified
amplitude prefilter.

Selection must be based on resolved geometry, measured total CPU/workspace,
coverage and a declared sensitivity budget, rather than target C/N0 alone.
The 4097-frequency limit is a hard implementation cap, not a benchmark proving
that this computer cannot execute a larger bank. First assess bounded tiling of
the complete coherent bank, with shared input statistics, against the actual
CPU and workspace budget. If that fits, retaining coherence avoids an unnecessary
sensitivity penalty. Increasing a cap without bounding its buffers, trial
accounting and processing deadlines would not be an adequate implementation.
Choose before examining candidate outcomes, or charge the additional detector
selection trials. Target controls could admit a lower setting only when a
complete supported backend exists for it; they must expose that backend's own
sensitivity/airtime model without changing TX coefficients or framing. The
present 18 dB integration rule may be insufficient for a lossy fallback.

Before enabling either new path:

- Derive scores using the exact shaped real-input I/Q Gram/covariance. Filtered
  complex envelope energies have covariance `abs(R_ij)^2+abs(P_ij)^2`; treating
  them as independent is generally wrong. Keep partial segments and pulse
  overlap, nuisance noise/gain estimates, and competing-bit evidence explicit.
- Preserve every timing/clock/frequency/epoch alternative and the current
  strongest-physical-quarter protection against concentrated unrelated energy.
  Fit/null-distribution and detector-choice thresholds need dedicated tests.
- Score the **whole** configured bit before absent classification. Segments are
  internal evidence, never shorter transmitted bits or early end-of-message.
  Keep next-poll publication, pending identity, cancellation and the six-second
  fully-scored absence rule. An hours-long absent bit still takes hours.
- Measure original-PCM paired full-bank sensitivity, false admissions, frequency
  and clock edges, fractional starts, phase diffusion, wrong/private keys,
  cover/interference and completion; benchmark full acquisition and rolling
  work, not just a single matched dot product.

GPSDO static frequency error is only one EME impairment. The example's combined
phase diffusion is .007071 degrees/sqrt(second), an illustrative oscillator
model. It does not describe time-varying Doppler, lunar multipath or libration.
The current manual Doppler control is a constant correction. Real coherence
may be much shorter than 128 seconds; envelope variation can also be distorted
by channel fading. Such a channel needs its own measurements and may require
shorter segments or a bounded frequency trajectory model.

Primary background: [ESA on coherent/noncoherent integration and squaring loss](https://gssc.esa.int/navipedia/index.php/Baseband_Processing),
[WSJT authors' measured microwave EME spectra](https://wsjt.sourceforge.io/small_station_eme.pdf),
and [WSJT-X EME guidance](https://wsjt.sourceforge.io/wsjtx-main_en.html).
These sources motivate the tradeoffs; they do not validate this modem.

## Reproduction and checkpoint limits

### Actual execution of the clock-lane correction

Three isolated serial pairs linked the same probe against the frozen and new
libraries on the local Ryzen 5 PRO 5650U. They used identical generated original
6000-sample/s PCM, one public bit, the same noise seed, 256-second symbols,
512 MiB workspace allowance, one scoring worker, and 768.0167 seconds of input
including fully observed absence. Both versions returned the correct bit and
physical completion in all three runs. Input and transmitted waveform were
unchanged. Setup, PCM ingestion, search, progress polling and final pending
drain are timed; fixture waveform/noise generation is outside the timing.

| Quantity | Frozen baseline | Corrected bank |
| --- | --- | --- |
| Total CPU median (observed range), seconds | 62.018 (61.646–62.104) | 37.696 (37.234–38.129) |
| Wall median (observed range), seconds | 62.026 (61.685–62.244) | 37.716 (37.243–38.136) |
| Receiver-accounted peak workspace, bytes | 1796338 | 1441522 |
| First accepted-bit poll, received-media seconds | 342.699 | 385.024 |
| Carrier/clock pairs; complete requested frequency coverage | 8193; no | 3565; yes |

Median CPU improvement is **1.65x**, with paired ratios 1.63–1.67; three repeats
give execution variability, not a population confidence interval. Serial CPU
per input second falls from .0808 to .0491 for this finite fixture. This is not
a sustained live multi-profile or multi-key measurement. Accounted workspace
falls by 354816 bytes; this is not process peak RSS. Components other than setup
were not separately timed in this probe.

The **42.325-second acquisition regression** is a deterministic scheduling
tradeoff, not delayed publication after acceptance. Full frequency coverage
requires projection bins of 750 samples instead of 1000, crossing an FFT-size
boundary: 2048 to 4096 points, and an 85.333- to 128-second hop. This long public
geometry does not use the qualified restricted-start path. The first limited
start pass does not include the payload's 16-second pulse-padding offset; the
next full batch therefore determines acquisition. Next-poll publication and
physical completion remain intact. Restricted long/public scheduling is an
explicit remaining opportunity; this result must not be presented as a uniformly
better acquisition latency.

This comparison also covers different complete banks: the baseline is capped
at +/-1.333 Hz and the corrected bank covers +/-1.740 Hz. It is an actual
before/after cost improvement while restoring coverage, not an identical-bank
sensitivity-equivalence experiment. No dB sensitivity difference is claimed
from these three strong-signal receptions. The old and new engineering time
estimates (9.08/6.29 seconds in the separate geometry probe) use different media
duration, hardware-mode finalization and an aggregate parallel operation-rate
allowance; their ratio to the serial benchmark is not a calibrated model error.

### Local build and validation

The manual candidate is Linux x86_64, GCC 14.2, Release (`-O3 -DNDEBUG`, existing
`-ffp-contract=off`), FLTK, built through `./build.sh`. Configuration records
`52ace0a0e8e0f14baebcc113cdfb79d9a309fd1f` plus the local patch. That commit only
adds verbose-log ignores relative to the frozen `c9f02d89` baseline; its runtime
and test sources are identical before this patch. Binary hashes, exact source
overlay, build information and launcher are retained in the local artifact
directory's `final/manifest.json` and `MANUAL-EME-RECEIVER.md`.

Focused search tests, including the additional UTC-union cases, pass. All 38 GUI
cases have passing evidence for the final runtime; the planner fixture was
updated to retain its overload assertion at 100 MHz while retaining the now
cheaper 30 MHz geometry and its sustained-work assertions. The complete planner
and simulation-estimate executables were then rerun and passed.

The bounded regular functional selection passed **53 of 54** cases; the separately
run simulation-estimate case also passed. Two broad receiver-probability
calibrations remain deferred at this manual checkpoint. The failure is
`alsa_audio_contract`: its legacy-DSSS fractional-resampling clipping assertion
measured peak 1.00944, 63 clipped samples and energy fraction 2.47619e-8 against
the existing 1e-8 bound. Relinking the same fixture against the frozen baseline
library reproduces **the identical failure and measurements**. No assertion was
weakened; this pre-existing audio issue remains unresolved, and later assertions
in that executable are not counted as passing. This is not full qualification.

Local evidence is in `.agent-work/artifacts/eme-envelope-20261012/`: exact
geometry CSVs, standalone C++ source sampling/probes, `statistics.py`, raw paired
success arrays, thresholds and bootstrap results. The current source retains
only the clock-cell eligibility fix and regressions; the statistical and CZT
prototypes are not hidden production fallbacks. The preceding runnable GUI and
raw receiver references remain preserved. Full-bank sensitivity for a new
amplitude/segmented decoder, end-to-end CPU crossover, live EME, ARM and broader
platform/SDK qualification remain outstanding. No hosted jobs were dispatched.
