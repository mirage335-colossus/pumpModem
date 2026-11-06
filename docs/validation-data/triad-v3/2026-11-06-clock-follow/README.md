# Bounded clock-step source and receiver comparison

This evidence supports the limited [clock-step profile](../../../triad-clock-follow.md),
not universal clock synchronization. No machine clock was changed. No audio
device or cloud resource was used. Propagation, oscillator impairment and
additional selectivity are explicitly **None/Off** in this first discriminator.
The original channel implementations and ordinary receiver are unchanged.

## What changed and what was measured

The native source accepts a continuous quintic source-time correction, bounded
to ±2 seconds from its original map. The DATA-center NCO remains uniform.
Correction does not change the epoch, key, bit coefficients or interleave.
Live TX detects wall-minus-monotonic steps; independent RX uses the same public
calendar and nominal templates. No private TX trajectory is provided to RX.
The option rejects Helper, FHSS, Symbol Burst and fine rates above 0.001 Hz.

All source comparisons use actual native APSK v2 shaped samples. Nominal and
corrected arrays have equal physical sample times and unchanged gains; source
energy changes are measured separately. A +2 or −2 second change begins at
calendar time 100,000 seconds and settles in 12,500 seconds. The ×1000 case
begins its window at 50,000,000 seconds and the correction at 50,100,000 seconds.
This exercises genuine large source indices without simulating unused years.

| Fixture | Fine rate / Added | Sampled window | Correction | Worst two-bit template loss | Largest absolute energy change |
|---|---|---|---|---:|---:|
| low, seed 74001 | 0.0006 Hz / 1 | 1,000,000 s, Fs 0.125 Hz | +2 s | 0.000252 dB | 0.0000081 dB |
| independent boundary, 74002 | 0.001 Hz / 1 | 1,000,000 s, Fs 0.125 Hz | −2 s | 0.000706 dB | 0.0000062 dB |
| independent spreading, 74004 | 0.001 Hz / 1000 | 1,000,000 s, Fs 0.125 Hz | +2 s | 0.000701 dB | 0.0000086 dB |
| HF diagnostic, 74003 | 60 Hz / 10 | 800 s, Fs 500 Hz | +2 s, during transition | 51.77 dB with fixed timing | 0.0000224 dB |

Loss is `-10 log10(|<x,y>|²/(||x||²||y||²))`, allowing one common complex gain,
but initially **no timing or rate fitting**. The HF number is therefore not a
fundamental sensitivity limit. In a cropped 792-second window, an
oracle-initialized local affine arrival/rate fit reduces that loss to
**0.2490/0.2487 dB**. The fit needs approximately 288.55 ppm and 0.7488 seconds
midpoint offset. Exact-warp interpolation has only 0.0000032 dB shape error;
its separate interpolation energy bias is about 0.0072 dB. See
[affine-result.json](affine-result.json). This does not demonstrate blind
acquisition or a single affine fit over the entire bit, and the software rate
can lie outside oscillator-derived GPSDO grids. It is a reason to investigate
short affine timing fits before a more elaborate tracking architecture.

The three million-second records are payload windows, not all complete source
halos/tails or physical hardware observations. The ×1000 record is a selected
window from a longer source, not a full 100-million-second receiver run.
The paired keys were fixed before rendering; boundary/spreading fixtures use
independent keys and numerical-noise seeds. This small selection is not broad
channel or arbitrary clock-trajectory qualification.

## Marginal reception and null scope

[low-plus.json](low-plus.json), [edge-minus.json](edge-minus.json),
[spread-plus.json](spread-plus.json) and [hf-plus.json](hf-plus.json) retain
65,536 Gaussian sufficient-statistic trials per bit and E/N0 point (10, 12, 14,
16 dB). The exact four-template sampled Gram preserves dependence and gives
the same noise to nominal, corrected and oracle comparisons. Fixed variance,
alignment and an unknown constant complex gain define this diagnostic detector;
it is not substituted for the maintained receiver. The per-two-codeword-bank
threshold is `log(2/alpha)`, alpha=1e-4, with a union bound valid without pair
independence. This is **not** a per-day operational false-admission result.

For example, low-rate bit 0 at 10 dB window E/N0 gives 36,297/65,536 correct
admissions for nominal source and 36,296/65,536 for the corrected source with
nominal templates. At the independent rate boundary it is 36,338 versus
36,329. Raw reports separate correct admissions, wrong admissions, misses and
noise-only admissions, and retain marginal exact 95% binomial intervals.
Noise-only counts are 5, 7 and 8/65,536 for the three slow nominal banks; those
counts have sampling uncertainty and are neither independent across paired
detectors nor proof of physical or continuous-operation calibration.

The [maintained incremental receiver](incremental-validation.json) separately
processed all 125,000 rows per million-second window, with 30 blocks, one tap,
infinite channel coherence, unchanged operational log-e threshold 21.49338,
one fixed candidate and approximately 896 kB receiver workspace. At marginal
14 dB E/N0, each of two fixtures produced:

| Receiver/source combination | Correct model passes | Wrong model passes | Misses |
|---|---:|---:|---:|
| Nominal / nominal | 7/16 | 0/16 | 9/16 |
| Nominal / clock-corrected | 7/16 | 0/16 | 9/16 |
| Oracle warped / clock-corrected | 7/16 | 0/16 | 9/16 |

No decision changed in 32 paired trials. The largest nominal-versus-corrected
log-e change was 0.005143; selected FP32/FP64 score differences were at most
1.69e-5. These are conditional model passes on finite payload windows, not
newly qualified physical admissions. Zero noise admissions in 16 trials has
only a 20.59% two-sided 95% upper confidence limit; the theoretical fixed-bank
null is distinct from that weak empirical control. Processing the complete
comparison took **6.47 CPU seconds / 6.47 wall seconds** on one Ryzen 5650U
thread after imports. Simulated observation duration is not CPU wall time.

## Reproduction and inexpensive maintenance

[provenance.json](provenance.json) binds the base commit, changed source files
and native binaries. Raw reports retain source-array hashes and all measured
outcomes; manifests include native preparation/render times. Binary arrays stay
in ignored `local-evidence/`, avoiding a large repository artifact. Manifest
timing fields vary on reproduction; sampled array identities are deterministic.

Use the focused routine commands in the main guide. Four native/stdlib CTest
cases passed in **4.81 seconds**, including eight pull-protocol tests and
long-coordinate, zero-correction byte identity. Six adapter tests and seven
numerical diagnostic tests also pass. The new clock primitive passes ASan and
UBSan. LeakSanitizer is unavailable under this sandbox's ptrace supervision;
the focused sanitizer test passed with `ASAN_OPTIONS=detect_leaks=0`, and this
is not reported as a leak check. An initial sanitizer build accidentally pulled
ordinary native-audio dependencies; it was stopped and the standalone target's
composition fixed. No ordinary receiver campaign was run.

Native fixture recipe, from the repository root, using fresh output directories:

```sh
P=build/dev-cli/triad_pattern_probe
D=local-evidence/triad-clock-follow
"$P" --output "$D/low-base-74001" --rate .0036 --added 1 \
  --sample-rate 1 --sample-rate-denominator 8 --seconds 1000000 \
  --window-seconds 1000000 --start-seconds 0 --seed 74001 --fhss off
"$P" --output "$D/low-plus-74001" --rate .0036 --added 1 \
  --sample-rate 1 --sample-rate-denominator 8 --seconds 1000000 \
  --window-seconds 1000000 --start-seconds 0 --seed 74001 --fhss off \
  --clock-correction-seconds 2 --clock-correction-at-seconds 100000
OPENBLAS_NUM_THREADS=1 python tools/triad_clock_diagnostic.py \
  --nominal "$D/low-base-74001" --warped "$D/low-plus-74001" \
  --output "$D/low-plus.json" --window-en0-db 10 12 14 16 \
  --trials 65536 --seed 84001
```

For the edge pair use rate `.006`, seed 74002, correction −2, names
`edge-base-74002`/`edge-minus-74002` and noise seed 84002. For spreading use
rate `.000006`, Added 1000, seed 74004, source duration 100000000, start
50000000, correction date 50100000, names `spread-base-74004`/
`spread-plus-74004`, noise seed 84004. For HF use rate 36, Added 10, Fs 500
(denominator 1), source duration 200000, window 800, start 105000, seed 74003,
correction date 100000, names `hf-base-74003`/`hf-plus-74003`, noise seed 84003.

Copy the retained [incremental validation script](incremental-validation.py)
and [affine diagnostic](affine-diagnostic.py) into
`local-evidence/triad-clock-follow/` to reproduce their original relative paths,
then run them with the numerical environment and `OPENBLAS_NUM_THREADS=1
OMP_NUM_THREADS=1`. Their JSON outputs are diagnostic receipts in that ignored
directory, not a new release certificate. An initial validation-wrapper import
path error was fixed before receiver computations; it is retained in the receipt.

## Unresolved scope

No physical device-clock discipline, NTP slew handling, indefinite cumulative
correction, Helper/gated waveform following or ordinary/high-rate blind timing
tracking is demonstrated. No change to the configured false-admission policy
or private-trajectory shortcut is used to hide those gaps. Enabling the shared
audio flag alone would violate its existing timing contract. The implemented
bounded option and the measured short-affine alternative are starting points
for further support, not completion of general multi-day synchronization.
