# What the FSK lattice preserves at the receiver

A 160 ms rectangular observation resolves tones exactly 6.25 Hz apart when they fill the window at the expected frequencies. Three consequences matter for the draft. Moving the observation changes its samples. Multipath can remove most of the energy before the detector sees it. Adjacent positions can become nearly indistinguishable in a diffuse channel even when a wide randomly placed alphabet has little average penalty. The numbers below are deterministic references for those effects. [Native calculation and reproduction](../../_appraisal/algorithms-matrixModem/lattice-validity/README.md)

## Window, frequency and noise

Let the complex input rate be 3,200 Hz, with `N = 512` samples. Use `u_k[n] = exp(2πi k n/N)` and normalize each bin by `N`. A common frequency origin, or a common time-dependent phase of unit modulus applied to both templates, cancels in their inner product. Integer *differences* of bin indices suffice for orthogonality. A carrier can make a noninteger number of physical cycles during a cell. Whole-cell orthogonality is a property of these templates and weights. It says nothing about spectral skirts between bin centres. A single rectangular 160 ms tone has first spectral zeros 6.25 Hz to either side of centre, a 12.5 Hz null-to-null main lobe and sidelobes beyond it. The radiated bandwidth also depends on the actual message, transitions, filtering, and the bandwidth criterion. [Harris, 1978](https://doi.org/10.1109/PROC.1978.10837)

For a residual of `x` bins, the on-bin power fraction is `D_N(x)²`, where `D_N(x) = sin(πx)/(N sin(πx/N))`, continued by its limit at zero. At +1 Hz the fraction is −0.369 dB and the next bin is −14.772 dB relative to the incident tone. At +3.125 Hz both bins are −3.922 dB. This gives an exact ambiguity in this pair of energy readings under the two hypotheses “lower tone shifted up half a bin” and “upper tone shifted down half a bin.” Additional observations can separate those hypotheses when they constrain the common offset. It also explains why a 1 Hz error can be quite tolerable in some cases without being lossless.

For a window 10 ms early, 32 of its samples come from the preceding cell. Consider the allowed case of the same position in both cells. With the previous amplitude referenced to the present one, the expected-position projection has power `|(480 + 32a)/512|²`.

| Previous amplitude `a` | Displaced bin power relative to aligned |
| --- | ---: |
| Same phase (+1) | 0.000 dB |
| Quiet (0) | −0.561 dB |
| Opposite phase (−1) | −1.160 dB |

Even for uniformly random independent previous quarter turns, mean complex amplitude and mean power differ. The latter is `(480/512)² + (32/512)²` here. The draft's 0.56 dB value describes the desired-overlap term. The counterexample occurs at the maximum residual of its proposed 20 ms timing grid. A phase-only change of coordinates leaves the magnitude unchanged, so it cannot give the exact shifted-window statistic. For a rectangular window an exact update is available. Refer Fourier phases to a fixed sample origin, subtract contributions from samples leaving the window and add those entering. Convert to the new local phase convention if required. Whether this or a fresh transform is cheaper depends on the dimensions and hardware. The cheaper phase-only approximation needs sensitivity and joint-null checks including boundary transitions before its speed can be credited to a complete receiver.

For white proper complex Gaussian noise of unit variance, projections with analysis vectors `v, w` have covariance `vᴴw`, up to the convention for which projection is conjugated. Complete orthogonal bins have independent Gaussian noise. Overlapping windows use many of the same samples. At a displacement of 32 samples the magnitude of the same-bin noise correlation is 0.93750. Erasing one quarter of an aligned cell gives adjacent bins correlation magnitude 0.30011 when each is normalized to unit variance. With coloured Gaussian input, use `vᴴCw` for input covariance `C`. Linear sample generation and direct bin generation agree in distribution only if the **joint** projected law and encoder/channel memory are preserved. A subset of Gaussian bins can sometimes still be drawn exactly from that law. A data-dependent erasure, clip or estimated scale generally also requires the dependence on the observed input. Independently drawing each timing hypothesis changes the null distribution of the maximum. The counts and measurements for synthetic transforms in the draft remain stage evidence.

### Hop energy and antenna power

At the default 0.4 s hop, the two full-height cells occupy 0.32 s. A receiver retaining just those cells collects 0.80 of the energy of a continuous peak-power tone over the same hop, a 0.969 dB energy deficit. Including two 20 ms raised-cosine **amplitude** ramps contributes another 0.015 peak-power seconds because their mean squared envelope is 3/8. The transmitted fraction is then 0.8375, a 0.770 dB deficit. The envelope is constant only on the flat part. Neither ratio alone determines the block-duration penalty with fading, a nonlinear energy statistic and a finite code. Distinguish the hop oscillator's retune during quiet from the baseband position changes between adjacent active one-cell tones. [Earlier envelope calculation](Fable-5.2/fsk-lattice-design.md#envelope-and-guard)

For the sub-9 kHz ramp, the native single-resonance antenna calculation records both minimum net **delivered** power and instantaneous mismatch against the resonant resistance. Its 80 ms example can have positive net delivered power and a peak standing-wave ratio of 2.84 in the 24 Hz model simultaneously. A normalized reflected-to-forward power ratio at that reference plane of `((2.84−1)/(2.84+1))² ≈ 0.230` coexists with positive net power. Thus “no power returned” in the original calculation means no *negative net real power* there in those simulated transitions. Hardware protection limits, matching networks, filter memory and radiated envelope still require their own measurements. [Native antenna calculation](../../_appraisal/algorithms-matrixModem/sub9-fsk-rate/results/phase_slew.md)

## A narrow band inside a two-ray fade

Take the no-hop two-position HF option. For this example a complex tone is at bin 0 or 1, midway between them is `f_m = 3.125` Hz relative to baseband bin 0, and `d = 4` samples (1.25 ms). The received stream is

```text
y[n] = (x[n] + a exp(i 2π f_m d / 3200) x[n−d]) / sqrt(1+|a|²).
```

The tap powers sum to one for every row. Positive `a` gives constructive interference at the midpoint, negative `a` destructive. The delayed previous-cell waveform continues into the first four samples of the current cell. Enumerate both prior and present positions and all four relative phases with equal weight. Average powers before converting to dB. Correct-bin power is the projection onto the current ideal unit tone, for a perfectly aligned direct path. The wide control keeps the same channel and uses positions at −250 and +250 Hz.

| Channel and positions | Mean received energy | Mean correct-bin power |
| --- | ---: | ---: |
| Direct ray only, 6.25 Hz apart | 0.00 dB | 0.00 dB |
| Constructive, equal rays, 6.25 Hz apart | +2.99 dB | +2.98 dB |
| Destructive, equal rays, 6.25 Hz apart | −20.91 dB | −34.44 dB |
| Destructive, echo down 3 dB in power, 6.25 Hz apart | −11.91 dB | −12.27 dB |
| Destructive, equal rays, 500 Hz apart | +1.40 dB | +1.37 dB |

The two-position waveform can consequently have *very little incident signal energy* across a run of tones even though the excess delay is under one percent of the cell. A decoder using the transition energy has access to at most the energy in the middle column. The right-hand column includes the weaker projection of transitions onto the present tone. If a link budget already conditions on post-fade received energy, charging the physical fade again would double-count it. Frozen rays and the selected midpoint phase form a constructed adverse interval. Their duration and occurrence probability require channel data. The 500 Hz control demonstrates that changing frequency can escape this *particular* notch. An outage extending over all the used frequencies or the entire message cannot be cured by wider decoding. Block diversity and the two-quiet completion rule must be evaluated with continuous tap evolution and the actual sequence of received powers.

Measurements are consistent with differential delays on this scale. Dear et al. distinguish individual arrivals up to 1.654 ms after the first arrival in one Java NVIS sounding. Their quiet one-week campaign does not measure an equal-ray notch maintained over a message. ITU-R F.1487 provides reproducible *stationary* HF test profiles with distinct differential-delay and Doppler parameters. The triple Robust review records the limited duration, frequency and geometry of the Watterson validation. More recently Fernandes et al. measured long-term carrier Doppler residuals on a 10 MHz Fort Collins–Newark link, with different daytime and night-time statistics. Those residuals concern frequency displacement over time and cannot be substituted for the instantaneous Doppler power spectrum of a signal or for a notch probability. [Dear et al., 2024, §4](https://doi.org/10.1029/2023RS007877) · [ITU-R F.1487, Annex 3](https://www.itu.int/rec/R-REC-F.1487/en) · [triple Robust primary-source audit](../highFidelity-triple_Robust_Modem/Fable-5.2/33-hf-channel-models-and-simulators.md) · [Fernandes et al., 2026](https://doi.org/10.3389/fspas.2026.1713968)

## When the tones survive but their coherent sum vanishes

Removing a tone's keyed phase leaves the channel's phase at that frequency. A steady single ray contributes a linear phase slope across frequency, which a matching delay correction can remove. Two steady rays can give a different phase pattern. A long coherent window can then cancel strong projections from successive tones even with correct timing, frequency and key.

**Theoretical four-tone example.** Take no hopping, equal transmit amplitudes and four positions on the HF lattice, visited in the order shown. Observe an interior cell of each held tone after both paths have reached that tone. Normalize each noiseless matched projection to one for a unit-gain channel. Reference the phase to the transmit clock after removing common bulk delay. For frequency `f` relative to the first position, use a static two-ray response

```text
H(f) = (1+i)/2 × exp(−2πi f × 0.000625) + (1−i)/2 × exp(−2πi f × 0.001875).
```

| Tone in time order | 1 | 2 | 3 | 4 |
| --- | ---: | ---: | ---: | ---: |
| HF position index | 0 | 64 | 128 | 192 |
| Frequency above first position (Hz) | 0 | 400 | 800 | 1,200 |
| Complex projection after key removal | +1 | +1 | −1 | −1 |
| Individual projection energy | 1 | 1 | 1 | 1 |

The four-tone sum is zero at the stated alignment. Two successive pairs give `|1+1|² + |−1−1|² = 8`. Reorder the same tones to alternate signs and those pairs also cancel. The per-tone energy sum stays four in either order. These are raw signal terms. Changing window length also changes noise and the searched cutoff, so the numbers alone cannot rank word sensitivity.

The finite-waveform check sends four contiguous two-cell tones with keyed offsets 0°, 180°, 90° and 270°. It lets the old tone arrive through both paths at each boundary. Over those full tone intervals, the mean per-tone projection power was **0.991033** of the unit flat-channel control. At the same nominal alignment the long coherent sum retained **4.07006 × 10⁻⁷** of the control's squared magnitude. Thus the cancellation also occurs to high accuracy when the calculation includes the actual transitions. The experiment gives the normalization, delay-tail treatment and analytic boundary bound. [Native finite-tone calculation](../../_appraisal/algorithms-matrixModem/lattice-validity/README.md#four-contiguous-held-tones)

A common delay adjustment multiplies the four entries by `1, u, u², u³` for unit-magnitude `u`. Even the best such slope on these fixed projections retains only `16/27` of the squared magnitude possible with all four phases aligned. To reproduce the bound, factor `1+u−u²−u³ = (1+u)²(1−u)`, put `x = cos²(arg(u)/2)`, and maximize `64x²(1−x)/16` at `x = 2/3`. Finite tone boundaries, the other timing and oscillator hypotheses, four-bit scoring and competing words all enter an actual block decision. A physical fade at every used position removes energy before either branch can combine it. [Finite-cell fade calculation](#a-narrow-band-inside-a-two-ray-fade)

This bound depends on the allowed correction. For example, at four equally spaced tone centres indexed by `t = 0,1,2,3`, the quadratic phase factor `exp(iπt(t−1)/2)` supplies exactly the signs +,+,−,−. Applied to the four settled projections it aligns all four. A physical drift hypothesis rotates samples *within* each tone as well. Its usable range, within-tone loss and effect on competing words require the configured search. The four-point calculation alone cannot establish a worst-case loss for that search.

The native ten-bit experiment gives a complementary result. On its delayed, Doppler-shifted echo fixture, the joint rule recovered 115/120 words at 18 dB versus 15/120 for the original per-tone rule. On its independent phase-reset fixture at the same energy, the tighter per-tone cutoff in the joint rule reduced correct admissions from 105/120 to 91/120. Retain both branches and qualify their searched cutoffs on the weakest required channel. [Paired native trials and calibration](../../_appraisal/algorithms-matrixModem/block-reception/README.md#coherent-branch-and-protected-per-tone-fallback)

There is field evidence for resolvable echoes and for the possibility of tracking them. The Java HF sounding above measured arrivals on the scale of this example's 1.25 ms excess delay. Ji and Wu reported three tracked paths on a 12.36 MHz Xi'an–Ürümqi link using a repeated 9.6 kHz-wide 8PSK signal at 43 dB in-band SNR after filtering. The true field delays were unknown. Separately estimating the paths deserves reconsideration when repeated observations support it. A weak FSK receiver would still have to establish the gain after estimation, false admissions and the complete search cost. Neither campaign measures the probability of the four phases in this example. [Dear et al., 2024, Fig. 10](https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2023RS007877) · [Ji and Wu, 2026, §4.2](https://www.mdpi.com/1424-8220/26/5/1640) · [Triple Robust review of the Java sounding](../highFidelity-triple_Robust_Modem/Fable-5.2/32-skywave-dispersion-highlatitude-equatorial.md#java-nvis-public-received-iq-with-bounded-usability-checks)

## Adjacent tones under diffuse Doppler

The earlier lattice work compared average achievable metrics with a keyed placement, primarily for two- and 24-cell tones. The following calculation resolves a narrower question for the previously omitted one-cell duration. Model one proper complex zero-mean Gaussian fading gain with Gaussian Doppler power spectrum (standard deviation `s`), white receiver noise, and known symbol boundaries and centre. The normalized signal covariance for tone `k` is

```text
R_k[n,m] = exp(−2π² s² ((n−m)/3200)²) exp(2π i k (n−m)/512).
```

For equally energetic hypotheses with observation laws `CN(0,I+p R_0)` and `CN(0,I+p R_k)`, expand the complex Gaussian KL divergence at `p = 0`. Its leading coefficient is `‖R_0 − R_k‖²_F / 2` multiplying `p²` in nats. The table divides this coefficient by `‖R_0‖²_F` at the **same** spread. A coefficient near zero means those two covariance hypotheses are difficult to distinguish. It gives neither a block-error probability nor an absolute sensitivity comparison across spreads.

| Gaussian spread (2s), Hz | Adjacent pair | Five-bin pair | Mean, 352 positions one bin apart | Mean, 70 positions five bins apart |
| ---: | ---: | ---: | ---: | ---: |
| 0 | 1.00000 | 1.00000 | 1.00000 | 1.00000 |
| 1 | 0.98683 | 0.99953 | 0.99988 | 0.99998 |
| 10 | 0.28759 | 0.99202 | 0.99378 | 0.99966 |
| 30 | 0.04085 | 0.64460 | 0.97789 | 0.98906 |

Each mean uses all unordered pairs of distinct physical positions with their true multiplicities. Two distinct values sampled by a uniform permutation onto a grid have this distribution, including for a 64-symbol alphabet drawn from either grid. At 30 Hz the five-bin pair yields nearly 16 times the information coefficient of the adjacent pair. The wide randomly placed averages differ by only about one percent. This distinction explains how the existing aggregate result can coexist with severe local ambiguity. It supports retaining wideband randomized placement as a candidate while separating few-position links, repeated collisions, clipping, estimated spread, and complete-block admission from wideband averages. [Earlier lattice analysis](Fable-5.2/fsk-lattice-design.md) · [earlier received-channel envelope](Sol-6.2/channel-envelope-review.md)

Likewise the 40 ms / 160 ms comparison in the earlier calculation scales a two-cell flat interval in a 2.5-cell hop, assumes known Doppler centre and tests the listed stationary spectra with matched weights and a large alphabet. In those weak-signal rows the 160 ms choice is as good or better. Transferring that ranking to a one-cell format, an impulsive or time-selective channel, finite-alphabet narrowband ambiguity, filter transients, or CPU-limited acquisition requires separate evidence. This pass preserves the 160 ms trial setting. [Original scaled-dwell results, §9c](../../_appraisal/algorithms-matrixModem/fsk-lattice/results/lattice_receiver_section9.md)
