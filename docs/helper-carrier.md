# Helper Carrier

Helper Carrier is an experimental independent two-tone reference for propagation
diagnostics and optional assistance to Robust reception, including operation
without GPSDO hardware. It can accompany an ordinary transmission or transmit
alone. Its product name does not classify an operation as an automatically
controlled radio beacon. Operators select configurations appropriate to their
frequency, jurisdiction, license, station control and other authorizations.
The software retains general-purpose power options; it does not impose a blanket
regulatory power limit or certify an installation's compliance.

## Current two-tone preview

Updated 27 October 2026. The application now generates a **90/10 power split**:
90% pure frequency/phase pilot and 10% auxiliary carrying punctuation and
optional metadata. These fractions describe combined Helper power during normal
straight-tone intervals, before ramps, metadata and channel response. The pilot
is nine times stronger in power, three times in amplitude and 9.54 dB above the
auxiliary. The pilot has no intentional phase reversals or metadata modulation.

The rebuilt manual-checkpoint GUI is
`build/dev/datapump-gui`. The initial application build and five focused checks
passed. The R7 GUI/CLI rebuild and three focused Session/stage/tracker checks
pass; broader qualification remains incomplete. The rebuilt
GUI and archive hashes are recorded in [validation](validation.md). Prior single-tone
HF, cable and EME results do not qualify this changed waveform. In particular,
the old strict cable suffix/prefix failures and incomplete EME runs remain open.

## Manual reception

Helper reception is manually enabled with positive Helper Carrier power. The
tracker discovers the strongest qualifying tone within the configured
oscillator-derived pilot range, bounded by actual capture geometry. There is no
artificial ±100 Hz cap. Quiet-spectrum operation assumes this strongest straight
tone is the intended pilot; auxiliary detection and metadata decoding do not
qualify frequency assistance or authenticate the transmitter.

The live path feeds one ordinary Robust receiver bank. Positive-passband input
uses upstream correction before payload filtering/downsampling. Folded real mono
retains its frequency-origin-only limitation; a real waveform folded around DC
cannot receive the same analytic correction. Measured dated residual RF windows
refresh existing candidates and new admissions. Unresolved audio-clock and
UTC/timing alternatives remain required. The helper-specific work-admission
estimate is conditional; it is not a measured total-process CPU ceiling.

The segmented pilot path uses a bounded FFT discovery window (8,192 samples at
48 kHz), then ten 20 ms Hann projections per independent 200 ms context.
Frequency-only numeric records, at most 192 spanning 36 seconds, fit a locally
linear oscillator trend. A bounded numeric fallback fits the current context
with a possible phase gap; it supplies frequency while phase authority remains
local. Unknown retune phase is not pooled across contexts.
The current measured tracker workspace is 403,016 bytes at 48 kHz;
this excludes the frontend and ordinary receivers. Dated segment interfaces can
map compatible residual measurements using a signed RF scale, with independently
unknown phase. Their component controls use known clock relationships; they do
not resolve arbitrary RF/audio-clock ambiguity.

Current source also contains local context-phase observations and a separately
funded, at most 400 ms raw staging FIFO. The integration must preserve sample
dates and every committed payload sample, including reference loss. Pooled
frequency history cannot manufacture earlier phase support or advance a processed
frontier. The R5 causal 40/60 dB-Hz controls reduce payload relative RMS to
0.03700/0.006529, while 60 dB-Hz retains ten frequency-interval escapes. A dated
four-RF laboratory control has RMS 0.005967 versus frequency-only 0.999442 and
zero escapes across 328 consumed-geometry checks. That control assumes USB,
zero audio-clock error and known retune dates. Scoring all helper-present input,
rather than only phase-supported samples, exposes 1.2–1.4 dB remaining coherent
loss; only 80.4% of energy has phase support. That loss check does not include
the payload notch or 128-second symbols. A bounded strong RF-only Session test
passes 73 independent-phase retunes, exact `001`, one pending identity and
physical END; it uses an exact audio clock, DATA SNR 40 dB and zero drift.
The source-band guard accepts +2.19 Hz and declines +142.7 Hz while retaining
the old NCO. Covered-feed job checks pass; independent-clock/supplied-HF and
ordinary 128-second completion remain unqualified. The application has no actual rig-retune adapter. Fake FHSS keeps physical RF fixed and does not prove
reception through hardware phase jumps.

Without a fresh qualified reference, new assisted acquisition may wait. Reference
loss disarms new acquisition; established ordinary reception retains draining,
recovery, per-bit progress, pending-row identity and physical-absence scoring.
There is no parallel broad receiver bank or iterative historical PCM search.
The ordinary Robust wire format, short dictionary endpoints, explicit binary
bits, fixed framing, long text, attachments and post-END source decoding remain
required. Neither Helper loss, metadata, EOF nor expected length completes a
message: fully scored consecutive absent-symbol durations must cover six seconds.
Optional Helper work yields before payload functionality.

Matched 128-second component apertures with the new optional phase bound have
all-sample corrected loss 0.0695/3.6321 dB at 30 dB-Hz continuous/random phase,
and 0.00627/0.27925 dB at 40 dB-Hz. The old 30 dB-Hz gate admitted no phase fits;
its rejection was a software limit. These fixed-RF diagnostics exclude the
payload notch and ordinary 128-second completion, and do not replace acquisition
or uncertainty-coverage requirements.

R5 component controls retain useful precision without calibrated confidence
claims. At 24 dB-Hz sampled pilot C/N0, composite availability is 97.45% static
and 96.39% random-hop, with final U near 0.222/0.250 Hz. The baseline has no
observed escapes across 1,551 estimates, but a 12-seed blind 30 dB-Hz ensemble
retains one escaped straight-fit estimate among 2,225. Twelve actual-waveform
cases have no observed escapes across 2,071 frequency estimates and 1,339 sampled
phase fits. Earlier coverage failures remain preserved in validation. Component
precision, mask counts and phase RMS are not whole-receiver CPU savings.

HF40 remains incomplete. R5 improves reference availability but yields no bits;
the cached rerun yields only one pending bit under a non-TX epoch, without END.
True-epoch work is still starved. Cache hits and descriptive hot-feed CPU do not
establish a causal speedup across different fresh keyed waveforms or make that
failure pass. This is a software receiver-work limit, not a physical precision
limit. The strong HF15k and Rate-360 fixed-RF
Fake-FHSS cases pass their existing eight-bit primary checks. HF15k signal 4's false/truncated
wire `01` remains a strict qualification failure. Independent review finds
signal 6's retained `11` is a permitted compatible-prefix correction adding the
correct third bit. The Rate-360 primary epoch differs
from TX despite an admitted true-epoch owner. Correct primary data does not
qualify all GUI rows or cable acceptance.

Audited HF15k cost is 0.228392 process CPU/audio with 4,627 FFT jobs; its primary
owner actually narrows 127→2 before its first FFT. Rate-360 costs 1.464287
CPU/audio with 29,106 FFT jobs and actual masks unchanged at 9→9; its folded-mono
route has no phase correction. There is no matched OFF CPU control, and worker
concurrency is untraced. These simulation results do not measure native backlog
or establish a speedup. The previous GUI group is 38/39 with a controller
SegFault. A partial-bank retirement guard is implemented and the original
crashing-subsection check and full selected controller/Planner/self-check repair
run pass. Final full groups remain pending.
A new full-composite N0CALL metadata component decodes 7/7 frames; earlier
failures remain retained. Offset/random-hop and Live metadata routing are still
in progress, and frequency assistance remains independent of metadata.
All three modeled GPSDO HF controls pass exact eight bits on one visible row
with ordinary END. The EME R5 run instead has zero owners/bits despite 99.9369%
pilot qualification and all 5,397 correction blocks certified: provisional compact
CPU estimates reject every candidate. Its U median is 0.084627 Hz at measured
pilot C/N0 30.0399 dB-Hz. This software gate is not measured receiver throughput
or a physical link limit. The EME GPSDO control is also incomplete, capturing
only 2.73067 seconds with no full symbol or END. A separate pre-PCM constructor
check exposes workspace shortfalls at both 83 and 89 pairs; it measures no
throughput. A narrower exact-lattice reservation saves only 38,880 bytes and
still cannot fit either constructor; it was rejected without production edits. The reviewed native serial-row cache passes focused
exact-score/acquisition comparisons and R6 build/selected CTests; full groups
and throughput benefit remain pending. Full regression, cable and intended-hardware qualification remain pending. See
[validation](validation.md) for commands, preserved failures and measured limits.

## Controls and use

The Helper Carrier row is below Clock sync and the oscillator controls. Power
defaults to **0**, disabling transmission and the remaining helper controls.
Power remains editable as a finite, nonnegative number of watts. Presets include
15 kW, 10 kW, 1.5 kW, 100 W, 40 W and the existing Robust power presets. Invalid
edits retain the last valid setting and prevent transmission until corrected.
Ratio limits are 10, 20, 40, 60, 80 and 90 dB, with 90 dB the default. High is the
only placement. Low and 0 Hz have been removed. Saved GUI settings/imports
normalize retired values to High; new textual CLI values other than High reject.

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
preserves the requested helper/data ratio without changing the intrinsic Robust
source waveform. Composite headroom and physical RF calibration are accounted
separately below.
A ratio measured over a whole helper dwell can be slightly lower than its steady
tone calibration because ramps and metadata reduce average power, with further
differences from the physical route's frequency response.

The order is **Robust data → guard → auxiliary modulation → guard → pure
pilot**. The auxiliary center starts 450 Hz above the upper shaped data edge,
increasing in 50 Hz steps only as required by the power-dependent digital leakage
bound. The pilot is 400 Hz above the auxiliary. The auxiliary's nominal ±150 Hz
band leaves a 300 Hz data guard and a 250 Hz pilot guard at minimum placement.
The bound uses the actual shaped/gated data bandwidth and both tones' power,
ramps and auxiliary transitions. Tone-center separation alone is not the
isolation argument. No logical-sample-rate floor moves the group arbitrarily far
away.

The radio/audio path must pass the complete group with margin below steep filter
attenuation. Digital Nyquist margin does not establish an SSB radio passband.
The selected Robust Rate, carrier, logical sample rate, framing and target SNR
stay unchanged; a configuration that cannot pass the adjacent group is unavailable
for Helper assistance.

The two tones' possible voltage sum requires more peak headroom than the old
single tone. At unchanged audio volume, the native calibrated data gain is lower
by approximately 1.353 dB for 40 W Helper/4 W Robust and 1.989 dB for 15 kW/4 W.
Recalibrate the physical RF chain to retain the configured powers. This is an
audio-headroom change, not a requested payload-power reduction. Sampled simulation
applies the same data gain to signal and noise, preserving payload SNR and the
configured RF budget. Ramps/metadata and channel/filter gains are separate
received-power effects.

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
it does not silently remove an in-band helper.
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
180 dB path loss and -164 dBm/Hz noise, the total Helper budget is 30.02 dB-Hz
and pure pilot is 29.56 dB-Hz before relevant waveform/channel gains. Neither is
the level necessarily present in a physical loopback cable.

Predicted C/N0 does not grant lock, narrow the receiver by itself or change
payload SNR, Rate or PHY probability. Runtime uses the actual measured interval.
A bright reference can still leave broad uncertainty through ramps, rejected
observations, oscillator evolution, measured excess or interference. The following detector's
short-window level gate is different from SNR in a 10 Hz presentation bandwidth.
No calibrated acquisition probability or globally optimal estimator is claimed.

Helper-only lead and tail surround the unchanged ordinary waveform. The lead
baseline reserves at least two seconds plus the cold acquisition/frontend
aperture and source/capture clock allowance. The tail retains at least two
seconds and any greater filter, initial-FFT, pulse-support and physical-absence
obligations. The shared lead predictor seeks a frequency half-width of **0.1 Hz**
by adding complete 400 ms dwells, up to **36 extra seconds** beyond the baseline.
The forecast separates pilot C/N0 after the split, independent coherent
apertures, configured phase noise and estimator/model assumptions. At the cap,
payload starts with achievable precision; elapsed airtime grants no runtime
qualification. The ±0.1 Hz target is a lead-planning milestone, not a sufficient
bin width for every configuration, including 128-second symbols. Tail duration
never uses expected message length as completion authority.

For pilot C/N0 in linear Hz, the ideal uninterrupted noise-only three-times-CRLB
standard-deviation scale is

`S3(T) = 3 sqrt(3 / (2 pi² (C/N0) T³))`.

It is a theoretical comparison, not an operational confidence guarantee; see
[JPL Publication 88-21, A Comparison of Frequency Estimation Techniques for
High-Dynamic Trajectories](https://ntrs.nasa.gov/api/citations/19890011016/downloads/19890011016.pdf).

| Pilot C/N0 (dB-Hz) | Ideal ±scale at 1 s (Hz) | At 10 s (Hz) |
| --- | --- | --- |
| 40 | 0.0117 | 0.000370 |
| 30 | 0.0370 | 0.00117 |
| 20 | 0.117 | 0.00370 |
| 10 | 0.370 | 0.0117 |
| 6 | 0.586 | 0.0185 |
| 3 | 0.828 | 0.0262 |
| 0 | 1.17 | 0.0370 |
| −3 | 1.65 | 0.0522 |

For a common constant frequency with an independent unknown phase per aperture,
replace `(C/N0) T³` by `sum_i((C/N0)_i T_i³)`. Thus 25 ideal 400 ms apertures
at 30 dB-Hz give about ±0.029 Hz, rather than the uninterrupted-ten-second
±0.00117 Hz. Ramps, rejected contexts, drift, clock ambiguity, local model fits
and estimator inefficiency need additional treatment. Practical measured
intervals and their observed escapes remain separately reported in validation.

The ordinary baseline remains visible. Planner shows total Helper and pilot
C/N0, ideal uninterrupted and segmented references, practical model prediction,
baseline lead, capped precision extension, residual RF uncertainty and unresolved
clock/timing work. Conditional initial and sustained receiver prices use the
existing receiver-cost model, not frequency-interval ratios, and remain distinct
from discovery, tracking and total helper-assisted runtime cost. The current
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
out-of-band isolation depends on the actual filtering geometry. Bright-reference phase-noise
sidebands, DAC distortion and RF-chain dynamic range can still limit data
reception. Disabling the helper preserves ordinary transmit/channel arithmetic.
Simulation tails contain actual modeled noise long enough for the original
receiver aperture and absence scoring; neither EOF nor synthetic filter flushing
supplies physical END. No general sensitivity or throughput qualification follows
from digital isolation or a single controlled payload result.

## Dwell waveform and metadata

Every dwell is exactly 400 ms on the physical calendar, including when FHSS
is off. Entry and exit ramps occupy 4 ms at each boundary and affect both tones.
Only the auxiliary has one shaped reversal, over 236–244 ms and crossing zero at
240 ms. It stays inverted through the rest of the dwell, including metadata,
and resets while amplitude is zero at the boundary. There is no unintended
full-amplitude boundary reversal. The pilot remains unmodulated apart from
necessary start/end and dwell/hop ramps.

Partial dwells omit punctuation unless its required support fits. Graceful
streaming stop protects a reversal already begun: helper-only continuation can
extend to 268 ms, retaining a 20 ms inverted straight flank and final 4 ms ramp.
This adds no Robust bits or progress and does not supply physical END. Immediate
abort can truncate output. Earlier paired-reversal spectrum measurements apply
to the superseded single-tone format, not this waveform.

Metadata decoding is optional and does not qualify the pure-pilot frequency
reference. Its intended readability is primarily nearby or directly in the
antenna beam; distant assistance need not support metadata decoding.

Metadata uses APSK-2097152 at 125 baud, with only the symbols needed for the
selected identifier or time value, on the auxiliary only. A frame contains one
to seven symbols and ends at 388 ms; its start is `388 - 8 * symbol_count` milliseconds. The return to
the auxiliary's applicable underlying straight-tone state completes at 396 ms,
before the exit ramp. A maximum-length
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

[Validation](validation.md) retains current focused observations and separately
bound historical failures. The current 20 dB-Hz component declines qualification;
a short-window gate, poor bootstrap or inefficient aperture is not proof of a
physical precision limit. The actual-waveform matrix and seeded uncertainty
escapes must be resolved before operational interval claims. Capture passband,
real-mono ambiguity, unresolved RF/audio-clock relationships and actual rig
retuning are separate limitations.

Complete supplied HF and 5.8 GHz EME acceptance, causal CPU comparisons, strict
cable identity/prefix behavior, complete long text/attachments, payload progress,
fully scored END, measured receiver savings and current contract/native/sanitizer
coverage remain outstanding. Component acquisition and precision do not complete
those requirements. This is a manual experimental preview, not release
certification.
