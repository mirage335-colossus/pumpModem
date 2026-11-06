# Carrier-spacing and amplitude-stability follow-up — 2026-11-06

**Decision: retain the existing waveform.** Modest spacing increases can improve
some short-window extrema, but did not reliably achieve the preferred 100-ms
screen. The measured practical AGC-template benefit does not justify wider
occupied spectrum. The new reusable source diagnostic makes this decision
cheap to revisit. No transmitter, receiver, crypto, interleave, dwell, Helper,
power-allocation, template format or default spacing was changed.

The native source is bound to commit `5bf37e27` and the identities in the compact
[evidence archive](evidence.tar.gz), with [checksums](SHA256SUMS). The maintained
[pattern guide](../../../triad-patterns.md) describes the current construction.
The archive preserves scripts, manifests, raw reports, input-array hashes and
source/binary hashes; large I/Q arrays remain regenerable public-key fixtures.
This is new source-envelope evidence, not a receiver/channel sensitivity campaign.

## What was compared

The initial screen reused ten native source windows: three HF Rate-36/Added-10
keys, no-interleave, two microwave FHSS keys, Rate-36 without Added Symbols,
maximal spreading, Rate-24000, and Symbol Burst. We recombined the *actual native
shaped carrier envelopes*, changing only carrier mixing frequencies. The sweep
included spacing/fine-rate ratios 1.25, 1.30, 4/3, 1.375, 1.40, 1.45, 1.50,
1.60, 1.75 and 2.0. Larger diagnostic values are not a proposal to call a 60%
spacing increase "slight". Adjacent carrier centers remain symmetric about the
unchanged DATA center; no channel was split by carrier label.

Eight fresh fixture seeds, 74101–74108, then supplied independent 16-second
windows, both bit alternatives, at fine rate 60 Hz and 6000 complex samples/s.
These holdouts compared fixed ratios 1.25, 1.375, 1.5 and 1.6. Ratio choices were
made from the initial screen, not adjusted per secret key. Overlapping windows
are dependent observations; all reported maxima are observed extrema, **not
confidence bounds or guarantees for future messages**.

The strict engineering screen remains one quarter of the smallest base
amplitude step: `0.25 * 10 log10(1.6) = 0.5103 dB`. This is a diagnostic design
target, not a measured threshold at which a radio erases amplitude information.

| Eight fresh HF fixtures, worst over both alternatives | Current spacing 75 Hz | Candidate spacing 90 Hz |
| --- | ---: | ---: |
| Nominal total bandwidth | 225 Hz | 255 Hz (+13.3%) |
| 100-ms maximum absolute power/RMS-amplitude deviation | 0.7222 dB | 0.7029 dB |
| 150-ms maximum deviation | 0.5539 dB | 0.4730 dB |
| 200-ms maximum deviation | 0.3410 dB | 0.3453 dB |
| 800-ms maximum deviation | 0.1011 dB | 0.0828 dB |

Widening improved 150-ms extrema in this holdout, but did not consistently
improve the 80–120-ms neighborhood or meet the 100-ms screen. In the earlier
three fixtures, 100-ms maxima changed from 0.643/0.666/0.709 dB to
0.591/0.617/0.732 dB: one worsened. The previous report's 150-ms success therefore
remains an earlier limited-fixture observation; the fresh baseline passes at
200 ms, comfortably below 800 ms. High spreading and high rate already had
sub-100-ms margins. FHSS and burst gaps remain intentional amplitude changes;
known-envelope correction is only a measurement baseline, not waveform repair.

At unchanged pulse shaping, changing spacing cannot change
`sum_c |carrier_envelope_c(t)|²`. In the original HF windows this contribution
alone reached 0.405–0.488 dB at 100 ms; without interleave it reached 0.703 dB.
FHSS ramp and pulse-overlap effects also remain. A slightly wider triplet is
therefore not a universal AGC solution. At spacing 0.00075 Hz, even a 20%
increase leaves subsecond beat suppression negligible; the low-rate limitation
in the pattern guide remains unresolved.

## Fixed Helper placement

A small native experiment called the actual production Helper placement rule
with the unchanged selected pulse/ramp and a conservatively widened placement
band. When spacing increases by `delta`, adding `2*delta` to the existing
expected-spectrum certificate width encloses the shifted outer-carrier skirts.
It preserves the certificate's engineering status; it is not a new worst-key or
channel-output spectral bound. The correct nominal width is
`2*spacing + 1.25*fine_rate`, **not** `3*spacing` when pulse width stays fixed.

At the unchanged 6000-Hz sample rate:

| Configuration | DATA center | Spacing tested | High pilot from production planner |
| --- | ---: | ---: | ---: |
| HF 40-W Helper / 4-W DATA | 1725 or 1775 Hz | 75–96 Hz | 2400 Hz throughout |
| FHSS EME 15-kW Helper / 40-W DATA | 1725 Hz | 75–90 Hz | 2400 Hz throughout |
| Same EME | 1725 Hz | 96 Hz | **2450 Hz: ineligible under the user's fixed-pilot condition** |
| Same EME | 1775 Hz | 75–96 Hz | 2450 Hz throughout, including the baseline |

Thus lack of room is not the reason for rejecting the 90-Hz candidate. It fits
these plans but offers too little demonstrated practical benefit. EME's 75-Hz
baseline placement width is 265.3125 Hz at this sample rate; 90 Hz needs
295.3125 Hz and 96 Hz needs 307.3125 Hz. The sampled native pulse, ramp and
capture constraints remain unchanged. These calls do not measure bidirectional
Helper interference, phase precision or propagation under the candidate. Those
checks would be required before adoption. Paired channel studies would also
need a fixed frequency-selective correlation scale in Hz, rather than letting
that model widen with carrier spacing.

## Linear pulse alternative and observability cost

A separate inexpensive reconstruction used the native unshaped coefficients,
exact physical symbol centers, unit-energy RRC pulses, the same ±8-cell support
and C2 tail taper, and an eight-cell crop at each edge. The rolloff-0.25
reconstruction agrees with actual native carrier envelopes to below 1e-9
absolute error. Rolloff 0.35 and 0.5 were screened initially; 0.5 was then tested
on the fresh eight fixtures. This is an offline candidate, not a new native mode.
Rolloff controls excess bandwidth; see the primary
[GNU Radio filter API](https://www.gnuradio.org/doc/doxygen/classgr_1_1filter_1_1firdes.html)
and [MathWorks finite RRC design documentation](https://www.mathworks.com/help/signal/ref/rcosdesign.html).

With rolloff 0.5 and 90-Hz spacing, nominal width rises to 270 Hz (+20%). The
fresh worst 100-ms excursion improves from 0.7222 to **0.5426 dB**, still above
the screen; 150 ms improves to 0.3699 dB. The illustrative DATA-only, high-SNR,
100-ms one-pole power detector's maximum matched-template shape loss changes
from **0.0006969 to 0.0003821 dB**. This is only 0.000315 dB of modeled benefit,
not a measured radio sensitivity gain.

Wider rolloff also increases an existing public symbol-periodic power feature.
For independent zero-mean cell vectors, expected summed shaped power is
`sum_k |p(t-kT)|²`. Numerical integration of the same finite unit-energy pulse
finds its symbol-rate harmonic amplitude rises from **0.15915 to 0.31832** of
mean power (rolloff 0.25→0.5); the ideal infinite-RRC expression is `2*rolloff/pi`.
The corresponding periodic mean-power peak-to-peak variation is 1.394→2.864 dB.
A finite received realization and an adversary's detection performance are
separate questions. This is a reason not to claim improved noise-like appearance
merely from better 100-ms averaging. No nonlinear gain was applied to flatten the transmitted source.

## Reproduction and inexpensive regression

Use a numerical Python environment with NumPy and SciPy. No GPU, radio device,
ordinary Robust receiver or historical full campaign is needed:

```sh
./build.sh configure --cli
cmake --build build/dev-cli --parallel 2 --target triad_pattern_probe
build/dev-cli/triad_pattern_probe --output local-evidence/spacing-example \
  --rate 36 --added 10 --sample-rate 6000 --seconds 60 \
  --window-seconds 16 --seed 74101
python3 tools/triad_spacing_diagnostic.py local-evidence/spacing-example \
  --spacing-ratios 1.25,1.5 --output local-evidence/spacing-example.json
python3 tests/test_triad_spacing_diagnostic.py
```

The diagnostic verifies native composite reconstruction before sweeping,
retains both bit alternatives and all samples, and tests nearby averaging
windows plus one-pole power detectors. It rejects propagated input, unsupported
pulse descriptors and invalid sample geometry; its widths assume the current
rolloff-0.25 source. It caches spacing-invariant measurements and reuses the
native baseline instead of rebuilding or propagating waveforms. Bounds on
samples, candidates and aggregate work prevent accidental large campaigns.

The raw evidence distinguishes the exploratory script's initial short-record
one-pole failure from the corrected measurement: a detector with no samples
remaining after its initialization period is reported unmeasured. No signal or
receiver threshold was changed to get a pass. Functional checks cover the
new diagnostic, not a new transmitted waveform. All **10 diagnostic tests and 3 related metric tests passed** in 1.04 seconds total. Reanalyzing both alternatives of all eight fresh probes at 75/90-Hz spacing took **3.52 seconds** wall time, with one numerical-library thread on the local Ryzen 5 PRO 5650U. Probe generation and exploratory sweeps are separate costs; this is not a receiver-runtime measurement. The report's `summary.json` retains exact commands, CPU time and identities.

Unresolved: preferred universal <100-ms stability, very-low-rate <800-ms
stability, and calibrated physical-radio AGC effects. Neither these source
screens nor a successful placement call establish live sensitivity or false
admission rates. The current evidence does not warrant another RunPod or live
campaign; no cloud resources were rented for this follow-up.
