# Production Helper / isolated three-carrier sampled study

This record accompanies [the maintained integration guide](../../../triad-helper.md).
It tests the requested HF and microwave configurations with the production
Helper waveform and the corrected shared sampled channel. It does **not**
qualify the ordinary receiver, live audio, physical RF hopping or a daily
operational false-admission rate.

## Frozen comparison

The same actual noisy I/Q supplies both arms. The baseline searches its complete
public CFO bank; assistance profiles both pilots and retains a conservative
subset of that same bank. Both keep all thirteen epoch alternatives, the
declared study arrival interval, the same DATA FIR and the same two-ray
working likelihood. Public templates and FFT/Gram workspaces are prepared once.
Evaluator truth is read only after scores and decisions exist.

| Configuration | HF | Microwave |
| --- | --- | --- |
| Source duration, excluding 38 s Helper lead | 23.2 s | 26.4 s |
| GUI Rate / Added | 36 Hz / ×10 interleaved | 360 Hz / off |
| DATA / Helper power | 4 / 40 W | 40 / 15,000 W |
| Physical DATA C/N₀ | 20.0206 dB-Hz | 10.0206 dB-Hz |
| Separate weakened experiment | 8 dB-Hz | 1.5 dB-Hz |
| RF / DATA audio center | 30 MHz / 1725 Hz | 5.8 GHz / 4000 Hz |
| High Helper pilot | 2400 Hz | 6412.5 Hz |
| Effective delay / ray separation | 21 ms / 90 Hz | 0.5 ms / 6 Hz |
| Additional modulation | Each 65%, dispersive DFT | Each 65%, dispersive DFT |
| Oscillators | GPSDO hobbyist XO | GPSDO OCXO |
| DATA/FHSS source | No FHSS | Fake FCC envelope, centered chip lattice |
| Search | 13 epochs × 3167 lags × 3 CFOs | 13 epochs × 42 lags × 125 CFOs |

The high microwave DATA audio center retains the production Helper guard for
fast ramp/cover-chip energy. It is not an IC-7100/SSB passband result. Full
study clock uncertainty is retained; the finite integer timing bank and
single sample-clock hypothesis are stated limits. Actual phase-continuous
Fake FHSS gates and continuous channel echoes are simulated, not RF retuning.
The listed C/N₀ uses a fixed active-DATA power reference. FHSS quiet time reduces
calendar-average energy; source component energies are retained per trial with
one shared gain, including Helper-off and DATA-off controls.

Each configuration/C/N₀ condition uses 19 independent Helper-present,
DATA-absent calibration captures, then eight balanced-bit signal captures and
eight independent Helper-present null captures. Gates are frozen before
holdouts. The empirical per-arm rule is
`p=(1+count(calibration_score >= observed_score))/(19+1) <= 0.05`.
This controls marginal per-capture false admission only under exchangeability
of calibration and future nulls; it is not a conditional guarantee for every
realized gate, a two-arm simultaneous guarantee, or a daily rate. The physical
and weakened experiments reuse seeds for paired channel/noise comparisons;
they must not be pooled as independent trials.

The true transmitted epoch is nominal; all thirteen alternatives are searched.
The receiver fits static direct/delayed-Doppler complex coefficients and omits
the additional modulation structure. Channel mismatch is therefore actually
present. No threshold or receiver parameter is tuned after held-out outcomes.

## Held-out results

Each arm has eight signal and eight null trials per row. Entries are **correct
admissions / wrong admissions / misses / false admissions**; false admissions
have the separate null denominator. These are results at the frozen experimental
5%-per-capture gate, not an operational recommendation.

| Condition | Full CFO bank | Helper-assisted | Assisted signal CFO bins |
| --- | --- | --- | --- |
| HF, physical 20.0206 dB-Hz | 8 / 0 / 0 / 1 | 8 / 0 / 0 / 1 | 3 → 3 |
| HF, weakened to 8 dB-Hz | 8 / 0 / 0 / 1 | 8 / 0 / 0 / 1 | 3 → 3 |
| Microwave, physical 10.0206 dB-Hz | 8 / 0 / 0 / 0 | 8 / 0 / 0 / 1 | 125 → 5 |
| Microwave, weakened to 1.5 dB-Hz | 2 / 1 / 5 / 0 | 4 / 3 / 1 / 1 | 125 → 5 |

At 1.5 dB-Hz both arms made three raw bit errors. More admissions are therefore
**not** evidence of uniformly better reception: assistance admits more correct
and more wrong bits at this gate. Both arms miss the last signal; the baseline's
winning CFO lies outside the selected subset in that case. The static true CFO
lies inside the engineering interval in all eight signals of every condition.
That is finite-sample evidence, not a guaranteed interval or preservation of
every full-bank maximum.

The conservative 95% balanced-population correct-admission interval is
[0.3344, 1] for an 8/8 result and [0.0471, 0.9529] for the weak microwave
assisted 4/8 result. The null one-sided 95% upper bound is 0.4707 for 1/8 false
admissions and 0.3123 for 0/8. These wide intervals prevent a high-reliability or
low-false-admission claim. The paired C/N₀ conditions must not be pooled to
artificially narrow them.

There were 140 complete sampled captures: 76 calibration, 32 signal and 32 null
captures. Campaign wall times were 473.40, 465.21, 729.49 and 641.43 seconds
in table order. Jobs overlapped on the local Ryzen 5 PRO 5650U with two numeric
threads each, so these durations are not additive elapsed time or isolated
throughput benchmarks. The exact per-stage costs, work counts, frozen gates,
seeds, source/binary identities, failures and corrected uncertainty are in
[campaign-results.json](campaign-results.json).

## Known-channel benchmark and the weak microwave failure

The eight weak microwave captures were regenerated byte-for-byte (all raw
SHA-256 hashes matched), then compared against two near-noiseless complete
waveforms with the same actual channel, timing, oscillators and Helper.
Only this **oracle** receives that knowledge. For equal bit priors and raw
complex noise variance `v`, let `d²=||μ₁−μ₀||²`. The known-channel decision uses
`LLR = 2 Re[(y−(μ₀+μ₁)/2)ᴴ(μ₁−μ₀)] / v`, and its conditional binary error
probability is `Q(sqrt(d²/(2v)))`. This excludes acquisition, channel learning,
presence admission and daily false-admission constraints.

The oracle selected 8/8 bits correctly on the actual draws. Its theoretical
error probabilities range from 6.99% to 11.92%, mean 9.36%; a fortunate 8/8
sample does not erase that thermal limit. The practical receiver disagreed
with the oracle on three raw bits. Eight trials do not precisely estimate the
practical excess error or a sensitivity gap in dB.

The measured `d²/v` range is 2.7806–4.3624. A conditional 1% oracle bit-error
target requires 10.8238, or **2.48–3.89× discrimination energy** (+3.95–5.90 dB)
for those channels. This is not a measured duration factor: changing duration
can change channel evolution, pattern geometry, acquisition size and prior loss.
The observed failure cannot be repaired merely by lowering a presence threshold.

The EME format has 64 protected cells among 1,584 fine calendar cells. The
source bit-difference energy is 10.336% of average full DATA energy; in the
symmetric midpoint/half-difference decomposition the bit-dependent half is
only 2.584%. The midpoint includes both cover and common payload energy, so
it must not be mislabeled entirely as cover. Total received energy is therefore
a misleading binary-discrimination benchmark for this short guarded format.
Improving that allocation would be a separately tested waveform change; it is
not silently changed by this Helper integration.

[Oracle summary](eight-signal-oracle.json), [source-energy measurement](source-template-energy.json)
and the [compact oracle reproduction archive](oracle-reproduction.tar.gz) retain the benchmark inputs,
scripts, commands and hashes. The added seven cases took 72.61 s wall / 73.96 s
CPU for 21 native exports; the first case took 10.31 s. No practical receiver
was rescored or retuned for this benchmark.

## Numerical and leakage controls

The independent paired preflight uses seeds 87301 (HF) and 87302 (microwave),
outside campaign seed ranges. Both raw bits were correct; these preflight picks
were **not** calibrated admissions. Microwave narrowing was 125→5 CFOs.

- Microwave FP32 versus FP64 retained identical bit, epoch, timing, CFO and
  observable-rank histograms. Maximum bit-score difference was `1.639e-5`.
- Turning Helper off with identical channel/noise seeds and fixed DATA gain
  preserved the DATA bit and synchronization. Winning-score changes were
  `−0.00441255` HF and `−9.8335e-7` microwave (on minus off).
- Subtracting matched on/off raw captures isolates the linear propagated
  Helper component. After the actual continuous DATA FIR/crop, mean Helper
  leakage was 75.33 dB below expected filtered thermal noise in HF and
  104.86 dB below it in microwave. Peak 400-ms ratios were `1.48e-6` and
  `1.16e-10`. This is a sampled linear-chain measurement, not a hardware
  clipping, anti-alias or RF rejection guarantee.
- The 1105-tap FIR measured 95.50 dB stop-band rejection and maximum passband
  magnitude error `1.712e-5` in the focused test. Public template energy after
  filtering is reported, not silently renormalized to hide loss.
- Default legacy source/template/observation bytes match their pre-adapter
  exports. The new FHSS-center format is explicit and separately keyed.

In an otherwise idle serial preflight, microwave DATA scoring took 12.414 s
for 125 CFOs versus 0.466 s for five, with 0.304 s Helper estimation and 0.289 s
shared filtering. Reusable preparation took 6.911 s. Resident total receiver
work is therefore approximately 12.704 versus 1.060 s, a 12.0× improvement;
including preparation reduces this one-capture comparison to about 2.46×.
These are measured single-capture costs, not confidence-qualified performance
forecasts. Campaign jobs share CPU capacity, so their larger timers are reported
as campaign measurements, not substituted into this serial comparison.

HF retained all three CFOs. Its GPSDO prior after margin is already only
±0.009 Hz, smaller than the conservative Helper allowance. There is no measured
HF frequency-work reduction. Constant phase is already absorbed in fitted DATA
coefficients; no time-varying phase improvement is claimed.

## Qualification and reproduction

The native triad group initially passed 14/15 checks; the new source-export
configuration test supplied unsupported channel options and failed before its
new case ran. Its command was corrected and the focused test passed; the
original log is retained. Final focused Python checks passed **47/47** in
0.580 s excluding imports. Native exporter smoke passed in 11.57 s. The two
affected native source/channel ASan/UBSan/LeakSanitizer tests passed in 30.26 s.
Their first traced-sandbox run failed because LeakSanitizer cannot operate
under ptrace; the successful untraced rerun kept leak detection enabled.

Independent review found and corrected two reporting/reuse issues after the
frozen campaigns: pooled signal CP intervals did not match the fixed-bit design,
and cross-run gates needed a Helper on/off calibration-condition identity in
addition to the receiver procedure identity. All actual campaigns already held
that condition fixed. The correction changes neither DSP, scores, gates nor
saved decisions. The original script/raw summaries and
[reporting-only patch](reporting-correction.patch) are retained;
`campaign-results.json` supersedes their pooled signal intervals.

The [raw campaign archive](raw-campaigns.tar.gz) contains public manifests,
evaluator files, protocol/gate records and per-trial scores for file-only
verification. It omits large I/Q arrays, whose exact hashes and generation
commands remain recorded. Extract into an empty directory, then run:

```sh
mkdir -p /tmp/triad-helper-replay
tar -xzf docs/validation-data/triad-v3/2026-11-04-helper/raw-campaigns.tar.gz \
  -C /tmp/triad-helper-replay
python3 tools/triad_helper_evidence.py /tmp/triad-helper-replay/*-campaign \
  --output /tmp/triad-helper-replayed.json
```

This verifies frozen decision arithmetic, file identities, conditions, source
bits and corrected uncertainty without repeating simulation. It is not a new
sampled receiver validation or a cryptographic certification of the experiment.

Use the guide's focused checks first. The native source/channel adapter and the
new centered lattice have explicit state, tail, gain, superposition and parity
checks. Python checks exercise estimator numerics/fallback, actual receiver
behavior, orchestration, frozen gates and uncertainty separately; mocked
orchestration is not substituted for sampled reception.

The campaign is optional research, not a routine CI prerequisite. A complete
example after generating templates is:

```sh
OPENBLAS_NUM_THREADS=2 OMP_NUM_THREADS=2 python3 tools/triad_helper_campaign.py \
  --case eme --cn0 1.5 --seconds 16 --seed-base 92000 \
  --templates local-evidence/helper-eme-templates \
  --exporter build/dev-cli/triad_helper_export \
  --output local-evidence/helper-eme-target --discard-iq
```

Use seed base 91000 for HF, and replace the C/N₀ with `physical` for the exact
power/loss examples. `--discard-iq` preserves hashes, configuration, seeds,
per-trial results and generation commands while removing the large sampled
arrays after scoring. Template generation and loading occur once, not per
trial. CUDA can accelerate DATA template/observation FFT correlations and
selected likelihood work; Helper estimation, FIR conversion, Gram preparation
and native channel simulation remain CPU stages. This campaign measures CPU
execution only. No RunPod capacity was rented, and no GPU acceleration result
is claimed for this composition.

## Interpretation limits

Eight held-out signals cannot establish 99% reception. Eight nulls with no false
admissions would still have a one-sided 95% upper probability bound of 31.23%.
Signal uncertainty must be stratified by the fixed alternating bit assignments;
pooled exact-binomial intervals are not justified without equal bit success
probabilities. The final evidence uses per-bit bounds and a conservative
balanced-population bound. It does not promise operational reliability from a
small feasibility campaign.

The Helper estimator's intervals are conditional engineering allowances for
static CFO, not calibrated confidence or pathwise phase bounds over later DATA.
Its finite sinusoidal line model and unfitted gate harmonics are explicit.
Realistic stochastic spreading, stronger phase diffusion, competing tones
outside the quiet-spectrum assumption, real radio retuning and long-duration
phase transfer require further qualification. Ordinary integration and release
certification remain outside this study.
