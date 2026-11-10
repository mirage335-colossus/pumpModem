# Helper integration in the canonical triad path

The native Helper generator, single-key metadata codec and weak Helper
acquisition share the canonical C++ triad stack linked into the GUI. Helper
power, ratio limit and callsign configure that native source; the complete
receiver supplies noisy Helper proposals to DATA refinement. Helper-only and
noise transmissions use the same source interface. Python references are used
for bounded migration parity, not operating acquisition. Retire superseded
reference code and captures when those numerical questions close.

The older ordinary receiver and standalone Helper tracking/conditioning
implementations remain removed. Current component checks do not qualify every
Helper/DATA configuration or physical channel; see
[native integration validation](validation.md#native-gui-integration).

## Transmitter facts and assumptions

The [Helper waveform note](helper-carrier.md) records the pilot/auxiliary roles,
fixed 38-second lead and full 36-second observation assumption. Frequency and
phase search narrowing must not depend on successful auxiliary metadata decoding.
A valid frequency observation does not automatically establish pilot-to-DATA
phase transfer or uncertainty under a changing selective channel.

## Useful integration lessons

Keep the public arrival, epoch and oscillator priors explicit. A prediction may
choose a local coordinate or proposal neighborhood; it is not a second independent
measurement. Carry the original observation and uncertainty to sparse DATA
refinement. If phase transfer is unresolved, useful frequency assistance may
remain available; see the [stochastic findings](triad-stochastic.md).

Fresh phase belongs to the current hop. Independent hop phases cannot be
extrapolated from the preceding hop. Auxiliary marker timing locates the
transmitted calendar, not an independent hardware retune delay. Historical
marker-boundary correction worsened an early-retune fixture; correct marker
timing also did not cure arbitrary settling pulses or channel-phase changes.

The [Helper research index](validation-data/helper/README.md) and
[continuation lessons](helper-continuation.md) retain successful and failed
clock, phase, transient and timing approaches. Conditional reported intervals,
observed errors and calibrated confidence are different claims. Their old
standalone command paths are not integration requirements.

## Current development route

Evolve the canonical stages described in [current Robust architecture](robust-current.md).
Use bounded deterministic checks and [hierarchical abbreviated measurements](triad-performance.md).
Do not restore a separate Helper receiver or full campaign to reproduce historical
results. Tags and separate binary releases preserve older protocol implementations.
