# Fractional timing in the wide-band Matrix FSK receiver

28 November 2026. The earlier ten-bit pointing-loss experiment places the true
arrival at either −32 or +32 samples relative to the public calendar. Both are
members of its five coherent timing candidates. Its successful reception therefore
does not establish coverage between those candidates. This study tests that gap
using the same native sample receiver, with a selectable denser timing search.
It preserves the earlier results and their thresholds. [Original pointing study](../../_appraisal/algorithms-matrixModem/block-reception/pointing-loss.md)
· [New experiment and fixed protocol](../../_appraisal/algorithms-matrixModem/block-reception/timing-coverage.md)

## Why a small timing error matters

The 352 physical tone centres have signed FFT indices `k = −176,…,175` in a
512-sample cell at 3,200 complex samples/s. On a stationary tone, an arrival
displacement of `d` samples changes the projection by `exp(−i 2π k d/512)`.
A common phase is harmless to an energy decision; the phase differences between
different positions are not. The coherent branch must correct this slope before
adding tones at different frequencies.

As a theoretical reference, averaging this phasor uniformly over all positions
gives the magnitude

```text
|A(d)| = |sin(π · 352 d/512) / (352 sin(π d/512))|.
```

For an arrival at +16 samples and historical timing candidates −64, −32, 0, 32
and 64, every residual is an odd multiple of 16. Each has zero mean phasor in
this uniform-position calculation. The actual codeword occupies only 64 tones,
with a structured schedule; this calculation predicts a serious failure mechanism,
not zero received power or a particular word-error rate. Shorter coherent windows
and the per-tone fallback can still recover some words. Their actual outcomes
must be counted after admission calibration.

A wrapped negative bin `k+512` gives the same correction as `k` only at integer
sample displacements. At `d=0.25`, the two correction factors differ by a quarter turn.
The new fractional correction uses the signed physical frequency. Integer timing
results alone cannot detect this error.

The experimental grid has 513 points spaced by 0.25 sample over ±64 samples.
The worst distance to a grid point is 0.125 sample. For phase-matched contributions
with nonnegative weights on this frequency interval, removal of an irrelevant
centre-frequency phase bounds the retained coherent power from timing quantization
alone below by

```text
cos²(π · (351/512) · 0.125) = 0.929258…  (0.31864 dB loss).
```

The equal-position average retains 0.975939. These bounds exclude moved boundaries,
carrier mismatch, noise, quantization, filtering, channel phase and competition
between words. They are not sensitivity bounds for the full receiver. A finer
grid can reduce this particular loss; its cost and larger searched maximum still
need to be included. The specification's `1/(8 × band span)` is a proposed design
spacing, not a universal result from synchronization literature.

## A phase slope does not move the aperture

The comparator reuses the three existing FFT apertures at −64, 0 and +64 samples.
Each fine timing uses the nearest aperture, with the central aperture used at the
±32 ties. Consequently a corrected aperture can still be displaced by 32 samples
from its hypothesized tone boundary. This is the same `U/16` displacement for which
the retained adjacent-tone counterexample loses 1.160 dB when the next tone has
opposite phase; equal neighbouring phases retain full power. Fractional phase
correction cannot restore samples absent from the aperture. [Finite-window analysis](fsk-search-validity.md#what-a-phase-slope-and-a-tilted-bin-path-can-correct)

The signal fixture evaluates the rectangular waveform at the actual fractional
arrival, including each tone transition, before adding noise. It is a sampled
analytical waveform, not a measured analog frontend or a bandlimited fractional
delay filter. Known-arrival calculations are diagnostics only: neither receiver
receives the true timing or word. The experimental receiver still searches the
entire declared bank. This distinguishes improved phase-grid coverage from exact
shifted-window matching and from acquisition of an unknown calendar.

## Literature cross-check and alternatives

Ji and Wu observed the dominant correlation peak move as the first arriving path
faded on a 12.36 MHz HF link carrying repeated 8PSK. Absolute path delay was
unknown in that field recording. In a later simulation with two paths 0.4 ms
apart, the composite peak stayed biased as signal-to-noise ratio increased.
Tracking the individual paths reduced delay error in that model. Both studies
measured timing on known signals. For Matrix FSK the design consequence is to
retain the configured timing range and the per-tone decision even when one peak
is strong. How often a Matrix word would be lost by locking to that peak remains
unmeasured. [Ji and Wu, 2025, §4.2](https://doi.org/10.3390/s25144246) · [Ji and Wu, 2026, §4.1.4](https://doi.org/10.3390/s26051640)

- **Long coherent integration has more than one synchronization limit.** Majoral,
  Arribas and Fernández-Prades (2024) discuss data transitions, residual carrier
  error and oscillator instability, and implement a high-sensitivity Galileo
  receiver. Their review of partial correlations and transition-aware processing
  supports accounting for boundaries as well as phase. The GNSS codes, pilots and
  measured operating points do not transfer to Matrix FSK. [Primary article, introduction and literature review](https://doi.org/10.3390/s24051416)
- **Using the whole word can help, but the observation model matters.**
  Pfletschinger (2026) derives frame synchronizers that use the data and surrounding
  noise as well as a preamble; a metric optimal for repeated synchronization words
  need not be suitable for a single packet. The paper assumes one packet in a
  symbol-rate observation and a memoryless, non-dispersive channel. It does not
  qualify fractional timing, absent-block admission or physical quiet completion
  for this modem. It supports testing a whole-word hypothesis rather than making
  individual visible tones a prerequisite. [Primary article, §§2–5](https://doi.org/10.1186/s13638-026-02629-2)
- **Newer work distinguishes delay motion from a constant phase correction.**
  Yuan et al. (2026 volume; online 29 December 2025) describe correcting chip range
  walk using a sliding correlation matrix and fractional-delay compensation. The
  publisher's abstract and parameter tables were inspected; the complete derivation
  was not reviewed. Their DS/FH spacecraft example uses a 1 GHz sample rate and
  a very different spreading waveform. It motivates a separate clock-rate and
  moving-boundary check, not a Matrix performance claim. [Publisher abstract and tables](https://hkxb.buaa.edu.cn/EN/Y2026/V47/I12/332839)

Direct evaluation was selected here to preserve every word and timing hypothesis
and make comparison with scalar sums straightforward. A transform over coefficients
grouped by signed frequency is another possibility, but the many windows of the
short coherent rungs can dominate its cost. A local timing refinement seeded by
strong tones or a shortlist of words is cheaper only if its missed weak words are
acceptable and measured. It supplies no general replacement for the complete bank.
Dedicated preambles would change the waveform and require an energy, deadline and
sensitivity comparison; they are not a free remedy for this existing format.

## Scope of the result

At 15 dB block energy with arrivals drawn uniformly across ±64 samples, the
joint rule recovered 15/120 words on the old timing grid and 106/120 on the
dense grid, with 91 paired gains and no losses. The old grid admitted one wrong
word; the dense grid admitted none. On the grid-matched control, the larger
search's higher calibrated cutoff cost thirteen recoveries (105 to 92 of 120).
The independent-phase fixture retained all 100 per-tone recoveries under both
joint rules. These results favor broad timing coverage over the old fixture's
on-grid advantage for an unknown arrival within this configured range.
[Complete counts and calibration](../../_appraisal/algorithms-matrixModem/block-reception/timing-coverage.md#results--28-november-2026)

Both joint rules admitted 3/400 fresh quiet blocks, with one-sided 95% upper
bound 1.927%. Warm serial paired scoring took about 0.192 s on quiet and
0.209 s on active input on the 5650U. Neither the quiet result nor the timing
measurement establishes the stricter attended error target or complete modem
capacity. The 513-point route remains a selectable experimental comparator;
its calibration is not interchangeable with the historical ordinary receiver's.

A separately frozen follow-on tested 160 fresh unrelated-schedule transmissions
at each of 24 and 36 dB. Both joint rules falsely admitted one at each level,
through the shared per-tone branch; neither coherent branch admitted one. Each
joint count has an exact one-sided 95% upper bound of 2.93045%. This small control
does not qualify arbitrary interference rejection, and the paired power levels
must not be pooled as independent trials. [Control protocol and provenance](../../_appraisal/algorithms-matrixModem/block-reception/timing-coverage.md#follow-on-unrelated-schedule-control)

The experiment is a configured-calendar, ten-bit comparison on single-ray,
two-ray and independent-tone-phase fixtures, with a nominal 1% diagnostic
false-admission budget. See its results for the actual frozen-threshold quiet
rate, paired recoveries and complete configured-block costs. It does not certify
the attended 10⁻⁴ target, 16/24-bit service, clock drift, frontend filtering,
field propagation or unknown-epoch acquisition. The separate 32-component
diffuse-channel approximation remains conditional on that finite model; this
timing study does not resolve its discrepancy with a continuous Doppler spectrum.
[Channel-model qualification](fsk-reconciliation.md#choosing-tone-length-and-power-constraints)
