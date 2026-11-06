# Gentle GPS corrections: persistent default receiver

This qualification concerns **scheduled, post-acquisition, low-IF DATA** with
known simulated white Gaussian noise. It is not a qualification of arbitrary
multi-day trajectories, blind acquisition, Helper tracking, physical ADC/ASRC
false admissions, or uninterrupted multi-day hardware operation. Ordinary
Robust integration is unchanged. No cloud resource was needed or launched.

## Implementation and causal findings

The isolated live adapter now defaults to `PracticalTrackingReceiver`: a fixed
equal likelihood mixture of a persistent original-affine bank and persistent
adaptive timing inference. Both retain channel posteriors across segment
boundaries. The original affine anchor remains fixed even if the initial
segments are erased. Global bit, acquisition and coherence priors are retained;
pruning never renormalizes surviving evidence. This changes inference, not
encryption, source patterns, power allocation, interleave or the waveform clock.
The existing full-audio discipline remains enabled by default with a 200-ppm
phase catch-up cap, separate from device-rate calibration.

The adaptive component uses conditional Gaussian moment matching when histories
merge. This is an approximate channel model with normalized predictive
densities, not the exact unmerged switching likelihood. See the original
[interacting multiple-model paper](https://doi.org/10.1109/9.1299) and
[Ramdas and Wang, chapters 7–8](https://www.stat.cmu.edu/~aramdas/ebook-final.pdf)
for conditional likelihood products and dependent e-value mixtures. Under the
declared proper Gaussian null, averaging valid alternatives remains valid;
independence between the two lanes is unnecessary. The per-bit mixture obeys
`log E >= log E_lane - ln(2)`. This is an evidence bound, not a universal 3-dB
sensitivity penalty or a bound on the final bit ranking.

Two distinct defects were diagnosed and retained:

* Resetting channel priors at every timing update destroyed useful long-coherent
  evidence. A focused analytic test changed from log-e −1.397 to 15.076 with
  persistent channel state and identical retained timing mass.
* The first full GPS capture gave reset −71.212, persistent adaptive −49.239,
  and nominal persistent +24.252. A 50-ppm grid made 81 rates reachable while
  retaining only 64. Approximately 0.787 mass survived each update; cumulative
  initial-prior mass fell to 1.1e−32. A hard acceleration bound used as a
  uniform transition probability is a diffuse public model, not a measured GPS
  clock distribution. The default nominal lane prevents this loss from erasing
  otherwise useful evidence. Automatic low-IF spacing now coarsens up to 200 ppm
  within geometric phase and timing budgets; high-IF branches remain finer.

The native basis server also rejected the previous default ±2000-ppm bank at
its old ±1000-ppm diagnostic cap. It now accepts ±4096 ppm and checks the actual
candidate's occupied passband. Out-of-band candidates still reject explicitly.

## Physically coupled clock fixture

`--clock-profile gps-disciplined` uses one signed system oscillator drift for
monotonic and UTC time; GPS steps correct accumulated UTC error toward physical
time. It does not add independent positive phase commands on top of that drift.
The seeded drift varies between 70% and 100% of ±20 ppm over seven days.
Hourly fixes have ±300-s timing jitter and ±1-ms residual error; a weekly 24-hour
holdover produces occasional 1.7–1.9-s recovery corrections. The native controller
is evaluated every physical second, independently of exported sample rate.
The device frame clock is ideal in these experiments. These are controlled
plausible fixtures, not a measured population of GPS receivers or oscillators.

Complete 514.973-day captures contain over ten thousand fixes. Ordinary steps
were at most 84 ms; maximum source displacement was 1.793 s and 1.866 s for the
two independent signed-drift/key cohorts. Signal mapping retains
`s(u(t))*exp(j*2*pi*f_IF*(u(t)-t))` before the shared continuous channel. The
receiver receives only public format/key/bank parameters, never private TX maps.

Mismatch removes only one fitted constant complex gain:
`loss_dB=-10*log10(|template^H signal|^2/(energy_template*energy_signal))`.
Energy is recorded separately and never normalized to conceal channel loss.

| Complete source | RRC mismatch | C2 mismatch |
|---|---:|---:|
| Rate .01, Added ×1, five days, holdover | 0.000842 dB | 0.000843 dB |
| Rate .1, Added ×1, five days, positive drift | 0.08523 dB | Not separately tested |
| Rate .1, Added ×1, independent negative drift | 0.09262 dB | Not separately tested |
| Rate .1, Added ×10, ordinary fixes | 0.01268 dB | 0.01267 dB |
| Rate .1, Added ×10, holdover recovery | 0.96053 dB | 0.96050 dB |
| Rate .001, Added ×1, 514.973 days | 0.0000064 dB | Not separately tested |

All use half-rate IF, encryption and independent bit alternatives. Five-day
RRC integrated-energy changes were below 0.000005 dB; long-span changes below
0.0000004 dB. Explicit C2 rendering uses the same pulse as the shaping
implementation, with unchanged source geometry; it does not qualify every
automatic shaping-planner choice. The historical exporter default remains RRC;
new manifests and public template servers identify `--pulse-shape c2` explicitly.

At Rate .1 with Added ×10 the actual fine-symbol duration is six seconds. GUI
rate alone is insufficient to price tolerance. Conversely, at 1725-Hz IF,
200 ppm produces 0.345 Hz of common phase motion even when the envelope moves
very little. Low-IF results cannot be transferred to that regime without phase
inference qualification. A low-IF complex-baseband fixture is not an audio cable
capable of passing DC.

## Reception, uncertainty and boundaries

The unchanged default operational log-e threshold is **21.4933786883**: a
1e−4 daily noise-admission budget with up to 216001 acquisition attempts per
24 hours. The study also records a less stringent single-endpoint diagnostic
threshold; the following claims use the actual default threshold.

Full sampled receiver results and exact CPU/workspace measurements are in the
companion `streaming-summary.json`. One 14.472-day capture at −45 dB-Hz used the
actual default .25-s requested block setting (one 64-s sample per block at its
reduced Fs). It passed at **log-e 23.987**. Independent negative-drift bit-1
trials with 4096-s blocks passed at **26.822 and 21.639**. Block changes are
explicit computational/model regimes, not new transmitted durations.

The full **514.973-day, −60-dB-Hz** observation also passed at **37.654**, using
4096-s blocks and the default timing/grid/mixture policy. Every one of its
695213 samples was processed. The fixed affine lane retained strong evidence
while the adaptive lane alone missed; the protected mixture behaved as intended.
Two complete C2 .01-Hz observations at −41 dB-Hz scored **20.204 (miss)** and
**24.224 (correct)**. Neither threshold nor seed was adjusted to obtain a pass.
Their source-bound conditional screen predicts approximately 84–85% acceptance
at this deliberately marginal SNR; one miss in two trials is consistent with
that uncertainty. It is not evidence for 100% reliability.

The cheap conditional experiment processes every source sample once to form a
joint Gram preserving dependence across both patterns. It then draws 4096
Gaussian-noise trials per bit and 32768 null trials per source. Paired corrected
and uncorrected means share the same noise. Eight independently sampled noise
streams per bit/condition/null check the reduction through every actual row;
bulk FP32 versus direct FP64 scores differ by at most 6.37e−6. The complete
seven-source experiment took 7.29 CPU seconds, excluding imports.

The static nominal component has prior weight 1/6 (one half for its lane, one
third for its coherence model). Its conservative evidence screen therefore
uses the default threshold plus ln(6). Results are a lower bound on the full
mixture's evidence, **not a guarantee of its correct-bit selection**. The full
streaming runs test that additional distinction.

| Source / C/N0 | Corrected correct admissions, bits 0 / 1 | Paired no-clock control |
|---|---:|---:|
| Rate .01, five days, −41 dB-Hz | 3477 / 3449 of 4096 | 3478 / 3448 |
| Rate .1, five days, −41 dB-Hz | 3111 / 3146 | 3222 / 3232 |
| Rate .1, independent negative drift, −41 dB-Hz | 3125 / 3171 | 3225 / 3278 |
| Rate .1 ×10, ordinary fixes, −41 dB-Hz | 3261 / 3289 | 3278 / 3297 |
| Rate .1 ×10, holdover recovery, −41 dB-Hz | 2015 / 2054 | 3283 / 3305 |
| Rate .001, 514.973 days, −60 dB-Hz | 3977 / 3992 | 3977 / 3992 |

Every static case had zero wrong admissions and 0/32768 null admissions. Exact
two-sided 95% intervals for long-span correct reception are 96.53–97.59% and
96.93–97.92%. The zero-null empirical upper bound is approximately 1.13e−4 per
endpoint, not a demonstrated daily rate. The conditional likelihood argument
and explicit attempt budget supply the theoretical accounting; physical
ADC/ASRC covariance remains unqualified and live admission remains withheld.
Conditional noise trials on a few clock/channel paths do not estimate a broad
physical channel population.

The current C2 waveform was also checked independently: 3444/4096 and 3497/4096
correct weighted-static admissions, versus paired no-clock 3449/4096 and
3498/4096, with zero wrong and 0/32768 null admissions. Its correct-reception
95% intervals are 82.93–85.19% and 84.26–86.44%. This extra whole-waveform/statistic
check took 0.388 CPU seconds.

Random phase is an unresolved separate boundary. The independent negative-drift
514.973-day fixture with 0.001 rad/sqrt(s) phase diffusion retains negligible
clock-only mismatch but loses 5.58 dB against a static channel. Static evidence
passes only 23/4096 and 25/4096; a customized OU/static pair improves to 73/512
and 89/512, still far from its known-channel reference near 99%. The current
default .5/2/infinite-second coherence alternatives do not cover this useful
long channel timescale. The oracle knows the complete private channel, so its
performance is a reference, not an achievable promised result. Both long
fixtures carry E/N0 about 44.293 (16.46 dB); absent-signal admission and binary
decision error are different metrics. No fundamental impossibility is claimed.

The already-maintained 128-state unit-phase likelihood was also evaluated on
the same new fixture and paired noise. It improved to **132/512 and 131/512**;
the fixed OU/static/phase mixture produced **110/512 and 116/512**, with zero
wrong/null admissions, in 4.52 CPU seconds. Thus this relevant alternative was
not omitted, but it does not close the unknown-channel gap. Its phase grid and
blockwise approximations are a declared model, not a proof of optimality. It
remains an explicit experimental alternative, not the new default clock tracker.

## Costs, prediction scope and inexpensive reuse

The reference CPU is the local Ryzen 5 PRO 5650U, representative of the requested
5560U class. The default-block 14.472-day run used 94.908 Python CPU seconds plus
6.699 native-template CPU seconds: **0.0000813 CPU seconds per received second**,
with approximately 4.51 MB of declared receiver workspace. This includes both
lanes and template generation, excluding source simulation and interpreter
startup. Workspace is not total process RSS. Explicit long-block measurements
are retained separately; modest concurrent qualification work means these are
resource observations, not confidence-certified performance regression anchors.

The 514.973-day run used **334.132 Python + 154.419 native CPU seconds**,
**0.00001098 CPU seconds/input second**, 6.46 MB declared workspace and 488.85 s
wall time. It executed 173.8 million template row-cells and 57.0 million
transition visits; measured counters stayed within preflight bounds. Preparation
of its receiver objects took 2.74 ms, excluding imports and native process
startup. The two C2 runs averaged **0.0001918 CPU seconds/input second**, including
exact template generation; their declared workspace was 18.05 MB. The C2 timer
is a different sample-rate/pulse/work regime and must not be interpreted as a
direct shaping speed ratio. No GPU was useful for the few-second conditional
matrix experiment or these serial native-template/small-state qualification
paths; these measurements directly test the requested local CPU instead.

`triad_weak_plan.py --tracking-bank BANK.json` now exposes the actual two-lane
work calendar, proposal-cap scoring, transition visits and bounded workspace.
The planner itself is O(1) in observation length, tested at 2^50 rows. It respects
bank projection bands, arithmetic and public dynamics, and includes both local
and remote software endpoint allowances. It rejects insufficient workspace
rather than silently narrowing the search. A CPU forecast requires matching
source/hardware/regime and `receiver-excluding-basis-v1` timer scope; otherwise
it reports unknown. Even matching work-scaled forecasts are labeled engineering
estimates until independently qualified. Native preparation, ASRC and finalization
remain separate costs. Do not transfer a legacy-pulse generation timer to C2.

Use routine tests for affected branches, not the historical multi-day campaign:

```sh
./build.sh configure --cli
cmake --build build/dev-cli --target triad_tracking_export -j2
python3 tests/test_triad_tracking_export.py build/dev-cli/triad_tracking_export
ctest --test-dir build/dev-cli -R '^triad_tracking_budget$' --output-on-failure
# In an environment with numpy/scipy/torch:
python tests/test_triad_persistent_tracking.py
python tests/test_triad_continuous.py
python tests/test_triad_tracking_preflight.py
python tests/test_triad_weak_plan.py
```

Current focused receipts: 31 native protocol/pulse/rate checks, 12 persistent
receiver checks, eight pure budget checks, 11 adapter checks, three preflight
checks and 12 duration-wrapper checks. Existing Gaussian/timing/incremental
references were checked when their shared implementation changed. New long
observations are occasional qualification, not CI prerequisites. The registered
pure-budget CTest takes about 0.07 seconds.

## Reproduction and provenance

`evidence.tar.gz` retains compact manifests, exact source/input identities,
commands, raw scores/Grams, failure reports and study receipts. Regenerable large
sample files and executable binaries are omitted. Source generation commands
live in each `*-source/results.json`; receiver commands are retained with the
streaming summary. `tools/triad_gentle_tracking_study.py` consumes complete native
captures and refuses existing output files. The exact conditional replay script
is in the archive. Use the named receiver/pulse/block/noise regime rather than
substituting an execution witness for likelihood validation.

One initial full-default run reported `qualification_valid=false` because a
nonzero, nonbinary rate-bound rounding fix landed while it ran. Its loaded
receiver source is saved verbatim. Independent review verified that the default
zero-initial-rate construction is numerically identical before/after that fix.
A later optional C2 provider change preserves the absent-field legacy path;
both source deltas and old provider source are retained. The raw guard is not
changed to true and the long run was not wastefully repeated. Subsequent frozen
cohorts retain their own guards. Setup failures and original pruning failures
remain visible; passing results do not erase them.
