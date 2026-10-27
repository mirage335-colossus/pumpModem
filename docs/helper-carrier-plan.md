# Helper Carrier implementation and qualification

Updated 27 October 2026. Human checkpoint `62db765` is committed; the current
overlay remains under implementation. R13 passes unchanged positive GPS-clock
reception, while Default source-handoff repair passes a private matched check.
Fresh R14 application/test builds are pending. The manual GUI remains
`build/dev/datapump-gui`; no full receiver/contract qualification is claimed.
[Validation](validation.md) retains commands, source bindings, original failures
and measured limits.

## Current implementation

- Normal Helper power is 90% pure pilot/10% auxiliary. Only the auxiliary has
  shaped 240 ms punctuation and optional compact metadata. High-only placement
  is data, guard, auxiliary, guard, pure pilot: auxiliary starts 450 Hz above
  the shaped data edge, adjusted by leakage bound; pilot spacing is 400 Hz.
  The radio/audio passband and filter margin remain physical requirements.
- Full oscillator/capture-derived discovery precedes local refinement. The
  tested segmented 200 ms contexts are software apertures, not a continuous-tone
  physical limit. The reviewed continuous repair retains 54.805-second actual-
  channel coherence, but substantial drift-case interval escapes remain; no
  operational confidence or precision qualification follows.
  Numeric history retention is separate from the 36-second extra-lead cap.
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

Actual random-phase RF Session controls retain exact `001`, pending identity and
physical END under strong laboratory assumptions. The independent-LF constructor
reduces 437→5 pairs with the full ±300 ppm domain; further existing-mask work is
zero/unexercised there. A short continuous EME observation now demonstrates
existing-owner 33→3 narrowing with three clock alternatives retained, final
U 0.000189 Hz and actual compact work. Its 96.189 audio seconds end before the
128-second four-symbol batch: no bits/END or full EME reception is established.
The collector's earlier zero compact summary and corrected terminal-total replay
remain separately recorded. There is no qualified hardware retune adapter.

The actual-channel continuous control retains 54.805 seconds of coherence and
U 0.001678 Hz, but drift coverage still fails: one case escapes at 507/2,967
unique current dates, and aligned KF/WLS comparisons do not remove the failures.
The 384 ms settled-dwell extension has 44 shifted-calendar raw fits without
escapes. Its amplitude-aware kernel and new frequency-only admissions remain
unvalidated. The four-state RF/audio-rate model is component-only; positive Q is
uncalibrated and reaches ±0.103–0.110 Hz at 49 MHz/40 dB-Hz. These results supply
no operational-confidence guarantee. A private four-second forecast meets its
25-seed milestone but preserves eight earlier escapes; it remains unbuilt and
unpromoted, distinct from the 36-second additional-airtime cap.

The same-input R9 HF window measures 23.04% less process CPU and 27.80% fewer FFT
jobs, with zero bits in both controls. The full R11 HF run remains incomplete:
true-TX owner 48 has seven bits; the eight-bit matching primary has a different
epoch and no END. Existing HF15k/Rate-360 primary passes retain their row/epoch
limits. GPSDO-model HF controls pass eight bits/END, including a same-lab-key
control whose different TX epoch prevents exact dated-wire CPU comparison.
Earlier EME CPU/workspace admission failures and its incomplete GPSDO control
remain preserved, without physical-limit claims.

R13 unchanged positive GPS-clock reception passes after repairing optional-owner
birth funding. The unchanged Default case exposes a gap over a known prefix; its
narrow handoff repair passes the private same-Default CPU-0/1 check. The rolling
loss/drain check still fails with processed frontier zero; that frontier repair
is source-reviewed only. The first dedicated loss attempt is SETUP_INVALID due
to header ABI mixing and supplies no behavioral coverage. Metadata/API/CLI
checks pass within the 400,012-byte optional budget at 48 kHz, but metadata never
qualifies pilot frequency assistance.

The serial-row cache passes exact arithmetic/acquisition checks; 128-second
phase-component tests retain measured losses and earlier escapes. Neither is
full receiver throughput or completion qualification. Local-RF diagnostics retain
two 30 dB-Hz availability failures, and all historical failed checks remain in
validation. Full GUI/contract/native/sanitizer groups, large payloads, strict
cable, supplied HF/EME completion and intended-hardware qualification remain open.

Current preservation requirements are in the [development contract](development.md),
[build/test stages](building.md#testing-stages) and [Helper reference](helper-carrier.md).
Keep ordinary wire/framing, exact bit progress, pending identity and fully scored
physical END; Helper loss or expected length supplies no completion authority.
The superseded algorithm narrative is in
[Git history](https://github.com/mirage335-colossus/pumpModem/blob/8355e6406754fa46569bdee3e1411f0ca2a9cef7/docs/helper-carrier-plan.md),
with individual source-bound failures retained in [validation](validation.md).
