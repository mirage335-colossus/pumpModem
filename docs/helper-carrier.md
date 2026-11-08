# Helper Carrier waveform and research assumptions

The retained Helper Generator and single-key metadata codec compose with the
canonical triad source before shared propagation. Older ordinary-receiver and
standalone Helper receiver implementations are retired. See [current integration](helper-integration.md)
and [Robust architecture](robust-current.md) for the available backend boundary.

## Development priority

Helper frequency and phase observations can narrow DATA search. Successful
auxiliary APSK decoding is not a prerequisite for that assistance. Frequency
narrowing, phase transfer, identifier recovery and DATA admission are distinct
outcomes; do not present one as evidence for all of them.

## Lead-in operating requirement

The retained composition uses a 38-second lead: 95 dwells of 400 ms, including
90 dwells for the 36-second observation and five for initial detection. Plan
around a listener present for this lead. These durations are source geometry,
not a guarantee that an individual weak auxiliary marker is detectable. The
historical evidence does not qualify a listener joining an arbitrary ongoing
transmission.

## Quiet-spectrum assumption

The strongest tone in the oscillator-model-derived expected high-pilot range
may be treated as the pilot under the stated quiet-spectrum assumption. Do not
add elaborate competing-tone identification solely for interference excluded by
that assumption. Actual interference or a wider operating claim would need an
explicit model and separate evidence.

## Dwell waveform and metadata

High and low pilot references carry frequency/phase information; the auxiliary
waveform carries identifier/timing information. Placement and mean-power
normalization must use the complete composed waveform. Total Helper power is
not the power of the high pilot alone. The metadata codec accepts one selected
key; a key file may contain selectable entries without enabling simultaneous
decryption. Encrypted metadata is not, by itself, an authentication claim.

Keep the DATA/Helper power ratio, headroom, shaping, envelopes and delayed tails
in the shared channel model. Applying a transmitter guard again to channel
output would erase echoes. Power and RF examples in the retained studies are
modeled configurations, not device or regulatory qualification.

## Evidence and remaining limits

[Helper measurements](validation-data/helper/README.md), [frequency refinement](helper-frequency-refinement.md),
[continuation research](helper-continuation.md), and [triad Helper findings](triad-helper.md)
preserve technique choices and their failures. Arbitrary differential channel
phase can imitate clock evolution. A good frequency estimate does not establish
coherent DATA phase, and a short successful cable case does not establish a
lifetime physical false-admission rate. Use the [abbreviated policy](triad-performance.md)
for new intensive work; old standalone commands and calibration campaigns are removed.
