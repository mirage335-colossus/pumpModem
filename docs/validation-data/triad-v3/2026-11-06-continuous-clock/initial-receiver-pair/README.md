# Full-audio clock correction: paired maintained-receiver diagnostic

This is a deliberately small limitation-finding experiment, not sensitivity or
physical false-admission qualification. Run from the repository root:

```sh
g++ -std=c++20 -O2 -Iinclude local-evidence/continuous-audio-clock/receiver-pair/warp.cpp src/resampler.cpp -o local-evidence/continuous-audio-clock/receiver-pair/warp
/tmp/triad-weak-env/bin/python local-evidence/continuous-audio-clock/receiver-pair/run.py
```

`protocol.json` was written before generating observations. `results.json`
retains source/binary identities, complete source descriptors, clock-controller
receipts, receiver outputs and processing times. The exact source code is
`run.py` plus `warp.cpp`; no production source was changed here.

## Scope

Native independent-APSK-v2 encrypted DATA, Rate36 / Added10,1725Hz at6kHz,
60s requested allocation (native source resolves to108s including public lead
and finite waveform). Source energy is fixed by its existing bounded-tile gain.
The entire real audio waveform, including Helper when enabled, passes through
two actual existing adjustable `Resampler` instances. `ContinuousClock` controls
TX and RX with its default1000ppm and10ppm/s limits using ideal device-frame
observations. Propagation is explicitly None/Off; no oscillator error, ADC
quantization, clipping or extra dispersion is modeled.

TX clock updates:45s +100ms,55s +2s,75s−2s,85s−100ms.
Independent RX updates:48s−100ms,60s−2s,80s+2s,95s+100ms.
Both controllers apply peaks near299ppm; their opposing changes combine.
The corrected110s transport contains659893 samples versus660000 for control,
a107-sample(17.83ms) net advance. This is a nonaffine waveform deformation,
not an abrupt sample deletion. Two seconds of explicit source-end zeros retain
filter tails; the receiver uses the source plus one second(109s), never fabricated
observed samples.

White real PCM noise with prescribed8dB-Hz for unit average DATA power is added
between TX and RX. The same pre-RX noise coordinates are used for a pair.
The RX then uses the maintained `IncrementalReceiver`, a fixed nominal single
arrival/frequency/clock candidate, a fixed±160Hz DFT DATA projection, and the
predeclared0.1s/0.5s/infinite Gaussian coherence mixture. It does not receive
TX/RX correction traces. ASRC colors the noise; these likelihoods are diagnostic
and all operational admissions remain false. Helper is projected out, not used
for timing or frequency assistance. This is not qualification of a tracking
receiver or a proof of8dB-Hz performance in realistic fading.

## Results

| Helper | Key/noise seed | No correction log-e | Independent corrections log-e | Raw bit | Diagnostic threshold22 |
|---|---:|---:|---:|---:|---|
| Off |94001|435.581|36.565|0 both|Pass both|
| On |94001|435.604|36.492|0 both|Pass both|
| On |94002|444.453|6.664|0 both|Corrected miss|

For the first Helper-on case, true-bit model evidence for0.1s/0.5s/infinite
coherence is241.03/293.36/437.40 without corrections and24.87/38.28/4.24 with
corrections. The independent seed gives corrected−3.59/8.40/5.54. Thus short
coherence preserves some evidence but does **not** make the timing deformation
negligible. The source itself is unchanged, and the rate limits alone allow
approximately0.0087dB worst-case integrated-energy change through two ASRC maps;
this large score loss is receiver/template mismatch, not missing transmit energy.
The active-window energy and a numerical sensitivity penalty were not measured.

All six raw bit decisions are correct; one of three corrected diagnostics misses
the fixed gate. Helper-off and Helper-on use the same first seed and are correlated.
Only two independent seeds were used. No noise-only trials or false-admission
probability claim is made. This small result demonstrates a concrete failure of
an unchanged narrow fixed bank, not a statistical receiver boundary.

Campaign wall time11.88s. Each receiver took1.55–1.97CPU seconds for109s of
input(approximately0.014–0.018CPU s/input s), excluding generation and conversion.
Detailed boundaries and actual measurements are in the result JSON.

The first harness window attempted to consume the whole variable-length output
as a fixed frame count and failed its guard. Logs are retained. The final
protocol supplies a separate two-second source tail and consumes only one second
of it, preserving a complete common observed window without padding observations.
