# High-fidelity research for triple Robust Modem

Prepared 13 November 2026. Research emphasizes the **60.96 m / 200 ft
top-loaded vertical below 9 kHz**, with ground-loop alternatives, and
**passive Earth–Moon–Earth at 5.8 GHz**, following the user's clarification.
The other requested families are groundwave at 1 MHz, groundwave at 30 MHz,
and skywave over 1–30 MHz.

This is a substantial literature synthesis and a foundation for a separate
experimental suite. It contains physical mechanisms, measured and standardized
evidence, recent and contrary findings, candidate implementations and explicit
limits. **No high-fidelity simulator was implemented or run, and no new modem
performance or physical-link qualification is claimed.** Better implementations
and better environmental descriptions may still be found.

## Reading map

| Document | What it supplies |
| --- | --- |
| [Evidence and modeling method](00-evidence-and-modeling-method.md) | Search scope, evidence classes, joint extremes, measurement definitions, model discrepancy and bounded statistics |
| [Sub-9 kHz antenna and propagation](10-sub9khz-antenna.md) | Top-loaded vertical; transmit/receive differences; antenna and ground loss; waveguide physics; electrode/loop alternatives; noise and disturbance evidence |
| [Groundwave at 1 and 30 MHz](20-groundwave-1-and-30mhz.md) | Separate regimes; smooth and mixed ground, terrain and sea; competing propagation mechanisms; recent model-versus-measurement results |
| [Skywave over 1–30 MHz](30-skywave-1-to-30mhz.md) | Stationary baselines, measured spread definitions, O/X modes, NVIS, auroral/polar/equatorial paths, nonstationarity and outages |
| [Earth–Moon–Earth at 5.8 GHz](40-earth-moon-earth-5p8ghz.md) | Passive lunar radar/communication physics, geometry, scattering, beam and polarization effects, noise, an explicitly assumed link calculation and model alternatives |
| [Noise, interference and frontends](50-noise-interference-and-frontends.md) | Natural and man-made noise, burst structure, modern surveys, calibration and analog/digital receiver effects |
| [Native implementation options](60-native-implementation-options.md) | Current-source interface map, actual simplifications, candidate renderers, receiver boundaries, ownership and resource implications |
| [Contained appraisal directory](../../../_appraisal/highFidelity-triple_Robust_Modem/README.md) | Future experiment-code and operating/test-documentation home; currently design only |
| [Proposed experiment protocol](../../../_appraisal/highFidelity-triple_Robust_Modem/experiment-protocol.md) | Scenario records, independent physical checks, small staged pilots, complete-receiver outcomes and separate capacity claims |

Each subject chapter includes an annotated bibliography with direct primary
source or official-document links and access limitations. Papers, standards,
technical reports and practitioner measurements have different evidence roles;
a source count alone is not a measure of completeness. Recent material is
included where found, rather than assigning every regime an artificial 2026
reference. The search is broad and structured, but not an exhaustive systematic
review of every publication or language.

## Main research conclusions

The existing synthetic models are valuable controlled tests. Their purpose
does not require them to be a complete environmental description. A separate
appraisal should preserve these baselines and ask a different question: which
receiver outcomes change when specific observed physical structure is included?
Do not quietly relabel the current `HF Worst` preset as a measured natural
envelope. Current source at `edafea00dcc73163cde2e06cc4b7df09b4eb3b53`
already preserves one continuous channel across the composite DATA, Helper,
silence and tails; it does not supply a site-specific physical propagation
solver. See the [source assessment](60-native-implementation-options.md).

| Environment | Most consequential modeling distinction | What the literature does not yet establish for this modem |
| --- | --- | --- |
| Sub-9 kHz, 200 ft vertical | Antenna/matching/earth losses and usable bandwidth must be considered alongside propagation and noise; an earth-electrode link is a different coupling problem | Efficiency, range and bandwidth of the proposed installation without its actual geometry, loss measurements, power and receiver |
| 1 MHz groundwave | Conductivity, terrain and mixed paths matter; skywave coexistence changes a nominal groundwave experiment | A universal loss/fading distribution or successful weak-signal communication for an unspecified route |
| 30 MHz groundwave | Surface wave must be distinguished from space wave, diffraction and atmospheric contributions; the label alone cannot establish the mechanism | A general worst-case 30 MHz surface-wave channel from measurements at other frequencies or mixed mechanisms |
| 1–30 MHz skywave | Mode availability, polarization, structured motion and scattering change together; local stationary models remain useful within their domain | Worldwide severe-condition coverage, blind acquisition, or a joint distribution obtained by combining unrelated measured maxima |
| 5.8 GHz EME | Two-leg geometry and beam-weighted lunar scattering determine delay, Doppler and received power; bulk travel time is separate from echo dispersion | An exact universal 5.8 GHz scattering kernel, station-specific noise/pointing margin, or triple-modem sensitivity |

Several useful tensions in the literature are preserved rather than resolved
by choosing the more elaborate model:

- **Stationary models versus changing environments:** stationary random gains
  can be appropriate locally, while mode transitions or disturbances require
  different conditional descriptions. Stationary does not mean constant.
- **More physics versus better prediction:** uncertain environmental inputs and
  missing mechanisms can make a detailed model less predictive than a simpler
  empirical one. Validation must decide within a declared domain.
- **Noise climatology versus local recordings:** broad noise models are useful
  references, while antenna, bandwidth, region, season and modern interference
  can change the actual received distribution.
- **Radar evidence versus communication behavior:** measured delay–Doppler or
  radar cross section is valuable, but its illumination, receive beam,
  polarization, estimator and selection threshold must accompany any transfer.
- **Severe propagation versus useful propagation:** irregular structures can
  impair an existing route and can also enable a route otherwise unavailable.
  “More disturbed” is not a universal ordering of reception difficulty.

The relevant evidence and its exact applicability are in the domain chapters.
These are not new numerical modem results.

## What a high-fidelity experiment should preserve

The strongest starting point is a **physically conditioned scenario**, not
independently selected worst values of delay, Doppler, fading and interference.
Use a measured episode or a specified geometry/environment, then account for
unknown inputs and unresolved scattering. Keep measured severe cases,
statistical tails, plausible stresses, adversarial tests and complete outages
as distinct categories.

The three DATA carriers and Helper must see one physical realization, sampled
at their actual frequencies. Their correlation may be partial and time
dependent; neither perfect common gain nor independent per-carrier fading is
a universal default. Keep front-end saturation, antenna/matching response,
interference, oscillator noise and propagation distinct enough to determine
which mechanism caused a failure. A lower received level must not be erased
by automatic waveform renormalization.

The highest-priority receiver outcome beyond ordinary bit errors is physical
completion during a prolonged fade. If the transmitter continues while the
received signal disappears across quiet-scoring apertures, the observations
may resemble an ended message. The suite should measure that ambiguity,
integrity rejection, possible accepted truncation and reacquisition. It should
preserve the existing two-scored-quiet contract and never supply simulated
message length or EOF as completion evidence.

The native implementation already offers a route to a contained suite:
canonical emission, a new independently validated native propagation component
where needed, and the canonical complete stream receiver. Candidate channel
representations include measured replay, conditioned stochastic kernels,
physics plus measured residuals, and detailed solvers for selected subproblems.
No one representation is preselected as universally superior. The
[implementation chapter](60-native-implementation-options.md) explains their
tradeoffs and falsifiers.

## Remaining inputs and research limits

These are unresolved scenario parameters, not prerequisites for reading or
using this research:

- **Sub-9 kHz:** actual carrier/bandwidth, capacity-hat layout, conductor and
  loading-coil losses, ground system/soil profile, transmit power and voltage
  limits, receiving antenna/frontend, intended distance and route.
- **Groundwave:** station coordinates and heights, terrain/land-sea sequence,
  conductivity/permittivity and seasonal variability, polarization and whether
  other propagation mechanisms are intentionally present.
- **Skywave:** geographic/magnetic path, local time/season/solar context,
  supported frequencies and desired NVIS/oblique/multihop scope; simultaneous
  oblique sounding and noise measurements where available.
- **EME:** both station locations, dates/elevation, dish diameter and measured
  pattern/efficiency, polarization and feed loss, transmit power, system noise
  temperature, pointing accuracy, oscillator stability and desired bandwidth.
- **All families:** independent calibrated data for the particular installation,
  credible joint-event probabilities and holdouts suitable for weak-signal
  claims. Published detection-conditioned records do not supply unobserved
  outage tails automatically.

Configured public calendar and keyed ordinal remain the current native
receiver's scope. Unknown waveform epoch/ordinal acquisition needs its own
work and incorrect-address evidence. Published performance of another waveform
or a high-SNR tracker is not this receiver's acquisition sensitivity.
Communication simulations, actual-device qualification and complete-receiver
CPU capacity remain separate claims.

## Provenance and upkeep

Only the requested research and appraisal subdirectories are changed by this
work. Production code, defaults, maintained validation records and manual
capacity cases are unchanged. Validation of these documents consists of source
spot-checking, dimensional/arithmetic review, cross-document consistency and
local-link/format checks; receiver benchmarks are not appropriate for a
documentation-only change.

The research documents serve the concrete purpose of selecting and validating
the future contained appraisal. Keep this synthesis while that decision basis
is useful; revise or consolidate superseded findings when the model is selected
or better evidence arrives. Preserve contrary findings and original limits.
Do not retain redundant raw searches, copied papers, stale builds or abandoned
implementations as an archive. No such experiment artifacts were generated
for this phase.
