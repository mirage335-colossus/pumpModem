# Coding and message structure for the FSK lattice

26 November 2026 · Fable-5.2 pass · synthesis of the coding review, the lattice calculations and the GPU run

This note gives the reasoning behind the message format of the
[FSK specification draft](../../../_appraisal/matrixModem-FSK.md). The evidence is in the
[coding and interleaving review](reviews/review-coding-interleaving.md), with its exact energy
tables in [`short-message-energy/`](../../../_appraisal/algorithms-matrixModem/short-message-energy/README.md),
in the [lattice design note](fsk-lattice-design.md) and in the
[computation note](computation-and-simulation-speed.md). The code proposed here has not been
simulated end to end; statements about it that are not in those sources are marked as inference.

## Answer in brief

- **The 10-bit minimum is sound.** With `2^k` orthogonal codewords and maximum-likelihood
  detection, 10 bits need 2.01 times the energy of one bit at a message error rate of 10⁻³ on AWGN
  and 2.02 times on Rayleigh fading with optimum diversity (2.5 times at 10⁻², 1.8 times at 10⁻⁴).
  The reason is the reliability term, not convolutional coding: the knee sits at `log₂(1/ε)` bits.
- **More bits per block are cheap.** A 24-bit block needs 1.5–1.6 times the energy of a 10-bit
  block by the exact tables at 10⁻³, and 1.27 times the airtime in the weak-signal regime when the
  block is sized by a 10⁻⁶ presence test. Padding
  a short message into a 24-bit block is therefore better than sending it alone only if the
  padding carries something; a 10-bit block should exist as its own size.
- **For 10–24 bits, an exhaustive search of a keyed codebook is the right decoder.** It is
  maximum-likelihood, has no error floor and no convergence threshold, does not depend on the
  noise level, treats erasures as zeros, and makes detection, synchronization and decoding one
  operation. A keyed random 64-tone codebook is within 0.2 dB of the orthogonal reference at
  10 bits and 0.7 dB at 20 bits; with 256 tones the 20-bit figure is 0.3 dB. On the GPU a full 24-bit decode takes 0.12 ms.
- **Splitting a long message into such blocks is expensive.** 100 bits as four 25-bit blocks cost
  2.1 dB (AWGN) to 2.9 dB (Rayleigh) more than one 100-bit code; 300 bits as twelve cost 3.2 to
  4.7 dB. Runs of short blocks are right for one or two blocks and wrong for a few hundred bits.
- **Long blocks need a different code, and the choice is open.** The candidates are a tail-biting
  convolutional code over the tone alphabet with a CRC and list Viterbi decoding, and a Q65-like
  nonbinary repeat-accumulate or LDPC code. The first has no abrupt threshold and fits the
  preference for no drastic failure; the second is estimated 0.5–1 dB better at 100–300 bits. A
  K = 32 sequential decoder is the wrong family.
- **The rate reduction that weak signals need belongs to the dwell and to repeated keyed dwells,
  not to the code.** Repetition with energy combining collects at least 93% of what an ideal
  lower-rate code would while a symbol carries less than about a third of its bits.
- **Presence costs more than decoding.** At the point where a 10-bit block decodes with error
  10⁻³, its best score separates it from noise with only about 10⁻² reliability. Sizing the block
  for the presence test, or adding 1–3 dB, makes two scored quiet blocks a sound end of message.

## Block structure proposed for the specification

One block carries `k` information bits (10, 16 or 24) over `n` code symbols, each sent on `L`
dwells.

- **Code.** The tone index of code symbol `j` is `G_j·c xor m_j`: a keyed binary matrix `G_j` of
  8 × `k` applied to the message `c`, and a keyed mask. This is a random linear code over GF(2)
  whose 8 output bits per symbol select one of 256 tones jointly, so the decoder works on symbol
  metrics and pays no bit-metric loss (2.5 dB at 64 tones if it did). [Inference: a keyed random
  linear code has the pairwise statistics of a random codebook, which the review's Monte Carlo
  measured; the linear structure itself was not simulated.]
- **Symbols and repeats.** `n` = 32 symbols. Any two codewords agree in a symbol with probability
  1/256, so the code behaves as `2^k` nearly orthogonal sequences. `L` repeats of each symbol carry
  the rate reduction. The total of `n·L` dwells never falls below 32, which supplies the diversity a
  short block needs: the exact tables put the optimum at 18–37 independent branches and show a
  drastic loss below about 8 (at 10 bits: 18.2 dB with 18 branches, 21.0 dB with 4, 26.4 dB with 2,
  38.8 dB with 1).
- **Placement.** Every dwell of the block is assigned to a symbol by a keyed pseudo-random
  permutation of the block's dwell positions, redrawn per block, and the 256 tone values are mapped
  to physical positions by a keyed permutation redrawn per dwell. Nothing is periodic.
- **Accumulation.** The receiver keeps one table of 256 sums per symbol and adds each dwell's 4-bit
  evidence into it. Decoding is `Score[c] = Σ_j T_j[G_j·c xor m_j]` over all `2^k` messages.
- **Presence and end of message.** The block is present if the best score clears a threshold set
  from the null distribution of the same statistic, measured live on decoy keys. Two consecutive
  blocks scored absent end the message. A resumed schedule reopens it, with the gap as erasures.

Why 256 tones and 32 symbols, in numbers from the other notes:

- 256 candidates are 0.2–1 dB better than 64 when symbols are well filled and equal otherwise. In
  the review's codebook Monte Carlo a 20-bit block loses 0.71 dB to tone reuse with 64 tones and
  0.28 dB with 256. The direct decoder's cost does not depend on the alphabet.
- The direct GPU kernel does `n` lookups per codeword. With 24 symbols it ran 8,400 decodes of
  2²⁴ codewords per second; 32 symbols cost a third more.
- The linear map lets the search split the message into two halves, so the tables for one half are
  shared by all values of the other. That is what makes the direct kernel cache-resident, and it is
  also the "product-structured bank" that the review proposes independently for the matrix form.

## Why not a longer code for everything

| Decoder family | 10–24 bits | 100–300 bits | Failure modes | Shape |
| --- | --- | --- | --- | --- |
| Keyed codebook, exhaustive | 0.2 dB from the orthogonal reference at 10 bits and 0.3–0.7 dB at 20 bits (the review's Monte Carlo) | 2–5 dB behind as runs of blocks | none in decoding | one fixed gather or product per hypothesis |
| Tail-biting convolutional code over GF(q), CRC, list Viterbi | 0.3–1 dB (estimate) | 1.5–2.5 dB (estimate) | none with a list cap | regular max-plus recursion, not a matrix product |
| Nonbinary LDPC or repeat-accumulate over GF(64), belief propagation | not suited | 1–2 dB (estimate) | abrupt threshold; needs metric scaling; undetected errors need the CRC | gathers, 64×64 Hadamard products, FP16/FP32 |
| K = 32 sequential (WSPR, JT9) | — | bounded by the cutoff rate: at least 4.5 dB above 64-tone capacity on Rayleigh fading | unbounded computation | data-dependent tree search |
| Binary code on bit metrics | 1 dB or more | about 1.9 dB of bit-interleaving loss at 64 tones | — | mixed |

Gaps are against the Rayleigh orthogonal reference at 10⁻³. The estimates are the review's and are
its weakest numbers; only the first row and the sectioning losses are computed.

- A published point supports the tail-biting route on this kind of channel: rate-1/2 codes over
  GF(4) with CRC-aided list Viterbi decoding and 4-FSK on AWGN come within 0.59 dB of that
  channel's random-coding bound at 10⁻⁴ (King, Ryan and Wesel 2022). Larger alphabets and fading
  were left open there.
- Q65 is the fielded example of the nonbinary route.
- A convolutional code running across blocks was considered and set aside: its tail must be
  flushed, and partly filled blocks blur the quiet test. A tail-biting code closes inside its
  block.

**Recommendation.** Specify the short block now. Send longer messages as runs of 24-bit blocks
until a long-block code has been compared in a matched simulation, and say what that costs.
Qualify the tail-biting code first, because it has no threshold behaviour and reuses the block's
keyed placement, accumulation and presence test unchanged.

## Margin, erasures and block span

- A margin spent as stronger dwells and a margin spent as more dwells are different resources.
  Extra path loss is absorbed by energy per dwell; erased dwells are absorbed by their number.
- With dwells near 0 dB at the design point, the fraction of dwells that may be erased at random is
  0.57, 0.77, 0.87, 0.90, 0.91 and 0.92 for margins of 3, 6, 10, 13, 16 and 20 dB. With many weaker
  dwells it approaches `1 − 10^(−2X/10)`; with 18 strong branches it stays near 0.36–0.68.
- An outage that is flagged costs half as many dB as one that is not. Flagging needs the presence
  statistic over many dwells.
- A fade or burst of length `T_o` inside a block of span `T` erases `T_o/T` of the dwells. The
  block span should be about 1.3 times the longest outage to be survived at 6 dB of margin and 1.8
  times at 3 dB, with many weak dwells. Flare fadeouts with means of 13–39 minutes therefore need
  block spans of roughly 20–70 minutes, which weak-signal blocks have anyway.
- Diurnal closures of hours cannot be bridged inside a block unless it spans most of a day. They
  need repeated blocks or an erasure code over blocks, which is a layer above this format.

## Corrections to earlier documents

The review found three errors in earlier notes; each is annotated in place.

- The union-bound illustration in the
  [format note §5a](../Sol-6.2/fsk-format-for-fft-and-tensor-receivers.md) gave `E/N₀` of 15.2,
  27.7 and 152 for 1, 10 and 100 bits. The exact values are 12.43, 24.93 and 110.94, so relative
  airtime is 1 : 2.0 : 8.9.
- "Kennedy's optimum of about 3 per branch" overstates the branch count at 100 bits by a factor of
  two; the optimum branch SNR is 3.7 at 10 bits and 6.5 at 100 bits. The error that matters is on
  the other side, too few branches.
- WSPR's published threshold is −31 dB, not −28 dB, which puts its K = 32 code level with FST4W's
  LDPC code in data energy.

## Open items

- A block-error simulation of the proposed linear code with keyed placement, 4-bit evidence, the
  presence test and a hypothesis search. Nothing in this note replaces it.
- The long-block comparison: tail-biting list Viterbi against a nonbinary code, at 100 and 300
  bits, in Rayleigh fading with erasures, at matched airtime.
- Pilot positions for a long-block format, since exhaustive search no longer supplies
  synchronization there.
- The false-presence rate under interference, with the decoy-calibrated threshold.
- An outer layer for outages longer than a block.

## Addendum of 27 November 2026: adaptation, the ten-bit expectation and alternatives

Questions put after the tone and hop terms were separated: does the coding adapt to tone bandwidth
and total bandwidth; is the convolutional code fast for short messages and well chosen; might
something else be better; and is the expectation that about 10 bits take less than twice as long
as one bit at similar confidence in the specification.

**The premise about the code.** Short blocks do not use a convolutional code. They use the keyed
random code of this note with exhaustive decoding. The tail-biting convolutional code is only the
unqualified candidate for long messages.

**What adapts.** Tone length and level through the repeats `L`; total bandwidth through the keyed
value-to-position map (one-to-one from 256 positions, balanced below, with `L` at least
8/log₂ of the positions); block length through the keyed pseudo-random interleaver. **What does
not:** 32 symbols per block (16 or 8 would save 0.8 or 1.4 dB on steady paths with long tones), the
three block sizes, and the code. Burst and outage tolerance come from sizing rules, not from the
format.

**A gap found here: the interleaver does not know about hops.** With one-cell tones a 32-tone block
lasts about 5 s. Inside a 40 s hop it sits on one hop frequency, so one bad frequency takes the
whole block. Nothing spreads a short block across hops. Not yet specified.

**Ten bits against one bit** [calculated,
[section 15](../../../_appraisal/algorithms-matrixModem/fsk-lattice/results/lattice_receiver_section15.md)].
The expectation holds comfortably under the specification's sizing, which is a presence test at a
false-presence rate of 10⁻⁶ over `A·2^k` tests and a miss rate of 10⁻³. Transmit time against one
bit:

| Hypotheses `A` | 10 bits, block added as 1 / 32 / 1,024 energies | 24 bits |
| ---: | --- | --- |
| 1 | 1.27 / 1.19 / 1.15 | 1.66 / 1.45 / 1.35 |
| 400 | 1.20 / 1.14 / 1.11 | 1.51 / 1.35 / 1.27 |
| 4,000 | 1.19 / 1.13 / 1.10 | 1.47 / 1.33 / 1.25 |

By the Gaussian deflection of the sizing rule the 10-bit ratio is 1.20 to 1.30 and the 24-bit
ratio 1.50 to 1.73. On message error alone the exact ratios of this note stand: 2.48, 2.01 and
1.77 at 10⁻², 10⁻³ and 10⁻⁴, so "less than twice" then needs an error rate stricter than about
10⁻³. The brief does not state the expectation; the specification now does, in section 1.

**Is the short-block code well chosen.** For 10 to 24 bits, yes on the evidence of this note:
within 0.2 dB of ideal orthogonal signalling at 10 bits and 0.3 to 0.7 dB at 20 bits; no sync, CRC
or termination overhead; repetition costs nothing because two messages already differ in nearly
every symbol. The presence test sets the block length, so relaxing the placeholder false-presence
rate would shorten blocks more than a code change.

**Where something else would be better.** At roughly 30 to 300 bits, where runs of 24-bit blocks
cost 2 to 3 dB at 100 bits and 3 to 5 dB at 300. The convolutional candidate has the precedent of
coherent sub-9 kHz links; a nonbinary LDPC code of the Q65 kind is the main alternative. They
have not been compared. Pushing the exhaustive search past 24 bits doubles the decode work per
added bit and was not evaluated.

## Addendum of 27 November 2026: surviving the loss of hops

Requirement as stated: the interleaver should suffice to withstand the loss of hops. With keyed
hop frequencies and the SNR path-loss margin, communication should continue if something like half
of the hops are unusable, for example because of over-the-horizon radar on some frequencies or
during some hops, and such interference should have no more effect than a worse noise floor. This
replaces the gap noted in the previous addendum.

**What half the hops cost.** A fraction `p` of hops lost costs 1/(1 − `p`) in usable time: 3 dB of
time for one half, about 1.5 dB of power for weak tones. The margins of this note cover it: 3 dB
tolerates 57% of the tones lost, 6 dB 77%, 10 dB 87%.

**What the block needs is enough hops for the loss to average out** [calculated, exact binomial,
[section 16](../../../_appraisal/algorithms-matrixModem/fsk-lattice/results/lattice_receiver_section16.md)].
Hops a block must span so that more than the tolerated fraction is lost with probability at most
10⁻³ (10⁻² in brackets), tones spread evenly over the hops:

| Each hop lost with probability | 3 dB margin | 6 dB | 10 dB | 13 dB | 16 dB | 20 dB |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.25 | 22 (15) | 10 (6) | 5 (4) | 5 (4) | 5 (4) | 5 (4) |
| 0.50 | 492 (285) | 33 (19) | 18 (11) | 14 (7) | 14 (7) | 14 (7) |
| 0.75 | – | 4,436 (2,536) | 117 (65) | 72 (43) | 60 (37) | 54 (31) |

- A block that spans at least 32 hops survives the loss of half of them at margins of 6 dB and
  more. That is now the rule in the specification: where the level would allow a shorter block,
  the repeats are raised until it fills 32 hops.
- A 3 dB margin tolerates 57%, too close to one half, and needs about 500 hops.
- The price is the shortest block: 32 hops are 13 s with 0.4 s hops and 21 minutes with 40 s
  hops, whatever the level. That is a further reason for short hops when the signal is strong.
- **The receiver has to make a bad hop count as noise.** It estimates each hop's noise level from
  that hop's own bins, weights the hop's evidence by the inverse, and erases a hop beyond range;
  the evidence clip bounds any one tone. This is the per-hop form of the normalisation that the
  interference note found necessary: an unprotected sum failed with 1% of cells hit.
- Limits: hops are taken as lost independently and treated as erasures. Radar that degrades a hop
  without erasing it, losses that are correlated between hops, and the per-hop noise estimate
  itself were not simulated.
- **Settled on 27 November.** The 32-hop minimum stands as a reasonable point of diminishing
  returns. Half the hops lost is treated as extreme: over-the-horizon radar is itself unusual and
  is met mainly by enough transmit time to overcome the raised noise floor. The likelier case of
  one or two hops lost in 32 costs 0.14 to 0.28 dB of time (1/(1 − 1/32) and 1/(1 − 2/32)), under
  any margin. With 32 hops, 3 dB of margin covers a quarter lost and 6 dB covers half; 6 dB is no
  hardship, since a link should be planned with more budget than that. That 3 dB does not cover
  the half-lost case is accepted.
