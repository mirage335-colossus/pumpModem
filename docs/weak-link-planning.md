# Coherence and weak-link planning research

This is a retained research note, not an operating or qualification guide. The
[current Robust architecture](robust-current.md) and [abbreviated testing
policy](triad-performance.md) govern new work. The old single-carrier receiver,
planner and standalone campaign tools have been removed; their results do not
qualify the current three-carrier stages.

Received power and noise spectral density determine C/N0, not decoding success.
For ideal coherent observation duration T, energy/noise ratio is `(C/N0)*T` in
linear units. A real receiver also pays for unknown timing, frequency, clock,
channel response and false-admission control. Longer duration can increase both
energy and nuisance uncertainty. A hypothetical detector is not a compute or
coverage estimate for the current stages.

## Joint phase and frequency loss

For a constant-envelope reference with Wiener phase diffusion sigma in
radians/sqrt(second), residual frequency f, and duration L, mean normalized
correlation energy is:

```text
z = (sigma^2/2 - i*2*pi*f) * L
eta = 2 * Re((z - 1 + exp(-z)) / z^2)
```

The continuous zero-error limit is one. Pure constant frequency error gives
squared-sinc loss. Frequency and diffusion act jointly; multiplying separately
calculated losses is generally wrong. At zero residual frequency, one
uncorrected coherent filter approaches mean energy `4*(C/N0)/sigma^2` as duration
grows. This is a limitation of that filter and model, not an impossibility
result for communication or a current triad threshold.

A diffusion amplitude of 0.5 degrees/sqrt(second) produces 5 degrees RMS change
across 100 seconds under that model. Stable average frequency therefore does
not establish phase coherence over a very long symbol. GPS frequency accuracy,
phase diffusion and the independent audio sample clock are distinct assumptions;
see [oscillator references](oscillator-models.md).

## Short-section and differential strategies

Coherent segments can tolerate phase movement that defeats a single fixed-phase
projection. Combining segment energies discards relative phase information;
a state-space model can retain some of it. Differential products can exploit
nearby phase relationships, but they still need sufficient local integrated
energy and a valid phase model. They cannot restore information lost to arbitrary
within-section variation. A separate unconstrained phase at every chip can erase
the very transitions distinguishing candidate waveforms.

Historical reduced experiments substituted mean phase-loss energy for the
random path energy. Such results omit phase-fade variability and are conditional
approximations, not sampled-channel qualification. Current research should compare
small matched public-input observations, preserve negative outcomes, and report
proposal, channel-prior and admission assumptions. Kernel-cost projections from
[the abbreviated harness](triad-performance.md) do not predict end-to-end success.
