# Frequency leakage at the edges of the 16-position grid

## Fixed experiment

Use format H of `coherent_block_mc.cpp` (512 tones, 10.24 s per tone, 16 positions) and its channel 9 (48 equal-power components with Gaussian Doppler frequencies of standard deviation 0.25 Hz). Test one fixed schedule, 200 independent noise/channel realizations, one OpenMP thread, no antenna filter, no value-dependent phase. Compare the former 16-point approximation with 128, 256 and 512 subdivisions using the same payloads, oscillator draws and thermal noise for every resolution. The declared outcomes are the error count of the per-tone energy decoder and of the per-block rung selector at the largest tested energy, 44 dB, plus the exploratory grid crossing if one occurs. Use the existing 6–44 dB grid. Stop after these four runs. Paired Monte Carlo checks can identify a large numerical effect. Two hundred trials do not precisely qualify the proposed 1% block-error threshold.

As an independent deterministic check evaluate the expected projected signal power in the correct, adjacent and farthest bins at each resolution against the continuous-time Gaussian covariance. Include the equal-energy image of a single component one bin above the transmitted edge. This checks a physical ambiguity in the simulation even if the Monte Carlo decision does not change.

After the four fixed runs, repeat their inputs once with the analytic integral of each simulated ray. Compare its complex cell projections against a separately sampled reference using on- and off-grid rays over three changing positions.

## Results and reproduction

With 16 points per cell the discrete projection of a component at the upper edge position 15 shifted up one bin into the opposite position 0 has magnitude one. The sampled exponent makes precisely one cycle between successive samples. Its actual integral over the cell has 16 complete cycles and vanishes. This is leakage in the synthesized signal. The simulator draws receiver noise independently for each orthogonal bin.

For the channel-9 Doppler law let `s = 0.25 Hz × 10.24 s = 2.56` in cycles per cell. Averaging over component frequencies and independent phases gives correlation `r(u) = exp(−2π²s²u²)` at lag `u` cells. Expected mean-square complex projection at an integer position difference `k` is

```text
continuous: 2 ∫₀¹ (1−u) r(u) cos(2πku) du
M points:   [M + 2 Σ(j=1…M−1) (M−j) r(j/M) cos(2πkj/M)] / M² .
```

| Points per cell | Correct bin | Adjacent bin | Opposite edge, 15 bins away | Edge / correct | Wrong per-tone / selected at 44 dB, out of 200 |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 16, original | 0.148795 | 0.138476 | 0.138476 | −0.31 dB | 10 / 10 |
| 128 | 0.148117 | 0.137791 | 0.000259 | −27.57 dB | 5 / 5 |
| 256 | 0.148109 | 0.137783 | 0.000251 | −27.71 dB | 5 / 5 |
| 512 | 0.148107 | 0.137781 | 0.000249 | −27.74 dB | 5 / 5 |
| Continuous integral, corrected default | 0.148107 | 0.137781 | 0.000249 | −27.75 dB | 5 / 5 |

The numerical runs retain the historical quadrature convention, sampling the same channel trajectories at subinterval starts and the tone-reference phases at subinterval midpoints. The stagger changes finite-resolution complex projections and vanishes in the continuous integral. The table’s expected bin powers are invariant to the fixed per-frequency basis phase factor.

Composite Simpson integration with 8,192 and 16,384 intervals agreed to better than 7 × 10⁻¹⁴ in absolute power. For the error probabilities at 44 dB, 95% Wilson intervals are 2.74–8.96% (16 points) and 1.07–5.72% (each finer resolution). These are pointwise intervals for this fixed schedule. All four runs stayed above the empirical 1% error criterion through the 6–44 dB grid. The original 2,000-block result recorded the same missed crossing. At 16 points the error in the distant-bin power is large, and some decoded decisions changed under refinement. The remaining errors demonstrate a practical weakness of this tested detector. They leave the limiting error at arbitrarily high energy, and the performance of other detectors, open. [Historical summary](results/coherent_block_summary.md)

Run from this directory with GCC 14.2, using one numerical thread to reproduce the same sequence of random draws at every resolution.

```sh
g++ -O3 -march=native -std=c++20 -fopenmp coherent_block_mc.cpp -o /tmp/mm-fsk-quadrature
for points in 16 128 256 512; do
    OMP_NUM_THREADS=1 /tmp/mm-fsk-quadrature 200 H 9 0 0 "$points"
done
OMP_NUM_THREADS=1 /tmp/mm-fsk-quadrature 200 H 9 0 0
g++ -O3 -march=native -std=c++20 -fopenmp ray_projection_check.cpp -o /tmp/mm-fsk-ray-check
/tmp/mm-fsk-ray-check
```

For the 48-ray simulator each ray carries a known phase and fixed frequency. At position difference `d` and ray offset `b` cycles per cell, integrate the complex exponential exactly as `exp(iπ(b+d)) sinc(b+d)` with `sinc(x)=sin(πx)/(πx)`. The program builds these weights once per trial and advances each ray by `exp(2πib)` per cell. This avoids the expensive subdivided propagation. Against three independent 32,768-sample integrals over all 16 outputs, including a ray one bin above the edge, the largest complex projection discrepancy was 2.56 × 10⁻⁹ on float outputs. The analytic route has the same aggregate errors as the three finer integrations. All use the original 48-component channel model and the original normally distributed receiver noise.

The existing nearly flat-gain shortcut serves low-spread cases. Other time-varying channel laws use 16 quadrature points per position where within-cell variation is modeled. Their phase and frequency wander laws and cell geometries are unchanged. Other formats and channel laws retain their historical tables and need evidence appropriate to their own quadrature and noise model. In particular, this 0.5 Hz stress case was already outside the proposed sub-9 kHz path envelope.
