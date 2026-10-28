# Helper Carrier implementation and qualification

Updated 27 October 2026. Human checkpoint `aef6580` is committed. GUI
`build/dev/datapump-gui` has a current fixed-lag build with headless self-check
and seven of eight focused passes; the phase-count failure reproduces before
lag. The preceding index build passes 11/11 focused
and 13/13 preservation checks. Its matched fixed-prefix Live run measures 41.15% less process CPU; current
1,024-byte completion remains untested. Pre-lag full GUI is 39/39 in 217.89 seconds,
and framed text/attachment OFF/ON source/wire/pending/END controls pass. Full contract
is incomplete. Long-text deadlines, strict HF15k-row and EME failures remain.
[Validation](validation.md) retains exact source bindings, measured work, all
failures and qualification limits. The final snapshot remains provisional.

## Current implementation

- Normal Helper power is 90% pure pilot/10% auxiliary. Only the auxiliary has
  shaped 240 ms punctuation and optional compact metadata. High-only placement
  is data, guard, auxiliary, guard, pure pilot: auxiliary starts 450 Hz above
  the shaped data edge, adjusted by leakage bound; pilot spacing is 400 Hz.
  The radio/audio passband and filter margin remain physical requirements.
- Full oscillator/capture-derived discovery precedes local refinement. Earlier
  200 ms contexts are software apertures, not a continuous-tone physical limit.
  Settled 384 ms fits and dated continuous numeric support remain bounded.
  Proper-Q component coverage improves while startup escapes remain; no
  calibrated operational-confidence guarantee follows. Numeric retention is
  separate from the 36-second extra-lead cap.
  Unknown retune phases remain independent; raw staging stays bounded at 400 ms.
- One ordinary Robust bank narrows existing and new RF candidates while
  retaining unresolved clock/timing alternatives. Every committed payload sample
  and date remains required. Phase needs local support; later frequency cannot
  manufacture earlier phase or advance the processed frontier.
- Planner separates total/pilot C/N0, uninterrupted/segmented reference scales,
  practical prediction, baseline lead and at most 36 additional seconds. The
  ±0.1 Hz milestone does not qualify every 128-second configuration. CPU prices
  remain conditional on existing receiver work and unresolved geometry.

Two-tone headroom changes native payload calibration by −1.353 dB at 40 W/4 W
and −1.989 dB at 15 kW/4 W versus the old format at unchanged volume. Recalibrate
physical RF output to retain configured power. Simulation scales signal/noise
together; ramps, metadata and received gains are separate power effects.

## Measured status and remaining acceptance

R15 HF40 now completes public exact eight-bit data, pending identity, prefix
checks and fully scored END: 194.956 audio seconds, 334.370 fixture CPU, 80,304 FFT
jobs and 667,434,466 peak DSP bytes under 2 GiB. Granular latency/backlog is
unavailable from its invalid runtime join/unfinished reads; replay-publication
latency is not receiver backlog. This older binary does not validate R16 Live.
The earlier R14 publication deadline, HF15k false `01` and all failed rows remain.
The completed pass is not a matched CPU-speedup comparison.
Current R23 HF40 fails its 360-second public-delivery deadline despite canonical
exact eight bits/scored END in the trace: 145.115 audio seconds, 361.48 fixture
CPU and 172,289 FFT jobs. Current positive-Q dispatch is wider than R15's first
score (125 versus 49 selected pairs); raw-median precision was not the relevant
work comparison. The strict run remains failed.
Fixed-lag R24 HF40 also misses its original deadline; audit remains pending.

The strict R17 12-second retune prefix passes all ±300 ppm LF alternatives,
with 174/419 existing-mask narrowings. At the same processed frontier, FFT jobs
fall 315→224 (−28.9%) and first FFT advances 0.8 seconds. Total process CPU rises
5.69%, so only executed FFT work is reduced. The repaired full-Session clock-cell
proof passes all 32 geometries, exact `001`/identity/END; its original failure
and 7,483 versus 5,600 FFT comparison remain, without a work-gain claim there.
The adaptive consumer's 236 fits/237 next-hop predictions retain zero observed
escapes under conditional Q/noise/kernel assumptions.

Full R16 EME fails after 1,791.731 audio seconds with one of eight canonical bits;
its first window is not missed. Current HF15k +3 kHz/+100 ppm primary reception
passes exact eight bits/END, but signal 4's premature raw-`10` completion is a
strict row failure. Neither false rows nor END rules are merged or relaxed.
Actual-key code-row control confirms initially identical HF rows followed by
address divergence; six-second END duration is correct, but the strict failure
remains without a receiver fix. EME fully scores the first 128-second symbol but
narrowly misses threshold while the second passes. Both windows have frequency
support; score decomposition remains unobserved. Modeled DATA C/N0 is not cable
or DATA-fit measurement. The exact-CFO/truth-timing laboratory ceiling also
misses eight-bit thresholds, but lacks bitwise Live PCM/pipeline matching; no
physical SNR floor, Live gain or fine-arrival change is established.

Pre-lag proper-Q process/Hann covariance, phase initialization and dated queries pass
16 matched 30/40 dB-Hz constant/drift pairs: no post-startup current/backdated
escapes over 33,545/34,623 queries, but two startup escapes remain. U is
±0.03265–0.06052 Hz, availability 97.61–98.52%, storage 766,656 bytes. This is
component coverage and DSP cost, not calibrated confidence or total-process gain.
HF Q assumptions remain uncalibrated and are not assigned to GHz cases. Earlier
zero-Q/WLS escapes and all failed controls stay retained.
A matched current-Live 14-owner cohort holds PCM/dates/grants/all nine LF
alternatives equal with no phase correction: declared Q versus diagnostic Q=0
uses 12,250→4,410 FFT jobs and 26.425964→12.916053 CPU seconds. This quantifies
current uncertainty cost; Q=0 is an unsupported-drift ceiling, not a production
choice. Private four-second quadratic GLS fails late-drift coverage and is not
promoted despite better steady precision.
Production frequency-only fixed lag keeps Q/lead/raw staging unchanged. The
same Live cohort narrows U 0.07950→0.06641 Hz and pairs 125→109, with FFT jobs
12,250→10,682 (−12.8%) and process CPU 26.425964→24.002513 seconds (−9.17%). All
nine LF alternatives remain and phase is off; no full END/throughput result
follows. Normal refit/frozen-history controls pass with 14 retained correlated
lag escapes and 776,400-byte storage (+9,744), without PCM history.

Normal wide metadata, retune/continuous coverage and independent-RF/loss checks
pass. The implicit RF-only selector now passes existing/new 5→1 narrowing,
12-bit pending/END/loss and actual before-loss FFT counts 240→64 existing/48 new.
A same-frontier 785-bit prefix reduces FFT 19,650→3,930 (80%) with all GUI prefixes
verified offline. CPU changes only 4.470645→4.435823 seconds; one pair does not
establish throughput gain. Nested CPU points to frontend/optional auxiliary work,
not replay. Bitwise-preserving filter/Hilbert index walks are integrated, with
4–5× filter component speedup and lower translator CPU. Matched Live AFTER
measures 4.435823→2.610347 process CPU (41.15% lower) with unchanged 785 detector
bits, 260 GUI commits, 4,012 reference records and 3,930 FFT jobs. Kernels/delay/
estimation are unchanged; no before-PCM hash or actual-RF/model activity exists
in that prefix, and current 1,024-byte completion remains untested.
The 1,024-byte run still ends at its 45-second deadline with 7,770/17,024 bits,
stable matching diagnostic prefixes and no END; captured minus feed is 0.1295 s.
It is unfinished coverage, not proved backlog, content corruption or CPU gain.
The retained realistic/calendar diagnostics fail with no bits/END. Their concrete
admission defect retains nominal GPSDO cells by width despite the deliberate
+2.17 Hz offset; focused location/contact controls now pass. The first reference
fixture fails on retired single-tone geometry, then passes after only that
placeholder is migrated to current two-tone placement. The calendar flag
assertion was overstrict while all three LF alternatives remained. Repaired
realistic reception passes exact one bit/END; calendar still fails at its
original deadline with a stable 11/12-bit prefix/no END. Its owner fully joined
input past the hypothesized required endpoint, so neither the capture gap nor
unscored TX/absence events establish the cause. Retained-backend construction
passes before symbol scoring.
One separate fresh calendar diagnostic commits exact 12 bits/scored END but
fails before 37.18 seconds of planned tail are captured. Its positive-Q pilot
has no phase/model resets and long coherent support; no bootstrap/SNR floor
explanation follows.
The private raw-origin repair preserves 20 old-epoch corrections inside measured
apertures through changing RF/independent phases/+100 ppm, with identical 29
raw/model observations and zero escapes. FFT jobs stay 266, CPU rises; it grants
dated support, not CPU gain, whole-Live PCM identity or full END qualification.
Pre-lag OFF/ON framed controls pass 18-byte text/1,216 wire bits and a 17-byte
attachment/2,432 wire bits, authentication, source/wire identity and scored END.
ON has measured narrowing and 4,940/9,652 joined FFT jobs; differing TX epochs/
dated wire preclude a matched CPU-gain claim. These cases do not qualify the
unfinished 1,024-byte run.
Three conditional controls pass measured-reference exact `001`/END, fallback
progress and finite replay. The Helper-OFF positive control still fails at its
45-second deadline with correct `001`/stable identity but only 5.71225 processed
audio seconds, short of the earliest legal 10.49625-second END. No exact captured
count/backlog or eventual-completion claim follows.

Dated frequency support survives later current-reference shutdown only for valid
past apertures/saved dates; no earlier phase or advanced frontier is invented.
With phase publication deliberately disabled, the repaired retune prefix narrows
139/297 existing masks, retains all LF and reduces same-frontier FFT 294→238.
The original failure is retained; this is no physical SNR/full-END qualification,
and CPU/affinity observations supply no qualified throughput gain.

Metadata clock scaling/dedup is included, with seven unique dwells per ten
geometries. Raw-LS curvature coverage was initially 11/13; one bounded rematch
reaches 13/13 with RMS 0.230 ms. A private six-seed ensemble has 80/81 edge
detections and 79/80 conditional curvature coverage, retaining one miss and one escape;
it remains uncalibrated and without assistance authority.
The guarded producer matrix fails with zero publications, all 178 candidates
declined and all 81 normal eligible markers missed because the straight-tone
competitor is unconverged. Zero false outputs do not qualify timing; the private
branch remains unpromoted.

Contract runtime is incomplete. Normal native has zero registered tests under
this headless setup, not a pass.
Pre-index ASan/UBSan passes 3/3 only with leak detection explicitly disabled
after ptrace failures. Private exact-index filter/translator ASan/UBSan controls
also pass with bitwise output/state equality and no findings; leak coverage is
unavailable and full sanitizer coverage is not passed.
The 36-second lead cap and exact
identity/END contracts remain; full contract, 1,024-byte/large-payload, cable,
EME and intended-hardware qualification remain open.

Current preservation requirements are in the [development contract](development.md),
[build/test stages](building.md#testing-stages) and [Helper reference](helper-carrier.md).
Keep ordinary wire/framing, exact bit progress, pending identity and fully scored
physical END; Helper loss or expected length supplies no completion authority.
The superseded algorithm narrative is in
[Git history](https://github.com/mirage335-colossus/pumpModem/blob/8355e6406754fa46569bdee3e1411f0ca2a9cef7/docs/helper-carrier-plan.md),
with individual source-bound failures retained in [validation](validation.md).
