# Weak-receiver duration selection: theory and brief sampled validation

This study adds an automatic, **provisional** duration selector for the isolated
Helper-assisted weak receiver. It replaces manual duration selection when using
`tools/triad_weak_plan.py --target-cn0 …`; it does not change the ordinary Robust
receiver or reinterpret the native exporter's noise-injection `--cn0` option.
See the maintained [algorithm and operating guide](../../../triad-duration-planning.md).

The selector uses received energy, finite-span channel covariance, original
search size, operational attempt/duration budgets, declared outages, and the
native minimum-pattern/tile geometry. It uses a lower-tail probability bound,
not just mean likelihood. The default request is 99% correct reception and
10⁻⁴ admission probability per day. Half the per-message failure budget is
allocated to modeled detection; acquisition, wrong-bit selection and Helper
availability have a **reserved, unqualified** remaining budget. The outputs
explicitly leave model, correct-reception and physical-null qualification false.

## Frozen plans and scope

Forecasts were frozen before generating/scoring the validation observations.
Both configurations use DATA4W/Helper40W, 180dB loss, physical noise density
−164dBm/Hz, encrypted native patterns, 1725Hz DATA IF, auto shaping, no FHSS,
no burst waits, and a separate public 13-epoch/two-bit template bank. The
physical nominal DATA C/N₀ is20.0206dB-Hz. Fresh AES-256 full-rate noise was
added after the simulated channel and real-audio conversion, targeting nominal
DATA8dB-Hz and Helper18dB-Hz. This is a post-channel laboratory impairment;
it does not preserve a total44W radiated-power budget including that noise.

* **HF:** Rate36, Added×10/interleave, free-running base oscillator, IC-7100
  shift oscillator, 30MHz shift, default broad arrival prior, HFworst margin3
  (90Hz ray-center separation/21ms delay), stochastic per-ray Doppler width
  B=.5Hz, .2µs moving-delay excursion/20s knots, Each65% random selective
  modulation. Independent channel seed2026112601, bit1, arrival−2.2s.
* **Stable:** Rate100, Added×1, GPSDO OCXO base/shift, 500kHz shift, bounded
  arrival prior, None/Off spread/selective modulation, legacy fixed-amplitude
  channel with its selected oscillator effects. Independent channel
  seed2026112602, bit0, arrival+.04s.

The shared key-seed2417/public epoch1793850000 are fixture inputs. Public
templates used a separate seed and bit. No evaluator bit, channel trajectory,
arrival or component-energy measurement entered acquisition or admission.
Duration alternatives=2 was charged in both planner and receiver. The original
HF grid has about29.85billion atoms, stable64,233; pruning retains original
prior mass. The operational log-e threshold is22.186525869.

| Forecast target | HF active DATA | Stable active DATA |
|---|---:|---:|
| 20dB-Hz | 11.6s | 10.4s |
| 8dB-Hz | 46.4s | 20.8s |
| 5dB-Hz | 92.8s | 31.2s |
| 0dB-Hz | 487.2s | 83.2s |
| −10dB-Hz | 34,266.4s; export unavailable | 738.4s |
| −60dB-Hz | No solution within10-year model limit | 74,746,807.2s; export unavailable |

Only the8dB-Hz rows received sampled/live validation here. Long predictions
are hypotheses under the stated covariance model, not long-term oscillator,
streaming or physical demonstrations. Native fixture export remains limited
to3,600 requested active seconds and additional sample/byte/alias constraints.
Helper-off and infeasible placement are explicitly unavailable. Native burst
preflight checks exercised active20.8s versus elapsed52s; burst reception was
not newly qualified.

## Actual waveform and cable results

The following are individual conditional admissions, not estimated success
rates. Nominal C/N₀ uses quiet PSD and the fixed unit DATA reference. Realized
C/N₀ additionally uses post-score full-signal minus matched Helper-only
simulation energy, including tails. Every subtraction had exactly identical
quiet channel/noise prefixes. Live realized C/N₀ is an estimate using that
simulated component energy and the previously independently measured scalar
cable gain0.8414025 at1737Hz; frequency-response and gain-drift uncertainty is
not eliminated by this calculation.

| Configuration / active DATA | Path | Nominal / realized C/N₀ estimate | Log-e | Correct / wrong / missed |
|---|---|---:|---:|---:|
| HF planned46.4s | Software | 7.981 /9.249dB-Hz | 114.847 | 1/0/0 |
| HF planned46.4s | Cable | 7.971 /9.240dB-Hz | 126.407 | 1/0/0 |
| HF shorter34.8s | Software | 7.981 /9.576dB-Hz | 42.801 | 1/0/0 |
| Stable planned20.8s | Software, corrected fallback | 7.986 /7.985dB-Hz | 117.504 | 1/0/0 |
| Stable shorter10.4s | Software, corrected fallback | 8.066 /8.067dB-Hz | 60.992 | 1/0/0 |
| Stable planned20.8s | Cable, corrected fallback | 7.955 /7.955dB-Hz | 119.367 | 1/0/0 |

Each planned software/cable pair used **identical saved noisy transmit PCM**;
it was transmitted physically once. Different durations use the same channel
seed but different native patterns/calendars and fresh AES noise: those pairs
do not isolate a causal duration effect. The HF planned realization gained
1.268dB of mean DATA energy from the simulated channel. It must not be described
as demonstrated realized8dB-Hz HF reception.

Actual Helper-to-end transmit times are84.6675s/73.0675s for HF planned/shorter
and59.7922s/49.3920s for stable planned/shorter. Use these exported calendars,
not active DATA time or approximate calendar reporting. A reporting-only fix
after the forecasts were frozen adds pulse halos to the approximate estimate;
it changes neither chosen tile count nor any detection prediction.

The two initial stable software runs **failed before DATA acquisition**: Helper
returned `flat_profile` because the ±.00005017Hz oscillator prior was already
much narrower than the measurable pilot profile. The receiver incorrectly
required further narrowing. The correction searches the original interval when
Helper explicitly returns a frequency fallback, using the existing frequency,
FFT-product and memory guards. It preserves the original arrival prior, grid
mass and admission threshold; early failures without a frequency result remain
unresolved. Replaying the exact saved PCM then produced the two stable software
successes above. Both initial failures are retained in the evidence.

One fresh-noise, Helper-only stable software control produced0/1 admissions
(log-e−7.353). The one-sided95% binomial upper bound is95%, so this supplies
only a functional negative control. At1/1 success the two-sided95% binomial
interval is[2.5%,100%]; software/cable pairs sharing a waveform are not
independent channel trials. These tests do **not** establish99% physical
reliability or a10⁻⁴/day physical false-admission rate.

All noisy software outputs had zero reported digital clipping. Both raw48kHz
stereo S16 cable captures had no reported discontinuity and zero ADC near-full-
scale samples. No mixer setting was changed. This does not bound small analog
distortion. Propagation was simulated before the cable; the cable was not an
HF or microwave propagation experiment.

## Independent covariance calculation and allocation interpretation

A separate Toeplitz covariance eigensolver used512 time points per ray and
100,000 independent Gaussian-likelihood draws per condition (about10.7CPU s).
It did not use the equal-mode Gamma approximation to generate scores.

| Target / active duration | Planner surrogate detection lower bound | Independent covariance misses /100,000 |
|---|---:|---:|
| 8dB-Hz /23.2s | .768774 | 7,427 |
| 8dB-Hz /34.8s | .994033 | 177 |
| 8dB-Hz /46.4s | .999934 | 1 |
| 5dB-Hz /34.8s | 0; uninformative bound | 62,558 |
| 5dB-Hz /81.2s | .992398 | 204 |
| 5dB-Hz /92.8s | .998957 | 33 |

Exact binomial intervals are retained in `model-reference.json`. This checks
the surrogate at these points against a discretized **known covariance,
true-atom detection** model. It excludes acquisition, wrong-bit selection,
selective-modulation mismatch and audio. The5dB-Hz/34.8s modeled detection
probability is about37.4%; it is not a measured receiver success probability.

At8dB-Hz the planned nominal E/N₀ is292.764 for HF and131.239 for stable.
The known-received-waveform/search-accounted true-detection references are
72.651 and55.970 respectively: nominal gaps6.05dB and3.70dB. These gaps combine
different channel knowledge, conservative probability bounds and tile
granularity; they are not measured implementation losses or universal bounds.
Both shorter neighbors passed this particular observation. The additional
tile costs1.25dB HF and3.01dB stable, rather than orders of magnitude of energy.
The study supports a reasonable conservative starting allocation, while not
establishing that these longer durations are physically necessary or optimal.

## Reproduction, cost and remaining work

`summary.json` and `evidence.tar.gz` retain forecasts, protocol, commands,
raw JSON results, model-reference script, initial failures, component-energy
checks, source/binary/input hashes and frozen pre-correction sources. Base
revision is `e17d2c02b7a7cf17f4b2881a214749a93b35d2f5` plus the recorded source
changes. HF was measured before the fallback correction; its narrowed-Helper
branch is unchanged and covered by focused tests/review. Stable replay/live
used the corrected branch. This source transfer is explicit, not a claim that
the final source repeated every physical test.

Large I/Q/PCM files remain in ignored `local-evidence/triad-duration-planner/`.
The compact archive records their hashes. Ephemeral AES keys are not retained;
fresh reruns reproduce the procedure/distribution, not the exact noise bytes.
The retained local PCM supports exact replay of this run.

Cheap routine checks (57 tests,0 failures/errors/skips;2.01s including imports):

```sh
PYTHONPATH=tools:tests OPENBLAS_NUM_THREADS=2 OMP_NUM_THREADS=2 MKL_NUM_THREADS=2 \
  /tmp/triad-weak-env/bin/python -m unittest \
  test_triad_duration_model test_triad_weak_plan test_triad_operational_admission \
  test_triad_weak_receiver test_triad_stream_run test_triad_stream_slots \
  test_triad_segmented_fft test_triad_realtime_fft
```

Use the maintained wrapper example for normal planning; full native arguments
and independent seeds are in archived `protocol.json`/`prepare.py`. Run sampled
qualification only when a changed model/receiver path warrants it. Reuse the
compatible `build/dev-cli` binaries; no native code changed in this study.
The existing ordinary receiver's integration/regression campaign was not run.

LocalCPU is Ryzen5 PRO5650U with two numerical threads. HF planned preparation
cost8.20CPU s and prepared software reception80.64CPU s (including ingress,
Helper, acquisition and refinement), for46.4s active DATA/84.67s Helper+DATA.
This is a duration study, not a new continuous-message realtime qualification.
The corrected stable saved-PCM replays cost7.03/5.02CPU s including preparation.
The two live runs occupied194.2s/88.6s including capture and drain. GPU rental
would not improve this small diagnostic or the physical cable wall time; no
cloud resource was deployed.

Remaining gaps: acquisition/correct-bit tail probabilities, a justified physical
noise envelope, severe selective/moving-delay likelihood mismatch, full burst/
missed-hop outage distributions, overlapping-ray covariance, multi-day sampled
export/receiver operation, and independent many-seed reliability validation.
The planner exposes these limits rather than converting them into an arbitrary
fixed dB margin or claiming a configured probability has been achieved.
