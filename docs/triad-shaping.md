# Historical triad shaping research

This page summarizes a historical rate-aware shaping and Helper-placement study.
Its planner and receiver are not current Robust implementation or operating
instructions. See [Robust status](robust-current.md) and the shared
[channel-model contract](channel-models.md) for maintained boundaries.

## Technique and findings

The study selected shaped DATA pulses and Helper placement from the emitted
waveform's finite support and spectral envelope. It separated nominal carrier
bandwidth from finite-window sidelobes and FHSS ramp energy. This is a useful
planning method because Helper placement must account for shaped DATA energy;
a center-frequency gap alone is not a complete interference bound.

The source-bound [spacing follow-up](validation-data/triad-v3/2026-11-06-spacing/README.md)
compared short-window power variation at existing 75-Hz spacing and a 90-Hz
candidate. Some 150-ms extrema improved, but the 100-ms screen still failed in
fresh cases, and one configuration worsened. Increasing carrier spacing did not
justify changing the measured default. Very low spacing retains an unresolved
short-window beating limit. The record also identifies finite-pulse and Helper
placement assumptions and distinguishes source-envelope measurements from
receiver sensitivity.

These sampled source windows are correlated measurements, not confidence bounds
for all keys or channels. They do not qualify a radio AGC, physical Helper
interference, frequency estimation, low-rate reception, spectrum compliance, or
LPI. The dated record retains inputs, hashes, and failed controls for any
source-matched historical comparison.
