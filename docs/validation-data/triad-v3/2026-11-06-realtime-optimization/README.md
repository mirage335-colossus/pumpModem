# Broad HF acquisition: CPU improvements and fresh-key cadence

Measured on this computer's **AMD Ryzen 5 PRO 5650U**, six physical cores,
with two numerical threads. These changes preserve the current independent
encrypted amplitude/phase patterns, APSK spreading, source/channel waveform,
search hypotheses and conditional admission thresholds. They do not integrate
the ordinary Robust receiver or qualify physical false admissions.

## Why the previous costs were larger

The [current high-SNR measurement](../2026-11-06-hf-high-snr-cost/README.md)
and [8-dB-Hz measurement](../2026-11-06-hf-current-cost/README.md) used broad
timing/epoch acquisition with renewing rays. Older faster whole-frame and
incremental results used persistent rays or a small known-alignment bank.
Substituting those assumptions would not be an equivalent optimization.
Also, reducing DATA from 46.4 to 11.6 seconds increases fixed acquisition cost
per DATA second even while lowering total per-bit cost.

There were substantial implementation costs as well. The saved profile spent
about 9.8 seconds in the coarse FFT and 6.1 in refinement preparation;
38,940 cubic interpolation calls took 2.8 seconds. Templates repeated pulse
geometry and carrier rotations across 26 keys/bit alternatives. The segmented
FFT used a poorly performing length (14,641 = 11⁴ on this host) and many small
inverse transforms. DATA basis work also continued across exactly zero support.
Profile times diagnose work; unprofiled paired runs below measure improvement.

## Changes and preservation checks

- Default bounded smooth FFT sizing, modest product batching and private
  in-place buffers. All lags and logical hypotheses remain; explicit executed
  work counters distinguish saved arithmetic. Direct SVD at every lag and
  FP32/FP64/legacy comparisons cover rank-deficient and tiny-tail cases.
- Share interpolation geometry across both bits and common timing/clock
  alternatives, then apply distinct CFO/ray rotations. Skip only exactly zero
  projected DATA support. Retain every state time and Gaussian evolution across
  quiet gaps. Scalar basis references remain bit-exact in focused tests.
- Native bounded `PatternSource::render_bank` shares geometry across keyed
  sources. Source sample accumulation order is unchanged. Raw-template stream
  requests use it; mapped/delayed/rotated requests keep their reference path.
  File export uses bounded source-cache subbanks. The high-rate cache-limit
  control avoids multiplying an already-large single-source allocation by 26.
- Optional bounded scoring workers and an explicit FFT worker budget. Mutable
  receiver workspaces are private to each job. Ingestion never waits for a
  scorer; overload and incomplete drain remain explicit.
- Public one-slot template generation in memory avoids unnecessary channel
  simulation and temporary template files. It checks key, epoch, native format,
  source calendar and power gain before using the bank. It is a fixture API,
  not production private-key management.

Shared FFT acquisition and staged refinement are consistent with
[GNSS-SDR's acquisition description](https://gnss-sdr.org/docs/sp-blocks/acquisition/).
The implementation uses [SciPy's FFT interface](https://docs.scipy.org/doc/scipy/reference/fft.html)
and keeps per-job mutable arrays private, consistent with
[NumPy's thread-safety guidance](https://numpy.org/doc/stable/reference/thread_safety.html).
GPU offload, stronger pruning, narrower search and lower precision were not
needed to obtain these gains on the acceptance CPU. Changing acquisition or
likelihood decisions would require new sensitivity/admission qualification.

## Paired complete receiver measurements

Same saved observations, keys, priors and public noise calibration were used
before/after. The weak case reuses the exact saved full-audio AES-noise PCM,
not a new random draw. Runtime measurements are individual paired observations,
not confidence-qualified speedup bounds.

| Workload | Old RX CPU / wall | New RX CPU / wall | CPU improvement |
| --- | ---: | ---: | ---: |
| Nominal DATA 20.02 dB-Hz, 11.6 s DATA | 26.399 / 20.474 s | 14.367 / 11.839 s | 1.84× |
| Injected approximately 8 dB-Hz, 46.4 s DATA | 82.116 / 59.015 s | 41.539 / 33.363 s | 1.98× |

RX includes recorded 48-kHz PCM ingestion/DDC, Helper, acquisition and final
refinement, but excludes initial keyed templates and workspace construction.
Workspace construction was 1.434 CPU seconds (high case) and 8.324 (weak case).
The scoring sections alone were 11.995 CPU / 9.467 wall and 38.726 CPU /
30.550 wall respectively. Final bit, score, margin, bit scores, operational
decision and refinement coordinates were **identical** to their saved baseline.
The high and weak log-evidence values remained 1155.706950639086 and
117.54484061108323.

The separate native raw-bank benchmark generated all 26 × 71,417 complex
samples: **4.341 → 0.475 CPU seconds**, including child startup, a 9.14×
improvement. Every sample matched the original scalar export bit-for-bit.
The final exporter memory-guard build independently matched complete exported
DATA templates, observation and validity hashes; the in-memory helper took
0.522 child CPU seconds in a subsequent integration check. These are not
claims that every preparation stage or every rate became nine times faster.

The original native benchmark binary preceded the conservative file-export
cache guard. That guard does not alter the raw stream path used by the timed
bank, and the final native binary was used by the cadence campaign below.

## Independent sustained computational cadence

The frozen protocol generated eight fresh symbol ordinals and independent
stochastic channel/noise seeds, alternating transmitted bits, with public-prior
arrival positions −6.9, 2.7, 0, 6.9, −2.7, 0, 2.7 and −6.9 seconds. Each case
uses the requested HF configuration: Rate36/Added×10, free crystal/IC-7100,
30MHz, HF-worst margin3 (90-Hz ray separation, 21-ms delay), Each65%, renewing
ray width0.5Hz and random delay excursion0.0002ms with20-second knots.
FHSS and Symbol Burst are off. Nominal DATA/Helper C/N₀ are20.02/30.02dB-Hz.
All waveforms passed through maintained real PCM conversion and complex DDC
before the workload timer. Public keys and hypotheses were not selected from
evaluator truth; receiver results were saved before evaluator access.

| Measurement | Result |
| --- | ---: |
| Fresh-bank + workspace + complete scoring jobs | 8 |
| Offered cadence / DATA represented | 11.6 s / 92.8 s |
| Workers / numerical threads per worker | 2 / 2 |
| Aggregate parent + waited-child CPU | 111.587 s |
| Campaign wall time | 91.466 s |
| CPU seconds per offered DATA second | **1.2024** |
| Maximum queue wait / completion delay | 0.00230 s / 14.112 s |
| Overloads / execution errors | 0 / 0 |
| Correct conditional admissions / wrong / misses | 8 / 0 / 0 |
| Parent peak RSS | 683.3 MiB |
| Startup, separately | 1.469 CPU / 1.226 wall s |

This demonstrates computational throughput with fresh preparation at the bit
cadence. Aggregate CPU above one second per input second is compatible with
real time on this six-core machine. Two workers accommodate jobs whose latency
occasionally exceeds 11.6 seconds; one worker is not qualified by this result.
Use `--workers 2 --threads 2` in the maintained streaming harness for that
allocation. The default worker count remains one to avoid silently multiplying
memory use on other machines.

These are **independent captured windows**, not one contiguous transmitted
message. Each has its own Helper lead; the native whole-bit qualification
calendar still includes UTC rounding and gaps. The test intentionally offers
their computational jobs faster than that fixture calendar. ADC/ASRC, DDC,
waveform/channel simulation and a production message adapter are outside the
cadence timer. The prior approximately0.024 CPU-second/input-second DDC
measurement suggests approximately1.23 including that frontend, but this sum
is an estimate, not a concurrent end-to-end measurement. Parent RSS excludes
native child peaks and is not a hard DSP capacity ceiling.

Eight successes do not establish90% or99% reception reliability. There were
no null trials in this campaign; physical null qualification remains false.
No new multi-day hardware, GPU, continuous-message or cable result is claimed.
Runtime gains transfer algebraically to the tested paths; their numerical
boundary tests do not imply a universal speedup at every rate or search size.

## Reproduction and inexpensive maintenance

The compact `evidence.tar.gz` retains frozen protocols, exact generator and
measurement commands, native/source hashes, input hashes, raw JSON receipts,
paired comparisons, tests and original setup failures. Large I/Q, PCM and
template arrays are omitted; the native generator protocol reproduces the
independent cases. The historical weak AES-noise PCM is identified by hash;
its temporary key was intentionally not retained, so that exact noise draw
requires the saved local PCM. Other draws can reproduce the experiment, not
its bit-exact score. `SHA256SUMS` binds the compact archive and summary.

Routine checks use the existing numerical environment and native build:

```sh
./build.sh test triad --cli --jobs 2
PYTHONPATH=tools:tests OPENBLAS_NUM_THREADS=2 OMP_NUM_THREADS=2 python3 -m unittest test_triad_segmented_fft test_triad_dynamic_joint test_triad_stream_scheduler test_triad_weak_receiver test_triad_template_bank test_triad_weak_throughput
python3 tests/test_triad_stream_source.py build/dev-cli/triad_helper_export
```

The optional cadence run was a historical 92-second measurement plus fixture
generation. Its original result and source identity remain recorded above; this
page no longer provides a command to restore its fixtures or repeat the
sustained run. Any necessary compute-heavy Robust check follows the
[hierarchical abbreviated method](../../../triad-performance.md), and temporary
evidence follows the [material lifecycle](../../../development.md#material-lifecycle).

The final template helper added stricter metadata checks and four-response
buffer accounting after the frozen campaign. Its numerical bank construction
did not change. All eight real manifests were subsequently checked through
the final native API, and the original bank remained bit-exact. This bounded
identity transfer is recorded separately rather than rerunning92seconds merely
to attach new source hashes. The source identity in each original result is
retained; no prior receipt was rewritten.

The full isolated native group passed 28/28 (46.59 seconds test wall time).
The focused Python set collected 95 tests: 93 passed in 1.541 seconds, with
CUDA unavailable and an additional optional native replay explicitly skipped.
All 11 final native protocol tests passed in 5.467 seconds. Raw outcomes are
retained in the archive.
An initial build attempt failed before tests because the default ccache path
was read-only; the successful run used `CCACHE_DIR=/tmp/triad-native-render-ccache`.
Other preserved setup-only failures include a profile-script name shadowing
Python's standard library and an initial incorrect unit-test selector. They
are not counted as passing tests or performance measurements.
