# Matrix Modem FSK: specification draft

Draft of 26 November 2026, last reconciled 27 November (fourth pass), with the sub-9 kHz
configuration adopted, the hypothesis search restructured and the computational rules of
section 13.1 added on 28 November, for review. It extends the
[Matrix Modem brief](matrixModem.md) with a concrete keyed MFSK waveform, optionally frequency
hopped, and the receiver behaviour it requires. No implementation exists. Every number is a
calculation, a model simulation or a stage measurement on one GPU type and three CPUs. The supporting research is
indexed in
[`_ref/algorithms-matrixModem/Fable-5.2/`](../_ref/algorithms-matrixModem/Fable-5.2/README.md).
[Section 14](#14-decisions) lists what is settled and what still needs a decision, and
[section 15](#15-known-weaknesses) lists the known weaknesses.

## 1. Scope and fixed decisions

- One-way, one tone at a time at full peak power, fixed configuration per purpose, no feedback,
  no length fields.
- Target: 2–100 mW through more than 150 dB of path loss, C/N₀ from about +10 down to −20 dB-Hz,
  short messages that may take hours, days or weeks. On phase-stable paths without FHSS the range
  extends to about −37 dB-Hz, where coherent links below 9 kHz already work.
- **Constant envelope always; no amplitude shifts.** Power, not bandwidth, limits every link in
  this range, and a peak-limited transmitter that varies its amplitude lowers its average power
  ([4.2](#42-tone-length)).
- **Messages shorter than 10 bits are not supported.** A message of about 10 bits is expected to
  take less than twice as long to send reliably as a single bit at similar confidence. Under this
  specification's sizing it takes 1.1 to 1.3 times as long
  ([section 10](#10-sizing-and-expected-duration)).
- **The transmitter assumes a diffuse path; the receiver also tries a specular one.** The
  transmitter sends FSK suited to the expected Doppler spread of its configured path class, by
  default one tone per 160 ms cell on 6.25 Hz spacing, and never relies on coherence between
  tones. The receiver must always attempt to add tones coherently as well, in case the path is
  specular, and keep the better result.
- **The transmit format stays on the lattice; sensitivity is the receiver's work.** A path that
  allows more than per-tone detection is exploited by further receiver attempts on the same
  transmitted signal, not by another waveform.
- **Below 9 kHz the same format runs on a slower lattice.** FHSS off, a 1.28 s cell, sixteen
  positions 0.78125 Hz apart in 12.5 Hz, tones of 10.24 s, and a keyed phase reached by an 80 ms
  ramp instead of a step. Alphabet, code, keys and decoder do not change, and the receiver's
  coherent windows carry the sensitivity
  ([12.2](#122-phase-stable-paths-below-9-khz)).
- **Below 9 kHz not hitting the computational ceiling has priority, and concealment is kept only
  where it costs no computation.** The band is widely regarded as useless and is shared with
  every electronic device, so an emission there is unlikely to draw complaint. The keyed phase
  per tone stays because it is free; nothing that adds search work for concealment is offered
  there. This case is also the extreme one that fixes how the receiver's search is built
  ([section 8](#8-receiver-requirements), item 7).
- **Every tone carries a keyed phase.** At least one keyed phase change per tone is mandatory in
  every configuration, so that no observer on a steadier path than the intended receiver's can
  add the signal coherently without the key ([4.4](#44-phase-key)).
- **FHSS is optional, and its ramps are tolerated.** With FHSS nothing is assumed to carry over
  between hops: the shift oscillator changes phase at random on most radios and may come back
  slightly off frequency. Without FHSS (no shift oscillator, as below 9 kHz) tones run back to
  back with a phase the keyed receiver knows. Alphabet, code, keys and decoder are the same in
  both cases.
- **Helper Carrier is a separate, diagnostic system.** It stands in for more stable oscillator
  hardware. Its two-pilot frequency, phase and timing estimates are used, if at all, as standalone
  search narrowing with a benefit similar to a GPSDO. There is no joint detection between Helper
  tones and modem signals. No setting of this specification depends on it.
- **Computation is specified, not left to the implementation.** Three rules bind the receiver
  and the simulator ([13.1](#131-rules-that-bind-every-implementation)): every route is timed
  on the machine it runs on (CPU FFT against GPU FFT against a Tensor Core matrix-product DFT,
  and the like) and work is rebalanced so that the whole job finishes soonest; no work is done
  twice, in particular no outer loop repeats an inner one's work; and simulation speed is a
  requirement, on a 256-CPU machine and on a 32-CPU machine with an RTX PRO 6000.
- A general solution without drastic failure is preferred over one that is slightly better in the
  cases it handles. A theoretical weakness is listed, not dismissed.

## 2. Terms

| Term | Meaning |
| --- | --- |
| Cell | 512 samples = 160 ms at the default lattice scale; 1.28 s below 9 kHz. The unit of every duration. |
| Position | One of the tone frequencies, 6.25 Hz apart at the default scale; 0.78125 Hz below 9 kHz. |
| Lattice scale | The power of two `j` by which the cell is lengthened and the position spacing narrowed: cell 160 ms × 2^j, spacing 6.25 Hz / 2^j. |
| Tone | One tone on one position. **Tone length** is how long it stays there, in cells. |
| Hop | With FHSS, the radio's stay on one hop frequency. **Hop length** includes its edges. |
| Held tone | A tone longer than one cell. With FHSS it is as long as the flat part of its hop: one tone per hop. |
| Block | 10, 16 or 24 information bits sent as 32 symbols. |
| Phase key | The keyed phase of 0°, 90°, 180° or 270° that every tone is sent with. |
| Phase interval | The stretch sent with one keyed phase: the tone, or a part of it. |
| Phase ramp | Below 9 kHz, the 80 ms at the start of a phase interval in which the phase moves evenly from the previous keyed phase to the new one. |
| Per-tone reception | The receiver is coherent inside one tone and adds tones as energies. |
| Coherent attempt | The receiver undoes the phase key and adds different tones with their phases, over a window. |

"Dwell" is not used. Earlier drafts used it for both a tone and a hop.

## 3. Lattice

| Quantity | Default (lattice scale `j` = 0) | Below 9 kHz (`j` = 3) |
| --- | --- | --- |
| Complex baseband sample rate | 3,200 Hz (48 kHz audio ÷ 15) | 3,200 Hz, or 400 Hz after a further decimation by 8 |
| Cell | 512 samples = 160 ms | 4,096 samples at 3,200 Hz = 1.28 s |
| Tone bandwidth and position spacing | 6.25 Hz (one bin of one cell: the FT8 cell) | 0.78125 Hz |
| Total FSK bandwidth | Configurable, a whole number of positions. Default 2.2 kHz (450–2,650 Hz audio) = 352 positions; up to 2.8 kHz = 448; down to 12.5 Hz = 2 positions | 12.5 Hz = 16 positions, for example from 8,259.375 Hz |
| Tone rate | 6.25 tones per second by default (one cell per tone) | 0.098 tones per second (8 cells = 10.24 s per tone) |

- Every tone length and hop length is a whole number of cells. Every tone is on a position. A
  tone is unshaped and completes whole cycles in every cell. Its phase changes only by the keyed
  quarter turns of the phase key ([4.4](#44-phase-key)); below 9 kHz each change is spread over
  an 80 ms ramp.
- These rules give exact orthogonality over a cell, one transform plan, and bin statistics that a
  simulator may draw without forming samples.
- **Lattice scale.** The cell is 160 ms × 2^j with positions 6.25 Hz / 2^j apart. `j` = 0 is the
  default and serves every path through a voice-bandwidth radio. A larger `j` puts more
  positions into a band that the transmit antenna or the receiver's interference keeps narrow.
  - **Below 9 kHz the scale is `j` = 3**: a 1.28 s cell and positions 0.78125 Hz apart, sixteen
    of them in 12.5 Hz ([12.2](#122-phase-stable-paths-below-9-khz)).
  - `j` = 2 (1.5625 Hz, sixteen positions in 25 Hz) and `j` = 4 (0.390625 Hz, sixteen in
    6.25 Hz) are the alternatives there, for a wide antenna on one mains family and for an
    antenna of 20 Hz or less.
  - A scaled cell is 512 samples at 3,200 Hz / 2^j or 512 × 2^j samples at 3,200 Hz; the bins
    are the same. Conditioning runs before any further decimation.
  - Shorter cells are not offered: a 40 ms cell is never better than 160 ms for weak signals on
    a continuous path, at any spread up to 100 Hz ([4.2](#42-tone-length) gives the cost of
    faster tone rates). Meteor burst, which this draft does not support, is the one path that
    would want a shorter cell ([12.3](#123-meteor-burst-and-troposcatter-at-50-mhz)).

## 4. Tone, hop and phase key

### 4.1 Settings

| Setting | Default | Other values | Chosen from |
| --- | --- | --- | --- |
| Lattice scale | `j` = 0: 160 ms cell, 6.25 Hz spacing | `j` = 3 below 9 kHz (1.28 s, 0.78125 Hz); `j` = 2 or 4 there for other antennas | Transmit antenna bandwidth and the receiver's interference ([section 3](#3-lattice), [12.2](#122-phase-stable-paths-below-9-khz)) |
| Tone length | 1 cell (160 ms) | Any whole number of cells up to the hop. Below 9 kHz 8 cells (10.24 s), with 1 cell (1.28 s) and 32 cells (41 s) as the alternatives | Expected Doppler spread and level ([4.2](#42-tone-length)) |
| FHSS | On where the radio hops | Off | The radio; off below 9 kHz |
| Hop length | 0.4 s | 40 s recommended alternate; any whole number of cells | Hop channels, level, regulation ([4.3](#43-hop)) |
| Phase key | Always on: one keyed phase per tone | More often: every 8, 4, 2 or 1 cells of a held tone, or 2 or 4 times per cell; not below 9 kHz | Mandatory; more often for concealment ([4.4](#44-phase-key)) |
| Phase ramp | None: the keyed phase is stepped | 80 ms below 9 kHz; 160 ms for a transmit antenna narrower than about 40 Hz | The transmit antenna's Q ([4.4](#44-phase-key)) |

### 4.2 Tone length

The transmitter chooses the tone length for a diffuse path of the expected spread. With per-tone
reception a tone is worth holding for about 4 divided by the Doppler spread and no longer.

**Stated preference.** A tone rate as fast as the spread and the total bandwidth allow, up to
one tone per cell, with one keyed phase per tone. Two cases depart from it:

- **Paths where specular reception can be expected.** The receiver's wide-band coherent attempt
  then carries the load, with its 57 µs timing and a delay-rate search
  ([section 8](#8-receiver-requirements), item 8).
- **Hardware limited to an extremely narrow band**, in practice below 9 kHz. Tones are held
  for 8 cells of 1.28 s, 10.24 s. A 61 m antenna fed with 10 kW would pass a faster rate; the
  reasons are the receiver's.
  - Sensitivity with the right coherent window does not depend on the tone length. The tone is
    the one coherent window that costs no search and no coherence between tones, and 10 s is the
    best window under the worst sub-9 kHz spread bound.
  - The work of the coherent attempts follows the tone rate, as does the timing search of
    per-tone reception.
  - One cell (1.28 s) is the fastest tone offered there, for a link that turns out strong; 32
    cells (41 s) suit blocks longer than about three hours.
  - The search in millihertz bins through oscillator, clock and path drift is a transform
    across tone sums. For a ±1 ppm reference it is within reach of a laptop CPU for 24 bits;
    for a free-running crystal it still exceeds the ceiling for 24 bits and not for 16
    ([12.2](#122-phase-stable-paths-below-9-khz)).

The preference costs sensitivity on slow paths, which is open decision 1 of
[section 14](#14-decisions).

Extra transmit time against the theoretical bound at −10 dB-Hz, per-tone reception, for a
steady path and for spreads of 0.1 / 1 / 3 / 10 / 30 Hz:

| Tone length | With a 4 s hop | Without FHSS |
| --- | --- | --- |
| 1 cell | 21.2 / 14.2 / 5.7 / 2.3 / 1.2 / 0.5 dB | 21.0 / 14.0 / 5.5 / 2.1 / 1.0 / 0.3 dB |
| 24 cells (4 s) | 8.3 / 2.2 / 0.6 / 0.3 / 0.2 / 0.2 dB | 8.1 / 2.0 / 0.4 / 0.1 / 0.0 / 0.0 dB |

- The one-cell tone gives up 12 to 13 dB of time on steady and 0.1 Hz paths, 5 dB at 1 Hz and
  1 dB at 10 Hz. In the weak-signal regime these correspond to about half as many dB of power.
- For strong signals the order reverses, because a block cannot be shorter than its minimum
  number of tones: at +20 dB-Hz a one-cell tone gives the shortest block.
- Coherent attempts remove the cost only where the path turns out steadier than expected
  ([section 8](#8-receiver-requirements), item 8). The gain then follows the receiver's coherent
  window, not the tone length. When the expected spread appears they gain no more than a held
  tone of about 4 divided by the spread.
- **A tone shorter than one cell is not offered.** Rate times positions equals bandwidth, so a
  faster rate leaves fewer positions and, near the full bandwidth, a keyed-phase single carrier.
  Timing spacing is 1/(8 × rate): 20 ms at 6.25 Hz, 1.25 ms at 100 Hz, 57 µs at 2.2 kHz, and the
  clock-rate search grows in step (1, 6 and 126 hypotheses for a one-hour block at ±1 ppm). On a
  diffuse path the delay spread holds the rate to roughly 50 to 200 Hz on HF. A faster rate
  would improve concealment only for strong signals ([section 14](#14-decisions), open).
- **Where phase or amplitude would carry information instead.** Near the full bandwidth the
  signal is phase keying, and amplitude levels pay only when bandwidth limits the link: above
  roughly 2 bit/s/Hz, at a symbol energy of 12 to 15 dB over the noise density. In 2.2 kHz that
  is a C/N₀ of about 46 to 48 dB-Hz, against +10 to −20 dB-Hz here; the default lattice carries
  0.023 bit/s/Hz before coding. Both also need a coherent path. That regime belongs to a fast
  modem and is outside this specification. Textbook reasoning, not computed.
- Named held-tone lengths, and where each reaches diminishing returns with per-tone reception:

  | Held tone | Diminishing returns for |
  | --- | --- |
  | 0.4 s hop (2 cells) | Spreads of 10 Hz and more; steady paths near +10 dB-Hz |
  | 4 s hop (24 cells) | Spreads near 1 Hz; steady paths near 0 dB-Hz |
  | 40 s hop (249 cells) | Spreads near 0.1 Hz; steady paths near −10 dB-Hz |
  | 400 s hop (2,499 cells) | Spreads near 10 mHz; steady paths near −20 dB-Hz |
  | 4,000 s hop (24,999 cells) | Steady paths near −30 dB-Hz |

- **Rule for a held tone.** Sensitivity stops improving at the shorter of about 4 divided by the
  spread and the block time divided by 32. On a steady path that is 3.2 divided by the C/N₀ in
  hertz: 0.3 s at +10 dB-Hz, 32 s at −10 dB-Hz, 5 minutes at −20 dB-Hz. A block of 32 such
  tones needs 2.6 dB more energy than a block that is coherent throughout.
- **What a held tone asks of the radio.** Both oscillators must hold to a fraction of the tone's
  reciprocal while it lasts: 25 mHz for 40 s, 2.5 mHz for 400 s. The hop-to-hop frequency error
  should stay within the path's spread, or within a few tenths of a hertz
  ([4.3](#43-hop)).

### 4.3 Hop

- **Structure.** A hop holds a whole number of tones between its edges. The 0.4 s hop has 2
  cells flat and, at each edge, a 20 ms raised-cosine ramp and 20 ms of quiet. Longer hops have
  the same ramp and 60 ms of quiet, so a hop of `N` cells in all has `N` − 1 cells flat. The
  position changes at hop edges only while the envelope is zero.
- **Edge cost.** 0.97 dB of time at 0.4 s, 0.18 dB at 4 s, 0.02 dB at 40 s, 0.002 dB at 400 s.
- **A new phase in every hop is assumed and costs nothing more.** Per-tone reception does not
  use the phase.
- **A new frequency in every hop acts like added Doppler spread** on a held tone, unless the
  tone is strong enough to locate itself. Block time against a known centre: within ±0.1 Hz,
  nothing at 1 Hz of spread or more and 1.3 to 1.7 times on a 0.1 Hz path; within ±1 Hz, 1.6
  times at 1 Hz of spread and 6 to 11 times on a 0.1 Hz path. A one-cell tone is unaffected:
  an error of about ±1 Hz stays inside its 6.25 Hz bin.
- **0.4 s is the longest hop unlicensed hopping allows.** US rule 47 CFR 15.247 limits the
  average occupancy of a hop frequency to 0.4 s at 902–928 MHz, 2.4 GHz and 5.8 GHz, with at
  least 50, 15 and 75 hop channels (read in the 2023 annual edition, not the current eCFR).
- **40 s is the recommended alternate for one-cell tones.** It loses 0.02 dB to its edges, keeps
  the concealment of hopping for 50 channels ([section 11](#11-concealment)), gives coherent
  attempts a 40 s window, and asks the oscillators for 25 mHz. With one-cell tones a longer hop
  adds no sensitivity by itself; hops longer than about 40 s are worth having only with held
  tones.
- **A block spans at least 32 hops** ([section 6](#6-message-format-and-error-correction)).

### 4.4 Phase key

**Mandatory in all cases: every tone is sent with a keyed phase of 0°, 90°, 180° or 270°, so the
phase changes at least once per tone.** Tone, envelope and lattice are unchanged. The phase
carries no information and depends only on the key and the time, not on the position.

**Options.** The phase may change more often than once per tone: every 8, 4, 2 or 1 cells inside
a held tone, or 2 or 4 times inside a cell. A phase interval is never longer than the tone.
Below 9 kHz these options are not offered: they cost timing hypotheses, and concealment there is
kept only where it is free ([section 1](#1-scope-and-fixed-decisions)).

**Phase ramp for a high-Q transmit antenna (below 9 kHz).** The keyed phase is not stepped there.
It moves evenly from the previous keyed phase to the new one over the first 80 ms of the phase
interval (256 samples at 3,200 Hz), the shorter way round; a half turn goes in a keyed direction.
The position still changes at the tone boundary, and the signal stays phase-continuous
throughout. The ramp depends only on the key and the time, so the receiver's reference follows
it.

- **Why.** A plain half turn makes a high-Q antenna return almost the whole transmitter power to
  the source: for 2.6 ms and 11 J on an 86 Hz antenna at 10 kW, 9.2 ms and 41 J on a 24 Hz one.
  VLF practice forbids such steps because they trip or damage transmitters
  ([12.2](#122-phase-stable-paths-below-9-khz)).
- **With the ramp no power is returned.** The ramp is a frequency excursion of 3.125 Hz (quarter
  turn) or 6.25 Hz (half turn) for 80 ms, the kind of phase-continuous shift that FSK at VLF
  uses. Largest angle between drive and antenna current, with the standing-wave ratio against
  the antenna's resonant resistance:

  | Antenna | Steady tone on an outer position | 40 ms ramp | 80 ms ramp | 160 ms ramp |
  | --- | --- | --- | --- | --- |
  | 86 Hz | 8° (1.15) | 23° (1.53) | 16° (1.32) | 12° (1.23) |
  | 57 Hz | 12° (1.23) | 34° (1.90) | 23° (1.53) | 18° (1.37) |
  | 24 Hz | 26° (1.62) | 69° (6.2) | 49° (2.84) | 38° (2.12) |

  A 20 ms ramp still returns power on a 24 Hz antenna. Sending a half turn towards the centre of
  the positions instead of in a keyed direction does as much as doubling the ramp; it is not
  adopted because the ramp would then depend on the position.
- **Cost to the receiver.** None when its reference follows the ramp. A receiver that assumes a
  plain step loses 0.03 dB with 10.24 s tones and 0.24 dB with 1.28 s tones at 80 ms, and twice
  that at 160 ms. The timing spacing is unchanged.
- **Basis.** Calculated for a single-tuned antenna driven at constant amplitude
  ([`phase_slew`](algorithms-matrixModem/sub9-fsk-rate/results/phase_slew.md)). No transmitter
  or matching network was examined, and what a given transmitter tolerates is its
  manufacturer's figure. An observer sees a short frequency excursion at three tone changes in
  four, which is accepted below 9 kHz.

**Why it is mandatory**

- **Without it anyone can add the signal coherently.** Every position completes whole cycles in
  a cell, so every tone would start its cell at the same phase. The average of all possible
  tones is then a short pulse at the start of each cell, which an observer can add over its
  whole observation without knowing any position.
- **The gain is large and unbounded.** On a phase-stable path, against an observer measuring
  energy, it is 5 dB in an hour, 11.5 dB in 18 hours and 16 dB in a week (1.5, 7.7 and 12.5 dB
  if the observer must also search 10,000 frequency bins), growing 5 dB per tenfold of time.
  Over one block at −10 dB-Hz the observer's margin falls from 8.1 dB to −3.4 dB, and at
  −20 dB-Hz to −13.4 dB: the observer detects the block at a lower level than the link works
  at. A time-domain simulation confirms it, and confirms that a keyed phase removes it.
  Theoretical, steady phase at the observer
  ([calculation 17](algorithms-matrixModem/fsk-lattice/results/lattice_receiver_section17.md)).
- **The observer's path need not be the link's.** On HF an observer may be near enough for
  ground wave or line of sight, on a side or back lobe of the transmitting antenna, while the
  intended receiver further away sees a diffuse path and cannot add coherently at all.
- **It keeps coherent reception to the keyed receiver.** Where the path is steady, as below
  9 kHz, only a receiver with the key can add tones with their phases.
- **It limits perceived interference.** A nearby simple coherent receiver would lock onto the
  steady cell-rate component of an unkeyed signal. The keyed phase leaves none.
- **With few positions without FHSS** an unkeyed tone would also leave a steady line at every
  position, 6, 12 and 24 dB below the signal for 2, 4 and 16 positions. The keyed phase removes
  it.
- **Four phases, not two.** Squaring the signal removes a two-phase key and restores the pulse
  or line. In the simulation this needed tones about 10 dB stronger than the plain pulse, and
  detected nothing against four phases. A fourth-power attack remains in principle and was not
  computed.

**What it costs the receiver**

- **One phase per one-cell tone costs nothing.** Per-tone reception takes each cell's energy,
  which a quarter turn of the whole cell does not change; it need not undo the key. The timing
  spacing is 20 ms with or without it. Coherent attempts undo it with a quarter turn of each
  cell's transform output (a swap and a sign change, exact on 4-bit values).
- **One phase per held tone costs nothing either.** The timing spacing is an eighth of the tone,
  as it would be without the key.
- **The timing rule.** A timing error `τ` keeps (1 − `τ`/`T`)² of the power, where `T` is the
  phase interval. Hypotheses `T`/8 apart lose 0.27 dB on average and 0.56 dB at worst. The count
  per dimension is the timing uncertainty divided by that spacing; timing known to a few
  milliseconds needs one hypothesis.
- **Phase steps inside a held tone cost timing hypotheses.** A step every cell needs 20 ms
  spacing where held tones of 4, 40 and 400 s with one phase need 0.5 s, 5 s and 50 s: 24, 249
  and 2,499 times the hypotheses per dimension, where timing is uncertain.
- **More than one phase per cell costs transforms, not arithmetic.** The receiver undoes the key
  on the samples before the transform, at no cost. The coarse timing spacing shrinks with the
  phase interval, so 2 or 4 phases per cell need 2 or 4 times the transforms per second of
  timing uncertainty (10 and 5 ms spacing), for per-tone reception and coherent attempts alike.
  The 350 fine timing steps of the wide-band coherent attempt are unchanged. Each tone then
  appears 2 to 4 positions wide without the key, at the band edges too. Derived, not computed.
- **Delay spread smears every phase step, as it does every tone change.** Paths that arrive
  late still carry the previous phase, so about (delay spread ÷ phase interval) of the energy is
  lost. With one phase per one-cell tone this adds nothing, because the tone change already has
  that loss: about 0.01, 0.05 and 0.2 dB at 0.5, 2 and 7 ms of delay spread. Two phases per cell
  lose about 0.03, 0.1 and 0.4 dB and four about 0.05, 0.2 and 0.8 dB. A phase or tone rate
  comparable to the delay spread would be smeared into noise, which is the same limit of 50 to
  200 Hz on HF given in [4.2](#42-tone-length). Hand estimates.
- **Clock drift adds a timing-rate dimension.** A sample clock off by `e` slips `e` times the
  block time, so the timing rate needs about 2·`e`·(block time)/spacing hypotheses, and offset
  and rate multiply. For a 3.6 h block with a phase per cell: 1 with a disciplined clock, 2 at
  ±1 ppm, 26 at ±20 ppm.
- **Oscillator drift adds nothing to the timing search.** Frequency error is searched after the
  phase key is removed. It has its own frequency and drift-rate search wherever the receiver is
  coherent for long ([12.2](#122-phase-stable-paths-below-9-khz)).

**What more frequent steps are for**

- **Concealment against an observer with a filter bank** grows with the number of phase
  intervals in a tone, about 1.4 dB per doubling ([section 11](#11-concealment)).
- **They are not a cheaper substitute for tone changes.** At the same rate both need the same
  timing spacing, drift search and bandwidth. Phase steps inside a held tone are worth having
  only on a wide band over a diffuse path of low Doppler spread, where tones at different
  positions cannot be added coherently and a faster tone rate would cost sensitivity. In a very
  narrow band the two are equivalent.
- **Phase changes the receiver does not know are not an option.** The receiver would be coherent
  only inside a cell, and a block would take 3.2, 9.4 and 29 times as long at held tones of 4,
  40 and 400 s.

### 4.5 Without FHSS

- There is no ramp and no quiet interval; the envelope is constant through the block, and the
  edge cost is recovered.
- Every tone starts at its keyed phase. Because a tone completes whole cycles in every cell, the
  receiver knows the phase of every tone from the key, apart from one carrier phase and the
  frequency offset. The transmitter needs nothing extra for this.
- The receiver's coherent attempts may then span the whole block
  ([section 8](#8-receiver-requirements), item 8).

## 5. Alphabet and keyed placement

- **256 symbol values per tone** (8 coded bits).
- For every tone a keyed permutation of all positions maps the 256 values to positions. The
  remaining positions (96 of 352 by default) are not used for that tone.
- **Fewer than 256 positions.** The largest power of two `P` that fits is used, and the keyed map
  is balanced: 256/`P` values share each position, in a grouping redrawn for every tone. Symbols,
  code, tables and decoder do not change. In a coherent decode of a 10-bit block that cost 2.6,
  0.8, 0.2 and 0.0 dB for 2, 4, 8 and 16 positions (simulated; a second, smaller simulation
  agrees within its scatter of ±0.2 dB). Per-tone reception prefers more positions as well, by
  1.5 dB between two and sixteen (simulated for the sub-9 kHz formats on a steady path).
  **Below 9 kHz sixteen positions are used**, which avoids the loss
  ([12.2](#122-phase-stable-paths-below-9-khz)). A keyed map that also gives each value one of
  four quarter turns would remove the coherent loss for any number of positions; it is open
  decision 18 of [section 14](#14-decisions).
- Adjacent positions are allowed. With a fresh placement for every tone and the spread-weighted
  evidence of section 8, simulation shows no loss against wider spacing at spreads up to 30 Hz
  for tones of 2 and 24 cells. One-cell tones were not run in that simulation.
- No placement may have structure at fixed position spacings (2, 4, 8, 16) or fixed periods.

## 6. Message format and error correction

**Block.** A block carries `k` = 10, 16 or 24 information bits (configured) as 32 code symbols,
each sent on `L` tones: 32·`L` tones per block.

- **Code.** The value of symbol `j` is `G_j·c xor m_j`, with `c` the block's bits, `G_j` a keyed
  8 × `k` binary matrix and `m_j` a keyed 8-bit mask. This is a keyed random code, not a
  convolutional code.
- **Repeats.** `L` carries the adaptation to weak signals; the code does not change. `L` comes
  from the configured transmit power, path loss, noise density, margin and Doppler class. With
  `P` positions fewer than 256, `L` is at least 8/log₂`P`, so that a block still carries 256
  coded bits. With the sixteen positions used below 9 kHz `L` is at least 2: the shortest block
  is 64 tones, 10.9 minutes with 10.24 s tones (82 s with 1.28 s tones, 44 minutes with 41 s
  tones).
- **Interleaving.** Each tone of the block is assigned to a symbol by a keyed pseudo-random
  permutation over the whole block, redrawn for every block. Nothing is regularly spaced.
- **Hop spanning.** With FHSS a block spans at least 32 hops, and each symbol's tones are spread
  over the hops as evenly as the permutation allows. Where the level would allow a shorter
  block, `L` is raised until the block fills 32 hops; the extra energy is margin.
- **Decoding** is an exhaustive maximum-likelihood search of all `2^k` messages for every
  hypothesis of the receiver. There are no sync symbols: the block is its own sync pattern.
- **What adapts and what does not.** Tone length and level are met by `L`; total bandwidth by the
  keyed map of section 5; block length by the interleaver; lost hops by hop spanning and margin.
  Fixed regardless: 32 symbols per block, the three block sizes, and the code.
- **Why this code.** For 10 to 24 bits nothing better is known to this draft. It is within 0.2 dB
  of ideal orthogonal signalling at 10 bits and 0.3 to 0.7 dB at 20 bits. It has no sync, CRC or
  termination overhead. Repeating symbols loses nothing, because two messages already differ in
  nearly every symbol. The presence test, not the code, sets the block length.

**Message.** A message is a run of blocks on a public calendar derived from the system clock.

- It ends when two consecutive block periods are scored absent. A received length never selects
  geometry or completion. If the schedule resumes, the message reopens and the gap is erased
  blocks.
- Longer messages are runs of 24-bit blocks for now. That costs about 2–3 dB at 100 bits and
  3–5 dB at 300 bits against one long code, and is where another code would do better. The first
  candidate is a CRC-aided tail-biting convolutional code over the tone alphabet with list
  Viterbi decoding; the main alternative is a nonbinary LDPC code of the Q65 kind. They have not
  been compared, and a long-block format is not specified until one is qualified.

## 7. Keys and schedules

- AES-256 generates every schedule: the position map for every tone, the tone-to-symbol
  assignment for every block, the matrices `G_j` and masks `m_j`, the hop frequencies, and the
  phase key. Inputs are the key, the shift frequency rounded to the configured multiple of
  10 Hz, and the block, tone and phase-interval ordinals. The hop frequency is not an input to
  the other schedules.
- Payload bits are encrypted before coding by the AES-256 + Serpent cascade keystream. Encryption
  adds no bits.
- Encryption is always on, with the published zero key or a loaded keyfile, as in the brief.
- A signal under another key or shift occupies other positions and is noise to this receiver.
  Wrong-key admission is decided by the presence test of section 8.

## 8. Receiver requirements

1. **Front end.** Fixed gain (AGC off or held), transceiver noise blanker and noise reduction off,
   widest linear filter, noise 20–30 dB above the quantization floor.
2. **Conditioning, at full precision, before any 4-bit quantity.** Down-convert and decimate to the
   lattice rate. Estimate the noise scale from a lower quantile. When a sparse line comb or steady
   carriers are measured, excise them on at least 1 s of low-sidelobe analysis. Weight samples by
   inverse local power. Erase samples that still exceed the range.
3. **Four-bit samples.** 15 levels of 0.45σ per component plus a sixteenth code for an erased
   sample.
4. **Transform.** One transform per cell, rectangular window, all positions and the fine bins
   around them that the widest spread hypothesis covers. For a tone longer than one cell the
   cells are combined over the tone after the phase key has been undone. A CPU FFT serves live
   reception; on a GPU a matrix-product DFT serves when only the positions are wanted.
5. **Evidence.** For each position, a spread-weighted sum of bin energies for each hypothesis of
   a small bank (one bin, 10, 30, 60, 120, 250 and 500 Hz). The spread hypothesis is chosen per
   block, never per tone.
   - Normalise per position over time and per tone over positions by medians. With few positions
     the noise reference comes from the bins around the band, never from the positions alone.
   - With FHSS, estimate the noise level of every hop from that hop's own bins, weight the hop's
     evidence by the inverse of it, and erase a hop that is beyond range.
   - With FHSS and held tones of 4 s or more, the centre of each tone is unknown within the
     oscillator's stated range. The evidence is then the log of the mean, over centre hypotheses
     inside the tone, of the per-centre likelihood.
   - Quantize to four bits with the top level as a clip at 4 to 8 times the noise mean.
6. **Accumulation.** One table of 256 integer sums per code symbol and hypothesis.
7. **Search and decode.** Every timing, frequency-offset, drift and spread hypothesis is scored
   against every message. **The hypotheses are not an outer loop around the decode.**
   - *Per-tone reception:* one pass over the messages for all timing and spread hypotheses.
     The evidence of all hypotheses for a symbol value is stored together, so that the
     messages are enumerated once ([section 13](#131-rules-that-bind-every-implementation),
     rule 2).
   - *Coherent attempts with a single frequency hypothesis* (references locked and logged): one
     decode on the window ladder.
   - *Coherent attempts with many frequency or drift hypotheses:* for each message and timing
     offset the receiver gathers the bin value of the position that message puts each tone on,
     once. Every frequency bin of a window is then one output of a transform across those tone
     sums, and every drift hypothesis a de-chirp before it. Each rung of the ladder is searched
     only on the bins its window length resolves. An epilogue forms the energies, adds a
     rung's windows and keeps the best.
   - *The transform across tone sums is a routed stage* like the first transform: a CPU FFT,
     a GPU FFT or one matrix product on Tensor Cores (messages × tones times tones ×
     hypotheses), chosen by the scheduling rule of [section 13](#13-computation-and-simulation).
     Measured for a 24-bit block of 512 tones with 13,920 hypotheses on the whole block: 1,624 s
     by CPU FFT on a 12-thread laptop and 11 s by the matrix product on an RTX PRO 6000
     Blackwell, against 35,300 s and 980 s with a decode per hypothesis
     ([12.2](#122-phase-stable-paths-below-9-khz)).
   - Tones much shorter than the window's frequency resolution requires are first summed into
     sub-blocks, so that the transform does not grow with the tone rate. Derived, not built.

   Timing hypotheses are an eighth of the phase interval apart, in offset and in
   clock rate: 20 ms for a one-cell tone or a phase step every cell, less in proportion for
   more than one phase per cell. Coherent sums over tones on different positions need
   1/(8 × band span) where that is finer: 10 ms for the 12.5 Hz used below 9 kHz, whatever the
   tone length. Coherent windows add frequency bins half the window's reciprocal apart and
   drift-rate hypotheses ([12.2](#122-phase-stable-paths-below-9-khz)).
8. **Coherent attempts across tones (required).** The receiver makes these for every block,
   whatever the transmitter's settings and over the whole configured band. They are part of its
   fixed work and are not reduced under overload. Items 5 to 7 always run as well, and the
   better statistic per block decides.
   - **Span.** The phase key is undone first. Without FHSS the attempts may span the whole
     block. With FHSS they are confined to one hop: tones inside a hop have phases known from
     the key, each hop arrives with a new unknown one, and hops are added as energies.
   - **Tables.** The receiver keeps the complex value of each position's bin for every tone as
     two 4-bit numbers and sums the values into complex tables, one set per window.
   - **Window ladder.** The whole span, its halves, quarters and so on down to a configured
     shortest window (default 1/64 of the block). A message's score at one rung is the sum over
     that rung's windows of |sum over symbols of the table entries|². Only the shortest windows
     are looked up; longer ones are sums of neighbours.
   - **Ladder below 9 kHz.** Without FHSS and with held tones the ladder starts at two tones
     and doubles up to the block, at one lookup per message and tone. The tone itself is
     per-tone reception, and the cells inside a tone are the rungs below it. Under the worst
     sub-9 kHz spread bound the best window was about 10 s, and a ladder stopping at 1/64 of an
     87-minute block lost 3.6 to 4.0 dB with tones of 0.16 to 1.28 s
     ([12.2](#122-phase-stable-paths-below-9-khz)). The receiver's reference follows the phase
     ramp of [4.4](#44-phase-key).
   - **Frequency.** A transform across a message's tone or window sums gives the
     frequency-offset hypotheses (item 7). With FHSS each hop's frequency error is common to
     its tones and is searched inside that hop.
   - **Drift limits the window, not the method.** The frequency has to stay within a fraction of
     the window's reciprocal while the window lasts: 25 mHz over 40 s, 2.5 mHz over 400 s. An
     undisciplined HF transceiver drifting 0.1 to 1 Hz per minute is marginal at 4 s and fails
     from 40 s; at 9 kHz, 1 ppm is only 9 mHz. A shorter rung wins when drift spoils the
     longer ones, and the per-tone result is the floor.
   - **Specular path.** A specular path has one delay, which appears as a phase slope across the
     band. Scoring with one phase for all positions is then valid over any band, with timing
     hypotheses about 1/(8 × band span) apart: 57 µs for 2.2 kHz, 1.25 ms for 100 Hz. The slope
     is applied to the stored bin values, so no fine step repeats the transform. The fine
     steps are further hypotheses of the one pass over the messages, with their table entries
     stored together; they multiply the additions, not the enumeration. Over 2.2 kHz that is
     about 350 fine steps for each 20 ms step.
     A slow change of the path's delay, as the ionosphere's height changes, is far below the
     20 ms spacing but not below 57 µs, and adds a delay-rate search that is not sized.
   - **Multipath.** Where positions do not share a phase, the attempts gain only within the
     path's coherence bandwidth, which on HF is tens to a few hundred hertz. Scoring with one
     phase per position is available by configuration for an antenna or path whose phase differs
     across the band.
   - **Diffuse path.** The attempts gain only inside the coherence time and the coherence
     bandwidth together: at most what a held tone of about 4 divided by the spread would gain,
     and nothing at spreads of 10 Hz or more. The per-tone result then stands.
9. **Presence.** The block is present if its best score exceeds a threshold taken from the same
   statistic on decoy keys, for a false-presence rate of 10⁻⁶ per block across all hypotheses and
   attempts. Ten times as many hypotheses cost about 0.3 dB of block time.
10. **Precision.** After conditioning, operands are at most four bits (sample code, evidence, bin
    values, 0/1 code tables) with exact wide accumulation. Where a device has no native 4-bit path
    the values are carried in wider containers; the arithmetic is unchanged.

Properties that tests must show:

- One tone cannot move a block's score by more than the clip, whatever its level.
- Adding tones never raises the error probability, with or without interference.
- Reception at a spread of 30 Hz reaches 99% at a finite signal level (no cliff).
- With nothing but noise, conditioning and the 4-bit sample code together cost under 0.3 dB.
- The block result is never worse than per-tone reception alone by more than the threshold
  change for the added attempts.
- Work counts: for a block, the counted first transforms, gathers, transforms across tone sums
  and passes over the messages equal the minimum of the table in
  [13.1](#131-rules-that-bind-every-implementation), and no count grows with a loop that does
  not change that stage's inputs.
- Scheduling: from a cold start, on each target machine, the whole job finishes within a stated
  factor of the best fixed assignment found by trying the routes (placeholder 1.2; the
  prototype reached 0.94 to 1.12).
- Simulation: on both experiment machines of [13.5](#135-simulation) a run reports its
  throughput, per-stage times and utilization, and its per-configuration work is done once
  whatever the number of trials.

## 9. Margin, interference and lost hops

- **SNR path-loss margin** (3, 6, 10, 13, 16, 20 dB) lowers the C/N₀ the block is sized for. It is
  shared by path-loss error, fading and noise deciles, receiver losses and interference.
- **Erasures.** With tones near or below 0 dB at the design point, a margin of 3, 6, 10, 13, 16 or
  20 dB tolerates the random loss of about 57, 77, 87, 90, 91 or 92% of the tones.
- **Lost hops act as a worse noise floor.** The per-hop weighting of section 8 makes a bad hop
  count as noise, and the 32-hop minimum lets the loss average out.

  | Hops lost out of 32 | Cost in transmit time | Margin that covers it |
  | --- | ---: | --- |
  | One or two | 0.14 to 0.28 dB | any |
  | A quarter | 1.2 dB | 3 dB |
  | Half | 3.0 dB | 6 dB |

  Half lost is an extreme case. A persistent radar is met mainly by enough transmit time to
  overcome the raised noise floor. For a failure probability of 10⁻³ with half the hops lost, a
  block needs 33 hops at 6 dB of margin, 18 at 10 dB and 14 at 13 dB or more. A 3 dB margin would
  need about 500 hops for that case, which is accepted as not covered.
- **OTHR.** Margin alone does not cover a radar: an unhandled 10 kHz sweep 25 dB above the passband
  noise uses up 20 dB. With the conditioning of section 8, simulated loss was at most 1.8 dB for
  coherent radars from 0 to 60 dB above the noise at duties of 7–70%. Budget 1–2 dB of the margin
  for one radar and 3–4 dB for harder cases. A strong radar whose sweeps are not coherent at the
  receiver is not covered above about 20 dB over the noise.
- **Below 9 kHz** the interference is sferics and mains harmonics, not radar. The conditioning of
  section 8 is of the same kind as established practice there (wideband blanking or clipping worth
  about 20 dB at 8 kHz, hum removal) but has not been run on that noise. Harmonics near 8 kHz
  wander by 2.5–10 Hz rms; on the steadier grids about 13 Hz stays clear of the 50 Hz and 60 Hz
  families together, which holds sixteen positions 0.78125 Hz apart
  ([12.2](#122-phase-stable-paths-below-9-khz)). Where a railway supply at 16.7 Hz is in range
  no such slot exists: its harmonics are 16.7 Hz apart and one lies near 8,266.7 Hz. A line that
  sits on one of sixteen positions for a while is then left to the excision of section 8 and to
  the margin.
- **Block span.** At least ten times the longest burst to be bridged, and at least 1.3 to 1.8
  times the longest outage to be survived. Outages of hours need repeated blocks, which is a
  layer above this format.

## 10. Sizing and expected duration

A block is sized by its presence test: a false-presence rate of 10⁻⁶ over all hypotheses and
messages, and a miss rate of 10⁻³. For weak tones that is a total deflection of about 100 for
10 bits and 127 for 24 bits with 400 hypotheses, and the number of tones is that deflection
divided by the deflection per tone. A block has at least 32 tones, and with FHSS at least 32
hops.

**Ten bits against one bit.** Transmit time against a single bit at the same confidence:

| | 10 bits | 24 bits |
| --- | ---: | ---: |
| This sizing, 1 to 4,000 hypotheses, block of 1 to 1,024 tones | 1.10–1.30× | 1.25–1.73× |
| Message error rate alone, 10⁻² / 10⁻³ / 10⁻⁴, ideal orthogonal signalling | 2.48 / 2.01 / 1.77× | – |

The keyed code adds about 0.2 dB at 10 bits. A 24-bit block takes about 27% longer than a 10-bit
block.

**Per-tone reception, one 10-bit block** (Doppler centre known, no interference, no margin).
The one-cell columns were calculated before the 32-hop rule, which sets a floor of 13 s with
0.4 s hops and 21 minutes with 40 s hops:

| C/N₀ | Path | 1 cell per tone, 0.4 s hop | 1 cell per tone, 4 s hop | Held tone, 0.4 s | Held tone, 4 s |
| ---: | --- | --- | --- | --- | --- |
| +10 dB-Hz | steady to 1 Hz | 16 s | 13–14 s | 13 s | 2 min |
| 0 dB-Hz | steady to 1 Hz | 14–15 min | 12 min | 8–9 min | 2–5 min |
| 0 dB-Hz | 10 Hz | 48 min | 40 min | 43 min | 32 min |
| −10 dB-Hz | 0.1 Hz | 22 h | 18 h | 11 h | 69 min |
| −10 dB-Hz | 1 Hz | 23 h | 19 h | 13 h | 5.9 h |
| −10 dB-Hz | 10 Hz | 3.2 d | 2.7 d | 2.9 d | 2.2 d |
| −20 dB-Hz | 0.1 Hz | 91 d | 75 d | 45 d | 4.0 d |
| −20 dB-Hz | 1 Hz | 94 d | 79 d | 53 d | 24 d |

Halving the power quadruples these times below about 0 dB-Hz.

**Held tones on a steady path with FHSS** (a new phase every hop, centres known, 4,000
hypotheses, exact chi-square tails). `*` marks a block held at 32 tones:

| C/N₀ | 0.4 s | 4 s | 40 s | 400 s | 4,000 s | No FHSS, coherent block |
| ---: | --- | --- | --- | --- | --- | --- |
| +10 dB-Hz | 13 s | 2 min * | 21 min * | 3.6 h * | 36 h * | 6 s |
| 0 dB-Hz | 9 min | 2 min * | 21 min * | 3.6 h * | 36 h * | 57 s |
| −10 dB-Hz | 12 h | 63 min | 21 min * | 3.6 h * | 36 h * | 9 min |
| −20 dB-Hz | 48 d | 3.4 d | 9.8 h | 3.6 h * | 36 h * | 95 min |
| −30 dB-Hz | 13 yr | 335 d | 32 d | 4.1 d | 36 h * | 16 h |

On fading paths a 40 s held tone is already enough for 0.1 Hz of spread and more: 2.3 days at
−20 dB-Hz against 2.2 days for any longer tone.

**Coherent attempts without FHSS on a phase-stable path.** The block is sized for a configured
coherence window, the shortest the link must survive; anything steadier is margin. One 10-bit
block on 16 positions, 4,000 hypotheses (24 bits take about 25% longer):

| C/N₀ | Per-tone only, tones of 2 cells / 250 cells | Coherent windows of 22 min | of 5.8 h | Whole block coherent |
| ---: | --- | --- | --- | --- |
| −10 dB-Hz | 11 h / 17 min | – | – | 10 min |
| −20 dB-Hz | 44 d / 11 h | 2.2 h | – | 1.7 h |
| −30 dB-Hz | 12 yr / 36 d | 2.0 d | 23 h | 17 h |
| −37 dB-Hz | 300 yr / 2 yr | 32 d | 6.1 d | 3.5 d |

With coherent windows, halving the power doubles the time, down to the level where a window no
longer holds enough energy. On a path with spread, 5 s windows at −20 dB-Hz bring 0.1 Hz from
12.3 dB to 1.9 dB of the bound and 1 Hz from 3.1 to 0.3 dB.

**The sub-9 kHz configuration against this table.** Its shortest block is 64 tones of 10.24 s,
10.9 minutes, which is about the coherent block at −10 dB-Hz; a stronger link is not served
faster unless it is configured with one-cell tones (82 s). Its per-tone fallback lies between the
two per-tone columns, whose tones are 0.32 s and 40 s. The table was not recomputed for 10.24 s
tones. What a spread path costs the coherent attempts is in
[12.2](#122-phase-stable-paths-below-9-khz): 3 to 10 dB of block energy from the best to the
worst sub-9 kHz spread bound, at 1% wrong decodes and without the presence test.

## 11. Concealment

All figures are the extra C/N₀ an observer without the key needs over the keyed receiver to
notice one 10-bit block, with detection probability 0.9 at a false-alarm rate of 10⁻². The
observer is idealised: it knows the lattice, band, tone and hop timing, covers every hop channel
at the tone's resolution and knows its noise level. Steady path; block sized for its level.

**One-cell tones are concealed by their keyed placement, and keyed hopping adds to it.** These
figures are for observers that measure energy; they hold against an observer on a phase-stable
path only because of the mandatory keyed phase per tone ([4.4](#44-phase-key)). With 400 s hops:

| C/N₀ (block) | Hop known, or no FHSS | 50 hop channels | 1,000 | 10,000 |
| --- | ---: | ---: | ---: | ---: |
| −10 dB-Hz (19 h, 168 hops) | 8.1 dB | 14.8 dB | over 18 dB | over 18 dB |
| −20 dB-Hz (77 d, 16,700 hops) | 8.2 dB | 16.6 dB | 22.5 dB | 25.5 dB |
| Square-root ideal | 8.2 dB | 16.7 dB | 23.2 dB | 28.2 dB |

- The first 8 dB is the 352 keyed positions. Hopping adds the square root of the channel count
  while the observer cannot tell which channel is occupied.
- **Choosing the hop length.** Hopping keeps its benefit to within 0.8 dB while the block has at
  least 13 hops for every hop channel: a hop of at most 103 s with 50 channels and 5 s with
  1,000 at −10 dB-Hz, and 2.8 h and 9 min at −20 dB-Hz. A hop ten times too long gives back
  3.1 dB.
- Without hopping, an observer at the link's own C/N₀ needs about 40 times the block time.
- "Over" marks where the observer would need tones too strong for the weak-tone estimate.

**Fewer, stronger tones are easier to see.** The tone is in one of 352 / 17,600 / 3.5 million
cells (positions times unpredictable hop channels):

| Tones per block | Block energy `E/N₀` | Observer with a filter bank matched to the tone | Observer measuring energy for one hour in 2.2 kHz / in a 1 MHz hop span |
| ---: | ---: | --- | --- |
| 32 | 20.1 dB | 1.6 / 3.8 / 5.7 dB | 19.9 / 33.2 dB |
| 128 | 22.1 dB | 3.9 / 6.6 / 8.8 dB | 18.0 / 31.3 dB |
| 512 | 24.5 dB | 5.8 / 9.2 / 11.6 dB | 15.6 / 28.9 dB |
| 2,048 | 27.1 dB | 7.5 / 11.5 / 14.2 dB | 13.0 / 26.3 dB |
| 8,192 | 29.9 dB | 7.9 / 13.7 / 16.8 dB | 10.1 / 23.4 dB |

Long held tones help against energy detection, because the link needs less power, and hurt
against a matched filter bank, because each is a strong narrow line. The held tone that
maximises sensitivity (32 per block) leaves the key 2 to 6 dB.

**More frequent phase steps restore the concealment of a held tone.** The mandatory one phase
per held tone does not help against a filter bank. 32 held tones per block, same cell counts:

| Held tone | One phase per tone | Phase step every cell | Every 2 cells | Every 4 | Every 8 |
| --- | --- | --- | --- | --- | --- |
| 4 s | 1.6–5.7 dB | 8–10 dB | 6.5–9 dB | 5.5–8 dB | 4.5–7 dB |
| 40 s | 1.6–5.7 dB | 12–14 dB | 11–13 dB | 9.5–11.5 dB | 8–10 dB |
| 400 s | 1.6–5.7 dB | 17–19 dB | 16–17.5 dB | 14–16 dB | 13–14.5 dB |

These are for an observer that adds the energies of a tone's phase intervals and takes the
largest; allow about 2 dB for a better statistic. Two phase intervals per tone gain only 0.5 to
0.7 dB.

**Where the keyed receiver adds coherently, the gap grows with the coherent time.** The keyed
receiver needs a fixed total energy (about 57 times the noise density for a 10-bit block), and
an observer measuring energy needs an amount that grows with the square root of the cells
observed. For 4 positions and a block that is coherent for one hour that is about 13 dB, rising
5 dB per tenfold of block time. Hand estimate from the figures of section 10 and calculation 14;
not computed.

**Below 9 kHz concealment is relaxed, and was not evaluated.** The configuration has sixteen
positions, no hopping and 10.24 s tones with one keyed phase each: a narrow, strong line to an
observer with a filter bank. The table above gives such an observer 1.6 dB for 32 tones per
block hidden among 352 positions; here a tone hides among 16. That is accepted
([section 1](#1-scope-and-fixed-decisions)). What remains is what is free: the mandatory keyed
phase against a coherent observer, and the low power a coherent link needs. Phase steps inside
the tone would restore concealment against a filter bank, but they cost timing hypotheses and
are not offered there.

## 12. Paths other than mid-latitude HF

### 12.1 Paths the format serves

The receiver adapts to the path by its spread hypothesis and its coherent attempts. Extra
transmit time against the bound at −10 dB-Hz, held tone of 2 cells in a 0.4 s hop unless stated:

| Path | Spread | Gap | What else it needs |
| --- | --- | ---: | --- |
| HF Worst (30 Hz, 7 ms) | 30 Hz | 1.1 dB | nothing |
| Aurora, flutter | 50–500 Hz, with offset | 1.0–1.1 dB | spread hypotheses up to 500 Hz |
| Earth–Moon–Earth | about 20 Hz per GHz | 1.0–2.4 dB | the predicted Doppler shift (up to ±20 kHz and 1.3 Hz/s at 5.76 GHz) removed before the transform |
| Troposcatter at 0.9–5.8 GHz | roughly 0.3–20 Hz (an extrapolation from 30 MHz data) | 6.4 dB at 0.5 Hz, 2.4 dB at 3 Hz, 1.5 dB at 10 Hz | nothing; where hopping is not imposed, a longer tone recovers the slow end |
| Troposcatter at 50 MHz, up to about 300 km | about 0.01–0.1 Hz (inferred, not measured) | not computed | a held tone of seconds or more; served by the format, not yet a configured path class ([12.3](#123-meteor-burst-and-troposcatter-at-50-mhz)) |
| Phase-stable (sub-9 kHz, daytime groundwave), per-tone reception, no FHSS | below 1 mHz | 18 dB (28 at −20, 38 at −30 dB-Hz) | this is only the fallback, computed for 0.32 s tones. With the 10.24 s tones used below 9 kHz it needs 8.9 dB more block energy than a coherent block (another measure: 1% wrong decodes over 87 minutes, [12.2](#122-phase-stable-paths-below-9-khz)) |
| The same, coherent windows of 1 min / 22 min / 5.8 h | below 1 mHz | 1.4 / 0.2 / 0.0 dB (5.7 / 1.0 / 0.1 at −20; 14.1 / 4.4 / 0.7 at −30) | both references and the path steady over the window |

- **Wide spreads are the easy direction for weak signals.** With the spread bank stopping at
  60 Hz the loss would be 1.6 dB at 100 Hz, 3.6 dB at 200 Hz and 7.2 dB at 500 Hz, hence the
  bank to 500 Hz.
- **Strong signals on very wide spreads are limited in rate, not in sensitivity.** This modem
  does not chase that case.

### 12.2 Phase-stable paths below 9 kHz

Sources, calculations and limits are in the
[sub-9 kHz rate and spacing note](../_ref/algorithms-matrixModem/Fable-5.2/sub9khz-fsk-rate-and-spacing.md).
The case worked there is a 61 m transmit antenna fed with 10 kW near 8.27 kHz.

- **Phase-stable paths use FHSS off, a narrow band, held tones and the receiver's coherent
  windows.** In the records gathered by the
  [earlier research](../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/10-sub9khz-propagation.md),
  a path below 9 kHz held phase within about ±10° for hours around midday, wandered by tens of
  degrees at night and stepped by about 40° at sunrise. Coherent amateur links there run at −15 to
  −37 dB-Hz with references locked to GPS at both ends. A later search sharpened this: within
  one day or night state the rms phase is about 5 to 10° by day and 15 to 20° at night on 5 to
  10 Mm at 8 to 10 kHz, one constant phase has served decodes of 9.7 and 12.2 hours, and the
  largest phase rates are brief and deterministic (0.03 to 0.3 mHz at the terminator, 0.35 to
  2.3 mHz in flares and one great storm, scaled from higher frequencies). The Sub9 presets of
  1, 5 and 50 mHz are stress values 50 times and more above the quiet path.
- **Configuration** (adopted on 28 November 2026; [section 14](#14-decisions)):

  | Setting | Value | Alternatives |
  | --- | --- | --- |
  | FHSS | Off | – |
  | Lattice scale | `j` = 3: 1.28 s cell, positions 0.78125 Hz apart | `j` = 2 (1.5625 Hz) where one mains family matters and the antenna is 86 Hz or wider; `j` = 4 (0.39 Hz) for an antenna of 20 Hz or less |
  | Positions and span | 16 positions in 12.5 Hz, for example from 8,259.375 Hz | 16 in 25 Hz (`j` = 2); 16 in 6.25 Hz (`j` = 4) |
  | Tone length | 8 cells = 10.24 s (0.098 tones per second) | 1 cell (1.28 s) as the fastest; 32 cells (41 s) for blocks longer than about 3 h |
  | Shortest block | 64 tones = 10.9 minutes | 82 s with one-cell tones; 44 minutes with 41 s tones |
  | Phase key | One per tone, reached by an 80 ms ramp ([4.4](#44-phase-key)) | 160 ms ramp for an antenna narrower than about 40 Hz |
  | Receiver | Cells inside a tone, per-tone reception, and coherent windows from two tones to the block, with a reference that follows the ramp | – |
  | References | GPS-locked at both ends, corrections logged or the audio re-timed from the PPS | A lesser reference is searched, within the ceiling below |

  - **What it gives up.** A strong link is not served faster than the shortest block. A 10 s
    tone is conspicuous to an observer with a filter bank ([section 11](#11-concealment)). It
    is a second cell length beside the default 160 ms, and it departs from the preference for
    the fastest tone rate ([4.2](#42-tone-length)).
  - **Why slower tones were chosen although the antenna would pass faster ones.** With the
    right window the sensitivity is the same; the 10 s tone makes the worst spread bound a case
    for plain per-tone reception, and divides the work of every coherent decode by 64 against
    the default cell.

- **At 10 kW the antenna is voltage-limited, and the power buys bandwidth.** The top voltage `V`
  fixes the current and the radiated power: 5.5 mW (thin-wire T at 29 kV) to about 0.3 W (a
  2.5 nF tower at 100 kV) at 8.27 kHz. The −3 dB bandwidth that uses all 10 kW is
  `P / (2π·C·V²)`: 64 to 176 Hz at 100 kV for 2.5 to 0.9 nF, 189 to 525 Hz at 58 kV, and 16 to
  44 Hz only at 200 kV. A lower-loss antenna cannot take the power: under the earlier antenna
  note's loss cases a 61 m antenna passes 18.5 to 620 Hz at 8.27 kHz (6 Hz at 2.97 kHz), and its
  low-loss cases would stand at 111 to 309 kV with 10 kW. Theoretical; the voltage limit of a real 61 m installation is the
  largest unknown.
- **The antenna limits the span, not the tone rate.** Coherent loss with one phase for all
  positions, antenna unknown to the receiver:

  | Format | Antenna 86 Hz | 57 Hz | 24 Hz |
  | --- | ---: | ---: | ---: |
  | Default lattice, 0.16 s tone, 8 positions in 50 Hz | 1.0 dB | 1.9 dB | 5.6 dB |
  | Default lattice, 0.16 s tone, 4 positions in 25 Hz (plus 0.8 dB for four positions) | 0.4 dB | 0.8 dB | 2.6 dB |
  | `j` = 2, 16 positions in 25 Hz | 0.3 dB | 0.6 dB | 2.4 dB |
  | `j` = 3, 16 positions in 12.5 Hz, 10.24 s tone | 0.1 dB | 0.1 dB | 0.7 dB |
  | `j` = 4, 16 positions in 6.25 Hz | 0.0 dB | 0.1 dB | 0.2 dB |

  Slow detuning (wind, sag, temperature) turns all positions by nearly the same phase,
  `atan(2d/B)` for a resonance shift `d`: 5° and 21° at 86 Hz for capacitance changes of 10⁻³
  and 4×10⁻³, 19° and 54° at 24 Hz. The receiver sees it as path wander.
- **A keyed phase step is a transient the transmitter may not tolerate, hence the ramp.** VLF
  practice keys phase-continuously for that reason. At 10 kW and 86 Hz the antenna stores
  18.5 J. After a half turn the antenna current opposes the drive, and nearly the whole 10 kW
  flows back into the source for 2.6 ms (11 J; 9.2 ms and 41 J at 24 Hz). With the 80 ms ramp
  of [4.4](#44-phase-key) nothing flows back and the drive and the current stay within 16° of
  each other at 86 Hz and 49° at 24 Hz. A receiver whose reference follows the ramp loses
  nothing. Calculated for a single-tuned antenna; no transmitter was examined.
- **Mains harmonics leave about 13 Hz.** Near 8.27 kHz the 50 Hz harmonics (8,250 and 8,300 Hz)
  and the 60 Hz harmonics (8,220, 8,280, 8,340 Hz) wander by about 3 Hz rms on the steadier
  grids. Three standard deviations from both leaves 8,259 to 8,272.5 Hz. From grid-frequency
  statistics; no line amplitude near 8 kHz was measured.
- **Railway supply at 16.7 Hz leaves no slot.** Its harmonics are 16.7 Hz apart, near 8,250.0,
  8,266.7 and 8,283.3 Hz. If they wander as the mains harmonics do, no span of 12.5 Hz or even
  6.25 Hz is clear of them at three standard deviations where such a railway is in range
  (Germany, Austria, Switzerland, Sweden, Norway). The amateur convention of 8,270 Hz is 3 Hz
  from the nominal line; the earlier research records only that carriers are placed off the
  railway harmonics, and no line amplitude was found. A line on one of sixteen positions is
  left to excision and margin ([section 9](#9-margin-interference-and-lost-hops)).
- **The tone length does not change the sensitivity; it sets the floor and the work.** Block
  `E/N₀` at 1% wrong decodes for the 10-bit block over 87 minutes, the same within the
  simulation's scatter in every format with eight or more positions and tones from 0.16 s to
  41 s (simulated, known timing and frequency):

  | Path | Block `E/N₀` | Best coherent window |
  | --- | ---: | --- |
  | Steady | 12.9 dB | whole block |
  | Phase wander 30° rms, 1 h correlation | 13.4 dB | whole block |
  | Frequency wander within ±1 mHz (Sub9 best) | 16.2 dB | 5.5 min |
  | Within ±5 mHz (Sub9 moderate) | 18.4 dB | 82 s |
  | Within ±50 mHz (Sub9 worst) | 22.5 dB | 10 s |

  Per-tone reception alone needs 27.4, 24.5, 21.7 and 19.2 dB on the steady path with tones of
  0.64, 2.56, 10.24 and 41 s. A 10.24 s tone is the best window under the worst bound, so that
  case needs no coherence between tones.
- **Spacing equal to the reciprocal of a long tone is not offered.** Positions 0.098 Hz apart
  for 10.24 s tones decode as well up to the worst spread bound but have no shorter window: at
  0.5 Hz of spread they did not decode at any energy, where 0.78125 Hz spacing decoded at
  27.4 dB. They also tolerate only ±6 ppm of frequency error.
- **Below 9 kHz the search volume, not the sample rate, is the computational load.** Each
  frequency bin, drift-rate bin and timing offset is a block decode. Bins are half of 1 ÷ window
  apart (0.9 dB worst loss) and drift-rate hypotheses 3.9 bins of drift apart. Frequency ×
  drift-rate bins per coherent window, at 9 kHz, for assumed reference classes
  ([calculation 18](algorithms-matrixModem/fsk-lattice/results/lattice_receiver_section18.md)):

  | Reference at each end | 22 min | 1 h | 5.8 h |
  | --- | ---: | ---: | ---: |
  | GPS-disciplined, corrections logged | 1 × 1 | 1 × 1 | 1 × 1 |
  | GPS-disciplined, corrections slewed unseen at up to 1 ppm | 48 × 3 | 130 × 17 | 752 × 562 |
  | TCXO, ±1 ppm, 0.1 ppm/h | 48 × 1 | 130 × 2 | 752 × 57 |
  | Crystal, ±20 ppm (±0.18 Hz), 1 ppm/h | 951 × 3 | 2,592 × 17 | 15,034 × 562 |
  | Sound-card clock, ±100 ppm (±0.9 Hz), 5 ppm/h | 4,752 × 12 | 12,960 × 84 | 75,168 × 2,809 |
  | ±5 Hz, 5 ppm/h | 26,400 × 12 | 72,000 × 84 | 417,600 × 2,809 |

  - The volume grows with the cube of the window: frequency bins with the window, drift-rate
    bins with its square.
  - Timing multiplies it. Per-tone reception needs offsets an eighth of the phase interval
    apart. Coherent sums over tones on different positions need 1/(8 × span), which is 10 ms
    for a 12.5 Hz span and governs below 9 kHz: 20 offsets for ±0.1 s and 200 for ±1 s,
    whatever the tone length, and twice as many for 25 Hz.
  - A sample clock not tied to the carrier reference multiplies these by up to 200 at 1 h.
- **One decode follows the tone rate.** A decode on the full ladder costs one lookup per
  message and tone (measured: 3.3 thousand million message-tones a second on the 12-thread
  Ryzen 5 PRO 5650U, 120 on the RTX PRO 6000 Blackwell). The number of 24-bit decodes that fit
  into a block's own duration is therefore the same for any block length:

  | Device | 0.16 s tone | 0.64 s | 2.56 s | 10.24 s | 41 s |
  | --- | ---: | ---: | ---: | ---: | ---: |
  | Ryzen 5 PRO 5650U, 12 threads | 32 | 129 | 513 | 2,067 | 8,044 |
  | RTX PRO 6000 Blackwell | 1,198 | 4,741 | 18,378 | 74,483 | 288,546 |

  On the laptop a 16-bit block fits 8,180 to 2.1 million and a 10-bit block 459,000 to
  130 million (measured; the GPU was timed for 24 bits only). With logged GPS references one
  decode per block is all the search there is, and every device keeps up at every tone length.
- **With a lesser reference the search is a transform across tone sums, not a decode per
  hypothesis** ([section 8](#8-receiver-requirements), item 7). An 87-minute whole-block window
  at 8.27 kHz in a 12.5 Hz span needs 5,742 hypotheses with unseen slewing, 13,920 for a TCXO
  with ±0.1 s of timing, 23 million for a crystal and 570 million for a sound-card clock with
  ±1 s. Measured for the TCXO case and a 24-bit block of 512 tones, with every rung of the
  ladder searched on the bins its length resolves:

  | Route | Search time | Block duration ÷ search time |
  | --- | ---: | ---: |
  | Laptop, a decode per hypothesis | 35,300 s | 0.15 |
  | Laptop, FFT across tone sums (FFTW, 12 threads) | 1,624 s | 3.2 |
  | GPU, a decode per hypothesis | 980 s | 5.4 |
  | GPU, cuFFT across tone sums | 83 s | 63 |
  | GPU, FP16 matrix product across tone sums on Tensor Cores | 11.4 s | 461 |

  - The matrix product ran at 404 nominal TFLOP/s; over the whole pipeline the GPU showed 99%
    activity and 60% Tensor Core activity. With 8-bit integer operands the same library gave
    223 and 35%, and 18.9 s.
  - Block duration ÷ search time for the other assumed classes, scaled from that measurement
    (at or above 1 the device keeps up):

    | Reference at each end | Laptop, 24 bits | GPU, 24 bits | Laptop, 16 bits | GPU, 16 bits | Laptop, 10 bits |
    | --- | ---: | ---: | ---: | ---: | ---: |
    | Slewed unseen at 1 ppm | 18 | 1,900 | 4,550 | 488,000 | 291,000 |
    | TCXO, ±1 ppm, ±0.1 s (24 bits measured) | 3.2 | 461 | 827 | 118,000 | 52,900 |
    | Crystal, ±20 ppm, ±1 s | 0.013 | 0.47 | 3.3 | 119 | 208 |
    | Sound-card clock, ±100 ppm, ±1 s | 0.0006 | 0.02 | 0.16 | 5.9 | 10 |

  - A 24-bit block with a free-running crystal is about twice too slow on one GPU and 80 times
    on the laptop; with 16 bits both keep up. A sound-card clock is in reach for 10 bits on the
    laptop and 16 bits on the GPU. A 5.8 h window needs 53 to 64 times more hypotheses.
  - A single hypothesis is still cheapest as one plain decode; the transform pays from some
    tens of hypotheses on the laptop and about ten on the GPU. The scheduler chooses.
  - With the 10.24 s tone the tone sums are the transform's input as they stand. Shorter tones
    are summed into sub-blocks first, and the gather then grows with the tone rate; that case
    was not built. The benchmark has synthetic bin values and stock kernels and leaves out the
    forming of bin values per timing offset and the presence test.
- **Clock corrections must not add unknown drift or steps.** A GPS correction slewed at 1 ppm
  moves a 9 kHz carrier by 9 mHz, about 32 one-hour bins, and a correction stepped by 10 µs is a
  32° phase jump at 9 kHz. Proposed rule: no clock or phase steps inside a block at either end;
  corrections are slewed smoothly within a stated bound, or measured, logged and removed in the
  receiver's processing as a known trajectory. The bound is open. Re-timing the recorded audio
  from a recorded GPS pulse, as the amateur links below 9 kHz do, leaves under 50 ns.

### 12.3 Meteor burst and troposcatter at 50 MHz

Meteor burst is an exceptional path and is not supported by this draft. It was researched and
computed to decide whether it should be; sources and calculations are in the
[meteor-burst note](../_ref/algorithms-matrixModem/Fable-5.2/meteor-burst.md). Figures are for
50 MHz, 100 mW and galactic noise unless stated. Troposcatter is treated here because it
competes with meteor burst at short range.

- **Underdense trails are out of reach.** The strongest gives a peak of −6 dB-Hz between
  isotropic antennas over 1,300 km (−23 dB-Hz at 2 mW), and none delivers one tone's worth of
  energy. They need about 17 dBi at each end to give 23 to 73 one-tone bursts an hour.
  Operational systems use 300 W to 2 kW; no published result at 10 W or less was found.
- **Overdense trails are the working case, and they suit this FSK design.** They are under 5%
  of trails and carry about 30% of throughput on classic links, more as power falls. They
  last seconds, fade 1 to 10 times a second and keep a delay spread of microseconds, so a
  2.2 kHz signal meets Doppler spread and shift only: the diffuse case this specification
  assumes.
  - Relying on them instead of underdense trails means less transmit power, hence less to
    notice near the transmitter, and practical antennas.
  - On a single long path the level is too low without gain: over 1,300 km a strong overdense
    trail carries about one tone's worth for 100 mW, and 10 dBi at each end is needed for a
    block's worth
    ([calculation](algorithms-matrixModem/meteor-burst/results/meteor_burst.md), section 5).
- **The whole shared sky at a few hundred kilometres is the favourable geometry**
  ([calculation](algorithms-matrixModem/meteor-burst/results/meteor_sky.md)). With a receiver
  that has gain in every direction, usable trails an hour at 50 to 500 km:

  | Power, receive gain | A tone's worth | A block's worth | Block with 6 dB for fading |
  | --- | ---: | ---: | ---: |
  | 100 mW, none | 8–9 | about 2 | 0.1–0.5 |
  | 100 mW, 10 dBi | 37–43 | 11–13 | 4 |
  | 100 mW, 20 dBi | about 145 | 50–57 | 20–24 |
  | 2 mW, 20 dBi | 12–15 | 3–4 | about 1 |

  The figures hardly change from 50 to 500 km and fall quickly beyond: 1,300 km gives 100 mW
  without gain 0.6 tone-worth trails an hour. 2 mW without receive gain gets no block.
- **Up to about 300 km troposcatter is the better path at 50 MHz.** It is continuous: about
  +13 to +27 dB-Hz at 150 km and −2 to +11 dB-Hz at 300 km for 100 mW without antenna gain
  (median), which is inside this specification's range and needs no change of format. At
  500 km it falls to −16 to −6 dB-Hz and meteor trails win. It fades slowly (0.01–0.1 Hz), so
  a held tone fits it better than the one-cell tone. No prediction method is validated at 50 MHz and no
  measurement was found. Its footprint is wide where a trail's is a strip of about 5 × 40 km.
- **What limits these figures.** The published meteor flux law is continued two decades past
  where it is established. Fading inside an echo is not modelled, hence the column with 6 dB
  allowed. Receive gain is assumed down to 3° elevation. On long paths the length of real
  echoes is poorly established and changes a strong trail's energy by 10 to 20 dB; below
  500 km echoes are short enough for this to matter little. A check against the one measured
  long link came out 2.4 times high, which whole-sky coverage would explain but does not
  validate.
- **What would have to change in this specification.** A per-trail frequency search of about
  ±30 Hz, since the shift exceeds the 6.25 Hz spacing; a cell shorter than 160 ms, which sits
  at the edge of the fading rate; a higher evidence clip; and a block collected over one or
  several trails, with everything between them erased.
- **Probe.** An illuminator near 144 MHz at the transmitting site, received at the far end,
  shares the link's geometry and tells the receiver when a trail is present and how strong.
  A trail is 13.8 dB weaker and 8.3 times shorter-lived there than at 50 MHz. It may reveal
  the site but not the data transmission. A radar looking back along its own line of sight
  sees other trails than the link uses at long range. At short range the two conditions
  converge: a radar at the transmitter also sees 53–57% of the link's block-worth trails at
  50 km between the stations, 17–21% at 150 km and about 10% at 300 km. If only the receiver
  uses the probe, the link stays one-way. No literature on such a probe was found.
- **Why a keyed tone and not PSK.** The reason differs by path. On underdense trails the path
  itself would carry PSK (delay spread under 100 ns for 90% of the time), but no burst at these
  powers holds enough energy for a carrier. On overdense trails it is the fading of 1 to 10
  times a second. On HF it is the delay spread ([4.4](#44-phase-key)).
- **Wideband keyed-phase mode for underdense trails, outside this specification.** A rapid
  keyed phase sequence over a wide band, received by a digitally formed array. Assessed, not
  computed:
  - Underdense trails support phase steps to about a megahertz. It does not carry over to
    overdense trails, which fade and are assured only to about 100 kHz.
  - It keeps full coherence inside a burst, as a held tone does; its gains over a held tone
    are concealment (27 dB lower power density over 1 MHz, a small ground footprint) and the
    array, which gets gain without losing sky coverage.
  - It does not raise sensitivity or throughput: a burst delivers the same energy at any
    bandwidth, and 100 mW would still need about 21 to 31 dBi at the receiver.
  - Each burst needs a delay search of up to a millisecond and a Doppler search of ±30 Hz,
    which is transform and matrix work.

## 13. Computation and simulation

### 13.1 Rules that bind every implementation

Three rules bind the receiver and the simulator alike. A figure for the computational ceiling
counts only if it was obtained under them.

1. **Measure and rebalance.** Every stage that has more than one route is timed on the machine
   it runs on, and the work is redistributed so that the whole job finishes as soon as possible.
   - The routes to be held and timed are those of [13.4](#134-routes-and-scheduling-rule):
     above all CPU FFT against GPU FFT against a Tensor Core matrix-product DFT, for the first
     transform and for the transform across tone sums.
   - What is measured is processing time per unit of work, for every stage, route, portion size
     and device, taken while the real work runs.
   - The criterion is the completion of the whole job: backlog cleared by a live receiver,
     trials finished by a simulation. It is never one stage's rate or one unit's utilization.
   - No fixed assignment is specified, because the best one changes with configuration, batch
     and machine. The matrix product beat cuFFT by 7 in the sub-9 kHz search and lost to it by
     2.1 on small batches of the first transform ([13.2](#132-measured)).
   - Every run records the times it measured and the assignment it chose.
2. **Do no work twice.** Work is done at the outermost loop level at which its inputs change,
   and its result is reused inside. No loop repeats work that its loop variable does not change.
   - **Loop order of the search.** The search is over messages × hypotheses. The enumeration of
     the messages, the 32 symbol values of each, is done once. Hypotheses are an inner,
     vectorised dimension: a transform where they lie on a regular grid (frequency, drift), and
     table entries that are vectors over the hypotheses where they do not (timing fine steps,
     spread hypotheses). They are never a loop around the decode.
   - **Why it is a rule and not a detail.** A hypothesis loop around the decode repeats the
     gather and the whole ladder for every hypothesis. Measured for 13,920 hypotheses: 35,300 s
     against 1,624 s on the laptop and 980 s against 11.4 s on the GPU. The outer loop made the
     ceiling 22 and 86 times worse than it is.
   - **What is done once per what:**

     | Work | Done once per | Reused by |
     | --- | --- | --- |
     | Keys, schedules, symbol values of every message, hypothesis matrices, transform plans | Configuration or block | Everything below |
     | Conditioning and first transform | Cell | Every timing fine step (as a phase slope), spread hypothesis and message |
     | Bin values | Timing offset | Every message, frequency bin and drift |
     | Evidence table entries | Symbol value, for all spread and timing hypotheses together | Every message |
     | Gather of a message's tone values | Message and timing offset | Every frequency bin, drift and rung |
     | De-chirp and transform across tone sums | Message, timing offset and drift | Every frequency bin of the whole block |
     | Transform of a rung's windows | Message and timing offset | That rung's bins, and only those its window length resolves |
     | Decoy-key thresholds | Configuration | Every block, and every trial of a simulation |

   - **Repetitions this draft still contains.** Each has to be removed or shown to be necessary
     before a ceiling is quoted for the case it affects.
     - *Timing offsets* repeat the gather and the transform across tone sums: 20 times below
       9 kHz for ±0.1 s. Keeping a message's tone values apart by position and transforming
       across positions gives every offset at once, from as many partial transforms as there
       are positions. That pays only when the offsets outnumber the positions: below 9 kHz
       from about ±1 s of timing (200 offsets against 16 positions), and never over 2.2 kHz,
       where 352 positions meet 350 fine steps. Derived, not built.
     - *The spread bank and the wide-band fine timing steps* have separate evidence for every
       hypothesis. They cost 32 additions per message and hypothesis, which no route found so
       far avoids; what must not be repeated is the enumeration of the messages. One pass with
       vector entries is derived, not built.
     - *A single hypothesis* is one plain decode, which is the right form for it.
   - **Benchmarks.** A ceiling figure states its work counts against the table above. A figure
     taken with a repeated inner stage is labelled as the cost of that structure, not as the
     ceiling.
3. **Simulation speed is a requirement**, of the same rank as the receiver's ceiling
   ([13.5](#135-simulation)).

### 13.2 Measured

- **Live reception** needs a small fraction of one CPU core for the first transform. The
  receiver's work is the search over messages and hypotheses, laid out as rule 2 requires; it
  is not the hypothesis count times a decode.
- **Measured on a full RTX PRO 6000 Blackwell**, batches of 32,768 two-cell tones:
  - cuFFT and an FP16 matrix-product DFT both exceed 20 million times real time. Which is faster
    depends on the bins wanted and on the batch: at 32,768 per call the matrix product is 1.2
    times faster for the 352 positions and ran at 70% Tensor Core activity; at 4,096 per call,
    which fits the GPU's cache, cuFFT is 2.1 times faster (200 million per second).
  - A full 24-bit decode (16.7 million messages) ran 8,400 times a second as a direct kernel,
    5,000 as an FP16 matrix product and 2,000 as a fast Hadamard transform. That test used 24
    symbols of 64 tones; for the specification's 32 symbols of 256 values the direct kernel ran
    5,900 times a second (a later run on the same GPU type).
  - The coherent ladder over 64 windows ran 36.9 times a second as a direct kernel and 5.9
    times as an FP16 matrix product with 45% Tensor Core activity. A ladder over every window
    from two tones to the block costs one lookup per message and tone: 0.07 s for a block of
    512 tones and 4.4 s for 32,768. On the 12-thread Ryzen 5 PRO 5650U the same decodes take
    2.5 s and 166 s, and the per-tone decode runs 12 times a second
    ([12.2](#122-phase-stable-paths-below-9-khz)).
  - **The frequency and drift search as one matrix product is where Tensor Cores carry the
    coherent attempts.** Messages × tones times tones × hypotheses is a dense product; the
    search over messages as a matrix product is one-hot and wastes 255 of 256 products, which is
    why the 64-window figure above is slow. For a 24-bit block of 512 tones with 13,920
    hypotheses on the whole block the search took 11.4 s as an FP16 product (404 nominal
    TFLOP/s; 60% Tensor Core activity and 99% GPU activity over gather, product and epilogue),
    18.9 s with 8-bit integer operands, 83 s by cuFFT and 980 s as a decode per hypothesis. The
    product was 69% of the pipeline's time, the epilogue 24% and the gather 7%. FFTW on the
    12-thread laptop CPU took 1,624 s, against 35,300 s as a decode per hypothesis.
  - A sample-level simulation, noise generation included, ran at 7–12 million times real time: a
    day of signal in about 10 ms.
  - With a search running on the same GPU, every GPU transform route fell to the same 7–14
    million per second. The GPU's time was divided almost without loss (shares summing to
    0.95–1.03), so a rate measured while sharing does not show which route is cheaper.
  - A prototype of the scheduling rule below cleared five queued workloads in 0.94 to 1.12 times
    the time of the best fixed assignment for each, from a cold start. Every fixed assignment
    tried was 2 to 73 times slower than the best in at least one of them.
### 13.3 What the settings do to the work

- **What the settings do to the work** (reasoned from the receiver's structure and the
  measurements above; not measured for these configurations):
  - The search over messages is the same in every case, and is where the ceiling sits. The
    hypotheses over frequency and drift ride on it as a transform and must not repeat it.
  - Hop length does not enter the work with one-cell tones.
  - Tone length does. One-cell tones make a block of about 420,000 tones at −10 dB-Hz where held
    tones make 32 to a few hundred, so there is far more transform and accumulation work, in the
    stage with the most headroom. They need 20 ms timing spacing where a held tone needs an
    eighth of its length.
  - Phase steps inside a held tone multiply its timing hypotheses by the phase intervals per
    tone, per timing dimension, where timing is uncertain. The mandatory one phase per tone
    adds no work.
  - Below 9 kHz the work is the frequency, drift-rate and timing search of
    [12.2](#122-phase-stable-paths-below-9-khz), not the first transform. That search is itself
    a transform across tone sums, so the same routes serve both stages and the balance between
    them shifts with the configuration: at HF the first transform is the bulk and the search is
    small, below 9 kHz the first transform is negligible and the search is everything.
  - Coherent attempts add two table columns per window: 128 times the decode work for a ladder
    down to 1/64 of the block (about 65 full 24-bit decodes a second by extrapolation, 36.9
    measured for the specification's block shape). A ladder from two tones up follows the tone
    count instead. Over 2.2 kHz the specular search adds about 350 fine timing steps for each
    coarse one, which is not measured.
### 13.4 Routes and scheduling rule

- **Routes the scheduler has to hold.** For the first transform: CPU FFT, GPU FFT, and a
  matrix-product DFT on Tensor Cores when only some bins are wanted. For the search over
  messages: lookups in evidence tables, on CPU or GPU. For the search over frequency and drift:
  a plain decode per hypothesis when there are few, and otherwise a transform across tone sums
  by CPU FFT, GPU FFT or one Tensor Core matrix product, followed by its epilogue. Portions of
  the last kind are batches of messages for one timing offset. In simulation also: signal and
  noise generation on CPU or GPU, with trials as the portions.
- **Scheduling rule.** No fixed routing table is specified. The receiver, and the simulator in
  the same way, chooses its routes while it runs:
  1. Work is cut into portions (batches of tones, batches of hypotheses) whose results do not
     depend on the route, so every probe is real work and its output is kept.
  2. The portion size is tuned with the route; the fastest route changes with it.
  3. The scheduler measures cost, not rate: one of its portions at a time has each device, and the
     time each takes is kept as a running cost per unit of work for every stage, route, portion
     size and device. If something else shares the device, that time includes the wait and is
     still the cost to use.
  4. About 0.5 s in every 10 s goes to portions run by routes and sizes other than the current
     best, to keep the costs fresh.
  5. Each stage's work on a device goes by its cheapest route and size. A free executor takes the
     oldest portion of the stage in which it is least disadvantaged against the other devices, and
     leaves it alone when the others would clear all queued work sooner than it would finish that
     portion. No stage runs ahead of what the next can consume. The CPU worker budget is never
     reduced.
  6. Predicted and actual backlog cleared are compared every cycle; a persistent gap triggers an
     end-to-end comparison of the two best assignments. The measure of success is the backlog the
     complete receiver clears, or the trials a simulation finishes, never one stage's rate or
     one unit's utilization.
  7. When no full-fidelity assignment keeps up, the receiver may give up at most 2 dB of
     sensitivity, charged to the margin and reported: first by computing only the position bins
     (0 dB up to 1 Hz of spread, 0.3 dB at 3 Hz, 1.5 dB from 10 Hz), then by shedding tones evenly
     by key (1.0 dB for 1.6 times the throughput, 1.5 dB for 2 times, 2.0 dB for 2.5 times). It
     never drops hypotheses or coherent attempts. Beyond the limit it reports overload.
### 13.5 Simulation

Experiments have to be quick, so simulation speed is a design objective of the same rank as the
receiver's ceiling. Transmitter format and receiver structure are chosen with it in view.

- **One compiled implementation with two input modes.** The cell mode draws bin statistics
  directly and is exact for a linear front end under the rules of section 3. The sample mode
  forms samples and is required for anything that involves conditioning, the 4-bit sample code
  or timing error. The cell mode is used wherever it is valid.
- **Experiment machines.** Two kinds are the reference for simulation speed: a machine with 256
  logical CPUs and no GPU, and a machine with 32 logical CPUs and one RTX PRO 6000 Blackwell.
- **Layout of the work.**
  - A trial, one block with its noise, is a portion. Trials are independent and share only
    read-only data.
  - On the 256-CPU machine every worker runs whole trials with one numerical thread, and there
    are more software workers than hardware threads so that preparing the next trials overlaps
    the running ones.
  - On the GPU machine the dense stages batch across trials as they batch across messages, the
    CPU workers prepare the next batches while the GPU runs, and generation moves to the GPU
    where it is measured to be cheaper there.
  - Rule 1 applies as in the receiver: the routes are timed on that machine and the work is
    rebalanced between CPU and GPU so that the experiment finishes soonest.
- **Rule 2 in simulation.** Everything in the first row of the table of
  [13.1](#131-rules-that-bind-every-implementation) and the decoy-key thresholds are computed
  once per configuration, not per trial. One noise realisation may serve every signal level of
  a sweep. A trial runs the full hypothesis search only when the experiment's question needs
  it: an experiment about the signal's statistic at known timing and frequency scores every
  message on the known hypothesis, and an experiment about acquisition or false presence runs
  the search. The report says which.
- **Every run reports** trials per second, the ratio to real time, processing time per stage,
  the assignment chosen, and CPU and Tensor Core utilization. A run that leaves most of the
  machine idle is reported as a finding to act on.
- **Budget.** Sample budget, uncertainty target and stopping rule are declared before the run.
- **What a trial costs with the kernels measured so far**, for one 87-minute block of 512 tones:

  | Trial | Laptop, 12 threads | Pod CPU share | RTX PRO 6000 Blackwell |
  | --- | ---: | ---: | ---: |
  | 24 bits, one hypothesis | 2.5 s | 0.48 s (13 threads of an EPYC 9355) | 0.07 s |
  | 24 bits, ±1 ppm search | 1,624 s | 868 s (27 threads of an EPYC 9554, plain transform) | 11.4 s |
  | 10 bits, one hypothesis | 0.17 ms | not timed | not timed |
  | 10 bits, ±1 ppm search (scaled from 24 bits) | 0.10 s | 0.05 s | 0.7 ms |

  A sweep of 2,000 trials of 24-bit blocks with the ±1 ppm search is 6.3 hours on one GPU and
  is not a quick experiment; with 10-bit blocks it is seconds. Sample-level generation ran at
  7 to 12 million times real time on the GPU.
- **Not measured.** No simulator of the complete format exists. Nothing was run on a 256-CPU
  machine: the widest CPU run had 27 threads, and whether trials scale to 256 workers is
  untested. Generation was timed on the GPU only.

## 14. Decisions

**Settled**

| Decision | Where |
| --- | --- |
| One cell per tone is the default tone length; tone length and hop length are separate settings | 4.1, 4.2 |
| FHSS is optional; nothing carries over between hops; no setting depends on Helper | 1, 4.3 |
| The transmitter is formatted for a diffuse path; the receiver always makes coherent attempts, over the whole band | 1, 8 |
| A block spans at least 32 hops; half the hops lost is covered from 6 dB of margin, and not at 3 dB | 6, 9 |
| 40 s is the recommended alternate hop length for one-cell tones | 4.3 |
| About 10 bits take less than twice as long as one bit at similar confidence | 1, 10 |
| 256 values with a keyed placement over all positions, where earlier notes suggested 64 with wide spacing | 5 |
| Conditioning ahead of the 4-bit sample code uses full precision; everything after it is 4-bit | 8 |
| The spread bank reaches 500 Hz; EME needs a predicted-Doppler input | 8, 12.1 |
| "Dwell" is replaced by tone, hop and held tone | 2 |
| At least one keyed phase change per tone is mandatory in all cases, with four phases | 1, 4.4 |
| Preferred tone rate: as fast as the spread and total bandwidth allow, up to one tone per cell; below 9 kHz the tone is held for 10.24 s instead | 4.2 |
| Tones shorter than one cell and three phases per cell are not offered | 4.2, 4.4 |
| No amplitude shifts and no information in phase; those belong to a fast modem on a strong, coherent link | 1, 4.2 |
| Meteor burst is outside this draft; if pursued it is an FSK path class on overdense trails, not a PSK mode | 12.3 |
| Below 9 kHz: FHSS off, lattice scale `j` = 3 (1.28 s cell, 0.78125 Hz), sixteen positions in 12.5 Hz, tones of 8 cells (10.24 s), with `j` = 2 and 4 and tones of 1 and 32 cells as alternatives | 3, 4.1, 4.2, 12.2 |
| Below 9 kHz the keyed phase is reached by an 80 ms ramp, not a step; the receiver's reference follows it | 4.4, 12.2 |
| Below 9 kHz the window ladder runs from two tones to the block, with the tone and the cells inside it below; references are taken as GPS-locked at both ends | 8, 12.2 |
| Below 9 kHz not hitting the computational ceiling has priority; concealment is kept only where it costs no computation (the keyed phase per tone), and phase steps inside a tone are not offered there | 1, 4.4, 11 |
| Frequency and drift hypotheses are a transform across a message's tone sums, routed between CPU FFT, GPU FFT and a Tensor Core matrix product; they are never an outer loop of decodes | 8, 12.2, 13 |
| Three computational rules bind receiver and simulator: time every route and rebalance for the whole job; do no work twice, with hypotheses inside the one pass over the messages; simulation speed is a requirement on a 256-CPU machine and on a 32-CPU machine with an RTX PRO 6000 | 1, 8, 13.1, 13.5 |

**Open**

1. **Tone length for weak signals.** The stated preference is the fastest rate the path allows.
   Still to decide: whether the tone stays at one cell for the weak-signal use on slow paths or
   is set from the design C/N₀ and path class, as the hop length is. One cell costs 5 to 13 dB of
   time on paths of 1 Hz or less against a matched held tone. Coherent attempts do not recover
   this when the expected spread appears: they are limited to about the coherence time, like a
   held tone. A narrower band only helps paths with very little spread.
2. **Phase steps beyond the mandatory one per tone** (settled below 9 kHz: none). Whether held
   tones of 4 s and more step every cell by default, and whether 2 or 4 phases per cell are offered for one-cell tones
   (1.4 to 2.8 dB against a filter-bank observer for 2 to 4 times the transforms). The mandatory
   phase overrides the brief's "tones without unnecessary phase or amplitude shifts". Its effect
   on how the emission is classified (spread spectrum) is unexamined.
3. **Which observer the LPI goal means.** The brief intends the long tone for maximum LPI. That
   holds against energy detection, and against a matched filter bank only with phase steps
   inside the tone. The coherent observer is covered by the mandatory keyed phase.
4. **Hop edges.** This draft uses 20 ms of ramp and 20 ms of quiet per edge at 0.4 s. The
   existing triad envelope (55 ms quiet, 20 ms ramp) would cost a further 1.0–1.5 dB of power
   there. Confirm what the radios need.
5. **Shift oscillator.** The hop-to-hop frequency repeatability and the stability inside a held
   tone that section 4 asks for are to be checked against the radios.
6. **Symbols per block.** On a steady path with long held tones, 16 or 8 symbols instead of 32
   would save 0.8 or 1.4 dB, and would shorten the shortest block. Fading paths need the
   diversity of 32. Not adopted.
7. **False-presence rate** of 10⁻⁶ per block is a placeholder that sets block length.
8. **Overload limit** of 2 dB on what the scheduler may trade is a placeholder.
9. **Without FHSS.** Whether scoring with one phase per position is on by default, and the
   coherence window a sub-9 kHz block is sized for. The ladder itself is settled below 9 kHz:
   from two tones up. Outside that configuration the default stays at 1/64 of the block, which
   leaves a gap below it for tones shorter than about 10 s
   ([12.2](#122-phase-stable-paths-below-9-khz)).
10. **Long messages.** Which long-block code, and whether to extend the exhaustive search past
    24 bits at double the decode work per added bit.
11. **The brief's "40x alternative"** is read here as the 40 s hop.
12. **Measuring the always-on coherent attempts** against the computational ceiling. Done for a
    narrow band without FHSS, with the frequency and drift search as a transform across tone
    sums on CPU and on Tensor Cores (sections 12.2 and 13). The wide-band specular search is
    still unmeasured. Its 350 fine timing steps cannot be had from a transform across positions
    at a saving, because 352 positions meet them; they are vector entries of the one pass over
    the messages (13.1), which is not built.
13. **Search volume below 9 kHz.** The configuration assumes GPS-locked references at both
    ends, and a lesser reference is searched by the transform of section 8. Still to decide:
    which reference classes a configuration may declare (a ±1 ppm reference is within reach of
    the laptop CPU for 24 bits, a free-running crystal only for 16), and what the receiver does
    beyond them: shorter windows, or a report of overload.
14. **Clock-correction rule.** Adopt the rule of section 12.2 and set the slew bound.
15. **Tone rates above 6.25 Hz.** Not offered in this draft. Whether a faster rate is wanted for
    strong signals on paths that allow it, given the finer timing and larger drift search.
16. **Meteor burst.** Not supported (section 12.3). Whether to pursue it as an FSK path class on
    overdense trails over the whole shared sky at a few hundred kilometres, with an all-beam
    receive array. It needs a shorter cell, a per-trail frequency search and a decision on the
    144 MHz probe. The wideband keyed-phase mode for underdense trails would be a separate
    specification.
17. **Troposcatter at 50 MHz** (section 12.3) as a path class for up to about 300 km, with a
    held tone for its slow fading. It competes with meteor burst at short range and needs no
    array.
18. **Value-dependent phase with few positions.** Whether the keyed map may send each value to a
    position and one of four quarter turns when fewer than sixteen positions fit. It removes
    the few-position loss of coherent reception and leaves per-tone reception as it is, but it
    puts information into the phase, which section 1 rules out
    ([sub-9 kHz note](../_ref/algorithms-matrixModem/Fable-5.2/sub9khz-fsk-rate-and-spacing.md#7-positions-and-a-value-dependent-phase)).
    The sub-9 kHz configuration does not need it, because sixteen positions fit.
19. **Sub-9 kHz carrier and slot.** The example slot from 8,259.375 Hz clears the 50 Hz and
    60 Hz harmonics on the steadier grids only. Still to decide: the carrier where a 16.7 Hz
    railway supply or an unsteady grid is in range, and whether a receiver may ask for scale
    `j` = 4 instead. The night mode cut-offs near 5.17 and 8.27 kHz were not examined either
    ([sub-9 kHz note](../_ref/algorithms-matrixModem/Fable-5.2/sub9khz-fsk-rate-and-spacing.md#9-weaknesses-and-open-items)).
20. **Ramp length against a real transmitter.** 80 ms is calculated for a single-tuned antenna.
    What a transmitter and its matching network tolerate is to be confirmed, and with it whether
    160 ms should be the default.
21. **Acceptance figures for the computational rules.** The factor within which the scheduler
    has to come to the best fixed assignment (placeholder 1.2), and numeric speed targets for
    the two experiment machines once a simulator exists.
22. **Hypotheses that are still loops.** Whether to build the split by position for timing
    uncertainty of about ±1 s and more below 9 kHz, and the one pass with vector entries for
    the spread bank and the wide-band fine timing steps (13.1).

## 15. Known weaknesses

- **No end-to-end simulation exists** of the code, placement, evidence, presence test and search
  together. Durations in section 10 are estimates.
- **One cell per tone is the slower choice for weak signals with per-tone reception.** Against
  the bound it stands at 14.2, 5.7 and 1.2 dB at 0.1, 1 and 10 Hz of spread (−10 dB-Hz, 4 s
  hop), where a 4 s held tone stands at 2.2, 0.6 and 0.2 dB. This is the largest single
  sensitivity cost in this specification.
- **One-cell tones were not run in the alphabet simulation.** The adjacent-position result and
  the two-cell durations do not automatically carry over.
- **Required coherent attempts over a wide band are costly and unmeasured.** A specular path over
  2.2 kHz needs timing to 57 µs, about 350 times the accumulation and decode work of the per-tone
  search, and raises the presence threshold by about 0.7 dB of block time.
- **The search below 9 kHz still exceeds the computational ceiling for 24-bit blocks with
  free-running crystals and sound-card clocks**, by about two on one GPU and 80 on the laptop
  for a crystal (section 12.2); 16-bit blocks are within reach. Only the ±1 ppm case is
  measured; the other classes are scaled, the reference classes are assumptions, and the
  counting grid is rectangular.
- **The transform search is a benchmark, not a receiver.** It has synthetic bin values, one
  planted signal and stock kernels. Forming the bin values for every timing offset, the
  presence test, tilted bin paths for a drifting reference on the two rungs below the whole
  block, and the summing of short tones into sub-blocks are not in it.
  A specular HF path adds a delay-rate dimension to the 57 µs timing of the wide-band coherent
  attempt as the ionosphere's height changes; that is not sized.
- **Coherent attempts are calculated, not built.** Their gains rest on large-alphabet
  calculations for two-cell tones, a 10-bit Monte Carlo with ideal frequency and timing, and a
  threshold estimate. Attempts inside a hop with FHSS are derived, not computed. No receiver,
  drift-rate search or sub-9 kHz noise was run. A block Monte Carlo with phase and frequency
  wander now covers the sub-9 kHz formats (section 12.2), with known timing and frequency,
  Gaussian noise and constructed wander: no phase spectrum of a carrier below 9 kHz was found.
- **A block sized for a coherence window depends on that coherence.** If the path or either
  reference is less steady than the window, the block fails; per-tone reception would have
  needed 6 to thousands of times as long. Night-time wander and the sunrise and sunset steps
  below 9 kHz are the known cases. Sizing for 22-minute windows instead of a coherent block
  costs 1.3 to 9 times the block time.
- **The sub-9 kHz configuration rests on an assumed antenna.** The bandwidth at 10 kW goes with
  the inverse square of a top-voltage limit that is known only within 30 to 100 kV for a 61 m
  installation, and the mains slot rests on grid statistics without a measured line amplitude
  near 8 kHz (section 12.2).
- **The phase ramp is calculated, not tested.** A keyed phase step into a high-Q antenna is a
  transient that VLF practice forbids. The 80 ms ramp removes it in a single-tuned model with a
  constant-amplitude source; no transmitter, matching network or protection circuit was
  examined, and a 24 Hz antenna still sees the drive and the current 49° apart.
- **The sub-9 kHz slot is not clear everywhere.** A 16.7 Hz railway supply puts a harmonic near
  8,266.7 Hz inside it, and on unsteady grids the mains harmonics cross it. One line covers one
  position in sixteen for as long as it sits there; no line amplitude near 8 kHz was found, and
  the excision has not been run on such noise.
- **The sub-9 kHz configuration serves a strong link slowly and shows a narrow line.** Its
  shortest block is 10.9 minutes. A 10 s tone with one phase leaves an observer with a filter
  bank little to overcome, which is accepted there (section 1). It is also a second cell length
  to implement and test.
- **Held tones and frequency.** A held tone needs both oscillators steady to a fraction of its
  reciprocal while it lasts (2.5 mHz for 400 s, 0.25 mHz for 4,000 s) and a hop-to-hop frequency
  error no wider than the path's spread. An error of ±1 Hz per hop costs 6 to 11 times the block
  time on a 0.1 Hz path. The per-block search for the path's own Doppler shift is not designed.
- **Held tones with one phase are conspicuous to a matched observer.** With 32 per block an
  observer with a filter bank needs only 2 to 6 dB more C/N₀ than the link.
- **The observer's best statistic against the keyed signal is not established.** The pulse
  attack on an unkeyed signal is computed and simulated (11.5 dB in 18 hours); the fourth-power
  attack on the four-phase key is not.
- **Phase steps inside a held tone make timing a hard requirement.** Half a phase interval of
  timing error costs 6 dB and a whole one everything. The timing search has to cover the clock
  uncertainty in offset and in rate: 26 rate hypotheses for a 3.6 h block on a ±20 ppm clock,
  256 for a 36 h block. A clock whose rate changes during the block adds a dimension that is not
  sized. Phase steps through a narrow antenna are now calculated for the receiver's side and for
  a model transmitter (sections 4.4 and 12.2); through a radio's filters they were not examined.
- **Concealment figures are for an idealised observer on a steady path.** Fading paths, the
  periodic hop envelope, a noise-uncertain energy detector and real observers were not
  evaluated; the earlier
  [observer notes](../_ref/algorithms-matrixModem/Sol-6.2/observer-and-interference.md) apply.
- **Hop spanning lengthens strong-signal blocks.** A block has to fill 32 hops: 13 s with 0.4 s
  hops, 21 minutes with 40 s hops, whatever the level. The per-hop weighting and the lost-hop
  cases were calculated as erasures, not simulated with real interference.
- **Strong incoherent swept interference:** loss grows without bound above about 20 dB over the
  noise. Dechirp-domain excision is the candidate remedy and is untested.
- **FHSS off gives up what FHSS provided:** no hop diversity and a fixed footprint. On a few
  positions one steady interferer covers a large share of the alphabet.
- **Two to four positions** cost 2.6 and 0.8 dB against a coherent phase-keyed signal, which has
  no such loss. From 8 positions the difference is under 0.3 dB. A value-dependent phase would
  remove the loss (open decision 18); the sub-9 kHz configuration avoids it with sixteen
  positions.
- **Meteor-burst and 50 MHz troposcatter figures rest on thin ground** (section 12.3).
  - The long-path figures scale one measured 500 W link down by 37 to 54 dB and extend its
    trail rate above the overdense transition.
  - The whole-sky figures continue the published flux law two decades past where it is
    established and assume receive gain down to 3° elevation.
  - The overdense formulas were read only in secondary sources, real echo durations are poorly
    established, and fading inside an echo is not modelled.
  - The troposcatter figures come from methods not validated at 50 MHz, which differ by 10 to
    13 dB among themselves; no measurement was found.
- **Outage looks like quiet.** A fadeout longer than two block periods ends the message unless the
  schedule resumes.
- **Long messages** pay the block penalty until a long-block code is qualified.
- **No HF precedent below about +3 dB-Hz** at milliwatt power; the lower part of the target range
  rests on theory.
- **This draft still contains repeated work in three places** (13.1): timing offsets repeat
  the gather and the transform across tone sums, 20 times below 9 kHz; the spread bank and the
  wide-band fine timing steps were described as repeated decodes until this revision. The
  forms that avoid the repetition are derived, not built, and for the last two the additions
  per message and hypothesis remain.
- **Simulation speed is required but not measured on its machines.** No simulator of the
  complete format exists, nothing was run on a 256-CPU machine, and a 24-bit trial with the
  ±1 ppm search takes 11 s on the GPU, which makes sweeps of thousands of such trials slow.
- **The scheduler exists only as a test prototype.** Rules 1 to 5 of section 13 ran on synthetic
  portions with two executors on one GPU type and an emulated slow one. Rule 6 and rule 7 have
  not run, and nothing is in a receiver. Its exploration cost 4–12% and can probably be reduced.
- **Acquisition** over real clock, oscillator and drift uncertainty, native 4-bit devices,
  integrated GPUs and live operation are unmeasured.
