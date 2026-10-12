# Public Robust Modem alphabet — local manual checkpoint

The default unencrypted bit alternatives now have distinct amplitude and phase
profiles. Public patterns are known and repeatable; neither this change nor
amplitude variation makes them secret or indistinguishable from thermal noise.
No amplitude-only receiver is included.

## Construction and compatibility

Both rows use the existing circular bounded Gaussian-derived mapper: eight
public stream bytes per chip, radius `min(1.75,sqrt(-log(u))) /
sqrt(1-exp(-1.75²))` and uniform phase from the second 32-bit word. The known
32-byte `DataPump/pattern/binary/v1` seed and epoch zero remain fixed. Bit zero
uses Scrambler/Payload at byte `8*i`; bit one uses Scrambler/PatternOneV2 at byte
`8*(1886+i)`. Each symbol restarts `i=0`, including after a partial final chip.
There is no transmitted alphabet/version indicator and no additional epoch or
transmission-position hypothesis.

The 1886 offset is a fixed public construction constant, not a secret address
change. An offline scan of 2048 candidate prefixes rejected the unselected
independent prefix (5–6 dB less bit-one energy for 2–3 chips), then considered
amplitude and complex correlation, energy balance, shifts and frequency offsets.
No runtime seed search, pair balancing, amplitude compression, per-chip
normalization or whole-symbol normalization is used. Whole-symbol energy differs
between public bits; its finite-prefix variation is part of the waveform.

The first 128 pairs are a 4 KiB immutable read-only coefficient table. It removes
repeated mapping work without allocating a new PCM/cache buffer. Tests rebuild
all entries from their specified byte streams. Longer prefixes use bounded lazy
mapping; diagnostic TX byte tracing bypasses the table. The two existing shaped
chip caches retain separate rows, with their existing phase invalidation rules.
Their identity is configuration, alphabet, symbol-local address and bit; public
repetition is intentional. Private keys, epochs, candidates and addresses never
use this table or public offset.

Private Scrambler/DSSS and encrypted production output remain unchanged.
Low-level Data-only encrypted diagnostic configurations retain the historical
sign-mask waveform; their identity is included in FFT/correlator metadata and
planner caches. The receiver needs this diagnostic waveform identity but not
the settling Data key bytes; the regression replaces those bytes with an
unrelated key. Production key selection always enables private Scrambler.
There is no public legacy dropdown or automatic mixed-alphabet reception.
**Both public peers must update to exchange arbitrary messages**; framing, raw bits,
short dictionary codes and encrypted-peer interoperability are unchanged.

## Geometry limits

One chip is still ambiguous under unknown complex gain and phase. A vanishing
second-chip observation tends toward the same ambiguity. Shorter than 16 full
chips retains the existing rectangular pulse path; its broad spectral tails are
not repaired by choosing another alphabet. Pulse shaping, final limiter, tails,
thresholds, search hypotheses and physical six-second scored absence are
unchanged. Distinct envelopes are useful phase-independent evidence for future
algorithms, not proof that all timing/carrier/clock alternatives are separable.

## Evidence and outstanding work

Frozen baseline: `b581d41f494362dc3068fd594f560d67ba245395`, local Release
GCC 14.2/FLTK build on AMD Ryzen 5 PRO 5650U. The preceding GUI, static library,
source archive and CMake configuration remain in
`.agent-work/artifacts/public-alphabet-20261012/baseline/manifest.json`.
The new source overlay, binary identity, launcher, measurement programs and raw
results are retained in that artifact directory. Nothing was published.

### Shaped waveform measurements

These are actual analytic transmitter samples after finite shaping and limiting,
with both pulse tails, at 256 sample/s, carrier 64 Hz, bandwidth setting 32 Hz
(16 chips/s). Squared normalized correlation is shown; lower is less similar.
The amplitude metric compares absolute complex envelopes without fitting phase.
The shifted metric scans +/-2 chips and +/-4 Hz, with zero extension, and uses
full-row energy denominators. This bounded scan is not a proof for every search
geometry or partial observation.

| Chips | Amplitude correlation, old / new | Complex correlation, old / new | Worst shifted/frequency correlation, old / new | New bit-one/zero energy |
| --- | --- | --- | --- | --- |
| 2 | 1.000 / 0.722 | 0.717 / 0.195 | 0.720 / 0.508 | +0.641 dB |
| 3 | 1.000 / 0.662 | 0.059 / 0.213 | 0.742 / 0.307 | -0.825 dB |
| 16 | 0.936 / 0.626 | 0.141 / 0.008 | 0.398 / 0.227 | -0.005 dB |
| 64 | 0.913 / 0.636 | 0.034 / 0.004 | 0.180 / 0.089 | +0.225 dB |
| 128 | 0.903 / 0.651 | 0.033 / 0.003 | 0.220 / 0.053 | +0.301 dB |

The three-chip aligned complex correlation regresses, despite improved
amplitude/shift separation. The selected tested 2–128-chip coefficient prefixes
stay within 1.2 dB energy imbalance; shaping/limiting changes that balance
(the 32-chip measured imbalance is +1.101 dB). These are evaluated finite
prefixes, not a universal bound over arbitrary integration lengths. A candidate
with better aligned metrics was rejected because its two-chip shifted/frequency
correlation reached 0.914, worse than the old 0.720.

For the isolated 64-chip bit-one waveform, the fraction at the final limiter
falls from 4.61% to 1.48%; energy outside carrier +/-10 Hz falls from -30.08 to
-39.39 dBc. At 128 chips those values are 3.73% to 2.30%, and -32.01 to -36.00
dBc. Bit zero is unchanged. No gain/headroom/limiter setting changed, and these
finite captures do not establish a general occupied-bandwidth or regulatory
mask. Power fluctuations remain substantial (64-chip bit-one power coefficient
of variation about 1.02 including tails). Public symbol repetition, alphabet,
chip timing, spectrum, limiter signature and bit-dependent power remain visible.

### Detection and cost measurements

The full `PatternReceiver` measurements use paired original channel/noise seeds,
equal **ensemble average received power** across equiprobable bits, unchanged
input rates and identical search policies. The two changed waveforms necessarily
produce different PCM. Each capture includes fractional timing, one full symbol,
full observed-absence time and additional trailing noise. Noise variance is one;
the swept energy/noise ratio converts to a C/N0 shift without changing the
reported difference because symbol duration is unchanged. This is a finite
single-public-epoch receiver bank, not application-level multi-profile, live
hardware or exhaustive full-bank qualification.

At 64 chips (4 s symbols), 2048 paired seeds per 0.25 dB point give additional
required C/N0 of **-0.052 dB** at 90% detection (paired bootstrap 95% interval
**[-0.110, -0.004] dB**) and **-0.013 dB** at 99% (**[-0.129, +0.105] dB**).
At 128 chips (8 s), 1024 seeds give **+0.039 dB** at 90%
(**[-0.047, +0.132] dB**) and **+0.004 dB** at 99%
(**[-0.127, +0.195] dB**). Thresholds use linear CDF interpolation on the stated
grid; intervals reflect finite-seed uncertainty, not all interpolation/model
error. No wrong bits were admitted in those sweeps. Inconclusive bounds do not
establish a universal negligible-loss result or a sensitivity gain. No
amplitude-only detector or earlier amplitude-ablation estimate is involved.

For 16 chips (1 s), 1024 paired seeds per 0.25 dB point give -0.017 dB
at 90% (95% interval [-0.047, +0.013]) and -0.088 dB at 99%
([-0.162, 0.000]). These are the existing sample-fit detector's own threshold
region, around 29–30 dB energy/noise; the 64/128-chip detector's operating
threshold cannot be transferred to this geometry. A four-chip single-bit pilot
was poor for **both** alphabets: 0/16 acquisitions at 40 dB, then 7/64 old and
9/64 new at 60 dB with fractional starts. The new pair does not cure that
existing short-observation acquisition limitation; short-pattern separation
metrics alone do not qualify usable acquisition.

Additional paired full-symbol captures used the same rate and search within
each pair. At +/-0.1 Hz and +/-300 ppm search endpoints, 256/256 captures decoded
at 26 dB for both alphabets. Phase diffusion of 10 degrees/sqrt(second), partial
final chips and 32 s symbols also passed their bounded strong controls. Strong
1.3-Hz-offset sinusoidal interference at 1.5 times noise RMS was damaging: at
26 dB only 25/512 old and 15/512 new captures decoded; neither admitted a wrong
bit. A bounded 512-seed sweep from 28 to 34 dB reached 512/512 at 31 dB
for both. On its coarser 1 dB grid, interpolated differences are -0.163 dB
at 90% (paired 95% interval [-0.277, -0.075]) and -0.057 dB at 99%
([-0.388, +0.266]); grid interpolation error is additional. This one interferer
is a measured adverse condition, not general interference qualification.

Three-bit `001`/`110` captures at 22 dB completed exactly in 221/256 old and
219/256 new cases. The remaining observed prefixes are deletions of intended
bits, not substituted/extra bits. At 26 dB both completed 256/256, with identical
accepted-bit polls at input samples 2056, 3084 and 4112. Near threshold some
later confirmations occur in the same poll for both versions. Noise-only tests
admitted no bits in 2048 paired captures; this modest count is not evidence for
an extremely rare false-accept rate. Existing covariance/energy normalization,
trial penalties and all threshold rules are retained. For a fixed Gaussian
noise projection with known covariance, whitening fixes the null distribution
by its rank rather than the particular public row; this does not independently
certify adaptive full-bank or non-Gaussian false-acceptance behavior.

Six isolated serial old/new repetitions alternated order. The following CPU
cost includes receiver construction, input processing, search, publication,
physical absence and finish; it excludes waveform/channel fixture generation.
These are small finite public banks, not predictions for the live application.

| Geometry | RX CPU per complete capture, old / new | Relative CPU change, paired 95% interval | Peak accounted workspace, both | First accepted poll, both |
| --- | --- | --- | --- | --- |
| 16 chips, 256 sample/s | 5.593 / 5.680 ms | +1.58% [+0.13%, +3.03%] | 555618 B | 2.008 s |
| 64 chips, 256 sample/s | 1.501 / 1.464 ms | -2.47% [-3.90%, -1.03%] | 483939 B | 8.031 s |
| 128 chips, 256 sample/s | 2.369 / 2.180 ms | -8.01% [-11.73%, -4.29%] | 555618 B | 16.063 s |
| 512 chips, 256 sample/s | 44.571 / 45.375 ms | +1.80% [+1.33%, +2.27%] | 666178 B | 33.129 s |
| 64 chips, 6000 sample/s, 1200 Hz setting | 20.936 / 20.091 ms | -3.75% [-9.92%, +2.41%] | 483938 B | 0.214 s |
| 64 chips, 64 sample/s, 0.5 Hz carrier | 4.732 / 4.601 ms | -2.77% [-3.75%, -1.78%] | 443970 B | 192.750 s |

CPU and wall means were close; all raw wall times, constructor cost, input
lengths, FFT job counts and process RSS are in `performance-analysis.json`.
For example, the 64-chip fixture has 5651 input samples (22.074 s), about
3.86 million input samples/s in the new receiver. Constructor CPU falls from
0.371 to 0.345 ms. The 128-chip constructor falls from 0.778 to 0.669 ms.
Workspace is unchanged in the repeated strong controls; the public table adds
4 KiB of shared read-only image, with no symbol-sized PCM allocation. First-poll
times include existing FFT readiness and 257-sample polling, not just symbol
duration. No new algorithmic acquisition/publication delay was added. Very
long observations here are simulated captures, not wall-duration live tests.

The first independent-row prototype was about 15% slower on the measured
64-chip bank. The fixed prefix table removed that regression. Patterns longer
than 128 chips still map two rows lazily; the 512-chip run shows a small measured
regression. Planner work coefficients were not reduced to conceal it. Its
existing engineering cost model is not a separately calibrated cost model for
every public long-row construction. RX probability estimates now average both
finite public candidates (as they already did for private candidates), keeping
paired trial counts and unsupported/uncertain labels.

Whole eight-bit `00101110` bursts preserve substantial amplitude variation.
At 64 chips, power coefficient of variation changes from 1.007 to 0.926,
limiter occupancy from 5.98% to 3.80%, and energy outside carrier +/-10 Hz from
-30.29 to -33.91 dBc. At 16/128 chips the same tail measurement improves by
4.91/1.90 dB. This is finite-capture spectral evidence, not thermal-noise
indistinguishability. At unchanged digital gain, the balanced 64-chip burst's
mean power rises by 0.15 dB; its per-symbol power span rises from 0.055 to
0.295 dB. The construction deliberately accepts bit-dependent energy instead
of adding an artificial balancing or constant-envelope rule. Sensitivity
comparisons above remove mean-power differences; practical fixed-gain/peak
hardware behavior still includes them.

### Compatibility and regression checks

Twelve complete normal/cancelled encrypted analytic PCM captures are compared
byte for byte against the frozen library: Data-only diagnostic, Scrambler,
Scrambler plus DSSS factor one, and interleaved DSSS 10/100/1000. All use synthetic
keys. Both payload and surrounding waveform are included. Fixed private vectors,
key/epoch independence, block/partial-chip invariance, covariance/energies,
wrong-key rejection, pending progress and physical-end assertions remain intact.

The old settling fixture removed a Data-only diagnostic key, unintentionally
selecting the new public alphabet on RX. It now replaces the actual secret with
an unrelated key to retain the diagnostic identity, with every reception
assertion preserved. A separate pre-existing drift-fixture failure reproduced
against the frozen library: 128 bytes of idle slack could not fit even one lazy
timing entry. That fixture still constructs/shrinks at its original tight bound
and verifies fallback/unfinished accumulation, then grants the existing bounded
mandatory evidence headroom before admission. No receiver policy or threshold
was changed to pass it. Original failure logs are retained.

Local affected validation: all 38 GUI tests, the full pattern receiver and
22 other affected receiver/transport tests, plus pattern-code, FFT-template,
transfer and simulation-estimate groups passed. Earlier matching pulse-shaping
and GUI pattern-space evidence was reused. The revised drift fixture also
passes its preserved constructor/shrink and exact-score assertions.

This remains a manual checkpoint, not full platform, sensitivity, security or
long-duration qualification. Full contract/calibration, ARM, Windows, SDK,
packaging and live hardware interoperability qualification remain outstanding.
