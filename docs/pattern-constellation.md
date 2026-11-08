# Constellation and waveform diagnostics research

This is a retained research note, not an operating or qualification guide. The
[current Robust architecture](robust-current.md) and [abbreviated testing
policy](triad-performance.md) govern new work. The old single-carrier receiver,
planner and standalone campaign tools have been removed; their results do not
qualify the current three-carrier stages.

Constellation displays help diagnose phase rotation, amplitude variation and
candidate confusion, but only if their observation and normalization are stated.
A plot of source coefficients, a shaped three-carrier composite, raw ADC samples,
a matched projection and a fitted channel residual answer different questions.
The same axis labels do not make those quantities interchangeable.

## Receiver interpretation

Unknown common gain and phase can rotate or scale a constellation without
changing the message. Candidate distance must therefore be assessed after the
receiver's actual nuisance fit. A fit with too many independent degrees of
freedom can absorb differences between bits, especially if phase is free at
every fine cell. Short coherent sections and differential statistics can retain
local relationships, but cannot repair arbitrary within-aperture incoherence.

Display-only phase alignment is not evidence that a receiver acquired that
alignment from public inputs. Hidden transmitted bits, exact simulated timing
and true channel values belong only in explicitly labeled diagnostics. Preserve
wrong candidates, missed detections and residuals when judging a method.

The [current pattern note](triad-patterns.md), [coherence theory](weak-link-planning.md)
and [amplitude research](amplitude-probability.md) retain the relevant source and
statistical distinctions. New comparisons must use the canonical native waveform
and bounded statistical observations, with no resurrected standalone decoder.
