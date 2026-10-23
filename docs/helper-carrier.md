# Helper Carrier

Helper Carrier is an experimental independent reference tone for propagation
diagnostics and optional assistance to Robust reception, including operation
without GPSDO hardware. It can accompany an ordinary transmission or transmit
alone. Its product name does not classify an operation as an automatically
controlled radio beacon. Operators select configurations appropriate to their
frequency, jurisdiction, license, station control and other authorizations.
The software retains general-purpose power options; it does not impose a blanket
regulatory power limit or certify an installation's compliance.

## Controls and use

The Helper Carrier row is below Clock sync and the oscillator controls. Power
defaults to **0**, disabling transmission and the remaining helper controls.
Presets include 1.5 kW, 100 W, 40 W and all existing Robust power presets. Ratio
limits are 10, 20, 40, 60, 80 and 90 dB, with 90 dB the default. High is the
default placement. Low and 0 Hz remain selectable but are currently unavailable
in the integrated Robust path pending separate isolation qualification.

The independent callsign accepts at most 19 printable seven-bit ASCII
characters. Editing Developer Callsign copies a valid value to the helper.
Helper edits never modify Developer Callsign or the Robust draft. Invalid or
overlength identifiers are rejected without truncation. Callsign edits apply to
future transmissions without replacing live receivers or altering active PCM.

**Transmit** mixes the helper with data; **Transmit noise** mixes it with the ordinary
temporary-key noise waveform. **Helper** sends the reference alone, without data
or tuning noise. Existing stop/abort controls apply. The helper continues during
Symbol_Burst waits and scheduled data nulls. It bypasses payload DSSS,
Interleave and waveform time-shifting streams. It consumes the existing physical
dwell calendar; current Fake FHSS still does not tune radio hardware.

Power controls establish a calibrated relative audio ratio, not a radio-control
command. The amplitude ratio is the square root of the power ratio. When a ratio
limit is reached, effective helper power is reduced and displayed; configured
Robust power is retained. Linear composite normalization reserves headroom, and
the operator calibrates the RF chain for the intended helper/data levels.
Simulation scales noise from the original payload reference, preserving its
target SNR rather than treating composite power as data power.

High placement automatically increases separation with the effective power
ratio, using the digital waveform's conservative periodic leakage bound. It also
places the helper above the logical payload sample rate plus 250 Hz, so the
existing capture converter rejects it before Robust scoring. This wider guard
is deliberate: a closer strong tone corrupted an actual sampled payload despite
meeting the nominal spectral mask.
Placement includes occupied data bandwidth and gating allowance. If a safe
placement cannot fit, reception continues with the helper observer unavailable.
An enabled-helper transmission is rejected before payload PCM rather than
falling back to unscaled data, preserving the operator's relative power
calibration. CLI file generation likewise rejects that configuration before
writing a file. Turn helper power to zero for ordinary transmission. **0 Hz**
means zero offset from the Robust carrier, not DC audio;
the choice is retained but reports unavailable pending co-channel interference
qualification. This is a signal-preservation restriction, not a regulatory cap.
The radio and audio route must pass the resulting helper frequency. For example,
a 6 kHz logical rate requires at least a 6.25 kHz helper center, outside many
voice-radio filters. A lower original modem carrier/rate can make a narrowband
experiment fit; the software does not widen a radio's hardware passband.

Native and browser playback generate the helper after output resampling, so
Robust's logical sample rate is retained. WAV helper output uses at least 48 kHz
and four times the logical rate. For helper WAV reception, specify the same
logical modem geometry and helper profile with `--helper-power`: `rx` and
`status-rx` then use the existing capture converter before payload scoring.

The current equal-rate Live simulation cannot represent this independent physical
band and capture preselection. It reports helper assistance unavailable while
ordinary Robust simulation continues; helper-only simulation is rejected before
queuing a transmission. It does not raise Robust's rate, introduce a new receive
filter, or feed the known contaminating composite to the data decoder.

## Public callsign waveform, version 1

Every dwell is exactly 400 ms on the existing physical calendar, including when
FHSS is off. The steady tone has constant amplitude and phase. A quintic entry
and exit ramp occupies 4 ms at each dwell boundary. Fixed-end WAV generation
can include a single smooth amplitude reversal from 236 to 244 ms, passing
through zero at 240 ms. It is omitted in partial dwells unless at least 20 ms
of clean tone fits on either side. Live and other streaming output omit this
optional punctuation so a newly requested stop cannot change prior phase.
Transmission start/end also use independent 4 ms ramps, multiplied by any
overlapping dwell envelope.

With a nonempty callsign, eight APSK-2097152 symbols begin at 324 ms at 125 baud.
They finish at 388 ms, then return smoothly to the tone by 396 ms. Transitions
use quintic interpolation in the complex plane. The exit ramp finishes at
400 ms. A finite transmission can end during a dwell; its final envelope
tapers to zero without adding payload bits or delaying payload completion.

The public metadata frame is 168 bits, MSB first:

| Field | Bits |
| --- | --- |
| Callsign character count, 0 through 19 | 5 |
| Nineteen seven-bit ASCII characters, NUL padded | 133 |
| CRC-16/CCITT-FALSE over twenty bytes: count byte followed by padded characters | 16 |
| Zero padding | 14 |

Divide the frame into eight 21-bit words. The high ten bits select one of 1024
radii `0.9 + 0.1 * index / 1023`; the low eleven bits select one of 2048 phases
`2*pi*index/2048`. Thus the nominal alphabet has exactly 2,097,152 points.
`metadata_symbols()` and `apsk_point()` implement the public mapping.
The code and independent test vectors permit external implementations.

Successful metadata decoding is optional. This implementation transmits the
callsign but does not implement its dense-constellation receiver, encrypted time
code, or receiver-clock discipline from metadata. An empty callsign produces a
pure reference tone with ramps and optional punctuation. Callsigns and timestamps are
never combined. Tone detection and frequency observation do not depend on
metadata decoding. The format alone does not establish identification compliance
for every emission, band or radio installation.

## Receive observations

With helper configuration enabled, a separate raw-capture observer operates
before payload downsampling and scheduled capture exclusion. It uses bounded
100 ms coherent fits. Admission starts with four fits per evidence group;
group length grows logarithmically with observation history so weak arrivals
after a long idle period receive more evidence. The displayed helper
SNR is excess tone power relative to noise in a 10 Hz fit cell; it is distinct
from Robust's target SNR. The estimated excess must be at least 6 dB. The
finite-sample Gaussian noise gate spends a total false-admission budget of 1%
over searched frequency cells and repeated observations between resets. Once
admitted, the 6 dB level gate maintains presence; six seconds of observed
absence retires admission, and a later arrival must pass the confidence gate
again. This
is a conditional noise-model guarantee, not authentication or a posterior
probability that an arbitrary interferer is the intended helper.

Tracking covers at least +/-20 Hz and up to +/-100 Hz according to oscillator
search requirements; wider requirements retain the complete Robust search.
Both peers currently need matching helper power, data-power reference, ratio
limit, side selection and Robust bandwidth/sample-rate geometry, because the automatic
separation depends on their effective power ratio. The receiver does not yet
discover an unknown helper anywhere in the audio passband. Callsign content
does not affect placement. The short/long local profiles share the larger
occupied bandwidth so a helper keeps its frequency across those transmissions.
Unknown helper metadata is displayed as undecoded. Helper presence creates a
stable **helper pending** row. Six seconds of fully observed qualifying
presence confirms the same row as **helper received**; a shorter observation
can also finish after six seconds of observed absence. Brief fades retain the
row. Missing PCM resets evidence and never counts as observed silence.
Helper rows do not grant payload copy/save permissions or manufacture bits.
Repeated helper updates coalesce, and payload events reclaim helper queue space
before any ordinary event can be displaced. A full payload queue drops helper
updates rather than losing unpolled Robust progress.

The offset is available for diagnostics and manual alignment. Robust's original
search, hypothesis order, bounds, evidence penalties and active data tracks
remain unchanged. Merely reordering its present FFT bank does not reduce work:
that path scores the complete cohort before admitting signals. Automatic search
centering, phase discipline and CPU-ceiling improvements are deferred until a
receiver-level implementation preserves detection and fallback guarantees.
A tone alone cannot separate RF error, Doppler and sample-clock error, or
determine absolute UTC.

The observer's storage fits within proven spare capacity in the existing
sampled-channel reservation, including overlap during mode transitions.
Enabling the helper does not reduce the modem bank or the existing audio/monitor
reservations, including full-duplex playback.

## CLI examples

```
pump tx --text hello --output helper.wav --helper-power 1.5kW --helper-data-power 4W --helper-callsign N0CALL
pump listen --bw 200 --carrier 500 --helper-power 100W --helper-callsign N0CALL --helper-only --seconds 30
pump rx --input helper.wav --helper-power 1.5kW --helper-data-power 4W
```

The options `--helper-ratio-limit`, `--helper-frequency` and `--helper-callsign`
are also included in GUI command import/export. `--helper-only` is a CLI listen
action and requires positive helper power and no outgoing message input.
Generated WAV separation tails remain actual silence after both components end.

## Evidence and remaining qualification

See [validation](validation.md) for actual checks and limits, and the
[implementation plan](helper-carrier-plan.md) for design decisions. Digital
spectral tests, matched disabled-path tests and conditional Gaussian-noise
fixtures do not qualify physical DAC/RF distortion, phase noise, dynamic range,
real-time hardware throughput, or near-threshold detection probability. Nominal
16-bit conversion does not by itself prove useful reception at a 90 dB ratio.
Future metadata decoding, wider coarse acquisition, phase discipline and search
pruning require separate evidence and must yield to Robust preservation.
