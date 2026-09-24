# Live and replay outcomes

The SNR values in filenames identify automatic presets, not calibrated live SNR. The delay replay adds an artificial timing step to a real recording.

| Trial | Live | Depth | Source bytes | Exact | RX/TX intervals | Physical end, capture s | LDPC failures | Max FIFO, s |
|---|---|---:|---:|---|---|---:|---:|---:|
| baseline-snr0-final-replay | No (replay) | 8 | 60 | 1 | 508/508 | 167.800 | 0 | 0.000 |
| baseline-snr0 | Yes | 8 | 60 | 1 | 508/508 | 167.785 | 0 | 0.107 |
| baseline-snr3-final-replay | No (replay) | 8 | 60 | 1 | 508/508 | 92.550 | 0 | 0.000 |
| baseline-snr3 | Yes | 8 | 60 | 1 | 508/508 | 92.522 | 0 | 0.149 |
| delay-snr0-baseline | No (replay) | 8 | 60 | 0 | 26/508 | 27.250 | 0 | 0.000 |
| delay-snr0-final-replay | No (replay) | 8 | 60 | 1 | 508/508 | 167.800 | 0 | 0.000 |
| fixed-snr0-b20000 | Yes | 1 | 20000 | 1 | 160/160 | 72.532 | 0 | 0.301 |
| fixed-snr0-b60 | Yes | 1 | 60 | 1 | 64/64 | 40.277 | 0 | 0.384 |
| fixed-snr3-b20000 | Yes | 2 | 20000 | 1 | 192/192 | 48.724 | 0 | 0.320 |
| fixed-snr3-b60 | Yes | 2 | 60 | 1 | 128/128 | 38.762 | 0 | 0.149 |
