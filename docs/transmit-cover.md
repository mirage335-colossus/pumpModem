# Transmit cover and boundary research

This is a retained research note, not an operating or qualification guide. The
[current Robust architecture](robust-current.md) and [abbreviated testing
policy](triad-performance.md) govern new work. The old single-carrier receiver,
planner and standalone campaign tools have been removed; their results do not
qualify the current three-carrier stages.

The retired private-pattern cover engine tried to make payload surroundings
resemble the payload's waveform statistics. It is not retained as a transmitter,
compatibility requirement or reconstruction recipe. Current optional
[cryptographic background noise](triad-background-noise.md) is a separate shared
final-audio component; its added energy must not be described as this old cover
protocol.

## Useful design lessons

Cover, payload, timing and authentication serve different purposes. Dummy-looking
samples do not add accepted message bits, hide all transmission endpoints or
prove authenticity. Independent cryptographic purposes and addresses prevent
reusing payload secrets as a cover stream. Temporary random roots must not enter
saved key settings or diagnostics. Follow the maintained [cryptography](crypto.md)
and [security](security.md) contracts for current handling.

A transition before a finite shaper differs from appending noise after it.
Already emitted pulse contributions and delayed channel tails cannot be undone
when stopping. A smooth amplitude envelope or long tail can reduce a particular
spectral transient while leaving timing, energy or higher-order structure
observable. Clipping and normalization alter those statistics again.

Partitioned generation needs stable addresses and persistent filter state;
changing buffer boundaries must not restart randomness or replay a prefix.
A graceful stop has both source-support and device-buffer latency. Report these
separately from receiver completion, which must come from the actual replacement
protocol. Old silence-based termination rules do not transfer automatically.

Noise-like appearance and successful private correlation are not evidence of
low interception probability. See [energy-detector limits](lpi-estimates.md).
Any future cover research must use the canonical waveform and minimal bounded
observations rather than restoring the old implementation or its full campaigns.
