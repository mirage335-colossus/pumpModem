# DSSS and illustrative FHSS controls

These controls are in the local development candidate. New outer-DSSS reception,
timing integration and native GUI behavior are not fully qualified; they are not
a claim of practical 1000x spreading at constant receiver CPU cost.

## Separate streams and waveform geometry

The public DSSS mode choices are **Off** and **Interleave**, with a separate
10x, 100x or 1000x Spreading selector. Off retains the existing waveform and
remembers the selected factor, including save/load. Selecting a factor enables
Interleave.
An enabled factor applies fine QPSK chips from the keyfile's dedicated DSSS
stream. The `legacy` construction holds each inner coefficient for the factor's
fine chips. The local `interleave` construction additionally permutes the
complete fine-chip coefficient positions within each bit before shaping. The existing independent private zero
and one candidates remain at every bit position. The Data cipher and Scrambler
streams are not reused as outer spreading streams.

Each factor has its own versioned counter domain. Its address includes the
canonical epoch, symbol ordinal and fine-chip position. The two-entry inner-chip
cache is valid only for that candidate's exact epoch and coarse-chip position;
it does not share private patterns across bits, keys or epochs. The legacy DSSS
layer remains in separate domains. Factor1 bypasses the new outer layer.

The Rate field continues to describe the inner bandwidth. Intended outer
bandwidth is Rate multiplied by the selected factor; sample quantization, pulse
shaping and the existing amplitude limiter still apply. Automatic planning
reserves at least 64 complete inner chips, using actual integer sample geometry
to avoid rounding-induced symbol-length cliffs. Some sample-quantized geometries
therefore need a longer symbol. Neither short/raw message framing nor the fixed
interval format gains additional bits.

Outer QPSK chips are formed before pulse shaping. The final fine-chip sequence
receives the existing root-raised-cosine pulse with rolloff 0.25 and support
extending eight fine chips on either side, followed by the radial amplitude
limiter. DSSS does not abruptly switch an already-shaped envelope. Finite pulses,
burst boundaries and limiting still produce spectral tails; ideal RRC support
is not an emission mask or a guarantee of negligible radio-filtering loss.

The legacy unit-magnitude outer chips retain the inner chip's power envelope. At inner
Rate 10 Hz, that envelope can persist for 0.2 seconds. Adding independent random
outer amplitudes does not remove it: conditional mean power remains proportional
to the inner power, and the extra amplitude variation can increase peak clipping.
The 9 October spectrum investigation isolated substantial spectral regrowth in
the existing final limiter, especially in high-amplitude intervals. This is a
measured legacy waveform limitation; neither version may be advertised as
a sharp measured emission mask. See the quantitative
[spectrum follow-up](dsss-followup-validation.md#amplitude-envelope-and-limiter-investigation).

### Versioned interleaving and headroom

The local GUI/CLI defaults to `--dsss-version interleave` when outer DSSS is
active. Use `--dsss-version legacy` for the preceding wire construction. Peers
must select the same version; there is no transmitted version marker or automatic
cross-version negotiation. Off exports `--dsss-mode off` with the remembered factor and Interleave construction and keeps its existing
waveform. Saved parameter lists and launch commands carry the explicit factor and
construction. The old `interleaved-v2` identifier remains an accepted alias;
canonical export uses `interleave`. The explicit mode is saved separately from
the factor; older factor1 commands still load as Off, and older active factor
commands preserve their specified construction. Explicit `legacy` imports stay visibly labeled
Legacy (diagnostic), preserving their exact waveform until Interleave is selected.
Legacy is absent from the ordinary mode menu, but retained internally and through
CLI for diagnostic comparisons. There is no established universal 20% CPU penalty
that would justify deleting that reference. Cryptographic version/counter domains
and the internal `interleaved_v2` identifier are unchanged by this UI simplification.

V2 uses six new counter domains: an independent permutation-seed domain and a
rotation domain for each of factors 10, 100 and 1000. A canonical epoch/symbol
ordinal addresses a fixed 32-byte seed; variable rejection draws then occur in
that symbol's child stream. They cannot advance into another symbol's seed,
private zero/one pattern, Data, Scrambler, legacy DSSS or FHSS stream. Fisher–Yates
uses unbiased rejection draws with an explicit bounded setup budget. Unsupported
symbols exceeding 1,048,576 complete fine chips fail explicitly, not by silently
changing the wire construction.

Two adjacent symbol maps are retained because pulse tails overlap symbol
boundaries. Their storage is eight bytes per complete fine chip plus fixed cache
and setup accounting. The rate10/factor1000 example has 64,000 complete fine chips,
so its two maps occupy 512,000 bytes. Each bit alternative has a separate bounded
inner-coefficient cache. Reuse requires the exact key, version, factor, canonical
epoch/ordinal and inner-chip address. Pattern-mode phase changes invalidate
absolute-chip shape caches; the two canonically addressed V2 states remain
reusable only when their complete epoch/ordinal identities still match. A
partial final fine chip stays in place, preserving its original weighted energy.
The permutation and unit-magnitude rotations are common to the two independent
inner candidates, which preserves unshaped full-symbol norms and distances.
This is not an equivalence proof after finite pulses, clipping, partial observation,
interference or phase diffusion.

Interleaving distributes held inner coefficients through the bit rather than
adding independent multiplicative amplitude randomness. It does not equalize
whole-bit power or erase repeated coefficient radii/histograms. Public waveform
statistics remain a limitation; this is not a claim of noise indistinguishability.
Final RRC shaping and smooth burst boundaries follow the chip transformation.
V2 halves the complex amplitude before the radial limiter, including training and
suppression output. Away from limiting, that is 6.0206 dB of digital power backoff
at unchanged downstream gain. Clipped legacy intervals change that comparison;
measured average-power and equal-peak comparisons must be reported separately.
Restoring average power in software or hardware can reintroduce clipping.
The link budget takes actual average radio transmit power, not digital sample
peak. Its entered power is not automatically reduced by 6.02 dB. The simulation
channel still references the common nominal digital signal power for noise, so
unchanged simulation settings include V2's actual digital backoff. Equal-average-
power waveform comparisons explicitly normalize the measured finite waveform;
they do not imply available hardware gain or peak headroom.

V2 is an experimental manual-test construction. Its full-acquisition sensitivity,
wrong-key behavior and hardware spectrum are not qualified by the previous
conditional legacy-factor10 evidence. The planner may display an explicitly conditional RX reference when its bounded
finite-shape/limiter calculation is supported. It does not qualify full acquisition
or cumulative sensitivity. Unsupported geometries continue to show an unavailable
reason; see [simulation estimates](simulation-estimates.md).

Enabling Interleave from Off, or selecting 10x, 100x or 1000x in the GUI, sets a useful voice-passband starting
point: inner Rate 360, 36 or 3.6 Hz respectively, with stream Carrier 1500 Hz
(displayed absolute Carrier is Shift plus 1500 Hz). Explicit imported commands
retain their supplied Rate and Carrier. The outer nominal Rate is 3600 Hz in
all three presets. Ignoring sample quantization, the shaped span is 375–2625 Hz;
actual geometry can be narrower. Increasing factor at this fixed outer bandwidth
lengthens the inner chips and symbols; more collection energy requires airtime.

The IC-7100 manual specifies SSB transmit defaults of 300–2700 Hz for MID and
100–2900 Hz for WIDE. SSB-D receive filter defaults are 1.2 kHz, 500 Hz and 250 Hz,
so they must be widened for this waveform. Nominal containment does not establish
flat response or negligible filtering loss.
[Icom full manual, printed pages 5-6 and 6-6](https://www.icomamerica.com/api/download.php?fl=JTJGdXBsb2FkcyUyRnN1cHBvcnQlMkZtYW51YWwlMkZJQy03MTAwX0VOR19GTV81LnBkZg%3D%3D&post_id=2288)

The current receiver uses its matched-template paths at the outer chip rate.
This is a functional fallback, not a qualified fast despreader. Its work can grow
with the outer bandwidth and search bank. Higher bandwidth does not create extra
received bit energy at fixed power and bit duration. Qualified full-bank detection probabilities remain unavailable for the new outer factors.
For legacy DSSS the GUI can show a separately labeled matched-template AWGN
reference, which omits the presence guard and does not predict complete acquisition
success. Interleave uses its own bounded actual-source conditional reference,
including finite shaping and limiting, where that geometry is supported. Qualified coherent
searches now use bounded timing components and paired direct/tiled contraction;
this reduces actual acquisition work without claiming a fully qualified fast
despreader. See [measured search work](search-compute.md).

An early strong-signal control with the correct inner key and a different outer
key produced a standalone accepted candidate. Independent outer phases can have
small accidental correlation while still exceeding a sample-count statistic
calibrated for white noise: shaped waveform samples are correlated. Fine outer
chips also share the inner coefficient's amplitude envelope. No key/counter alias
was found; short reception remains detection, not authentication.

The follow-up receiver adds conditional independent-outer-code evidence when
coherent input has strong residual lack-of-fit in the chip subspace after
subtracting coherent explained energy and rank. It accumulates each
finite pulse's complete observed dot before squaring, retaining fractional/clipped
pulse observations. The original admission score and thresholds remain; active
guards can only reject an otherwise accepted candidate. Near-threshold white-noise
cases retain the original decision unless the conservative excess-energy trigger
activates. Additional bounded state is reserved after the original detector bank.
Enhanced phase/differential branches, insufficient-workspace cases, unsupported
rank/nesting and too few contributing pulse terms retain the old path and are not covered by this wrong-outer-code fix. Neither this conditional
bound nor clean decoding establishes universal wrong-key rejection or fully
qualified sensitivity; paired measurements and limitations are recorded in
[follow-up validation](dsss-followup-validation.md).

Long compact receptions additionally compare immediate timing neighbors with the
same key/epoch/clock/frequency and matching canonical accepted prefix. Accepted-bit
ownership stays fixed. A stronger compatible guide must finish scoring its absent
symbol before it can permit physical completion. Guide records are subscribed
on first admission, before later neighbors finish the same symbol, and retained
within the existing workspace. Idle lanes allocate no guide records. Shared-cache
headroom includes possible guide growth and transient allocations; exhaustion is
reported explicitly rather than permitting an unsupported early terminal event.
This does not interpolate exact off-grid arrival time or let EOF, cancellation or a decoded source end reception.

## Fake FHSS

Off and Fake are selectable. Genuine and IC-7100 hardware hopping remain disabled.
Fake uses a dedicated FHSS stream/domain to illustrate a permutation of 200
channels with 0.4-second dwell. During transmission, Carrier and Shift display
the same hop delta in their disabled editors, and the FHSS selector shows the
current channel. The base values used by settings, launch commands and parameter
lists remain unchanged, and return to view after transmission or cancellation.
The transmitted audio and radio hardware do not hop.
Simulation is changed only when Simulation is actually enabled.

The Link Planner reserves the highest displayed carrier/shift frequency for its
oscillator calculation. The observer preview now models secret hopping with every
channel captured simultaneously, including all signal energy. It compares the
hop-set radiometer with a channelized dwell-energy maximum at the same global
false-alarm criterion and selects the faster strategy. No interception loss or
universal channel-count gain is assumed. This is an analytical detector comparison,
not calibrated adversary performance. The actual Fake output remains fixed.
Channel spacing accounts for the proposed occupied bandwidth, but this is not
a measured emission mask or regulatory conformity statement. See the
[observer model](lpi-estimates.md#frequency-hopping).

## Regulatory scope

US [47 CFR 15.247](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.247)
has band-specific channel-count, spacing, bandwidth, power and occupancy rules.
It does not prescribe one universal hopping pattern or require a particular
internal oscillator architecture. Passing a dropdown preset is not certification.

European requirements also depend on band and equipment category. The relevant
examples include [ETSI EN 300 328 V2.2.2](https://www.etsi.org/deliver/etsi_en/300300_300399/300328/02.02.02_60/en_300328v020202p.pdf)
for 2.4 GHz equipment and [ETSI EN 300 220-2 V3.3.1](https://www.etsi.org/deliver/etsi_en/300200_300299/30022002/03.03.01_60/en_30022002v030301p.pdf)
for specified sub-GHz equipment. A 200-channel/0.4-second illustration is not
universally compliant with their occupancy, duty-cycle or return-time conditions.
Real hopping needs an appropriate hardware control path and band-specific testing.

The IC-7100 RF oscillator preset uses the published ±0.5 ppm per-radio frequency
stability as an illustrative relative-link model. It does not assert an oven,
measured phase noise, supported ISM-band operation or timed hardware hopping.
[Icom specifications](https://www.icomjapan.com/lineup/products/IC-7100USA/)

See [clock synchronization](clock-sync.md), [oscillator models](oscillator-models.md)
and the [development contract](development.md). Conditional **legacy** factor10 coherent
threshold curves estimate0 dB additional loss with a simultaneous95%-coverage
upper bound0.08 dB. Full-bank curves, enhanced branches, hardware/regulatory
measurements and broader platform qualification remain open.

The receiver geometry, arithmetic coverage, template reuse, arrival grid and bank
work details are grouped under **Model limits and references** in Link Planner.
The developer arithmetic selector and the main RX/CPU results remain visible.
Manual Doppler is documented in the [modem reference](modem.md#manual-doppler-calculator).
