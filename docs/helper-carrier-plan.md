# Helper Carrier implementation and qualification plan

Updated 23 October 2026 for the frequency/phase, metadata and physical-simulation
extension to the experimental helper carrier. The operating reference and wire
layouts are in [Helper Carrier](helper-carrier.md); actual checks and limits
belong in [validation](validation.md). Initial source investigation used
`a858f6f31d19431d3d15b077752f67a30cd1866f`; the first experimental implementation
was committed as `b47c7a0ebd6a29d21ffa8748d5bc7ffbbeb7e942`. Neither revision
identifies the current uncommitted extension or a qualified release.

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
completion, key/calendar addresses and full admitted search coverage. Resolve
bandwidth, dynamic range and CPU conflicts on the helper side. Successful dense
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
   companion controls disable with it. Power presets include the requested
   values and existing Robust presets. Ratio defaults to 90 dB. High/Low/0 Hz
   choices and the independent 19-character printable ASCII callsign remain.
   Developer callsign edits copy one way; helper edits affect future output
   without replacing live receivers or modifying the Robust draft. Encryption
   ignores the saved helper callsign and hides its GUI field; the preference
   reappears when unencrypted. The helper-only GUI action is Developer-mode-only.
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
   includes ramp/metadata duty and physical frequency-response losses. High separation grows with effective ratio and includes data gating
   allowance; integrated placement lies beyond the original logical sample rate
   plus 250 Hz. Low is functional below the data band when its guard fits;
   co-channel 0 is functional intentional overlap and discouraged. Low's adjacent
   tone remains in ordinary capture and can interfere; neither choice silently
   cancels the selected reference. An enabled-helper output that cannot fit its
   physical route fails before payload PCM. The radio/audio route must pass the
   selected tone and metadata band.
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
   unavailable tone geometry are rejected on the helper side. High additionally
   requires isolation; Low/0 retain their actual adjacent/overlapping contribution.
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
   Fixed-grid AWGN admission/variance spending totals at most 1% per reset;
   refined intervals remain local LS/model estimates. Doubled-phase joint
   refinement retains compatible frequency-cell unions, capped at 4096 cells,
   otherwise falling back wide. Frequency-only output grants no phase/timing
   authority. Independently dated narrow-frequency/strong-phase fusion must not
   redate an older measurement. Useful precision at 24 dB-Hz is seed-dependent,
   including cases with no usable interval; no fixed acquisition latency is
   promised.
9. The optional receiver preserves the existing backend: native coherent FFT
   stays FFT, and an explicitly selected compatible compact correlator stays
   compact. FFT fill duration is not a reason to switch automatically; the
   36 Hz/DSSS 10/SF 256 profile retains about 45.5 s fill for 14.22 s symbols.
   Frequency/timing guidance uses actual surplus after ordinary reservations
   and growth headroom. It does not borrow ordinary
   projection, permutation, temporal-template or source caches. Helper offset
   transfers to data while retaining original clock hypotheses. The original
   search stays admitted and has priority at growth/admission fences. A stricter,
   lifetime-spent optional-branch evidence budget accounts for repeated attempts.
   Before accepting bits, the optional branch can yield to unavailable geometry,
   backlog or ordinary resource/admission demand. If the tone ends before the
   first native FFT is scored, its admitted finite initial search can remain
   without fresh helper updates, subject to optional resource limits.
   The first accepted bit promotes its same context to ordinary retained payload
   work before publication or optional reclamation. Its source/recovery state,
   bit progress and complete
   scored-symbol absence retain normal payload lifetime; fresh acquisition stops.
   Helper loss or pressure can suspend phase updates and freeze the last committed
   phase-model value, but cannot discard accepted payload as optional work.
   Genuine receiver errors follow ordinary reconstruction policy; helper loss
   never supplies END.
   Logical raw-PCM history can pre-roll through the ordinary native FFT payload
   receiver once helper measurements settle; compact correlation does not replay it. It retains up to 16 s and
   at most 1 MiB, charged against the helper cap of min(64 MiB, bank capacity/8).
   It does not backdate phase correction, advance ingress or manufacture END.
   First-bit promotion also occurs during replay before reclamation/publication.
   Calendar persistence requires all ordinary promises, the maximum ordinary
   constructor grant and node/control costs to fit; speculative history/helper
   acquisition otherwise yield before ordinary sizing.
10. Qualified punctuation timing removes the known helper phase sign. Compatible
    coherent FFT paths can rotate completed complex bins by measured residual
    phase wander while retaining unknown constant data phase and per-bin
    covariance eigenvalues. Phase assistance may activate after admission with
    a continuous reanchor at the committed processing frontier, preserving the
    initial template carrier offset and applying no later correction backward.
    The timing-search range stays fixed at admission; later phase qualification
    does not dynamically narrow it. That fixed-trace argument does not certify
    adaptive helper-derived lifetime false-alarm rates; empirical calibration remains
    separate. Real PCM remains unchanged. Sample-fit, differential,
    real-rank, fallback and other unsupported paths retain ordinary processing.
    References must predate the first input sample of a corrected bin.
11. The earliest privately retained qualified tone-support measurement ranks
    one advisory private epoch, accounting for the profile's training/padding
    prefix and propagation. Observer slices of at most 40 ms preserve its true
    original sample date, while public callbacks retain their last-update
    cadence. This onset hint guarantees neither UTC nor the first physical tone
    start. Weak qualification can lag across a second boundary, so unknown-epoch
    late-start reception can choose the wrong candidate and fail assisted
    acquisition. Fixed-epoch frequency assistance does not establish coverage of
    that case. The full ordinary bank remains unchanged; this is not a general
    GPS Clock sync replacement. A future solution needs concurrent epoch
    hypotheses with separate false-alarm, memory and CPU budgets, rather than
    one scalar onset adjustment. Other epochs currently rely on ordinary
    fallback; full private-epoch alias coverage is not certified.
    Within a chosen epoch, qualified dwell timing supplies a bounded optional
    location hypothesis: for receiver-local dwell `D`, integer epoch `E` and
    retained rate `r`, `B = D + (0.2 * (E % 2) + 0.4 * k) / r`.
    `PatternStartWindow` keeps `B + phase_scale * p` (seconds) over the retained
    rate/private-phase envelope. Bounded modulo-.4 alternatives intersecting the
    original prior can narrow one optional lane to the nearest alias. Ordinary
    admitted priors remain unchanged. This supplies no UTC/GPS qualification,
    source-bit retiming or generic dwell-to-symbol-zero identity. Timestamp
    metadata is not needed; a separately justified symbol-to-dwell relation
    remains another planner input.

12. Link Planner publishes a separate helper reference budget: effective ratio-
    limited power, received dBm, C/N0, 10 Hz tone SNR and audio center. With 4 W
    data, a 90 dB ratio ceiling and -164 dBm/Hz noise, 40 W/180 dB loss yields
    20.02 dB tone SNR; 1.5 kW/210 dB loss yields 5.76 dB, below the 6 dB presence
    gate. Predicted level alone does not establish measured confidence/coherence.
    Payload SNR/probability and CPU estimates retain their original meanings;
    no helper-assisted sensitivity or CPU gain is invented by these curves.

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
  Low and 0 are functional diagnostic configurations that retain real interference
  in ordinary input. The earlier unisolated 1.5 kW/4 W sampled case admitted an
  extra data bit; it remains the reason for High capture isolation and the Low/0
  caution, not a change to Robust's decoder or admission threshold.
- Assistance is selective. Frequency/timing priors support the existing eligible
  native coherent FFT or compact coherent-correlator backend, preserving that
  choice rather than automatically changing it to shorten FFT fill. Dynamic
  per-bin phase remains
  limited to eligible FFT paths. Nondeferred Fake: FCC can use its ordinary gated
  template, while true scheduled, Symbol_Burst and deferred hopping modes retain
  transmission/observation without this assisted acquisition. A single tone
  cannot distinguish RF error, Doppler and sample-clock error or establish
  absolute UTC. One advisory private epoch and a within-epoch nearest-alias
  hypothesis can narrow the optional lane's timing cells; ordinary admitted
  search remains unchanged, without certifying complete private-epoch coverage.
  First-bit promotion retains the assisted payload under ordinary lifetime rules. Helper
  loss, resource demand or lag may stop optional new acquisition/phase updates;
  they never substitute for its scored physical absence or discard accepted
  payload as optional work.
- The original full search remains. An optional branch can use additional work;
  this implementation makes no CPU-ceiling, frequency-range or full-bank
  sensitivity improvement claim before qualification. Additional acquisition
  coverage or coherence does not guarantee lower aggregate CPU. It yields to ordinary
  workspace and backlog, without shrinking normal data/audio reservations.
  The assisted receiver retains full normal framing, long-text/attachment
  decompression and recovery, per-bit progress and physical completion. New
  optional acquisition yields when those resources cannot fit; first-bit
  promotion preserves accepted payload as ordinary work.
- The 90 dB ratio option does not certify 16-bit DAC/RF dynamic range. Physical
  oscillator phase noise is shared: strong helper sidebands can leak into data
  even with correct digital placement. Those effects remain visible in physical
  simulation, rather than being removed from the channel model. Hardware phase
  noise, compression, spurs and gain calibration require actual measurements.

## Validation and acceptance

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
clock/timing alternatives, unmodified original searches, finite optional evidence
spending, supported phase-bin correction and unsupported-path fallback. Validate
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
Keep calendar-retention checks with a clean, strong early reference separate from
weak unknown-epoch late-start coverage. The former uses 1.5 kW/0.004 W and noise
40 dB below the weak fixture to qualify an early reference; it does not resolve
the latter's epoch ambiguity. Actual outcomes belong in [validation](validation.md).

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
