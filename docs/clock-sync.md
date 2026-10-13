# System-clock synchronization and audio timing

This describes the local development candidate, not completed hardware timing
qualification. The previous receiver optimization also still awaits broader
qualification. Changes remain local; the next delivery is a manual-test GUI.

## Independent controls

Clock accuracy, region and offset describe arrival time. They do not select an
oscillator model or imply GPS discipline of an ADC, DAC or radio oscillator.
Conversely, a GPSDO oscillator does not establish synchronization of PC clocks.

`Default` preserves the existing six-second epoch search setting and its complete
receiver bank. Selecting Default in any timing field resets all four controls. An explicit
policy such as `GPS_1ms-400ms_region-2564ms_offset` means:

* Each station's system-clock error allowance is 1 ms; the link allowance is 2 ms.
* The remaining propagation region is 400 ms total, centered on the offset.
* The fixed propagation offset is 2564 ms.

Thus the physical arrival interval is offset ±202 ms before adding backend
timing uncertainty. These are user-supplied assumptions, not measured GPS accuracy
or a calculated lunar ephemeris. Fields accept editable durations, including
fractional ns/us/ms/s. The accuracy menu shows durations rather than composite
policy strings. Composite `--clock-sync` values remain supported for commands
and parameter lists, and are split into the three editable controls.
The first custom selection starts with a 1 ms region and zero offset.
Entering a smaller number cannot improve the timing evidence of an audio device.
Zero Audio error is a strict requested allowance: ordinary native device uncertainty
then forces the visible ordinary-playback/full-search fallback, not fictitious exact timing.

The adjacent editable **Audio error** dropdown has a **Default** entry, resolving
to **0 through +50 ms of audio lag per station**, after compensating the
driver-reported queue. It is neither ±50 ms nor an extra allowance on top of
provider error. GPS accuracy keeps its original ± bound. It is
saved/exported as `--audio-error 50ms`; fractional ns/us/ms/s values from
zero and up to 60 seconds are accepted. Live's finite epoch-radius limit can
reject a larger combined window. Choosing Default in any of the four timing
editors resets all four and restores ordinary playback/search behavior. A numeric
Audio error edit remains independent: it does not enable GPS timing or change
oscillator models. Existing explicit numeric imports remain valid; a partial
command that omits Audio error preserves its value. Both peers must satisfy
the selected assumption; it is not a measured device specification. This control
does not change the audio buffer used to prevent underruns. The first custom clock
selection uses 50 ms unless an explicit numeric audio allowance is already set.
With clock sync at Default, the audio allowance does not widen or narrow the
ordinary search, add oscillator hypotheses or change its modeled CPU cost. With
custom timing, playback targets the midpoint +25 ms while retaining the immutable
cryptographic epoch and source coordinates. Its audio-lag interval must fit
between zero and +50 ms; the separate GPS phase allowance is included once in
the full true-UTC check described below. Provider uncertainty is NEVER halved.
Capture retains its physical-event timestamp center and audio uncertainty U.
Equivalently its upper timestamp edge has a causal lag from zero to 2U; 2U must
fit the selected Audio error. The search map adds only the TX +25 ms center,
with TX radius 25 ms plus capture U (conservatively the declared 25 ms), then
unchanged GPS, propagation and rate allowances. Do not add the capture midpoint
a second time. This representation covers both endpoints without treating lag
as a symmetric offset. Insufficient evidence retains the visible full-search
fallback. Default broad search remains unchanged.
The default is a configurable assumption, not a loopback measurement or proof
of a particular sound card's accuracy.

## Timing and private pattern addresses

The transmitter generates a monotonically advancing logical PCM sequence. UTC
presentation timing must never rewind its sample position, reseed a private
stream, replay a pulse tail, or change an already generated canonical address.
The optional variable-rate converter preserves its filter history and adjusts
the source coordinate continuously. Its maximum rate derivative is limited using
the complete coherent symbol duration, rather than an audio callback's duration.
For long symbols this can make convergence very slow; safe waveform continuity
does not imply that arbitrary clock corrections can be followed promptly.

Custom-clock native Linux playback uses an explicitly **estimated** device model.
Reported queued frames are compensated as an offset. Residual driver uncertainty
is modeled as one negotiated period plus 1 ms of query jitter and a rate-aware
codec/USB component, `max(2 ms, 1 ms + 18 / hardware_rate)`. The period and query
uncertainty also enter the rate-estimator endpoint errors; a fixed codec delay
does not become rate precision by averaging or waiting. Period-sized pointer
uncertainty is an engineering assumption, not a universal driver guarantee.
Timed ALSA capture and playback now try a **10 ms hardware period** when the
convenience configuration's approximately 25 ms period would fail the requested
allowance and the shorter period can fit. The existing negotiated buffer capacity
(usually 100 ms), exact rate/format/channels, application `avail_min` and playback
start threshold stay unchanged. No samples exist yet: the adapter saves hardware
and software parameters, frees the installed configuration, negotiates fresh
constraints, and restores both snapshots on rejection. Failed restoration closes
the stream and reports an error. Optional API absence or an unsupported period
retains the ordinary visible fallback. Already sufficient geometries, Default
clock mode and explicit timestamp providers keep their existing configuration.

Protected playback additionally considers the stop horizon when selecting the
period. A timestamp that fits now can still leave too little room for already
committed pulses and shutdown. Selection reserves the full
declared physical rate cone over the valid-pulse stop duration, actual buffer capacity and
100 ms of block reaction allowance, plus one period for pointer-phase movement.
It requests **5 ms** instead of 10 ms when that setup estimate requires it. This
is a configuration heuristic, not a timing certificate: actual resampler
lookahead, queue position, full observation intervals and every existing runtime
frontier check remain authoritative. Periods below 5 ms are not requested.
Capture, explicit timestamp providers and Default clock mode keep their preceding
policies. Drivers may reject or round the request; successful negotiation uses
the actual period, and rejected negotiation restores the original configuration.
The native period appears in a protected-stop failure diagnostic.

The playback buffer remains 100 ms in the usual configuration. Increasing it
would not repair a UTC-headroom failure: compensated queued frames still extend
the forecast, although they do not become unknown Audio lag. Finer periods may
increase driver interrupts (10 to 5 ms means 100 to 200 period events/s), but the
application's software wakeup thresholds, 50 ms maximum blocks, allocations and
receiver work are unchanged. Physical driver CPU and timing need manual testing.
At 8 kHz, the native single-query radius before measured bracketing falls from
14.375 to 9.375 ms; at 48 kHz, from 13.0208 to 8.0208 ms. These reductions come from
a smaller negotiated period, never from dividing an unchanged error bound.

At 48 kHz, the modeled minimum radius changes from 28.0208 ms to 13.0208 ms;
equivalent total Audio allowances are **56.0417 ms and 26.0417 ms**. These include
one sample of position rounding, before query-bracketing and extrapolation uncertainty. At
8 kHz the corresponding totals are 58.75 ms and 28.75 ms. The model's reported
period error is never divided or clamped to make 50 ms pass. Failure diagnostics
show the required full width rounded outward and the selected total lag width.
This is not a measurement that physical PC audio latency exceeds 50 ms.

The implementation follows the [ALSA hardware configuration API](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m___h_w___params.html)
and [ALSA configuration source](https://raw.githubusercontent.com/alsa-project/alsa-lib/master/src/pcm/pcm.c):
`hw_params_current` fixes intervals, and even `hw_params_any` can retain installed
constraints until `snd_pcm_hw_free`. Preserving application software thresholds
avoids increasing application callback frequency or reducing underrun buffering;
a shorter hardware period may still increase driver interrupts. This setup-only
change adds no receiver bank, sample transform or steady-state allocation.
Native stub tests verify PCM identity and success/rejection/partial failure,
cancellation, and source-before-qualification ordering. A real ALSA `null` endpoint
confirms parameter negotiation and rollback, **not physical timing**. The reported
PC loopback and IC-7100 remain unmeasured in this follow-up. Plugins can reject the
new period, and later uncertainty, discontinuities or rate-model checks can still
force the documented fallback. See the [validation record](validation.md#native-audio-period-and-total-lag-allowance--local-manual-checkpoint-11-october-2026).

Native running timestamps use interval prediction. Each provider observation
`(frame_i, utc_i, U_i)` gives an interval at the requested frame after propagation
through the full admitted clock-rate envelope. The predictor intersects those
intervals and rejects contradictory evidence. Native playback observations split
`U_i = U_shared + U_period + query_bracket_i / 2`. Playback's immutable `U_shared`
contains fixed query-jitter allowance, codec/USB allowance and one frame of
rounding. Its full period error remains in **every raw observation**, but is not
also imposed as a permanent floor after intersection. Capture preserves the
preceding conservative policy: its shared floor still includes the period.
Neither floor depends on the operator's Audio error setting. A constant period
bias or sustained broad queries retain broad intervals; no independence,
zero-mean jitter or statistical averaging assumption is used. The output retains
at least the shared floor plus extrapolation error. Explicit providers without
this decomposition retain the historical fitted path/largest-provider allowance.
Relative UTC arithmetic and outward rounding retain fractional frames at
contemporary epoch magnitudes.

### Declared sample clock, host UTC phase, and sparse schedules

The Sched_Hop UTC follow-up separates three independent quantities:

* Physical sample-rate offset `a`, with `|a| <= A` from Baseband Osc. Its UTC
  duration scale is `q = 1/(1+a)`, including the asymmetric inverse endpoints.
* Host UTC phase error `C`, the selected per-station GPS/clock accuracy. Every
  complete raw provider interval `Ui` is propagated as `Ui + C` against true
  UTC. Bounded host clock adjustments are not persistent DAC frequency error.
* Provider timestamp uncertainty, queue position and explicit software events.
  Known queue delay is compensated; it is not added to `Ui` as unknown lag.

The crystal preset's 100 ppm remains the documented **effective relative link**
model. For native timing it is used once as an explicit local consistency
assumption, not silently relabeled as measured per-device accuracy. Receiver
margins and clock hypotheses remain unchanged. GPSDO similarly assumes actual
achieved sample-clock discipline: choosing it does not calibrate a sound card.
No additional independent USB crystal term is added. Shared ADC/RF references
retain their coupled receiver model; the current generic shared-radio route does
not acquire software steering that would break that coupling.

Native playback and declared-clock capture retain one feasible position interval
from the original reference across the entire stream, including history wraps
and null hops. Full observations intersect that interval transactionally; a
contradiction rejects. The 64-entry diagnostic ring does not restart the budget.
The fixed-size state checks consistency, not absolute GPS lock. A wrong GPSDO
selection can remain consistent with broad timestamps for a while before it is
rejected. Neither a short fit nor a transport name establishes discipline.

`TimePrediction` exposes the **full true-UTC radius W**, the separately declared
`C`, the raw provider floor, and physical-clock extrapolation. Playback checks
`|center error| + W <= Audio/2 + C`, while preserving its independent provider
floor check against `Audio/2`. Audio remains causal `[0,A_audio]`. Capture passes
`max(provider floor, W-C, 0)` into the existing arrival calculation, which already
includes both stations' GPS bounds. This keeps every raw provider radius and
counts GPS uncertainty once; when `W<C` it conservatively overcovers. Paired-rate
observations include `C` and intersect with the declared physical rate before
being used for software correction. These intersections remain **assumed**, not
measured clock certification.

Software correction `c` and residual waveform error differ: the emitted logical
rate against UTC is `(1+c)/q - 1`. Diagnostics report that interval separately
from the correction cap. The existing receiver-covered correction domain and
original affine waveform guard remain; this change does not pretend that an
ordinary crystal card can be corrected to GPSDO accuracy from noisy timestamps.
No source coordinate, epoch, sample or accumulated guard is reset or replayed.

The generic approximately 2,000 ppm timeline is retained when no declared policy
is supplied, including diagnostic reference fixtures. Remaining `.002` bounds
in the PipeWire adapter concern short local graph/host-clock interpolation and
bounded interruption corroboration, not a whole sparse-frame oscillator forecast.
They have not been lowered to make the new tests pass. Existing forward-repair
energy/time budgets and explicit failure for unbounded discontinuities remain.

`audio_provider_timing.hpp` defines a transport-independent capability contract:
hardware-dated capture, timestamped playback, exact continuous counters, clock
lock/holdover, expiring certified future-rate intervals, and bounded lost,
inserted, repeated or displaced samples. Tests validate the contract and reject
stale certificates, counter resets, lock loss and unbounded events. The contract
is an integration foundation; an actual dedicated-radio adapter, end-to-end
certificate enforcement through device submission, and hardware qualification
remain outstanding. Ordinary native timestamps remain estimated.

Exact-zero null retiming was investigated but is **not enabled**. The unchanged
receiver reconstructs one affine physical-time map across all nonzero samples.
Moving successive bursts by `0, +5 ms, 0` at `0,40,80 s` leaves at least 2.5 ms
error against every affine map, despite making every adjustment inside zero
PCM. The current 6120 Hz waveform guard is about 1.634 microseconds. Ordinary
10–16 ms timestamp radii cannot certify that invariant. A future implementation
needs final-FIR zero certificates, forward-only advancement/zero holding, and
precise provider dates validating each resumed nonzero sample against the
original calendar. Resetting a finite guard at each hop is not a valid substitute.

The tighter scheduled support bound and runtime keyed payload endpoints remove
unnecessary reservations. A real valid-pulse contribution that spans a long null
still has to be protected; independent future cover is excluded from that strict
payload forecast, while its actual emitted blocks retain timing and shaping
checks. See [scheduled-hop timing](scheduled-hops.md#native-utc-timing-and-remaining-qualification).

### Earlier provider-interval and preparation fixes

The preceding interval-predictor follow-up corrected a reproduced false stop: three valid period-quantized observations
on a steady 48 kHz clock gave the old fit a 26.2908 ms radius, exceeding the 25 ms
radius for Audio50. The native interval predictor retains 13.0208 ms of provider
uncertainty instead of adding the fit residual to it. It does not change the
operator allowance, erase jitter, or establish a calibrated hardware bound.
Playback errors now show signed center error, timestamp radius, allowed radius,
current-source versus generated-block-end check, period and prediction horizon.
Native failures also include the shared radius and latest/maximum retained query
brackets. In the preceding query-bracket candidate, the shared floor at 8 kHz
with a 10 ms period was 14.375 ms. A deterministic
case matching the reported 102.125 ms horizon and 8.709924 ms center error, with
one older 3.440737 ms query, gave the preceding model 16.300028 ms uncertainty:
25.009952 ms total exceeded Audio50's 25 ms radius. The split model retains
14.57966 ms uncertainty and 23.28958 ms total in that case. This is a reproduced
software failure consistent with the screenshot, not a recorded device-query
trace or proof of that PC's actual latency. All-broad or contradictory intervals
still reject; the Audio allowance and physical-device assumptions are unchanged.
Known queued frames remain compensated in the local timestamp; the remote
receiver neither receives nor needs the transmitter's driver-buffer report.
It allows for the remaining causal TX lag `[0,A]` together with its own capture
uncertainty, GPS and propagation model. Unknown plugin/radio latency outside
that declared model still invalidates a tight timing claim.

The subsequent block-end screenshot reports **10.368093 + 14.681870 =
25.049963 ms**, exceeding Audio50 by 49.963 microseconds. Playback's new shared
floor is 4.375 ms at 8 kHz, while each raw observation still has at least
14.375 ms. At a 153.125 ms horizon the unchanged generic rate cone adds
0.306864 ms; this does not imply that every prediction achieves the floor.
A deterministic 50-second fixture freezes the initial source origin 10.368093 ms
early, then presents repeating period-error extremes. The preceding predictor
exceeds 25 ms in 1664 queried positions; the revised predictor has zero such
exceedances, with a worst combined bound 21.3563 ms, including 3.2-second forecasts.
This is a constructed failure/regression, not a captured trace of the user's DAC.

Native playback forecasts the current source, next output edge, protected-stop
edge and stop-after-next-block edge **using the same full position envelope**
before requesting private PCM. Preparation includes that stop horizon, too, in
both estimated and zero-correction native modes. It also forecasts from the latest
full observation without accumulated phase history: silence preparation is
stopped and restarted before source scheduling, so its refined position evidence
cannot qualify the new stream. Actual post-restart blocks are still checked.
The final generated-block check remains. The prior stop forecast's small
oscillator correction domain no longer hides the broader native position cone.
The original affine-source guard and phase-feedback deadband remain: this change
does not inject a millisecond source warp to force a timestamp check to pass.
A looming limit triggers protected stopping while feasible; a contradiction or
insufficient remaining headroom still aborts. Very long protected tails may
require visible fallback before scheduling: with the 8 kHz shared floor alone,
the generic cone consumes a 25 ms radius after roughly 10.29 seconds of
extrapolation, sooner with center error or broader observations. This is a
conservative model limit, not a measurement of hardware lag. Native failure
context includes measured rate interval and the configured correction limit.

The earlier 100 ms buffer-based floor and fixed 500 ms transmit guard are removed.
Both transmit presentation checks use half the selected Audio error around its
positive midpoint; the complete allowed physical lag remains [0, Audio error].
Larger provider uncertainty is never clamped to that setting; insufficient
allowance or rate evidence prevents UTC-following playback. GUI and CLI hardware
transmission then visibly fall back to ordinary playback before private PCM or
source scheduling. The selected allowance is not enlarged or falsely satisfied.
Low-level callers remain strict unless they opt in and provide a status observer.
These are engineering assumptions, not IC-7100 specifications or measurements.
The preceding candidate retained a generic device/UTC envelope of about
±2000 ppm. Declared-clock native paths now use the separated physical-rate and
host-phase model above; low-level callers without a declaration retain the
generic reference behavior. Neither is a guarantee against arbitrary wander. Message and
open-ended noise use the same preparation based on their protected stopping
horizon, including pulse support, useful cover and converter/prefetch allowance.
They no longer select different timing policies based on known message length.
Preparation emits only silence, remains capped at 120 seconds and may visibly
fall back before source scheduling. Direct native playback can select zero initial
correction when that common stopping horizon fits. The optional native PipeWire
path instead prepares for at least four seconds and selects a bounded initial
correction before scheduling; see below. Neither policy promises unlimited transmission
duration.

After preparation only the silent stream stops. Source construction and fresh
scheduling happen with the DAC stopped, then the source advances continuously.
The receiver still assumes one affine clock rate, so correction stays within
0.01 logical sample of ONE original source origin and initial rate. For warp e,
rate deviation v and slew a, the braking invariant is
`e + max(v,0)^2/(2a) <= W` and `e - max(-v,0)^2/(2a) >= -W`.
The implementation reserves the additional whole-frame partial-braking distance
`a/(8*hardware_rate^2)`, and an arithmetic reserve growing with original elapsed
time and prospective braking. It forecasts full ramp steps, the last partial
step and the hold, including internal extrema. An inner feedback corridor leaves
outer braking room for the next callback. No budget is replenished at a block
or symbol boundary. Absolute timestamps/epochs remain high precision.

The final adjustable converter measures actual per-frame warp extrema, including
rate-reversal extrema. A certificate checks these, the endpoint/rate and the
remaining braking corridor BEFORE any part of a generated block is submitted.
Source prefetch is not confused with output progress. Known output queue time is
already included in the provider prediction of that frontier. The stop forecast
adds prefetched source, filter lookahead, committed valid-pulse support and one further
output block. It distinguishes safe continuation, a protected stop that still
fits, and an already-infeasible stop requiring immediate abort. Native future
positions retain their full device/UTC rate envelope; reported UTC position
uncertainty is never discarded. Unexpected jumps can still abort before
a full tail is possible.

Native runtime forecasts also accept the source's absolute logical PCM endpoint,
including its existing suppression and shaping. Once known, that endpoint can
only decrease; it cannot precede PCM already supplied to the converter. Normal
messages have a finite endpoint; open-ended tuning keeps the worst-stop reserve
until its shared finish operation commits an endpoint. Preparation and the
initial/continuous correction policy use the common valid-pulse stopping horizon,
independent of message length.

At runtime the remaining terminal distance is computed from the converter's
next **output source coordinate**, not the transmitter's prefetched input cursor.
Converter support and one logical sample of rounding are retained, and the
slowest admitted steering converts this to an output-frame upper bound E.
For private production sources, subtract the independently keyed final cover
from both the known endpoint and worst-stop duration before computing E and B.
The subtraction leaves every already-contributed valid payload/prefix pulse,
including prefetched PCM and both sides of finite support. For next-block N
and this valid-pulse worst-stop B, frontiers are
`0, min(N,E), min(B,E), min(N+B,E)`. Both stop forecasts use the SAME absolute end;
no budget resets at a block or symbol. The actual generated block still passes
its endpoint and timestamp checks before device submission. Known queued audio
remains part of the timestamp prediction. This avoids reserving another full
tail after the source is already draining, without shortening actual protection
or fabricating receiver completion. Independent cover still passes the original
current/next generated-block UTC, source-affine and FHSS checks. This removes
a speculative full-cover reserve; it does not promise to finish cover through
an arbitrary later clock failure. Generic PCM diagnostics retain their full-tail
forecast unless their caller supplies the explicit independent-cover contract.

Both production entry points install the same protected-stop hook. Low-level
arbitrary PCM diagnostics without a coefficient source retain strict runtime
error rejection. The CLI reports a timing-protected truncated transmission as
unsuccessful after drain. The GUI latches the failure immediately, offers
**Abort now** while finishing cover and device drain, then retains **FAIL: transmit**
and the reason across capture restart and Clear received. This is truncation, not
a successful send; its cancellation flag also remains set. A later explicit TX
request clears the old failure. The diagnostic includes current/next-block,
cover-end and cover-after-next-block error bounds plus the allowed radius and
cover horizon. These are conservative timing bounds, not measured physical lag.
Default clock mode does not impose this qualified UTC limit, so absence of this
stop in Default does not qualify the device for a tight arrival window.
Clock steps, missed starts,
uncertainty/domain failures and output discontinuities never trigger a silent
policy switch, waveform restart or reused address. A finite-horizon guard remains
only as a separately tested historical diagnostic utility.

Direct ALSA estimated preparation retains its zero-correction or bounded
initial-correction selection, with an independent full-cone stopping-horizon
check. The native PipeWire path estimates an initial correction, clipped to the
same receiver-covered domain as every subsequent correction, as described below. During playback, absolute DAC/system-UTC rate is **not** restricted to the
software steering cap derived from the relative link oscillator. The two are
different quantities: a GPS-disciplined link model does not measure a PC DAC's
absolute relation to system time. Native playback uses all four full-cone
position frontiers instead. The software correction clamp, original affine
reference, generated-block check and Audio error allowance remain unchanged;
receiver hypotheses do not expand. Explicit timestamp-provider diagnostics keep
their existing admitted-rate contract. Long transmissions may still reach a real
position/cover limit: this change does not promise unlimited UTC tracking.

A physical PC/PipeWire run reproduced the old rejection at source 35.730673 s:
measured absolute rate +283.059..+919.097 ppm, steering cap ±0.000300 ppm,
center error 9.017217 ms and position radius 4.568643 ms (25 ms allowed).
These are driver-model estimates, not calibrated ADC/DAC frequency or latency.
The new paced-native regression admits an absolute +1500 ppm trajectory over
eight seconds while keeping the original ±0.0003 ppm correction cap and all
position checks. It failed before the fix. See the local manual validation record
for complete physical-run outcomes and remaining scope.

The correction domain is separate from the oscillator description. For existing
effective-link bound A and correction cap C, the bank covers A+C+AC and retains
every original lane. If its complete union cannot fit the existing bank/passband
bounds, steering stays zero and the original bank remains. Shared RF-clock
topologies, Windows and hosted timing adapters retain their documented fallback.
Default clock mode preserves existing playback. A recovered native output underrun
or suspension during a message now fails visibly rather than silently continuing
a private transmission across a gap. The failure survives capture restart and
Clear received; a new transmit request clears it. Generated private epochs remain
reserved even if a device fails before audible output.

Receive PCM and its noise covariance remain unchanged. A timestamp is mapped
through the existing fixed-rate converter's source coordinate, including filter
lookahead. This is metadata; it does not introduce a receiver drift tracker.
Lost input continuity and EOF do not supply observed absence.

## Conditions for narrowing a search

The compact correlator supports an internal affine arrival map
from canonical stream phase to input sample position. It preserves half-chip
timing coverage, clipped endpoints, fractional starts, the original phase lattice,
every supplied frequency/rate lane and complete-symbol retirement. A region
narrower than one timing step retains all relevant neighboring coverage cells.
Eligible short private FFT banks now select the original timing cells inside each
canonical phase interval, including the full oscillator-rate envelope and the
original fractional-cell/two-bin-refinement margin. Empty jobs are skipped before
private template construction. Small retained sets use paired direct correlation;
larger sets can use partitioned convolution with an automatically selected tile
size. Both candidates retain I/Q, the existing noise conditioning, observation
energy and independently accumulated template norms. No decimation or filtering
is introduced by this path. Unsupported or uncertain geometry keeps the full
scanner; no frequency, clock, epoch or private bit alternative is removed.

Acquisition can close when its retained starts have complete symbol observations,
instead of waiting for the original FFT hop. Disconnected timing components close
separately only when the existing peak-exclusion radius proves that each can
produce at most one peak and the total respects the original candidate capacity.
Otherwise the complete admitted union is observed together. All original logical
hop alternatives are charged before the first component is admitted. Ready tracks
continue before the next unscored competing start. A shorter prior does not shorten
a symbol, invent unobserved input, or change the six-second physical absence rule.
This internal map is not a new persisted user setting.

Custom hardware mode combines both stations' selected GPS errors and propagation
region with the peer TX lag [0,A] and the full capture uncertainty. A 30 ms
selection places the TX midpoint at +15 ms and gives a conservative link audio
half-width of 30 ms (15 ms TX plus 15 ms capture), separately from GPS and
propagation. Inverse-slope corner bounds cover the complete initial phase
interval. The GUI explicitly calls this an **estimated audio/radio model**, even
when capture timestamps themselves are bounded. Capture uncertainty U with
2U greater than the selected Audio error disables pruning and retains the full
search. Both stations must satisfy the selected
timing assumptions. A capture
timestamp alone cannot establish the peer's timing behavior.

The map narrows compact and eligible short FFT acquisition; unsupported paths and missing/invalid timing
metadata keep the full fallback. The fixed propagation offset shifts the receive
epoch center and retirement time. Subsequent symbols retain all frequency/rate
lanes and the original physical completion rule. A cold listener inside a long
symbol can wait for the next symbol boundary; it does not recover unobserved PCM.
GUI simulation retains its full window because its random startup delay is not
a calibrated provider timestamp. Its known unsteered waveform uses the original
clock bank. Hardware estimates retain every peer correction-domain lane even if
local playback falls back: another transmitter may still be steered. The planner
reports timing-lattice work and separate setup, ingestion, admitted acquisition,
continuation and rolling-epoch allowances. The GUI distinguishes fine/inner chip
and symbol durations, search spacing, admitted arrival width, retained positions
and frequency/clock/epoch counts. A full-symbol observation and canonical phase
alternatives remain even with an extremely small clock allowance. Unsupported
geometry and uncertain model enumeration retain explicit fallback estimates.
These are engineering work estimates, not measured speedups or evidence of
reduced reserved RAM. Supported legacy UTC/DSSS probability references are
labeled conditional matched-template AWGN calculations; device steering failure
and the outer-code presence guard are outside that model. Interleaved V2
withholds numerical RX probability pending its separate sensitivity validation.

## Playback route and clock policy are independent

“Ordinary playback” means a fixed logical-to-output sample-rate conversion and
ordinary scheduled start, without Data Pump's continuous UTC rate steering or
UTC timestamp guarantees. It does not select an older Linux sound system or
bypass the selected device. In ordinary playback, `pulse` selects the ALSA
PulseAudio plugin, which can connect to PulseAudio or PipeWire's compatible server;
`pipewire` selects its ALSA plugin. The host tested in October2026 uses PipeWire.
For custom UTC mode, these resolved endpoints can use the native PipeWire
presentation-clock adapter below, targeting the same verified local sink.
`default` follows the configured route and the documented advertised-server
fallback; unknown explicit aliases are never silently replaced. Direct/exclusive ALSA
remains a separate route selection, with its existing availability constraints.

PipeWire itself uses ALSA for device access and supports PulseAudio clients; see
[PipeWire architecture](https://docs.pipewire.org/page_overview.html) and
[PulseAudio-compatible server](https://docs.pipewire.org/page_man_pipewire-pulse_1.html).
Its adapters can convert formats, mix channels and resample to reconcile graph,
stream and device clocks. This is configurable processing, not evidence that
noise suppression or echo cancellation is present on a particular route; see
[adapter/resampler properties](https://docs.pipewire.org/page_man_pipewire-props_7.html).
No system-wide effects, routing, sample rates or volumes were changed by the
local diagnostic runs. A successful strong loopback does not qualify weak-signal
sensitivity or prove absence of downstream processing.

Continuous UTC adjustment remains required for tight GPS arrival bounds and
accurately aligned genuine FHSS tuning/ramp boundaries. Ordinary playback is a
temporary fallback, not an implementation of that requirement. It preserves the
selected fake-hop waveform/construction but supplies no continuous presentation
clock guarantee. The native shared-server implementation below now performs
continuous estimated UTC adjustment on the tested PC route. Complete link timing
validation remains outstanding; no Audio-error allowance is silently increased
to make a failed model pass.

FHSS ramps are evaluated on the source calendar: epoch plus the continuous
source coordinate and the configured lag allowance. Timed playback maps that
coordinate to modeled DAC presentation UTC through the adjustable resampler.
Ordinary fallback does not maintain that mapping. The Fake hop display follows
generated samples; it is not measured DAC timing or radio-tuning telemetry.

## Shared sound-server playback

Custom UTC mode now has an optional native PipeWire output path for a verified
local `default`, `pulse` or `pipewire` route. It loads the public PipeWire0.3
runtime ABI dynamically; no SDK or new link-time dependency is required.
The tested library is1.4.2 on64-bit little-endian Linux. Other platforms, missing
libraries, non-PipeWire Pulse servers and unsupported/custom routing retain
visible preflight fallback before private source scheduling. Unknown ALSA aliases
and custom ALSA/server overrides are not automatically rerouted. `PULSE_SINK`
is resolved to its canonical PipeWire node name. No system configuration or mixer settings are modified; normal server
format/rate/latency negotiation remains active. Capture remains independent.

The adapter uses dated `pw_time` plus the matching `SPA_IO_Position` clock instead
of ALSA Pulse's interpolated latency query. Queued/buffered stream frames, graph
delay ticks and the application's unread ring each contribute exactly once.
A fresh MONOTONIC/UTC bracket maps the dated presentation edge to system UTC.
The uncertainty includes downstream latency's half-range, a **full graph quantum**,
codec floor, stream-frame rounding, clock bracket and a generic device/UTC rate
cone. The minimum retained shared codec floor is3.25ms, including when a48kHz
stream feeds an8kHz graph. Callback age is handled through the dated observation;
it is not mistaken for a change in device rate. Topology/clock/rate/quantum
or downstream-latency changes invalidate private playback. Bounded counter-only
xrun reports have the separate continuation policy below. The prepared-PCM copy
runs in PipeWire's `RT_PROCESS` data callback, rather than its ordinary control
loop. That flag selects callback context; it does not guarantee privileged OS
realtime scheduling. Multiword observations use nonblocking fixed-size mailboxes;
the callback never waits for the consumer. A separate gate protects mapped clock
storage through IO replacement and quiescent public-silence reset. Identical IO
notifications do not interrupt audio. The callback only copies prepared PCM and
records timing; it does not allocate, log, generate cryptographic waveforms or
run DSP. Control/reset API calls never hold that gate. See also the
[PipeWire stream callback contract](https://github.com/PipeWire/pipewire/blob/1.4.2/src/pipewire/stream.h).
See [PipeWire time units](https://docs.pipewire.org/structpw__time.html),
[graph clock](https://docs.pipewire.org/structspa__io__clock.html) and
[latency propagation](https://docs.pipewire.org/page_latency.html).

A bounded 750 ms S16 stereo ring provides preparation headroom, charged to the
audio workspace. Logical rates up
to192kHz use48/96/192kHz output; the server may independently resample to its
device clock. Known queued delay is compensated rather than added to Audio error.
The normal50ms generation blocks remain. End-of-input declares an exact final
frame frontier: only its final partial buffer is allowed, then all prepared audio
and server buffers drain. Unexpected starvation remains a visible discontinuity;
it does not restart private audio. Repeated starts/reset/drain in one process
are tested. Default clock mode continues using ordinary playback.

The scheduling request remains512/48kHz (10.667ms), independently of the ring
capacity and generation-block size. A256/48kHz prototype lowered reported
uncertainty but lost a graph quantum during full-GUI receiver load; a smaller
period alone also failed the preceding timing-headroom check. These failed
experiments are retained. Negotiation is a hint: uncertainty always includes the
full quantum actually reported. Changed clock geometry remains fatal, with previous/current fields in the error.
Bounded graph reports and forward presentation repair are described below;
neither is hidden as jitter.

Live reception on native Linux now leaves one whole physical CPU core outside
the decoder's affinity when its current allowed domain contains at least four
physical cores and at least80% of the preceding automatic search participant
count would remain. All allowed SMT siblings of that core are excluded. Capture,
playback, GUI and encoder keep their original domain. Missing/invalid topology,
small CPU domains or rejected affinity operations retain ordinary scheduling.
The decoder also lowers its own priority to nice5, without raising an existing
lower priority. New search workers inherit that priority; previously created
workers can retain their original priority. Each serialized search submission
propagates its caller's affinity to persistent workers, including workers created
before the Live decoder. A later non-Live caller supplies its own domain. A
worker unable to adopt the submitted mask takes no jobs; the caller and other
workers still drain every candidate. No frequency, timing, clock, key or epoch
alternative is removed. This is scheduling headroom, not a receiver speedup,
exclusive core reservation against other applications or a hard deadline guarantee.
Other platforms and cooperative execution retain their previous policy.

On this six-core/twelve-thread PC, excluding one physical core leaves ten logical
CPUs in the decoder domain. The automatic pool then uses up to nine participants,
including its caller, versus eleven previously. CPU work can still arrive in
bursts: bank orchestration is serial while individual searches use persistent
workers. A transient LATE indication remains meaningful even when average
multicore utilization is modest; it does not alone mean samples were dropped.
The bounded measurements and failed alternatives are recorded in the local
`MANUAL-UTC-HEADROOM.md` artifact and the validation record.

In the preceding checkpoint, observed audio threads on this host remained
`SCHED_OTHER` despite PipeWire's RTKit success wording. In the pinned1.4.2 implementation,
[`pw_rtkit_make_realtime`](https://github.com/PipeWire/pipewire/blob/1.4.2/src/modules/module-rt.c#L379)
queues its D-Bus request without waiting for an acknowledged result. That log
therefore does not establish realtime scheduling. The actual kernel policy was
sampled in the physical runs; the specific host-side reason for no promotion
remains unresolved. No system permission, mixer, daemon configuration or global
priority was changed to make the test pass.

Native UTC playback now makes one optional, acknowledged RTKit request for its
**own exact callback TID**, while public silence is inactive. The requested
priority is the service maximum capped at10, below the usual driver priority20.
An existing realtime policy is retained. A successful bus reply is insufficient:
actual kernel FIFO/RR policy and positive priority must be observed. The prepared
TID and policy are rechecked before private admission; later callback replacement
is fatal. Explicit scheduled-provider paths without public preparation retain
best-effort scheduling. No system daemon, global priority, mixer, route or process
resource limit is changed. The existing finite `RLIMIT_RTTIME` hard limit must
already fit the service's watchdog cap; otherwise acquisition is skipped.
The optional `libsystemd.so.0` ABI adds no build/link dependency. Custom
`DBUS_SYSTEM_BUS_ADDRESS` routes decline this request. Missing service/library,
rejection and lost replies retain a visible best-effort/unconfirmed status, with
no blind retry. An owned bus is closed before release, without a blocking flush.
See the [systemd public bus API](https://github.com/systemd/systemd/blob/v257/src/systemd/sd-bus.h)
and the [PipeWire RTKit integration](https://github.com/PipeWire/pipewire/blob/1.4.2/src/modules/module-rt.c).

This protects a bounded prepared-PCM callback from ordinary compute scheduling;
it cannot repair a stalled external sound server/driver or guarantee deadlines
under arbitrary load. Full-quantum uncertainty and UTC rate/affine-source checks
remain. The later counter-only continuation and valid-pulse forecast below
supersede unconditional xrun rejection and full-independent-cover reservation. A larger1024-frame quantum would
contribute21.333ms at48kHz before codec and stopping-forecast uncertainty; it does
not fit this25ms-radius example. Increasing an application queue cannot prevent
a missed graph/driver cycle. The512-frame request remains; the forward-realignment follow-up below increases the prepared ring to 750 ms.
Physical stress outcomes and the separate capture-validation limitation are
recorded in [validation](validation.md#2026-10-12--acknowledged-audio-callback-priority-local-checkpoint).

Before either message or open-ended noise constructs its private source, at least
four seconds of public silence provide an initial stationary-rate estimate.
The existing64-point timeline fit selects a desired correction, but a short noisy
fit is **not an independent DAC calibration**. Both the initial correction `b`
and every subsequent absolute software correction `c(t)` are clipped to the
existing receiver-covered domain `[-C,+C]`. No extra baseline is composed outside
that cap. For configured hardware/link error `|a|<=A`, the complete scale error
still obeys `|(1+a)(1+c)-1| <= A+C+AC`. Exact receiver contents and their coverage
under the original oscillator assumption are preserved. An earlier local
prototype installed a larger fitted baseline; review rejected that policy because
its fit error was an uncovered clock/frequency uncertainty, even though strong
shared-PC duplex runs decoded. That prototype is not the delivered policy.

The preparation retains the complete admitted future position cone and valid
pulse stopping horizon. Declared-clock paths check the combined Audio and GPS
allowance as described above. It stops before fresh source scheduling.
The source starts with the **clipped** initial correction installed. The original
source coordinate, timing origin and affine baseline never reset. The original
braking invariant and0.01-logical-sample warp limit remain. Stop/terminal forecasts
include queued PCM, converter support and a conservative device-rate bound;
position uncertainty uses the declared physical-rate cone, or the historical
approximately ±2000 ppm cone for low-level callers without that declaration.
Every generated block is checked before submission. Receiver lanes, waveform
coefficients, cryptographic addresses and source bits are unchanged.

This implements bounded continuous UTC correction, not arbitrary sound-card
calibration or indefinite tracking. A raw PC sound card does not become a GPSDO
by selecting that oscillator model. External discipline/calibration must actually
satisfy the chosen hardware model; the four-second fit does not prove that fact.
With very tight oscillator coverage, permitted software corrections are tiny;
accurate dated observations can still prevent spurious timing stops without
injecting a larger unsearched source-rate error. Incompatible evidence still
causes protected stopping where feasible, or immediate abort for discontinuity.

Audio50 remains causal lag[0,50ms], targeted at25ms with25ms allowed radius.
Neither period refinement nor this baseline fit enlarges that interval or proves
unknown radio/peer delay. Status reads “Native PipeWire UTC adjustment active;
estimated hardware timing.” A successful physical duplex run establishes local
execution and strong-signal reception, not calibrated RF presentation time or
qualified tight peer acquisition. Capture may retain its full search fallback.

The earlier ALSA-plugin experiments are retained as failed diagnostic evidence:
one39.8623s transmission/reception succeeded with a4ms period, but its repeat
contradicted the clock envelope at37.85s; native48kHz ALSA-plugin playback also
failed. Neither rate change nor finer period was retained as a cure. The new
native adapter addresses the interpolated timestamp path instead. Optional
shared-plugin period refinement is still skipped: freeing/reapplying parameters
to improve an unused native model can fail to restore the plugin. Other-route
restoration failures still reject before PCM, with direction/endpoint/context.
An explicitly supplied timestamp provider retains its separate tested contract.
See [local validation](validation.md) for final build identity, full-GUI live
outcomes, retained failures and outstanding qualification.

## Hardware evidence and limits

ALSA distinguishes application, DMA, link and analog presentation time. A
hardware link counter can help measure rate separately from unknown codec/radio
latency, but counter resolution is not a bound on RF presentation time.
[Linux ALSA timestamp documentation](https://docs.kernel.org/sound/designs/timestamping.html)
describes these distinctions and the reported timestamp types.

The Linux 6.12 USB audio driver uses estimated delay and has no hardware
`get_time_info` callback in its PCM operations. Repeated delay queries do not
establish a calibrated DAC or radio-DSP timing bound. The IC-7100 USB path,
built-in cards and custom synchronized cards must be evaluated separately.
[Linux 6.12 USB audio source](https://github.com/torvalds/linux/blob/v6.12/sound/usb/pcm.c)

The PCM2901 datasheet gives ADC and DAC digital-filter delays of `17.4/Fs` and
`14.3/Fs`, and describes initial playback buffering of one 1 ms USB packet. At
48 kHz those converter delays are approximately 0.363 and 0.298 ms. They support
the scale of the component estimate above, but do not specify total IC-7100 radio
DSP, host/plugin or analog latency. The rate-aware term also avoids assuming a
2 ms ADC allowance at 8 kHz, where the specified filter delay alone is larger.
[TI PCM2901 datasheet, pages 4, 5 and 24](https://www.ti.com/lit/ds/symlink/pcm2901.pdf)

The later PC audio tests use the host analog route and a physical return path;
there is still no measured IC-7100 loopback calibration. Publicly documented
IC-7100 RF/USB clock assumptions are recorded in
[oscillator models](oscillator-models.md#ic-7100-assumptions); they are used as
models, not presented as measurements of this particular radio. Software work
continues without making hardware calibration a prerequisite.

The rate evidence utility bounds the mean ratio from paired UTC/link-counter
endpoints, retaining their full errors rather than dividing jitter by the number
of observations. A future-rate guarantee additionally requires clock-wander
evidence. Existing oscillator models describe effective link uncertainty; their
grid spacing is not spare allowance for a new transmit rate correction.

## Validation status

Focused tests cover continuous resampling, arbitrary block boundaries, tiny slew
increments, timestamp discontinuities, uncertainty retention and finite shaped
waveforms. Conditional waveform-norm screening is not a C/N0 sensitivity curve,
full acquisition-bank calibration or a hardware latency measurement.

Native adapter fixtures now cover exact and ±100 ppm synthetic frame clocks,
partial writes, cancellable nonblocking tail drain, stopped-device source
construction, rejection before private PCM generation when rate evidence is
insufficient, and capture metadata with exactly unchanged PCM. Live queue tests
preserve acquisition with a 30-second processing delay. Qualified-window tests
compare raw and pulse statistics, thresholds, addresses and next-poll events
with signed fractional origins, clipped cells and nonzero paired clock lanes.
These fixtures exercise the provider contract, not a physical device.

Still required: actual IC-7100 and sound-card measurements, a hardware-qualified
timing model, complete provider-to-search integration tests and threshold-region
paired receiver experiments for the timing path. Existing conditional receiver
optimization sensitivity evidence does not qualify this new path. Broader contract,
native, SDK/platform and packaging qualification remains pending the user's
manual-test checkpoint. See [development requirements](development.md),
[oscillator models](oscillator-models.md) and [spread controls](spread-spectrum-controls.md).

The 9 October focused native adapter run also exercises active default slew,
partial writes, finite tapered PCM, cancellation and timestamp failure after
private output begins. A probed interpolation-table derivative bounds its PCM
difference from the initial affine trajectory; this is a waveform-envelope check,
not a sub-sample hardware measurement or detection-probability experiment.
One-sided whole-message guard tests cover 1-second, 1-hour and 1-day horizons.

The subsequent local follow-up fixes the discrete timing-neighbor endpoint
fixture and the coherent wrong-outer-key fixture. Complete affected GUI37/37,
receiver, correlator, Live and estimate tests pass. Native tests additionally
cover downgrade from qualified capture while retaining resampler state, stopped
source construction and visible fallback before private playback. The former
13/14 and43/44 core counts belong to the earlier frozen baseline.

These passes do not prove the exact true endpoint for arbitrary off-grid starts,
strong factor1 structured-null rejection, enhanced DSSS detector protection or
hardware timing. Conditional factor10 sensitivity is now measured separately;
its known-hypothesis scope does not qualify complete UTC acquisition.
See the [follow-up evidence and open coverage](dsss-followup-validation.md).

A component workspace probe at 48 kHz hardware rate measures 100,336 bytes for
the converter with either ordinary or timed 64/4,800 Hz input. At equal 48 kHz
rates, enabling interpolation adds 100,112 bytes. The UTC and link-rate timeline
objects occupy 5,216 and 192 bytes in this build, excluding their surrounding
adapter. These are component allocations, not a peak-RSS measurement or a new
end-to-end CPU/acquisition benchmark. Silent preparation can add wall latency;
receive PCM buffering and per-bit publication are unchanged by the audio field.

## Open-ended tuning source

Tuning and messages share fresh epoch scheduling, prefix alignment, continuous
original-reference timing, protected-stop preparation and discontinuity abort.
The controller does not need the final source length. Its accumulated deviation
budget is never renewed at a block or symbol boundary. If the original affine
receiver map and UTC allowance become incompatible, both sources request a
protected stop while feasible, otherwise abort visibly. Stationary-clock
assumptions and physical-device qualification limits still apply; this does not
promise indefinite tracking of arbitrary clock wander. See
[transmitter cover](transmit-cover.md#playback-and-timing).

## FHSS transmitter and arrival timing

`--audio-error default` and `--audio-error Default` now resolve to50ms in GUI
launch commands, parameter-list imports and the CLI. They retain an explicitly
selected Clock sync policy; the GUI dropdown's reset-all action remains separate.
Exports use the stable numeric50ms form. Unitless nonzero durations remain errors,
now identifying Audio error. GUI `--self-check` validates the supplied launch
settings before running, so the self-check shortcut cannot hide a bad launcher.

FHSS uses the configured per-station GPS accuracy at BOTH endpoints (±10ms each
by default), TX audio lag [0,A], total propagation region R and fixed offset O.
Receiver RF hops must occur O later than TX hops using the original hop ordinal
and key/epoch. Fixed offset cancels from the source guard; its interval is
[N−A−2G−R/2,N+2G+R/2], plus the selected C2 ramp transitions.
Capture audio uncertainty is downstream of RF tuning and does not add another
transmit lag. See [FHSS power controls](spread-spectrum-controls.md#hop-power-transitions-local-manual-candidate).
Fake mode shows the TX tuning and a separate delayed arrival preview; it does not
retune an actual transmitter or receiver. Neither preview nor pre-arrival state
changes receive admission, progress or physical completion. Impossible LOCAL
guards block production transmission. When the selected full remote guard spans
the dwell, Fake can illustrate local-safe ramps, explicitly without a remote
overlap guarantee; the full uncertainty is not discarded from the configuration.
Smooth-envelope RX shares original templates and treats the ramps as fading.
Qualified arrival timing retains complete physical phase cells for each exact
cipher representative. A qualified prior is intersected with the existing bank's
original domain; it need not be wholly contained there. This does not enlarge
that domain or promise coverage outside it. An unavailable/disjoint model retains
the broad fallback. Default retains its broad search. Physical absence still
requires complete failed-symbol observations, never just the quiet interval.


For automatically deferred FHSS bits, both peers additionally share the immutable
UTC slot calendar and its timing allowances. The qualified arrival prior covers
canonical slot starts rather than pretending that logical bits remain contiguous.
Default still retains its broad six-second acquisition policy. Hop guard defaults
remain ±10ms at EACH endpoint and audio lag0..50ms. The slot calendar is not a
clock-tracking algorithm, and scheduled holes cannot supply observed absence.
See [deferred whole symbols](spread-spectrum-controls.md#deferred-whole-symbols-local-manual-candidate)
for the exact rounding, fit limits, work/airtime and interoperability requirements.

## Developer timing instruments

`datapump-gui --verbose` runs the complete ordinary GUI with an additional
asynchronous JSONL timing trace. `--verbose-file NEW-PATH` selects a new file;
an existing file is never overwritten. Otherwise a unique `datapump-verbose-*.jsonl`
is created in the working directory and its path is printed to stderr. This is
an opt-in diagnostic, not a saved modem setting or a timing-policy override.

The trace records each consumed native timestamp with full `long double`
serialization precision, model/reset identity, coherent PipeWire graph/queue
snapshots, native ALSA query brackets, original epoch and presentation midpoint,
rate limits/estimates/plans, all four protected-stop frontiers, generated-block
checks and native playback discontinuities. It contains no PCM, message bits,
keystream bytes or root keys. Formatting occurs on ordinary producer threads;
the real-time callback only supplies its bounded existing clock snapshot.
The bounded nonblocking queue holds at most 256 records of at most 16 KiB each;
file output is capped at 256 MiB. Slow storage cannot block audio submission.
Shutdown waits at most 500 ms for the writer after controllers join. The schema2
footer is provisional: `prior_io_failed` describes writes before finalization.
After flushing and explicitly closing the file, the writer emits a matching
`trace_finalization` JSON line on stderr with its token, path, accounting and
flush/close results. Complete evidence requires both the footer and this successful
external seal, with no dropped records, size limit or I/O failure. Preserve stderr
with the trace. A missing seal/footer makes the trace incomplete, including a
stalled writer detached at shutdown. The diagnostic watchdog's exit124 is a
failure, not completion. Failed finalization never deletes or renames user paths.

For a bounded device/return-path experiment, run:

```sh
./build/dev/datapump-gui --audio-timing-test 30 --audio-test-device default --verbose
```

This developer CLI instrument has no GUI button and needs no keyfile. Durations
are 5–300 seconds. It emits quiet public, smoothly windowed chirps (peak 0.005 FS),
requests 48 kHz input/output, filters the input to 8 kHz and searches each known
probe using bounded FFT correlations with fractional refinement. It does not
alter mixer/routing settings. The recorded PCM stays in bounded RAM, not files.
The process watchdog exits124 if device teardown exceeds duration + 30 seconds.
Exit 0 means enough input returns were measured; exit 2 means insufficient/no
return, and exit 1 means a device, continuity, cancellation or analysis error.
No-return measurements are null. None of these is a modem or timing-qualification
pass. A digital monitor can also return the probe; establish the physical route
before attributing the result to DAC/cable/microphone/ADC hardware.

The probe deliberately observes ordinary input rather than requiring the ADC's
estimated UTC model to qualify. Native sound-server pointers can remain frozen
while startup PCM is drained; strict estimator failure would otherwise prevent
the measurement. Full-GUI verbose runs retain the production capture model and
record that failure. The probe's immutable nominal-map delay includes unknown
startup/driver/analog offsets, and is not calibrated one-way latency. Its fitted
delay slope includes relative clock, path and buffering changes. Per-probe host
round-trip delay includes output/input queues and callback quantization; detrended
jitter is the observed finite-run residual, not a bound on future jitter. Playback
UTC is separately labeled estimated or fallback. The optional radius sum uses
playback **preflight** uncertainty and is not a runtime DAC-error certificate.

Comparing 50 and 60 ms Audio error changes both the allowed radius (25 versus 30 ms)
and scheduled midpoint (+25 versus +30 ms). A successful 60 ms run does not prove
actual hardware lag exceeded 50 ms. Replaying timestamp/frontier arithmetic can
identify a conservative forecast or numerical error; it cannot independently
calibrate analog presentation time, certify a sub-ppb GPSDO or prove an arbitrary
future clock trajectory stays within the allowance. These instruments never
widen the receiver's hypotheses or relax the unchanged protection guards.


## Bounded graph-xrun continuation (local manual candidate)

Native PipeWire previously aborted on any increase in the shared graph's xrun
counter. That counter estimates graph interruption duration; it is not an exact
count of this stream's missing or replayed DAC samples. The first recovery candidate
permitted a counter-only report when clock ID, rate, quantum and mapped IO
are unchanged, position and cycle advance by exactly compatible quanta, and
MONOTONIC nanoseconds agree with that advance within two graph frames plus the
existing 2000ppm envelope and nanosecond rounding. Local prepared-ring starvation,
failed buffer submission, changed topology, backwards counters and contradictory
UTC evidence still fail. The unchanged timestamp history, original source/epoch,
absolute rate cap and affine guard are never reset. Each queued PCM frame is
copied at most once; no private source is rewound or restarted.

Production selects a token budget B = min(100ms, 1% of symbol duration), draining
at B/symbol-duration. In any interval T, admitted reported duration is at most
B*(1+T/symbol-duration). A graph quantum larger than B is ineligible; short fast
symbols do not silently tolerate whole-bit gaps. The cumulative event count and
estimated duration never reset. These are conservative operational limits, **not
an erasure-energy certificate or a guaranteed sensitivity loss**. Private-pattern
power varies; stable graph telemetry cannot certify downstream PCM continuity.
The GUI timing status and `pipewire_recovery` verbose event expose that limitation.
The real-time callback performs only bounded arithmetic and existing PCM copies;
status formatting, logging and timestamp checks stay on the playback thread.

A separate known-erasure regression blanks 10.7422ms of transmitted signal,
leaving channel noise and original time coordinates intact. Off/10/100/1000 DSSS
bursts retain exact bits, individual progress publications and physical absence.
Conditional worst-phase I/Q matched-projection penalties are 0.070751, 0.028056,
0.017622 and 0.004702dB for their particular synthetic templates. These are
finite-template calculations, not estimates of full-bank detection probability,
not threshold-region sensitivity measurements, and not a bound on arbitrary
hardware xruns, inserted/deleted PCM or persistent phase/time shifts. Larger or
unmeasured damage remains outside this recovery scope. An actual clock-map change
cannot be repaired by pretending a graph report was advisory. The following
forward-realignment path repairs eligible presentation interruptions explicitly.

The recorded headroom failure had a current bound near16.15ms but added about
8.75ms for a4.37s independent-cover forecast. Forecasting only through committed
valid pulse support avoids this premature stop without enlarging Audio50ms's
[0,50ms] interval. All actual cover/output blocks retain current UTC validation.
Indefinite tracking on an uncalibrated sound card, arbitrary clock steps and
unbounded downstream interruptions remain unsupported; this checkpoint does not
establish that50ms universally suffices or that60ms is necessary.

Continuation is restricted to active source production. The final generated
block and device drain retain strict xrun handling. A counter event racing the
final frontier must have a subsequent validated timestamp and visible report;
otherwise drain fails explicitly. No event may disappear at a zero-frame return.
The CLI message path and GUI message/tuning-noise paths install the same policy.


## Forward presentation realignment and 750 ms preparation (local candidate)

The first recovery policy accepted the graph xrun report but could still fail
`audio device clock exceeds UTC timing model`. The recorded 13-minute failure
advanced source deliveries 42.667 ms, graph position 64 ms and dated graph time
77.022 ms. The application ring was full 250 ms; the 12.604 ms graph counter increase
was **not** the 34.355 ms source/presentation displacement. More FIFO capacity
alone would not fix a missed graph delivery.

The native callback now compares the next dated delivery with the previous
successful buffer **end**, including server queue/delay. With the same clock,
rate, quantum, latency and queue geometry, a forward interruption accompanied by
missed graph cycles or an xrun may discard expired **unplayed** final PCM. A 5 ms
quintic fade reaches zero, the consumer advances once past the expired addresses,
and a 5 ms fade restores gain. This permits approximately 200 Hz transition bandwidth
for a rare repair; it is not the original arbitrarily narrow pulse bandwidth.
No private generator, resampler, epoch or UTC history restarts. The converter's
filter history and every subsequent sample address remain original. Both stereo
channels receive the same gain and skip. No repair inserts or replays PCM.

The next publication occurs only after the transition; the playback thread must
validate it against the **original** timestamp history and timing allowance.
Integer-frame discards carry a signed cumulative displacement interval. Each
proposal uses the original generic inverse ±2000 ppm clock-rate envelope, not a
rate fit treated as metrology. The interval is committed only when the buffer
containing the actual skip is successfully queued. It never resets at a block,
symbol or elapsed-time boundary. The initial recovery candidate used a half-logical-sample ceiling (superseded
by the complete anchor certificate below); this is a tested impairment allowance, not a universal detector-loss
bound. The recorded fixture removes 1,649 frames at 48 kHz and charges roughly
70 microseconds of possible persistent displacement, within 81.7 microseconds
at 6,120 logical samples/s. Another comparable interruption would exhaust this
conservative cumulative bound and stop. Higher-bandwidth configurations can be
ineligible even for the first interruption. Fractional-delay recovery and a
better independently justified device-rate bound remain future work.

A separate token bucket charges the entire affected bound: interruption,
previous delivery, downstream queue/delay, timestamp uncertainty and both ramps.
B is capped at min(100 ms,1% of symbol duration); at most 2 B can occur in any
symbol-length interval. The bound must also be shorter than a symbol and six
seconds. This is an operational damage limit, **not** a sensitivity guarantee
for amplitude-varying private patterns. The recorded callback fixture charges
91.0632 ms, not merely the 12.604 ms counter. Negative/ambiguous shifts, changed
clock/queue geometry, insufficient unplayed suffix, overlapping interruptions,
exhausted budget and final/drain events fail. No threshold, hypothesis, bit
position, publication or physical-completion rule is changed. Source EOF and
repair never count as observed absence.

The application ring is now 750 ms instead of 250 ms. At 48 kHz stereo S16 this adds
96,000 bytes; at 192 kHz it adds 384,000 bytes. Its delay remains compensated; it does
not add 500 ms to the Audio error allowance. The existing 2000 ppm timestamp cone
adds about 1 ms for 500 ms more lead. Callback quantum remains 512/48000, rather than
increasing the driver's timing uncertainty. More prepared data tolerates source
thread scheduling stalls; it does not guarantee sound-server/driver deadlines.
Graceful stopping can have up to 500 ms more already-admitted PCM to finish.
Immediate abort still destroys the stream without waiting for the ring to play.
The new verbose fields distinguish submitted frames, discarded source addresses,
repair count and affected bound; graph counter duration remains separate.

The exact callback regression includes the recorded six-cycle/four-delivery
failure, small callbacks splitting the transition, unchanged original UTC
history, unsafe short symbols, backwards/changed clocks, repeated interruptions
and an empty final callback. Paired finite-channel receptions cover DSSS
Off/10/100/1000, production FHSS ramps, middle/final-bit interruptions, every
stream, next-poll publication and scored absence. Conditional I/Q projection
penalties are 0.0048–0.0695 dB for those particular signals and the stated channel.
The additional ±half-logical-sample persistent-displacement fixtures measure up
to 1.61 dB under the same explicit finite-channel interpolation model. These
are deterministic calculations, not 90%/99% full-bank sensitivity estimates.
Actual downstream missing/repeated DAC samples, rare false accepts, other devices,
repeated real interruptions, ARM and broader qualification remain outstanding.


## Counter-free presentation dates: confirmation before repair

The later manual trace `datapump-verbose-1791810770262374091.jsonl` contains
10.854313 ms and18.062712 ms date jumps with a full750 ms prepared ring, unchanged
queue geometry and no independently reported xrun/skipped cycle. The preceding
candidate rejected both immediately. Neither trace includes the failed raw clock
pair, so these are not measurements of missing DAC samples.
[SPA clock documentation](https://docs.pipewire.org/structspa__io__clock.html)
explicitly permits jitter in `nsec`; [stream time](https://docs.pipewire.org/structpw__time.html)
does not turn it into a hardware loss ledger.

The native provider now quarantines a counter-free forward date and checks two
later, advancing graph dates against the **same pre-event delivery anchor**.
PCM remains sequential during confirmation. A persistent coherent displacement
can use the shaped forward repair only if independently sampled callback
`CLOCK_MONOTONIC` elapsed time supports it at onset and both later points.
The supporting tolerance is2ms +2native samples +the original2000ppm elapsed
clock allowance +clock-query bracketing. Queries taking over2ms cannot support
a counter-free skip. This screens a provider-only timestamp step at ordinary
callback cadence and a late callback that subsequently catches up; it does not
prove downstream DAC sample loss. An excursion that returns to the original
grid and remains there for two further dates changes no PCM. Both paths charge
the entire uncertain interval and must pass the original UTC timeline before
publishing recovery. No timestamps, counters, keys, epoch origins or receiver
hypotheses are reset. A correlated provider/arrival error or downstream buffering
can still differ from actual DAC presentation. The residual certificate below
is conditional on the estimated presentation provider being valid; it is not a
physical xrun diagnosis or an end-to-end hardware guarantee.

Confirmation is bounded by the same100 ms/1%-of-symbol token policy. Reserve its
remaining callbacks before admitting their PCM. The18.063 ms case at512/48k adds
21.333 ms confirmation time and charges approximately96.104 ms affected duration.
Short symbols, drifting or contradictory offsets, backward dates, changed queues,
insufficient unplayed suffix, overlapping repairs and final/drain events remain
ineligible. A transient event that cannot return and settle inside that budget
still stops. A larger Audio error does not override these impairment constraints.

The residual certificate now includes rate uncertainty from the preceding
successful buffer and **all confirmation deliveries**, as well as skipped frames.
For nominal gap g, total anchor-delivery count n, skipped count k and true seconds
per native frame q, the increment is `g + n/Fnative - (n+k)*q`. The whole original
inverse±2000 ppm envelope is propagated outward and never reset. The old34.355 ms
fixture therefore needs approximately90 microseconds, rather than the incomplete
70 microsecond certificate. The bounded impairment ceiling is now5/8 logical
sample (102.124 microseconds at6120Hz). This modest relaxation is a waveform
tradeoff, not a precision improvement. Repeated repairs can still exhaust it.

The original96 paired finite-channel receptions and their assertions remain.
An additional32 conditional I/Q projection cases use48kHz analytic waveform
construction,1500Hz carrier, DSSS Off/10/100/1000, FHSS on/off, middle/final-bit
interruptions and both±102.124 microsecond residual extrema. They include every
later bit and measure1.0550–1.09486dB worst-phase loss. The48k construction avoids
misattributing coarse6120Hz linear-interpolation attenuation to the production
48k integer skip. These are finite-template matched-projection calculations,
not a full-bank90%/99% detection curve, confidence interval, or universal loss
bound. Subsequent source addresses stay original and physical completion remains
fully scored absence; no EOF or recovery event substitutes for it.

Verbose failure and recovery records now retain the previous/current graph dates,
cycles, positions, flags, xrun values, rate estimates, queue/delay fields, anchor
frame count, confirmation count and independent callback-arrival evidence.
Formatting stays off the realtime callback; its clock reads allocate nothing.

### Separate sustained-drift limitation

The same trace contains a distinct protected stop at682.454 source seconds.
The reported timing center moved to+10.378 ms while uncertainty stayed near14.21ms;
the next-block-plus-stop bound reached25.002431ms against a25ms radius. The applied
correction remained+0.0003ppm under the selected GPSDO model. Graph dates accrued
10.551ms over688.704s, whereas the UTC/monotonic bridge varied by only396ns. This
is not sample rounding or a system-clock step, but it also does not independently
calibrate the DAC: the past rate interval still straddled zero.

This recovery does not conceal sustained drift with repeated skips or enlarge
clock coverage. A generic sound card cannot acquire an independently demonstrated
sub-ppb discipline merely by selecting GPSDO. Longer operation can require a
larger arrival allowance or actual audio-clock discipline; supporting broader
continuous correction needs separate waveform-map and receiver validation. The
successful786.518s default-timing transmission does not qualify tight UTC timing.

In a genuinely calibrated controller, let `1+a` be physical UTC seconds per
nominal DAC second and `1+c` be generated source seconds per nominal DAC second.
Emitted source rate is `(1+c)/(1+a)`, so exact `c=a` would cancel device drift.
The current conservative software cap and affine guard do not certify that
inverse calibration. A replacement would need a bounded residual interval
`[(1+c)/(1+aUpper)-1, (1+c)/(1+aLower)-1]` and a physical-UTC waveform-affinity
bound. The trace's past interval[-18.939,+49.312]ppm leaves about±34.125ppm
uncertainty even at its midpoint, versus the selected±0.0003ppm software domain.
Simply removing the cap or restarting its reference would not establish the
required receiver compatibility. Capture is also fixed-ratio: correcting only
TX can remove shared-card ADC/DAC cancellation and expose ADC drift. A certified
hardware-to-UTC controller and corresponding capture discipline remain separate
work; the present change addresses bounded presentation interruptions.
