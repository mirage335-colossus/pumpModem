# Scalar HF cutoff, reflection and absorption component

This component investigates a gap left by the guarded parabolic-layer
eikonal: what happens to reflected power and phase when a declared plasma
profile approaches or crosses its reflection transition. It is a static,
one-dimensional, normal-incidence, **unmagnetized (`B=0`)** calculation. It is
not a complete modem test or a model of all ionospheric modes. The
[research reconciliation](hf-research.md#absorption-and-critical-frequency-boundaries)
explains why magnetic mode conversion, strong scattering and measured long
histories remain separate requirements. The existing eikonal cutoff guard
remains intact.

## Physical convention

Use electric fields `Re{E(z,omega) exp(-i*omega*t)}`, a nonmagnetic medium and
fixed ions. The scalar cold-electron momentum equation gives

```
omega = 2*pi*f
omega_p^2 = Ne*e^2/(epsilon_0*m_e)
epsilon_r(z,omega) = 1 - omega_p(z)^2/[omega*(omega + i*nu(z))]
E''(z) + (omega/c)^2*epsilon_r(z,omega)*E(z) = 0
```

`f` and any exposed plasma frequency `fp` are in Hz; their angular frequencies
are in rad/s. `nu >= 0` is a momentum-relaxation rate in s^-1, not a frequency
to multiply by `2*pi` again. It is an explicit constructed cold-fluid
parameter. Importing an empirical collision law requires its definition and
energy-distribution assumptions; the same numerical collision rate cannot
automatically be shared between Appleton–Hartree and Sen–Wyller formulations.
[Zawdie et al. (2017)](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1002/2017RS006256)

This sign convention has `Im(epsilon_r) >= 0` for a passive plasma. Choose the
square-root branch with `Im(k) >= 0` and forward propagation `exp(+i*k*d)`.
With vacuum ports, reflected and transmitted power fractions are `R=|r|^2`
and `T=|t|^2`; `A=1-R-T` is absorbed power only because both outgoing ports
are accounted for. Reflection is not absorption. No per-frequency/window
normalization restores power lost to either mechanism.

For this phasor convention, a pure delay has phase `+omega*tau`, so delay is
`d arg(H)/d omega`. A future interface to the appraisal's negative phase-slope
baseband convention must explicitly transform the convention. Reflection
phase and its derivative are not meaningful at a reflection zero; frequency
steps must also resolve phase winding. Do not interpret a phase-wrap artifact
as an extraordinary ionospheric delay.

## Numerical discriminator and independent references

The component forms a symmetric two-port scattering matrix for each
homogeneous midpoint slab from a scaled exact solution, then cascades those
matrices. Its propagators decay in evanescent material; it avoids growing
transfer-matrix products. It uses the analytic zero-index slab limit at exact
collisionless cutoff. Adding artificial collisions to hide a numerical
singularity would change the physical problem.
The implemented zero-index slab limit is `t=2/(2-i*k0*d)` and `r=1-t`,
where `k0=omega/c`. A cascade denominator of magnitude at most 64 machine
epsilons is reported as an unresolved resonance rather than hidden by an
opaque-layer shortcut.

Independent checks must include vacuum propagation, interface Fresnel
coefficients and a finite uniform slab. For a slab with refractive index `n`
between vacuum ports, let `q=(1-n)/(1+n)` and `p=exp(i*k*d)`. The independent
closed form away from its removable zero-index limit is

```
r = q*(1-p^2)/(1-q^2*p^2)
t = (1-q^2)*p/(1-q^2*p^2)
```

Check complex amplitudes, not just power closure; define transmission phase
at the exit plane. Include a passive collisional slab, an exactly critical
collisionless slab and a thick evanescent case. `R+T=1` alone cannot detect a
solver that produces the wrong lossless reflection coefficient.

A smooth lossless reference is `omega_p(z)^2 = omega_pm^2*sech^2(z/L)`.
Writing `K=omega*L/c` and `U=(omega_pm*L/c)^2`, the equation becomes a
Pöschl–Teller barrier. For `U >= 1/4`, the exact infinite-domain result is

```
mu = sqrt(U-1/4)
T = sinh(pi*K)^2/[sinh(pi*K)^2 + cosh(pi*mu)^2]
R = 1-T
```

This follows the independent scattering solution in
[Wehrhahn, Smirnov and Shirokov (1992), equations 17–18](https://www-library.desy.de/preparch/desy/postpr/1992/desy92-035.pdf).
Use moderate dimensionless parameters so the reference does not overflow.
The lower-barrier continuation replaces `cosh(pi*mu)` by
`cos(pi*sqrt(1/4-U))`; the declared test inventory includes both branches.
Finite-domain truncation error must be distinguished from grid error.

## Constructed profiles and convergence gates

`high_fidelity_fullwave_probe` takes no arguments. Its declared inventory has
14 rows at 3.4 MHz, with vacuum reference planes at 0 and 400 km. The F-region
density is proportional to `cos^2(pi*(z-290km)/(2*100km))` on 190–390 km and
zero outside. Its peak plasma-frequency/RF ratio is 0.9999, 1 or 1.0001 in
each of four profile families:

| Profile | F-region collision rate | D-region peak density |
| --- | --- | --- |
| `f-lossless` | 0 s^-1 | absent |
| `f-collisional` | 60 s^-1 | absent |
| `df-quiet` | 60 s^-1 | 8×10^8 m^-3 |
| `df-enhanced` | 60 s^-1 | 8×10^9 m^-3 |

`df-quiet` also has peak ratios 0.97 and 1.03. The D-region density uses a
cosine-squared shape centered at 85 km with 20 km half-width; its collision
rate is `5e5*exp((85km-z)/6km)` s^-1. The D and F supports are disjoint.
Each occupied region uses its own collision rate; vacuum has `fp=nu=0`.
These are constructed profile choices, including the names “quiet” and
“enhanced”; they are not measured days, climatological quantiles or a
calibrated flare-strength relation.

The spatial hierarchy uses 20, 10 and 5 m midpoint slabs, merging uniform
vacuum spans. The central-frequency complex-amplitude convergence gate is a
maximum difference of 0.005 between the 10 and 5 m results. Reflection-delay
reporting additionally requires agreement within 5 microseconds under both
10/5 m grid refinement and ±10/±20 Hz frequency differences, reflected power
at least `1e-8`, and local principal phase differences below `pi/2`.
Unsupported delay estimates are withheld, not coerced to zero. Absolute power
error, passive flux accounting and complex amplitudes remain separate checks.
Convergence is conditional on the declared profiles; it does not establish
how often they occur in nature.

Admission bounds RF to 100 kHz–100 MHz, total extent to 2,000 km and the
slab count to 250,000. Slab plasma frequency is at most 100 MHz and collision
rate at most `1e9` s^-1; the column builder admits D-region peak density at
most `1e13` m^-3 and steps of 1–1,000 m. The solver rejects nonfinite flux
and `R+T > 1+1e-8`; it retains the signed absorption residual for numerical
audit rather than clipping it to manufacture passive closure.

## Scope and result record

A frozen `r(f)` or `t(f)` is not yet a causal time-varying channel. Turning
snapshots into a long received waveform requires a validated wideband,
causal operator with state and memory; changing a per-sample frequency
response is insufficient. This component does not render HF messages,
qualify receiver sensitivity or compare modem protocols. It has no O/X
splitting, oblique incidence, mode conversion, three-dimensional diffraction,
caustics or evolving plasma. The
[_ref model alternatives](../../_ref/highFidelity-triple_Robust_Modem/Fable-5.2/33-hf-channel-models-and-simulators.md)
retain those distinct questions and the limits of ray/scatter approximations.

The 14 November 2026 [probe receipt](local-evidence/hf-fullwave-cutoff/stdout.json)
and [execution receipt](local-evidence/hf-fullwave-cutoff/execution.json) bind
this component run to the following identities. The commit is the base;
content hashes identify the built working-source state.

| Identity | Recorded value |
| --- | --- |
| Base commit | `13db435fc85114be2e2a0298580d5c3e2bcba996` |
| Appraisal source SHA-256 | `0505c94964ed1b82a66ab699272a896c95bdbb27a1a50f6243bef888e4a84b07` |
| Canonical native source SHA-256 | `1749313fcabbf16527ac687c5cf4947acde3d00dca6bcd364b87d453b6d79a16` |
| Full-wave probe SHA-256 | `5605c4b6cdd549243be60d90a36c82b5f191db7a3ecdbda10461466d3de50c09` |
| Build | GNU 14.2.0, Release |

All 14 central-frequency complex responses passed the declared mesh gate;
the largest 10-to-5 m change was `2.5546755e-5`, below 0.005. Thirteen
reflection delays passed the local mesh/frequency gates. The remaining
`df-quiet`, peak-ratio 0.97 row withheld delay because reflected power was
`3.3536848e-17`, below the declared threshold. Its null is an admissibility
result, not a zero-delay observation. Near-cutoff resolved delays were
3.704–3.914 ms; the ratio-1.03 quiet row gave 2.439755 ms. The process exited
successfully after 0.429834 s externally measured wall time, within a 60 s
cap; the probe's own elapsed time was 0.427132 s. No receiver executed.

The finest-grid power fractions at peak ratio 1 are:

| Profile | Reflected | Transmitted | Absorbed |
| --- | ---: | ---: | ---: |
| `f-lossless` | 0.4999765 | 0.5000235 | −6.24e-14 residual |
| `f-collisional` | 0.4376636 | 0.4377047 | 0.1246317 |
| `df-quiet` | 0.2134649 | 0.3056851 | 0.4808501 |
| `df-enhanced` | 0.0002987622 | 0.01143599 | 0.9882652 |

Across ratios 0.9999, 1 and 1.0001, lossless reflected power changed from
0.0546547 through 0.4999765 to 0.9453282. The largest absolute lossless
absorption residual over all grids was `4.41e-13`; its signed residuals were
retained. With F-region collisions alone, absorption was
0.1137360, 0.1246317 and 0.1137488: even this small profile family has a
nonmonotonic absorption response to increasing peak density. The enhanced
D-region absorbed 0.978077–0.998162 across the same ratios and strongly
suppressed the reflected port. Density changes therefore alter the joint
reflection, transmission, phase and loss response. Their effect cannot be
read from a single scalar attenuation multiplier.

The wider quiet controls retained useful accounting at both ends: ratio 0.97
had transmission 0.667772 and absorption 0.332228 despite negligible
reflection; ratio 1.03 had reflection 0.465543 and absorption 0.534457, with
transmission underflowing to zero numerically. That zero does not prove
mathematically zero transmission. These constructed vertical-column results
do not establish communications outage probabilities or loss on an oblique
ground-to-ground link.

The initial suite run passed 21/22 groups and failed full-wave assertion 4,
the exact-zero-index reflection limit (absolute error 1.68616). Its stale
independent-reference sign was corrected consistently with the boundary
equations above; the [original failure log](.build/hf-coverage-checks-initial.log)
is preserved. The subsequent [22-group run](.build/hf-coverage-checks.log)
passed in 81.52 s, including
[36 full-wave assertions](.build/hf-coverage-ctest-details.log). A later
[focused integration/runner check](.build/hf-coverage-final-checks.log) also
passed. These software checks and the 14 static component rows retain
separate denominators from complete-receiver communication trials.

Keep the receipts and initial/final check logs while this numerical
qualification and its cutoff-related review remain active. Once superseded,
retain the applicable equations, identities, limits and conclusions here and
remove redundant generated outputs under the appraisal's retention policy.

## Frozen waveform-kernel feasibility discriminator

The next component is an admission test for a quasi-static waveform operator,
not yet a modem campaign. Freeze two geometries before execution: a D/F
trajectory from peak F/RF ratio .9999 to 1.0001 while D-region peak density
rises from 8e8 to 8e9 m^-3, and the same D change at fixed F/RF ratio 1.03.
Each has two endpoints and an independently solved midpoint. All other column
parameters retain the static solver's declared meaning.

Use 1920 complex samples/s, an occupied band of +/-768 Hz and a C2 guard taper
to Nyquist. Conjugate the solver response to the native baseband convention.
Report the reference-plane bulk delay and the additional 32-sample numerical
delay explicitly. That added delay is not an ionospheric measurement and would
need to be included in public arrival bounds before any communications test.
Retain a common causal tap support and continuous source history, including
quiet input and kernel changes. No gain normalization restores reflected loss.

Frequency sampling proceeds through 256, 512, then at most 1024 bins, with
cached dyadic 5 m spatial solves. Six profiles each permit up to 1024 such
solves, 256 matching 10 m base-grid solves, and 65 independently placed
off-grid frequencies at both spatial resolutions: at most 8,460 solves under
a 9,216-solve ceiling. Stop at a qualified consecutive frequency refinement;
otherwise report the final unresolved gates. A single probe has a declared
180-second external wall limit. No adaptive parameter selection follows a
communications outcome.

Separate outputs must report mesh error, occupied/guard-band reconstruction,
independent off-grid reconstruction, finite-band pre-arrival energy, discarded
pre/tail energy, frequency refinement, and exact-midpoint versus interpolated
profile response. The engineering gates are .005 maximum complex error and
1e-6 discarded relative energy. Analytic delay/slab references and streaming
chunk/tail tests precede this inventory. Finite-band precursors alone are not
proof of physical acausality; deleting them is acceptable only if the resulting
declared approximation independently meets its response/error gates.

Even a numerically admitted kernel is qualified against frozen scalar profiles,
not a truly evolving dielectric. Output-time kernel interpolation is causal
as implemented, but its physical approximation needs slow evolution relative
to propagation and kernel memory, and independent profile interpolation checks.
Static passivity does not prove energy conservation during kernel changes.
Do not turn an unresolved near-cutoff interpolation into an invented deep-fade
waveform. Keep receipts in `local-evidence/hf-repair-validation/` while this
feasibility question is active; remove redundant output after its conclusions
are retained here.

The first waveform-kernel probe completed in 24.822 seconds with 5,388 RF
solves and exit 1 (a valid unresolved-model result). All twelve frozen
port/profile kernels passed their static reconstruction, spatial mesh,
discarded-energy and consecutive-frequency-grid checks at N=512. Increasing
the frequency grid is not the missing remedy. Interpolating the endpoint
responses failed at the independently solved physical midpoint:

| Physical trajectory | Reflection max complex error | Transmission max complex error |
| --- | ---: | ---: |
| Coupled near-cutoff F and D change | .41270 | .81377 |
| D change at fixed F/RF=1.03 | .39229 | Below absolute tolerance at numerically minute transmission |

The common modeling-delay phase does not change occupied-band magnitudes of
these errors. The comparison never extracts a wrapped phase; a phase branch
cut cannot cause the rejection. Subtracting endpoint reconstruction error
still leaves .41265, .81363 and .39211 on the three substantial paths.
The finite-band transmission power in the opaque control underflows to zero
while its complex amplitudes remain nonzero; do not interpret that as exact
zero transmission or relative accuracy. No dynamic modem trial uses these
rejected interpolations.

Freeze a separate global profile-axis discriminator before further results:
`high_fidelity_fullwave_kernel_probe --profile-refinement`, with a 180-second
external wall limit. Keep both physical trajectories over their full interval,
and use exactly five RF offsets (-864, -768, 0, 768, 864 Hz), including occupied
edges and guard points. At 16, 32, 64, 128 and at most 256 equal profile
intervals, solve every knot and every independent midpoint at both 5 m and
10 m spatial resolution. Cache nested exact profile coordinates. The hard
inventory is 513 profiles × two trajectories × five frequencies × two
meshes = 10,260 RF solves under a 10,300-solve cap.

Compare direct complex endpoint interpolation with independently solved
arithmetic physical parameters. Stop at the first look where all path/port
interpolation and mesh errors pass the unchanged .005 gate, otherwise retain
the final unresolved result. This identifies required profile spacing at five
fixed frequencies; even a pass does not qualify full-band FIRs, arbitrary
inter-profile curvature, temporal slowness, evolving-plasma physics or a
complete communications channel. Do not silently substitute a shorter local
trajectory for this full-interval test.

That discriminator completed in 41.404 seconds with the full 10,260 solves.
Both spatial meshes agreed within 2.009e-5 at every tested knot and midpoint.
The physical-profile interpolation error converged strongly but did not pass
both reflection ports at the declared maximum:

| Profile intervals | Coupled cutoff reflection | Coupled cutoff transmission | Fixed-F reflection |
| --- | ---: | ---: | ---: |
| 16 | 1.065164 | .591023 | .997472 |
| 32 | .422733 | .171479 | .363819 |
| 64 | .119487 | .044761 | .100879 |
| 128 | .030986 | .011348 | .026038 |
| 256 | .007842 | .002852 | .006582 |

The largest reflection errors occur at the -864 Hz guard point, near the
low-D end; that guard is part of the declared test. At 256 intervals the
coupled transmission port passes, while both reflection errors still exceed
.005. The observed near-quadratic refinement trend distinguishes a resolvable
profile interpolation error from a spatial-solver floor, without granting an
unexecuted finer grid a pass. The experiment stops at its declared cap with
exit 1 and preserves the remaining full-band, causal and slow-evolution gates.
No parameter span, frequency set or tolerance was reduced to obtain admission.
