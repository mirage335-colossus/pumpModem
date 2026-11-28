# Short-message energy and keyed-codebook calculations (Fable-5.2 pass, 26 November 2026)

Standalone programs written by the coding reviewer of this pass. They support the
[coding and interleaving review](../../../_ref/algorithms-matrixModem/Fable-5.2/reviews/review-coding-interleaving.md),
the [coding note](../../../_ref/algorithms-matrixModem/Fable-5.2/coding-and-message-structure.md)
and the message format in the [FSK specification](../../matrixModem-FSK.md). The detailed historical sizing assumptions are in the [dated design study](../../../_ref/algorithms-matrixModem/fsk-design-study.md). They are
analysis aids, not modem code, and use only the standard library.

**Purpose and retention.** These are theoretical calculations. `short_message_energy.cpp` holds the
exact finite-length reference values that bound any later coded simulation: energy per message for
1–300 bits, the 10-bit premise, sectioning loss, combining loss, margin against erasures and the
quiet-slot test. Keep them under the
[theoretical-calculation rule](../../../docs/development.md#theoretical-calculations). Replace one
only with a corrected or extended calculation that carries its values forward.

| Program | What it computes | Output | Runtime (12-thread 5650U) |
| --- | --- | --- | --- |
| `short_message_energy.cpp` | Energy per message for `2^k` orthogonal codewords with maximum-likelihood detection: noncoherent and coherent AWGN (exact), the Polyanskiy–Poor–Verdú converse, Rayleigh fading with an optimised number of independent branches (exact); ratios against 1 and 10 bits and the marginal cost per bit; sectioning into independently decoded blocks; square-law combining loss against the number of looks; energy against a capped branch SNR; tolerable erased fraction against margin; the quiet-slot test on the largest codeword score; the cutoff rate of q-ary noncoherent symbols | `results/short_message_energy.md` | about 8 min |
| `codebook_mc.cpp` | Monte Carlo of a keyed random q-ary codebook with exhaustive decoding against the orthogonal reference, including a product-structured codebook; information of one symbol repeated over Rayleigh looks against an ideal lower-rate code; the same decoder under interference for several cell metrics without side information against genie metrics | `results/codebook_mc.md` | 21.5 min |

```bash
cd _appraisal/algorithms-matrixModem/short-message-energy
g++ -O2 -std=c++17 -fopenmp short_message_energy.cpp -o /tmp/short_message_energy
/tmp/short_message_energy > results/short_message_energy.md
g++ -O3 -march=native -std=c++17 -fopenmp codebook_mc.cpp -o /tmp/codebook_mc
/tmp/codebook_mc > results/codebook_mc.md
```

## Values the specification uses

- **Ten-bit premise.** `E(10)/E(1)` is 2.48, 2.01 and 1.77 at message error rates of 10⁻², 10⁻³ and
  10⁻⁴ on noncoherent AWGN, and 2.53, 2.02 and 1.78 on Rayleigh fading with optimum diversity. The
  energy reaches twice the one-bit energy at `k = log₂(1/ε)` bits.
- **Validation.** The Rayleigh values reproduce Yang, Durisi and Polyanskiy's published table
  (66.3 against 67 at 10 bits, 238.6 against 2.4·10² at 100 bits, 1319 against 1.3·10³ at 1,000
  bits), and the AWGN value at 16 bits reproduces the earlier
  [`calc/`](../calc/README.md) result (2.97 dB).
- **Diversity.** At 10 bits and 10⁻³ the Rayleigh requirement is 18.2 dB with 18 branches, 21.0 dB
  with 4, 26.4 dB with 2 and 38.8 dB with 1; with 128 branches it is 20.0 dB.
- **Sectioning.** 100 bits as four 25-bit blocks: 2.11 dB (AWGN) and 2.92 dB (Rayleigh) more than
  one 100-bit codebook. 300 bits as twelve: 3.19 and 4.70 dB.
- **Keyed codebook against the orthogonal reference** (Monte Carlo). A random 64-tone codebook needs
  0.21 dB more at 10 bits and 0.71 dB at 20 bits; with 256 tones the 20-bit figure is 0.28 dB. With
  16 tones the 10-bit figure is 0.74 dB and with 8 tones 1.42 dB.
- **Unprotected sums fail.** With 1% of cells hit 20 dB above the noise, a summed-energy decoder
  with 6 dB of margin has an 80% message error rate; a normalised and clipped metric has no error in
  at least 10⁵ trials.
- **Weak looks.** At a branch SNR of −10 dB a 10-bit block needs 4,744 branches and pays 8.55 dB
  against the optimum branch SNR.
- **Margin against erasures** and the **quiet-slot test** tables are quoted in the review's
  sections 5.5 and 6.

Model limits: ideal orthogonality, independent branches, known timing and frequency, Gaussian
noise of known level, no interference (except part 3 of the Monte Carlo), no acquisition cost and
no quantization.
