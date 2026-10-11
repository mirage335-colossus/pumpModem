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

The ordinary choices are **Off**, **Fake: FCC**, and **Fake: EU**.
Developer mode additionally exposes **Fake: List** and the **FHSS** editor tab.
`--fhss off|fake-fcc|fake-eu|fake-list` is shared by GUI startup, Link Planner launch
commands and parameter-list load/save. An omitted option preserves the current
selection on partial import (new configurations default to Off). The historical
`fake-0.4s-200` identifier maps to `fake-fcc` and exports canonically as such.
It previously selected no jurisdiction; this mapping preserves Fake intent,
not its obsolete universal 200-channel/100 kHz geometry. Changing RF frequency
never changes the saved FCC/EU preference.

DSSS and every FHSS mode require an active transmit encryption key. Without
one, their selectors are disabled, effective DSSS is Off, and FHSS generates no
hops or hopping-based planner adjustments. Saved/CLI preferences and list
contents are retained but explicitly labeled **Off · encryption key required**;
loading a key and selecting it enables those remembered choices. A receive-only
keyfile or selecting transmit key None does not enable spreading. Developer
list editing remains available for preparing a configuration.

### Developer frequency lists

`--fhss fake-list --fhss-list '30MHz,30.006MHz,30.02MHz'`
selects exact, possibly nonuniform Shift frequencies. Alternatively use
`--fhss fake-list --fhss-list-file frequencies.txt`. A file or the multiline
FHSS editor accepts one frequency per line, commas or semicolons, optional
Hz/kHz/MHz/GHz/THz units (bare numbers are Hz), and `#` comments to end of line.
Lists contain 2–200 distinct nonnegative finite Shift frequencies (zero is
allowed) within 8192 bytes.
Files must be regular files; arguments are never evaluated as shell commands.
The launch parser itself performs no file I/O. Loading and validation precede
application of the settings, including asynchronous keyfile loading.

**Apply list** commits an editor draft. Invalid edits retain the last applied
list and show an FHSS-specific error. Choosing Off/FCC/EU or hiding Developer
mode retains the list; hiding Developer mode also retains an active CLI/list
selection. The editor and Apply action are disabled during transmission.
Launch exports and parameter-list saves contain canonical inline frequencies,
so subsequent edits to the original file cannot change a saved configuration.
Omitted list options preserve the current applied list on partial import.
Supplying both inline and file options is rejected.

Every list entry gets the existing 0.4-second residence. The dedicated keyed
permutation indexes the entered order. The list specifies Shift, not Carrier.
Each illustrated RF center equals its
listed Shift plus the unchanged Doppler-adjusted audio carrier. Carrier and Shift
receive equal display deltas, so Carrier minus Shift stays fixed. Physical audio,
radio tuning, framing and keystream addressing are unchanged.

The planner uses the exact count, minimum adjacent spacing, min/max RF and
center span. It adds the unchanged audio carrier to the highest listed Shift for
oscillator/search uncertainty.
A list is a custom experimental illustration with no regulatory profile.
Guarded spectra must fit any resolver operating-band limits and stay above DC;
illustrated Shift cannot become negative. Unsupported geometry is reported and
retains fixed-band estimates. Overlapping guarded channels can still illustrate
hops, but cannot use the disjoint-channel observer model: fixed-band observer
estimates are retained with an explicit model limitation. The observer captures
the entire list simultaneously; no automatic channel-count benefit is added.

Fake uses the dedicated `Fhss` purpose key and `FhssCycleV2` counter domain
(`fh-cyc02`). Each complete channel cycle uses a fresh keyed permutation;
the previous fixed repeated permutation is no longer selected. `FhssSchedule`
owns this algorithm below the GUI. It receives the actual TX key and source epoch;
its sample-indexed result is the sole source of the displayed hop. No Data,
Scrambler, private-pattern or DSSS stream is borrowed. An active transmit
encryption key is required; there is no unkeyed sequential fallback.
Every illustrated channel receives equal residence in each complete cycle.
Carrier and Shift display the same hop delta during TX, retaining their baseband
difference. Their configured values, launch exports, physical transmitted PCM
and radio tuning stay fixed; the configured displays return after completion
or cancellation. No access protocol or actual RF hopping is implemented.

### Fresh cycle addresses and bounded work

For hop `h` and channel count `N`, cycle `c = floor(h/N)` owns byte region
`[1024*c, 1024*c + 1023]` in the FHSS purpose/epoch/domain stream. A descending
Fisher–Yates shuffle reads big-endian 32-bit draws from that region. For each
bound `b`, draws at or above `floor(2^32/b)*b` are rejected before modulo
selection, avoiding modulo bias. Unused reserve bytes are skipped, never lent
to another cycle or cryptographic role. All 64-bit byte arithmetic is checked;
exhaustion fails explicitly instead of wrapping or replaying an earlier order.
`FakeFhssV1` remains available for diagnostic vectors only; existing keyfiles,
FCC/EU/List selections and configured lists need no migration.

Only the requested cycle is constructed, even on a distant or backward seek.
The cache holds one order (at most 200 indices), with an immutable key, epoch and
plan; its exact cycle is the validity key. Repeated lookups in that cycle reuse
the same order. A cycle construction has 1024 bytes of stream scratch and an
800-byte replacement order. It needs at most 256 draws for at most 199 accepted
shuffle selections; exhausting the draw budget throws, with no biased fallback.
Assuming independent pseudorandom 32-bit draws, a conservative union bound on
58 rejections among 256 draws is `2^256*(199/2^32)^58 < 2^-1150`.
This is a conditional analytical bound, not an observed rare-event rate.

Fresh cryptographic input does **not** imply unique channel sequences forever.
There are only `N!` permutations, so independent cycles can coincide, especially
for short lists. The scheduler removes programmed recycling; it does not reject
legitimate repeated random outcomes. Equal-use permutations also constrain the
remaining channel choices within a cycle; they are not independent uniform
channel draws at every hop. It is also not literally infinite: the
address space is finite, checked and far beyond practical transmission lengths.
Reusing the same key, TX epoch and cycle recreates the same schedule. This fix
adds no cross-process nonce/replay coordination.

### Generation scope and future tuning adapters

The TX scope captures the first up-to-32 bytes actually consumed by the **current
FHSS cycle**, alongside separate DSSS mapper, outer-rotation and Interleave-seed
bytes. Its snapshot includes cycle and byte offset, and selects the new order
before copying its prefix. It does not regenerate bytes using a GUI-side key.
Noise transmission uses the fresh temporary key inside its transmitter, never
the saved message key. Exact sample coordinates rather than GUI polling determine
each hop; the first generated sample belongs to hop zero. Completion/cancellation
retains the last trace; replay restores its matching trace; a new request starts
a new schedule. The sequence includes settling and suppression audio.

A future hardware adapter **must consume the same keyed `FhssSchedule`**, with
its immutable Shift list and actual TX epoch, and map sample coordinates to DAC
presentation time. It must also implement device command latency, settling,
channel/band constraints, dwell/access policy, cancellation and receiver tuning.
Equal use within each fresh cycle does **not** establish a sliding-window
occupancy limit. A channel can appear at the end of one cycle and start of the
next: at 0.4 s per slot this gives up to 0.8 s contiguous residence, or two visits
in a window crossing a cycle boundary. Maximum start-to-start return is
`(2*N - 1)*dwell`, including 37.2 s for 47 channels versus the quoted 20 s
sub-GHz requirement. The EU 20-channel example has a 15.6 s upper bound, within
its 32 s return target, but remains Fake with no conformity claim.

The profile descriptions quote requirements; the fresh schedule does not enforce
FCC/EU sliding occupancy or the EU sub-GHz return requirement. At full duty,
when all per-channel caps sum exactly to the window duration, preserving each
sliding cap forces a window-periodic schedule. A varying physical implementation
therefore needs explicit scheduling slack/access gaps or another constrained
construction. Those tradeoffs are not silently added to the illustrated profiles. A physical
scheduler must also retain per-channel airtime history across transmissions and
key/epoch changes; a fresh epoch does not reset a regulatory observation window.
The 0.4 s slot is also not permission for an uninterrupted EU LBT burst;
required burst splitting and idle periods remain unimplemented. The observer
still captures the entire hop set simultaneously; there is no new channel-count
multiplier or RF-hopping claim.

### Automatic geometry

Below **800 MHz actual RF**, both preferences resolve to the same narrow
experimental profile. This threshold is a **product convention**, not a
regulatory boundary. Actual RF includes the manual Doppler adjustment. A shaped
waveform uses its sampled chip rate times `(1 + rolloff)` for ideal occupied
width; other waveforms use their nominal width. Channel spacing is 1.25 times
the larger of that ideal width and nominal `Rate × DSSS factor`: an additional
12.5% edge allowance on each side of this conservative width envelope. The
nominal envelope also covers the rectangular-pulse alternative, so fractional
rates that cross pulse eligibility as the planner changes target do not leave
a stale hop set. This is deliberately wider than the ideal shaped width. The
allowance is an engineering choice, not a measured 20 dB bandwidth, emission mask or bound on all
finite-pulse tails. There is no absolute spacing floor in this mode.

The experimental list has up to 200 channels and 0.4 s residence. Center span is
`(N - 1) * spacing`; total guarded extent additionally includes one guarded
channel width. The entire set stays below 800 MHz and within supplied band
limits. It shifts down near the upper edge, and reduces the experimental count
if necessary; an impossible set is reported unavailable. Illustrations never
require negative Shift. The current GUI has no independent operating-band
selector; the resolver accepts explicit bounds and tests cover them. Regulatory
profiles use their own band bounds and never shrink their required count/coverage
to fit a narrower band.

For example, resolving the existing application configurations gives:

| Experimental configuration | Ideal shaped width | Spacing | 200-channel center span |
| --- | --- | --- | --- |
| Rate 0.01, DSSS Off | 0.00625 Hz | 0.0125 Hz | 2.4875 Hz |
| Rate 360, DSSS 10× | 2250 Hz | 4500 Hz | 895500 Hz |
| Rate 10, DSSS 1000× | 6250 Hz | 12500 Hz | 2487500 Hz |

These are resolved geometry values, not measured RF emissions or receiver speedups.

### Band profiles and sources

These are illustrative engineering profiles checked against primary sources on
2026-10-10. They are not regulatory approval, and the entered transmit power is
not silently reduced to meet the listed ceilings. Antenna gains, spurious
emissions, national availability and hardware certification still matter.

| Preference / actual RF band | Channels / spacing | Residence and access assumptions |
| --- | --- | --- |
| FCC 902–928 MHz | 50 for guarded width <250 kHz; otherwise 25; spacing=max(25 kHz, guarded width), width <=500 kHz | 0.4 s/channel in 20 s or 10 s; 1 W or 250 mW conducted ceiling before antenna restrictions |
| FCC 2400–2483.5 MHz | Prefer 75; 15 if 75 cannot fit; same spacing rule | 0.4 s/channel in 0.4×N s; 1 W with 75 nonoverlapping channels, otherwise 125 mW conducted before antenna restrictions |
| FCC 5725–5850 MHz | 75; same spacing rule, guarded width <=1 MHz | 0.4 s/channel in 30 s; 1 W conducted before antenna restrictions |
| EU 863–870 MHz national SRD example | 47 at 100 kHz, guarded width <=100 kHz; this implementation uses only 863–868.6 MHz to avoid alarm exclusions | 0.4 s residence; return within 20 s; **0.1% whole-transmission duty**, 25 mW e.r.p.; national availability must be checked |
| EU 2400–2483.5 MHz adaptive | 79 at max(1 MHz, guarded width); complete set must fit | 0.4 s accumulated/channel in 6 s (0.4×minimum required 15 channels), >=70% band capability; 100 mW e.i.r.p.; adaptive CCA/DAA required; LBT burst <=60 ms followed by >=5% idle, minimum 100 µs |
| EU 5725–5875 MHz SRD | 20 at max(7.2 MHz, guarded width); >135 MHz center span | 0.4 s residence (standard allows <=1 s), every channel within 4×residence×N; 25 mW e.i.r.p. |

FCC source: [47 CFR 15.247(a)(1), (b)](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.247).
The resolver substitutes the guarded waveform estimate for unmeasured RF 20 dB
width, and uses conservative nonoverlapping spacing rather than the optional
2.4 GHz two-thirds-bandwidth alternative.

European sources are band-specific:
[EN 300 220-2 V3.3.1 Table 4 band V, clause 4.4.11 and Annex C](https://www.etsi.org/deliver/etsi_en/300200_300299/30022002/03.03.01_60/en_30022002v030301p.pdf)
for the national sub-GHz example;
[EN 300 328 V2.2.2 clauses 4.3.1.4–7](https://www.etsi.org/deliver/etsi_en/300300_300399/300328/02.02.02_60/en_300328v020202p.pdf)
for adaptive 2.4 GHz;
[EN 300 440 V2.2.1 clause 4.2.6](https://www.etsi.org/deliver/etsi_en/300400_300499/300440/02.02.01_60/en_300440v020201p.pdf)
and [EU 2025/105 Annex row 61](https://eur-lex.europa.eu/eli/dec_impl/2025/105/oj/eng)
for 5.8 GHz SRD. EU sub-GHz operation is not assigned the EU 2.4 GHz rules.
Other bands, excluded subbands and geometries that cannot fit return an explicit
unavailable reason while preserving the selected preference. Fake transmission
remains fixed-band and is not disabled by this advisory result.

### Planner and observer scope

Link Planner shows resolved channels, spacing, residence, center span, guarded
width, RF endpoints and access assumptions under **Model limits and references**.
Its oscillator/search estimates use the highest illustrated actual RF and Shift.
The observer and draft inspection receive the actual resolved count/residence,
not the old 200-channel defaults. Unsupported profiles retain fixed-band estimates.
The observer captures every channel simultaneously, including all signal energy,
and compares aggregate and channelized dwell-energy detection. No automatic
channel-count multiplier or receiver CPU credit is granted. Access gaps, duty
restrictions and retransmission scheduling are not modeled: EU estimates are
conditional on the illustrated on-air dwell, not elapsed compliant operation.
See the [observer model](lpi-estimates.md#frequency-hopping).

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
