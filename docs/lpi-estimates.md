# Energy-detector research and interception limits

This is a retained research note, not an operating or qualification guide. The
[current Robust architecture](robust-current.md) and [abbreviated testing
policy](triad-performance.md) govern new work. The old single-carrier receiver,
planner and standalone campaign tools have been removed; their results do not
qualify the current three-carrier stages.

A receiver that knows a private waveform can correlate against it while an
observer can still detect its added energy. Encryption does not remove energy.
Spreading changes power density; long transmissions can become easier to detect
by integration even if individual chips are weak. No observer/receiver duration
ratio establishes a safe message length or an adversary's capability.

## Ideal radiometer comparison

Assume a known band of width B, a known observation window T, stationary exactly
known noise power, and independent Gaussian signal and noise. With signal/noise
power ratio rho, the ideal real bandpass energy statistic has approximately
`2BT` degrees of freedom. Its normalized mean and standard deviation are
`1, 1/sqrt(BT)` without signal and `1+rho, (1+rho)/sqrt(BT)` with signal.
For a 1% false alarm and 90% detection normal approximation:

```text
T90 = ((2.326347874 + (1+rho)*1.281551566) / rho)^2 / B
```

This is a model equation, not a current planner estimate. The removed estimator
normalized the intended receiver to an assumed 18 dB one-symbol energy; that
number was never a measured threshold of the current triad receiver.

The approximation depends on time-bandwidth product and the signal-present
variance. Deterministic fixed-energy signals, correlated amplitude envelopes,
finite shaping, receiver filtering, uncertain noise and repeated searched
windows change the comparison. Nominal bandwidth does not establish independent
power samples. An observer might exploit public patterns, envelope features,
cyclostationarity, timing, direction or other side information. Noise uncertainty
can also limit an ideal radiometer; it is not a security guarantee.

## Interpretation for current work

Keep power, noise density, observation bandwidth and actual emitted duration
separate. Count burst gaps and hop transitions by their real energy and time,
not an invented independent spreading multiplier. Compare matched assumptions
and record both receiver search/admission costs and observer alternatives.
The [background-noise component](triad-background-noise.md) adds energy and must
be included in an observer model. Neither cryptographic appearance nor a
successful receiver decode proves low probability of intercept.
