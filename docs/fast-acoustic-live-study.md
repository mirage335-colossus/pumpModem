# Fast acoustic capacity study

This development study measures the real default speaker/microphone path and
uses those measurements to develop an independent acoustic Fast waveform. The
validated cable waveform and regular transport remain separate. Fast acoustic
OFDM uses the capacity codec (LDPC, approximately 0.3% outer RS, eight-bit source
bytes and integrity-protected fixed-cycle completion/padding).

## Measurement method

The connected default ports were the Ryzen HD Audio/ALC257 internal speakers and
internal microphone, at the user's existing 90% playback and 27% capture settings.
No mixer or device settings were changed. ALSA reported Capture 58/63 (+26.25 dB)
and Mic Boost 0 dB. The two capture channels were retained independently before
computing the mono average used by the production audio path.

`tools/acoustic_channel_capture.py` captures real stereo float32 PCM through the
PulseAudio-compatible desktop path. `tools/acoustic_channel_analysis.py` generates
and analyzes independent random-phase broadband sounders: 16,384-sample periods,
a half-period cyclic prefix, eight different phase blocks and four repeats per
block. Three chirps estimate global delay and clock mismatch. A response fit on
even phase blocks predicts odd blocks and vice versa; the held-out residual
therefore includes signal-dependent distortion and variation, rather than
fitting them away. Separate repeat differences and quiet sections provide
additional residual/noise estimates. The modem trials use production S16 audio.

Capacity calculations integrate `log2(1 + |H(f)|² Sx(f) / N(f))` over frequency.
The noise model takes the maximum of smoothed held-out residual, repeated-block
residual and independent quiet noise. The numbers are conditional stationary
linear/Gaussian-equivalent engineering estimates at the tested waveform level,
not a proven Shannon limit, success probability or modem throughput. Colored
noise and frequency-selective channels require this frequency integral; see
[MIT's Gaussian-channel lecture](https://ocw.mit.edu/courses/6-441-information-theory-spring-2010/resources/mit6_441s10_lec18/).
Redistributing power can alter speaker distortion, which ordinary water filling
does not predict.

## Measured channel

These results use the arithmetic mean of the two microphones and the
300–18,000 Hz analysis band. Waveform peak is digital amplitude before the
unchanged output-device volume.

| Speaker routing | Peak | Active source RMS | Conditional capacity |
| --- | ---: | ---: | ---: |
| Both speakers | 0.08 | 0.01769 | 83.99 kbit/s |
| Both speakers | 0.20 | 0.04422 | 121.06 kbit/s |
| Both speakers | 0.40 | 0.08845 | 130.86 kbit/s |
| Right speaker only | 0.20 | 0.04422 | 92.72 kbit/s |

Both-speaker and one-speaker measurements use the same per-speaker amplitude,
not equal total electrical power. At right-only routing, each microphone alone
modeled about 99 kbit/s, exceeding their average: phase cancellation matters.
There was no digital clipping in these sounder captures.

At 0.20 peak, aggregate signal/held-out-residual ratios were 19.21, 25.58, 21.53,
23.01 and 18.37 dB in the 0.3–2, 2–4, 4–8, 8–12 and 12–18 kHz bands. At 0.40,
the upper bands improved little: the 8–12 kHz ratio stayed near 23 dB and
12–18 kHz rose only to 19.26 dB. Four times the source power bought only 8.1%
more modeled capacity. Signal-dependent residual, not merely background noise,
therefore limits the benefit of turning up the waveform at this placement.

The strongest response was near 1.008 kHz; deep troughs appeared near 7.477,
11.710, 15.381 and 17.965 kHz. The band-limited impulse response's central 90%
energy span was about 10.8 ms with both speakers and 28.5 ms with right-only
output. Its longer tail includes measurement noise and ringing from the finite
analysis band; it is not a certified room RT60 measurement.

## Why the old physical layer is insufficient

The existing acoustic preset succeeded on a 128-byte live transfer, requiring
43.48 seconds including startup and physical-end observation. It uses only
600 Hz occupied bandwidth, QPSK, convolutional coding and the classic source
format. Its 21-tap, half-symbol equalizer covers only 10 symbol periods: 1.25 ms
at 8 kbaud. The faster 2 kbaud/16-QAM acoustic capacity experiment failed to
acquire, although capture/playback ran without clipping or dropped samples.
Simply raising the constellation or substituting LDPC does not fix acquisition
and echo cancellation.

The new acoustic OFDM path divides the band into narrow subcarriers, estimates
response and residual separately for each, and uses a cyclic prefix to tolerate
echoes. Its training/verification and physical-presence decisions are specific
to that waveform. [Goldsmith's multicarrier chapter](https://web.stanford.edu/class/ee359/doc/WirelessComm_Chp1-16_March32020.pdf)
explains the cyclic-prefix/equalization principle. The finite guard, pilots,
training, LDPC rate and nonuniform subcarrier quality all consume capacity;
actual live file trials determine the useful rate.

The compact evidence archive is in
[`validation-data/fast/acoustic-capacity-20260920/`](validation-data/fast/acoustic-capacity-20260920/).
Raw PCM and full-resolution channel arrays remain at the recorded `/tmp` paths,
with hashes in the archive. Long-file predictions must remain explicitly
separate from completed physical transfers.

## Receiver diagnostics during development

The initial OFDM prototype transferred 4,096 bytes exactly with 16-QAM/LDPC 3/4.
Its first 100,000-byte 64-QAM trials failed. Independent replay against recorded
transmitted bits showed why: at 0.20 amplitude, the decoder's actual bit metrics
carried only 4.427 generalized-information bits per data tone, below the 4.5
required by 64-QAM×3/4. At 0.40 this rose to 4.711, with weak frequency bands and
substantial variation between blocks. A longer 32768-sample FFT/8192-sample
prefix trial also failed to eliminate the errors; a longer guard alone is not a
validated solution.

The old estimator used a single normalized pilot-error floor after equalization.
It underestimated errors in deep fades and produced overconfident wrong bits.
Estimating noise in received-frequency-bin units, then dividing by the local
channel gain, improved the SAME recorded 0.20/0.40 bit metrics to 4.608/5.071
bits per tone without changing their hard bit errors. This is a controlled
receiver improvement, separate from a new live test. The subsequent waveform
uses 16 initial blocks: two repeated for clock estimation, 13 independent phase
blocks for response/residual estimation, and a held-out verification block.
It refreshes the full frequency response between locally fixed coding cycles.

An early mapper-fill bug coherently aligned unused carriers into peaks above
full scale. Independent QPSK fill removed those peaks; later trials used the
fixed transmitter and verified no digital clipping. The early recording is
retained and explicitly labeled in the archive.
