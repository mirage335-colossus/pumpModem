# Multi-day and multi-year duration forecasts

The isolated weak-receiver planner now verifies native waveform geometry for
selected durations beyond 3,600 seconds, including days and years, without
generating the waveform. This completes the long-forecast path; it does **not**
qualify sampled multi-day reception. The [maintained guide](../../../triad-duration-planning.md)
documents the CLI, theory and remaining model assumptions.

The probability search already had a ten-Julian-year default horizon. The
previous limitation was that the native exporter rejected long `--plan-only`
requests and the wrapper skipped their final geometry check. Only the native
plan-only guard was lifted. Sampled export and power calibration retain the
3,600-requested-active-second limit, 2 GiB artifact budget and independent
calibration/sample constraints. Native checked integer geometry still rejects
overflow. No duration-sized samples, coefficients, channel histories or receiver
arrays are allocated by these forecasts.

The wrapper adds `--days` and `--max-active-days`, rechecks every selected span,
preserves exact integer tile counts, and permits only four binary64 ULPs of
time-arithmetic disagreement. It retains a report when geometry verification
fails. Availability separates a duration forecast, meeting the modeled detection
allocation, native geometry verification and sampled-export preflight.
In particular, a manually specified 38-day duration is not automatically a
successful detection forecast.

## Representative outputs

These are **model forecasts with checked source geometry**, not measured
receiver success probabilities. All cases retain the existing requested 99%
correct reception, 10⁻⁴/day conditional admission budget and two declared
duration alternatives. The detector tail allocation, original grid prior and
selection rule are unchanged. New search diagnostics are separate from that
charged prior; no improved long-duration acquisition algorithm is asserted.

| Configuration | Target C/N₀ | Active DATA forecast | Geometry |
|---|---:|---:|---|
| Stable, Rate 100 / Added 1 | −40 dB-Hz | 8.46288 days | Verified |
| Stable, Rate 100 / Added 1 | −50 dB-Hz | 84.79226 days | Verified |
| Stable, Rate 100 / Added 1 | −60 dB-Hz | 865.12508 days | Verified |
| Changing HF, Rate 36 / Added 10 | −16 dB-Hz | 6.07469 days | Verified |
| Changing HF, Rate 36 / Added 10 | −20 dB-Hz | 38.06156 days | Verified |
| Changing HF, Rate 36 / Added 10 | −30 dB-Hz | No passing duration within ten years | No selected duration |
| Stochastic low-rate VLF, Rate 0.01 / Added 1 | −30 dB-Hz | 8.00006 days | Verified |
| Stochastic low-rate VLF, Rate 0.01 / Added 1 | −60 dB-Hz | No passing duration within ten years | No selected duration |

Stable means OCXO GPSDO base/shift oscillators, 500 kHz shift, 1725 Hz IF, bounded
timing prior and None/Off spread/modulation, using the legacy fixed-amplitude
model. HF retains the preceding study's free-running base/IC-7100 shift,
30 MHz shift, broad timing, HF worst margin 3, per-ray stochastic width 0.5 Hz,
0.2 µs moving-delay excursion / 20 s knots and Each 65% modulation. VLF uses 8270 Hz
IF, zero RF shift, OCXO/shared-clock topology, bounded timing, Sub9 best
margin 1, stochastic width 0.001 Hz and Each 65%. The supplied target C/N₀ is a
forecast input, not an assertion that the configured powers and default
fixture path loss physically produce that noise density. Complete commands
and parameters are in `protocol.json` in the evidence archive.

Additional manual forecasts verified 38.00026 active days for VLF and
38.24173 days with Added 1000, rounded to their respective legal native tiles.
Both fail the modeled detection allocation at −60 dB-Hz. A stable 38-day request
with three wait tiles gives 38.00008 active days and 151.99997 elapsed days;
wait time contributes no DATA energy. A 3652-day request also verifies.
These are geometry/forecast checks, not qualification of maximal-spreading,
burst reception, long-term channel stationarity or Helper availability.

## Synchronization and admission limits are explicit

The report compares the current final grid with hypothetical full-span coherent
frequency spacing `1/(4T)` and a clock endpoint error of at most 1/8 fine chip.
The former gives about 0.2244 dB worst nearest-bin loss for rectangular coherent
integration; the latter is an engineering assumption requiring shaped-waveform
validation. It also reports 0.5 s / 2 s coherent-window alternatives. A renewing
receiver can use shorter coherence, with its separate sensitivity cost.

For the stable −60 dB-Hz forecast, full-span coherent frequency spacing would be
about 3.34 nHz, or 30,003 points across the original oscillator bound. The current
three final-grid frequency points do not meet that reference. For the changing
HF −20 dB-Hz forecast, hypothetical full-span coherence would require about
2.38 billion frequency points; the existing clock grid would accumulate about
7,400 fine chips of endpoint timing error. This does not imply that a useful
renewing/phase-tracking receiver must perform that giant coherent search.
It demonstrates why increasing a duration alone cannot establish acquisition
adequacy. Long-term drift, trajectories and receiver computation remain
unqualified.

Daily alpha remains daily. A separately stated assumed accounting horizon adds
per-bit Helper lead, up to 30 s UTC alignment, pulse halos and endpoint allowance;
the union bound charges its covered 24-hour periods without assuming independence.
The 865.125-day example charges 866 days: the conditional bound is 0.0866 over
that assumed operation, not 10⁻⁴ for the entire message. Longer listening
schedules need their own exposure bound. None of these calculations qualifies
a physical audio/ADC or Helper-residual noise null.

## Cheap validation and resource evidence

* Native plan-only cases: 3,600 s, 1 day, 38 days, 365 days and 10 Julian years; burst
  active/elapsed separation; oversized sampled/power requests refused; extreme
  integer overflow explicitly classified. The new CTest passed in 0.74 s.
* 46 focused Python tests passed, 0 errors/failures/skips, about 2.15 s including
  imports. They preserve short 8 dB-Hz selected durations, check day/second APIs,
  geometry failure receipts, exact tile comparisons, search resolution, and
  a two-bit exposure boundary that must charge three rather than two days.
* The existing isolated native Helper exporter test passed in 33.62 s, covering
  its established short sampled paths. The ordinary Robust campaign was not run.

Fresh-process native measurements at the five spans took 0.065–0.080 s wall time
and 11,756–12,184 KiB maximum child RSS. JSON output grew from 5,639 to 5,689 bytes.
These are single-run engineering observations, not confidence-qualified runtime
forecasts. The source audit also verifies return before waveform generation.
At ten years the geometry represents about 1.89 trillion source samples, while
the reusable tile-workspace bound stays unchanged for that format.

No audio test or cloud resource was needed: transmission/reception algorithms
and short waveform samples were unchanged. GPU setup would not accelerate
these small geometry/probability calculations.

Reproduce the routine checks with an existing compatible `build/dev-cli`:

```sh
ctest --test-dir build/dev-cli -R '^triad_helper_plan_long$' --output-on-failure
PYTHONPATH=tools:tests OPENBLAS_NUM_THREADS=2 OMP_NUM_THREADS=2 MKL_NUM_THREADS=2 \
  /tmp/triad-weak-env/bin/python -m unittest test_triad_duration_model \
  test_triad_weak_plan test_triad_operational_admission test_triad_weak_receiver
```

For a changed native exporter, configure through `./build.sh configure --cli`,
build `triad_helper_export`, then run the focused checks. The existing sampled
exporter test is a separate occasional check; do not repeat old sensitivity or
Live campaigns for unchanged forecast-only paths.

`summary.json`, `evidence.tar.gz` and `SHA256SUMS` retain source/build identities,
commands, forecasts, resource receipts and test outcomes. Base revision is
`045458f4` plus the recorded changes. Initial build failure was a read-only
ccache directory, resolved by bypassing the compiler cache. `/usr/bin/time`
was unavailable, so resource measurements use fresh-process POSIX child usage.
The first overflow test incorrectly required a nonzero exit; it was corrected
to accept the native planner's explicit infeasible-geometry JSON. Initial
reports before review corrections are retained separately.

Limits remain deliberate: stationary channel/oscillator assumptions, acquisition
and bit-identification error budgets, physical false admissions and multi-day
receiver execution are unqualified. Extremely large custom horizons can also
exceed integer or tile-rounding representability; the wrapper records failed
verification rather than silently accepting a different duration. Checked
geometry is not a complete future UTC/Helper calendar qualification.
