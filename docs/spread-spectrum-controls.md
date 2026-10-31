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
point: inner Rate 360, 36 or 3.6 Hz respectively. Carrier recommendations are
recalculated and a valid selected baseband Carrier is retained. Explicit imported
commands retain their supplied Rate and Carrier. The outer nominal Rate is
3600 Hz in all three presets. With a 1725 Hz stream carrier and ignoring sample
quantization, the shaped span is 600–2850 Hz; actual geometry can be narrower. Increasing factor at this fixed outer bandwidth
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
receive equal display deltas, so Carrier minus Shift stays fixed. Audio frequency,
radio tuning, framing and keystream addressing are unchanged. Hop power ramps
change audio amplitude as described below.

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
difference. Their configured values, launch exports, audio frequency
and radio tuning stay fixed; the configured displays return after completion
or cancellation. No access protocol or actual RF hopping is implemented.

For simulated propagation and **Simulation → Audio test**, the unchanged
smooth-ramp source is followed by Doppler/delay spread and the optional
frequency-selective amplitude modulation. The channel output may contain
delayed DATA during an original transmitter quiet interval; the transmitter
guard applies before propagation and is not reapplied afterward. Helper uses
its separate original envelope. These effects add neither RF retuning nor an
echo equalizer; existing receiver templates may not recover cross-hop echoes.
See [channel ordering and limits](simulation-estimates.md#propagation-spread-and-additional-perturbation).

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

## Cover and tuning transmission

Transmit noise and private surrounding cover now use full virtual-symbol outer
DSSS, including the selected permutation/rotation construction. The enclosing
TX retains one continuous dedicated-key Fake-FHSS schedule. See
[transmitter cover](transmit-cover.md) for domains, workspace, controlled stopping,
fixed-duration narrowband limitations and the manual qualification checkpoint.

### Hop power transitions (local manual candidate)

The current production path uses **smooth envelope V1**, replacing the costly
per-source-phase pulse-mask template banks described in the historical diagnostic
section below. Fake still changes amplitude and the illustrated tuning only;
physical audio frequency and radio tuning remain fixed. The independently keyed
FHSS schedule, private candidates, Data/DSSS domains and payload bits are unchanged.

After the ordinary full-DSSS finite shaper and limiter, amplitude follows the C2
polynomial `g(t)=t^3*(10-15t+6t^2)` on each transition. Its first two derivatives
vanish at both ends. Ramp duration is `2/max(200, fine_bandwidth_Hz)` seconds,
rounded UP to source samples. The accepted 200 Hz is an engineering spectral
allowance, not a brick-wall mask or a compliance claim. The illustrated profile
spacing includes an additional 200 Hz full-band margin. Narrow waveforms therefore
use about 10 ms ramps instead of seconds of original pulse support. No filtered
PCM is spliced, no oscillator phase resets, and no whole-symbol PCM buffer is
introduced. Native playback applies the same envelope after resampling so FIR
interpolation cannot refill the quiet interval; this slightly deepens the ramps
and adds a small power cost. Its source coordinate follows the same timing map.

For TX boundary N, endpoint errors ±Gtx and ±Grx, total region R and lag [0,A],
let B=Gtx+Grx+R/2. Quiet is `[N-A-B,N+B]`, plus a ramp on either side. Default is
110 ms quiet (±10 ms each endpoint and 0..70 ms audio). GPS1ms/Region1ms/Audio10ms
is 15 ms. Fixed offset O delays the nominal receive-hop schedule without widening
TX silence. All selected allowances are retained. If full remote uncertainty
occupies the dwell, Fake uses a LOCAL guard with B=Gtx and explicitly reports
**remote overlap not guaranteed**. The worst real single-channel overlap can be
zero in this case; it is not an RF-hardware implementation or a timing guarantee.
There is never an ungated production fallback. Impossible local guards block TX.

Short symbols are deferred into complete full-gain scoring slots when they fit.
Every sample of their scored interval has g=1, irrespective of private amplitude;
outside-symbol pulse tails may be attenuated. Original pulse support is still
reserved for the legacy coefficient-omission diagnostic. Deferral changes airtime
and actual UTC addresses, never bit order, duplicates or framing. If no complete
slot fits, a short continuous interval must retain at least half its flat-power
integral in the worst hop phase or TX is blocked. Long symbols span hops normally.

For a stationary envelope, `mean(g^2)=(D-W-2r+2r*181/462)/D`, where W is quiet
width and r is one ramp. `minimum_flat_power_fraction(S)` integrates this envelope
at the worst phase of ANY length-S interval, including fractional boundaries.
The GUI reports that explicitly as a **flat-power** bound. It is not a universal
bound on arbitrary keyed amplitude concentration: without more assumptions that
worst-case bound is 100% loss. Actual finite-key weighted-energy scans and
sensitivity measurements are recorded separately. No lost power is silently
restored, and no observer channel-count multiplier is applied.

RX retains the original full-energy I/Q templates, covariance, physical sample
counts and noise energy. It treats g as amplitude fading, instead of making a new
bank for each gate phase. The coherent unestablished-chain guard retains the
chip-subspace clamp from the pulse-gap investigation. This is an APPROXIMATE
receiver with a measured sensitivity cost, not exact mask matching. Same-template
noise-only statistics and thresholds remain; structured cover is separately tested.
Neither hop gaps nor EOF/drain manufacture observed absence. Long symbols must
still be fully observed; deferred schedules score six seconds of failed eligible
symbols. Accepted bits remain visible on the next poll.

Exact private-template cache identity stays key/epoch/bit/address/clock/frequency
and arithmetic specific; smooth gates do not enter those ungated templates.
Actual-source estimator caches additionally include all envelope parameters,
calendar origin and local/full guard policy. Changes invalidate the appropriate
cache. Qualified UTC filtering intersects complete physical phase cells sharing
each cipher address, rather than only their gcd representative points. These
cells share exact templates; they do not create a per-sample template bank.

This is a local manual-test checkpoint, not full sensitivity, security, hardware
or platform qualification. See the latest [validation record](validation.md).

### Diagnostic pulse-omission path (preceding candidate)

The following records the retained `resolve_fhss_pulse_power` diagnostic and its
prior measurements. It is no longer the automatically selected production path;
its stricter finite-support limits and multiplied phase bank do not describe
smooth envelope V1. Its failure fixtures and assertions remain intact.


Production Fake modes now apply audio power transitions at the SAME dedicated
keyed schedule boundaries. They still do not tune physical RF. The low-level
`FhssPlan::power_ramps=false` diagnostic preserves the original ungated PCM;
Production requires a resolved mandatory ramp plan; impossible geometry blocks
message and tuning-noise transmission. The diagnostic ungated path remains
low-level only. Matched gaps and the deferred calendar reserve EVERY UTC
boundary, including adjacent equal channel selections. No cryptographic address is rewound or reused.
Deferral selects each bit's address from its actual scheduled UTC start.

For nominal TX boundary N, per-station clock error ±G, TX audio lag [0,A],
propagation offset O and total region R, the receiving RF schedule follows the
same keyed hop at **N+O**. The conservative source quiet interval is
**[N−A−2G−R/2, N+2G+R/2]**. Production resolves both endpoint clocks from the
selected accuracy, or ±10 ms each at Default; Audio error is always the selected
causal lag (Default 0..70 ms). A custom clock policy contributes its full region
and offset. Default has no modeled propagation region/offset; its six-second
message-epoch bank is not hop-calendar uncertainty. Whenever either hopping mode
is enabled the receiver assumes the shared GPS calendar with these same endpoint
bounds. Dated capture intersects each message epoch and source phase with that
calendar instead of translating it independently through every payload-start
alternative. Actual provider uncertainty remains in the joint bound, even when
Audio error is zero; this is an explicit timing model, not hardware certification.
Missing dates retain the broad fallback. See [clock synchronization](clock-sync.md).

Default therefore requests a110 ms source quiet interval (90 ms before N through
20 ms after), before pulse support. GPS1ms/Region1ms/Audio50ms requests55ms
(52.5ms before through2.5ms after). Offset does not widen that interval: it shifts
the receiver's nominal hop timeline, not TX hop order, crypto epoch or emitted
carrier. The arrival preview uses the original keyed ordinal at elapsed−O and
preserves fractional offsets. Before O it reports no nominal arrival yet; this
NEVER discards receive samples or changes acquisition/absence semantics.

Capture audio lag occurs after RF channel selection and is not added as another
transmit lag. A future tuner adapter must account for radio-control latency and
output-filter/device uncertainty. None is established by the Fake preview.
With Region400ms and Dwell400ms, the guard necessarily cannot fit, even before
clock/audio/pulse margins; transmission is explicitly blocked. A large fixed
EME offset alone (for example2.564s) does not create seconds of silence. TX
Carrier/Shift continue to show the transmitter; the FHSS label separately shows
the delayed nominal arrival hop and Shift during generation.

Let H be the existing finite RRC pulse half-support (eight fine chips). Suppress
only coefficients whose pulse support could touch that quiet interval; retain
all other coefficients and run the ordinary final shaper/limiter. Each transition
adds at most 2H outside the interval. There is no post-filter PCM splice or fixed
10 ms fade. Pulse overlap, full DSSS Interleave, carrier continuity and shared
message/noise/cover construction remain intact. Fractional dwell boundaries are
resolved in original sample coordinates. No whole-symbol PCM buffer is added.
The original punctured-symbol path requires at least 75% unaffected ordinary statistical support
in EVERY symbol phase, counting quiet gaps and transitions conservatively. For
symbol length S, dwell D and affected gap W, the maximum exclusion is
`floor(S/D)*W + min(W, S modulo D)`. W must also fit inside D. This is an
engineering admission bound, not a sensitivity certificate. A wider tested
DSSS10 geometry lost a clean final bit; its failing evidence is retained.

Unsupported geometries show **TX blocked**. There is no production ungated
fallback. `transfer` rejects unresolved low-level plans; tuning noise resolves
its immutable plan before generating fresh private coefficients. Neither path
shortens the final shaper to force an impossible dwell fit.

Production now includes each UTC pulse omission in the receiver's private
zero/one templates. The old 25% exclusion rule is only the first selection test:
if it fails, try whole-symbol deferral below; if a complete symbol cannot fit,
use **matched partial-symbol gaps** when the finite support leaves a nonzero
statistical-cover interval in every dwell and at least one surviving coefficient
in every symbol phase. In samples, with coefficient radius Q=q+H and fine chip C,
require `D>2*(q+2H)`, `D-2Q>C`, and `S>2Q+2C`. The latter two bounds apply to the
partial-gap fallback. Minutes-long symbols can span many hops; their duration
alone no longer disqualifies them. Tiny occupied bandwidth can still make H too
large for a 400ms dwell. A different dwell or wider waveform is then required;
waiting cannot fix this geometry.

The gate is applied to pre-permutation fine-chip centers, including a shortened
last chip. Original independent private coefficients, DSSS permutation/rotations
and stream addresses are preserved. The receiver retains both quadratures and
recomputes template norms, overlap terms and noise covariance from these gated
coefficients. Omitted pulses contribute zero template rank, but their physical
PCM noise energy and sample counts remain. The structured-code guard permits an
exact zero-dimensional cell; a nonzero near-singular cell retains its conservative
fallback. No thresholds, trials or required observations are removed.

A retained nine-hypothesis fractional/clock/frequency fixture exposed a weak
wrong-key chain in the new gapped cover: five individually unconfirmed decisions
accumulated the FFT receiver's raw PCM-count scores. The matched-gap coherent FFT
branch now uses the compact receiver's chip-subspace score to confirm an
unestablished chain, reusing its existing I/Q fits and actual projected rank.
Raw ranking, standalone thresholds, established-stream continuation and next-poll
publication remain unchanged. This rejects the retained fixture; it is not a
universal structured-interference bound. Drift/differential branches do not have
this coherent independent-code calculation and remain outside that validation.

UTC gate masks distinguish phases that used to share a cipher epoch/ordinal.
The admitted canonical grid is therefore every integer source sample, with
fractional timing cells and clock-rate alternatives retained separately. Qualified
arrival priors may prune this grid. Default broad searches can become materially
more expensive; cipher-address equivalence is no longer a valid reason to merge
their masks. Groups are enumerated without a phase table, compact fits remain
workspace charged. Compact affine continuation caches its group count only when
it fits the compact field; larger banks bypass that shortcut and score every
group using the full-width loop count.
The packed correlator's three-group ABI is bypassed for matched gaps; scalar,
pulse-statistic and FFT paths retain coverage. The planner counts the expanded
phase bank but uses preceding template-construction calibration: extra UTC-gate
work is not yet separately calibrated. Its matched-gap CPU total is an engineering
estimate. Compact rolling/resident-bank and additional-track limits still apply. Source/build measurements and
remaining performance limits are recorded at the manual checkpoint.

Exact cache identity includes epoch, private symbol/bit, source phase, clock and
frequency geometry and the immutable hop guard. Changing phase invalidates shaped
coefficient caches; FFT configuration identity includes guard parameters. Absolute
timing and epoch arithmetic remain high precision. Supported receiver-only reuse
rules otherwise remain unchanged. Nonzero diagnostic `first_chip` is rejected for
matched-gap transmission unless the caller rebases its epoch/phase; silently
applying a second, shifted gate would be incorrect.

Gaps reduce emitted energy and interrupt echo masking. The tail retains at least
three useful seconds OUTSIDE gaps/transitions, not three uninterrupted seconds.
No guarantee is made for echoes arriving in a gap. The planner reports the
matched gate-phase geometry and work. The bounded Interleave actual-source
reference includes its configured pulse gaps and actual deferred payload origin;
other probability branches remain diagnostic references. Source-statistics cache
identity includes all guard parameters and payload origin. These conditional
values do not qualify the distribution across hop alignments, emitted power or
full acquisition sensitivity. No observer channel-count gain or automatic restoration
of lost power is applied. Different waveforms require matched channel/noise and
explicit equal-energy versus unchanged-hardware-gain comparisons.

### Deferred whole symbols (local manual candidate)

Production tries a shared TX/RX **whole-symbol calendar** for symbols shorter
than a dwell. Smooth mode keeps each complete scoring interval outside the quiet
interval and ramps; the legacy diagnostic also reserves finite pulse support.
It adds waiting airtime, not payload bits, erasures, markers or altered thresholds.
If no complete slot fits, the smooth-path usable-power screen above applies.
Deferring a symbol cannot make an empty safe dwell longer.

For the current 0.4 s profiles, UTC hop h is exactly `2*h/5` seconds. In original
sample coordinates let S be a symbol and H the pulse half-support. With
`q=ceil(Fs*(2G+R/2+A/2))` and `a=Fs*A/2`, reserve
`L=ceil(q-a)+J` after each hop and `U=ceil(q+a)+J` before the next,
where J is the rounded smooth-ramp length in production, or H for the legacy
coefficient-omission diagnostic.
The minimum dwell capacity over all five fractional-sample residues determines
`M=floor((min(floor(N[h+1])-ceil(N[h]))-L-U)/S)` slots. Slot j starts at
`ceil(N[floor(j/M)])+L+(j mod M)*S`. The first slot at or after the requested
source epoch/phase is used. Integer quotient/remainder calculations support
constant-time seeking and midstream entry; elapsed hops and PCM symbols are not
buffered. Checked exhaustion raises an error instead of wrapping counters.

All peers must agree on sample geometry and the immutable clock/audio/region
allowances; they now affect the deferred waveform calendar. Runtime audio
uncertainty still controls qualification of the arrival search window, not a
silent change of that calendar. Fixed propagation offset moves the receiving
hop preview by O and does not alter transmitted slots. The existing continuous
UTC controller and sample-clock hypotheses remain in force.

The unchanged Data/private-pattern address format uses each slot's actual UTC
second and `floor(sample_in_second/S)`. Starts are at least S apart, so those
addresses do not collide. The new UTC hop schedule uses dedicated FHSS domain
`FhssUtcV3` (`fh-utc03`), epoch zero, and successive disjoint 1024-byte cycle
regions; it is independent of transmission start. Original `FhssCycleV2` remains
for the original path. Waiting cover uses separate child-secret context
`CoverWaitV1` (`cv-wai01`), full virtual DSSS symbols and the common finite shaper.
Its stream origin never resets at waits or output blocks. Cover does not use
payload candidates. Key, role, epoch, phase and geometry bound all private caches.

The calendar-aware FFT receiver retains all frequency/clock lanes and initial
canonical slot alternatives. Tracks advance to the next eligible slot. Deferred
holes neither become missing bits nor count toward the six seconds of absence:
completion still needs `ceil(6/(S/Fs))` fully scored failed eligible symbols.
Capture tails, airtime, initial/rolling acquisition lifetime and work-model
identity include the calendar. Each accepted bit is published at the next poll.
The compact low-memory correlator does **not** support this calendar; insufficient
FFT workspace produces an explicit error, not a silently contiguous search.

Receiver and transmitter schedule helpers cache validated calendar geometry;
private-pattern address-cache misses still reconstruct it. Deferred phase groups
have an exact constant-time split at the next canonical slot. Eligible
FFT receivers retain up to 8 MiB of exact canonical template transforms, energies
and covariance terms in spare workspace. The key is canonical slot identity
under the immutable key/epoch, frequency/clock bank, arithmetic and geometry.
Phase aliases share entries; indices alone do not identify entries. Arithmetic
fallback and workspace pressure invalidate/evict them. If a cache replaced
streamed construction, eviction restores streaming and discards incompatible
scratch; it cannot allocate unreserved per-lane rows. Native FP32 selections
retain their existing algorithm/precision choice. Native and parallel broad
FFT batches can additionally share scores for duplicate canonical jobs within
the same immutable PCM batch. Every original phase/frequency job is still
collected in its original order with unchanged trial charges and thresholds.
Restricted arrival ranges keep separate score execution. Score reuse never crosses PCM
batches, keys or frequency/clock lanes. The parallel alias map and native score
cache each also have an 8 MiB cap; combined capacities must fit the same
workspace, including the exact template cache and original scratch. The internal
`PatternSearch::reuse_deferred_templates=false` diagnostic disables the new exact
reuse; paired tests compare every next-poll event, identity and trial count.
The planner includes calendar slots, waits and rolling lifetime but currently
prices work conservatively before these optional cache savings; it labels that
limitation instead of claiming newly calibrated CPU accuracy.

Examples at target40, DSSS10, Default clocks/audio:

| Rate / baseband carrier | Symbol | Selected behavior |
| --- | ---: | --- |
| 1200 Hz / 9000 Hz | 106.667 ms | Two complete slots per400ms:5bit/s steady state, plus settling/suppression |
| 360 Hz / 1500 Hz | 355.556 ms | No full slot fits; smooth partial-symbol ramps pass the worst flat-power screen and retain the ordinary receiver |

A total Region400ms cannot guarantee remote overlap within a400ms dwell even
with zero symbol duration. Production Fake now keeps the local smooth guard and
reports that missing guarantee; it does not claim useful worst-case overlap for
a future single-channel radio. Fake is still not radio control or compliance.

The following measurements describe the earlier finite-pulse calendar candidate,
before smooth ramps restored the shared receiver paths. They are retained as
baseline evidence; current results are in [validation](validation.md).

Three isolated paired synthetic-key runs per factor compared the frozen preceding
candidate with this calendar, at unchanged Fs and eight identical source bits.
Waveforms differ because slots, source addresses and cover durations differ.
This is complete single-bank reception at zero frequency error, with all bits
and scored physical completion checked, not a full Live-bank sensitivity result.

| DSSS / Fs | Total source+RX CPU before → after | TX / RX CPU after | Burst before → after | TX working memory increase |
| --- | ---: | ---: | ---: | ---: |
| Off /4096 |33.5→61.9ms|48.8/13.1ms|5.531→7.185s|8282bytes|
| 10 /40960 |429.4→690.0ms|420.7/269.2ms|5.503→6.743s|57890bytes|

These first rows fix the initial stream phase. Paired total-CPU ratios were
1.737–1.849 and1.551–1.629 respectively. Wall time closely matched CPU. Peak
process RSS was about12–16MiB (includes fixture PCM).
Physical completion moved8.58→12.50s and8.59→12.44s, as expected from counting
eligible observations rather than quiet holes. Per-bit publication was checked;
FFT acceptance can still occur in batches, as it did in the baseline.

The separate full-phase, single-epoch/key acquisition comparison is essential:

| DSSS | Total CPU before → after | RX CPU before → after | RX retained peak before → after | Paired total ratio range |
| --- | ---: | ---: | ---: | ---: |
| Off |37.6→106.2ms|17.3→56.0ms|480040→530355bytes|2.788–3.162|
| 10 |797.1→1283.8ms|625.4→808.0ms|1059373→2281853bytes|1.517–1.611|

These medians/ranges contain three paired runs, not a confidence interval. This
is added work and airtime relative to the preceding ungated candidate, **not a
receiver speedup**. Deferred acquisition includes all canonical slots within a
UTC second instead of the original four initial contiguous symbols. The first
deferred prototype took595–608ms (Off) and2881–2895ms (10) on the same full-phase
fixtures; bounded exact reuse removed much of that avoidable regression. It does
not remove the remaining slots, wait-cover generation or longer eligible-absence
observation. No full multi-key/rolling Live-bank throughput result is implied.
GUI Simulation checks retain the original five-second graceful-stop deadline;
both FCC and delayed-arrival EU fixtures pass after reuse. Final controller
measurements were4.329s(FCC),3.425s(EU), and5.1ms for second-Cancel abort. The narrow long-list
fixture keeps its explicit second-Cancel immediate abort.

One whole-burst Hann spectral diagnostic per factor gave out-of-band/in-band
power−34.70→−34.38dBc (Off) and−45.08→−45.16dBc (10), outside the nominal finite-RRC
band. Off reached the 0.992 limiter used for that capture; DSSS10's peak was below0.75.
The current sampled source radius is 0.988; these historical spectral measurements
retain their original 0.992 source identity.
Different burst lengths/addresses and only one finite capture prevent a general
spectral-improvement or indistinguishability claim. No downstream gain restoration
was assumed. Full-bank threshold-region sensitivity, BER/false acceptance,
DSSS100/1000 deferred reception, strong echoes during waits, ASRC/tuner/physical
loopback timing and cross-platform coverage remain unqualified. No additional
C/N0 loss or confidence interval has been measured for the new calendar.

### Earlier FHSS power-ramp measurements (preceding candidates)

The preceding local-clock-only diagnostic measurements below remain evidence for
that exact geometry, not a measurement of the enlarged production guard.
Three isolated paired source runs per factor used identical synthetic keys and
unchanged 12 ksample/s input geometry, 0.4 s dwell, 0..50 ms audio lag and
±10 ms clock allowance. The nominal finite-RRC occupied width was 1500 Hz.
This is source construction timing, not full receiver or physical radio timing.

| DSSS | Baseline / new CPU (s) | New / old CPU | Charged memory increase | Payload average-power reduction |
| --- | ---: | ---: | ---: | ---: |
| 1 | 0.0618 / 0.1032 | 1.670× (paired range 1.653–1.700) | 168 B | 1.000 dB |
| 10 | 0.0591 / 0.1057 | 1.788× (paired range 1.765–1.813) | 168 B | 0.953 dB |
| 100 | 0.0694 / 0.1155 | 1.663× (paired range 1.644–1.692) | 168 B | 0.985 dB |
| 1000 | 0.2527 / 0.2730 | 1.080× (paired range 1.072–1.089) | 168 B | 1.001 dB |

The longest measured four-bit waveform was 54.227 s before and 55.195 s
after; the shorter cases added the same 0.968 s of conservative cover reserve.
New source cost was at most 1.3% of one core per second of generated audio in
these examples. Peak process RSS was about 33 MiB under the Python/taskset
harness; that includes its inherited measurement overhead. Source allocation
accounting above is the more useful incremental RAM measure.

In one matched finite capture per factor, shaped out-of-band power relative to
in-band power changed by −0.05 to +0.16 dB. Direct PCM chopping of the same
quiet intervals was 2.9 dB worse for Off and 11.3–13.3 dB worse for DSSS.
No source clipping occurred in these captures. This is a finite-waveform
comparison, not an RF mask certificate or a thermal-noise resemblance claim.

Ramps removed about 1 dB of average signal power at unchanged downstream gain.
Projection onto the original template at equal waveform energy also lost about
1 dB in these captures. That projection is not a detection-threshold experiment:
no additional required C/N0 or full-bank sensitivity interval is established.
Receiver and observer estimates with active ramps remain ungated references;
hardware cannot be assumed to restore average power without peak clipping.

Exact source-PCM quiet intervals precede the nominal tuner boundary as above.
A future physical tuner must include the selected output converter/filter
support, device latency and actual radio settling in its timing contract; the
Fake presentation does not qualify those hardware margins. Qualification after
ASRC, physical loopback, strong multipath and threshold-region sensitivity is
still outstanding.
