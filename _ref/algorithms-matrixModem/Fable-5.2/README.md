# Matrix Modem FSK research pass (Fable-5.2)

26–27 November 2026 · Research, calculations, one GPU run and four literature reviews

This pass supports the [FSK specification draft](../../../_appraisal/matrixModem-FSK.md). It
builds on the [Sol-6.2](../Sol-6.2/README.md) and [Opus-5.5](../Opus-5.5/README.md) passes and
does not replace them; the directory name records provenance. Read each finding with its evidence
label. Nothing here is a qualified receiver: no end-to-end simulation of the proposed format
exists yet.

## Notes

| Note | Question | Evidence |
| --- | --- | --- |
| [FSK lattice design](fsk-lattice-design.md) | Which cell, dwell, alphabet and precision, and how long does a block take? | Exact large-alphabet calculations and Monte Carlo ([`fsk-lattice/`](../../../_appraisal/algorithms-matrixModem/fsk-lattice/README.md)) |
| [OTHR and interference tolerance](othr-and-interference-tolerance.md) | What keeps the link alive under over-the-horizon radar, and how much margin does it take? | Time-domain simulation of model radars ([`othr-interference/`](../../../_appraisal/algorithms-matrixModem/othr-interference/README.md)) and the OTHR review |
| [Coding and message structure](coding-and-message-structure.md) | Is the 10-bit minimum sound, and which code for short and long blocks? | Exact finite-length energy tables ([`short-message-energy/`](../../../_appraisal/algorithms-matrixModem/short-message-energy/README.md)) and the coding review |
| [Computation and simulation speed](computation-and-simulation-speed.md) | Can a DFT beat an FFT, where do Tensor Cores help, how fast is simulation? | One run on a full RTX PRO 6000 Blackwell ([`fsk-lattice/results/gpu/`](../../../_appraisal/algorithms-matrixModem/fsk-lattice/README.md#gpu-run)) and the DFT/FFT review |

Literature reviews written by delegated agents, kept in full with their own source lists,
verification marks and not-accessed lists:

- [OTHR and swept-FM interference](reviews/review-othr-interference.md)
- [FT8 and Q65 design evidence](reviews/review-ft8-q65-design.md)
- [Direct DFT versus FFT](reviews/review-dft-fft-computation.md)
- [Coding, look combining, interleaving and termination](reviews/review-coding-interleaving.md)

## Findings that change the design

1. **Keep FT8's 160 ms by 6.25 Hz cell as the lattice, and let the dwell and the repetition count
   adapt to weak signals.** The cell is the narrowest spacing that keeps every mid-latitude spread
   class under a sixth of a bin. A 160 ms *symbol* with one-bin detection is what fails: FT8 has a
   decode ceiling near 0.66 at 10 Hz of spread.
2. **The dwell decides sensitivity, and the long dwells need steady oscillators.** At
   −10 dB-Hz on a 0.1 Hz path the gap to the bound is 12.0 dB for a 0.4 s dwell, 2.2 dB for 4 s and
   0.3 dB for 40 s with the centre known. With an unsearched ±1 Hz shift the last two become 9.9 and
   9.4 dB. The 0.4 s dwell is the robust choice without a frequency reference. (First written as
   "need Doppler-shift tracking", with Helper guidance as the means. Finding 11 replaces that:
   Helper is a diagnostic stand-in and no dwell may depend on it.)
3. **A fresh keyed tone placement per dwell removes the tone-spacing constraint at short dwells.**
   256 candidates on adjacent positions lose nothing against wider spacing at 30 Hz of spread, given
   a spread-weighted metric.
4. **Four bits are enough after conditioning, and not before it.** Four-bit evidence loses 0.09 dB
   of time and four-bit samples 0.10 dB when the band is noise-dominated. A strong interferer has
   to be excised at full precision first.
5. **OTHR is handled by conditioning, not by margin or interleaving.** Comb excision, inverse-power
   weighting, an erasure code and median-normalised clipped evidence held the simulated loss to
   1.8 dB for coherent radars up to 60 dB above the noise. A strong incoherent swept radar is not
   covered.
6. **The 10-bit minimum is sound and 24 bits cost little more.** Ten bits need twice the energy of
   one bit at 10⁻³. A block is sized by its presence test: about 27% more airtime for 24 bits than
   for 10.
7. **Short blocks are decoded by exhaustive search; long messages need another code.** The search
   is maximum-likelihood, has no failure mode of its own and doubles as synchronization. Runs of
   short blocks cost 2–5 dB at 100–300 bits.
8. **No fixed route is fastest, so the receiver needs an adaptive scheduler scored by backlog
   cleared.** At 32,768 dwells per call a matrix-product DFT beats cuFFT when under about 40% of
   the bins are wanted, and ran at 70% Tensor Core activity; at 4,096 per call cuFFT is 2.1× faster.
   The message search ran fastest as a direct CUDA-core kernel. With a search on the same GPU all
   GPU transform routes fell to the same rate because the streams take turns, so rates measured
   while sharing hide cost; a race on those rates chose a route with twice the GPU cost of the
   best, and a fixed "CPU FFT, then GPU search" rule is 12–100× slower on weak-signal blocks. A running
   prototype of a cost-based rule stayed within 12% of the best fixed assignment in five scenarios
   where every fixed assignment lost by 2–73× somewhere. Computing only the tone-grid bins or shedding dwells trades
   a known 1–2 dB for 2–2.5× throughput. Every stage is millions of times faster than real time
   on one GPU.

9. **The fixed 160 ms cell is not necessary, and it is not a limitation.** A shorter cell never
   helps a weak signal; wide spreads (HF Worst, aurora, EME) cost 1.0–1.1 dB once the spread bank
   reaches them. On phase-stable paths such as sub-9 kHz, per-dwell reception is 18–38 dB from the
   coherent bound. A lattice scaled to cells of minutes was the first remedy considered; finding
   10 replaces it, and the scale remains only for antennas narrower than about 50 Hz.
10. **FHSS is optional, and leaving it off lets the receiver detect coherently across dwells
    with no change to the transmit format beyond dropping the guard.** Whole-cell dwells back to
    back are phase-continuous and of known phase. Coherent windows of 22 minutes bring a
    phase-stable path at −20 dB-Hz from 28 dB to 1.0 dB of the bound; a 10-bit block is estimated
    at 1.7 hours when coherent throughout, against 44 days per dwell. Very narrow bands keep the
    256-value symbols through a keyed balanced map onto 2–16 positions, at a simulated cost of 2.6,
    0.8, 0.2 and 0.0 dB. The receiver's extra work is the same decode in more table columns. The
    weakness is that a block sized for coherence fails without it.
11. **With FHSS the dwell has to match the level, and 40 s was too short for steady paths below
    −10 dB-Hz.** Phase is taken as new in every dwell. The best dwell is the shorter of the block
    divided by 32 and about 4 over the Doppler spread. A block of 32 such dwells is within 2.6 dB
    of a fully coherent block, where 0.4 s dwells are 19–39 dB away. At −20 dB-Hz on a steady
    path a 10-bit block takes 48 days with 0.4 s dwells, 9.8 hours with 40 s and 3.6 hours with
    400 s. Dwells of 400 s and 4,000 s are added and the dwell is adjustable. On paths with
    0.1 Hz of spread or more, 40 s is already at diminishing returns. A frequency error that
    changes from dwell to dwell acts like added Doppler spread unless the dwell is strong enough
    to locate itself: within ±1 Hz it costs 6–11 times the block time on a 0.1 Hz path. The 0.4 s
    dwell equals the occupancy limit of unlicensed hopping (47 CFR 15.247).
12. **Long dwells raise sensitivity and lower the key's protection against a matched observer.**
    An observer without the key gets the keyed receiver's information minus what a dwell reveals
    about its own position. With 32 dwells per block a filter-bank observer needs 1.6–5.7 dB more
    C/N₀ than the link; with 2,048 dwells, 7.5–14.2 dB. Against an energy detector the trend is
    the reverse (19.9–33.2 dB falling to 13.0–26.3 dB). The brief's intent that the long dwell
    maximises LPI therefore holds for energy detection only, unless finding 13 is adopted.
13. **A keyed phase in every cell hides the dwell's coherence at almost no receiver cost.** Each
    160 ms cell carries a keyed quarter-turn phase, which the receiver undoes exactly on 4-bit
    values before combining the cells of a dwell; sensitivity is unchanged at exact timing. The
    price is the timing search: hypotheses an eighth of the phase interval apart (20 ms for a
    step every cell), in offset and in clock rate, where a plain long dwell needs almost none.
    With timing known to a few milliseconds that is one hypothesis; a 3.6 h block on a ±20 ppm
    clock needs 26 rate hypotheses. Oscillator frequency drift adds nothing. A phase step every
    2, 4 or 8 cells divides the search by as much and returns about 1.4 dB per doubling to the
    observer. (First written as "four times as many as at 40 ms", which holds only at the 0.4 s
    dwell.)
14. **The tone length is separate from the hop dwell, and its default is one cell.** The draft
    had held one tone per hop (2.5 tones per second at the 0.4 s default); it now sends 6.25
    tones per second as intended. One cell per tone halves the shortest block and is quicker
    above about +10 dB-Hz. Below that it takes twice the block time on paths of 1 Hz or less
    (22 h against 11 h at −10 dB-Hz) and 1.1 times at 10 Hz. With one-cell tones a longer hop adds
    no coherence, and the phase key adds no timing search. Longer tones remain a setting.
    The specification no longer says "dwell": a tone is one tone on one position, a hop is the
    radio's stay on a hop frequency, a held tone is a tone as long as its hop. Earlier findings
    in this list that say "dwell" mean a held tone.
15. **Concealment from the phase key scales with phase segments per tone, not with a threshold.**
    About 1.4 dB per doubling. A phase step at about the tone rate gains 0.5–0.7 dB; the large
    gains need long held tones. On few positions without FHSS a step per tone removes the steady
    line at no timing cost.
16. **One-cell tones are concealed by keyed placement alone, and keyed hopping adds to it.** An
    observer needs about 8 dB more C/N₀ than the link from the 352 keyed positions, with no phase
    change. Keyed hopping adds the square root of the channel count (16.7 dB in all with 50
    channels, 23.2 dB with 1,000) while the observer cannot find the occupied channel. A 400 s
    hop holds 2,500 tones on one channel and gives some of that back at −10 dB-Hz (14.8 dB with
    50 channels); at −20 dB-Hz it does not (16.6, 22.5 and 25.5 dB for 50, 1,000 and 10,000).
    Gaussian estimate, steady path, idealised observer.
17. **For one-cell tones, hops should be short, and that is nearly free.** Hopping keeps its
    benefit to within 0.8 dB with at least 13 hops per block for every hop channel: at most 103 s
    with 50 channels at −10 dB-Hz. A shorter hop costs the receiver only its edges (0.02 dB at
    40 s) and does not change its computation, since it is coherent inside a tone. Hops longer
    than about 40 s are worth having only with held tones. The computational ceiling moves with
    the tone length, not the hop length; the decode per hypothesis is the same in every case.
18. **The one-cell tone costs 12–13 dB of time on steady and slow paths with per-tone reception,
    and coherent adding across tones is what removes it.** Against a 4 s tone at −10 dB-Hz it
    stands at 14.2 against 2.2 dB at 0.1 Hz, 5.7 against 0.6 dB at 1 Hz and 1.2 against 0.2 dB at
    10 Hz; FHSS on or off changes that only by the hop edges. Where the positions in use share
    one path phase (a narrow band), the receiver can add tones coherently and the tone length
    stops mattering: across the block without FHSS, inside one hop with it. Oscillator drift
    limits the window (2.5 mHz over 400 s), not the method, and the ladder falls back to shorter
    windows by itself. The within-hop case is derived, not computed.
19. **The transmitter is formatted for a diffuse path and the receiver is required to try a
    specular one.** Coherent attempts across tones are now a receiver requirement. On a specular
    path they are valid over the full 2.2 kHz, at the price of a timing search of 57 µs steps
    (about 350 per per-tone step; accumulation and decode repeat, the transform does not). Not
    measured. Decided: they always run over the whole configured band. 40 s is the recommended alternate hop length for one-cell tones: it costs 0.02 dB,
    keeps the hopping benefit for 50 channels, and gives coherent attempts a 40 s window; many
    channels at −10 dB-Hz want shorter hops and steady paths below −10 dB-Hz want longer windows.
20. **Ten bits take 1.1 to 1.3 times as long as one bit under the specification's sizing.** The
    expectation of "less than twice" holds comfortably with the presence test (24 bits: 1.25 to
    1.7 times); on message error alone it needs a rate stricter than about 10⁻³. Short blocks
    use the keyed random code, not a convolutional one, and nothing better is known for 10 to 24
    bits; a better code is needed at 30 to 300 bits and the candidates are not compared. The
    interleaver does not spread a short block across hops, which is a gap (closed by finding 21).
21. **A block spans at least 32 hops so that losing half of them acts only as a worse noise
    floor.** Half the hops cost 3 dB of time, about 1.5 dB of power for weak tones. At a failure
    probability of 10⁻³ a block needs 33 hops at 6 dB of margin, 18 at 10 dB and 14 at 13 dB or
    more; 3 dB of margin needs about 500. The receiver weights each hop by the inverse of its
    own noise level and erases a hop beyond range. The shortest block becomes 32 hops long.
    Settled: 32 hops stands. One or two hops lost in 32 cost 0.14–0.28 dB; 3 dB of margin
    covers a quarter lost and 6 dB half.
    An observer then needs 12–14 dB more C/N₀ than the link at a 40 s dwell and 17–19 dB at
    400 s, against 1.6–5.7 dB for a plain tone, so the long dwell is best for sensitivity and
    concealment together. Four phases are needed; squaring undoes two. On few positions without
    FHSS a plain tone leaves a steady line 6–24 dB below the signal, which the phase key removes.
    Phase changes unknown to the receiver would cost 3–29 times the block time.

22. **At least one keyed phase change per tone is mandatory in all cases** (user decision,
    27 November 2026). Without it every tone starts its cell at the same phase, and an observer
    on a phase-stable path can add the resulting cell-rate pulse coherently with no knowledge of
    the positions: 11.5 dB over energy detection in 18 hours (calculation 17), 5 dB more per
    tenfold of time. The observer may be on ground wave or line of sight while the intended receiver is
    diffuse. One phase per one-cell tone costs the receiver nothing; 2 or 4 per cell cost 2 or
    4 times the transforms where timing is uncertain. Computed and simulated in calculation 17. My
    earlier suggestion of a narrow band with coherent attempts as a general remedy was wrong:
    it only suits paths with very little spread. See
    [the lattice design note](fsk-lattice-design.md#a-keyed-phase-on-every-tone-is-mandatory-27-november-2026).

23. **Below 9 kHz the search volume is the computational load, and phase steps are no cheaper
    than tone changes.** An hour of coherence means bins 0.14 mHz apart: 12,960 for ±0.9 Hz
    and 72,000 for ±5 Hz, times 84 drift-rate bins at 5 ppm/h and the timing offsets. That
    exceeds the measured decode rate for free-running references and long windows; a
    disciplined reference with logged corrections needs one hypothesis. Clock corrections slewed or stepped without the receiver's knowledge add
    to it (9 mHz per ppm of slew at 9 kHz; 32° per 10 µs step). A phase change costs the same
    timing and drift search and the same bandwidth as an FSK change at the same rate; it is the
    better choice only on a wide band over a diffuse path of low Doppler spread. Tone rates
    above 6.25 Hz are not offered. Sized in calculation 18 for assumed reference classes, after the user's corrections of
    27 November;
    my first assessment ("the ceiling is unlikely to bind") was wrong.

## State of the specification

The specification was rewritten as one consistent document on 27 November 2026, after the
findings above had been added to it piecemeal. Its sections were renumbered (terms, lattice,
settings, alphabet, message format, keys, receiver, margin, sizing, concealment, other paths,
computation, decisions, weaknesses), and its decisions are now split into settled and open.
Some supporting figures were left out of it for brevity and remain only in
[the lattice design note](fsk-lattice-design.md): the cost of an unsearched ±1 Hz Doppler shift,
the cost of 10 to 1,000 possible centres per held tone, the block time of a freely chosen held
tone, and the rate carried by strong signals on very wide spreads.

Later the same day the phase key changed from an optional setting to a mandatory keyed phase on
every tone (finding 22), with more frequent steps left as options.

It also gained the sub-9 kHz search volume, a proposed clock-correction rule, and the reason
tone rates above 6.25 Hz are not offered (sections 4.2, 12, 14 and 15). The estimates behind
them are by hand and are in the same subsection of the lattice design note.

The stated tone-rate preference and its two exceptions are in section 4.2, and the equivalence
of phase steps and tone changes in section 4.4.

## Corrections to earlier Matrix Modem documents

Found by the reviews of this pass. The first four are annotated where they occur.

| Document | Earlier statement | Correction |
| --- | --- | --- |
| [Sol-6.2 format note §5a](../Sol-6.2/fsk-format-for-fft-and-tensor-receivers.md) | Union-bound `E/N₀` of 15.2, 27.7 and 152 for 1, 10 and 100 bits; relative airtime 1 : 1.8 : 10 | Exact values 12.43, 24.93 and 110.94; relative airtime 1 : 2.0 : 8.9. Optimum branch SNR is 3.7 at 10 bits and 6.5 at 100 bits, not "about 3" ([coding review §8](reviews/review-coding-interleaving.md)) |
| Same note, §2 and §5 | "Never one large DFT matrix"; a Tensor Core DFT pays only when fused; the 4-bit DFT error floor "limits coherent gain" | Measured: one unfused FP16 product is within 1.3× of cuFFT for all bins and faster for the tone grid. The 4-bit floor is harmonic leakage (worst spur −25 to −29 dBc), with a correlation loss of only 0.01–0.04 dB ([computation note](computation-and-simulation-speed.md), [DFT/FFT review §6](reviews/review-dft-fft-computation.md)) |
| [Sol-6.2 practice review](../Sol-6.2/mfsk-alternatives-practice-and-computation.md), Q65 threshold table | Columns "60D, maximum AP" and "120E, no AP" | The two columns are swapped. Q65-120E without AP loses 6.3 dB from 0 to 100 Hz, not 3.6 dB; the remark that 120E "lost less" is withdrawn ([FT8/Q65 review §7](reviews/review-ft8-q65-design.md)) |
| [Sol-6.2 waveform and repetition review](../Sol-6.2/waveform-and-repetition-review.md) | DOI `10.1109/TWC.2007.06040154` given for Cheng, Valenti and Torrieri | That DOI is Guillén i Fàbregas and Grant; Cheng, Valenti and Torrieri is `10.1109/TWC.2007.06020004` (both verified through Crossref by the review) |
| Opus-5.5 anchors and weak-signal reviews | "FT8 loses 12.2 dB at 10 Hz" | The 50% point moves by 12.2 dB but the curve saturates near 0.66; read it as a failure, not a penalty |
| [Opus-5.5 decoding review](../Opus-5.5/reviews/review-gemm-decoding.md) | WSPR at −28 dB | The WSJT-X threshold is −31 dB; in data energy WSPR's K = 32 code equals FST4W's LDPC code |
| Sol-6.2 practice review, STANAG 4415 | −7 dB, 40 Hz, approaching 12 ms | The documented test points are 10 ms and 0.5–50 Hz at −1 to 0 dB; no source contains 16 ms |
| Earlier guidance on impulsive noise | "Clip or blank before the transform" | Insufficient for OTHR: 7–35% duty with long filter tails; a 3σ clipper loses 8 dB at 28% duty ([OTHR review](reviews/review-othr-interference.md)) |

## Open questions carried forward

1. An end-to-end block-error simulation of the proposed code, placement, 4-bit evidence, presence
   test and hypothesis search, in the cell domain and at sample level.
2. A per-block search for the path's Doppler shift with the long dwells; the hop-to-hop frequency
   repeatability and in-dwell stability of real shift oscillators; whether blocks of 8 or 16
   symbols should be offered for long dwells on steady paths.
3. Dechirp-domain excision for strong incoherent swept interference; several radars at once;
   recordings of real radars.
4. A long-block code: tail-biting list Viterbi against a nonbinary code at 100 and 300 bits.
5. The integer and FP8/FP4 Tensor paths with hand-written kernels and fused epilogues, and the same
   stages on the other target devices.
6. Acquisition over real clock and oscillator uncertainty, and the false-presence rate under
   interference.
7. Coherent attempts without FHSS: the window ladder under real phase wander and at the day–night
   transitions, a drift search for undisciplined references, conditioning against sferics and
   mains harmonics, per-dwell reception with few positions, and the attempts running in a
   receiver.
8. Observer detection on fading paths, from the periodic dwell envelope and by a noise-uncertain
   energy detector; which observer the LPI goal is set against.
9. The phase key: the observer's best statistic and the cell-rate feature, phase steps through
   transmitter filters and narrow antennas, the timing loss at sample level, and its bearing on
   emission classification.

## Cost and provenance of the GPU run

Three on-demand RTX PRO 6000 Blackwell pods, 0.135 h, 0.061 h and 0.090 h, about 0.77 USD
together, all verified removed, no persistent volume. The calculations of 27 November used no
remote compute. The text of 47 CFR 15.247 was read once through a delegated web fetch (Cornell LII
and the GovInfo 2023 annual edition). Device, software, hashes and method are in the
[computation note](computation-and-simulation-speed.md#the-run). All other calculations ran on the
local 12-thread Ryzen 5 PRO 5650U.
