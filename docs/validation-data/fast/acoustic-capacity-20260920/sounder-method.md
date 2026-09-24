# Nearby-speaker acoustic sounder

These four recordings use the verified internal laptop speakers and microphone
array at unchanged 90% playback and 27% microphone settings. They are acoustic
measurements, not the earlier headphone-to-microphone cable. The capture wrapper
recorded both microphone channels at 48 kHz float32; converting or storing samples
as float32 does not establish the hardware converter's effective resolution.

`a08`, `a20`, and `a40` play the same signal on both speakers with source peaks
0.08, 0.20, and 0.40. `a20-right` drives only the right speaker at peak 0.20.
Corresponding active source RMS values are 0.0176891, 0.0442228, and 0.0884456.
Equal per-speaker peak is not equal total electrical drive power: driving two
speakers changes both total drive and the acoustic interference pattern.

The [manifest](sounder-manifest.json) retains capture hashes, original paths,
source hashes, level settings and analysis-source hashes. The raw recordings and
full-resolution complex response/impulse NPZ files remain at the stated `/tmp`
paths; they are not included in this archive. JSON contains each microphone and
their arithmetic mean. The 50 Hz CSVs average linear powers and coherence within
each cell; they are display summaries, not substitutes for raw complex H when
designing an equalizer. The [comparison plot](sounder-comparison.svg) uses those
CSV summaries. `summary.csv` collects the operating-point measurements.

## Sequence and estimation

[`tools/acoustic_channel_analysis.py`](../../../../tools/acoustic_channel_analysis.py)
generates a 15.938-second known waveform. Three 0.3-second logarithmic chirps
bracket and divide eight independent random-phase broadband blocks. Each block
has a flat commanded spectrum from 150 through 18,000 Hz, a 16,384-sample period,
an 8,192-sample cyclic prefix, and four identical periods. All blocks share one
peak normalization and equal RMS. Silence precedes and follows the sequence.

Chirp correlation estimates bulk delay and one common sample-clock slope. A
64-tap windowed-sinc interpolation corrects that slope. Correlation is refined
at sub-sample resolution and refitted using the stretched chirp; a native-rate
three-point fit was insufficient in the synthetic 80 ppm control. The reported
bulk delay includes playback/capture buffering and must not be converted into
speaker-to-microphone distance. Marker residuals expose deviations from a single
linear time mapping. No independently fitted phase or gain is applied to a
held-out test period.

Even random-phase blocks estimate a per-frequency linear H and predict odd
blocks; odd blocks predict even blocks. This cross fitting retains nonlinear
response that would be absorbed by fitting H to the same repeated waveform.
Within-block repeat differences independently estimate stochastic noise and
time variation, with the finite-repeat degrees-of-freedom correction. Independent
silence has its own PSD and level statistics; it is not substituted for in-signal
noise. The coherence estimate pools the independently phased blocks.

The capacity model uses the largest of approximately 47 Hz-smoothed held-out,
repeat-difference, and silence PSDs. Its response power subtracts the disagreement
between the two independently fitted phase folds before smoothing. Integrals use
the measured source power inside each listed band. Narrow-band entries do not
silently concentrate the whole 18 kHz source power into the narrower band.
Uniform-power and water-filled integrals use the same total source power.

These are **conditional stationary linear/Gaussian-equivalent engineering
models**, not measured Shannon capacity, formal lower confidence bounds, modem
throughput, or delivery probabilities. Signal-dependent distortion, room motion,
finite training error and residual interpolation error remain in the residual.
Changing power loading can change distortion; water filling assumes it does not.
The word `qualified` reports basic numerical/sync/clipping checks, not statistical
qualification of any modem or room.

## Observations

For the arithmetic microphone mean, the conditional uniform-power integrals over
300–18,000 Hz are:

| Speaker drive | Source peak | Conditional rate | Water-filled rate |
| --- | ---: | ---: | ---: |
| Both | 0.08 | 83.99 kbit/s | 84.40 kbit/s |
| Both | 0.20 | 121.06 kbit/s | 121.29 kbit/s |
| Both | 0.40 | 130.86 kbit/s | 131.01 kbit/s |
| Right only | 0.20 | 92.72 kbit/s | 93.06 kbit/s |

Read the machine-readable JSON for unrounded values. Doubling source peak from
0.20 to 0.40 quadruples source power but increases this integral only about 8.1%.
The upper bands develop substantially more held-out residual than repeat noise
or separate silence, consistent with signal-dependent distortion or channel
variation. No converter full-scale samples occurred in these recordings.

A separate variation diagnostic holds the opposite-phase-fold H fixed, fits one
complex gain and one delay (three real parameters) using repeats 0 and 2, and
scores only repeats 1 and 3. At peak 0.40 this reduces the 8–12 and 12–18 kHz
residual powers by about 45% and 49%; fitted delays drift by roughly 0.12 sample
across the sequence. At peak 0.20 it removes only about 1.6% and 3.6%, with delays
within ±0.017 sample. Smooth time variation therefore explains much of the
additional upper-band residual at 0.40. The remaining residual still cannot be
uniquely labeled loudspeaker nonlinearity. This diagnostic does not replace the
primary conservative PSD or capacity figures.

| Band (kHz) | Both 0.08 | Both 0.20 | Both 0.40 | Right 0.20 |
| --- | ---: | ---: | ---: | ---: |
| 0.3–2 | 11.15 dB | 19.21 dB | 24.78 dB | 12.32 dB |
| 2–4 | 17.98 dB | 25.58 dB | 30.06 dB | 18.89 dB |
| 4–8 | 14.46 dB | 21.53 dB | 24.37 dB | 16.37 dB |
| 8–12 | 17.23 dB | 23.01 dB | 22.96 dB | 20.23 dB |
| 12–18 | 11.54 dB | 18.37 dB | 19.26 dB | 13.71 dB |

This table is integrated estimated linear signal power divided by conservative
held-out residual power. It is not pure thermal-noise SNR. Spectral ratios vary
widely within a band. The strong response near 1.008 kHz and troughs near 7.48,
11.71, 15.38 and 17.97 kHz are visible in the response data.

At source peak 0.20, stereo output gives a roughly 10.8 ms span containing the
middle 90% of band-limited impulse energy; right-only takes 28.54 ms. The raw 99%
span extends beyond 100 ms. These values include band limitation and estimation
noise and are not RT60 measurements. Independent phase-fold uncertainty in the
stereo 0.20 impulse is only about 0.036% of total estimated response energy,
whereas roughly 2.9% lies outside ±20 ms. A separate 300 Hz cosine taper of both
excited-band edges leaves a 10.65 ms 90% span, 104.60 ms 99% span and 2.83% energy
outside ±20 ms: sharp band edges alone do not explain the long tail. The 341 ms
period and 171 ms cyclic prefix limit identification of still later paths.

At stereo peak 0.20, signed phase-fold-noise-corrected impulse energy outside a
window starting 1 ms before the strongest path is 2.046% for a 2,048-sample
(42.67 ms) prefix and 0.786% for a 4,096-sample (85.33 ms) prefix. Right-only
leaves 4.318% and 1.530%, respectively. An 8,192-sample OFDM useful symbol plus
4,096-sample prefix spends 33.3% of symbol time on its prefix; 16,384 plus 2,048
spends 11.1%. The longer prefix lowers identified out-of-window energy by about
4.2 dB in this stereo path. These impulse-energy ratios are not measured OFDM
interference powers; live waveform tests must choose that tradeoff.

For right-only playback, the individual microphones give approximately 99.0 and
99.2 kbit/s conditional integrals, exceeding their arithmetic mean's 92.7 kbit/s.
Simple microphone averaging can therefore cancel useful acoustic signal. With
both speakers, averaging performed slightly better than either microphone for
this fixed placement. This does not establish an optimal array combiner.

## Reproduce or rerun

Use a Python environment with NumPy. This machine's bundled interpreter is
`/home/user/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3`;
the system interpreter lacks NumPy, and the bundled runtime lacks SciPy. The
analysis therefore implements FFT correlation, Welch PSD and its windows directly
with NumPy.

```sh
python3 tools/acoustic_channel_analysis.py generate --output /tmp/new-sounder --amplitude .20
# Add --right-only for the corresponding routing control.
# Only the separately authorized live wrapper opens devices:
python3 tools/acoustic_channel_capture.py --help
python3 tools/acoustic_channel_analysis.py analyze --input /tmp/new-sounder \
  --capture /tmp/new-sounder/capture-stereo.f32 --channels 2
python3 tests/test_acoustic_channel_analysis.py
```

The seven synthetic controls check known gain/noise power, held-out coherent cubic
distortion, delayed echoes, 80 ppm clock mismatch, analytic AWGN capacity and
water-filling power conservation, independently tested gain-motion correction,
and clipping/invalid inputs. They do not replace
live modem trials. `sounder-archive.py` compacts the existing `/tmp` analyses into
this archive and regenerates the native SVG plot; it does not open audio devices.
