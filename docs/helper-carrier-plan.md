# Helper Carrier implementation and qualification plan

Updated 24 October 2026 for adjacent placement, helper setup fallback and
bounded primary conditional acquisition, in addition to frequency/phase,
metadata and physical simulation. The operating reference and wire
layouts are in [Helper Carrier](helper-carrier.md); actual checks and limits
belong in [validation](validation.md). Initial source investigation used
`a858f6f31d19431d3d15b077752f67a30cd1866f`; the first experimental implementation
was committed as `b47c7a0ebd6a29d21ffa8748d5bc7ffbbeb7e942`. Neither revision
identifies the current uncommitted extension or a qualified release. Its current
source base is `b2d3539919f0955cee94986269ccbd6402e37b1b`; ongoing source changes
and their actual acceptance results must be distinguished from that base.

## Governing requirements

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
original admitted search state and replayable input until an ordinary detector
accepts its first payload bit. Resolve bandwidth, dynamic range and CPU conflicts
on the helper side. Successful dense
APSK metadata decoding is optional and never gates helper presence or payload
reception. Robust message payload bits never move onto the helper waveform:
that waveform contains only tone, punctuation and optional callsign/time. The
assisted receiver is an ordinary Robust receiver guided by helper measurements,
with full long-text/attachment framing, decompression, recovery, bit progress and
fully scored six-second physical completion. Insufficient resources disable
optional assistance instead of removing receive functionality. The original
decoder remains available without helper assistance.

## Current implementation

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
   Mixed Live output retains ordinary unscaled payload PCM if helper placement,
   generator setup or its requested device rate is unavailable, with a visible
   helper-unavailable reason. One ordinary-rate retry is allowed only before
   the first PCM callback. Actual output gaps remain TX failures without retry.
   Helper-only output fails when its helper cannot be generated.

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
9. Primary conditional bank replacement is limited to a fixed configured
   transmit timestamp with unchanging epoch admission, using the same
   fixed-bank predicate as ordinary refresh. Replacement also requires explicit
   **Complex only**, an actual native continuous backend and a structurally
   identical bounded normal `Error`-reset recipe. Clock sync, FHSS, DSSS/Interleave,
   calendar, compact, configured drift and differential paths are excluded from
   primary replacement. Auto, energy,
   differential, compact/correlator and scheduled paths retain the ordinary bank,
   as do rolling/unknown epochs; any additional helper lane must be independently
   eligible. Within an eligible fixed bank, the route targets coherent ordinary
   profiles with at least 512 oscillator hypotheses. A fresh measured helper can narrow
   frequency/clock scoring to at most 257 alternatives per narrowed profile,
   only with a substantial reduction and enough bounded workspace. All admitted
   keys, profiles, epochs and original start alternatives receive replacement
   ordinary contexts before switching; inexpensive companion profiles can keep
   their full searches. Original continuous clock coverage remains, including
   residual error in helper-to-data frequency transfer. Only a whole domain
   no wider than one quarter-chip cell can use one equivalent clock hypothesis,
   with its residual still included in the frequency interval. The primary
   route uses ordinary coherent decoding without nearest-epoch or punctuation
   alias pruning.
   Before qualification, expensive-profile history attempts to retain the full
   original initial FFT/acquisition aperture within min(64 MiB, bank capacity/8)
   and actual surplus. Switching declines if retained history starts after any
   original receiver origin, so late qualification cannot discard its early
   input. The exact original bank remains dormant and charged until an ordinary
   detector accepts a payload bit. It remains a required processing consumer
   while raw input awaits replay. A bounded real-PCM ring covers its initial
   acquisition aperture plus guards, within min(64 MiB, bank capacity/8).
   Precommit replay eligibility bounds a locally configured physical capture
   aperture and its resources, never received length, total message size or a
   reduced source quota. Full configured ordinary source quotas remain unchanged,
   with their ordinary receiver grants retained. Separate immutable cold-start
   and replay grants preserve normal `Error` recovery. Source review and focused
   finite-resource, full-payload, fallback, rolling-retention and forced-reset
   checks and the normal application build passed. Selected ASan/UBSan paths
   also passed; full regression and broader sanitizer qualification remain open. Helper resource planning declines
   assistance rather than introducing `FAIL: transmit` or a global capture reset.
   Reference loss/inconsistency, failed initial acquisition, history pressure or
   unavailable conditional coverage before acceptance restore those same
   original contexts and replay every retained raw sample. No synthetic silence
   or END substitutes for input. A continuously present unrelated tone does not
   repeatedly trigger new conditional attempts. The first ordinary accepted bit
   commits that same full ordinary decoder and releases the backup/history.
   The precommit replay bound no longer constrains accepted payload lifetime;
   it introduces no helper-specific message-length or source quota. Long text,
   attachments, framing, decompression and recovery remain ordinary behavior.
   Helper tone, punctuation and metadata cannot commit it. After commitment,
   helper loss retains accepted payloads and broadens future cohorts. All payload source
   quota, framing, decompression, recovery, next-poll progress and physical-end
   rules remain ordinary Robust behavior.
   The rolling/unknown-epoch and smaller-search additional optional lane remains
   available separately alongside the full ordinary bank.
   It preserves native coherent FFT or explicitly selected compatible compact
   correlation; long FFT fill does not cause an automatic backend switch. The
   36 Hz/DSSS 10/SF 256 profile therefore retains about 45.5 s fill for 14.22 s
   symbols. Actual surplus after ordinary growth/admission promises bounds this
   lane and its stricter, lifetime-spent evidence budget. First-bit acceptance
   promotes that same context to mandatory retained payload before publication
   or optional reclamation. Helper loss cannot discard accepted payload or
   supply END. This smaller lane's optional native FFT pre-roll retains up to
   16 s/1 MiB, charged
   to the same helper allowance, without backdating phase correction, advancing
   ingress or creating missing samples. Compact correlation does not replay it.
   Calendar persistence requires the maximum ordinary constructor grant and
   node/control costs to fit; speculative history/acquisition otherwise yield.
10. An adjacent helper inside the logical capture band can use a real linear
    bandstop FIR in assisted receivers, centered on the measured helper with
    225 Hz half-width. Fully observed centered windows preserve dated receiver
    coordinates; EOF never adds zero padding. The original path and fallback
    keep raw PCM. Filtered noise is correlated, so presence-tracker admission
    bounds do not certify payload false-alarm rates after this frontend.
    Qualified punctuation timing removes the known helper phase sign.
    Compatible coherent FFT paths can rotate completed complex bins by measured
    residual phase wander, preserving unknown constant data phase and per-bin
    covariance eigenvalues. Post-admission activation uses a continuous reanchor
    at the committed processing frontier, preserving initial carrier offset and
    applying no later correction backward. Timing-search bounds stay fixed at
    admission; later phase qualification does not dynamically narrow them.
    References must predate the first sample of a corrected bin. Other detector
    paths keep their ordinary phase processing. The fixed-trace covariance
    argument does not certify adaptive helper-derived lifetime false-alarm rates.
11. The additional optional lane ranks one advisory private epoch from the
    earliest privately retained qualified tone support, accounting for training,
    padding and propagation. At most 40 ms observer slices preserve that
    measurement's original date; public callbacks keep their usual cadence.
    Weak qualification can cross a second boundary and rank the wrong epoch.
    Other epochs remain in its ordinary bank. In contrast, the primary
    eligible fixed-bank conditional route retains every already admitted epoch and
    its original start alternatives; it does not use this ranking to discard candidates.
    Within one optional epoch, qualified dwell timing can refine a bounded
    location hypothesis: for receiver-local dwell D, integer epoch E and retained
    rate r, B = D + (0.2 * (E % 2) + 0.4 * k) / r. PatternStartWindow keeps
    B + phase_scale * p over retained clock/private-phase envelopes. Bounded
    modulo-.4 alternatives can narrow that extra lane to the nearest alias.
    This does not identify symbol zero with a dwell boundary or establish UTC,
    GPS qualification or source-bit retiming. Timestamp metadata is unnecessary.
12. Link Planner publishes the separate reference budget: effective ratio-
    limited power, received dBm, C/N0, 10 Hz tone SNR and audio center. With 4 W
    data, a 90 dB ratio ceiling and -164 dBm/Hz noise, 40 W/180 dB loss yields
    20.02 dB tone SNR; 1.5 kW/210 dB loss yields 5.76 dB, below the 6 dB level
    gate. Adequate predicted level alone does not establish measured precision.
    For a large eligible search, prospective conditional counts and the maximum
    measured frequency uncertainty needed to fit 257 alternatives explain the
    acquisition policy. They are geometry calculations, not actual observations
    or a C/N0-based speedup prediction. Runtime resource, history and reference
    requirements still apply. The planner distinguishes fixed timestamp
    replacement eligibility, including explicit Complex only and generic runtime
    backend/recovery checks, from ordinary-bank retention with an independently
    eligible additional lane. Payload SNR/probability curves and CPU estimates
    keep their original meanings; no assisted sensitivity or CPU gain is invented.

## Helper-side tradeoffs and limits

- The metadata rate is 125 baud, with one to seven symbols ending at 388 ms.
  Actual callsign characters use at most 56 ms; clear/encrypted timestamps use
  at most 24 ms. Optional CRC occupies existing spare bits only. Dense decoding
  needs a clean, strong helper and is not promised at low SNR. A 1.5 kW/4 W mixed
  capture can fail metadata quality checks while still supplying presence and
  frequency/phase references. FF1 provides length-preserving confidentiality,
  not authentication. CRC is limited error detection, not reliable wrong-key
  rejection. All metadata is unauthenticated and provides no freshness, UTC truth
  or tone/payload association. Timestamps are displayed without clock adjustment.
- Helper acquisition is around a configured center within +/-20 to +/-100 Hz.
  Peers match power ratio/geometry because automatic placement depends on them.
  Full-band unknown-helper discovery and co-channel cancellation are deferred.
  Adjacent High/Low remain real interference in capture; optional bandstop
  filtering has its own calibration limits. Co-channel overlap is discouraged.
  Closest digital placement does not guarantee a fit inside a 3 kHz SSB filter.
- Assistance is selective. Primary replacement requires a fixed timestamp and
  unchanging epoch-admission bank; rolling reception retains the ordinary bank.
  The eligible primary bank retains every admitted epoch/start alternative and
  declared clock domain, while the additional
  optional lane may try one advisory epoch and nearest dwell alias alongside
  its original bank. Neither proves UTC or replaces GPS Clock sync generally.
  A single tone cannot separate RF error, Doppler and sample-clock error.
  Dynamic per-bin phase requires an eligible coherent FFT path. Nondeferred
  Fake: FCC can use its existing gated template; true scheduled, Symbol_Burst
  and deferred modes retain helper transmission/observation without assisted
  acquisition. Helper loss or lag cannot substitute for scored payload absence.
- Conditional acquisition temporarily scores a measured frequency strip rather
  than running the broad original bank simultaneously. Exact dormant state and
  raw replay preserve fallback until the first ordinary accepted bit, after
  which accepted contexts have the full normal payload lifetime and future
  cohorts can broaden. The source must preserve processing-frontier accounting
  for dormant and FIR-delayed consumers as well as memory accounting. Optional
  work yields when ordinary payload resources cannot fit. A lower hypothesis
  count is not itself real-time CPU qualification or weak-signal sensitivity;
  aggregate CPU and latency need matched full-receiver measurements. No actual
  C/N0-to-speedup claim follows from the planner's prospective counts.
- The 90 dB ratio option does not certify 16-bit DAC/RF dynamic range. Physical
  oscillator phase noise is shared: strong helper sidebands can leak into data
  even with correct digital placement. Those effects remain visible in physical
  simulation, rather than being removed from the channel model. Hardware phase
  noise, compression, spurs and gain calibration require actual measurements.

## Validation and acceptance

As of 24 October 2026, final source qualification is pending. The earlier
`80ac7f2f` exact HF probe acquired no ordinary payload within its 600 s bound.
The later `22125d5b` diagnostic recovered `001`/`e` in the same GUI row but
retained failing receiver health and did not separately observe the initial
`0`/`00` prefixes. This predates the fixed-epoch containment policy and cannot
establish a benefit for the final rolling route. Its EME probe was cancelled
incomplete. These source snapshots
identify diagnostic results, not a final qualified source. The current-source
48 kHz physical Controller check at public Volume 20 observed clean adjacent
40 W helper output and exact ordinary `001` pending-to-completed reception on
the same row while capture remained open, with no receiver-health failure,
missing or unexpected bits. Its strict runner exited 3 because separate `0` and
`00` prefixes were not observed in polling; individual-prefix/next-poll coverage
remains incomplete. It does not establish weak-RF, default-Auto or GPSDO-
replacement benefit. The earlier clipped Volume 100 return remains evidence.
A proposed 64-point frequency history failed
24 dB-Hz truth coverage and is not accepted. The experimental recipe bootstrap
was withdrawn from production following independent review of a native
unprocessed-queue (`Q - R`) overrun and unreserved simulation DSP use. Its patch
is preserved in ignored evidence for future work; it is not implemented behavior
or a qualified fix for the exact weak EME case. A private joint raw-bin frequency
estimator remains unaccepted, with no production integration planned for this
candidate. Payload-contamination handling and the stochastic phase prior need
a supported argument and validation before any future integration; private
diagnostics do not establish production payload SNR or CPU gains.

The later normal placement application build (`39954499`) passed; it predates
the accepted containment source. Its actual
48 kHz output-PCM frequency regression and complete fallback suite passed after
test-only gate/UTC setup repairs; their original failures remain retained in
[validation](validation.md). The 294-registration normal run completed with
288 passes, five setup failures and a 1,500.12 s
`scheduled_parallel_preparation` timeout. Exact web and native display retries
passed all five setup-failed cases with unchanged source and binaries, giving
293/294 distinct cases passed. Physical completion and strict prefix coverage
have the separate limits above; preparation coverage remains failed/incomplete
and no full matrix pass is claimed. Separate focused ASan/UBSan checks passed
for the accepted source, including framed batch transfer and three-bit native
Live success/replay/Error paths. Full-payload Live, GUI/CLI entry points,
hardware/RF, throughput, leaks and the full sanitizer suite remain outside that
selected scope.

The existing safe assistance routes remain optional. Full ordinary receiver
grants, retained history and any conditional receiver must coexist within the
configured resource contract; otherwise helper work yields. Ordinary framing,
source quota, bit progress and scored physical END remain intact. This decision
adds no RAM allowance or source quota and changes no disk/I/O contract. Full
regression and final source qualification remain pending. Keep the failed,
withdrawn and incomplete results separate from future repairs, as recorded in
[validation](validation.md).

Begin with independent metadata vectors and focused actual-class checks, then
run the applicable normal groups through `./build.sh`. Do not weaken existing
limits or count skipped/incomplete checks as passed. Metadata tests cover frozen
compact callsign/clear/encrypted vectors, actual-character counts, all timestamp
widths, canonical minimum symbol counts, optional CRC/no-CRC boundaries, zero
padding, unsupported types and unavailable cipher. FF1 needs official NIST
examples plus independent binary vectors and length-preserving round trips.
Wrong-key controls must retain the unauthenticated/plausible-output limitation,
not promise rejection. Cover exclusive packet types, physical timestamp
coordinates and cache recovery after invalid times. Waveform tests cover
punctuation guards, streaming protected endpoints and the actual planned data
band, including both real-carrier images and active-duration normalization.

Precision tests need raw sampled tone/reversal/metadata acquisition, dated
uncertainty, clock/CFO/phase changes, noise rejection, chunk/gap behavior and
16-bit captures. Simulation tests need common physical impairments, data-band
AWGN density, bounded centered lookahead, helper-only operation, exact disabled
behavior and actual Robust decoding of mixed PCM. Assistance tests need retained
clock/timing alternatives, exact original snapshot/raw fallback, finite optional
evidence spending, supported phase-bin correction and unsupported-path fallback.
Check primary conditional admission across multiple private epochs/profiles,
all original starts and continuous clock residuals, its 257-alternative limit,
refusal when snapshot/history cannot fit, and no first-bit commitment from tone
or metadata. Before acceptance, exercise helper loss, unrelated continuous tone,
failed initial windows, resource pressure and complete original replay. After
acceptance, preserve the payload through helper loss and broaden future cohorts.
Check processing frontiers during dormant fallback and centered FIR input,
including live capture and simulation. Filter checks must use the actual FIR,
confirm wanted-band gain and helper suppression, and include filtered noise-only
and helper-only controls without manufacturing bits or END. Validate
both native coherent FFT and compact coherent correlation, nondeferred Fake: FCC
with its original gate, and rejection of true scheduled/Burst/deferred routes.
Measure realistic frequency-only acquisition at 48 kHz and both 30 and 24 dB-Hz
helper C/N0, recording seed-dependent latency, interval width and wide/no-useful-
reference cases rather than promising a fixed time to 0.1 Hz precision. Phase,
punctuation timing and metadata require independent qualification; 15.76 dB-Hz
is below the 6 dB/10 Hz presence level. Verify 144 ms exact-Gram/noise accounting,
fixed-grid lifetime spending, the doubled-phase cell union and its 4096-cell wide
fallback, and independent frequency/phase measurement dates. Do not infer full
Live CPU/memory margins from observer-only benchmarks.

Verify advisory-epoch ranking and its ordinary fallback, within-epoch parity and
retained clock/private-phase envelopes, bounded alias selection, fewer executed
optional FFT timing cells with the same `001` and END, and context-preserving
helper-loss absence handling. Cover the privately retained earliest qualified
evidence and unchanged public callback cadence, post-admission phase activation
with a continuous committed-frontier reanchor and preserved initial template
carrier offset, and timing bounds remaining fixed at admission. Cover native FFT
pre-roll, its 16 s/1 MiB bound, no backward phase correction, calendar admission's
maximum-constructor reserve,
and first-bit promotion during replay. Preserve existing backend choice even
when native FFT fill spans several symbols. Assisted long-text and
attachment cases must traverse the ordinary framing, decompression and recovery
path with unchanged bit progress and completion. Verify first-bit promotion
before optional reclamation, normal retained-payload lifetime under helper loss
or pressure, and resource-priority refusal of new optional acquisition.
Keep the older optional-lane calendar-retention checks with a clean, strong early
reference separate from weak unknown-epoch qualification of the additional
helper lane or future work.
The former uses 1.5 kW/0.004 W and noise 40 dB below the weak fixture to qualify
an early reference; it does not establish the latter's behavior. Test the actual
startup 3.6 kHz shaped geometry without changing Rate, and narrow/wide controls
which must likewise keep Rate unchanged. Test pre-PCM mixed-TX fallback against
helper-off PCM, helper-only rejection and no retry after an output gap. Actual
outcomes belong in [validation](validation.md).

Retain independent short/raw/interval wire vectors, next-poll `0`, `00`, `001`
progress and fully scored payload-symbol absence. Exercise stable helper rows,
metadata presentation, callsign edits, short/long placement, simulation,
Live/duplex and noise/helper-only actions. One-shot helper diagnostics must remain
available when private payload acquisition fails, preserving its failure exit
status and withholding payload completion/validation. Test ordinary resource priority and
stale/future-reference rejection, not only a successful assisted reception.
End-to-end throughput, matched near-threshold sensitivity and noisy oscillator
benefit remain separate from functional fixtures.

Run normal contract and affected GUI/audio/browser checks for the final source.
Use meaningful sanitizer coverage and record environment limitations explicitly.
Native rendering requires a display; duplex audio and physical DAC/radio behavior
require devices and measurements. Neither is inferred from shared GUI or mocked
audio tests. Record actual results, initial failures, repairs and omitted or
unavailable coverage in `docs/validation.md`. Publication, certification and
promotion remain separate operations, unauthorized by implementation alone.
