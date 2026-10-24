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

For a mixed Live transmission, unavailable helper placement, generator setup or
a helper-specific physical-rate request disables the helper and reports the
reason visibly; ordinary Robust transmission continues with its original
unscaled payload PCM. A provider setup failure
can be retried once without the helper rate request only before the first PCM
callback. An actual output gap is still a transmit failure and is never retried
as a fresh transmission. A helper-only request has no ordinary payload to retain
and fails if the helper cannot be generated. Other ordinary transmission errors
retain their normal failure behavior.

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

## Link Planner reference budget

Link Planner shows the helper separately from the ordinary payload: effective
power after the ratio ceiling, received power, C/N0, expected tone SNR in a
10 Hz fit cell and the required audio center. With effective helper power `P`
watts, path loss `L` dB and noise density `N0` dBm/Hz, its received power is
`30 + 10*log10(P) - L` dBm, C/N0 is that result minus `N0`, and tone SNR is
C/N0 minus 10 dB. The planner's current editable Robust transmit power supplies
the ratio-limit reference.

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

The 210 dB example is below the receiver's 6 dB presence level, so usable
assistance cannot be assumed. Even an adequate predicted level still needs
measured confidence, coherence and suitable geometry/resources. These auxiliary
values do not change the payload's C/N0, target SNR, success-probability curves
or CPU estimates.

For a sufficiently large ordinary oscillator search, the planner also shows a
prospective conditional acquisition size: at most 257 frequency/clock
alternatives per narrowed profile, the ordinary count, retained clock-rate count
and the measured helper frequency-uncertainty threshold needed to fit that size.
It derives that threshold from the configured search geometry, not from a
received measurement or C/N0-to-latency prediction. All original epoch and start
alternatives remain. Runtime admission also needs an eligible coherent route,
a sufficiently fresh reference and bounded snapshot/history/workspace resources.
The planner distinguishes fixed-timestamp replacement eligibility from rolling
epochs, where the ordinary bank stays resident and only an additional helper
search is considered. These counts do not quantify a speedup, assisted sensitivity or probability of
successful reception. The ordinary curves retain their original meanings.

## Physical simulation

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

## Receive observations and assistance

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

Live has two bounded assistance routes. Both feed the ordinary Robust payload
decoder and retain the configured full source quota, framing, decompression,
recovery, next-poll bit progress and fully scored six-second physical completion.
Neither route carries message bits in helper metadata or treats helper loss as
payload END. Scheduled, Symbol_Burst and deferred hopping retain helper
transmission/observation without these additional acquisition routes.
Nondeferred Fake: FCC can use its existing gated data template; it still does
not tune radio hardware.

For a large oscillator search, primary bank replacement is restricted to a fixed
configured transmit timestamp and an unchanging epoch-admission bank. It uses
the same fixed-bank condition as ordinary refresh. It additionally requires an
explicit **Complex only** selection and an actual native continuous backend
without Clock sync, FHSS, DSSS/Interleave, calendar, compact, configured drift or
differential processing. Its bounded construction and ordinary receiver-error
recovery recipe remain structurally identical. Auto, energy and differential
modes, compact/correlator backends, scheduled geometries and rolling/unknown
epochs retain the ordinary bank; they may use the separate
additional lane only where that route is independently eligible. A fresh,
sufficiently precise frequency reference can condition an eligible fixed bank with at least 512 frequency/clock
hypotheses and a substantial reduction fitting at most 257 alternatives per
narrowed profile. Already inexpensive companion profiles can retain their
complete ordinary searches.
Every admitted original key, profile and private epoch gets a replacement
ordinary receiver, with its original start alternatives. The original declared
clock domain is retained, including continuous residual uncertainty when
transferring the measured helper offset to the data frequency. A clock domain
no wider than one quarter-chip cell can share one equivalent clock hypothesis;
its remaining uncertainty still widens the frequency interval. A tone alone
never converts all measured offset into pure RF error.

Before frequency qualification, a large-search history ring attempts to retain
the complete original initial FFT/acquisition aperture, within the smaller of
64 MiB and one eighth of bank capacity and only with genuine surplus. A
conditional switch is declined if the retained history begins after any original
receiver's input origin; assistance cannot silently discard an early symbol.
Before switching, the bank also retains the exact original receiver state and a
bounded ring for subsequent real raw PCM. It builds all replacement receivers
before committing the switch. The dormant original remains a required processing
consumer until the decision is resolved; capture accounting cannot treat its
unreplayed samples as already processed. The fallback ring must cover the
initial search aperture plus guards and fit within the smaller of 64 MiB and
one eighth of bank capacity. If that state, the replacements and ordinary
resource promises cannot fit, this assistance is not admitted. Precommit replay
eligibility also bounds the locally configured physical capture aperture and its
resource cost. It does not use a received length, total message size or reduced
source quota to choose eligibility. All configured ordinary source quotas remain
intact; optional helper work yields rather than lowering them. Separate immutable
cold-start and replay grants preserve normal receiver-error reconstruction.
Focused containment/recovery checks, the normal application build and selected
ASan/UBSan checks have passed. The broad normal run completed with a recorded
preparation timeout; full contract qualification is incomplete. Web and native
setup retries passed. Current physical `001` pending-to-completed reception was
observed; strict individual-prefix coverage remains incomplete. Helper resource
planning does not introduce a transmit failure or global capture reset.

Before any ordinary detector accepts a payload bit, reference loss or
inconsistency, unsuccessful initial search, exhausted history or an unavailable
conditional domain restores the same original bank and replays the retained raw
PCM through its ordinary receive path. No synthetic silence, skipped capture or
helper-derived END is inserted. A continuously present unrelated tone does not
trigger repeated conditional attempts. The first bit accepted by an unchanged
ordinary detector commits the conditioned bank and releases the dormant bank
and fallback history. That precommit replay bound then ceases to constrain the
accepted reception: the same full ordinary decoder handles long text,
attachments, framing, decompression and recovery under its ordinary configured
limits. No helper message-length quota is added. Tone presence, punctuation and
decoded metadata cannot commit it. After that boundary, helper loss retains accepted payload contexts
and restores broad searches for future cohorts; it does not restart or discard
the accepted reception. Helper association remains an assumption about the
transmitter, not authentication.

For rolling/unknown epochs, smaller searches or other eligible configurations,
one additional optional frequency/timing-assisted receiver can operate alongside
the original bank.
This lane preserves the existing backend: native coherent FFT remains FFT,
and an already selected compatible compact coherent correlator remains compact.
Long FFT fill alone does not trigger an automatic backend switch. One 36 Hz,
DSSS 10, SF 256 profile therefore retains about 45.5 seconds of FFT fill for its
14.22-second symbols. This lane uses actual spare workspace after ordinary
reservations and growth headroom. Its stricter, lifetime-spent evidence budget
accounts for repeated attempts. Before accepting payload bits, unsupported
geometry, ordinary resource needs or excessive backlog can retire it. Its first
accepted bit promotes that same context to ordinary retained payload work before
publication or optional reclamation. Helper loss or resource pressure may freeze
the last committed phase-model value but cannot discard accepted payload as
optional work. Normal receiver error and reconstruction policy still applies.

For the smaller additional lane, the optional history ring retains up to
16 seconds of real logical PCM, capped at 1 MiB, only when genuine surplus fits. It is charged to the helper allowance
(the smaller of 64 MiB and one eighth of bank capacity). Eligible native FFT
receivers can pre-roll this dated input when a useful estimate settles; compact
correlation does not replay it. Replay does not advance capture ingress or apply
a later phase correction backward. It cannot guarantee an arbitrary delayed
first symbol remains available. Calendar persistence requires all ordinary
growth promises, the maximum ordinary constructor grant and node/control costs
to fit; speculative history and optional acquisition otherwise yield before
ordinary admission sizing.

For an adjacent helper within the logical capture band, these assisted routes
can use a real linear bandstop FIR centered on the measured helper, with a
225 Hz half-width. This is not synthetic tone subtraction. Its centered windows
must be fully observed; output coordinates include the filter delay and EOF
never supplies zero padding. The unassisted path and raw fallback keep the
original PCM. This frontend suppresses the strong adjacent reference but also
correlates noise, so the tone tracker's 1% admission bound does not certify
payload false-alarm behavior after filtering.

In the additional optional lane's compatible coherent FFT paths, qualified punctuation timing removes the
helper's known phase reversal before phase assistance. Dated references can then
correct measured phase wander in completed complex FFT bins, retaining the
unknown constant data phase and each bin's covariance eigenvalues. Phase
assistance can become active after receiver admission: a continuous reanchor at
the committed processing frontier preserves the initial template carrier offset
without applying the new correction backward. The timing-search range remains
fixed at admission; later phase qualification does not dynamically narrow it.
Phase correction acts on complex bins rather than rotating real PCM; the
separate adjacent-helper FIR above is an input filter. Sample-fit, differential,
real-rank, fallback and other phase-unsupported detector paths retain their
ordinary phase processing; frequency/timing-prior eligibility is separate. The reference must predate the
input bin being corrected, so captured future samples cannot
condition earlier noise scores.

The covariance argument covers fixed, independently selected phase traces.
Helper-derived traces are adaptive; deterministic tests do not certify their
lifetime false-alarm rate, especially with correlated filtered noise. The
additional lane uses stricter attempt-spent evidence thresholds and retains the
complete ordinary receiver. Full adaptive-noise calibration remains separate.

The additional optional lane ranks one advisory private epoch from the earliest
privately retained qualified tone-support measurement, adjusted by the selected profile's
training/padding prefix and configured propagation allowance. Observer slices
of at most 40 ms retain that evidence's original sample date, while public
callbacks keep their existing last-update cadence. This improves the onset hint
without identifying UTC or guaranteeing the first physical tone start. Late
qualification at weak levels can cross a second boundary: the unknown-epoch,
late-start case can select the wrong epoch and fail assisted acquisition.
Frequency assistance with a fixed epoch is a separate case and does not remove
this optional-lane limitation. Its ordinary bank remains available, while the
eligible fixed-bank primary route described above retains all already admitted
epochs and their original start alternatives without choosing this nearest alias.
Neither route establishes UTC or a general replacement for GPS Clock sync.
Complete acquisition still depends on the ordinary admitted domain and measured
reference quality.

Within a candidate private epoch, punctuation timing can refine a bounded
optional epoch-location hypothesis for compatible unscheduled reception. If `D`
is an observed dwell boundary in receiver-local seconds, `E` is that private
integer epoch and `r` is a retained clock-rate hypothesis, its possible locations
are `B = D + (0.2 * (E % 2) + 0.4 * k) / r` for integer aliases `k`.
`PatternStartWindow`, expressed here in seconds, keeps the affine phase relation
`B + phase_scale * p` for private stream phase `p`, with an envelope covering
retained rate/private-phase values. It does not assume symbol zero is a dwell
boundary. The bounded modulo-400 ms alternatives intersecting that epoch's prior
are considered and one optional receiver takes the nearest alias. This can
reduce its timing-cell work while the ordinary admitted prior remains unchanged.
It does not establish UTC/GPS qualification, retime source bits, prune ordinary
search or promise complete private-epoch coverage. Excessive alias sets leave
timing refinement unavailable. A separately known symbol-to-dwell relation can
also be expressed without treating the marker as an absolute date.

Decoded timestamp metadata is unnecessary for frequency, phase and dwell-timing
assistance; it never bypasses frequency or time search. A single tone cannot
independently separate RF error, Doppler and sample-clock error. No reduction of
the CPU ceiling or improvement of full-bank sensitivity follows merely from
tone presence or the planner's prospective counts. Conditioning can reduce the
oscillator alternatives actually scored, while optional assistance can also
consume spare work. End-to-end latency, aggregate CPU reduction and weak-signal
sensitivity require matched measurements for the selected configuration.

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

As of 24 October 2026, final source qualification remains pending. A later
exact HF diagnostic recovered ordinary `001` and completed dictionary text `e`
in one GUI row, but did not separately observe `0` and `00` and retained a
failing receiver-health state. That pre-containment result does not demonstrate
benefit for the current rolling-epoch route. An EME diagnostic was cancelled
before payload reception. These partial results do not establish complete assistance
qualification. A current-source 48 kHz physical Controller check at public
Volume 20 returned the adjacent 40 W helper cleanly and observed exact ordinary
`001` pending and then completed on the same row while capture remained open.
It reported no receiver-health failure, missing or unexpected bits. Its strict
runner exited 3 because separate `0` and `00` pending prefixes were not observed
in its polls; full individual-prefix coverage remains unqualified. The earlier Volume 100
return clipped. These results do not establish weak-RF, default-Auto or GPSDO-
replacement benefit. Exact source identities, original failures and limits are
recorded in [validation](validation.md).
Focused sanitizer coverage includes DSP/framed batch transfer and three-bit
native-provider Live paths, including replay and ordinary-error recovery. It
does not cover full-payload Live, GUI/CLI entry points, hardware/RF, throughput
or leaks; it is not a full sanitizer suite. Focused shared GUI/controller/parser/
planner checks passed. The 294-registration normal run completed with 288
passes, five setup failures and a 1,500.12 s preparation timeout. Exact web and
native display retries passed all five setup-failed cases, giving 293/294
distinct cases passed with unchanged source and binaries. Physical completion
and strict prefix coverage have the separate limits above. Preparation coverage
remains failed/incomplete, so no full matrix pass is claimed.
A proposed longer frequency history failed weak truth-coverage checks. The
experimental recipe bootstrap was withdrawn after independent resource-contract
review, and a separate private joint raw-bin estimator remains unaccepted.
No production integration is planned for this candidate. Payload-contamination
handling and the stochastic phase prior need a supported argument and validation
before any future integration. Neither path
provides a qualified fix for the exact weak EME case or establishes an SNR or CPU
speedup. Existing assistance must
fit alongside full ordinary receiver grants and retained history; otherwise the
helper yields. This preserves ordinary framing, source quota and scored END
without adding RAM or changing disk/I/O contracts.

See [validation](validation.md) for actual checks and limits, and the
[implementation plan](helper-carrier-plan.md) for acceptance work. Digital
spectral tests, independent vectors, disabled-path comparisons and conditional
noise fixtures do not qualify physical DAC/RF distortion, oscillator phase
noise, dynamic range, native duplex hardware behavior, real-time CPU margin or
near-threshold reception. The original Robust wire/progress/physical-end
regressions remain required. Neither metadata decoding nor a successful
helper-only fixture establishes sensitivity or throughput of a mixed radio link.
