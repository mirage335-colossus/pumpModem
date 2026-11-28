# Matrix Modem FSK: specification draft

This experimental specification extends the [original brief](matrixModem.md). The initial
draft and its retained calculations date from 26–28 November 2026 (UTC).
The [receiver design review](../_ref/algorithms-matrixModem/fsk-reconciliation.md)
records corrections to the waveform, search geometry, code, presence statistic and overload policy.
The native reference decodes configured ten-bit words and closes a reception after two
scored quiet blocks. The interoperable waveform and file format, full uncertainty search,
sustained service on the target hardware and field sensitivity remain open.
The [earlier research index](../_ref/algorithms-matrixModem/Fable-5.2/README.md)
preserves the original measurements and their provenance. Sections 14 and 15 distinguish design
choices from evidence about their performance.

## 1. Scope and fixed decisions

- One-way, one tone at a time at full peak power, fixed configuration per purpose, no feedback,
  no length fields.
- The HF design target uses 2–100 mW and high path loss. Received C/N₀ also depends on
  antenna gains and noise density. The +10 to −20 dB-Hz study points, and the −37 dB-Hz
  coherent sub-9 kHz comparison, are research inputs. The proposed format has no field
  sensitivity measurement at these limits.
- **Receive through transmit-antenna pointing loss.** If an intact transmission from a
  misdirected high-gain antenna produces visible data tones on the receiving station's
  waterfall, the design goal is for the ordinary receiver to deliver the message. Choose
  payload rate, tone duration, bandwidth and repetitions for the expected off-axis C/N₀,
  path coherence and delivery deadline. [How to measure this goal](#81-reception-after-antenna-pointing-loss)
- **Constant amplitude on the active flat interval.** The proposed transmitter uses its available
  peak power throughout that interval. Hopping has ramps and quiet guards. Bandwidth, oscillator
  error, antenna response and channel nulls can independently limit reception. Use conducted,
  radiated or received power consistently in each link budget.
- **The smallest configured payload is 10 bits.** For short words read by an operator,
  use a provisional target of one false admission per 10,000 quiet public blocks (10⁻⁴).
  When a single detection statistic gets the entire budget, the weak-tone model gives
  5.9% more unrounded airtime for ten bits at 10⁻⁴ than for one bit at the historical
  10⁻⁶ target (400 nuisance hypotheses per word, 10⁻³ true-template miss). One bit is a
  theoretical comparison. The reference receiver divides its budget between two searches.
  Use section 10's rounded block periods to choose a transmit profile.
  [Receiver evidence](#8-receiver-requirements) · [Sizing assumptions](#10-sizing-and-expected-duration)
- **Choose the transmit profile before sending.** The HF comparison starts with 160 ms
  tones on a 6.25 Hz grid. Compare one-cell and held-tone profiles at the intended power,
  path spread and deadline. Longer tones can substantially shorten a weak, slowly fading
  link. They also impose a longer minimum block. A phase-stable profile below 9 kHz needs
  an explicit assumption about how long the received phase stays coherent.
- **Keep a per-tone decision available on the same samples.** The receiver also combines
  tones coherently within the configured windows when the waveform supplies a common phase
  reference. With FHSS that span ends at each retune. A hop containing one held tone supplies
  no additional inter-tone observation. Section 8 sets the joint admission and word-selection
  rule. A receiver can exploit extra coherence, but cannot recover energy the transmitted
  profile never collected.
- **Below 9 kHz the same format runs on a slower lattice.** FHSS off, a 1.28 s cell, sixteen
  positions 0.78125 Hz apart in 12.5 Hz, tones of 10.24 s, and a keyed phase reached by an 80 ms
  ramp instead of a step. The code still produces 8-bit symbols. With sixteen physical
  positions their scheduled repetitions can share position labels. Correct recovery depends
  on the resulting whole codeword and the channel. Coherent windows can supply sensitivity
  ([12.2](#122-phase-stable-paths-below-9-khz)).
- **Below 9 kHz not hitting the computational ceiling has priority, and concealment is kept only
  where it costs no computation.** Existing site noise and emissions measurements do not establish interference at another
  receiving site. The research profile assumes a separately authorized experiment. Charge
  the per-tone phase operation, timing search and workspace to the receiver. The profile uses at most one
  phase assignment per tone and disables hopping. Test scheduling on both HF and sub-9 kHz
  profiles with the complete search and workspace admitted.
- **Every tone has a scheduled phase.** Successive draws can be equal. The common-phase
  calculation assumes a private schedule, while the published zero-key profile is reproducible
  by any receiver ([4.4](#44-phase-key)).
- **FHSS is optional.** The hopping profile allocates time for ramps and guards and allows an
  unknown phase and frequency error on each retune. Each public ordinal and codeword extends
  across its scheduled hops. Without hopping, tones run back to back. The receiver knows
  their keyed transmit phases and must still account for oscillator and propagation phase.
- **Helper Carrier is a separate, diagnostic system.** It stands in for more stable oscillator
  hardware. Its two-pilot frequency, phase and timing estimates are used, if at all, as standalone
  search narrowing with a benefit similar to a GPSDO. There is no joint detection between Helper
  tones and modem signals. No setting of this specification depends on it.
- **Measure the complete receiver.** Compare qualified CPU and GPU routes on the available
  machine, reuse shared work within the workspace limit, and measure simulation throughput
  on the stated CPU and GPU reference classes. A candidate route needs to finish the full
  public search, including quiet periods. [Scheduling rules](#131-rules-that-bind-every-implementation)
- **Prefer a profile that can receive through the difficult cases.** A modest increase in
  airtime or processing cost is acceptable when it avoids a severe sensitivity loss or lets
  the complete receiver keep up with incoming samples across the intended configurations.
  Record theoretical failure cases with their assumptions even when their field frequency
  is unknown.

## 2. Terms

| Term | Meaning |
| --- | --- |
| Cell | The lattice time unit for flat tone intervals. At the HF reference it is 512 complex samples at 3,200 samples/s, or 160 ms. Hop guards and phase ramps have their own durations. |
| Position | One of the tone frequencies, 6.25 Hz apart at the default scale; 0.78125 Hz below 9 kHz. |
| Lattice scale | The integer exponent `j` sets the cell to 160 ms × 2^j and the position spacing to 6.25 Hz / 2^j. |
| Tone | One tone on one position. **Tone length** is how long it stays there, in cells. |
| Hop | With FHSS, the radio's stay on one hop frequency. **Hop length** includes its edges. |
| Held tone | One position sustained for two or more consecutive flat cells. The named FHSS held-tone comparisons use one tone over the whole flat part of each hop. |
| Block | 10, 16 or 24 information bits sent as 32 symbols. |
| Quiet-block false admission | The receiver reports a word in a physically quiet public block after searching all configured hypotheses. One reported block counts once. |
| Wrong active word | The wanted transmission is present and the receiver admits a payload different from the transmitted one. |
| Phase key | The keyed phase of 0°, 90°, 180° or 270° that every tone is sent with. |
| Phase interval | The stretch sent with one keyed phase: the tone, or a part of it. |
| Phase ramp | Below 9 kHz, the initial part of an interval over which phase moves evenly to its new keyed value. The reference is 80 ms, with a 160 ms alternative for a narrower antenna. |
| Per-tone reception | The receiver is coherent inside one tone and adds tones as energies. |
| Coherent attempt | The receiver undoes the phase key and adds different tones with their phases, over a window. |

"Dwell" is not used. Earlier drafts used it for both a tone and a hop.

## 3. Lattice

| Quantity | Default (lattice scale `j` = 0) | Below 9 kHz (`j` = 3) |
| --- | --- | --- |
| Complex baseband sample rate | 3,200 complex samples/s after translating and filtering 48 kHz real audio | 3,200 complex samples/s, or 400 after further filtering and decimation by 8 |
| Cell | 512 samples = 160 ms | 4,096 samples at 3,200 Hz = 1.28 s |
| Reciprocal cell duration and position spacing | 6.25 Hz (one bin of one cell: the FT8 cell) | 0.78125 Hz |
| Nominal position grid (`N` slots × spacing) | Default 2.2 kHz, with 352 slots starting at 450 Hz audio. At 2.8 kHz there are 448 slots, and at 12.5 Hz there are two. | 12.5 Hz, with 16 slots starting for example at 8,259.375 Hz |
| Tone rate during flat transmission | 6.25 tones/s at one cell per tone. The 0.4 s hop carries two such tones, averaging 5 tones/s including edges. | 0.09765625 tones/s at 8 cells (10.24 s) per tone, FHSS off |

With first tone centre `f₀` and spacing `Δf`, the grid centres are `f₀ + nΔf` for
`n = 0…N−1`. The default centres run from 450 to 2,643.75 Hz. The upper grid boundary
at 2,650 Hz is exclusive. The transmitted occupied bandwidth depends on the waveform,
taper and filtering. Translate the selected real-audio band to complex baseband and
reject images and out-of-band energy before decimation. A 3,200 samples/s real-audio
stream alone cannot represent the full 450–2,650 Hz passband by direct low-pass sampling.
[Bandwidth definitions](../_ref/algorithms-matrixModem/fsk-reconciliation.md#operating-assumptions)

Admit the grid together with the configured frequency offsets, spread, transition bands
and usable analog passband. As an idealized example, a centred 2.8 kHz grid leaves
about 200 Hz at either edge of a 3,200 complex samples/s band. At an outer tone with
200 Hz clearance, a Gaussian Doppler spectrum of full two-standard-deviation width
500 Hz puts about 21.2% of its power beyond that edge, even before an additional
frequency offset. A wider qualified input and baseband rate can capture more of the
spectrum. If the passband is fixed, score only the measured in-band response and
characterize the resulting sensitivity at edge positions. Aliased or rejected
components supply no independent bin observations. [Two-standard-deviation spread convention, ITU-R F.1487 Annex 3](https://www.itu.int/dms_pubrec/itu-r/rec/f/R-REC-F.1487-0-200005-I%21%21PDF-E.pdf) · [Projected spectra and front-end assumptions](../_ref/algorithms-matrixModem/fsk-lattice-validity.md)

- A tone occupies an integer number of flat cells. The frequencies differ by integer multiples
  of the reciprocal cell duration. The common carrier phase still evolves at the chosen band
  origin. A flat aligned observation of a single undistorted tone has zero projection onto
  distinct lattice positions. An identical known unit-modulus phase modulation on all candidate
  references also preserves their inner products. A rectangular tone has sidelobes outside the
  nominal position band. Filters, masks, delayed transitions and displaced receiver windows
  change the projected means or noise covariance. Model them explicitly when drawing joint bin
  statistics. [Finite-window checks](../_ref/algorithms-matrixModem/fsk-lattice-validity.md)
- **Lattice scale.** The cell is 160 ms × 2^j with positions 6.25 Hz / 2^j apart. `j` = 0 is the
  voice-band reference. A larger `j` puts more
  positions into a band that the transmit antenna or the receiver's interference keeps narrow.
  - **Below 9 kHz the scale is `j` = 3**: a 1.28 s cell and positions 0.78125 Hz apart, sixteen
    of them in 12.5 Hz ([12.2](#122-phase-stable-paths-below-9-khz)).
  - `j` = 2 (1.5625 Hz, sixteen positions in 25 Hz) and `j` = 4 (0.390625 Hz, sixteen in
    6.25 Hz) are the alternatives there, for a wide antenna on one mains family and for an
    antenna of 20 Hz or less.
  - A scaled cell is 512 samples at 3,200 Hz / 2^j or 512 × 2^j samples at 3,200 Hz; the bins
    are the same. Conditioning runs before any further decimation.
  - The draft offers 160 ms as its shortest cell. Earlier weak-signal comparisons hold only for
    the specified diffuse-channel, bandwidth and receiver model. Transient openings, a rapidly
    varying channel or other deadlines can favor another aperture or within-cell processing.

## 4. Tone, hop and phase key

### 4.1 Settings

| Setting | HF reference | Other values | Chosen from |
| --- | --- | --- | --- |
| Lattice scale | `j` = 0: 160 ms cell, 6.25 Hz spacing | `j` = 3 below 9 kHz (1.28 s, 0.78125 Hz); `j` = 2 or 4 there for other antennas | Transmit antenna bandwidth and the receiver's interference ([section 3](#3-lattice), [12.2](#122-phase-stable-paths-below-9-khz)) |
| Tone length | 1 cell (160 ms) | A whole number of flat cells. With FHSS the tone must fit inside one hop. Below 9 kHz use 8 cells (10.24 s), with 1 or 32 cells as alternatives at `j = 3` | Expected Doppler spread and level ([4.2](#42-tone-length)) |
| FHSS | On where the radio hops | Off | The radio; off below 9 kHz |
| Hop length | 0.4 s including 80 ms of edges | 40 s is the named alternate. The longer HF examples use a whole number of cells including 160 ms of edges | Hop channels, level, regulation ([4.3](#43-hop)) |
| Phase key | One scheduled phase per tone | Experimental HF candidates shorten the phase interval to 8, 4, 2 or 1 cells of a held tone, or to half or one quarter of a cell. The below-9 kHz profile uses one phase per tone. | [Phase reference and timing cost](#44-phase-key) |
| Phase ramp | None: the keyed phase is stepped | 80 ms below 9 kHz; 160 ms for a transmit antenna narrower than about 40 Hz | The transmit antenna's Q ([4.4](#44-phase-key)) |

### 4.2 Tone length

The transmitter chooses the tone length for a diffuse path of the expected spread. The retained Gaussian diffuse-channel calculations give diminishing returns around four
divided by their stated Doppler spread. The spectrum shape, tone energy, clock and receiver
metric determine the appropriate aperture.

**Profile selection.** Configure the tone length before transmission. One cell is the fastest
tone offered at a given lattice scale. Compare it with held tones at the expected signal level,
Doppler spread, frequency error and deadline. Include the repeat and hop floors in section 6
and check that the receiver can sustain the search. Two particular cases follow.

- **Paths where specular reception can be expected.** Count coherent gain over the
  bandwidth, time and oscillator range that the receiver can match. A wide-band component
  across 2.2 kHz calls for timing near 57 µs and may need a delay-rate search. The per-tone
  branch still serves paths with a different phase response. [Coherent search](#8-receiver-requirements)
- **Hardware limited to an extremely narrow band**, in practice below 9 kHz. Tones are held
  for 8 cells of 1.28 s, 10.24 s. A 61 m antenna fed with 10 kW would pass a faster rate; the
  reasons are the receiver's.
  - With the frequency, timing and channel phase correctly matched, coherent energy integration
    can extend across tones. In the retained model, 10 s is a candidate aperture under the
    assumed spread bound. Even a single tone needs a suitable timing and frequency hypothesis.
  - The work of the coherent attempts follows the tone rate, as does the timing search of
    per-tone reception.
  - One cell (1.28 s) is the fastest tone offered there, for a link that turns out strong; 32
    cells (41 s) suit blocks longer than about three hours.
  - The search in millihertz bins through oscillator, clock and path drift is a transform
    across tone sums. Historical kernel projections favour the narrow bank and already
    exceed real time on some broad 24-bit searches. Complete CPU service for these
    oscillator envelopes remains unmeasured
    ([12.2](#122-phase-stable-paths-below-9-khz)).

At fixed 10.24 s airtime, 10 payload bits and 18 dB block energy, a paired native HF test
compared 64 one-cell tones with 32 two-cell tones. In the 0.5 Hz diffuse fixture, holding
tones raised correct per-tone admissions from 81 to 98 out of 120 and joint admissions from
90 to 101. The formats used their respective repeat counts and separately calibrated
thresholds. At 20 Hz spread, joint reception recovered 0 and 3 of 120 at that energy.
At 22 dB it recovered 81 and 85. These counts support testing held tones on a slow path.
The 32-component fading model closely reproduced the continuous Gaussian spectrum's
variation in block-average received power at 0.5 Hz. At 20 Hz it produced much more
block-to-block variation, so the fast-path counts apply to that finite model.
[Matched experiment and channel-model check](algorithms-matrixModem/block-reception/README.md#one-cell-versus-two-cell-hf-tones)

The reference rate costs sensitivity on slow weak paths. Profile selection is open decision 1 of
[section 14](#14-decisions).

Extra transmit time against the theoretical bound at −10 dB-Hz, per-tone reception, for a
steady path and for spreads of 0.1 / 1 / 3 / 10 / 30 Hz:

| Tone length | With a 4 s hop | Without FHSS |
| --- | --- | --- |
| 1 cell | 21.2 / 14.2 / 5.7 / 2.3 / 1.2 / 0.5 dB | 21.0 / 14.0 / 5.5 / 2.1 / 1.0 / 0.3 dB |
| 24 cells (3.84 s flat) | 8.3 / 2.2 / 0.6 / 0.3 / 0.2 / 0.2 dB | 8.1 / 2.0 / 0.4 / 0.1 / 0.0 / 0.0 dB |

- The one-cell tone gives up 12 to 13 dB of time on steady and 0.1 Hz paths, 5 dB at 1 Hz and
  1 dB at 10 Hz. In the weak-signal regime these correspond to about half as many dB of power.
- On strong links without hopping, the minimum number of tones can favor the one-cell
  choice. With hopping, profiles on the same hop period can tie when both reach the
  32-hop floor. Select from the rounded public block times in section 10.
- Coherent attempts remove the cost only where the path turns out steadier than expected
  ([section 8](#8-receiver-requirements), item 8). The gain then follows the receiver's coherent
  window, not the tone length. When the expected spread appears they gain no more than a held
  tone of about 4 divided by the spread.
- **A tone shorter than one cell is outside the proposed format.** For one-cell tones on
  this reciprocal lattice, the position count times the flat tone rate equals the nominal
  grid bandwidth. A held tone keeps the position spacing and reduces the tone rate. The
  `T/8` per-tone timing grid gives a 20 ms step for a 160 ms tone. Wide-band phase-coherent
  combination can require much finer timing even at this tone rate (section 8).
- **Where phase or amplitude would carry information instead.** Near the full bandwidth the
  signal approaches a single phase-keyed carrier when the occupied band allows only one position. This draft uses tones at full active amplitude.
  Noncoherent reception can distinguish changes in amplitude from energy. Different peak,
  average-power and deadline constraints can change the ordering of waveforms.
  [Power constraints and tone-length comparison](../_ref/algorithms-matrixModem/fsk-reconciliation.md#choosing-tone-length-and-power-constraints)
- Named held-tone lengths, and where each reaches diminishing returns with per-tone reception:

  | Held tone | Diminishing returns for |
  | --- | --- |
  | 0.4 s hop (2 cells) | Spreads of 10 Hz and more; steady paths near +10 dB-Hz |
  | 4 s hop (24 cells) | Spreads near 1 Hz; steady paths near 0 dB-Hz |
  | 40 s hop (249 cells) | Spreads near 0.1 Hz; steady paths near −10 dB-Hz |
  | 400 s hop (2,499 cells) | Spreads near 10 mHz; steady paths near −20 dB-Hz |
  | 4,000 s hop (24,999 cells) | Steady paths near −30 dB-Hz |

- **Sizing heuristic for a held tone.** The historical known-centre calculation found
  diminishing returns near the shorter of four divided by the assumed Gaussian spread
  and the block time divided by 32. For its steady path, the estimate was 3.2 divided by
  C/N₀ in hertz, giving about 0.3 s at +10 dB-Hz and 32 s at −10 dB-Hz. Under that
  presence model, 32 independently phased tones needed 2.6 dB more energy than a fully
  coherent block. Recompute the rounded duration at the chosen admission target.
- **What a held tone asks of the radio.** Both oscillators must hold to a fraction of the tone's
  reciprocal while it lasts: 25 mHz for 40 s, 2.5 mHz for 400 s. The hop-to-hop frequency error
  should stay within the path's spread, or within a few tenths of a hertz
  ([4.3](#43-hop)).

### 4.3 Hop

- **Structure.** A hop holds whole tones between its edges. At `j = 0`, the 0.4 s hop has 2
  cells flat and, at each edge, a 20 ms raised-cosine ramp and 20 ms of quiet. Longer hops have
  the same ramp and 60 ms of quiet, so a hop of `N` cells in all has `N` − 1 cells flat. The
  radio changes its hop frequency during a quiet edge. A tone keeps its position through
  all its cells. For `F` flat cells and `C` cells per tone, a hop offers `q = floor(F/C)`
  whole tone slots. Require `q ≥ 1`. If `C` does not divide `F`, leave the remaining
  cells idle. The named HF geometries divide exactly. Section 6 specifies the final hop.
- **Flat-window exposure.** A receiver using only the flat cells captures 80% of the
  full-power 0.4 s reference energy (0.97 dB loss). Including transmitted raised-cosine
  amplitude ramps gives 83.75% (0.77 dB). The flat-window losses at 4, 40 and 400 s are
  0.18, 0.02 and 0.002 dB. Extra time to reach an error target depends on the decoder and channel.
- **Retuning can introduce a new common phase in each hop.** The per-tone energy
  detector already discards that phase. Across-hop coherent gain requires a justified
  relative phase model. The reference FHSS calculation accumulates separate hop energies.
- **A new frequency in every hop acts like added Doppler spread** on a held tone, unless the
  tone is strong enough to locate itself. Block time against a known centre: within ±0.1 Hz,
  nothing at 1 Hz of spread or more and 1.3 to 1.7 times on a 0.1 Hz path; within ±1 Hz, 1.6
  times at 1 Hz of spread and 6 to 11 times on a 0.1 Hz path. A one-cell tone offset by 1 Hz retains approximately
  91.86% of its nominal-bin energy, a 0.37 dB loss before combining.
- **The 0.4 s hop is an engineering example.** US Part 15.247(a)(1) limits occupancy per
  frequency over specified observation periods in the 902–928, 2400–2483.5 and 5725–5850 MHz
  bands. Channel count and bandwidth conditions differ by band. This rule does not govern HF
  amateur operation. [47 CFR 15.247](https://www.ecfr.gov/current/title-47/section-15.247)
- **40 s is the recommended alternate for one-cell tones.** It loses 0.02 dB of flat-window exposure to its edges, keeps
  the concealment of hopping for 50 channels ([section 11](#11-concealment)), gives coherent
  attempts a 40 s window, and asks the oscillators for 25 mHz. With one-cell tones a longer hop
  adds no sensitivity by itself; hops longer than about 40 s are worth having only with held
  tones.
- **With FHSS, a block spans at least 32 hops** ([section 6](#6-message-format-and-error-correction)).

### 4.4 Phase key

**Mandatory in all cases: every tone is sent with a keyed phase of 0°, 90°, 180° or 270°, with a fresh scheduled value per tone. Consecutive values can coincide.** Tone, envelope and lattice are unchanged. The phase
carries no information and depends only on the key and the time, not on the position.

**Experimental HF intervals.** The shorter intervals in the settings table start at the
tone boundary. If the tone ends before an interval would finish, end the interval there.
Whether to offer these settings in an HF operating profile is open decision 2 of
[section 14](#14-decisions). The below-9 kHz profile uses one scheduled phase per tone.

**Phase ramp for a high-Q transmit antenna (below 9 kHz).** The reference moves evenly
from the previous keyed phase to the new one over the first 80 ms of the phase interval
(256 samples at 3,200 Hz). The narrow-antenna alternative uses 160 ms (512 samples).
Take the shorter rotation, with a keyed direction for a half turn.
The position still changes at the tone boundary, and the signal stays phase-continuous
throughout. The ramp depends only on the key and the time, so the receiver's reference follows
it.

- **Transient in the antenna model.** A plain half turn produced a period of reversed net
  real power, with 11 J returned over 2.6 ms for an 86 Hz antenna at 10 kW, and 41 J
  over 9.2 ms for a 24 Hz antenna. Equipment-specific protection limits remain open
  ([12.2](#122-phase-stable-paths-below-9-khz)).
- **The 80 ms ramp avoided reversal of net real power in the modeled cases.** A reflected
  traveling wave can still be present. A standing-wave ratio of 2.84 gives approximately
  23% reflected-to-forward power on the assumed reference impedance. The ramp is a frequency excursion of 3.125 Hz (quarter
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
- **Cost to the receiver.** A matched reference removes phase-template mismatch for the modeled waveform. Antenna attenuation and channel distortion still apply. A receiver that assumes a
  plain step loses 0.03 dB with 10.24 s tones and 0.24 dB with 1.28 s tones at 80 ms, and twice
  that at 160 ms. The timing spacing is unchanged.
- **Basis.** Calculated for a single-tuned antenna driven at constant amplitude
  ([`phase_slew`](algorithms-matrixModem/sub9-fsk-rate/results/phase_slew.md)). No transmitter
  or matching network was examined, and what a given transmitter tolerates is its
  manufacturer's figure. An observer sees a short frequency excursion at three tone changes in
  four, which is accepted below 9 kHz.

**Why it is mandatory**

- **Scope of the unkeyed reference.** The earlier calculation used aligned rectangular
  cells and a phase-stable channel. Position offsets relative to the grid origin complete
  whole cycles per cell. The common carrier phase at that origin can still rotate. The
  reported periodic mean of the unkeyed waveform was calculated under those assumptions.
- **The model advantage grows over the stated observation interval.** On a phase-stable path, against an observer measuring
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
- **Scope of the coherent comparison.** The cited calculation studies a particular
  statistic with an unknown schedule. The zero-key receiver and its schedule are public.
- **It limits perceived interference.** A nearby simple coherent receiver would lock onto the
  steady cell-rate component of an unkeyed signal. The cited calculation suppresses that particular mean component.
- **With few positions without FHSS** an unkeyed tone would also leave a steady line at every
  position, 6, 12 and 24 dB below the signal for 2, 4 and 16 positions. The keyed phase removes
  it.
- **Four phases, not two.** Squaring the signal removes a two-phase key and restores the pulse
  or line. In the simulation this needed tones about 10 dB stronger than the plain pulse, and
  detected nothing against four phases. Other statistics remain untested.

**What it costs the receiver**

- **One constant phase per aligned one-cell tone preserves its bin energy.** Per-tone reception takes each cell's energy,
  which a quarter turn of the whole cell does not change; it need not undo the key. The timing
  spacing is 20 ms with or without it. Coherent attempts undo it with a quarter turn of each
  cell's transform output (a swap and a sign change, exact on 4-bit values).
- **A constant phase over an aligned held tone likewise preserves bin energy.** The timing spacing is an eighth of the tone,
  as it would be without the key.
- **The timing rule.** For an isolated rectangular tone, the overlap contribution at offset
  `τ` has normalized power `(1−|τ|/T)²`. At half of a `T/8` grid step it loses 0.56 dB.
  A neighboring tone contributes its own samples. At the same position with opposite phase
  this gives `(1−2|τ|/T)²`, or 1.16 dB loss at that offset. The overlap-only figure cannot
  bound a sequence or the picked search maximum. [Sample calculation](../_ref/algorithms-matrixModem/fsk-lattice-validity.md)
- **Phase steps inside a held tone cost timing hypotheses.** A step every cell needs 20 ms
  spacing where held tones of 4, 40 and 400 s with one phase need 0.5 s, 5 s and 50 s: 24, 249
  and 2,499 times the hypotheses per dimension, where timing is uncertain.
- **More than one phase per cell increases timing work.** The receiver removes the
  scheduled phase on samples at each tested alignment. For two or four intervals per HF
  cell the proposed coarse timing step shrinks to 10 or 5 ms. Across a fixed uncertainty
  span this gives two or four times as many coarse alignments. At 2.2 kHz the candidate
  fine timing spacing remains about 57 µs, or about 352, 176 and 88 intervals per
  20, 10 and 5 ms coarse step respectively. Actual costs depend on boundary handling
  and reuse across alignments.
- **Delay spread changes the contribution of each path near transitions.** For a
  single excess delay `τ` shorter than a phase interval `T`, the previous interval
  occupies fraction `τ/T` of the current window on that path. This is a time fraction.
  The matched energy depends on the delayed path's power and phase, adjacent positions
  and interference with the direct path. A weak echo, an in-phase repeat and a cancelling
  echo with the same delay can yield different results. The earlier hand estimates cannot
  supply a universal HF tone-rate or dB-loss limit. [Finite-window and multipath checks](../_ref/algorithms-matrixModem/fsk-lattice-validity.md)
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

- With FHSS disabled, tones have no hop-amplitude ramp or quiet guard. The configured sub-9 kHz
  phase ramp still applies. Predict phase in a specified baseband/carrier convention, including
  the common phase accumulated at the chosen band origin. Relative lattice spacings make whole
  cycles per cell. The path can still impose a different complex gain on each position.
- The receiver's coherent attempts may then span the whole block
  ([section 8](#8-receiver-requirements), item 8).

## 5. Alphabet and keyed placement

- **256 internal symbol values.** For a grid with `N ≥ 256` positions, draw a keyed
  permutation of the grid for each tone. The first 256 entries give a different position
  to each byte. For the default grid, 96 of 352 positions are unused on that tone.
- **Fewer than 256 positions.** Use a configured pool of `P = 2^floor(log₂ N)` positions
  for `2 ≤ N < 256`. The placement maps all 256 bytes onto that pool with exactly
  `256/P` bytes per position. Redraw the grouping for each repeat. A non-power-of-two
  grid needs an agreed pool of `P` of its `N` available slots on both ends. The named
  2-, 4-, 8- and 16-position fixtures use their whole grids. In a coherent decode of a 10-bit block the smaller pools cost 2.6,
  0.8, 0.2 and 0.0 dB for 2, 4, 8 and 16 positions (simulated; a second, smaller simulation
  agrees within its scatter of ±0.2 dB). Per-tone reception prefers more positions as well, by
  1.5 dB between two and sixteen (simulated for the sub-9 kHz formats on a steady path).
  **Below 9 kHz sixteen positions are used**, for which this fixture resolved no additional loss
  ([12.2](#122-phase-stable-paths-below-9-khz)). A value-dependent phase is a candidate to test under its phase-coherence assumptions; it is open
  decision 18 of [section 14](#14-decisions).
- Adjacent positions are allowed. With a fresh placement for every tone and the spread-weighted
  evidence of section 8, simulation shows no loss against wider spacing at spreads up to 30 Hz
  for tones of 2 and 24 cells. A separate one-cell calculation at 30 Hz spread finds similar
  average discrimination for two wide placement pools, while two nearby positions alone lose
  substantial pairwise discrimination. A frozen two-ray channel can put the entire narrow
  alphabet in a notch. [One-cell and two-ray controls](../_ref/algorithms-matrixModem/fsk-lattice-validity.md)
- The reference draws a fresh placement at each tone with no prescribed recurring spacing
  or period. A finite random schedule can have repeated spacings, positions or whole
  codeword coincidences. Check the actual projected code when claiming identifiability.

## 6. Message format and error correction

**Block.** A block carries `k` = 10, 16 or 24 information bits (configured) as 32 code symbols,
each sent on `L` tones: 32·`L` tones per block.

- **Code.** The value of symbol `s` is `G_s·c XOR m_s` over GF(2). Here `c` is the
  encrypted block, `G_s` is a keyed 8 × `k` binary matrix and `m_s` is its 8-bit mask.
  When the surviving positions determine complete bytes, the stacked rows of those
  matrices distinguish all payloads exactly when they have rank `k`. When a position
  combines multiple byte values, count distinct *surviving position sequences*. If that
  count is `D`, noiseless equiprobable words have error probability at least `1−D/2^k`
  even before adding noise.
- **Repeats.** Select the positive integer `L` from the configured link, channel and
  calendar. For a narrow pool the tuple-space floor is `ceil(8/log₂ P)`. At 16 positions,
  two independent balanced maps give a mean of only 167.77 distinct position pairs for
  the 256 byte values. Other symbols can still resolve the payload. With hopping off
  and `L = 2`, a 64-tone block takes 655.36 s with 10.24 s tones, 81.92 s with 1.28 s
  tones, or 43.69 min with 40.96 s tones. [Identifiability calculation](../_ref/algorithms-matrixModem/fsk-code-identifiability.md)
- **Interleaving.** Shuffle the multiset containing exactly `L` copies of each of the 32
  symbol indices for each block. A shuffle can place several copies on the same hop.
  Tolerance of a burst depends on the actual hop assignment and surviving positions.
- **Hop spanning and calendar rounding.** For the hop-aligned sizing profile, let `q` be
  the number of complete tones that fit in one hop (section 4.3). Require `L ≥ q` so the
  `32L` tones fill at least 32 hops' worth of slots. Reserve `ceil(32L/q)` whole hop
  periods. Leave any unused slots in the final hop inactive and charge the full period.
  The receiver knows those slots from the calendar. Only a complete scored public
  **block** can count toward the two-quiet-block rule. For example, one-cell tones at
  a 4 s hop give `q = 24`, a floor of 768 tones and a 128 s minimum block. With hopping
  off, the scheduled time is `32L` times the tone duration.
- **Decoding** scores all `2^k` messages at every configured hypothesis. This finds the
  maximum of the chosen metric. It implements maximum likelihood only when that metric
  represents the observation law, including quantization, channel uncertainty and erasures.
  The block supplies the timing evidence on the configured calendar. Acquisition of an
  unknown epoch or block ordinal still needs its own hypotheses and tests.
- **Configuration at both ends.** The chosen tone length, grid, repeat count and hop
  geometry determine the calendar. The interleaver permutes the scheduled repeats
  within that geometry. Usable energy and distinguishable payloads after any hop loss
  depend on which observations survive. The encoder always produces 32 symbols.
- **Evidence for the code.** The short affine code supports exhaustive comparisons at the
  proposed block sizes. Earlier random-code/energy-bank comparisons gave gaps of about
  0.2 dB at 10 bits and 0.3–0.7 dB at 20 bits under their own models. These values do not
  specify the sensitivity of this placement and detector. Repeats supply energy and sometimes
  diversity. Replacing independent parity with repetition can cost information and correlated
  loss can erase multiple copies together. Compare on equal elapsed time, transmitted energy,
  false admission and payload error. [Earlier coding study](../_ref/algorithms-matrixModem/Fable-5.2/coding-and-message-structure.md)

**Message.** A raw message is a run of configured `k`-bit words on a public calendar
derived from the system clock. Payload bits, including their decoded values, do not
select the geometry of the next public block.

- While an episode is open, hold a gap provisionally. If another word arrives before
  closure, retain the intervening slots as erasures at their public ordinals. Two consecutive
  scored absences close the episode. The next admitted word opens a new episode. An
  unobserved, wholly erased or unscored slot breaks the consecutive-quiet
  count. The receiver needs two newly consecutive scored absences to close. A false
  admission in trailing quiet can therefore postpone closure even when the operator
  recognizes the word as gibberish.
  An *observed* slot in which propagation completely suppresses the signal can score as
  quiet even while the transmitter continues sending. Count a premature closure even if
  later reception opens a new episode. Keep the public ordinal through closure. [Receiver state and replay](algorithms-matrixModem/block-reception/README.md#coherent-branch-and-protected-per-tone-fallback)
- The long-message research profile carries successive 24-bit words. A final partial word
  has no intrinsic bit count. For example, a final block made from two bytes and eight
  zero padding bits has the same payload as three bytes ending in zero. Two quiet blocks
  mark the receiver's physical completion. Exact file reconstruction requires an agreed
  application encoding for the final partial word. A verified file transfer also needs
  a defined integrity policy. Both remain open for the file profile. [Stream and endpoint requirements](../_ref/algorithms-matrixModem/Sol-6.2/stream-keys-and-spectrum.md#authentication-wrong-key-admission-and-safe-streaming)
- The earlier comparison estimated a cost of about 2–3 dB at 100 bits and
  3–5 dB at 300 bits for short blocks against one long code under its own assumptions. The first
  candidate is a CRC-aided tail-biting convolutional code over the tone alphabet with list
  Viterbi decoding; the main alternative is a nonbinary LDPC code of the Q65 kind. They have not
  been compared, and a long-block format is not specified until one is qualified.

## 7. Keys and schedules

- Use the shared version-4 keyfile and distinct Matrix schedule purposes for AES-256
  generation of the position map, symbol assignment, matrices, masks, hop and phase sequence.
  Use the nominal configured shift rounded to the operator's multiple of 10 Hz, together
  with the appropriate epoch and ordinals. A hop retune or measured frequency correction
  leaves that key context intact. Fixed serialization, tie rounding and test vectors for
  these proposed Matrix-specific addresses remain interoperability work.
- Encrypt the payload before coding with the repository's length-preserving dual-lane
  construction. The AES-256-CTR and Serpent-256-CTR keystreams are XORed with the payload.
  Reusing the same key and address reuses the mask. Root lengths describe stored material.
  The brief's requested 512 bits of entropy require a separate source assessment. Native
  receiver experiments use seeded surrogate schedules and post-encryption words. [Keyfile and stream design](../_ref/algorithms-matrixModem/Opus-5.5/09-keys-schedules-spectrum-and-stream-contract.md#findings) · [Finite experimental schedules](algorithms-matrixModem/block-reception/README.md#fixed-protocol-before-sampling)
- Encryption is always on, with the published zero key or a loaded keyfile, as in the brief.
- At a fixed key and block ordinal, decrypting a 10-bit winner XORs it with a fixed mask.
  A uniform noise winner therefore displays as a uniform word. The mask permutes any
  bias among the labels, and a one-bit error in real ciphertext changes one plaintext bit.
  [Stream construction](../_ref/algorithms-matrixModem/Opus-5.5/09-keys-schedules-spectrum-and-stream-contract.md) · [CTR error properties, Appendix D](https://nvlpubs.nist.gov/nistpubs/Legacy/SP/nistspecialpublication800-38a.pdf)
- A signal under a different schedule can contribute structured energy. Include wrong-schedule
  traffic as its own false-admission case alongside quiet noise and occupied interference.
  A public schedule and a threshold supply no sender authentication.
- The zero-key configuration is public and reproducible. US amateur rules restrict
  obscuring a message, one-way transmissions, authorized emission types and identification.
  A publicly decodable configuration still needs a specific band and operating basis. The
  private-key research profile is distinct. [Prior Matrix operating review](../_ref/algorithms-matrixModem/Opus-5.5/09-keys-schedules-spectrum-and-stream-contract.md)

## 8. Receiver requirements

1. **Front end.** Use fixed gain, disable the transceiver's blanker and noise reduction, and
   pass the configured search band and its guard bins linearly. The historical 20–30 dB
   separation between the noise and quantization floors refers to the input converter. Verify that
   interferers also fit its upper range. Clipping before capture loses information.
2. **Conditioning before four-bit encoding.** Down-convert and decimate to the lattice
   rate. Estimate the component noise scale from a lower quantile. The reference sample
   receiver uses the lower quartile of complex sample power, calibrated for circular
   Gaussian noise. Interference, masks and gain changes affect this estimate. The proposed
   interference front end also weights by inverse local power and conditionally removes
   sparse lines detected using at least 1 s of low-sidelobe analysis. Include the choice
   of conditioning in calibration. Section 9 gives the measured scope.
3. **Four-bit samples.** The reference rounds each component in units of 0.45 times its
   estimated standard deviation. Integers −7 through +7 carry 15 levels. Either component
   out of range erases the complex sample. Preserve reception state for unusable input
   through the presence decision in item 9.
4. **Transform.** Form rectangular-window projections for every required position and
   offset within the captured band. For a held tone, retain the information needed to
   form its longer aperture after undoing the known phase schedule. Its bin spacing is
   the reciprocal of the held duration. Adding aligned on-grid cell bins reproduces the
   matching on-grid projection. At a half-cell-bin offset, two 512-sample projections
   from the original bin retain only 40.53% of the matched signal power even after
   aligning their phases. Intermediate frequencies require within-cell projections at
   those frequencies or an equivalent long transform of the samples for full gain. Search width,
   edge bins and partial sample masks are part of the geometry. Measure the applicable
   CPU FFT, GPU FFT and matrix-product routes. [Window and frequency checks](../_ref/algorithms-matrixModem/fsk-lattice-validity.md#window-frequency-and-noise) · [Routing](#134-routes-and-scheduling-rule)
5. **Evidence.** For each position, a spread-weighted sum of bin energies for each hypothesis of
   a small bank (one bin, 10, 30, 60, 120, 250 and 500 Hz). The spread hypothesis is chosen per
   block, never per tone. The finite-width Gaussian cases use full two-standard-deviation
   widths. Admit a spread and centre-offset combination only where the sampled and analog
   passbands support its specified weighted aperture, including at the outer positions.
   A truncated aperture needs weights, noise normalization and calibration for the bins
   actually observed. Physical frequencies beyond the passband supply no evidence and
   cannot be replaced by wrapped FFT bins. Section 3 gives the available guard space.
   - Normalise per position over time and per tone over positions by medians. With few positions
     the noise reference comes from the bins around the band, never from the positions alone.
   - With FHSS, estimate the noise level of every hop from that hop's own bins, weight the hop's
     evidence by the inverse of it, and erase a hop that is beyond range.
   - A hop retune can leave an unknown offset common to that hop's tones. With one held
     tone per hop, the proposed likelihood sums the per-centre likelihoods weighted by
     the assumed centre probabilities, then takes the logarithm. A simple mean assumes
     equally likely centres. For several tones sharing an offset, evaluate their evidence
     at a common offset before combining offset hypotheses. Keep this uncertainty separate
     from propagation spread and from any frequency or clock error shared across hops.
   - Quantize to four bits with the top level as a clip at 4 to 8 times the noise mean.
6. **Accumulation.** For additive per-tone evidence, accumulate one integer score for each
   of the 256 coded byte values per symbol and hypothesis using each repeat's placement.
   Coherent scores require the complex observations and window geometry in item 8.
7. **Search and decode.** Every timing, frequency-offset, drift and spread hypothesis is scored
   against every message. **Hypotheses that share work are not an outer loop around it.**
   - *Per-tone reception:* timing and spread hypotheses use their own evidence tables.
     Reuse sample projections and message indices where their geometry agrees. The two
     measured routes were one pass with the evidence of all
     hypotheses for a symbol value stored together, or a pass per hypothesis with its table in
     fast memory. Measured for 7, 56 and 350 hypotheses, the first is faster on CPUs once the
     hypotheses are many (laptop 1.6, 6.0 and 6.7 times; 27 threads of an EPYC 9554 0.7, 4.2
     and 11.8) and the second is faster on the GPU (by 1.7, 8 and 10). The scheduler chooses
     ([13.1](#131-rules-that-bind-every-implementation)).
   - A single coherent frequency hypothesis suffices only where the residual path and
     relative-reference errors fit the tested range for every required window.
   - *Coherent attempts with many frequency or drift hypotheses:* for each message and timing
     offset the receiver gathers the bin value of the position that message puts each tone on,
     once. Every frequency bin of a window is then one output of a transform across those tone
     sums, and every drift hypothesis a de-chirp before it. Each rung of the ladder is searched
     only on the bins its window length resolves. An epilogue forms the energies, adds a
     rung's windows and keeps the best.
   - *The transform across tone sums is a routed stage* like the first transform: a CPU FFT,
     a GPU FFT or one matrix product on Tensor Cores (messages × tones times tones ×
     hypotheses), chosen by the scheduling rule of [section 13](#13-computation-and-simulation).
     Stage timing projected over the historical 24-bit, 512-tone search shape gives 1,643 s
     with laptop FFTW and 11.4 s with Blackwell FP16 GEMM. See section 12.2 for the sampled
     messages, physical drift correction and omitted work.
   - Reuse the position indices across timing offsets. Within a shared aperture, a phase slope
     models a delay of each stationary tone. For moved boundaries, form shifted bin values,
     add the entering/leaving samples, or establish the loss of reusing the old aperture.
   - Sum short tones into sub-blocks when the residual frequency and curvature over the
     sub-block preserve the required sensitivity. Historical laptop kernel estimates for 512
     sub-blocks were 2,148 s with 2.56 s tones and 4,667 s with 0.64 s tones. The estimate
     without summation at 2.56 s was 7,956 s. All exclude physical window formation.
   - *Where the line lies.* A decode per hypothesis is the right form for one or a few
     hypotheses and the wrong one for many. With the kernels measured the transform takes over
     at about 30 hypotheses on the laptop and 10 on the GPU; the scheduler decides from its
     measured costs.

   The proposed coarse timing step is one eighth of the phase interval, or 20 ms for a
   160 ms interval. Specify a clock-rate step by the accumulated timing slip it creates
   over the block. For a fractional rate difference Δε and duration T this slip is T·Δε.
   Coherent sums over tones on different positions use a proposed offset spacing of
   1/(8 × band span) where that is finer: 10 ms for the 12.5 Hz used below 9 kHz, whatever the
   tone length. Coherent windows add frequency bins half the window's reciprocal apart and
   drift-rate hypotheses ([12.2](#122-phase-stable-paths-below-9-khz)).
8. **Coherent attempts and the per-tone fallback.** Evaluate the configured coherent
   windows over all used positions and the per-tone branch on every scorable block, including
   quiet blocks. Both count towards the receiver deadline. Use item 9 to make one decision.
   Score the data in the configured sideband across the supported timing and frequency
   ranges without waiting for pilot lock, a Helper indication or a visible trace. After a
   miss, score the next eligible public block. Dedicated searches for a reversed spectrum,
   mis-set tone spacing or damaged tones require explicit selection. In ordinary operation,
   neither a miss nor a persistent bright trace triggers these searches. The format may accommodate
   optional recovery without extra airtime or redundancy. With the option off, preserve
   the ordinary receiver's sensitivity, false-word performance, available DSP workspace
   and maximum sustained throughput, including during prolonged unsuccessful acquisition.
   [Optional fault experiments and their measured costs](algorithms-matrixModem/block-reception/fault-tests.md)
   - **Span.** Undo the phase key first. Without FHSS the maximum span is the block. With
     FHSS, allow an unknown phase for each retune, combine coherently only inside each hop,
     then add hop energies. A held tone occupying the entire active hop has no second
     tone in the same hop. Its matched projection and any configured shorter within-tone
     windows provide the available coherent evidence there.
   - **Tables.** Store complex-bin components as signed four-bit codes after the
     configured normalization. Specify the component scale and weight for each aperture.
     Summing codes with varying scales changes the effective tone weights and requires
     calibration of that statistic. Bound the stored tables by the admitted batch size.
   - **Window ladder.** Begin with the block or single-hop span and subdivide to the
     configured shortest window (HF reference target 1/64 of the block). If a hop is
     already shorter, retain its full active span. For each candidate, add the squared
     magnitudes of the coherent projections for that rung's windows. Include the final
     partial window and its actual exposure in the score and null calibration. Larger
     windows can reuse adjacent complex sums when they share the same phase correction.
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
   - **Residual drift.** The Fourier-bin widths for 40 s and 400 s windows are 25 mHz and
     2.5 mHz. Residual offset, drift and phase noise after correction determine how much
     energy the window retains. Correct significant curvature within a window or qualify
     the loss on the stated oscillator and path envelope. Moving between bins from one
     window to the next only corrects the motion of their centres. Shorter windows can
     recover some lost sensitivity. The per-tone fallback has its own admission threshold.
     [Physical drift and window bounds](../_ref/algorithms-matrixModem/fsk-search-validity.md)
   - **Single stable ray.** A constant delay gives a linear phase slope across frequency.
     After removing that slope and the known transmit phase, a common-phase score assumes
     the residual channel and hardware phase are stable across the used positions. The
     proposed timing step 1/(8 × band span) is 57 µs for 2.2 kHz and 1.25 ms for 100 Hz. The slope
     can be applied to stored bin values for a common aperture. Boundary samples require
     explicit correction or a tested approximation. The fine
     steps are further hypotheses of the one pass over the messages, with their table entries
     stored together; they multiply the additions, not the enumeration. Over 2.2 kHz that is
     about 350 fine steps for each 20 ms step: as kernels, 9.7 s per coarse step for 24 bits
     on a laptop CPU in one pass (44 s as a pass per step), and 66 ms on the GPU as 350 passes
     with real entries.
     A slow change of the path's delay, as the ionosphere's height changes, is far below the
     20 ms spacing but not below 57 µs, and adds a delay-rate search that is not sized.
   - **Multipath.** Unequal delays can change gain and phase across positions. A single
     phase correction over the band can then lose energy even when each path is individually
     stable. The suggested per-position phase model requires repeated observations and
     its own null calibration and work estimate. The two-ray sample test below exercised
     the common-phase and per-tone branches.
   - **Diffuse path.** The coherent gain depends on the joint time and frequency correlation.
     The earlier diffuse examples gave little gain at 10 Hz and above. Specular components
     or a multimodal Doppler spectrum at the same quoted width can give a different result.
     Reserve a stated false-admission budget for the per-tone branch and test its sensitivity
     after combining decisions.
9. **Presence and word selection.** The reference decision reserves half of the public
   block's false-admission budget for the maximum over the per-tone search and half for the
   maximum over the coherent search. Each maximum includes all that branch's messages,
   alignments and other configured hypotheses on the same samples. Freeze a deterministic
   tie order for equal scores. Train a separate strict-exceedance cutoff for each maximum.
   If the per-tone branch exceeds its cutoff, return
   its word, including when the branches disagree. Otherwise return the coherent winner
   if it exceeds its cutoff. If both scored and neither crossed, return a scored absence.
   An unavailable required branch or unusable input produces an unscored block and breaks
   the consecutive-quiet count. The sum of valid branch budgets bounds the union without
   assuming independence. Changing the split, winner priority, search or conditioning
   requires evaluating the resulting joint decisions again. For short messages read by an
   operator, use **10⁻⁴ per quiet public block**
   as a provisional design target. If achieved on one continuously monitored calendar, it
   gives about 0.84 raw false blocks per quiet day at 10.24 s per block, or about one every
   76 quiet days at 655.36 s per block. Select the threshold using the planned listening
   time, the calendars monitored and the number of false indications the operator can tolerate.
   If only eight of the 1,024 values make sense in context and words admitted from noise
   are uniformly distributed, one in 128 false words will look plausible. Decryption
   preserves any bias among the winning words (section 7). Count every false admission,
   including words a reader dismisses. Operator recognition remains unmeasured. This
   rationale applies to attended reading. File integrity and the two-quiet-slot end decision
   are evaluated from the received data.
   Freeze thresholds on training data and evaluate fresh quiet blocks, interference,
   wrong-schedule traffic and outage sequences. Decoy keys on one capture share noise.
   Report missed active blocks, admitted wrong payloads, premature or delayed ends and
   operator-visible false messages separately. [Admission target and calibration](../_ref/algorithms-matrixModem/fsk-presence-calibration.md#how-often-an-operator-sees-a-false-word)
10. **Precision.** Four-bit input component codes, evidence entries and stored complex-bin
    component codes are design targets. Accumulate integer evidence without overflow. The
    sample receiver represented these codes in wider containers and used floating-point
    FFTs, rotations and coherent sums. It tests these quantization points. Qualification
    of four-bit transform arithmetic remains open (decision 23). Size the storage,
    intermediates and squared magnitudes from the configured maximum length and scales.
    At 420,000 equally allocated tones an additive symbol entry can reach 196,875, and one
    coherent energy can need 44 unsigned bits even with four-bit input components.
    [Accumulator bounds](../_ref/algorithms-matrixModem/fsk-reconciliation.md#what-reducing-the-work-can-buy)

**Sample-level result.** With 10 bits, 64 one-cell tones, hopping disabled, and an 18 dB
block, the new bounded search admitted the correct payload on 115/120 two-ray blocks when
it combined the coherent and per-tone branches. A per-tone-only baseline admitted 15/120
on the same draws. On a path resetting phase every tone, the corresponding counts were
91/120 and 105/120. Reserving a branch budget preserved the fallback but cost some of its
sensitivity. The joint rule admitted 3/400 quiet blocks. These thresholds target a nominal
1% family false-admission rate. [Full fixture, grids, calibration and uncertainty](algorithms-matrixModem/block-reception/README.md#coherent-branch-and-protected-per-tone-fallback)

On the one-cell HF fixture, 19,999 quiet blocks set the thresholds. At the nominal
**10⁻⁴ target**, the receiver admitted **one of 30,000** independent Gaussian-noise
holdouts. The exact one-sided 95% upper bound was **1.58 × 10⁻⁴**, above the design
target. At the 1% setting it admitted 287 of the same 30,000. On paired signal trials
at 0.5 Hz and 18 dB, the two settings recovered 50 and 93 correct words out of 120.
At 20 Hz and 18 dB, the looser setting gained two correct words and admitted two wrong
ones. On a 36 dB transmission using another schedule, it admitted six of 160 blocks,
compared with zero of 160 at the 10⁻⁴ setting. All trials used 10.24 s blocks and the
same search. Repeating the run with the same seeds reproduced the counts. [Frozen thresholds, signal and wrong-schedule results](algorithms-matrixModem/block-reception/README.md#one-false-admission-per-ten-thousand-quiet-blocks)

Properties to test:

- Verify the one-tone influence bound for a fixed additive clipped evidence table. A coherent
  squared sum has cross terms with earlier tones, and estimating a noise scale can couple
  several observations.
- Compare additional tones and receiver branches at fixed energy or fixed per-tone power as
  appropriate, and at the same family false-admission rate. A mismatched extra observation can
  increase error. Report when the per-tone branch loses acceptance after joint calibration.
- Test the proposed 99% target at 30 Hz under a specified channel law, bandwidth, signal level
  and outage allowance. A coherent notch or two consecutive extinguished blocks can defeat a
  universal guarantee.
- Measure the 0.3 dB conditioning/quantization loss target on matched Gaussian fixtures and
  test separate colored, interfered and erased cases.
- Work counts: for a block, report counted first transforms, gathers, turns, transforms across tone
  sums and passes over the messages against the reference counts of
  [13.1](#131-rules-that-bind-every-implementation), and explain extra work used to respect workspace or improve complete service.
- Scheduling: from a cold start, on each target machine and for both extreme configurations
  (default HF and sub-9 kHz), the whole job finishes within a stated factor of the best fixed
  assignment as 13.1 defines it (placeholder 1.2; the prototype reached 0.94 to 1.12).
- Simulation: on both experiment machines of [13.5](#135-simulation) a run reports its
  throughput, per-stage times and utilization, and its per-configuration work is done once
  whatever the number of trials.

### 8.1 Reception after antenna pointing loss

For a steady, frequency-flat pointing loss on a single path, reduce the received power
of every transmitted tone by the same number of decibels and hold receiver noise fixed.
Report the resulting C/N₀ or block E/N₀. A claim in degrees off boresight also needs the
antenna pattern. Give the waterfall and the decoder the same capture, passband and
observation time. State the display's time and frequency resolution, visibility rule and
false-alarm rate, and the receiver's timing, frequency and public-calendar uncertainty.
Count correct, wrong and missed words both across all transmissions and among visible
blocks. Check false words on quiet and unrelated traffic, and message completion on
continuous input. A claimed dB gap needs both visibility and decoding thresholds at
stated success probabilities, with uncertainty. [Link model and field precedents](../_ref/algorithms-matrixModem/fsk-visible-signal-recovery.md#what-a-pointing-loss-does-to-the-samples)

On the configured ten-bit calendar, each simulated word occupied 10.24 s on a steady
path. At 16.5 dB block E/N₀ the ordinary receiver recovered 157/160 words while the
software waterfall flagged none. At 24 dB it recovered 160/160, including all 104
visible words. At the prespecified 27 dB point all 160 were visible and decoded
correctly, giving a one-sided 95% upper bound of 1.855% on failure given visibility
there. No visible word was missed or decoded incorrectly at any of the nine sampled
levels. Human recognition, a moving antenna pattern, blind calendar acquisition and
field sensitivity remain unmeasured. [Paired measurements and reproduction](algorithms-matrixModem/block-reception/pointing-loss.md)

## 9. Margin, interference and lost hops

- **SNR path-loss margin** (3, 6, 10, 13, 16, 20 dB) lowers the C/N₀ the block is sized for. It is
  shared by path-loss error, fading and noise deciles, receiver losses and interference.
- **Erasures.** In the earlier orthogonal-signalling calculation with branches near 0 dB
  at the design point, margins of 3, 6, 10, 13, 16 and 20 dB tolerated about 57, 77, 87,
  90, 91 and 92% independent flagged erasures at a 10⁻³ message error criterion (worse
  of the modeled 10- and 100-bit rows). A design using 18 stronger branches gave 36–68%
  over the same margin range for ten bits. These calculations assume that the decoder
  knows which observations were erased. The available margin also depends on how much
  was spent on other losses. [Original erasure model and branch counts](../_ref/algorithms-matrixModem/Fable-5.2/reviews/review-coding-interleaving.md)
- **Lost hops reduce observed energy and can remove diversity.** The table below counts
  the surviving exposure for equal-power known erasures. It assumes that usable symbol
  evidence is spread across the surviving hops.

  | Hops lost out of 32 | Cost in transmit time | Margin that covers it |
  | --- | ---: | --- |
  | One or two | 0.14 to 0.28 dB | any |
  | A quarter | 1.2 dB | 3 dB |
  | Half | 3.0 dB | 6 dB |

  Four deterministic 10-bit schedules on 16 positions retained distinct codewords after any
  26 out of 32 two-tone hops were erased, assuming perfect surviving labels. That result
  concerns four specific codebooks and is separate from their decoding error in noise.
  For independent per-hop loss with probability 1/2 and an assumed 77% erasure budget,
  32 hops exceed that budget with probability 0.0010512, and 33 hops with probability
  0.0006594. Those are budget exceedances. With mean bad runs of ten hops at the same
  stationary lost fraction, all 32 hops are lost with probability 0.01908. That event alone
  precludes 10⁻³ reliable delivery. Channel memory and symbol coverage matter even when a
  long-run lost fraction is known. [Burst-loss calculation](../_ref/algorithms-matrixModem/fsk-code-identifiability.md)
- **Swept interference.** At 7–70% in-band duty and 0–60 dB above passband noise, the
  better of two conditioning chains for each tested coherent radar lost at most 1.8 dB
  in the weak-tone deflection metric. Applying comb excision to the 10 Hz line-spacing
  fixture at +60 dB cost 4.7 dB, compared with 1.8 dB for weighting and erasure alone.
  A receiver has to choose from its observations or calibrate both chains as additional
  search branches. The historical 1–2 dB single-radar and 3–4 dB harder-case margins
  are planning estimates. The study did not demonstrate those margins for decoded
  messages or strong incoherent sweeps. [Conditioning chains, metric and cases](../_ref/algorithms-matrixModem/Fable-5.2/othr-and-interference-tolerance.md)
- **Below 9 kHz** the interference is sferics and mains harmonics, not radar. The conditioning of
  section 8 is of the same kind as established practice there (wideband blanking or clipping worth
  about 20 dB at 8 kHz, hum removal) but has not been run on that noise. Harmonics near 8 kHz
  wander by 2.5–10 Hz rms; on the steadier grids about 13 Hz stays clear of the 50 Hz and 60 Hz
  families together, which holds sixteen positions 0.78125 Hz apart
  ([12.2](#122-phase-stable-paths-below-9-khz)). Where a railway supply at 16.7 Hz is in range
  no such slot exists: its harmonics are 16.7 Hz apart and one lies near 8,266.7 Hz. A line that
  sits on one of sixteen positions for a while is then left to the excision of section 8 and to
  the margin.
- **Block span.** A single flagged outage of length O removes about O/T of the exposure
  from a uniformly occupied block of duration T. Using the earlier 77% and 57% budgets
  in that approximation gives T ≥ 1.30 O and T ≥ 1.75 O respectively. Repeated bursts,
  hop guards and loss of all observations for a symbol change the result. The interference
  study proposed ten burst lengths as an interleaving design point. Check it against
  the actual keyed placements, duty and burst persistence. Repetition across blocks for
  long closures needs a separate message-level delivery and completion model.

## 10. Sizing and expected duration

Choose the word size, channel, admissible search and block calendar before transmission.
The tables below size **one transmitted ten-bit word** for a true-template miss probability
of 10⁻³ in the stated model. They use the attended quiet-block target of 10⁻⁴ and the
reference allocation in section 8. The per-tone and coherent families each receive
5 × 10⁻⁵ across their candidates. A correct template can cross its threshold while a
different word wins. Correct payload, false admission in interference, processing backlog
and message completion require receiver measurements.

**Ten bits against one bit.** An isolated presence-test model helps compare payload sizes.
With 400 nuisance hypotheses per word, giving ten bits the full 10⁻⁴ allowance and one
bit the historical 10⁻⁶ allowance gives **1.059×** the unrounded weak-tone airtime.
Giving both 10⁻⁴ gives **1.275×**. The ten-bit requirement at 10⁻⁴ is 13.3% below its
10⁻⁶ value before block rounding. Reserving half the budget for the other branch changes
the required deflection from 86.73 to 88.75, against 99.98 at 10⁻⁶. These calculations
give an 11.2% unrounded ten-bit saving for the reserved per-tone branch against the
historical single-statistic 10⁻⁶ case. A receiver threshold change leaves a scheduled block's
duration fixed. [Selectable-budget model and historical ratios](algorithms-matrixModem/fsk-lattice/results/admission_tradeoff.md)

The earlier common-10⁻⁶ calculation gave ten bits 1.10–1.30 times the one-bit requirement
across its weak-tone and steady-look cases, and 24 bits 1.25–1.73 times. Ideal orthogonal
signaling sized solely for payload error gave ten-bit ratios of 2.48, 2.01 and 1.77 at
error probabilities of 10⁻², 10⁻³ and 10⁻⁴ respectively. These models address different
events. [Historical presence sizing](algorithms-matrixModem/fsk-lattice/results/lattice_receiver_section15.md) · [Short-message energy](algorithms-matrixModem/short-message-energy/README.md)

### Hopping HF profiles

For the Gaussian-deflection approximation, assume 400 nuisance hypotheses per word, a
matched Doppler centre and width on the true template, Gaussian noise and no interference
or additional link margin. A hop of 0.4 or 4 s has 2 or 24 flat cells. One-cell tones score
each 160 ms cell. A held tone spans one flat interval of 0.32 or 3.84 s. In both cases
the block contains exactly 32L tones, where L is the number of repeats of each code symbol.
Section 6 sets the 32-hop floor. A partially occupied final hop costs its whole public
period. The table quotes rounded **elapsed block periods**. Only active samples supply
signal energy. Doppler width is twice the standard deviation of the modeled spectrum.

| C/N₀ | Doppler width | One-cell, 0.4 s hop | One-cell, 4 s hop | Held, 0.4 s hop | Held, 4 s hop |
| ---: | --- | ---: | ---: | ---: | ---: |
| +10 dB-Hz | steady through 1 Hz | 19.2 s | 128 s | 12.8 s | 128 s |
| 0 dB-Hz | steady | 12.8 min | 10.7 min | 7.04 min | 2.13 min |
| 0 dB-Hz | 1 Hz | 13.3 min | 11.1 min | 8.11 min | 4.27 min |
| 0 dB-Hz | 10 Hz | 42.7 min | 35.6 min | 38.2 min | 29.9 min |
| −10 dB-Hz | 0.1 Hz | 19.5 h | 16.2 h | 9.85 h | 61.9 min |
| −10 dB-Hz | 1 Hz | 20.3 h | 16.9 h | 11.5 h | 5.23 h |
| −10 dB-Hz | 10 Hz | 2.87 d | 2.39 d | 2.56 d | 1.92 d |
| −20 dB-Hz | 0.1 Hz | 80.4 d | 67.0 d | 40.3 d | 3.55 d |
| −20 dB-Hz | 1 Hz | 83.7 d | 69.8 d | 47.1 d | 20.9 d |

At +10 dB-Hz, the earlier 13–14 s one-cell estimate for a 4 s hop could not fit the
required 32 hops. The current minimum is 128 s at either admission target. At −10 dB-Hz
and 0.1 Hz, the same hop gives 58,428 s with one-cell tones and 3,712 s with held tones.
This is a reason to select a transmit profile on the assumed path. The metric weights
individual bin powers using their finite-window means. It omits cross-products between
bins, which can carry information when the fading signal is correlated. The duration
calculation uses twice the generalized mutual information for this metric as a weak-tone
deflection proxy and adds independent tone contributions. Strong individual tones,
correlated fading, narrow grids, clipping and competition among words can change these
durations. [Current calculation with all three budgets](algorithms-matrixModem/fsk-lattice/results/attended_hf_durations.md) · [Original unrounded geometry](algorithms-matrixModem/fsk-lattice/results/lattice_receiver_section13.md)

For a **steady path with one unknown phase per hop**, use the separate matched energy
test below. Each held tone is one normalized complex observation and the receiver adds
its squared magnitude. The calculation evaluates the central chi-square tail under noise
and the noncentral tail under signal, with 4,000 nuisance hypotheses per word and the same half
budget. The search count and statistic differ from the approximation above. Both the
number of tones and the retune guards enter the block period.

| C/N₀ | 0.4 s hop | 4 s hop | 40 s hop | 400 s hop | 4,000 s hop |
| ---: | ---: | ---: | ---: | ---: | ---: |
| +10 dB-Hz | 12.8 s | 128 s | 21.3 min | 3.56 h | 35.6 h |
| 0 dB-Hz | 7.89 min | 128 s | 21.3 min | 3.56 h | 35.6 h |
| −10 dB-Hz | 10.6 h | 57.6 min | 21.3 min | 3.56 h | 35.6 h |
| −20 dB-Hz | 43.3 d | 3.10 d | 8.89 h | 3.56 h | 35.6 h |
| −30 dB-Hz | 11.8 yr | 301 d | 28.8 d | 3.70 d | 35.6 h |

These long-hop values assume the centre remains matched throughout the flat interval.
The original study treats a centre error drawn afresh each hop separately. [Current steady-path calculation](algorithms-matrixModem/fsk-lattice/results/attended_coherent_duration.md) · [Earlier steady and offset-path calculations](algorithms-matrixModem/fsk-lattice/results/lattice_receiver_section11.md)

### Phase-stable profile below 9 kHz

Use the specified sixteen positions, 10.24 s tones and at least two repeats for each of
32 code symbols. Compare the same transmit geometry under different assumptions about how
long the receiver can combine phase. Each column assumes a matched timing and phase law
within its windows, including boundaries, and the 4,000-hypothesis family allowance above
includes all tested rungs and offsets. Each complete or partial window is normalized for
its own noise variance. The signal energy uses its actual length. The ideal position-mean
subtraction in this calculation retains 15/16 of the matched tone energy.

| C/N₀ | Per-tone only | Up to 128 tones (21.85 min) | Up to 2,048 tones (5.83 h) | Whole block coherent |
| ---: | ---: | ---: | ---: | ---: |
| −10 dB-Hz | 32.8 min | 10.9 min | 10.9 min | 10.9 min |
| −20 dB-Hz | 31.9 h | 1.91 h | 1.55 h | 1.55 h |
| −30 dB-Hz | 124 d | 43.7 h | 17.1 h | 15.2 h |
| −37 dB-Hz | 8.45 yr | 29.0 d | 5.32 d | 3.17 d |

The 655.36 s transmit floor at this tone length applies even on a stronger link. To
confirm an isolated word under the two-quiet rule, allow two more public block periods
after the transmitter stops. At the floor the earliest normal closure is 1,966.08 s
(32.77 min) from the word's start. A missed word, false admission in quiet, or unscored
slot can change the outcome. For the effect of phase wander, use the measured
word-selection results in section 12.2 with their stated search, noise and timing scope.
[Whole and partial window calculation](algorithms-matrixModem/fsk-lattice/results/attended_coherent_duration.md) · [Historical 10⁻⁶ model with other tone lengths](algorithms-matrixModem/fsk-lattice/results/lattice_receiver_section10.md)

## 11. Concealment

These historical comparisons retain the keyed receiver assumptions and block durations of
the cited studies. The attended 10⁻⁴ target changes the intended receiver's admission budget.
The observer's false-alarm probability below belongs to a separate test. The public zero-key
profile gives an observer access to the same receiver. Field detectability and perceived
interference remain unmeasured.

The figures are the extra C/N₀ an observer without the key needs over the keyed receiver to
notice one 10-bit block, with detection probability 0.9 at a false-alarm rate of 10⁻². The
observer is idealised: it knows the lattice, band, tone and hop timing, covers every hop channel
at the tone's resolution and knows its noise level. Steady path; block sized for its level.

**One-cell tone comparison.** The table reports the modeled energy observer with the stated
key, timing and phase assumptions and 400 s hops. [Waveform assumptions](#44-phase-key)

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

**Held-tone comparison.** The following historical receiver and observer thresholds use
32 held tones per block and the same cell counts.

| Held tone | One phase per tone | Phase step every cell | Every 2 cells | Every 4 | Every 8 |
| --- | --- | --- | --- | --- | --- |
| 4 s | 1.6–5.7 dB | 8–10 dB | 6.5–9 dB | 5.5–8 dB | 4.5–7 dB |
| 40 s | 1.6–5.7 dB | 12–14 dB | 11–13 dB | 9.5–11.5 dB | 8–10 dB |
| 400 s | 1.6–5.7 dB | 17–19 dB | 16–17.5 dB | 14–16 dB | 13–14.5 dB |

These values use a particular maximum-energy statistic. The earlier study's 2 dB allowance
for another statistic is a modeling assumption. The behavior of other observers remains open.

**Where the keyed receiver adds coherently, the gap grows with the coherent time.** The keyed
receiver needs a fixed total energy (about 57 times the noise density for a 10-bit block), and
an observer measuring energy needs an amount that grows with the square root of the cells
observed. For 4 positions and a block that is coherent for one hour that is about 13 dB, rising
5 dB per tenfold of block time. This hand estimate used the historical duration and
energy calculations. The current sizing tables do not measure observer performance.

**Below 9 kHz the observer comparison remains open.** The HF example with 32 tones and
352 positions has a different waveform and calendar from the sixteen-position profile
with at least 64 tones. The reported 1.6 dB result belongs to the former. The latter
profile retains one phase assignment per tone, and receiver timing and derotation enter
its processing budget. [Profile choice](#1-scope-and-fixed-decisions)

## 12. Paths other than mid-latitude HF

### 12.1 Paths evaluated for the format

The gap column gives modeled extra transmit time against the bound for the same channel
at −10 dB-Hz, using a two-cell held tone and 0.4 s hops unless stated. The HF calculation
uses known timing and Doppler centre, a large alphabet, ideal coding and Gaussian noise.
It measures the cost of a receive statistic for the modeled signal. Finite-word errors,
the searched false-admission rate, path availability and processing deadlines each need
separate evidence. [Per-channel calculations](algorithms-matrixModem/fsk-lattice/results/lattice_receiver.md)

| Path | Spread | Gap | What else it needs |
| --- | --- | ---: | --- |
| HF stress case (30 Hz, 7 ms) | 30 Hz | 1.1 dB for the Gaussian Doppler component | Exercise the delayed path on actual waveform samples. The 30 Hz calculation supplies no 7 ms delay test. |
| Aurora, flutter | 50–500 Hz, with offset | 1.0–1.1 dB | spread hypotheses up to 500 Hz |
| Earth–Moon–Earth | about 20 Hz per GHz | 1.0–2.4 dB | the predicted Doppler shift (up to ±20 kHz and 1.3 Hz/s at 5.76 GHz) removed before the transform |
| Troposcatter at 0.9–5.8 GHz | roughly 0.3–20 Hz (an extrapolation from 30 MHz data) | 6.4 dB at 0.5 Hz, 2.4 dB at 3 Hz, 1.5 dB at 10 Hz | Test longer tones against the required deadline on the slow end. |
| Troposcatter at 50 MHz, up to about 300 km | about 0.01–0.1 Hz (inferred) | not computed | A held tone of seconds or more is a candidate. The path has no qualified profile. [Path study](#123-meteor-burst-and-troposcatter-at-50-mhz) |
| Phase-stable (sub-9 kHz, daytime groundwave), per-tone reception, no FHSS | below 1 mHz | 18 dB (28 at −20, 38 at −30 dB-Hz) | this is only the fallback, computed for 0.32 s tones. With the 10.24 s tones used below 9 kHz it needs 8.9 dB more block energy than a coherent block (another measure: 1% wrong decodes over 87 minutes, [12.2](#122-phase-stable-paths-below-9-khz)) |
| The same, coherent windows of 1 min / 22 min / 5.8 h | below 1 mHz | 1.4 / 0.2 / 0.0 dB (5.7 / 1.0 / 0.1 at −20; 14.1 / 4.4 / 0.7 at −30) | both references and the path steady over the window |

- **A small gap to a wide-spread bound can still imply a long block.** At −10 dB-Hz the
  model's bound rises from 5.39 dB in energy per bit over noise density at 0.1 Hz to 28.68 dB
  at 30 Hz. At 100, 200 and 500 Hz the 60 Hz weighting alone costs 1.55, 3.55 and 7.17 dB
  of time over the corresponding bound. Extending the weights to 500 Hz reduces those gaps
  to 1.09, 1.10 and 0.98 dB in the known-centre model. Actual sensitivity also depends on
  total available signal energy, overlap between positions and the cost of selecting the
  correct word over the public search. [Spread and aperture comparison](algorithms-matrixModem/fsk-lattice/results/lattice_receiver_section9.md)

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
  | Positions and span | 16 centres at 0.78125 Hz spacing. A first centre at 8,259.375 Hz puts the last at 8,271.09375 Hz. The nominal grid width is 12.5 Hz. | Nominal width 25 Hz (`j` = 2) or 6.25 Hz (`j` = 4) |
  | Tone length | 8 cells = 10.24 s (0.098 tones per second) | 1 cell (1.28 s) as the fastest; 32 cells (41 s) for blocks longer than about 3 h |
  | Shortest transmitted block | 64 tones = 655.36 s. Two repeats per symbol give room for 256 position pairs. [Mapped-code check](../_ref/algorithms-matrixModem/fsk-code-identifiability.md#what-two-16-position-observations-convey) | 81.92 s with one-cell tones or 2,621.44 s with 32-cell tones |
  | Phase key | One per tone, reached by an 80 ms ramp ([4.4](#44-phase-key)) | 160 ms ramp for an antenna narrower than about 40 Hz |
  | Receiver | Cells inside a tone, per-tone reception, and coherent windows from two tones to the block, with a reference that follows the ramp | – |
  | References | GPS-locked at both ends, corrections logged or the audio re-timed from the PPS | Search the configured relative error budget and qualify complete service |

  - **What it gives up.** A strong link is not served faster than the shortest block. A 10 s
    tone is conspicuous to an observer with a filter bank ([section 11](#11-concealment)). It
    is a second cell length beside the default 160 ms, and it departs from the preference for
    the fastest tone rate ([4.2](#42-tone-length)).
  - **Tone length and deadline.** At fixed elapsed time, 10.24 s tones give the coherent
    candidate sum 64 times fewer tone contributions than 0.16 s tones. FFTs inside tones,
    window construction, search coverage and quiet-block decisions have separate costs.
    On the simulated ±50 mHz wander case the per-tone statistic reaches the best tested
    window's word-error point. A single shortest block followed by two scored quiet periods
    takes at least 1,966.08 s (32.77 min) from the start of transmission to confirmed
    completion on the 10.24 s profile. This assumes correct decisions on all three periods.

- **At 10 kW the antenna is voltage-limited, and the power buys bandwidth.** The top voltage `V`
  fixes the current and the radiated power: 5.5 mW (thin-wire T at 29 kV) to about 0.3 W (a
  2.5 nF tower at 100 kV) at 8.27 kHz. The −3 dB bandwidth that uses all 10 kW is
  `P / (2π·C·V²)`: 64 to 176 Hz at 100 kV for 2.5 to 0.9 nF, 189 to 525 Hz at 58 kV, and 16 to
  44 Hz only at 200 kV. A lower-loss antenna cannot take the power: under the earlier antenna
  note's loss cases a 61 m antenna passes 18.5 to 620 Hz at 8.27 kHz (6 Hz at 2.97 kHz), and its
  low-loss cases would stand at 111 to 309 kV with 10 kW. The formula assumes the whole 10 kW
  is dissipated in series with the antenna; a matched source or energy recovery changes the
  bandwidth by 1.5 to 3 times. Theoretical; the voltage limit of a real 61 m installation is
  the largest unknown.
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
  each other at 86 Hz and 49° at 24 Hz. A matched reference accounts for the scheduled phase. The antenna transfer function still affects the received energy. Calculated for a single-tuned antenna; no transmitter was examined.
- **Candidate gap between mains harmonics.** Near 8.27 kHz the 50 Hz harmonics (8,250 and 8,300 Hz)
  and the 60 Hz harmonics (8,220, 8,280, 8,340 Hz) wander by about 3 Hz rms on the steadier
  grids. Applying three-standard-deviation guards using the reported 3.1 Hz and 2.5 Hz
  estimates leaves about 8,259.3 to 8,272.5 Hz. The interval is a grid-statistics estimate.
  Local line amplitudes and the time the desired positions remain usable require site evidence.
- **Railway supply can eliminate the candidate gap.** Its harmonics are about 16.7 Hz apart, near 8,250,
  8,266.7 and 8,283.3 Hz. If they wander as the mains harmonics do, no span of 12.5 Hz or even
  6.25 Hz is clear of them at three standard deviations where such a railway is in range
  (Germany, Austria, Switzerland, Sweden, Norway). The amateur convention of 8,270 Hz is 3 Hz
  from the nominal line; the earlier research records only that carriers are placed off the
  railway harmonics, and no line amplitude was found. A line on one of sixteen positions is
  left to excision and margin ([section 9](#9-margin-interference-and-lost-hops)).
- **Tone-length comparison at matched total energy and known timing/frequency.** The table
  gives approximate block `E/N₀` values at 1% wrong 10-bit words, with a signal present for
  87.4 minutes and the best tested window chosen for each simulated channel. The compared
  formats used at least eight positions and cells up to 2.56 s, with tones from 0.16 to
  40.96 s. These word-selection measurements have no quiet-block admission cut.

  | Path | Block `E/N₀` | Best coherent window |
  | --- | ---: | --- |
  | Steady | 12.9 dB | whole block |
  | Phase wander 30° rms, 1 h correlation | 13.4 dB | whole block |
  | Frequency wander within ±1 mHz (Sub9 best) | 16.2 dB | 5.5 min |
  | Within ±5 mHz (Sub9 moderate) | 18.4 dB | 82 s |
  | Within ±50 mHz (Sub9 worst) | 22.5 dB | 10 s |

  Per-tone reception alone needs 27.4, 24.5, 21.8 and 19.2 dB on the steady path with tones of
  0.64, 2.56, 10.24 and 40.96 s. In the ±50 mHz wander fixture a 10.24 s tone reaches the
  best-window value using per-tone energies alone. On the steady-path fixture the 1.28 s
  option needs 26.0 dB with per-tone energies, 4.2 dB above the 10.24 s result. Selecting
  a window from the data cost up to 0.8 dB against the best window for the tested channels.
- **Two further reasons for the span and the spacing.** Differential delays of 0.2 to 1.9 ms give a small
  relative phase change across 12.5 Hz. Comparable rays can still cancel across the entire
  narrow band. Their powers alone cannot establish available signal energy. The earlier
  eight-times-spread heuristic puts 0.390625 Hz spacing near its suggested 0.4 Hz edge for
  50 mHz. A bound on instantaneous frequency wander and the statistical width of a diffuse
  Doppler spectrum describe different channels. Use the corresponding received-word
  comparison when choosing a spacing.
- **Spacing equal to the reciprocal of a long tone is not offered.** Positions 0.098 Hz apart
  for 10.24 s tones decode as well up to the worst spread bound but have no shorter window: at
  0.5 Hz diffuse spread, none of the tested energies from 6 to 44 dB met an observed 1%
  error target in the H trial. At 44 dB, correcting numerical aliasing at the grid edges
  reduced the error count from 10 to 5 in the same 200 trials. Performance above that energy
  remains open. The wider-spaced format previously reported 27.4 dB under its simulated
  conditions. About 6 ppm at 8.27 kHz is half a 0.098 Hz position, an offset a receiver must
  compensate or include in its search. [Projection and decoder checks](algorithms-matrixModem/sub9-fsk-rate/quadrature-check.md)
- **Coherent search volume below 9 kHz.** The following counts assume a relative oscillator
  error budget between the emitted and sampled signals. At window duration `T` the frequency
  centres are half a Fourier bin apart and candidate drift rates differ by `3.88/T²` Hz/s.
  With rectangular integration the isolated worst half-cell losses are each about 0.91 dB.
  The joint corner gives 1.75 dB. The table uses 9 kHz.
  [Physical-grid calculation](../_ref/algorithms-matrixModem/fsk-search-validity.md) ·
  [Original counts](algorithms-matrixModem/fsk-lattice/results/lattice_receiver_section18.md)

  | Assumed relative reference error | 22 min | 1 h | 5.8 h |
  | --- | ---: | ---: | ---: |
  | GPS-disciplined, logged corrections leaving ±0.001 ppm and negligible drift | 1 × 1 | 1 × 1 | 1 × 1 |
  | GPS-disciplined, unobserved corrections bounded by ±1 ppm and 1 ppm/h | 48 × 3 | 130 × 17 | 752 × 562 |
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
  - If a separately clocked sampler has relative rate uncertainty ±`r` (seconds per
    second), the end-to-end slip spans `2rT`. Covering that span by timing cells of width
    `Δt` gives `max(1, ceil(2rT/Δt))` rate hypotheses in this constant-rate model. At
    one hour and a 10 ms step, ±100 ppm gives 72. An independent ±5/9000 sampling-rate
    bound would give 400. A ±5 Hz **carrier** offset alone specifies no such sampling-rate
    error. The historical factor of 200 used a 20 ms timing step.
- **One decode follows the tone rate.** A decode on the full ladder costs one lookup per
  message and tone (measured: 3.3 thousand million message-tones a second on the 12-thread
  Ryzen 5 PRO 5650U, 120 on the RTX PRO 6000 Blackwell). The following ratios estimate
  single-hypothesis 24-bit ladder work within one block duration if the measured
  message-tone rate holds as the block grows.

  | Device | 0.16 s tone | 0.64 s | 2.56 s | 10.24 s | 41 s |
  | --- | ---: | ---: | ---: | ---: | ---: |
  | Ryzen 5 PRO 5650U, 12 threads | 32 | 129 | 513 | 2,067 | 8,044 |
  | RTX PRO 6000 Blackwell | 1,198 | 4,741 | 18,378 | 74,483 | 288,546 |

  On the laptop a 16-bit block fits 8,180 to 2.1 million and a 10-bit block 459,000 to
  130 million. The GPU timing covers 24 bits only. These stage ratios assume a single
  supplied hypothesis. Even disciplined endpoints can leave residual path delay, clock
  corrections or frequency uncertainty.
- **Search over a less stable reference.** For an 87.38-minute block at 8.27 kHz the
  example relative bounds of ±1 ppm, ±0.1 ppm/h and ±0.1 s need 174 frequency × 4 drift
  × 20 fine timing cells. If *each* independently referenced endpoint contributes those
  frequency and drift limits, the worst relative range requires 347 × 7 before timing
  and propagation uncertainty. A drift `g` in end-to-end Fourier bins contributes phase
  `πg((t−T/2)/T)²`. The historical CPU/GPU benchmarks and simulator generated and searched
  half this curvature. Their four synthetic rows covered centres from −2.91 to +2.91 bins.
  The corrected centres reach ±5.82. [Geometry and tests](../_ref/algorithms-matrixModem/fsk-search-validity.md)

  | Historical 24-bit search route | Projected time | Block duration ÷ projected stage time |
  | --- | ---: | ---: |
  | Laptop, repeated decode | 35,300 s | 0.15 |
  | Laptop, FFTW across tone sums | 1,643 s | 3.2 |
  | GPU, repeated decode | 980 s | 5.4 |
  | GPU, cuFFT across tone sums | 83 s | 63 |
  | GPU, FP16 matrix product across tone sums | 11.4 s | 461 |

  The laptop actually evaluated 393,216 candidate messages with 20 synthetic timing
  rotations. The GPU projection scales one 131,072-message batch at one offset by 128 × 20.
  Its matrix product ran at 404 nominal TFLOP/s, with 60% Tensor Core activity over the
  profiled pipeline. The [historical clock-class extrapolations](algorithms-matrixModem/sub9-fsk-rate/results/search_margins.md)
  put the 24-bit crystal case below real time even for these stages. Other clock classes,
  smaller message sizes and complete receiver service require their own evidence.

  A length-`U` rectangular pre-sum leaves power `sinc²(δf U)` at residual `δf`.
  With 512 tones, the edges of centred spans of 512, 696 and 1,024 half-bin cells cost
  0.91, 1.72 and 3.92 dB before the final frequency-grid mismatch. At one reciprocal of
  the tone length the first projection has a null. Form additional pre-mixed bands or
  preserve shorter observations when the configured trajectories exceed the allowed loss.
  Drift can also change frequency *within* a shorter ladder window. At two windows per
  block this spans 1.58 ordinary local bins for the relative TCXO limit, 15.79 for the
  crystal and 78.93 for the sound-card limit. Summing powers on a tilted bin path only
  corrects the motion *between* windows.

  In a new physical-grid check the full 188-SM RTX PRO 6000 Blackwell returned the
  planted message, frequency and drift for four arithmetic routes in one 4,096-message
  batch. For the 10-bit AWGN code fixture, with one timing and on-grid frequency/drift,
  the known/searched whole-block decisions gave 91/1,257 wrong among 8,192 at 13 dB
  and 0/49 among 8,192 at 15 dB. The simulation includes rectangular tone projection
  and leakage. It conditions on a present block and one fixed code. The older 1.6 dB
  inferred search penalty belongs to the original, narrower bank. The new two points
  do not determine a 1% crossing. [Device, uncertainty and reproduction](algorithms-matrixModem/search-validity/device-validation.md)

  - The antenna's group delay (3.7 ms at 86 Hz, 13 ms at 24 Hz) and the path delay (3.3 ms per
    1,000 km) are as large as one 10 ms timing step. With locked references they have to be
    known, or a few timing offsets searched.
  - The benchmarks have synthetic bin values and stock kernels, leave out the forming of bin
    values and the presence test, and time the large CPU cases on a sample of the messages.
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

- **Underdense trails in the modeled link.** The strongest modeled example gives a peak of
  −6 dB-Hz between isotropic antennas over 1,300 km (−23 dB-Hz at 2 mW). At 100 mW the
  modeled isotropic examples fall below the single-tone energy criterion. The study
  projects 23 to 73 one-tone bursts per hour with about 17 dBi at each end.
  Operational systems use 300 W to 2 kW; no published result at 10 W or less was found.
- **Overdense trails are candidates for a separate intermittent-link profile.** They are under 5%
  of trails and carry about 30% of throughput on classic links, more as power falls. They
  last seconds and fade 1 to 10 times a second. A single trail's microsecond-scale
  differential delay is small relative to a 160 ms cell. Variable Doppler, multiple simultaneous
  trails and long gaps between echoes still affect recovery of a complete word.
  - Relying on them instead of underdense trails means less transmit power, hence less to
    notice near the transmitter, and practical antennas.
  - On a single long path the level is too low without gain: over 1,300 km a strong overdense
    trail carries about one tone's worth for 100 mW, and 10 dBi at each end is needed for a
    block's worth
    ([calculation](algorithms-matrixModem/meteor-burst/results/meteor_burst.md), section 5).
- **Modeled opportunities at a few hundred kilometres.** The table counts trails per hour
  at 50 to 500 km that exceed 12, 20 or 26 dB of echo energy over noise density, assuming the
  stated receive gain in every direction. Successful reception also requires usable tones
  from enough distinct code symbols within the public block. Message completion requires
  two consecutive scored absences after transmission. [Link model and energy criteria](algorithms-matrixModem/meteor-burst/results/meteor_sky.md)

  | Power, receive gain | A tone's worth | A block's worth | Block with 6 dB for fading |
  | --- | ---: | ---: | ---: |
  | 100 mW, none | 8–9 | about 2 | 0.1–0.5 |
  | 100 mW, 10 dBi | 37–43 | 11–13 | 4 |
  | 100 mW, 20 dBi | about 145 | 50–57 | 20–24 |
  | 2 mW, 20 dBi | 12–15 | 3–4 | about 1 |

  The figures hardly change from 50 to 500 km and fall quickly beyond: 1,300 km gives 100 mW
  without gain 0.6 tone-worth trails an hour. 2 mW without receive gain gets no block.
- **Troposcatter at 50 MHz.** At 100 mW without antenna gain, the models in the path study
  predict median C/N₀ of +13 to +27 dB-Hz at 150 km, −2 to +11 dB-Hz at 300 km, and
  −16 to −6 dB-Hz at 500 km. These are median link levels. Ranking this path against
  intermittent meteor arrivals requires the probability of delivering the same word by
  the same deadline. The inferred 0.01–0.1 Hz fading makes held tones worth comparing.
  ITU-R P.617-6 discusses trans-horizon mechanisms above 30 MHz. The cited study found
  no 50 MHz path-loss measurements for these distances with which to validate the numerical
  crossover. [Path study](../_ref/algorithms-matrixModem/Fable-5.2/meteor-burst.md) · [ITU-R P.617-6](https://www.itu.int/rec/R-REC-P.617-6-202509-I/en)
- **What limits these figures.** The published meteor flux law is continued two decades past
  where it is established. Fading inside an echo is not modelled, hence the column with 6 dB
  allowed. Receive gain is assumed down to 3° elevation. On long paths the length of real
  echoes is poorly established and changes a strong trail's energy by 10 to 20 dB; below
  500 km echoes are short enough for this to matter little. A check against the one measured
  long link came out 2.4 times high, which whole-sky coverage would explain but does not
  validate.
- **Open requirements for a meteor profile.** A per-trail frequency search of about
  ±30 Hz, since the shift exceeds the 6.25 Hz spacing; a cell shorter than 160 ms, which sits
  at the edge of the fading rate; a higher evidence clip; and a block collected over one or
  several trails. An interval without a usable echo can be indistinguishable from a quiet
  transmitter. Treating all such intervals as known erasures would require side information
  or a different preconfigured calendar. The current receiver closes after two consecutive
  scored absences, including during an unobserved continuation through a propagation outage.
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

1. **Measure and rebalance.** Use the fastest qualified routes for the full configured search
   within the device's admitted memory and the receiver's deadline. Qualify their public
   decisions, sensitivity and actual arithmetic before comparing service. For a live capacity
   claim, feed one stream through preparation, transfers, search, calibration as incurred, ordered
   decisions and two-quiet closure. Include unsuccessful search, report sustained backlog change
   over a stated interval and account for peak live workspace. Independent simulation trials
   measure experiment turnaround. [Automatic-mode contract](../docs/gpu-precision-controls.md#automatic-mode-prioritizes-computational-ceiling)
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
   - **Use the same scheduling policy on both reference profiles.** Record the limiting stage
     on each device with the entire configured work running. The HF workload can shift between
     transforms, evidence formation and message search.
   - **Reference for the test of section 8.** Compare with the fastest fixed assignment
     actually tested on the same recorded workload and device. Declare the qualified routes,
     feasible portion sizes and memory limits first. Include an independent challenging
     workload before extrapolating from a kernel or a single block.
   - **Workers.** Resolve the default once as `W = 2H`, where `H` is the effective logical
     CPU count after affinity and quota. Honour explicit allocations, share the budget across
     stages, and charge scratch, retained results and worker stacks over their lifetimes.
     [CPU allocation](../docs/development.md#receiver-cpu-allocation)
2. **Reuse common work within the admitted workspace.** Compute shared inputs once when storage,
   traffic and synchronization permit. Bounded tiling or measured recomputation can raise
   complete service when materializing all intermediates would exhaust memory. When possible,
   compute at the outermost loop level at which the input changes and reuse the result inside.
   - **Loop order of the search.** The search is over messages × hypotheses. Work that the
     hypotheses share is done once, and the hypotheses become an inner dimension of it. For
     frequency and drift the shared work is the gather and the ladder, and the inner dimension
     is a transform. Where every hypothesis has its own evidence (timing and spread in per-tone
     reception, fine timing steps) only the enumeration of the messages is shared, and that is
     cheap; the loop order is then a route like any other.
   - **Precedence.** Prefer reuse where it fits and improves whole-receiver service. Where nothing substantial is shared,
     and for a handful of hypotheses, rule 1 decides between the forms by measured cost. The
     measurements below show why: the form that won on two CPUs lost on the GPU.
   - **Why it is a rule and not a detail.** A hypothesis loop around the decode repeats the
     gather and ladder work. At the historical 13,920-hypothesis shape, the measured
     component costs projected a 21-fold saving on the laptop and 86-fold on the GPU.
     Section 12.2 gives the evidence and its bounds.
   - **What is done once per what:**

     | Work | Done once per | Reused by |
     | --- | --- | --- |
     | Transform plans and configuration-dependent matrices | Transform shape and configuration | Blocks and candidate tiles with the same shape and parameters |
     | Keyed schedule and message-to-symbol lookups | Key, public ordinal and all configured schedule inputs | Candidate tiles for that block. Split or regenerate tables where a full table would exceed workspace |
     | Conditioning and first transform | Physical sample support, conditioning state and cell alignment | Hypotheses sharing those samples and operations. A shifted aperture needs boundary work or a qualified approximation |
     | Bin values | Aligned aperture and first-stage band admitted by the residual frequency, drift and aperture-loss budget | Messages and frequency/drift hypotheses in the band. Reuse for other timing offsets only with the boundary treatment below |
     | Evidence table entries of per-tone reception | Spread and timing hypothesis | Every message |
     | Gather of a message's tone values and positions | Message, alignment and band | Timing offsets sharing the source projections after any necessary boundary corrections, plus frequency, drift and rung hypotheses within their aperture limits |
     | Turn by a timing offset's phase slope; sum of short tones into sub-blocks | Message and timing offset | Every frequency bin, drift and rung |
     | De-chirp and transform across tone sums | Message, timing offset and drift | Every frequency bin of the whole block |
     | Transform of a rung's windows | Message, band, timing offset, window and any necessary intra-window drift correction | Bins resolved within that window |
     | Optional decoy-key controls | Received block and distinct decoy schedule | Calibration diagnostics. Charge each search and retain the shared-observation dependence |

   - **Illustrative counts for the aligned, drift-free lower-rung route.** With `M` messages,
     `A` alignments, `F` bands, `T` timing offsets per alignment, `D` drift hypotheses and `W`
     windows in all rungs below the whole block: first transforms = cells × `A`; gathers =
     `M`·`A`·`F`; turns = `M`·`A`·`F`·`T`; transforms across tone sums = `M`·`A`·`F`·`T`·(`D` +
     `W`); passes over the messages in per-tone reception = 1 with vector entries, or one per
     hypothesis, whichever route is chosen. Add band-specific window formation, boundary
     corrections, dechirps within affected windows and searches across their tilted paths
     where needed. Count executed operations and memory for the actual route.
   - **Timing offsets within an alignment.** A phase slope rotates stationary bin values.
     A translated aperture exchanges samples at the boundaries. A one-sixteenth-tone
     offset on a legal unshaped same-position phase reversal loses 1.16 dB after the slope
     is removed. Include the required boundary work or measure the approximation.
   - **How each hypothesis dimension is carried.**

     | Dimension | Form | Basis |
     | --- | --- | --- |
     | Frequency and drift in coherent attempts | A transform across the message's tone sums, by FFT or as one matrix product; each rung only on the bins its window length resolves | Historical projected kernel ratios of 21 on the laptop and 86 on the GPU versus repeated decode ([12.2](#122-phase-stable-paths-below-9-khz)) |
     | Timing offsets in coherent attempts | One operand per offset: the positions a message selects are looked up once, and each offset turns the gathered values by the phase slope of their positions before the transform | Historical laptop projection from 20 synthetic rotations per sampled message, 1,643 s. Boundary sample corrections excluded. One GPU offset was timed |
     | Short tones | Sum into sub-blocks when the retained bandwidth and drift tolerance suffice | Historical laptop stage projections of 2,148 s and 7,956 s for 2.56 s tones |
     | Spread hypotheses and timing offsets of per-tone reception; fine timing steps of the wide-band coherent attempt | Two routes: one pass over the messages with the entries of all hypotheses stored together, or a pass per hypothesis with its table in fast memory | Measured for 7, 56 and 350 hypotheses, one pass against a pass per hypothesis: laptop 1.6, 6.0 and 6.7 times faster; 27 threads of an EPYC 9554 0.7, 4.2 and 11.8; RTX PRO 6000 slower, by 1.7, 8 and 10 |
     | A frequency range exceeding the allowed pre-sum loss | Use additional pre-mixed bands or shorter retained sums | 0.91 dB at the edge of a 512-half-bin band with 512 tones and constant offset. Budget drift and final-grid loss separately |
     | Drift on shorter rungs | Correct local curvature before summing and match window-centre frequencies across the block | Tilt and intra-window correction depend on relative oscillator limits. Wider classes remain unmeasured |
     | Independent sample-clock rate | One hypothesis per resolvable change in accumulated timing slip | Size from the relative rate range, elapsed time and timing grid of [12.2](#122-phase-stable-paths-below-9-khz). A carrier-frequency envelope alone does not specify sampling-rate error |
     | Frequency error of each hop with FHSS | The same transform across the tones of one hop | Not measured |
     | Centre of a held tone with FHSS | Inside the evidence, before the search over messages (section 8, item 5) | Not a search dimension |
     | Delay rate on a specular HF path | No form stated | Not sized |
     | A single hypothesis, or a few | A plain decode each | Measured; the transform takes over at about 30 hypotheses on the laptop and 10 on the GPU |

   - **A transform across positions as well is not specified.** It would give every timing
     offset inside a cell at once. Estimated from the measured transform times it gains about
     two on a CPU at 200 offsets and nothing at 20, and on the matrix-product route it loses,
     because the operand would have to be spread over the positions: 16 times the product
     below 9 kHz, 352 times over 2.2 kHz. Not built.
   - **What remains per message and hypothesis.** Where the evidence differs per hypothesis the
     32 additions per message and hypothesis remain. On the laptop they take 0.8 ns (real) and
     1.7 ns (complex) per message and hypothesis in one pass of 350 hypotheses, and 3.1 and
     5.6 ns in a pass of 7; on the GPU 0.011 ns as a pass per hypothesis with real entries. No
     route found so far avoids them.
   - **Benchmarks.** A ceiling figure states its work counts against the counts above. A figure
     taken with a repeated inner stage is labelled as the cost of that structure, not as the
     ceiling.
3. **Measure simulation throughput separately.** Among designs that support the intended
   sensitivity and live service, favour those that also permit rapid matched experiments.
   Charge each simulated trial all work required for the question it answers.
   [Simulation requirements](#135-simulation)

### 13.2 Measured

- **Stage measurements.** The first transform had substantial headroom on the tested
  lattice batches. The work for a live stream also depends on alignments, bands, apertures,
  candidate count and memory traffic. The measurements below describe their stated
  fixtures. [Benchmarks and timing boundaries](../_ref/algorithms-matrixModem/Fable-5.2/computation-and-simulation-speed.md)
- **Measured on the 12-thread Ryzen 5 PRO 5650U**, one pass over all 2²⁴ messages with vector
  entries against the best pass per hypothesis (the per-tone kernel, 0.086 s per hypothesis,
  and its complex form, 0.126 s):

  | Hypotheses in the pass | Real entries | Faster by | Complex entries | Faster by |
  | ---: | ---: | ---: | ---: | ---: |
  | 7 (the spread bank) | 0.37 s | 1.6 | 0.65 s | 1.4 |
  | 56 | 0.80 s | 6.0 | 1.8 s | 3.9 |
  | 350 (the wide-band fine timing steps) | 4.5 s | 6.7 | 9.7 s | 4.5 |

  The planted message won for every hypothesis. Synthetic tables; the gain comes from adding
  eight to sixteen hypotheses in one instruction and from forming the symbol values once. On
  27 threads of an EPYC 9554 the same comparison gave 0.7, 4.2 and 11.8 with real entries and
  0.6, 3.0 and 8.9 with complex ones (0.012 s per hypothesis as the baseline): slower for the
  spread bank alone, faster for many hypotheses. The tables for 350 hypotheses (5.7 and
  11.4 MB) fit the last-level cache of both CPUs; on a smaller cache the gain would shrink.
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
    why the 64-window figure above is slow. The historical GPU batch measured 404 nominal
    TFLOP/s on the FP16 product, which represented 69% of the measured stage sum. The
    corrected search used FP16 operands with FP32 compute and FP16 or FP32 output,
    INT8 operands with INT32 compute and output, or a complex FP32 FFT. Each recovered
    the planted parameters in one candidate batch. This establishes neither four-bit
    transform arithmetic nor branch calibration. [GPU source and device check](algorithms-matrixModem/search-validity/device-validation.md)
  - **The historical 10-bit Monte Carlo** ran at 34,900 trials/s when the matrix included
    all ladder rungs and 81,000 trials/s for the whole block only. Both decided on the
    whole-block output and used the earlier half-range drift bank. The physical-grid
    two-point experiment in section 12.2 uses the corrected rectangular projection.
  - A pass per hypothesis over all 2²⁴ messages with its table in shared memory took 0.19 ms.
    One pass with the entries of all hypotheses stored together was slower there: 1.7, 8 and
    10 times for 7, 56 and 350 hypotheses.
  - A batched sample generator and front end ran at 7–12 million times real time,
    about 10 ms per day of represented signal. The timer covered noise, a tone with a
    Rayleigh gain constant within each dwell, the transform and four-bit evidence.
    Codeword search, admission and message completion were outside the timer.
  - With a search running on the same GPU, every GPU transform route fell to the same 7–14
    million per second. The GPU's time was divided almost without loss (shares summing to
    0.95–1.03), so a rate measured while sharing does not show which route is cheaper.
  - A prototype of the scheduling rule below cleared five queued workloads in 0.94 to 1.12 times
    the time of the best fixed assignment for each, from a cold start. Every fixed assignment
    tried was 2 to 73 times slower than the best in at least one of them.
### 13.3 What the settings do to the work

- **What the settings do to the work** (reasoned from the receiver's structure and the
  measurements above; not measured for these configurations):
  - Cost depends on the number of candidate messages, hypotheses, tones and retained
    intermediates. The search over candidates can dominate. Share computations across
    frequency and drift where their inputs are common.
  - At fixed total active cells and alignments the raw flat-cell transform count is independent of hop
    length. Per-hop phase, frequency handling, guards and minimum block span can change.
  - Historical weak-link examples used about 420,000 one-cell tones versus 32 to a few
    hundred held tones. Count both the flat-cell transforms and the distinct tone sums for
    the chosen block. A smaller number of longer tones can still contain many cells. On
    the one-cell HF reference the coarse timing spacing is 20 ms, where a held tone uses an
    eighth of its length for the stated coarse grid. Fine timing, phase changes within a tone
    and boundary correction can require additional work in either profile.
  - Phase steps inside a held tone multiply its timing hypotheses by the phase intervals per
    tone, per timing dimension, where timing is uncertain. A known phase per tone
    introduces no extra search dimension. Charge its derotation or fused lookup.
  - Below 9 kHz the work is the frequency, drift-rate and timing search of
    [12.2](#122-phase-stable-paths-below-9-khz), not the first transform. That search is itself
    a transform across tone sums, so the same routes serve both stages and the balance between
    them depends on the particular workload, device and admitted buffer sizes.
  - The historical table ladder used two real columns per shortest complex window,
    or 128 columns for 64 windows. Its direct 24-bit kernel ran at 36.9 decodes/s,
    compared with 5,900/s for the per-tone table. Column count alone gives no runtime ratio.
    A ladder from two tones up follows the tone count instead.
    Over 2.2 kHz the specular search adds about 350 fine timing steps for each coarse one,
    measured as kernels only ([13.2](#132-measured)).
### 13.4 Routes and scheduling rule

- **Candidate routes.** Admit the implementations available and qualified on the device.
  For the first transform these include CPU FFT, GPU FFT, and a
  matrix-product DFT on Tensor Cores, which computes only the bins wanted. For the search over
  messages: lookups in evidence tables, on CPU or GPU. For hypotheses with their own evidence:
  one pass with vector entries, or a pass per hypothesis with its table in fast memory. For the search over frequency and drift:
  a plain decode per hypothesis when there are few, and otherwise a transform across tone sums
  by CPU FFT, GPU FFT or one Tensor Core matrix product, followed by its epilogue. Portions of
  the last kind are batches of messages for one timing offset. In simulation also: signal and
  noise generation on CPU or GPU, with trials as the portions.
- **Scheduling rule.** No fixed routing table is specified. The receiver, and the simulator in
  the same way, chooses its routes while it runs:
  1. Divide admitted work into portions such as tone batches and candidate tiles. Keep probe
     outputs when their route is qualified for the active decision rule and score scale. If
     an alternate representation changes branch ordering or thresholds, evaluate it on
     paired data and calibrate the resulting decision before it serves live traffic.
  2. The portion size is tuned with the route; the fastest route changes with it.
  3. Measure elapsed cost per useful unit for each route and portion size under the contention
     the receiver actually experiences. The prototype used one portion per executor at a
     time. Overlap or additional streams require a whole-job comparison at the same coverage.
  4. The prototype tried another route every twentieth portion. This is a count of portions.
     If one probe takes twenty times as long as each of nineteen ordinary portions, it
     consumes 20/39 of their combined time. Bound live probes by the time and workspace
     available before the oldest deadline. When the stream has insufficient headroom,
     use previously qualified costs and defer exploration. Partition long decodes by
     candidate ranges while preserving all candidates.
  5. Each stage's work on a device goes by its cheapest route and size. A free executor takes the
     oldest portion of the stage in which it is least disadvantaged against the other devices, and
     leaves it alone when the others would clear all queued work sooner than it would finish that
     portion. No stage runs ahead of what the next can consume. The CPU worker budget is never
     reduced.
  6. Predicted and actual backlog cleared are compared every cycle; a persistent gap triggers an
     end-to-end comparison of the two best assignments. The measure of success is the backlog the
     complete receiver clears, or the trials a simulation finishes, never one stage's rate or
     one unit's utilization.
  7. When complete full-fidelity work cannot keep up, report overload. Use a reduced route only
     within a measured channel and timing envelope, with a recalibrated decision and adequate
     remaining deadline and sensitivity margin. Keeping half the tones costs about 1.51 dB in the
     weak independent-energy model and 3.01 dB with coherent combining at constant tone power.
     If the retained tones fall in a fade the loss can be total. Unchanged per-message work
     limits the speedup. Keep reduced work explicitly identified and preserve the configured
     public hypotheses. The draft's 2 dB allowance is a design proposal. Missing input or
     an unscored block breaks the consecutive-quiet count.
     [Sensitivity and service bounds](../_ref/algorithms-matrixModem/fsk-reconciliation.md#what-reducing-the-work-can-buy)

### 13.5 Simulation

Fast experiments are a design objective alongside live receiver capacity. Batch independent
trials where practical while preserving the physical model and decision under test.

- **Simulator design.** The intended compiled receiver accepts sample input and, for bounded
  experiments, modeled projections at the corresponding stage. The cell mode draws bin statistics
  directly using the signal means and joint noise law of the sample projections. Independent
  Gaussian bins are exact for orthogonal projections of white Gaussian noise. Fractional bins,
  window overlap, colored noise and shared hypotheses can produce correlated bins even in a
  linear front end. The sample mode
  forms samples and is required for anything that involves conditioning, the 4-bit sample code
  or timing error. The cell mode is used wherever it is valid.
- **Experiment machines.** For simulation throughput the reference hosts are 256 logical
  CPUs, and about 32 logical CPUs with a full RTX PRO 6000 Blackwell. Record the allocation
  and exact GPU edition. Receiver operating targets also include the RTX 4090 desktop
  and Laptop, RTX 4060 Ti **16 GB**, and Ryzen 5 PRO 5650U / Ryzen 7 8840U CPU and integrated
  graphics. Measure each executed route on its actual target and record unavailable routes.
  The [representative receiver hardware guide](../docs/triad-performance.md#representative-receiver-hardware)
  includes the other GUI selection targets.
- **Layout of the work.**
  - An independent draw is a block for a block decision, or a continuous channel history for
    a message-completion or tracking test. Preserve receiver state within each history.
    Paired variants may share samples, and replays of the same draw supply no new trials.
  - Independent CPU trials may use one numerical thread per worker. A full allocation of
    256 effective logical CPUs has a default receiver budget of 512 software workers. Trial
    dispatch can be smaller to respect the declared experiment allocation, RAM and fixed
    draw count. Measure live capacity with concurrent work on a single stream.
  - On the GPU machine the dense stages batch across trials as they batch across messages, the
    CPU workers prepare the next batches while the GPU runs, and generation moves to the GPU
    where it is measured to be cheaper there.
  - Rule 1 applies as in the receiver: the routes are timed on that machine and the work is
    rebalanced between CPU and GPU so that the experiment finishes soonest.
- **Rule 2 in simulation.** Cache plans and tables at the scope of their inputs as in
  [13.1](#131-rules-that-bind-every-implementation). A change of public ordinal can require
  a new keyed schedule. Reuse a frozen admission threshold only for its calibrated search,
  preprocessing, arithmetic, branch selection and null population. The same noise realization
  may serve every signal level of a paired sweep. A simulation states
  its projected duration from its first measured batches and preserves the declared trial
  work. A known-timing, known-frequency word-selection trial scores all words on that
  hypothesis. An admission or acquisition trial runs the entire declared search. Report
  which decisions and stages each rate includes.
- **Performance runs report** trials per second, represented signal seconds per wall
  second, the timed stages and assignment, actual processor, memory and arithmetic.
  Identify available utilization counters and their denominator. A correctness run can
  report an uninstrumented stage without implying a throughput or utilization measurement.
- **Budget.** Sample budget, uncertainty target and stopping rule are declared before the run.
- **Measured on the 32-CPU RTX PRO 6000 class** (a pod with one RTX PRO 6000 Blackwell and a
  CPU share of 27 threads of an EPYC 9554), a cell-mode Monte Carlo of the keyed 10-bit block
  of one sub-9 kHz fixture (512 tones, 87.38 minutes per trial, 128 trials per batch). The
  rows used the historical half-range drift bank and the earlier bin generator. Times below
  describe that fixture. The 64-tone minimum was not timed.

  | Simulation | Trials a second | Times real time |
  | --- | ---: | ---: |
  | GPU: generation, gather, product over every rung, epilogue; frequency and drift searched | 34,900 | 1.8 × 10⁸ |
  | GPU: the same with the whole-block window only | 81,000 | 4.3 × 10⁸ |
  | CPU share, 27 threads: the earlier block Monte Carlo on every window, four channels, frequency known | 613 | 3.2 × 10⁶ |
  | Laptop, 12 threads: the same program | 67 | 3.5 × 10⁵ |

  - The GPU run kept 98% GPU activity and 72% Tensor Core activity; the product is 82% of a
    batch, the gather 9%, the epilogue and read-back 8% and the generation 1%.
  - At 13 dB with the exact frequency/drift row supplied, 1.0% were wrong in the
    historical simulation. The corrected two-point study and its assumptions are in section 12.2.
  - Six signal levels of 204,800 trials each took 35 s. That is a quick experiment.
  - Limits: 10 bits; offsets on the grid; a steady path; a decision on the whole block; no
    presence test; one timing offset; the two programs do not simulate the same thing, so
    their rates are not a comparison of CPU against GPU.
- **Historical kernel-based trial projections**, for one 87-minute block of 512 tones:

  | Trial | Laptop, 12 threads | Pod CPU share | RTX PRO 6000 Blackwell |
  | --- | ---: | ---: | ---: |
  | 24 bits, one hypothesis | 2.5 s | 0.48 s (13 threads of an EPYC 9355) | 0.07 s |
  | 24 bits, historical 174 × 4 bank, FFTW | 1,643 s | not installed there | – |
  | 24 bits, historical 174 × 4 bank, plain transform or matrix product | 6,917 s | 880 s (27 threads of an EPYC 9554) | 11.4 s |
  | 10 bits, one hypothesis | 0.17 ms | not timed | not timed |
  | 10 bits, historical 174 × 4 bank at one timing offset, generation included | not built | not built | 0.029 ms |

  At the historical stage projection, 2,000 24-bit trials would take 6.3 GPU-hours.
  Initial sensitivity studies can use 10-bit blocks, or 24-bit
  blocks on the known hypothesis.
- **Current native coverage.** The sample-level CPU fixture exercises 10-bit quiet,
  intended-signal and wrong-schedule decisions on a configured HF calendar. It also tests
  ordered two-quiet closure. The corrected GPU fixture measures whole-block word selection
  on a steady, known-timing path. A complete-format simulator, one-stream capacity across the
  operating targets and scaling to 256 CPUs remain unqualified. The widest CPU experiment
  reported here used 27 threads. Prefer RunPod for short experiments within the authorized
  allocation. [Sample receiver](algorithms-matrixModem/block-reception/README.md) ·
  [Corrected GPU check](algorithms-matrixModem/search-validity/device-validation.md)

## 14. Decisions

**Design choices for the experimental profiles**

| Decision | Where |
| --- | --- |
| Size the HF profile for the expected off-axis power, path and deadline, counting whole blocks and hop periods. One-cell tones are the reference and held tones an alternative | 4.1, 4.2, [8.1](#81-reception-after-antenna-pointing-loss), 10 |
| FHSS is optional. The receiver allows a phase discontinuity on retuning and preserves the codeword and public calendar across hops. Helper is optional and independent | 1, 4.3 |
| Evaluate per-tone evidence and the distinct coherent windows available on each scorable block. In the reference rule per-tone acceptance takes priority on a word disagreement | 1, 8 |
| With FHSS, a block spans at least 32 hops. Erasure budgets require the assumed hop correlation and symbol coverage | 6, 9 |
| 40 s is the recommended alternate hop length for one-cell tones | 4.3 |
| Size operator-read ten-bit words at a provisional 10⁻⁴ raw false admissions per quiet public block. Count erroneous active words and physical completion separately | [8](#8-receiver-requirements), [10](#10-sizing-and-expected-duration) |
| Each code symbol has 256 possible values. A per-tone placement uses physical positions from the configured grid. When fewer than 256 are available, verify the distinctions preserved by the complete codeword | 5, 6 |
| Low-precision operands are the design target after conditioning. Size accumulators for each admitted block | 8 |
| The proposed spread bank extends to 500 Hz where the usable capture band supports it. The EME study assumes predicted Doppler | 3, 8, 12.1 |
| "Dwell" is replaced by tone, hop and held tone | 2 |
| Each tone gets a scheduled quarter-turn phase, which can equal the preceding phase | 1, 4.4 |
| Tones shorter than one cell and three phases per cell are not offered | 4.2, 4.4 |
| The format uses constant amplitude on flat cells and tone labels carry the payload | 1, 4.2 |
| Meteor burst is outside this draft; if pursued it is an FSK path class on overdense trails, not a PSK mode | 12.3 |
| Below 9 kHz: FHSS off, lattice scale `j` = 3 (1.28 s cell, 0.78125 Hz), sixteen positions in 12.5 Hz, tones of 8 cells (10.24 s), with `j` = 2 and 4 and tones of 1 and 32 cells as alternatives | 3, 4.1, 4.2, 12.2 |
| Below 9 kHz the keyed phase is reached by an 80 ms ramp, not a step; the receiver's reference follows it | 4.4, 12.2 |
| Below 9 kHz the window ladder runs from two tones to the block, with the tone and the cells inside it below; references are taken as GPS-locked at both ends | 8, 12.2 |
| Below 9 kHz, prioritize complete receiver service and include the cost of the keyed phase. The reference profile has one phase assignment per tone | 1, 4.4, 12.2, 13 |
| Frequency and drift hypotheses are a transform across a message's tone sums, routed between CPU FFT, GPU FFT and a Tensor Core matrix product; a decode per hypothesis is kept for a few hypotheses only | 8, 12.2, 13 |
| Compare qualified routes on whole-job time, reuse shared work within memory limits, and measure simulation speed on both reference machine classes | 1, 8, 13.1, 13.5 |
| Apply one scheduling policy to HF and sub-9 kHz and measure the bottleneck separately on each device | 1, 8, 13.1 |

**Open**

1. **Choosing between the HF profiles.** Size the one-cell and held-tone candidates against
   the credible diffuse path, signal level, uncertainty and deadline. Section 4.2 predicts
   about sixteen times the airtime with one-cell tones at −10 dB-Hz and 0.1 Hz spread on the
   4 s hop. Verify the crossover with calibrated payload and presence decisions before making
   either profile the general default.
2. **Phase steps beyond the mandatory one per tone** (settled below 9 kHz: none). Whether held
   tones of 4 s and more step every cell by default, and whether 2 or 4 phases per cell are offered for one-cell tones
   (1.4 to 2.8 dB against a filter-bank observer for 2 to 4 times the transforms). The mandatory
   phase overrides the brief's "tones without unnecessary phase or amplitude shifts". Its effect
   on how the emission is classified (spread spectrum) is unexamined.
3. **Which observer the LPI goal means.** The brief intends the long tone for maximum LPI. That
   holds against energy detection, and against a matched filter bank only with phase steps
   inside the tone. The stored study evaluates specified observers. Its outcomes do not establish a best-observer guarantee.
4. **Hop edges.** This draft uses 20 ms of ramp and 20 ms of quiet per edge at 0.4 s. The
   existing triad envelope (55 ms quiet, 20 ms ramp) would cost a further 1.0–1.5 dB of power
   there. Confirm what the radios need.
5. **Shift oscillator.** The hop-to-hop frequency repeatability and the stability inside a held
   tone that section 4 asks for are to be checked against the radios.
6. **Symbols per block.** On a steady path with long held tones, 16 or 8 symbols instead of 32
   would save 0.8 or 1.4 dB, and would shorten the shortest block. Fading paths need the
   diversity of 32. Not adopted.
7. **Qualification of the attended target.** Establish the achieved quiet-block rate and
   correct-word recovery on each intended channel. During the expected listening time,
   measure how many distinct false messages are displayed and how many a reader accepts as
   plausible. The Gaussian-noise test in section 8 fell short of its confidence criterion.
8. **Reduced-work routes.** The earlier 2 dB proposal supplies no worst-case loss bound.
   A route needs the full configured public coverage and evidence for its sensitivity,
   service time and workspace on the affected paths. Report an unmet deadline or missing
   input as such. Neither establishes physical quiet.
9. **Without FHSS.** Whether scoring with one phase per position is on by default, and the
   coherence window a sub-9 kHz block is sized for. The ladder itself is settled below 9 kHz:
   from two tones up with shorter cell windows inside held tones. The HF reference uses
   the 1/64-block target within the available coherent span. A retune bounds that span and
   the per-tone fallback still applies. A coarse lower rung can miss intermediate coherence
   ([12.2](#122-phase-stable-paths-below-9-khz)).
10. **Long messages.** The choices of a long-block code, byte framing, exact final-word
    padding and file-integrity checks remain open. A physical closing decision establishes
    the end of the observed word stream. It cannot establish how many padding bits the
    sender used or whether every admitted word is correct. See section 6.
11. **Alternate hop duration.** The 40 s hop is a research comparison from section 4.3.
    Its 32-hop floor alone costs 1,280 s. Select it only where the intended deadline and
    radio's measured retuning behavior allow it.
12. **Measure the complete coherent attempts.** The narrow-band CPU and GPU kernel shapes
    have timing evidence, and the corrected physical-grid GPU search has a bounded
    correctness check. The wide-band path has only synthetic stage measurements. Include
    window formation, drift inside each affected window, boundary handling and quiet-slot
    decisions in a real-time acceptance check.
13. **Search volume below 9 kHz.** State combined relative bounds for frequency, drift, clock
    and delay, including which errors share a reference and any measured path contribution.
    A hardware budget is qualified only by complete service over those bounds. An overloaded
    configuration reports its backlog and retains all required hypotheses.
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
18. **Value-dependent phase with few positions.** An earlier comparison assigned some
    information to a quarter turn as well as a position. That would change the waveform
    and require fresh evidence for both receiver branches. The present format encodes the
    value through its position alone
    ([sub-9 kHz note](../_ref/algorithms-matrixModem/Fable-5.2/sub9khz-fsk-rate-and-spacing.md#7-positions-and-a-value-dependent-phase)).
    The sub-9 kHz configuration does not need it, because sixteen positions fit.
19. **Sub-9 kHz carrier and slot.** The example starting at 8,259.375 Hz fits between
    the assumed 50 Hz and 60 Hz harmonic families on the steadier grids. Confirm the
    actual site spectrum and radio passband before choosing the carrier and scale,
    especially near a 16.7 Hz supply. The night mode cut-offs near 5.17 and 8.27 kHz remain open
    ([sub-9 kHz note](../_ref/algorithms-matrixModem/Fable-5.2/sub9khz-fsk-rate-and-spacing.md#9-weaknesses-and-open-items)).
20. **Ramp length against a real transmitter.** 80 ms is calculated for a single-tuned antenna.
    What a transmitter and its matching network tolerate is to be confirmed, and with it whether
    160 ms should be the default.
21. **Scheduling acceptance.** The proposed factor of 1.2 compares a scheduler with the
    best qualified fixed assignment actually tested on the same workload and device.
    Measure absolute one-stream service and workspace on the operating targets as well as
    relative route performance. Report independent simulation trials per second separately.
22. **What is left of the hypothesis forms.** Whether a transform across positions is worth
    building for a CPU-only receiver with ±1 s of timing below 9 kHz (estimated gain about
    two); the dimensions for which 13.1 states no measured form (clock rate, hop frequency,
    delay rate, tilted bin paths); and tiered screening.
23. **Precision of the transforms.** The CUDA experiment used FP16 inputs with FP32
    compute and FP16 or FP32 output. Its integer route used INT8 inputs and INT32 compute
    and output. Exact integer accumulation for quantized operands still leaves coefficient
    error and range to check. A four-bit transform and its effect on near-threshold word
    and quiet decisions remain unqualified. [Device validation](algorithms-matrixModem/search-validity/device-validation.md)

## 15. Known weaknesses

- **Complete-format performance is open.** A native 10-bit experiment now connects samples,
  the modeled schedule, exhaustive codeword search, calibrated admission and two-quiet closure.
  The 1% diagnostic exercised physical closure on a configured calendar. With the ordinary
  receiver's attended thresholds and an intact 18 dB block E/N₀ signal, all 64 five-block
  pointing-loss trials delivered both initial words, closed on the second quiet block and
  reopened on the next word. Its independent quiet-only holdout admitted one false
  word in 30,000 blocks. The one-sided 95% upper bound of 1.58 × 10⁻⁴ exceeds the 10⁻⁴
  target. Long-run false closures and false messages under field interference remain open.
  Durations in section 10 remain model estimates.
  [Continuous-stream outcomes](algorithms-matrixModem/block-reception/pointing-loss.md#results) ·
  [Original quiet and wrong-schedule controls](algorithms-matrixModem/block-reception/fault-tests.md#false-words-at-the-attended-target)
- **One cell per tone is the slower choice for weak signals with per-tone reception.** Against
  the bound it stands at 14.2, 5.7 and 1.2 dB at 0.1, 1 and 10 Hz of spread (−10 dB-Hz, 4 s
  hop), where the flat part of a held tone gives 2.2, 0.6 and 0.2 dB. These are airtime
  gaps in the cited model. A fade which erases the complete block can have a larger cost.
- **One-cell alphabet evidence is conditional.** The wide-pool average in the one-cell
  pairwise calculation hides a severe narrow-band failure. A complete-codeword spacing
  comparison across diffuse and notched paths remains open. [One-cell controls](../_ref/algorithms-matrixModem/fsk-lattice-validity.md)
- **Wide-band coherent service is unmeasured.** The proposed 2.2 kHz timing grid is about
  57 µs, or 0.18 sample at 3,200 complex samples/s. One 20 ms coarse interval contains
  about 352 such spacings. Integer shifts of a decimated input cannot realize that grid.
  Validate a fractional-delay reference or equivalent window correction across boundaries.
  The historical 9.7 s laptop and 66 ms GPU figures cover kernels for one coarse interval.
  Sensitivity and throughput of the complete fine search remain open.
- **Broad oscillator uncertainty can exceed the computational ceiling.** The earlier
  24-bit crystal estimate failed even for projected stages. Neither the successful
  narrow-bank device check nor the small-message projections establish complete service
  for these clock classes. See section 12.2 for combined reference bounds, tested rows,
  omitted intra-window curvature, boundary processing and the cost of multiple bands.
- **Coherent search has component evidence.** Corrected physical-grid CPU and GPU
  kernels recovered a planted signal. A 10-bit GPU experiment measured wrong messages
  on a steady, known-timing path. The CPU benchmark includes short-tone sub-block sums.
  The field oscillator trajectories, sub-9 kHz noise, real timing search, complete
  ladder acquisition and full receiver service remain unqualified. A specular HF path
  also adds a delay-rate dimension across a wide band.
- **A block sized for coherence can lose sensitivity when phase wanders.** At −37 dB-Hz
  the ideal sub-9 kHz sizing gives 3.17 days with whole-block coherence, 29.0 days with
  21.85-minute windows and 8.45 years with per-tone reception alone. The receiver can try
  shorter windows on the available samples. Achieving the same miss probability may
  require a different transmit duration. Night-time wander and transitions near sunrise
  or sunset need their own channel histories. [Current sizing](#phase-stable-profile-below-9-khz)
- **The sub-9 kHz configuration rests on an assumed antenna.** The bandwidth at 10 kW goes with
  the inverse square of a top-voltage limit that is known only within 30 to 100 kV for a 61 m
  installation, and the mains slot rests on grid statistics without a measured line amplitude
  near 8 kHz (section 12.2).
- **The phase ramp has model evidence.** The 80 ms ramp reduces the transient in a
  single-tuned model with a constant-amplitude source. The calculated drive and current
  can still be 49° apart for a 24 Hz antenna. Transmitter, matching-network and protection
  behavior require hardware measurements.
- **The sub-9 kHz slot is not clear everywhere.** A 16.7 Hz railway supply puts a harmonic near
  8,266.7 Hz inside it, and on unsteady grids the mains harmonics cross it. One line covers one
  position in sixteen for as long as it sits there; no line amplitude near 8 kHz was found, and
  the excision has not been run on such noise.
- **The sub-9 kHz configuration serves a strong link slowly and shows a narrow line.** Its
  shortest block is 10.9 minutes. A 10 s tone with one phase leaves an observer with a filter
  bank little to overcome, which is accepted there (section 1). It is also a second cell length
  to implement and test.
- **Held tones and frequency.** Reciprocal-duration resolutions are about 2.5 mHz for
  400 s and 0.25 mHz for 4,000 s. Retained energy depends on the residual offset, drift and
  phase noise after correction. In the cited 0.1 Hz path model, a fresh error within ±1 Hz
  per hop cost 6–11 times the transmit duration. The joint oscillator and path-Doppler
  search still needs an acquisition and service measurement for the intended envelope.
- **Held tones with one phase are conspicuous to a matched observer.** With 32 per block an
  observer with a filter bank needs only 2 to 6 dB more C/N₀ than the link.
- **The observer's best statistic against the keyed signal is not established.** The pulse
  attack on an unkeyed signal is computed and simulated (11.5 dB in 18 hours); the fourth-power
  attack on the four-phase key is not.
- **Phase steps inside a held tone make timing a hard requirement.** Half a phase interval of
  timing error can severely reduce the score. On a single abrupt phase reversal a
  half-interval displacement cancels the sum, while equal phases retain full power. The timing search covers the clock
  uncertainty in offset and in rate: 26 rate hypotheses for a 3.6 h block on a ±20 ppm clock,
  256 for a 36 h block. A clock whose rate changes during the block adds a dimension that is not
  sized. Phase steps through a narrow antenna are now calculated for the receiver's side and for
  a model transmitter (sections 4.4 and 12.2); through a radio's filters they were not examined.
- **Concealment figures are for an idealised observer on a steady path.** Fading paths, the
  periodic hop envelope, a noise-uncertain energy detector and real observers were not
  evaluated; the earlier
  [observer notes](../_ref/algorithms-matrixModem/Sol-6.2/observer-and-interference.md) apply.
- **Hop spanning lengthens strong-signal blocks.** With FHSS the 32-hop floor is 13 s with 0.4 s
  hops, 21 minutes with 40 s hops, whatever the level. The per-hop weighting and the lost-hop
  cases were calculated as erasures, not simulated with real interference.
- **Strong incoherent swept interference.** The retained tests establish no finite loss
  bound above about 20 dB over the noise. The measured results in section 9 depend on
  waveform coherence and which conditioning chain was used.
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
- **Outage can look like quiet.** Two consecutive transmitted periods extinguished at the receiver
  caused premature closure in the native test. One extinguished interior period followed by
  successful reception remained an erasure. After closure, resumed reception starts a new
  episode at the continuing public ordinal. [Continuous-IQ checks](algorithms-matrixModem/block-reception/README.md)
- **Long messages** pay the block penalty until a long-block code is qualified.
- **Weak HF field cases.** The retained milliwatt-power HF examples extend to about
  +3 dB-Hz. Lower study points in section 10 are theoretical.
- **The search forms of 13.1 are measured as kernels with synthetic values.** The transform
  across tone sums ran on CPU and GPU for a fixed bank, including a corrected on-grid check.
  Historical projections for broader classes lack full receiver costs.
  Six of the dimensions listed in 13.1 have no measured form. For hypotheses with their own
  evidence the 32 additions per message and hypothesis remain, and the faster loop order
  differs between CPU and GPU, so it has to be measured on every machine.
- **GPU simulation speed depends on the fixture.** The corrected ten-bit Monte Carlo
  measured about 78,000 warm trial iterations/s on a full RTX PRO 6000 Blackwell Server
  Edition for its steady, on-grid, known-timing signal in Gaussian noise. The older
  34,900/s run used different geometry. Both omit physical window formation and quiet
  decisions. The historical 11.4 s projection for 24 bits concerns selected GPU stages.
  Complete receiver capacity on the intended hardware remains unmeasured.
  [Device, arithmetic and timing scope](algorithms-matrixModem/search-validity/device-validation.md)
- **Four-bit transform performance remains open.** The tested floating-point and integer
  matrix products use the wider compute formats in open decision 23. Stored four-bit bin
  components do not establish four-bit transform arithmetic or sufficient accumulator range.
- **The scheduler exists as a test prototype.** Its fixed-work comparisons used synthetic
  portions and emulated device limits. The reported 4–12% exploration cost applies to
  those fixtures. Runtime feedback, reduced-work qualification and continuous full-search
  service on a received stream remain open.
- **Acquisition** over real clock, oscillator and drift uncertainty, native 4-bit devices,
  integrated GPUs and live operation are unmeasured.
