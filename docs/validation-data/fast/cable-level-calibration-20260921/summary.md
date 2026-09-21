# Cable output-level measurements

Known-symbol signal/error includes noise, distortion and receiver estimation errors; it is not calibrated AWGN SNR. Raw trials test uncoded bit decisions and have no file pass/fail outcome. Codec pass requires an exact, physically completed file. BER is a pre-FEC demapped payload-bit error fraction, not a percent.

Every radio-labeled trial used the same headphone-to-microphone cable; no IC-7100 was connected. The manually selected mid-band cable trials exceed the production one-second FIFO allowance and do not qualify real-time reception.

| Trial | Mode | Bandwidth Hz | Amplitude | TX RMS / peak | Known signal/error dB | BER | Codec | FIFO max s | TX range |
|---|---|---:|---:|---:|---:|---:|---|---:|---|
| radio-a010 | raw | 2400 | 0.1 | 0.07084 / 0.2575 | 60.72 | 0 | — | 0.0402 | within range |
| radio-a020 | raw | 2400 | 0.2 | 0.1417 / 0.5151 | 62.87 | 0 | — | 0.0408 | within range |
| radio-a030 | raw | 2400 | 0.3 | 0.2125 / 0.7726 | 63.64 | 0 | — | 0.044 | within range |
| radio-a040 | raw | 2400 | 0.4 | 0.2834 / 1.03 | 63.87 | 0 | — | 0.0427 | OVERLOAD >1 |
| radio-narrow-a010 | raw | 240 | 0.1 | 0.07024 / 0.2396 | 62.38 | 0 | — | 0.041 | within range |
| radio-narrow-a020 | raw | 240 | 0.2 | 0.1405 / 0.4791 | 62.85 | 0 | — | 0.0406 | within range |
| radio-narrow-a030 | raw | 240 | 0.3 | 0.2107 / 0.7187 | 62.9 | 0 | — | 0.0407 | within range |
| radio-narrow-a040 | raw | 240 | 0.4 | 0.281 / 0.9583 | 62.8 | 0 | — | 0.0414 | within range |
| wide-a010 | codec | 18000 | 0.1 | 0.07077 / 0.2497 | 55.93 | 0.0646 | incomplete / not exact | 1.58 | within range |
| wide-a015 | codec | 18000 | 0.15 | 0.1061 / 0.3612 | 59.37 | 0.041 | incomplete / not exact | 1.51 | within range |
| wide-a020 | codec | 18000 | 0.2 | 0.1416 / 0.4888 | 61.68 | 0.0281 | incomplete / not exact | 1.47 | within range |
| wide-a025 | codec | 18000 | 0.25 | 0.1769 / 0.6195 | 63.32 | 0.0197 | pass | 0.128 | within range |
| wide-a030 | codec | 18000 | 0.3 | 0.2122 / 0.7555 | 64.59 | 0.0139 | pass | 0.107 | within range |
| wide-a030-b1000000 | codec | 18000 | 0.3 | 0.2132 / 0.8594 | 64.5 | 0.0143 | pass | 0.107 | within range |
| wide-a035 | codec | 18000 | 0.35 | 0.2476 / 0.8727 | 65.45 | 0.0105 | pass | 0.0854 | within range |
| wide-a035-b1000000 | codec | 18000 | 0.35 | 0.2488 / 1.021 | 65.41 | 0.0103 | pass | 0.102 | OVERLOAD >1 |
| wide-a040 | codec | 18000 | 0.4 | 0.2831 / 0.9997 | 66.14 | 0.008 | pass | 0.064 | near full only |
| wide-a040-b1000000 | codec | 18000 | 0.4 | 0.2843 / 1.136 | 63.35 | 0.00747 | pass | 0.235 | OVERLOAD >1 |
| mid-a010 | raw | 1800 | 0.1 | 0.06832 / 0.2618 | 65.43 | 0.0103 | — | 4.49 | within range |
| mid-a020 | raw | 1800 | 0.2 | 0.1366 / 0.5236 | 69.49 | 0.00132 | — | 4.61 | within range |
| mid-a030 | raw | 1800 | 0.3 | 0.205 / 0.7854 | 70.99 | 0.000443 | — | 4.64 | within range |
| mid-a040 | raw | 1800 | 0.4 | 0.2733 / 1.047 | 70.42 | 0.000519 | — | 4.51 | OVERLOAD >1 |
| narrow-a010 | raw | 180 | 0.1 | 0.06522 / 0.2639 | 68.87 | 0.00195 | — | 0.0409 | within range |
| narrow-a020 | raw | 180 | 0.2 | 0.1304 / 0.5277 | 69.52 | 0.000946 | — | 0.0402 | within range |
| narrow-a030 | raw | 180 | 0.3 | 0.1956 / 0.7916 | 70.35 | 0.000427 | — | 0.0416 | within range |
| narrow-a040 | raw | 180 | 0.4 | 0.2609 / 1.055 | 68.77 | 0.000824 | — | 0.0409 | OVERLOAD >1 |

Generated peak >1 proves digital overload. A peak from .999 through 1 is only near full scale. The probe’s legacy `clipped` count uses the .999 threshold; it is exported as `tx_near_full_samples`, not an overload count.

## Separate 997 Hz tone measurements

Steady-window fitted fundamental gain is captured PCM RMS divided by generated sine PCM RMS. It includes the production capture routing and gain, is not a separate analog-voltage calibration, and is separate from the normalized modem constellation gain.

| TX sine peak | Fitted chain gain dB | SINAD 300–18300 Hz dB | SINAD 20–20000 Hz dB |
|---:|---:|---:|---:|
| 0.03 | -13.6795 | 47.627 | 46.686 |
| 0.06 | -13.6768 | 53.679 | 52.704 |
| 0.12 | -13.6743 | 59.764 | 58.822 |
| 0.25 | -13.6732 | 65.988 | 65.114 |
| 0.35 | -13.6733 | 68.85 | 67.91 |
| 0.5 | -13.6733 | 72.125 | 71.167 |
| 0.7 | -13.673 | 74.851 | 73.888 |
| 0.9 | -13.6728 | 76.794 | 75.835 |

Fitted gain spread over 8 steady levels: 0.0066591 dB.

Omitted incomplete/unreadable probe summaries: 0. Missing/incomplete/stale known-symbol analyses: 0.
