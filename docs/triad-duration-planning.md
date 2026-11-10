# Conditional duration-planning research

Native `triad_duration.hpp` supplies the provisional duration model used by
`robust_profile` and the GUI. Target C/N0, oscillator/channel assumptions and
public waveform geometry determine the modeled bit duration. Planning uses a
fixed public one-bit assumption rather than transmitted draft length. The
Python `triad_duration_model` remains a numerical comparison reference while
migration parity and historical forecasts require it; it is not in the operating
path. Remove superseded reference code and captures when those questions close.

A duration forecast is a model calculation, not receiver sensitivity,
reliability, real-time capacity or physical-link qualification. Native source
geometry owns exact tile, sample, pulse, clock-address and burst calendars.
[Current integration checks](validation.md#native-gui-integration) are separate
from the dated research below.

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
