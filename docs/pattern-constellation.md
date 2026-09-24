# Pattern constellation

The **Constellations** tab places phase/amplitude and pattern views side by side.
The pattern view shows hollow ideal symbols, unsnapped matched receiver points,
first-order noise ellipses and an amber connection between the closest templates.
Both axes use the same scale in noise standard deviations, so equal distances
have the same meaning in every direction. Wheel to zoom; double-click to reset.

The modem currently multiplies one shared sign template by each differential
phase/amplitude coefficient. Its full analytic pattern alphabet therefore spans
two real dimensions. Projecting onto that template and its quadrature preserves
every pairwise analytic template distance. Fixed patterns, keyed sign patterns
and tones use this same geometry; a length of 16,384 does not imply a separate
bank of 2^16,384 codewords or extra transmitted bits.

The projection uses the exact quantized symbol duration `T = samples / Fs` and
the selected white-noise model:

```text
Es/N0 dB = C/N0 dB-Hz + 10 log10(T)
sigma² = nominal_signal_power / (Es/N0 linear)
displayed point = differential, gain-normalized observation / sigma
```

This matches the modem's integrated complex AWGN generator. Four times the
integration doubles template separation in these units. Changing the sample
clock without changing actual integration time preserves the distance scale.
Simulation uses its preset's C/N0; other operation uses the configured target.
Neither is presented as an empirical receive-noise estimate or packet error rate.

Each ellipse has radial standard deviation `sigma` and approximate tangential
standard deviation `sigma * sqrt(1 + (symbol_radius/reference_radius)^2)`.
The weakest configured reference ring gives a conservative first-order model
of differential phase uncertainty. The highlighted Euclidean gap conditions on
a fixed phase reference; it is not a complete differential-decoder likelihood.
Phase noise, clock drift and failed fits remain visible deviations. Finite
real-PCM windows also use a measured oscillator Gram matrix, so their exact
quadrature-noise covariance can differ from this analytic white-noise model.

The view shares the existing fresh-symbol batches and replay clock. Provisional
and locked receiver symbols may appear; raw unlocked I/Q never becomes a pattern
point. Actual transmission is labelled TX. Simulations show receiver observations
during their three-second replay, then clear those points and return to awaiting
live acquisition. Empty or very slow symbol intervals create no artificial dots.

The view holds at most 64 ideal centres and 2,048 observations, independently of
pattern length, sample rate or transmission duration. Omitted observations are
counted; plotting does not change decoding, framing, key handling or replay storage.
