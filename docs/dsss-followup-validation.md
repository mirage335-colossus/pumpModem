# DSSS receiver follow-up: local manual-test evidence

This 9 October 2026 local candidate implements Live/Duplex, receive-queue reset,
clock-control and estimate corrections, a conditional coherent DSSS presence
check, and long-symbol timing-neighbor completion checks. It is ready for the
requested manual-test checkpoint, **not fully qualified**. No push, publication
or hosted CI was performed. Broader qualification waits for the user's request.

## Exact baseline and build

Both builds are local overlays on `ac63e3ab18850daf4f093c9dfeb1bcb6541abf27`,
branch `codex/pulse-moments`. The preceding optimized receiver is the frozen
clock/spread candidate, not the original raw implementation. Its runnable GUI
remains `.agent-work/artifacts/receiver-opt-20261008/clock-spread-manual-gui`,
SHA256 `24ec8757fc613c62b821db08660af9effa67bb0965a4487ef7e95b03397d89f4`.
Baseline library SHA256 is
`13cc4ae7428e1a5afa6813445b79b5cf1c8f130cdee63e8fc0903851bee54995`;
its manifest is `clock-spread-manual-manifest.json` in that directory.
The old diagnostic header was frozen and matched when linking the old library;
this avoids a diagnostic-structure ABI mismatch.

Final library SHA256 is
`2bc0dbc37e94f728a512690c4b15c5ae06e6dfcacd40816b1d7ab6f9a92fd68a`.
Final GUI SHA256 is
`7a88d9496158fcb626abf8973bd40e6c603a992bd81d027de11a8f5efc2115e7`.
Build: GCC 14.2, Release/O3, FP contraction disabled, FLTK; native graphical tests,
sanitisers and web-worker build are OFF in this prepared build. The source overlay,
CMake cache, executables, test evidence and baseline hashes are retained in
`dsss-followup-manual-manifest.json`. The build directory is
`build/agents/receiver-opt-20261008/native`; `./build.sh` was used to build it.
Raw diagnostic modes and earlier raw/optimized baselines remain available.

## Implemented changes and bounds

- Independent hardware capture remains active during transmission when
  Live/Duplex is enabled. There is no second application-level PCM queue. An
  additional OS thread stack is outside the configured DSP workspace.
- Clear received cancels the old acquisition generation and discards its queued
  PCM, pending rows and health/backlog state. It preserves transmission and the
  transmitter's private-key-use lock.
- Clock accuracy displays editable durations. Composite CLI/parameter-list clock
  syntax still imports into separate accuracy/region/offset controls. Duration
  estimates distinguish sampled simulation, ordinary hardware fallback and
  qualified timing. Short FFT acquisition is still a full scan; tiny requested
  clock errors do not silently remove that work or the peer-steering bank.
- An unsupported audio timing allowance falls back visibly before generating
  private PCM. TX does not wait up to 120 seconds merely because a native 1 ms
  allowance cannot be supported. Once private output has started, continuity
  failures stop it; the private source is never rewound/reseeded/replayed.
  Capture downgrade preserves resampler/filter state and stops using UTC metadata.
- A separately labeled **RX reference** is available in supported UTC/DSSS
  geometries. It is an AWGN engineering reference, without a qualifying confidence
  interval or a claim of complete acquisition probability. Unsupported geometry,
  coverage or workspace can still produce unavailable.
- Selecting DSSS 10/100/1000 interactively sets inner Rate 360/36/3.6 Hz and stream
  carrier 1500 Hz (absolute Carrier=Shift+1500). Explicit imported values remain
  intact. Invalid-bandwidth hints show intended shaped edges and a carrier hint.
- Fake FHSS still changes only the displayed frequencies/conservative oscillator
  planning. Its physical signal is unchanged, so no observer-time gain is added.
  Secret genuine hopping can disadvantage bandwidth-limited observers; there is
  no universal channel-count multiplier against a wideband/channelized observer.
  See [observer-model limits](lpi-estimates.md#frequency-hopping).

The coherent outer-code guard responds to **residual lack-of-fit**, after
subtracting coherent explained energy and rank from the chip subspace. Each finite
pulse's complete observed I/Q dot is accumulated before squaring; partial tails,
covariance and sample-quantized template energy are retained. The original score
and admission threshold remain unchanged. The guard can only reject an otherwise
accepted candidate. Full-rank nesting, adequate residual degrees of freedom,
nonzero contributing pulse terms and available bounded state are prerequisites.
FFT covariance conditioning enters the weak-energy allowance.

Independent private 0/1 patterns remain at every bit position. Reuse is confined
to the same key/epoch/canonical ordinal/timing/clock/frequency/pulse interval;
outer-code evidence is not reused across secret patterns. Existing public pulse
statistics remain shareable on identical observation geometry. There is **no new
mixer, lowpass filter or reduced-rate audio frontend** in this follow-up.

Enhanced phase/differential branches, insufficient optional guard workspace and
unsupported rank/nesting/term geometry retain the preceding detector. They do
not gain this wrong-outer-code protection. Short reception remains statistical
detection, not authentication. This is a conditional correction, not universal
wrong-key rejection or all-DSSS qualification.

Long compact reception subscribes sparse records for the accepted timing lane
and its immediate neighbors. A guide must share frequency/rate/key/epoch and the
exact canonical accepted prefix; earlier missing history invalidates the relation.
Records retain fully observed start/end/sample count and two tagged decisions.
Any stronger compatible neighbor accepting the terminal slot vetoes completion;
a stronger absence guide must finish its whole symbol. Accepted-bit ownership
stays fixed. Records persist until acquisition state is discarded, within the
workspace bound; preflight includes growth/transient allocation. Exhaustion is
explicit, not an early-completion fallback. This fixes the retained discrete
neighbor fixture. It does not establish the exact true boundary for arbitrary
off-grid starts. EOF/cancellation never manufacture observed absence.

## Paired execution measurements

Each primary row below is three serial AB/BA paired runs, pinned to CPU 2, one
worker, with no concurrent agent build/test workload. Background OS activity was
not controlled. Each pair uses identical original float PCM, sample rate, keys,
seeds, impairments and bank. PCM hashes and full arguments are in
[raw and summarized data](validation-data/robust/dsss-followup-20261009/README.md).
Ranges are observed paired ranges, **not confidence intervals**. Ratios are
medians of paired before/after CPU ratios, not ratios of the two displayed medians.

Short/FFT runs include constructor, all pushes, progress polling and finish;
destruction is separately checked below. Long runs also include finish/destruction.
PCM generation and diagnostic evidence copying are excluded. No row is a planner
prediction. Speedup below 1 means a regression.

| Case | Before CPU s | Current CPU s | Current wall s | Paired speedup [range] | Current media/s wall throughput | Peak accounted bytes change |
| --- | ---: | ---: | ---: | --- | ---: | ---: |
| Ordinary fast link | 0.288048 | 0.312564 | 0.312634 | 0.962 [0.917, 1.001] | 25.589 | +32,800 |
| DSSS 10 voice | 0.314446 | 0.390269 | 0.390333 | 0.879 [0.546, 0.880] | 30.743 | +32,800 |
| DSSS 100 voice | 0.872611 | 1.005496 | 1.005606 | 0.863 [0.855, 0.892] | 23.866 | +32,800 |
| DSSS 1000 voice | 13.232205 | 13.445419 | 13.446913 | 0.984 [0.958, 0.995] | 11.899 | +32,800 |
| DSSS 10 wider | 0.660792 | 0.799758 | 0.799889 | 0.826 [0.818, 0.843] | 12.502 | +32,800 |
| DSSS 10 weaker | 0.309340 | 0.354242 | 0.354334 | 0.860 [0.859, 0.889] | 33.866 | +32,800 |
| DSSS 10 fixed 48 ksample/s | 4.089130 | 4.619261 | 4.619826 | 0.888 [0.876, 0.960] | 2.598 | +32,800 |
| Primary 0.005 Hz | 0.106591 | 0.101247 | 0.101319 | 1.053 [1.044, 1.285] | 1973.973 | +2,704 |
| Primary 0.5 Hz | 0.105775 | 0.102436 | 0.102505 | 1.033 [1.006, 1.056] | 1951.126 | +2,704 |
| Primary 1500 Hz | 0.327910 | 0.327232 | 0.327309 | 1.016 [1.002, 1.054] | 611.043 | +2,704 |
| Strong 0.1 Hz / 0.05 Hz | 7.810612 | 8.261312 | 8.262241 | 0.945 [0.933, 0.955] | 19.365 | +27,680 |
| 30 MHz RF prefix | 21.424738 | 19.907505 | 19.909793 | 1.073 [1.061, 1.076] | 0.005 | -8,484,704 |
| 8.2 kHz prefix | 5.583229 | 5.661511 | 5.663097 | 1.003 [0.986, 1.035] | 0.006 | -4,722,864 |
| Primary 0.005, fixed Fs 6000 | 0.334744 | 0.322023 | 0.322123 | 1.040 [1.030, 1.067] | 620.880 | +2,704 |
| Primary 0.5, fixed Fs 6000 | 0.330537 | 0.326264 | 0.326348 | 1.015 [1.003, 1.025] | 612.843 | +2,704 |
| Complete affine, two keys / three epochs | 27.367669 | 26.309521 | 26.340171 | 1.030 [1.023, 1.041] | 10.527 | +3,269 |

The coherent correction costs about 14% CPU in DSSS 10, 16% in DSSS 100 and 21% in
the wider case, based on paired medians. The DSSS 10 outlier is retained. The
DSSS 1000 case uses an enhanced branch without the new coherent guard. Its successful
reception is not evidence of wrong-outer-code protection. The short runs use one
key/epoch and three frequency/clock pairs, not the whole GUI 13-epoch bank.

Every complete short case receives exactly `001`, with zero bit errors and one
physical completion in both receivers in every run. Accepted-bit progress/event
hashes are unchanged. Accounted workspace rises 32,800 B in these one-bank cases;
additional banks scale that metadata. Sparse long-lane layout reduces measured
RF/Sub9 peak workspace by about 8.48/4.72 MB. No configured workspace is increased.
Process peak RSS and absolute accounted storage are in the data, separately;
RSS includes the process and input fixture, not just DSP allocations.

One additional pair per short case includes destruction. These are separate
measurements, not extra samples silently merged into the three-pair series.
The largest observed cleanup CPU time is 0.003703 s; all payload/event/latency
checks match. The raw supplemental CSV omitted the last two header labels;
`performance-inclusive-normalized.json` names them from the driver's verified
emit order and preserves the original raw data. Total time already includes them.

| Supplemental complete-lifetime pair | Before CPU s | Current CPU s | Current wall s | CPU ratio |
| --- | ---: | ---: | ---: | ---: |
| Ordinary fast link | 0.317965 | 0.270506 | 0.270520 | 1.175 |
| DSSS 10 voice | 0.314485 | 0.366293 | 0.366325 | 0.859 |
| DSSS 100 voice | 0.883234 | 1.002305 | 1.002370 | 0.881 |
| DSSS 1000 voice | 13.425208 | 12.977100 | 12.980264 | 1.035 |
| DSSS 10 wider | 0.656434 | 0.773971 | 0.774107 | 0.848 |
| DSSS 10 weaker | 0.306283 | 0.349634 | 0.349757 | 0.876 |
| DSSS 10 fixed 48 ksample/s | 4.187278 | 4.609113 | 4.609714 | 0.908 |

### Geometry and latency

All seven short cases use 64 MiB workspace. Application-selected Fs is 14,400 for
the voice/ordinary/weaker cases and 48,000 for wider; the separate fixed48,000
voice comparison retains that original PCM within the pair. Fine-chip length
is 8 samples except fixed48,000 voice (27). Symbol durations are 0.008889 s ordinary,
0.355556 s DSSS 10,3.555556 s DSSS 100,35.555556 s DSSS 1000,0.106667 s wider and
0.355563 s fixed48,000. Each includes full symbols and the six-second observed
absence, not only an early prefix.

| Complete case | First accepted bit, media s | Max accepted-bit publication lag, samples / s | Current acquisition wall s |
| --- | ---: | --- | ---: |
| Ordinary fast link | 0.142222 | 1843 / 0.127986 | 0.005101 |
| DSSS 10 voice | 1.137778 | 11440 / 0.794444 | 0.025888 |
| DSSS 100 voice | 9.102222 | 55472 / 3.852222 | 0.260027 |
| DSSS 1000 voice | 36.693333 | 14512 / 1.007778 | 4.120209 |
| DSSS 10 wider | 0.341333 | 7084 / 0.147583 | 0.018532 |
| DSSS 10 weaker | 1.137778 | 11440 / 0.794444 | 0.022826 |
| DSSS 10 fixed 48 ksample/s | 1.365333 | 8110 / 0.168958 | 0.421866 |

There are zero **added** accepted-bit media-latency samples in these comparisons;
absolute existing FFT/block latency is shown above. This is not a zero-latency
claim. Wall progress bounds and maximum push duration remain in raw rows.
The complete affine fixture spans 277.282167 s, Fs 6000,384001-sample symbols,
two keys, three epochs and 36 hypotheses. Both receive001, no wrong-bank bits,
and one physical completion. First accepted bit remains at 64.170667 s and maximum
accepted-bit publication lag is 1029 samples (0.1715 s), with zero added samples.
The legacy acquisition diagnostic waits to 128 s; it must not be confused with
the first accepted bit. EOF adds no accepted bits or completion.

Long cases preserve the earlier resolved workspace snapshot1,999,661,056 B.
Primary captures are200 s of 6400 s symbols: Fs 64/chip 12800 at 0.005/0.5 Hz;
Fs 6000/chip 1,200,000 at 1500 Hz;13 epochs/78 hypotheses. The fixedFs6000 low-carrier
rows separate algorithm effects from application sampling. The strong case is
160 s of 398107.171875 s symbols, Fs 64/chip 1280. RF is only601 samples atFs6000,
0.100167 s of 16384 s symbols, chip 12000,29 epochs/530845 hypotheses. Sub9 is only
1024 samples atFs32800,0.0312195 s of 5011872.33628 s symbols, chip 6560000,
1613 epochs/325826 hypotheses. These prefixes have no acquisition, accepted bits
or completion. Their CPU must not be extrapolated as measured full-reception,
steady-state throughput or sensitivity.

The separately captured [resolved configuration table](validation-data/robust/dsss-followup-20261009/resolved-configurations.csv)
uses the current `tuning::resolve` geometry and current available-memory snapshot
4,594,946,048 B, yielding2,297,473,024 B at 50%. `model_*` columns are planner
references for one key/default 6 s fallback, not runtime allocated banks; the
later tight-clock command's waveform geometry is included, but this table does
not simulate qualified native timestamps or animated FakeFHSS. Requested targets
are retained without GUI nearest-fit adjustment. Hardware ADC/DAC rate is unknown
until device negotiation. The exact executable benchmark banks are in their raw
rows and differ from the one-bit model probe.

Mixing still operates at the logical input sample rate. The existing compact
frontend contracts into pulse moments/cells (nominal cell rate Fs/chip_samples);
it does not emit decimated PCM or assert a new internal sample floor. FFT runs
retain the existing bin/covariance projection. No new filter delay is introduced.
The old4096-samples/chip projection eligibility limit is already removed by the
preceding optimization; the primary chip lengths above exercise that path.

### Measured backend and memory selection

| Case family | Backend / branches | Current peak DSP bytes | Current peak process RSS bytes |
| --- | --- | ---: | ---: |
| Primary 1500 | Pulse moments, one coherent section | 5,037,992 | 18,378,752 |
| Strong low-carrier | Pulse segments, four sections and 20,480-sample differential window | 6,062,448 | 17,350,656 |
| RF prefix | Pulse segments, four sections | 410,542,736 | 337,588,224 |
| Sub9 prefix | Pulse segments, four sections and 104,960,000-sample differential window | 1,013,561,608 | 928,464,896 |
| Complete affine | Pulse segments, one coherent section | 403,814 | 19,755,008 |

Accounted retained capacity can exceed committed process RSS; they measure
separate quantities. Short cases select FFT, with the enhanced branch in DSSS1000.
These diagnostics describe the measured bank, not a claim that the whole GUI
profile runs in real time.

### Component costs and remaining profiling limits

These separate instrumented runs identify where time goes. Instrumentation is
expensive, particularly for the 200 s primary case, so these times are not the
speedup measurements above. Frontend covers shared input moments/mixing; pulse
statistics/covariance and kernel contractions have separate counters. Search
includes private construction/contraction and decision work; internal counters
can nest and must not be summed as independent totals.

| Case | Input validation CPU before / after s | Frontend CPU before / after s | Pulse-statistics CPU before / after s | Search CPU before / after s | Poll CPU before / after s |
| --- | --- | --- | --- | --- | --- |
| Primary 1500 Hz | 0.005445 / 0.005585 | 1.378781 / 1.375601 | 0.542051 / 0.532714 | 0.354466 / 0.362806 | 0.006386 / 0.006786 |
| 30 MHz RF prefix | 0.000027 / 0.000027 | 0.305771 / 0.303488 | 0.000000 / 0.000000 | 20.456254 / 19.068515 | 0.009611 / 0.008669 |
| 8.2 kHz prefix | 0.001233 / 0.001240 | 1.061736 / 1.062909 | 0.000000 / 0.000000 | 3.590949 / 3.529804 | 0.003025 / 0.002999 |
| Complete affine, two keys / three epochs | 0.003744 / 0.003859 | 0.360510 / 0.351781 | 0.000000 / 0.000000 | 27.388376 / 26.271369 | 0.004546 / 0.004882 |

RF/Sub9 and complete affine work remains search-dominated. Primary1500's repeated
per-sample frontend/statistics dominate its instrumented run. Input validation
scales with samples; covariance/moments with observation geometry; private
coefficients, contraction and guide work with bank size, pulse intervals and
completed symbols. Existing interval caches avoid repeated coefficient generation
within a valid interval. The new guard adds complete-pulse work rather than
removing hypotheses. Short FFT measurements distinguish construction/push/poll,
not mixing versus private-template time; allocation and other overhead are the
unseparated remainder. No GPU execution was measured. Planner/GPU figures remain
engineering models, not newly calibrated platform timings.

## Conditional sensitivity and statistical limits

The four fixed coherent cases use factor 10, known timing/frequency/rate, both bit
values, nominal and adverse waveforms. Fs 64, carrier 16 Hz, inner rate3.2 Hz,
fine chip 4, 20 s symbols; the adverse 1293-sample case includes a partial tail,
100 ppm clock,0.05 Hz frequency offset, a fixed0.1 degree/sqrt(s) phase trace,
and a17 Hz sinusoid. Shaping/limiting is generated by the actual waveform path.
One exact hypothesis, one phase branch,8 MiB workspace and one worker are used;
drift/differential detectors are off. The common false-alarm policy is 1e-10 and
actual branch threshold 24.4121452911. Original PCM/noise/key/seeds/impairments are
paired; the new guard is the only decision change.

Each case has 80,000 fresh independent holdout draws (320,000 total); earlier
exploration is excluded. The plan, two grids and analysis were frozen before
holdout data. Estimated additional required C/N0 is **0.000 dB** at both90% and 99%
correct-bit detection. Conservative grid-cell/binomial bounds give **at least95%
simultaneous confidence that additional loss is at most0.08 dB**, across these
four conditional cases and both points. Seven of the eight points also meet the
0.05 dB grid bound. The analysis includes unbracketed/nonmonotone exception
allowances; zero observed disagreements is not its confidence argument.

| Fixed conditional case | Baseline90% C/N0, dB Hz | Baseline99% C/N0, dB Hz | Estimated additional dB |
| --- | ---: | ---: | ---: |
| Nominal bit0 | 1.791198 | 2.838898 | 0.000 |
| Nominal bit1 | 2.790857 | 3.823170 | 0.000 |
| Adverse bit0 | 1.819614 | 2.865021 | 0.000 |
| Adverse bit1 | 2.946251 | 3.955433 | 0.000 |

Selected actual raw/automatic float-PCM replays agree with these reconstructed
thresholds; the maximum observed initial root difference is 6.92e-8 dB. Final
library replay at the four90%/99% locations exactly preserves all 32 previously
measured production crossings;64 score probes differ by at most3.624e-13.
These selected checks are not a universal floating-point enclosure.
The [holdout plan, analysis and final replay](validation-data/robust/dsss-followup-20261009/README.md)
retain the exact scope and uncertainty construction.

Under the declared fixed nested real-AWGN geometry and weak mismatch-energy
premise, activation is bounded by 2*exp(-30)=1.871525e-13 per fixed bit. This is
an analytical activation bound, not a measured rare false-accept rate. A selected
bank requires its own union/selection treatment. A strong wrong-outer positive
control changes one wrong admitted bit to none; it is not a rare-event rate.
The reject-only check cannot add accepted bits in the compared fixed coherent
branch; this alone does not calibrate bank selection or every detector branch.

No full-bank BER/detection curve, factor 100/1000 guard sensitivity, unconditional
phase/start distribution, broad interference law or hardware sensitivity is
qualified here. Cumulative loss below 0.1 dB relative to the original raw receiver
is **not yet established for the full acquisition system**. Earlier conditional
pulse-moment results cannot simply be added across different scopes.

## Spectral evidence

No waveform was changed by this receiver follow-up. Fine outer chips are already
formed before RRC shaping (rolloff 0.25, support ±8 fine chips) and radial limiting;
there is no abrupt post-shaping DSSS multiplier. A baseline 12 ksample/s,
carrier 1500 Hz, three-bit finite post-limiter probe covers factors1/10/100/1000 and
a partial final chip. Quantization gives7 samples/fine chip and 2142.86 Hz ideal
RRC support. Energy outside that support is 0.0443–0.1517%; outside300–2700 Hz is
0.0327–0.1103%; outside100–2900 Hz is 0.0222–0.0615%. The regression measures actual
PCM, including finite tails and limiting. Exact CSV/probe provenance is preserved;
these are baseline-waveform measurements, not new receiver sensitivity results.

These energy fractions do not establish hardware-filtering C/N0 loss, flat
passband response, a legal emission mask or regulatory certification. In the
14.4 ksample/s voice presets fine chips are8 samples and ideal edges375–2625 Hz.
See [spread controls](spread-spectrum-controls.md) for IC-7100 filter caveats.

## Validation completed and still required

The final affected GUI group passes 37/37 in169.09 s. Complete PatternReceiver
passes 77.38 s, simulation estimates26.07 s, Live profiles151.14 s, and complete
PatternCorrelator114.84 s. After a test-only strengthening of the tiny-workspace
fixture, its full `parallel_long_tiles` case passes again. It first retains the
original512-byte worker-fallback condition before completion, then reserves the
now-required timing-proof state; no boundary/progress assertion is removed.

The additional affected selection passes pulse-batch, pattern-code/spectrum,
pattern-search, exact start-geometry, tuning, ALSA, Windows/host audio contracts,
resampler, clock-sync, audio-rates and Live resource tests. Focused Live coverage
includes duplex with/without receive keys, Clear received, timing fallback,
backlog and health. The GUI and CLI are rebuilt. Earlier interrupted runs and
initial failures are not counted as passes; matching final evidence is reused.

The strict discrete-neighbor endpoint fixture and coherent wrong-outer fixture
now pass. An initial new timing fixture at very strong factor 1/sigma0.03 exposed
an older structured-null wrong-bit admission; it is still an open limitation.
The new timing-only fixture uses declared sigma1.5 with the same key/seed and
boundary assertions to isolate timing behavior. Existing assertions were not
weakened. General off-grid nominal-boundary correctness remains open.

Outstanding before overall qualification: physical audio/IC-7100 duplex and timing
measurements; native graphical smoke and provider integration; full general and
contract regression, calibration with every seed/gate, sanitizers, platform/SDK
and packaging checks; full-bank paired detection/BER curves and cumulative
raw-reference sensitivity; unsupported/enhanced guard paths, strong factor 1
structured-null behavior and arbitrary off-grid physical-end geometry. Long
reproduction full-symbol/full-message CPU and sensitivity remain unmeasured.
The user explicitly requested this bounded manual checkpoint before that stage.


## Amplitude envelope and limiter investigation

The later 9 October investigation used source `1e016a0dffe033c36fc71afad5241d16dc8957e5`
and frozen library SHA256 `66b59da08a5a8444a54bada79a79358bb3b345bb30895702ca6eb1580685ab97`.
It changes no transmitter or receiver waveform. These are deterministic digital
spectral measurements, not sensitivity curves, acoustic tests or emission-mask
qualification.

All three cases use 40 ksample/s, carrier 7500 Hz, nominal outer bandwidth
10 kHz, eight samples per fine chip, 64 inner chips per bit, and 38.4 seconds
of payload. Factor 1/10/1000 uses inner Rate 10000/1000/10 Hz and
3000/300/3 bits respectively, repeating `001`. Synthetic spreading seed byte
`i` is `3*i+7`, DSSS seed byte `i` is `13*i+29`, epoch 1789312671, phase zero.
Actual `PatternTransmitter` includes live surrounding noise. An instrumented
source copy exposes its pre-limiter complex samples and reproduces every actual
limited output float byte-for-byte, including training, padding and suppression.
Comparisons isolate the limiter on identical samples within each case; comparisons
across factors are not sensitivity evidence.

The ideal RRC support is 4375–10625 Hz. The guarded OOB metric integrates real
spectral power below 4175 Hz or above 10825 Hz, using 40 ms Hann windows with
10 ms hops. It excludes a 200 Hz transition allowance at each edge. Overlapping
windows do not establish independent trials or a confidence interval.

| Factor | Limiter occupancy | Complex energy removed | Linear guarded OOB | Limited guarded OOB |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 3.635% | 1.858% | −48.20 dBc | −33.72 dBc |
| 10 | 5.112% | 4.664% | −48.21 dBc | −29.38 dBc |
| 1000 | 3.826% | 3.976% | −48.19 dBc | −30.14 dBc |

For factor 1000, an inner coefficient persists for 0.2 seconds. In the eight
coarse intervals with coefficient power above 3, limiter occupancy is 51.71%.
Interior windows, excluding eight fine chips at either boundary, have guarded
OOB −48.06 dBc before limiting and −22.95 dBc afterward: **25.1 dB regrowth**.
The 38 intervals below coefficient power 0.25 show no limiting and unchanged
−48.23 dBc. The longer held amplitude therefore produces repeated clipping inside
an interval, not just a transition at its boundary.

Actual 40→48 ksample/s fixed resampling, or adjustable resampling with fractional
start 0.37 and constant +100 ppm, changes aggregate OOB by less than 0.06 dB.
Unity-gain int16 clipping/quantization has similarly small impact in these
captures. This excludes varying feedback correction, device underruns, analog
clipping and radio processing; audible clicks remain unmeasured.

Diagnostic pre-limiter backoff of 3.0103/6.0206 dB yields −45.15/−48.19 dBc
for factor 1000, but reduces digital transmit power by 2.838/5.844 dB relative
to the current limited waveform. That is not a negligible-power fix. Post-limiter
volume reduction cannot undo distortion already introduced. The normalized
squared correlation of the saved linear and limited real payloads is 0.9963493
(0.01588 dB ideal coherent mismatch); this is **not** measured additional required
C/N0, and excludes covariance, acquisition and false-accept behavior.

No waveform change is shipped from this investigation. Independent outer random
gain retains conditional mean power proportional to the inner coefficient and
can increase the fourth moment and clipping. Radius-quantile or byte whitening
before the Gaussian mapper could change the held envelope, but requires a new
versioned waveform/domain, matched templates and sensitivity/false-accept tests.
A naive implementation also moves Gaussian mapping from once per inner chip to
once per fine chip and increases outer-stream bytes substantially. Neither its
low CPU cost nor loss below 0.1 dB has been established.

Local reproducible probes, CSVs, PCM and stage analyses are retained in
`.agent-work/artifacts/receiver-envelope-20261009/REPORT.md` and its manifest
SHA256 `19dadd8c1710b82da1d7af19f5954def44de5eb0bdd3cb2a2d1fe53bbce5bfc8`.
The report records exact compiler commands and hashes. This measured limiter
limitation remains open at the manual-testing checkpoint.
