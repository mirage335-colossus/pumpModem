# Throughput and processing-cost interpretation

Current Robust geometry and receiver stages are described in [the architecture
guide](robust-current.md). The old single-carrier bit-rate tables, planner and
fixed-laptop CPU estimates are retired. The GUI uses the native C++ receiver;
its real-time capacity and Link Planner receive/CPU forecasts remain
unqualified. Provisional duration and modeled airtime are separate quantities.
[Fast](indexes/fast.md) and [Legacy](legacy-modem.md)
retain their own mode-specific airtime and coding contracts.

## Quantities to keep separate

Payload throughput is accepted application bits divided by elapsed time.
Transmitted coded bits, waveform symbols, fine cells, complex samples and real
audio frames are different units. Coding, framing, pulse tails, Helper lead time,
hop gaps and scheduled waits can reduce payload throughput without changing a
nominal chip rate. Three simultaneous adjacent carriers do not imply three
independent messages or three times the accepted data rate.

For a source, report actual native geometry and complete emitted duration. For
a receiver, distinguish acquisition preparation and first-result latency from
steady tracking work. Average processing time below signal duration does not
prove a bounded backlog, tolerable startup delay or acceptable peak memory.
Peak workspace, resident cache and captured samples are different memory costs.

## Current performance evidence

[The abbreviated harness](triad-performance.md) measures source-bound reference
work, while retained complete-receiver estimates describe the historical
Python-orchestrated runtime. Neither supplies native GUI capacity calibration.
New native timing must cover the actual complete operating path.
Fixed-geometry extrapolation requires recorded shapes,
source/configuration identity, device, precision, cache state, sample budget and
uncertainty. A kernel projection does not include every receiver stage and is
not an end-to-end throughput certificate. Historical hardware timings apply
only to their recorded revisions and regimes. Resolve uncertainty with the
smallest predeclared discriminating sample, never a sustained whole-receiver
run or an old full-duration calibration recipe.
