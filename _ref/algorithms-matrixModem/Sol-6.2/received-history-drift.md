# Received-history correlation under carrier drift

The [narrowband follow-up](tensor-receiver-bottlenecks.md#where-channel-filtering-changes-the-calculation) uses the same capture generator with a matched guard, a physical low-pass FIR and covariance-aware scoring.

## What cancels between repeats

Let the repeated pattern occupy time `T`, with corresponding samples a time `L` apart. After aligning the two windows, multiply the recent window by the complex conjugate of the earlier one. If a single common oscillator phase is `φ(t) = 2π(f₀ t + a t²/2)`, the phase of the signal part of that product is

```text
φ(t + L) − φ(t) = 2π(f₀ L + a L²/2) + 2π(a L)t .
```

The magnitude of the coherent sum removes the constant term containing the unknown initial CFO `f₀`. A linear rate `a` in Hz/s leaves a relative frequency `Δf = aL`. A search over `Δf` can therefore be much smaller than a search over absolute CFO. For a rectangular coherent window and otherwise identical copies, zero drift correction leaves a squared-amplitude factor `sinc²(Δf T)`. To keep loss below approximately 0.9 dB on grid error alone, keep residual `|Δf| ≤ 0.25/T`, for example by using a grid step of `0.5/T`. If the drift uncertainty is small enough, the zero-difference hypothesis suffices. A phase crossing of multiple turns can make a long uncompensated window ineffective even when the absolute frequency is precisely known.

This calculation assumes matching timing, the same transmitted phase trajectory up to a constant phase, a common rate across the pattern, and sufficient receive passband margin for both copies. For a waveform with symbol phase resets the same reset phases must recur or the coherent sum must respect the reset boundaries. Dropped samples, sample-rate drift, switched oscillators, higher-order phase and fading that changes the multipath response can all break the model. A detected repeat establishes relative consistency. Payload and absolute frequency still need to be inferred or separately known. The principle has a precedent in repeated-symbol frequency synchronization. [Moose, IEEE Transactions on Communications, 1994](https://calhoun.nps.edu/bitstream/handle/10945/62145/Moose_Technique_for_Orthogonal_Frequency.pdf?sequence=1)

## Microwave paths

EME can combine oscillator error, a large predictable common Doppler shift, and a time-varying spread of Doppler and delay from different lunar patches. Predicting the common shift using ephemerides can reduce the search burden, leaving short-term residuals to the receiver. A diffuse lunar return can lose coherence even with a GPS-disciplined station reference and accurate ephemeris. Its coherence depends on geometry and the selected delay/frequency region. Taylor measured echo spectra at 144 and 432 MHz and gives a calculated full-disk spread as large as 300 Hz at 10 GHz. Independent bistatic lunar X-band experiments measured echo spectra with ephemeris-based frequency prediction. These results support keeping mean shift, differential spread and delay as separate axes. The experimental drift model here addresses the common phase term. [Taylor, lunar echo measurements](https://wsjt.sourceforge.io/LunarEchoes_QEX.pdf), [bistatic lunar X-band measurements](https://pmc.ncbi.nlm.nih.gov/articles/PMC7180880/), [broadband EME channel study, indexed abstract](https://jeit.ac.cn/en/article/doi/10.11999/JEIT251028), [WSJT-X Doppler tracking](https://wsjt.sourceforge.io/wsjtx-doc/wsjtx-main-2.7.0.html#VHF_FEATURES)

Terrestrial microwave troposcatter is a different propagation path. Even for disciplined radios, distributed scatterers can change the amplitude and complex phase over the repeat interval. Relevant field parameters are the post-isolation signal-to-noise ratio, coherence across both windows, relative delay change, and whether a persistent component dominates. The Canadian high-Arctic field study and the ITU's trans-horizon design guidance describe rapid fading and frequency/delay spreading on troposcatter paths. They do not supply a universal frequency-drift range for synchronizing an arbitrary pair of radios. [Canadian high-Arctic troposcatter experiment](https://publications.gc.ca/collections/collection_2020/isde-ised/Co24/Co24-3-7-94-002-eng.pdf), [ITU-R P.617-6](https://www.itu.int/rec/R-REC-P.617-6-202509-I/en)

## Costs of the smaller search

For independent noise on aligned copies `x=s+n₁` and `y=s+n₂`, the raw product contains signal times noise and noise times noise. With `E=‖s‖²`, noise variance `ν` per complex sample, and `N` samples, `Var(xᴴy)=2νE+Nν²`. A static matched filter with the correct template does not incur the final term. A smaller search and better preservation of unknown persistent distortion can make received history useful, but the benefit needs matched-energy tests against this noisy-reference cost. Channelizing or whitening before correlating can reduce the noise dimension. Multiple overlapping windows and all pairs from repeated observations share noise and cannot be counted as independent constraints.

For one lag the drift search has a useful exact factorization. Form `u[n]=conj(earlier[n])*recent[n]` once, then evaluate its Fourier projection at each relative frequency. With a handful of hypotheses, a small loop or fused GPU reduction can suffice. With many regular hypotheses a short Fourier transform can reuse the products. Complex GEMM is attractive when a search supplies many useful recent and previous windows with dense pairings, so every row of the recent-window bank can reuse many historical references. One known alignment and a few relative frequencies give a thin matrix or a reduction. Running a large dense product for all pairs would change the useful receive workload. The live alternative calls for the same results, including unsuccessful hypotheses, with preparation, readback and latency included.

## Measured oscillator-drift fixture

Two identical eight-tone CPFSK motifs span 0.64 s each at 3,200 complex samples/s. Their alignment is configured. Unknown grid carrier offset varies over ±25 Hz. Rates of −2.4414, 0 and +2.4414 Hz/s produce relative offsets of −1.5625, 0 and +1.5625 Hz. Independent proper complex white noise is added on each copy. The history detector gets the two IQ captures, their alignment, and three relative bins, with the actual transmitted motif hidden. The known-template detector is given the motif and tests 33 absolute offsets × three rates using both received copies. It takes advantage of this extra information.

The pair statistic is the squared correlation magnitude divided by earlier-window observed energy. Under independent unit-variance white noise, the score for each fixed relative bin is exponential when conditioned on the earlier window. The known-template score uses the corresponding exponential scale derived from its known template energy. A threshold of `log(number of queries / 0.001)` bounds the probability of any false detection at a fixed opportunity under this idealized null. Detection counts and correct drift labels measure different outcomes.

| Fixed-seed scenario | History, zero drift present / correct | History, 3 bins present / correct | Known motif, 99 hypotheses present / correct |
| --- | ---: | ---: | ---: |
| +10 dB-Hz, 48 captures | 0 / 0 | 0 / 0 | 32 / 30 |
| +20 dB-Hz, 48 captures | 0 / 0 | 1 / 1 | 48 / 48 |
| +28 dB-Hz, 48 captures | 15 / 15 | 48 / 48 | 48 / 48 |
| +28 dB-Hz, fractional CFO and rate, 48 captures | 18 / 18 | 48 / 48 | 48 / 0 |
| +28 dB-Hz, halfway 180° phase step in the recent copy, 48 captures | 33 / 0 | 48 / 0 | 48 / 35 |
| White noise, 192 captures | 0 / — | 0 / — | 1 / — |

All 16 clean cases recovered the exact relative bin, and adding a common 613.25 Hz offset did not change any of their correlation magnitudes. The fractional holdout uses +0.22 of an absolute CFO bin and +0.13 of a drift bin relative to the nominal bin labels. The static detector's coarse CFO and rate estimates trade off against one another on that grid. The test does not establish an inherent static-template drift error. A centered or refined grid should change that result. The phase step creates two half-windows whose signal cross-products cancel at the correct bin. The two histories are still individually energetic, so a strong score in the wrong relative bin is possible. A single detected repeat is insufficient to assert a correct relative shift.

The three-bin raw detector's weak-signal result is consistent with the noise × noise term. At +20 dB-Hz, the ideal expected coherent signal energy in each clean 0.64 s window is 64 noise units. Even before searching, the variance of the complex raw dot product is `2 × 64 + 2048 = 2176` under the stated noise normalization. The fixed waveform and short lag illustrate CFO cancellation. They do not simulate the stochastic channel or oscillator time series of a lunar or troposcatter link.

The static comparison also uses the factored route. It removes the known motif, multiplies by the conjugate chirp for each rate, and calculates all 33 CFO hypotheses with one 4,096-point FFT per rate. Direct and FFT scores match across the 99 hypotheses of every clean and noisy fixture. An already-known motif can preserve the sensitivity advantage without one explicit sample dot product per absolute carrier offset.

## Arithmetic and latency

The raw GPU comparator puts the real and imaginary parts of the prior window in two rows, and the real and imaginary parts of the three corrected recent windows in six columns. FP16 operands and FP32 accumulation produce a GEMM of shape `2 × 6 × 2,048`. The 12 real dots reconstruct three complex correlations. It produces exactly the same three candidate scores that the CPU pointwise method uses.

Across all 432 noisy trials, the host-staged GPU and CPU selected the same accept decisions and accepted drift labels. The largest complex dot difference was (1.87 × 10⁻⁵) after dividing by the product of the input window norms. On the Blackwell pod, mean processing times over each stratum ranged from **8.19–8.53 µs** for the native CPU loop and **159.6–162.8 µs** for the GPU implementation, including operand packing, transfers, the GEMM, and the score selection. Device-resident inputs could reduce the transfer cost. The thin shape still has only 12 useful real dot products, and Tensor Core execution for this shape was not measured. A separate native `g++ -O3` run found 149–160 µs for the factored known-template path versus 396–435 µs direct. On the pod the same factored CPU code, built inside the CUDA translation unit, took 562–564 µs versus 397–407 µs direct. The FFT is an exact alternative, with no portable speedup established by these two builds.

This is a concrete example where reducing the frequency search also reduces the work available to a dense accelerator. A bank of many physically motivated delay hypotheses, reference windows or active channels could produce a better GEMM shape. It would need its own full-work comparison against exact reuse of pointwise products and filter-bank projections. The [codeword experiment](tensor-reception-results.md) shows what a legitimate large matrix does on the same GPU. It does not transfer its activity figure to the thin drift search. [Native experiment and commands](../../../_appraisal/algorithms-matrixModem/tensor-reception/README.md)

The `matrix_drift` executable measured on the full SM120 RunPod device used CUDA 12.8.93 and driver 595.112.07, with SHA-256 `e4ad31c7affee632311ea81032ea65def6f9f17c8b798ab7a41db0b19bfcb285`.
