# Matrix Modem FSK research pass (Fable-5.2)

26–28 November 2026 · Research, calculations, GPU runs and eight literature reviews · last change: a stated form for every hypothesis dimension

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
| [Computation and simulation speed](computation-and-simulation-speed.md) | Can a DFT beat an FFT, where do Tensor Cores help, how fast is simulation? | One run on a full RTX PRO 6000 Blackwell ([`fsk-lattice/results/gpu/`](../../../_appraisal/algorithms-matrixModem/fsk-lattice/README.md#lattice_gpucu)) and the DFT/FFT review |
| [Meteor burst](meteor-burst.md) | Is meteor burst a usable path at 2–100 mW, and which modulation suits it? | ITU-R P.843 and P.372 formulas, one measured 500 W link, a delegated literature search with verified DOIs, and a link calculation |
| [Sub-9 kHz FSK rate and spacing](sub9khz-fsk-rate-and-spacing.md) | Which tone rate and spacing for a coherent sub-9 kHz link through a 61 m antenna at 10 kW, and what does the rate do to the computational ceiling? | Antenna calculation, a block Monte Carlo with phase and frequency wander, CPU and GPU decode timings ([`sub9-fsk-rate/`](../../../_appraisal/algorithms-matrixModem/sub9-fsk-rate/README.md)), and four delegated literature searches |

Literature reviews written by delegated agents, kept in full with their own source lists,
verification marks and not-accessed lists:

- [OTHR and swept-FM interference](reviews/review-othr-interference.md)
- [FT8 and Q65 design evidence](reviews/review-ft8-q65-design.md)
- [Direct DFT versus FFT](reviews/review-dft-fft-computation.md)
- [Coding, look combining, interleaving and termination](reviews/review-coding-interleaving.md)
- [Coherent FSK tone rate, spacing and positions](reviews/review-coherent-fsk-rate-spacing.md)
- [VLF antenna bandwidth, keying and voltage limits](reviews/review-vlf-antenna-bandwidth-keying.md),
  with three sub-searches (station bandwidths and voltages; FSK transients and tuning drift;
  direct antenna modulation)
- [Phase stability, Doppler and delay below 9 kHz](reviews/review-sub9khz-phase-stability.md),
  with four sub-searches (classical and modern phase statistics; mode theory; transients)
- [Long-coherence weak-signal search](reviews/review-long-coherence-search.md)

The last four were returned on 27 November for the sub-9 kHz note and stored on 28 November as
returned, each under a header that says what was changed and how far it was checked; all 340 of
their DOIs resolve. Their scratch calculations are in
[`sub9-fsk-rate/literature-calcs/`](../../../_appraisal/algorithms-matrixModem/sub9-fsk-rate/literature-calcs/README.md).

A cross-check of the repository's own earlier research, not a literature review:

- [Earlier sub-9 kHz notes: inconsistencies and gaps](reviews/review-sub9khz-earlier-notes-cross-check.md):
  what three re-readings of the earlier antenna, propagation and noise notes found inconsistent,
  wrongly assumed or missing. Nothing in those notes was changed.

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

24. **Amplitude shifts would pay only far above this modem's range.** They need a bandwidth-
    limited link: roughly 2 bit/s/Hz and a C/N₀ of 46 to 48 dB-Hz in 2.2 kHz, against +10 to
    −20 dB-Hz here. A tone rate near the bandwidth turns the signal into phase keying but is not
    by itself the condition. Below that point a peak-limited transmitter loses average power by
    varying amplitude. Textbook reasoning after the user's question of 27 November; not
    computed.
25. **The keyed phase keeps coherent reception to the keyed receiver.** On a steady path the
    gap to an energy observer grows with the coherent time: about 13 dB for 4 positions and a
    one-hour coherent block, 5 dB more per tenfold. Hand estimate.

26. **Why FSK and not PSK differs by path.** On HF the reason is diffuse delay spread: it smears
    every phase step or tone change over its own length, so rates comparable to it become noise,
    and PSK at 2,000 baud would need an equalizer that weak signals cannot train. The mandatory
    one phase per tone adds no loss; 2 and 4 phases per cell lose up to 0.4 and 0.8 dB at 7 ms.
    On meteor burst there is little delay spread, and the reason is the low symbol energy and
    the unknown phase and frequency of each burst, which is received like a hop. Meteor burst is
    not researched; it is an open decision. Hand estimates and general knowledge, from the
    user's questions of 27 November.

27. **Meteor burst is not a usable path at 2–100 mW with small antennas.** The strongest
    underdense trail gives −6 dB-Hz at 100 mW and −23 dB-Hz at 2 mW between isotropic antennas
    (50 MHz, 1,300 km). With 17 dBi at each end, 100 mW gets 23 to 73 one-tone bursts an hour.
    My earlier estimate of +10 to +30 dB-Hz at these powers was wrong; it needs 10 to 17 dBi at
    each end. The channel in a burst is clean (delay spread under 100 ns, Doppler shift about
    20 Hz at 40 MHz) and brief (median under 150 ms). See [the meteor-burst note](meteor-burst.md).

28. **A wideband keyed-phase mode for meteor burst is physically supported but does not add
    sensitivity** (user's proposal of 27 November, assessed, not computed). Underdense trails
    carry phase steps to about a megahertz; a probe sharing the link's geometry and a digitally
    formed receive array give concealment, gating and gain without loss of sky coverage. Energy
    per burst remains the limit: 100 mW needs 21 to 31 dBi at the receiver. The strong,
    long-lasting trails a probe would select show multipath far more often (71% against 12%).
    See [the meteor-burst note](meteor-burst.md).

29. **Overdense trails are the working case for low-power meteor burst, and they suit the FSK
    design** (user's direction of 27 November, then researched and computed). They are under 5%
    of trails and about 30% of throughput on classic links. In 2.2 kHz they present Doppler
    fading of 1–10 Hz and microseconds of delay spread. At 100 mW and 50 MHz with no antenna
    gain a strong one carries about one tone's worth of energy every two to six hours; with
    10 dBi at each end every overdense trail carries a block's worth, about 3 an hour. My first
    hand figures were about 6 dB too high. Real echo durations are the main uncertainty.

30. **The whole shared sky at a few hundred kilometres is the favourable meteor geometry, and
    troposcatter competes there** (user's proposal of 27 November, computed and checked). From
    50 to 500 km a 100 mW link with no antenna gain gets about two block-worth trails an hour;
    a 20 dBi all-beam receive array raises that to about 50, and makes 2 mW workable at 3 to 4.
    At 1,300 km the unaided figure is almost nothing. A radar at the transmitter sees about
    half the link's trails at 50 km and a tenth at 300 km. Up to about 300 km, continuous
    troposcatter (−2 to +11 dB-Hz at 300 km, median, for 100 mW) is the better path and is
    served by the specification as it stands; beyond about 400 km meteor trails win. No
    troposcatter method is validated at 50 MHz. My hand estimates were too low for the unaided
    meteor link and about right for troposcatter.

31. **Below 9 kHz the appropriate spacing is 0.78125 Hz with sixteen positions in 12.5 Hz, and
    the appropriate tone is held for about 10 s** (28 November, for a 61 m antenna fed with
    10 kW). The span fits an antenna of 64 Hz or more for 0.1 dB and a 24 Hz one for 0.7 dB, and
    sits between the wandering 50 Hz and 60 Hz mains harmonics. A 10.24 s tone equals the best
    coherent window under the worst sub-9 kHz spread bound. See
    [the sub-9 kHz note](sub9khz-fsk-rate-and-spacing.md).
32. **At 10 kW a 61 m antenna is voltage-limited, and the power buys bandwidth.** The bandwidth
    that uses the power is `P / (2π·C·V²)`: 64 to 176 Hz at 100 kV, more at lower voltage. It is
    the quotient of two classical handbook formulas. Radiated power is fixed by the voltage, 5.5
    to about 325 mW at 8.27 kHz. The antenna limits the span, not the tone rate. The lower Q
    also divides the phase wander from wind and sag.
33. **Sensitivity does not depend on the tone rate when the receiver uses the right coherent
    window; the floor and the work do.** The formats with eight or more positions, with tones
    from 0.16 s to 41 s, needed the same block energy within the simulation's scatter on every
    channel up to the worst spread bound. Per-tone reception alone needs 27.4, 24.5, 21.7 and
    19.2 dB for 0.64, 2.56, 10.24 and 41 s tones against 12.9 dB coherent. The best window ran
    from 10 s (±50 mHz of wander, +9.6 dB) to the whole block.
34. **The specification's default ladder depth leaves a gap for short tones.** With windows of
    1/64 of an 87-minute block and per-tone reception only, tones of 0.16 to 1.28 s lost 3.6 to
    4.0 dB under the worst spread bound. A ladder from two tones up closes it at one lookup per
    message and tone.
35. **One decode follows the tone rate; the search must not be a decode per hypothesis.**
    Measured: a 24-bit decode on the full ladder takes 166 s with 32,768 tones and 2.5 s with
    512 on the 12-thread laptop CPU, 4.4 s and 0.07 s on an RTX PRO 6000 Blackwell. The 64-window
    ladder runs 36.9 times a second on that GPU as a direct kernel (65 was the estimate) and 5.9
    as an FP16 matrix product. With logged GPS references every device keeps up at any rate.
    Coherent sums across positions need the timing to 1/(8 × span), 10 ms at 12.5 Hz, which the
    earlier search count had left out. The margins first reported for a ±1 ppm reference (laptop
    430 and seven times too slow) assumed a full decode per hypothesis and are superseded by
    finding 40.
36. **A keyed phase jump into a high-Q antenna is the transient VLF practice forbids, and an
    80 ms ramp removes it.** MIL-STD-188-140A excludes PSK at VLF for it. At 10 kW and 86 Hz a
    half turn sends nearly the whole 10 kW back into the source for 2.6 ms (11 J of the 18.5 J
    stored; 9.2 ms and 41 J at 24 Hz). With the keyed phase reached by an 80 ms ramp nothing
    flows back, the drive and the current stay within 16° at 86 Hz and 49° at 24 Hz, and a
    receiver whose reference follows the ramp loses nothing (0.03 dB with 10.24 s tones if it
    assumes a step). Calculated for a single-tuned antenna; no transmitter was examined.
37. **Spacing at the reciprocal of a long tone has no fallback.** Sixteen positions 0.098 Hz
    apart decode as well as the others up to the worst spread bound, and not at all at ten
    times it, where 0.78 Hz spacing loses 5 dB and decodes.

38. **The quiet sub-9 kHz path is a steady carrier with 5 to 20° of slow phase wander, and the
    Sub9 presets overstate its spread by 50 times and more.** Direct amateur measurements at
    8.8 kHz over 6,194 km show about 15° rms through a night, nightly mean phases repeat within
    1.5 to 7°, and one constant phase has served decodes of 9.7 and 12.2 h. The largest rates
    are brief: 0.03 to 0.3 mHz at the terminator, 0.35 to 2.3 mHz in flares and one great storm
    (scaled from 13 to 40 kHz). Night echoes of 0.2 to 1.9 ms on short and medium paths are a
    further reason for a span near 12.5 Hz. No phase spectrum for seconds to minutes exists at
    any VLF frequency in the sources found.
39. **A 16.7 Hz railway supply puts a harmonic inside the sub-9 kHz slot.** Its harmonics fall
    near 8,250.0, 8,266.7 and 8,283.3 Hz, 16.7 Hz apart, so where such a railway is in range no
    12.5 Hz span is clear of them if they wander as the mains harmonics do. The earlier antenna
    note already had the frequencies; the first version of the sub-9 kHz note missed them, and a
    re-reading of the earlier notes found it. No line amplitude near 8 kHz is in any source.
40. **The frequency and drift search is a transform across a message's tone sums, and as a
    matrix product it is the Tensor Core workload of the coherent attempts.** Gather the tone
    values once per message and timing offset; every frequency bin is then a transform output
    and every drift a de-chirp, and each rung of the ladder is searched only on the bins its
    length resolves. Measured for 24 bits, 512 tones and 13,920 hypotheses on the whole block
    (±1 ppm, ±0.1 s), against a block of 5,243 s: 35,300 s as a decode per hypothesis and
    1,624 s by FFTW on the 12-thread laptop CPU; 980 s per hypothesis, 83 s by cuFFT and 11.4 s
    as one FP16 matrix product on the GPU (404 nominal TFLOP/s, 60% Tensor Core activity over
    the whole pipeline; 8-bit integer operands 18.9 s). The laptop keeps up by 3.2 and the GPU
    by 461. Scaled, not measured: a free-running crystal with 24 bits is about twice too slow on
    one GPU and 80 times on the laptop, and within reach of both with 16 bits. A matrix product
    for the search over messages is one-hot and stays slow; the earlier statement that Tensor
    Cores do not help the coherent attempts tested only that.
41. **Below 9 kHz concealment is relaxed by the user's direction of 28 November.** The band is
    regarded as uncontended, so concealment is kept only where it costs no computation (the
    keyed phase per tone), and not hitting the computational ceiling has priority. The
    sub-9 kHz case is meant as the extreme one that makes the specification call for the right
    balance between CPU FFT, Tensor Core DFT and the hypothesis search that follows.
42. **Three computational rules are now requirements of the specification, for the receiver and
    the simulator** (the user's requirements of 28 November; section 13.1). (1) Every route is
    timed on the machine it runs on, CPU FFT against GPU FFT against a Tensor Core
    matrix-product DFT and the like, and work is rebalanced so that the whole job finishes
    soonest. (2) No work is done twice: work sits at the outermost loop level at which its
    inputs change, the messages are enumerated once, and hypotheses are an inner dimension, as
    a transform or as vector table entries; a table states what is done once per what, and
    tests have to count the work. (3) Simulation speed is a requirement on a 256-CPU machine
    and on a 32-CPU machine with an RTX PRO 6000, with trials as portions under the same
    scheduling. An audit found the earlier text short on all three: rebalancing was required
    for the live receiver only, the no-repetition rule covered frequency and drift only while
    the text itself described repeated decodes for the spread bank and the wide-band fine
    timing steps, and simulation speed was not required at all.
43. **Every hypothesis dimension now has a stated form, and none is a loop of decodes.**
    Frequency and drift: a transform across tone sums (finding 40). Hypotheses with their own
    evidence (spread bank, timing offsets of per-tone reception, wide-band fine timing steps):
    one pass over the messages with vector table entries, measured on the laptop at 1.6, 6.0
    and 6.7 times faster than the best pass per hypothesis for 7, 56 and 350 hypotheses (1.4,
    3.9 and 4.5 with complex entries). Timing offsets of coherent attempts: one operand per
    offset with the positions looked up once; a transform across positions as well is estimated
    to gain about two on a CPU at 200 offsets and to lose on the matrix-product route, and is
    not specified. The 32 additions per message and hypothesis remain where evidence differs.

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

The specification was reconciled a second time on 27 November after these additions. What
changed is listed at the end of the mandatory-phase subsection of the lattice design note.

Section 1 now states constant envelope without amplitude shifts as a fixed decision, with the
reasoning in section 4.2.

Section 4.4 also carries the delay-spread loss of phase steps, and section 12 an unresearched
entry for meteor burst with open decision 16.

The meteor-burst entry of section 12 was then replaced by the researched result (finding 27),
which lists the path as not supported.

Section 12 also summarises the proposed wideband keyed-phase mode for meteor burst as outside
this specification (finding 28).

Section 12 now also gives the overdense-trail result (finding 29), and open decision 16 is
rephrased around it.

Section 12 also carries the whole-sky meteor result and the 50 MHz troposcatter comparison
(finding 30), with open decision 17.

The specification was reconciled a third time on 27 November after the meteor-burst and
troposcatter work. Section 12 is now split into supported paths (12.1), phase-stable paths below
9 kHz (12.2) and meteor burst with 50 MHz troposcatter (12.3); the long-path and whole-sky
meteor figures no longer stand side by side as if they were the same case; cross-references and
the limits paragraph were brought into line; and a settled row records that meteor burst, if
pursued, is an FSK path class on overdense trails.

A fourth pass the same day read the whole specification again. It removed a duplicated
statement in section 1, widened the definition of "held tone" to cover tones without FHSS,
replaced the last uses of "segment" by "phase interval", renamed 12.1 to "Paths the format
serves" since it includes troposcatter at 50 MHz, restated why a keyed tone and not PSK
separately for underdense trails, overdense trails and HF, noted in section 3 that meteor burst
is the one path that would want a shorter cell, and split the meteor-burst weakness into its
four parts.

On 28 November the sub-9 kHz rate and spacing work was added (findings 31 to 38). Section 12.2
was rewritten around a recommended configuration, the antenna at 10 kW, the Monte Carlo and the
measured decode rates. Sections 3, 4.2, 8 (item 8), 9, 13, 14 (open decisions 9, 12 and three new ones)
and 15 were brought into line. Three statements left the specification and remain in the lattice
design note and calculation 18: the antenna roll-off with 4 to 16 positions on the default
lattice, the comparison of the search volume with 8,400 and 65 decodes a second, and the 422,000
hypotheses of a 5.8 h window with unseen slewing.

Later on 28 November the sub-9 kHz configuration was adopted in the specification at the user's
request, where it had been a recommendation with open decisions.

- Settled now: FHSS off, lattice scale `j` = 3, sixteen positions in 12.5 Hz, tones of 8 cells
  (10.24 s); the keyed phase reached by an 80 ms ramp; the window ladder from two tones up;
  GPS-locked references assumed.
- Changed for it: sections 1 to 3 (scope, terms, lattice table), 4.1, 4.2 and 4.4 (settings,
  tone length, phase ramp), 5 and 6 (sixteen positions, shortest block), 8 (timing of
  1/(8 × span), ladder), 9 to 11 (railway harmonics, sizing, concealment), 12.2, 14 and 15.
- Open decisions were renumbered: 18 is now the value-dependent phase, which stays open because
  it would put information into the phase; 19 is the carrier and slot where railway or unsteady
  mains harmonics are in range; 20 is the ramp length against a real transmitter.
- The phase ramp was calculated for this
  ([`phase_slew`](../../../_appraisal/algorithms-matrixModem/sub9-fsk-rate/results/phase_slew.md)),
  where the first version of the note had a hand estimate.
- Finding 39 and the present form of finding 36 date from this step.

After that the user asked whether the timing search had failed to use Tensor Cores and whether
anything received in real time, and set the sub-9 kHz priorities of finding 41. The search was
rebuilt as a transform across tone sums and measured on the laptop and on a third pod (finding
40). In the specification: section 1 (ceiling before concealment below 9 kHz), 4.1, 4.2 and 4.4
(no phase steps inside tones there), 8 item 7 (hypotheses are not an outer loop; the transform
across tone sums as a routed stage), 11, 12.2 (measured search table and scaled classes), 13
(the product as the Tensor Core workload; routes the scheduler has to hold), 14 (two settled
rows; open decisions 2, 12 and 13 narrowed) and 15.

The user then asked whether the specification calls for measuring and rebalancing routes, for
simulation speed on the experiment machines, and for not repeating work (finding 42). Section
13 was divided into 13.1 (the three rules, the table of what is done once per what, and the
repetitions the draft still contains), 13.2 (measured), 13.3 (what the settings do), 13.4
(routes and scheduling rule, now for the simulator as well) and 13.5 (simulation: machines,
layout of the work, reporting, what a trial costs, what is not measured). Section 1 states the
rules as a fixed decision; section 8 has three new properties that tests must show (work
counts, scheduling within a stated factor, simulation reporting). Three passages had described
exactly the repetition the rule forbids and were rewritten: section 13 had opened with "the
receiver's work is the hypothesis count times the decode", item 7 of section 8 had called for
one decode per timing and spread hypothesis, and item 8 had said that each of about 350
wide-band fine timing steps "repeats the accumulation and the decode". Further, section 14 has one settled row
and open decisions 21 and 22; section 15 lists the remaining repetitions and the unmeasured
simulation speed. A statement of the previous step was corrected: the wide-band fine timing
steps cannot be had from a transform across positions at a saving, because 352 positions meet
350 steps.

On the user's request to update the specification again, the list of "repetitions this draft
still contains" in 13.1 was replaced by a table of the form each hypothesis dimension takes and
what that form rests on (finding 43), the one pass with vector entries was built and measured
([`hypothesis_pass_bench`](../../../_appraisal/algorithms-matrixModem/sub9-fsk-rate/results/hypothesis_pass_bench.md)),
and 13.2, item 7 and item 8 of section 8, open decisions 12 and 22 and the weaknesses were
brought into line.

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
| [Lattice design note](fsk-lattice-design.md), "Without FHSS" | "The lattice scale is worth at most 1.6 dB, and only for an antenna narrower than about 50 Hz" | Holds for antenna loss alone at 4 to 16 positions. The scale is also what lets sixteen positions fit the mains slot and a 10 s tone keep a wide spacing ([sub-9 kHz note](sub9khz-fsk-rate-and-spacing.md)) |
| [Sub-9 kHz note](sub9khz-fsk-rate-and-spacing.md), first version, and specification 12.2 of 28 November | With a ±1 ppm reference the laptop is seven times too slow for 24 bits at 10.24 s tones; a 24-bit block with a free-running crystal is out of reach at any rate; Tensor Cores do not help the ladder | Those figures loop a full decode over every hypothesis. As a transform across tone sums the laptop keeps up by 3.2 and the GPU by 461 (measured); a crystal with 24 bits is about twice too slow on one GPU (scaled). The matrix product that was slow was the search over messages; the search over frequency and drift runs at 404 nominal TFLOP/s |
| Specification 13.1 and the sub-9 kHz note, 28 November, before the last revision | Splitting tone values by position "pays whenever the timing offsets outnumber the positions" and would be "12 times cheaper" at 200 offsets | That count left out the transform across positions. Costed from measured transform times the gain is about two on a CPU at 200 offsets, and on the matrix-product route it is a loss |
| Specification section 13 and finding 10 above | "About 65" coherent-ladder decodes a second, by extrapolation from 8,400 | Measured 36.9 for 64 windows and the specification's block shape; the per-tone decode measures 5,900, the 8,400 was for 24 symbols of 64 tones |
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
   receiver. (The ladder is now simulated under constructed wander, and per-tone reception with
   2 to 16 positions; measured wander, the drift search and the conditioning remain.)
8. Observer detection on fading paths, from the periodic dwell envelope and by a noise-uncertain
   energy detector; which observer the LPI goal is set against.
9. The phase key: the observer's best statistic and the cell-rate feature, phase steps through
   transmitter filters and narrow antennas, the timing loss at sample level, and its bearing on
   emission classification.

10. Below 9 kHz: the top-voltage limit of a real 61 m installation; a real transmitter's
    tolerance of the 80 ms phase ramp; mains and railway harmonic amplitudes near 8 kHz at a
    receiving site, and the carrier to use where no slot is clear; the night mode cut-offs near
    5.17 and 8.27 kHz; a measured phase spectrum of a sub-9 kHz carrier; the transform search
    inside a receiver (bin values per timing offset, tilted bin paths, short tones summed into
    sub-blocks, a fused epilogue, batched small transforms on the CPU, the 4-bit and FP8/FP4
    Tensor paths); one pass with vector table entries for the spread bank and for the fine
    timing steps of the wide-band specular search; the split by position for timing
    uncertainty of ±1 s and more; tiered screening; a simulator of the complete format and its
    speed on a 256-CPU machine and on a 32-CPU machine with an RTX PRO 6000.

## Cost and provenance of the GPU run

Three on-demand RTX PRO 6000 Blackwell pods, 0.135 h, 0.061 h and 0.090 h, about 0.77 USD
together, all verified removed, no persistent volume. The calculations of 27 November used no
remote compute. The sub-9 kHz decode timings of 28 November (UTC) used two more pods of the same
type for 0.076 h and 0.095 h, about 0.46 USD together, both verified removed. The second reran
the timings after an independent check found that the first CPU benchmark had left threads idle.
The adoption work of 28 November (phase-ramp calculation, DOI checks, storing the reviews) used
no remote compute. The search benchmark that followed used a third pod of the same type for
0.116 h, about 0.31 USD, verified removed. The text of 47 CFR 15.247 was read once through a delegated web fetch (Cornell LII
and the GovInfo 2023 annual edition). Device, software, hashes and method are in the
[computation note](computation-and-simulation-speed.md#the-run). All other calculations ran on the
local 12-thread Ryzen 5 PRO 5650U.
