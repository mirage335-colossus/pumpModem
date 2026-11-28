# Sub-9 kHz FSK rate and spacing: Monte Carlo summary

Condensed from the `coherent_block_mc` outputs by `summarize_mc.py`. Block E/N0 in dB at 1% wrong decodes;
every format lasts 87.4 minutes. `1/64 ladder` is the best window among those of 1/64 of the block (82 s)
and longer together with per-tone reception, i.e. the specification's default ladder depth.

## steady

| format | one cell | per tone | best window | window picked per block | 1/64 ladder |
|---|---:|---:|---:|---:|---:|
| A: 6.25 Hz spacing, 0.16 s tone, 4 positions (25 Hz) | 30.9 | 30.9 | 13.6 (87.4 min) | 13.9 | 13.6 |
| a: 6.25 Hz spacing, 0.16 s tone, 8 positions (50 Hz) | 30.4 | 30.4 | 13.1 (87.4 min) | 13.7 | 13.1 |
| I: 6.25 Hz spacing, 2.56 s held tone, 4 positions (25 Hz) | 30.7 | 25.1 | 13.6 (87.4 min) | 13.9 | 13.6 |
| B: 1.5625 Hz spacing, 0.64 s tone, 16 positions (25 Hz) | 27.4 | 27.4 | 13.0 (87.4 min) | 13.7 | 13.0 |
| b: 1.5625 Hz spacing, 0.64 s tone, 8 positions (12.5 Hz) | 27.7 | 27.7 | 13.1 (87.4 min) | 13.8 | 13.1 |
| C: 1.5625 Hz spacing, 2.56 s held tone, 16 positions (25 Hz) | 27.4 | 24.5 | 13.0 (87.4 min) | 13.5 | 13.0 |
| D: 1.5625 Hz spacing, 10.24 s held tone, 16 positions (25 Hz) | 27.3 | 21.7 | 12.9 (87.4 min) | 13.6 | 12.9 |
| E: 1.5625 Hz spacing, 40.96 s held tone, 16 positions (25 Hz) | 27.4 | 19.2 | 12.9 (87.4 min) | 13.6 | 12.9 |
| F: 0.78125 Hz spacing, 1.28 s tone, 16 positions (12.5 Hz) | 26.0 | 26.0 | 12.8 (87.4 min) | 13.6 | 12.8 |
| f: 0.78125 Hz spacing, 10.24 s held tone, 16 positions (12.5 Hz) | 26.1 | 21.8 | 12.9 (87.4 min) | 13.6 | 12.9 |
| G: 0.390625 Hz spacing, 2.56 s tone, 16 positions (6.25 Hz) | 24.5 | 24.5 | 12.8 (87.4 min) | 13.5 | 12.8 |
| H: 0.0977 Hz spacing, 10.24 s tone, 16 positions (1.56 Hz) | 21.9 | 21.9 | 13.1 (87.4 min) | 13.7 | 13.1 |

## phase wander 30 deg rms, 1 h correlation (quiet night)

| format | one cell | per tone | best window | window picked per block | 1/64 ladder |
|---|---:|---:|---:|---:|---:|
| A: 6.25 Hz spacing, 0.16 s tone, 4 positions (25 Hz) | 31.0 | 31.0 | 14.0 (87.4 min) | 14.2 | 14.0 |
| a: 6.25 Hz spacing, 0.16 s tone, 8 positions (50 Hz) | 30.6 | 30.6 | 13.4 (87.4 min) | 14.2 | 13.4 |
| I: 6.25 Hz spacing, 2.56 s held tone, 4 positions (25 Hz) | 30.9 | 25.0 | 14.2 (87.4 min) | 14.3 | 14.2 |
| B: 1.5625 Hz spacing, 0.64 s tone, 16 positions (25 Hz) | 27.4 | 27.4 | 13.4 (87.4 min) | 14.0 | 13.4 |
| b: 1.5625 Hz spacing, 0.64 s tone, 8 positions (12.5 Hz) | 27.7 | 27.7 | 13.4 (87.4 min) | 14.1 | 13.4 |
| C: 1.5625 Hz spacing, 2.56 s held tone, 16 positions (25 Hz) | 27.4 | 24.5 | 13.4 (87.4 min) | 14.0 | 13.4 |
| D: 1.5625 Hz spacing, 10.24 s held tone, 16 positions (25 Hz) | 27.4 | 21.8 | 13.6 (87.4 min) | 14.1 | 13.6 |
| E: 1.5625 Hz spacing, 40.96 s held tone, 16 positions (25 Hz) | 27.4 | 19.0 | 13.2 (87.4 min) | 13.8 | 13.2 |
| F: 0.78125 Hz spacing, 1.28 s tone, 16 positions (12.5 Hz) | 26.0 | 26.0 | 13.3 (87.4 min) | 14.1 | 13.3 |
| f: 0.78125 Hz spacing, 10.24 s held tone, 16 positions (12.5 Hz) | 26.0 | 21.6 | 13.2 (87.4 min) | 13.8 | 13.2 |
| G: 0.390625 Hz spacing, 2.56 s tone, 16 positions (6.25 Hz) | 24.5 | 24.5 | 13.6 (87.4 min) | 14.1 | 13.6 |
| H: 0.0977 Hz spacing, 10.24 s tone, 16 positions (1.56 Hz) | 21.8 | 21.8 | 13.6 (87.4 min) | 14.1 | 13.6 |

## phase wander 60 deg rms, 10 min correlation (disturbed night)

| format | one cell | per tone | best window | window picked per block | 1/64 ladder |
|---|---:|---:|---:|---:|---:|
| A: 6.25 Hz spacing, 0.16 s tone, 4 positions (25 Hz) | 30.7 | 30.7 | 16.3 (10.9 min) | 16.4 | 16.3 |
| a: 6.25 Hz spacing, 0.16 s tone, 8 positions (50 Hz) | 30.5 | 30.5 | 16.0 (10.9 min) | 16.0 | 16.0 |
| I: 6.25 Hz spacing, 2.56 s held tone, 4 positions (25 Hz) | 31.0 | 25.1 | 16.6 (10.9 min) | 16.4 | 16.6 |
| B: 1.5625 Hz spacing, 0.64 s tone, 16 positions (25 Hz) | 27.4 | 27.4 | 16.0 (10.9 min) | 16.0 | 16.0 |
| b: 1.5625 Hz spacing, 0.64 s tone, 8 positions (12.5 Hz) | 27.5 | 27.5 | 16.2 (10.9 min) | 16.2 | 16.2 |
| C: 1.5625 Hz spacing, 2.56 s held tone, 16 positions (25 Hz) | 27.3 | 24.4 | 16.2 (10.9 min) | 16.1 | 16.2 |
| D: 1.5625 Hz spacing, 10.24 s held tone, 16 positions (25 Hz) | 27.4 | 21.8 | 16.4 (10.9 min) | 16.4 | 16.4 |
| E: 1.5625 Hz spacing, 40.96 s held tone, 16 positions (25 Hz) | 27.4 | 19.3 | 16.1 (10.9 min) | 16.2 | 16.1 |
| F: 0.78125 Hz spacing, 1.28 s tone, 16 positions (12.5 Hz) | 26.1 | 26.1 | 16.3 (10.9 min) | 16.0 | 16.3 |
| f: 0.78125 Hz spacing, 10.24 s held tone, 16 positions (12.5 Hz) | 26.0 | 21.8 | 16.2 (10.9 min) | 16.1 | 16.2 |
| G: 0.390625 Hz spacing, 2.56 s tone, 16 positions (6.25 Hz) | 24.6 | 24.6 | 16.2 (10.9 min) | 16.2 | 16.2 |
| H: 0.0977 Hz spacing, 10.24 s tone, 16 positions (1.56 Hz) | 21.7 | 21.7 | 16.3 (10.9 min) | 16.3 | 16.3 |

## frequency wander within +-50 mHz (Sub9 worst bound)

| format | one cell | per tone | best window | window picked per block | 1/64 ladder |
|---|---:|---:|---:|---:|---:|
| A: 6.25 Hz spacing, 0.16 s tone, 4 positions (25 Hz) | 30.9 | 30.9 | 22.7 (10.24 s) | 22.8 | 26.5 |
| a: 6.25 Hz spacing, 0.16 s tone, 8 positions (50 Hz) | 30.5 | 30.5 | 22.6 (10.24 s) | 22.7 | 26.6 |
| I: 6.25 Hz spacing, 2.56 s held tone, 4 positions (25 Hz) | 30.9 | 25.1 | 22.7 (10.24 s) | 22.9 | 25.1 |
| B: 1.5625 Hz spacing, 0.64 s tone, 16 positions (25 Hz) | 27.4 | 27.4 | 22.6 (10.24 s) | 22.8 | 26.5 |
| b: 1.5625 Hz spacing, 0.64 s tone, 8 positions (12.5 Hz) | 27.7 | 27.7 | 22.4 (10.24 s) | 22.7 | 26.4 |
| C: 1.5625 Hz spacing, 2.56 s held tone, 16 positions (25 Hz) | 27.4 | 24.7 | 22.5 (10.24 s) | 22.8 | 24.7 |
| D: 1.5625 Hz spacing, 10.24 s held tone, 16 positions (25 Hz) | 27.4 | 22.6 | 22.6 (10.24 s) | 22.8 | 22.6 |
| E: 1.5625 Hz spacing, 40.96 s held tone, 16 positions (25 Hz) | 27.6 | 24.8 | 22.5 (10.24 s) | 22.8 | 24.8 |
| F: 0.78125 Hz spacing, 1.28 s tone, 16 positions (12.5 Hz) | 26.0 | 26.0 | 22.4 (10.24 s) | 22.7 | 26.0 |
| f: 0.78125 Hz spacing, 10.24 s held tone, 16 positions (12.5 Hz) | 26.0 | 22.6 | 22.6 (10.24 s) | 22.8 | 22.6 |
| G: 0.390625 Hz spacing, 2.56 s tone, 16 positions (6.25 Hz) | 24.6 | 24.6 | 22.5 (10.24 s) | 22.7 | 24.6 |
| H: 0.0977 Hz spacing, 10.24 s tone, 16 positions (1.56 Hz) | 22.6 | 22.6 | 22.6 (10.24 s) | 22.8 | 22.6 |

## Rayleigh, 0.5 Hz Gaussian spread (outside every sub-9 kHz record)

| format | one cell | per tone | best window | window picked per block | 1/64 ladder |
|---|---:|---:|---:|---:|---:|
| A: 6.25 Hz spacing, 0.16 s tone, 4 positions (25 Hz) | 30.9 | 30.9 | 27.4 (1.28 s) | 27.5 | 30.9 |
| a: 6.25 Hz spacing, 0.16 s tone, 8 positions (50 Hz) | 30.5 | 30.5 | 27.2 (1.28 s) | 27.4 | 30.5 |
| I: 6.25 Hz spacing, 2.56 s held tone, 4 positions (25 Hz) | 31.0 | 28.4 | 27.9 (1.28 s) | 27.9 | 28.4 |
| B: 1.5625 Hz spacing, 0.64 s tone, 16 positions (25 Hz) | 27.8 | 27.8 | 27.2 (1.28 s) | 27.2 | 27.8 |
| b: 1.5625 Hz spacing, 0.64 s tone, 8 positions (12.5 Hz) | 28.0 | 28.0 | 27.4 (1.28 s) | 27.5 | 28.0 |
| C: 1.5625 Hz spacing, 2.56 s held tone, 16 positions (25 Hz) | 27.9 | 27.9 | 27.3 (1.28 s) | 27.5 | 27.9 |
| D: 1.5625 Hz spacing, 10.24 s held tone, 16 positions (25 Hz) | 27.8 | 31.7 | 27.1 (1.28 s) | 27.3 | 31.7 |
| E: 1.5625 Hz spacing, 40.96 s held tone, 16 positions (25 Hz) | 27.8 | 41.9 | 27.2 (1.28 s) | 27.3 | 41.9 |
| F: 0.78125 Hz spacing, 1.28 s tone, 16 positions (12.5 Hz) | 27.4 | 27.4 | 27.4 (1.28 s) | 27.6 | 27.4 |
| f: 0.78125 Hz spacing, 10.24 s held tone, 16 positions (12.5 Hz) | 27.4 | 32.1 | 27.4 (1.28 s) | 27.5 | 32.1 |
| G: 0.390625 Hz spacing, 2.56 s tone, 16 positions (6.25 Hz) | 28.4 | 28.4 | 28.4 (2.56 s) | 28.5 | 28.4 |
| H: 0.0977 Hz spacing, 10.24 s tone, 16 positions (1.56 Hz) | > | > | > | > | > |

## frequency wander within +-1 mHz (Sub9 best bound)

| format | one cell | per tone | best window | window picked per block | 1/64 ladder |
|---|---:|---:|---:|---:|---:|
| I: 6.25 Hz spacing, 2.56 s held tone, 4 positions (25 Hz) | 30.8 | 25.0 | 16.4 (5.46 min) | 16.5 | 16.4 |
| B: 1.5625 Hz spacing, 0.64 s tone, 16 positions (25 Hz) | 27.5 | 27.5 | 16.1 (5.46 min) | 16.5 | 16.1 |
| b: 1.5625 Hz spacing, 0.64 s tone, 8 positions (12.5 Hz) | 27.7 | 27.7 | 16.1 (5.46 min) | 16.4 | 16.1 |
| C: 1.5625 Hz spacing, 2.56 s held tone, 16 positions (25 Hz) | 27.4 | 24.5 | 16.1 (5.46 min) | 16.5 | 16.1 |
| D: 1.5625 Hz spacing, 10.24 s held tone, 16 positions (25 Hz) | 27.5 | 21.8 | 16.3 (5.46 min) | 16.6 | 16.3 |
| E: 1.5625 Hz spacing, 40.96 s held tone, 16 positions (25 Hz) | 27.4 | 19.3 | 16.2 (5.46 min) | 16.5 | 16.2 |
| F: 0.78125 Hz spacing, 1.28 s tone, 16 positions (12.5 Hz) | 26.1 | 26.1 | 16.2 (5.46 min) | 16.3 | 16.2 |
| f: 0.78125 Hz spacing, 10.24 s held tone, 16 positions (12.5 Hz) | 26.0 | 21.8 | 16.2 (5.46 min) | 16.5 | 16.2 |
| G: 0.390625 Hz spacing, 2.56 s tone, 16 positions (6.25 Hz) | 24.6 | 24.6 | 16.2 (5.46 min) | 16.4 | 16.2 |
| H: 0.0977 Hz spacing, 10.24 s tone, 16 positions (1.56 Hz) | 21.8 | 21.8 | 16.1 (5.46 min) | 16.3 | 16.1 |

## frequency wander within +-5 mHz (Sub9 moderate bound)

| format | one cell | per tone | best window | window picked per block | 1/64 ladder |
|---|---:|---:|---:|---:|---:|
| I: 6.25 Hz spacing, 2.56 s held tone, 4 positions (25 Hz) | 30.9 | 25.1 | 18.6 (81.92 s) | 18.7 | 18.6 |
| B: 1.5625 Hz spacing, 0.64 s tone, 16 positions (25 Hz) | 27.5 | 27.5 | 18.2 (81.92 s) | 18.5 | 18.2 |
| b: 1.5625 Hz spacing, 0.64 s tone, 8 positions (12.5 Hz) | 27.5 | 27.5 | 18.4 (81.92 s) | 18.8 | 18.4 |
| C: 1.5625 Hz spacing, 2.56 s held tone, 16 positions (25 Hz) | 27.5 | 24.6 | 18.4 (81.92 s) | 18.6 | 18.4 |
| D: 1.5625 Hz spacing, 10.24 s held tone, 16 positions (25 Hz) | 27.4 | 21.8 | 18.5 (81.92 s) | 18.8 | 18.5 |
| E: 1.5625 Hz spacing, 40.96 s held tone, 16 positions (25 Hz) | 27.5 | 19.3 | 18.5 (81.92 s) | 18.8 | 18.5 |
| F: 0.78125 Hz spacing, 1.28 s tone, 16 positions (12.5 Hz) | 26.1 | 26.1 | 18.4 (81.92 s) | 18.7 | 18.4 |
| f: 0.78125 Hz spacing, 10.24 s held tone, 16 positions (12.5 Hz) | 25.9 | 21.7 | 18.4 (81.92 s) | 18.7 | 18.4 |
| G: 0.390625 Hz spacing, 2.56 s tone, 16 positions (6.25 Hz) | 24.5 | 24.5 | 18.3 (81.92 s) | 18.6 | 18.3 |
| H: 0.0977 Hz spacing, 10.24 s tone, 16 positions (1.56 Hz) | 21.8 | 21.8 | 18.5 (81.92 s) | 18.6 | 18.5 |

## Rayleigh, 1 mHz Gaussian spread

| format | one cell | per tone | best window | window picked per block | 1/64 ladder |
|---|---:|---:|---:|---:|---:|
| I: 6.25 Hz spacing, 2.56 s held tone, 4 positions (25 Hz) | 33.0 | 26.9 | 18.2 (5.46 min) | 18.2 | 18.2 |
| B: 1.5625 Hz spacing, 0.64 s tone, 16 positions (25 Hz) | 29.5 | 29.5 | 17.9 (5.46 min) | 18.1 | 17.9 |
| b: 1.5625 Hz spacing, 0.64 s tone, 8 positions (12.5 Hz) | 29.6 | 29.6 | 17.8 (5.46 min) | 18.1 | 17.8 |
| C: 1.5625 Hz spacing, 2.56 s held tone, 16 positions (25 Hz) | 29.6 | 26.8 | 17.9 (5.46 min) | 18.2 | 17.9 |
| D: 1.5625 Hz spacing, 10.24 s held tone, 16 positions (25 Hz) | 29.4 | 23.6 | 18.1 (5.46 min) | 18.3 | 18.1 |
| E: 1.5625 Hz spacing, 40.96 s held tone, 16 positions (25 Hz) | 29.8 | 21.5 | 18.4 (5.46 min) | 18.7 | 18.4 |
| F: 0.78125 Hz spacing, 1.28 s tone, 16 positions (12.5 Hz) | 28.3 | 28.3 | 17.9 (5.46 min) | 18.2 | 17.9 |
| f: 0.78125 Hz spacing, 10.24 s held tone, 16 positions (12.5 Hz) | 27.9 | 23.7 | 18.0 (5.46 min) | 18.2 | 18.0 |
| G: 0.390625 Hz spacing, 2.56 s tone, 16 positions (6.25 Hz) | 27.0 | 27.0 | 18.2 (5.46 min) | 18.6 | 18.2 |
| H: 0.0977 Hz spacing, 10.24 s tone, 16 positions (1.56 Hz) | 23.9 | 23.9 | 18.2 (5.46 min) | 18.3 | 18.2 |

## Rayleigh, 5 mHz Gaussian spread

| format | one cell | per tone | best window | window picked per block | 1/64 ladder |
|---|---:|---:|---:|---:|---:|
| I: 6.25 Hz spacing, 2.56 s held tone, 4 positions (25 Hz) | 31.4 | 25.4 | 18.9 (81.92 s) | 19.2 | 18.9 |
| B: 1.5625 Hz spacing, 0.64 s tone, 16 positions (25 Hz) | 28.0 | 28.0 | 19.0 (81.92 s) | 19.0 | 19.0 |
| b: 1.5625 Hz spacing, 0.64 s tone, 8 positions (12.5 Hz) | 28.1 | 28.1 | 18.7 (81.92 s) | 19.2 | 18.7 |
| C: 1.5625 Hz spacing, 2.56 s held tone, 16 positions (25 Hz) | 27.9 | 25.0 | 18.8 (81.92 s) | 19.1 | 18.8 |
| D: 1.5625 Hz spacing, 10.24 s held tone, 16 positions (25 Hz) | 28.0 | 22.2 | 18.8 (81.92 s) | 19.1 | 18.8 |
| E: 1.5625 Hz spacing, 40.96 s held tone, 16 positions (25 Hz) | 27.9 | 19.7 | 19.0 (81.92 s) | 19.2 | 19.0 |
| F: 0.78125 Hz spacing, 1.28 s tone, 16 positions (12.5 Hz) | 26.6 | 26.6 | 18.7 (81.92 s) | 18.8 | 18.7 |
| f: 0.78125 Hz spacing, 10.24 s held tone, 16 positions (12.5 Hz) | 26.4 | 22.1 | 18.7 (81.92 s) | 19.0 | 18.7 |
| G: 0.390625 Hz spacing, 2.56 s tone, 16 positions (6.25 Hz) | 25.0 | 25.0 | 18.8 (81.92 s) | 19.1 | 18.8 |
| H: 0.0977 Hz spacing, 10.24 s tone, 16 positions (1.56 Hz) | 22.3 | 22.3 | 19.1 (81.92 s) | 19.0 | 19.1 |

## Rayleigh, 50 mHz Gaussian spread

| format | one cell | per tone | best window | window picked per block | 1/64 ladder |
|---|---:|---:|---:|---:|---:|
| I: 6.25 Hz spacing, 2.56 s held tone, 4 positions (25 Hz) | 30.9 | 25.2 | 22.8 (10.24 s) | 23.0 | 25.2 |
| B: 1.5625 Hz spacing, 0.64 s tone, 16 positions (25 Hz) | 27.5 | 27.5 | 22.5 (10.24 s) | 22.7 | 26.5 |
| b: 1.5625 Hz spacing, 0.64 s tone, 8 positions (12.5 Hz) | 27.6 | 27.6 | 22.7 (10.24 s) | 22.8 | 26.8 |
| C: 1.5625 Hz spacing, 2.56 s held tone, 16 positions (25 Hz) | 27.4 | 24.7 | 22.7 (10.24 s) | 22.9 | 24.7 |
| D: 1.5625 Hz spacing, 10.24 s held tone, 16 positions (25 Hz) | 27.5 | 22.6 | 22.6 (10.24 s) | 22.7 | 22.6 |
| E: 1.5625 Hz spacing, 40.96 s held tone, 16 positions (25 Hz) | 27.4 | 24.7 | 22.6 (10.24 s) | 22.9 | 24.7 |
| F: 0.78125 Hz spacing, 1.28 s tone, 16 positions (12.5 Hz) | 26.0 | 26.0 | 22.5 (10.24 s) | 22.7 | 26.0 |
| f: 0.78125 Hz spacing, 10.24 s held tone, 16 positions (12.5 Hz) | 26.0 | 22.6 | 22.6 (10.24 s) | 22.8 | 22.6 |
| G: 0.390625 Hz spacing, 2.56 s tone, 16 positions (6.25 Hz) | 24.7 | 24.7 | 22.5 (10.24 s) | 22.7 | 24.7 |
| H: 0.0977 Hz spacing, 10.24 s tone, 16 positions (1.56 Hz) | 22.7 | 22.7 | 22.7 (10.24 s) | 22.7 | 22.7 |

