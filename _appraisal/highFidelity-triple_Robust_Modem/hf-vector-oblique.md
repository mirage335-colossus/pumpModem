# Coupled vector diffraction and oblique HF reflection

This extension addresses a specific environmental gap: the previous magnetic
column and scalar scattering models did not evolve polarization, transverse
diffraction and oblique reflection together. The new native experiment uses
one density-dependent Maxwell operator for these effects. Numerical admission
must precede complete-message trials; an implemented solver is not an admitted
channel or a successful receiver.

## Physical construction and research reconciliation

The construction is periodic in one horizontal coordinate, stratified in
height and invariant in the other horizontal coordinate. A cold collisional
plasma tensor describes a tilted, fixed geomagnetic field. Smooth horizontal
density harmonics change both the diagonal and off-diagonal dielectric
entries, coupling diffraction orders and electric-field components. The
calculation retains upward and downward waves and internal reflection.
It does not multiply a scalar fade by an independently chosen polarization
trace. Its initial physical domain is a constructed sporadic-E return,
with plane-wave illumination and a fixed receiving antenna.

Thin sporadic-E layers and horizontal structure provide a concrete physical
setting for this bounded geometry. [Wang et al. (2011)](https://doi.org/10.1029/2011JA016696)
report layer-type radar structures with 0.1–4 km vertical and 5–28 km zonal
extents, while also describing limitations of reconstruction from one radar.
Extent is not a measured periodic wavelength.
[Hysell et al. (2014)](https://doi.org/10.1002/2014GL061691) discuss natural
decakilometric layer structure; their heater-generated small-scale radar
irregularities must not be substituted for a natural channel realization.
[Haldoupis et al. (2024)](https://doi.org/10.1029/2024JA032505) also distinguish
virtual from true layer height. Ambient E-region ionization contributes to
ionosonde critical frequency. The constructed total-density profiles below
are therefore explicit test assumptions, not inversions of these measurements.

The method uses the Fourier-modal approach to anisotropic media described by
[Li (2003)](https://doi.org/10.1088/1464-4258/5/4/307), with a stable
[scattering-matrix cascade](https://web.ics.purdue.edu/~pbermel/ece595/Lectures/Li96.pdf).
Smooth transverse density avoids introducing discontinuous horizontal
interfaces whose Fourier products need separate factorization treatment;
see [Li (1996)](https://doi.org/10.1364/JOSAA.13.001870). These are numerical
method precedents, not observations validating the chosen ionosphere.

[Gondarenko et al. (2003)](https://doi.org/10.1029/2003JA009985) provide a
full-wave precedent for oblique magnetized HF propagation and mode conversion
near reflection. Their heating-related applications do not supply a weak
communications-channel distribution. The vector forward model of
[Rino and Carrano (2021)](https://doi.org/10.1016/j.jastp.2021.105558) couples
polarization and diffraction but omits backward waves. That omission matters
for this reflecting experiment. The open three-dimensional FDTD model
[FARR, Green et al. (2025)](https://doi.org/10.3389/fspas.2025.1521497)
provides a broader possible comparator; its published mode and smooth-path
checks do not independently qualify disturbed scattering in this appraisal.

Standardized statistical channels remain useful complementary controls.
[ITU-R F.1487](https://www.itu.int/rec/R-REC-F.1487-0-200005-I/en)
distinguishes comparative modem tests from quantitative predictions based on
representative channel parameters. The
[2024 Keykhosravi–Bedeer preprint](https://arxiv.org/abs/2402.16618)
reports pilot-estimation/BER gains using two independent fading paths with
specified delay/frequency spreads. Those scoped gains do not validate reception
through coupled polarization cancellation or long interior outages, and these
constructed vector tests do not invalidate their within-model findings.

[Gherm, Zernov and Strangeways (2005)](https://doi.org/10.1029/2004RS003093)
already developed a physically based, three-dimensional fluctuating-ionosphere
simulator including combined E and F returns using the complex-phase method.
This provides a broader-geometry precedent. Its asymptotic scattering treatment and the
present finite full-vector reflecting calculation have different domains;
neither supplies calibrated disturbed-day captures for this receiver.

The dimensional reduction remains consequential.
[Galiègue et al. (2017)](https://doi.org/10.1002/2016JA023233) show that
discarding a transverse dimension and irregularity orientation can change
scintillation estimates. A finite periodic two-dimensional field therefore
cannot establish a three-dimensional disturbed-day distribution, arbitrary
multiple-hop propagation or real hardware availability. The reference
collection makes the same distinction between a physical construction,
measured summary statistics and independently calibrated channel replay in
[_ref, Fable 33](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/33-hf-channel-models-and-simulators.md)
and [_ref, Astra 60](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/60-native-implementation-options.md).
The renewed [measurement audit](hf-research.md#transverse-diffraction-and-usable-measurement-audit)
records actual wideband disturbed-HF studies and the metadata/access barriers
to using their raw observations here.

## Maxwell and reference-plane contract

Use the time convention `exp(-i omega t)` and normalized magnetic field
`h = Z0 H`. For diffraction order `m`,
`P_m = sin(theta) + m*c/(period*frequency)`. The physical incident angle is
held fixed across RF frequency. Eliminating longitudinal fields gives

```text
Dz = -P hy,       hz = P Ey
Ez = -epsilon_zz^-1 (epsilon_zx Ex + epsilon_zy Ey + P hy)
d/dz (Ex, Ey, hx, hy) = i k0 (hy + P Ez, -hx, P^2 Ey - Dy, Dx)
```

Every dielectric block is the corresponding Fourier convolution from the same
electron-density field. Four independent scattering blocks retain illumination
and return from either side; ordinary reciprocal slab symmetry must not be
imposed on a fixed magnetic field. A thin scaled exponential is converted to
scattering form and doubled/cascaded without propagating a thick exponentially
growing transfer matrix. Linear-solve residuals and finite-work guards remain
part of admission.

Vacuum ports are normalized to vertical power flux. Their amplitudes must be
converted back to laboratory electric fields before summing orders and
projecting onto the antenna, including longitudinal TM field where applicable.
The up/down vacuum phases and horizontal receiving-position phase are applied
once. Only the explicitly stated common phase/delay may be removed for the
baseband representation. Ground separation in this plane-wave calculation
does not introduce point-source spreading, an antenna radiation pattern or an
absolute transmitter-power link budget. Pointwise focusing and total outgoing
flux are different observables.

Rigid translation of the density by `a` multiplies the response from incident
order zero to output order `m` by `exp(-i*m*2*pi*a/period)`. A direct solve of a
translated density must verify that shortcut before it supplies time variation.
It is exact for the stipulated frozen frequency-domain snapshots. The flight-time and finite-FIR check bounds changes from evaluating the
frozen FIR coefficients at different clocks. It does not certify omitted
moving-medium Maxwell physics, in-medium frequency conversion or resonance
dwell effects. Those require a time-dependent medium solver. The construction
is therefore a low-drift sequence of frozen scattering responses.

## Admission hierarchy before receiver execution

The initial bounded solver must pass independent controls before expensive
table construction:

- Vacuum and oblique isotropic slab Fresnel solutions, including phase.
- Uniform-in-x order isolation and the zero-magnetic-field limit.
- Normal-incidence tilted-field agreement with the existing independent
  stratified solver.
- Full matrix lossless flux conservation and passive collisional propagation,
  including cross terms between input channels.
- A weak-corrugation derivative check and the direct translation check.
- Rejection of unresolved longitudinal poles, near-grazing/closed vacuum
  ports and excessive work without denominator clipping or added collisions.

A nonzero-corrugation RF pilot must then demonstrate both non-specular return
and magnetic cross-polarization, and establish actual preparation cost.
Conservation alone is insufficient: finite Fourier truncations can conserve
energy while representing a different field. Refinement must compare both
order spectra and projected antenna voltage. Check vertical resolution,
successive diffraction-order limits, outer-order flux and destructive
interference positions. Every frequency and refinement order must fit the
declared propagating-port envelope; omitted orders cannot silently disappear.

Frequency interpolation, spatial interpolation and the actual causal FIR each
need separate held-out witnesses. Deep cancellation needs absolute and relative
error accounting rather than a convenient normalized plot. Numerical failure
withholds receiver admission and remains a recorded result. Numerical refinement
may repair an approximation, but cannot silently change the physical field or
the originally failed gate.

The shared native transmitter/channel/complete receiver preserves DATA, quiet
slots, delayed tails and continuous ordinals. An optional existing native
endpoint chain adds actual clock, oscillator and interference effects. The
initial table is limited to 1,920 samples/s and the declared occupied band;
Helper's wider composite waveform requires separate band qualification.
The receiver receives its configured public hypotheses and samples, not the
physical density, channel coefficients or evaluator-known bit history.

## Frozen physical fields and numerical pilot

The two fields were set before modem outcomes. Frequencies below describe the
peak *total* electron density through its plasma frequency, not an ionosonde
inversion or an empirical occurrence probability.

| Parameter | Primary | Independent holdout |
|---|---:|---:|
| Carrier / fixed incident angle | 10 MHz / 50° | 10 MHz / 53° |
| Layer height / vertical support | 112 km / 960 m | 104 km / 1,200 m |
| Transverse period | 24 km | 20.5 km |
| Peak plasma frequency | 6.55 MHz | 6.12 MHz |
| Collision frequency | 1,300 s⁻¹ | 2,450 s⁻¹ |
| Local corrugated slice | 80 m | 100 m |
| Density harmonic amplitudes, in 10-MHz critical-density units | 0.008, 0.0038 | 0.007, 0.0044 |
| Harmonic phases | 0.45, −1.2 rad | −1.15, 0.8 rad |
| Magnetic field (x,y,z) | (18,17,−43) μT | (−11,25,−46) μT |
| Incident TM power fraction | 0.10 | 0.23 |
| Frozen-field drift / initial epoch | 8 m/s / 600 s | 7.5 m/s / 520 s |

The antenna is laboratory y; the cross-antenna ablation uses (0.6,0,0.8).
Off removes only transverse perturbations, retaining the layer and magnetic
field. The zero-B ablation retains density perturbations. At most 2,400 seconds
are rendered, with explicit rejection before a whole transverse period repeats.
These slow translations do not cover rapid spread-F evolution.

The first carrier pilot, with ±8 retained orders and a ±12 comparison,
**failed numerical admission**: outer-order flux was 9.918×10⁻⁴ against a
5×10⁻⁵ limit, and the refined antenna-voltage L1 difference was 0.06341
against 0.004. The minimum-relative error was 1.076 against 0.15. Vertical
refinement passed (0.000536). The mixed launch returned 0.738 of incident
vertical flux, with 0.395 in non-specular orders; a separate unit-TE launch
returned 0.410 in non-specular TM orders. These differently normalized
cross-polarization and mixed-launch metrics are not subsets of one another.
No decoder used this rejected approximation.

The numerical-only repair retains the physical fields and thresholds and uses
±24 orders, a ±32 refinement and a ±16 coarse diagnostic. It adds all eight
midpoints of the nine-knot RF grid to the five original direct RF holdouts.
There are 29 RF solves per full table, 13 direct frequency witnesses and
2,048 transverse witnesses. Occupied-band minima include nine frequencies.
Maximum dense work remains 600 million charged terms per RF solve; preparation
has a conservative 16 MiB allocation bound. A carrier pilot has a 120-second
limit, and each standalone full-table qualification a 600-second limit.
These are execution limits, not allowed approximation errors.

The admission thresholds are voltage L1 differences ≤0.004 for mesh and order
refinement, outer-order flux ≤5×10⁻⁵, RF and FIR differences ≤0.0006,
spatial-period-averaged discarded energy ≤10⁻⁶, Hermite peak error ≤2×10⁻⁵,
frozen coefficient-clock error ≤0.004 and combined error relative to the worst
checked-band minimum ≤0.15. Period-averaged precursor energy is also reported;
it is not a maximum over receiving positions. Full coherent-input passivity
uses a 2×10⁻⁹ certificate slack and solve residuals ≤2×10⁻¹².

The [frozen receiver campaign](campaigns/hf-vector-oblique-receiver.json)
contains 13 selected complete-receiver rows: two primary incident-C/N0 points,
three physical ablations, two held-out-field points with a fresh payload,
one doubled-duration/−3 dB row, three native endpoint interaction rows and
two complete negative controls. It is capped at 8,400 seconds overall,
600 seconds per wanted row and 900 per negative control. Numerical rejection
is separate from communications failure; a rejected table cannot enter a
receiver. The 128-second/−3 dB comparison is approximately equal incident `E/N0`
at a common noise reference, and selected output `E/N0` is separately measured.
For direct scenarios C/N0 changes the noise variance; raw waveform sample
energy alone must not be used to describe the energy tradeoff. The endpoint chain uses
actual ±1-ppm sample clocks, ±0.01-ppm RF clocks and, where selected, a continuing
RF-plane tone. Helper and experimental receiver policies remain off.

## Qualified numerical outcomes

All five fields/controls passed full numerical and streaming admission on the
local Ryzen 5 PRO 5650U. Each uses 29 RF solves, 13 frequency holdouts, 2,048
period witnesses and 54 independent comparisons of the final assembled table
against raw physical RF responses. The latter include off-knot positions across
period boundaries and destructive-interference minima; their error must remain
below the existing FIR-plus-Hermite allowance plus 2×10⁻¹² arithmetic slack.
A moving tone is also evaluated through the actual stream against an independently
indexed tap sum; impulse, full drain and chunk boundaries are checked without
rebuilding the table.

| Field | Full preparation and stream checks | Assembled-table maximum voltage error |
|---|---:|---:|
| Primary | 8.877 s | 2.454×10⁻⁶ |
| Independent holdout | 9.459 s | 1.643×10⁻⁵ |
| No transverse perturbation | 2.547 s | 1.463×10⁻⁶ |
| Zero magnetic field | 5.755 s | 1.386×10⁻⁵ |
| Cross antenna | 8.868 s | 1.160×10⁻⁵ |

The primary sampled carrier minimum is 0.003005 times the incident reference power
(−25.22 dB), at a position crossed about 670 seconds into the frozen
primary capture. Its worst checked-band lower bound is 0.002903; combined
relative numerical error is 0.01225. The held-out field's full-period carrier
minimum is −14.78 dB, but that particular minimum lies outside its frozen
capture: it must not be presented as an observed fade in that message.
Pointwise maxima above one represent coherent focusing, not a total-flux gain.

The primary returns 0.738 of mixed incident vertical flux, including 0.394 in
non-specular orders. The independent unit-TE diagnostic returns 0.409 in
non-specular TM orders. With zero B, the latter is exactly zero while
non-specular scattering remains substantial. With perturbations off, total
reflection remains substantial but the y antenna receives only about 0.01278
of its incident reference (−18.94 dB). The controls therefore test physically
coupled redistribution, not just additional independently prescribed fading.

The admitted table occupies 1,061,112 bytes. Preparation reserves a conservative
12 MiB analytical quote within the 16 MiB limit; this is not measured process
RSS. All native receiver trials retain 24 software workers on 12 available
logical CPUs. No GPU or RunPod instance was used. These timings do not qualify
complete-receiver real-time capacity.

## Interpretation of initial complete-message failures

The original coupled-primary 0/+10 dB-Hz trials delivered no message. The
higher level recovered two additional late bits, while the first nine active
slots still missed presence. Equal source/table/calendar identities and strong
late evidence do not indicate a global bridge or conjugation failure. They do
not resolve attenuation versus mismatch in the early slots; the private
follow-up below addresses one such slot.

The independent field at 0 dB-Hz accepted **`i` instead of transmitted `B`**.
Slots 4–7 produced the correct surviving bits `1010`; quiet decisions in
interior slots 8–9 completed that fragment as another valid short message.
No admitted DATA bit was wrong. This is a new physical realization of the
previously exposed message-integrity weakness, now under coupled magnetic,
oblique and diffraction physics. The +10 version accepted no message but
still lost two active slots and ended prematurely. None of these outcomes
repairs or promotes the ordinary receiver.

## Receiver follow-up declared from the first misses

Before further receiver outcomes, the [boundary follow-up](campaigns/hf-vector-oblique-boundary.json)
was fixed after the original primary and holdout 0/+10 dB-Hz misses. It contains
primary, flat-layer and holdout +20 dB-Hz trials, and a matched primary +10
slot-zero replay using the existing native private target-presence diagnostic,
plus a +20 dB-Hz wrong-key control. The latter is frozen before the first +20
outcome, so weak-signal negative controls do not stand in for strong-signal
address discrimination.
Only if the primary +20 case still misses is one extra primary +30 trial
admitted. These are focused brackets, not a fitted success-rate campaign.

The diagnostic is evaluated **after** blind complete reception. It reuses the
canonical transmitter and native Channel to construct target, target-absent
and alternate-bit means, retaining the public calendar and whole-history
support. Receiver input hash, table hash, calendar and every blind decision
must agree with the archived +10 trial. It cannot supply physical coefficients
or target bits to blind acquisition. Its conditional Gaussian distances and
likelihoods represent evaluator-known means; they are not a realizable receiver
sensitivity or a claim that the same computation budget reaches them.

## Completed brackets and known-channel interpretation

The ordinary direct primary has 11, 9, 4 and 0 active-slot erasures at incident
C/N0 0, +10, +20 and +30 dB-Hz respectively. The +30 trial delivers the complete
`A`; the +20 flat-layer and held-out-field trials also deliver their complete
payloads. This establishes functioning reception and a fixed-case bracket,
not a sensitivity rating, environmental success probability or optimality gap.
All wanted observations have zero incorrect admitted DATA bits, including the
incorrect `i` acceptance described above. Message integrity is a separate gate.

The direct +20 wrong-key control produces **three false DATA decisions**, with
no accepted message. The endpoint noise/tone and wrong-key/tone controls at
0 dB-Hz instead score all fifteen slots quiet. Weak negative controls therefore
do not qualify strong-signal address discrimination. These failures remain
visible; no decision threshold, address prior or production receiver was changed.

The doubled-duration primary uses actual 139.2-second bits instead of 69.6,
with incident C/N0 reduced by 3 dB. Incident E/N0 is 910.252 versus 909.935,
a ratio of 1.000348, while selected-antenna E/N0 is 145.658 versus 174.606,
a ratio of 0.8342. The longer waveform encounters a different part of the
translating field; both cases fail. This is a useful equal-incident-energy
challenge, **not** an equal-received-information comparison or proof of a
receiver-only integration cliff. Across the C/N0 bracket, the implementation
changes white-noise variance while retaining source amplitude. Raw emitted
energy is unchanged; E/N0 expresses the noise-referenced comparison.

The private +10 primary slot-zero replay is exactly matched to its archived
blind input: 2,554,139 complex samples, the same table and public calendar,
and all fifteen scores, decisions and message outcomes. The retained
[match audit](local-evidence/hf-vector-oblique-preflight/known-channel-match-audit.json)
records the full hashes and excluded timing fields. The aperture starts at
sample zero, so equality of a pre-aperture prefix is vacuous in this case.

Known target-versus-absent noise-normalized squared distance is 11.3222 and its observed log
likelihood ratio is **−0.02854**. Conditional correct-bit-versus-alternate
noise-normalized squared distance is 22.2748, with observed log likelihood ratio **9.50489**. These
answer different questions: the received sample slightly favors absence under
the supplied known means, yet favors the transmitted bit if presence is
assumed. It does not establish a receiver-only missed-presence defect in this
slot. The +20 blind trial recovers slot zero; its four remaining erased slots
have not received a private diagnostic. The replay's 69.39 s wall cost includes
27.15 s for private reference preparation/rendering and is not blind receiver
service. Known-channel likelihoods must not be compared directly with a public
search's multiplicity-calibrated threshold as an achievable performance bound.

## Bright endpoint completion control

After direct +30 delivery, the [bright endpoint control](campaigns/hf-vector-oblique-bright-endpoint.json)
was frozen before execution. It keeps the primary field, payload, seeds,
public hypotheses, actual clock errors and continuing tone unchanged, reducing
only post-RX white-noise variance from 1,920 to 1.92. Source and tone amplitudes
and their ratio stay fixed. The native execution completes in 161.21 s, within
its 300-second cap; this is not a time-censored result.

All thirteen active bits are correct DATA with no active erasures. Nevertheless,
both final physical quiet slots are **ambiguous**, so no message completes or
is accepted. Their quiet energies are 22,034.33 and 22,172.19 against a
16,237.85 threshold; reported reasons are `unresolved_bit_or_null` and
`coherent_line_in_public_data_aperture`. This is a distinct completion failure
from the interior-fade `B`→`i` acceptance. Correct recovery of every active bit
does not establish delivery when continuing interference and endpoint effects
are present. The result demonstrates the combined case; isolating clocks from
the continuing tone requires a further matched comparison, not inference from
the reason string alone.

The [conditional endpoint-off discriminator](campaigns/hf-vector-oblique-bright-endpoint-off.json)
retains +30 C/N0, field, source/noise seeds, geometry and the same public
uncertainty. It switches only to the named zero-actual-clock/no-tone endpoint
case. This one-row control was admitted by the bright campaign's stopping rule;
it does not authorize a new sweep or receiver tuning.
It delivers the complete `A` in 149.08 s, with both ending slots quiet:
energies 15,843.35 and 15,826.70 against the same 16,237.85 threshold.
The physical field plus native endpoints and broad public hypotheses can
therefore succeed at this level. The combined clocks-plus-tone case remains
a failure; no clock-only +30 result or tone-specific causal isolation is claimed.

## Complete-receiver observation ledger

The six frozen campaigns contain **21 complete native observations**: twenty
original strict receipts and one explicitly metadata-audited receipt (†).
There are eighteen wanted observations: four correct complete messages, one
incorrect accepted `i`, and thirteen without delivery. The three negative
controls accept no message, but the direct strong wrong-key case has three
false DATA decisions. All wanted admitted DATA bits are correct. The counts
include one exact private-diagnostic replay and matched SNR/ablation cases;
they are not 21 independent success trials or a population error rate.

| Frozen observation | C/N0 (dB-Hz) | Active erasures | Complete-message result | Native wall (s) |
|---|---:|---:|---|---:|
| `primary-A-0` | 0 | 11 | No delivery | 46.14 |
| `primary-A-10` | 10 | 9 | No delivery | 42.09 |
| `flat-layer-A-0` | 0 | 13 | No delivery | 41.68 |
| `zero-B-A-0` | 0 | 3 | No delivery | 30.54 |
| `cross-antenna-A-0` | 0 | 6 | No delivery | 37.49 |
| `holdout-B-0` | 0 | 9 | Incorrect `i` accepted | 42.80 |
| `holdout-B-10` | 10 | 2 | No delivery | 33.14 |
| `primary-A-double-duration` | -3 | 12 | No delivery | 86.03 |
| `chain-endpoints-off †` | 0 | 11 | No delivery | 151.70 |
| `primary-A-20` | 20 | 4 | No delivery | 33.93 |
| `flat-layer-A-20` | 20 | 0 | Correct complete | 23.74 |
| `holdout-B-20` | 20 | 0 | Correct complete | 31.62 |
| `primary-A-10-known-channel-slot0` | 10 | 9 | No delivery | 69.39 |
| `primary-A-20-wrong-key` | 20 | — | wrong-key: 3 false DATA, 0 accepted | 44.47 |
| `primary-A-30` | 30 | 0 | Correct complete | 30.49 |
| `chain-clocks` | 0 | 11 | No delivery | 160.61 |
| `chain-clocks-tone` | 0 | 11 | No delivery | 153.77 |
| `chain-noise-tone` | 0 | — | noise: 0 false DATA, 0 accepted | 157.95 |
| `chain-wrong-key-tone` | 0 | — | wrong-key: 0 false DATA, 0 accepted | 176.83 |
| `chain-clocks-tone-A-30` | 30 | 0 | No delivery | 161.21 |
| `chain-off-A-30` | 30 | 0 | Correct complete | 149.08 |

Actual default bit duration is 69.6 s; the doubled row uses 139.2 s.
The private replay includes its diagnostic cost. Original endpoint-off †
retains the failed receipt and full native result as described below. Campaign
elapsed time totals 1,707.08 s (about 28.5 minutes), including orchestration;
this is neither a real-time capacity measurement nor a GPU result. Exact
row hashes and descriptive totals are in the
[observation audit](local-evidence/hf-vector-oblique-preflight/observation-summary.json).

## Receipt correction and provenance

The original thirteen-row campaign stopped after its ninth native observation.
That endpoint-off observation completed in 151.70 s, but strict receipt
validation rejected a reporting-only flag: `HfNativeChain::report()` declared
the engineering propagation-band allowance only for the earlier scalar model,
omitting the new vector model. The planned descriptor already declared the
correct scope. A one-boolean reporting correction adds the vector case; no DSP
or admission consumer uses that flag.

The original raw result and failed campaign status remain intact. An
[explicit metadata audit](local-evidence/hf-vector-oblique-preflight/endpoint-off-metadata-audit.json)
changes exactly that false flag to true in memory and passes the full receipt
validator. Count it as a **metadata-audited complete native observation**, not
an originally strict-validated trial. The four never-executed rows use the
[unchanged continuation](campaigns/hf-vector-oblique-endpoint-continuation.json).
This avoids repeating 151.70 seconds of identical DSP merely to change a
receipt flag, without hiding the original failure.

Native source remains
`dd2c9e267ae030db4393f815275ec615e8d6425732545bbdaf5d0705243e8e26`,
configured at repository commit `a225dfc4f2c6612507295b10b0fcfc9ec9b64707`.
Original receiver rows use appraisal identity
`d8357796c16a6f1128a9f01c4c88944d63313ace1d03811926e3090a1d8888e4`;
the reporting/private-diagnostic admission correction, boundary, upper bracket
and endpoint continuation use
`8b87689c1d5ebcf2aa3cfa7b8239ec067956a870e91e80bc68a4f298346fa2cf`.
Every raw receipt also binds its exact build options and source identities;
campaign run records bind the executable and frozen campaign hashes.
The final endpoint +30 admission build, which also includes the automatic
waveform-test registration, uses
appraisal identity
`bc1472525fa6ee35b3d80207e0e6afd06a57df784673fba1389f0da223775a30`.
It changes no physical calculation or receiver algorithm.

## Reproduction and remaining domain

From the repository root, the normal contained software check includes the
coupled solver and primary assembled-waveform check:

```sh
./_appraisal/highFidelity-triple_Robust_Modem/build.sh test
```

Run a frozen receiver campaign with the same native executable; use a fresh
output directory so earlier receipts cannot be overwritten:

```sh
python3 _appraisal/highFidelity-triple_Robust_Modem/run.py \
  --probe _appraisal/highFidelity-triple_Robust_Modem/.build/bin/high_fidelity_appraisal \
  --campaign _appraisal/highFidelity-triple_Robust_Modem/campaigns/hf-vector-oblique-receiver.json \
  --output-dir _appraisal/highFidelity-triple_Robust_Modem/local-evidence/vector-reproduction
```

The model closes the earlier *separation* of magnetic polarization, diffraction
and oblique reflection for this explicitly bounded two-dimensional geometry.
It does not close arbitrary three-dimensional irregularities, multiple hops,
fast evolving plasma, Helper acquisition, unknown calendars or hardware
nonlinearity. The new cases use one constructed sporadic-E family, one fresh
field/payload and named ablations; they are not independent draws from a
measured distribution of unusually disturbed days. Private diagnostics do not
establish an implementable optimum or smooth sensitivity versus CPU cost.

The [measurement audit](hf-research.md#transverse-diffraction-and-usable-measurement-audit)
now inspects actual public archive directories and representative data schemas.
McMurdo and Cameron products can constrain path observables; the inspected
products do not supply calibrated continuous complex transfer functions.
The full archive payloads were not exhaustively checked. Binary download
fragments were removed after recording checksums, metadata, schemas and scope;
they are not synthetic modem fixtures. Field calibration and independent
disturbed-event channel recordings remain a different evidence requirement.

## Evidence lifecycle

Keep numerical failures, the admitted table witnesses, frozen case inputs and
complete receiver receipts under this directory's ignored `local-evidence/`
while establishing this coupled operator and investigating its communications
outcomes. Preserve exact source/configuration identities and unsuccessful-search
costs. When a result is superseded, retain the useful physical, algorithmic and
negative findings here and remove redundant outputs. Reuse the existing build
cache; this experiment does not justify retaining another source tree or a
standalone modem implementation.
The original rejected ±8-order pilot and original invalid metadata receipt
remain necessary for numerical and provenance assurance. Final five-field
assembly witnesses supersede successful intermediate table pilots, whose
redundant outputs were removed. Keep the complete receiver receipts and exact
match audit while the wrong-content, false-DATA and missing-message findings
remain unresolved; remove redundant reruns when their current comparison ends.
