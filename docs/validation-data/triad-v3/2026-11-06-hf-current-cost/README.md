# Current Helper-assisted HF receiver cost

For shorter transmission at the physical link's approximately 20 dB-Hz DATA
C/N₀, see the [11.6-second high-SNR follow-up](../2026-11-06-hf-high-snr-cost/README.md).
This report retains the separate 46.4-second allocation planned for 8 dB-Hz.

This is a fresh cost measurement on **AMD Ryzen 5 PRO 5650U**, using two
numerical-library threads, source `7ac6bc22`, and the current encrypted
independent-pattern/4-APSK waveform. It answers the specified physical
40 W Helper / 4 W DATA / 180 dB link question. It is not a throughput
qualification for arbitrary messages, a physical cable test, or ordinary Robust
receiver integration. No runtime implementation was changed for this measurement.

## Configuration and timing boundary

Rate 36 Hz, Added Symbols Interleave ×10, free-crystal base oscillator,
IC-7100 shift oscillator, 30 MHz shift, DATA IF 1725 Hz, HF-worst spread,
Each 65%, margin 3, default broad timing uncertainty, Helper on, FHSS/Burst off,
and automatic shaping. The planning target is 8 dB-Hz. At the native declared
noise density −164 dBm/Hz, the specified powers/loss instead give **20.0206 dB-Hz
DATA and 30.0206 dB-Hz total Helper** before channel energy changes.

The changing channel explicitly uses `--channel-evolution stochastic`, ray
renewal width 0.5 Hz (2σ), delay excursion 0.0002 ms, and 20-second delay knots.
These are retained study assumptions, not parameters uniquely implied by
“HF worst”: margin 3 makes the preset ray-center separation 90 Hz and nominal
excess delay 21 ms. The random Each-65 modulation and all sampled channel tails
remain present. A different renewal law can change both sensitivity and cost.

The public bank is independent of the received channel seeds. Both bits, all
13 key epochs, three coarse clock branches and the broad arrival lattice are
searched. Helper narrows the coarse frequency bank from **365 to 15 points
(24.33×)** in both physical-link trials. This is a candidate-count reduction,
not a measured whole-receiver speedup. The segmented FFT proposes 30 fine
candidates for Gaussian-state refinement; the approximately 29.84 billion
atoms in admission accounting are **not** all executed fine-grid evaluations.
No pilot phase-reference correction is applied in these trials.

Measured RX CPU is the outer process timer across real 48 kHz PCM ingestion,
continuous complex downconversion to 6 kHz, Helper estimation, acquisition,
observation-dependent refinement preparation/scoring and final drain. Nested
process timers overlap and are not summed. Source/channel simulation, PCM
construction and optional AES noise generation are outside RX. Hardware ADC,
ALSA capture and the native shared-clock ASRC are not exercised by this unpaced
software replay. Its elapsed time is processing latency, not a live backlog
measurement. Bulk arithmetic is FP32, with existing higher-precision preparation
and reductions; there is no GPU or arithmetic-policy change.

## Fresh measurements

| Independent physical-link realization | RX CPU s | RX elapsed s | Cached-template workspace CPU s | Workspace elapsed s | Conditional result |
|---|---:|---:|---:|---:|---|
| seed 202611064001, bit 0, arrival +2.7 s | 73.401 | 52.116 | 7.852 | 7.781 | Correct candidate; gate passed |
| seed 202611064002, bit 1, arrival −6.9 s | 71.592 | 50.921 | 9.409 | 9.333 | Correct candidate; gate passed |

Both windows were fully scored and drained. These are two measurements, **not
a confidence interval, worst-case CPU ceiling, or measured reception probability**.
`physical_null_qualified` remains false. The conditional model's 10⁻⁴/day
accounting is not a physical false-admission guarantee.

The fixture records 132.750 seconds including UTC scheduling wait and capture
padding. Dividing by it gives 0.539–0.553 CPU s/input s, but that would overstate
practical throughput. More useful denominators are:

| Denominator | RX with preparation already done | RX plus fresh preparation/startup |
|---|---:|---:|
| 84.8 s one-bit transmission, including Helper lead | 0.844–0.866 CPU s/s | 1.177–1.178 CPU s/s |
| 46.4 s active DATA | 1.543–1.582 CPU s/s | 2.151–2.153 CPU s/s |

The explicit fixture's nonzero Helper-to-end support is 84.6675 seconds; using
that instead of the default stream calendar gives 0.846–0.867 prepared CPU s/s.
This small endpoint difference is unrelated to fixture scheduling padding.

### Preparation accounting

The paired native exporter runs cost 21.237 CPU / 21.259 elapsed seconds with
templates and 4.569 CPU / 4.574 elapsed seconds without templates. Their
difference estimates **16.668 CPU / 16.685 elapsed seconds** for raw keyed
templates. This is one paired timing difference, not an isolated API timer or
confidence-qualified forecast. The full exporter additionally constructs an
unused simulated observation, so its entire time is not intrinsic RX setup.

Add 7.852–9.409 CPU seconds for template-dependent FFT/Gram workspaces and about
2.0–2.2 CPU seconds for one-time library startup. Thus a fresh bit requires an
estimated **26.5–28.2 CPU seconds, 26.3–27.9 elapsed seconds of preparation**.
With cached raw templates, workspace setup alone takes about 7.8–9.3 elapsed
seconds. Using the existing full exporter unchanged instead of subtracting its
simulation work gives a conservative cold setup of about 31–33 CPU seconds.

Pattern derivation includes epoch and symbol ordinal. Template-dependent work
must therefore be charged again for new bits until cross-bit reuse is separately
implemented and measured. Shape-compatible allocations/plans may be reusable;
the keyed numerical contents are not automatically reusable.

## Transmit durations and the `fox` estimate

The current provisional planner chooses four 11.6-second tiles, **46.4 seconds
of active DATA per bit**, at the requested 8 dB-Hz target. This is selected both
for one source bit and for the 22-bit `fox` message under the requested 99%
per-message planning target. The planner's model, correct-probability and
physical-null qualification flags remain false. In particular, selected strong
selective modulation and moving-delay mismatch are not fully represented in
the duration surrogate. Do not interpret this duration as demonstrated 99%
reliability.

* **One bit:** 84.8 seconds using the native default stream calendar, including
  the single 38-second Helper lead and endpoint. The planner's conservative
  pulse-halo allowance is 85.067 seconds.
* **`fox`:** the maintained short-text dictionary yields 22 bits
  (`1110001001111101111000`); encryption does not change the count. Serialized
  DATA takes 1020.8 seconds. With one shared Helper lead and endpoint, the
  forecast is **1059.2 seconds, about 17 minutes 39 seconds** (1059.467 with
  the planner's conservative halos).

The isolated receiver has no complete arbitrary-text message adapter, and the
whole-bit fixture adds a new Helper lead and UTC-grid wait to each slot. Those
fixture waits are excluded from this FHSS-off/Burst-off serialization estimate.
No full `fox` transmission or reception was measured here. Physical-end
recognition also requires fully scored absent-symbol durations covering six
seconds; this is a separate completion latency, not an extra six seconds
silently included in the transmit estimate.

A transparent conservative repetition forecast is:

```
fox_CPU = 22 × (measured_RX_CPU + workspace_CPU + keyed_template_CPU)
          + startup_CPU
fox_air_seconds = 22 × 46.4 + 38 + 0.4 = 1059.2
```

This gives approximately **2151–2156 CPU seconds (35.9 CPU minutes)** including
preparation, or **2.03–2.04 CPU seconds per transmitted second**. Repeating the
single-bit prepared RX alone gives about 1.49–1.52 CPU s/s. These are engineering
estimates; repeated one-bit lead acquisition can overcharge a future shared-lead
receiver, while missing message-adapter work can add cost. No model-validity
confidence interval is asserted.

The current scoring worker takes approximately **48–49 elapsed seconds**,
longer than the 46.4-second DATA cadence even before preparing subsequent keys.
Although the isolated one-bit Helper lead makes its average look comfortable,
**sustained contiguous-message real-time operation is not demonstrated** by
these results. Aggregate CPU seconds are summed across numerical threads and
must not be treated as elapsed seconds or as an all-core capacity certificate.

## Additional nominal 8 dB-Hz diagnostic

The same channel/I/Q realization as the bit-0 physical trial was converted to
audio and mixed with fresh temporary-key AES-256 full-DAC-band noise. Quiet
DATA-band metrology gave **8.0056 dB-Hz nominal DATA C/N₀**; this uses the fixed
unit DATA reference, not measured post-fading DATA energy. The changing
channel's realized energy can differ. This stress test changes the received
noise floor; it is not the unchanged 4 W / 180 dB / −164 dBm/Hz link.

The correct bit passed the unchanged conditional gate (log-e 117.545 versus
threshold 21.493). Helper again selected 15 of 365 coarse CFO points. Prepared
RX took **82.116 CPU / 59.015 elapsed seconds**, with **7.726 CPU / 7.696 elapsed
seconds** of workspace preparation. That is 0.968 prepared CPU s/s over the
84.8-second transmission, or 1.770 per active-DATA second. Including estimated
keyed-template preparation and startup gives approximately 1.277 and 2.334
CPU s/s, respectively. The scoring worker took 55.6 elapsed seconds.

There was no clipping. AES impairment generation took 0.864 CPU seconds,
excluded from RX. One noise realization cannot establish a sensitivity curve,
near-target reliability, false-admission rate, or a reliable causal 12–15%
timing penalty: host variation and data-dependent search both remain possible.
No new live cable measurement was performed. Exact AES-noise replay requires
the locally retained PCM, because its temporary key is intentionally not saved.

## Reproduction and evidence

The [compact archive](evidence.tar.gz) accompanying this report retains raw measurement reports,
native generation argv/times, public/received manifests, input hashes, source
hashes, plans and the measurement wrapper. Large I/Q, raw templates and PCM stay
in ignored `local-evidence/triad-hf-current-cost/`. Use the archived source
revision and build via `./build.sh`; the measured native exporter SHA-256 is
`76e059e8a17452a5e7955bd01544a8f030bf9b85b55b7437df622fd2142210f4`.

For a fresh reproduction, extract only `generate.py`, `generate_scheduled.py`
and `measure.py` from the archive into an empty directory beneath
`local-evidence/`, then run from the repository root (replace `RUN`):

```bash
python3 local-evidence/RUN/generate.py
python3 local-evidence/RUN/generate_scheduled.py
OMP_NUM_THREADS=2 OPENBLAS_NUM_THREADS=2 MKL_NUM_THREADS=2 \
  /tmp/triad-weak-env/bin/python local-evidence/RUN/measure.py \
  --public local-evidence/RUN/public \
  --capture local-evidence/RUN/signal0-scheduled \
  --output local-evidence/RUN/new-physical0 --threads 2
```

Use a Python environment with the versions in `report.json`; the `/tmp` path is
local, not a portable dependency installer. Archive receipts are immutable;
generation scripts intentionally refuse to overwrite their existing receipts.
The original `signal0`/`signal1` exports changed UTC epoch and therefore changed
fixture geometry; they were not scored. The scheduled replacements retain the
declared full epoch search, independent seeds and arrivals, but are not a blind
epoch-acquisition validation. There was no repeat-until-pass policy.

This focused measurement does not replace the efficient performance-regression
suite or authorize repeating historical qualification campaigns. It preserves
the previous studies and their limitations.
