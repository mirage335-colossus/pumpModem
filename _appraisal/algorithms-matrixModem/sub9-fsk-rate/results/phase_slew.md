# Slewed keyed phase through a high-Q antenna

Theoretical: single-tuned antenna, constant-amplitude drive, sixteen positions 0.78125 Hz apart
centred on the resonance, random positions and keyed quarter turns, linear phase ramp of length S
at the start of each tone. 1,000 tones of 10.24 s or 8,000 tones of 1.28 s per row. Power figures
are for 10 kW delivered on resonance. Assumptions are in the head of `phase_slew.cpp`.

Static reference, steady tone on an outer position (5.86 Hz from resonance): angle between drive and
current, and standing-wave ratio against the resonant resistance:

| Antenna | Angle | VSWR |
| ---: | ---: | ---: |
| 24 Hz | 26.0 deg | 1.62 |
| 57 Hz | 11.6 deg | 1.23 |
| 86 Hz | 7.8 deg | 1.15 |
| 174 Hz | 3.9 deg | 1.07 |

## Tone of 8 cells = 10.24 s

### Receiver: loss in dB

Coherent sum over tones with one phase for all positions, for a receiver whose reference follows
the ramp, one that assumes a plain step at the start of the tone, and one that leaves the ramp
interval out; then the mean energy per tone for the first two.

| Antenna | Ramp S | Coherent, ramp known | Coherent, step assumed | Coherent, ramp left out | Per tone, ramp known | Per tone, step assumed |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 24 Hz | 0 ms | 0.728 | 0.728 | 0.728 | 0.370 | 0.370 |
| 24 Hz | 20 ms | 0.725 | 0.735 | 0.739 | 0.368 | 0.377 |
| 24 Hz | 40 ms | 0.723 | 0.743 | 0.753 | 0.366 | 0.385 |
| 24 Hz | 80 ms | 0.721 | 0.758 | 0.786 | 0.364 | 0.400 |
| 24 Hz | 160 ms | 0.719 | 0.788 | 0.855 | 0.362 | 0.430 |
| 57 Hz | 0 ms | 0.143 | 0.143 | 0.143 | 0.074 | 0.074 |
| 57 Hz | 20 ms | 0.141 | 0.151 | 0.156 | 0.072 | 0.082 |
| 57 Hz | 40 ms | 0.140 | 0.158 | 0.173 | 0.071 | 0.089 |
| 57 Hz | 80 ms | 0.140 | 0.174 | 0.207 | 0.070 | 0.104 |
| 57 Hz | 160 ms | 0.139 | 0.204 | 0.275 | 0.070 | 0.135 |
| 86 Hz | 0 ms | 0.065 | 0.065 | 0.065 | 0.034 | 0.034 |
| 86 Hz | 20 ms | 0.063 | 0.072 | 0.079 | 0.032 | 0.042 |
| 86 Hz | 40 ms | 0.063 | 0.080 | 0.096 | 0.032 | 0.049 |
| 86 Hz | 80 ms | 0.062 | 0.095 | 0.130 | 0.031 | 0.064 |
| 86 Hz | 160 ms | 0.062 | 0.126 | 0.198 | 0.031 | 0.095 |
| 174 Hz | 0 ms | 0.017 | 0.017 | 0.017 | 0.009 | 0.009 |
| 174 Hz | 20 ms | 0.016 | 0.024 | 0.032 | 0.008 | 0.017 |
| 174 Hz | 40 ms | 0.015 | 0.032 | 0.049 | 0.008 | 0.024 |
| 174 Hz | 80 ms | 0.015 | 0.047 | 0.083 | 0.008 | 0.040 |
| 174 Hz | 160 ms | 0.015 | 0.078 | 0.152 | 0.008 | 0.070 |

### Transmitter

Largest angle between drive and antenna current, lowest delivered power (negative: returned to
the source), energy returned per tone on average, largest standing-wave ratio against the
resonant resistance (none where the current opposes the drive), and the share of time with the angle above 30 degrees. "Keyed" and
"inward" are the two rules for the direction of a half turn.

| Antenna | Ramp S | Half turn | Largest angle | Lowest power | Returned per tone | Largest VSWR | Time above 30 deg |
| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 24 Hz | 0 ms | keyed | 180.0 deg | -9.87 kW | 10.32 J | none: power returned | 0.0939% |
| 24 Hz | 0 ms | inward | 180.0 deg | -9.87 kW | 10.32 J | none: power returned | 0.0939% |
| 24 Hz | 20 ms | keyed | 107.6 deg | -1.25 kW | 0.37 J | none: power returned | 0.1353% |
| 24 Hz | 20 ms | inward | 87.9 deg | +0.19 kW | 0.00 J | 67.39 | 0.1321% |
| 24 Hz | 40 ms | keyed | 68.9 deg | +2.09 kW | 0.00 J | 6.15 | 0.1499% |
| 24 Hz | 40 ms | inward | 49.0 deg | +4.73 kW | 0.00 J | 2.84 | 0.1275% |
| 24 Hz | 80 ms | keyed | 49.0 deg | +4.73 kW | 0.00 J | 2.84 | 0.1339% |
| 24 Hz | 80 ms | inward | 38.1 deg | +6.35 kW | 0.00 J | 2.12 | 0.0544% |
| 24 Hz | 160 ms | keyed | 38.1 deg | +6.35 kW | 0.00 J | 2.12 | 0.1115% |
| 24 Hz | 160 ms | inward | 32.3 deg | +7.22 kW | 0.00 J | 1.85 | 0.0393% |
| 57 Hz | 0 ms | keyed | 180.0 deg | -9.72 kW | 4.51 J | none: power returned | 0.0387% |
| 57 Hz | 0 ms | inward | 180.0 deg | -9.72 kW | 4.51 J | none: power returned | 0.0387% |
| 57 Hz | 20 ms | keyed | 51.9 deg | +4.31 kW | 0.00 J | 3.10 | 0.0497% |
| 57 Hz | 20 ms | inward | 43.0 deg | +5.62 kW | 0.00 J | 2.40 | 0.0449% |
| 57 Hz | 40 ms | keyed | 33.5 deg | +7.04 kW | 0.00 J | 1.90 | 0.0144% |
| 57 Hz | 40 ms | inward | 23.1 deg | +8.47 kW | 0.00 J | 1.53 | 0.0000% |
| 57 Hz | 80 ms | keyed | 23.1 deg | +8.47 kW | 0.00 J | 1.53 | 0.0000% |
| 57 Hz | 80 ms | inward | 17.5 deg | +9.10 kW | 0.00 J | 1.37 | 0.0000% |
| 57 Hz | 160 ms | keyed | 17.5 deg | +9.10 kW | 0.00 J | 1.37 | 0.0000% |
| 57 Hz | 160 ms | inward | 14.6 deg | +9.36 kW | 0.00 J | 1.30 | 0.0000% |
| 86 Hz | 0 ms | keyed | 180.0 deg | -9.59 kW | 3.00 J | none: power returned | 0.0254% |
| 86 Hz | 0 ms | inward | 180.0 deg | -9.59 kW | 3.00 J | none: power returned | 0.0254% |
| 86 Hz | 20 ms | keyed | 36.8 deg | +6.56 kW | 0.00 J | 2.05 | 0.0193% |
| 86 Hz | 20 ms | inward | 30.2 deg | +7.52 kW | 0.00 J | 1.77 | 0.0023% |
| 86 Hz | 40 ms | keyed | 23.2 deg | +8.46 kW | 0.00 J | 1.53 | 0.0000% |
| 86 Hz | 40 ms | inward | 15.7 deg | +9.26 kW | 0.00 J | 1.32 | 0.0000% |
| 86 Hz | 80 ms | keyed | 15.7 deg | +9.26 kW | 0.00 J | 1.32 | 0.0000% |
| 86 Hz | 80 ms | inward | 11.8 deg | +9.58 kW | 0.00 J | 1.23 | 0.0000% |
| 86 Hz | 160 ms | keyed | 11.8 deg | +9.58 kW | 0.00 J | 1.23 | 0.0000% |
| 86 Hz | 160 ms | inward | 9.8 deg | +9.71 kW | 0.00 J | 1.19 | 0.0000% |
| 174 Hz | 0 ms | keyed | 180.0 deg | -9.18 kW | 1.49 J | none: power returned | 0.0124% |
| 174 Hz | 0 ms | inward | 180.0 deg | -9.18 kW | 1.49 J | none: power returned | 0.0124% |
| 174 Hz | 20 ms | keyed | 19.6 deg | +8.88 kW | 0.00 J | 1.42 | 0.0000% |
| 174 Hz | 20 ms | inward | 15.8 deg | +9.26 kW | 0.00 J | 1.33 | 0.0000% |
| 174 Hz | 40 ms | keyed | 11.9 deg | +9.57 kW | 0.00 J | 1.23 | 0.0000% |
| 174 Hz | 40 ms | inward | 7.9 deg | +9.81 kW | 0.00 J | 1.15 | 0.0000% |
| 174 Hz | 80 ms | keyed | 7.9 deg | +9.81 kW | 0.00 J | 1.15 | 0.0000% |
| 174 Hz | 80 ms | inward | 5.9 deg | +9.89 kW | 0.00 J | 1.11 | 0.0000% |
| 174 Hz | 160 ms | keyed | 5.9 deg | +9.89 kW | 0.00 J | 1.11 | 0.0000% |
| 174 Hz | 160 ms | inward | 4.9 deg | +9.93 kW | 0.00 J | 1.09 | 0.0000% |

## Tone of 1 cell = 1.28 s

### Receiver: loss in dB

Coherent sum over tones with one phase for all positions, for a receiver whose reference follows
the ramp, one that assumes a plain step at the start of the tone, and one that leaves the ramp
interval out; then the mean energy per tone for the first two.

| Antenna | Ramp S | Coherent, ramp known | Coherent, step assumed | Coherent, ramp left out | Per tone, ramp known | Per tone, step assumed |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 24 Hz | 0 ms | 0.791 | 0.791 | 0.791 | 0.441 | 0.441 |
| 24 Hz | 20 ms | 0.771 | 0.851 | 0.880 | 0.425 | 0.501 |
| 24 Hz | 40 ms | 0.756 | 0.912 | 1.002 | 0.410 | 0.559 |
| 24 Hz | 80 ms | 0.738 | 1.035 | 1.277 | 0.391 | 0.675 |
| 24 Hz | 160 ms | 0.723 | 1.287 | 1.873 | 0.375 | 0.899 |
| 57 Hz | 0 ms | 0.175 | 0.175 | 0.175 | 0.107 | 0.107 |
| 57 Hz | 20 ms | 0.160 | 0.235 | 0.281 | 0.092 | 0.166 |
| 57 Hz | 40 ms | 0.152 | 0.296 | 0.416 | 0.084 | 0.225 |
| 57 Hz | 80 ms | 0.145 | 0.419 | 0.699 | 0.077 | 0.342 |
| 57 Hz | 160 ms | 0.141 | 0.670 | 1.298 | 0.073 | 0.566 |
| 86 Hz | 0 ms | 0.086 | 0.086 | 0.086 | 0.056 | 0.056 |
| 86 Hz | 20 ms | 0.073 | 0.147 | 0.200 | 0.043 | 0.116 |
| 86 Hz | 40 ms | 0.068 | 0.207 | 0.338 | 0.038 | 0.175 |
| 86 Hz | 80 ms | 0.065 | 0.330 | 0.622 | 0.035 | 0.291 |
| 86 Hz | 160 ms | 0.063 | 0.581 | 1.221 | 0.033 | 0.515 |
| 174 Hz | 0 ms | 0.028 | 0.028 | 0.028 | 0.020 | 0.020 |
| 174 Hz | 20 ms | 0.019 | 0.088 | 0.152 | 0.011 | 0.080 |
| 174 Hz | 40 ms | 0.017 | 0.149 | 0.291 | 0.010 | 0.139 |
| 174 Hz | 80 ms | 0.016 | 0.271 | 0.576 | 0.009 | 0.255 |
| 174 Hz | 160 ms | 0.016 | 0.521 | 1.175 | 0.008 | 0.480 |

### Transmitter

Largest angle between drive and antenna current, lowest delivered power (negative: returned to
the source), energy returned per tone on average, largest standing-wave ratio against the
resonant resistance (none where the current opposes the drive), and the share of time with the angle above 30 degrees. "Keyed" and
"inward" are the two rules for the direction of a half turn.

| Antenna | Ramp S | Half turn | Largest angle | Lowest power | Returned per tone | Largest VSWR | Time above 30 deg |
| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 24 Hz | 0 ms | keyed | 180.0 deg | -9.87 kW | 10.09 J | none: power returned | 0.7515% |
| 24 Hz | 0 ms | inward | 180.0 deg | -9.87 kW | 10.09 J | none: power returned | 0.7515% |
| 24 Hz | 20 ms | keyed | 107.6 deg | -1.25 kW | 0.36 J | none: power returned | 1.0762% |
| 24 Hz | 20 ms | inward | 87.9 deg | +0.18 kW | 0.00 J | 69.96 | 1.0543% |
| 24 Hz | 40 ms | keyed | 68.9 deg | +2.09 kW | 0.00 J | 6.15 | 1.2028% |
| 24 Hz | 40 ms | inward | 49.0 deg | +4.73 kW | 0.00 J | 2.84 | 1.0392% |
| 24 Hz | 80 ms | keyed | 49.0 deg | +4.73 kW | 0.00 J | 2.84 | 1.1195% |
| 24 Hz | 80 ms | inward | 38.1 deg | +6.35 kW | 0.00 J | 2.12 | 0.4886% |
| 24 Hz | 160 ms | keyed | 38.1 deg | +6.35 kW | 0.00 J | 2.12 | 0.8837% |
| 24 Hz | 160 ms | inward | 32.3 deg | +7.22 kW | 0.00 J | 1.85 | 0.3469% |
| 57 Hz | 0 ms | keyed | 180.0 deg | -9.72 kW | 4.40 J | none: power returned | 0.3084% |
| 57 Hz | 0 ms | inward | 180.0 deg | -9.72 kW | 4.40 J | none: power returned | 0.3084% |
| 57 Hz | 20 ms | keyed | 51.9 deg | +4.31 kW | 0.00 J | 3.10 | 0.3956% |
| 57 Hz | 20 ms | inward | 43.0 deg | +5.62 kW | 0.00 J | 2.40 | 0.3606% |
| 57 Hz | 40 ms | keyed | 33.5 deg | +7.04 kW | 0.00 J | 1.90 | 0.1075% |
| 57 Hz | 40 ms | inward | 23.1 deg | +8.47 kW | 0.00 J | 1.53 | 0.0000% |
| 57 Hz | 80 ms | keyed | 23.1 deg | +8.47 kW | 0.00 J | 1.53 | 0.0000% |
| 57 Hz | 80 ms | inward | 17.5 deg | +9.10 kW | 0.00 J | 1.37 | 0.0000% |
| 57 Hz | 160 ms | keyed | 17.5 deg | +9.10 kW | 0.00 J | 1.37 | 0.0000% |
| 57 Hz | 160 ms | inward | 14.6 deg | +9.36 kW | 0.00 J | 1.30 | 0.0000% |
| 86 Hz | 0 ms | keyed | 180.0 deg | -9.59 kW | 2.93 J | none: power returned | 0.2027% |
| 86 Hz | 0 ms | inward | 180.0 deg | -9.59 kW | 2.93 J | none: power returned | 0.2027% |
| 86 Hz | 20 ms | keyed | 36.8 deg | +6.56 kW | 0.00 J | 2.05 | 0.1505% |
| 86 Hz | 20 ms | inward | 30.2 deg | +7.52 kW | 0.00 J | 1.77 | 0.0170% |
| 86 Hz | 40 ms | keyed | 23.2 deg | +8.46 kW | 0.00 J | 1.53 | 0.0000% |
| 86 Hz | 40 ms | inward | 15.7 deg | +9.26 kW | 0.00 J | 1.32 | 0.0000% |
| 86 Hz | 80 ms | keyed | 15.7 deg | +9.26 kW | 0.00 J | 1.32 | 0.0000% |
| 86 Hz | 80 ms | inward | 11.8 deg | +9.58 kW | 0.00 J | 1.23 | 0.0000% |
| 86 Hz | 160 ms | keyed | 11.8 deg | +9.58 kW | 0.00 J | 1.23 | 0.0000% |
| 86 Hz | 160 ms | inward | 9.8 deg | +9.71 kW | 0.00 J | 1.19 | 0.0000% |
| 174 Hz | 0 ms | keyed | 180.0 deg | -9.18 kW | 1.45 J | none: power returned | 0.0991% |
| 174 Hz | 0 ms | inward | 180.0 deg | -9.18 kW | 1.45 J | none: power returned | 0.0991% |
| 174 Hz | 20 ms | keyed | 19.6 deg | +8.88 kW | 0.00 J | 1.42 | 0.0000% |
| 174 Hz | 20 ms | inward | 15.8 deg | +9.26 kW | 0.00 J | 1.33 | 0.0000% |
| 174 Hz | 40 ms | keyed | 11.9 deg | +9.57 kW | 0.00 J | 1.23 | 0.0000% |
| 174 Hz | 40 ms | inward | 7.9 deg | +9.81 kW | 0.00 J | 1.15 | 0.0000% |
| 174 Hz | 80 ms | keyed | 7.9 deg | +9.81 kW | 0.00 J | 1.15 | 0.0000% |
| 174 Hz | 80 ms | inward | 5.9 deg | +9.89 kW | 0.00 J | 1.11 | 0.0000% |
| 174 Hz | 160 ms | keyed | 5.9 deg | +9.89 kW | 0.00 J | 1.11 | 0.0000% |
| 174 Hz | 160 ms | inward | 4.9 deg | +9.93 kW | 0.00 J | 1.09 | 0.0000% |

## Closed forms for a plain step

After a half turn the delivered power is 1 - 2*exp(-pi*B*t) of the matched power: negative for
ln(2)/(pi*B), returning (1 - ln 2)/(pi*B) seconds of matched power. After a quarter turn it is
1 - exp(-pi*B*t): it falls to zero and nothing is returned.

| Antenna | Stored energy at 10 kW | Reverse-power time after a half turn | Energy returned by one half turn |
| ---: | ---: | ---: | ---: |
| 24 Hz | 66.3 J | 9.19 ms | 40.7 J |
| 57 Hz | 27.9 J | 3.87 ms | 17.1 J |
| 86 Hz | 18.5 J | 2.57 ms | 11.4 J |
| 174 Hz | 9.1 J | 1.27 ms | 5.6 J |
