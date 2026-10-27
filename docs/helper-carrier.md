# Helper Carrier

Helper Carrier is an experimental independent reference tone for propagation
diagnostics and optional assistance to Robust reception, including operation
without GPSDO hardware. It can accompany an ordinary transmission or transmit
alone. Its product name does not classify an operation as an automatically
controlled radio beacon. Operators select configurations appropriate to their
frequency, jurisdiction, license, station control and other authorizations.
The software retains general-purpose power options; it does not impose a blanket
regulatory power limit or certify an installation's compliance.

## Manual reception preview

The current preview replaces the earlier conditional-bank design with an
upstream tone tracker feeding one ordinary Robust receiver bank. Positive-band
HF uses a continuously retuned NCO. A wide real waveform that folds around DC
instead preserves PCM with the fixed helper notch and sets the ordinary first
demodulator's frequency reference from the measured helper offset plus the
data/helper separation times each candidate's fractional audio-clock error.
This avoids analytically shifting an already folded real payload. Folded-mono
operation is explicitly frequency-origin-only; full upstream correction is
unavailable there. The current preview applies RF masks to work in the existing
ordinary bank, retaining its original LF values/cells. Direct residual-clock
mapping uses the data/helper separation `D − H`. There is no parallel/broad bank.
Reception assistance is manually enabled. While enabled without
qualified lock, it admits no new payload acquisition. This route does not launch
broad fallback/probe banks or replay historical receiver capsules. The ordinary
payload format, full text/attachment decoding, accepted-bit progress and
physical-END rules remain required.

Cold capture can use four exact-adjacent, mutually consistent fine
observations to recenter the NCO only. They do not enter fit history or grant
lock: the ordinary 16-observation/0.3-second qualification and all subsequent
model/freshness gates remain unchanged.

The bounded tracker uses a 4,096-point FFT seed and a 20 ms FLL, with up to
384 recent numeric frequency observations spanning at most eight seconds. A
separate precision refinement retains at most 256 disjoint coherent-segment
summaries over 36 seconds; it retains no corresponding raw-PCM history. Each
segment uses up to 16 adjacent frequency differences with their actual shared
phase-endpoint covariance. Weighted fits over the full, newest-half and
newest-quarter apertures add a measured smooth-evolution bias allowance, along
with oscillator-process, residual/excess-noise and numerical terms. The current
short FLL independently vetoes inconsistent frequency or slope. Refinement does
not create lock, extend its lifetime or guarantee future thermal behavior or
vehicle Doppler tracking. Tracker storage is fixed and charged through its
actual workspace query; the raw-PCM ring is unchanged. The 0.2-second frequency
expiry remains.

Its optional clock-rate measurement uses the repeated 400 ms phase reversal, at
most 16 numeric markers over six seconds and about 96 ms of recent phasors.
Marker projection uses an 8 ms Hann window in eight staggered 1 ms branches,
with exact overlap/noise bounds and an absolute 80 ms duplicate-crossing guard.
While the clock chain is freshly qualified, candidate crossings are checked
against all integer 400 ms aliases under the broad ±1% operational bound and
both dated radii. Live intersects the original configured LF domain separately.
Off-cadence events are rejected without changing dates or intervals.
That additional gate expires after the existing 550 ms freshness limit, leaving
ordinary reseeding unchanged.
The real-PCM image model accounts for both images. This measures clock rate,
not epoch, phase or timecode, and needs no GPS or known transmit date. It does not
iteratively reprocess old capture. The recent-observation FIFO now avoids
reusing stale observations after wrap. Frequency variance accounts
for measurement covariance while retaining model/excess-noise floors; this is
not a new global confidence guarantee. Earlier standalone checks cover tone
drift, punctuation, noise, loss and sparse updates; source-bound precision
results remain in [validation](validation.md). They do not promise coherence
over a 128-second symbol.
An independently qualified clock interval can intersect the original LF cells
and RF strip in the same ordinary receiver, retaining the full residual
catalogue. Frequency qualification remains independent. New input without a
fresh clock retains full LF coverage; already captured queued input may retain
its own dated clock measurement. The actual receiver backend and calendar
geometry determine whether clock masking is supported; eligible native/compact
paths can include long symbols and fake-FHSS. Unsupported shapes retain all original LF cells. This eligibility
is not qualification of 128-second EME reception. Software-generated helper and
DATA use the same corrected source clock, including its UTC steering. Measured
marker spacing therefore describes that combined DATA rate; the individual
physical-audio and UTC factors remain unresolved. Its clock-rate interval is an
aperture-mean estimate under a locally stationary model, not a future stability
lease. There is no PCM time warp or new decoder.
Waveform phase correction and epoch/timing metadata assistance remain inactive.
A queued row may refresh only its CLOCK metadata from a fresh, dated raw-clock
measurement in the same input generation/rate and original LF bounds. Valid
queued clock is retained if no current clock is available; queued RF, NCO, PCM,
phase, epoch and processing frontiers do not change. This is not another decoder,
new staging or a retired-row replacement policy.

The last physically measured preview GUI has hash prefix `4d0a71d0`. Eight
focused checks and five targeted GUI/Session/scope modes passed. Its same-GUI two-cycle cable run
completed both exact one-bit payloads with no input health faults. ID 1 first
appeared already complete; ID 2 was observed pending then complete on that ID. Total GUI CPU/audio was 0.7620, peak
backlog 3.125 seconds and final zero. The observed raw-clock qualification and
CLOCK-only queued-row refresh leave RF/NCO and the processing frontier unchanged.
The preceding three-cycle run recovered all three payloads but failed its
300-second deadline before the third helper tail; its separate reentry peak
backlog was 18.125 seconds. Counts/tracing differ, so this is not a universal
performance ratio. The reported intermittent missing pending row after the
helper tail remains unreproduced, not declared resolved. The same-GUI quiet
GPSDO/helper-ON eight-bit run failed: a transient wrong seven-bit prefix preceded
correct final data/END. That is not a functional pass; diagnosis is pending.
The quiet crystal/helper-ON eight-bit run passed with correct pending prefixes
and ordinary END at 0.5947 total GUI CPU/audio overall and 0.6465 in the body,
peak backlog 0.275 seconds and final zero. The failed GPSDO/helper-ON run used
0.6833 overall and 0.7533 body CPU/audio; correct final data does not erase its
prefix failure.

The configured 0.6 receiver budget is an Automatic backend-selection objective
in processing seconds per audio second, not a hard total-GUI process-CPU
ceiling. These whole-process costs do not isolate ordinary receiver work;
earlier strict-ceiling interpretations were incorrect, while raw measurements
remain retained. Both current quiet cases used automatic 50% workspace rather
than fixed bytes and different keyed dates/waveforms. Their median reported
helper C/N0 was about 49.07/49.87 dB-Hz, roughly 19 dB stronger than the configured
30.02 dB-Hz RF budget. Software loss/noise was not injected into the cable.
No universal causal CPU gain, weak-link or GPSDO-substitution claim follows.
Earlier failures remain in validation. Broad qualification is
explicitly deferred; long text, attachments and 128-second EME are unqualified
this iteration.
The common corrected-source mapping retains the complete original DATA-clock
and UTC prior without adding the same UTC source-rate term twice to residual RF
uncertainty. A separate physical-helper relation retains its conservative UTC
allowance, and the original shared-radio zero-UTC case remains. A current
source-handoff defect is unresolved: the prediction and presentation changes do
not establish current-build payload/prefix/END or cable qualification. The
earlier `7bafc96a` trial is a separately bound precursor, not the delivered
binary's performance result.
The previous receive architecture below is superseded
and its historical evidence does not qualify this route. See
[validation](validation.md) for the current build and check status.

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
not add a duplicate 200 Hz allowance during output preparation. The reported
EME geometry consequently plans a 2,250.3125 Hz High helper center. That is a
placement result, not EME reception qualification. Larger power ratios can still
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

## Link Planner reference budget

Link Planner shows the helper separately from the ordinary payload: effective
power after the ratio ceiling, received power, expected tone C/N0 (numerically
SNR in a 1 Hz reference bandwidth) and the required audio center. With effective
helper power `P` watts, path loss `L` dB and noise density `N0` dBm/Hz, its received power is
`30 + 10*log10(P) - L` dBm and C/N0 is that result minus `N0`. For the separate
10 Hz presentation reference, tone SNR is C/N0 minus 10 dB. The planner's current
editable Robust transmit power supplies the ratio-limit reference.

These hardware-budget values use the operator's measured/calibrated RF transmit
power: 4 W of data and 40 W of helper mean a 10 dB reference ratio, including
when the waveform has digital backoff. They are distinct from the sampled
simulator's nominal signal-power convention below.

For a 4 W data setting, the default 90 dB ratio limit and -164 dBm/Hz noise:

| Helper power | Path loss | Helper C/N0 | Tone SNR in 10 Hz |
| --- | --- | --- | --- |
| 40 W | 180 dB | 30.02 dB-Hz | 20.02 dB |
| 1.5 kW | 180 dB | 45.76 dB-Hz | 35.76 dB |
| 1.5 kW | 205 dB | 20.76 dB-Hz | 10.76 dB |
| 1.5 kW | 210 dB | 15.76 dB-Hz | 5.76 dB |

The 10 Hz SNR column is a presentation reference, not the real-time tracker's
lock gate. Its 20 ms Hann following windows have a different equivalent noise
bandwidth; following requires measured tone power at least four times the
estimated noise in that window, together with cadence, innovation, coherence,
model and freshness checks. The weak examples cannot be declared usable from
this table. No predicted C/N0, SNR or lead duration qualifies synchronization or
changes the payload's C/N0, target SNR, Rate or reception-probability curve.

Runtime RF narrowing uses the actual dated helper uncertainty `U`, with only a
1e-6 Hz numerical floor, rather than retaining the original oscillator RF
width after lock. The residual catalogue keeps the full original DATA-clock
prior and maps it through the data/helper separation `D - H`. Software helper
and DATA share their corrected source clock, so individually unknown audio and
UTC factors are not counted twice. Frequency alone does not narrow that clock
prior. Passband, catalogue and workspace limits still apply; oscillator details
report when the requested full peer UTC-rate union cannot be represented.

The shared transmit/Planner lead predictor now uses the bounded clean-segment
kernel above. Inputs include effective straight-tone C/N0, physical capture
rate, the configured relative oscillator phase process, source/capture clock
ratio, actual callsign-symbol count or timestamp allowance, punctuation and
dwell ramps. Sampled simulation uses its channel phase-process setting. The
forecast excludes an expected one-second seed/loop warmup and evaluates twenty
sampled capture alignments through the known dwell gaps. It assumes accepted
clean segments, zero measured excess and the stated smooth-evolution model;
these sampled alignments are not a guarantee for every capture boundary,
unknown interference, glitch or later oscillator change. Actual rejection and
measured excess can produce a wider interval or no lock.

Helper-only lead and tail surround the unchanged ordinary waveform. The
lead baseline reserves at least two seconds, the cold helper aperture and
frontend lookahead, with the source/capture clock bound. The tail retains at
least two seconds and any larger FFT/filter, pulse-support and physical-absence
obligations across the configured receive profiles. The predictor seeks a
frequency half-width of **0.1 Hz**, adding complete 400 ms dwells up to **36 extra seconds**
beyond that baseline. The extension is separate from the source/capture clock
ratio. For a supported forecast, Planner shows target reached or cap reached
with the achievable predicted half-width; at the cap, transmission starts its
payload without claiming the target was met. Unsupported forecast geometry is
explicitly declined and retains the physical baseline. Runtime acquisition
still waits for an actual fresh qualified helper measurement regardless of
elapsed lead. Tail planning retains physical drain/absence support and never
uses expected payload length to declare END.

Link Planner preserves the ordinary baseline and shows a separate conditional
frequency-only ordinary-receiver CPU curve using the predicted residual
catalogue. It retains full LF/UTC, arrival, keyed-phase and epoch work, required
receiver storage, ordinary payload SNR, Rate and PHY probability. No marker-clock
gain is inferred from tone power. The prediction is a conditional engineering
estimate, not measured acquisition latency, lock probability or received
sensitivity. The former ideal uninterrupted 384-observation calculation and its
103-pair example do not describe the current forecast or normal metadata
precision.

Expanded details separate finite receiver frontend, search, kernel,
continuation, payload and startup/other prices from continuous-listening
cohort/epoch work. Setup diagnostics are already included, not added again.
Complete totals are withheld when the underlying component work is unsupported;
workspace eligibility is reported separately. Discovery/tracking/conversion,
post-lock lead-in receiver work, audio, transmit and GUI processing remain
unpriced additive costs, not available zero-cost terms. There is no parallel
broad fallback/probe bank in manual helper mode. Configured DSP allowances and
backend checks are reported; peak rolling-bank storage is not qualified.

The ordinary rolling-cohort lifetime model was corrected from 47.51 to 78.8
seconds, changing a reference estimate from 0.093589 to 0.183126 processing
seconds per audio second. A fixed-256 MiB actual-receiver two-hop regression
checks that lifetime; it is not total CPU calibration. Neither fewer catalogue
pairs nor these receiver-model curves establish an end-to-end CPU, backlog or
sensitivity improvement. The recorded cable and full-payload limitations above
and in [validation](validation.md) remain.

## Physical simulation

The existing 80 KiB channel/plot reservation includes 16,640 bytes for fixed
helper observation and presentation controls, including the two-record END
retry owner. Optional helper cohort allowances are 6,912 bytes for the legacy
channel and 9,984 bytes for the helper-enabled physical channel. The combined
fixed-plus-cohort shares remain 23 KiB and 26 KiB respectively. These are helper
reservations within the same total; ordinary receiver grants and payload PCM
storage are unchanged. Optional cohort work may decline when its smaller
allowance cannot cover its actual storage.

Helper-enabled sampled simulation uses a separate physical sample rate, at least
four times the original logical rate and no lower than the helper's physical-rate
floor, with bounded multistage anti-alias preselection before the unchanged
logical decoder input. Data and helper share the same sample-clock error, carrier offset and oscillator phase trajectory.
Physical AWGN is scaled to preserve the original in-data-band noise spectral
density relative to data power. The FIR transition band slightly reduces total
logical noise variance; it is not normalized upward at the expense of payload SNR.

Sampled simulation retains its existing AWGN reference to
`modem::nominal_signal_power`, including the waveform's digital backoff. If
`C_nom` is C/N0 under that nominal reference and `R` is the helper/data power
ratio in dB, interleaved V2 DSSS has data C/N0 `C_nom - 6.02 dB` and calibrated
steady-helper C/N0 `C_nom + R - 6.02 dB`, before helper dwell/route losses. Their
reference difference is `R`; helper C/N0 is not simply `C_nom + R` in this
case. The simulator does not change its noise standard deviation to cancel
that existing data backoff or silently boost Robust reception.
Centered filter lookahead preserves logical time coordinates and finite source
extent. Live phase updates wait until the committed processing frontier reaches
their measured raw samples, even when physical simulation has generated bounded
lookahead. The separate bounded logical-PCM pre-roll described below reprocesses
retained samples without applying a later phase correction backward.

The simulator rejects geometry that cannot preserve the data passband or fit
the chosen tone. An adjacent High or Low helper inside the logical band remains
in the sampled mixture. If an out-of-band helper would alias into occupied data,
the capture anti-alias filter must provide the required isolation. Co-channel 0
retains its overlapping interference. The optional assisted receiver's separate
bandstop frontend is described below; the physical channel does not cancel the
tone.
Disabling the helper preserves the ordinary transmit waveform and channel sample
arithmetic. Finite Live simulation supplies real channel noise after transmission
long enough for the resident native FFT receivers' initial windows to be scored.
At source completion it snapshots their origins and conservative initial input
spans once; this can extend reception beyond the usual absence tail with either
zero or positive helper power. It does not extend physical transmission, invent
absent symbols or declare payload END. A bright helper's phase-noise sidebands
remain real interference in the physical model; the 90 dB ratio limit does not
guarantee an adequate phase-noise floor, DAC
linearity or RF dynamic range. Reduce helper power or improve the physical
oscillator/chain when those effects dominate. No general sensitivity or CPU
qualification follows from the digital isolation checks.

## Dwell waveform and metadata

Every dwell is exactly 400 ms on the physical calendar, including when FHSS is
off. Most of the dwell is constant-amplitude, constant-phase tone. Quintic entry
and exit ramps occupy 4 ms at each dwell boundary. A single smooth amplitude
reversal runs from 236 to 244 ms, passing through zero at 240 ms. It fits only
when at least 20 ms of clean tone precedes and follows it. Transmission start/end
also have independent 4 ms ramps, multiplied by any overlapping dwell envelope.

Fixed-end output can omit punctuation in a partial dwell. Streaming output
protects a reversal already begun when a graceful stop is requested: if needed,
zero payload padding with the helper continuing extends through 268 ms of that
dwell, retaining clean tone through 264 ms and a final 4 ms ramp. This does not extend Robust bits,
advance payload progress, or change physical completion. Immediate abort can
truncate the output. Finite boundary dwells can contain incomplete metadata.

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

## Previous receive observations and assistance — superseded

The following describes the preceding observer/conditional-bank route for
interpreting historical results. It is not the manual tracker/NCO preview above.

Presence admission remains independent of metadata decoding and modem assistance.
A bounded tracker uses 100 ms coherent fits. Evidence groups start with four fits
and grow logarithmically with observation history. Estimated excess tone power
must be at least 6 dB above noise in a 10 Hz fit cell. A finite-sample Gaussian
gate spends a total 1% false-admission budget across searched frequency cells
and repeated observations between resets. Once admitted, the 6 dB level gate
maintains presence; six seconds of observed absence retires admission.
This is conditional on the noise model, not authentication or a posterior
probability that an arbitrary interferer is the intended helper.

Tracking covers at least +/-20 Hz and at most +/-100 Hz according to oscillator
search requirements. Peers currently need matching helper power, data-power
reference, ratio limit, side selection and modem bandwidth/sample-rate geometry,
because automatic separation depends on their effective ratio. Unknown full-band
helper discovery is not implemented. Callsign content does not affect placement;
short/long local profiles share the larger occupied bandwidth.

An admitted tone creates a stable **helper pending** row, even without metadata.
Six seconds of observed qualifying presence confirms the same row as **helper
received**; a shorter observation can finish after six seconds of observed
absence. Brief fades retain its identity. Missing PCM resets evidence and never
counts as observed silence. Decoded callsigns/times appear in the helper row;
undecoded metadata does not hide the tone. Rows grant no payload copy/save
permissions, manufacture no bits and never hold a Robust reception open.
Repeated helper updates coalesce, yielding queue space to ordinary payload
progress.

A separate bounded precision receiver estimates frequency, phase and the repeated
punctuation's modulo-400 ms timing from raw samples. Tone uncertainty includes
an eight-standard-error local least-squares term using actual finite Gram
covariance, numerical floors and measured model allowance. These are local
noise/model estimates, not deterministic guarantees under interference or future
oscillator drift. Metadata decoding fits the actual smooth APSK transitions and
is deliberately optional: this dense constellation needs a very clean, strong
helper. A 1.5 kW/4 W mixed signal can provide useful tone references without
passing metadata quality checks. Nominal 16-bit output and a large power ratio
do not promise decoded metadata. At lower C/N0, useful frequency precision can
require averaging across several dwells. Frequency-only observations do not
qualify phase or punctuation timing; each must meet its own measured bounds.
The planner's predicted level alone does not establish that acquisition latency
or precision.

A separate frequency-only path fits 144 ms windows against raw physical PCM,
using their exact finite Gram matrices and a fixed frequency grid. Disjoint
raw-time windows also form deterministic evidence groups: fixed-grid F-test
probabilities combine by Fisher's method, with allowance spent across grid cells
and completed groups. Group accumulation continues on its raw-time schedule even
when a strong reference makes another precision fit unnecessary. Under the
white-Gaussian model, the single-window, group-admission and noise-variance
allowances together are at most 1% per reset. When qualified dwell timing places
the entire observation inside straight tone, local model bias can use centered
finite-bin moments, including the real-carrier image, Taylor remainder and
half-sample date offset. Unresolved windows keep the original conservative
model; the CW cancellation bound does not apply across punctuation or metadata.
Unstable bias feedback leaves refinement unavailable.
Combining windows doubles phase to handle possible
punctuation sign changes and retains compatible frequency-cell unions rather
than selecting an arbitrary unwrap. The refinement examines at most 4096 cells;
if the prior cannot be covered within that limit, its interval stays wide.
Refined intervals remain local statistical/model estimates, not a new lifetime
confidence guarantee. A frequency-only reference has no phase/timing authority.
Narrow frequency and newer strong phase observations retain independent dates;
a newer phase publication does not make an older frequency measurement current.
Practical latency is seed/profile dependent: the 24 dB-Hz simulator-backoff case
may yield few or no sufficiently narrow references, despite passing the tone's
level gate. Actual observations and limits belong in [validation](validation.md).

The wide-capture and compact-receiver corrections are under development and have
not completed end-to-end qualification. The preceding build's 441-input overlay
`87513574` passed its application build, recorded in
`local-evidence/helper-v4/candidate-v8-application-build/execution-result.json`
(hash begins `0fe66ddd`); the GUI hash begins `8f2af1f4`. Records are
`candidate-v8-source-bindings.json`, `candidate-v8-auxiliary-source-bindings-r1.json`
and `candidate-v8-binary-bindings.json` in the same evidence directory.
Current lifecycle focus passed three sampled/control functions and a deterministic
rolling pair, in `candidate-v8-lifecycle/focused-checks-r1/execution-result.json`
(hash begins `b555072b`). Current full normal passed 381/381 registered cases
exactly once and all 24 Fast CLI cases, without omissions/limits and with valid
provenance/cleanup audits. Current sanitizer focus passed 15 named, all 24 Fast
CLI and three gap-focused checks; 364 other headless cases remain unselected,
without full-suite qualification. Current Native FLTK passed 3/3, and current
physical cable text96 strictly passed full wire/source and ordinary END with
large backlog. Attachment129 also passed its full 900/910-second window;
HF8 cable completed exact data and physical END, but its strict full-pending-prefix
audit failed and remains limited. The current controlled work and exact-HF8 simulation
results have the narrower scopes below; broader performance, remaining cable
and weak EME proofs remain pending.

Earlier v7, `f47fa339`/`e2b951fc` and `a469ce48` results are historical, without an
equivalence or full-coverage bridge to current source. Their normal 379 pass,
sanitizer 366/377 with eleven failed/incomplete, v7 normal failures and earlier
payload/work/cable measurements retain their original scope. See the [plan](helper-carrier-plan.md)
and [validation](validation.md) for bindings and limits.

In the intended quiet licensed or underutilized spectrum, without an unrelated
or deliberate adjacent transmitter, a qualified helper may serve as a conditional
common-link frequency reference. Tone presence alone does not establish that
association or authenticate a peer. Retaining ordinary interference tolerance
is a validation requirement; bounded fallback alone does not prove equivalence.
All message bits still use the full ordinary Robust receiver: short/raw/long
text and attachments, configured source quotas, framing, decompression,
recovery, next-poll accepted-bit progress and fully scored six-second physical
completion remain required. Helper loss or metadata never supplies payload END.

Within the configured helper search range, assume the strongest stable constant
tone is the helper. Its straight-tone measurements alone can qualify frequency
narrowing. Punctuation decoding is only for independently qualified dwell timing
and phase assistance; callsign/time decoding is optional information. Neither
is required for frequency assistance. Optional drift tracking uses the declared
relative oscillator model and must not add a moving-vehicle Doppler allowance.

For supported Automatic or Complex native rolling reception, one ordinary
executor per row processes helper-conditioned acquisition batches first across
current and newly admitted keys, profiles and private epochs. It retains the
original catalogue, start alternatives and real observations needed for broad
audits; it does not create a second payload decoder or choose one guessed epoch.
Frequency search still runs within the conditional strip. Fixed-epoch reception
retains its separate legacy helper path. Energy, differential, unsupported
frontends/backends, keyed symbols of at least 60 seconds using compact reception,
and scheduled/Burst geometries decline this optional cohort and retain ordinary
reception. Nondeferred Fake: FCC can retain its existing gated template, subject
to actual eligibility; it does not tune radio hardware.

The 2D mask uses measured uncertainty, helper-to-data frequency separation,
explicit original/UTC-expanded physical audio-clock domain `A` and authoritative
`beta = clock_step_ppm/2`. A tone cannot separate RF error, Doppler and clock
error. No adjacent-centre or heuristic beta substitutes for missing provenance.
Selected original RF cells plus nearest audio-clock ties must cover the entire
mapped confidence strip across closed clock slabs using RF interval unions.
Every original audio-clock value remains. Uncovered support, missing provenance
or unsupported floating-point arithmetic declines the optional mask. Selection
tries zero origin first, then the measured baseline origin; a nonzero origin can
align a full tiny catalogue without reducing pairs. Report alignment and
narrowing separately. The original phase convention and unconditional
`search_limited` flag remain unchanged.

Temporary selector setup arrays use about 19.5 KiB plus the existing 8 KiB mask.
Measured static frames were 19,920 bytes versus the old 1,248, plus an 8,464-byte
caller; these do not bound the whole call chain or fit all storage in 7/16 KiB.
Historical v7 private setup timings increased in the tested HF case and were inconclusive
for normal clock-zero; they establish no universal CPU or payload work-gain claim.

The helper can retry the original RF catalogue with measured delta as its
origin, retaining every original audio-clock value; it is not absolutely restricted
to +/-7.97985 Hz. The ordinary `search_limited` flag still describes its
origin-zero catalogue. For the supplied HF geometry, that raw range remains
+/-7.979848 Hz rather than the requested +/-90.45 Hz, with all nine clock values
retained. At 6,120 Hz, native bin 17 checks the absolute offset of every shifted
original pair, including unselected pairs, against `Fs/(4*17) = 90 Hz`.
This geometry's conservative helper-origin condition is therefore about +/-82.02015 Hz,
subject to other coverage/passband gates. Unsupported assistance declines with
a reason and keeps ordinary broad acquisition; it does not qualify full range.

Exact-HF geometry controls reached +/-30.15 Hz across all clock values. Current
exact Rate 36/interleave 10 HF simulation passed full bits/END with injected
PCM +30 Hz, 100 ppm and 0.502494 degrees per square-root second phase diffusion.
It reported 8,181 original/49 selected pairs, retaining the baseline limited
search flag. Its 166-second audio took about 468 seconds wall, without an HF-off
comparison or real-time result. +80 Hz PCM and full requested RF remain unqualified.

Current matched normal Auto 1,024-bit reception at Rate 1500, +30 Hz and 100 ppm
passed full wire/source in both legs: 7.3091 times fewer acquisition jobs,
3.6578 times less process CPU and 1.4361 times less wall time, with more input
FFTs. This strong controlled-HF result establishes no universal gain, weak EME
or real-time performance. See [validation](validation.md) for source-bound results.

Each established track freezes its frequency origin and affine phase convention.
The executor retains conditional/saved-batch and original broad-scoring views.
The audit policy arms its first due date when an original ordinary batch first
becomes ready, using valid original row dates. Audits become due after
`max(6 seconds, 8*T)`, where `T` is the symbol duration, and are serviced at the
next ready FFT batch. This is not a hard bound on completed broad coverage;
actual service and coverage remain separate observations. Helper renewal cannot
postpone the due date. Initial probation and accepted-link leases retain
`max(6 seconds, 2*T)`; stale, lost or contradictory references resume broad
acquisition immediately. Accepted payloads
keep their ordinary lifetime and error recovery through loss. Actual audit and
interference coverage require runtime checks, not inference from lower pair counts.

A fresh causal measured helper reference now arms allocation-free sticky gap
protection for native ordinary rows, including new and legacy/cohort-declined
rows. An accepted owner survives a pending gap until ordinary full-symbol
absence can finish it; a later alias END cannot complete an owner already erased.
Reference loss stops protection for new births while protected accepted owners
retain the ordinary absence rule. Original new-start thresholds, the 16-track cap,
helper-off retirement and unsupported compact-backend policy remain unchanged.
New optional auxiliary construction yields to active ordinary payload work;
existing accepted assisted ordinary payloads remain retained.

The integrated cohort-capacity repair removes the former sticky 240-row limit
without bypassing ordinary admission/resource checks. A base-template cache
reuses unmodulated operands while retaining every original continuation fit,
including nonprimary cells. It does not merge suffix decoders, and reuse alone
establishes no speedup. Historical `f47fa339` binary and `a469ce48`
long-text/native proofs passed within their separate scopes; broad qualification
remains open.

Independently qualified distinct streams keep their accepted prefixes and
separate GUI rows through later helper uncertainty. A fresh helper-involved
candidate with unknown identity cannot erase an established owner. Each onset
and suffix decoder remains independent: helper frequency alone cannot merge
receptions, suppress starts or combine framing/decompression state. Published
source-address metadata uses only diagnostic history in helper-involved
receivers; for example, 128 entries can become 118 without increasing actual
Bank allocation or reducing payload-bit capacity, source quota, recovery or
candidate peaks. Every owner still requires ordinary scored six-second absence
for END. Private same-PCM and thin-catalogue checks do not qualify general
false-alarm behavior or a full runtime path.

Replay staging packs verified binary prefixes only for compatible ID/revision
progress, retaining at most 60 exact replay-frame checkpoints per timeline.
Generic fallback, terminal, recovery and alias semantics remain. The shared
351,760-byte pool and 124 owner slots do not grow; neither wire/source quotas
nor ordinary physical END changes. Private nine-row and 25-row actual-class
checks passed; `a469ce48` normal-path text and native text/attachment proofs
passed within their recorded scope. The original nine-row failure
retains its scope, while broad checks remain pending.

A null-by-default diagnostic callback borrows ordinary detector wire bits before
Data XOR/framing. Only tests install a bounded collector, outside DSP accounting,
to compare the entire actual wire without the JSON trace cap. It supplies no
expected length or other decoding input and adds no Helper Carrier payload.

The optional adjacent-helper FIR is a real input filter, not synthetic signal
subtraction. Its complete declared RF/audio-clock/profile support must pass the
separation and amplitude-error checks within the existing reserved resources.
Only fully observed centered windows are passed, at their actual delayed input
coordinates; EOF adds no zero padding. Broad scoring retains ordinary catalogue
semantics, and filtered-noise payload false-alarm behavior remains a separate
qualification requirement. If helper work cannot fit alongside mandatory decoder
and source resources, it yields without adding a message quota or reducing payload
functionality.

Phase correction is independent and optional, with a fixed 24-segment history.
Both phase and punctuation timing must qualify dated references for future
compatible complex bins. No later observation corrects an earlier bin. On loss
or history capacity exhaustion, new correction stops with a UI decline reason;
established tracks retain their frozen frequency origin and last affine phase
slope. Unknown constant data phase and covariance handling remain ordinary.
A visible helper or frequency-aligned search can therefore have no applied
phase work. Punctuation may provide bounded slight timing help without identifying
symbol zero, UTC or a different source/key address. Metadata grants no clock
authority and never bypasses ordinary search.

Helper lead and tail surround the entire ordinary waveform, including training,
pulse padding and drain, with at least two seconds aligned to absolute 400 ms
dwells. Tail length must also cover actual FFT/FIR and scored-absence buffering.
Ordinary Rate, waveform, source/key/epoch coordinates and source-only progress
remain unchanged. Two seconds of lead does not promise weak-reference precision
before data begins. Current 32-second weak EME observer checks at the 205/210 dB
path-loss settings had minimum frequency interval half-widths about 2.277/3.183 Hz,
with no phase or timing assistance. These are empirical observer limits, not
physical precision floors or ordinary weak EME payload qualification; pair
selection also needs the simultaneous measured centre and full 2D coverage check.
At the reported 20.7609 dB-Hz EME budget, an earlier
observer measurement had minimum interval half-width about 2.2992432 Hz. Its simultaneous
centre was not logged; width alone does not prove zero pair reduction. A
nonzero centre could trim an edge. Historical one-dimensional padding thresholds
do not qualify the current 2D mask, which needs full closed-slab coverage.
This is neither a current measured gain nor a physical precision floor. The failed private
full-union estimator's reported `0/3565` result is separate evidence.

A separate diagnostic of the `d072` observer on retained 32-second/24 kHz
helper-only input at 20.76091259 dB-Hz produced minimum half-width 2.3018353 Hz,
with public parity 165/165 and no phase/timing/metadata or noise-only references.
Its model allowance met or exceeded the maximum chord cost in 159 of 160 evaluations;
all retained every estimator frequency cell. History covered at most 2.44 seconds,
without gap, recenter or cell-cap failure. This is an algorithm/model-allowance
limit, not a physical floor, original RF-pair count or ordinary EME qualification.
No algorithm change follows from the diagnostic. See the
[plan](helper-carrier-plan.md) and [validation](validation.md) for scope and hashes.

Keyed long-symbol compact eligibility and original workspace coverage are separate
limits. Optional EME pulse/cache representations can use the existing cohort
share when proven smaller, keeping the original grant, bit quota, detectors and
geometry. A local constructor pass fixes the earlier baseline workspace case;
it does not qualify aggregate EME reception or weak precision. The `T >= 60 s`
compact-cohort decline and exact weak HF/EME limits remain.

The historical native HF observation's approximately +/-0.528 Hz interval is an estimator
bound, not a physical floor: it uses the current conservative 40 ms free-quadratic
endpoint fit with eight-standard-error and model allowances. The mapped radius
of about 0.565 Hz retained all nine original clock lanes in that case. At 0.2 seconds that
frequency interval alone contributes about 0.663 rad, exceeding the optional
phase bridge's 0.25 rad forecast gate; phase assistance may therefore decline.
The current keyed `T = 128 s` EME case also lacks a supported common-link compact
backend. Neither interval implies a fundamental precision limit or a backend
change.

Private analysis of the public hardware PCM identified payload-band data energy
inflating the raw white-residual covariance estimate: payload residual variance
was about 852 times lead/tail, with 99.857% of residual energy in the Robust
band. Median interval half-widths were about 0.016--0.017 Hz in lead/tail versus
0.48 Hz during payload. Independent finite-operator controls confirmed the
aliasing term without threshold changes. The working source now accepts optional
public `OccupiedBand` nuisance conditioning, covering all public receive profiles
and the full requested RF/audio-clock domain. It refines already accepted raw
tone publications, never fresh admission or decoded payload content. At most 27
DFT nuisance frequencies are used; unsuitable geometry or an unsafe fit retains
the raw estimate. Receiver-owned storage plus project scratch retains its 9 KiB
bound, with external runtime/provider allocations separate. Current build and
selected focused checks passed; broader current receiver/precision coverage is
pending. Historical ordinary-cover checks do not qualify the changed selector;
refined precision and false-alarm behavior remain unqualified.
Private compact stable/loss checks passed but repeated-audit work failed;
they do not alter the current unsupported compact-cohort gate. See the
[plan](helper-carrier-plan.md) and [validation](validation.md) for scope.

The actual row verdict in runtime `helper_status` determines whether acquisition
narrowed. An active-frequency flag says only “reference aligned”. Pair counts are
independent maxima across rows, labelled “pair maxima across rows: selected X,
original Y”; they may come from different rows. Their ratio establishes neither
narrowing nor speedup. Phase-applied state, decline reason and actual conditional/
audit jobs are separate. Counters can publish only after an ordinary cohort
barrier, so partial closing traces and public snapshots can differ. No CPU-ceiling,
payload-SNR or probability gain follows from presence or these maxima; matched
full-receiver measurements must include observer/filter/audit overhead.

The shared helper observer fits within spare capacity in the existing sampled
channel reservation, including mode handoff. Ordinary modem/audio reservations
retain priority, including duplex playback. When helper work cannot safely fit,
Robust continues without that optional assistance.

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

Actual source/binary bindings, original failures, partial captures and the scope
of normal, sanitizer, simulation and cable checks are recorded in
[validation](validation.md). A result for one recorded source or configuration
does not qualify a later candidate or weak RF link. Private class checks do not
establish broad false-alarm behavior, hardware duplex throughput or complete
payload functionality on their own.

The recorded `a469ce48` manual long-text proof recovered its complete authenticated source
and all 7,296 wire bits on the same pending row through ordinary physical END,
with no health failure. Its approximately 29.4-minute wall time is functional
evidence, not real-time or earlier-attempt speedup qualification.

The historical `f47fa339` encrypted Auto/rolling 1,024-bit binary pair recovered the
complete source and actual wire in both runs, with lifetime prefixes, pending-row
progress, ordinary physical END and health checks passing at matched workspace.
Independently reconciled acquisition template jobs
decreased; the trace-bound work audit includes incomplete snapshots without
cancellation excess. These counters exclude retained full-RF scalar continuation
fits and cache preparation; they do not measure all work or frequency coverage.
Whole-process CPU was 480.734253/128.263774 seconds and wall time
87.396753/64.160263 seconds, 3.748013 and 1.362163 times less respectively;
input transforms increased. Different legitimate raw key epochs and secondary
rows limit the claim. The trace-bound `candidate-v5-long-binary-work-check.json`
reconciles acquisition counters across all seven record classes.
This is neither false-alarm nor general throughput qualification.
Detailed counters, configurations and bindings are in [validation](validation.md).

Historical `f47fa339` native text96 cable strictly passed all 3,648 wire bits,
authenticated 96-byte text, and 11,510 retained correct prefix observations on
row 2/revision 1 through physical END after 6.050667 seconds of scored absence.
Health checks were clear, the light trace was complete without drops and PCM
was clean. This is functional coverage, without numeric-work, real-time or
CPU-gain qualification. See
[validation](validation.md) for
`cable/candidate-v5-long-private/candidate-v5-full-text96-v1/result.json`.

Historical `f47fa339` native attachment129 strictly passed all 4,864 wire bits, including
those beyond the preview limit, authenticated file kind/name/data, and 19,575
correct prefixes
on stable row 2/revision 1 through END after 6.039333 seconds of scored absence.
The full configured Auto RF/audio-clock domains were retained; frequency and
timing-qualified phase were observed, with later bounded fallback. Health,
complete no-drop trace, clean
910-second PCM, unchanged source/binaries and child cleanup passed. This is
functional evidence without numeric-work, CPU-gain or real-time qualification;
see [validation](validation.md) for `candidate-v5-full-attachment129-v1/result.json`.

The retained `a469ce48` native text96/attachment129 strict passes keep their own
source bindings and full source/wire/prefix/END scope. Their clear health,
PCM/seal/hash gates and the text run's 340.952-second maximum backlog establish
eventual functionality, not current-source or real-time qualification; details
remain in [validation](validation.md).

Historical `f47fa339` normal encrypted Auto/rolling HF simulation strictly passed all
eight encrypted wire bits and source `11010110`, stable primary row 2/revision 8,
strict prefixes and ordinary physical END, without a known TX timestamp. At
Rate 36/interleave 10 with crystal/IC-7100 at 30 MHz, reported pairs narrowed
from 8,181 to 51 while retaining all nine original audio-clock lanes; phase was
not applied. The complete trace had no drops, input health was clear and the
ordinary `search_limited=1` catalogue report remained unchanged. This helper-on
pass supplies no matched CPU-gain or real-time claim. See
[validation](validation.md) for `candidate-v5-hf-eight.log`, bindings and counters.

The retained `a469ce48` HF simulation's original exit-1 fixture failure remains:
it rejected a permitted secondary completed-to-pending transition at a new
revision. Read-only post-hoc checks of that sealed trace passed all eight
wire/source bits and historical END checks; this is not a new reception or
health run. The `a469ce48` exact-HF cable run is incomplete after 900 seconds,
with seven of eight bits (`0010110`) pending on the same row and no END. TX
passed; `search_limited=1` was its sole health flag. Trace, PCM clipping/XRUN,
hash and cleanup checks passed. See [validation](validation.md) for both scopes.

Historical `f47fa339` HF8 cable showed an exact stable eight-bit final GUI entry in
its 900-second window, but the capped trace dropped records and lacks complete
eighth-bit/END trace proof. With `search_limited=1` still reported, this is limited
GUI evidence rather than a strict native pass.

Broader full-receiver/interference controls, strict exact-HF native and weak EME
remain pending. The original normal core stage had 318 passes/five failures;
the unchanged attachment check passed in isolation, while four GPG sandbox/IPC
setup failures and the interrupted/unrun scopes remain recorded. Serial test
scheduling changes no timeout or assertion. Historical e2/v6 normal qualification
passed all 379 registered tests exactly once with no failures or omissions and
passing source/binary/profile/cleanup audits. The historical e2 ASan/UBSan build and provenance
checks passed. All seven original sanitizer scopes ended: Core 315/321 passed, Fast 34/36,
Frontends 3/3, Web 13/13, Live 1/2 and Preparation 0/1 after a 9,000.22-second
timeout, plus Calibration 0/1 after a 10,800.21-second timeout. Their eleven original failures remain,
including an actual pre-existing UBSan finding in Fast; that historical 377-case
headless profile was not qualified. Preparation's partial checks do not qualify its
unfinished work; it reported no sanitizer diagnostic. Calibration's partial
controls also do not qualify its unfinished work; no diagnostic before its
timeout is not a pass. The final wrapper exited 1 and was reaped. Final audit
is complete: 366/377 passed, eleven failed/incomplete, official omission lists
empty, and all 18 source/auxiliary/instrumented receipts plus cleanup checks valid.
These original sanitizer failures remain historical. The Fast empty-write fix is
now integrated; current sanitizer focus passed 15 named, all 24 Fast CLI and
three gap checks, with 364 other headless cases unselected and no full-suite
qualification. Current normal passed 381/381 with no omissions/limits, and
Native FLTK passed 3/3 (`native-v8-prep/execution-v8/execution-result.json`,
hash begins `9b317cf5`).

Current physical text96 strictly passed exact 3,648 wire bits, authenticated
96-byte source, same pending row 2 and ordinary END, without health errors.
First source at 462.300 seconds, TX drain at 171.861 seconds and peak backlog
325.855 seconds establish functional completion, not real-time or cable CPU gain.
Current physical attachment129 strictly passed all 4,864 dated wire bits,
authenticated `cable-tail-129.bin`, same row, 19,302 correct prefix polls and
6.067333 seconds of scored absence before END. Health, complete no-drop trace
and 910-second recording without XRUN/clipping passed. Its 607.766333-second
backlog likewise supplies no real-time or cable CPU-gain claim. See
[validation](validation.md) for text result `76d21e76` and attachment `c9db2f61`.
Current controlled work/exact-HF8 simulation results retain their stated scope;
Current HF8 physical cable confirmed exact `00101101`, identity, canonical
phase/seed and scored END on completed row 2. Its unchanged strict audit failed
because full eight-bit pending/all-prefix coverage was not observed. Actual
native acceptance chunks were 2/3/2/1 bits, with physical END already scored at
the last bit; shared commits were 2/5/7 pending bits followed by 8-bit completion
on the same row. No earlier accepted eight-bit pending state existed. This is a
fixture coverage gap, without a demonstrated runtime progress gap; ordinary
completion does not require fabricating a pending state after valid END.
The failed/limited result hash begins `1c1e61ff`, reader `f1257e59`. Baseline
`search_limited=1` remained; other health checks, the complete 521-record trace
and 900/910-second Controller/recording window were clear, with no trace drops,
clipping or XRUNs. Maximum backlog was 774.169935 seconds. The reference was
active with up to 220 selected pairs from 8,181; phase assistance declined.
This supplies separate exact-wire/END proof, without a strict full-prefix pass,
real-time or CPU-gain proof, or full requested +/-90.45 Hz HF coverage.
Broader performance, full sanitizer, weak EME and
remaining Rev qualification remain pending;
there is no old-source equivalence or full-coverage bridge. See
[validation](validation.md) for the retained and current evidence. Physical oscillator noise, DAC/RF dynamic range and near-threshold performance
remain separate scopes. Enabling a helper, narrowing pairs or decoding metadata
provides no automatic SNR/probability gain or GPSDO-replacement claim.

Capacity/cache repairs are integrated; their focused and manual proofs retain
the limits above. Full ordinary decoder and source resources remain required;
optional helper work yields when it cannot coexist.
