# Usable noise and equipment scenarios

- **Part of.** [High-fidelity weak-signal channels for the triple Robust Modem](README.md). Shared units and evidence grades are in [00](00-method-and-modem-context.md); physical noise evidence is in [50](50-noise-and-interference-mf-hf.md), and receiver/transmitter effects in [55](55-equipment-and-front-end-effects.md).
- **Status.** Research specification, prepared 13 November 2026. These cases are model inputs and discriminators, not validated environment probabilities or completed modem trials.
- **Scope.** External noise, interference, and equipment effects from source/reference plane through digitized receiver input. They complement the five propagation families and do not specify propagation or a transmitter authorization.

## 1. Rules for using these cases

Keep measured environment, standardized reference, calculated case, and deliberate stress input labeled separately. Do not assign natural occurrence weights unless a cited data set supports that population, antenna, threshold, period, and estimator. A thresholded channel-occupancy fraction is not noise PSD and is not the probability that this modem decodes. A good-channel or empty-allocation selection is a conditional case, not a representative average. Preserve unsuccessful searches and quiet-slot completion in later receiver tests.

Each run records carrier and occupied bandwidth; antenna/site and polarization; temperature or external-noise condition; source and scenario identity; sample rate; absolute UTC and keyed sample ordinal; all gains, filters and AGC/blanker settings; calibration date and uncertainty; reference-plane units; initialized state and seed; gaps and clipping; and the public receiver configuration. Keep the evaluator's injected components and true state unavailable to receiver decisions.

## 2. Reference planes and stage order

Use one declared signal path and report every conversion:

1. **Environment plane:** incident electric field in dBµV/m, spectrum/APD, bandwidth and polarization, or source waveform with calibrated field. This is the appropriate input for field-probe and occupancy reproduction.
2. **Antenna/feed plane:** antenna factor, pattern, polarization, effective height or gain, mismatch, cable/feed loss and temperature. Report available antenna-terminal power in dBm and PSD in dBm/Hz only after conversion with the documented antenna and impedance convention. Preserve external signal and noise together through passive antenna loss.
3. **RF chain:** preselector → passive loss → LNA and its added noise → gain/attenuation → nonlinear blocker/compression and intermodulation → mixer and LO phase noise/reciprocal mixing → IF/channel filters and delay → AGC/blanker/noise reduction → ADC clip/quantization → calibrated complex samples. Every susceptible stage, including the LNA, mixer and ADC, can overload or produce intermodulation before later filtering; one downstream lump cannot represent all such effects. This is a representative explicit SDR chain, not a universal transceiver schematic. If hardware has a different ordering, use its schematic and calibrate it as built.
4. **Digital modem plane:** complex I/Q units, calibrated full scale, sample clock, timestamp/ordinal mapping, validity mask and persistent filter/state history. Add no receiver noise twice: if samples already include receiver noise or front-end response, mark those components as included.

Interferer fields/powers are reported at the same upstream plane as the wanted signal, with carrier offset, occupied spectrum, waveform/envelope, polarization, duty schedule and source location when known. A digital filter cannot undo an upstream overload. Record where each blocker is rejected and measure compression, reciprocal-mixing noise, IM products and ADC overrange before interpreting a decode failure.

## 3. Stateful initialization and dependence

For every colored or stateful component record a state definition, covariance or measured cross-spectrum, initialization method, and seed. A stationary colored process starts from a draw from its stationary joint distribution (or an explicitly recorded reference-record start with sufficient prior context); it does not start at zero and call the resulting transient an environment. Define shared latent drivers where evidence supports them: e.g. one storm/activity envelope can modulate atmospheric-noise power across nearby frequencies; the RF chain sees the same blocker ensemble and AGC state across Helper and DATA; a shared oscillator drives their phase coherently. Keep cross-frequency, antenna-elevation, and polarization correlation unspecified where evidence is absent, then compare explicit alternatives rather than asserting independence. Advance state across arbitrary chunks, quiet slots, and observation gaps; a missing interval is invalid data, never a zero signal. AGC starts at a declared settled operating point or a deliberately measured acquisition transient.

For receiver-added thermal noise, specify each stage's equivalent noise temperature/noise figure and temperature, input bandwidth, and gain/loss before summing referred contributions. For a measured record, do not add synthetic noise or filters already present in the record. Preserve the external-noise estimator (mean-square, RMS, APD percentile, or floor estimator) with every quoted number.

## 4. Scenario set

These cases are conditional anchors. Values taken from ITU or field studies retain their own estimator and geometry; values from equipment tables retain model, setting, reference plane and test bandwidth. The rows intentionally do not combine separate marginal maxima into a purported observed extreme.

| Case | Input and state | Use and limit |
|---|---|---|
| **N0: linear calibration/control** | No external interferer; inject a calibrated complex tone/noise source at a named antenna-terminal or digital plane. Record density in dBm/Hz, integrated power in dBm, equivalent noise bandwidth, and I/Q full scale. For RF path checks also inject a traceable field or connector source with measured antenna/feed transfer. | Verify gain, noise bandwidth, phase, delay, sample scale and clipping thresholds independently of a natural environment. This is a controlled calibration, not a quiet-rural claim. |
| **N1: conditioned P.372 background** | For 0.3–30 MHz choose a named frequency, Northern Hemisphere season/local-time block, site category and antenna assumption. Choose explicitly the pinned reference-code approximation, fixed component-median powers, or literal positive random component powers with a declared joint law (50 §4); then report Fa (dB above kT0), equivalent N0 (dBm/Hz relative to a lossless reference antenna), and integrated power over the receiver's stated ENBW. Retain the standard's component variability separately. | Standardized reference only. P.372-18 atmospheric maps retain older underlying observations; medians/deciles are not a target-site measurement. Do not attach a cross-band correlation or event probability not provided by the source. Compare independent versus common slow-power drivers as sensitivity alternatives. |
| **N2: impulsive noise conditional record/model** | Use a full calibrated time record or a source-specific model with measured pulse waveform, amplitude distribution, width, arrival clustering/repetition, duty fraction, observation bandwidth, and censoring. State whether APD is measured before or after front-end filtering. Preserve pulse history and shared slow envelope across chunks. | Select only a bandwidth/source supported by the evidence. The 1962 NBS paper includes a measured 0.64-Hz APD, but its bandwidth transformation had incomplete validation and no temporal law. The distinct 1985 Vd conversion below 6 Hz remains extrapolated. Neither establishes sub-Hz impulse timing/covariance. Narrow filtering may smooth individual pulses but does not guarantee Gaussian tails or independent samples when arrivals cluster or share flash/storm activity. Independent single-sample impulses are a stress control, not a faithful burst or lightning process; cited clustering/burst evidence is limited to 10 Hz–60 kHz/VLF and does not define an HF or sub-Hz rate model (chapter 50, refs. 44–46). |
| **N3: HF allocation congestion, Cyprus-conditioned** | Reproduce the 2022 study's observation definition if comparing its result: calibrated HE016 low-angle monopole, R&S EM 510, 1 kHz filter, 2 kHz channel spacing, 100 ms dwell, 1.606–30 MHz and chosen incident-field threshold (−20…45 dBµV/m); calculate allocation fraction Q. Keep day/season and quiet-geomagnetic scope. | Observable is thresholded allocation congestion. It is not PSD or modem 250 Hz-slot occupancy. A waveform blocker case needs separately measured/replayed emissions; absent those, label each constructed blocker and duty schedule as synthetic stress, with no natural occurrence weight. Never convert Q directly to packet/slot loss. |
| **N4: measured floor estimator, Cyprus-conditioned** | Reproduce the distinct 2022 SAL-12 campaign definition: antenna azimuth, allocation, 1 s dwell, 10-minute scan, average of lowest 10% of frequency readings in each sequential scan, filter bandwidth/switch sequence, and campaign period; carry readings as dBµV/m under that estimator. Record antenna orientation and scan-order timestamps. | Checks diurnal/directional/bandwidth estimator sensitivity over a 23-day azimuth campaign and 9-day bandwidth campaign. This does not supply annual statistics or a modem-antenna PSD; do not pool with N3 as if simultaneous or colocated. |
| **N5: HF blocker and front-end stress** | Add one or more explicit upstream interferers at calibrated antenna-terminal power (dBm), with offsets, bandwidths, modulation, phase/noise, pulse timing and duty. Choose device-specific AGC, filter and ADC parameters from chapter 55 or measured hardware; initialize and carry AGC/filter/oscillator state. Include a blocker below and above the measured compression/clip point as separate fixed cases. | Deliberate robustness stress unless a matched field record establishes its occurrence. Measure wanted gain/phase, compression, IM products, reciprocal mixing and overrange at each stage. A software-only post-ADC tone cannot test pre-ADC blocking. |
| **N6: receiver-noise-limited comparison** | At the same calibrated antenna-terminal plane compare external noise with receiver-added noise using stated temperature, preamp state, noise figure, loss and ENBW. Include an external-noise-dominated case and a receiver-noise-dominated case only where the antenna/site/device combination supports them. | Prevents applying P.372 external-noise medians where equipment noise dominates. 5.76 GHz system-temperature rows and MF/HF receiver NF tables have different bands and hardware; do not use them as one continuous frequency curve. |

The full [NTIA TR-25-579](https://its.ntia.gov/publications/download/TR-25-579.pdf)
was retrieved and inspected during completion after the web reader's size limit
and earlier sandbox DNS failure. Chapter 50 gives the exact Appendix G medians
and pages. Its seven Antarctic-area sites, 3–30 MHz antennas and site-specific
noise/occupancy comparisons are now a usable source-conditioned reference.
They do not define a joint waveform law: minute-spaced PSDs, 7.647 kHz ENBW,
manual gain, overload exclusions, theory/manufacturer-based antenna corrections
and incomplete observation periods constrain transfer. In particular, BITF
lost 00:00–04:30 observations to overload. These are external-noise factors,
including natural noise, not pure residential man-made-noise factors.
Selected methods/results within the full 107-page NTIA 93-293 scan are also now inspected (pp. 2–5, 11, 80–85): its five selected
one-second cases include man-made impulse models and explicitly omit atmospheric
waveform modeling. Its high-threshold discrepancies argue against using its
burst parameters as a universal lightning law. Both sources inform the
measurement/validation route; neither calibrates C1's deliberately constructed
arrival and energy distribution. The 2026 Ofcom indoor model remains outside
the HF domain (80 MHz–2 GHz).

### Executed evidence checks and remaining calibration

The pinned P.372 library and independent positive-sum calculation in 50 §4 establish a consequential branch distinction: at 30 MHz, January, 08 UTC, 50°N 10°E, quiet rural, code Fa is 17.916115 dB, below its galactic component 18.026211 dB; a declared independent asymmetric-lognormal positive sum instead gives 19.45085 dB. N1 must not combine these meanings or call either a measured site median. The official code has integer-hour/longitude conventions and no foF2 screening; a physically conditioned sky model needs screening and an antenna response separately.

Public ENAMS plot arrays now support an actual24-hour/two-site processed-observable and cross-site/cross-band dependence analysis (50, public-products subsection), while their 50-kHz plotted frequency spacing, 305-Hz analysis bandwidth and hourly aggregates do not supply 250-Hz occupancy or subsecond waveform covariance. NTIA's inspected publication/data pages supply the two reports but no linked raw-IQ archive for those campaigns; no raw-record reanalysis is claimed. Original 1962 narrowband APDs, 2015 impulse-event metrology, directional atmospheric-noise modeling and manufacturer equipment sheets narrow the model choices without calibrating one joint natural process. The public 2016 lightning archive was also inspected and its LMA rows parsed (50, event-specific alternatives): source geometry/quality and selected waveforms are available, but do not constitute continuous background-noise or sub-Hz calibration.

A target natural episode requires one common manifest: site/antenna/ground geometry; UTC and sample-clock trace; calibrated field or matched-port scale; full complex chain and settings; frequency/ENBW/detector; weather/ionosphere/activity state; emission labels or reproducible excision; overrange/gap mask; and prehistory. Validate each source observable at its original aperture before deriving modem-filter APDs, envelope covariance, line dwell, overload recovery or cross-band dependence. Hourly percentiles, plot minima and thresholded arrival counts are different random variables. No single studied source contains all manifest fields; unsupported independence and shared-driver extremes remain explicitly labelled sensitivity branches. Completing target measurements is E, and implementing/running the native C0–C4/end-to-end tests is I; neither is disguised as an already performed literature calculation.

### Calibration, identifiability and physical holdout (S48)

Before fitting, declare the consumer and observable: port PSD after calibrated antenna/receiver subtraction, bandwidth-conditioned APD, threshold-conditioned pulse arrivals/marks, cross-frequency covariance, or complex hardware transfer. Save the corresponding measurement equation and units. Do not estimate a raw atmospheric process from a floor-excised spectrum, an AGC output from an unknown gain history, or missing-sample probability from a stream that already interpolated gaps. If both antenna gain and incident field are unknown, their product is identifiable but the two separate factors are not; an independent calibration is necessary.

Use complete physical episodes as the split unit. Keep all windows from one storm, mains event, oscillator warm-up, device session or continuous capture together. Choose training episodes for parameter fitting; a distinct development set for model/threshold choice; and locked validation episodes from different days, with a separate site/device holdout when transfer is claimed. Record the split before looking at validation outcomes. The one-day/two-site ENAMS check cannot supply independent-day training, development and validation sets; it supports the descriptive processed-observable statistics already reported. Do not manufacture independence by changing a seed, overlapping windows, frequency bins or resampling one event. When data permit only a single event, report a source-conditioned reproduction, not an empirical population validation.

Fit PSD/transfer with calibration uncertainty separated from stochastic variation; retain covariance of frequency-bin estimates. Fit pulse marks and interarrival laws jointly with threshold, dead time, saturation and censoring; retain a missing/overrange state rather than truncating a tail and calling it complete. Compare at least a simple Gaussian/Poisson or fixed-gain baseline with the selected clustered/colored/stateful law on held-out APD quantiles, envelope autocovariance/PSD, interarrival survival, threshold crossings, and recovery gain/phase. Physical-model rejection uses discrepancy against declared measurement uncertainty and multiple observables, not an arbitrary universal dB threshold or agreement in one marginal. Check sensitivity to ENBW, integration duration and excision threshold; an unidentifiable parameter stays a bounded sensitivity axis rather than receiving a fictitious confidence interval.

Keep source-parameter uncertainty, model discrepancy, numerical approximation and modem stochastic trials separate. Propagate supported parameter intervals or fitted joint draws through the frozen native simulator; do not infer occurrence weights for synthetic extremes. Preserve correlated environmental variables and the clock/AGC state shared by Helper and DATA. Complete-receiver probabilities and the single predeclared H1 confirmation use [86](86-statistics-and-calculation-checks.md); the 12 wanted plus six control pilot is a failure-finding comparison, not an IID population sample, and does not fit a natural joint law. No target records have yet passed this calibration/physical-holdout protocol.

## 5. Closed mathematical controls for implementation

The N0–N6 rows above describe evidence-conditioned families. They are not all
executable specifications: a calibrated record and a measured receiver cannot
be replaced by an instruction to choose them later. The controls below close
those choices for an implementable mathematical comparison. Their numerical
laws are **[E] engineering assumptions**, not inferred lightning, occupancy,
storm or hardware populations. Use them alongside, not as validation of, the
measurement-conditioned alternatives. Better fitted laws remain possible.

### Common signal and normalization

Use the source, codec, calendar, randomness and full-message observation contract
in [00, section 7](00-method-and-modem-context.md#7-reproducible-source-and-evidence-contract).
For these broadband front-end controls select the HF waveform (rate 36, added symbols 10, FHSS off, requested 16 s; actual 23.2 s active geometry). Use `Fs=48000 samples/s`, the physical-band
IF branch and the actual returned Helper plan; reference run has Helper off.
The channel/equipment input is a **complex power wave**: `E|x|²` is watts at a
matched antenna-terminal reference plane, not the sum of two independent RF
polarizations. A complex proper white sequence with density `N0` W/Hz has
`E|n|²=N0 Fs`, shared equally by its real and imaginary parts. Density refers to
the full complex interval `[-Fs/2,Fs/2)`; the corresponding physical real passband
has the same available power. This convention prevents a hidden 3 dB change.

The reference density is −164 dBm/Hz (`3.981071706e−20 W/Hz`), a chosen flat
reference matching the old simulator, not a P.372 site prediction. Set the
unfaded wanted reference to `C/N0=20 dB-Hz`, hence `C=3.981071706e−18 W`.
Use the native deterministic DATA power reference and exact multiplier
`sqrt(C/(mu*G²))` from 00, applied once to the whole emitted composite. Do not
renormalize each received slot or realization, or divide a normalized Emission
by sqrt(mu) a second time. Report that DATA reference, its bounded-estimate
uncertainty, Helper ratio and actual signal energy separately when ramps,
waits or hopping change occupancy. Other propagation chapters may substitute
their explicitly stated C and N0 with this same normalization.
The sub-9 kHz magnetic-field comparison uses its own fT/√Hz plane; it must not
silently acquire this watts conversion.

The reference epoch is `2025-03-20T00:00:00Z`. Use the domain-separated seed
construction in 00. The independent diagnostic roots there test repeatability;
the probability design of [86](86-statistics-and-calculation-checks.md) governs
any probability claim. All schedules below use elapsed physical time from the
first active DATA support, including the prehistory needed by each component.

For C0–C4 use explicit public search settings: profile CFO bound 100 Hz,
arrival interval `[0,.2] s`, spread bound 1 ms, delay excursion zero,
relative clock bound 10 ppm and `audio_pair_ppm=10`. Receiver settings are
`frequency_bound_hz=100`, `spread_delay_seconds=.001`,
`clock_bound_ppm=10`, `endpoint_clock_bound_ppm=5`,
`delay_excursion_seconds=0`, `time_varying_channel=true`,
`time_varying_common_doppler_hz=1`, `time_varying_atmospheric_hz=0` and automatic
native coarse precision. Only C4 injects the declared ±5 ppm receive-clock
error; the wider public support is not a second random impairment. These
search choices are not a guarantee that every OU realization is contained or
that nonlinear equipment fits the native receiver's model. Preserve failed
acquisition/out-of-model outcomes. Tail/halo allocation covers the entire
selected FIR/delay support; AGC recovery continues through the observation
without being mistaken for signal-path delay. For C3 the static public
noise/gain calibration uses the chosen pre-blocker linear filter and initial
gain g0 below (`noise_variance=Npre·ENBW·g0²` and the matching deterministic
DATA basis gain). It is not updated from evaluator-known impulses, fades or
future AGC state. Receiver sensitivity to that calibration mismatch is an
observable of the experiment.

### C0: Gaussian background and passive-noise accounting

The ideal control is the wanted composite plus proper white external noise at
the stated N0, with no added equipment effects. The passive comparison has
linear power loss `L=2` (3.0103 dB), physical temperature 290 K, followed by an
amplifier of unit signal gain and input-equivalent added temperature 100 K.
Use exact `k=1.380649e−23 J/K`. At that amplifier input the external noise is
`N0/L`, loss noise is `k·290·(1−1/L)`, and amplifier noise is `k·100`, all W/Hz;
the wanted signal loses the same factor L. These are three independent proper
Gaussian components. A second fixed case uses external antenna temperature
20 K (`N0=k·20`) to expose receiver-noise dominance. The 20 K case is a
mathematical comparison, not an HF sky-temperature claim; keep wanted C unchanged between the two noise-temperature cases. Do not add these
temperatures to a measured system temperature that already includes them.

### C1: equal-power independent and clustered impulses

Use a real causal double-exponential shape with rise constant `τr=50 µs`,
decay constant `τd=500 µs` and finite support `T=5 ms`. At this Fs define
`q[k]=exp(−k/(Fs τd))−exp(−k/(Fs τr))`, `0≤k<240`, zero elsewhere,
and normalize `p[k]=q[k]/sqrt(sum(q²)/Fs)` so each unit-energy discrete pulse
obeys `sum(|p|²)/Fs=1`. Round each continuous arrival to the nearest sample,
ties upward; this quantization is part of this digital control. It is not
claimed to preserve an unmeasured analog sub-sample pulse shape.

Compare three fixed branches: no impulses; independent Poisson arrivals at
8/s; and Poisson parents at 2/s, each with four children at offsets
`{0, 0.003, 0.011, 0.029} s`. Draw parent gaps by `−ln(U)/λ` and each child
phase independently uniform on `[0,2π)`. Parent/child sharing is a deliberately
simple cluster law. Start the stationary parent process before the first
needed sample by `T+0.029 s`; generate a Poisson count in this prehistory and
uniform conditional positions, then exponential gaps forward from the boundary.
This retains every finite-support pulse contributing to the observation and
does not create an artificial empty first interval.

Choose mean impulsive power ratios `ρ∈{1,10}` relative to `N0 Fs` as separate
fixed cases. The mean child energy is `Jbar=ρ N0 Fs/8 joules`. Each child has
energy `J0=Jbar/1.99` with probability 0.99 or `100 J0` with probability 0.01;
its contribution is `sqrt(J) exp(jφ) p[k]`. Independent phases make the expected
cross terms zero even where pulses overlap. Both arrival models therefore
have the same mean power and mark law, allowing clustering to be the tested
difference. These probabilities belong only to the constructed control.
The source domain is wideband at Fs; changing Fs requires preserving the
declared physical bandwidth and pulse energy, not silently reusing `N0 Fs`.

For a deterministic nonstationary sensitivity, multiply both background and
impulse amplitudes by `10^(a(t)/20)`, with `a(t)=10 sin²(πt/1800) dB` for
`0≤t≤1800 s` and zero outside. Compare that common driver with a branch that
applies it only to the impulses. No recurrence rate or measured cross-band
dependence is assigned to this one constructed episode. Pass each complete
waveform through the same equipment chain; do not fit its post-filter APD
and then add a second independently fitted impulse process.

### C2: continuous, mains-like and swept interference

At the antenna-terminal plane, add complex tones at offsets +1000 and +2000 Hz
relative to DATA centre, each at either −40 or −20 dBm as two separate cases,
independent initial uniform phases and constant instantaneous frequency. Their
third-order product `2f1−f2` falls at DATA centre. These powers deliberately
stress the constructed chain; they are not measured HF occupancy levels.
The component-control on interval is `[60,120] s`, with 10 ms raised-cosine amplitude ramps inside its ends and zero elsewhere. The separate full-message challenge substitutes the single interval `[tm,tm+60] s`, where tm is the middle active slot's DATA-support start; it does not also retain the component interval. Report the actual overlap with DATA and quiet apertures.

A distinct mains-like control puts a single line at `ν0=0.01 Hz` with phase
`2πν0 t + 0.02 sin(2πt/10)` radians and power `C` (0 dB relative to the
unfaded wanted reference). A swept control at the same power crosses offsets
−150 to +150 Hz linearly in 30 s, restarts every 60 s with 0.1 s cosine ramps
and silence in the second half of each period. Define phase by integrating
the linear frequency on each active interval, beginning each interval at the
same seeded phase. These are reproducible interference discriminators, not
calibrated mains harmonics, radars or communication emitters. They remain
separate branches; simultaneous maxima are not labeled an observed worst case.

### C3: explicit front-end memory, clipping and quantization

After the optional passive/noise stage, apply the following frozen constructed
chain in order. All amplitudes retain the square-root-watt convention:

1. A complex one-pole preselector, `v[n]=(1−a)x[n]+a v[n−1]`, with
   `a=exp(−2π·3000/Fs)`. Its exact discrete ENBW is
   `Fs(1−a)/(1+a)` Hz for the complex full interval. This broad scalar filter
   is a control with known gain/phase, not a measured SSB transceiver filter.
2. Compression `z=v/sqrt(1+|v|²/Psat)`, `Psat=1e−6 W`.
   Compare AM/PM off with phase factor
   `exp(j·0.1 |v|²/(Psat+|v|²))`. This bounded memoryless response defines its
   own distortion; it is not a quoted P1dB/IP3 model of a commercial receiver.
3. Optional oscillator frequency error f, an OU process with stationary
   standard deviation 0.05 Hz and correlation time 10 s. On each sample,
   `f[n+1]=b f[n]+0.05 sqrt(1−b²) Z[n]`, `b=exp(−1/(10 Fs))`,
   initialized `f[0]~Normal(0,0.05²)`. Start phase uniform on `[0,2π)`;
   linearly interpolate f between samples and integrate exactly with
   `φ[n+1]=φ[n]+π(f[n]+f[n+1])/Fs`. Multiply the whole composite by
   `exp(jφ)`, once, defining `w=z exp(jφ)`. This is residual mixer-frequency noise in hertz, not a
   physical fractional-frequency law to extrapolate across RF carriers.
4. A power detector `d[n]=ad d[n−1]+(1−ad)|w[n]|²`, with
   `ad=exp(−1/(Fs τd))`, `τd=0.01 s` when input power exceeds the old d,
   otherwise 1 s. Desired amplitude gain is
   `g*=clamp(sqrt(1e−8/max(d,1e−20)),0.01,1000)`.
   Update `g[n]=ag g[n−1]+(1−ag)g*`, `ag=exp(−1/(Fs τg))`, with τg=0.02 s when g* is below the previous gain and τg=2 s otherwise. The ADC input is `g[n] w[n]`; no second phase multiplication occurs.
5. Quantize real and imaginary components separately with 16-bit midrise
   quantization, full-scale A=`sqrt(1e−6)`, step `Δ=2A/65536`,
   `Q(u)=Δ(floor(u/Δ)+1/2)` clamped to `[-A+Δ/2,A−Δ/2]`.
   No dither. Count pre-clamp overrange separately. Compare against an
   unquantized/unclipped branch with the same upstream state.

For C3 use **explicit settled approximation**, not a claimed exact stationary
nonlinear state: start filter=0; detector `d0=Npre·Fs(1−a)/(1+a)` where Npre is the sum of the selected preselector-input densities (C0 with loss/noise if enabled, otherwise N0); and gain `clamp(sqrt(1e−8/max(d0,1e−20)),0.01,1000)`. This deterministic starting approximation ignores nonlinear compression and impulse power; the burn-in check tests that dependence. Run a 60 s noise/interference prehistory
before the earliest retained source support. Retain the final states and label
this finite burn-in. The impulse process and oscillator run throughout it;
the OU frequency itself has stationary initialization. A coupled 120 s
burn-in comparison uses the identical physical innovations, pulse/tone history and oscillator increments from −60 s onward, adding only the earlier 60 s prehistory; it must show that the reported observables are insensitive to
this initialization: compare retained-window mean output power within 0.1 dB, detector/gain median and 99th percentile within 1%, and ADC-overrange fraction within 0.001 absolute. These are declared numerical comparison gates, not proven measurement uncertainties or receiver-loss tolerances. Otherwise report state dependence and redesign the initialization before a qualified campaign. The fixed 60 s branch remains uniquely defined even when its convergence check fails. Never reset the filter,
detector, gain or oscillator at DATA, Helper, chunk or quiet-slot boundaries.
After source completion, emit the full physical/receiver quiet apertures plus
60 s equipment recovery; retain any remaining gain excursion as a result.

### C4: sample-time disturbance with an explicit validity mask

C4 is a **separate linear HF control**, using the wanted source plus C0
background only, before any nonlinear stage or ADC. It does not resample C1
impulses or quantized C3 output as though they were an unspecified continuous
bandlimited waveform. At nominal Fs=48000, first apply the exactly defined
1025-tap FIR with coefficients proportional to
`(2fc/Fs) sinc((2fc/Fs)(k−512)) · (1−cos(2πk/1024))/2`, `k=0,…,1024`,
`fc=8000 Hz`, normalized to unit DC gain, `sinc(u)=sin(πu)/(πu)` with
`sinc(0)=1`. Preserve its 512-sample group delay and full support. This is a
constructed antialias/reconstruction input, not a measured analog filter.

Define the continuous reconstruction of its samples v[k] as
`vhat(t)=sum_k v[k] K(Fs·t−k) / sum_k K(Fs·t−k)`, with
`K(u)=sinc(u)(1+cos(πu/64))/2` for `|u|<64`, zero otherwise.
The finite sum is over all contributing integer k, including retained source,
noise and FIR prehistory; use the exact limiting sinc value at zero. This
fixed windowed-sinc law specifies interpolation rather than leaving its choice
open. The clock-only control has constant +5 ppm rate error and zero epoch
error: output sample n is `vhat(n/(Fs(1+5e−6)))`. Compare −5 and zero ppm.
Keep the declared 1025-tap filter fixed when refining interpolation support 64→128. The bounded frequency-domain calculation in [86](86-statistics-and-calculation-checks.md) gives full-Nyquist white-noise relative MSE, weighted by that fixed filter, `9.43410515755e−13` at 4096 quadrature intervals (2048 intervals: `9.43410515705e−13`), across 17 fractional phases. This satisfies the declared ≤10⁻⁶ criterion for that calculation. The separate comparison of the original joint pair `H1025·I64` versus `H2049·I128`, after group-delay alignment, found ≤1.58160e−6 complex error and ≤1.37376e−5 dB gain difference over 0–3 kHz (17 phases×257 frequencies, refined to33×513 with unchanged reported maxima). That wanted-band agreement does not reverse the full-output failure below. Native moving-clock/chunk/mask checks remain unexecuted.

The earlier proposal also changed the filter to 2049 taps and required whole-output relative MSE ≤10⁻⁶. That **failed**: aligned FIR outputs alone differ by `2.28531838154e−4` for white noise, even at integer phase where interpolation is exact. The longer filter changes the apparatus's transition band; classify it as a separate filter-model sensitivity, not convergence of the frozen 1025-tap apparatus. Preserve the original failure. This research correction does not claim ideal brick-wall behavior or complete-receiver validation.

Apply the same ADC rule only **after** these sample-time operations when an
ADC comparison is selected. This timing error is not an extra RF CFO; an RF
oscillator tied to the same reference needs a separately declared common
fractional-frequency model. The dropout control invalidates a 0.1 s interval starting at the middle active
slot's observation midpoint. Samples retain their timestamps and invalid mask;
do not concatenate around the gap or turn it into zeros interpreted as quiet.
The masked interval is a fixed stress, not an observed operating-system rate.

### Traceability and unresolved calibration

| Required input | Closed control here | Physical-source constraint / alternative |
| --- | --- | --- |
| N0, units, passive loss and temperature | Exact C0 numbers and equations | P.372 component/site statistics in 50; a measured system temperature must have matching reference planes |
| Impulse shape, rate, marks and clustering | C1 explicit finite pulse and point process | VLF cluster measurements and context-specific APD/Bi-Kappa evidence do not calibrate these numbers; replay needs raw calibrated waveforms |
| Occupancy, line and sweep schedules | C2 deterministic schedule | Cyprus congestion Q is not a waveform or a 250 Hz failure probability; SPINE is a possible source/path model, with stated missing source classes |
| Filter, nonlinear response and AGC | C3 fully specified constructed chain | 55's receiver evidence supports mechanisms and ordering, not these time constants or saturation levels |
| LO and sample clock | C3 OU residual f; C4 separate fixed clock cases | Sub-kHz phase-noise/clock covariance and hardware recovery remain target-specific measurements |
| State and dependence | One composite stream; explicit prehistory and shared-driver alternatives | Compare measured cross-spectra and transient recovery when records become available; independent noise terms are a control assumption |

No choice in this table grants a calibrated natural probability. The research
specification for these mathematical controls is closed; measured replay and
device reproduction remain conditional on the identified data. The later suite
must report mathematical verification, source reproduction, receiver outcome
and real-time capacity as four different results.

## 6. Calibration, observables, and acceptance boundaries

Before receiver outcomes, check independent quantities at each boundary: (1) field-to-antenna-terminal transfer and impedance; (2) integrated noise power against ∫S(f)|H(f)|² df for colored noise, or PSD × ENBW when the density is approximately flat over the filter; declare one-/two-sided and complex-I/Q conventions; plus spectrum and APD under stated detector/averaging; (3) tone gain, phase, group delay and latency; (4) blocker rejection, compression, intermodulation and reciprocal-mixing spectrum; (5) AGC gain trajectory/recovery; (6) ADC full scale, overrange count and quantization; (7) calibrated I/Q, sample rate, timestamp, invalid intervals, state continuity and delayed tails. Match the cited measurement bandwidth, estimator, antenna and period before claiming source reproduction. A decoded result is a separate end-to-end observable; it does not prove that the injected PSD or front-end model matched reality.

Receiver appraisal must then use the canonical native C++ receiver and preserve configured-calendar/keyed-ordinal scope, all public hypotheses, Helper and DATA processing, physical two-quiet-slot completion, and the full `W=2*H` budget. Compare paired channel/equipment conditions on identical waveforms and public configuration; retain hidden injected truth only for evaluator scoring. Statistical replication units are independent measured episodes or independently initialized supported episodes, not adjacent correlated slots. Predeclare sample strata, estimator, uncertainty, budget and stop rule per the project validation contract.

## Remaining evidence gaps

- No target-installation calibrated long-duration field/noise/APD record jointly covers antenna response, weather/activity, occupancy, front-end state and modem outcome.
- Cyprus interference and noise studies use separate datasets, antenna systems, campaign lengths and estimators; neither is transferable to a different modem antenna without calibration.
- There are no defensible occupancy rates for this modem's 250 Hz slots, nor a full emission waveform corpus paired with the relevant allocation occupancy statistic.
- Sub-hertz impulse amplitude, timing and correlation distributions are unvalidated; similarly, cross-band noise covariance is not established by the cited marginal maps.
- Some transceiver sub-kHz phase noise, AGC-induced phase change, audio-clock wander/dropout, blocker recovery and input-field-to-ADC calibration remain unmeasured. See chapter 55 for the detailed inventory.
- The full NTIA McMurdo and 1993 reports now have inspected methods/results in chapter 50. Their PSD/case-study records still do not supply a calibrated long-duration, sub-hertz target-site waveform or a joint modem outcome population.
