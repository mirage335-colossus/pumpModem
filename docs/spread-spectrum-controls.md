# DSSS and illustrative FHSS controls

These controls are in the local development candidate. New outer-DSSS reception,
timing integration and native GUI behavior are not fully qualified; they are not
a claim of practical 1000x spreading at constant receiver CPU cost.

## Separate streams and waveform geometry

The DSSS choices are Off, 10x, 100x and 1000x. Off retains the existing waveform.
An enabled factor multiplies each existing private inner chip by fine QPSK chips
from the keyfile's dedicated DSSS stream. The existing independent private zero
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

The unit-magnitude outer chips retain the inner chip's power envelope. At inner
Rate 10 Hz, that envelope can persist for 0.2 seconds. Adding independent random
outer amplitudes does not remove it: conditional mean power remains proportional
to the inner power, and the extra amplitude variation can increase peak clipping.
The 9 October spectrum investigation isolated substantial spectral regrowth in
the existing final limiter, especially in high-amplitude intervals. This is an
unresolved waveform limitation; the current shaping must not be advertised as
a sharp measured emission mask. See the quantitative
[spectrum follow-up](dsss-followup-validation.md#amplitude-envelope-and-limiter-investigation).

Selecting 10x, 100x or 1000x in the GUI sets a useful voice-passband starting
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
The GUI can show a separately labeled matched-template AWGN reference, which
omits the new presence guard and does not predict complete acquisition success.

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
and the [development contract](development.md). Conditional factor10 coherent
threshold curves now estimate0 dB additional loss with a simultaneous95%-coverage
upper bound0.08 dB. Full-bank curves, enhanced branches, hardware/regulatory
measurements and broader platform qualification remain open.
