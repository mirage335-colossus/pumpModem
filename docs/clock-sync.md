# System-clock synchronization and audio timing

This describes the local development candidate, not completed hardware timing
qualification. The previous receiver optimization also still awaits broader
qualification. Changes remain local; the next delivery is a manual-test GUI.

## Independent controls

Clock accuracy, region and offset describe arrival time. They do not select an
oscillator model or imply GPS discipline of an ADC, DAC or radio oscillator.
Conversely, a GPSDO oscillator does not establish synchronization of PC clocks.

`Default` preserves the existing six-second epoch search setting and its complete
receiver bank. Selecting Default in any clock field resets all three. An explicit
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
Entering a smaller number cannot improve the timing evidence of an audio device.

The adjacent editable **Audio error** dropdown is independent of that triplet.
Its default is **30 ms per station**, interpreted as the assumed maximum residual
audio/radio timing error after compensating the driver-reported queue. It is
saved/exported as `--audio-error 30ms`; fractional ns/us/ms/s values greater than
zero and up to 60 seconds are accepted. Live's finite epoch-radius limit can
reject a larger combined window. Returning Clock sync to Default preserves this
field and restores ordinary playback/search behavior. Both peers must satisfy
the selected assumption; it is not a measured device specification. This control
does not change the audio buffer used to prevent underruns.

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
Both transmit presentation checks use the selected Audio error allowance.
Larger provider uncertainty is never clamped to that setting; insufficient
allowance or rate evidence prevents UTC-following playback. GUI and CLI hardware
transmission then visibly fall back to ordinary playback before private PCM or
source scheduling. The selected allowance is not enlarged or falsely satisfied.
Low-level callers remain strict unless they opt in and provide a status observer.
These are engineering assumptions, not IC-7100 specifications or measurements.
The clock is assumed stationary over the finite waveform. A longer waveform
can require silent rate preparation, capped at 120 seconds; inconclusive rate
evidence triggers the same visible pre-transmission fallback. A known impossible
allowance falls back immediately, without waiting through that preparation cap. Short waveforms
that fit the allowance at the declared rate limit need no extended preparation.
For example, a 48 kHz route with a 25 ms period and a 300 ppm correction cap
has only about 1 ms of conservative rate-error slack under a 30 ms tolerance.
That permits roughly 3.3 seconds without rate preparation. The endpoint bounds
cannot establish this cap within the 120-second preparation limit, so longer
transmissions on that assumed route can require ordinary-playback fallback. This is an analytical
limitation, not measured IC-7100 performance; smaller declared clock bounds,
finer timestamp evidence or a larger selected allowance change it. The GUI does
not silently replace the selected Audio error with a larger value.

After preparation, only the silent stream is stopped. Source construction and
fresh-epoch scheduling run while the device is stopped, followed by a fresh
device/UTC timeline. The private source advances once and never restarts. The
whole finite message is constrained to within 0.01 logical sample of its initial
affine trajectory, with arithmetic allowance reserved, as well as the UTC error
limit. This limits ongoing correction; it cannot follow arbitrary clock wander.
The rate-deviation cap is fixed from the complete finite duration, reserving
arithmetic error once. It does not shrink after rounding each callback's position.
Clock steps, lost continuity, a missed start or exceeded error allowance stop
timed playback; there is no fallback restart after private output begins. Nonblocking completion drains the actual buffered tail, with
cancellation checks, without replaying it.

The correction domain is separate from the oscillator description. For existing
effective-link bound A and correction cap C, the bank covers A+C+AC and retains
every original lane. If its complete union cannot fit the existing bank/passband
bounds, steering stays zero and the original bank remains. Shared RF-clock
topologies, Windows and hosted timing adapters retain their documented fallback.
Default clock mode preserves existing playback.

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
The FFT path keeps its legacy search until equivalent phase-dependent scanning
is implemented. This internal map is not a new persisted user setting.

Custom hardware mode combines both stations' selected GPS errors and propagation
region with the selected peer TX Audio error and the larger of that per-station
allowance and reported capture uncertainty. Thus a 30 ms selection adds at least
60 ms of link audio half-width, separately from GPS and propagation. Inverse-slope corner
bounds cover the complete initial phase interval. The GUI explicitly calls this
an **estimated audio/radio model**, even when capture timestamps themselves are
bounded. Capture uncertainty greater than the selected Audio error disables
pruning and retains the full search. Both stations must satisfy the selected
timing assumptions. A capture
timestamp alone cannot establish the peer's timing behavior.

The map narrows compact acquisition; FFT/coupled paths and missing/invalid timing
metadata keep the full fallback. The fixed propagation offset shifts the receive
epoch center and retirement time. Subsequent symbols retain all frequency/rate
lanes and the original physical completion rule. A cold listener inside a long
symbol can wait for the next symbol boundary; it does not recover unobserved PCM.
GUI simulation retains its full window because its random startup delay is not
a calibrated provider timestamp. Its known unsteered waveform uses the original
clock bank. Hardware estimates retain every peer correction-domain lane even if
local playback falls back: another transmitter may still be steered. The planner
reports timing-lattice work at a representative anchor only for the existing long
compact path when qualified capture metadata is available, and separately reports
the full-window fallback. Short FFT paths retain full scan cost. Tight clock
settings can therefore increase their estimate through extra clock lanes without
reducing their timing scan. These are engineering estimates, not measured speedups
or evidence of reduced reserved RAM. UTC/DSSS probability values are separately
labeled conditional matched-template AWGN references; device steering failure and
the outer-code presence guard are outside that model.

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
