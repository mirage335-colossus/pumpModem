# Gentle phase correction: original one-tap receiver rerun

The same original native waveform, flat explicit None/Off propagation, fixed
nominal single-tap receiver, paired noise seeds and independent correction
histories were rerun after changing only the controller default UTC phase catch-up
limit to50ppm. Its total rate limit remains1000ppm and the slew limit10ppm/s.
Measured device-rate feedforward is separate and is not capped at50ppm. The
ideal device rates here are exactly nominal; this test does not exercise100ppm
physical-device drift (the native controller tests cover that distinction).

`run.py` writes its protocol before measurements. Source/header/receiver hashes
are in `results.json`. The standalone `warp` was compiled from the parent
`warp.cpp` against the new header. No production source was changed by this
experiment. The earlier stronger-correction and delay-basis campaigns remain
unaltered.

| Helper | Seed | Stable log-e | Corrected log-e | Threshold22 |
|---|---:|---:|---:|---|
|Off|94001|435.581|62.715|Both pass|
|On|94001|435.604|62.658|Both pass|
|On|94002|444.453|37.037|Both pass|
|On|94003(fresh)|409.965|79.602|Both pass|

All eight raw choices are the correct bit0. All eight conditional model gates
pass. The Helper on/off cases for94001 are correlated; there are three unique
seeds, with only94003 new since the original one-tap design. No success-rate
bound or false-admission probability follows from this small experiment. All
operational admissions remain withheld: physical/Helper/ASRC colored-noise
nulls are unqualified.

The old seed94002 miss at6.664 becomes37.037 without changing the receiver,
observation length, noise realization, emitted power or statistical gate.
Peak phase correction is50ppm TX and50.0025ppm RX (the reciprocal capture map).
The110s transport contains659976 output samples, versus660000 uncorrected:
24samples/4ms net waveform advance, compared with107samples/17.83ms before.
The source is padded by2s and the receiver consumes only1s of padding, preserving
complete tails and a common109s observed window without inventing noise samples.

This is **not negligible sensitivity loss**. Corrected infinite-coherence
true-bit evidence remains approximately4.7–10.3 versus411–446 stable; the0.5s
model produces38.8–81.4. The time-varying audio phase and remaining envelope
warp still violate a fixed coherent template. Passing a gate at these energies
establishes only useful bounded short-coherence reception, not unchanged weak
signal margins or arbitrary multi-day remote-clock tracking.

Campaign wall time16.748s. Each receiver consumed1.515–2.294CPU seconds per109s input (excluding generation and conversions). No hardware was used.
