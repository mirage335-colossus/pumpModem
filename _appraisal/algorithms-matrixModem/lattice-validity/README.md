# Finite-cell FSK checks

This deterministic reference calculation uses the unfiltered complex 3,200 Hz baseband, 512-sample cell, and 6.25 Hz position spacing of the FSK draft. All results use double precision. The following cases and stopping rule were set before execution.

1. Project a complete on-grid tone, an off-grid tone at +1 Hz, and a half-bin tone onto the rectangular cell basis. Move the receive window by 32 samples, one half-step of the proposed timing grid. Compare equal, absent, and opposite-phase neighbours on the same position. Compute the white-noise correlation of aligned neighbouring bins, a quarter-cell erasure and overlapping timing hypotheses.
2. Use a one-ray control and two coherent rays 1.25 ms apart with gains of equal magnitude. Place the destructive or constructive midpoint between two adjacent tones on the 12.5 Hz HF grid. Enumerate all 16 combinations of previous position, current position and relative phase. Report total received energy and correct-bin projection power averaged on the linear scale. Repeat at two positions separated by 500 Hz on the same channel, and with the weaker ray down 3 dB in the narrow band.
3. Compare two distinct one-cell tone hypotheses under proper complex Gaussian fading and white noise, using Gaussian Doppler widths (2σ) of 0, 1, 10 and 30 Hz. Evaluate the low-SNR pairwise KL coefficient for separations of one and five bins. Average over all distinct pairs in 352 available positions and in 70 available positions five bins apart. This compares uniform placements for two distinct values. The 70-position set accommodates a 64-symbol alphabet.

Stop after these finite sums and analytic consistency checks. There is no sampling uncertainty, seed selection, decoder benchmark or field-channel frequency estimate.

From the repository root:

```sh
c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic _appraisal/algorithms-matrixModem/lattice-validity/lattice_validity.cpp -o /tmp/matrix-lattice-validity
/tmp/matrix-lattice-validity
```

The [research note](../../../_ref/algorithms-matrixModem/fsk-lattice-validity.md) gives the formulas, recorded results and limits.
