# Selective-stage sampling refinement for the long-rate cases

The historical report said the selected long-tier sampling rates passed this
**stage-specific** refinement. It tested the actual
`DispersiveSelectivePerturbation` operator against its ideal physical-delay
response; it did not establish whole-waveform, oscillator, compression or
receiver convergence. No runtime source was changed.

The historical report covered 48 profiles at GUI rates 0.001 and
0.01 Hz with Added Symbols ×1, ray separations 0.001 and 0.05 Hz, Each65,
common input offsets ±0.00005 Hz and both ray-center offsets (0 and the selected
separation). The base sample rates are 0.25 Hz for 0.001-Hz separation and
0.5 Hz for 0.05-Hz separation; each is repeated at 2× and 4×. This is one fixed
modulation seed, not 48 independent channel trials.

For each profile, three isolated tone probes measured the complex response at
the carrier frequencies. A fourth instance processed their sum with the same
uninterrupted FIR state. Its output was compared with the sum of isolated
outputs and with the ideal delayed-branch construction. Tone gain was only a
diagnostic: neither the production operator nor this simultaneous check
demixed an overlapping waveform by carrier label.

The probe discarded the full finite initial FIR support plus 1,000 seconds,
then measured 100 slow and 1,000 fast modulation cycles: 1,000,000 seconds per
low-separation profile and 20,000 seconds per high-separation profile. Measured
sidebands included both signs of both modulation frequencies. Phase error
excluded samples where either compared amplitude was at most `1e-8`; complex
and amplitude errors included every retained sample.

| Maximum over the complete tested grid | Result |
| --- | ---: |
| Individual finite-versus-ideal complex gain RMS | 6.44 × 10⁻¹³ |
| Individual amplitude error RMS | 4.40 × 10⁻¹³ |
| Individual phase error RMS | 1.24 × 10⁻¹² rad |
| Individual tone energy error | 3.21 × 10⁻¹³ dB |
| Simultaneous three-tone energy error | 1.96 × 10⁻¹³ dB |
| Sideband amplitude error | 1.76 × 10⁻¹³ |
| Simultaneous versus summed-isolated relative error | 1.78 × 10⁻¹⁶ |

The adjacent complex-response difference RMS remains 0.923–1.128; adjacent
amplitude difference RMS is 0.226–0.921, and phase difference RMS is
0.781–1.018 radians. Thus these rate/offset/ray combinations do not collapse
the selected modulation into a common response.

The unusually small interpolation error has a specific explanation. Carrier
spacing is `1.25 × rate / 6`; the two nonzero ideal delays are therefore
1,600/3,200 seconds at rate 0.001 and 160/320 seconds at rate 0.01. These are
integral sample delays at every tested sample rate, apart from floating-point
roundoff. This favorable property must not be generalized to arbitrary rates,
spacings or sample clocks. All deterministic tone/modulation lines lie inside
Nyquist; stochastic oscillator spectra and shaped-data sidelobes are outside
this particular line-support check.

The [retained summary](summary.json) passed the declared 0.1-dB energy-error
and 0.01 complex-gain-error limits. It recorded an 8.84-second serial
diagnostic, about one second of compilation, and a source header matching its
recorded Git revision; the [raw grid](response-results.jsonl) and
[run receipt](run-receipt.json) remain available. The standalone probe source
is no longer retained, so the archived measurements can be inspected but this
specific executable cannot be rebuilt from this directory. Ordinary-receiver
tests, physical device measurements and receiver admissions were not part of
the check.

## Reproduction

This historical check used a compiled deterministic probe and a bounded,
single-process run. Its raw grid, summary and receipt remain as evidence for
this interpolation finding; the probe is not retained as a routine test
recipe. Future intensive Robust checks follow the
[abbreviated statistical harness](../../../../triad-performance.md)
and the [material lifecycle policy](../../../../development.md#material-lifecycle).

The construction follows the earlier
[selective-correction response probe](../../2026-11-02-selective-correction/response-v2/README.md)
and the [current channel-model guidance](../../../../channel-models.md).
