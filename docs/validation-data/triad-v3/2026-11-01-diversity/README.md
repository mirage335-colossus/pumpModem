# Three-carrier receiver diversity and hour/two-day validation

Status: **completed bounded campaign; operational planner and broader
qualification remain incomplete**. This is an isolated three-carrier study.
Nothing here integrates the ordinary Robust receiver or validates a physical
radio link.

The maintained model, assumptions, likelihood definitions, test design and
limitations are in [the triad noise-calibration guide](../../../triad-noise-calibration.md).
The machine-readable summary and reproduction utility are in
[`analysis.json`](analysis.json) and [`analyze_results.py`](analyze_results.py).
The technical archive, its index and source provenance are included beside
this report. They contain technical experiment records only.

## Main result: hour-scale reception with calibrated noise

The repaired hour-long observation holdout used 300 independent physical
simulation paths at `C/N0 = -16 dB-Hz`, with 64-second projection blocks. The
preselected primary method, residual-UCB, accepted the correct bit on all
300/300 paths. The two comparator methods also scored 300/300. There were no
wrong admissions, misses or failed capture records in this repaired run.

For residual-UCB, the one-sided pointwise 95% exact lower confidence bound on
the correct-reception probability is **99.006%** (300 successes in 300
independent paths). This interval applies to the preselected primary point
only. It is not simultaneous across all three methods and does not establish
physical-hardware performance. The exploratory development run had only 32
paths, all correctly accepted, and is not the holdout result.
For comparison, a Bonferroni simultaneous 95% lower bound over these three
methods is 98.644%; the pointwise 99.006% result must not be substituted for it.

The final strict false-admission family uses nine externally selectable
methods, three duration choices, two bit hypotheses, 1,440 scheduled attempts
per day at 60-second spacing, and two calibration alternatives. The target is
at most `1e-4` probability of any noise-only admission per 24 hours. The hour
scores were rethresholded from the original seven-method gate by adding
`ln(9/7)`; correct acceptance counts did not change. The analytical bound is
conditional on stationary white proper-complex Gaussian noise and independent
whole-bank residual calibration.
The signal holdout is not a day-long noise-only admission trial. Separate
finite null simulations support the implementation under their stated
conditions; they do not empirically measure a `1e-4/day` field rate.
This `K=9` accounting holds the selected rate, waveform format and channel
geometry fixed. If an operational receiver also selects among configurations,
those alternatives must be included in the family budget.

The holdout was generated as actual noisy samples and channel outputs, then
scored with exported projections. It used the strict acceptance family
with `C/N0 = -16 dB-Hz`, Rate 36, Added Symbols 1, 500 kHz shift, both hobbyist
GPSDO oscillator profiles, 10 ms clock sync, a 1 ms region, 0 ms offset,
70 ms audio error, margin 1, HF-best two-ray channel, 1% selective perturbation,
and Symbol Burst 3. The first 300-path batch completed only 60 paths before
file-descriptor exhaustion caused 240 failures. That batch is excluded; the
descriptor handling was repaired before the complete 300-path repeat.

This is a constrained acquisition result: seven timing branches, one keyed
epoch, matched two-ray delay/Doppler geometry, and one zero sample-clock
hypothesis with independently generated small GPSDO clock errors. The channel
coefficients, phase trajectory and actual fractional arrival are unknown to
inference. It does not qualify a blind epoch bank, arbitrary propagation
geometry, free-running microwave RF capture, or changing-noise operation.

## Noise uncertainty experiments

The mismatch diagnostics below retain their recorded K=7 gates. They expose
noise-model failures; their finite null counts do not establish the final K=9
operational false-admission rate.

At one hour, `C/N0 = -16 dB-Hz`, the nominal known-variance Gaussian likelihood
was deliberately given a noise variance two times too small. Across 4,096
conditional null trials it falsely admitted 3,508 captures. The angular
common-scale method and residual-UCB admitted zero of 4,096 each under their
respective declared assumptions. In a separate 1.25x scale experiment, all
three methods had zero false admissions in 1,024 null trials; that smaller
sample does not establish a low operational rate.

A blockwise 2x noise-step test used 4,096 trials. The mismatched known-variance
method admitted 23 noise-only captures; per-block residual-UCB admitted zero.
The common-scale angular likelihood was correctly reported as unsupported for
this changing-scale condition, rather than being counted as a miss or a pass.
This demonstrates the value of matching the calibration model to the stated
noise uncertainty; it does not show robustness to arbitrary colored or
impulsive interference.

In a separate 32-capture actual sampled-noise-only under-reference control,
the known-variance Gaussian-OU method falsely admitted 31 captures; angular
Gaussian and residual-UCB admitted zero each. Zero in 32 still has a one-sided
95% upper false-admission bound of 8.94%, far above either operational target.
This is a small mismatch diagnostic, not rare-tail validation.

The relaxed operating target requested in this study is 90% correct reception
with at most `0.01` probability of any noise-only admission per 12 hours. The
strict target above is also retained for comparison. The
[maintained K=9 known-channel benchmark](known-channel-benchmarks.json)
gives ideal coherent known-waveform detection energy references of 13.159 dB
for the relaxed family and 15.471 dB for the strict family. These references
include the predeclared attempt, duration, method and bit-family corrections,
but assume known waveform, channel and phase. They are not correct-bit bounds
for the uncertain keyed waveform.

The likelihood guide separates that known-channel reference from an achievable
unknown-channel/noise model and from computation limits. The current results
do **not** yet quantify a near-bound receiver loss under blind synchronization
and channel uncertainty. At weak per-coherence-block SNR, the uncertain
Gaussian likelihood's information can scale quadratically in `C/N0`; a
coherent known-channel detector instead collects energy linearly. These laws
are model-specific and do not imply universal optimality.

Using the benchmark's requested 3,600-second useful duration at `-16 dB-Hz`,
nominal `E/N0` is 19.563 dB. That is 4.092 dB above the strict known-waveform
detection reference and 6.404 dB above the relaxed reference. The successful
300-path hour holdout is not a near-bound measurement: it shows reliable
reception at this energy and model point, but no completed duration or `C/N0`
sweep locates its sensitivity threshold. Requested duration does not assert
that tile-rounded transmitted support is exactly 3,600 seconds.

At `-34 dB-Hz` over the requested 172,800 seconds, nominal `E/N0` is
18.375 dB, 2.905 dB above the strict and 5.216 dB above the relaxed
known-waveform references. This is not a threshold sweep or an achieved
unknown-channel bound.

## Short configuration screens

The following actual-sample screens each requested 120 seconds on 12
independent physical simulation paths at `C/N0 = -4 dB-Hz`, using five
Gaussian-channel and phase/frequency detectors. Transmitted support is tile-rounded and can be
longer; for example, the Rate 3.6 / Added 10 case occupies about 247 seconds.
Counts are correct accepted bits out of 12; the run summary also tracks wrong
admissions, misses and failed records separately. These screens locate
configuration and acquisition problems; they are not low-`C/N0` reliability
qualification or equal-energy duration comparisons.
These are diagnostic screens with their archived per-screen gates; their
counts are not a calibrated K=9 false-admission qualification for scanning
across all configurations.

| Case | Configuration change | Gaussian OU | Angular | Gaussian UCB | Phase/frequency | Phase UCB |
|---|---|---:|---:|---:|---:|---:|
| `tight500k` | Rate 36, Added 1, 500 kHz, GPSDO/GPSDO, tight sync, margin 1 | 11 | 11 | 11 | 11 | 10 |
| `lowrate` | Rate 3.6, Added 1 | 12 | 12 | 12 | 12 | 12 |
| `added10matched` | Rate 3.6, Added 10 (matched fine-symbol rate) | 12 | 12 | 12 | 12 | 12 |
| `highrate_aligned` | Rate 360, Added 1, sample rate 640; zero search error | 12 | 12 | 11 | 12 | 12 |
| `microwave` | 5.8 GHz, 64-sample/s capture and 1-second blocks | 0 | 0 | 0 | 4 | 3 |
| `basecrystal500k` | Free-running base crystal, GPSDO shift oscillator | 1 | 1 | 0 | 11 | 11 |
| `selective5` | Selective amplitude perturbation increased to 5% | 11 | 11 | 11 | 11 | 11 |
| `margin3` | Margin 3; 1.5 ms path delay and 0.3 Hz ray Doppler separation | 11 | 11 | 11 | 11 | 11 |
| `tap_mismatch` | Truth has 0.5 ms / 0.1 Hz rays; receiver assumes 1.5 ms / 0.3 Hz | 0 | 0 | 0 | 0 | 0 |
| `erased10ms` | A 10 ms observation dropout | 12 | 12 | 10 | 12 | 11 |
| `burst_off` | Symbol Burst disabled | 12 | 12 | 11 | 12 | 11 |
| `epoch_search` | Truth epoch offset +1 with a three-epoch candidate bank | 8 | 8 | 6 | 8 | 7 |

The table uses the combined K=9 strict gate on the same 12 sampled paths for
all five methods in each case. These reports originally used K=7 and two
calibration policies, so raising the score threshold preserves the stated
calibration allowance. The [paired recount](screen-comparison.json), reproduced
with [compare_screens.py](compare_screens.py), binds report/score hashes and
checks input identity. No wrong accepted bits occurred in this table. The
legacy K=5, one-calibration-policy diagnostics remain in the archive; their
K=9 score-only counterfactuals do not establish combined-family false-admission
control.

The Gaussian-family methods in the original 5.8 GHz screen all missed (0/12).
The phase/frequency screens improved with shorter projection blocks: at the
original 1-second setting, the reference accepted 4/12 and its UCB version
3/12; with 128 samples/s, 0.25-second blocks and an 87.16-second tile-rounded
capture, they accepted 11/12 and 9/12. On those same short-block paths, the
static-frequency FFT reference accepted 11/12 and its UCB method 8/12. These
remain modeled short screens, not field reliability results. The coarse 93.75
ms timing spacing in the two-day
exploratory acquisition run loses acquisition information; it does not isolate
an oscillator-grade effect. A retained-fraction diagnostic found that the RC
triplet calculation matched observed retained fractions, consistent with the
timing-grid limitation. Sufficiently wide coarse RF capture at 5.8 GHz remains
necessary.

Separate eight-path static-frequency FFT UCB screens accepted 8/8 for the
HF 30 MHz case and 8/8 for the 5.8 GHz case, both one-hour observations at
`-16 dB-Hz`. The microwave case
used a controlled GPSDO oscillator preset; it is not a validation of physical
5.8 GHz frequency acquisition.

The 12-path `margin3`, selective-5%, dropout, burst-off and epoch-offset rows
are controlled model changes, not a factorial study. FHSS-on acquisition was
not qualified by these results. The 36/Added-1, 3.6/Added-1 and 3.6/Added-10
screens preserve different inner pattern structures; the matched fine-symbol
rate does not make them interchangeable formats.

## Two-day low-SNR results

The dense holdout used 32 unique actual sampled paths at `C/N0 = -34 dB-Hz`,
requested duration 172,800 seconds, Rate 36 / Added 1, a 500 kHz shift, OCXO
GPSDO profiles for both oscillators, the tight clock/region/offset/audio
settings, margin 1, HF-best two-ray channel, 1% selective perturbation and
Symbol Burst 3. Timing spacing was 15.625 ms and projections used 64-second
blocks. After applying the K=9 strict gate, the frozen phase/frequency UCB
primary accepted **32/32** paths. A static-frequency FFT UCB receiver,
implemented later and evaluated as a paired secondary comparison, also
accepted **32/32** on the exact same manifest and observation identities. The
Gaussian residual and phase/frequency results at `-30 dB-Hz` share their 273
physical paths, but
the dense `-34 dB-Hz` 32-path set is a separate seed range; cross-setting
counts are not a paired causal estimate of denser-grid gain. The primary's
32/32 gives a pointwise one-sided 95% exact lower confidence bound of
**91.063%**, supporting the requested relaxed 90% target at this selected
simulated condition. The paired FFT result has the same descriptive
pointwise bound, but is a secondary comparison rather than an independently
predeclared holdout; no selection-adjusted confidence claim is made for it.
Neither result establishes 99% reception. There are only 32 paths, and this is
not a near-bound measurement.

On those same 32 inputs, Gaussian residual-UCB accepted 26/32 under the strict
K=9 gate. Its same-score relaxed-threshold counterfactual accepted 30/32; the
pointwise one-sided 95% lower bound is 81.606%, so that count does not yet
demonstrate the 90% target. The counterfactual is a rethresholding of saved
scores, not a fresh validation set.

The coarser two-day `-30 dB-Hz` holdout was incomplete: the shard ledger scored
272 paths and a separately reported tail added one, for 273 unique paths out
of 300. The outer 2,700-second timeout left 23 exports running and four
pending; the 27 absent paths are not 27 individually recorded source failures
or detector misses. At the strict K=9 gate, Gaussian residual-UCB accepted
147/273 and phase/frequency UCB accepted 172/273. This is not an
oscillator-grade comparison. An independent retained-fraction diagnostic
measured retained fraction `0.066`, close to the rough `0.065` calculation,
supporting the predicted coarse-grid information loss. The coarse and dense
datasets use different physical seeds and `C/N0` values, so their reception
count difference is not a paired estimate of timing-grid gain.

Preliminary eight-path OCXO/TCXO runs are diagnostic only because their coarse
93.75 ms timing grid confounds oscillator comparison. A separate extra run
used a `-4 dB-Hz` likelihood reference for actual `-16`/`-34` captures due to a
C/N0 argument mismatch; those results are excluded from known-reference
sensitivity claims.

## Reproducibility and evidence

The archived run environment reports Python 3.12.3, NumPy 2.5.2 and PyTorch
2.8.0+cu128. The hour holdout likelihood run used CPU FP64, two threads,
60 GiB configured workspace and six block-coherence values (256 through
262,144). A phase/frequency GPU serial-versus-batched parity check had a
maximum absolute log-score difference of about `1.15e-5` on two two-day
sampled paths. In a separate static-frequency FFT comparison, CPU FP64 and GPU
FP32 had maximum score differences `1.285e-5` on 12 short microwave captures
and `2.216e-5` on two dense two-day captures; all compared candidates retained
the same winner and admission. These are narrow parity checks, not receiver
reliability qualifications.

The GPU recorded a warm one-hour run of about 2.7 seconds, while its CPU
reference was right-censored beyond 120 seconds. Because the CPU runtime is
censored, this does not support a speedup ratio. Other GPU timing comparisons
also have right-censored CPU references and are retained as such in the
analysis.

SHA-256 hashes of the archived study sources:

| Archived file | SHA-256 |
|---|---|
| `triad/tools/triad-noise-study.py` | `97c404a87b27f2b250217dca8a7c787c0263f595b4e9e521a7972af12fba63a3` |
| `triad/tools/triad_noise_likelihood.py` | `6964eee7372e0d3dbd590fb7f78a6c2b9c6d133dc012b488cce566f2d2447110` |
| `triad/tools/triad-diversity-export.py` | `bb871b98d436ab45605ad8127513d92fc25fff0461cc66ed26e3c5967e4a9015` |
| `triad/tools/triad_bounded_run.py` | `f117d6be27d38d055f5ab65af2f850c038501badd16cb5f524b5388530218cf2` |
| `triad/tools/triad-static-frequency-study.py` | `5622df804c6ebd1c1233b761d62eeb84a10dd34c451743069f1778ac852b9363` |

The [source provenance manifest](source-provenance.json) records the deployed
archive hash and current-tree source hashes separately. The verified
[technical archive](technical-results.tar.gz) and
[file index](technical-results.index.json) cover 2,113 technical artifacts,
including source snapshots, input manifests, export metadata,
scores and result JSON. Raw projection and sample arrays are not included;
their hashes are recorded in the archived manifests/reports. Provider and
account metadata are excluded.

The [`artifact-checksums.json`](artifact-checksums.json) manifest binds the
maintained report artifacts by hash.

The final analysis covered 164 reports with zero parse errors and zero strict
score-NPZ versus report mismatches. Detailed analyses and tests are retained
alongside the archive. The captured software checks include 33 noise-study
unit tests passing, seven static-frequency tests passing, and the focused triad
CTest group passing 13/13. Negative parser/calibration cases intentionally
print argparse usage/errors before their assertions pass. The numerical Python
tests require NumPy, SciPy and PyTorch. Reproduce with:

```sh
python3 -m unittest discover -s tests -p 'test_triad_noise*.py' -v
python3 -m unittest discover -s tests -p 'test_triad_static_frequency_study.py' -v
./build.sh test triad
```

The initial attempts also exposed and corrected: file-descriptor exhaustion
during a sharded holdout; a rejected precision spelling in an argparse
invocation; a mismatched C/N0 reference in an extra experiment; and a four-
second holdout accidentally started and stopped. The initial hour batch's 240
descriptor-failure records are retained as failures and excluded from the
separate repaired 300-path holdout denominator. For the timed-out coarse
two-day export, only the 273 completed unique paths enter the conditional score
rates; the 27 unfinished paths are reported as unfinished, not silently
counted as misses or successes.

## Remaining limitations

- The principal completed holdout is one-hour-duration data at `-16 dB-Hz`,
  while the completed two-day `-34 dB-Hz` holdout has only 32 independent
  paths. The coarse two-day `-30 dB-Hz` acquisition result is incomplete.
- The 300-path 99.006% lower bound is pointwise; the Bonferroni bound over the
  three tested Gaussian-family methods is 98.644%. The 32-path 91.063% bounds
  are also pointwise, not simultaneous over receiver families or all possible
  channel/noise conditions.
- The fixed-duration detectors do not constitute a complete operational
  duration planner. The new triad configuration is not supported by the
  current `triad_long --mode plan`; continuous rolling scheduling, outages,
  changing channel state and physical uncertainty still need integration into
  a planner with explicit reception and false-admission targets.
- The false-admission accounting covers the declared fixed protocol, method
  and duration family. Searching additional rates, formats, RF offsets or
  operating configurations requires a larger family correction.
- The static-frequency FFT receiver assumes a static residual CFO/channel over
  its decision window. Its two-day result on this simulated path family does
  not validate arbitrary oscillator or multipath evolution.
- All propagation and oscillator effects are simulated; neither HF nor 5.8 GHz
  transmissions were measured over radios or antennas.
- Sampled waveform duration and channel tails are real within the simulator,
  but phase-noise spectra and channel distributions remain chosen models.
- Near-bound performance with blind timing/frequency/phase and channel
  uncertainty has not been demonstrated. Unknown-noise calibration overhead,
  coarse acquisition loss and two-day computational scaling still need
  matched, completed measurements.
- The existing captured-hour projection output is a set of sufficient
  projections under the declared observation model, not a full retained raw-IQ
  recording for later arbitrary receiver experiments.

No universal optimality or field reliability claim follows from this report.
