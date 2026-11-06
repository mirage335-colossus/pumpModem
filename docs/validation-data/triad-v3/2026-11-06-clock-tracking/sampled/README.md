# Native sampled qualification of200ppm full-audio clock tracking

This is a bounded qualification of calibrated-start clock tracking. All
operational admissions are withheld: physical ASRC covariance and the Helper
null remain unqualified. These small signal experiments do not establish a
reception probability or false-admission rate. Ordinary Robust integration and
its regressions are outside scope. `summary.json` indexes every completed
outcome with source identities; each case retains complete results and a log.

## Frozen experiment and evidence boundaries

`protocol.json` was frozen before the first measurement. It uses encrypted
native Rate36×Added10,1725Hz DATA IF,6000Hz full real audio,225Hz nominal DATA
bandwidth, requested60s DATA (resolved108s source), nominal8dB-Hz DATA C/N0,
and one additional observed second. Each receiver starts from the public
calibrated arrival/CFO/rate-zero bank. Both keyed bit alternatives are scored;
all transmitted fixtures use bit0. The three-carrier waveform and full Helper
composition are retained, with no TX-private timing map supplied to reception.
Helper-on cases test compatibility/interference through the declared DATA
projection; this receiver does not infer phase from Helper pilots.

The actual native Resampler is driven by independently injected TX/RX UTC
updates: ±100ms and ±2s. It steers the complete PCM waveform, not just DATA
coefficients. Real white thermal noise is added between TX and RX conversion,
with identical underlying draws across paired cases. This is synthetic thermal
noise, not the separately available cryptographic background-noise feature.
Only the exact local RX source-coordinate map is exposed to the receiver.
Flat controls explicitly select None/Off propagation and disable oscillator
errors; they do not qualify blind acquisition or the full RF uncertainty bank.

The public timing HMM uses4s segments,0.5s Gaussian blocks, fixed coherence
alternatives0.5/2/infinity seconds,1/900s delay spacing,50ppm rate spacing,
20ppm/s maximum acceleration,64 retained states and256 proposals. The initial
zero-state assumption is calibrated start, not an inferred synchronization
oracle. Physical/Helper/ASRC null qualification remains absent even when a
conditional log-threshold22 diagnostic passes.

## Local-map isolation and original computational failure

`rxonly-first/results.json` preserves the pre-cache RX-only200ppm comparison.
`initial-provenance.json` binds its private probe and protocol. Four RX updates
produced at most6.605ms cumulative waveform-map displacement in this short
observation, not an abrupt2s waveform jump.

| Receiver | Correct candidate | Diagnostic log-e | CPU seconds including native basis |
|---|---:|---:|---:|
| Fixed nominal downmix/template |0|96.965|4.479|
| Fixed remote timing, exact local map |0|437.995|4.298|
| Remote timing HMM, exact local map |0|358.415|206.062|

The original HMM spent about198.6CPU seconds repeatedly generating native
candidate templates. Its worst4s segment took12.641s wall. Expansion stopped
at this serious performance problem; the complete first fixture used216.555CPU
seconds. The old evidence was not overwritten.

The bounded uniform-template cache was independently checked against direct
native synthesis at six mapped windows, nine delay/rate states, and both bit
alternatives. Predeclared limits were1e-5 relative L2 and1e-4 normalized maximum
complex error. Measured maxima were4.4011e-6 and1.0996e-5. The frozen final cache
was checked again with the same protocol. See `cache-protocol.json`,
`cache-check-results.json` and `cache-frozen-check-results.json`.

The paired complete cached rerun changed HMM log-e by only3.5992e-5 and reduced
receiver cost to23.840CPU/22.163wall seconds:8.64×CPU and9.22×wall acceleration.
Those are single-run paired engineering measurements, not timing confidence
intervals. `rxonly-cached/results.json` includes cache work and storage counts.

## Independent remote motion, Helper and missing observations

Six design configurations used seeds94001/94002, Helper off/on, and50/200ppm
phase caps. Two independent seed95017 no-erasure controls and its two
predeclared valid-mask outage controls followed. Every completed flat tracking
configuration selected bit0 and passed the conditional diagnostic gate. The
same seed and observation are reused across paired conditions, so these are
not ten independent reception trials.

The moving200ppm Helper-off seed94001 causal comparison is:

| Receiver | Diagnostic log-e |
|---|---:|
| Fixed nominal |64.307|
| Fixed timing with exact local map |87.362|
| Remote timing HMM with exact local map |364.480|

`moving-causal-pair-identity.json` verifies matching source waveform, RX map,
TX/RX reports and noise-seed construction. Thus the remote inference benefit
is distinct from local dewarping. Independent seed95017 gives tracking
340.887/340.877 versus fixed56.601/56.593 for Helper off/on.

The first held-out outage attempt failed with the maintained fixed receiver's
explicit partial-DFT-erasure guard; preserve
`moving-off-95017-200-erasure.log`. Its failed-case source hash was not flushed
before the first receiver result; the sequential batch protocol, command,
preceding source-bound case and traceback remain. Do not claim a separately
recorded failed-case hash. After the tracking owner added conservative whole
DFT-block exclusion, tracking-only reruns succeeded at328.798/328.783 log-e.
The physical67–69s post-conversion observation mask requested11998 invalid
rows; the receiver discarded15000, explicitly reporting3002 additional rows
(0.5003s) and two expanded partial blocks. Values were never substituted as
observed zeros. This is missing processed observations, not an ADC discontinuity
or a claim that unknown filter history across raw device loss was recovered.

## Longer repeated-clock observation and sustained computation

`extension-protocol.json` froze the long waveform stress and
`long-runtime-forecast.json` froze the forecast before execution. Seed96023,
Helper off, requested960s DATA and8dB-Hz were retained. ±100ms/±2s schedules
repeat every180s, with additional TX+50ms/120s and RX−40ms/150s updates.

The completed run scored6,013,200 actual6000Hz samples over1002.2s, with32 TX
and29 RX updates. It selected bit0 with diagnostic log-e4659.826. Receiver
processing, including native template-cache filling and downmix, took277.053CPU
seconds and263.572wall seconds (0.2764CPU seconds per received second). The
worst4s segment took1.573s wall. Fixture generation/noise/TX/RX-ASRC preparation
cost18.598CPU seconds; this is not operational receiver startup. Total fixture
CPU was295.699s, below the predeclared378.6s forecast and400s hard stop.

The cache reached its32MiB bound with1156 evictions and no direct fallback.
Peak timing proposals were105. The final displayed MAP remote source offset
was137.78ms, beyond the original few-ms bank. TX finite-length counts imply
about138.67ms near the source endpoint; that is an approximate independent
endpoint check, not a private per-segment truth map fed to the receiver.

This is a genuine1002.2s sampled observation, not days-long hardware operation.
It is a strong-signal timing/workspace test, not a sensitivity-optimal duration.
Retained initial-path probability fell to1.36e-11; the non-renormalized beam can
lose trajectories, especially through weak evidence or long outages. This run
does not qualify arbitrary multi-day timing or ultraweak coherent sensitivity.

## Selected stochastic-channel limitation

The shared-channel pair uses the full maintained stochastic model:21ms excess
delay,90Hz ray separation,30Hz random ray width, Each65 random dispersive
modulation,1µs random delay excursion with1000s knots, and explicit oscillator
effects off. See `channel-pair-protocol.json` and `channel-pair-results.json`.
The native channel is set to200dB-Hz so its thermal component is negligible;
after real IF reconstruction, explicit real8dB-Hz thermal noise is added before
RX ASRC. Receiver variance retains both images of the negligible native noise.
This avoids understating noise by3dB when reconstructing real PCM from a
full-Nyquist circular complex noise source. No TX warp is applied after channel
propagation. Selected effects, filter state and tails remain present.

| Receiver | Stable RX clock log-e | Moving RX clock log-e |
|---|---:|---:|
| Fixed timing, exact local map |−6.74595|−6.74495|
| Timing HMM, exact local map |−32.86134|−33.75579|

Both131.0703s arms select bit0 but miss the diagnostic threshold. Complete DATA
energy is89.2908 power-seconds, versus69.8174 for the matching flat source
(+1.068dB). Pre-ASRC whole-window energy within±160Hz is80.6091; it is not
exactly the receiver's short-block projection. No realization is renormalized.
The failure therefore is not explained by energy removal alone. The original
one-tap0.5/2/infinity coherence model is outside this fast channel regime; these
results do not establish that any receiver could decode this duration under
unknown30Hz channel evolution. The paired arms bound only this realization's
incremental clock effect. No model tuning or fundamental-limit claim was made.

## Reproduction and minimal future checks

Build the current isolated native exporter using the repository build workflow.
Compile the private actual-Resampler probe:

```sh
c++ -std=c++20 -O2 -Iinclude \
  local-evidence/triad-tracking-200/sampled/warp.cpp src/resampler.cpp \
  -o local-evidence/triad-tracking-200/sampled/warp
```

A cheap complete moving-clock diagnostic is:

```sh
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=2 /tmp/triad-weak-env/bin/python \
  local-evidence/triad-tracking-200/sampled/run.py \
  --tag NEW_OUTPUT --helper off --seed 94001 --cap 200 \
  --tx-events --rx-events --methods fixed_mapped,tracking \
  --template-backend auto
```

Use separate arguments `--seed 94001 --cap 200`. A fresh output name is
mandatory. To reproduce the original costly mapped implementation, select
`--template-backend direct`; do not run that full comparison routinely. The
final private CLI selection preserves the auto default and passed parser and
compile checks; `run.qualified.py` retains the preceding measured driver.
The original `warp.v1.cpp` TX-map scalar is invalid because no TX map was
written; current output correctly reports null. RX maps remain valid.

Prefer focused native/cache/receiver unit checks plus one source-matched short
fixture. Reuse these source-bound full/sustained results until relevant timing,
interpolation, work regime or arithmetic changes invalidate them. The longer
qualification command is:

```sh
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=2 /tmp/triad-weak-env/bin/python \
  local-evidence/triad-tracking-200/sampled/run.py \
  --tag NEW_LONG --helper off --seed 96023 --cap 200 \
  --seconds 960 --tx-events --rx-events --long-events \
  --methods tracking --cpu-limit 400 --template-backend auto
```

Again use separate numeric arguments. The full completed experiment collection
used908.8CPU seconds, excluding small template probes and the retained failed
attempt. That historical cost is not a routine regression requirement. All
private outputs are within this directory; no production source or audio device
was modified by this qualification worker.

An exact RX-only direct-reference replay (the original expensive arm) is:

```sh
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=2 /tmp/triad-weak-env/bin/python \
  local-evidence/triad-tracking-200/sampled/run.py \
  --tag NEW_DIRECT_RXONLY --helper off --seed 94001 --cap 200 \
  --rx-events --methods fixed_nominal,fixed_mapped,tracking \
  --template-backend direct --cpu-limit 280
```

To repeat the selected channel pair, use separate new output names with
`--channel --helper off --seed 94001 --cap 200 --methods fixed_mapped,tracking
--cpu-limit 90`, once without and once with `--rx-events`. Do not add
`--tx-events` to this channel path: the driver rejects TX warping after channel
propagation. These commands reproduce a documented diagnostic miss, not a
passing qualification requirement.

The template comparison script reads the retained local RX map under
`rxonly-first/`; a compact evidence export may omit that5MiB binary. Regenerate
the RX-only fixture before comparing templates if the map is absent, preserving
its new identity rather than silently replacing historical evidence. Private
maps, all JSON results, logs and protocol snapshots remain under this directory;
no raw full-waveform PCM file is necessary to replay the deterministic source.
