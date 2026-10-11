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
cryptographic epoch and source coordinates. Its full predicted event interval
must fit between zero and +50 ms lag: provider uncertainty is NEVER halved.
Capture retains its physical-event timestamp center and full uncertainty U.
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
The earlier 100 ms buffer-based floor and fixed 500 ms transmit guard are removed.
Both transmit presentation checks use half the selected Audio error around its
positive midpoint; the complete allowed physical lag remains [0, Audio error].
Larger provider uncertainty is never clamped to that setting; insufficient
allowance or rate evidence prevents UTC-following playback. GUI and CLI hardware
transmission then visibly fall back to ordinary playback before private PCM or
source scheduling. The selected allowance is not enlarged or falsely satisfied.
Low-level callers remain strict unless they opt in and provide a status observer.
These are engineering assumptions, not IC-7100 specifications or measurements.
The future native clock is assumed stationary within its measured/admitted
rate interval; this is not a guarantee against arbitrary wander. Message and
open-ended noise use the same preparation based on their protected stopping
horizon, including pulse support, useful cover and converter/prefetch allowance.
They no longer select different timing policies based on known message length.
Preparation emits only silence, remains capped at 120 seconds and may visibly
fall back before source scheduling. Zero initial correction is valid when that
common stopping horizon fits without an extended rate measurement. This makes
no promise about unlimited transmission duration.

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
adds prefetched source, filter lookahead, support/cover duration and one further
output block. It distinguishes safe continuation, a protected stop that still
fits, and an already-infeasible stop requiring immediate abort. Native future
rate evidence retains its explicitly assumed stationary domain; reported UTC
position uncertainty is never discarded. Unexpected jumps can still abort before
a full tail is possible.

Both production entry points install the same protected-stop hook. Low-level
arbitrary PCM diagnostics without a coefficient source retain strict runtime
error rejection. The CLI reports a timing-protected truncated transmission as
unsuccessful after drain; the GUI marks it cancelled. Clock steps, missed starts,
uncertainty/domain failures and output discontinuities never trigger a silent
policy switch, waveform restart or reused address. A finite-horizon guard remains
only as a separately tested historical diagnostic utility.

Native estimated preparation can accept zero correction using the independently
admitted stationary device-rate domain (derived from the configured oscillator
bound); otherwise it requires the complete measured rate interval inside that
domain. Runtime stopping intersects the measured interval with that domain.
This is an explicit engineering assumption, not measured rate precision. A
disjoint interval fails. UTC position uncertainty is never reduced by that
intersection. Arbitrary device wander outside that domain is unsupported,
including during a protected stop.

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

The user has no loopback available for this development stage. Publicly documented
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
