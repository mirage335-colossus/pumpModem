# Conditional duration-planning research

Native `triad_duration.hpp` supplies the provisional duration model used by
`robust_profile` and the GUI. Target C/N0, oscillator/channel assumptions and
public waveform geometry determine the modeled bit duration. Planning uses a
fixed public one-bit assumption rather than transmitted draft length. The
former Python `triad_duration_model` is retired. Tests and benchmark orchestration
call the native planner rather than carrying a second numerical implementation.

A duration forecast is a model calculation, not receiver sensitivity,
reliability, real-time capacity or physical-link qualification. Native source
geometry owns exact tile, sample, pulse, clock-address and burst calendars.
[Current integration checks](validation.md#native-codec-and-measurement-migration) are separate
from the dated research below.

Rate is the inner pattern rate. The native source produces inner coefficients
at Rate/6 and requires at least 64 protected coefficients per tile, plus the
shaped waveform edges. With Spreading and FHSS Off, the one-tile minimum at
0.1 Hz is 5,760.4 seconds per DATA bit.
Interleave spreading raises the fine coefficient rate and can reduce the edge
time at a fixed Rate, while the 64-coefficient inner floor remains. Lower
Target C/N0 can add more whole tiles when the duration model needs them, so
changing Target may leave the selected duration unchanged until another tile
is required. The Console time estimate includes two scored quiet slots to
confirm message completion.

The [duration-planner evidence](validation-data/triad-v3/2026-11-06-duration-planner/README.md)
and [long-coordinate forecast checks](validation-data/triad-v3/2026-11-06-long-forecast/README.md)
record the assumptions and limits. The latter checked integer geometry without
creating multi-day waveforms; it did not establish blind multi-day acquisition.

The GUI currently selects the fixed-energy true-atom model. The optional
stochastic calculation uses an equal-mode Gaussian fading surrogate and an
explicit covariance width; GUI HF Worst bounded frequency motion is not that
width. Enabling the stochastic flag alone would leave that width zero and
would not qualify the GUI channel. A lower shared target lengthens the agreed
waveform in whole native tiles and changes the live reference amplitude. Check
ordinary reception and complete workspace admission separately from the
planner's selected duration; neither a longer waveform nor repeated raw zeros
by itself establishes a specified success probability.

## Interpret a tested HF duration

At 180 dB path loss and −164 dBm/Hz input noise density, transmit powers
of 2 mW and 100 mW give nominal received C/N₀ of −12.99 and +4 dB-Hz.
At a given power, lowering the planner Target can buy more tiles and more
transmitted energy. At a fixed 256.8 s and Target +4, changing the simulated
power or fading instead changes the noisy waveform presented to the receiver.
For the same transmitted zero the two passing powers were 15.85 mW on the
stable path and 1 W on the severe GUI path. Their 18 dB difference measures
physical link margin for that trial. [Fixed-airtime power sweep](validation.md#bounded-2-mw-hf-fhss-investigation--18-november-2026).

To bracket airtime, keep the physical power, channel law, seed and bit fixed.
Record the planned pattern, then check both signal presence and separation
between the two bit scores. At 100 mW on the severe GUI path, seed 940601
produced strong presence at 9,501.6 s (Target −20), but the transmitted one
led by 4.061 log units against 5.293 required. At 12,069.6 s (Target −21)
its lead reached 6.353 and it decoded. For seed 940600 and bit zero,
lengthening from 12,069.6 to 17,205.6 s reduced the correct-bit lead from
4.747 to 4.068. An isolated success or the longest observed miss leaves
the reliable operating duration unresolved. [Paired durations and holdouts](../_tests-RobustModem-HF-simulations-2026-11-18.md).

### When a longer pattern still erases

Two diagnostics replayed the same 2 mW severe GUI capture at 5,906.4 s.
Supplying the nearest public bin to the actual direct arrival gave the
signed-lag scorer a correct-bit lead of just 0.031 against 5.293 required.
In a separate diagnostic with timing and oscillator-frequency bins supplied,
correcting the simulated common Doppler phase changed the persistent-gain
scorer's correct-bit lead from −0.553 to +16.714 with less than 8.1×10⁻¹⁶
relative change in sample power. A matched noise-only control stayed below
the DATA gate. Resetting local gain and timing state
every 4 or 16 s, even with that phase correction, yielded leads of −1.100
and +3.180. The measurements show that carrying state helped on this
realization. The shortest sufficient memory and an operational method for
estimating the phase remain open. [Receiver diagnostics](validation.md#channel-history-diagnostics-at-2-mw).

A candidate phase tracker can be tested on the original noisy input with
only the public search bounds, accounting for the uncertainty and cost of
its inferred trajectory. An amplitude-only candidate first needs a statistic
that distinguishes the two keyed bits using their amplitudes after the modeled
fading. For either approach, separate the two bit decisions from noise-only
admission, repeat on fresh realizations and measure the cost of the full
search. These checks distinguish an operational improvement from a gain
available only with simulator-supplied information.

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
