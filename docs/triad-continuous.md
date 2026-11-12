# Bounded source and continuous inference components

The operating interfaces are native C++: `triad_stream.hpp` supplies bounded
source and public slot geometry; `triad_receiver.hpp` scores complete slots;
`triad_stream_receiver.hpp` owns continuous ingress and ordered completion.
The GUI links these components through `live::Session`, sharing immutable input
between in-process scorer owners. Python tools invoke native probes and analyze
returned statistics; they do not implement receiver DSP. See
[current architecture](robust-current.md).

## What streams

The native pull service generates bounded waveform or basis chunks from checked
integer coordinates. Emitted DATA/Helper is distinct from a channel-simulated
observation; do not send simulated receiver noise as the intended DAC waveform.
Current shaping/channel state and all delayed tails remain shared and continuous.
Whole-source exports and full-duration normalization/calibration are retired.

Native `FineSession` retains Gaussian and timing state across bounded input
continuations. Native `CoarseSession` exposes preparation, bounded search
progress and completion from the same production coarse path, and can acquire
its frequency intervals while it prepares. Probe prefixes
are diagnostics, not additional operational admission attempts. Observation
endpoints, priors and permitted attempts remain explicit; incomplete input
cannot be padded with fictitious observed zeros. Continuation does not reset
the keyed source or prior evidence.

Long text similarly streams one fixed marker/codeword interval at a time,
retaining bounded source bytes and diagnostic prefixes. Message completion
remains two fully scored quiet slots, independent of codec length.

## Resource and timing boundaries

Duration support is integer geometry, not proof of long physical reception.
Use exact work/memory preflight and tiny large-coordinate prefix checks instead
of constructing long sample arrays. CPU allocation respects affinity/quotas;
actual numerical thread pools and cold preparation remain part of measured cost.
A larger thread count does not guarantee faster small-matrix updates.

The native Fejer-tapered Hilbert frontend converts real PCM to analytic I/Q,
propagates validity across the full filter support and labels centered sample
coordinates. Startup transients are excluded; EOF never supplies invented
filter-tail observations. This replaces the unused Python Kaiser experiment,
not a numerically equivalent implementation of it. Native audio retains
byte/format/limit/EOF checks and
[continuous clock discipline](triad-clock-follow.md). Known ADC mapping does not
whiten interpolation noise or identify arbitrary remote motion. Adaptive
tracking remains distinct from physical-noise qualification.

The native complete receiver separately maintains bounded ingress, independent
slot ownership, ordered commit, quiet completion and continued listening. Slot
roots and Helper, coarse and fine leaves share one lazy bounded executor; each owner keeps
its admitted scratch bound, and started idle stacks remain charged until close.
Admission quotes the largest ready prefix at full-`W` scratch per owner,
preferring balanced scratch only when its complete bound admits a strictly wider
ready prefix. Ready leaves prioritize the oldest eligible public
slot, and queued roots use otherwise unneeded lanes.
Ingress backpressure waits for one completion before retry; EOF drains all
accepted work. A standalone slot, coarse session or fine session owns a
private instance of that executor, so its timings alone do not qualify shared-pool throughput. Its
current scope and checks are in [validation](validation.md#native-codec-and-measurement-migration).

The historical [continuous record](validation-data/triad-v3/2026-11-06-continuous/README.md)
and [CPU follow-up](validation-data/triad-v3/2026-11-06-cpu-default/README.md)
retain dated outcomes. They do not mandate full/sustained runs; use
[abbreviated testing](triad-performance.md) for new intensive work.
