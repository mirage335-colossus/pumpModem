# Bounded continuous transmission and reception

This change removes the native Helper exporter's arbitrary 3,600-second
argument limit and adds an execution path that does not need a complete-bit
recording or template bank. See the [continuous guide](../../../triad-continuous.md)
for the interface and the [duration guide](../../../triad-duration-planning.md)
for the separate provisional forecasts. Ordinary Robust integration and its
regression campaign remain outside this study.

## Implemented scope

The native pull service retains keyed source tiles, Helper generation, selected
channel state and filter tails across requests. Its separate template sources
synthesize only the requested public candidate block. Sample addresses are
checked 64-bit integers. Operational sources accept a raw 32-byte key file;
streaming defaults to a system-clock epoch fixed for the bit. Explicit fixture
seeds remain available for reproducible experiments.

Streaming uses a fixed, bounded-tile DATA power reference rather than a
whole-transmission prepass. All chunks retain the same gain. The original
whole-active calibration and its 4,194,304-sample work guard remain available;
finite file exports retain their 2 GiB artifact guard. These are resource limits,
not a time limit on the new stream.

The incremental receiver retains posterior channel mean/covariance and cumulative
log likelihood for every declared candidate/model. It does not restart a short
receiver for each block. Infinite coherence retains a static channel hypothesis;
finite coherence supplies the existing Gaussian evolution hypotheses. The
projection, final remainder merge, normalization and mixture match the existing
batch receiver. Memory depends on block size, bank and tap count, not duration.

The initial practical interface is a **fixed public acquisition bank**, with
at most 128 requested basis columns (both bits and all taps included) and a
64 MiB native service workspace. It does not automatically acquire the complete
broad timing/clock/frequency prior, track drift over days, or use fresh Helper
estimates to change that bank. Those are real remaining scope limits. The
existing finite-window weak receiver remains available separately.

Only a complete declared observation can produce an admission decision.
Helper-bearing and physical observations expose a working-model pass separately;
they never report an operational admission under the unqualified zero-mean null.
The audio bridge uses the shared backend and final hardware-rate AES noise stage,
with bounded PCM buffers. Capture overflow and reported playback discontinuities
fail the session. No checkpoint/restart or multi-day hardware-uptime guarantee
is implemented.

## Measured execution

Measurements used this machine's Ryzen 5 PRO 5650U, two numerical-library
threads, and the existing optimized native build. These are engineering timing
observations, not confidence-qualified cross-hardware forecasts.

| Experiment | Actual observation | Result | Cost |
|---|---:|---|---:|
| New complete source/channel/incremental path, Rate 36, Added 10, stable channel, one public alignment, −16 dB-Hz | 7,265.049 s at 6 kHz; 43,590,296 samples | Correct bit 1 admitted; no missing rows | 171.592 s wall, 216.377 combined CPU s = **0.02978 CPU s/input s** |
| Same path short baseline, 8 dB-Hz | 84.649 s | Correct bit 0 admitted | 2.851 s wall, **0.04253 CPU s/input s**, including startup |
| Independent stable holdout, 0 dB-Hz | 189.049 s | Correct bit 1 admitted | 0.05356 CPU s/input s |
| Independent Gaussian noise-only control | 189.049 s | No admission | One control, not a rare-tail calibration |
| Random HF worst, margin 3, Each 65%, 8 dB-Hz; two public taps, no oscillator error, Helper active | 189.070 s | Correct bit 1, working-model pass; operational admission withheld | 0.09197 CPU s/input s |
| Independent Helper-only random-HF control | 189.070 s | No working-model pass or admission | One control, not a Helper-null certificate |

The two-hour bit had 7,203.6 seconds of active DATA after tile rounding. Preparation
took 1.031 seconds. The incremental explicit-array bound was 907,392 bytes and
the final native source/template workspace report was 1,843,008 bytes. Process
peak RSS was about 241 MiB for Python including numerical libraries; the recorded
child high-water mark was about 54 MiB. These OS high-water readings are not just
the algorithm's live arrays. Actual integrated DATA mean power differed from
its nominal normalized value by **−0.00338 dB**.

These timing results support substantial real-time headroom in the named
configurations. They do not forecast a billion-point acquisition bank or prove
that arbitrary changing channels can be decoded at the same sensitivity.
A final repeat of the 189-second stable holdout, with executable/module hashes
recorded before processing, produced exactly the same bit evidence and admission.
It took 10.381 seconds wall and 0.07784 combined CPU s/input s. Retain that timing
variation; these few runs do not establish a runtime confidence interval.

## Genuine multi-day samples and likelihood equivalence

A native reciprocal-rate run processed every sample, including physical-time
channel evolution and tails: 52,937 samples at 1/64 Hz, or 3,387,968 seconds of
capture. The requested 38-day allocation rounded to 3,360,008.4 seconds of
active source geometry. Rate was 0.0036, Added 1, Sub9-best delay/Doppler, no
additional modulation, no phase diffusion, and a narrow public bank. At
−40 dB-Hz the existing native receiver admitted the correct bit. The selected
−44 and −46 dB-Hz runs missed; one noise-only run was rejected. These are
individual seeded observations, not a reliability curve or −60 dB-Hz result.

The same actual encrypted waveform, sampled channel, noise and native basis
were passed through the **new incremental Gaussian receiver** and existing
batch receiver. Both consumed 827 fixed blocks. FP64 evidence matched exactly;
FP32 differed by at most **7.75e−6 log-evidence units**, with the same correct bit
0. Incremental explicit workspace was 23,880 bytes in FP32 and 34,736 bytes in
FP64. Feed/scoring CPU time was 2.13/1.83 seconds respectively. Small-workload
FP64 timing here does not justify changing bulk precision defaults.

This is a real long physical-time sampled observation, not repetition of a
short capture or an energy-rescaled statistic. Its low sample rate cannot
represent audio hardware or arbitrary fast Doppler/sidebands. Conversely, the
two-hour 6 kHz run tests sustained full sampled processing, not months of
oscillator stability. Both kinds of evidence are needed.

## Cable and power checks

A 63.6-second actual cable run used 13 predeclared timing candidates, both bits,
two coherence models, complete Helper emission and fresh 14-bit-equivalent AES
background noise. Every requested frame was delivered and the correct bit was
selected. Combined streaming CPU was 9.960 seconds (0.15661 CPU s/input s);
this excludes initial source/receiver preparation. Playback/capture reported no
clipped samples. The backend used 44.1 kHz output, and its measured noise
variance was 1.24165e−9 versus expected 1.24176e−9. Noise was added at that
hardware rate, not confined to the 6 kHz complex stream.

The first cable run preceded a review correction enabling strict playback-gap
failure; its completion alone cannot rule out silently recovered gaps. The final
repeat used that observer and an independent keyed pattern: all 369,600 playback
and 381,600 capture frames completed, no error or clipping was reported, and
bit 0 was correctly selected (bit log evidence 1279.38 versus 956.69). Combined
streaming CPU was 8.237 seconds, or **0.12952 CPU s/input s**, excluding setup;
wall time was 63.960 seconds. Hardware-rate noise variance was 1.24249e−9 versus
expected 1.24176e−9. Raw outcomes are in `live-strict/live.json` and its stderr
receipt. Neither run certifies marginal physical sensitivity, physical false
admissions or multi-day device uptime.

Four short bounded-power comparisons against whole-active calibration used
low rate, requested HF rate, higher rate and FHSS. Resulting output-power changes
were +0.0191, −0.0290, −0.00652 and +0.12073 dB. This is a small fixed-key screen,
not a statistical confidence bound. The estimator reports sampled tile identities,
node count and between-tile variation rather than claiming exact full-bit energy.

## Cheap regression and reproduction

Routine checks are the five native stream-protocol tests, native long-plan guard,
eight audio pipe guards, and focused incremental/adapter/planner Python tests.
The final three native CTest entries passed in 3.92 seconds; the 38 focused
Python tests passed in 0.301 seconds of test time, excluding interpreter imports.
The existing finite Helper exporter regression also passed. The source protocol
checks match emitted/channel samples and basis chunks, and exercise a 38-day
prefix plus a day-30 template request without constructing a full recording.
Python checks compare batch and incremental static/renewing likelihoods, gaps,
terminal blocks, incomplete inputs and huge declared geometry. One review found
and fixed an oversized requested DFT allocation for a short observation.

```sh
ctest --test-dir build/dev-cli \
  -R '^triad_(stream_source|helper_plan_long|audio_stream)$' --output-on-failure
OPENBLAS_NUM_THREADS=2 OMP_NUM_THREADS=2 MKL_NUM_THREADS=2 PYTHONPATH=tools:tests \
  /tmp/triad-weak-env/bin/python -m unittest \
  test_triad_incremental_receiver test_triad_continuous \
  test_triad_duration_model test_triad_weak_plan
```

Use `./build.sh configure --cli` and the existing compatible build profile when
native sources change. Do not rerun the two-hour or cable qualification for an
unchanged arithmetic/protocol path. Run those focused targets when a change
invalidates the recorded source/calibration/likelihood relationship. The archive
retains commands, raw outcomes, short reproduction scripts and source identities;
it omits reproducible binary waveform files and private temporary noise keys.

Original setup failures are retained: an incomplete concurrent Python edit,
invalid test-bank exclusion on the raw-row path, duplicate native-audio linkage,
missing NumPy in the CTest interpreter (the native test was converted to standard
library only), and the wrong `--spread-margin` spelling. None is counted as a
successful receiver trial. Fixed paths were rechecked. No RunPod resources were
needed: this qualification targeted local CPU execution and actual audio.

## Provenance limits

[`summary.json`](summary.json) indexes the measured outcomes and file identities;
[`evidence.tar.gz`](evidence.tar.gz) contains the compact raw evidence and source
snapshot. [`SHA256SUMS`](SHA256SUMS) binds these artifacts. Inside the archive,
`evidence/reproduce.py` can repeat one chosen CPU case into a new output path
instead of launching a full campaign. The long export manifest retains the
command needed to recreate the omitted waveform arrays before running
`long/compare_incremental.py` from the repository root.

The compact evidence archive includes raw reports, commands, fixture hashes,
reproduction scripts and the final source snapshot. It is rooted at Git commit
`2f37dad1` plus the accompanying changes, not a release certification. The first
two-hour run preceded the final descriptor-only Gaussian-null capability flag
and input guard changes; its source/binary hashes were not frozen at launch.
The final cable script recorded hashes at completion; the native source binary
was rebuilt during that run for the descriptor flag, so that native hash is not
proof of the running process image. The audio executable was unchanged during
the strict run. These results validate the stated arithmetic and flow paths;
they are not exact-build performance regression anchors. Future CLI runs record
input executable/module identities before processing. Requalify changed DSP or
noise assumptions instead of transferring these measurements universally.
