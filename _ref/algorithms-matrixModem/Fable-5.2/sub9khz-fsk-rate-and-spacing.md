# Sub-9 kHz FSK rate and spacing for a 61 m antenna at 10 kW

27–28 November 2026 · Calculations, a block Monte Carlo, CPU and GPU decode measurements, and four
delegated literature searches · The configuration was adopted in the specification on 28 November

This note answers one question for the
[FSK specification draft](../../../_appraisal/matrixModem-FSK.md): which FSK tone rate and tone
spacing suit a link below 9 kHz on which coherent reception across tones is necessary, some spread
is expected, and the total bandwidth is set by a practical 200 ft (61 m) vertical antenna fed with
10 kW, and what that rate does to the receiver's computational ceiling. The programs and outputs
are in
[`sub9-fsk-rate/`](../../../_appraisal/algorithms-matrixModem/sub9-fsk-rate/README.md). The
earlier sub-9 kHz research on
[propagation](../../highFidelity-triple_Robust_Modem/Fable-5.2/10-sub9khz-propagation.md),
[antennas and practice](../../highFidelity-triple_Robust_Modem/Fable-5.2/11-sub9khz-antennas-links-and-practice.md)
and [noise](../../highFidelity-triple_Robust_Modem/Fable-5.2/12-sub9khz-noise.md) supplies the
physical inputs and is not repeated.

Evidence labels: [calculated] closed form or exact recursion; [simulated] Monte Carlo of a model;
[measured] a timing on real hardware; [derived] reasoning from the above, not computed; [earlier
research] taken from the notes just named, with their own evidence grades; [literature] from a
delegated search, with the access level of the source in the [source list](#sources) and the
search's full report in [`reviews/`](#the-delegated-reviews). No antenna, path or receiver was
measured. Nothing here is a qualified receiver.

## Answer in brief

| Quantity | Recommended for this case | Why |
| --- | --- | --- |
| Tone spacing | 0.78125 Hz (lattice scale `j` = 3, a 1.28 s cell) | Sixteen positions then fit in 12.5 Hz |
| Positions and total span | 16 positions, 12.5 Hz | About 0.1 dB of loss on an antenna of 64 Hz or more, 0.7 dB on a 24 Hz one; fits between the wandering 50 Hz and 60 Hz mains harmonics together; flat across night echoes of 2 ms; 10 ms timing steps; no few-position loss |
| Tone length (FSK rate) | 8 cells = 10.24 s, 0.098 tones per second | Equals the best coherent window under the worst spread bound, so plain per-tone reception is the floor; the coherent attempts cost in proportion to the tone rate |
| Other tone lengths | 1 cell (1.28 s) is the fastest worth offering; 32 cells (40.96 s) for blocks longer than about three hours | Shortest block of 82 s against 11 and 44 minutes; costs below |
| Keyed phase | One per tone, as the specification requires, but reached by an 80 ms ramp (160 ms for an antenna under about 40 Hz) | A phase step into a high-Q antenna is the transient VLF practice forbids; with the ramp no power flows back into the transmitter and a receiver that follows the ramp loses nothing |
| Receiver | Per-tone reception, the cells inside a tone, and coherent windows from two tones up to the block, every block | The best window ran from 10 s to the whole block across the spread cases, and to one or two cells beyond them |
| References | GPS-locked at both ends, corrections logged or the audio re-timed from the recorded PPS | One hypothesis instead of thousands to billions |

1. **At 10 kW the antenna is voltage-limited, and the power buys bandwidth, not radiated power.**
   The −3 dB bandwidth that uses all 10 kW is `B* = P / (2π·C·V²)`: 64 to 176 Hz at 100 kV for
   0.9 to 2.5 nF, 189 to 525 Hz at 58 kV, and 16 to 44 Hz only at 200 kV. Radiated power is fixed
   by the voltage: 5.5 mW (thin-wire T at 29 kV) to about 0.3 W (heavily top-loaded tower at
   100 kV) at 8.27 kHz.
2. **The antenna does not limit the tone rate; it limits the span.** Even 6.25 tones per second
   pass an 86 Hz antenna for 0.4 dB. A span of a quarter of the antenna bandwidth costs 0.3 dB and
   a span equal to it about 2.4 dB.
3. **Sensitivity does not depend on the tone rate when the receiver uses the right coherent
   window.** The formats with eight or more positions, with tones from 0.16 s to 41 s, needed
   the same block energy within the Monte Carlo's scatter on every channel up to the worst
   spread bound. What the tone length changes is the floor
   without coherence between tones (27.4, 24.5, 21.7 and 19.2 dB for 0.64, 2.56, 10.24 and 41 s
   tones against 12.9 dB coherent) and the receiver's work.
4. **The computational ceiling moves with the tone rate.** A decode on the full window ladder
   costs one lookup per message and tone. On the 12-thread laptop CPU a 24-bit decode of an
   87-minute block takes 166 s with 0.16 s tones and 2.5 s with 10.24 s tones: 32 against 2,067
   decodes in the block's own duration. With GPS-locked references one decode is enough.
   **With a lesser reference the search must not repeat the decode for every hypothesis.** A
   ±1 ppm reference with ±0.1 s of timing needs 13,920 hypotheses on the whole block. Done as
   13,920 decodes, a 24-bit block takes 35,300 s on the laptop and 980 s on the GPU, against a
   block of 5,243 s. Done as a transform across each message's tone sums it takes 1,624 s on
   the laptop (an FFT) and 11 s on the GPU (one FP16 matrix product on Tensor Cores at 404
   nominal TFLOP/s), both measured (section 6.4). The first version of this note reported only
   the first way and concluded that the laptop was seven times too slow and that Tensor Cores
   did not help; both conclusions were wrong.
5. **Spacing equal to the reciprocal of a long tone is the wrong economy here.** A 0.098 Hz
   spacing for 10.24 s tones fits 16 positions in 1.6 Hz and decodes as well as the others up to
   the worst spread bound. It has no shorter window to fall back on: at ten times that bound it
   did not decode at any energy, where the 0.78 Hz spacing still decoded 5 dB down. It also
   tolerates only ±6 ppm of frequency error.
6. **Disadvantages of the recommendation.** The shortest block is 11 minutes; a strong link is
   not served faster. A 10 s tone is a narrow line to an observer with a filter bank, which is
   accepted: below 9 kHz concealment is kept only where it costs no computation (the keyed phase
   per tone), and not hitting the computational ceiling has priority (the user's direction of
   28 November). It is a second cell length beside the default 160 ms. The transmitter has to ramp the keyed phase,
   which is calculated and not tested on a transmitter. Where a 16.7 Hz railway supply is in
   range, one of its harmonics lies inside the 12.5 Hz slot (section 3).

## 1. The case

- Carrier near 8.27 kHz; the calculations also give 2.97 and 5.17 kHz where they differ.
- Transmit antenna: a 61 m vertical with a top hat. The earlier antenna note's modelled
  geometries are used: a T with a 122 m top wire (902 pF, effective height 49.8 m), an umbrella
  (1,850 pF, 38.4 m) and a bare wire (340 pF, 29.8 m) [earlier research, own estimates there]. One
  documented design figure at this scale was found: a 206 ft top-loaded tower of 2.5 nF and 40 m
  effective height in a 1960s scaling table [literature, delegated read].
- Transmitter: 10 kW of real power available. No transmitter is specified.
- Level: with 10 kW the radiated power is 5.5 to about 325 mW (section 2). The earlier link
  budgets per milliwatt then give, for 5,000 km at 8.27 kHz against the ITU-R P.372 minimum
  noise, about −22 to −4 dB-Hz at night and −35 to −17 dB-Hz by day, and 15 dB more at 1,000 km
  at night. With the quietest reported amateur noise it is 22 dB better, with the P.372 maximum
  28 dB worse [derived from earlier research]. Most of that range needs coherent reception
  across tones: the specification's per-tone figures at −20 dB-Hz are days.
- Spread: the Sub9 presets bound the frequency wander at 1, 5 and 50 mHz with echoes of 5 µs,
  50 µs and 5 ms. They are defined in
  [`src/tuning_presets.cpp`](../../../src/tuning_presets.cpp) and described in the earlier
  [method chapter](../../highFidelity-triple_Robust_Modem/Fable-5.2/00-method-and-modem-context.md);
  the earlier appraisal simulator has other sub-9 kHz scenarios, and no stated derivation of the
  preset values was found. The earlier propagation note measured them against the evidence:
  quiet paths
  move at 2 to 22 µHz, terminators at 30 to 120 µHz for about an hour, a median flare onset at
  10.2 kHz at 0.58 mHz for about six minutes, and large flares extrapolate to 0.2 to 7 mHz for
  minutes; measured differential delays are 50 µs to 1.9 ms. The presets are generous stress
  values, and no measured phase spectrum of a carrier below 9 kHz was found [earlier research].
- What a delegated search added [literature]:
  - **The quiet path is a steady carrier with small, slow, bounded phase wander, not a fading
    channel.** Within one day or night state the rms phase is about 5 to 10° by day and 15 to
    20° at night on 5 to 10 Mm at 8 to 10 kHz (Omega statistics: 3 and 5 µs at 10.2 kHz). The
    equivalent rms phase rate is 3 to 20 µHz.
  - Direct amateur measurements below 9 kHz with GPS at both ends agree: hourly phases at
    8.8 kHz over 6,194 km with a standard deviation of 18.6° through a night, about 15° after
    measurement noise is removed; on another night the wander was below the noise. Nightly mean
    phases repeated within 1.5 to 7° on 0.9 to 3.6 Mm at 6.47 and 8.27 kHz. One constant phase
    served decodes of 9.7 h over 6,194 km and 12.2 h over 881 km.
  - The largest phase rates are deterministic and brief: 0.03 to 0.06 mHz for 2 to 5 hours
    while the terminator crosses an east–west path, 0.2 to 0.3 mHz for about an hour on a
    north–south one, 0.35 mHz for the ten-minute rise of an M-class flare on 5 Mm, 1.1 mHz for
    an X-class flare on 10 Mm, 2.3 mHz for minutes in one great storm. The last three are
    scaled from 13 to 40 kHz.
  - The 1 mHz of the best preset is therefore 50 to 300 times the quiet value and about the
    size of the worst events; 5 and 50 mHz are beyond anything found.
  - Echoes are real at night on short and medium paths: hops 0.23, 0.77, 1.3 and 1.9 ms behind
    the ground wave at 172 km, and an apparent group delay swinging by ±1 ms across 60 Hz at
    1,028 km. Long daytime paths are single-mode.
  - Below 10 kHz the phase response to a change of reflection height rises again (about 1.3
    times at 5 kHz and 1.9 times at 3 kHz against 9 kHz at night), so scaling from 20 kHz in
    proportion to frequency stops working. No published spectrum, structure function or Allan
    deviation of phase for seconds to minutes was found at any VLF frequency.

## 2. The antenna at 10 kW

### 2.1 Bandwidth

[Calculated; `antenna_keying`, section 1.]

- The top voltage `V` fixes the current, `I = V·ω·C`, whatever the loss. Radiated power is then
  `(V·ω·C)²·R_r` and does not depend on the transmitter.
- A transmitter with `P` to spend can afford a series resistance `P / I²`. The −3 dB bandwidth of
  the series circuit is then `B* = P / (2π·C·V²)`, independent of frequency. It is the stored
  energy `C·V²` and the definition of Q.
- An antenna narrower than `B*` cannot take the power at that voltage. A wider one radiates less,
  in proportion to `B*/B`.

`B*` for 10 kW:

| Antenna | C | 29 kV | 58 kV | 100 kV | 150 kV | 200 kV |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| T, 122 m top wire | 902 pF | 2,098 Hz | 525 Hz | 176 Hz | 78 Hz | 44 Hz |
| Umbrella | 1,850 pF | 1,023 Hz | 256 Hz | 86 Hz | 38 Hz | 22 Hz |
| Heavily top-loaded 63 m tower | 2,500 pF | 757 Hz | 189 Hz | 64 Hz | 28 Hz | 16 Hz |

Radiated power at 8.27 kHz at the limit: T 5.5 / 22 / 66 mW at 29 / 58 / 100 kV; umbrella 14 / 55 /
164 mW; the 2.5 nF tower about 27 / 109 / 324 mW.

- **The earlier note's low-loss cases cannot take 10 kW.** Its engineered and good cases (total
  resistance 26 to 147 Ω, natural bandwidth 18.5 to 69 Hz) would stand at 111 to 309 kV with
  10 kW; they reach 100 kV on 1.0 to 8.1 kW. Its typical and adverse cases (296 to 1,182 Ω, 174
  to 621 Hz) absorb 10 kW at 37 to 101 kV.
- **The formula is the standard result** [literature]. It is the quotient of the two formulas of
  the US Navy shore-station handbook NAVELEX 0101,113 (1972): the voltage-limited radiated power
  and the "inherent bandwidth–efficiency product". The closed form itself was not found printed.
  The handbook, A. N. Smith (1981) and the DECO study for NBS use the same two regimes,
  voltage-limited and transmitter-limited.
- **Spending the power on a resistor is real practice, and not the only way.** Anthorn at 19 kHz
  runs with "bandwidth resistors" (100 Hz, 44% efficiency). Hartley (1968, abstract) reports more
  than three times the bandwidth-efficiency of resistance loading by returning stored energy to
  the supply. A matched source widens the response by 1.5 to 2 (Belrose; NIST for WWVB), which a
  stiff switching output does not give. Retuning the antenna per tone removes the limit
  altogether (2.4).
- **Which voltage is practical.** Stranded-cable fits from Hansen and Rodriguez (2011) give an
  ideal isolated-wire onset of about 43 kV for 2 mm wire, 71 kV dry and 61 kV wet for 5 mm, and
  106 and 81 kV for 10 mm at 61 m, before end effects of about two. Thin wire loses little when
  wet; large smooth conductors, corona rings included, lose 36 to 50%. In small stations the
  coil top or the bushing gave way first: 11 to 28 kV in amateur reports, about 100 kV at WWVL.
  The Navy's historical limit was 200 to 250 kV on megawatt antennas [literature]. A practical
  61 m station is therefore somewhere between 30 and 100 kV, and `B*` between about 64 Hz and
  2 kHz.
- At 29 kV on thin wire the loaded Q is about 4 and the narrow-band model no longer holds
  [literature remark]; the antenna is then no constraint at all.

**Design value.** The format has to fit the narrow end, about 64 to 86 Hz, and should degrade
gently on a 20 to 25 Hz antenna (a low-loss antenna run below 10 kW, or a 150 to 200 kV build).

### 2.2 What keyed tones lose in the antenna

[Calculated; `antenna_keying`, section 2: exact envelope recursion over 40,000 keyed tones,
constant-amplitude drive, resonance at the centre of the positions.]

Coherent loss in dB with one phase for all positions and a receiver that does not know the
antenna:

| Format | Span | 18.5 Hz | 24 Hz | 57 Hz | 86 Hz | 174 Hz |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 6.25 Hz, 0.16 s tone, 8 positions | 50 Hz | 7.2 | 5.6 | 1.9 | 1.0 | 0.3 |
| 6.25 Hz, 0.16 s tone, 4 positions | 25 Hz | 3.6 | 2.6 | 0.8 | 0.4 | 0.2 |
| 1.5625 Hz, 0.64 s tone, 16 positions | 25 Hz | 3.4 | 2.4 | 0.6 | 0.3 | 0.1 |
| 0.78125 Hz, 1.28 s tone, 16 positions | 12.5 Hz | 1.2 | 0.8 | 0.2 | 0.1 | 0.0 |
| 0.78125 Hz, 10.24 s held tone, 16 positions | 12.5 Hz | 1.1 | 0.7 | 0.1 | 0.1 | 0.0 |
| 0.390625 Hz, 2.56 s tone, 16 positions | 6.25 Hz | 0.4 | 0.2 | 0.1 | 0.0 | 0.0 |

- **The span decides, not the rate.** At equal span the 0.16 s and 10.24 s tones differ by
  0.4 dB or less.
- A receiver that knows the static amplitude and phase of each position recovers about 40% of
  the loss (0.17 against 0.28 dB for 25 Hz in 86 Hz). The static phase at the outer position
  is 14° for 25 Hz in 86 Hz and 7° for 12.5 Hz. It is mostly a delay, which the timing
  hypothesis absorbs.
- Per-tone reception loses less than the coherent sum: 0.17 against 0.28 dB for 25 Hz in 86 Hz.
- The block Monte Carlo with the static response put into the signal agrees. A 25 Hz span costs
  0.2 dB of block energy in an 86 Hz antenna and 2.2 dB in a 24 Hz one; 12.5 Hz costs 0.1 and
  0.7 dB; 6.25 Hz costs 0.1 and 0.3 dB [simulated, steady path, 2,000 blocks].
- Energy left in other positions is 22 dB or more below the tone at 86 Hz, and 30 dB or more
  for tones of 0.64 s and longer.
- **The transient after a keyed phase step costs `2/(π·B·T)` of the power** for a tone of length
  `T`: 0.1 dB at `B·T` = 28, 0.25 dB at 11, 1 dB at 3. With the keyed phase the default lattice's
  0.16 s tone loses 0.19 dB more at 86 Hz and 0.55 dB more at 24 Hz than with phase-continuous
  tone changes; a 10.24 s tone loses 0.02 dB more. This answers the specification's open question
  on phase steps through narrow antennas for the receiver's side.

### 2.3 Detuning

[Calculated; section 3. Detuning sizes from the earlier antenna note.]

- A resonance shift `d` turns every position by nearly the same phase, `atan(2d/B)`, and costs
  nearly the same power: 7°, 14°, 27° and 45° with 0.07, 0.26, 0.97 and 3.0 dB for `d` of `B`/16,
  `B`/8, `B`/4 and `B`/2. The receiver sees it as path phase wander and fading.
- What differs across the span is small when the span is small: 0.03 dB for 12.5 Hz in 86 Hz and
  0.35 dB in 24 Hz, hardly changing with `d`.
- Capacitance changes of 10⁻⁴ (sideways bow), 10⁻³ (thermal or rain drift) and 4×10⁻³ (sag with a
  shortened down-lead) are 0.4, 4.1 and 16.5 Hz at 8.27 kHz: 0.5°, 5° and 21° at 86 Hz, and 2°,
  19° and 54° at 24 Hz.
- **So the bandwidth that 10 kW buys also buys phase stability.** The literature gives the same
  relation, `Δφ ≈ Q·ΔC/C` (Morgan 1962), and measured gust phase of a few microseconds at 20 kHz
  on WWVL at a Q near 540, which scales to about 14° at Q = 500 and 3° at Q = 96. Stations that
  need better feed the antenna-current phase back to the exciter (WWVL, Omega: under 1° over a
  minute); variometer servos are slower than gusts [literature].

### 2.4 The transmitter and the keyed phase

- **VLF practice forbids phase jumps.** MIL-STD-188-140A says PSK is not used at VLF and LF
  "because of the transients caused by phase discontinuities", which can damage the final
  amplifier or trip the transmitter, and applies the same rule to FSK. The Navy handbook allows a
  mark-to-space transition only at coincident zero crossings, because an antenna Q above 200
  does not tolerate an abrupt change without arcing. A patent on solid-state VLF transmitters
  warns that stored energy returns into the output devices when the frequency is changed
  drastically [literature].
- **The specification's keyed quarter-turn on every tone is such a jump.** At 10 kW and 86 Hz
  the antenna stores 18.5 J; a step of 90° or 180° makes the circuit dissipate two or four times
  that at the old phase within about three time constants (11 ms), a transient of kilowatts
  against a 10 kW transmitter. Averaged, it is 231 W at 6.25 tones per second, 58 W at 1.5625,
  and 3.6 W at 0.098. On a 24 Hz antenna it is 66 J and 829, 207 and 13 W [calculated, section
  4].
- **Remedy: a phase ramp [calculated; `phase_slew`; not tested on hardware].** The keyed phase
  moves evenly to its new value over the first `S` of the tone, the shorter way round. In the
  single-tuned model with a constant-amplitude source:

  | | Plain step | 20 ms | 40 ms | 80 ms | 160 ms |
  | --- | --- | --- | --- | --- | --- |
  | Lowest delivered power, 86 Hz antenna | −9.6 kW | +6.6 kW | +8.5 kW | +9.3 kW | +9.6 kW |
  | Lowest delivered power, 24 Hz antenna | −9.9 kW | −1.3 kW | +2.1 kW | +4.7 kW | +6.4 kW |
  | Largest angle between drive and current, 86 / 57 / 24 Hz | 180° | 37° / 52° / 108° | 23° / 34° / 69° | 16° / 23° / 49° | 12° / 18° / 38° |
  | Largest standing-wave ratio, 86 / 57 / 24 Hz | – | 2.1 / 3.1 / – | 1.5 / 1.9 / 6.2 | 1.3 / 1.5 / 2.8 | 1.2 / 1.4 / 2.1 |
  | Extra loss, 10.24 s tone, receiver assumes a step | 0 | 0.01 dB | 0.02 dB | 0.03 dB | 0.06 dB |
  | Extra loss, 1.28 s tone, receiver assumes a step | 0 | 0.06 dB | 0.12 dB | 0.24 dB | 0.50 dB |

  - After a plain half turn the current opposes the drive: nearly the full 10 kW flows back into
    the source for `ln 2/(π·B)`, 2.6 ms at 86 Hz and 9.2 ms at 24 Hz, returning 11 J and 41 J
    (closed form, `(1 − ln 2)·P/(π·B)`). A quarter turn drops the delivered power to zero and
    returns nothing.
  - From 40 ms up nothing is returned on any antenna of 24 Hz or more. For comparison a steady
    tone on an outer position of the 12.5 Hz span already stands at 8°, 12° and 26°
    (standing-wave ratio 1.15, 1.23 and 1.62) on antennas of 86, 57 and 24 Hz.
  - **A receiver whose reference follows the ramp loses nothing**; it does slightly better than
    with a step, because the antenna's transient is smaller (0.062 against 0.065 dB at 86 Hz).
    Leaving the ramp interval out costs about twice what assuming a step costs.
  - The ramp is a phase-continuous frequency excursion of 3.125 Hz (quarter turn) or 6.25 Hz
    (half turn) lasting 80 ms, which is the kind of shift FSK practice at VLF allows. One
    commercial class-D VLF transmitter's data sheet rates it for full power up to a
    standing-wave ratio of 5 [literature, through a delegated read].
  - Sending a half turn in the direction that moves the frequency towards the centre of the
    positions does as much as doubling the ramp (80 ms inward equals 160 ms in a keyed
    direction). The specification keeps the keyed direction, so that the phase still depends
    only on the key and the time.
  - **Adopted: 80 ms** (256 samples at 3,200 Hz), and 160 ms for an antenna narrower than about
    40 Hz. Tone changes need no such care: they are phase-continuous on the lattice, and the
    positions are a few hertz apart.
- **Faster keying than the antenna passes is possible but not needed.** Synchronous retuning of
  the antenna with the keying is documented from 1951 (460 words per minute through a 10 kW
  28.5 kHz antenna) to Omega, which hopped 10.2 to 13.6 kHz in segments of about a second with a
  relay and tap per frequency. No published example switches an antenna reactance at tens of
  kilovolts for this purpose with modern devices; the high-power implementations are saturable
  reactors of 10 kV and 1 MVA on a tap [literature]. At the rates below, plain keying through the
  fixed-tuned antenna is enough.

## 3. The receiver's band: mains harmonics and sferics

[Earlier research; the grid-frequency statistics are measured, the conversion to harmonics is an
estimate there, and no line amplitude near 8 kHz was measured.]

- Near 8.27 kHz the 50 Hz harmonics are at 8,250 and 8,300 Hz and the 60 Hz harmonics at 8,220,
  8,280 and 8,340 Hz. They wander with the grid: about 3.1 Hz rms on the Continental European
  grid, 2.5 Hz on the US grids, 8 to 10 Hz on the Nordic and British ones.
- Three standard deviations away from both families leaves 8,259 to 8,272.5 Hz, about 13 Hz. From
  one family alone it leaves about 31 Hz (50 Hz grids) or 45 Hz (60 Hz grids). On the unsteady
  grids no slot is safe and the positions near a harmonic are simply noisier for a while.
- A 12.5 Hz span from 8,259.375 Hz (sixteen positions 0.78125 Hz apart) sits in the common slot.
  The amateur convention of 8,270 Hz is inside it. [Derived.]
- Wideband blanking of sferics is worth about 20 dB at 8 kHz and is done before the band is
  narrowed. It does not depend on the tone rate.
- **Railway supply at 16.7 Hz puts a line inside the slot** [earlier research for the
  frequencies; the consequence is derived here]. In Germany, Austria, Switzerland, Sweden and
  Norway the railway network runs at 16.7 Hz, and its harmonics near 8.27 kHz fall at about
  8,250.0, 8,266.7 and 8,283.3 Hz. They are 16.7 Hz apart, so if they wander as the mains
  harmonics do, no span of 12.5 Hz or 6.25 Hz is clear of them at three standard deviations, and
  at one standard deviation about 10 Hz is. The amateur convention of 8,270 Hz is 3.3 Hz from
  the nominal line; the earlier note records only that carriers are placed off these harmonics
  and recommends measuring line positions. No amplitude was found. This was missed in the first
  version of this note and found when the earlier notes were
  [re-read](reviews/review-sub9khz-earlier-notes-cross-check.md).
- **What that leaves.** A line on one of sixteen positions spoils one sixteenth of the alphabet
  while it sits there. The keyed placement spreads that over all symbols, the receiver's
  line excision and per-position normalisation are meant for it, and scale `j` = 4 (6.25 Hz) is
  the smaller target. None of this was simulated.

## 4. Spacing

Constraints, each from the sections above:

| Constraint | Wants | Value |
| --- | --- | --- |
| Antenna of 64 to 86 Hz at 10 kW | Span of about a quarter of it or less | 16 to 21 Hz or less |
| Antenna of 20 to 25 Hz (low-loss or high-voltage build) | Loss under about 1 dB | 12.5 Hz or less |
| Mains harmonics of both families | Span inside the common slot | about 13 Hz |
| No few-position loss in coherent reception | 16 positions (8 cost 0.2 dB, 4 cost 0.8 dB and 2 cost 2.6 dB in the specification's earlier 20,000-block run; section 7 here agrees within its scatter) | spacing = span / 16 |
| Worst spread bound of 50 mHz | Spacing at least eight times the spread (the FST4 rule) | 0.4 Hz or more |
| One path phase across the positions (night echoes to 1.9 ms; worst preset 5 ms) | Span well under the reciprocal of the echo delay | Two equal paths stay correlated to 0.997 across 12.5 Hz at 1.9 ms and 0.98 at 5 ms; across 50 Hz to 0.96 and 0.71 |
| Frequency error without a search | Half a spacing | ±47 ppm at 0.78 Hz |
| Timing for coherent sums across positions | Steps of 1/(8 × span) | 10 ms at 12.5 Hz, 5 ms at 25 Hz, 2.5 ms at 50 Hz |

- **0.78125 Hz with 16 positions in 12.5 Hz meets all of them.** It is the specification's
  lattice at scale `j` = 3: a 1.28 s cell.
- 1.5625 Hz with 16 positions in 25 Hz (`j` = 2) is the alternative where the receiver sits on
  one grid family and the antenna is 86 Hz or wider: 0.2 dB more antenna loss, twice the margin
  against spread and frequency error.
- 0.390625 Hz with 16 positions in 6.25 Hz (`j` = 4) suits an antenna of 20 Hz or less, at the
  edge of the spread rule.
- **The default 6.25 Hz spacing is the poorer fit.** In 25 Hz it has four positions: 0.8 dB of
  few-position loss on top of 0.4 dB in an 86 Hz antenna and 2.6 dB in a 24 Hz one, and it
  straddles a harmonic of one family.
- **Half spacing is not worth having.** Positions half a reciprocal apart are orthogonal only
  for a known phase and a fixed phase relation between tones; with keyed quarter-turns adjacent
  positions correlate by 0.64, and per-tone reception sees a leak of 0.41. Scaling the lattice
  gives the same number of positions in the same span with nothing lost [literature and derived].
- **Spacing equal to the reciprocal of a 10 s tone (0.098 Hz) has no margin.** The worst spread
  bound is half of it and ±6 ppm of frequency error moves a tone to the next position. It decodes
  up to that bound and not far beyond it (section 5).

## 5. Tone rate

### 5.1 Monte Carlo

[Simulated; `coherent_block_mc`. The keyed 10-bit block of the specification, 87.4 minutes in
every format, the same energy, 2,000 blocks per row. The receiver scores all messages on every
coherent window from one cell to the block and adds windows as energies. Entries are the block
`E/N₀` in dB at 1% wrong decodes; 12.87 dB is the exact coherent reference. Known timing and
frequency, Gaussian noise.]

Best window per channel over the formats with eight or more positions and cells of 2.56 s or
less. The spread across formats is the Monte Carlo's scatter (about ±0.2 dB):

| Channel | Block `E/N₀` | Best window | Against steady |
| --- | ---: | --- | ---: |
| Steady | 12.8–13.1 dB | whole block | – |
| Phase wander 30° rms, 1 h correlation (a quiet night) | 13.2–13.6 dB | whole block | +0.4 dB |
| Phase wander 60° rms, 10 min correlation | 16.0–16.4 dB | 11 min | +3.2 dB |
| Frequency wander within ±1 mHz (Sub9 best) | 16.1–16.3 dB | 5.5 min | +3.3 dB |
| Frequency wander within ±5 mHz (Sub9 moderate) | 18.2–18.5 dB | 82 s | +5.5 dB |
| Frequency wander within ±50 mHz (Sub9 worst) | 22.4–22.6 dB | 10 s | +9.6 dB |
| Rayleigh, 1 / 5 / 50 mHz Gaussian spread | 17.8–18.4 / 18.7–19.0 / 22.5–22.7 dB | 5.5 min / 82 s / 10 s | +5.2 / +5.9 / +9.7 dB |
| Rayleigh, 0.5 Hz (outside every record) | 27.1–27.4 dB with cells of 0.64 and 1.28 s; 28.4 dB with 2.56 s | 1.28 s (2.56 s where that is the cell) | +14.4 dB and more |

Formats with four positions sit 0.2 to 0.9 dB higher. The receiver that picks the most
significant window per block needs up to 0.8 dB more than the best window on the steady
channels and up to 0.5 dB more on the others.

Against the measurements of section 1, the "quiet night" channel here (30° rms, one hour of
correlation) is harsher than a measured night (15 to 20° rms with hours of correlation), and the
±1 mHz channel is about a flare or a great storm lasting the whole block. The ±5 and ±50 mHz
channels are the presets' stress values.

What the tone length changes, steady channel and worst wander:

| Format | Per tone, steady | Per tone, ±50 mHz | Best window, ±50 mHz | Default ladder (1/64 of the block and per tone), ±50 mHz |
| --- | ---: | ---: | ---: | ---: |
| 6.25 Hz, 0.16 s tone, 8 positions | 30.4 dB | 30.5 dB | 22.6 dB | 26.6 dB |
| 1.5625 Hz, 0.64 s tone | 27.4 dB | 27.4 dB | 22.6 dB | 26.5 dB |
| 0.78125 Hz, 1.28 s tone | 26.0 dB | 26.0 dB | 22.4 dB | 26.0 dB |
| 1.5625 Hz, 2.56 s held tone | 24.5 dB | 24.7 dB | 22.5 dB | 24.7 dB |
| 0.78125 Hz, 10.24 s held tone | 21.8 dB | 22.6 dB | 22.6 dB | 22.6 dB |
| 1.5625 Hz, 40.96 s held tone | 19.2 dB | 24.8 dB | 22.5 dB (cells inside the tone) | 24.8 dB |

The default lattice with one-cell tones (6.25 Hz spacing, 0.16 s tones, four positions in 25 Hz
and eight in 50 Hz) was run on five channels with 1,200 blocks per row. It follows the same
pattern: 13.6 and 13.1 dB on the steady path, 22.7 and 22.6 dB with 10 s windows under ±50 mHz of
wander, and 27.4 dB with 1.28 s windows at 0.5 Hz of Rayleigh spread (four positions).

- **The path, not the format, sets the sensitivity.** Tone length, spacing and position count
  (from eight up) moved the best-window energy by no more than the scatter. A window ladder is
  the right receiver structure: the best window ran from 10 s to the whole block, and to 1.3 s
  beyond the worst bound.
- **The tone is the window that costs nothing.** It needs no coherence between tones, no common
  phase across positions, no ladder and no extra hypotheses. Per-tone reception with 10.24 s
  tones is 8.9 dB from coherent on a steady path, with 0.64 s tones 14.5 dB and with 0.16 s
  tones 17.3 dB.
- **Under the worst spread bound the best window is about 10 s.** A 10.24 s tone then needs
  nothing but per-tone reception. A tone of 0.16 to 1.28 s needs coherent windows of 8 to 64
  tones, far below the specification's default shortest window of 1/64 of the block (82 s here);
  with that default it loses 3.6 to 4.0 dB. A 41 s tone needs the cells inside it, or loses
  2.3 dB.
- **Positions a reciprocal of a long tone apart have no fallback.** The format with 0.098 Hz
  spacing and 10.24 s tones (16 positions in 1.6 Hz) matched the others on every channel up to
  the worst bound, 22.6 to 22.7 dB at ±50 mHz and at 50 mHz Rayleigh. Its tone is its cell, so
  no window shorter than 10 s exists that still separates the positions. At 0.5 Hz of Rayleigh
  spread it did not reach 1% errors at any energy up to 44 dB, where the 0.78125 Hz spacing
  decoded at 27.4 dB on its 1.28 s cells and 0.39 Hz at 28.4 dB. That spread is outside every
  record below 9 kHz; the point is that the wide spacing fails gently and the narrow one does
  not.
- **Loss grows with the block.** These figures are for 87 minutes. A longer block has more
  windows of the same length, and adding more windows as energies costs more. On the steady
  path the rungs of 4, 16, 64 and 256 windows needed 1.0 to 1.3, 2.7 to 3.0, 4.9 to 5.1 and 7.2
  to 7.6 dB more than the whole block at 1% wrong decodes (an exact calculation by the
  independent check gives 1.2, 2.8, 4.9 and 7.4 dB). Under the specification's presence test,
  with its far lower false-presence rate, the same steps are smaller: 0.7, 1.8 and 3.5 dB for 4,
  16 and 64 windows there, and 0.6 to 0.9, 1.7 to 2.3, 3.3 to 4.2 and 5.5 to 6.5 dB in the
  delegated search's exact calculation for 4 to 256 windows.

### 5.2 Choice

| Consideration | Faster tones | Slower tones |
| --- | --- | --- |
| Sensitivity with the right window | equal | equal |
| Floor without coherence between tones | 26 to 31 dB | 19 to 22 dB |
| Worst spread bound (10 s windows) | needs the ladder down to 10 s | per-tone reception at 10 s; cells inside the tone beyond |
| Work per decode on the full ladder | in proportion to the tone rate | – |
| Timing hypotheses for per-tone reception (an eighth of a tone) | in proportion to the tone rate | – |
| Timing hypotheses for coherent sums across positions | 1/(8 × span), whatever the tone | the same |
| Transient loss and transmitter stress | in proportion to the tone rate | – |
| Shortest block (64 tones for 16 positions) | 82 s at 1.28 s | 11 min at 10.24 s, 44 min at 41 s |
| Observer with a filter bank | better concealed | a 10 s tone is a narrow, strong line |

- **10.24 s (8 cells of 1.28 s; 0.098 tones per second) is the recommended tone length** for
  −10 to −30 dB-Hz. The specification's own coherent block at −10 dB-Hz is 10 minutes, so the
  11-minute floor costs little in the range where coherent reception is necessary.
- The measured path would carry a longer tone: its worst rates of 0.2 to 2 mHz put the best
  window at minutes (5.5 minutes at ±1 mHz in the table above, 82 s at ±5 mHz). The 10 s tone
  is sized for the worst preset, which nothing measured reaches, and keeps the shortest block
  usable at −10 dB-Hz.
- 1.28 s (one cell) is the fastest tone worth offering here. It keeps the shortest block at
  82 s for a link that turns out strong, at eight times the ladder work and a floor 4.2 dB
  worse. The phase ramp costs it nothing when the receiver's reference follows the ramp, and
  0.24 dB when the receiver assumes a step.
- 40.96 s (32 cells) suits blocks longer than about three hours (256 tones or more). It improves
  the floor by another 2.6 dB and quarters the work. Longer tones than that gain little: under
  the moderate bound the best window is already 82 s.
- This departs from the specification's stated preference for the fastest rate the spread and
  bandwidth allow. Section 4.2 of the specification already lists hardware limited to a very
  narrow band as an exception; the reason found here is not the antenna's speed but the
  receiver's floor and work.
- The practice found in the literature sits in the same place: EbNaut runs coherent symbols of
  0.5 to 180 s below 9 kHz and at LF, FST4W-1800 uses 11.2 s symbols, and the Mars-landing
  tones were 10 s long. Spacing wider than the reciprocal of the tone is used wherever spread or
  drift approaches the spacing (JT4, JT9, Q65, the landing tones) [literature].

## 6. Computational ceiling for that rate

### 6.1 Measured decode rates

[Measured; `decode_bench` on the local Ryzen 5 PRO 5650U with 12 threads and on a pod's EPYC 9355
with 13 threads (its CPU quota), `decode_gpu` on a full RTX PRO 6000 Blackwell. One decode scores
all 2²⁴ messages of one hypothesis for the specification's 32 symbols of 256 values. Synthetic
tables; every route returned the planted message. The CPU timings keep all threads busy for
seconds; the large cases time a sample of the message space and scale it, and the output lists
how much was timed.]

| Route | Ryzen 5650U, 12 threads | EPYC 9355, 13 threads | RTX PRO 6000 Blackwell |
| --- | ---: | ---: | ---: |
| Per-tone decode (32 lookups per message) | 12.5 /s | 72 /s | 5,900 /s |
| Per-tone decode by fast Walsh–Hadamard transform | 6.5 /s | 67 /s | not run |
| Full ladder, 128 tones (41 s tones, 87 min) | 0.65 s | 0.12 s | 0.018 s |
| Full ladder, 512 tones (10.24 s tones) | 2.5 s | 0.48 s | 0.070 s |
| Full ladder, 2,048 tones (2.56 s tones) | 10 s | 1.9 s | 0.29 s |
| Full ladder, 8,192 tones (0.64 s tones) | 41 s | 7.7 s | 1.1 s |
| Full ladder, 32,768 tones (0.16 s tones) | 166 s | 31 s | 4.4 s |
| Table ladder, 64 windows (windows of 1/64 of the block and longer) | 7.9 s | 1.5 s | 0.027 s (36.9 /s) |
| The same as one FP16 matrix product on Tensor Cores | – | – | 0.169 s (5.9 /s) |

- **The full ladder costs one lookup per message and tone**: 3.3 to 3.4 thousand million
  message-tones per second on the laptop at every tone count for 16 and 24 bits (3.0 to 3.2 for
  10 bits), 18 on the EPYC and about 120 on the GPU. Decode time is in proportion to the number
  of tones in the block.
- Six threads on the laptop gave the same rate as twelve, and bursts of tens of milliseconds ran
  up to 4.6 thousand million. The sustained figure is the one a receiver gets.
- The specification's estimate of 65 coherent-ladder decodes a second (64 windows, by
  extrapolation) is now a measurement of 36.9 as a direct kernel. The per-tone decode measures
  5,900 a second for the specification's block shape, against 8,400 for the smaller shape timed
  earlier.
- **The matrix product on Tensor Cores is six times slower than the direct kernel** for the
  64-window ladder (product 90 ms at 390 nominal TFLOP/s, 45% Tensor Core activity; forming the
  rung energies and maxima from 8.6 GB of scores another 75 ms). The one-hot operand wastes 255
  of 256 products, as in the earlier per-tone measurement. This product is the search over
  messages, which is the wrong job for Tensor Cores. The search over frequency and drift is a
  dense product and runs at 404 nominal TFLOP/s (section 6.4).
- On the laptop the plain lookups beat the fast transform by two for the 24-bit per-tone decode.
  For 16 bits the transform is four times the faster (13,500 against 3,200 a second).
- The GPU's full-ladder kernel does a tenth of its table kernel's lookups per second; it is
  untuned and should improve. The GPU was not timed for 10 and 16 bits.
- A first version of the CPU program left threads idle at 8,192 and 32,768 tones and timed the
  10-bit cases with thread start-up included. An independent check of this note caught it; the
  figures here are from the corrected program, rerun on the laptop and on a second pod.

### 6.2 Hypotheses that fit, and hypotheses needed

[Calculated from those times and the counting rules of the specification's calculation 18;
`search_ceiling`. The reference classes are assumptions.]

**Every margin in this subsection assumes one full decode per hypothesis.** That is the right
count for a single hypothesis and the wrong way to search many. Section 6.4 measures the search
done properly and replaces these margins for the adopted configuration; they are kept because
they show what the naive loop costs and how it depends on the tone length.

A block of duration `D` holds `D/T` tones, so the number of full-ladder decodes that fit into the
block's own duration is `rate / (2^k × tone rate)`, whatever `D`. For a 24-bit block:

| Device | 0.16 s tone | 0.64 s | 2.56 s | 10.24 s | 40.96 s |
| --- | ---: | ---: | ---: | ---: | ---: |
| Ryzen 5650U, 12 threads | 32 | 129 | 513 | 2,067 | 8,044 |
| EPYC 9355, 13 threads | 171 | 682 | 2,755 | 10,948 | 42,281 |
| RTX PRO 6000 Blackwell | 1,198 | 4,741 | 18,378 | 74,483 | 288,546 |

Measured on the laptop for the smaller blocks: 8,180 to 2.1 million for 16 bits and 459,000 to
130 million for 10 bits, from 0.16 s to 41 s tones.

Hypotheses needed for a whole-block window of 87 minutes: frequency bins × drift-rate bins ×
timing offsets. Coherent sums over tones on different positions need the timing to 1/(8 × span),
10 ms for 12.5 Hz, which is finer than an eighth of any tone considered here; the timing count
therefore follows the span and not the tone.

| Reference at each end | Frequency × drift | Timing offsets | Total |
| --- | ---: | ---: | ---: |
| GPS-locked, corrections logged or audio re-timed from the PPS (±1 ms) | 1 × 1 | 1 | 1 |
| GPS-disciplined, corrections slewed unseen at up to 1 ppm (±1 ms) | 174 × 33 | 1 | 5,742 |
| TCXO, ±1 ppm, 0.1 ppm/h, ±0.1 s | 174 × 4 | 20 | 13,920 |
| Crystal, ±20 ppm, 1 ppm/h, ±1 s | 3,469 × 33 | 200 | 23 million |
| Sound-card clock, ±100 ppm, 5 ppm/h, ±1 s | 17,344 × 163 | 200 | 570 million |

Margin, fitted divided by needed, for tones of 0.16 / 2.56 / 10.24 / 41 s:

| Reference | Laptop, 24 bits | GPU, 24 bits | Laptop, 10 bits |
| --- | --- | --- | --- |
| GPS-locked | 32 / 513 / 2,067 / 8,044 | 1,198 / 18,378 / 74,483 / 288,546 | 459,000 and up |
| Slewed unseen at 1 ppm | 0.006 / 0.09 / 0.36 / 1.4 | 0.21 / 3.2 / 13 / 50 | 80 / 1,319 / 5,494 / 21,870 |
| TCXO, ±0.1 s | 0.002 / 0.04 / 0.15 / 0.6 | 0.09 / 1.3 / 5.4 / 21 | 33 / 544 / 2,266 / 9,021 |
| Crystal, ±1 s | 10⁻⁶ to 0.0004 | 5×10⁻⁵ to 0.013 | 0.02 / 0.33 / 1.4 / 5.5 |
| Sound-card clock, ±1 s | below 10⁻⁴ | below 10⁻³ | 0.0008 / 0.013 / 0.06 / 0.22 |

- **With locked references the ceiling is far away at any tone rate**, even for 24 bits on the
  laptop. The tone rate decides how far: 32 decodes per block at 0.16 s tones, 2,067 at 10.24 s.
- **Without them, and with a decode per hypothesis, the margin follows the tone length.** For a TCXO with ±0.1 s of timing and 24
  bits the laptop is 430 times too slow at 0.16 s tones, seven times too slow at 10.24 s and 1.7
  times too slow at 41 s; the GPU keeps up from 2.56 s tones. For 10 bits the laptop keeps up
  with a TCXO at every tone length and with a crystal from 10.24 s.
- **With a decode per hypothesis a 24-bit block with a free-running crystal or sound card stays
  out of reach on any device and at any rate**, as the specification said then. So does a 10-bit block on a sound-card clock on
  the laptop (0.22 at best). The GPU's 10-bit figures are scaled from 24 bits, not measured;
  scaled, it would keep up with a crystal from 0.64 s tones and with a sound-card clock from
  10.24 s. The needed count grows with the cube of the window: a 5.8-hour block needs 53 to 64
  times more.
- **The timing count goes with the span.** A 25 Hz span doubles it and 50 Hz quadruples it. An
  earlier version of this count, and section 12.2 of the specification before 28 November, used
  an eighth of the tone alone and understated it for narrow bands.
- The antenna's group delay (3.7 ms at 86 Hz, 13 ms at 24 Hz) and the path delay (3.3 ms per
  1,000 km) are of the size of one timing step. They have to be known or searched.

### 6.3 What would cut the count

[Literature; none of it measured here.]

- **Discipline is the largest lever.** Re-timing the received audio from a recorded GPS pulse
  leaves under 50 ns of timing error, 3 mrad at 8.27 kHz, and removes the frequency, drift and
  clock-rate dimensions (Nicholson's tools, as used by the amateur links below 9 kHz). VERDIN was
  given atomic standards because a ±1 s timing search was judged economically unfeasible.
- **A sample clock tied to the carrier is not a separate dimension.** GNSS receivers slave the
  code rate to the carrier offset; the specification's calculation 18 already assumes this.
- **A transform across a message's tone sums** gives all frequency bins inside ±1/(4`T`) of the
  tone length at once (Psiaki's two-level acquisition; at most 0.9 dB at the edge). This is now
  built and measured (section 6.4): the laptop's 24-bit margin for the unseen-slewing reference
  at 10.24 s tones rises from 0.36 to about 18, where an assumed cost had given about 3.
- **Hierarchical screening** with a stated false-dismissal budget gave 19 to 26 times in a 2026
  gravitational-wave search and more elsewhere. Here the cheap statistic would be the per-tone
  decode, and it only works where that statistic keeps enough signal, which is what a long tone
  provides and a short one does not.
- **The timing offsets could come from a transform across positions.** Summing each position
  separately and transforming across the sixteen sums gives every delay inside one cell at once,
  for about sixteen times the accumulators. Derived; not built. Costed from the measured
  transform times it gains nothing at the 20 offsets of ±0.1 s and about two at the 200 offsets
  of ±1 s on a CPU; a first estimate of twelve left out the transform across positions itself.
  On the matrix-product route it loses, because the operand would have to be spread over the
  sixteen positions. The specification therefore carries timing offsets as one operand per
  offset, with the positions a message selects looked up once (its 13.1).
- **Thinning hypotheses for statistical reasons does not pay**: 10³, 10⁶ and 10⁹ independent
  hypotheses cost 1.2, 2.0 and 2.7 dB of block energy at a false-presence rate of 10⁻⁶.
- The counting rules stand against the continuous-wave literature: half-bin frequency spacing is
  0.91 dB at worst, and a drift spacing of 3.9 bins is the same metric mismatch of about 0.2. The
  worst corner of the rectangular grid is about 0.4 by the metric, more than either alone. Past
  about 30 minutes a drifting sound card needs a third dimension (the rate of the drift), and
  the count then grows with the sixth power of the window.

### 6.4 The search as a transform across tone sums

[Measured; `search_bench` on the laptop and on a pod's EPYC 9554 with 27 threads, `search_gpu`
on a full RTX PRO 6000 Blackwell; `search_margins` for the arithmetic. The adopted configuration:
512 tones of 10.24 s, a block of 5,243 s, 24 bits. Synthetic 4-bit bin values with one planted
message, frequency bin and drift, which every route returned. Reference classes other than the
measured one are scaled and marked so.]

For one timing offset a message fixes which position's bin value each tone contributes. Those
512 complex values are the message's tone sums. A frequency offset and a drift only turn them:

- **Gather once.** One lookup per message and tone gives the tone sums.
- **Transform.** Every frequency bin of the whole block is one output of a transform across the
  tone sums, and every drift hypothesis is a de-chirp before it. A shorter window is a transform
  across its own tones, and needs only the bins its length resolves: 174, 87, 44, … on the
  whole block, its halves, its quarters and so on for a ±1 ppm reference. One gather thus
  serves 696 frequency and drift hypotheses on the whole block and 176 more on the shorter
  windows, where the per-hypothesis loop repeats the gather and the whole ladder 696 times.
- **Epilogue.** Energies, sums over a rung's windows, best bin per rung, best message.

The transform is the step where a CPU FFT, cuFFT and a Tensor Core matrix product compete, as
they do for the receiver's first transform. As a matrix product it is dense: messages × tones
times tones × hypotheses, with no one-hot operand.

Whole search for a 24-bit block with a ±1 ppm reference and ±0.1 s of timing (174 frequency
bins, 4 drift hypotheses and 20 timing offsets on the whole block, every rung of the ladder):

| Route | Search time | Block duration ÷ search time |
| --- | ---: | ---: |
| Laptop, one full decode per hypothesis (13,920 decodes; section 6.2) | 35,300 s | 0.15 |
| Laptop, FFT across tone sums, FFTW, 12 threads | 1,624 s | 3.2 |
| Laptop, the same with a plain radix-2 transform | 7,046 s | 0.74 |
| EPYC 9554, 27 threads, plain radix-2 transform | 868 s | 6.0 |
| GPU, one full decode per hypothesis | 980 s | 5.4 |
| GPU, cuFFT across tone sums | 83 s | 63 |
| GPU, FP16 matrix product across tone sums, Tensor Cores | 11.4 s | 461 |
| GPU, the same with 8-bit integer operands | 18.9 s | 277 |

- **The laptop keeps up with a ±1 ppm reference for 24 bits**, by 3.2, where the per-hypothesis
  loop was seven times too slow. Six threads gave the same time as twelve.
- **The Tensor Core product is 7 times faster than cuFFT and 86 times faster than the
  per-hypothesis kernel on the same GPU**, and 143 times faster than the laptop. The product
  itself ran at 404 nominal TFLOP/s. Over the whole pipeline (gather, product, epilogue) the GPU
  showed 99% activity and 60% Tensor Core activity at 540 W; the product is 69% of the time,
  the epilogue 24% (1.1 TB/s read) and the gather 7% (2.0 × 10¹¹ lookups a second).
- **FP16 beat the 8-bit integer product**, 404 against 223 nominal units and 60% against 35%
  Tensor Core activity, with this library's general matrix product. The integer result is
  exact; the planted signal came out of all three.
- **The library transform matters on the CPU**: FFTW was 4.3 times faster than the plain
  radix-2 loop, and more than half of the laptop's time went into the many small transforms of
  the shorter rungs, which are called one at a time here.
- With 41 s tones (128 per block) the laptop needs 570 s; with 2.56 s tones (2,048) and no
  summing into sub-blocks 7,412 s. Shorter tones should be summed into about 512 sub-blocks
  before the transform, which leaves the gather as the part that grows with the tone rate; that
  was not built.
- **One hypothesis is still one plain decode.** With logged GPS references the full-ladder
  kernel (2.5 s on the laptop, 0.07 s on the GPU) is the cheaper route; the transform pays from
  some tens of hypotheses on the laptop and from about ten on the GPU. Choosing between them is
  the scheduler's work.

Block duration divided by search time for the assumed reference classes (at or above 1 the
device keeps up; only the ±1 ppm row at 24 bits is measured, the rest is scaled by rows and
passes as `search_margins` states):

| Reference at each end | Laptop, 24 bits | GPU, 24 bits | Laptop, 16 bits | GPU, 16 bits | Laptop, 10 bits |
| --- | ---: | ---: | ---: | ---: | ---: |
| GPS-locked, corrections logged | 2,067 | 74,483 | 529,000 | 19 million | 34 million |
| Slewed unseen at 1 ppm | 18 | 1,900 | 4,550 | 488,000 | 291,000 |
| TCXO, ±1 ppm, ±0.1 s | 3.2 | 461 | 827 | 118,000 | 52,900 |
| Crystal, ±20 ppm, ±1 s | 0.013 | 0.47 | 3.3 | 119 | 208 |
| Sound-card clock, ±100 ppm, ±1 s | 0.0006 | 0.02 | 0.16 | 5.9 | 10 |

- **What stays out of reach is narrower than first reported.** A 24-bit block with a
  free-running crystal is about twice too slow on one GPU and 80 times on the laptop, where the
  per-hypothesis count had it 300 and 11,000 times too slow. A 16-bit block with a crystal keeps
  up on both. A sound-card clock is in reach for 10 bits on the laptop and 16 bits on the GPU.
- More than 1,024 frequency bins exceed what tone sums can carry (half the tone's reciprocal);
  the scaled rows cut the range into bands of 512 bins with bin values taken at each band's
  centre, at 0.9 dB at a band edge.

Not in these figures: forming the bin values for each timing offset; the presence test; and, at
the two rungs below the whole block, a drifting reference moving the signal by up to 1.5 bins
between windows, which the epilogue covers by summing along tilted bin paths and which was not
timed. The kernels are stock: the library's general matrix product and a plain epilogue.

### 6.5 Hypotheses that are not a transform

[Measured; `hypothesis_pass_bench` on the laptop, 12 threads, all 2²⁴ messages in every timed
pass, synthetic tables with a planted message that won for every hypothesis.]

Spread hypotheses, the timing offsets of per-tone reception and the fine timing steps of a
wide-band coherent attempt each have their own evidence, so no transform yields them. What can
be shared is the enumeration of the messages: the entries of all hypotheses for a symbol value
are stored together, a lookup returns a vector over the hypotheses, and the 32 additions per
message are vector additions.

| Hypotheses in the pass | Real entries, one pass | Against a pass per hypothesis | Complex entries, one pass | Against a pass per hypothesis |
| ---: | ---: | ---: | ---: | ---: |
| 7 | 0.37 s | 1.6 times faster | 0.65 s | 1.4 |
| 56 | 0.80 s | 6.0 | 1.8 s | 3.9 |
| 350 | 4.5 s | 6.7 | 9.7 s | 4.5 |

- The pass per hypothesis is the best one measured, the per-tone kernel with symbols in the
  outer loop (0.086 s per hypothesis for real entries, 0.126 s for complex).
- A first version of this benchmark compared against a slower loop of its own and showed gains
  of 3.8 to 18; that comparison was unfair to the baseline and is not kept.
- The additions remain: 0.8 ns per message and hypothesis with real entries and 1.7 ns with
  complex ones. This does not bear on the sub-9 kHz configuration with locked references, which
  has one timing offset and no spread bank in its coherent attempts; it is here because the
  specification's rule against repeated work needed it.
- Not run on a GPU.

## 7. Positions and a value-dependent phase

[Simulated; `coherent_block_mc` with and without the option, 2,000 blocks per row, steady path;
[literature] for the explanation.]

In the specification the keyed phase of a tone depends on the key and the time only. Two
messages that fall on the same position in a tone then also share its phase, so a fraction 1/`P`
of the energy is common to all messages. At the capacity limit this is the `M/(M−1)` penalty of
coherent orthogonal signalling: 3.0, 1.25, 0.58 and 0.28 dB for 2, 4, 8 and 16 positions (Dolinar
and others, 2000). The penalty goes away if the keyed map sends each of the 256 values to a
position and to one of four quarter turns: messages on the same position then differ in phase
three times in four. Padovani and Wolf (1986) and the biorthogonal sets are the precedents.

| Positions (span at 1.5625 Hz) | Coherent, phase from key and time | Coherent, value-dependent phase | Per tone, 0.64 s tones |
| --- | ---: | ---: | ---: |
| 16 (25 Hz) | 13.0 dB | not run | 27.4 dB |
| 8 (12.5 Hz) | 13.1 dB | 12.8 dB | 27.7 dB |
| 4 (6.25 Hz) | 13.9 dB | 12.9 dB | 28.0 dB |
| 2 (3.125 Hz) | 15.3 dB | 12.8 dB | 28.9 dB |
| 4, with 10.24 s tones | 13.6 dB | 12.7 dB | 22.3 dB |

- **With a value-dependent phase two positions decode coherently as well as sixteen.** The
  0.6 to 1.0 dB of four positions and the 2.5 dB of two are recovered in full. The same holds under
  wander: 18.3 to 18.5 dB for 2, 4 and 8 positions within ±5 mHz, against 19.2 dB for two
  positions without it.
- Per-tone reception does not see the phase and is unchanged. It still prefers more positions,
  by 1.5 dB between two and sixteen.
- The receiver's work is unchanged: the quarter turn goes into the table entry of each value.
  The transmitter still sends one keyed quarter turn per tone.
- **It puts information into the phase, which the specification rules out.** That decision was
  taken for diffuse HF paths. Here only the coherent attempts read the phase, and they need a
  coherent path in any case. It is open decision 18 of the specification, not adopted.
- For this case sixteen positions remain the recommendation: they fit, they give the better
  per-tone floor, and nothing then depends on an open decision. With the option, an antenna of
  6 Hz could carry four positions 0.78125 Hz apart with no coherent loss, and the lattice scale
  `j` = 4 would not be needed.

## 8. What this changed in the specification

The configuration was first written into the specification as a recommendation with three open
decisions, and adopted on 28 November 2026 at the user's request.

- Sections 1 to 3: the sub-9 kHz configuration is part of the scope, the terms and the lattice
  table. Below 9 kHz the scale is `j` = 3, with `j` = 2 and 4 as alternatives.
- Sections 4.1 and 4.2: lattice scale and phase ramp are settings; the sub-9 kHz exception has
  its tone length (8 cells, with 1 and 32 as alternatives) and its reasons.
- Section 4.4: the keyed phase is reached by an 80 ms ramp below 9 kHz.
- Sections 5 and 6: sixteen positions below 9 kHz; shortest block of 64 tones.
- Section 8, items 7 and 8: timing of 1/(8 × span) for coherent sums across positions; below
  9 kHz the window ladder runs from two tones up, with the tone and its cells below, and the
  reference follows the ramp.
- Sections 9 to 11: mains and railway harmonics, the shortest block against the sizing table,
  and the concealment that was not evaluated.
- Section 12.2: the configuration, the antenna at 10 kW, the ramp and the measured search
  margins.
- Section 13: the coherent ladder is measured.
- Section 14: three rows settled (configuration; ramp; ladder and references). Still open: the
  value-dependent phase (18), the carrier and slot where railway or unsteady mains harmonics are
  in range (19), and the ramp length against a real transmitter (20).
- Section 15: weaknesses for the assumed antenna, the untested ramp, the slot, and the slow
  service of a strong link.

Two further revisions followed on 28 November, both from the user's questions. The hypothesis
search became a transform across tone sums (section 6.4 here; the specification's section 8
item 7, 12.2 and 13), with concealment below 9 kHz limited to what costs no computation. Then
three computational rules became requirements for the receiver and the simulator (the
specification's 13.1 and 13.5): time every route and rebalance for the whole job, do no work
twice, and treat simulation speed on a 256-CPU machine and on a 32-CPU machine with an RTX PRO
6000 as a requirement. Last, the specification's list of repetitions it still contained was
replaced by a stated form for every hypothesis dimension, with the one pass over the messages
measured (section 6.5 here).

The earlier statement that the lattice scale "is worth at most 1.6 dB, and only for an antenna
narrower than about 50 Hz" compared antenna loss alone at 4 to 16 positions. It stands as a
calculation. It did not weigh the mains slot, the transmitter, the per-tone floor or the
receiver's work.

## 9. Weaknesses and open items

- **No antenna, transmitter, path or receiver was measured.** The antenna is a single-tuned
  series circuit with assumed capacitances; the 2.5 nF tower is one line of a 1960s scaling
  table read through a delegated search.
- **The voltage limit is the largest unknown**, and `B*` goes with its inverse square. Corona
  data are from 18 to 57 kHz laboratory work; small stations failed first at the coil or
  bushing.
- **`B*` assumes the whole 10 kW is dissipated in series with the antenna.** A matched source,
  energy recovery or a different coupling network changes the bandwidth by factors of 1.5 to 3.
- **The phase ramp is calculated, not tested.** It removes the power reversal in a single-tuned
  model with a constant-amplitude source. No transmitter, matching network, protection circuit
  or voltage limit was in the model, and a 24 Hz antenna still sees the drive and the current
  49° apart with the 80 ms ramp. Until a transmitter is examined, the mandatory keyed phase on
  a high-Q antenna rests on this calculation.
- **Mains harmonics near 8 kHz were not measured anywhere in the sources.** The slot rests on
  grid-frequency statistics; amplitudes at a real site may make it irrelevant or insufficient.
- **Railway harmonics leave no clear slot** where a 16.7 Hz supply is in range (section 3), and
  the earlier notes say line positions should be measured, not computed.
- **The wander channels are constructed.** No phase spectrum of a carrier below 9 kHz exists in
  the sources. The Sub9 bounds exceed the measured quiet rates by 45 times at the least (the
  best preset) and by about four orders of magnitude at the most (the worst).
- **The Monte Carlo has known timing and frequency, Gaussian noise, one block length and message
  error only.** No sferics, no mains lines, no presence test, no antenna transients. Longer
  blocks lose more to window combining than the 87-minute figures.
- **The decode timings are synthetic kernels**, not a receiver: no transform, no accumulation,
  no memory traffic from real tables. The large CPU cases are scaled from a timed sample of the
  message space. The GPU ladder kernel is untuned, and the GPU was timed for 24 bits only.
- **The search counts are for assumed reference classes** and a rectangular grid. The transform
  across tone sums is built as a benchmark with synthetic bin values and measured for one class;
  the other classes are scaled, and tiered screening was not built.
- **A 10 s tone is conspicuous** to an observer with a filter bank (the specification's section
  11: 2 to 6 dB of margin with 32 tones per block). Accepted by the user's direction of
  28 November: below 9 kHz the band is regarded as uncontended, concealment is kept only where
  it costs no computation, and phase steps inside the tone are not offered there.
- **Per-tone reception with few positions** is now simulated for 2, 4, 8 and 16 positions, on
  the steady path and two wander channels only.
- **The carrier should stay clear of the night mode cut-offs.** They sit at multiples of about
  1.72 kHz for a reflection height of 87 km: 5.17 kHz is on the third, and 8.27 kHz is on the
  fourth daytime cut-off for 72.5 km. An order near cut-off is slow and long-tailed (tweek tails
  of 15 to 60 ms near 5 kHz). This is about the choice of carrier, not of rate or spacing, and
  was not examined here [literature, with the delegated search's own arithmetic].
- **Fading is not covered beyond Rayleigh.** Night-time nulls of 10 to 40 dB lasting tens of
  minutes on short paths, terminator minima and day-night level steps were not simulated; a
  block has to outlast them or be repeated.

## Sources

Access level as reported by the delegated searches: F full text, P part, A abstract, B record or
snippet, X not accessed. DOIs marked ✓ were checked against Crossref for this note (authors, title
and year match); the others were checked by the searches, and again when the reviews were stored
(below). This list is the selection the note uses; the reviews hold the full lists.

**Antenna, keying and voltage**

- NAVELEX 0101,113, *Naval Shore Electronics Criteria: VLF, LF and MF Communication Systems*
  (1972). Page images read (F): voltage-limited radiated power, bandwidth–efficiency product,
  the two regimes, the keying table after Watt, the zero-crossing keying rule. No DOI.
- A. N. Smith, CEL CR 81.020 (1981, ADA102473), OCR: `B = f/Q` with static-capacitance
  reactance, design classes, historical voltage limits, insulator behaviour. No DOI.
- NBS Report 9725 with the appended DECO study, and NBS Report 7286 (Morgan 1962): the 206 ft
  tower line, WWVL's 100 kV bushing limit, `Δφ ≈ Q·ΔC/C`. Delegated read, numbers re-checked by
  the search. No DOI.
- A. D. Watt, *VLF Radio Engineering* (1967): not accessed; cited by the handbook.
- H. A. Wheeler, "Fundamental relations in the design of a VLF transmitting antenna", IRE Trans.
  Antennas Propag. 6 (1958), 10.1109/TAP.1958.1144566 ✓ (A).
- J. S. Belrose and others, AGARD CP-529 (1993, ADA267991): matched-source broadening, Anthorn's
  bandwidth resistors, station bandwidths, the STANAG 5030 phase-linearity figure as quoted.
  Delegated read.
- H. F. Hartley, "Analysis of some techniques used in modern LF-VLF radiation systems", IEEE
  Trans. Commun. Technol. 16 (1968), 10.1109/TCOM.1968.1089913 ✓ (A); "Electronic broad banding
  of VLF/LF antennas for FSK radio communication" (1971), 10.1109/TCOM.1971.1090683 ✓ (title
  only).
- H. G. Wolff, "High-speed frequency-shift keying of LF and VLF radio circuits", IRE Trans.
  Commun. Syst. 5 (1957), 10.1109/TCOM.1957.1097513 ✓ (A). The 1951 BuShips *Electron* article
  on the same work was read as page images (F).
- J. Galejs, "Switching of reactive elements in high-Q antennas", IEEE Trans. Commun. Syst.
  (1963), 10.1109/TCOM.1963.1088740 ✓ (A); "Electronic broadbanding of high-Q tuned circuits or
  antennas", Arch. Elektr. Übertragung 17 (1963), no DOI (not read; described through a 2021
  dissertation).
- MIL-STD-188-140A: scanned pages read (F): no PSK at VLF/LF, phase-continuous FSK at index 1.0,
  necessary bandwidths, tolerances. STANAG 5030 itself: not accessed.
- US patents 2,824,955; 2,853,634; 4,689,803; 5,034,697; 5,404,114; 5,345,200: OCR or delegated
  read.
- P. Hansen and A. Watt, SSC San Diego TR 1904 (2003, ADA526052), OCR with garbled equations;
  P. Hansen and A. Rodriguez, "Performance analysis of large electrically small transmit
  antennas", IEEE AP-S (2011), 10.1109/APS.2011.5996389 ✓ (figure read). Corona fits and wet
  factors. P. Hansen, NRaD TR 1681 (1994): Cutler gradients (F).
- Omega station reports (AD0630900 and others), NIST SP 250-67, Andrews (1965),
  10.1109/TIM.1965.4313482 ✓: per-frequency tuning, servo behaviour, gust phase. Delegated read.
- M. A. Kemp and others, "A high Q piezoelectric resonator as a portable VLF transmitter", Nat.
  Commun. 10, 1715 (2019), 10.1038/s41467-019-09680-2 ✓ (OCR of the full text). J. P. Dytioco
  Santos and others, IEEE Trans. Microw. Theory Techn. (2020), 10.1109/TMTT.2020.3016381 ✓, and
  K. Schab, "Radiation efficiency limits in direct antenna modulation transmitters", IEEE
  Antennas Wireless Propag. Lett. (2020), 10.1109/LAWP.2020.3005412 ✓ (delegated read).
  A. E. Hassanien and others, Sci. Rep. 10 (2020), 10.1038/s41598-020-73973-6 ✓ (delegated
  read): FSK with both tones inside a Q of 850.
- Not researched for lack of search budget: aerostat, kite and trailing-wire antennas, several
  European stations, transportable systems.

**Coherent FSK, positions and windows**

- S. Dolinar, D. Divsalar, J. Hamkins, F. Pollara, "Capacity of pulse-position modulation on
  Gaussian and Webb channels", JPL TMO Progress Report 42-142 (2000) (F): the `M/(M−1)` penalty
  of coherent orthogonal signalling, 3.01 / 1.25 / 0.58 / 0.28 / 0.02 dB for 2 / 4 / 8 / 16 / 256.
  R. Padovani and J. Wolf, "Coded phase/frequency modulation", IEEE Trans. Commun. 34 (1986),
  10.1109/TCOM.1986.1096564 ✓ (B); S. MacMullan and O. Collins, IEEE Trans. Inf. Theory 44
  (1998), 10.1109/18.669291 ✓ (B).
- S. Butman and B. Levitt, "Capacity for noncoherent, soft-decision MFSK signaling", JPL TR
  32-1526 XV (1973) (F, page images): non-coherent capacity against per-tone SNR and alphabet.
  S. Butman and others, IEEE Trans. Commun. 24 (1976), 10.1109/TCOM.1976.1093213 ✓ (A).
- W. Osborne and M. Luntz, IEEE Trans. Commun. 22 (1974), 10.1109/TCOM.1974.1092333 ✓ (A);
  T. Schonhoff, IEEE Trans. Commun. 24 (1976), 10.1109/TCOM.1976.1093344 ✓ (A): multi-symbol
  CPFSK gains, which need phase memory this lattice does not have.
- S. Verdú, "Spectral efficiency in the wideband regime", IEEE Trans. Inf. Theory 48 (2002),
  10.1109/TIT.2002.1003824 ✓ (A).
- D. Divsalar and M. Simon, "Multiple-symbol differential detection of MPSK", IEEE Trans. Commun.
  38 (1990), 10.1109/26.48887 ✓ (B; JPL Publication 89-38 read in part).
- J. Östman and others, "Short packets over block-memoryless fading channels: pilot-assisted or
  noncoherent transmission?", IEEE Trans. Commun. 67 (2019), 10.1109/TCOMM.2018.2874993 ✓ (F,
  key passages): pilot-free transmission is the more energy-efficient.
- M. Khanzadi and others, IEEE Trans. Commun. 63 (2015), 10.1109/TCOMM.2015.2465389 ✓ (A);
  H. Ghozlan and G. Kramer, IEEE Trans. Inf. Theory 63 (2017), 10.1109/TIT.2017.2662698 ✓ (A);
  L. Barletta and S. Rini, arXiv:2001.07485 (A): Wiener phase-noise channels and the regime in
  which partially coherent combining is optimal.
- S. Franke, B. Somerville, J. Taylor, *Quick-Start Guide to FST4 and FST4W* (F) and the FST4
  decoder source (summarised): block detection over 1, 2, 4 and 8 symbols, Doppler spread to be
  at most 1/8 of the tone spacing. J. Taylor and others, *Q65 Quick-Start Guide* (2021) (F).
- E. Satorius and others, "Direct-to-Earth communications and signal processing for Mars
  Exploration Rover entry, descent, and landing", IPN Progress Report 42-153 (2003) (F): 256
  tones of 10 s, coherent length matched to the dynamics, 2 to 3 dB from search-space size.
- S. Bernstein, D. McNeill, I. Richer, IEEE Trans. Commun. 22 (1974), 10.1109/TCOM.1974.1092208
  ✓ (A), and MIT Lincoln Laboratory's history (F): ELF minimum-shift keying detected as
  antipodal.
- EbNaut: the primary site did not resolve on 27 November 2026; archived copies and secondary
  notes (DL4YHF, IZ7SLZ, PA3FWM) were read.

**Path below 9 kHz**

- P. Nicholson and others, posts to the rsgb_lf_group mailing list, December 2014 to December
  2017 (public archive, F): hourly phases at 8.82 kHz over 6,194 km, nightly phase
  repeatability at 6.47 and 8.27 kHz, day–night offsets at 2.97 to 8.27 kHz, the two-tone
  group-delay series near 8.3 kHz, constant-phase decodes of 9.7 and 12.2 h. Nicholson's own
  pages were read from archived copies.
- E. Swanson and C. Kugel, NELC TR 1740 (1972) (F): Omega phase statistics at 10.2 and 13.6 kHz.
  NBS Technical Notes 206-1 to 206-3 (1963–64), 10.6028/nbs.tn.206-3 and companions (F):
  structure function of phase at 18 kHz. A. D. Watt and R. Plush, J. Res. NBS 63D (1959),
  10.6028/jres.063d.009 (F).
- F. Han and S. Cummer, J. Geophys. Res. 115, A09323 (2010), 10.1029/2010JA015437 (F): hourly
  night reflection heights from 3 to 8 kHz sferics (mean 84.9 km, standard deviation 1.1 km).
  N. Thomson, M. Clilverd, W. McRae, J. Geophys. Res. 112, A07304 (2007), 10.1029/2007JA012271
  (F), and N. Thomson and others (2025), 10.1029/2024JA033610 (F): night heights and the dawn
  and dusk transitions.
- N. Thomson, C. Rodger, M. Clilverd, J. Geophys. Res. (2005), 10.1029/2005JA011008 (F);
  W. Peter, M. Chevalier, U. Inan (2006), 10.1029/2005JA011346 (F); M. Clilverd and others
  (2008), 10.1029/2008JA013220 (F): flare, storm and substorm phase rates at 13 to 40 kHz.
- D. Crombie, J. Res. NBS 68D (1964), 10.6028/jres.068d.012 (F): sunrise modal fades at 18 to
  20 kHz. J. Wait and K. Spies, NBS TN 300 (1964), 10.6028/NBS.TN.300 (F, figures by eye): mode
  attenuation and velocity.
- Not accessed: Barr, Jones and Rodger (2000); Watt (1967); Reder and Westerlund (1970); Kikuchi
  (1986); the Siple and ionospheric-heater phase literature (abstracts only, amplitudes only).
  The search budget ran out before recent timing and navigation papers were covered.
- These DOIs were verified against Crossref by the search, not re-checked for this note.

**Search cost, windows and oscillators**

- P. Brady, T. Creighton, C. Cutler, B. Schutz, Phys. Rev. D 57, 2101 (1998),
  10.1103/PhysRevD.57.2101 ✓ (P); P. Brady and T. Creighton, Phys. Rev. D 61, 082001 (2000),
  10.1103/PhysRevD.61.082001 ✓ (P); R. Prix, Phys. Rev. D 75, 023004 (2007),
  10.1103/PhysRevD.75.023004 ✓ (P); C. Cutler, I. Gholami, B. Krishnan, Phys. Rev. D 72, 042004
  (2005), 10.1103/PhysRevD.72.042004 ✓ (P); H. Pletsch and B. Allen, Phys. Rev. Lett. 103, 181102
  (2009), 10.1103/PhysRevLett.103.181102 ✓ (F); R. Prix and M. Shaltev, Phys. Rev. D 85, 084010
  (2012), 10.1103/PhysRevD.85.084010 ✓ (F); C. Dreissigacker, R. Prix, K. Wette, Phys. Rev. D 98,
  084058 (2018), 10.1103/PhysRevD.98.084058 ✓ (P); B. Allen, Phys. Rev. D 104, 042005 (2021),
  10.1103/PhysRevD.104.042005 ✓ (F): metric, template counts, semi-coherent loss, optimal
  segments.
- V. Dergachev, Class. Quantum Grav. 27, 205017 (2010), 10.1088/0264-9381/27/20/205017 ✓ (F);
  S. Suvorova and others, Phys. Rev. D 93, 123009 (2016), 10.1103/PhysRevD.93.123009 ✓ (P);
  A. Melatos and others, Phys. Rev. D 104, 042003 (2021), 10.1103/PhysRevD.104.042003 ✓ (P):
  loosely coherent sums and Viterbi tracking of a wandering frequency.
- S. Ransom, S. Eikenberry, J. Middleditch, Astron. J. 124, 1788 (2002), 10.1086/342285 ✓ (P);
  B. Andersen and S. Ransom, Astrophys. J. Lett. 863, L13 (2018), 10.3847/2041-8213/aad59f ✓
  (F); K. Dimoudi and others, Astrophys. J. Suppl. 239, 28 (2018), 10.3847/1538-4365/aabe88 ✓
  (F): drift and jerk searches and GPU throughput.
- M. Psiaki, "Block acquisition of weak GPS signals in a software receiver", ION GPS 2001 (F):
  two-level frequency grid, squaring loss, code rate slaved to carrier.
- A. Nitz, K. Kacanja, C. Capano, arXiv:2611.17229 (2026) (F): tiered screening with a
  false-dismissal target.
- A. Thompson, J. Moran, G. Swenson, *Interferometry and Synthesis in Radio Astronomy*, 3rd ed.
  (2017), 10.1007/978-3-319-44431-4 ✓ (P): coherence against Allan deviation. M. Lombardi,
  NCSLI Measure 3 (2008), 10.1080/19315775.2008.11721437 ✓ (F): disciplined-oscillator
  stability. P. Nicholson's receiver-toolkit notes (archived copy, P): re-timing from a recorded
  pulse to under 50 ns.
- Not accessed: the GNSS oscillator papers asked for (Gaggero and Borio; Pany and others;
  Curran and others), so the coherence formulas come from the interferometry text.

### The delegated reviews

The four searches' reports are in [`reviews/`](reviews/), as returned, each under a header that
says what was changed on storing and how far it was checked. They were added on 28 November at
the user's request; the permission system had declined a first attempt, so this note was written
from the reports before they were stored.

| Review | Covers | Used here in |
| --- | --- | --- |
| [Coherent FSK rate, spacing and positions](reviews/review-coherent-fsk-rate-spacing.md) | Orthogonality spacings, coherent and non-coherent capacity against the number of positions, block detection and window ladders, loss against window length, VLF, ELF, amateur and deep-space practice | Sections 4, 5.2, 7 |
| [VLF antenna bandwidth, keying and voltage limits](reviews/review-vlf-antenna-bandwidth-keying.md), with three sub-searches | The two Navy formulas behind `B*`, station bandwidths, synchronous tuning and direct antenna modulation, keying rules, transmitter tolerance, corona and insulation limits, tuning drift | Section 2 |
| [Phase stability, Doppler and delay below 9 kHz](reviews/review-sub9khz-phase-stability.md), with four sub-searches | Amateur phase series below 9 kHz, classical and modern VLF phase statistics, mode theory for 3 to 9 kHz, flares, storms and the terminator, design values for spread and delay | Sections 1, 5.1, 9 |
| [Long-coherence search](reviews/review-long-coherence-search.md) | Template counts and scaling laws, semi-coherent loss, wander tracking, GNSS acquisition, hierarchical screening, the statistical price of searching, oscillators, GPU implementations | Section 6 |
| [Earlier sub-9 kHz notes: inconsistencies and gaps](reviews/review-sub9khz-earlier-notes-cross-check.md) | What three re-readings of the repository's own antenna, propagation and noise notes found wrong, inconsistent or missing | Sections 1, 3 |

- All 340 distinct DOIs in the four literature reviews resolve (checked on 28 November 2026 at
  Crossref, one arXiv DOI at doi.org), and the registry's authors match the citations. That
  shows each DOI exists and belongs to the cited work, not that the work says what is quoted.
- The searches' own calculations are kept with their outputs in
  [`literature-calcs/`](../../../_appraisal/algorithms-matrixModem/sub9-fsk-rate/literature-calcs/README.md).
- The reviews contain more than this note uses. Not carried into the design here, among others:
  the optimum coherent window against linewidth and C/N₀, the Viterbi and loosely coherent
  methods for a wandering frequency, the station-by-station bandwidth table, and the mode-theory
  delay tables.

## Independent check of this note

A separate agent compared the first version of this note, the specification's section 12.2 and the
index with the program outputs, recomputed the closed forms, and reported what did not match. Its
report is not kept; what it found and what was done:

| Found | Done |
| --- | --- |
| The CPU ladder benchmark parallelised only over a few sampled blocks of the message space, so at 8,192 and 32,768 tones most threads idled, and the result was reported as a property of the device | Benchmark rewritten with one dynamic loop over (repetition, block) tasks; rerun on the laptop and on a second pod; all CPU figures replaced (for example 221 s became 166 s for 32,768 tones) |
| The 10-bit and 16-bit margins were scaled from 24 bits, and the direct 10-bit timings were slower | Now measured for 10, 16 and 24 bits on the laptop; the search arithmetic reads the measured files |
| The hypothesis count used timing steps of an eighth of a tone only | 1/(8 × span) added, which governs below 9 kHz; the margin now follows the tone length, not its square |
| A sentence said a 10-bit block on a sound-card clock is out of reach on any device | Limited to the laptop; the scaled GPU figure keeps up from 10.24 s tones |
| Window-combining losses were quoted "at these error rates" from the presence-test calculation | The Monte Carlo's own rung differences are quoted (1.0 to 1.3, 2.7 to 3.0, 4.9 to 5.1, 7.2 to 7.6 dB for 4 to 256 windows), with the check's exact values of 1.2, 2.8, 4.9 and 7.4 dB, and the presence-test values kept apart |
| "Eleven formats on every channel" did not match the outputs | Restated as the formats with eight or more positions, with the 0.5 Hz row and the reciprocal-spacing format named as exceptions |
| The few-position loss was given three ways; the weakness list said two positions were not simulated for per-tone reception | Earlier and present runs labelled; the weakness corrected |
| The specification's section 4.2 said the timing search follows the tone rate | Limited to per-tone reception |
| Smaller items: 22.6 for 22.5 dB, 325 and 330 for 324 mW, "a third" for 40%, "12.5 to 21 Hz" for 16 to 21 Hz, 0.8 for 0.7 dB on the recommended tone, "one cell" for a 1.28 s best window at 0.5 Hz, "6 to 620 Hz" in a paragraph about 8.27 kHz | Corrected in the note, the specification and the index |

- **Checked and found correct:** `B*` for all capacitances and voltages; the radiated powers; all
  thirty cells of the keyed-tone loss table; the detuning, stored-energy and mains-slot figures;
  the spacing table; every range of the channel table and the tone-length table; the
  few-position table; the transcription of every measured decode time; the needed-hypothesis
  counts and all margin rows.
- **Not covered by that check:** the phase-ramp calculation and the railway harmonic, which were
  added afterwards, and the literature itself.
- Corrections made apart from that check: a row in the index that called the earlier note's
  radiation-Q range wrong was removed, because that range was right; and the loss of the
  default ladder depth with short tones, first given as 3.4 to 3.9 dB, is 3.6 to 4.0 dB.

## Provenance and cost

- Local calculations and the CPU timings: 12-thread Ryzen 5 PRO 5650U.
- The phase-ramp calculation, the reruns of the searches' scratch calculations and the CPU
  search benchmark: the same laptop, 28 November.
- The search benchmark on the GPU: a third on-demand pod of the same type on 28 November, 0.116 h,
  about 0.31 USD, verified removed, no persistent volume; identity and hashes in
  [`results/gpu/identity_search.out`](../../../_appraisal/algorithms-matrixModem/sub9-fsk-rate/results/gpu/identity_search.out).
- GPU and second CPU timings: two on-demand RTX PRO 6000 Blackwell Server Edition pods, 0.076 h
  and 0.095 h, about 0.46 USD together, both verified removed, no persistent volume. The second
  reran both programs after the CPU benchmark was corrected; its outputs are the stored ones.
  No Linode instance was used. Device, driver, toolkit, host CPU and source and binary hashes
  are in
  [`results/gpu/identity.out`](../../../_appraisal/algorithms-matrixModem/sub9-fsk-rate/results/gpu/identity.out).
