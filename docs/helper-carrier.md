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

The rebuilt GUI is at `build/dev/datapump-gui`. At human checkpoint `871a55a`
plus the current overlay, the coast/support-API app rebuild is bound in
validation. Protocol/support and both default actual-RF controls pass. The latest
full GUI group remains 38/39; the isolated geometry-repaired controller now
passes on the preceding core, without a new full-group result. The preceding
filter/translator index build passes 11/11 focused and 13/13 preservation checks.
Its matched fixed-prefix Live run measures 41.15% lower process CPU with
identical references/progress; current 1,024-byte completion remains untested.
The pre-lag full GUI group passes 39/39 in 217.89 seconds, and its framed-text/
attachment OFF/ON controls pass exact source/wire/pending/END. Implicit RF
narrowing and frequency-only retune controls now reduce actual
FFT work, without a qualified CPU-gain claim. Full contract remains incomplete;
long-text deadlines, strict HF15k-row and full EME failures remain open. Human
checkpoint history, exact bindings and all failures are in
[validation](validation.md). Metadata clock scaling/dedup is included, with seven
unique dwells per ten component geometries and normal wide-offset reception.
It grants no additional assistance or general phase/filter-delay certification.

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

Discovery precedes local refinement. Earlier 200 ms contexts are software
apertures, not a physical coherence limit. The reviewed full settled-dwell path
uses up to 384 ms; uninterrupted pilots retain longer dated numeric frequency
information without searching historical PCM or bridging unrelated hop phases.
Numeric retention remains separate from the 36-second extra-airtime cap.

Dated frequency/rate covariance survives phase-only resets while each phase
nuisance is initialized independently. Presence loss withdraws assistance even
when numeric state is retained; weak continuous phase cannot renew the existing
0.4-second lease. In the pre-lag proper-Q component ensemble, 16 matched 30/40 dB-Hz
constant/drift pairs have zero post-startup escapes across 33,545 current and
34,623 backdated queries, retaining two startup escapes. U at final signal
observation is ±0.03265–0.06052 Hz with 97.61–98.52% availability and 766,656 bytes.
These are bounded component results, not calibrated confidence or Live/hardware
qualification. The previous zero-Q drift/coverage failures remain; operational
HF Q assumptions do not constitute drift calibration or a physical floor.
A matched current-Live first-cohort comparison holds PCM/dates, 14 owners,
grants and all nine LF alternatives equal, with phase correction off in both.
Declared-Q versus diagnostic Q=0 uses 12,250→4,410 FFT jobs and
26.425964→12.916053 CPU seconds. This measures the receiver cost of current
uncertainty; Q=0 is an unsupported-drift ceiling, not a production recommendation.
A private four-second quadratic GLS alternative improves steady precision but
fails late-drift coverage and is not promoted.
Production now uses bounded frequency-only fixed-lag refinement without changing
Q, TX lead or the 400 ms raw stage. In the same Live cohort, residual U falls
0.07950→0.06641 Hz, selected pairs 125→109, FFT jobs 12,250→10,682 (12.8%) and
process CPU 26.425964→24.002513 seconds (9.17%). Phase remains off and all nine
LF alternatives remain. This is a bounded prefix result, not full END or
hardware throughput qualification. The normal continuous test retains 14
correlated smoothed-query escapes, passes refit/frozen-history support and uses
776,400 bytes, 9,744 more than before, with no PCM history.
The later independent-record fallback leaves this Live cohort's 10,682 FFT jobs
and U 0.06640714 Hz unchanged, while CPU rises 3.37%. It publishes no independent
records in that fixture, so it establishes no new Live fallback benefit. The
repaired laboratory stage still retains 35 snapshot and four dated escapes.

The frozen R15 Live retune adapter uses capture-effective RF and common-source
clock/rate covariance, actual RF/sideband geometry and causal old-segment seeds.
It preserves the original B/UTC clock domain and Helper–DATA relation, retains
payload-onset state and grants no phase from the model. The current explicit Q
assumptions cover HF through 54 MHz; adaptive measured retune scatter and a
six-sigma innovation veto do not constitute hardware calibration. Optional
model storage and 64 KiB scratch are funded after mandatory work, with fallback
on allocation or growth failure. The reviewed extended-arithmetic constructor
proof now passes actual geometries with exact payload/END; the original failed
proof remains retained. LF cells are clipped to their original physical domain
even without a qualified clock measurement. Hardware and general uncertainty
qualification remain open.
The joint model can project a DATA-frequency marginal with RF/audio covariance.
The explicit `data_frequency_valid` field is preparation only: direct Helper
apertures grant no DATA certificate, and a consumer still needs dated support
for the whole scored observation. No new DATA-frequency mask is applied.
A bounded frequency coast no longer requires a current-hop full fit/presence
bootstrap: availability lasts at most 0.5 seconds from independent frequency
evidence, using integer dates. Retunes never renew that evidence. The three-
second mathematical model history remains separate; explicit close/reset/veto
and local phase-fit requirements remain. Normal actual-RF cases exercise no
pre-fit coast events. The matched retune/loss case demonstrates 30 prior-only
corrections, including five after final loss at witness ages 0.436–0.5 seconds,
without witness renewal, new phase or escapes. Both keep exact `001`/ID 2/END,
but FFT jobs rise 2,058→2,226 and CPU 3.074368→3.107025 seconds; no gain follows.
Only an END-covering PCM prefix is hashed, not its entire tail.
An engineered strong fixed-RF phase-gap test exercises 56 independent-aperture
publications versus zero before, with no nominal-flank escapes and unchanged
1,127 FFT jobs. Phase counts also change; only a wrong-epoch pending one-bit
suffix is observed, so it grants no canonical payload/END acceptance.
The const native/compact support API exposes earliest future frequency-scoring
support, but whole-score DATA masking and Live rolling provenance remain open.

Local phase observations and a separately funded raw staging FIFO remain bounded
at 400 ms. Every committed sample and date must survive loss and staging.
Frequency history cannot manufacture earlier phase support or advance the
processed frontier. A closed current reference still permits valid captured
past-aperture frequency support; closed saved queries stop before the saved
pilot end. Retune bootstrap does not erase those dated buffered observations. No actual hardware rig-retune adapter is qualified; Fake
FHSS with fixed physical RF is not proof of hardware retune phase handling.

Without a fresh qualified reference, new assisted acquisition may wait. Reference
loss disarms new acquisition; established ordinary reception retains draining,
recovery, per-bit progress, pending-row identity and physical-absence scoring.
There is no parallel broad receiver bank or iterative historical PCM search.
The ordinary Robust wire format, short dictionary endpoints, explicit binary
bits, fixed framing, long text, attachments and post-END source decoding remain
required. Neither Helper loss, metadata, EOF nor expected length completes a
message: fully scored consecutive absent-symbol durations must cover six seconds.
Optional Helper work yields before payload functionality.

The R15 HF40 run completes public exact eight-bit reception, pending identity,
exact prefixes and fully scored END. Its 194.956 captured audio seconds use
334.370 fixture CPU seconds, 80,304 FFT jobs/306 transforms and 667,434,466 peak
DSP bytes under a 2 GiB grant. No health drops/resets occur. Runtime-observer
invalid joins/unfinished reads leave granular latency and backlog unavailable;
the 319-second replay-publication interval is not receiver backlog. This uses
an earlier binary than R16 Live. It preserves the prior R14 publication deadline
and HF15k false `01` failures separately. The same-input R9 diagnostic measured
23.04% less process CPU and 27.80% fewer FFT jobs without payload completion in
either comparison; the full HF pass is not a matched CPU comparison.
The current R23 HF40 run fails its 360-second deadline: the trace has canonical
exact eight bits and scored END, while public delivery/replay is unfinished.
It uses 145.115 audio seconds, 361.48 fixture CPU and 172,289 FFT jobs, with no
health faults. Current positive-Q uncertainty is wider at dispatch than in R15
(125 versus 49 selected pairs at first score); the earlier raw-median precision
comparison did not measure that receiver cost. The strict run remains failed.
The fixed-lag R24 HF40 run also misses its original deadline. Its fixture never
observes the simulation-finish/replay conjunction or a completed exact public
row. Public exact is accumulated with OR, so an earlier wrong row cannot veto
it. Canonical eight-bit/END trace commits precede staging acceptance and do not
prove public completion; the precise unmet tail/finish subcondition is unobserved.

Actual random-phase RF Session controls retain exact `001`, same pending
identity and END. The extended-arithmetic clock-cell proof passes all 32 physical
geometries after preserving the original failure. The strict R17 12-second,
SF512/sampled-pilot-40-dB-Hz prefix retains all 15 LF alternatives and narrows
174/419 mask snapshots. At the same processed frontier, FFT jobs fall 315→224
(28.9%) and first FFT advances 11.514→10.714 audio seconds; 29 model updates and
643 corrections have no observed bound escape. Whole-process CPU rises
1.207813→1.276541 seconds (5.69%), so there is no CPU-saving claim. The strong
full Session instead uses 7,483 FFT jobs versus the R14 baseline's 5,600. These
are separate cases, and the 12-second prefix has no payload-completion result.

Short EME previously demonstrates existing-owner 33→3 narrowing and U 0.000189 Hz.
The completed R16 full run nevertheless fails at one of eight canonical bits:
1,791.731 audio seconds and 1,095.872 CPU seconds. Projected work executes;
compact lane counters do not apply to its shared-pulse lattice, and the first
window is not missed. The R17 +3 kHz/+100 ppm HF15k primary passes public exact
eight bits/END, but secondary signal 4's raw `10` completes before the true
physical end. That strict failure stays open, with no row merge or weakened
identity/END protection. An actual-key code-row control confirms bit-identical
initial rows followed by address divergence; this alias is not a failed
six-second END duration, and no receiver fix or strict pass follows. A bounded EME probe fully scores
the correct first 128-second symbol: score 30.1203 misses threshold 31.0201,
while the second symbol passes. There is no missed startup/support gap in that
probe. Both windows have full frequency support and 114.384/119.055 seconds
of phase correction. A laboratory exact-CFO/truth-timing ceiling still misses
several eight-bit thresholds, but its PCM/pipeline is not bitwise Live-matched;
no physical SNR floor, Live gain or fine-arrival change follows. DATA C/N0 remains
a modeled channel value. Optional auxiliary phase certification also fails its
noisy-mixture/coarse and post-DATA residual checks; full-Live delay mapping is
not certified.
The matched EME first-symbol phase-ON/OFF comparison scores 30.1203/30.0786
against threshold 31.0201 with the same covered raw PCM and frequency records.
Phase gives a small benefit in that window; both fail, establishing neither an
SNR floor nor eight-bit completion.
A known-truth EME waveform component has no Interleaved V2 backoff (DSSS factor
1): nominal DATA C/N0 is −4.9794 dB-Hz, actual keyed/gated C/N0 −6.4142 dB-Hz and
symbol E/N0 29.2276 linear. Fake-FHSS gating costs 1.5918 dB relative to the
actual ungated waveform. This is power/noise accounting, not an ordinary score,
decode or physical-floor result.
An ungated-template component opportunity remains: on the same generated PCM,
including the known source gate in the template raises guarded score
17.9804→20.9997. It is not actual Live acquisition or an eight-bit result, and
no production gate change is applied. Full regressions remain held while the
fundamental receiver-gate diagnosis is settled.

The R15 rolling check now passes withdrawal, drain and dated post-loss joined
work. R16 GPS, Default-clock and final-loss controls pass, preserving the same
pending source through post-loss wire progress and physical END. The stronger
replay-timeline test repairs and all earlier failed assertions remain recorded.
Earlier metadata capture/API/CLI checks use 400,012 bytes at 48 kHz/1.68 seconds
maximum raw PCM; the final private dedup candidate adds eight bytes for 400,020,
with a conditional punctuation-clock export; metadata
remains optional and supplies no frequency authority.

Normal RF-only/independent fallback checks restore 118 qualified references,
exact `001`/identity/END and final coverage sentinels. The implicit RF-only
selector passes existing/new 5→1 narrowing: before loss, 64 existing and 48 new
FFT jobs versus 240 in the control, retaining exact 12 bits and END/loss behavior.
A separate same-frontier prefix reduces 19,650→3,930 FFT jobs with all 785 detector
bits/260 GUI prefixes verified, but CPU is 4.470645→4.435823 seconds: one pair's
0.8% difference is not meaningful throughput gain. Nested timing points to the
frontend and optional auxiliary work rather than tracker replay. Exact filter/
Hilbert index walks now preserve tested output/state bits and improve component
CPU without changing kernels, delay or estimation. The matched Live AFTER
preserves all 785 detector bits, 260 GUI prefixes and 4,012 reference records,
with unchanged 3,930 FFT jobs/786 transforms, while process CPU falls
4.435823→2.610347 seconds (41.15%), or 0.0172958→0.0101781 CPU/audio. This
fixed-prefix result has no full END, actual-RF/model activity or whole-Live
before-PCM hash; it does not generalize to current 1,024-byte completion.
The 1,024-byte test still hits 45 seconds unfinished at 7,770/17,024 bits, with
matching recorded 128-bit prefixes and stable identity/no END. Its one owner
narrows 5→1 and executes 38,320 FFT jobs, but the 0.1295-second captured/feed gap
does not explain the deadline as input backlog or establish completed-content
corruption. The older cutoff and completion controls remain source-bound.

A private phase-publication-off repair reduces FFT jobs 294→238 at equal
frontiers while retaining all LF alternatives; existing-mask narrowing rises
0/267→139/297 with 29 identical model updates across four RFs. Phase flags and
updates remain false/zero. This deliberate publication fault injection is no
physical phase-noise/SNR or full physical END proof; changed affinity and higher whole-
process CPU preclude a qualified CPU-gain claim. An unintegrated auxiliary raw-LS
control originally has 11/13 curvature coverage; a bounded rematch reaches
13/13, timing RMS 0.230 ms/max 0.450 ms in one case. A six-seed private ensemble
detects 80/81 eligible edges with 79/80 conditional curvature coverage, one
escape, one miss and zero null/wrong-edge detections. It remains uncalibrated
and unintegrated, with no assistance authority.
The guarded producer matrix then fails: all 178 candidates decline and all 81
normal eligible markers are missed, with no publications. The straight-tone
competitor remains unconverged after two clipped steps. Zero false outputs in
that matrix do not qualify detection; the private branch is not promoted and
establishes no physical SNR limit.
A subsequent private null-variable-projection matrix qualifies/covers 75/81
normal markers and 77 boundaries, with six marker misses, no accepted-interval
escapes and no straight-tone publications. Its fade/loss case covers 10/11
eligible markers. The 14-capture run costs 30.095 CPU seconds; normal process
cost averages 0.484 CPU/audio. This remains conditional component evidence with
no Live timing authority or calibrated coverage claim.

The retained realistic and calendar Live diagnostics fail with no bits/END, despite
qualified pilot observations. The calendar check also rejects a clock flag even
though all three LF alternatives remain. Width-only prior retention leaves
nominal GPSDO cells in place despite the deliberately injected +2.17 Hz offset;
the location-compatibility repair passes focused ±2.17 Hz/contact controls.
An initial reference fixture fails because its retired single-tone placement
puts the auxiliary in DATA; migrating only that placeholder to current two-tone
geometry passes. Both original failure/context remain. Separate retained-backend
controls pass five ordinary numeric owners before symbol scoring. Migrated
realistic reception now passes one exact bit/END with 98 FFT jobs and one owner.
Calendar reception still fails its original deadline with a stable 11/12-bit
prefix and no END. Its owner already fully joined input beyond its hypothesized
twelfth-bit/absence endpoint; true TX boundaries and scored decisions are absent
from this filtered trace. The 0.516-second capture gap is not an established
cause, and no completion rule is relaxed.
A separate fresh calendar diagnostic reaches exact 12 bits and scored END but
still fails before 37.18 seconds of planned Helper tail are captured. Its
positive-Q reference is continuously qualified after five seconds with no
phase/model resets and up to 128.985 seconds of coherent support. Its precision
price is the model, rather than failed bootstrap or an established SNR floor.

The private changing-RF/+100 ppm raw-origin comparison preserves 20 old-model-
epoch corrections within their actual measured apertures, versus zero before,
with 29 identical raw/model observations and no escapes. All LF alternatives
remain; FFT jobs stay at 266 while CPU rises 1.2171→1.2887 seconds. This is dated
support repair, not CPU gain or full payload/END proof; matched whole-Live PCM
bytes were not measured. Normal actual-RF controls retain exact `001`/pending/END.

Three current conditional controls pass: measured-reference exact `001`/END,
fallback capture/processing progress and finite replay. The Helper-OFF positive
control remains failed at 45 seconds: stable primary bits `0`→`00`→`001` have
no END after only 5.71225 processed audio seconds, before the earliest legal
10.49625-second END. This establishes insufficient processed END support;
the final captured count/backlog is unavailable and no completion pass follows.

Pre-lag framed controls pass 18-byte text/1,216 wire bits and a 17-byte
`tail.bin` attachment/2,432 wire bits, including authentication, exact source/
wire, stable pending identity and scored END. Helper-ON keeps row 2/revision 1
and executes 4,940/9,652 joined FFT jobs with measured narrowing at timestamp zero, Auto/rolling,
1,500 W Helper and 64 MiB. OFF/ON TX epochs and dated wire differ, so their
33.77/120.04-second pair CPU totals are not matched CPU-gain evidence.

Serial-cache exact-score checks, 128-second phase-component controls and
full-dwell/model experiments retain their own source bindings and failures.
An earlier pre-lag GUI group passes 39/39; the later group is 38/39 with its
original LPI-geometry fixture failure retained. The repaired full controller
passes in 138.40 seconds on the preceding core; a latest-app full-group rerun
and final qualification remain pending.
Migrated
R19 joint-clock/query/selector-cache checks pass, using dated pilot support and
14 independent auxiliary markers; original fixture failures remain retained.
The startup-guide migration passes and contract prerequisites compile. Full
contract runtime was interrupted/reaped 130 in its first serial test without a
completed result; the stage is incomplete. The headless native group has zero
registered tests. Three pre-index ASan/UBSan checks pass with leak detection
explicitly disabled after ptrace failures. Private exact-index filter/translator
ASan/UBSan checks also preserve tested output/state bits with no findings;
leak coverage remains unavailable and full sanitizer coverage remains open. Large
payloads, strict cable, EME and intended-hardware acceptance remain open. See [validation](validation.md) for
commands, CPU/work counts, support dates and limits.

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
audio-headroom change, not a requested payload-power reduction. Headroom and
physical-oversampling compensation scale sampled signal/noise together; they do
not remove Interleaved V2's separate **−6.0206 dB sampled DATA gain** relative to
the nominal RF budget. Ramps/metadata and channel/filter gains are separate
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
The independent-retune comparison assumes constant drift and configured Brownian
phase noise. It excludes operational RF/audio acceleration, retune scatter and
integrated-model precision; it selects neither TX lead nor receiver-cost
precision. It is not a prediction of measured retune performance.

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
The current adaptive-factory consumer passes 236 frequency fits, including
21 outside the earlier phase-qualified subset, and 237 next-hop predictions
without observed escapes. Fits use 384 ms apertures in independently phased
7/21/35/49 MHz hops. Model storage is 42,816 bytes; measured component model CPU
is about 0.0004 seconds per 32 audio seconds. Hardware noise, positive Q and the
nominal Hann-kernel approximation remain uncalibrated. These controls do not
erase the original proof failure or earlier WLS/drift-case escapes.
A private practical four-second forecast meets 25 seeded milestones versus the
older 38-second forecast while retaining eight earlier escaped estimates;
prediction alone supplies no runtime qualification or confidence calibration.

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
−6.0206 dB sampled digital power gain: DATA C/N0 is the nominal reference minus
6.0206 dB,
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
