# Simulation estimates

The simulation controls show a **modeled whole-draft receive probability** and
two **rough compute-time estimates**. They are planning aids, not measured
confidence, certified error rates, observed hardware performance, or guarantees.
They change when the draft, channel preset, transmit geometry or receive search
changes. Nothing in this model changes transmission or receiver admission.
For patterns eligible for four-section fitting, **RX estimate** models the
original coherent match and that detector. Expanded details retain the
coherent-only comparison. **RX reference** identifies a limited fallback model.
Still longer patterns can enable local differential matching. Supported local
geometries use a joint model of all three detector branches. Unsupported
geometries retain timing, search and compute diagnostics, with an explicit
coverage reason instead of a probability from a different detector.

The separate [LPI relative observation advisory](lpi-estimates.md) compares an
unkeyed energy detector's total observation with a one-symbol receiver design
reference. It normalizes both listeners to 18 dB Es/N0 in that one symbol, an
uncalibrated planning reference. Simulation on/off, the channel link budget and
oscillator presets do not change that ratio at fixed waveform geometry. Its
detection target is distinct from this whole-draft decoding probability and
from the receiver's admission thresholds; it does not predict a 1-in-N bit
acceptance rate.

The probability is conditional on finishing receiver computation; it does not
predict completion within a time limit. During computation, the GUI labels
the percentage as generated **audio**, with a separate elapsed wall time that
continues advancing while the receiver scores that input. After all transmitted
audio is generated, it reports **Checking reception after transmission** while
observed-absence samples are processed. Neither stage forces physical completion.
Elapsed computation freezes before the three-second replay or when cancelled.

At low SNR, an unaccepted bit cannot exclude overlapping searches. Those later
full acquisition passes add work that an already accepted bit can avoid. Thus
identical waveform settings can spend different amounts of computation on strong
and weak channels, and a nearly complete audio percentage is not a nearly
complete computation. The noise-model percentage is not a measured reliability
claim for the full receiver or its long, wide carrier search.

The adjacent **Oscillator model** selector provides the original free-running
crystal and three GPSDO cases: hobbyist XO without an oven, TCXO without an
oven, and OCXO. It sets the same relative clock offset and phase diffusion
for the sampled simulation and this estimate. The selected numeric values
appear alongside the dropdown. These are illustrative sensitivity models,
not measured GPSDO specifications; see [oscillator models](oscillator-models.md).

The fixed reference machine is an **Intel Core i9-13900H** with an
**NVIDIA GeForce RTX 4090 Laptop GPU**. The estimator never benchmarks the
computer, queries CPU/GPU identity, counts local processor threads, or measures
simulation execution. The GPU figure is hypothetical: the current numerical
search backends execute on the CPU. It describes a possible implementation
offloading receiver scoring while retaining waveform/channel generation and
projection work on the CPU. It does not promise an available GPU mode.

## Draft and channel inputs

The model takes the existing exact `transfer::Estimate`, the actual selected
transmit modem profile, independently selected receive profiles, FEC setting,
channel configuration and receive-key count. It does not encode a second draft
or allocate PCM. Short dictionary text and explicit binary input use their
exact wire-bit count and receive **no FEC benefit**, regardless of the saved FEC
choice. Longer text and attachments use the actual count of fixed 128-byte coded
intervals and their existing 192-bit markers.

**TX target C/N0 is a design input, not an enforced detection threshold.** In
automatic modes it chooses pattern duration: lower targets request longer
integration; higher targets shorten it until the applicable minimum pattern
length is reached. Fixed pattern modes keep their chosen length. The target
does not change the simulated signal or noise power. The preset supplies actual
C/N0 using transmit power, attenuation and the default 10 dB noise figure:

| Preset | Received power | Actual C/N0 |
| --- | ---: | ---: |
| 3dBm -170dB | -167 dBm | -3 dB-Hz |
| 3dBm -120dB | -117 dBm | +47 dB-Hz |

At 100 Hz with public auto-pattern and the default 1,500 Hz carrier, target
140 selects 64 chips and 1.28 seconds per bit; a +47 dB-Hz channel can therefore
receive it despite being below the design target. Target -61 requests
79,432,823 seconds (about 919 days) per bit. The default 100 ppm clock shift is
0.15 Hz, while the capped long-profile search covers only approximately
±6.45 microhertz. More nominal integration cannot compensate for drift outside
that finite search. The
current UI reports **Carrier outside RX search** for this long-symbol case,
with no numeric probability. A sampled regression receives exact `011` / `a`
for the strong 100 Hz case; it does not establish a calibrated 99.9% success
rate across channel realizations.

Useful weak-channel profiles need a design target near the actual link budget,
with enough integration margin for acquisition. As reference geometries,
public auto-pattern at target -6 dB-Hz chooses 256 seconds per bit at 1 Hz and
327.68 seconds per bit at 100 Hz. Both searches include the default 0.15 Hz
carrier shift. The sampled checks in [validation](validation.md) recovered
exact `011` / `a` at both geometries with the -170 dB preset and default clock
and phase noise. Those finite cases do not establish population reliability;
airtime and search coverage alone are also insufficient to predict success.

Elapsed simulated media includes the estimated waveform's settling, payload,
filter padding and suppression; channel delay, an average 175 ms randomized
startup and waveform clock-rate conversion; then
the same whole-symbol absence duration used by the live simulator, plus one
second of lookahead and twice the pulse padding. In particular, a four-hour
symbol requires a fully scored four-hour absent symbol. Compute time and media
duration are different quantities. UI replay pacing is excluded.

## Probability model

Numeric probability is available only when a matching receive profile covers
the simulated carrier shift, the expanded FFT core fits its modeled
workspace allowance, and every eligible detector lies within model coverage.
The application enables an expanded carrier/clock
search for pattern symbols lasting at least 16 sample-quantized seconds. Its
frequency lattice has spacing `0.25/T`, where `T` is the actual sampled symbol
duration. It retains the original local offsets when those suffice; otherwise
it adds symmetric pairs toward ±200 ppm of the configured carrier.
The search reserves the same intended waveform support above DC and below
Nyquist as modem validation, including RRC rolloff for shaped patterns, and
is capped at 4,097 distinct frequencies. Hitting a limit reduces the actual
covered span; it never makes the lattice coarser to claim complete coverage.

Each expanded frequency has two timing alternatives: the nominal sample clock
and a clock scaled by `1 + frequency_offset/carrier_hz`. This retains coverage
for both independent oscillator error and shared sample-clock error. The
maximum expanded bank therefore contains 8,194 frequency/timing hypotheses.
Projection bins shrink when necessary so averaging does not discard the
carrier offsets the bank is intended to score. All tone profiles and pattern
symbols shorter than 16 seconds retain the local bank of up to five frequencies.
Implicit pattern offsets outside valid waveform headroom are omitted at any
symbol duration, retaining the center even at a passband edge. The low-level
`PatternSearch` API also preserves its local-bank default; live and
transfer reception explicitly enable the application policy with
`expand_clock_search`.

Expanded coupled banks use the FFT receiver for public and private patterns.
The compact-private hint does not replace that comparison. If its core cannot
fit, application callers may retain the original local, nominal-clock
correlator search if it fits; live status identifies that narrower coverage. They never
substitute independent per-lane admission over the expanded clock bank.
Low-level automatic expanded requests require an explicit opt-in for this
fallback. Explicitly supplied frequency banks remain strict.

The model withholds its percentage and the UI shows **Wide RX search exceeds
RAM** when its approximate per-bank allowance predicts this case. A missing
profile or a carrier outside the requested wide bank takes precedence in that
label. The displayed coverage and compute times then describe the requested
FFT search, not the narrower fallback. No probability is inferred for that
fallback. The direct correlator API retains its existing local frequency banks.

The simulated shift includes both the explicit frequency offset and
`carrier_hz * clock_error_ppm / 1e6`. Outside that span, the UI shows **Carrier
outside RX search** and retains the CPU/GPU estimates. This is a model-coverage
limit, not a claim that reception has exactly zero probability.

For example, public auto-pattern at 1 Hz and a 32 dB-Hz target uses 128-second
symbols at the default 1,500 Hz carrier. The expanded application search uses
309 distinct frequencies spanning ±0.30078125 Hz, with both clock alternatives,
and includes the default 100 ppm simulation shift of 0.15 Hz. The sampled
regression requires exact `011` / `a` reception with that default clock error
and a completely observed absent symbol.

The former five-frequency search spanned only ±0.00390625 Hz. An explicit
five-frequency negative control retains the original reception failure. The
previous model incorrectly reported over 99.9% for `a` at +3 dBm/-120 dB because
it treated mismatch solely as signal attenuation; the
actual receiver divides fitted energy by total received energy, so signal that
does not fit the template can limit evidence even when thermal noise is tiny.
A high link budget cannot justify extrapolation outside the actual search.
The coverage check remains necessary after expanding the receiver, especially
for integrations long enough to reach the finite frequency-bank cap.

The channel SNR uses the simulator's `Fs/2` noise bandwidth. Before losses,
the integrated symbol energy is

`Es/N0 (dB) = channel.snr_db + 10 log10(symbol_samples / 2)`.

The original coherent approximation applies a fixed 3 dB implementation margin, squared-sinc loss for
residual carrier frequency after selecting the closest actual frequency
hypothesis using sample-quantized symbol durations, expected
coherent-energy loss from Wiener phase diffusion,
and a triangular correlation loss for pattern timing smear within a symbol.
For expanded banks the timing loss uses the smaller residual of the nominal
clock and carrier-coupled alternatives. The nominal alternative prevents an
independent oscillator offset from being mistaken for sample-clock drift.
Tone profiles omit the pattern smear term. These are analytical approximations
to the sampled channel; they do not reproduce adaptive tracking or the exact
public/private codeword correlations.

`phase_coherence_loss_db` describes whole-symbol coherent phase loss. It is not
the combined detector's complete penalty. Known pattern reversals are removed before coherent matching;
an unknown constant phase cancels, but phase wander reduces the match.

The production receiver additionally fits four fixed quarters for patterns
lasting at least 16 seconds with at least 16 complete chips in every quarter.
Each section fits its own complex gain, accommodating different amplitudes and
phases. Before scoring, the strongest quarter's explained energy is removed,
so an isolated tail or burst cannot supply all the section evidence. The
remaining explained energy is divided by the whole symbol's received energy.
Without that removal, the independent Gaussian reference gives
`Beta(4, N/2 - 4)` for N real samples, or `Beta(4, N - 4)` for N complex samples.
The section score uses the negative log of this survival function. Removing
the strongest contribution only lowers the statistic, so the same tail remains
a conservative bound. Existing covariance/Gram corrections apply before scoring.
This retains the penalty for eight fitted real coefficients. The receiver keeps
the coherent score
and uses `max(coherent, section) - ln(2)`, clipped at zero, to account for trying
both detectors. Ineligible profiles keep their original score.

In addition, sufficiently long pattern symbols use local differential soft
products. The receiver-local window defaults to 100 seconds, rounded upward
to whole chips with at least 16 chips. At least 256 complete windows are
required, so an ordinary dense pattern becomes eligible at 25,600 seconds.
Very narrow bandwidths require longer windows. Disjoint neighboring pairs
retain phase relationships; their statistic has its own finite-noise bound,
strongest-quarter removal and an additional `ln(2)` detector-choice cost.
See [the actual score](pattern-constellation.md#local-differential-evidence).

The local model samples a continuous Brownian phase path and correlated complex
noise in every complete window. Both bit patterns, the full coherent fit,
four-section fit and local products share the same observations and received
energy. It calls the production `DifferentialAccumulator` score, including its
eight phase directions, strongest-quarter guard and both detector-choice costs.
No local hard bit decisions or noiseless phase differences are substituted.
The two projected templates supply their actual local energies and complex
cross-correlations; shaped alternatives retain separate normalization weights.
Finite code power, transmitter radial limiting, and bounded quadrature of
shaped FFT projection bins determine the signal coefficients. Fitted and
unfitted signal both remain in the received-energy denominator.
Within-window phase integration uses bounded quadrature, with its second moment
corrected to the analytical coherent-energy mean. Template weighting within a
wandering window remains an approximation.

The supported local envelope is 256–4096 complete windows, with no partial tail
and boundaries aligned to the four sections. Projected-template integration is
capped at 524,288 observations; locally singular templates, appreciably
noncircular projected noise, local phase variance above 0.5 radian², or unresolved
local carrier rotation have no numeric probability. These are model limits,
not new receiver restrictions. The same receiver-local window helper and
workspace checks select the detector. `differential_model_available` states
whether its probability was included; `probability_model_limit` explains a
coverage failure. `differential_windows` and `differential_window_seconds`
describe eligible geometry, not a guaranteed live allocation.

Carrier candidates use the actual frequency lattice and shared noisy fits.
Banks of at most 17 candidates are evaluated in full when their local rotation
is resolved. Larger banks use a bounded neighborhood around nominal frequency
and the phase-path slope; omitted candidates set
`probability_search_approximation`. The actual search bank still determines the
admission threshold. This improves on selecting a signal-only best frequency
before adding noise, but does not reproduce full adaptive acquisition, clock
arbitration, interference or competing tracks. There is no hardware-class gate:
oscillator inputs are frequency error and phase diffusion.

The default estimate uses 4096 fixed trials; graph locations use 512. Returned
trial counts and 95% Wilson intervals describe **Monte Carlo sampling uncertainty
only**, not model error or physical-link reliability. Zero observed failures do
not establish perfect reception. For supported raw multi-bit cases, intervals propagate simultaneous
acquisition/continuation bounds under the existing independent-bit approximation;
no full-draft interval is claimed for the nonlinear FEC calculation. Headline
percentages use whole-percent precision. `differential_added_detection_probability`
reports first-bit admissions supplied by the local branch when the older two
branches fail in the same trials; it can be zero even though the branch is active.
Independent sampled multi-bit tests exposed a specific coverage limit: the
compact receiver retains the earliest admitted timing lane, even if a later,
closer lane has a stronger match. Later bits depend on that choice. A product
of independent nearest-lane probabilities overestimates some complete drafts.
Until this conditional timing selection is modeled, compact differential
multi-bit drafts have no numeric whole-draft probability. Their independently
modeled first-bit probability remains available through
`one_bit_confidence_available`, the one-bit graph, and its sampling interval.
This exclusion changes no receiver admission, pending bits or completion.
Shorter profiles continue to use the existing models below.

`drift_sections` and `drift_section_seconds` identify this geometry;
`section_phase_coherence_loss_db` describes phase loss in the longest quarter.
`drift_model_available` reports that the combined statistical model ran;
`coherent_reference_only` identifies an eligible but unmodeled fallback.
These fields describe eligible geometry, not a live allocation. Compact banks
can retain coherent-only scoring when section state cannot fit; the reference
conservatively keeps `ln(2)` even if that fallback would omit it. Runtime
`PatternReceiver::drift_tolerant()` and `PatternCorrelator::drift_tolerant()`
report the selected detector policy.
The coherent/four-quarter model below uses 4096 deterministic matched-statistic trials for the selected estimate.
It models correlated noise and a shared whole-symbol energy denominator, finite
correlation between bit patterns, and continuous Brownian phase drift through
four section means using bounded quadrature and a mixing approximation. Each
trial applies the strongest-quarter removal, rank-corrected Beta score,
`ln(2)` choice penalty and competing-bit margin. This models both detectors
together; it does not substitute quarter-duration phase loss into the original
coherent formula. It generates no PCM and does not run the full adaptive search
or evidence-chain logic. Finite-trial precision limits small differences between
reported probabilities.

The phase calculation uses 32 midpoint nodes per quarter, with Brownian
increments continuing across quarter boundaries and a correction to the known
mean coherent energy. For a resolved phase path, a least-squares phase slope
selects a neighborhood of at most six frequencies on the actual finite search
grid, including the nominal candidate. Coherent and combined detectors choose
their signal fit separately before matched noise is added. This approximates
the carrier bank's ability to follow a linear phase trend; it is not a full
adaptive search over noisy candidates. For strongly mixed phase, it transitions to a joint
complex-Gaussian approximation with section means and cross-section covariance.
That approximation neglects pseudocovariance and clips the negligible phasor
averages above unit magnitude. The remaining noise energy uses a bounded
Wilson–Hilferty gamma approximation. These choices keep cost independent of
symbol duration; they do not reproduce every extreme tail of the sampled
channel.

For patterns with at most 1024 complete chips, the model calculates quarter
correlations from the unshaped chip weights and sign mask. Denser patterns use
the orthogonal mean. Pulse shaping and within-section coupling between pattern
weights and a particular phase path remain approximations. The same fixed
normal draws serve both bit alternatives and both detectors; a small cache
reuses results for matching model parameters. Changing transmission length
does not select a different random experiment.

Fractional start timing is uniform over the nearest candidate's half-bin
spacing. Unshaped chips use triangular timing correlation; shaped chips use
the matched raised-cosine correlation. Energy lost at chip edges during
projection is removed before the score denominator, while phase-spoiled signal
energy that remains in those observations stays in the denominator.

The initial admission threshold includes the actual first FFT batch or compact
start window, frequency/timing alternatives, and private phase groups and four
initial stream symbols where applicable. Every eligible bit lasts at least
16 seconds, exceeding the six-second absence interval, so following bits need
standalone admission evidence. The combined model approximates that growing
threshold at the midpoint of the remaining draft. It does not reproduce the
full running search counter or competing evidence chains.

`coherent_success_probability` compares the original coherent score, without
the extra `ln(2)` penalty, in that same statistical scenario. It is a comparison
model, not a second measured receiver run. The CLI reports this comparison as
`null` when the combined model or confidence is unavailable. Support-only
planner searches bypass probability trials. The Link planner
curve shows one-bit reception at the fixed power, path, noise and clock settings;
it does not replace the whole-draft headline. Each build adds at most twelve
new statistical estimates of 512 trials, reuses a bounded cache and refines around probability
changes. Short coherent estimates use the cheaper analytical model. Clock/RAM
checks cover the existing graph samples; an unusable rounded target may use an
independently checked fit within 0.001 dB. Wider unsupported gaps break the RX line.
Intermediate values interpolate the sampled estimates.

Sections must still be coherent; four quarters do not provide arbitrary drift
tracking over a days-long bit. Differential windows can preserve shorter phase
relationships; the joint local model above includes their noisy products and
penalties where supported. Pattern transitions do not reset oscillator
phase error. This channel omits GPS servo corrections, which can constrain
long-term phase error in a real locked device. There is no special 0.01 Hz threshold.
The differential constellation points remain display diagnostics. All admission,
pending-bit publication and physical-absence decisions still require complete symbols.

For short/ineligible profiles and the limited coherent reference, with the
remaining linear energy `g`, the assumed bit error rate is
`0.5 exp(-g/2)`, the noncoherent orthogonal binary AWGN model. Symbol admission
uses a Gaussian energy-statistic approximation with mean `g` and variance
`1 + 2g`, threshold 5 for continuation, and a larger acquisition threshold
`-ln(1e-10) + 2 ln(trials + 1) + ln(2 * hypotheses)`. Here `hypotheses` includes
both timing alternatives when the frequency bank expands. Trials reflect the
half-chip start window, that actual hypothesis count and private phase groups
of a matching receiver.
For the four-section receiver's coherent reference, both thresholds additionally
include `ln(2)`; the bit-error approximation remains the coherent reference.
Other receive profiles/key families add compute work; their independent searches
do not raise that receiver's admission threshold.
The acquisition trial approximation describes the initial search geometry; it
does not reproduce the actual running trial counter during an extended scan.
The continuation approximation likewise omits the receiver's additional
comparison penalties and adaptive chain decisions.
These gates approximate the receiver's evidence requirements. Its real adaptive
scores are **not calibrated receive probabilities**. In particular, the model
does not turn the tuning planner's 18 dB target into an empirical success curve.

For raw/short drafts, the combined model requires correct initial acquisition
and correct admission of **every remaining wire bit**. For interval drafts, an independent-byte
dynamic program applies the existing Reed–Solomon capacity rule
`2 * erroneous_bytes + erased_bytes <= parity_bytes` to each 128-byte codeword.
A byte with any unadmitted bit is an erasure. The marker approximation allows
at most the existing eight erroneous/missing bits per 192-bit marker. Each
interval must survive. A union bound for runs of missing symbols covering six
seconds reduces interval-draft success for possible premature physical
termination; raw success already requires every bit to be observed.
An incompatible receive profile produces no
matching-profile confidence; an empty draft has no probability estimate.

The independence assumptions are especially approximate for oscillator drift,
correlated interference, adaptive acquisition and long receptions. Requiring
initial acquisition and every marker is conservative: the actual receiver can
recover some leading-marker damage. Additional exhaustive recovery, content
syntax, MAC validation and decoder success are not used as evidence of physical
completion or as extra confidence. A displayed value near 100% is still a model
prediction, never a guarantee or authentication claim.

## Fixed reference compute model

Link planner's **CPU estimate** compares estimated processing for a one-bit receive
preview with its incoming audio duration. It uses `receiver_cpu_seconds`: carrier
projection, acquisition and tracking, plus the fixed startup allowance, without generating
or resampling a synthetic channel. The simulation's existing `cpu_seconds`
includes that channel work. Both use the named reference laptop; neither
measures the current computer or reports CPU utilization.

The workload ratio is `receiver_cpu_seconds / simulated_seconds`. The audio
span includes the fully observed absent symbols needed to finish reception.
One second of CPU work per second of audio means equal modeled processing and
arrival rates; above that, the reference workload cannot keep pace on average.
Below it, FFT batches and competing searches can still delay reception or
overrun a live capture queue. This is a planning comparison, not a real-time
pass/fail guarantee. RX probability remains conditional on completing the work;
CPU pace does not change the waveform, LPI model or receiver admission rules.
The planner's compact CPU graph uses this same one-bit ratio across the time
graph's target range. Work estimates are cheap enough to evaluate at every
sampled target, including selected and refined RX points, without additional
probability trials. The vertical scale is logarithmic, and the reference line
marks equal processing and incoming audio durations. Unsupported Clock/RAM
targets leave gaps; independently checked fits within 0.001 dB handle tiny
rounding gaps just as on the RX curve.

The model counts full-rate waveform/channel samples, receiver projection work,
FFT acquisition, established-stream tracking or bounded streaming correlation lanes. It reflects half-chip
start searches, the actual bounded frequency/timing bank, private phase/initial-symbol
searches and key/epoch/profile multiplicity. Expanded coupled banks use the FFT
cost model, including when their requested core exceeds the allowance. For
unexpanded banks, large private symbols or an unaffordable FFT core retain the
existing correlation cost model.
The modeled per-bank allowance is the total divided by the modeled bank count,
capped at half the total to match the live receiver's initial per-bank ceiling.
The exact receiver's memory arbitration, reuse, bootstrap paths and template
cache behavior can select different work. The modeled allowance can withhold
confidence but does not guarantee that the receiver can allocate its workspace.

The FFT estimate uses the same projection-bin reduction as the receiver and
extends the observation window for the slowest covered clock hypothesis. The
transform starts at the next power of two covering twice the nominal symbol
length, growing when needed to contain the extended window. For symbols lasting
at least 16 seconds, a smaller transform is used when it retains room for at
least a quarter-symbol range of new starts and the tracking scratch. Acquisition
batches then cover at most half a nominal symbol of new starts. The first batch
covers at most one second of starts after a complete observation window is
available; later batches use the bounded hop. The estimate counts these live
passes without relying on an offline end-of-input flush. Established tracks
score available complete symbols at input progress boundaries independently of
the acquisition batches. These choices reduce input buffering latency but do
not remove the CPU cost of searching every configured hypothesis.
The work estimate allows for every scheduled acquisition batch; reception may
skip batches whose starts are all already excluded by retained accepted bits.
This allowance does not make the overall runtime estimate an upper bound.

The workspace approximation includes the FFT buffers, ring, energy prefix, row
metadata and separate tracking scratch whenever expanded clocks or a smaller
transform require it. When that core fits but retaining
both transformed bit templates for every hypothesis would exceed the allowance,
the model selects streamed FFT work: generate a template and perform its forward
transform for each job, using bounded scratch. Expanded live searches sharing
memory with other keys, epochs or profiles also stream their rows, preserving
room for the other banks. A single bank without section fitting can retain its
public transformed templates when they fit. Four-section jobs always generate
partial templates in shared scratch. An unaffordable expanded core
leaves confidence unavailable; the model keeps the requested FFT cost rather
than estimating the runtime's narrower correlator fallback.
The existing compact-private/correlator choices apply only to unexpanded banks.
These choices do not reduce the requested hypotheses. The
streamed-template estimate includes extra forward transforms and template
generation for public as well as private profiles, without benchmarking the
computer.

Four-section acquisition normally performs four forward/inverse template FFT
pairs per candidate bit and hypothesis. Summing the partial correlations also
supplies the coherent match, so it needs no fifth pair. Batches of at most four
starts directly match the available windows instead; the input spectrum is
still computed once. The core allowance adds 48 bytes per hop position, with
bounded additional worker scratch. Tracking stays one pass through the samples
and scores four section fits plus the coherent fit. Compact lanes retain one
active section fit and fixed-size summaries, without storing a bit's section
history. They reserve the added state when constructing the bank and allocate
it only when section accumulation needs it; if the added state cannot fit,
the original coherent bank remains usable. The estimate includes the extra
work; bounded memory does not imply that every long or wide search is
computationally practical.

For each matching FFT profile, the model also budgets one desired stream
acquired at its first bit. Acquisition supplies that bit; continuation
then scores `wire_bits - 1 + ceil(6 / symbol_seconds)` complete windows. This
includes the full observed-absence duration, even for hours-long symbols, and
does not add transmitted bits. The count uses nominal symbol durations; clock
scaling at an exact six-second boundary can add an observed absent window.
Each window fits five timing refinements per
possible phase group, then compares every other carrier/clock hypothesis at
the selected timing. Template pairs are reused across the five timing fits.
Private patterns budget up to three phase groups. Unrelated key/epoch banks
and incompatible profiles add acquisition work, not additional desired streams.
The correlator's continuous lane budget already covers its observation time;
it receives no duplicate FFT-tracking charge.

`tracking_seconds` reports a serial-rate allowance, included in **both** CPU and
hypothetical GPU totals; `tracking_symbol_windows` reports its window count.
Long-symbol continuation can distribute independent carrier fits across bounded
CPU workers. This model conservatively retains the serial-rate allowance rather
than assuming that those workers or their workspace are available.
The GPU projection accelerates FFT scoring but does not assume a GPU rewrite
of established-track continuation. This is a one-stream planning allowance,
not a runtime upper bound or a probability-weighted expected runtime. Competing
or noise tracks, late acquisition, replacements and reacquisition can change
the work. The workload correction does not change the RX probability model.

For streamed long public patterns, the CPU receiver can optionally cache the
unmodulated nominal-clock waveform before applying each carrier offset. This
cache is released after the input push and is used only when it fits without
reducing the affordable FFT worker count. Time-scaled clock hypotheses keep
their own waveform generation. The engineering estimate retains its ordinary
generation allowance; it does not promise a cache hit or its measured speedup.

For eligible differential patterns, acquisition includes an additional full
template-generation pass and window transforms for both bit alternatives.
The same input FFT is reused. Small batches use direct full-symbol matches
when their start count is at most `max(4, window_count * log2(FFT_size))`;
broad batches pay the many-window FFT cost. Tracking adds local accumulation
and product evaluation while retaining the existing one-stream allowance.
The compact estimate includes the additional active local fit work. FFT
workspace includes a fixed differential accumulator for every batch start,
and the compact budget includes the paired active fits and summaries.
There is no per-lane array proportional to the number of local windows.
These additions can be substantial; the existing four-quarter compute figure
does not represent a many-window differential search.

The fixed engineering budgets are:

| Assumption | Value |
| --- | ---: |
| Serial CPU work | 1.5 billion equivalent operations/s |
| Aggregate CPU scoring work | 8 billion equivalent operations/s |
| Hypothetical GPU scoring work | 80 billion equivalent operations/s |
| Host-to-device transfer | 8 billion bytes/s |
| Channel generation/resampling/noise | 400 equivalent operations/sample |
| Receiver projections | 40 equivalent operations/sample/bank |
| Tracking template-pair generation | 40 equivalent operations/projected bin |
| Tracking paired fit | 32 equivalent operations/projected bin; 64 for exact-real Gram fits |
| Tracking evidence calculation | 64 equivalent operations/fit |
| Differential local product/score allowance | 128 equivalent operations/window |
| CPU startup allowance | 30 ms |
| GPU path startup allowance, including CPU startup | 110 ms |

FFT work uses a conventional five-operation complex FFT element/stage estimate
plus template multiplication and scoring; correlation uses 64 equivalent
operations per two-bit lane observation. These are deliberately rounded
**assumed effective budgets**, not vendor benchmark results or measured
application throughput. The CPU aggregate budget allows parallel scoring on
the reference laptop while keeping channel and tracking allowances separate.
The GPU budget reserves substantial headroom for double-precision arithmetic,
memory access and irregular batches; it is not derived by multiplying CUDA
cores by advertised FP32 clock rates. A tenfold scoring budget never becomes a
tenfold whole-program speedup because CPU work, transfers and startup remain.
Very small jobs can consequently have a slower hypothetical GPU estimate.

The hardware names anchor a single repeatable high-end-laptop planning case.
[Intel's processor listing](https://www.intel.com/content/www/us/en/ark/products/series/230485/13th-generation-intel-core-i9-processors.html)
identifies the i9-13900H as a 14-core part with up to 5.40 GHz turbo.
[NVIDIA's laptop comparison](https://www.nvidia.com/en-us/geforce/laptops/compare/)
lists the RTX 4090 **Laptop** GPU with 9,728 CUDA cores, a 1,455–2,040 MHz boost
range and an 80–150 W subsystem range. Those specifications identify the
reference products; **they do not establish the assumed throughput rates**.
Laptop power limits, sustained thermals, compiler choices, caching and eventual
GPU implementation can change runtime substantially. Treat the displayed times
as order-of-magnitude estimates; even a factor of four is not a validated error
bound. Content compression, exceptional recovery searches and concurrent
background tasks are excluded. A total shorter than simulated airtime does not
guarantee real-time operation: FFT block scheduling can delay acquisition and
accepted bits, and bursts of work can exceed the live capture queue's capacity.
See the [1.2 kHz case study](1200hz-weak-link-planning.md).

The UI supplies the profile geometries of every distinct receive-key family,
including plaintext where permitted, so private epochs and search costs are
counted separately. The API's optional `receive_key_count` can instead scale
equivalent profile banks. The exact dynamically allocated and retained bank is
not reproduced by this compact model. The UI's receive-profile match concerns
waveform geometry, not the secret key's validity or successful authentication.

## Verification

See the [dated validation record](validation.md) for measured discrepancies and
coverage. Passing a finite set of synthetic captures does not certify the
whole parameter range, adaptive carrier search, or a physical RF link.

`simulation_estimate` checks deterministic reference estimates, SNR and draft
length response, fixed-interval FEC versus unprotected short input, incompatible
receive profiles, additional receiver work, phase noise and full four-hour
absence accounting. It also feeds the reported 1 Hz case through the sampled
channel with expanded application search, retaining an explicit old-five-bin
failure control and zero-drift controls. A capture without a full absent symbol
must remain incomplete even at EOF. Coverage checks include both signs, exact
frequency endpoints, sample quantization, the finite cap, and independent
carrier versus coupled sample-clock error. `pattern_search` separately checks
the 16-second expansion boundary, short/tone compatibility, projection-bin
divisors and very large sample coordinates.
Compute regressions distinguish retained templates, streamed FFT templates and
unaffordable expanded cores, preserving the requested carrier span while
withholding unsupported confidence. Tracking regressions check exact bit-count
scaling, nominal complete-symbol absence, empty drafts, matching profiles,
unrelated keys and avoiding duplicate correlator work. CLI checks retain the
exact one-bit and three-bit wire paths while exposing the tracking breakdown.
Differential boundary regressions retain the four-quarter model immediately
below eligibility, exercise the joint model above it, withhold probabilities
outside model coverage, preserve phase/search diagnostics, and include the
additional FFT and tracking work. The full sampled differential matrix carries
the CTest `calibration` label; its pulse-shaped captures are substantially slower
than matched-statistic tests. `differential_probability` checks the joint
statistic against an independent noncentral-beta coherent limit and weak-product
controls. `differential_receiver_probability` compares predictions with
independent sampled channel captures through the production receiver.
These checks validate model mechanics, not its empirical
calibration. The independent protocol and physical-completion regressions remain
the authority for actual transport behavior.
