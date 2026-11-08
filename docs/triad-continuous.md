# Bounded source and continuous inference components

The canonical Python integration API is `tools/triad_backend.py`. It exposes
the native source/basis service, weak acquisition, persistent tracking and
shared frontend/timing/planning kernels. These are building blocks, not a
standalone modem CLI or a GUI-connected message receiver. See
[current architecture](robust-current.md).

## What streams

The native pull service generates bounded waveform or basis chunks from checked
integer coordinates. Emitted DATA/Helper is distinct from a channel-simulated
observation; do not send simulated receiver noise as the intended DAC waveform.
Current shaping/channel state and all delayed tails remain shared and continuous.
Whole-source exports and full-duration normalization/calibration are retired.

`IncrementalReceiver` retains bounded input and Gaussian likelihood state for
a declared candidate bank. Its accumulated score alone does not admit a bit.
Observation endpoint, priors and permitted attempts remain explicit; incomplete
input cannot be padded with fictitious observed zeros. Persistent timing lanes
extend this state without resetting the keyed source or prior evidence.

## Resource and timing boundaries

Duration support is integer geometry, not proof of long physical reception.
Use exact work/memory preflight and tiny large-coordinate prefix checks instead
of constructing long sample arrays. CPU allocation respects affinity/quotas;
actual numerical thread pools and cold preparation remain part of measured cost.
A larger thread count does not guarantee faster small-matrix updates.

The native full-audio bridge retains byte/format/limit/EOF checks and
[continuous clock discipline](triad-clock-follow.md). Known ADC mapping does not
whiten interpolation noise or identify arbitrary remote motion. Adaptive
tracking remains distinct from physical-noise qualification.

The [continuous record](validation-data/triad-v3/2026-11-06-continuous/README.md)
and [CPU follow-up](validation-data/triad-v3/2026-11-06-cpu-default/README.md)
retain dated outcomes. They do not mandate full/sustained runs; use
[abbreviated testing](triad-performance.md) for new intensive work.
