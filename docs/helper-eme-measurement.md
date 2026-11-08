# Historical Helper frequency measurements for EME budgets

Current implementation and validation rules are in [Robust architecture](robust-current.md)
and [abbreviated testing](triad-performance.md). This note preserves research
ideas and source-bound evidence, not a second implementation or a recipe to
rerun retired campaigns. Historical results do not qualify changed code.

The study compared illustrative 5.8-GHz link budgets using known RF hop commands
and complex sampled input. It was not a physical Moon-bounce link, a hardware
retuner, or an integrated DATA receiver. The [Helper evidence index](validation-data/helper/README.md)
retains the source-bound measurement context.

The important distinction was between bounded-audio frequency observation under
independent hop phases and a separate phase-continuous, non-hopping case. RF
translation error, audio-clock error and pilot measurement noise contribute
differently; a narrow frequency result in one geometry is not a universal
DATA search width. Fresh phase and a dated frequency estimate require separate
error descriptions.

Pilot fraction must be taken from total Helper waveform mean power, not assumed
equal to the transmitter's total power. Antennas, path loss, noise temperature
and oscillator assumptions define a modeled link budget; they do not establish
a real station's sensitivity or availability. Reuse only matching recorded
conditions, and evolve the [canonical integration](helper-integration.md)
through bounded checks rather than restoring this retired receiver.
