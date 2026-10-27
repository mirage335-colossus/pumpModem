# Helper Carrier

Helper Carrier is an experimental independent reference tone for propagation
diagnostics and optional assistance to Robust reception, including operation
without GPSDO hardware. It can accompany an ordinary transmission or transmit
alone. Its product name does not classify an operation as an automatically
controlled radio beacon. Operators select configurations appropriate to their
frequency, jurisdiction, license, station control and other authorizations.
The software retains general-purpose power options; it does not impose a blanket
regulatory power limit or certify an installation's compliance.

## Manual reception

Helper reception is manually enabled with positive Helper Carrier power. Within
its configured oscillator prior, the tracker selects the strongest qualifying
stable tone. This assumes the intended helper is the dominant tone in otherwise
quiet spectrum; tone presence does not authenticate its transmitter. Frequency
search still runs over the measured residual uncertainty.

The live path feeds one ordinary Robust receiver bank. A positive-band signal
uses a continuously retuned upstream NCO. For a real mono waveform that folds
around DC, the frontend preserves PCM and its fixed helper notch instead of
applying an invalid analytic shift. That case uses the measured helper to center
the ordinary receiver's frequency origin; full upstream frequency correction is
unavailable. The displayed centering must not be read as phase correction.
There is no parallel broad bank, periodic broad-probe route or iterative search
through old PCM.

While assistance is enabled without a fresh qualified reference, new payload
acquisition pauses. Reference loss disarms new acquisition; already accepted
ordinary receivers retain their payload, recovery and physical-absence scoring.
Neither helper loss, helper metadata nor EOF completes a message. Helper-off
reception retains its ordinary policy. All message bits use the unchanged Robust
wire format and full receiver: short text, raw bits, long framed text,
attachments, decompression, recovery, next-poll accepted-bit progress and the
same pending row through physical END. Reception ends only after fully scored
absent-symbol durations cover six seconds. Optional helper work cannot impose a
smaller source quota or a raw-bits-only decoding mode.

For a new long-symbol compact candidate, the typed ordinary constructor is
invoked with the actual full residual catalogue and bytes remaining after wrapper
costs. Its specific shared-pulse workspace-limit outcome leaves the candidate
waiting without failing transmission; retry requires an observed UTC second to
advance. This is an actual constructor outcome, not a separate public preflight. This is not an arbitrary frequency-precision
threshold or permission to reduce ordinary framing, source quota or Rate.

Supported manually enabled Automatic/keyed long-symbol shared-pulse candidates
also have work-based admission. A new candidate waits when its supported finite
provisional serial-work estimate exceeds the selected receiver CPU objective;
there is no fixed Hz gate. Existing/started owners, helper-OFF or forced-strategy
operation, unsupported shapes and unknown estimates keep their existing policy.
The shared-pulse caller's serial work is not discounted by extra search workers.
The estimate interface requires explicit helper-policy opt-in; helper power or
a supplied catalogue alone does not select it. This engineering admission model
is not a measured CPU ceiling or a demonstrated EME throughput cure.

When qualification is lost, an already observed first ordinary symbol may
continue to its next ordinary decision, including its pending cell. Rejected
cold work retires at that decision. This bounded continuation does not admit a
new broad search or change accepted-owner lifetime, timing, emission, bit
progress or physical-absence closure. It is not a second receiver or a historical
PCM search.

Across inactive helper authority, the ordinary receiver now retains its already
paid numeric lookups while immediately revoking their frequency authority. This
preserves the paid cache (5,152 bytes in the measured control), not a reference lease, extra receiver or permission
to invent EOF/END. Shared/segmented partial-first-symbol loss controls preserve
exact `01`, pending identity, all retained bit polls and ordinary physical END.
The short matched cache discriminator improves cost at unchanged work counters;
it does not update the latest full EME throughput result.

The tracker combines an independent short FLL with a bounded phase/frequency
fit. Each joint fit uses ten actual 20 ms complex projections; at most 240
numeric anchors span 36 seconds. Saved mixer coordinates, finite-window noise
and shared-endpoint/process covariance remain part of the calculation. This is
numeric observation history, not a search through stored PCM. The FLL and phase
fit qualify independently; neither metadata nor a known transmit date supplies
measurement authority. Tracker storage is fixed and charged to its actual
workspace query. The joint interval retains a 0.005 Hz engineering allowance;
that is not a numerical rounding term. The consumer applies the actual measured
interval rather than restoring the original broad RF width.

The precision model is conditional on locally linear frequency evolution plus
the configured Brownian phase process. Three abrupt-change controls still fail:
step, slope and clock changes produced 6, 19 and 5 escaped qualified polls.
They are not hidden by successful steady/drift controls. Current evidence does
not establish all initial oscillator-prior coverage, arbitrary thermal/Doppler
evolution, calibrated confidence or a future coherence lease.

The measured RF window is continuously refreshed in the same ordinary receiver.
It uses actual dated uncertainty, with only a numerical floor and the original
clock/UTC geometry required by the configured source relation. A tone does not
universally resolve audio-clock error. Under a validated shared-radio relation,
frequency can condition the common source-clock interval; independently
qualified 400 ms punctuation cadence can also constrain that interval. Consumers
intersect the original configured LF domain and retain the required cells. Clock
loss restores the original LF search; it does not rewrite PCM, epochs or already
accepted bits. Queued input may use only a fresh clock measurement with matching
source/rate provenance. This is not a second receiver or a PCM time warp.

Punctuation supplies clock-rate evidence, not an epoch or timecode. Waveform
phase assistance is currently inactive/declined: NCO phase continuity is not a
measurement of the transmitter's absolute phase. Callsign/timecode transmission
and standalone decoding exist, but metadata decoding in this Live manual route
is not qualified and is not required for frequency assistance. Fake FHSS uses a
fixed physical radio frequency; actual hardware retuning/hopped-helper following
is unsupported in this preview.

R16 remains an experimental preview. The current built GUI is
`571664fa0faa8bb849165698b57ff6327717c37cd9ba180b2eb295289350f1c5`,
with core prefix `3148dd7b`. Application/focused builds and four focused checks
passed. Repeated helper-enable requests now avoid rebuilding dispatch control
when both permissions are already true; frequency-window changes and numeric
pushes still prepare their own closure. A matched eight-bit component retained
exact per-push prefixes, identity, pending status and physical END. This avoids
duplicate closure work but supplies no measured throughput gain. Earlier R13
focus/R15 full `simulation_estimate` passes remain separately bound, not fresh
full-contract, native or sanitizer coverage. A helper-specific grid union preserves the complete
declared clock domain; its component reduced 69 pairs to 35 without narrowing
that domain. Bounded trace coalescing avoids the preceding attachment trace-cap
failure. These mechanisms do not qualify an EME link or establish a CPU gain.
The rejected arbitration proposal was not integrated; strict cable failures
remain. Available full-case measurements below are bound to R11, not fresh R16
runtime passes.

This closeout keeps the existing application path. A future two-tone design
must keep a separate pilot continuously unmodulated; it is not implemented in
the application.

| Area | Handoff state |
| --- | --- |
| Application | One ordinary bank/NCO, measured RF/LF narrowing, ordered lookup cache and work admission; ordinary payload contracts unchanged. |
| [Isolated pilot reference](../tools/pilot-reference/README.md) | Standalone CW-only estimator and short reproduction test; 11 component cases passed. No app/CMake, transmitter, metadata, audio-device or ordinary receiver integration. |
| Old metadata-depth and exact-seed-cache experiments | Stopped or isolated; current waveform/metadata behavior unchanged. |

The pilot component's engineering intervals assume a constant or locally linear
carrier trend within 0.03 Hz/s and the configured Wiener phase model. They are
not calibrated confidence intervals or future precision leases. Its dated
measurements, memory/work accounting and remaining gaps are documented with the
[check results](validation.md). The component does not fix the strict cable or
EME failures below, demonstrate receiver CPU savings or complete the feature.

Current manual GUI:
`/home/user/___quick/p/_cur/dataPump/pumpModem/build/dev/datapump-gui`.
The controls below describe the operator settings; measured controlled cases
retain their separately bound profiles, often fixed 2 GiB rather than automatic
50% workspace.

R11 controlled HF reception passed exact eight-bit source/wire, retained
prefixes, the same pending identity, complete TX/channel drain and ordinary END
with no input health fault. Actual +3,000 Hz CFO and +100 ppm audio-clock error
were applied to helper and DATA before decimation. Whole-process CPU/audio was
0.26449; complete body ordinary-feed intervals measured 0.17676. Measured raw
helper C/N0 median was 48.9042 dB-Hz, separate from the 55.7609 RF-budget
prediction and 49.7403 sampled reference. Different dates, waveforms and media
endpoints prevent a strict speedup claim against earlier trials. An R11
matched OFF/ON attachment check also passed all 2,432 wire bits, authenticated
17-byte source, prefixes, pending identity and physical END. That small framed
case does not qualify full-size attachments or 1,024-byte text.

Both actual R11 cable controls failed their strict checks. Default and
GPSDO-configured runs eventually completed the correct eight bits on ID 3, but
an earlier seven-bit suffix on ID 2 and aliases remained unexpected receptions.
Final correctness does not erase those failures. Input health counters were
zero. Whole-GUI CPU/audio was 0.42886/0.38494, with mean measured helper C/N0
53.908/55.413 dB-Hz against the configured 30.0206 prediction. The cable did not
inject the RF path loss/noise. Both helper-ON controls had different measured
C/N0; their costs are descriptive, not a causal hardware-GPSDO speedup. They do
not qualify the weak RF budget, GPSDO substitution or functional acceptance.

The latest normal EME result is now R11 and remains incomplete: 300.612 wall
seconds/301.331672 CPU seconds for 95.44 captured audio seconds, no bits/END and
unfinished TX/channel drain. Complete body ordinary-feed intervals cost 3.94552
CPU/audio, excluding lead. RF masks updated 79→73→63 pairs; a stale mask is not
shown. The longest qualification island was only 2.36 seconds. This result
advanced less audio than R8 and establishes no grid/cache work benefit or full
128-second reception. The 1.907 dB PHY deficit remains untested; the new R13 gate has no full EME
result. The precision model still fails the
abrupt step/slope/clock controls (6/19/5 escaped polls). Rejected private precision
variants are estimator evidence, not fundamental physical limits. Larger framed
text, attachments, full contract/native/sanitizer and complete EME qualification
remain incomplete. Phase assistance is inactive, actual hardware hops are
unsupported and Planner CPU predictions remain uncalibrated. See
[validation](validation.md) for individual bindings and retained failures; no
release certification or full feature completion is claimed.

## Controls and use

The Helper Carrier row is below Clock sync and the oscillator controls. Power
defaults to **0**, disabling transmission and the remaining helper controls.
Power remains editable as a finite, nonnegative number of watts. Presets include
15 kW, 10 kW, 1.5 kW, 100 W, 40 W and the existing Robust power presets. Invalid
edits retain the last valid setting and prevent transmission until corrected.
Ratio limits are 10, 20, 40, 60, 80 and 90 dB, with 90 dB the default. High is the
default placement. High, Low and 0 Hz are functional for output, raw helper
observation and physical simulation, subject to their physical band geometry.
0 Hz is an overlapping diagnostic choice and is discouraged.

The independent callsign accepts at most 19 printable seven-bit ASCII
characters. Editing Developer Callsign copies a valid value to the helper.
Helper edits never modify Developer Callsign or the Robust draft. Clear-mode
invalid or overlength identifiers are rejected without truncation. Callsign edits apply to
future transmissions without replacing live receivers or altering active PCM.
Without encryption, a nonempty callsign takes precedence over clear timestamp
metadata; an empty callsign selects the timestamp. With a station encryption key
selected, the helper ignores the stored callsign and sends encrypted time.
The GUI hides the helper callsign field while encrypted; its saved preference
returns when encryption is disabled. An inactive stored callsign is ignored even
if invalid; it cannot block encrypted operation. Switching to clear mode restores
callsign validation. The generator can also omit optional time
metadata while retaining the tone, but never exposes the stored callsign in
that encrypted mode.

**Transmit** mixes the helper with data; **Transmit noise** mixes it with the ordinary
temporary-key noise waveform. **Helper** sends the reference alone, without data
or tuning noise. The GUI **Helper** action is available in Developer mode;
ordinary mixed transmission does not require that diagnostic action. These
modes also operate in Live simulation. Existing stop/abort controls apply. The
helper continues during Symbol_Burst waits and scheduled data nulls. It bypasses payload DSSS, Interleave and waveform
time-shifting streams. It uses the existing physical dwell calendar; current
Fake FHSS still does not tune radio hardware. Timestamp encryption uses the
selected station key, including during Transmit noise, rather than that action's
temporary payload key.

Robust message payload bits are never transmitted on the helper waveform. They
remain in the ordinary low-power Robust transmission. The helper carries only
its tone, punctuation and optional callsign or timestamp. An assisted receiver
is an ordinary Robust receiver guided by those measurements: it retains long
text and attachment framing, decompression, recovery, per-bit progress and the
six-second fully scored physical-end rule. If the required resources cannot fit,
optional assistance yields rather than imposing a reduced decoding mode.

Power controls establish a calibrated relative audio ratio, not a radio-control
command. The amplitude ratio is the square root of the power ratio. When a ratio
limit is reached, effective helper power is reduced and displayed; configured
Robust power is retained. Linear composite normalization reserves headroom, and
the operator calibrates the RF chain for the intended helper/data levels.
Robust's target SNR remains referenced to its low-power data component.

The helper also follows any existing digital amplitude backoff of the selected
Robust waveform. Interleaved V2 DSSS already applies a data amplitude factor
`g = 0.5` (6.02 dB in power). Applying that factor to the helper component alone
preserves the requested helper/data ratio; this calibration correction does not
change Robust PCM, the composite data gain or the existing noise calibration.
A ratio measured over a whole helper dwell can be slightly lower than its steady
tone calibration because ramps and metadata reduce average power, with further
differences from the physical route's frequency response.

High and Low use the closest placement that satisfies the digital waveform's
conservative periodic leakage bound at the effective power ratio. Placement uses
the actual shaped occupied data bandwidth, plus any data gating allowance, rather
than treating the nominal Rate as an unshaped rectangular band. The initial
helper-center distance from the data edge is 450 Hz; it increases in 50 Hz steps
as needed for the leakage bound. There is no additional High floor tied to the
logical sample rate. Hardware FHSS counts its data gate allowance once; it does
not add a duplicate 200 Hz allowance during output preparation. Larger power ratios and the paired-reversal leakage allowance can still
require substantial separation. Low needs enough space below the data band for
that guard and the helper band.
**0 Hz** means the same audio center as the Robust carrier, not DC. Its intentional
overlap can strongly interfere with data and is discouraged.

The selected Robust Rate, carrier, logical sample rate and target SNR stay
unchanged. There is no automatic Rate reduction to fit the helper: 100 Hz,
3.6 kHz SSB and 12 kHz selections all retain their geometry. The startup
3.6 kHz passband failure was a nominal-versus-shaped-bandwidth calculation
error; correcting that geometry requires no Rate reduction. The radio and audio
route must pass the complete data, guard and helper bands; closest digital placement does not guarantee a fit in a voice
SSB filter. Software cannot widen that hardware passband or move the data band
to make Low fit.

Native and browser playback generate the helper after output resampling.
Helper-enabled audio requests a separate physical clock, with a practical
48 kHz floor and enough bandwidth for the selected helper, while Robust's
logical DSP clock and symbol geometry stay unchanged. Live/duplex observes raw
capture before payload downsampling and scheduled capture exclusion. If a
provider cannot supply the requested higher capture rate, helper observation
yields during setup and ordinary capture is retained. This fallback occurs
before PCM/format acceptance; later errors retain the original capture gap
handling.

Current mixed Live fallback can disable the helper for a helper-specific
provider-rate failure or past helper epoch before PCM delivery, reporting the
reason visibly. Ordinary output retains calibrated `data_gain`, the same logical
PCM, wire and key epoch. It does not restore full-scale data, which would violate
the operator's RF calibration. A provider setup failure can be retried once
without the helper rate request only before the first PCM callback.

Validated `calibrated_data_gain` is now separate from all placement geometry.
Placement or minimum-output-rate `Error` disables the helper before PCM while
retaining calibrated ordinary PCM. A valid FHSS engineering gate allowance may
cross DC without making ordinary transmission unusable; calibration no longer
requires a cochannel placement plan. A helper-only request fails
if its helper cannot emit, and malformed powers/settings still reject. Actual
output gaps and other ordinary transmission errors retain their normal failures
without starting a replacement transmission.

WAV helper output uses at least 48 kHz and four times the logical rate, enlarged
when the selected helper requires it. For helper WAV reception, specify the
same logical modem geometry and helper profile with `--helper-power`: `rx` and
`status-rx` observe metadata before conversion and payload scoring. Adjacent
helpers inside the logical capture band remain real interference there. Capture
anti-alias filtering rejects a helper outside that band when the geometry allows;
it does not silently remove an in-band High or Low helper.
CLI payload reception can try the same real adjacent-helper bandstop filter
described below. Its first output keeps the original FIR-center sample coordinate;
it changes neither the source epoch nor the capture date. The full ordinary
receiver and original PCM remain the fallback when this optional attempt accepts
no payload bits. EOF never flushes synthetic filter samples.

## Helper presence and receiver lock

A detected helper creates a **helper pending** entry even when no callsign or
timecode decodes. Presence and synchronization are separate: a pending helper
entry does not mean that frequency correction, marker-clock masking or Robust
payload decoding is active. A continuously present tone stays pending; elapsed
presence does not complete it. After at least six seconds of contiguous captured
absence, the same helper entry moves to the received log. Missing capture,
wall-clock waiting and provider gaps do not count as observed silence. Brief
fades retain the entry's identity. The real-time detector's level and freshness
gates are engineering checks, not a calibrated 99% lifetime-confidence claim.
Terminal diagnostic updates can be retried without taking storage from ordinary
payload progress.

The entry labels current or last measured C/N0 and peak C/N0 in dB-Hz, alongside
SNR in a 10 Hz reference bandwidth in dB and the dated capture observations.
`SNR(10 Hz) = C/N0 - 10 dB`; these are different units, not two estimates of the
same bandwidth. Metadata, if decoded, is advisory information. Helper entries
contain no message bits, grant no payload copy/save permission and neither hold
an ordinary reception open nor supply its physical END. The ordinary Robust
receiver still needs complete scored absent-symbol durations covering six
seconds before completing payload reception.

## Link Planner and lead time

Link Planner separates the helper reference budget from the ordinary payload.
For effective helper power `P` watts, path loss `L` dB and noise density `N0`
dBm/Hz, received helper power is `30 + 10*log10(P) - L` dBm and expected C/N0
is that result minus `N0`. Its 10 Hz presentation reference is C/N0 minus 10 dB.
A 4 W data/40 W helper setting has a 10 dB calibrated reference ratio. At
180 dB path loss and -164 dBm/Hz noise, that helper budget is 30.02 dB-Hz;
it is not the level necessarily present in a physical loopback cable.

Predicted C/N0 does not grant lock, narrow the receiver by itself or change
payload SNR, Rate or PHY probability. Runtime uses the actual measured interval.
A bright reference can still leave broad uncertainty through metadata gaps,
oscillator evolution, measured excess or interference. The following detector's
short-window level gate is different from SNR in a 10 Hz presentation bandwidth.
No calibrated acquisition probability or globally optimal estimator is claimed.

Helper-only lead and tail surround the unchanged ordinary waveform. The lead
baseline reserves at least two seconds plus the cold acquisition/frontend
aperture and source/capture clock allowance. The tail retains at least two
seconds and any greater filter, initial-FFT, pulse-support and physical-absence
obligations. The shared lead predictor seeks a frequency half-width of **0.1 Hz**
by adding complete 400 ms dwells, up to **36 extra seconds** beyond the baseline.
The forecast uses forty closed 10 ms phase-interval bins per 400 ms dwell with
the current joint kernel. Its pure forecast check passed: at C/N0 30 dB-Hz it
predicts an initial 0.0979 Hz half-width at 8.8 seconds and 0.01882 Hz at the
38-second cap. A separate noisy control measured roughly 0.180/0.117/0.031/
0.019 Hz at 10/20/30/38 seconds. Predictions remain conditional on accepted
observations and the stated model; they do not qualify lock probability,
initial full-prior coverage or arbitrary channel evolution. The R6 Planner check passed in 4.119 seconds under the unchanged deadline;
its preceding 90-second timeout remains recorded. These checks do not price
all helper-assisted runtime work. At the cap, payload starts
without claiming that the target was reached; actual fresh receiver qualification
is still required. Tail duration never uses expected message length as completion
authority.

The ordinary baseline remains visible. Conditional initial and sustained receiver
prices must be distinguished from total helper-assisted runtime cost. The current
forecast/pricing update is not a calibrated overall CPU forecast: tracking,
conversion, audio, transmit and GUI work are separate. The configured 0.6 receiver
budget is an Automatic backend-selection objective in modeled receiver seconds
per audio second, not a hard ceiling on whole-GUI CPU. Geometry, central-carrier
redistribution or fewer pairs alone does not establish a throughput gain.

## Physical simulation

Helper-enabled sampled simulation generates the helper at a separate physical
sample rate and applies bounded anti-alias conversion before the unchanged
logical modem input. Data and helper share their sample-clock error, carrier
offset and oscillator phase trajectory. The simulation exercises the same manual
tracker/ordinary-receiver route; enabling a helper does not confer synchronization
or waive its actual measurement gates.

Physical AWGN preserves the original in-data-band noise spectral density and
the existing `modem::nominal_signal_power` convention. Interleaved V2 DSSS has a
6.02 dB digital power backoff: data C/N0 is the nominal reference minus 6.02 dB,
and the calibrated steady helper adds the requested helper/data ratio to that
data reference. The simulator does not reduce noise or boost Robust PCM to
make assistance succeed. Physical cable tests do not inject the configured RF
path loss/noise and therefore cannot qualify the corresponding weak RF budget.

An adjacent helper inside the logical capture band remains real interference;
out-of-band isolation depends on the actual filtering geometry. Cochannel
placement retains its intentional overlap. Bright-reference phase-noise
sidebands, DAC distortion and RF-chain dynamic range can still limit data
reception. Disabling the helper preserves ordinary transmit/channel arithmetic.
Simulation tails contain actual modeled noise long enough for the original
receiver aperture and absence scoring; neither EOF nor synthetic filter flushing
supplies physical END. No general sensitivity or throughput qualification follows
from digital isolation or a single controlled payload result.

## Dwell waveform and metadata

Every dwell is exactly 400 ms on the physical calendar, including when FHSS is
off. Entry and exit ramps occupy 4 ms at each boundary. Paired smooth C2
reversals cross zero at 240 and 280 ms, over 236–244 and 276–284 ms. The first
creates a 32 ms inverted plateau; the second restores the original running
carrier by 284 ms. They do not reset carrier phase or change Robust DATA.
Partial dwells omit the pair when its complete support cannot fit.

Graceful streaming stop protects a pair already begun: helper continuation with
zero payload padding can extend to 308 ms, retaining restored pure carrier
through 304 ms before the final 4 ms ramp. This adds no Robust bits or progress
and does not supply physical END. Immediate abort can truncate output.

In an exact digital full-dwell punctuation-only spectrum, restoring the carrier
changed the central-line fraction from 4.085% to 64.306% (11.93 dB of spectral
redistribution). This is not added helper power or payload/link gain. Across the
measured full-dwell metadata cases, less than 0.053% of energy lay outside
±150 Hz. These are digital waveform controls, not RF/DAC bandwidth or reception
qualification. Placement's leakage bound accounts for both reversals and can
require a larger nearest guard than the earlier waveform. Metadata framing and
Robust power, Rate and wire formats remain unchanged.

Metadata uses APSK-2097152 at 125 baud, with only the symbols needed for the
selected identifier or time value. A frame contains one to seven symbols and
ends at 388 ms; its start is `388 - 8 * symbol_count` milliseconds. The return to
straight tone completes at 396 ms, before the exit ramp. A maximum-length
callsign therefore occupies seven symbols, 56 ms; timestamps need at most three
symbols, 24 ms. Transitions use quintic interpolation in the complex plane. The
high ten bits of each 21-bit symbol select one of 1024 radii
`0.9 + 0.1 * index / 1023`; its low eleven bits select one of 2048 phases
`2*pi*index/2048`. The alphabet contains exactly 2,097,152 points.

The compact helper format replaces the earlier experimental fixed eight/nine-
symbol frames. This is a deliberate helper-format change; Robust's ordinary
wire format remains unchanged. All fields are MSB first:

| Kind | Header and transmitted contents | Canonical symbol count |
| --- | --- | --- |
| Callsign | Type `00` (2 bits), actual character count `N` (5), `N` seven-bit ASCII characters | `ceil((7 + 7*N) / 21)`, `0 <= N <= 19`; at most 7 |
| Clear time | Type `01` (2 bits), `B - 1` (6), `B` unsigned time bits | `ceil((8 + B) / 21)`, `1 <= B <= 48`; at most 3 |
| Encrypted time | Type `10` (2 bits), `B - 1` (6), `B` encrypted time bits | `ceil((8 + B) / 21)`, `20 <= B <= 48`; at most 3 |

Type `11` is reserved. The callsign has no unused character slots or NUL padding.
The symbol count is determined before adding an optional checksum. If at least
seven spare bits already exist in the canonical frame, they contain CRC-7/MMC:
polynomial `0x09` (`x^7 + x^3 + 1`), initial value zero, no reflection and no final
XOR. The CRC covers every header and transmitted-content bit, MSB first, before
the checksum; for encrypted time it covers ciphertext. Remaining spare bits are
zero. If fewer than seven bits are spare, no checksum is added. CRC never adds
a symbol or airtime. Noncanonical counts, invalid lengths, nonzero padding and
incorrect present checksums are rejected.

A timestamp describes the nominal physical dwell start, not payload symbol
zero or the wall clock at each audio callback. Its unsigned Q16 Unix value is
`ticks / 65536` seconds, up to 32 whole-second bits and 16 fractional-second bits,
with nominal resolution about 15.26 microseconds. Clear time uses
`B = max(1, bit_width(ticks))`; encrypted time uses
`B = max(20, bit_width(ticks))`. Times outside the 48-bit range omit the timestamp
while retaining the tone. Callsign and timestamp are never transmitted together;
encryption always suppresses the stored callsign.

Encrypted time uses the length-preserving FF1 mode from NIST SP 800-38G: AES-256
forward encryption, radix 2, ten rounds, an empty tweak and integer arithmetic.
The 20-bit minimum gives a domain of at least 1,048,576 values, consistent with
the larger minimum domain in the
[NIST revision draft](https://csrc.nist.gov/pubs/sp/800/38/g/r1/2pd).
The 32-byte AES key is the selected station key's HMAC of ASCII
`datapump/helper-time/v2/ff1-key`. FF1 transforms exactly the declared `B` time
bits; there is no authentication tag or extra authentication symbol. The minimum
20-bit domain can widen very small encrypted timestamps, while current Unix
values still fit the three-symbol maximum. Payload and calendar key streams are
unchanged. Cipher unavailability/failure rejects encrypted output before PCM;
it never silently substitutes clear time.

All helper metadata is unauthenticated, including encrypted timestamps. CRC
provides only limited transmission-error detection. Length-preserving encryption
does not reliably detect a wrong key: it can yield a plausible but incorrect
time value. The receiver uses only the first selected receive key for encrypted
time rather than selecting a key by timestamp plausibility. Decoded times never
set an internal UTC correction or qualify GPS synchronization. Metadata also
provides no freshness, replay protection or proof that the tone and a separately
received Robust payload share a transmitter. The code and independent vectors
specify the compact mapping; the format alone does not establish identification
compliance for every emission, band or radio installation.

## CLI examples

```
pump tx --text hello --output helper.wav --helper-power 1.5kW --helper-data-power 4W --helper-callsign N0CALL
pump listen --bw 200 --carrier 500 --helper-power 100W --helper-callsign N0CALL --helper-only --seconds 30
pump listen --simulation '3dBm -60dB' --helper-power 100W --helper-only --seconds 30
pump simulate --text hello --helper-power 100W --helper-callsign N0CALL --json
pump rx --input helper.wav --helper-power 1.5kW --helper-data-power 4W --json
```

`--helper-ratio-limit`, `--helper-frequency` and `--helper-callsign` are included
in GUI command import/export. `--helper-only` is a CLI listen action and requires
positive helper power and no outgoing message input. Helper-enabled CLI
simulation can save actual physical-rate capture with `--output`. WAV RX and
simulation JSON include a `helper_carrier` object with presence, frequency and
optional metadata. When an admitted helper is present but one-shot Robust
acquisition fails, `status-rx` and `rx --json` still emit helper diagnostics;
the original payload error and failure exit status remain. Plain-text RX reports
helper presence on stderr in that case. Live helper events include a display
description. Generated WAV separation tails remain real quiet capture after both components end;
simulated tails contain the configured physical channel noise.

## Evidence and remaining qualification

[Validation](validation.md) binds each result to source, binaries, settings and
original failures. Current R16 component precision and digital-spectrum controls
do not reuse preceding cable, full-matrix or sanitizer results as current
qualification. Abrupt-change failures remain explicit. No whole-process CPU,
false-alarm, interference-tolerance or initial full-prior guarantee is inferred
from a narrow measured interval.

C/N0 20 and 24 dB-Hz are below the current short-FLL software qualification gate;
this is an implementation/estimator limit, not a physical proof that weak EME
assistance is impossible. Capture passband and real-mono image ambiguity impose
separate geometry limits. Full 128-second EME, larger framed text, attachments and
broader helper-loss/normal/native/sanitizer acceptance remain pending. This is a
manual experimental preview, not release certification.
