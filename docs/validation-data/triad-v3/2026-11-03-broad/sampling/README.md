# Selective-stage sampling refinement for the long-rate cases

The selected long-tier sampling rates pass this **stage-specific** refinement.
It tests the actual `DispersiveSelectivePerturbation` operator against its ideal
physical-delay response; it does not establish whole-waveform, oscillator,
compression or receiver convergence. No runtime source was changed.

The [48 retained profiles](response-results.jsonl) cover GUI rates 0.001 and
0.01 Hz with Added Symbols ×1, ray separations 0.001 and 0.05 Hz, Each65,
common input offsets ±0.00005 Hz and both ray-center offsets (0 and the selected
separation). The base sample rates are 0.25 Hz for 0.001-Hz separation and
0.5 Hz for 0.05-Hz separation; each is repeated at 2× and 4×. This is one fixed
modulation seed, not 48 independent channel trials.

For every profile, three isolated tone probes measure the actual complex
response at the three carrier frequencies. A fourth instance processes their
sum with the same uninterrupted FIR state. Its output is compared with the sum
of isolated outputs and with the ideal delayed-branch construction. Tone gain
is a diagnostic only: neither the production operator nor this simultaneous
check demixes an overlapping waveform by carrier label.

The probe discards the full finite initial FIR support plus 1,000 seconds, then
measures 100 slow and 1,000 fast modulation cycles. This means 1,000,000 seconds
per low-separation profile and 20,000 seconds per high-separation profile.
Measured sidebands include both signs of both modulation frequencies. Phase
error excludes samples where either compared amplitude is at most `1e-8`;
complex and amplitude errors include every retained sample.

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

The [source-bound summary](summary.json) passes the declared 0.1-dB energy-error
and 0.01 complex-gain-error limits. The serial diagnostic completed in
8.84 seconds; compilation took about one second. The [run receipt](run-receipt.json)
retains both bounded-process outcomes and the executable/compiler identities.
The source header matched its recorded Git revision. Ordinary-receiver tests,
physical device measurements and receiver admissions were not part of this
check.

## Reproduction

From the repository root, compile the small standalone probe and run it under
the normal bounded runner. Use a fresh output directory and keep the complete
JSONL file; the summary rejects missing or duplicate grid points.

```sh
mkdir -p local-evidence/triad-long-rate-refinement
g++ -std=c++20 -O3 -DNDEBUG -Isrc -Iinclude \
  docs/validation-data/triad-v3/2026-11-03-broad/sampling/long_rate_probe.cpp \
  -o local-evidence/triad-long-rate-refinement/probe
python3 tools/triad_bounded_run.py --cwd "$PWD" \
  --log local-evidence/triad-long-rate-refinement/response-results.jsonl \
  --status local-evidence/triad-long-rate-refinement/status.json \
  --timeout 45 --max-seconds 45 --term-grace 1 -- \
  local-evidence/triad-long-rate-refinement/probe
python3 docs/validation-data/triad-v3/2026-11-03-broad/sampling/summarize.py \
  --results local-evidence/triad-long-rate-refinement/response-results.jsonl \
  --output local-evidence/triad-long-rate-refinement/summary.json
```

The construction follows the earlier
[selective-correction response probe](../../2026-11-02-selective-correction/response-v2/README.md)
and the [versioned channel contract](../../../../channel-models.md#versioned-three-carrier-selective-modulation).
