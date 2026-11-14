# Scalar forward scattering through separated HF screens

This constructed appraisal isolates diffraction between two underdense plasma
regions and its effect on an otherwise unchanged receiver. It extends the
single translating Gaussian lens with two separated, transverse density maps;
scattering by the first screen changes the field incident on the second. It
does not solve for a reflecting ionosphere, three-dimensional backscatter,
cutoff, magnetic or polarization coupling, or an evolving Maxwell medium.

[Ding et al. (2021), §§2.2 and 4.1](https://doi.org/10.1029/2020RS007239)
give primary support for alternating phase changes and diffraction and for
frozen advection. Their fitted 12.7 MHz example used ten screens across a 30 km
width, 15 m resolution, 15 m inner scale, 2.5 km outer scale and 750 m/s
advection. This appraisal covers much smoother, slower structures. It neither
reproduces that fit nor estimates a field prevalence or decoding probability.
The fitted numbers are not independent plasma measurements.

## Frozen model and receiver comparison

The incident field is a plane wave at 10 MHz on a prescribed unfolded 600 km
vacuum path. Two 3 km thick slabs centered at 110 and 300 km have background
column 0.018 TECU apiece. Each slab has four elliptical Gaussian variations
whose centers, widths and signed differential TEC values are fixed in
[`hf_phase_screens.cpp`](src/hf_phase_screens.cpp). Every sigma is at least
1.25 km. The second field changes centers, widths and strengths before any
receiver outcomes. Both fields use the exact scalar index
`sqrt(1 - f_p² / f²)` at each density and physical RF frequency. Kicks apply
the local index difference from the ambient index; the same input density
map is used at every RF frequency. Between kicks the model propagates the
scalar angular spectrum on its forward, propagating branch in ambient plasma
or vacuum. Background phase is restored exactly once. The split model assumes
small angles, weak reflection at boundaries and negligible change of advection
over the flight across each slab. With nominal center flight compensation,
common advection translates the output field on the declared straight path.
This removes any need to normalize, regenerate, or repeat a moving patch.

The primary field drifts at 42 m/s, has impact 0 m and is centered at output
time 600 s before the sampled modeling delay. The off control sets only the
perturbations to zero. The holdout uses the second set of clumps, 750 m impact,
35 m/s drift and closest time 490 s. It is a new constructed field, not a new
measured day. The supported horizon is at most 1,600 s and must fit the
statically observed ±45 km cut, including padding for the retained FIR tail.
The native stream keeps continuous source time, causal lags 4–84 at 1,920
samples/s and a disclosed 32-sample delay. The occupied source band is ±384 Hz.
The link uses a common incident-power reference and white-noise variance, with
no gain adjustment based on the resulting intensity or received energy. The
public receiver gets only its configured arrival/frequency priors and its
ordinary samples, never the screen values or the geometry of individual fades.
Count complete message outcomes only when two physical quiet slots are scored.

The geometry check before receiver trials evaluated the fixed density maps
along the declared trajectories at about 0.1 s spacing within each of the 13
DATA supports in the [64 s receiver plan](campaigns/hf-phase-screens-receiver.json). For each slab,
let `φ` be its scalar phase change from ambient at 10 MHz and `k` the vacuum
wavenumber. The local lens strength is `(D/k) max |eig(∇²φ)|`, where `D` is
190 km from the first slab to the second and 300 km from the second to the
receiver. On about 55% of primary DATA samples and 63% of holdout samples,
the larger of the two lens strengths was at least one. A majority of samples
reached that level in seven consecutive primary slots and eight consecutive holdout
slots, at 69.6 s actual bit duration. This measures local curvature of the
prescribed density maps. Received `S4` requires intensity statistics, and
decoding probabilities require separate receiver evidence. If the receiver
results reveal a localization gap, a denser set of staggered smooth clumps
could test sustained exposure from the first DATA slot to the last.

For the bounded native qualification, first compare the no-screen plane and
single Gaussian to independent analytic responses, and check causal support,
tail, whole-horizon guards and cancellation. Then run one CPU cost pilot and
the following witnesses for each field being admitted. Cap this initial
numerical pilot and all its witnesses at 240 s and 64 MiB; do not widen the
work or move it to a GPU based on receiver outcomes.

- Production mesh: 1024×256 cells at 125 m, width 128 km and height 32 km.
  Change only mesh size to 62.5 m at fixed extent, then independently double
  both extents at the production cell size. At each of −384, 0 and +384 Hz,
  compare complex voltage across the full cut at 15.625 m spacing. Each
  difference must be at most 0.001 of the incident voltage. The FFT assumes
  periodic boundary conditions, so only the fixed interior cut with this
  independent box check is admitted. The time trajectory never wraps.
- Split both fixed 3 km slabs into one, two and four midpoint kicks. At the
  same three RF frequencies and all points, 2→4 must be at most 0.012 and at
  most half of 1→2 unless already below 0.001. The four-slice field is the
  production field. Separately measure the change caused by collapsing both
  integrated phase maps onto one plane at the first slab center.
- The initial global slice gate does not certify a deep relative fade. At
  each mesh/box witness frequency, freeze the location of the lowest
  four-slice voltage and all locations whose observed 2→4 voltage difference
  reaches 10% of the four-slice voltage. Only then solve the same physical
  field at eight and sixteen slices. For the frozen detectable points,
  require `|V4−V8|+|V8−V16| ≤ 0.10 |V16|`, along with successive local
  differences no greater than half the previous one or `10⁻⁶` incident volts.
  Apply the 10% allowance to every original point at those three frequencies
  whose final witness is at least `0.002` incident volts. Flag lower final
  amplitudes as unresolved in relative terms. Report frozen pointwise minima
  and the full-cut maximum ratio independently of the raw gain range.
- Resolve what happens between the original 15.625 m samples near each frozen
  four-slice minimum. Evaluate the retained spectral rows for four, eight and
  sixteen slices on a 0.125 m grid extending 15.625 m to either side of that
  minimum, clipped to the admitted cut. The independent Fourier sum must
  agree with each original sampled anchor within 10⁻¹¹ incident volts.
  Evaluate the actual Hermite/FIR response at every fine position and compare
  it to the four-slice field with the existing absolute limit of 0.0005.
  Where the sixteen-slice amplitude is at least 0.002, require the sum of
  `|FIR−V4|`, `|V4−V8|` and `|V8−V16|` to be at most 10% of `|V16|`.
  Record the lowest reference and FIR amplitudes, the largest relative error
  allowances, and the sampled intervals below the same 0.002 floor. This
  finer check covers the declared windows at three RF offsets.
- Track full-plane normalized squared field to 5×10⁻¹¹ and report angularly
  weighted longitudinal flux separately. Require spectral energy beyond
  75% of spatial Nyquist along either axis ≤10⁻⁸ and beyond `q/(k*n)=0.10`
  ≤10⁻⁶. The numerical power invariant and pointwise power at the antenna
  are different quantities. The small-angle model does not enforce exact
  electromagnetic longitudinal flux conservation at the phase kicks.
- Fit nine RF points every 240 Hz across ±960 Hz after removing the shared
  bulk phase. Compare all eight midpoints and four independent occupied offsets
  on the full cut (complex error ≤0.00005). Validate cubic interpolation of
  62.5 m knots at all three quarter points in every interval against direct
  spectral oversampling (≤0.0005). Then validate the actual causal FIR on the
  whole cut at −384, −103.7, 0, +196.5 and +384 Hz (≤0.0005, also ≤10% for
  target amplitude ≥0.002). Report below-floor points as unresolved; do not
  interpret their relative phase. Retain the usual independent energy report
  for the raw finite-band precursor, omitted FIR energy ≤10⁻⁶, and the
  per-tap freeze bound ≤0.005 of peak input. The last bound does not cover
  true time-dependent electromagnetic effects.

If admission fails, retain the witness and do not feed the failed table to a
receiver. A successful numerical table validates these finite constructions;
the receiver comparison is a separate deterministic modifier of the measured
appraisal, not a channel calibration or general receiver-capacity test.

## Initial numerical qualification

The independent native component test passed the uniform ambient plane,
independent spherical one-screen Gaussian limit, sample guards, cancellation,
and causal impulse/tail checks. A matched CPU probe run admitted both fields
with 34 solves, 12 held RF offsets, and 5,761 spatial positions per field.
Measured preparation took 7.03 s and 7.08 s respectively, each charging a
53,807,888-byte preparation peak. The initial probe source manifest was
`2ca3cfb49d1cb4735c497dd6e22272518500c14da2ac27fdb40ccbe0b1433837`.
These are appraisal preparation times, not modem throughput.

| Witness | Primary | Held field |
| --- | ---: | ---: |
| Half-cell complex error | 9.58×10⁻⁹ | 5.41×10⁻⁹ |
| Larger-box complex error | 8.16×10⁻⁸ | 5.60×10⁻⁹ |
| 1→2, 2→4 slice changes | 0.01381, 0.003450 | 0.01810, 0.004528 |
| FIR complex error | 5.75×10⁻⁵ | 4.93×10⁻⁵ |
| Raw carrier power range on the cut | 0.000788–11.3881 | 0.00004518–8.35614 |
| Maximum complex change after collapsing both phase maps onto one plane | 4.52 | 4.19 |

Normalized plane power was conserved within 3.4×10⁻¹³; angularly weighted
longitudinal flux exceeded 0.99999. The actual local signal retains both
defocusing and focusing, without a received-power adjustment. The slice
comparison is the dominant numerical difference for these constructed slabs;
small interpolation errors do not bound the fidelity of the atmospheric model.

The subsequent relative gate checked all 17,283 position/frequency points for
each field. No final witness fell below the 0.002 amplitude floor. The worst
relative allowances were 0.947% (primary) and 3.511% (held field), under the
10% gate. In the held field at its sampled carrier minimum, x = 328.125 m, the four-,
eight- and sixteen-slice amplitudes were 0.00672189, 0.00666256 and 0.00664841.
Their relative allowance was 3.430%, so the roughly 43.4 dB local fade on this
cut is resolved within the constructed scalar model. The relative probe
measured 40 solves and 8.70 seconds per field on the same CPU. Neither sampled
refinement nor the angular-spectrum propagator removes the small-angle
approximation where density and transverse wavenumber vary together.
The homogeneous angular propagation is exact for the ambient index; local
index kicks plus that propagator still neglect mixed angle/index and interface
effects. The slice convergence is convergence of this split forward model.

The exact-precision full-cut repeats used source manifest
`634065934263986d99e12c947a8e84be9240569b27d7ff9547af94739abb3ef0`. They
took 8.66 s and 8.71 s with a 53,807,888-byte preparation peak. The finer
probes reproduced every shared numerical witness exactly and added the
local minima checks below. Their charged peak rose to 53,915,616 bytes.
The two earlier successful JSON receipts were retired. Their commands,
hashes and host remain in the [earlier probe provenance](local-evidence/hf-phase-screens-replay-provenance.txt),
which also identifies the retained Gaussian thickness receipt.

The finer check covered 251 positions in each of the six windows, with no
sampled points below the 0.002 reference floor. The largest disagreement
with a shared Fourier anchor was 2.66×10⁻¹⁴. The worst combined FIR and slab
allowances were 0.4011% for the primary field and 4.2068% for the held field.
At carrier, the held field's sixteen-slice minimum moved to 326.25 m with
amplitude 0.00560930. The actual FIR reached 0.00555374 at 326.125 m, about
45.11 dB below incident power. At +384 Hz the sampled reference and FIR
minima were 0.00542703 and 0.00537158. These counts and minima apply to the
fixed spatial and frequency samples used by the finer check.

The decisive fine-window probes took 8.63 s and 8.61 s with the same 40
field solves per case. Retain the [primary receipt](local-evidence/hf-phase-screens-primary-null-window.json),
[holdout receipt](local-evidence/hf-phase-screens-holdout-null-window.json),
and [fine-window provenance](local-evidence/hf-phase-screens-null-window-provenance.txt)
through the current acceptance review and any investigation that needs their
exact numerical inputs.

## Complete-receiver outcomes

These cases use the configured public calendar with Helper and FEC off.
They exercise the native short-message codec and its existing two-scored-quiet
completion rule; no received length or evaluator EOF establishes completion.

All seven frozen cases completed and passed strict validation, including
independent offline checks of the raw native receipts. Total campaign wall
time was 272.95 seconds. The ambient Off row used 21 physical solves and
omitted active slab and null-window witnesses. All perturbed rows report 40
solves and three fine null windows. The [frozen input](local-evidence/hf-phase-screens-receiver/frozen-input.json)
and [run report](local-evidence/hf-phase-screens-receiver/report.md) bind the
cases to executable SHA-256
`d951992789968ccf2a2334fa5706efb4d18b9e864c3fc3baa3ad9ca9fad60639`.

| Fixed case | Incident C/N0 (dB-Hz) | Active slots called quiet | Premature completions | Accepted payload |
| --- | ---: | ---: | ---: | --- |
| Ambient Off | 0 | 0 | 0 | Correct `A` |
| Primary, 69.6 s actual bits | 0 | 8 | 2 | None |
| Primary, 69.6 s actual bits | +10 | 1 | 0 | None |
| Primary, 34.8 s actual bits | +3 | 8 | 2 | None |
| Independent field and `B` payload | 0 | 8 | 2 | Incorrect `[32]` (space) |
| Primary noise only | +10 | — | 0 | None |
| Primary wrong key | +10 | — | 0 | None |

Both negative controls made zero DATA decisions. No wanted row made a wrong
bit decision on a DATA slot. In the `B` holdout, slots 7–9 supplied three
correct bits and active slots 10–11 were scored quiet. The resulting
premature fragment decoded as a space and the receiver accepted it.
The receipt reports `matches_transmitted_message=false` and
`authenticated=false`. DATA resumed at slot 12. This shows an
accepted-content failure caused by missed presence and physical framing
even with bitwise correctness on every admitted DATA slot.

The existing native RS20/RS60 modes include CRC32 validation after physical
completion. They are a conditional integrity alternative, not a demonstrated
repair of this capture. At the original duration their longer waveform exceeds
this model's 1,600-second capture horizon; shortening bits would also change
energy and fade exposure. The [earlier coding comparisons](hf-experiments.md)
retain those tradeoffs. No FEC comparison or completion-policy change is
claimed for these cases.

At +10 dB-Hz, the primary field recovered 12 of 13 active bits. Slot 10
scored −6.379 nats of presence against 21.493. The codec rejected the
full framed observation after the single erasure. The short +3 dB-Hz
comparison had 99.88% of the long 0 dB-Hz row's incident energy divided
by added noise and 93.63% of its selected received energy divided by noise.
Both missed eight active slots and rejected the payload. The two fixed
energy conditions and deterministic constructed fields provide no
frequency-of-failure or population sensitivity estimate.

### Higher C/N0 at the same two placements

The [fixed two-row bracket](campaigns/hf-phase-screens-bracket.json)
raises the primary from +10 to +20 dB-Hz and the `B` holdout from 0 to
+10 dB-Hz. The initial runner preflight rejected +20 under its former
0/3/+10 admission list, before launching native work. The corrected
runner admits +20 for primary wanted receive at the original duration.
Both rows then completed and passed strict validation in 67.59 seconds.
Each pair shares its qualified table digest, channel incident and
received DATA energy sums, source DATA energy, public geometry and
canonical native source. By placement, the incident/received energy
sums were 1,747,075.16/1,552,214.30 and 1,747,066.93/1,464,648.52.
The variance of the added noise fell by a factor of ten in each pair.

The primary at +20 accepted exact `A`, with 13 correct DATA decisions
and the two physical quiet slots. The holdout at +10 scored active
slots 0 and 6 quiet, made 11 correct DATA decisions, and rejected the
single physically completed frame. Its codec tracked the interior
erasure at slot 6. The initial quiet slot accounts for the other missing
DATA decision. The higher level removed the original accepted space
fragment and both premature endings on this holdout. It still delivered
no payload. These fixed points record improved detection and reduced
failure severity under the two specific placements.
[Bracket report](local-evidence/hf-phase-screens-bracket/report.md),
[frozen input](local-evidence/hf-phase-screens-bracket/frozen-input.json).

## Frozen same-input causal check

The [two-row known-mean follow-up](campaigns/hf-phase-screens-known-mean.json)
selects the primary +10 dB-Hz case's sole active miss, slot 10, and the
second active quiet slot in the pair that prematurely ended the `B`
holdout fragment, slot 11. Its presence score was 11.273 nats, with an
18.379-nat bit-evidence gap. This selection was fixed before inspecting
private template outcomes. The preceding active quiet slot 10 remains
beyond this two-row scope. Each row has a 90-second, 3,072,000-sample
limit and both share a 180-second wall budget.

Require the original full-capture digests before interpretation.
The primary capture is
`7499581a87b6d65067892ce97dc8a176e20d0681f911517c572cb89cff073008`,
and the holdout capture is
`55d74abe9cbf41eda0b90980dd057c9e3a7f2c2d35f0bd57c2f874c70c1bc92c`.
The ordinary receiver first scores the unchanged full capture and hashes
the selected aperture. Private full-prefix comparisons then test the
known wanted and alternate bit, and the known wanted and target-absent
waveform. All three templates use the original qualified physical table.
The conditional known means preserve all other transmitted slots and
use the original synthetic noise variance. They measure available
information under a fixed known channel and context. Blind recovery
and any eventual receiver policy remain separate questions.

Every missed active slot in the +10 dB-Hz primary and `B` holdout
already ran the short varying, coherent-gain and drift models. Applying
the existing compare-admitted policy would add work to earlier successful
slots, with the same alternatives at the observed presence failures.
The full-grid phase shadow would require 559 timing atoms and about
53.3 billion FFT row-stages for the fixed 97-model bank, beyond its
512-atom and 40-billion limits. The correlated phase variants also
require their fixed 39-atom, 128-row geometry at ±1 Hz. These screen
cases keep the full ±5 Hz configured allowance. A useful alternative
will need distinct support or an evidence-preserving reduction in work
over the full public geometry, followed by fresh wanted and negative
controls.

Retain the raw cohort and selected diagnostics until both causal
questions and the incorrect accepted fragment have been reviewed.
Keep the findings here when redundant raw evidence is removed.

### Matched known-mean outcomes

Both fixed rows passed strict receipt validation in 136.87 seconds.
For each row, the full IQ and validity digest, qualified physical table,
all 15 native slot geometries, decisions and evidence, and the decoded
payloads and framing reproduce the original receipt exactly. The
post-receive evaluator retained the original selected samples. Its
three full-prefix mean templates agree sample for sample before the
selected aperture. The target-absent mean has zero energy in the
aperture and nonzero signal earlier in the prefix.

| Original failed aperture | Presence (D²/v) | Observed presence log LR | Binary (D²/v) | Observed correct-bit log LR |
| --- | ---: | ---: | ---: | ---: |
| Primary +10 dB-Hz, slot 10 | 57.301 | 57.182 | 112.382 | 100.825 |
| `B` holdout, slot 11 | 33.318 | 43.929 | 66.143 | 86.696 |

As a conditional Neyman–Pearson reference, take
`α = exp(−21.4933786883) ≈ 4.630×10⁻¹⁰` as a reference size. For two fixed known means
with separation `λ = D²/v` in proper-complex independent white
noise of variance `v`, their log likelihood ratio has mean `−λ`
under the absent mean and `+λ` under the wanted mean, with variance
`2λ` in either case. At size `α`, known-mean detection is
`Φ(√(2λ) − Φ⁻¹(1−α))`. This gives about 99.9998% for the primary
miss and 97.94% for the holdout aperture that ended the false
fragment. These are analytic conditional detection probabilities
under known phase, amplitude, timing, bit and other-slot context.
The ordinary receiver still missed both apertures on the matched inputs.

The private steps took 30.50 and 30.89 seconds, including three
independent preparations of the qualified physical operator in each
row. The [follow-up report](local-evidence/hf-phase-screens-known-mean/report.md)
and [frozen input](local-evidence/hf-phase-screens-known-mean/frozen-input.json)
bind the two rows to follow-up executable SHA-256
`5f9251c15b8c712e84211052f3fea5ae435880d7b1a47f2eb1cf5002d19853f6`.

## Independent thickness diagnostic for the single Gaussian

The two-screen model above changes both geometry and the medium. To isolate
thickness near the previously identified single-Gaussian null, retain the
200 km incident and 400 km outgoing point-source geometry, 0.015 TECU
ambient, −0.00857 TECU perturbation, 1,600 m by 2,400 m sigmas, and
10 MHz − 118.7 Hz. Compare the thin vacuum-diffraction convention and a
midpoint approximation that includes ambient diffraction to 2, 4 and 8 slices
of the same 3 km slab. Probe only the predeclared displacements 800, 825,
850, 875 and 900 m with no cross-track shift. At 850 m repeat the eight-slice
field with half-size cells at fixed extent and twice the extent at fixed cells.

In the scalar paraxial approximation, use `B(z)=∫dz/n₀(z)` for the background
diffraction coordinate. Factoring out spherical curvature on each plane
leaves a plane-wave Fresnel propagation in the transverse coordinate
`ξ = x B(z₀)/B(z)` with effective flight differences
`Δτ = B(z₀)² [1/B(z₁) − 1/B(z₂)]`, where `z₀=200 km`. Evaluate each physical
Gaussian in its physical transverse coordinate at that plane. In vacuum and
for one midpoint this reduces to the single-screen effective distance. Keep
the raw complex value and the 4→8 change even where local voltage is small.
The diagnostic divides out the corresponding uniform spherical reference in
each comparison; this is not received-power normalization in the channel.
This diagnoses sensitivity to an assumed thickness and depth profile under a
consistent spherical illumination; it is not validation against a measured
ionospheric null and does not change production admission criteria.

`high_fidelity_phase_screens_probe wave-thickness` agreed with the analytic
thin case within 1.55×10⁻¹¹ in complex voltage. The eight-slice half-cell and
larger-box differences at 850 m were both below 7×10⁻¹². At this displacement,
the thin and eight-slice magnitudes were 0.0885343 and 0.0825975; the complex
change was 0.006040 and the 4→8 difference was 0.000306. This constructed
thickness profile deepened the fading there by about 0.60 dB. Across the five
predeclared positions, the largest thin-to-eight complex difference was
0.007420 and the largest 4→8 difference was 0.000374. The probe receipt
`local-evidence/hf-single-gaussian-thickness.json` supports the current appraisal
review; remove the raw receipt once its numerical provenance has been reviewed
and no outstanding decision depends on the complex rows.
