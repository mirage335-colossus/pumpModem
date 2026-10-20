# Pending Symbol_Burst validation

Snapshot requested on 2026-10-20. Status was checked at **19:59 UTC** for
candidate **049e84edc1f8d3b6b69fef661680ff0e0100bc04**. This file records that
checkpoint; later CI results may supersede it.

## Count of executions awaiting current-candidate results

The verified planning subtotal is **944 test executions**:

| Coverage | Executions |
|---|---:|
| Current focused run | 15 |
| Linux Release core | 252 |
| Linux sanitizer Debug core | 249 |
| Windows Release core | 231 |
| SDK general, Live and calibration contracts | 197 |
| **Subtotal** | **944** |

The three native core counts exclude declared runtime omissions. The SDK count
is 194 general contract cases, two Live cases and one calibration case.

This is a **lower-bound planning subtotal**, not an audited total of every
remaining check. Additional frontend, web, packaging and copied-binary checks
are not included. Platform repetitions and focused/full reruns count separately;
944 does not mean 944 distinct test definitions or 944 known failures.

Most of these tests already passed on earlier candidate
`1e9b83238a50bd7983c5b2301f22ac451b42a7b0`. They have not yet supplied successful
results for the current receiver correction. Reuse matching accepted evidence
only where its source, configuration and scope remain applicable.

## The 15 immediate focused executions

| Check | Executions |
|---|---:|
| Original no-argument Symbol_Burst Live test | 1 |
| Paced plaintext/encrypted, Burst off/on baseline cases | 4 |
| Keyed Burst-on full-input follow/trace case | 1 |
| Temporal-support component checks: FFT receiver, Correlator and batch | 3 |
| Original FFT/search tests | 2 |
| Original strict GUI tests, serial | 2 |
| Original strict GUI tests, parallel | 2 |
| **Total** | **15** |

[Focused run 39487011213](https://github.com/mirage335-colossus/pumpModem/actions/runs/39487011213)
was building prerequisites at the checkpoint. None of these current-candidate
runtime results was yet claimed as a pass. The full native and SDK matrices
had not been launched for this correction.

## Known unresolved behavior and remaining gates

- **One reproduced functional defect is under correction:** the paced keyed
  Burst-on fixture completed eight incorrect bits (`01001001`) instead of the
  expected 16 bits (`0010010010010010`). Tracing located excessive support for
  a competing longer-symbol interpretation. Candidate `049e84e` contains the
  reviewed temporal-support correction; its runtime acceptance is pending.
- **Post-startup backlog rescue remains unproven.** Functional success alone
  does not demonstrate that queued work is cleared during the required gaps.
  This coverage obligation is not assigned a fabricated case count in the
  subtotal above.
- The current correction must pass focused checks and the original strict GUI
  serial/parallel gates, followed by the required full native and SDK
  qualification. Any functional failure can expose another blocker.
- The earlier missing-`Help` and missing pending-progress assertions remain
  fatal. Corrected copied Rev TGZ/ZIP smokes passed on Ubuntu and Debian, but
  the lost original failing input prevents conclusive attribution of the
  original missing-`Help` incident.

The **10-second GUI stop limit**, **six seconds of fully scored physical
absence**, source-tail checks and content/input assertions remain mandatory.
The separately approved compact hosted Live 40/120-second warning records
incomplete compact coverage only with a completed functional analogue and all
remaining assertions; it is not a waiver for these functional failures.

## Earlier successful evidence and scope

- [Native qualification 39462367772](https://github.com/mirage335-colossus/pumpModem/actions/runs/39462367772)
  completed successfully on `1e9b832`, with documented warnings and omissions.
- [SDK qualification 39462388103](https://github.com/mirage335-colossus/pumpModem/actions/runs/39462388103)
  completed successfully on that same earlier candidate.
- Those results do not qualify newer runtime revision `049e84e`. A green
  focused run is not full qualification or publication of binaries.
- Public release and certification are separate delivery steps; neither is
  claimed by this snapshot.

The detailed checkpoint and retained evidence pointers are in
[the current temporal-support handoff](.agent-work/notes/symbol-burst-temporal-support-current-20261020.md).
Historical failures and repair evidence remain preserved.
