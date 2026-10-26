# Helper Carrier implementation and qualification plan

Updated 26 October 2026 for the manually enabled tracker/NCO preview. Live
integration repairs and the rebuilt GUI remain pending. The immediate target
is a manual GUI trial within the requested one-hour window, with short focused
checks; broad qualification is explicitly deferred. This is not feature or
release completion. Source-bound results and original failures remain in
[validation](validation.md); waveform details are in the
[Helper Carrier reference](helper-carrier.md).

## Current preview design and immediate checks

Use an upstream real-time tone tracker and continuously retuned NCO feeding one
ordinary Robust receiver bank. Assistance is manually enabled; enabled without
qualified lock admits no new payload acquisition. The preview does not use
the previous broad fallback/probe banks, old capture replay or receiver
capsules. Full ordinary wire, framing, long-text/attachment recovery, accepted
progress and fully scored physical END remain required.

The tracker is fixed at 54,160 bytes: a 4,096-point FFT seed, 20 ms FLL and at
most 128 recent numeric observations over three seconds, without iterative
historical work. Standalone tracker cases for drift, punctuation, noise, loss
and sparse updates passed, and continuous NCO retuning passed a 0.376-second
smoke check. These are component checks pending final source/check bindings,
not Live or GUI acceptance. Uncertainty near 0.209 Hz at C/N0 49.7 dB-Hz and
1.06 Hz near 31 dB-Hz does not establish 128-second-symbol coherence. New
metadata, punctuation-phase or timing assistance is not qualified; the NCO
phase stays continuous during retuning.

Build the GUI, exercise lock/no-lock and loss behavior, and verify the complete
ordinary payload/progress/END path in the manual trial. Link Planner must show
the ordinary baseline and explicitly unavailable assisted estimates, with the
old conditional overlays removed. Do not infer a CPU/SNR gain from enabling
the feature. Final source/binary pins and these focused results are pending.

The earlier receiver requirements and rolling architecture below are superseded
by this preview. Their preserved measurements describe their own builds and do
not qualify the new route.

## Previous design and source-bound evidence

The preceding build's 441-input source/build overlay is
`87513574c569de7d9362d498f747cea6658b51e0b681884555242884c2a0e17f`, bound by
`local-evidence/helper-v4/candidate-v8-source-bindings.json`,
`candidate-v8-auxiliary-source-bindings-r1.json` and
`candidate-v8-binary-bindings.json` for six production binaries. The application
build passed, recorded in `candidate-v8-application-build/execution-result.json`
(hash begins `0fe66ddd`); the GUI hash begins `8f2af1f4`.

That build's lifecycle focus passed three sampled/control functions and the
deterministic rolling pair, recorded in
`candidate-v8-lifecycle/focused-checks-r1/execution-result.json` (hash begins
`b555072b`). That build's full normal qualification passed 381/381 registered cases
exactly once: Core 325, Fast 36, Frontends 3, Web 13, Live 2, Preparation 1 and
Calibration 1. All 24 Fast CLI cases passed, with no omissions or limits and
passing provenance/cleanup audits; the final record hash begins `b810c8a5`.
That build's sanitizer focus passed 15 named cases, all 24 Fast CLI checks and three
gap-focused checks; 364 other headless cases remain unselected, and the original
eleven full-suite failures remain historical. Its Native FLTK checks passed 3/3.
Its physical cable text96 strictly passed full wire/source and physical END,
with large backlog rather than real-time performance. Attachment129 also passed
its full 900/910-second window. HF8 cable completed exact data and physical END,
but its strict full-pending-prefix audit failed and remains limited. Its
controlled 1,024-bit work and exact-HF8 simulation results have the narrower
scopes below. Broader performance, remaining cable and weak EME qualification remain pending.

All `27fd8614`, `f47fa339`/`e2b951fc` and `a469ce48` results below are historical and retain
their own bindings. There is no current-source equivalence or full-coverage
bridge. Earlier normal 379 passes, sanitizer 366/377 passes with eleven
failed/incomplete, v7 normal failures and prior payload/cable/work measurements
do not qualify v8. See [validation](validation.md) for source-bound evidence and failures.

## Previous design requirements — superseded

The current acceptance cases add two encrypted 15 kW HF configurations: Rate 36,
shared crystal RF/audio reference at 30 MHz with Interleave 10, and Rate 360,
independent crystal audio/ic-7100 RF references with Fake-FCC and Interleave 10.
The earlier 5.8 GHz GPSDO-OCXO, Rate 1, 128-second-symbol case remains a separate
requirement. Bounded baseline runs retained in `local-evidence/helper-v9/`
completed no payload in any of these cases. Rate 360 advanced only 2.05 seconds
of captured audio in 55 seconds of processing; the EME case advanced 0.341
seconds in 20 seconds. Neither executed conditional acquisition work. These
observations establish failures, not a completed performance comparison.

The correction must discover the strongest stable tone in the captured helper
range before narrow conversion, retain both admissible signed-mono images, and
use independently qualified frequency measurements without requiring phase,
punctuation or metadata decoding. Shared-radio clock inference requires a new
immutable conditional catalogue with its actual nonzero audio-clock values;
an RF-only correction cannot repair a clipped clock catalogue. The ordinary
compact and calendar backends must skip actual cold projection/fit work while
preserving all accepted state and complete-symbol absence scoring. Optional
workspace reclamation must yield to ordinary broad decoding and cannot erase a
required audit obligation. Punctuation preserves all modulo-400-ms aliases;
it cannot reduce one-second EME arrival cells. Configured oscillator diffusion
may support phase continuation, but does not authorize vehicle-Doppler tracking.

Physical capture remains a prerequisite. A sufficiently fast ADC does not
restore a helper or payload removed by a narrow SSB radio filter. Qualification
must report that limit separately from conditional digital-search coverage.

Helper Carrier is an independent experimental reference transmission, optionally
assisting Robust reception when GPSDO hardware is unavailable. It can accompany
manual traffic or operate alone. General-purpose power options, including
1.5 kW, remain available. Radio operating conditions and authorization, including
any Special Temporary Authority, are the operator's responsibility. The public
ASCII callsign format supports the identification objective; software alone
does not certify an installation's regulatory compliance.

Robust takes priority. Preserve its wire format, source-bit positions, logical
sample rates, configured data power/SNR meaning, next-poll progress, physical
completion and key/calendar addresses. Conditional acquisition must retain the
original ordinary search catalogue, accepted state and the real input needed for
bounded fallback. Resolve bandwidth, dynamic range and CPU conflicts on the
helper side. Successful dense APSK metadata decoding is optional and never gates helper presence or payload
reception. Robust message payload bits never move onto the helper waveform:
that waveform contains only tone, punctuation and optional callsign/time. The
assisted receiver is an ordinary Robust receiver guided by helper measurements,
with full long-text/attachment framing, decompression, recovery, bit progress and
fully scored six-second physical completion. Insufficient resources disable
optional assistance instead of removing receive functionality. The original
decoder remains available without helper assistance.

The intended experimental environment is quiet licensed or underutilized
spectrum, without an unrelated or deliberate adjacent transmitter. Under that
explicit same-link assumption, a qualified helper may condition a common
frequency reference for ordinary Robust acquisition. Tone presence alone does
not prove that assumption. Independent broad audits and immediate fallback on
reference loss bound reliance on it. The ordinary decoder, payload processing
and interference tolerance remain required; the helper is not a substitute for
them. A tone cannot distinguish RF error, Doppler and sample-clock error, so the
original clock catalogue must remain intact.

## Implemented controls, waveform and observation

These behaviors remain subject to their source-bound qualification limits.

1. Shared controls sit below the clock/oscillator row. Power defaults to zero;
   companion controls disable with it. Power is editable as finite nonnegative
   watts; presets include 10 kW, 15 kW, the requested lower values and existing
   Robust presets. Invalid edits keep the last valid setting and prevent TX
   until corrected. Ratio defaults to 90 dB. High/Low/0 Hz
   choices and the independent 19-character printable ASCII callsign remain.
   Developer callsign edits copy one way; helper edits affect future output
   without replacing live receivers or modifying the Robust draft. Encryption
   ignores the saved helper callsign and hides its GUI field; the preference
   reappears when unencrypted. Inactive encrypted-mode callsign text is ignored
   even if invalid, without losing the saved preference; clear mode validates it.
   This normalization/parser change passed source review and focused shared
   controller/parser checks; broader GUI qualification remains separate. The helper-only GUI action is Developer-mode-only.
2. A separate final-device-rate generator bypasses payload Interleave, DSSS,
   masking and time-shifting streams. Physical 400 ms dwells, 4 ms ramps, a
   single optional 236–244 ms reversal and trailing 125-baud APSK metadata leave
   most airtime coherent. Graceful streaming stop can append helper-only padding
   through 268 ms to preserve the reversal's post-tone guard and final ramp.
   Payload samples/bits and physical-end rules do not extend with this padding.
   Partial boundary dwells can omit punctuation or contain incomplete metadata.
3. Compact metadata sends only actual content. Callsign type `00`, a five-bit
   count and seven bits per character require `ceil((7 + 7*N)/21)` symbols,
   at most seven for 19 characters. Empty callsign selects time: type `01` clear
   or `10` encrypted, six bits for `B - 1` and `B` Q16 Unix-time bits, requiring
   `ceil((8 + B)/21)` symbols, at most three. Clear `B` is at least one;
   encrypted `B` is at least 20; both are at most 48. The underlying value has
   up to 32 whole-second plus 16 fractional-second bits. CRC-7/MMC is included
   only when at least seven spare bits already fit the canonical frame; remaining
   padding is zero. It covers header/transmitted contents and never adds airtime.
   Frames end at 388 ms and start `8 * symbol_count` ms earlier. This deliberate
   helper-format change replaces the earlier fixed experimental frames while
   leaving ordinary Robust wire formats unchanged.
   Encrypted time uses FF1 with AES-256, radix 2, ten rounds, empty tweak and
   integer arithmetic. Its key is the station-key HMAC of ASCII
   `datapump/helper-time/v2/ff1-key`; the 20-bit minimum retains a domain above one
   million. Encryption preserves the declared time length and adds no tag. All
   metadata is unauthenticated; wrong-key decryption can give a plausible time.
   The first selected receive key alone is used. In unencrypted mode a nonempty
   callsign takes precedence over clear time. Encryption always ignores that
   saved callsign, selecting encrypted time or tone without optional time;
   callsign/time never mix. Cipher failure has no clear fallback and decoded time
   never adjusts UTC. Station-key selection remains independent of the temporary
   tuning-noise key.
4. Linear scaling uses square-root power ratios and reserves composite peak
   headroom. The ratio ceiling reduces helper power rather than raising data
   power. The helper follows existing waveform amplitude backoff: interleaved
   V2 DSSS uses `g = 0.5`, so only the helper component receives the corresponding
   calibration factor. Robust PCM, composite data gain and noise standard
   deviation remain unchanged by this correction. Whole-dwell helper RMS also
   includes ramp/metadata duty and physical frequency-response losses. High and
   Low use the nearest leakage-qualified placement based on actual shaped
   occupied data bandwidth plus gating allowance. Center-to-data-edge distance
   starts at 450 Hz and grows in 50 Hz steps with effective power ratio; there
   is no logical-sample-rate High floor. Hardware FHSS counts the data gate
   allowance once; output preparation no longer adds a duplicate 200 Hz
   allowance. The reported EME geometry plans a 2,250.3125 Hz High center, which
   is placement evidence only. Low must fit above DC. Co-channel 0
   is intentional overlap and discouraged. Adjacent High/Low remain real
   interference when inside the logical capture band; no synthetic cancellation
   is applied. The radio/audio route must pass the complete bands and guard.
   Rate, carrier, logical DSP clock and target SNR remain selected values.
   Enabling a helper does not change 100 Hz, 3.6 kHz SSB or 12 kHz geometry.
   The startup passband failure was a nominal-versus-shaped-bandwidth error;
   correcting it requires no automatic Rate reduction.
   Current mixed Live fallback covers helper-specific provider-rate failure or
   a past helper epoch before PCM delivery, with a visible reason. It preserves
   calibrated `data_gain`, logical payload PCM, wire and key epoch; restoring
   full-scale data would violate the operator's RF calibration. One provider
   retry without the helper rate request is allowed only before the first PCM
   callback. Validated `calibrated_data_gain` is now independent of placement
   geometry. Placement or minimum-output-rate `Error` disables helper output
   before PCM while retaining calibrated ordinary PCM. This includes valid
   ordinary FHSS whose engineering gate allowance crosses DC; calibration does
   not require a cochannel placement plan.
   Helper-only output fails if its helper cannot emit;
   malformed powers/settings still reject. Actual output gaps remain TX failures
   without retry.

5. Native/browser playback adds the helper after resampling. Live/duplex raw
   capture observes it before payload downsampling or scheduled exclusion.
   Audio requests a physical clock of at least 48 kHz and sufficient helper
   bandwidth, independently of the original logical DSP rate. If that higher
   capture format is unavailable, optional observation yields before PCM/format
   acceptance and ordinary capture resumes; later gaps retain original handling.
   Helper, mixed-data and Transmit noise actions operate in simulation. The
   Developer-mode-only helper GUI action suppresses ordinary data/noise without
   inventing bits; mixed output and CLI helper-only use retain their own controls.
   Helper remains active through data waits/nulls. The existing physical hop
   calendar remains; Fake FHSS still does not tune radio hardware.
6. Helper-enabled sampled simulation has a separate physical clock and compact
   multistage anti-alias preselection. Both signals share clock error, carrier
   offset, phase noise and physical AWGN. Data-band AWGN spectral density retains
   the original payload reference. FIR transition-band removal slightly reduces
   total logical variance; renormalization must not worsen matched data SNR.
   The original AWGN reference is `nominal_signal_power`, including preexisting
   waveform backoff. For interleaved V2 DSSS, actual data C/N0 is nominal minus
   6.02 dB; the calibrated steady helper is nominal plus the configured ratio
   minus 6.02 dB, before dwell/route losses. Noise is not changed to remove that
   existing difference. Hardware Link Planner instead uses the operator's
   measured/calibrated RF powers, so 4 W data and 40 W helper correctly mean
   a 10 dB reference ratio.
   Centered lookahead preserves original logical coordinates/source extent;
   phase references wait for the measured frontier. Bounded native logical-PCM
   pre-roll is separate and applies no later phase correction backward. Helper
   zero preserves the original transmit waveform and channel sample arithmetic.
   Finite Live simulation snapshots resident FFT origins and conservative initial
   input spans at source completion, then feeds real channel noise through that
   fixed target when needed. This post-transmission tail correction applies
   equally with zero or positive helper power; it does not extend transmission,
   manufacture absence or supply END. Unpreservable data passbands and
   unavailable tone geometry are rejected on the helper side. In-band adjacent
   High/Low retain their actual contribution. Out-of-band helpers which alias
   into data require sufficient anti-alias isolation; co-channel 0 retains its
   intentional overlap. A separate optional assisted-receiver FIR can suppress
   an adjacent helper without changing the physical mixture or raw fallback.
7. Independent presence tracking retains bounded 100 ms fits, logarithmically
   growing evidence groups, a 6 dB fit-cell level gate and a conditional Gaussian
   1% lifetime false-admission budget. Six seconds of observed presence confirms
   the stable helper row; six seconds of observed absence can finish a shorter
   row and retire admission. Gaps reset evidence. Metadata is displayed when
   decoded but grants no payload bit, completion, copy or save permission.
   Coalesced helper events yield queue space to normal pending progress.
8. A bounded precision receiver measures dated frequency/phase and repeated
   punctuation timing. Local uncertainty uses actual finite-Gram covariance,
   an eight-standard-error noise term, numerical floors and model allowance.
   This is conditional local precision, not a deterministic interference or
   future-drift bound. The metadata decoder fits actual quintic transitions,
   checks canonical symbol counts, field widths, padding and any present CRC,
   and accepts only very clean captures. Encrypted metadata uses the first
   selected receive key; no authentication or key-selection claim is made.
   Callsign presentation follows received-text restrictions.
   The frequency-only path uses 144 ms raw-PCM fits with exact Gram matrices.
   Deterministic groups of disjoint raw-time windows combine fixed-grid F-test
   probabilities by Fisher's method; grid/group allowance is spent independently
   of whether a strong reference skips a later precision fit. Combined
   single-window/group admission and variance spending totals at most 1% per
   reset under AWGN. Centered finite-bin model-bias bounds include the
   real-carrier image moment, Taylor remainder and half-sample date offset;
   unstable bias feedback rejects refinement. Refined intervals remain local
   LS/model estimates, not exact global confidence intervals. Doubled-phase joint
   refinement retains compatible frequency-cell unions, capped at 4096 cells,
   otherwise falling back wide. Frequency-only output grants no phase/timing
   authority. Independently dated narrow-frequency/strong-phase fusion must not
   redate an older measurement. Useful precision at 24 dB-Hz is seed-dependent,
   including cases with no usable interval; no fixed acquisition latency is
   promised.
9. Adjacent helper suppression uses a real linear bandstop FIR where eligible.
    Fully observed centered windows preserve receiver input coordinates and the
    processing frontier; EOF cannot supply zero padding or END. Raw ordinary
    input remains available for fallback. Filtered noise is correlated, so helper
    presence bounds do not certify payload false alarms after that frontend.
    Phase and punctuation assistance require independent dated qualification;
    metadata never grants clock, payload or completion authority.
10. Link Planner keeps the separate helper power, received level, C/N0, tone SNR
    and placement budget. Prospective hypothesis counts explain a proposed
    acquisition domain, not executed receiver work. Payload SNR/probability
    curves retain their ordinary meanings. No CPU or sensitivity gain follows
    merely from enabling helper power or predicting a strong tone.

## Previous rolling common-link acquisition — superseded

### One ordinary executor and an immutable original search

Use one ordinary native executor per receive row. It performs the existing
coherent payload search, framing, decompression, recovery, accepted-bit progress
and scored physical END. Helper conditioning selects which acquisition batches
it executes first; it does not create a second payload decoder or a helper
message channel. Preserve all ordinary source quotas, decoder grants, retained
payload lifetime and error-reset behavior. If optional observer, history,
filter or phase work cannot coexist with mandatory ordinary work, it yields.

Each row retains its immutable original frequency and clock catalogue, original
source/key addresses, input origin and start/epoch alternatives. A tracking
context freezes its frequency origin when created. Later helper updates may
condition a search relative to that origin; they must not reinterpret an
existing template, erase its affine phase/frequency term or redate old samples.
Clock hypotheses and continuous residual coverage stay ordinary: a common tone
is not permission to collapse the clock search.

Apply qualified narrowing first to every eligible ordinary cohort, including
currently admitted and newly admitted keys, profiles and rolling/private epochs.
Do not restrict useful assistance to one guessed epoch or a fixed timestamp.
Do not omit an ordinary cohort because the helper ranked another one first.
Backend-specific support remains explicit; an unsupported backend runs ordinary
acquisition rather than silently switching algorithms or dropping alternatives.

The common-link cohort currently targets Automatic or Complex native reception
with rolling/unknown epochs; fixed-epoch reception retains its separate legacy
helper path. Keyed symbols of at least 60 seconds retain the selected compact
backend and decline this optional cohort, as do unsupported strategies,
scheduled/Burst geometry, unsafe all-profile FIR support and unavailable
reserved resources. Do not change ordinary backend or source quotas to admit it.

The 2D selector uses explicit original or UTC-expanded physical audio-clock
domain `A` and authoritative cell half-width `beta = clock_step_ppm/2`.
Neither adjacent clock-centre spacing nor a heuristic beta substitutes for that
provenance. Selected original RF cells and nearest audio-clock ties must cover
the entire mapped helper confidence strip across closed clock slabs, checked
as unions of RF intervals. Every original audio-clock value remains available.
Missing provenance, uncovered support or unsupported floating-point arithmetic
declines optional assistance. Try zero RF origin first, then the measured
baseline origin. A nonzero origin can align a full tiny catalogue without
removing pairs; report alignment separately from actual narrowing. The original
phase convention remains unchanged, and `search_limited` stays latched
for the unconditional catalogue.

Selector setup uses about 19.5 KiB of temporary arrays in addition to the existing
8 KiB mask. Measured individual static frames were 19,920 bytes versus the old
1,248 bytes, plus an 8,464-byte caller; these are not a whole-call-chain bound.
Do not claim all selector storage fits in 7 or 16 KiB. Historical v7 private setup timing was
1.595 ms versus 0.312 ms for the HF case, with observed worst cases around
4--6 ms; normal clock-zero timing was inconclusive. These are setup measurements,
not a universal receiver CPU claim or current payload work-gain qualification.

The original search-limit report remains valid when a helper translates the
conditional RF origin. The selector can retry the original catalogue at an
origin equal to the measured helper delta, retaining every original audio-clock value;
it is not confined to an absolute +/-7.97985 Hz baseline range. In the supplied
HF geometry, all nine audio-clock values remain and the unconditional origin-zero catalogue spans only
+/-7.979848 Hz of the requested +/-90.45 Hz, so `search_limited=1` stays latched.
At 6,120 Hz, native bin 17 validates every shifted original pair, including
unselected pairs: `max(abs(shifted_offset)) <= Fs/(4*17) = 90 Hz`.
Thus this geometry's conservative origin condition is
`abs(delta) <= 82.02015 Hz`, subject to the other coverage/passband gates, not
full requested +/-90.45 Hz support. Unsupported translated support declines
assistance with a reason and retains broad ordinary acquisition.

Exact-HF geometry controls exercised +/-30.15 Hz across all audio-clock values.
Current exact Rate 36/interleave 10 HF simulation also passed all encrypted bits
and ordinary END with actual injected PCM +30 Hz CFO, 100 ppm audio-clock error
and 0.502494 degrees per square-root second phase diffusion. It used crystal/
IC-7100, 30 MHz, 180 dB loss, encrypted Auto and receive date zero. It retained
all clock values while reporting 8,181 original/49 selected pairs; the baseline
`search_limited=1` report remains. Its 166-second audio took 468.07195 seconds
wall and 1,777.029994 seconds process CPU, with 90,293 conditional plus 57,267
audit jobs and 264 input FFTs. This is not real-time or an HF-off comparative
gain result; +80 Hz PCM and full requested RF coverage remain unqualified.

The current matched normal Auto 1,024-bit case at Rate 1500, +30 Hz and 100 ppm
recovered full wire/source in both legs. It measured 7.3091 times fewer
acquisition jobs, 3.6578 times less process CPU and 1.4361 times less wall time,
while input FFTs increased. This is a strong controlled-HF result, without
universal gain, weak EME or real-time qualification. See [validation](validation.md)
for `candidate-v8-long-binary-owned-focused` pair0 and corrected r1 work audit
(hash begins `92744ff7`), and `exact-v8-prep/owned-execution-v8/execution-result.json`
(`8816a324`) with `candidate-v8-hf-eight.log`/bindings (`e1cb8a82`).

The integrated capacity repair removes the former sticky 240-row cohort limit;
ordinary admission and checked resource limits still apply. The unmodulated
base-template cache reuses valid operands while executing every original
continuation fit. It cannot merge starts, suppress nonprimary cells or replace
an ordinary detector. Historical `f47fa339` binary and `a469ce48`
long-text/native proofs passed within their separate recorded scopes; broad
qualification remains pending. Cache reuse alone is not a speedup claim.

Suffix decoders remain independent. Sharing a qualified helper frequency does
not authorize merging their source bits, suppressing alternate starts, or sharing
framing/decompression state. Each onset retains its own source/key/epoch address,
accepted prefix and ordinary completion. Only ordinary source-identity rules
can associate receptions; helper frequency alone is not identity.

### Conditional batches, broad audits and fallback

The executor has distinct observe, track, saved-batch conditional and full-audit
states. Observation or metadata alone supplies no acquisition authority.
Qualified, dated frequency support can place conditional batches first while
retaining the original batch/catalogue state and real input needed to resume
broad acquisition. The ordinary frequency search still runs inside the measured
strip; it is narrowed, not eliminated. Bounds include helper-to-data transfer,
clock residuals, sample date, local covariance and model error.

Arm the first broad-audit due date when the original ordinary batch first
becomes ready, using its valid original row dates. Audits become due after
`max(6 seconds, 8*T)`, where `T` is the ordinary symbol duration, and are serviced
at the next ready FFT batch. This is a due cadence, not a guarantee of completed
broad coverage within that period. Helper renewal cannot postpone the due date;
report actual service and completed coverage separately. Initial probation and
accepted-link leases retain the shorter `max(6 seconds, 2*T)` interval. Reference
loss or stale or contradictory measurements trigger immediate ordinary broad
fallback; resource pressure must not starve a mandatory audit. Preserve the same
raw input and processing-frontier
accounting through conditional, saved-batch and broad work. Never substitute
silence, skip a required acquisition window or use helper loss as payload END.

The retained `a469ce48` HF trace spent 229,068 acquisition jobs in four broad-audit
group resolutions, 70.9% of its 323,085 recorded acquisition jobs, versus 259
conditional jobs. Its 31.286-second FFT readiness stride already exceeded the
old `2*T` period of 28.444 seconds. This motivates the revised audit cadence;
it establishes no gain for the changed source.

Accepted payload contexts remain ordinary mandatory work. Helper loss cannot
replace them with shorter candidates, reclaim them as optional or restart their
source/key stream. Future acquisition cohorts broaden as required. Retaining
ordinary interference tolerance under a failed same-link assumption is a
validation requirement. Bounded broad fallback alone is not evidence of
equivalent tolerance or identical latency to continuously scoring the original
domain.

A fresh causal measured helper reference now arms allocation-free sticky gap
protection for native ordinary rows, including new rows and legacy rows whose
optional cohort declined. This prevents a pending gap from retiring an accepted
owner before its ordinary full-symbol absence can finish; a later alias END
must still have that owner to complete. Reference loss stops protection for new
births, while protected accepted owners retain the full ordinary absence rule.
New-start admission keeps its original threshold and 16-track cap. Helper-off
retirement and unsupported compact-backend policy are unchanged.

New optional auxiliary-receiver construction yields to active ordinary payload
work. Existing accepted payloads in assisted ordinary receivers remain retained;
these rules neither move payload onto the helper waveform nor change framing,
source quotas or physical END.

### Phase correction and candidate arbitration

Candidate arbitration preserves accepted prefixes and their correct full
completion. Independently qualified distinct starts keep their owners; an unknown
fresh helper candidate cannot retract an established prefix.

The phase curve has a fixed 24-segment history. New correction requires
independently qualified phase and punctuation timing, dated before the corrected
future bins. Loss or capacity exhaustion closes fresh phase qualification and
sets the UI decline reason. Established tracks retain their frozen frequency
origin and final affine phase slope; the model is not flattened, and no earlier
bin is corrected retroactively. Frequency-only assistance and ordinary payload
processing continue without granting new phase authority. Preserve unknown
constant phase, ordinary frequency search and covariance handling. Neither phase
history nor metadata accepts a payload bit or supplies END.

Punctuation may assist slight timing under independently supported bounds,
without assuming that a 400 ms dwell starts payload symbol zero or identifies
UTC. Retain ordinary clock/private-phase/start alternatives and original key
addresses. Helper timing and phase corrections yield when unsupported or late;
frequency-only help remains useful without granting phase or timing authority.

### Preserve published owners within optional storage

Independently qualified distinct private streams must retain their accepted
prefixes and separate GUI rows, including when later helper uncertainty widens.
An unknown fresh helper-involved candidate cannot erase an established owner.
Published source-address metadata preserves the original stream/phase identity;
it does not use a received length as framing or change ordinary bit acceptance.
Every owner still needs ordinary six-second scored absence for END.

Fund that metadata only from diagnostic history in helper-involved receivers,
releasing its old storage before allocation. An example retains 118 diagnostic
entries instead of 128; actual Bank allocation must not increase, even transiently. Candidate peaks,
ordinary payload-bit capacity, configured source quota, recovery and required
scheduler checkpoints remain intact. Exact eight-versus-seven-bit same-PCM and
six thin-catalogue private cases passed; they are not normal/cable qualification
or a general false-alarm result.

For long compact EME geometry, optional pulse/cache representation selection may
use its existing cohort share only when proven smaller. The original receiver
grant, bit quota, detectors and complete geometry remain unchanged; no larger
representation or unsegmented raw fallback may be substituted merely to fit.
The constructor now passes the baseline workspace case locally. Aggregate EME
runtime and weak-helper precision remain unqualified, and keyed `T >= 60 s`
compact reception still declines the common-link cohort.

### Bounded replay progress and diagnostic wire observation

Compatible binary progress with the same ID/revision can share one packed,
verified `0`/`1` prefix plus at most 60 exact replay-frame checkpoints. Each
checkpoint reconstructs its original prefix and observation metadata; this is
storage compaction, not skipped progress. Incompatible prototypes, terminal
results, recovery and alias handling retain the generic staging path. The
351,760-byte shared pool, 124 owner slots and future-terminal reservations stay
unchanged. Wire format, source quotas, allocation authority and ordinary scored
physical END do not change.

The original actual nine-row text failure remains recorded. Private actual-class
checks passed for nine rows with 540 progress plus nine terminal observations
(98,852-byte reported peak), and 25 rows with 1,500 plus 25 observations
(259,668-byte peak). At `a469ce48`, the normal-path 7,296-wire-bit text case and
native text/attachment proofs subsequently passed. These private class checks are
not a substitute for those proofs or the still-pending broad checks.

An optional diagnostic callback observes borrowed ordinary detector bits before
Data XOR and framing. It defaults to null and cannot feed expected bit count,
framing or allocation decisions back to the receiver. A bounded collector exists
only in tests, outside the DSP quota; it compares the complete actual wire
without depending on the 256 MiB JSON trace cap. It carries no data on Helper
Carrier and supplies no payload or END authority.

### Helper lead, tail and physical coordinates

Provide helper-only lead and tail of at least two seconds, aligned to absolute
400 ms dwell boundaries. They surround the entire ordinary waveform, including
training, pulse padding and drain, rather than only valid payload symbols.
Extend the tail as required by real buffering: ordinary scored absence,
sample-rate and pulse guards, physical FIR support and the initial FFT aperture
must all fit. A two-second minimum alone does not establish that coverage.

Keep the ordinary waveform, Rate, carrier, source bits, key/epoch addresses and
logical coordinates unchanged. Progress describes the ordinary source, not the
extra helper airtime. Hardware scheduling must leave room before a future
ordinary epoch for the helper lead. Simulation must use explicit physical and
ordinary coordinates rather than retiming source/key lookup; these lead-mode
coordinates are still being validated. Helper-only padding remains real output,
not synthetic payload absence. Helper loss, capture exhaustion and EOF cannot
supply END: only ordinary fully scored absent-symbol durations can do that.

### Report measured work and qualify weak references

The runtime's actual row verdict in `helper_status` is the sole narrowing
classification. An active-frequency flag adds only “reference aligned”; presence
and aligned frequency do not establish pair reduction or applied phase. The
selected/original counters are independent maxima across rows, displayed as
“pair maxima across rows: selected X, original Y”. They may describe different
rows, so their ratio must not classify narrowing or estimate speedup.

Report actual conditional/audit jobs, phase-applied state, decline reason and
completed audit/fallback progress separately. A presence row, planner count or
enabled control is not measured CPU relief. Ordinary payload SNR/probability and
completion meanings stay unchanged; full-receiver measurements must include
observer, filter and audit overhead.

Current 32-second weak EME observer checks at the 205/210 dB path-loss settings
had minimum frequency interval half-widths about 2.277/3.183 Hz, with no phase or
timing assistance. These are empirical observer limits, not physical precision
floors or qualification of ordinary weak EME payload reception. Pair reduction
also depends on the simultaneous measured centre and the full 2D coverage check.

At the reported 20.7609 dB-Hz weak EME helper budget, an earlier observer
measurement had minimum interval half-width about 2.2992432 Hz. Its simultaneous
centre was not logged, so width alone cannot establish zero pair reduction:
a centre near +0.58 Hz could trim a small far-negative edge. The historical
one-dimensional mask's padding thresholds do not qualify the current 2D selector;
current selection requires its full closed-slab coverage check. This is neither
a measured current gain nor a physical precision floor. The reported `0/3565` result
belongs to the retained failed private full-union estimator, not this observer
interval alone.

A separate `d072` observer diagnostic used the retained helper-only 32-second,
24 kHz fixture at 20.76091259 dB-Hz, +0.58 Hz CFO, +0.0001 ppm clock error,
0.03719-second delay and 0.007071 phase random walk. Public parity was 165/165,
with minimum interval half-width 2.3018353 Hz, distinct from the earlier result;
there were no phase/timing/metadata publications or noise-only references.
All 160 long-frequency evaluations retained every estimator frequency cell;
in 159, the model allowance met or exceeded the maximum possible chord cost
`2*wsum` (median ratio 1.308). Twelve-point history was full in 154 calls but
spanned only 2.12 seconds median/2.44 seconds maximum, without gap, recenter or
cell-cap failure. This identifies an algorithm/model-allowance limit, not a
physical floor or ordinary EME qualification. It does not establish original
RF-pair retention without simultaneous centre/mask mapping. Production observer
CPU was 0.110524 seconds for 32 seconds of input, excluding diagnostic logging
and full-receiver work. Hashes are in
`local-evidence/helper-v4/native-precision-private/weak-eme-diagnostic/results.sha256`;
no algorithm change is implied by this diagnostic.

The keyed long-symbol compact gate and aggregate workspace/runtime remain
separate limits; the current keyed `T = 128 s` case has no supported common-link
compact backend. No backend change is implied. Estimator changes need
wide/noise/interference cases, clock and
stochastic-phase accounting, valid input dates and bounded resources. Strong-tone,
metadata or observer-only results do not qualify weak EME reception.

The historical native HF observation's approximately +/-0.528 Hz interval is not a physical
precision floor. It comes from the current conservative 40 ms free-quadratic
endpoint fit, with eight-standard-error and model allowances. Its mapped radius
of about 0.565 Hz retained all nine original audio-clock lanes in that case. Optional phase
bridging needs a forecast of at most 0.25 rad over at least 0.2 seconds; that
frequency interval alone contributes about 0.663 rad at 0.2 seconds, so declining
phase assistance is consistent with its gate.

Private analysis of the public hardware PCM at the exact +/-90.45 Hz prior
identified Robust data energy as a strong cause of the wider helper interval.
Payload residual variance was about 852 times the lead/tail value, with 99.857%
of residual energy in the public 1,275--1,725 Hz Robust band. Median interval
half-widths were about 0.016--0.017 Hz in lead/tail and 0.48 Hz during payload:
data energy inflates the raw white-residual covariance estimate. Independent
finite-operator controls across six 2 ms windows confirmed data-band aliasing
in weak-tone rejection without changing thresholds.

The working source now accepts an optional public `OccupiedBand` for nuisance
conditioning. Live supplies the union of every public receive profile across
the full requested RF/audio-clock domain, even when the ordinary bank is limited;
it uses no decoded payload or current-frame information. Refinement follows an
already accepted raw tone publication and cannot grant fresh admission. At most
27 DFT nuisance frequencies are used; an invalid, too-wide, overlapping or unsafe
fit retains the raw estimate. Receiver-owned storage plus project scratch stays
within the unchanged 9 KiB bound; external runtime/provider allocations remain
separate. The current application build and selected focused checks passed;
broader current receiver/precision coverage remains pending. Historical ordinary-
cover checks preserved strong off-grid/weak joint cases and the `001` to `001000`
continuation. Refined precision and false-alarm
behavior remain unqualified. Private compact stable/loss checks passed,
but repeated-audit work failed; that path is neither integrated nor qualified.

## Helper-side tradeoffs and limits

- Metadata stays at 125 baud with actual-length airtime: callsigns use at most
  56 ms and clear/encrypted timestamps at most 24 ms per complete dwell. Dense
  decoding is optional and may fail while presence or frequency help remains
  useful. Length-preserving encryption provides no authentication; optional
  spare-bit CRC is limited error detection. Callsign/time never mix and do not
  prove UTC, freshness or association with a payload.
- Acquisition begins around configured helper geometry. Full-band unknown-helper
  discovery and co-channel cancellation remain separate work. Adjacent High/Low
  remain real interference in the physical mixture; filters have real wanted-
  band and correlated-noise costs. Closest digital placement does not guarantee
  that a radio's narrow SSB filter passes every data, guard and helper component.
  Do not alter Rate at 100 Hz, 3.6 kHz, 12 kHz or other settings to make room.
- The common-reference policy is conditional on the stated quiet-spectrum and
  same-link assumptions. Independent broad audits bound that reliance; they
  consume real receiver work. Do not weaken ordinary interference tolerance,
  discard clock/epoch/key alternatives or claim GPSDO/GPS Clock replacement
  merely because a helper is present. Dynamic phase/timing still require their
  own qualification, and helper loss never establishes payload absence.
- The 90 dB ratio option does not certify 16-bit DAC/RF dynamic range. Oscillator
  phase noise and a powerful helper's sidebands can contaminate weak data.
  Physical simulation must retain shared noise, phase and frequency impairments.
  Hardware compression, spurs, filtering and calibration need measurements.
- Optional resources must fit inside existing receiver and source contracts.
  Retain correct accounting for saved batches, raw history, FIR-delayed input,
  ordinary error recovery and accepted payload growth. No additional source
  quota, RAM allowance or disk/I/O contract is implied by this plan.

## Qualification and remaining work

Actual build, normal/GUI/sanitizer, sampled-simulation and cable results bind only
their recorded source and binary identities in [validation](validation.md).
Private class checks are narrower evidence; original failures and incomplete
captures remain recorded there. Earlier source-bound performance or cable passes
do not qualify a changed candidate, broad false-alarm behavior or the exact weak
HF/EME cases. Complete wire, prefix, framing, recovery and physical-END coverage
must be checked for the final source, including long text and attachments.

Reviewed capacity and base-template cache fixes are integrated. Private capacity
checks passed 21 commands including ABI/recovery boundaries. Cache checks passed
114 nonprimary bitwise cells, two exact 16-bit flows and 80 actual-HF nonprimary
cells; these retain their narrower scope.

The `a469ce48` manual long-text proof recovered the
complete authenticated source and all 7,296 wire bits on the same pending row
through ordinary physical END, without health failures. Phase was applied, with
4,940 bytes reported inside the 7,168-byte helper allowance. Its 1,764.93 s wall
time (about 29.4 minutes) establishes functionality, not real-time operation or
a speedup over earlier attempts.

The historical `f47fa339` matched encrypted Auto/rolling binary pair recovered all 1,024 source/wire
bits in both runs, without a TX timestamp, at 30 Hz CFO, 100 ppm clock error and
about 0.5025 degrees per square-root second phase diffusion. Workspace matched.
Independently reconciled acquisition template jobs fell from 5,809,320 to 794,792
(7.309233 times less), with 563,000 conditional and 231,792 audit jobs when enabled.
The trace-bound `candidate-v5-long-binary-work-check.json` passed in 3.523 seconds,
joining all seven record classes and all 76 OFF/77 ON numeric instances with no
dropped records. These counters
exclude retained full-RF scalar continuation fits and cache preparation; they
measure neither all work nor frequency coverage. Whole-process CPU was
480.734253/128.263774 seconds and wall time 87.396753/64.160263 seconds,
3.748013 and 1.362163 times less respectively. Input transforms increased from
44,010 to 48,614. Peak DSP was 1.372208/1.372669 GB; health, lifetime prefixes,
pending-row progress and ordinary physical END passed in both runs.
Each run had three rows including two secondary candidates, so this is not
false-alarm qualification. Legitimate raw key epochs were 1,800,000,000 and
1,800,000,005; helper lead caused different wires, each matched to its own actual
TX. Historical evidence is `candidate-v5-long-binary.log` and its bindings under
`local-evidence/helper-v4/`; the earlier text proof remains
`rolling-qualified-long-text.log`. See [validation](validation.md) for full scope.

The historical `f47fa339` native text96 cable run strictly passed all 3,648 wire bits
and authenticated 96-byte text. Row 2/revision 1 retained 11,510 correct prefix
observations through END after 6.050667 seconds of scored absence. Health checks
were clear, the light trace was complete without drops and PCM was clean. This
is functional coverage without a numeric-work, real-time or CPU-gain claim.
Evidence is `cable/candidate-v5-long-private/candidate-v5-full-text96-v1/result.json`
under `local-evidence/helper-v4/`.

Historical `f47fa339` native attachment129 also strictly passed all 4,864 wire bits,
including beyond the 4,096-bit preview, and the authenticated 129-byte file kind,
filename `cable-tail-129.bin` and data. Row 2/revision 1 remained stable through
pending/completed observations, with 19,575 correct retained prefixes and END
after 6.039333 seconds of scored absence. The full configured Auto RF/audio-clock
domains were retained; frequency plus timing-qualified phase were observed,
followed by bounded fallback. Health, complete no-drop trace, clean 910-second
PCM, unchanged 441 source inputs/binaries and reaped-child checks passed. This cable proof supplies no
numeric-work, CPU-gain or real-time claim. Its result is
`cable/candidate-v5-long-private/candidate-v5-full-attachment129-v1/result.json`;
see [validation](validation.md) for bindings and scope.

The retained `a469ce48` native text96 and attachment129 proofs also strictly
passed full authenticated source/wire, same-row prefixes and physical END, with
clear health/PCM/seal/hash gates. The text run's maximum 340.952-second backlog
limits it to eventual functionality. These earlier proofs keep their own source
scope; see [validation](validation.md) for bindings and details.

The historical `f47fa339` HF simulation strictly passed all eight encrypted wire bits
and exact source `11010110`, with primary row 2/revision 8, strict prefixes and
ordinary physical END. It used normal encrypted Auto/rolling reception without
a known TX timestamp: Rate 36, interleave 10, crystal/IC-7100 at 30 MHz, path
loss 180 dB, 4 W data and 1,500 W helper. Reported pairs narrowed from 8,181 to
51 while all nine original audio-clock lanes remained. Acquisition counters
reported 94,017 conditional plus 57,267 audit jobs, 264 input FFTs and 180
instances; phase was not applied. The complete trace had no drops and input
health checks were clear. `search_limited=1` remained valid for the ordinary
requested +/-90.45 Hz versus represented +/-7.97985 Hz catalogue.

END feed 853,185 minus payload end 715,414 provided 22.5116 seconds, covering
a full 14.22-second absent symbol plus buffers. Process CPU was 1,824.504075
seconds, wall time 470.716839 seconds and peak DSP 1,639,494,085 bytes. This
helper-on functional pass is neither real-time nor a matched CPU-gain result;
acquisition counters retain their limited scope. All source/binary/key/fixture
bindings remained unchanged. See `candidate-v5-hf-eight.log` and
`candidate-v5-hf-eight-bindings.json` under `local-evidence/helper-v4/` and
[validation](validation.md) for the bound result.

The retained `a469ce48` controlled HF simulation keeps its original exit-1 result:
the fixture rejected a secondary completed-to-pending transition at a new
revision, which the existing contract permits. The repaired fixture keeps primary
lifetime, complete wire, prefixes and END strict; its focused geometry check
passed. Read-only checks of the original sealed trace subsequently passed all
eight physical wire/source bits and historical END checks for its three rows,
with primary row 2/revision 8. The
`rolling-qualified-hf-eight-posthoc` bindings, artifacts and log retain this
scope: post-hoc analysis is neither a new reception nor a new health run.

The `a469ce48` exact-HF cable run remains incomplete, with strict result 2 after its
900-second window: seven of eight bits, exact prefix `0010110`, remained pending
on row 2 with no ordinary END. TX passed; `search_limited=1` was the sole health
flag. The complete trace had no dropped records or cap failure, and the
910-second PCM capture had no clipping or XRUN. Hashes and child cleanup passed.
See [validation](validation.md) for
`cable/rolling-qualified-full-hf8-on-v1-result.json` and the post-hoc bindings.

The historical `f47fa339` HF8 cable run showed an exact stable eight-bit final GUI
entry in its 900-second window. Its trace was capped, dropped records and lacks
the eighth-bit/END trace proof; `search_limited=1` remained reported. This is
limited GUI evidence, not a strict native pass.

The original normal core stage retained 318 passes and five failures: four
disposable GPG sandbox/IPC setup failures and the concurrent attachment case.
That case had all 2,432 bits pending but only 4.035 seconds of scored absence at
its deadline, below the 6.08 seconds needed; no payload fault was demonstrated.
The unchanged attachment check passed both legs in isolation in 167.35 seconds.
Only its CTest scheduling is now serial; the 90-second leg and 240-second CTest
limits and assertions remain unchanged. The focused host-CI apt check passed
30 cases in 10.068 seconds. Original logs/JUnit remain in `final-normal-v5`.
The fast stage was interrupted with seven passes; five remaining scopes were
unrun. That original v5 run remained incomplete.

Historical e2/v6 normal qualification passed all 379 registered tests exactly once:
Core 323, Fast 36, Frontends 3, Web 13, Live 2, Preparation 1 and Calibration 1,
without failures or omissions. Source/binary/profile and process-cleanup audits
passed. The report is `local-evidence/helper-v4/final-normal-v6/execution-result.json`,
SHA-256 `1675a40e3482176112e3338a88019e543d44debe310444d360c2a341ddf2a7ba`.

The historical e2 ASan/UBSan build and provenance checks passed after 7,700.597 seconds.
All seven original sanitizer scopes ended: Core 315/321 passed with six failures, Fast
34/36 with two, Frontends 3/3, Web 13/13 including `web_live`, and Live 1/2
with one, plus Preparation 0/1 after a 9,000.22-second timeout and Calibration 0/1
after a 10,800.21-second timeout. The final wrapper exited 1 and was reaped.
Final audit is complete: 366/377 passed, with eleven failed/incomplete and empty
official omission lists. All 18 source/auxiliary/instrumented receipts and owned
cleanup checks are valid. Preparation's wrapper ended
after 9,000.535 seconds. Core and Preparation reported no sanitizer diagnostics,
but Fast includes an actual pre-existing UBSan finding. All eleven original failures
remain recorded; full sanitizer qualification has not passed. Preparation
completed reservation policy and all three arithmetic serial/parallel comparisons,
but was killed during `shared.serial`; later shared comparison, small/tiled,
cancellation and no-false-END work remains incomplete/unrun. Calibration also
left later shaped/null/default controls incomplete; no sanitizer diagnostic
before its timeout is not a pass. These original failures and partial results
remain historical; the current Fast empty-write fix is integrated, with current
focused sanitizer passes but no full-suite qualification. That headless instrumented
profile registers 377 cases: `gui_self_check` is
not registered with `DATAPUMP_BUILD_GUI=OFF`, and `build_speculation_codegen`
is intentionally not registered under `DATAPUMP_SANITIZERS` because it is an
optimized-code witness. Both passed in the normal 379-case run; no sanitizer
result is claimed for them. See [validation](validation.md) for case classification
and profile details. Historical v7 focus passed 15 CTest and all 24 Fast CLI
cases; its normal failures retain their own scope. Current v8 normal passed all
381 cases and all 24 Fast CLI cases, with no omissions/limits and valid audits.
Current sanitizer focus passed 15 named cases, all 24 Fast CLI checks and three
gap checks; 364 other headless cases remain unselected, so full-suite qualification
is still unclaimed. Current Native FLTK passed 3/3, recorded in
`native-v8-prep/execution-v8/execution-result.json` (hash begins `9b317cf5`).

Current physical text96 strictly passed exact 3,648 wire bits, authenticated
96-byte source, the same pending row 2 and ordinary physical END, without health
errors. First source was at 462.300 seconds, TX drain at 171.861 seconds and
peak backlog 325.855 seconds: functional completion, not real-time or cable
CPU-gain evidence. Its result is
`cable/candidate-v8-prep/full-long-text96-v1/execution-result.json` (`76d21e76`),
with reader hash beginning `d3331414`.

Current physical attachment129 also strictly passed all 4,864 dated wire bits
and authenticated 129-byte `cable-tail-129.bin` on the same pending row 2, with
19,302 correct prefix polls and END after 6.067333 seconds of scored absence.
The 910-second recording had no XRUN/clipping, the complete trace had no drops
and health checks were clear. Peak backlog was 607.766333 seconds, without a
real-time or cable CPU-gain claim. The `full-attachment129-v1/execution-result.json`
hash begins `c9db2f61`, with reader hash `32a8442d`; see [validation](validation.md)
for full source bindings. The controlled 1,024-bit and exact-HF8 simulation
results retain their stated scopes. Current HF8 physical cable confirmed exact
`00101101`, identity, canonical phase/seed and scored END on completed row 2.
Its unchanged strict audit failed because full eight-bit pending/all-prefix
coverage was not observed. Native acceptance arrived in chunks of 2, 3, 2 and
then 1 bit with END already scored; shared commits were 2/5/7 bits followed by
8-bit completion, all on row 2. There was no earlier accepted eight-bit pending
state. This is a fixture coverage gap, without a demonstrated runtime progress
gap; ordinary completion does not require fabricating a pending state after
valid physical END.

The result remains failed/limited (`full-hf8-v1/execution-result.json`, hash
`1c1e61ff`, reader `f1257e59`), also retaining baseline `search_limited=1`.
The 900-second Controller run and 910-second recording had no other health
failures, clipping or XRUNs; its complete 521-record trace had no drops. Maximum
backlog was 774.169935 seconds, without real-time or cable CPU-gain proof.
The reference was active with up to 220 selected pairs from 8,181, while phase
assistance declined. Full requested +/-90.45 Hz HF coverage is not qualified.
Broader performance, full sanitizer, weak EME and remaining Rev qualification
are pending. No old-source
equivalence or full-coverage bridge exists.
Strict exact-HF native and weak EME limits also
remain open. These configuration-specific
results do not establish payload-SNR gain, native CPU gain, RF accuracy,
general throughput or real-time qualification.

Acceptance requires the following focused evidence before broader regressions:

- Reproduce the 64-to-62 arbitration replacement and `0101`-to-`010`
  affine-phase-slope failures with actual ordinary receiver classes, then check
  matched corrected output, complete initial prefixes and final bits, next-poll
  progress, same-row completion and no premature END. Include actual helper loss;
  a controlled phase fixture alone does not cover it. Preserve original failure
  evidence and exercise ordinary error recovery.
- Exercise one executor's observe/track/conditional/saved-batch/full-audit
  transitions with current and newly admitted keys, profiles and rolling epochs.
  Check immutable original clock/frequency catalogues, frozen per-track frequency
  origins, full ordinary start alternatives and exact real-input frontiers.
  Arm the first audit due date from the first-ready original batch and valid
  original row dates; use `max(6 seconds, 8*T)` and service it at the next ready
  FFT batch. Check actual coverage without claiming a hard completion deadline.
  Keep probation/link leases at `max(6 seconds, 2*T)`. Repeated helper renewal
  must not postpone audits;
  reference loss must immediately resume broad acquisition. Validate ordinary
  interference tolerance independently of bounded fallback. Test unrelated tone, stale/future
  measurements, no useful reference, conditional failure and resource pressure.
- Exercise integrated large cohorts and new admission beyond the former
  240-row boundary. Compare cached/uncached continuation
  paths and verify every original fit and independent suffix decoder remains.
  Helper frequency must never merge or suppress independently qualified starts.
- Show actual conditional work reduction together with bounded broad audits and
  ordinary payload progress. Compare matched helper-off and helper-on full
  receivers, recording observer/filter/phase overhead, latency, CPU and memory.
  Do not derive speedup from a planner count, a strong presence row or one small
  observer benchmark. Unsupported backends must retain ordinary behavior.
- Test at least two seconds of dwell-aligned helper lead/tail around the full
  ordinary waveform, with longer tails when FFT/FIR/absence buffering needs
  them. Check absolute physical scheduling and explicit simulation coordinates,
  unchanged training/pulse/drain, Rate, source/key/epoch addresses and source-only
  progress. Exercise short/long symbols, chunk boundaries, stop, live device
  resampling and simulation noise tails. Helper loss or EOF must never finish
  an ordinary reception.
- Qualify frequency-only precision at realistic weak levels, including the
  reported 20.7609 dB-Hz EME budget, wide initial error and no-useful-interval
  cases. Retain clock-transfer and stochastic phase/model uncertainty; do not
  infer a narrow interval from presence alone. Test raw physical PCM, noise,
  interfering payloads, real-carrier images and dates under bounded memory/CPU.
  Phase, punctuation timing and metadata each need independent controls.
- Decode long text and attachments through unchanged ordinary framing,
  decompression and recovery, with full configured quotas and retained payload
  lifetime under helper loss or pressure. Preserve independent short/raw/wire
  vectors, exact binary bit counts and six seconds of fully scored physical
  absence. Tone and metadata must never create bits, copy/save authority or END.

Retain the baseline metadata, waveform and placement controls: independent FF1
and compact frame vectors; canonical widths, optional CRC, padding and cipher
failure; punctuation guards and actual physical timestamps; same-link sampled
impairments and helper-off exactness; actual shaped passband with unchanged
Rate; existing provider-rate/past-epoch mixed-output fallback and helper-only
failure; stable helper rows, encrypted callsign hiding and Live/duplex/noise
controls. Placement/minimum-rate fallback preserves calibrated data gain and the
ordinary wire/key epoch; retain invalid-setting, helper-only and output-gap
controls. No retry
is allowed after an actual output gap.

Run affected normal contract, shared/native GUI, audio and browser checks for
the final source. Selected sanitizer tests do not replace full applicable
sanitizer coverage or throughput qualification. Native rendering needs a display;
duplex audio and physical DAC/RF behavior need real measurements. Record source
and binary bindings, actual results, original failures, setup repairs and omitted
coverage in [validation](validation.md) after the source and checks settle.
Publication, certification and promotion remain separate operations.
