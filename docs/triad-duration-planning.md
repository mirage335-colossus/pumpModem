# Conditional duration-planning research

The reusable `triad_duration_model` is exposed through `tools/triad_backend.py`.
The former standalone planner/export driver is removed. A duration forecast is
a model calculation, not receiver sensitivity, reliability or physical-link
qualification. Native source geometry still owns exact tile, sample, pulse,
clock-address and burst calendars.

The [duration-planner evidence](validation-data/triad-v3/2026-11-06-duration-planner/README.md)
and [long-coordinate forecast checks](validation-data/triad-v3/2026-11-06-long-forecast/README.md)
record the assumptions and limits. The latter checked integer geometry without
creating multi-day waveforms; it did not establish blind multi-day acquisition.

## Useful accounting distinctions

Count each declared epoch, bit, channel-prior and operational attempt. Helper
narrowing does not erase prior mass or the cost of choosing a proposal. Allocate
miss probability separately to true-candidate evidence and other failures such
as acquisition, Helper availability and wrong-bit decisions.

Elapsed DATA calendar and active energy duration differ when burst gaps exist.
Finite covariance modes, Doppler width, ray-center separation and phase diffusion
represent different effects. Static Rayleigh amplitude fading can cause deep
outages; it is not equivalent to a fixed-energy unknown-phase model.

Longer coherent integration requires finer frequency/clock resolution. As a
reference, a rectangular coherent span T with spacing 1/(4T) has about 0.2244 dB
worst half-bin loss; this is not a prescription for the renewing-block receiver,
whose shorter coherent windows trade search cost against sensitivity. A forecast
must expose those assumptions and unsupported regions.

For current testing follow [the minimal hierarchy](triad-performance.md), using
cheap geometry and model checks before any bounded statistical estimate. Do
not generate a full predicted-duration observation to validate a planning number.
