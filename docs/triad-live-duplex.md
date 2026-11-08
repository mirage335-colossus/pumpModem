# Historical cable transfer and receiver-margin lessons

Current implementation and validation rules are in [Robust architecture](robust-current.md)
and [abbreviated testing](triad-performance.md). This note preserves research
ideas and source-bound evidence, not a second implementation or a recipe to
rerun retired campaigns. Historical results do not qualify changed code.

The retired cable suite measured a combined DAC/cable/input/ADC path, fitted a
model from dedicated probes, then tested held-out three-carrier waveforms. Its
standalone duplex/modem/calibration drivers are removed. The current native
audio bridge is a transport component, not another modem implementation.

Keep a model-fit result separate from held-out prediction and actual receiver
admission. Gain, phase, passband attenuation, resampling, clipping and channel
selection can each invalidate reuse. A positive hardware result at one geometry
is not a general sensitivity margin or a calibration of every device.

A quiet PSD is measured after ADC quantization. A stationary colored-Gaussian
surrogate using that PSD should not silently quantize the same noise again.
Stationarity, Gaussianity below measured spectral resolution and long-term
behavior remain assumptions. No absolute voltage, receiver noise figure or ADC
effective-bit count follows from those samples alone.

The [November 4 cable record](validation-data/triad-v3/2026-11-04-live-duplex/README.md),
[margin bounds](validation-data/triad-v3/2026-11-04-live-margin-bounds/README.md),
and [reception evidence](validation-data/triad-v3/2026-11-04-live-reception/README.md)
retain matched configurations, failures and limits. Reuse source-matched captures
for cheap discriminating checks; do not recreate the old whole-capture suite or
its 50-minute campaign budget as a current test requirement.
