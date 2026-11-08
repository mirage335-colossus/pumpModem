# Narrowband attenuation and duration research

This is a retained research note, not an operating or qualification guide. The
[current Robust architecture](robust-current.md) and [abbreviated testing
policy](triad-performance.md) govern new work. The old single-carrier receiver,
planner and standalone campaign tools have been removed; their results do not
qualify the current three-carrier stages.

The old 1,200 Hz examples combined a particular transmit-power assumption,
noise figure, wire format and hypothetical receiver. Their decoded-bit timing,
CPU/RAM projections and GPU extrapolations were not measurements of the current
three-carrier implementation and have been removed from maintained guidance.

The reusable energy accounting is independent of those old presets. At fixed
received power and noise density, ideal coherent energy grows linearly with
duration. An additional 30 dB attenuation requires 1,000 times the integration
time to restore the same ideal energy; 60 dB requires 1,000,000 times. Those
factors assume the waveform remains coherently usable and ignore acquisition
cost, phase diffusion, sample-clock error, channel changes and detector thresholds.
They do not promise a decode within a day or any other time limit.

Noise integrated over bandwidth B has power N0*B under a flat-noise model;
`SNR_dB = (C/N0)_dBHz - 10*log10(B)`. Changing the audio passband, chip bandwidth
or receiver filter changes which noise is included. A 1,200 Hz label alone does
not specify waveform occupied bandwidth or the effective observation filter.

For the current source, derive emitted duration, padding and energy from native
geometry, then distinguish total acquisition preparation, repeated tracking
work and peak workspace. Average compute below airtime does not prove bounded
startup delay or real-time operation. Use [coherence research](weak-link-planning.md)
and the [abbreviated performance contract](triad-performance.md); never revive the
removed full-duration examples as tests or calibration recipes.
