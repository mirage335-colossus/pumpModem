# Finite-cell FSK checks

This deterministic reference calculation uses the unfiltered complex 3,200 Hz baseband, 512-sample cell, and 6.25 Hz position spacing of the FSK draft. All results use double precision. The following cases and stopping rule were set before execution.

1. Project a complete on-grid tone, an off-grid tone at +1 Hz, and a half-bin tone onto the rectangular cell basis. Move the receive window by 32 samples, one half-step of the proposed timing grid. Compare equal, absent, and opposite-phase neighbours on the same position. Compute the white-noise correlation of aligned neighbouring bins, a quarter-cell erasure and overlapping timing hypotheses.
2. Use a one-ray control and two coherent rays 1.25 ms apart with gains of equal magnitude. Place the destructive or constructive midpoint between two adjacent tones on the 12.5 Hz HF grid. Enumerate all 16 combinations of previous position, current position and relative phase. Report total received energy and correct-bin projection power averaged on the linear scale. Repeat at two positions separated by 500 Hz on the same channel, and with the weaker ray down 3 dB in the narrow band.
3. Compare two distinct one-cell tone hypotheses under proper complex Gaussian fading and white noise, using Gaussian Doppler widths (2σ) of 0, 1, 10 and 30 Hz. Evaluate the low-SNR pairwise KL coefficient for separations of one and five bins. Average over all distinct pairs in 352 available positions and in 70 available positions five bins apart. This compares uniform placements for two distinct values. The 70-position set accommodates a 64-symbol alphabet.
4. Send four contiguous two-cell tones through a static two-ray channel. Compare the settled second cells with the full tone intervals. Check both against a unit direct path and the finite boundary-error bound below.

Stop after these finite sums and analytic consistency checks. There is no sampling uncertainty, seed selection, decoder benchmark or field-channel frequency estimate.

From the repository root:

```sh
c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic _appraisal/algorithms-matrixModem/lattice-validity/lattice_validity.cpp -o /tmp/matrix-lattice-validity
/tmp/matrix-lattice-validity
```

The [research note](../../../_ref/algorithms-matrixModem/fsk-lattice-validity.md) gives the formulas, recorded results and limits.

## Four contiguous held tones

The four tones visit positions 0, 64, 128 and 192 in order with keyed phases 0°, 180°, 90° and 270°. Each lasts 1,024 samples. Transmission starts after silence. At the first position the tap gains are `(1+i)/2` and `(1−i)/2`, with delays of two and six samples respectively. Both rays carry their previous tone through every transition. The calculation retains the six-sample tail after the fourth tone. The scored intervals end at the nominal transmit boundary.

For each tone, `z` is its complex projection on the keyed transmit tone divided by the number of samples in the chosen interval. A unit flat channel gives `z = 1` for every tone on both supports. Consequently both columns below equal one for that control.

| Two-ray channel at nominal timing | Four-tone coherent power `abs(Σz/4)²` | Mean per-tone power `Σ abs(z)² / 4` |
| --- | ---: | ---: |
| Second cell of each tone | Zero analytically (below 10⁻²⁸ numerically) | 1.000000 |
| Full contiguous tones, including transitions | 4.07006 × 10⁻⁷ | 0.991033 |

The four settled projections are +1, +1, −1, −1. For each ray the old samples at a boundary can differ from the extrapolated new tone by at most twice the ray's amplitude for the duration of its delay. The full-tone projection therefore differs from its settled value by at most `2(2+6)/(√2 × 1024)` in magnitude. Squaring this gives an upper bound of 1/8,192 on the four-tone coherent power fraction for these frequencies in this order. These fixed noiseless projections illustrate cancellation. A word decision also depends on the other hypotheses, the competing words and the calibrated cutoffs. [Two-ray derivation](../../../_ref/algorithms-matrixModem/fsk-lattice-validity.md#when-the-tones-survive-but-their-coherent-sum-vanishes)
