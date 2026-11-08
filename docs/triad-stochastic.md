# Historical stochastic-channel research

This page retains compact findings from the isolated three-carrier stochastic
channel study. It is research context, not a current receiver guide or execution
recipe. See [current Robust status](robust-current.md) for the canonical native
transmitter/channel/Helper path and the disconnected Python receiver stages.

## Technique and scope

The study modeled continuous time-varying dispersive paths with delayed complex
ray gains, declared Doppler structure, selective amplitude modulation, and
independent noise. DATA and Helper traversed the same channel state, including
guards and delayed tails. An experimental receiver compared complete hypotheses
using shared ray states and optional pilot evidence.

This matters because fixed-ray or sinusoidal modulation tests do not establish
reception under renewed random fading, and band-labeled carrier filters do not
represent overlapping carrier spectra. The study also found that Helper
frequency narrowing can remain informative when coherent pilot-to-DATA phase
transfer is unresolved; that is a scoped research result, not a universal
receiver architecture requirement.

## Retained evidence

The source-bound [2026-11-05 stochastic record](validation-data/triad-v3/2026-11-05-stochastic/README.md)
compares static and dynamic likelihoods on declared sampled conditions, including
8 and 0 dB-Hz HF cohorts and selected moving-delay/fast-selective cases. It
records both successes and misses, wrong-bit outcomes, independent null
controls, calibration identities, timing-bank failures, and a later DATA-only
fallback. Known synchronization in some cohorts and finite timing/epoch banks in
others limit interpretation. Replays and pilot ablations share captures and are
not independent samples.

The results do not qualify the configuration cross-product, daily false
admissions, a general −60 dB-Hz receiver, blind multi-day acquisition, a physical
radio link, or an integrated GUI path. Apply the exact source, channel law,
acquisition assumptions, and uncertainty limits in the dated record before
using a number as a historical comparison.
