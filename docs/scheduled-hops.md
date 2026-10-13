# Scheduled burst hops (local experimental candidate)

**Sched_Hop** spreads a Robust Modem transmission over keyed active and null
time slots. It gives the receiver additional physical time to process captured active samples.
Queued processing may cross subsequent bursts when memory and sustained throughput permit. It is independent of Fake FHSS: Sched_Hop changes physical audio
by inserting silence, whereas Fake FHSS illustrates tuning without retuning the
radio. Both can be enabled together. Sched_Hop requires a selected encryption
key and matching peer settings.

This is a manual-test candidate, not qualified hardware, receiver-sensitivity or
security coverage. The packed complex, mapped split complex and amplitude paths have no
calibrated scheduled full-bank RX-success probability. A favorable CPU estimate is not proof of
real-time reception. The [receiver strategies](receiver-strategies.md),
[clock timing](clock-sync.md), [spread-spectrum controls](spread-spectrum-controls.md)
and [transmit cover](transmit-cover.md) describe the underlying paths and their
separate evidence.

## Controls and timing

The Console places **Sched_Hop** after FHSS. Its choices are **disabled**, **10**,
**100**, **1k** and **10k**. With the ordinary dilution of one, these mean one
active hop per 10, 100, 1,000 or 10,000 total hops. Each base hop is 0.4 seconds;
the setting is a nominal total-time multiplier, not an added frequency spread.

Developer mode exposes **Sched_Hop Dilution** beside the receiver controls at the
bottom of the persistent Link planner. It sets the number `G` of consecutive
active hops inside a frame of `M` total hops. Two active hops in a ten-hop frame
therefore give an effective 5× multiplier. They do not make a twenty-hop frame.
Changing to a smaller `M` clamps the GUI dilution to that frame's size. Imported
settings with `G > M` are rejected; disabling scheduling retains the preference.
`G = M` is continuous transmission and uses the ordinary source-time map.

The keyed group start is chosen in the first `floor((M-G)/2)+1` positions of
each UTC frame. Restricting that range guarantees a processing interval even
when adjacent keyed choices are unfavorable:

| Quantity, before capture uncertainty and ramps | Duration |
| --- | --- |
| Frame | `0.4 M` seconds |
| Consecutive active group | approximately `0.4 G` seconds |
| Mean null interval | `0.4 (M-G)` seconds |
| Minimum null interval | `0.4 ceil((M-G)/2)` seconds |
| Maximum null interval | `0.4 ((M-G)+floor((M-G)/2))` seconds |

For `M=10, G=1`, this is a 4-second frame, 0.4-second active group, and 2 through
5.2 seconds of null time between groups. For `M=10, G=2`, the group is 0.8 seconds
and the null interval is 1.6 through 4.8 seconds. These are calendar bounds, not
measured processing deadlines. Multiple receive keys, arrival uncertainty and
clock hypotheses can require recording overlapping groups and consume the quiet
interval. A broad several-second prior can erase most of the recording benefit
of 10× scheduling. A tighter qualified prior can make that union much smaller.
The planner selects using estimated sustainable mean work. It also displays the
more stringent work-to-next-active-group ratio as a diagnostic, not a mandatory
per-hop clearance deadline. Split-DSSS weight construction and setup remain
uncalibrated outside the explicitly measured family below. These are engineering
estimates pending complete execution evidence. The processing budget
is advisory; an uncertain estimate does not authorize discarding hypotheses or
forbidding transmission. A per-frame raw recording-union demand is not measured
peak memory or a guarantee that the configured workspace can retain it.
The conservative recording bound is currently 100% unless the live receiver has
an applicable complete capture certificate. Nominal active duty and the estimated
union of eligible FFT windows are separate quantities; neither proves that raw
capture can be discarded between them.

The **Freq search** dropdown is enabled only with a selected key and enabled
Sched_Hop. Its 1×, 2×, 4×, 8×, 16× and 32× choices widen oscillator frequency
uncertainty; they do not widen the oscillator clock-rate bound. The complete
expanded frequency domain must fit the input passband and receiver workspace.
The implementation retains the required hypotheses or reports unsupported
coverage. More null time does not supply more RAM, more sampled bandwidth or a
calibrated sensitivity model.

Exact draft airtime uses the keyed source-to-physical map. Planning without a
usable transmission anchor uses a labeled representative positive UTC anchor;
it does not change the actual transmitter's anchor. Mean per-bit airtime and one
bit's exact keyed wall span are different quantities. The hidden model details
retain their assumptions and fallback labels.

CLI and launch/parameter-list representations are:

```text
--sched-hop disabled|10|100|1k|10k
--sched-hop-dilution N
--sched-frequency-search 1|2|4|8|16|32
```

The parser also accepts `off` or `1` for disabled, and `1000`/`10000` for
`1k`/`10k`. Older configurations default to disabled, dilution one and frequency
multiplier one. Launch commands and parameter lists preserve all three
preferences, including inactive ones. The active schedule still requires the
selected communication key; a receive-only keyfile does not enable the TX
control.

## Split symbols and cryptographic coordinates

A long symbol may span many active groups. The transmitter retains two time
coordinates: physical UTC/sample position `t`, and waveform-source position
`u(t)`. Source time advances through each active group and pauses in null time.
The ordinary private coefficients, full DSSS Interleave construction, finite
pulse shaper and limiter follow source time. Carrier phase and the separate
FHSS calendar follow physical time. Filtered PCM is not shuffled or restarted
at a hop boundary, and no whole-symbol PCM buffer is required.

At sample rate `Fs`, one active base hop consumes `floor(2 Fs / 5)` source
samples. A group consumes `G` times that many contiguous samples. Physical
boundaries use the exact rational 2/5-second calendar with integer floor/ceiling
operations, without accumulated rounded-dwell error. Any fractional-boundary
remainder is placed at the group's end as null samples. Common 6,120, 8,000 and
48,000 Hz rates have no remainder; other rates add less than one sample of
rounding per active base hop. Absolute coordinates remain integral, with a
separate local fractional residue.

The start/right inverse skips a quiet interval to the next active group. The
end/left inverse identifies the preceding active end, allowing a fully scored
bit to be published before a long null interval. Each payload bit's private
pattern epoch and ordinal derive from its actual physical first-payload sample
through this map and, for protected short bits, the joint slot calendar below.
A compressed global UTC coordinate is not substituted for that address. Bit
positions and independent 0/1 candidates remain unchanged; neither silence nor
dilution inserts payload bits.

### Protecting short bits

For a bit shorter than the applicable gate period, a conservative full-power
fraction below one half selects shared transmitter/receiver deferral into a
complete full-gain slot. The gate period is one 0.4-second base hop when FHSS
power ramps are present, otherwise the consecutive `G`-hop active group, using
the source map's integer-sample lengths. Both conditions are required: a long
symbol remains split even when guards remove more than half its source interval.
A slot lies inside the intersection of the scheduled
active group and every applicable FHSS full-gain window, with the complete finite
pulse support reserved on both sides. This is one joint calendar, rather than
separate deferrals whose placements could disagree. If no such slot exists, the
geometry is explicitly unsupported. Long split symbols retain the continuous
source-time construction described above.

Waiting positions still advance the source map through the active group, but
contain independent cover rather than payload candidates. They do not increment
the payload bit index and do not count as scored absent symbols. The calendar
adds airtime without repeating a bit or restarting its private pattern. Exact
payload starts, key-exposure guards, duration estimates and receiver candidates
use the same slot ranks and physical UTC starts.

The integer calendar reserves the minimum slot capacity across the five possible
fractional 0.4-second hop residues. Direct rank lookup and bounded window search
avoid arrays of all elapsed frames or all sample phases. Packed starts are exact
singleton phase cells anywhere in the UTC second; the old private-address gcd
does not describe their guard offsets. Acquisition and its work model enumerate
these actual keyed starts, including epochs with no possible start, while
retaining the admitted fractional-arrival and clock cells around each one.

Calendar generation uses the dedicated FHSS purpose with new versioned domains
`SchedHopKeyV1` (`sh-key01`) and `SchedHopUtcV1` (`sh-utc01`). The geometry-specific
child root binds both `M` and `G`; each UTC frame uses its own bounded address.
It does not borrow Data, Scrambler/private-pattern, DSSS or ordinary FHSS
keystream positions. The immutable schedule supports direct frame lookup;
it does not replay a finite calendar or restart a byte stream at audio blocks.
Unrepresentable future coordinates fail rather than wrapping. The small frame
cache is exact and local to its sequential source or receiver bank; immutable
plan, key and sample rate identify its lifetime, and a new frame invalidates it.

When multiple group positions are available, the key hides which will be
selected. High dilution reduces that choice and can leave one publicly
determined position. The public frame geometry and emitted bursts remain
observable. This is not a claim of hidden
operator activity, thermal-noise resemblance or unconditional timing secrecy.

## Guards, shaping, energy and termination

The final active-group gate uses the existing smooth quintic transition
`10z³−15z⁴+6z⁵`, with zero slope and curvature at its endpoints. This gate is
applied after the ordinary source shaper; any additional FHSS gates retain their
own physical-time boundaries. The source does not rewind or replay while quiet.

Let `B = clock_TX + clock_RX + region/2`, where each clock value is a plus/minus
bound, region is total width, and `A` is nonnegative audio lag `[0,A]`. An active
group reserves `B` at its beginning and `B+A` at its end, plus one ramp of duration
`R` at each end, rounded outward to samples. Default scheduling uses ±10 ms at
each station, no additional region, and the selected Audio error (hidden default
0..50 ms). A custom clock policy supplies both endpoint clock bounds, its region
and its fixed arrival offset. The offset shifts receiver arrival timing; it does
not select different secret groups or stretch the uncertainty interval.

Before any separate FHSS gate or spectral-window omission, a conservative
full-power fraction of active source time is
`max(0, (0.4 G − 2B − A − 2R)/(0.4 G))`. The ramps still contribute some energy;
this expression deliberately counts only the flat part. It is not an exact
received-energy or sensitivity prediction. Unsupported guards that leave no
flat samples are rejected. Final measured spectral width, usable energy and
detector loss must include both gates, pulse tails, sample rounding and the
receiver's whole-window selection.

The planner also adds a full nominal bandwidth allowance of `2/R` Hz for each
independent smooth gate. A 10 ms scheduled ramp contributes 200 Hz; an additional
FHSS gate contributes its own scale, and the scales add because cascaded gates
convolve spectra. This is an engineering rolloff allowance, not a strict spectral
cutoff or regulatory mask. Inner-chip and symbol geometry remain unchanged.

Training, prefix cover and suppression use the same source map. Their wall time
can grow approximately with `M/G`, plus keyed group placement and guards.
Protected stopping preserves already-contributing source coefficients, continues
independently keyed cover for at least three seconds of useful active cover,
then completes the shaped shutdown. This is intermittent cover; it does not
promise three uninterrupted wall seconds of masking during scheduled nulls.
Ordinary Cancel does not wait to admit an entire long symbol, but retained pulse
support and the protected cover can still make the final wall-time stop long.
**Abort now** remains prompt and explicitly forfeits echo protection.

UTC preparation and protected-stop checks use physical wall-time horizons, not
source-time seconds subtracted from a paused physical map. Independent waiting
cover and suppression do not extend the valid-payload horizon; previously emitted
payload pulse support does. The immutable timing reference and accumulated
correction budget survive null hops. Large multipliers with tight UTC settings
can remain unsupported under the unchanged receiver-compatible clock guard,
even when their mean CPU estimate is favorable. This is not a claim that every
multiplier is usable on hardware or that arbitrary clock wander can be tracked
indefinitely.

## Receiver and workspace limits

Packed short symbols use the calendar-aware existing complex FFT receiver when
its complete declared frequency/clock bank is covered. Each candidate occupies a
complete full-gain slot, with finite pulse support reserved on both sides. The
receiver enumerates actual keyed singleton starts in the UTC epoch; it does not
turn the calendar's one-sample address grid into a row for every input sample.
Initial symbol alternatives and independent private 0/1 candidates retain their
original addresses. The complex I/Q statistic, energy/covariance calculations,
quarter guards, trial charges and numerical fallbacks are unchanged.

Continuation scores the complete carrier/clock bank. Its timing radius includes
rate displacement across the actual keyed gap from the last accepted anchor;
a failed slot does not reset that anchor. Cache entries remain specific to the
key, epoch, exact calendar rank, independent bit candidate and hypothesis.
Calendar/phase storage and the continuation ring are charged before allocation.
An uncovered frequency/clock bank or exhausted workspace remains explicit.

### Long split symbols

Automatic, Existing only and Complex only can use a mapped complex streaming
correlator for a completely supported split-symbol bank. This is a physical-I/Q
direct contraction, distinct from the packed short-symbol FFT path. The keyed
source map controls private coefficients and finite pulse positions; carrier
phase continues in physical time across nulls. Samples from separate active
groups are never concatenated as if carrier time had paused. Original limited
templates include scheduled/FHSS gains, with both real-input I/Q basis vectors,
their Gram covariance, usable observation energy and noise degrees retained.
Zero-template-gain samples add neither signal evidence nor noise degrees; PCM
is not divided by the gate gain.

Physical coherence requires a finer frequency bank when scheduling increases a
symbol's wall aperture. The metadata-only builder uses a conservative physical
span `A` in samples, enlarged for the slowest admitted physical clock rate,
with frequency spacing at most `0.25 Fs/A` and clock spacing
at most `2e6 min(chip/4, flat/8)/A` ppm. It retains every original exact alternative
and appends the required refined lanes. Oscillator topology and UTC correction
supply joint coverage provenance; marginal extrema alone do not certify it.
Without an oscillator topology, a declared rectangular domain is conservatively
overcovered. Finite hypothesis/passband/workspace limits remain enforced.
Explicit diagnostic banks may run without a continuous-domain certificate, but
must not be advertised as full coverage. More null time therefore does not make
a fixed coarse coherent frequency bank sufficient.

Noise normalization follows the actual selected observations. A rejected
prototype capped raw-sample evidence degrees while keeping broadband raw noise
energy; it would sacrifice useful weak-signal gain and is not the shipped score.
The coherent calculation retains its raw I/Q fit and white-noise sample count.
The existing nested projected-chip excess guard may only lower that evidence;
projected energy and rank change together. Four-quarter correction requires
rank-two quarter fits and aligned chip/quarter partitions, uses the complete
quarter projection for the excess check, and retains strongest-quarter rejection
for admission. Uncertified quarter geometries keep the preceding conservative
quarter fallback, exposed by a work counter. Differential evidence remains
separately whitened. These detector guards require their own sensitivity and
structured-interference qualification; clean decoding is not that evidence.

Quarters, local differential windows and symbol ownership follow source time.
The physical completion fence includes the entire timing/phase/clock cell. If
the next symbol begins before that fence, separate charged carry state retains
its fresh independent candidate evidence; it is not discarded or assigned to
the previous bit. More than one extra source symbol beyond the fence explicitly
exhausts supported coverage. Neither a null interval nor filter draining creates
failed-symbol evidence, and the original timing reference is never restarted.

Amplitude-energy reception remains the separately guarded fallback when complete
mapped complex geometry/state is unavailable, or when Amplitude only is forced.
It retains whole nonoverlapping real-input FFT observations only when the entire
window fits the admitted active flat portion, including timing cells, clock
scaling and any FHSS guard. It never concatenates samples across a null into one
FFT. Original shaped-envelope quadrature follows source time; capture and
publication retain physical time. I/Q bin energy, noise degrees, quarter guards
and trial accounting accompany every retained observation.

Every noncontinuous scheduled profile retains the possible physical start phases
throughout the full UTC second, including when a source symbol is shorter than one second.
The ordinary `min(symbol samples, sample rate)` phase extent relies on shifting
the symbol index along a continuously advancing source. Scheduled null plateaus
break that equivalence: reducing a late physical start modulo the source-symbol
duration can move it into a null hop and discard the transmission. The shared
`canonical_phase_extent` therefore returns a full second for scheduled bursts;
ordinary and continuous G=M profiles retain their preceding extent. Active-keyed
pruning and exact packed-slot enumeration still apply separately. Receiver,
planner and simulation counts charge the retained domain rather than silently
truncating it to improve cost estimates.

For split amplitude reception, the receiver and planner share a flat-safe geometry calculation. A long chip's
ordinary half-chip timing cell can be wider than an entire active interval;
retaining that cell unchanged would leave no complete FFT that is safe for all
represented arrivals. The scheduled path therefore refines coverage cells:

- Canonical phase spacing uses an exact divisor of the original cell, small
  enough for the active flat interval. This retains every original representative
  and its cryptographic boundaries. Packed short-bit phases remain the exact
  singleton slot starts.
- Timing spacing is the original half-chip grid divided by a power of two,
  with at least two subdivisions on the scheduled path: no wider than a quarter
  chip. The preceding half-chip origins remain represented; a narrow qualified
  arrival prior prunes that grid without moving it to the edge of the prior.
- Clock spacing is refined by binary subdivisions, preserving the preceding
  scheduled wall-span clock-grid representatives and the complete declared
  clock interval. The maximum remains 65 lanes; a required larger bank is
  unsupported. The ordinary unscheduled clock grid is unchanged.

With `W` usable flat samples, phase cells are bounded by about `W/8`, full timing
cells by `W/4`, and clock displacement at each endpoint by `W/8` over the physical
wall span used for geometry. FFT size is then chosen from the remaining complete
window. These limits follow active flat duration rather than an arbitrarily long
chip. Refinement adds charged timing/phase/clock work and can exhaust workspace;
it does not remove hypotheses, change private addresses or certify sensitivity.
The planner reports the actual refined spacing and retained-position count.
Mapped-complex admission uses the same exact origin/phase enumeration as its
constructor. Its cheap necessary-state lower bound can rule out impossible RAM
shares; it does not certify complete allocation. The constructor additionally
reserves prefixes, private/permutation state, optional detectors, retained bits
and caches against the actual workspace share.

Within an FFT interval already proven flat for the complete physical coverage
cell, the source map is affine. The original canonical grid aligns with active
group edges, and receiver refinement uses exact divisors of that grid. Thus an
admitted active anchor's whole cell remains in the same affine active group;
fractional-hop and packed geometries have singleton cells. A canonical phase
displacement shifts both the physical argument and its source anchor equally,
so it cancels in their difference. Source-envelope and quarter boundaries
therefore do not discard another phase-cell width. The physical flat-window
guards and latest admitted endpoint remain unchanged; this cancellation does
not permit observations across a ramp, null or source-map discontinuity.

Split amplitude eligibility requires shaped private patterns, at least 64 chips, at least 64
retained whole energy observations per symbol, a complete clock grid of at most
65 lanes, and a positive-frequency band with all transition margins inside
capture Nyquist. Scheduled Interleave DSSS and supported FHSS masks use this
path; legacy outer DSSS, too-short symbols, inadequate flat groups or unavailable
coverage remain explicitly unsupported for amplitude reception. Existing only
and Complex only prohibit that fallback; a complete supported mapped complex
bank remains available at those settings.
`G=M` returns to ordinary decoder eligibility. Forced Amplitude only retains the
64-observation requirement even for packed slots. Automatic, Existing only and
Complex only can instead select the packed complex path when its full bank is
supported. Split symbols may use the mapped complex path under the same
policies; forcing Amplitude only never relaxes its independent guards.

| Scheduled geometry | Production selection | Important limits |
| --- | --- | --- |
| Short symbol packed wholly inside a full-gain slot | Existing complex FFT | Complete carrier/clock coverage; exact keyed slots; bounded continuation ring |
| Long split symbol, complete refined bank fits | Mapped physical-I/Q direct complex | Physical-aperture refinement, finite banks/passband, carry and workspace bounds |
| Long split symbol without supported complete complex state | Automatic may use amplitude; Amplitude only forces it | At least 64 retained whole observations, refined clock grid and complete flat-window coverage |
| Continuous `G=M` | Ordinary receiver policy | Ordinary eligibility and numerical fallbacks |

### Unavailable receiver versus exhausted workspace

The ordinary 3,600 Hz rate / 1,500 Hz carrier defaults now use packed complex
reception after a communication key is selected and Sched_Hop is enabled. At the
resolved 14,400 Hz input rate and eight samples per chip, the short/long target
profiles have 1,024/512 sample symbols (71.111/35.556 ms). The preceding candidate
incorrectly required these profiles to use the amplitude detector. Its minimum
16-sample FFT gives only 56/24 retained observations after boundary allowances,
below the required 64. That diagnostic remains correct for forced amplitude
reception; it no longer prevents the full-complex alternative.

The GUI's representative pattern illustration uses a deterministic public
synthetic schedule key. It never borrows communication key bytes. Previously,
removing the illustration's key while retaining its scheduled geometry could
make valid TX settings report a missing encryption key. Actual transmission,
exact keyed airtime and receiver banks continue using the selected real key.
The public illustration is not evidence of receiver availability or sensitivity.

Live reception now retains the concrete eligibility or allocation error for
Automatic selection as well as forced strategies. Unsupported initial banks
show **receiver search limited**, with `RX unavailable:` and the reason in the
status detail; partial banks show `RX search incomplete:`. They retain their
physical sample coordinate and retry at ordinary UTC epoch-admission fences,
instead of resetting acquisition and retrying on every PCM callback. A valid
configuration change clears this diagnostic. Actual processing failures that
discard acquisition state still report **receiver restarted**.

A nominal-arrival geometry preflight also reports intrinsically unsupported
profiles during long null frames. It retains production private flags, joint
FHSS guards, the declared oscillator frequency/clock grid and the applicable UTC
rate-correction allowance. Only arrival uncertainty and workspace are optimistic.
Clearing the oscillator here would hide an unsupported forced grid whenever the
current keyed epochs were quiet. Passing remains only necessary: actual admission
still requires the complete configured uncertainty, epoch and cohort-workspace
checks. This preflight neither admits a receiver nor removes a hypothesis.
Verbose `receiver_profile_preflight` records its grid, backend, reason and elapsed
time separately from actual epoch admission.

This eligibility handling also applies to an unsupported forced ordinary
amplitude profile. A keyed epoch proved to have no admitted active start is
separately identified as an empty epoch, not a decoder failure. Missing coverage
revokes any existing capture-omission certificates, including when a quota or
candidate limitation arises during processing. Unavailable banks cannot admit
bits, certify skipped capture, or manufacture physical completion. RX eligibility
details survive transient audio/TX status clearing; an actual TX error retains
display priority. The snapshot's `receiver_bank_builds` diagnostic counts initial
construction and subsequent admission refreshes, independently of successful
receiver admissions.

Search arithmetic remains independent of the schedule. Split amplitude Default
and FP32 minimum use the demonstrated native FP32 spectral FFT and packed FP32
envelope path, with finite-range FP64 transform fallback. FP64 force uses native
FP64 FFTs and original FP64 coefficients, bypassing approximate interpolation.
Energy and limiting calculations retain FP64, and sensitive moments/scoring use
wide arithmetic. All settings retain the envelope-quadrature approximation;
FP64 is not an exact continuous-envelope integral.

Packed complex reception retains the existing arithmetic dispatch. Small and
unsupported native geometries use FP64 operands, including under Default and
FP32 minimum; eligible large acquisition geometries may use native FP32 with
bounded numerical fallbacks. FP64 force keeps original FP64 templates and
bypasses approximate template interpolation. Exact reuse is allowed at every
precision. The trace identifies actual resolved operands/backend rather than
inferring execution precision from the dropdown.

Mapped split complex physical projections, products, real Gram terms and scoring
retain FP64 at every public Search arithmetic setting. Default and FP32 minimum
may additionally use the bounded source-curve and physical-moment reductions
below. FP64 force keeps original per-sample templates and bypasses these
interpolations. Globally affine continuous-source caches cannot cross nulls
unchanged; exact worker-local private-chip reuse remains allowed at every setting. Each
source map has an eight-frame exact keyed calendar cache; an entry is identified
by frame index within one immutable plan/key/sample-rate instance. Exhaustion
recomputes the frame exactly, and copies own independent storage. The local ABI
adds 312 bytes per map versus the preceding single-frame cache; that is not a
process-RSS or complete receiver memory measurement.

Mapped complex input preparation can omit mixing only where source-map
monotonicity and timing/phase/clock endpoint bounds prove the whole input block
null for every retained lane. This is key-specific preprocessing reuse, not a
capture-omission certificate: original raw PCM remains retained for rolling banks.
Permanent physical END and first-full-symbol coordinates are cached under the
immutable key/calendar, origin, rate, phase and source-symbol identity. Temporary
adjacent-symbol carry has a separate source-start identity. A cached null deadline
also includes timing, phase-cell width and accumulated clock-cell uncertainty;
reaching an END fence always executes ordinary scoring and publication. A null
fast path advances physical counters only. It supplies no fit, trials or absence.
Unchanged progress polls need not rescan every lane, but committed/queued output
and timing work keep publication pending for the next poll.

Mapped contraction uses bounded 512-sample dispatch blocks; its original raw
diagnostic retains 128. Exact worker/private-chip caches may survive PCM pushes,
remain fully charged. Live retains them across ordinary admission refreshes,
but reclaims them before they could exclude a required rolling bank or a
promised detector reservation during workspace reduction. Reclamation changes neither observations nor detector
promises. This avoids repeatedly constructing worker state without letting
optional scratch exclude a required key, epoch or profile.

A table-affine flat-span prototype retains original ramps, clipping and boundary
fallbacks, with original real I/Q Gram and moments. The first full Live experiment
was slower; bounds-only preparation improved its one-bank result but did not
establish a complete-execution advantage. It is an internal diagnostic, not an
automatic production selection. FP64 force bypasses it. Fine-frequency moment
fusion is likewise a measured kernel prototype, not an integrated receiver gain.
The older affine prototype is separate from the new bounded pulse moments and
source curves below. The cost/workspace models retain engineering allowances;
complete live-bank CPU is measured only for the configurations below; sensitivity is not
calibrated. A successful bounded mapped
correlation is not proof that arbitrary multipliers, DSSS factors or oscillator
models are real-time feasible. Full-bank scheduled RX probability and GPU time
remain unavailable.

### Practical split-symbol manual checkpoint (2026-10-13)

The bounded full Live fixture uses encrypted production PCM, 6,000 samples/s,
0.25-second chips and 16-source-second symbols split across 0.4-second active
groups (`M=10,G=1`). It retains the complex coherent/four-quarter branches,
five exact original/refined frequency-rate pairs, up to 230 timing/phase lanes
per epoch and 54 resident rolling banks. Capture dates are mocked qualified
GPSDO-OCXO dates, not sound-card measurements. The full 348-physical-second
capture includes 34.8 source seconds, settling, cover and a complete absent
source symbol. Original input is retained; no raw recording omission is claimed.

An early mapped implementation accepted the bit but failed its 120-second
drain bound at about 184 physical seconds. That failure is retained. Exact
null/control caching, larger dispatch and reusable worker state subsequently
completed identical PCM in 115.808 receiver wall seconds and 906.683 CPU seconds.
The preceding direct512 reference completed in 129.054 wall/904.865 CPU seconds.
Worker reuse therefore improved this observed wall pair 1.114×, with essentially
unchanged CPU; a single pair supplies no speedup confidence interval. DSP peaks
were 47,763,702 versus 44,387,190 bytes (3.38 MB additional, 7.6%). These are
backpressured functional runs, not independent real-time backlog evidence.

The independent normal-rate producer subsequently passed: 87 active groups and
86 intervening null-clearance checks, 838.312 CPU / 348.256 wall seconds,
1.4507 seconds maximum backlog and 47,790,038 DSP bytes. Provider delivery lag
peaked at 13.0 ms. All previous active work cleared before the next active group;
null samples still arrived independently. At twice real-time pacing the same
fixture **failed** that clearance assertion at about 136 physical seconds with
2.048 seconds backlog. This strict per-hop failure does not establish unsustainable average throughput.
The later sustained fixture below measures that separately. Dates and provider are
mocked; actual Live processing, queues, admission, scoring and publication run.

The frozen preceding scheduled receiver used amplitude-only on the identical
PCM and completed in 49.233 CPU / 51.048 wall seconds with 29,264,000 DSP bytes.
Restoring complex evidence is therefore not a CPU improvement over that detector.
Its sensitivity difference has not been measured by these strong-signal runs.
The prior conditional amplitude penalty must not be treated as universal.

Without scheduling, the same public settings used the optimized existing complex
FFT/pulse receiver: 42 physical/source seconds took 0.373 CPU / 0.926 wall seconds
and 16,664,877 DSP bytes. That reference actually used its broader FFT timing
window; the requested narrow timing was unavailable and was explicitly classified
as such. Its waveform and energy differ: captures use the same synthetic key,
payload, noise seed, downstream gain and hardware peak limit, with no power
normalization. These are not identical-PCM or equal-received-energy comparisons.

This exposes an unfinished architectural efficiency gap. The mapped split direct
loop is much more expensive than the optimized continuous FFT/pulse path; that
cost is not a fundamental requirement of phase-sensitive reception. The normal
paced result proves headroom for this expensive scheduled bank, but does **not**
prove that this already-feasible continuous configuration became feasible because
of scheduling. The later follow-up below implements bounded shared pulse moments and source
curves. Calendar-aware transform processing remains a substantial opportunity. Broad-prior, high-DSSS and wide
oscillator banks need separate performance evidence.

Every received row was inspected: one intended raw bit, no extra admissions,
pending publication at 180.394667 physical seconds and completion at 341.248,
after the required 338-second absence endpoint. The roughly three seconds beyond
ideal endpoints reflect conservative calendar/clock-cell observation coverage,
not a byte/message publication batch. One sixteen-second source symbol must be
fully scored absent; six physical seconds of silence cannot complete it.

The mapped path remains FP64 at all public arithmetic choices. Focused tests
also exercise FHSS, fractional timing, frequency/clock endpoints, wrong keys,
noise, cancellation, original-template parity, source-quarter evidence and an
eligible DSSS100 differential branch under phase diffusion. They do not establish
full-bank negligible-loss sensitivity. Headroom outside the measured normal-paced
scope, broader long-prior banks and other multipliers require separately identified
execution evidence; nominal duty alone must not be reported as a speedup.

Scheduling-specific limits are distinct:

| Restriction | Nature and current handling |
| --- | --- |
| Frequency-axis CZT/FFT engine is not used for split source time | Implementation gap in its contiguous-observation interface; supported mapped complex is retained instead |
| Full-span coherent frequency spacing follows physical aperture | Mathematical requirement of this coherent statistic; burst-local combination would be a changed detector and remains unimplemented |
| 4,097 frequencies / 8,194 joint pairs / 65 clock rows | Finite implementation/resource bounds; unsupported coverage is explicit, never silently coarsened |
| At most one adjacent source-symbol carry | Bounded implementation; a larger clock-cell displacement explicitly exhausts coverage |
| Ten-million-position metadata enumeration bound | Implementation/work bound; failure does not discard required cells |
| Four-quarter and differential eligibility | Ordinary statistic rules, not Sched_Hop prohibitions; the eligible DSSS100 phase-diffusion regression retains both |
| Singular or chip-unaligned quarter certificate | Uses the conservative original raw-reference quarter fallback; sensitivity outside the certified geometry is unqualified |
| Amplitude needs 64 whole retained FFT observations | Eligibility of that particular statistic; it is no longer imposed on complex reception |
| Mapped Default/FP32 minimum retain FP64 moments/products | Partial precision coverage; native FP32 curve interpolation was slower in a complete pair and remains diagnostic-only; reduced-precision contraction remains outstanding |
| Complex cannot omit raw capture | Missing complete capture certificate; internal null computation skipping is separate |

### Split-symbol efficiency follow-up (2026-10-13)

Default and FP32 minimum now share physical carrier prefix moments within bounded
512-sample input pieces. Finite-table affine pulse spans contract those moments
into the original real I/Q correlation vector and full 2×2 template Gram. Raw
input energy, sample degrees, chip projection, quarter/differential partitions,
thresholds and trial charges remain unchanged. Candidate-specific ramp, limiter,
pulse-knot, chip, symbol, window and carry boundaries retain direct fallbacks.
The central candidate's flatness admits this algebra shortcut; the stronger
whole-cell proof still governs admission, null skipping and absence.

An eight-row source-chip curve cache shares independently generated 0/1 values
under the exact resolved physical epoch, ordinal and local source chip. Matched
FHSS also includes sample-in-second. The immutable parent binds keys, waveform
geometry, DSSS construction and calendar. Reader leases pin rows; exhaustion
uses original construction. Limiting and physical gates occur after lookup.
Both intervals touching a closed support-change knot, and shifted final partial
chip support, retain original evaluation. This is numerical interpolation/reuse,
not guaranteed bitwise identity with original FP64 evaluation order. No root-key
bytes or communication addresses change. Optional storage is fully charged and
reclaimable; required banks and deferred detector state take priority.

On the complete identical-PCM fixture above, the preceding optimized mapped
receiver used 906.683 CPU / 115.808 wall seconds and 47,763,702 DSP bytes. The
new source-curve path used 417.138–474.716 CPU / 61.193–64.657 wall seconds in
separate local observations, with approximately 60.29 MB DSP peak. This is an
observed 1.91–2.17× CPU improvement and about 12.53 MB additional DSP storage,
not a statistical confidence interval or a universal speedup. A conservative
whole-cell prototype improved CPU only 2.3% for +9 MB and was superseded. The
planner applies a provisional 2× contraction allowance only to the matching
measured geometry; broader geometries retain uncalibrated engineering costs.

A final driver linked against the exact frozen `before-moments` headers/libraries
completed in 923.545 CPU / 118.884 wall seconds and 47,752,902 DSP bytes. The
preflight-predecessor candidate used 520.099 CPU / 71.986 wall seconds and 60,289,862 DSP bytes on the
same PCM: 1.776× CPU, 1.652× wall, +12,536,960 bytes DSP (26.3%). A separate repeat
against the earlier worker-reuse binary measured 1,209.052 / 153.493 versus
520.099 / 71.986, or 2.325× CPU. These observations show substantial run variation;
they are not a confidence interval. Unpaced publication/end capture times remained
180.394667 / 341.248 seconds. No new frontend or preparation cost is excluded.

Native FP32 curve storage/interpolation used 498.686 CPU / 67.443 wall seconds
versus the paired FP64 curve's 474.716 / 64.657, saving 1.78 MB DSP but costing
5.1% more CPU. It is retained as an internal experiment, not selected by either
Default or FP32 minimum. Generated code includes native packed float arithmetic;
the result is not evidence that every FP32 strategy is unhelpful. FP64 force
bypasses approximate interval/curve reuse and retains the original direct
reference. No automatic INT8 path or GPU execution is added.

The separate sustained FIFO fixture independently delivered the full capture at
4× accelerated physical time. It processed all 87 keyed groups, retained complex
decoding, crossed 11 subsequent active-hop starts with previous work queued,
and drained completely. Peak backlog was 7.083 physical seconds under a declared
16-second cap; provider delivery lateness was below 44 ms under its unchanged
750 ms limit. CPU was 430.103 seconds (12.359 per source second, 1.236 per
physical second), wall 87.254 seconds, DSP peak 60,376,178 bytes and process RSS
72,308 KiB. Pending publication occurred at capture time 180.48 seconds and
completion at 341.504, after the unchanged 338-second absence endpoint. This is
accelerated mocked transport/timestamps with actual Live queues and receiver
execution, not physical audio or long-duration clock qualification. Processing
time is separate from captured carrier phase. All input remains retained unless
a complete omission certificate exists.

The preflight-predecessor sustained confirmation used the optimistic processing frontier
to certify 42 later active starts with prior work pending. All 87 group ledgers
closed. Peak backlog was 9.813 seconds, still below 16 seconds; final drain took
88.556 wall seconds for 348 physical seconds at 4× delivery. CPU was 516.955
seconds (14.855 per source second, 1.486 per physical second), DSP peak 60,424,390
bytes and RSS 67,608 KiB. Provider delivery lateness stayed below 104 ms. Pending
publication at capture frontier 181.418667 and completion at 346.112 reflect queued
processing delay; accepted output still appears on the next poll. These are
mocked transport/timestamps, not hardware measurements.

After the declared-oscillator preflight correction, the rebuilt candidate passed
the identical complete replay in 457.735 CPU / 65.709 wall seconds, with the same
60,289,862-byte DSP peak and unchanged publication/end capture coordinates.
Against the exact frozen run above this is an observed 2.018× CPU / 1.809× wall
improvement; the preceding 1.776× observation and host variation preclude a
confidence interval. All 348 declared-grid preflights summed to 0.0321 wall
seconds (maximum 0.195 ms), included in the total. This bounds their measured
wall contribution only in this five-pair geometry, not larger oscillator banks.

The rebuilt-source independent 4× capture also drained all 87 groups, certified
21 later active starts with prior work pending, and peaked at 7.424 seconds of
backlog below the 16-second cap. CPU was 462.850 seconds (13.300 per source second,
1.330 per physical second), wall 87.786 seconds and DSP peak 60,378,022 bytes.
Queued PCM peaked at 44,544 samples / 178,176 bytes. Pending publication at
181.674667 and completion at 343.722667 reflect queued processing, while the
fully observed absence endpoint remains 338. These are bounded mocked-provider
runs, not indefinite sustainable-throughput or physical-clock qualification.

The workspace regressions also cover an exact minimal direct-bank budget and a
reduced loan between live storage and deferred quarter reservations. Optional
counters/caches must fall back or be evicted before they displace those promises.
Bounded identical-PCM tests include fractional timing, frequency/clock endpoints,
FHSS, phase diffusion and noise, with unchanged 1e-7 relative statistic gates,
bit decisions, publication and completion. They do not measure full-bank 90%/99%
sensitivity, BER or cumulative loss below 0.1 dB. The conditional AWGN evidence
below predates these interpolation changes and does not qualify them.

Exact manifests, timing logs, arithmetic experiment and retained failures live in
`.agent-work/artifacts/sched-practical-20261013/EFFICIENCY-FOLLOWUP.md`. Dense/tiled
correlation, shared rolling-bank moment caches, reduced-precision contraction,
broader timing/high-DSSS/multiple-key performance, hardware, ARM and long-duration
qualification remain outstanding. The continuous reference's different waveform,
energy and bank prevent presenting its timing as an attainable scheduled speedup.
This manual checkpoint does not restore universal continuous-receiver parity.
Those preceding work traces provide counts with fine timers disabled. The
later [shared-execution diagnosis](#shared-execution-measured-diagnosis-2026-10-13)
adds bounded whole-Live statistical CPU attribution and a separate coarse
frontend bound. Neither turns the earlier single-bank fraction into a universal
whole-Live Amdahl bound.

The conditional exact-template AWGN guard instrument used 40,000 paired draws
at each of three raw observation dimensions (512, 8,192 and 196,608), rank 128
and threshold 30. Decisions agreed over the sampled SNR grid; observed additional
required signal-energy/noise was 0 dB **in that model**. Monte Carlo 95% halfwidths
on absolute 90%/99% crossings were 0.017–0.045 dB. Zero discordance yields a
pointwise one-sided 95% event-probability upper bound of 7.49e-5 per 40,000 draws;
the same draws across SNR points are not additional independent trials. This is
not PCM, BER, full-bank sensitivity or a measured rare false-acceptance rate.
Cumulative loss below 0.1 dB against the original raw receiver remains unqualified.

The current packed complex path retains raw input through null intervals; it has
no complete capture-omission certificate. Its planner uses a full-input CPU and
recording allowance, not a nominal `G/M` discount. Timing-position fields are
engineering allowances for a representative keyed epoch and complete cohort,
not executed counters or measured throughput. Full-bank probability and GPU time
remain unavailable. This is independent of successful signal decoding.

Recording may be skipped only for an interval proved null for every admitted
key, epoch, timing cell and clock hypothesis, including the future rolling bank
that can consume it. Otherwise the original PCM remains required. A successfully
skipped interval advances physical coordinates without producing fits, symbols
or absence. Cache exhaustion recomputes eligible statistics within the workspace;
it does not remove candidates. Burst operation does not justify an unbounded
recording queue or duplicated shifted-PCM matrices.

The live future-admission certificate conservatively unions current and possible
rolling receivers. Bounds for the same key and immutable schedule are merged at
one physical reference without sharing private templates. Certificate storage is
bounded before allocation and charged to its DSP reservation; exhausted or
unavailable certificates retain capture. A broader future proof can require all
samples even when a shorter current-cohort proof identifies some usable nulls.
Original affine origins and stream age survive new epoch admissions.

The future timestamp certificate checks both endpoints of each callback using
its actual mapped seconds per frame and the original UTC envelope. Containment
at both ends proves containment throughout the affine callback; a valid start
timestamp alone is insufficient. This adds no extra per-sample timing allowance.
An invalid or nonfinite mapping revokes the certificate instead of granting a
null skip. Current-cohort proofs also stop before the next UTC admission fence.

Completion still requires fully scored failed source-symbol durations covering
the existing six-second minimum. Packed reception scores actual subsequent
calendar slots; waiting cover and null gaps are not symbol alternatives and add
no absence duration. Thus physical completion may require many wall seconds
beyond the final payload even for short source symbols. Null wall time does not count; this is not a
sum of only retained flat FFT observation durations. Scheduled nulls, EOF, flushes,
cancellation and decoding success do not provide that evidence. Every newly
accepted bit reaches the next progress poll in the same pending reception.

For a represented arrival/phase/clock cell, scoring waits for its latest admitted
physical endpoint. At a source-group boundary, that conservative endpoint can
fall in the next active group, beyond the nominal symbol end and intervening
null interval. Publication and completion cannot precede those observations;
next-poll publication applies once the fully admitted endpoint has been observed
and the bit accepted. Extra endpoint slack is not rounded into failed-symbol
duration, and a fixture ending at only the nominal endpoint is incomplete.

Clock-cell uncertainty continues growing from the original timing reference;
new bits and rolling epochs do not restart its budget. The current grid provides
finite support, not indefinite clock tracking. On an admitted amplitude stream, a failed
standalone full-symbol test cannot classify absence if fewer than 64 whole FFT
observations remain or any quarter is unobserved. It instead reports
`scheduled amplitude timing coverage exhausted` before changing pending,
missing-bit or completion state. Strong evidence retains the existing admission
statistic. Adaptive reconstruction of an aging clock grid remains outstanding.

### Shared execution: measured diagnosis (2026-10-13)

The preserved runnable baseline has GUI SHA256 `a4303d6c…` and core
`8bfa035c…`, with source preserved at `fe3e2c28…`. An instrumented copy of that
receiver processed the same 348-second, 6,000 Hz private capture used above:
PCM hash `12980061178210671347`, M10/G1, DSSS/FHSS Off, 16-source-second symbols,
five frequency/rate pairs, qualified mocked arrival timing, one key, at most
230 lanes per epoch and 54 resident banks. This is a baseline diagnosis, not a
measurement of the subsequently integrated shared candidate. The compact
[data summary](validation-data/robust/shared-scheduled-20261013/component-summary.json)
retains exact identities, counts, raw-result hashes and scope.

| Complete baseline diagnostic | Receiver CPU / wall | Scope |
| --- | ---: | --- |
| Profiler compiled, disabled | 473.881 / 69.473 s | One complete reference run |
| Native-thread sampler, 10 ms CPU period | 486.050 / 66.988 s | One complete statistical attribution run |
| Coarse exclusive thread-CPU scopes | 517.102 / 73.219 s | Coarse category timing; overhead included |

The sampler delivered 47,998 tagged ticks, 264 outside ticks and 181 unassigned
overruns. Among tagged ticks, search residual was 71.37%, template construction
9.79%, template/cache access 6.76%, Gram work 7.45% and guards 3.02%. Residual
includes work not tagged more narrowly; it is not a measured correlation-only
fraction. Periodic/serial sampling bias and unassigned overruns prevent an IID
confidence interval. The sampled outer contexts covered 481.230 CPU seconds,
leaving 4.820 seconds against independently measured receiver process CPU.
Stage fractions are not exact seconds or portable model coefficients. The
sampled-versus-disabled CPU difference is 2.57% in single runs with host
variation, not a precise instrumentation-overhead estimate or confidence interval.

The coarse run measured 1.35185 CPU seconds for mixing plus physical moments,
0.2614% of total CPU. Eliminating both entirely would improve this one F5,
DSSS-Off complete capture by at most 1.00262× before replacement/cache overhead.
This quantitatively limits frontend-only gains here; it does not bound other
frequency banks, DSSS factors or template/search optimizations. The fine nested
profiler failed to drain after about 290 wall seconds and 2,397.65 program CPU
seconds, with 2,012,998,100 clock reads. Its partial fractions are excluded from
calibration. A balanced profiler context is not a completed reception.

The current source adds several separately bounded reuse mechanisms:

- Physical prefix/moment rows are push-scoped and match the original oscillator
  recurrence's exact real/imaginary seed bits, absolute PCM range/count, sample
  rate, exact carrier, phase convention and precision. Canonical carrier-origin
  rotation is not used: algebraically equivalent rotation can cross an existing
  rank/whitening cutoff after rounding. Different seeds miss and use original
  local preparation; no all-rolling-origin sharing discount is justified.
- The L2 source-chip curve pool stores the preceding approximation, keyed by
  controller key namespace, exact purpose roots, waveform/table revisions,
  geometry, resolved physical epoch/ordinal/local chip and applicable FHSS phase.
  Leases preserve immutable rows. Generation changes retire the pool; required
  admissions/reservations reclaim optional storage or use local/direct fallback.
  This does not make interpolated curves an original-template reference.
- DSSS permutation snapshots copy the existing complete permutation and seed
  into private symbol state under exact key namespace/root/domain, resolved
  epoch/ordinal, complete-chip count and factor. Rotation streams and private
  candidate state remain separate. Busy, oversized or exhausted arenas retain
  the original generator. No finite noise buffer or cryptographic address is
  replayed, and this exact reuse remains independent of arithmetic choice.
- Common source-family execution is enabled in the local candidate. Bounded jobs
  contain at most sixteen lanes and up to eight distinct canonical phases, with
  exact origin/rate, processed interval, source coordinates, private address and
  current-or-carry ownership checked before sharing. Every original alternative
  still executes when that proof fails; a tighter bank is never expanded to fill
  a job. The public timing/phase/frequency domain and detector guards are retained.
- Same-frequency siblings reuse a pure two-candidate FP64 Fit contribution within
  one proven common piece or raw sample. Each lane still updates its own complete
  fit, chip/quarter/differential/outer state and original trial/publication path.
  Distinct frequencies retain their own physical projections and contractions.
  This is exact contribution reuse, separate from interpolated source curves.
- An optional per-job private-identity proof cache is tagged by the complete
  target symbol index under the immutable family members, anchors, key, geometry
  and omission policy. Current and adjacent carry indices have separate entries;
  parity alone cannot hit. Positive and negative address equality may be cached.
  Flatness, gain, physical endpoints, source coordinates, noise and energies are
  checked anew. Eviction, retirement or unavailable workspace removes reuse only.
  The preceding frequency-only results below do not qualify every phase family.

The common-shape experiment has complete **one-bank**, identical-PCM ABBA
measurements with four workers. The receiver timer includes construction,
all pushes/polls, finish and destruction; common fixture generation is outside.
The fixture's retained statistic, event and physical-END assertions passed.

| One-bank geometry | Original CPU range | Common-shape CPU range | Peak accounted DSP, original → common |
| --- | ---: | ---: | ---: |
| Raw F9 | 6.468–6.651 s | 1.994–2.170 s | 7,272,411 → 7,296,923 B |
| Moment F9 | 9.864–10.311 s | 2.573–2.706 s | 7,480,858 → 7,505,370 B |
| DSSS10 raw F9 | 42.369–46.294 s | 11.840–13.350 s | 9,837,623 → 9,891,943 B |

These ranges describe the observed runs, not confidence intervals or full-Live
speedups. The sparse unique-clock fixture exercised zero shared families;
2.675–2.805 versus 2.616–2.631 CPU seconds does not establish an optimization
gain. A complete broad-prior fixture refused its full coverage at 64 MiB; that
failure remains, and workspace/hypotheses were not relaxed. The first raw
reference also lacked the admitted physical-END noise interval and failed before
comparison. The corrected fixture appends independent observed Gaussian samples
through the original full timing/clock/phase fence; EOF cannot supply absence.

| Substantial strategy | Current disposition and limits |
| --- | --- |
| Frontend-only elimination in measured F5 capture | Quantitatively bounded: ideal total gain ≤1.00262×; exact-seed cache cost/hits still need final complete comparison |
| Exact common-source families, bounded adjacent phases and same-frequency Fit reuse | Implemented and enabled in the local candidate; one-bank ABBA evidence above, preliminary complete Live result below. Complete final paired/sustained results follow; broad/high-DSSS generality remains unqualified |
| Shared L2 curves, exact DSSS snapshots and per-job private-identity proof | Implemented with bounded/reclaimable storage; complete combined attribution is recorded below; high-DSSS full reception remains unfinished |
| Native FP32 curve storage/interpolation | Prototyped and rejected for automatic selection in the preceding complete pair: 5.1% more CPU, 1.78 MB less DSP storage |
| Native FP32 signal contractions / packed Gram | Implemented as separate default-off diagnostics. Signal prototype failed the retained 1e-7 whole-fixture statistic comparison despite correct clean bits; Gram has bounded numerical/fallback tests and complete fixed-bank costs below. Full-bank sensitivity remains outstanding |
| Integer operands / frequency-moment fusion | No production or full-receiver gain established; current ≤6-sample spans miss the existing temporal AVX2 INT8 loop; Q15 prefix design and complete-cost integer/fusion execution remain outstanding |
| Whole physical-span silence FFT | Unfavorable analytical operation bound for the preceding sparse pieces; the small tiled native-float/float64 prototype and original guard overlay were measured; integrated guard convolutions remain outstanding |
| Component-based planner calibration | Design/helper proposal exists but is uncompiled/unintegrated; no speculative coefficient reduction is applied |

An intermediate complete Live run with common execution and phase cap eight
processed the identical PCM in 381.809 CPU / 56.381 receiver wall seconds versus
473.881 / 69.473 for the compiled-profiler-disabled baseline. DSP peak was
62,790,662 bytes and process RSS 72,676 KiB; pending/end input coordinates remained
180.394667 / 341.248 seconds. This is a single unrepeated observation before the
latest same-frequency/proof-cache changes, not the final combined gain or its
uncertainty. It proves neither independent live pacing nor full-bank sensitivity.
The cap-two phase fixture had an approximately 8% CPU regression with many rejected
families; subsequent cap-four/eight and F5 fixtures passed their retained assertions
and improved CPU. These geometry-specific outcomes must not be collapsed into a
universal phase-sharing speedup or rejection.

The demanding continuous DSSS100 control completed 40.02 audio seconds in
148.258 CPU seconds (3.7046 CPU seconds/audio second), with 435,450,849 accounted
DSP bytes. Its preceding scheduled M10/G4 counterpart failed its 180-second drain
bound after processing only 12.032 of 89.62 supplied audio seconds, with 1,595.19
program CPU seconds and no admitted bit. This partial failure is preserved;
nominal duty does not establish practical feasibility, and the changed continuous
and scheduled waveforms are not an identical-PCM sensitivity comparison. The later M100/G8 DSSS100 attempt also failed its 180-second drain bound after
124.075 of 484.02 physical seconds and 1,569.88 program CPU seconds, with no bit.
The latest hot/cold repeat also stops at 124.0747/484.02 physical seconds after
1,564.37 program CPU/180.127 wall seconds, with no bit. Its partial ledgers show
87,360 hypotheses, 1,005,860,449 template constructions and 632,966,269 shared raw
shape evaluations, with zero affine contributions or curve hits. These are
execution counts, not component CPU fractions. Fine-chip-15 raw construction
remains the gap. No partial prefix is a complete-reception speedup; the demanding
headroom delivery criterion remains unmet.

All scheduled full-bank sensitivity and RX-probability claims remain unqualified.
These CPU results do not establish cumulative loss below 0.1 dB, 90%/99%
detection crossings, BER or rare wrong-key/false-admission rates. Threshold-region
PCM, multiple-key/epoch/high-DSSS, interference, phase diffusion, long schedules,
real audio/radio timing, ARM and broader qualification remain outstanding. Final
candidate total CPU, workspace/RSS, publication latency and sustained queue/drain
results must be stamped separately against its exact source/build. Raw capture
remains retained without a complete omission certificate; null computations,
EOF, recovery and draining never create observed absence.

### Final shared hot/cold measurements and remaining gap

The final memory-corrected GUI is `47b26398…`, core `e1a0253d…`, with exact
source/configuration in the local `compact-frozen/identity.json`. It retains the
same detector arithmetic and shared algorithms as the preceding hot/cold build.
A complete identical-PCM **B A A B** comparison against frozen optimized core
`8bfa035c…` includes all rolling banks, setup, guards, publication and drain:

| Path | CPU seconds | Receiver wall seconds | Peak accounted DSP |
| --- | ---: | ---: | ---: |
| Frozen optimized | 474.791, 507.007 | 66.234, 70.228 | 60,289,862 B |
| Final shared | 389.681, 390.137 | 55.600, 56.744 | 63,348,934 B |

The ratio of CPU means is **1.259009×**, with paired ratios 1.218410–1.299560;
the wall ratio is **1.214669×**, with pairs 1.191248–1.237619. Two runs per path
on an uncontrolled host do not establish a stable confidence interval. RSS was
71,988–72,240 KiB versus 72,372–73,000 KiB. The fixture remains 348 physical /
34.8 source seconds, original 6,000 Hz PCM, M10/G1, DSSS/FHSS Off, one key and
qualified mocked capture. Pending/end coordinates, all 105 admissions,
56 retirements and the 54-bank peak remain matched. The earlier hot/cold ABBA
result (1.260797× CPU) is retained separately; these gains must not be multiplied.

The final independently paced 4× run fully released, delivered, processed and
drained all 87 groups. Decoder backlog peaked at 3.7547 physical seconds, below
the existing 16-second cap, while work crossed subsequent active hops. Cost was
382.497 CPU / 87.256 wall seconds: 10.9913 CPU/source second and 1.09913
CPU/physical second. Peak accounted DSP was 63,412,806 B and RSS 72,956 KiB.
First pending appeared at 180.48 input / 45.1221 wall seconds; physical completion
at 341.504 input / 85.3841 wall seconds. Provider release/delivery lateness was
below 0.75 s. This is mocked software capture, not hardware qualification.
Its continuous counterpart is cheap: it **does not** satisfy the requested proof
that scheduling rescues a genuinely demanding continuous link. The demanding
DSSS100 failures above remain unfinished implementation evidence.

The memory correction places scheduled-only telemetry and physical views in
scheduled state, preserving the full public work snapshot. The original
fractional-clock four-hour ordinary fixture now uses 65,504 B under its unchanged
65,536 B limit, with all 75 hypotheses. Integer-clock use is 57,104 B; the 4,096
sample noisy run has no allocation growth. Scheduled capacity remains charged;
this complete 54-bank fixture adds only 864 B over the measured hot/cold layout.

A fixed-epoch/phase cohort on identical 742,208-sample PCM measured the following
complete execution, including its original observation-END fence:

| Total arrival width | Retained hypotheses | Shared CPU mean |
| --- | ---: | ---: |
| 6 s | 882 | 8.6735 s |
| 1 s | 162 | 1.5668 s |
| 100 ms | 27 | 0.26845 s |
| 10 ms | 18 | 0.18222 s |
| 1 ms | 18 | 0.18151 s |

All original statistic/event/absence assertions passed. The plateau is from
retained fractional/clock cells and fixed costs, rather than a hidden broad FFT.
This is not every rolling/key/phase bank or hardware timing qualification.

Complete current sampled attribution records 43,670 tagged 10 ms thread-CPU
ticks, 151 unassigned overruns and zero timer setup/foreign/clock/nesting failures.
Residual is 74.17%, template construction/cache 11.63%, Gram 6.76%, guards 3.52%
and calendar 3.33%. Residual is not correlation-only. Instrumentation, periodic
bias and host variance prevent treating these as exact CPU seconds or portable
model coefficients. Native FP32 signal/Gram and exact candidate-axis SIMD remain
internal default-off diagnostics: complete fixed-bank comparisons did not show
an automatic-selection benefit; FP32 signal also failed strict statistic parity.

The bounded paired PCM pilot executed 22 of 24 case/seed/energy pairs, with zero
strict original-FP64 statistic/event differences. Both wrong-key pairs refused
complete workspace and are failures, not wrong-key rejection evidence. Two
seeds per group cannot establish negligible sensitivity loss, BER, 90%/99%
probability or additional C/N0. A complete-bank interpolation/completion-policy
sensitivity qualification is still required. The exact frozen-baseline pilot
was separately bounded and retained as partial evidence.

Detailed build identities, raw-result hashes, arithmetic costs, opportunity
bounds, preserved failures and strategy dispositions are in the maintained
[data summary](validation-data/robust/shared-scheduled-20261013/component-summary.json)
and local `.agent-work/artifacts/sched-shared-20261013/MANUAL-CHECKPOINT.md`.
Integrated large-frequency fusion, candidate-axis integer arithmetic, adaptive
transform batches and guard convolutions remain explicit substantial work;
small kernel prototypes do not complete those strategies. This rebuilt GUI is
a **partial manual checkpoint**, not optimization completion or qualification.

## Practical diagnostics

Use `--verbose` with the full GUI, optionally adding `--verbose-file NEW-PATH`.
The asynchronous JSONL trace includes GUI inspection/advice completion or
failure, receiver profile selection and rejection, exact packed geometry,
workspace admission/retirement, accepted-bit publication and physical-end
coordinates. `receiver_selection.timing_positions_allowance` is a modeled work
allowance; it is not an executed-correlation count. `packed_complex_constructed`
records actual FFT geometry, carrier/clock counts, resolved arithmetic and
workspace use. `split_complex_constructed` records the physical aperture,
required carrier/clock spacing, coverage-qualified flags, refined lane count,
phase/timing spacing, mapped-direct backend and actual FP64 execution. Progress/end events record accepted counts and failed source
support without logging bits, PCM or key material.

For a bounded physical-loopback run, add `--debug --debug-bits 0
--debug-timeout 300` and a synthetic named key. This drives the ordinary GUI
through hardware duplex audio, emits JSON diagnostics to stderr and closes on
error, completion or deadline; it does not enable Simulation. A large multiplier
may outlast this bounded watchdog and must not be called a completed reception.
Keep stderr with the trace: complete trace evidence requires the successful
footer and external finalization record described in [clock timing](clock-sync.md).
No default, example or preparation check substitutes for real audio/long-duration
qualification.

## Compact storage and workspace accounting

Schedule-only correlator state is allocated only for noncontinuous scheduled
amplitude reception. Its object, source map, optional joint calendar and vector
capacities remain charged to both constructor preflight and working workspace.
Ordinary receivers retain only an owning pointer; this does not remove required
hypotheses or substitute a smaller statistical bank.

The legacy diagnostic suppression cache is allocated during construction only
when that tail is actually used. It is charged to workspace and cleansed on
destruction. This cache occupies 1,024 bytes on the measured local ABI; the
protected/private `VirtualCover` engine and inline payload/cancellation cache
are unchanged. No allocation is deferred into playback.

The same ordinary long-symbol fixture gives the following `working_bytes()`
counts. These are accounted object/storage measurements, not process RSS.

| Ordinary fixture | Preceding baseline | Before storage fix | After storage fix |
| --- | --- | --- | --- |
| Four-hour transmitter | 16,363 B | 16,555 B | 15,531 B |
| Compact fractional four-hour receiver | 65,280 B | 65,664 B | 65,424 B |

The unchanged 16 KiB transmitter and 64 KiB compact-receiver assertions pass
again. The receiver still accounts for 144 bytes more than its preceding
baseline. The 8 KiB `PatternCode` assertion also remains intact; its measured
count is 5,448 bytes versus 5,424 in the baseline. Exact fixture commands and
before/after readouts are in `memory-probe-results.json`, `memory-after-result.json`
and `memory-probe.cpp` under `.agent-work/artifacts/sched-hop-20261012/`.
This storage correction changes no waveform, cryptographic address, arithmetic,
search geometry or detection threshold.

## Bounded execution evidence

The following preceding amplitude-path measurements remain diagnostic references.
They do not measure the newly added mapped complex path or its finer physical
frequency bank. New complete-bank CPU/sensitivity evidence must identify the
actual selected backend before comparison.

The final local Live probes exercise the complete rolling bank with two
synthetic keys (one wrong), one encrypted raw bit, an eight-second source symbol,
2 kHz original PCM, a 500 Hz carrier and seeded additive noise. The tight cases
use 64 Hz bandwidth, 31.5 ms chips and 254 chips with a partial final chip. Their
mocked capture has a qualified 10 microsecond timestamp radius; GPS accuracy,
region and Audio error are each 1 ms. Qualification was asserted rather than
silently falling back. The broad case uses 16 Hz bandwidth, 125 ms chips and the
full six-second arrival window. All use Default arithmetic and DSSS Off.

Input is backpressured in 512-sample blocks. The three final captures ran
serially with no overlapping task builds or tests; unrelated system activity
was not controlled. These are complete functional/CPU captures, not hardware
real-time ceiling measurements. Wall time includes intentional waits; peak RSS
includes the buffered fixture. No component-level profile or statistical timing
confidence interval is established by these single final runs.

| Final rolling case | Input audio | Receiver CPU / wall | CPU per audio second | Peak DSP workspace / process RSS | First bit / completion, input time |
| --- | --- | --- | --- | --- | --- |
| M10/G1, qualified tight, FHSS Off | 168.716 s | 32.309 / 32.507 s | 0.19150 s | 27,833,465 B / 27,688 KiB | 84.480 / 164.096 s |
| M10/G1, qualified tight, Fake FHSS ramps | 168.716 s | 31.145 / 31.335 s | 0.18460 s | 27,833,465 B / 27,932 KiB | 84.480 / 164.096 s |
| M10/G5, broad, FHSS Off | 41.528 s | 108.484 / 107.512 s | 2.61231 s | 83,286,559 B / 65,424 KiB | 19.200 / 35.584 s |

Every received row was inspected: one correct bit became pending then complete,
without extra received bits or streams, or EOF completion. The tight runs peaked
at 57 banks with 107 admissions and 54 retirements; the broad run peaked at 47
banks with 59 admissions and 17 retirements. Tight runs used a 256 MiB workspace
bound. The broad run required a configured 1 GiB bound; the 512 MiB version was
refused before media processing. Its observed 83.3 MB peak is smaller than that
conservative full-bank preflight reservation, not proof that the refused
configuration retained complete coverage.

The tight FHSS-Off capture recorded 140.812 seconds and certified 27.904 seconds
null; the FHSS capture recorded 140.044 and skipped 28.672 seconds. The complete
broad bank retained all 41.528 seconds. Nominal transmit duty therefore does not
predict recording duty for the admitted/future candidate union. This broad
execution is **not real-time**: it used 2.59 wall and 2.61 CPU seconds per audio
second, despite correctness and a successful backpressured capture. Aggregate
CPU alone would not prove a multicore deadline failure.

Nominal tight payload and scored-absence endpoints were 80.252 and 160.652
seconds. Latest-cell coverage delayed publication/completion by 4.228 and
3.444 seconds respectively. The broad endpoints were 17.000 and 33.400 seconds,
with added delays of 2.200 and 2.184 seconds. Null time still supplies no absence
evidence. Replay elapsed time to first pending/completed row was 11.727/31.345
seconds for tight FHSS Off, 11.056/30.179 for FHSS, and 25.502/83.860 for broad.
Those accelerated replay times are not physical device acquisition latency.

The final tight FHSS-Off PCM hash is `15869057065016913182`, identical to the
preceding captures at the same input rate and public settings. Correcting the
timing grid adds required intermediate positions: final CPU is 1.78 times the
pre-final 18.141-second run, with publication endpoints unchanged. The preceding
probe-4 grid-corrected candidate used 31.029 seconds, versus 32.309 in the final
storage-only revision. These single runs establish no efficiency gain or
statistically significant storage-change cost; neither comparison isolates every
individual correction. Two earlier Default runs used 21.318–24.354 CPU
seconds, and one earlier paired FP64 run used 21.004 seconds. There is no final
paired arithmetic timing sweep, and no native FP32 whole-Live benefit or
statistically significant arithmetic difference established here. The FHSS
waveform differs (PCM hash `10039107031633985083`); its near-equal CPU is a
separate functional regression result, not an identical-PCM speedup comparison.

The preceding M100/G1 prototype completed a two-key rolling capture of
1,679.116 seconds using 85.142 CPU / 89.583 wall seconds, 24,687,208 DSP bytes and
50,340 KiB RSS. Its first bit/completion appeared at 823.296/1,632.512 input
seconds, 36.644/33.460 seconds after nominal endpoints. This result predates the
final timing grid, complete phase extent and capture safety fixes; it does not
qualify the current M100 implementation or establish inverse-duty CPU scaling.
An earlier broad M10/G1, 64 Hz fixture was explicitly refused at 256 MiB.

Preserved diagnostic failure evidence explains why the finer grid is necessary.
On the same broad M10/G5 PCM (hash `17480372299629854541`), the preceding
single-bank implementation scored 16.442 below threshold 33.913 and admitted no
payload. The refined prototype scored 72.154 above 35.308 and completed observed
absence. Its 579 hypotheses/128-sample FFTs used 1.798 CPU seconds and 1,082,512
bytes, versus 291 hypotheses/64-sample FFTs, 1.813 seconds and 658,776 bytes.
The threshold rose appropriately with trial count. That strong-signal
single-bank restoration did not predict the final rolling bank's 108.484 CPU
seconds and is not a sensitivity curve or rolling-bank speedup.

Separate exact-bank synthetic Interleave DSSS fixtures on the preceding probe-4
receiver use M10/G2, known timing, frequency and clock, two-kHz input coordinates
and certified null skips. Each
receives one correct bit and completes through fully scored absence, without
whole-symbol PCM storage. The reported CPU includes fixture generation and
receiver execution; memory was not separately profiled.

| DSSS factor | Source-symbol duration | First bit / completion, physical input time | Fixture CPU |
| --- | --- | --- | --- |
| 10 | 25.6 s | 123.6785 / 253.2895 s | 0.044330 s |
| 100 | 256 s | 1,275.657 / 2,556.060 s | 0.454395 s |
| 1,000 | 2,560 s | 12,796.0635 / 25,597.2685 s | 4.665879 s |

These conditional fixtures do not include full acquisition, rolling epochs,
wrong-key banks, hardware timestamps or threshold-region sensitivity. They do
not establish that DSSS100/1000 rolling reception is feasible at those costs.

Raw logs, exact commands and identities are under
`.agent-work/artifacts/sched-hop-20261012/`. Final `probe-5/` contains
`live-tight.log`, `live-fhss.log`, `live-broad.log`, `execution-results.json`,
`identity.json` and `digest-comparison.json`. Its library SHA256 is
`79ef2c1202c284f1cdfde0eee5a362abe2de0fa724136dbeb6c901ce2a8cbb31`;
all frozen source/instrument inputs match. Four encrypted transmit digest rows
for DSSS Off/10/100/1000 remain byte-identical to the frozen preceding baseline.
The rebuilt GUI SHA256 is
`9fd6d4f78f110a9fbdc018cf921053b3a8f01421b01a9770a014e7f23a6ecc48`.

The preceding `probe-4/` library
`dad507e2919b28370a5d2009909ec01b7eb591669ccf022eaacaf6661fd46fa3`
and its captures remain historical references. Its `dsss10.log`, plus parent
`dsss100.log`/`dsss1000.log`, identify the conditional DSSS results above;
those isolated receiver captures were not rerun on the storage-only revision.
The earlier `probe-2`/`probe-3` captures and
`.agent-work/artifacts/sched-hop-rx-refine-20261012/` prototype remain diagnostic
references. The probe-2 instrument source was edited after its receipt, so that
older receipt does not identify its current instrument bytes. No capture here
measures sensitivity loss or physical sound-card behavior.

## Native UTC timing and remaining qualification

The preceding candidate reserved at least two full frames for any positive
support and propagated a generic approximately 2,000 ppm clock envelope through
it. That source-derived forecast required about 327 ms total Audio error at
100× even before real device uncertainty. It was a controller limitation, not a
measured sound-card requirement. Its baseline and failed traces remain preserved.

For source support `N`, sample rate `Fs`, active group size `G`, multiplier `M`,
let `H=G*floor(2Fs/5)`, `k=ceil(N/H)` and `J=floor((M-G)/2)`. The new
key-independent upper bound is

`N + ceil(2Fs*(kM+J)/5) - kH`.

Only endpoint jitter remains; interior jitter telescopes. Portable exact
quotient/remainder arithmetic avoids oversized intermediate products. Empty,
disabled and continuous schedules remain identities. Runtime already uses the
exact keyed last valid-payload endpoint, including queued/prefetched samples,
with the left/end inverse at a group boundary. No transmitted sample changes.

Native forecasts now use the explicitly declared physical-clock envelope, with
host UTC phase and provider uncertainty handled separately. For an illustrative
0.147375 s support at 48 kHz and G1, the new maximum horizons are 5.347375,
59.347375, 599.347375 and 5999.347375 s for 10/100/1k/10k. At an assumed
100 ppm physical sample clock, their clock-only uncertainty is approximately
0.535, 5.935, 59.941 and 599.995 ms. At a genuinely disciplined 1 ppb it is
0.005, 0.059, 0.599 and 5.999 microseconds. These are **analytical bounds**, not
resolved application geometry, device measurements or full-reception results.

This makes 100× practical within reasonable allowances for eligible ordinary
clock/provider geometry, and removes the generic-cone obstacle at larger
multipliers for genuinely disciplined clocks. It does not promise that every
ordinary sound card qualifies at 1k/10k with Audio50. Selecting GPSDO does not
provide discipline. Exact-key pre-source preparation and safe null retiming are
not implemented: see the [timing invariant and capability limits](clock-sync.md#declared-sample-clock-host-utc-phase-and-sparse-schedules).

The source calendar, short-bit deferral, long-symbol splitting and receiver
statistic remain unchanged. Null time and filter drain still cannot count as
observed absence. Updated bounds can reduce excess receiver lifetime/clock-bank
reservations; they never discard an admissible endpoint. Any timing fallback is
visible and retains full arrival coverage. Preparation success alone does not
qualify an hours-long reception.

Focused regression fixtures cover the calendar/source map, TX/RX integration,
joint short-bit slots, controls and fullscreen spectrum. The preceding manual
checkpoint recorded 38/38 GUI and 62/63 regular cases; `alsa_audio_contract` retained its
clipping-assertion failure, also reproduced by the preceding-baseline control.
It is a failure, not a skipped or passing case. Exact logs and combined counts
are in `validation-summary.json` under the artifact directory above; broader
platform, SDK and hardware qualification remains deferred. The current lifetime regression additionally
requires an explicit coverage error rather than false completion as clock-cell
uncertainty grows. The preceding amplitude receiver's reported conditional
sensitivity penalty is not a universal bound.

For this timing follow-up, five mocked native audio runs use Audio50 and a
compensated 750 ms queue. Disabled/10/100 qualify under the declared 100 ppm
model; 1k/10k qualify under a genuinely disciplined 1 ppb model. These exercise
preparation and 0.5 s of native-adapter runtime, not full sparse transmission.
Accelerated 20,000-second timing trajectories retain the original constraints
and reject wrong-clock declarations and explicit steps/displacements.

Complete paired Live captures retain identical PCM, media publication and
completion coordinates. One pair each measured 15.186 to 13.975 CPU seconds
(M10 tight/G1), 54.552 to 51.392 (M10 broad/G5), and 46.699 to 45.474 (M100
tight/G1). These single pairs do not establish a speedup confidence interval.
The M100 capture contains 1,679.116 physical seconds and 17.052 source seconds;
new cost is 0.0271 CPU seconds per physical second, or 2.667 per source second.
It still constructs 1,681 rolling banks. The broad case still costs 1.238 CPU
seconds per physical second. The timing correction does not establish a new
general real-time ceiling. Every pair adds only 32 bytes to accounted workspace.
Encrypted TX digests match for DSSS Off/10/100/1000.

Exact timing replay, native mocks, paired builds and final local test receipts
are linked from `.agent-work/artifacts/sched-utc-20261012/MANUAL-SCHED-UTC.md`.
The rebuilt timing candidate passed 38/38 GUI and 63/64 local regular cases
(calibration and native-display labels deferred). The unchanged ALSA clipping
failure remains: peak 1.00944, 63 clipped samples, energy fraction 2.47619e-8
against the 1e-8 gate. New sparse native and existing bounded-repair checks
completed before that assertion. It is still a failure, not qualification.
Original provider recordings establish no independently measured hardware UTC
truth; complete Live captures use mocked capture dates and backpressure. No new
sensitivity difference is inferred from those successful functional runs.

Required remaining qualification includes longer/multi-bit rolling-bank and
multiple-key execution across broad and qualified tight arrival windows, full
acquisition for DSSS Off/10/100/1000, wrong-key and structured-cover rejection,
threshold-region sensitivity with
uncertainty, interference, clock/frequency boundaries, cancellation and physical
completion, hardware UTC/capture behavior, ARM execution and broader platform/SDK
regressions. A manual-test handoff does not replace those gates.

## Fullscreen configured searched spectrum

Click the compact Console waterfall to open its full-window view; click again or
press Escape to return. **Clear waterfall** continues to clear the retained
history separately. The compact plot keeps its environmental view, while the
expanded view crops to the configured union of receive search bands.

Frequency runs left to right and time downward. Each adjacent pair of columns
shows amplitude on the left and wrapped line-to-line phase on the right, after
subtracting nominal FFT-bin rotation. Both values belong to the same retained
native bin. At very small widths, adjacent amplitude and phase panels replace
the paired columns. Black phase denotes unavailable data; noise phase is
arbitrary. Phase is display information, not a receiver admission statistic.

The view reuses the existing 2,048-point environmental FFT and retains its 1,025
complex bins before compact pooling. It adds no second FFT or receiver search.
Cropping cannot improve the physical bin resolution: at 48 kHz, bins remain
23.4375 Hz apart. Captions disclose outward bin rounding and bands clipped by
the captured spectrum. Power-only replay, partial windows, discontinuities,
capture reopenings and new segments cannot manufacture continuous phase.

The bounded 160-row native power/phase history adds about 2.52 MiB of retained
mutable/current-snapshot payload beyond the compact history. This is a storage
calculation, not measured peak RSS; metadata and older consumer-held snapshots
add storage until released. This GUI history is outside the reserved DSP pool;
the manual candidate's total memory report must include it separately. A
configured search-band display does not certify that an unsupported receiver has
successfully retained or processed every hypothesis.


## Shared execution and fine-chip follow-up (13 October 2026)

This remains a local manual-test investigation, **not completion of the demanding
split-symbol efficiency objective**. The preceding bounded pulse moments,
source-curve reuse, approximately 2× recorded improvement and multi-cycle queueing
are retained. Queueing was already implemented; a FIFO or a favorable duty cycle
is not new shared execution. Current work adds shared public temporal Plans,
joined owner retention across no-new-bank refreshes, exact R1/flat exports,
optional typed native FP32 payloads, ordered runtime-dispatched FP64 SIMD and
exact public outer-ring replay with independent private callbacks.

The complete six-run prefix comparison (two runs each, serial, uncontrolled host)
measured mean drained CPU 265.904 s for the frozen preceding export receiver,
269.719 s for the new wide path and 270.021 s for native FP32 with packed reuse.
Mean wall times were 32.754, 31.257 and 32.408 s. **No additional CPU gain was
established by that integration.** These are the same original 243,720 PCM samples
at 6 kHz, 40.62 physical / 3.22 source seconds, 8 rolling banks, 8 admissions and
no retirements. The prefix provider is backpressured (`paced_scale=0`), so its small queue is not
a sustained independent-producer result. All input/work drained, but no whole 16-source-second symbol,
accepted bit or physical END occurred. Two uncontrolled samples per path do not
provide a stable confidence interval, sustained headroom or sensitivity evidence.

The separate complete-prefix CPU profile covers 289.608 of 290.909 process CPU
seconds with exclusive per-thread sampling. Candidate residual work is 20.86%,
common raw construction 20.57%, public Plan construction 17.95%, export 12.03%,
evaluation 8.27%, common residual 5.41%, Gram 4.28%, template construction 2.97%,
validation 2.27% and guards 1.47%. Treating each sampled class as entirely removable gives conditional
Amdahl estimates of approximately 1.264×, 1.259×, 1.219×, 1.137× and 1.090× for
the first five classes. These are prioritization estimates, not certified bounds or attainable gains;
periodic/serial sampling bias and seven unassigned overruns remain. The fractions
are specific to this complete prefix; overlapping worker wall times and execution counts were not treated as CPU shares.

The shared-owner retention fixture actually retains one owner across 31 refreshes
without retirement; genuine receiver admission still releases it before original
required sizing. Exact outer replay has a positive integration witness of 3,072
samples and 186 private flush events, with byte-identical serialized evidence and
state digest `9fffe572900a3d0fbf8e4ec41f0a4836cf7e94651a0592d4c73c3675339ef35d`.
The demanding prefix does not enter that outer-guard geometry, so the witness does
not imply a benefit there. All existing guards, trials and original callbacks remain.

Fine-chip source curves are a separate **default-off internal experiment**:
`DATAPUMP_SCHED_FINE_CURVES=1`. Revision 1 retains 257 FP64 knots and the old
256 KiB L2 ceiling. It matches the existing pulse lookup/interpolation expression
in the bounded oracle, but its complete prefix cost regressed to 961.948 CPU /
110.665 wall seconds after removing writer serialization. The earlier serialized
variant failed its 180-second drain. Neither is selected automatically.

`DATAPUMP_SCHED_FINE_CURVE_DIVISIONS=64` requests a separately keyed revision 2,
with 65 original knots, expanded exclusion around the two finite-support cells,
and up to 1 MiB of final-surplus L2 storage. Its initial copied-row implementation
still regressed: 366.498 CPU / 45.759 wall versus 279.735 / 32.588 for a nearby
frozen wide control. Its actual 124-row pool logged 13,016,098 hits and 96,677
builds; copying all 257 storage slots on these hits alone moves about 107 GB.
The maximum is a ceiling, not an unconditional grant: original required banks,
workers, deferred work and earlier optional grants retain priority. A pre-existing
coarse pool can remain at its smaller actual size. Exhaustion never removes banks.

The 65-knot bounded oracle covers 120 private rows, 61,440 exact-node/off-grid
points, DSSS 10/100/1000, partial final chips, two stream phases and two symbol
positions. Maximum source difference is 3.39629e-4 and post-limiter difference
2.47929e-4; the sampled normalized template-angle loss is 4.68886e-7 dB.
These are finite template/statistic measurements, **not detector sensitivity,
full-bank 90%/99% detection points or BER qualification**. All strict 257-knot
assertions remain independently enforced. FP64 force, raw/moment references,
packed schedules, unsupported geometry and pulse-boundary/tail cells retain
original templates/evaluation. No waveform or cryptographic address changes.

Revision 2 can borrow an immutable L2 row for the current chip/job rather than
copying it through the parent cache (`DATAPUMP_SCHED_FINE_CURVE_BORROW=0` retains
the copy diagnostic). Its lease pins the complete family/epoch/ordinal/chip/
precision/table identity and the charged backing through retirement. It never
shares private evidence. Busy/exhausted rows use original direct evaluation;
this can differ in rounding from copied interpolation under contention. Bounded
actual-class tests cover concurrent duplicate fallback, cancellation at the final
knot, retry, mixed local/shared moves, owner destruction and joined reclamation.
Borrow counts include reclaimed caches; pool counts are cumulative per live pool
and must not be summed across successive snapshots. Complete cost is recorded in
the local checkpoint report when measured.

Serious remaining opportunities include integrated fine-frequency fusion,
calendar-aware tiled timing/guard contractions and shared public pulse projections
across independent private candidates. Earlier large sparse FFT, whole-dot plus
direct-guard and conversion-heavy FP32 experiments reject only their measured
variants. They do not bound these broader strategies. Full public Gram contraction
changes FP64 association and guard histories; it needs certified continuation,
original shadow states or complete replayable history, with that cost included.
The 43× prepared frequency kernel is not a complete-receiver speedup. Burst-local
complex combination changes the statistic and requires its own noise/threshold law;
it is not silently substituted for full complex reception.

The preserved shared128 full sustained DSSS100/M100/G8 fixture failed its unchanged
aggregate backlog bound after 2,725.931 process CPU / 361.644 wall seconds, with no
accepted bit or END. A prefix or successful cheap DSSS-Off control cannot repair
that failure. Current full-bank sensitivity, demanding sustainable headroom,
hardware timing, ARM, long-duration and broader platform/SDK qualification remain
explicitly outstanding. Planner/simulation scheduled CPU remains uncalibrated;
no blanket precision discount, manufactured probability or GPU estimate is added.


The subsequent B/A/A/B cached-per-push experiment also regressed: mean
426.880 CPU / 82.641 wall seconds versus 309.255 / 37.061 for the frozen wide
path, with identical original PCM/hypotheses and strict prefix drain. Its lower
~114 MiB RSS/~115 MB DSP reservation does not offset the 1.380× CPU cost. Direct
borrowing was also slower. Neither is automatic. Within-push cached leases use
a tagged union so original Key/Row/Cache sizes and alignment remain unchanged;
mandatory State/coarse-cache diagnostic growth found in review was removed.
Pins release after joined pushes, including exceptions. Three fixed process
diagnostic counters have explicit cumulative scope and must not be summed across
bank snapshots. Fine-table sensitivity and complete transitive stack accounting
remain unqualified; these variants are retained as internal diagnostic references.

The final demanding-prefix timing audit distinguishes the mocked capture timestamp
radius (10 microseconds) from the actual combined admitted arrival width
(about141.00336ms). All eight selection records qualify timing; they retain the
separate4-second source/epoch start radius,1/3/5canonical phases and5frequency-rate
pairs. Constructed totals1365/4095/6825 include those alternatives and must not be
relabeled simple arrival-position counts. The historical fixture field
`arrival_half_width_s` records the capture radius only. This does not measure a
20-microsecond complete Live acquisition window.

An isolated worker-local fine65 COPY prototype was subsequently compiled and
measured, rather than inferred from the preceding cache failures. It retained
the original cache arithmetic and full key identity, with one eight-row cache
or two hashed eight-row banks per worker, bounded final-surplus admission and
push-scoped ownership. All 372 observed owners per variant were actually granted.
The three focused cache/accounting/cancellation/provenance tests passed. Their
initial inactive-phase construction failure is retained with its test-only fix;
no workspace increase, coverage removal or receiver change concealed that error.

The predeclared serial A1/B1/C1/W1 complete-prefix measurements were:

| Path | Process CPU seconds | Wall seconds | Peak RSS KiB |
|---|---:|---:|---:|
| Current shared fine65 COPY | 391.808 | 48.204 | 116860 |
| Worker-local eight rows | 486.193 | 66.262 | 118184 |
| Worker-local two eight-row banks | 468.930 | 62.338 | 119112 |
| Stable wide path, fine curves off | 270.653 | 33.705 | 218068 |

These are single uncontrolled exploratory runs, not confidence intervals. Each
uses identical original PCM/hypotheses/input rate and strict prefix drain, but
has no complete symbol, accepted bit or END. Local hit rates were only 0.260%
and 1.173%; local misses matched the sole pool's final cumulative hit/build/busy
counts. Per-worker ownership lost useful cross-worker reuse and repeatedly copied
full-size rows. These implementations remain isolated and are not in the rebuilt
manual GUI. Compact65/indexed rows and longer-lived bounded owners remain distinct
unattempted opportunities, not rejected by this result. Fixed192-byte/thread TLS
metadata and complete transitive stack accounting also prevent a stronger memory
qualification claim. The local `fine-local-explore-summary.json` records exact
binary/source identities, grants, counters, fixtures and limits.

The delivered stable GUI is `97d92eb573469892ad01f969571a02b6a7fa2210a1455c2b6f5ffbda874c855e`,
core `05696f6f26bb242b4fe9409af6428e6e055739e41bac456a2e2f912f6eefbdd6`.
Affected local coverage is regular104/105, GUI38/38 and contract79/79. The sole
regular failure remains the preserved ALSA clipping assertion; the regular group
is not a pass. A final fineOFF B/A/A/B preservation comparison establishes no
obvious regression in its stated prefix scope, not an additional proven speedup.
The demanding sustained-headroom objective and substantial shared contraction/
transform opportunities above remain unfinished. This is a local manual checkpoint,
not overall receiver, security, timing, hardware or platform qualification.
