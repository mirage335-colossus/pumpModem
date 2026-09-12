# Pattern / scrambler constellation

The **Modem flow** tab shows a static view of the configured complete pattern
symbols. Each row is one permitted phase/amplitude symbol, shown across its sign
period as complex chips. All configured coefficients are included, up to 64;
long periods have pages so every position remains inspectable. Tone mode has a
constant base sign row; fixed patterns, scrambling or independent DSSS reverse
selected chips.
This view changes with the modem settings and does not display live samples.
The Console retains its live phase/amplitude constellation.

The current modem multiplies one shared sign sequence by each differential APSK
coefficient. Write a complete template as `s[k] = a * p[k]`, with `p[k]` equal to
`+1` or `-1`. For `L` complex chips there are `2L` real coordinates. The valid
alphabet occupies a two-dimensional subspace of that larger space; the receiver
can reject structure outside it as well as distinguish coefficients within it.
A two-dimensional matched I/Q projection preserves the distances between valid
templates but discards off-template evidence. Showing the full chip rows makes
that difference visible. It does not create a separate bank of `2^L` transmitted
codewords or add payload bits.

The displayed alphabet conditions on a common differential phase reference.
An arbitrary shared complex rotation or gain changes every chip together; it
does not turn every sign vector into a permitted template. The unused-vector
example illustrates a sign sequence outside that shared complex line when one
exists. It is a diagnostic example, not a second transmitted alphabet or a
statistical sample of white noise. A single-chip template has no off-template
chip direction. Constant tones can benefit from integration but give no
sign-transition evidence for cyclic chip alignment.

Distances use every position of the full template, including positions outside
the visible page. Each position is weighted by its exact quantized sample
duration. For two coefficients `a` and `b`, the squared full-template distance
is the time integral of `|a*p(t) - b*p(t)|²`, or `T * |a-b|²` for unit sign
sequences. Repeated periods and partial final chips contribute their actual
durations; untransmitted positions have zero weight. The model also converts
this energy distance to quadrature-noise units using the configured C/N0 and
nominal signal power. Longer observation improves separation relative to noise
without changing the underlying normalized coefficient geometry. The display
reports integration gain separately:

```text
chip-to-symbol gain dB = 10 log10(symbol duration / nominal chip duration)
```

The distance map compares every complete legal template on a common scale;
`U` is the displayed unused sequence at symbol zero's coefficient. Dark cells
mean smaller distance. The matched/off-pattern bars additionally show the mean
isotropic-noise result: one fitted complex direction out of `N` independent,
whitened chip observations carries `1/N` of the noise energy on average. This
is an ensemble expectation, not a fabricated received point or lock threshold.

This is a coherent white-noise model. In those assumptions, four times the
integration halves amplitude uncertainty and doubles separation in noise units.
It does not promise that a long observation retains coherence under real clock
drift or phase noise. A different sample clock alone does not provide processing
gain; actual quantized durations determine the calculation.

The cyclic one-chip-shift comparison reports signed, duration-weighted
correlation `rho` and residual energy fraction `1-rho²` after the best complex
amplitude/phase fit to the reference template. This identifies
whether a shifted sequence can masquerade as a gain or phase change. A zero
residual means that particular shift supplies no distinguishing sign evidence.
The comparison is a static property of the selected sign period, not a complete
timing search, false-alarm probability or measured receiver lock result. Fixed
periodic patterns can have ambiguous shifts; a long pattern is not necessarily
a good synchronization code. The separate energy distance for an evidence
example retains equal amplitude while fitting phase, giving `2T * (1-|rho|)`.

Keyed modes use explicitly labelled **public illustrative signs**. The view
never reads or displays the actual epoch-derived private scrambling stream.
The sign period is stored compactly with at most 16,384 positions. Automatic
integration may last much longer than one period, without building an audio
array proportional to airtime. The full alphabet, distances and page contents
therefore remain bounded for hour-long symbols or high internal sample rates.

The production receiver despreads complex chip samples and integrates them
before APSK symbol decisions. It does not first require hard chip decisions or
a visibly clean phase/amplitude cloud. This order permits whole-pattern evidence
to emerge when individual chip samples resemble noise. The current implementation
searches a finite set of timing origins and initial phase/gain hypotheses; it has
no continuous clock or frequency tracking loop. Accelerated simulation assumes
matched chip despreading, so its successful decoding does not demonstrate blind
acquisition below the chip noise floor. Real PCM acquisition is tested separately.

This inspection changes neither the wire format, keyfiles, decoding, nor the
three-second simulation replay. It introduces no runtime dependency.
