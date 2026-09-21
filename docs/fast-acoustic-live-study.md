# Fast acoustic capacity study

This development study measures the real default speaker/microphone path and
uses those measurements to develop an independent acoustic Fast waveform. The
validated cable waveform and regular transport remain separate. Fast acoustic
OFDM uses the capacity codec (LDPC, approximately 0.3% outer RS, eight-bit source
bytes and integrity-protected fixed-cycle completion/padding).

## Selected default and completed bulk transfer

The speaker/microphone default is now **64-QAM, LDPC 3/4, depth 8**, with a
32,768-point FFT, 4096-sample cyclic prefix, pilot stride 16, 500–18,000 Hz,
nominal amplitude 0.40 and both output channels. The classic acoustic option
remains available; the cable default is unchanged.

A complete **5,000,000-byte** live transfer took **746.471 seconds** including
startup and final silence. Both file hashes were
`7269849ebd08cdadfdca97dc85d62c038cbd28caf5ee99d487201322a68552bb`.
All 840 LDPC frames converged; the decoder changed 1,893,045 hard bit decisions
in 4,723 total iterations. No missing intervals, FIFO overflow, audio errors,
or digital signal clipping occurred. Maximum DSP chunk time was 0.274 seconds
and maximum queued capture was 0.256 seconds. Source goodput including the
probe's eight-second tail was 53.67 kbit/s; including its additional start/stop
wall time gives 53.59 kbit/s. This is a completed transfer, not an extrapolation.

Known-bit replay of all 105 recorded cycles gave 4.777–5.229 information
bits per data tone (mean 5.056), compared with the code's 4.5-bit requirement.
These receiver-metric measurements corroborate the successful decode; they are
not a calibrated prediction of an unobserved error floor.

The final application's exact estimates include its waveform-specific
7.688-second end tail:

| Source bytes | Estimated time | Status |
| --- | ---: | --- |
| 60 | 33.032 s | Model; deliberately amortizes setup over large files |
| 100,000 | 46.856 s | Normal CLI live pass: 47.976 s wall |
| 5,000,000 | 744.968 s | Model; measured probe wall time 746.471 s |
| 50,000,000 | 7,166.216 s / 119.44 min | **Projection only** |

The normal `pump fast-tx` / `pump fast-listen` path then transferred 100,000
bytes exactly using only `--profile acoustic`: 47.976 seconds including a
one-second listener lead-in, 32 successful LDPC frames, and observed physical
completion. The source and saved file both hashed to
`186c4e6a64ded6aada5644318829477841734567799275b2368b72a9bfdb7a60`.
This validates the actual CLI defaults, four-second production FIFO, and shared
end-tail calculation, beyond the diagnostic probe.

All acoustic live sounders and modem trials together used approximately
**29 minutes 38 seconds**. No device or mixer setting was changed.

One successful 5 MB transfer does not establish an 80% success probability for
50 MB. No whole 50 MB acoustic trial was completed within the user's live-test
budget. The changing channel and correlated errors make a simple independent
bit-error extrapolation especially unreliable.

A faster selectable candidate, **1024-QAM / LDPC 1/2 / depth 16** with the
same FFT, guard, and pilots, also transferred 500,000 bytes exactly in 96.770
seconds. All 144 LDPC frames converged; maximum DSP/FIFO times were 0.606/0.597
seconds, with no clipping or audio errors. Its nine recorded cycles measured 5.889–6.047 information bits per tone
against a five-bit coding requirement. It provides a modeled steady rate
of 61.15 kbit/s, about 9.1% above the default. Its much shorter trial does not
qualify it for long-file reliability; the default retains the 5 MB-tested setting.
Select it explicitly with:

```
pump fast-info --profile acoustic --qam 1024 --code-rate 1/2 --interleave 16 --estimate-bytes 50000000
```

Use the same modulation/coding options on both `fast-tx` and `fast-listen` to
transfer. Its 50 MB duration is approximately 109.58 minutes, **projected only**.
The successful dense-constellation experiment shows that a stronger code can
outperform simply increasing QAM at a high code rate on this room channel.

## Remaining capacity gap

The default's steady public source rate is 56.04 kbit/s, about 43% of the
130.86 kbit/s sounder model at the same nominal RMS. Shannon capacity has **not**
been reached. The accounting makes the practical losses explicit:

- 64-QAM carries six bits per tone; LDPC 3/4 retains 4.5 source-code bits per
  data tone, versus about 7.4 Gaussian-equivalent bits/Hz in the sounder model.
- The cyclic prefix consumes 11.11% of each block; pilots consume 6.253% of
  active tones; one refresh after eight data blocks consumes another 11.11%
  of steady airtime.
- Fixed-cycle mapper fill consumes 3.238% of the data-label coordinates.
  The 2,048-bit interval rounding adds 0.346% relative to LDPC code bits.
- RS contributes **148 parity bytes per 48,452 RS data bytes**, or **0.3055%
  parity/data**. The one-byte flag and 32-byte digest cost only 0.0681% of that
  area. Those compact-source overheads are already negligible here.

Reducing the prefix, increasing uniform QAM density, and reducing coding margin
were tested, with the failures documented below. Frequency-dependent bit
loading, a feedback/calibration mechanism for matching transmitter power and
modulation to each tone, and better tracking of late echoes are plausible ways
to close more of the gap. The offline ideal bit-loading study predicts only a
modest gain under its stationary model; it is not a live-tested implementation.
Selective retransmission could improve whole-file reliability under occasional
room disturbances. Neither adaptive bit loading nor retransmission is claimed
as implemented by this change.

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
analysis band; it is not a certified room RT60 measurement. A tapered,
noise-corrected causal-tail analysis estimated about 2.05% of response energy
beyond 42.7 ms and 0.786% beyond 85.3 ms. Those are model-dependent tail-energy
estimates, not direct measurements of modem inter-symbol interference. They
explain why the short central energy span alone is insufficient when choosing
a guard for dense QAM; the actual guard trials remain the decisive evidence.

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

## Continued live tests and rate boundaries

A later session rechecked the same default physical ports and unchanged mixer
levels before continuing. These are sequential room measurements, not
simultaneous controlled channel realizations. Files use decimal KB/MB.

| Configuration | Source | Outcome | Signal time | Elapsed live time |
| --- | ---: | --- | ---: | ---: |
| 64-QAM, 3/4, depth 4, N8192/P4096, pilots 8 | 100 KB | Exact; 24 LDPC frames, none failed | 31.488 s | 40.65 s |
| 256-QAM, 3/4, same geometry | 100 KB | Failed | 25.344 s | 34.50 s |
| 64-QAM, 7/9, depth 4, N16384/P4096, pilots 8 | 100 KB | Exact; 20 frames, none failed | 27.733 s | 36.89 s |
| 64-QAM, 7/9, N16384/P4096, pilots 16 | 200 KB | Failed | 40.960 s | 50.12 s |
| 64-QAM, 7/9, N16384/P4096, pilots 8 | 200 KB | Failed after first cycle | 44.800 s | 53.98 s |
| Smoothed refresh; 64-QAM, 7/9, depth 4, N32768/P4096, pilots 4 | 500 KB | Exact; 84 frames, none failed | 108.288 s | 117.45 s |
| Smoothed refresh; 256-QAM, 3/4, depth 8, N32768/P4096, pilots 8 | 200 KB | Failed | See archived JSON | 57.55 s |
| Smoothed refresh; 256-QAM, 2/3, same geometry | 500 KB | Failed after two cycles | See archived JSON | 100.56 s |

The last two failed trials exceeded the old production one-second capture queue
while eight LDPC frames exhausted their iteration budgets (1.45–1.50 seconds).
The probe retained all samples. Production OFDM now uses a fixed four-second
queue and up to four bounded decoder workers; single-carrier capture retains its
original one-second bound. Queue overrun remains an explicit transfer failure.

### Guard length

Three recorded raw-bit trials held N16384, 64-QAM, depth 4, and amplitude 0.40
fixed. A 1024-sample (21.33 ms) prefix failed to acquire. A 2048-sample (42.67 ms)
prefix produced 4.499 generalized-information bits per tone, below 64-QAM/7⁄9's
4.667-bit requirement. A 4096-sample (85.33 ms) prefix produced 5.074. The
information statistic uses the receiver's actual soft bit metrics and exact
transmitted labels; it does not fit the channel to those labels. These trials
support retaining the longer guard at this placement. They do not imply a
universal minimum guard for every room.

### Channel estimation, constellations, and failure margin

On the failed 200 KB dense-pilot recording, replacing the response estimate
with a single refresh block produced per-cycle information values from 5.059
down to 2.919 bits/tone. Retaining the original response without refreshing also
failed to retain margin. Blending 75% of the gain-aligned prior estimate with
25% of each new refresh improved the same recording's values, but did not repair
its worst disturbance. On a separate N32768 recording, smoothed refresh improved
later cycles from roughly 5.16–5.20 to 5.25–5.27 bits/tone without additional
airtime. The subsequent 500 KB live pass used that estimator.

Two pilot interpolation experiments (linear and windowed-sinc complex residual
interpolation) reduced information on the recordings and were discarded.
Cautious decision-directed tracking improved the 256-QAM recording by only
about 0.01–0.04 bits/tone and did not recover its failed cycles; it was also
omitted. These were actual receiver experiments against fixed captured PCM,
not assumed nonbenefits of equalization.

256-QAM/3⁄4 requires six information bits per tone before finite-code losses;
its recording provided only about six. The standard DVB-S2 2/3 code was added
with an independently generated codeword fixture. Its live 256-QAM trial
provided 5.78 bits/tone initially, falling to 5.18–5.56 later, with a nominal
requirement of 5.333 plus the decoder's finite-block gap. The larger constellation
therefore did not deliver a dependable improvement at the tested placement.
It remains selectable for better acoustic channels. A 40–60 dB flat-channel
assumption would misrepresent this measured speaker/microphone path.

The residual measurements identify frequency-selective fades, insufficient
guard duration, noisy response estimates, and time variation as practical
constraints. They do not uniquely separate loudspeaker nonlinearity, moving
reflections, microphone processing, and background sound. Calling any one of
those the sole physical cause would exceed the evidence. The sounder's capacity
integral is a useful stationary reference, not a guarantee that this receiver
can maintain that information rate over a changing room channel.

## Software verification

The Release build succeeded. The selected 43-target Fast/shared-GUI and Regular
compatibility coverage passed after correcting the old CLI fixture that treated
the newly implemented 2/3 rate as invalid. The initial run passed 42 targets;
the full corrected 18-case CLI rerun passed. All eleven independent channel
measurement and ideal-BICM numerical controls also passed. New sampled coverage
includes long echoes, changing gain with a noisy refresh, sparse pilots and
clock offset, exact S16 WAV duration, and rejection of EOF before enough complete
absent blocks. The existing independent wire vectors and pending GUI checks were
retained. This software coverage is separate from the physical-link results.
