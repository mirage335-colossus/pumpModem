# Full-audio clock discipline and tracking lessons

The old source-only `bounded-data` controller and its standalone receiver modes
are removed. The current native Robust stack is integrated with GUI simulation
and live audio; these historical clock-control experiments do not qualify that
integrated path. See [current architecture](robust-current.md) and
[current validation](validation.md).

## Mechanism and limits

System time and ADC/DAC time are different clocks. A wall-clock step does not
prove the analog waveform changed. Dated device frames support a bounded rate
controller that disciplines complete PCM while retaining its initial association,
logical source sequence and keyed epoch. This is distinct from strict affine
clock following and does not identify an independent remote transmitter.

Steering the whole passband keeps DATA, Helper and dwell/burst envelopes together.
Their physical durations change slightly with rate while logical sequence stays
intact. A finite phase catch-up cap cannot absorb arbitrary corrections arriving
faster than its reserve. Controller stability is not receiver sensitivity or
physical-noise qualification.

## Local mapping versus remote motion

Actual native ADC coordinates accompany resampled capture. Mixing and template
evaluation at those coordinates avoid artificial local steering phase error.
The mapping does not invert the interpolation filter or whiten its colored noise.

Unknown remote delay/rate uses bounded sparse timing state and persistent complex
channel likelihood. No private transmitter correction trace selects the path.
The nominal/adaptive mixture preserves accumulated evidence instead of restarting
at each segment. Limits on the public dynamics and retained state remain explicit.

The [continuous-clock record](validation-data/triad-v3/2026-11-06-continuous-clock/README.md),
[clock-tracking evidence](validation-data/triad-v3/2026-11-06-clock-tracking/README.md),
and [gentle-tracking study](validation-data/triad-v3/2026-11-06-gentle-tracking/README.md)
retain dated boundaries and failures. Long logical-coordinate checks are cheap;
new intensive work follows [abbreviated testing](triad-performance.md), not full
multi-day replay, uptime or clock-correction campaigns.
