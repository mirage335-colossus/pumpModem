# Matrix Modem FSK: specification draft

Draft of 26 November 2026, last reconciled 27 November, for review. It extends the
[Matrix Modem brief](matrixModem.md) with a concrete keyed MFSK waveform, optionally frequency
hopped, and the receiver behaviour it requires. No implementation exists. Every number is a
calculation, a model simulation or a stage measurement on one GPU. The supporting research is
indexed in
[`_ref/algorithms-matrixModem/Fable-5.2/`](../_ref/algorithms-matrixModem/Fable-5.2/README.md).
[Section 14](#14-decisions) lists what is settled and what still needs a decision, and
[section 15](#15-known-weaknesses) lists the known weaknesses.

## 1. Scope and fixed decisions

- One-way, one tone at a time, constant envelope at full peak power, fixed configuration per
  purpose, no feedback, no length fields.
- Target: 2–100 mW through more than 150 dB of path loss, C/N₀ from about +10 down to −20 dB-Hz,
  short messages that may take hours, days or weeks. On phase-stable paths without FHSS the range
  extends to about −37 dB-Hz, where coherent links below 9 kHz already work.
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
- A general solution without drastic failure is preferred over one that is slightly better in the
  cases it handles. A theoretical weakness is listed, not dismissed.

## 2. Terms

| Term | Meaning |
| --- | --- |
| Cell | 512 samples = 160 ms. The unit of every duration. |
| Position | One of the tone frequencies, 6.25 Hz apart. |
| Tone | One tone on one position. **Tone length** is how long it stays there, in cells. |
| Hop | With FHSS, the radio's stay on one hop frequency. **Hop length** includes its edges. |
| Held tone | A tone as long as the flat part of its hop: one tone per hop. |
| Block | 10, 16 or 24 information bits sent as 32 symbols. |
| Phase key | The keyed phase of 0°, 90°, 180° or 270° that every tone is sent with. |
| Phase interval | The stretch sent with one keyed phase: the tone, or a part of it. |
| Per-tone reception | The receiver is coherent inside one tone and adds tones as energies. |
| Coherent attempt | The receiver undoes the phase key and adds different tones with their phases, over a window. |

"Dwell" is not used. Earlier drafts used it for both a tone and a hop.

## 3. Lattice

| Quantity | Value |
| --- | --- |
| Complex baseband sample rate | 3,200 Hz (48 kHz audio ÷ 15) |
| Cell | 512 samples = 160 ms |
| Tone bandwidth and position spacing | 6.25 Hz (one bin of one cell: the FT8 cell) |
| Total FSK bandwidth | Configurable, a whole number of positions. Default 2.2 kHz (450–2,650 Hz audio) = 352 positions; up to 2.8 kHz = 448; down to 12.5 Hz = 2 positions |
| Tone rate | 6.25 tones per second by default (one cell per tone) |

- Every tone length and hop length is a whole number of cells. Every tone is on a position. A
  tone is unshaped and completes whole cycles in every cell. Its phase changes only by the keyed
  quarter turns of the phase key ([4.4](#44-phase-key)).
- These rules give exact orthogonality over a cell, one transform plan, and bin statistics that a
  simulator may draw without forming samples.
- **Lattice scale.** The cell may be set to 160 ms × 2^j with positions 6.25 Hz / 2^j apart.
  `j` = 0 is the default and serves every path. A larger `j` only refits the tones to a transmit
  antenna narrower than about 50 Hz, where it recovers up to 1.6 dB. Shorter cells are not
  offered: a 40 ms cell is never better than 160 ms for weak signals, at any spread up to 100 Hz
  ([4.2](#42-tone-length) gives the cost of faster tone rates).

## 4. Tone, hop and phase key

### 4.1 Settings

| Setting | Default | Other values | Chosen from |
| --- | --- | --- | --- |
| Tone length | 1 cell (160 ms) | Any whole number of cells up to the hop | Expected Doppler spread and level ([4.2](#42-tone-length)) |
| FHSS | On where the radio hops | Off | The radio; off below 9 kHz |
| Hop length | 0.4 s | 40 s recommended alternate; any whole number of cells | Hop channels, level, regulation ([4.3](#43-hop)) |
| Phase key | Always on: one keyed phase per tone | More often: every 8, 4, 2 or 1 cells of a held tone, or 2 or 4 times per cell | Mandatory; more often for concealment ([4.4](#44-phase-key)) |

### 4.2 Tone length

The transmitter chooses the tone length for a diffuse path of the expected spread. With per-tone
reception a tone is worth holding for about 4 divided by the Doppler spread and no longer.

**Stated preference.** A tone rate as fast as the spread and the total bandwidth allow, up to
one tone per cell, with one keyed phase per tone. Two cases depart from it:

- **Paths where specular reception can be expected.** The receiver's wide-band coherent attempt
  then carries the load, with its 57 µs timing and a delay-rate search
  ([section 8](#8-receiver-requirements), item 8).
- **Hardware limited to an extremely narrow band**, in practice below 9 kHz. Tones are long
  because the antenna passes little bandwidth, and the load is the search in millihertz bins
  through oscillator, clock and path drift. It exceeds the computational ceiling for
  free-running references and long windows, at any tone or phase rate
  ([section 12](#12-paths-other-than-mid-latitude-hf)).

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
- **Clock drift adds a timing-rate dimension.** A sample clock off by `e` slips `e` times the
  block time, so the timing rate needs about 2·`e`·(block time)/spacing hypotheses, and offset
  and rate multiply. For a 3.6 h block with a phase per cell: 1 with a disciplined clock, 2 at
  ±1 ppm, 26 at ±20 ppm.
- **Oscillator drift adds nothing to the timing search.** Frequency error is searched after the
  phase key is removed. It has its own frequency and drift-rate search wherever the receiver is
  coherent for long ([section 12](#12-paths-other-than-mid-latitude-hf)).

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
  0.8, 0.2 and 0.0 dB for 2, 4, 8 and 16 positions (simulated). Per-tone reception with few
  positions is not simulated.
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
  coded bits.
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
  10 Hz, and the block, tone and phase-interval ordinals. The hop frequency is not an input to the
  other schedules.
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
7. **Search and decode.** Every timing, frequency-offset and spread hypothesis gets a full decode
   of the block. Timing hypotheses are an eighth of the phase interval apart, in offset and in
   clock rate: 20 ms for a one-cell tone or a phase step every cell, less in proportion for
   more than one phase per cell. Coherent windows add frequency bins half the window's
   reciprocal apart and drift-rate hypotheses
   ([section 12](#12-paths-other-than-mid-latitude-hf)).
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
   - **Frequency.** A transform across a message's window sums gives the frequency-offset
     hypotheses. With FHSS each hop's frequency error is common to its tones and is searched
     inside that hop.
   - **Drift limits the window, not the method.** The frequency has to stay within a fraction of
     the window's reciprocal while the window lasts: 25 mHz over 40 s, 2.5 mHz over 400 s. An
     undisciplined HF transceiver drifting 0.1 to 1 Hz per minute is marginal at 4 s and fails
     from 40 s; at 9 kHz, 1 ppm is only 9 mHz. A shorter rung wins when drift spoils the
     longer ones, and the per-tone result is the floor.
   - **Specular path.** A specular path has one delay, which appears as a phase slope across the
     band. Scoring with one phase for all positions is then valid over any band, with timing
     hypotheses about 1/(8 × band span) apart: 57 µs for 2.2 kHz, 1.25 ms for 100 Hz. The slope
     is applied to the stored bin values, so each fine step repeats the accumulation and the
     decode but not the transform. Over 2.2 kHz that is about 350 fine steps for each 20 ms step.
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
  wander by 2.5–10 Hz rms; four positions fit between two 50 Hz harmonics on the steadier grids.
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

These are for an observer that adds a tone's segment energies and takes the largest; allow
about 2 dB for a better statistic. Two segments per tone gain only 0.5 to 0.7 dB.

**Where the keyed receiver adds coherently, the gap grows with the coherent time.** The keyed
receiver needs a fixed total energy (about 57 times the noise density for a 10-bit block), and
an observer measuring energy needs an amount that grows with the square root of the cells
observed. For 4 positions and a block that is coherent for one hour that is about 13 dB, rising
5 dB per tenfold of block time. Hand estimate from the figures of section 10 and calculation 14;
not computed.

## 12. Paths other than mid-latitude HF

The receiver adapts to the path by its spread hypothesis and its coherent attempts. Extra
transmit time against the bound at −10 dB-Hz, held tone of 2 cells in a 0.4 s hop unless stated:

| Path | Spread | Gap | What else it needs |
| --- | --- | ---: | --- |
| HF Worst (30 Hz, 7 ms) | 30 Hz | 1.1 dB | nothing |
| Aurora, flutter | 50–500 Hz, with offset | 1.0–1.1 dB | spread hypotheses up to 500 Hz |
| Earth–Moon–Earth | about 20 Hz per GHz | 1.0–2.4 dB | the predicted Doppler shift (up to ±20 kHz and 1.3 Hz/s at 5.76 GHz) removed before the transform |
| Troposcatter at 0.9–5.8 GHz | roughly 0.3–20 Hz (an extrapolation from 30 MHz data) | 6.4 dB at 0.5 Hz, 2.4 dB at 3 Hz, 1.5 dB at 10 Hz | nothing; where hopping is not imposed, a longer tone recovers the slow end |
| Phase-stable (sub-9 kHz, daytime groundwave), per-tone reception, no FHSS | below 1 mHz | 18 dB (28 at −20, 38 at −30 dB-Hz) | this is only the fallback |
| The same, coherent windows of 1 min / 22 min / 5.8 h | below 1 mHz | 1.4 / 0.2 / 0.0 dB (5.7 / 1.0 / 0.1 at −20; 14.1 / 4.4 / 0.7 at −30) | both references and the path steady over the window |

- **Wide spreads are the easy direction for weak signals.** With the spread bank stopping at
  60 Hz the loss would be 1.6 dB at 100 Hz, 3.6 dB at 200 Hz and 7.2 dB at 500 Hz, hence the
  bank to 500 Hz.
- **Strong signals on very wide spreads are limited in rate, not in sensitivity.** This modem
  does not chase that case.
- **Phase-stable paths use the default lattice with FHSS off**, few positions, and the receiver's
  coherent windows. In the records gathered by the
  [earlier research](../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/10-sub9khz-propagation.md),
  a path below 9 kHz held phase within about ±10° for hours around midday, wandered by tens of
  degrees at night and stepped by about 40° at sunrise. Coherent amateur links there run at −15 to
  −37 dB-Hz with references locked to GPS at both ends.
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
  - Timing multiplies it by the timing uncertainty over an eighth of the phase interval: 100
    offsets for ±1 s with a phase per 160 ms cell, 2 with a 10 s phase interval.
  - Against the measured 8,400 decodes a second, with ±1 s of timing and a phase per cell: the
    crystal passes at 1 h and fails at 5.8 h; the sound-card clock fails from 1 h; ±5 Hz fails
    from 22 min. With a 10 s phase interval only the 5.8 h window fails, for the last two.
  - Against the 65 a second estimated for the coherent ladder, the crystal already fails at
    1 min with a phase per cell.
  - A disciplined reference with logged corrections needs one hypothesis throughout. The same
    reference with unseen slewing needs 422,000 per 5.8 h window.
  - A sample clock not tied to the carrier reference multiplies these by up to 200 at 1 h.
- **Clock corrections must not add unknown drift or steps.** A GPS correction slewed at 1 ppm
  moves a 9 kHz carrier by 9 mHz, about 32 one-hour bins, and a correction stepped by 10 µs is a
  32° phase jump at 9 kHz. Proposed rule: no clock or phase steps inside a block at either end;
  corrections are slewed smoothly within a stated bound, or measured, logged and removed in the
  receiver's processing as a known trajectory. The bound is open.
- **The antenna sets the number of positions.** Practical sub-9 kHz antennas pass 6–620 Hz. Antenna
  roll-off and few-position loss together cost 0.1 dB with 16 positions in 458 Hz, 0.4 dB with 8
  in 174 Hz, 1.0 dB with 4 in 57 Hz and 2.4 dB with 4 in 18.5 Hz (0.8 dB there with `j` = 2).

## 13. Computation and simulation

- **Live reception** needs a small fraction of one CPU core for the transform. The receiver's
  work is the hypothesis count times the decode.
- **Measured on a full RTX PRO 6000 Blackwell**, batches of 32,768 two-cell tones:
  - cuFFT and an FP16 matrix-product DFT both exceed 20 million times real time. Which is faster
    depends on the bins wanted and on the batch: at 32,768 per call the matrix product is 1.2
    times faster for the 352 positions and ran at 70% Tensor Core activity; at 4,096 per call,
    which fits the GPU's cache, cuFFT is 2.1 times faster (200 million per second).
  - A full 24-bit decode (16.7 million messages) ran 8,400 times a second as a direct kernel,
    5,000 as an FP16 matrix product and 2,000 as a fast Hadamard transform.
  - A sample-level simulation, noise generation included, ran at 7–12 million times real time: a
    day of signal in about 10 ms.
  - With a search running on the same GPU, every GPU transform route fell to the same 7–14
    million per second. The GPU's time was divided almost without loss (shares summing to
    0.95–1.03), so a rate measured while sharing does not show which route is cheaper.
  - A prototype of the scheduling rule below cleared five queued workloads in 0.94 to 1.12 times
    the time of the best fixed assignment for each, from a cold start. Every fixed assignment
    tried was 2 to 73 times slower than the best in at least one of them.
- **What the settings do to the work** (reasoned from the receiver's structure and the
  measurements above; not measured for these configurations):
  - The decode per hypothesis is the same in every case, and is where the ceiling sits.
  - Hop length does not enter the work with one-cell tones.
  - Tone length does. One-cell tones make a block of about 420,000 tones at −10 dB-Hz where held
    tones make 32 to a few hundred, so there is far more transform and accumulation work, in the
    stage with the most headroom. They need 20 ms timing spacing where a held tone needs an
    eighth of its length.
  - Phase steps inside a held tone multiply its timing hypotheses by the phase intervals per
    tone, per timing dimension, where timing is uncertain. The mandatory one phase per tone
    adds no work.
  - Below 9 kHz the work is the frequency, drift-rate and timing search of
    [section 12](#12-paths-other-than-mid-latitude-hf), not the transform.
  - Coherent attempts add two table columns per window: 128 times the decode work for a ladder
    down to 1/64 of the block (about 65 full 24-bit decodes a second by extrapolation). Over
    2.2 kHz the specular search adds about 350 fine timing steps for each coarse one. Neither
    is measured.
- **Scheduling rule.** No fixed routing table is specified. The receiver chooses its routes while
  it runs:
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
     complete receiver clears, never one stage's rate or one unit's utilization.
  7. When no full-fidelity assignment keeps up, the receiver may give up at most 2 dB of
     sensitivity, charged to the margin and reported: first by computing only the position bins
     (0 dB up to 1 Hz of spread, 0.3 dB at 3 Hz, 1.5 dB from 10 Hz), then by shedding tones evenly
     by key (1.0 dB for 1.6 times the throughput, 1.5 dB for 2 times, 2.0 dB for 2.5 times). It
     never drops hypotheses or coherent attempts. Beyond the limit it reports overload.
- **Simulation.** One compiled implementation with two input modes. The cell mode draws bin
  statistics directly and is exact for a linear front end under the rules of section 3. The sample
  mode forms samples and is required for anything that involves conditioning, the 4-bit sample
  code or timing error.

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
| The spread bank reaches 500 Hz; EME needs a predicted-Doppler input | 8, 12 |
| "Dwell" is replaced by tone, hop and held tone | 2 |
| At least one keyed phase change per tone is mandatory in all cases, with four phases | 1, 4.4 |
| Preferred tone rate: as fast as the spread and total bandwidth allow, up to one tone per cell | 4.2 |
| Tones shorter than one cell and three phases per cell are not offered | 4.2, 4.4 |

**Open**

1. **Tone length for weak signals.** The stated preference is the fastest rate the path allows.
   Still to decide: whether the tone stays at one cell for the weak-signal use on slow paths or
   is set from the design C/N₀ and path class, as the hop length is. One cell costs 5 to 13 dB of
   time on paths of 1 Hz or less against a matched held tone. Coherent attempts do not recover
   this when the expected spread appears: they are limited to about the coherence time, like a
   held tone. A narrower band only helps paths with very little spread.
2. **Phase steps beyond the mandatory one per tone.** Whether held tones of 4 s and more step
   every cell by default, and whether 2 or 4 phases per cell are offered for one-cell tones
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
9. **Without FHSS.** The default window ladder (down to 1/64 of the block), whether scoring with
   one phase per position is on by default, and the coherence window a sub-9 kHz block is sized
   for.
10. **Long messages.** Which long-block code, and whether to extend the exhaustive search past
    24 bits at double the decode work per added bit.
11. **The brief's "40x alternative"** is read here as the 40 s hop.
12. **Measuring the always-on coherent attempts** against the computational ceiling.
13. **Search volume below 9 kHz.** State the oscillator and clock quality a configuration
    assumes, and decide what is narrowed by hardware and what by shorter windows (section 12
    sizes the volume for assumed classes).
14. **Clock-correction rule.** Adopt the rule of section 12 and set the slew bound.
15. **Tone rates above 6.25 Hz.** Not offered in this draft. Whether a faster rate is wanted for
    strong signals on paths that allow it, given the finer timing and larger drift search.

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
- **The search below 9 kHz exceeds the computational ceiling for free-running references and
  long windows** (section 12). The reference classes there are assumptions, not measurements,
  and the count is hypotheses per window against decode rates measured for other block shapes.
  A specular HF path adds a delay-rate dimension to the 57 µs timing of the wide-band coherent
  attempt as the ionosphere's height changes; that is not sized.
- **Coherent attempts are calculated, not built.** Their gains rest on large-alphabet
  calculations for two-cell tones, a 10-bit Monte Carlo with ideal frequency and timing, and a
  threshold estimate. Attempts inside a hop with FHSS are derived, not computed. No receiver,
  drift-rate search or sub-9 kHz noise was run.
- **A block sized for a coherence window depends on that coherence.** If the path or either
  reference is less steady than the window, the block fails; per-tone reception would have
  needed 6 to thousands of times as long. Night-time wander and the sunrise and sunset steps
  below 9 kHz are the known cases. Sizing for 22-minute windows instead of a coherent block
  costs 1.3 to 9 times the block time.
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
  sized. Phase steps through a narrow antenna or filter were not examined.
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
  no such loss. From 8 positions the difference is under 0.3 dB.
- **Outage looks like quiet.** A fadeout longer than two block periods ends the message unless the
  schedule resumes.
- **Long messages** pay the block penalty until a long-block code is qualified.
- **No HF precedent below about +3 dB-Hz** at milliwatt power; the lower part of the target range
  rests on theory.
- **The scheduler exists only as a test prototype.** Rules 1 to 5 of section 13 ran on synthetic
  portions with two executors on one GPU type and an emulated slow one. Rule 6 and rule 7 have
  not run, and nothing is in a receiver. Its exploration cost 4–12% and can probably be reduced.
- **Acquisition** over real clock, oscillator and drift uncertainty, native 4-bit devices,
  integrated GPUs and live operation are unmeasured.
