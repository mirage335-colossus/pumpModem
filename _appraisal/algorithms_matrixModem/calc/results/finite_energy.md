## One-shot orthogonal signaling: Eb/N0 (dB) for word error eps, M = 2^k, AWGN

| k | coherent 1e-2 | coherent 1e-3 | noncoherent 1e-2 | noncoherent 1e-3 | noncoherent penalty @1e-3 (dB) |
|---:|---:|---:|---:|---:|---:|
| 1 | 7.33 | 9.80 | 8.93 | 10.94 | 1.14 |
| 2 | 5.57 | 7.60 | 6.89 | 8.61 | 1.01 |
| 4 | 3.91 | 5.56 | 4.97 | 6.41 | 0.85 |
| 8 | 2.53 | 3.83 | 3.33 | 4.50 | 0.68 |
| 12 | 1.89 | 3.00 | 2.53 | 3.56 | 0.57 |
| 16 | 1.50 | 2.48 | 2.04 | 2.97 | 0.49 |
| 20 | 1.22 | 2.13 | 1.70 | 2.56 | 0.43 |
| 24 | 1.02 | 1.86 | 1.44 | 2.25 | 0.39 |
| 32 | 0.72 | 1.47 | 1.07 | 1.80 | 0.32 |
| 48 | 0.36 | 1.00 | 0.63 | 1.25 | 0.25 |
| 64 | 0.14 | 0.70 | 0.35 | 0.91 | 0.20 |
| 128 | -0.31 | 0.11 | -0.18 | 0.23 | 0.12 |

## Fade margin (dB) when message energy is averaged over N_eff independent Rayleigh fades

Margin = -10 log10 of the eps-quantile of Gamma(N_eff, 1/N_eff); this is the power margin over the mean that keeps outage below eps for an ideal energy-averaging code.

| N_eff | eps=1e-1 | 1e-2 | 1e-3 | 1e-4 |
|---:|---:|---:|---:|---:|
| 1 | 9.77 | 19.98 | 30.00 | 40.00 |
| 2 | 5.75 | 11.29 | 16.44 | 21.48 |
| 4 | 3.60 | 6.87 | 9.70 | 12.37 |
| 8 | 2.35 | 4.40 | 6.08 | 7.61 |
| 16 | 1.57 | 2.91 | 3.98 | 4.91 |
| 32 | 1.07 | 1.97 | 2.67 | 3.27 |
| 64 | 0.74 | 1.35 | 1.82 | 2.22 |
| 128 | 0.51 | 0.94 | 1.26 | 1.52 |
| 256 | 0.36 | 0.65 | 0.87 | 1.06 |
| 1024 | 0.18 | 0.32 | 0.43 | 0.52 |
| 4096 | 0.09 | 0.16 | 0.21 | 0.26 |

## Log-normal slow fading that is constant over the message: margin = z_eps * sigma_dB

| sigma (dB) | eps=1e-1 | 1e-2 | 1e-3 |
|---:|---:|---:|---:|
| 2 | 2.6 | 4.7 | 6.2 |
| 4 | 5.1 | 9.3 | 12.4 |
| 6 | 7.7 | 14.0 | 18.5 |
| 8 | 10.3 | 18.6 | 24.7 |
| 10 | 12.8 | 23.3 | 30.9 |
