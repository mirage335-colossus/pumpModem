# Long-coherence weak-signal search: cost, algorithms, statistics and oscillators: literature review

27 November 2026 · Delegated literature review for the sub-9 kHz FSK rate and spacing note · web research, sources marked by access level

Original title line: *LONG-COHERENCE WEAK-SIGNAL SEARCH: COST, ALGORITHMS, STATISTICS, OSCILLATORS — literature report (2026-11-27)*. The question was what is known about the computational cost and the best algorithms for detecting a very weak, nearly coherent narrowband signal by long coherent integration when frequency, drift, timing, clock rate and a slow phase wander are unknown. The report's own calculations ("own calc") are kept with their outputs in [`literature-calcs/`](../../../../_appraisal/algorithms-matrixModem/sub9-fsk-rate/literature-calcs/README.md).

**Status.** This is the report of a delegated literature search, as it was returned to the
coordinating session on 27 November 2026. It was written into the repository on 28 November at the
user's request; a first attempt to store it was declined by the permission system, so the
[note it served](../sub9khz-fsk-rate-and-spacing.md) was written first and carries the synthesis.
Research findings, not a qualified design; nothing here is a Matrix Modem measurement.

**What was changed on storing.** This header was added and the report's own title line moved
into it. Paths to temporary files were replaced by a description, and paths to the scratch
programs that were kept now point at their copies. Nothing else was edited: "you" and
"your" address the coordinating session, and "your derivation", "your list", "the design context"
and the lettered conditions refer to the brief, which is reproduced at the end.

**How far it was checked.** All 68 distinct DOIs in this file resolve at Crossref (checked on 28 November 2026). For 67 the record's authors are named beside the citation; the other (Albersheim 1981) is misspelt in the record. The check shows that each DOI exists and belongs to the cited authors, not that the source says what is quoted from it. The search states its own access level for every source; numbers from sources it could not open are marked there.

**Where the note differs.** The note is later and was corrected after an independent check of its
numbers. Where a figure in this report was superseded there (hypothesis counts with the
1/(8 × span) timing rule, the measured decode rates, the block Monte Carlo), the note stands.

---

Reading-status tags: [F] full text read; [P] part of the full text read (first ~100k characters of the HTML, or selected sections of the PDF); [A] abstract only; [M] bibliographic record verified, content not read; [X] not accessed. "Own calc" marks numbers I computed, not taken from a source.

Three method notes:
- Every DOI was looked up or confirmed in Crossref, and every arXiv id in the arXiv API (authors, title, year matched). None was constructed from a pattern.
- ar5iv HTML sometimes duplicates digits inside maths ("2–5" rendered "22–55"). I caught and corrected three such cases against the arXiv abstracts. Other numbers taken from ar5iv renderings are marked "(ar5iv)" and should be spot-checked against the PDF before they go into a design document.
- No repository files were touched. Two scratch programs produced the "own calc" tables (temporary): [`trials_cost.py`](../../../../_appraisal/algorithms-matrixModem/sub9-fsk-rate/literature-calcs/trials_cost.py) and [`frequency_wander.cpp`](../../../../_appraisal/algorithms-matrixModem/sub9-fsk-rate/literature-calcs/frequency_wander.cpp)

## 0. Headline findings

1. Your frequency and drift spacings are the continuous-wave (CW) metric at mismatch about 0.2 per dimension, and the T³ law is the standard one. The literature refines the lattice shape, the fine grid a window ladder needs, and the point where a T⁶ law takes over.
2. Semi-coherent loss at your false-alarm levels is far below the N^(1/4) law for small N: about 0.6–0.9 dB for 4 windows and 1.7–2.3 dB for 16 (own exact calc), not 3 and 6 dB.
3. The statistical price of searching is small: about +1.2, +2.0 and +2.7 dB of energy for 10³, 10⁶ and 10⁹ independent hypotheses at P_FA = 10⁻⁶, P_miss = 10⁻³.
4. At 8.27 kHz the receiver's frequency, drift and clock-rate dimensions disappear if the audio is re-timed from a recorded GPS PPS (residual under 50 ns, about 3 mrad). A free-running sound card needs about 10⁴ (f, drift) cells at 20 minutes and a jerk dimension beyond about 30 minutes.
5. The largest algorithmic lever in the literature is hierarchical screening with an explicit false-dismissal budget (reported gains 19–26× to over 1000×). Your 129:1 cost ratio between the energy statistic and the coherent ladder fits it directly.
6. Requested sources I could not open are listed in 7(iii); the GNSS oscillator papers are the main gap, so the coherence-loss formulas come from the VLBI text instead.

## 1. Parameter-space metric, template counting, semi-coherent scaling, wander

### 1.1 Mismatch-versus-spacing rules

Mismatch m is the fractional loss of squared SNR at an offset Δλ: m ≈ g_ij Δλ^i Δλ^j, with phase metric g_ij = <∂_iφ ∂_jφ> − <∂_iφ><∂_jφ> (time averages over the window). Sources: Brady et al. 1998 Eqs. 5.4, 5.13 [P]; Prix 2007 Eqs. 46–47, 107 [P].

For φ(t) = 2π(f t + ḟ t²/2 + f̈ t³/6), t measured from the window centre, window length T, each parameter maximised over the others:
- frequency: m = π²T²Δf²/3
- drift: m = π²T⁴Δḟ²/180
- jerk: m = π²T⁶Δf̈²/25200

The first two follow from the Pletsch & Allen spacings Δf = √(12m)/(πT), Δḟ = √(720m)/(πT²) [F] and Wette's det g = π⁴T⁶/540 for (f, ḟ) [P]. The jerk term is my ratio of Wette's two determinants (π⁶T¹²/13,608,000 for f, ḟ, f̈).

Grid spacing (twice the maximum offset) in "bins", own arithmetic from these formulas:

| per-dimension m | frequency (units of 1/T) | drift ḟT² (bins end-to-end) | jerk f̈T³ |
|---|---|---|---|
| 0.05 | 0.25 | 1.9 | 23 |
| 0.10 | 0.35 | 2.7 | 32 |
| 0.20 | 0.49 | 3.8 | 45 |
| 0.30 | 0.60 | 4.7 | 55 |

- Pulsar practice (PRESTO) uses Δr = 0.5, Δz = 2, Δw = 20 (Andersen & Ransom [F]), i.e. m ≈ 0.21, 0.055, 0.04 by these formulas.
- CW searches typically use m_max = 0.2–0.3 (Brady 0.3; Cas A 0.2).
- The quadratic metric overestimates the true loss at large mismatch (Wette 2014 [P], qualitative).

### 1.2 Template counts and scaling laws

- N = θ · m_max^(−n/2) · √(det g) · V, with θ the lattice's normalised thickness and V the coordinate volume (Prix 2007 CQG Eq. 24 [F]; Wette 2014 Eq. 29).
- For (f, ḟ) over band B and drift range Ḟ: N = θ π² T³ B Ḟ / (√540 · m_max). θ = 0.500 for a square grid, 0.385 for hexagonal A2*.
- Your grid (1/(2T) × 3.9 bins) gives 0.513·T³BḞ. That equals a square lattice with covering mismatch 0.41 (0.2 + 0.2 at the worst corner).
- Scaling: T³ for (f, ḟ); T⁶ once f̈ is needed. Brady et al. fit T², T⁵, T⁹ for 1–3 spin-down parameters beyond frequency (Eqs. 7.4–7.6, ar5iv), with the frequency FFT adding one more power of T. Riles states steps in f, ḟ, f̈ scale as T, T², T³ [P].
- Lattice thickness ratio Z^n / A_n* at equal worst-case mismatch: 1.3 (n=2), 1.9 (n=3), 2.8 (n=4), 4.3 (n=5) (Prix 2007 CQG Table 1; I re-derived the values).
- Important refinement, Allen 2021 [F]: at fixed template count the expected fraction of lost detections depends on the mean mismatch (quantizer constant G), not the worst case. On that criterion the best lattices beat the cubic one only marginally (D4 versus Z⁴: about 8%). Lattice choice matters for guaranteed worst-case loss, hardly for average success probability.
- Mean versus maximum mismatch: <μ> = m_max/3 for hyper-cubic, about 0.5–0.6·m_max for A_n* (Prix & Shaltev [F]).
- Brady et al. quote maximum coherent times at 1 Tflops of about 0.8 d (all-sky, young fast pulsars) and about 18 d (directed) (ar5iv) — an illustration of how weakly T grows with compute.

### 1.3 Semi-coherent ("stack-slide") sensitivity

- Coherent amplitude sensitivity scales as √(S/T). The semi-coherent "classic" law is h_th ∝ N^(1/4)·√(S/T_obs) = √S/(T_coh·T_obs)^(1/4). Riles gives 4.54·√(S_h/T_coh) coherent (α = 1%, β = 5%) and 7.7·√S_h/(T_coh·T_obs)^(1/4) for stack-slide (α = 1%, β = 10%) [P, arXiv HTML].
- In energy terms the classic law is a 5·log10(N) dB penalty against coherent integration of the same span.
- The law is only asymptotic. Prix & Shaltev write the critical non-centrality as ρ*² ∝ N^(1/(2w)), with w = 1 the weak-signal Gaussian limit and w > 1 at low false alarm and small N (Eqs. 38–41, Fig. 2). They state the weak-signal approximation is often not well satisfied and can lead to dramatically different optima.
- Dreissigacker, Prix & Wette [P] say the N^(1/4) law is not a good approximation except for N_seg ≳ 100–1000, and that using it overestimated one Einstein@Home search's sensitivity by a factor of two.
- Both grids cost sensitivity: <μ> = ξ(m_coarse + m_fine) (Prix & Shaltev Eq. 23), an effect earlier optimisations had overlooked.
- Fine grid for the incoherent sum (Pletsch & Allen [F]): only ḟ needs refining, by γ with γ² = 1 + 60·Σ_j(t_j − t_0)²/(N·T²). For N contiguous windows this is γ = √(5N² − 4) ≈ 2.2·N (my evaluation). Frequency needs no refinement. Brady & Creighton's refinement factors grow as N, N³, N⁶ for 1, 2, 3 spin-down parameters (Eqs. 2.17–2.19, ar5iv).

Own exact calculation (central and non-central χ², 2M degrees of freedom). The per-trial false alarm is 10⁻⁶ for the first loss row and 10⁻¹⁵ (i.e. 10⁻⁶ spread over 10⁹ hypotheses) for the second; P_miss = 10⁻³:

| M windows | 2 | 4 | 8 | 16 | 64 | 256 | 1024 |
|---|---|---|---|---|---|---|---|
| loss, per-trial P_FA = 10⁻⁶ (dB) | 0.39 | 0.90 | 1.53 | 2.29 | 4.19 | 6.51 | 9.11 |
| loss, per-trial P_FA = 10⁻¹⁵ (dB) | 0.25 | 0.60 | 1.07 | 1.68 | 3.33 | 5.47 | 7.97 |
| 5·log10(M) (dB) | 1.5 | 3.0 | 4.5 | 6.0 | 9.0 | 12.0 | 15.1 |

Loss is the extra total E/N0 a non-coherent sum of M equal windows needs relative to one coherent window. This agrees with Richards' memo [F]: the non-coherent gain exponent is above 0.8 at small N and about 0.7 at N = 100 for P_D = 0.9, P_FA = 10⁻⁶.

### 1.4 Optimal number and length of segments at fixed computing cost

Prix & Shaltev [F] model cost as C = κ·m^(−n/2)·N^η·ΔT^δ and sensitivity as (1 − ξ<m>)·N^(−1/(2w))·T.
- Coherent optimum: ξ·m_opt = (1 + 2δ/n)^(−1), independent of budget (Eq. 69). Then h_th⁻¹ ∝ C^(1/(2δ)) (Eq. 71): with δ ≈ 3–7, more compute on a coherent search yields "meagre returns".
- Semi-coherent optimum: ξ·m_opt = (1 + 4wε/n)^(−1), ε = δ − η (Eq. 78).
- The sign of a = 2wε − δ for the incoherent stage separates a "bounded" regime (finite optimal observation time) from an "unbounded" one (use all data).
- Worked example (Cas A, same cost): the optimal coherent search is T = 13.6 d at m = 0.36; the optimal StackSlide is N = 139 segments of 1.9 d (span 266.5 d), mismatches 0.16 and 0.24, more than 2× better in amplitude. Sensitivity then grows as C^(1/10) instead of C^(1/14).
- Brady & Creighton [P]: at fixed compute, stacked searches are 2–4× more sensitive in amplitude than coherent ones, and a two-stage hierarchical search adds another 20–60% (arXiv abstract).
- Cutler, Gholami & Krishnan [P]: three semi-coherent stages plus a coherent follow-up need about 2–5× less compute than two stages for the same sensitivity, and four or more add little. First-stage false dismissal is 10%, later stages 1%.

### 1.5 Random phase and frequency wander

- Hidden Markov / Viterbi (Suvorova et al. 2016 [P]). The hidden state is the frequency bin; transitions are stay or ±1 bin with probability 1/3 each. The coherent step T_drift is chosen so wander stays within one bin Δf = 1/(2·T_drift) per step (10 d for Sco X-1). Cost is (N_T+1)·N_Q·ln N_Q operations. Amplitude sensitivity scales roughly as (T_obs/T_drift)^(1/4) relative to one coherent step, i.e. it is a semi-coherent method. The path-maximum false-alarm distribution is left to Monte Carlo.
- Phase-tracking HMM (Melatos et al. 2021 [P]). Frequency follows an Ornstein–Uhlenbeck process, df/dt = −γf + σξ(t), with σ ≈ (4·T_drift³)^(−1/2). It tracks phase across steps (32 phase bins), gains about 1.5× in amplitude over the frequency-only HMM, and costs up to about 10× more.
- Loosely coherent (Dergachev 2010 [F]). Power P = Σ_kl a_k* a_l K_kl with K_kl = (sin δ/δ)^|k−l|, δ the allowed phase drift between adjacent sub-windows; δ = 0 is fully coherent. Sensitivity scales as 1/(α^(1/4)·√N) with α = −ln(sin δ/δ), so a 10× shorter effective coherence costs under 2× in amplitude. Naive cost is quadratic in the number of sub-windows; eigen-decomposition makes it bilinear (2 to 351 eigenvectors for δ = 1° to 45° at N = 1000). The 2018 rewrite [F] is linear per coherent sum and adds dynamic programming, about 9× faster overall.
- SOAP (Bayley et al. [F]): Viterbi on a spectrogram, cost linear in (time steps × bins), about 0.6 s for a 0.1 Hz band over 1.3 years on a laptop; sensitivity depth about 33 Hz^(−1/2) in gapless Gaussian noise.
- Own Monte Carlo of the coherence cost of random-walk frequency. The carrier frequency is a Wiener process with diffusion σ_f² (Hz²/s); q = σ_f²·T³; noise-free; 256 samples per window, 600 trials.

| q | 0.25 | 1 | 4 | 8 |
|---|---|---|---|---|
| mean loss, frequency-only search (dB) | 0.10 | 0.38 | 1.24 | not obtained |
| mean loss, frequency + drift search (dB) | 0.02 | 0.08 | 0.29 | 0.53 |
| 10th-percentile loss, frequency + drift (dB) | 0.05 | 0.16 | 0.67 | 1.16 |

The frequency-only column is valid only at q = 0.25, 1 and 4; a grid-alignment bug in my program invalidated the other rows. The HMM "one-bin" rule corresponds to q = 0.25, so it is conservative for intra-window loss: T can be about 2.5× longer for about 1.2 dB. In Allan-deviation terms for random-walk FM, q = 3·ν0²·σ_y²(T)·T².

### 1.6 Pulsar acceleration and jerk searches

- Ransom, Eikenberry & Middleditch [P]. A tone between bins gives power sinc²(π(k − r)): 0.405 at half a bin (3.9 dB), mean loss about 23%. "Interbinning", A_(k+1/2) ≈ (π/4)(A_k − A_(k+1)), cuts the worst-case amplitude loss to 7.4%. Drift is handled by correlating the spectrum with Fresnel-integral templates of width m bins, ṙ < m ≲ 2ṙ, on a grid Δṙ = 2, Δr = 0.5. This is memory-local and avoids one full-length FFT per trial drift, which time-domain "stretching" needs. Trials: P = 1 − (1 − e^(−P_meas))^N_IFS, with independent Fourier spacing 1/T.
- Andersen & Ransom [F]. z = ḟT², w = f̈T³. Adding w up to 900 raised run time almost 80×. N_trials ≈ (N_r/m)(N_z/6.95)(N_w/44.2), i.e. about 3.5 z-templates and 2.2 w-templates per independent trial at their spacings. The jerk search helps when T is about 5–15% of the orbit.
- Fast folding (Morello et al. [P]): N·log2(N/p) additions per base period; search efficiency about 0.93; more sensitive than FFT plus incoherent harmonic summing at all duty cycles.
- GPU throughput is in section 6.

## 2. GNSS weak-signal acquisition

What I could open (the specific Borio, Gaggero, Pany, Curran, O'Driscoll and Ziedan items were not accessible; see 7(iii)):

- Psiaki 2001 [F], coherent presum, then squaring, then non-coherent sum:
  - Pre-squaring intervals avoid data-bit transitions either by using alternate half bits (two interleaved sets) or by estimating bit start time (20 hypotheses, Bonferroni factor 20).
  - "Any SNR gain achieved before squaring is worth double that amount, in dB, after squaring"; acquisition time goes as 1/SNR² (5 dB weaker needs about 10× more sums).
  - Results: 21 dB-Hz acquired from 4 s of data (199 half-bit sums); theory 18 dB-Hz in 4 s and 14 dB-Hz in 30 s.
  - Cost: 14–36 h in MATLAB on a 733 MHz PC (about 10¹² flops).
  - Two-level frequency grid: FFTs only on a rough Doppler grid (up to 500 Hz), accumulations phase-rotated onto a 25 Hz fine grid (20× fewer FFTs). Loss is the product of two sinc² terms (Eq. 12): 0.45 dB worst case at 250 Hz rough / 25 Hz fine with 10 ms presums, 1.82 dB if both are doubled, 3.9 dB at 1000 Hz.
  - Independent cells: K ≈ 1023 × (Doppler range) × (coherent time).
  - Code-Doppler coupling: chip-rate perturbation Δη = Δω_D/(2π·1575.42 MHz). Rate error is tied to carrier offset, not a separate dimension.
- Squaring loss in numbers: the table in 1.3, and Albersheim's equation as reproduced by Richards [F]: SNR_1(dB) = −5·log10 N + [6.2 + 4.54/√(N + 0.44)]·log10(A + 0.12AB + 1.7B), A = ln(0.62/P_FA), B = ln(P_D/(1 − P_D)).
- Coherent loss versus oscillator stability, from Thompson, Moran & Swenson ch. 9 [P]:
  - Coherence C(T) = |(1/T)∫₀ᵀ e^(jφ(t)) dt| (Eq. 9.140).
  - For Gaussian phase, <C²(T)> = (2/T)∫₀ᵀ (1 − τ/T)·exp(−π²ν0²τ²[σ_y²(τ) + σ_y²(2τ) + …]) dτ (Eq. 9.148, where the series converges).
  - White FM (S_y = h0, σ_y² = h0/(2τ)): <C²> = 2(e^(−aT) + aT − 1)/(aT)², a = π²·h0·ν0² (Eq. 9.150).
  - White PM: <C²> = exp(−h2·f_h·ν0²), independent of T (Eq. 9.149).
  - Coherence time: 2π·ν0·τ_c·σ_y(τ_c) ≈ 1 rad (Eq. 9.139), at which rms coherence is about 0.85 (white PM) or 0.92 (white FM).
  - Power-law relations (Wikipedia "Allan variance" table, secondary): σ_y² = h0/(2τ) white FM; 2·ln2·h₋₁ flicker FM; (2π²/3)·h₋₂·τ random-walk FM; linear drift D gives σ_y = Dτ/√2.
  - The Hadamard variance is insensitive to linear drift (NIST SP 1065 [P]). It is therefore the right statistic for a receiver that searches drift; the Allan deviation is right for one that searches frequency only.
- Scaled to ν0 = 8270 Hz (own calc):
  - 1 ppm = 8.27 mHz; 2πν0 = 5.2×10⁴ rad/s per unit fractional frequency.
  - White FM at σ_y(1 s) = 10⁻⁹ gives a = 1.35×10⁻⁹ s⁻¹: no limit on any practical time scale.
  - The 1-rad rule allows σ_y(T) up to 3.2×10⁻⁷ (60 s), 1.6×10⁻⁸ (20 min), 5.3×10⁻⁹ (1 h), 8.9×10⁻¹⁰ (6 h), 2.2×10⁻¹⁰ (1 day).
  - For random-walk FM with frequency searched, about 0.4 dB loss (q = 1) allows σ_y(T) ≈ 7.0×10⁻⁵/T, i.e. 3.6× looser.
- Operational analogues read:
  - FST4/FST4W guide [F]: FST4W-1800 (4-GFSK, 0.089 Hz tone spacing, 11.2 s symbols, LDPC (240,74), 25% sync symbols) decodes at −44.8 dB in 2500 Hz, about −10.8 dB-Hz (my conversion). Drift plus Doppler must stay under the tone spacing over the sequence; sensitivity is best when Doppler spread is at most 1/8 of the tone spacing.
  - EbNaut [F, archived copy]: coherent BPSK, both ends locked to GPS PPS, rate-1/16 constraint-length-25 convolutional code with list decoding (list 50,000): 84–87% success at Eb/N0 = −0.5 dB. VLF paths give "a steady phase sometimes for hours". The receiver must know frequency and start time, so there is no search.

## 3. Algorithms that replace brute-force enumeration

| Method | Complexity | Loss | Key reference |
|---|---|---|---|
| One long FFT for all frequency bins | N log N for N bins | 3.9 dB worst at 1/T spacing; 0.91 dB at 1/(2T) (zero-pad ×2); interbinning 7.4% amplitude worst case | Ransom et al. [P] |
| Two-level: presum L sub-windows of T_sub, then FFT across them | O(N) + O(L log L) per (drift, timing) | sinc²(π·Δf·T_sub) envelope × fine-grid sinc²; 3.9 dB at band edge ±1/(2T_sub); ≤0.91 dB if band kept within ±1/(4T_sub) | Psiaki Eq. 12 [F] |
| Dechirp (or resample), then FFT, per drift trial | N_drift × N log N | grid mismatch only | Ransom et al. (described as heavy for long series) |
| Fourier-domain correlation with drift/jerk kernels | one FFT + N × N_z × (kernel ≈ z…2z taps), memory-local | Δz = 2 gives ≤0.25 dB (metric, own calc) | Ransom [P]; Andersen & Ransom [F]; Dimoudi et al. [F] |
| Fast chirp transform | O(N0·N1·log(N0·N1)) for N1 ≤ N0/2 chirp-rate bins; reduced variant about O(N0·N1·log N1); same order as dechirp + FFT | not quantified by the authors | Jenet & Prince [F] |
| Keystone, Radon–Fourier, polynomial-phase transforms | not verified | not verified | Perry et al. 1999; Xu et al. 2011; Peleg & Porat 1991; Peleg & Friedlander 1995 [all M] |
| Hierarchical coarse-to-fine | expected cost c1 + Σ c_i·π_(i−1) (Nitz Eq. 10) | set by the false-dismissal budget | below |
| Viterbi over frequency tracks | (N_T+1)·N_Q·ln N_Q, or linear in steps × bins | semi-coherent sensitivity; path-maximum statistics need Monte Carlo | Suvorova et al. [P]; Bayley et al. [F] |
| Loosely coherent kernel | bilinear in sub-windows × eigenvectors | interpolates coherent to semi-coherent | Dergachev [F] |
| Resampling for clock-rate error | one interpolation pass per rate hypothesis plus FFT | grid mismatch only | Dunn et al. [F] (GPU); Patel et al. 2010 [M] |

On the unread row: from general knowledge only, the lag-product (high-order ambiguity) methods are non-linear and have an SNR threshold, which makes them doubtful at per-window SNR far below 0 dB. Treat that as a hypothesis to check.

Hierarchical results in detail:
- Brady & Creighton: +20–60% amplitude at fixed cost from two stages.
- Cutler et al.: three stages about 2–5× cheaper than two; 10% then 1% false dismissal.
- Meinshausen, Bickel & Rice [A]: dynamic-programming-optimised multi-stage search nearly matches exhaustive-search power at more than 1000× lower cost (gamma-ray pulsar example).
- Nitz, Kacanja & Capano 2026 [F]: up to three coarse tiers run before the full filter. Thresholds are set by quantile inversion of the joint coarse/full distribution so that P(rejected by any tier | full statistic ≥ threshold) = α. At α = 10⁻², tier 1 rejects 94.4% of noise pairs and 0.22% reach the full filter; total speed-up is 19–26×. Tightening α from 10⁻² to 10⁻⁴ costs only 2–3× more run time. The measured false-dismissal rate tracks the target.
- Pulscan (White et al. 2025 [F]): boxcar filters on the power spectrum as a first stage, more than 10× faster than matched acceleration search on one core (0.42 s versus 4.8 s at zmax = 100), with fewer unique detections at high acceleration (170 versus 479 at zmax = 1200).

## 4. The statistical price of searching

- Coherent statistic, noise power normalised to mean 1: per-trial P_fa = e^(−P_th), so P_th = ln(N_h/P_FA). The same form appears in Brady et al. Eq. 6.12 and Ransom et al. Eq. 46. Psiaki uses a Bonferroni scaling by the number of independent cells.
- Own exact calculation (non-central χ² with 2 degrees of freedom, λ = 2E/N0), overall P_FA = 10⁻⁶, P_miss = 10⁻³:

| independent hypotheses | 1 | 10³ | 10⁶ | 10⁹ | 10¹² |
|---|---|---|---|---|---|
| threshold P_th | 13.8 | 20.7 | 27.6 | 34.5 | 41.4 |
| required E/N0 (dB) | 15.34 | 16.51 | 17.39 | 18.09 | 18.68 |
| cost versus one trial (dB) | 0 | +1.17 | +2.05 | +2.75 | +3.34 |

- At 90% detection the costs are +1.5, +2.6, +3.4 dB for 10³, 10⁶, 10⁹.
- The 2^24 codewords are themselves about 1.7×10⁷ trials (+2.35 dB if independent). Adding 10³, 10⁶, 10⁹ search hypotheses on top costs a further +0.65, +1.2, +1.7 dB.
- Rule: each decade of trials adds ln 10 = 2.3 to the threshold and about 3.2–3.5 to the required E/N0, i.e. 0.4 falling to 0.2 dB per decade.
- Effective number of independent templates. There is no universal ratio. Tenorio et al. [P]: the loudest-candidate distribution tends to a Gumbel law (location ≈ 2·ln N, scale 2 for a χ²₄ statistic), but a fitted "effective N" systematically misdescribes correlated banks; they recommend fitting the batch-maximum distribution empirically. Dreissigacker et al. make the same point. For your grid, Andersen & Ransom's widths suggest about 0.5 independent trials per template in frequency and about 0.56 in drift, so N_eff ≈ 0.3·N. That is worth only about 0.1 dB, so counting every template as independent is a cheap, conservative choice.

## 5. Oscillator and timing reality at 8.27 kHz

Sourced numbers:
- GPSDOs, Lombardi 2008 [F]: stability at 1 s from 10⁻¹⁰ to 10⁻¹²; at 1 day from 8×10⁻¹³ to 5×10⁻¹⁴; 1-day frequency offset from 10⁻¹² to 5×10⁻¹⁴. Two rubidium-based units both reached about 10⁻¹² at 100 s. Short-term stability of some units is worse than a free-running oscillator because of steering steps.
- TCXO-based hobby GPSDO, Riley 2016 [F]: short-term stability is that of the TCXO, about 10⁻¹⁰ (1.6×10⁻¹⁰ from phase noise), random-walk FM character, loop time constant about 15 s. In holdover the output slews at about −0.6 ns/s (6×10⁻¹⁰), "accumulating microseconds of phase error in a few minutes". N8UR 2022 [F]: 5-day mean offset −1.55×10⁻¹¹ for the same model.
- GPS PPS itself, Ackermann 2020 [P]: Allan deviation about 10⁻⁸ at 1 s for single-band u-blox modules (4×10⁻⁹ dual-band), falling as 1/τ; sawtooth about 17 ns peak-to-peak (M8N). u-blox note [P]: time pulse derived from a 48 MHz clock; LEA-6T 30 ns rms, 15 ns with quantisation-error correction.
- Quartz, Vig 2004 [P]: TCXO accuracy 2×10⁻⁶ per year, aging 5×10⁻⁷ per year, temperature 5×10⁻⁷ (−55 to +85 °C), σ_y(1 s) = 10⁻⁹. OCXO 10⁻⁸, 5×10⁻⁹, 10⁻⁹, 10⁻¹². Plain XO accuracy 10⁻⁵ to 10⁻⁴.
- Sound cards: Nicholson's example log shows 192003.34 Hz for a nominal 192000 (+17 ppm). DL4YHF [F]: 1.5 ppm shift in 30 minutes when airflow changed (15 mHz at 10 kHz); under 1 ppm drift after warm-up; one interface started more than 2 ppm from its final rate.
- PPS re-timing, Nicholson's notes [P, archived copy]: vttime measures a PPS recorded on a spare channel (centroid, short-pulse or edge method) and resamples so samples are synchronous with UT. Timing jitter is below 50 ns with a good PPS, often better than 30 ns; timestamp accuracy is better than 100 ns. The absolute offset can be calibrated only "to about the nearest sample" without extra effort. He warns of USB packet loss on small computers and of resets when the card's rate changes quickly (cured by insulating the crystal). DL4YHF measured a rate scatter of about 30 ns per second at 192 kS/s.

Implications (own calc):

| Class | 1 min (bin 16.7 mHz) | 20 min (bin 0.83 mHz) | 1 h (bin 0.28 mHz) | 6 h (bin 46 µHz) |
|---|---|---|---|---|
| Locked GPSDO, or audio re-timed from PPS (time error ≤ 50 ns) | phase ≤ 3 mrad; 1 cell | same | same | same |
| TCXO GPSDO in holdover (6×10⁻¹⁰) | 5 µHz offset; 2 mrad | 37 mrad | 0.11 rad | 0.67 rad, offset 0.1 bin |
| Free TCXO, ±2 ppm = ±16.5 mHz | 4 frequency cells | 79 | 238 | 1430 |
| Free sound card, ±100 ppm = ±0.83 Hz | 200 frequency cells | 4.0×10³ | 1.2×10⁴ | 7.1×10⁴ |
| Sound card drift 1.5 ppm/30 min = 6.9 µHz/s | 0.02 bins (none) | 10 bins (about 5 drift cells) | 89 bins (about 46 cells) | 3200 bins (about 1650 cells) |

- Frequency cells are at 1/(2T) spacing; drift cells at 3.9-bin spacing over ± the drift.
- Free TCXO random phase: if random-walk FM holds from σ_y(1 s) = 10⁻⁹ (a pessimistic assumption of mine), q reaches 0.25 at 18 min, 1 at 28 min, 4 at 45 min. Starting from 10⁻¹⁰ the times are 4.6× longer.
- Jerk: if the sound-card drift rate reverses over about 30 minutes, f̈ ≈ 3.8×10⁻⁹ Hz/s², and f̈T³ reaches the m = 0.2 limit of about 22 at T ≈ 30 min. Beyond that the count grows as T⁶ unless a tracker replaces the grid.
- Timing error to phase: 1 µs = 0.052 rad; one 48 kHz sample (20.8 µs) = 1.08 rad.
- Not sourced: medium-term (10²–10⁴ s) Allan deviation of free consumer TCXOs and sound-card crystals. The table uses the measured examples above as bounds.

## 6. GPU implementations, 2018–2026

- Fourier-domain acceleration search (Dimoudi et al. [F]): Tesla P100, 2²³-sample series (about 537 s). The number of series searchable in real time was 42,099 with 64 templates and 8,600 with 256 (34,196 and 6,954 with interbinning). That is about 1.7×10¹⁰ (frequency, drift) cells per second per GPU (my arithmetic). At least 4× faster than PRESTO-GPU and 6× faster than PRESTO OpenMP. No energy figures.
- Reduced precision (White et al. 2023 [F]): convolution in bfloat16 with the FFT kept in float32 runs about 1.6× faster; 97.5% of bright peaks land in the same bin and all peaks are within 3% of predicted SNR. IEEE half precision was rejected for overflow risk from the DC bin.
- GPU resampling F-statistic (Dunn et al. [F]): 10–100× faster than CPU; about 5×10⁻⁹ s per statistic value on a P100 versus about 4×10⁻⁷ s per CPU core; pilot search 17.2 GPU-hours versus about 1092 core-hours. No per-watt data.
- Tensor-core FFT (Li et al. [F]): radix-16 butterflies as 16×16 tensor-core products; 1.9× (V100) and 1.24× (A100) over half-precision cuFFT in 1-D, up to about 3× in 2-D; relative error about 1.8% (same as cuFFT half precision); memory-bandwidth bound at large sizes.
- Tensor cores for matched filtering (Yamaguchi et al. 2019 [A]): template matching cast as matrix multiplication on Volta tensor cores, 4.74× faster than the cuBLAS routine, with "local normalization" to hold half-precision accuracy.
- Tensor-Core Beamformer (Oostrum et al. 2025 [A]): over 600 TeraOps/s at about 1 TeraOp/J (16-bit, AMD MI300X); over 3 PetaOps/s at more than 10 TeraOps/J (1-bit, NVIDIA A100). These are the only per-joule figures I found.
- Schwarz et al. 2025 [A]: double-precision matrix multiply emulated on low-precision tensor units, up to 13.2× over native double precision on an RTX PRO 6000 Blackwell Server Edition, under 10% overhead.
- Nitz et al. 2026 [F]: fused correlation plus screening, 0.25 to 2.0–6.6 million template–data pairs per second per CPU core at N = 4096; 21× and 19× over vendor FFTs on two integrated GPUs. They suggest, without measuring, that early screening tiers could run in float16, bfloat16 or fp8 on tensor units.
- No 2019–2026 paper I could open benchmarks an INT8 or FP16 matrix-multiply matched-filter bank end to end against float32 on a current GPU. The Tensor-Core Correlator paper (Romein 2021) is the likely exception and I could not open it.

## 7. Conclusions

### (i) Recommendations for cutting hypothesis count or cost per hypothesis

1. Remove dimensions with timing discipline. Re-time received audio from a recorded PPS (and lock the transmitter likewise). Residual is under 50 ns, about 3 mrad at 8.27 kHz, so receiver frequency, drift and clock-rate collapse to one cell each. Sensitivity cost: none. Saves about 10⁴ (20 min) to 10⁷–10⁸ (6 h) cells against a free sound card. Sources: Nicholson; EbNaut; Lombardi; Riley.
2. Tie clock rate to frequency. If carrier and symbol clock come from one oscillator at each end, the rate error is ε = Δf/f_c. Treat it as GNSS treats code Doppler (Psiaki) and apply it as per-tone shifts ε·f_k, with no resampling. Removes one dimension at no loss (own inference from the GNSS practice).
3. Two-stage screening. Run the energy statistic (8,400/s) on the whole grid and the coherent ladder (65/s) only on survivors. Expected cost per hypothesis is c1·(1 + 129·π1): about 56× more hypotheses per second than all-coherent at 1% noise survival, 114× at 0.1%. The sensitivity cost is the chosen false-dismissal α (10⁻² to 10⁻⁴; Nitz: only 2–3× run time between them; Cutler: 10% then 1%). The binding question is whether the energy statistic keeps enough SNR at the coherent ladder's target level; calibrate the threshold from the joint distribution as Nitz et al. do.
4. Coarse-to-fine with codeword lists (my adaptation of Brady & Creighton and Cutler et al.). Run the full 2^k decode only on a coarse grid, keep the top L codewords, and rescore only those on the fine grid at L/2^k of a decode. Cost: a false-dismissal term for the true codeword missing the list at coarse mismatch; this needs measurement.
5. Lattice. Hexagonal placement in (f, drift) saves 23% of templates at equal worst-case loss; A3* saves 46% and A4* 64% (Prix 2007). It gains only about 8% in mean-loss terms (Allen 2021). Use it if you design to the worst case.
6. Mismatch tuning. Prix & Shaltev's optimum for n = 2, δ = 3 is a mean mismatch of 0.25 against your roughly 0.14. That means spacings about 1.35× coarser (0.67/T and 5.3 bins) and 1.8× fewer templates, at +0.6 dB mean loss. If the saved compute lengthens T by 1.22× the net is about +0.3 dB (own calc). If T is capped by wander or message length it is a pure loss. Low priority; the optimum is flat.
7. Window ladder. Prefer few long windows. Refine only drift, by about 2.2·N, on the incoherent grid. The hypothesis count is then about 2.2·N·T³ against N³·T³ for a coherent search of the same span.
8. Wander. Size windows by q = σ_f²·T³ ≤ 1–4 (frequency only) or ≤ 8 (frequency and drift). Beyond that, track frequency across windows with a Viterbi pass instead of adding drift and jerk grids. Sensitivity is semi-coherent; thresholds for the path maximum need Monte Carlo (Suvorova et al.).
9. Make the frequency dimension nearly free for stage 1 with the two-level presum-then-FFT, keeping the band within ±1/(4·T_sub).
10. Do not thin hypotheses for statistical reasons: 10× fewer buys only 0.2–0.4 dB.
11. Reduced precision belongs in the screening stage: bfloat16 or FP16 tensor paths gave 1.6× to 4.7× in the sources above with a few percent amplitude error. Keep float32 for the confirming stage.

Pointer, not verified: if the keyed code is linear, ordered-statistics decoding (Fossorier & Lin 1995 [M]) or Hadamard-transform coset decoding (Be'ery & Snyders 1986; Conway & Sloane 1986 [M]) could replace the 2^k enumeration. I did not read these and do not know your code structure.

### (ii) Where the literature contradicts or refines what you use

- Frequency bins at 1/(2T): confirmed (m = 0.206 by the metric, 0.91 dB exact worst case, about 0.3 dB mean). PRESTO uses the same.
- Drift at 3.9 bins: equals the CW metric at m ≈ 0.21 (√(720m)/π). Pulsar practice is finer (2 bins). On a rectangular grid the worst corner adds to m ≈ 0.41, about 2 dB by the metric (less in reality), not 0.9 dB.
- Timing at 1/8 tone: no direct analogue found. For rectangular tones the worst-case loss is about 0.56 dB and the mean 0.27 dB (own calc); 1/4 tone would be 1.16 and 0.55 dB. Timing mismatch is linear, not quadratic, so lattice arguments do not apply to it.
- Cubic growth: correct for (f, drift). It becomes T⁶ once f̈T³ exceeds about 22 (about 30 minutes on a drifting sound card). A window ladder also multiplies the drift count by about 2.2·N.
- Clock rate as a fourth dimension: not independent in GNSS practice; it is slaved to carrier offset.
- Window ladder loss: the N^(1/4) law overstates the penalty by 2–4 dB for 4–64 windows at your false-alarm levels. Coarse and fine mismatches both count.
- Author correction: the 1998 paper is by Brady, Creighton, Cutler and Schutz (not Patel).
- Dergachev's 2018/2019 "medium scale coherence lengths" paper has no journal reference on arXiv.

### (iii) Not accessed, and unverifiable identifiers

Not accessed [X]:
- Gaggero & Borio, ION GNSS 2008 (not found online; Gaggero's 2008 thesis exceeds the fetch size limit).
- Pany et al., Inside GNSS Nov/Dec 2009.
- Borio 2008 thesis; O'Driscoll 2007 thesis; Strässle et al. 2007; Gowdayyanadoddi thesis (HTTP 403).
- Lorimer & Kramer, Handbook of Pulsar Astronomy; PEASOUP documentation.
- Romein 2021, The Tensor-Core Correlator (HTTP 403; DOI 10.1051/0004-6361/202141896 verified).
- Rogers & Moran 1981 full text.
- Einstein@Home GPU run-time figures for 2023–2025 (seen only in search snippets, so not quoted).

Record verified, content not read [M] (no number above is attributed to these):
- Borio, O'Driscoll & Lachapelle 2009, 10.1109/TAES.2009.5259196
- Curran, Lachapelle & Murphy 2012, 10.1109/TAES.2012.6129629
- Akopian 2005, 10.1049/ip-rsn:20045096
- Cheng, Hurd & Statman 1990, 10.1109/26.47859
- DiCarlo & Weber 1983, 10.1109/TCOM.1983.1095867
- Perry, DiPietro & Fante 1999, 10.1109/7.745691
- Xu, Yu, Peng & Xia 2011, 10.1109/TAES.2011.5751251
- Peleg & Porat 1991, 10.1109/18.75269
- Peleg & Friedlander 1995, 10.1109/78.403349
- Albersheim 1981, 10.1109/PROC.1981.12082
- Shnidman 2002, 10.1109/TAES.2002.1039422
- Rogers & Moran 1981, 10.1109/TIM.1981.6312409
- Barniv 1985, 10.1109/TAES.1985.310548
- Streit & Barrett 1990, 10.1109/29.52700
- Irsigler & Eissfeller 2002, 10.1007/PL00012911
- Spangenberg et al. 2000, 10.1023/A:1008848916834
- Ziedan & Garrison 2004, 10.1109/PLANS.2004.1309030
- Johnston & Kulkarni 1991, 10.1086/169715
- Fossorier & Lin 1995, 10.1109/18.412683
- Conway & Sloane 1986, 10.1109/TIT.1986.1057135
- Be'ery & Snyders 1986, 10.1109/TIT.1986.1057189
- Cannon et al. 2010, 10.1103/PhysRevD.82.044025
- Messenger, Prix & Papa 2009, 10.1103/PhysRevD.79.104017
- Pletsch 2010, 10.1103/PhysRevD.82.042002; Pletsch 2011, 10.1103/PhysRevD.83.122003
- Dergachev 2012, 10.1103/PhysRevD.85.062003
- Suvorova et al. 2017, 10.1103/PhysRevD.96.102006; Sun et al. 2018, 10.1103/PhysRevD.97.043013
- Wette et al. 2018, 10.1103/PhysRevD.97.123016; Wette 2023, 10.1016/j.astropartphys.2023.102880
- Keitel & Ashton 2018, 10.1088/1361-6382/aade34
- La Rosa et al. 2021, 10.3390/universe7070218
- Patel et al. 2010, 10.1103/PhysRevD.81.084032
- Gross & Vitells 2010, 10.1140/epjc/s10052-010-1470-8
- Allen 2019, 10.1103/PhysRevD.100.124004

No DOI exists or was found for: Psiaki 2001 (ION GPS proceedings); Riley 2016; Ackermann 2020; the u-blox note; the Vig tutorial; Nicholson's pages; the DL4YHF page; the FST4 guide; Richards' memo; arXiv:1807.02351; arXiv:2611.17229. The arXiv id of Suvorova et al. 2017 was not looked up.

## References read

CW and pulsar:
- Brady, Creighton, Cutler, Schutz, Phys. Rev. D 57, 2101 (1998), 10.1103/PhysRevD.57.2101, arXiv:gr-qc/9702050 [P]
- Brady, Creighton, Phys. Rev. D 61, 082001 (2000), 10.1103/PhysRevD.61.082001, arXiv:gr-qc/9812014 [P]
- Prix, Phys. Rev. D 75, 023004 (2007), 10.1103/PhysRevD.75.023004, arXiv:gr-qc/0606088 [P]
- Prix, Class. Quantum Grav. 24, S481 (2007), 10.1088/0264-9381/24/19/S11, arXiv:0707.0428 [F]
- Wette, Phys. Rev. D 90, 122010 (2014), 10.1103/PhysRevD.90.122010, arXiv:1410.6882 [P]
- Wette, Phys. Rev. D 85, 042003 (2012), 10.1103/PhysRevD.85.042003, arXiv:1111.5650 [P]
- Cutler, Gholami, Krishnan, Phys. Rev. D 72, 042004 (2005), 10.1103/PhysRevD.72.042004, arXiv:gr-qc/0505082 [P]
- Prix, Shaltev, Phys. Rev. D 85, 084010 (2012), 10.1103/PhysRevD.85.084010, arXiv:1201.4321 [F]
- Pletsch, Allen, Phys. Rev. Lett. 103, 181102 (2009), 10.1103/PhysRevLett.103.181102, arXiv:0906.0023 [F]
- Dergachev, Class. Quantum Grav. 27, 205017 (2010), 10.1088/0264-9381/27/20/205017, arXiv:1003.2178 [F]
- Dergachev, arXiv:1807.02351 (v3, 2019) [F]
- Suvorova, Sun, Melatos, Moran, Evans, Phys. Rev. D 93, 123009 (2016), 10.1103/PhysRevD.93.123009, arXiv:1606.02412 [P]
- Melatos, Clearwater, Suvorova, Sun, Moran, Evans, Phys. Rev. D 104, 042003 (2021), 10.1103/PhysRevD.104.042003, arXiv:2107.12822 [P]
- Riles, Living Rev. Relativ. 26, 3 (2023), 10.1007/s41114-023-00044-3, arXiv:2206.06447 [P]
- Dreissigacker, Prix, Wette, Phys. Rev. D 98, 084058 (2018), 10.1103/PhysRevD.98.084058, arXiv:1808.02459 [P]
- Tenorio, Modafferi, Keitel, Sintes, Phys. Rev. D 105, 044029 (2022), 10.1103/PhysRevD.105.044029, arXiv:2111.12032 [P]
- Allen, Phys. Rev. D 104, 042005 (2021), 10.1103/PhysRevD.104.042005, arXiv:2102.11254 [F]
- Bayley, Messenger, Woan, Phys. Rev. D 100, 023006 (2019), 10.1103/PhysRevD.100.023006, arXiv:1903.12614 [F]
- Jenet, Prince, Phys. Rev. D 62, 122001 (2000), 10.1103/PhysRevD.62.122001, arXiv:gr-qc/0012029 [F]
- Ransom, Eikenberry, Middleditch, Astron. J. 124, 1788 (2002), 10.1086/342285, arXiv:astro-ph/0204349 [P]
- Andersen, Ransom, Astrophys. J. Lett. 863, L13 (2018), 10.3847/2041-8213/aad59f, arXiv:1807.07900 [F]
- Morello, Barr, Stappers, Keane, Lyne, MNRAS 497, 4654 (2020), 10.1093/mnras/staa2291, arXiv:2004.03701 [P]
- Dimoudi, Adámek, Thiagaraj, Ransom, Karastergiou, Armour, Astrophys. J. Suppl. 239, 28 (2018), 10.3847/1538-4365/aabe88, arXiv:1804.05335 [F]
- White, Adámek, Roy, Dimoudi, Ransom, Armour, Astrophys. J. Suppl. 265, 13 (2023), 10.3847/1538-4365/acb351, arXiv:2206.12205 [F]
- White, Adámek, Roy, Ransom, Armour, Astrophys. J. Suppl. 279, 8 (2025), 10.3847/1538-4365/adc89e, arXiv:2406.15186 [F, v1]

Screening and statistics:
- Meinshausen, Bickel, Rice, Ann. Appl. Stat. 3, 38 (2009), 10.1214/08-AOAS180, arXiv:0712.1663 [A]
- Nitz, Kacanja, Capano, arXiv:2611.17229 (12 Nov 2026) [F]
- Richards, "Noncoherent Integration Gain, and its Approximation", memo, 9 June 2010 [F]

GNSS, oscillators, timing:
- Psiaki, "Block Acquisition of Weak GPS Signals in a Software Receiver", ION GPS 2001 [F]
- Thompson, Moran, Swenson, Interferometry and Synthesis in Radio Astronomy, 3rd ed. (Springer 2017), ch. 9, 10.1007/978-3-319-44431-4 [P]
- Lombardi, NCSLI Measure 3(3), 56 (2008), 10.1080/19315775.2008.11721437 [F]
- Riley, "An Informal Evaluation of a Leo Bodnar GPS Frequency Reference", 23 May 2016 [F]
- Ackermann (N8UR), "Timing and Location Performance of Recent u-blox GNSS Receiver Modules", Aug 2020 [P]; blog post, 16 July 2022 [F]
- u-blox, Application Note GPS.G6-X-11007 [P]
- Vig, "Quartz Crystal Resonators and Oscillators… A Tutorial", SLCET-TR-88-1 Rev. 8.5.2.0, Jan 2004 [P]
- Riley, NIST SP 1065 (2008), 10.6028/NIST.SP.1065 [P; extracted text poor]
- Nicholson, "VLF Receiver Toolkit – Notes" and "EbNaut Coherent BPSK", abelian.org — read from Internet Archive copies, because the live site did not resolve on 2026-11-27 [P / F]
- Büscher (DL4YHF), Spectrum Lab "Sample Rate- and Frequency Calibration" [F]
- Franke, Somerville, Taylor, "Quick-Start Guide to FST4 and FST4W" (WSJT-X 2.3.0) [F]
- "Coherent Integration Time Limits", Inside GNSS, 14 March 2011 [F] (antenna-motion limits only; no oscillator formula)

GPU:
- Dunn, Clearwater, Melatos, Wette, Class. Quantum Grav. 39, 045003 (2022), 10.1088/1361-6382/ac4616, arXiv:2201.00451 [F]
- Li, Cheng, Lin, arXiv:2104.11471; IEEE CLUSTER 2021, 10.1109/Cluster48925.2021.00035 [F, arXiv]
- Yamaguchi, Ichimura, Fujita, Kato, Nakagawa, IEEE Signal Process. Lett. 26, 1857 (2019), 10.1109/LSP.2019.2951305 [A]
- Oostrum, Veenboer, Rook, Brown, Kruizinga, Romein, arXiv:2505.03269 (IPDPS 2025) [A]
- Schwarz et al., arXiv:2511.13778, 10.1145/3773656.3773670 [A]
- Mérou, Tenorio, Sintes, arXiv:2507.07816 [A; no numbers]

---

## The brief given to the search

Reproduced as sent, so that the report's references to it can be read.

> Literature research task (web research; do NOT edit or create files in any repository). Your final message is the deliverable and nobody else sees your intermediate work, so put everything there. It may be long (3,000–5,000 words is fine).
>
> Question: What is known about the computational cost and the best algorithms for detecting a very weak, nearly-coherent narrowband signal by long coherent integration (minutes to days) when the frequency offset, frequency drift rate, timing offset, clock-rate error and a slow random phase wander ("spin wandering", small Doppler spread) are unknown? I need this to size the "computational ceiling" of a receiver for a slow M-ary FSK signal near 8 kHz carrying a short keyed codeword (10–24 bits, 32 code symbols, each symbol repeated on many tones), received at carrier-to-noise densities of about −10 to −40 dB-Hz, where every hypothesis in (frequency, drift rate, timing, clock rate) currently costs one full exhaustive maximum-likelihood decode over all 2^k codewords.
>
> What I already use (check and improve): frequency bins half the reciprocal of the coherent window T apart (0.9 dB worst-case loss); drift-rate hypotheses about 3.9 bins of end-to-end drift apart; timing hypotheses one eighth of a tone apart; so the hypothesis count per window grows as T (frequency) × T² (drift) — cubic — times timing; a "window ladder" that adds windows non-coherently (semi-coherent) when the full span is not coherent. Measured decode throughput on one high-end GPU: about 8,400 full 24-bit (16.7 million codeword) decodes per second for the energy (per-tone) statistic and an estimated 65 per second for the coherent ladder.
>
> Please find and report, with full citations:
> 1. Parameter-space metric and template counting for long coherent integrations with frequency and frequency-derivative unknown: the continuous gravitational-wave literature (Brady, Patel, Creighton & Schutz 1998 "Searching for periodic sources with LIGO"; Brady & Creighton 2000 hierarchical searches; Prix 2007 on the F-statistic metric; Wette's lattice template banks; Cutler, Gholami & Krishnan 2005 "Improved stack-slide searches"; Prix & Shaltev 2012 "Search for continuous gravitational waves: Optimal StackSlide method at fixed computing cost"; Pletsch & Allen 2009 global-correlation / GCT method; Dergachev's "loosely coherent" methods 2010–2019 which explicitly allow bounded phase wander; Suvorova, Sun, Melatos et al. 2016–2021 hidden-Markov-model (Viterbi) tracking of wandering frequency; Riles 2023 review) and pulsar acceleration/jerk searches (Ransom, Eikenberry & Middleditch 2002 Fourier-domain acceleration search; Andersen & Ransom 2018 jerk search; Lyne/Lorimer handbook; fast folding algorithm; recent GPU implementations such as PEASOUP/AstroAccelerate with quoted throughput). I want: the mismatch-versus-spacing rules, the template-count scaling laws with coherent time, the sensitivity scaling of semi-coherent searches (the N^(1/4) law and its refinements), the optimal number/length of coherent segments at fixed computing cost, and how these methods handle random phase/frequency wander (the coherence time limit set by wander and the sensitivity cost).
> 2. GNSS weak-signal acquisition and tracking literature on the same trade (coherent versus non-coherent versus differential integration, bit-transition handling, Doppler-rate bins, code-Doppler i.e. clock-rate coupling to carrier frequency, "squaring loss"), including high-sensitivity acquisition at about 10–20 dB-Hz and theoretical work on the optimal coherent time under oscillator phase noise (Allan-deviation-limited coherence; e.g. Borio, Pany, O'Driscoll, Ziedan, Psiaki 2001 "Block acquisition of weak GPS signals in a software receiver", Gaggero & Borio 2008 on ultra-stable oscillators and coherent integration, Curran et al. on clock effects). I want formulas for coherent-integration loss versus oscillator Allan deviation or phase-noise h-parameters (h0, h-1, h-2) at a given carrier frequency, so I can scale to 8 kHz where even 1 ppm is only 8 mHz.
> 3. Efficient algorithms that replace brute-force hypothesis enumeration: FFT across window sums to get all frequency bins at once; chirp-z / dechirp-then-FFT for drift; "fast chirp transform"; keystone or polynomial-phase transforms; sequential/hierarchical (coarse-to-fine, multi-stage with thresholds) search and its expected-cost savings and false-dismissal accounting; dynamic programming (Viterbi) over frequency tracks; resampling for clock-rate error; and "sum over shorter windows then FFT" (i.e. a two-level coherent combine) with its scalloping/straddle losses. For each, give the complexity, the loss, and a key reference.
> 4. The statistical price of searching: how the detection threshold (and therefore required energy) grows with the number of independent hypotheses (trials factor) — exact results or accepted approximations (e.g. effective number of independent templates versus template count at given mismatch), so I can say how many dB of signal energy 10^3, 10^6, 10^9 hypotheses cost at a false-alarm probability of 10^-6 and miss probability 10^-3.
> 5. Oscillator and timing reality for practical stations: typical short- and medium-term stability (Allan deviation at 1 s to 10^4 s) of GPS-disciplined oscillators (OCXO-based, and cheap TCXO/XO "hobbyist" GPSDOs including their steering/slew behaviour and sawtooth PPS error), free-running TCXOs/XOs, and sound-card sample clocks (ppm offset, temperature drift per hour); and how audio interfaces can be disciplined after the fact from a recorded GPS PPS (the vlfrx-tools "vttime"/"vtresample" approach used by sub-9 kHz amateurs, Paul Nicholson) — what residual timing/phase error that leaves. I want numbers good enough to state, at an 8.27 kHz carrier, what frequency uncertainty, drift rate and phase random walk each class implies over 1 minute, 20 minutes, 1 hour and 6 hours.
> 6. Anything recent (2019–2026) on GPU implementations of long-coherence searches with measured throughput per watt/device and on using tensor/matrix (GEMM) hardware for matched-filter banks.
>
> Method requirements:
> - Prefer primary sources. For each source say whether you read the full text, only the abstract, or could not access it; never attribute a number to a source you could not open.
> - Verify every DOI or arXiv id by fetching https://api.crossref.org/works/<DOI> or the arXiv abstract page and confirm authors/title/year match; list unverifiable identifiers separately; never construct a DOI from a pattern.
> - Give formulas explicitly (plain text is fine) with the meaning of each symbol, and numbers with units.
> - Do not include any personal or project-identifying information in web requests; use generic queries.
> - End with (i) concrete recommendations for cutting the hypothesis count or the cost per hypothesis for the receiver described above, each tied to a source and with its sensitivity cost; (ii) where the literature contradicts or refines the "what I already use" list; (iii) a not-accessed list.
