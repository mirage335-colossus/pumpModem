# Scheduled burst hops (local experimental candidate)

**Sched_Hop** spreads a Robust Modem transmission over keyed active and null
time slots. It gives the receiver time to process captured active samples before
the next burst. It is independent of Fake FHSS: Sched_Hop changes physical audio
by inserting silence, whereas Fake FHSS illustrates tuning without retuning the
radio. Both can be enabled together. Sched_Hop requires a selected encryption
key and matching peer settings.

This is a manual-test candidate, not qualified hardware, receiver-sensitivity or
security coverage. In particular, the scheduled amplitude detector has no
calibrated RX-success probability. A favorable CPU estimate is not proof of
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
The planner compares mean work with work due before the next guaranteed recovery
deadline. Split-DSSS weight construction and setup remain uncalibrated in this
cost model; these are engineering estimates pending complete execution evidence.
The larger ratio informs decoder selection and warnings. The processing budget
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

The initial supported split-symbol receiver is the experimental amplitude-energy
path. It retains whole nonoverlapping real-input FFT observations only when the
entire window fits the admitted active flat portion, including timing cells,
clock scaling and any FHSS guard. It never concatenates samples from opposite
sides of a null interval into one FFT. Original shaped-envelope quadrature
follows source time, while capture coordinates and publication endpoints retain
physical time. I/Q bin energy, original noise degrees, quarter guards and trial
accounting accompany every retained observation.

Every noncontinuous scheduled profile retains physical start phases throughout
the full UTC second, including when a source symbol is shorter than one second.
The ordinary `min(symbol samples, sample rate)` phase extent relies on shifting
the symbol index along a continuously advancing source. Scheduled null plateaus
break that equivalence: reducing a late physical start modulo the source-symbol
duration can move it into a null hop and discard the transmission. The shared
`canonical_phase_extent` therefore returns a full second for scheduled bursts;
ordinary and continuous G=M profiles retain their preceding extent. Active-keyed
pruning and exact packed-slot enumeration still apply separately. Receiver,
planner and simulation counts charge the retained domain rather than silently
truncating it to improve cost estimates.

The receiver and planner share a flat-safe geometry calculation. A long chip's
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

Eligibility requires shaped private patterns, at least 64 chips, at least 64
retained whole energy observations per symbol, a complete clock grid of at most
65 lanes, and a positive-frequency band with all transition margins inside
capture Nyquist. Scheduled Interleave DSSS and supported FHSS masks use this
path; legacy outer DSSS, too-short symbols, inadequate flat groups or unavailable
coverage remain explicitly unsupported. Disabling the new decoders prevents
split-symbol reception. Complex only also prohibits this amplitude path.
`G=M` returns to ordinary decoder eligibility. Packing a short symbol into a
full-gain slot does not relax the 64-retained-observation requirement: very fast
geometries can remain unsupported even when their waveform fits a slot.

### Unavailable receiver versus exhausted workspace

The initial manual launcher opens the ordinary 3,600 Hz rate / 1,500 Hz carrier
defaults with scheduling disabled. Loading a key and choosing 10 does **not**
make those defaults eligible for scheduled amplitude reception. At the resolved
14,400 Hz input rate and eight samples per chip, the default short/long target
profiles have 1,024/512 sample symbols (71.111/35.556 ms). Even the minimum
16-sample FFT gives conservative retained counts of only 56/24 after boundary
allowances, below the required 64. This is a detector limitation, not a shortage
of DSP workspace. A scheduled complex decoder for such short profiles remains
unimplemented; changing allocation alone cannot enable them.

Live reception now retains the concrete eligibility or allocation error for
Automatic selection as well as forced strategies. Unsupported initial banks
show **receiver search limited**, with `RX unavailable:` and the reason in the
status detail; partial banks show `RX search incomplete:`. They retain their
physical sample coordinate and retry at ordinary UTC epoch-admission fences,
instead of resetting acquisition and retrying on every PCM callback. A valid
configuration change clears this diagnostic. Actual processing failures that
discard acquisition state still report **receiver restarted**.

An exact-nominal geometry preflight also reports intrinsically unsupported
profiles during long null frames. It uses the production private flags and joint
FHSS guards, but zero frequency/clock/arrival error, so it tests maximum available
support and minimum spectral margins. Passing is only necessary: actual admission
still requires the complete configured uncertainty, epoch and workspace checks.
This preflight does not remove any hypothesis from an admitted receiver.

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

Search arithmetic remains independent of the schedule. Default and FP32 minimum
use the demonstrated native FP32 spectral FFT and packed FP32 envelope path,
with finite-range FP64 transform fallback. FP64 force uses native FP64 FFTs and
original FP64 coefficients, bypassing approximate template interpolation. Energy
and limiting calculations retain FP64, and sensitive moments/scoring retain
wide arithmetic. Every choice retains the existing envelope-quadrature
approximation; FP64 is not an exact continuous-envelope integral.

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
the existing six-second minimum. Null wall time does not count; this is not a
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
finite support, not indefinite clock tracking. On an admitted stream, a failed
standalone full-symbol test cannot classify absence if fewer than 64 whole FFT
observations remain or any quarter is unobserved. It instead reports
`scheduled amplitude timing coverage exhausted` before changing pending,
missing-bit or completion state. Strong evidence retains the existing admission
statistic. Adaptive reconstruction of an aging clock grid remains outstanding.

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
