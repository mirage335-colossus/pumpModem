# Final5 bounded receiver measurement report

Completed 2026-10-09. This is a manual-stage evidence handoff, not full sensitivity or release qualification. All measurements use owned artifact tools, exact frozen snapshots and synthetic key0x37/epoch1800000000. No production source was edited here. Final5 library SHA25683a2a828bf9b74d0db8635e83babf7742ba9663586dfe7b201286df07c382291; baseline9ee268e library c3022e9c11fbd6f2ef717ac3e0a2fdb1971931e58bd3ba0f981d5b777221d8c0. Final5 detector/waveform sources equal final4; model/tests/docs changes are separately identified in final5-runtime-identity.json. Source3 results are historical and are not the final5 primary table.

## Same-waveform final5 timing

The fixed D1000 bank uses nominal Config bandwidth10, carrier7500, Fs40000, chip8samples, symbol512000samples/12.8s, legacy envelopeV1, one key/epoch, all five oscillator frequency/clock pairs, workspace67108864bytes and identical saved60s/2.4Msample PCM. Nominal bandwidth is a configuration field, not a measured occupied-bandwidth claim for outer DSSS. Input C/N0 is80.020599913279625. CPU11 affinity; one uninstrumented variant/process; loading, generation and hashing outside receiver totals. Constructor, push, progress polls, finish/EOF and cleanup are inside totals. Every accepted bit is drained on the next poll. Three alternating pairs at endpoints give medians and observed ranges, not a calibrated95% performance interval. The slow first6s current run is retained.

| Nominal total UTC width | Baseline median CPU s | Current median CPU s | Paired speed median [range] | Current wall median s |
|---|---:|---:|---|---:|
|6s|13.001188|5.988077|2.171 [1.665,2.221]|5.988545|
|1ms|9.866389|1.828950|5.395 [5.380,5.630]|1.829048|

Candidate-only interior controls:1s2.986412CPU s,100ms2.365489s,10ms2.167563s. These are single runs, not paired estimates. Current6s/1ms median CPU ratio3.274. Actual mapped windows include sample/rate/numerical guards:6.000001311s and0.001000711s. These are genuinely qualified synthetic maps; no fallback/reduced clock coverage was accepted. Original oscillator hypotheses and the original full-hop false-alarm criterion remain present.

| Width | Scored-start executions | Direct jobs | Partition jobs | Full FFT jobs | Full input FFTs | Partition input transforms | Partition points | Components / component fallbacks | EOF replays |
|---|---:|---:|---:|---:|---:|---:|---:|---|---:|
|6s|2275570|0|35|40|2|132|24936448|1 / 3|1|
|1s|461730|0|50|0|0|717|31420928|6 / 1|0|
|100ms|100500|0|40|0|0|499|23742464|5 / 1|0|
|10ms|10500|0|40|0|0|5597|22769920|5 / 1|0|
|1ms|1500|25|15|0|0|10346|8507232|5 / 1|0|

Counters include repeated EOF scoring; they are executed statistics, not unique admitted origins. Final aggregate threshold_trials16,520,014 versus baseline16,520,028 retains a14-trial continuation-schedule difference; unchanged thresholds are not claimed universally. Source3's unfinished-hop prepayment difference2,260,160 was corrected by cold/unpublished EOF replay in final5. Published tails cannot be rewound; their full near-threshold equivalence remains unqualified.

At6s firstbit is exposed at26.2144media s; at1ms13.7728s. The physical-symbol-to-poll delay is13.2831s versus0.8415s. Completion occurs at1,048,576 versus1,046,528observed samples; known synthetic true-absence lag is19,324 versus17,276samples, both nonnegative. Faster scheduling intentionally changes the1ms event timing/hash;6s hashes match baseline. Prefix and completion assertions pass. These media delays do not certify hardware latency or make exact noisy origins identifiable.

Retained workspace is35,867,785bytes at6s and35,867,826bytes at1ms; representative RSS55.3–55.5MB. Exact ranges and constructor/push/poll/cleanup values are retained in final5-primary-summary.json and every result row.

## Arithmetic controls and component profile

Four separate controls retain the same selected cells, components, scheduler, original thresholds and complete setup. They change only chosen arithmetic versus forced original full FFT. At1ms forced FFT3.928852CPU s versus chosen1.875372s (2.095×); at6s7.235669 versus5.955689s (1.215×). Event hashes, accepted bits, completions and threshold_trials16,520,014 agree within each control. This supports the chosen overall mix; it does not measure a retained broad FFT job versus forced partition. That crossover remains unfinished. The chooser's numerical work estimate is not a universally measured crossover.

The private coarse-instrumentation binary is a copy of frozen receiver/batch sources with scopes only. It uses serial timings, records no overlapping timed threads and preserves output/counts. Instrumented totals are2.105159s at1ms and5.986719s at6s:12.25% and0.52% above their plain single controls. They are excluded from primary totals. Exclusive CPU accounting leaves0.017071s and0.016229s unattributed, respectively; inclusive parents must not be summed.

At1ms dominant exclusive costs: paired direct template+contraction fused0.905832s; partition template generation0.459729s; ready tracking0.307381s; partition template FFT0.107235s; partition scoring residual0.105422s; guard0.084894s; partition products0.072297s. At6s: streamed template generation2.066270s, partition template generation1.004508s, guard0.700293s, combined correlation inverse FFTs0.648508s (115calls:80full+35partition), full template FFT0.586198s, partition template FFT0.355344s, tracking0.307714s. The metric named partition_inverse_fft wraps both full and partition inverse calls. The residual push frontend/projection/validation/ring/scheduler category is0.019740s/0.019231s; observation copy/energy0.005837s/0.006127s; input FFT0/0.014132s; allocation/initialization0.009829s/0.010399s; progress publication0.000767s/0.000752s. Fused direct generation versus contraction and the mixed frontend category are not separately measured. Full Live component costs are unmeasured. No filtering/sample-rate change was introduced by this comparison.

## FinalV2 Live functional control

One actual public Live60s V2 one-bit+absence run passed exact prefix0, stable pending identity, one physical completion, no queue/health/quota/reduced-coverage failure, with unchanged2,216,646,656byte budget. CPU21.364913s, wall24.482916s including pacing/polling, firstbit13.9media s and completion26.3s, retained752,267,248bytes/RSS759,214,080bytes,73admissions/47retirements, peak33/current26receiver instances. Snapshot polling CPU0.115076s/wall0.124279s, max backlog0.1s. Provider uncertainty0.0001s and0.1ppm. One short archive identity read occurred concurrently, so treat these timings as functional diagnostics, not a primary isolated ratio. V2 differs from V1; no cross-waveform receiver speedup is calculated. Old baseline V1 Live failed its search quota before firstFFT at25.16s; that failure is retained, never converted into a CPU ratio. Fixed-bank success does not stand in for rolling Live. No hardware real-time qualification is claimed.

## Regression and original geometries

Prospective final5-regression-prospective-plan.json caps ordinary/boundary children120s, originals180s, stops launching after300s and never extends a failed cap. All26pairs/52runs plus10generations finished in235.632s, no failures/cap exhaustion/planned matrix omissions. Three alternating pairs per ordinary/original case; one paired control per co-signed boundary. Every slower run remains in final5-regression-summary.json. Small slower controls preclude a blanket no-regression claim; three pairs do not establish a narrow nonregression confidence interval.

| Case | Pairs | Baseline CPU median s | Current CPU median s | Paired speed median [range] | Scope |
|---|---:|---:|---:|---|---|
|fast|3|0.529487|0.537975|0.969 [0.956,0.989]|bit+absence|
|wide|3|0.076052|0.078319|0.935 [0.933,1.093]|bit+absence|
|weak|3|0.070332|0.071850|0.979 [0.968,0.991]|bit+absence|
|original-p005-auto|3|0.199604|0.199160|0.990 [0.801,1.002]|bit+absence|
|original-p5-auto|3|0.201227|0.198702|1.009 [0.996,1.013]|bit+absence|
|original-p005-6000|3|0.260179|0.267447|0.994 [0.972,1.016]|partial|
|original-p5-6000|3|0.262395|0.276726|0.934 [0.930,0.968]|partial|
|original-1500-auto|3|0.263514|0.265571|1.007 [0.913,1.042]|partial|
|boundary-positive|1|10.281255|1.848316|5.562 [5.562,5.562]|bit+absence|
|boundary-negative|1|10.088117|1.877285|5.374 [5.374,5.374]|bit+absence|

The fast/wide/weak controls are F1, Fs6000/carrier1500, nominal bandwidth1200/100/100, chip10/120/120, symbol640/7680/61440samples (0.106667/1.28/10.24s), three pairs, workspace67108864bytes. Input C/N0 is72/32/22; they are functional weaker/wider references, not near-threshold loss curves. At the joint boundaries actual clock±0.00060000000009ppm and additive offset±0.0003Hz reach the physical combined frequency bound±0.0003045Hz; true origin lies±20samples from map center within20.0142sample halfwidth. Both bit/completion counts and selected initial first5252/end517256 agree. Their event hashes differ due faster publication/completion timing (26.2144→13.7728s firstbit), not a differing bit prefix. Only these two co-signed corners were measured, not every clock/frequency/phase corner.

Original reproduction uses synthetic key0x37/epoch1800000000, B0.01, target4.2185134083910505, input C/N0−3 (tx3−path170−noise−164), GPSDO-XO, shift0, independent reference, margin3, phase diffusion0.007071067811865475deg/√s, actual clock0.0001ppm, seed117 and F1. Fixed matching workspace2,216,646,656bytes is the supplied historical50% reference, not a fresh dynamic RAM resolution. Complete resolved per-case geometry, hypothesis widths, PCM hashes, memory, throughput and latency are in the JSON summary.

| Carrier / sampling | Fs | Chip samples | Symbol samples / seconds | Captured media / samples | Actual top receiver | Hypothesis pairs | Coverage |
|---|---:|---:|---|---|---|---:|---|
|original-p005-auto|64|12800|409600 / 6400|30000 / 1920000|clock_correlator|3|onebit+fully observed absence|
|original-p5-auto|64|12800|409600 / 6400|30000 / 1920000|clock_correlator|3|onebit+fully observed absence|
|original-p005-6000|6000|1200000|38400000 / 6400|2000 / 12000000|clock_correlator|3|partial startup/boundaries;0bits/0completions|
|original-p5-6000|6000|1200000|38400000 / 6400|2000 / 12000000|clock_correlator|3|partial startup/boundaries;0bits/0completions|
|original-1500-auto|6000|1200000|38400000 / 6400|2000 / 12000000|clock_correlator|3|partial startup/boundaries;0bits/0completions|

Carrier1500 application Fs6000 equals the requested fixed Fs6000; that row is reused for both geometries, with no duplicate run. Different carriers have different generated waveforms; only identical PCM within each old/new pair is compared. The original6400s symbol is32chips, correcting the preliminary unverified12800s assumption. Fs6000 captures are2000s/12Msamples, before complete first-symbol acquisition; they measure startup/frontend/boundaries only. A full first symbol+absence would exceed this harness's256MiB retainedPCM cap. They cannot prove full acquisition speed or completion at high Fs. Compact inner raw/pulse/moments selection is unobserved; only the public top clock_correlator is measured. No inferred backend is substituted for an actual getter.

## Conditional finite-waveform sensitivity

Four fresh20,000-draw holdouts (80,000total), bit0/1 nominal and adverse tail4samples/±100ppm/±0.05Hz/fixed phase diffusion0.1deg/√s, waveform seed117. Nominal has zero clock/frequency/diffusion. ±100ppm is a matched-branch stress, not a GPSDO bank boundary. All use actual finite shaped/limited transmitter vectors, actual noncircular bin4 real covariance and joint V1/V2 Gaussian physical-noise realizations, one matched epoch/frequency/rate/phase with5/6retained start cells and original fullhop134145trial criterion (nominalα10^-10). Outer guard is explicitly disabled. There is no interferer in this model. Setup4.42–4.70s and trial loops10.62–14.34s per20k; every draw retained, no sampled-grid exceptional crossing.

The contrast is matched V1 versus matched V2 at equal actual bare transmitter payload+tail energy, with different waveforms. V1/V2 energies61099.65/15925.97 and scaling2.89478/5.66999 in the nominal case are explicitly retained. This is not samePCM algorithm loss, raw cumulative loss, or unchanged tx-dBm sensitivity. The latter needs additional power/operator qualification.

Grid−5…25dB in0.05dB increments (601points), sampled-grid first crossings with sampled monotonicity/bracketing checks; no continuous monotonicity proof.10,000paired bootstrap resamples, fixed seed823903, Bonferroni family allocation across four cases×q90/q99. Conservative intervals also include absolute order-statistic uncertainty, geometry and grid enclosures, and a Clopper-Pearson allowance for unseen sampled-grid crossing exceptions (upper0.00025373). Covariance/span geometry enclosures exclude floatPCM casts/FFT score arithmetic, which are checked separately below. These intervals describe only the ideal conditional sampled-grid model.

All four point estimates: q90 V1=V2=9.25dB (Δ0.00dB); q99 V1=9.90,V2=9.85 (Δ−0.05dB). The conservative additional-V2-C/N0 intervals are:

| Case | q90 interval dB | q99 interval dB |
|---|---|---|
|Nominal bit0|[−0.15,+0.10]|[−0.20,+0.15]|
|Nominal bit1|[−0.15,+0.05]|[−0.20,+0.15]|
|Tail4,+100ppm,+0.05Hz,bit0|[−0.15,+0.05]|[−0.20,+0.10]|
|Tail4,−100ppm,−0.05Hz,bit1|[−0.10,+0.10]|[−0.20,+0.15]|

Result: INCONCLUSIVE for the0.1dB maximum; no additional MC was run to chase the limit. Curves include correct-bit and bit-error counts; no rare false-accept probability is inferred from these feasible draws. Theα10^-10 value is an original analytical criterion, not an empirically measured rare-event rate. Fullbank detection/noise-only/interference, active-guard loss, multiple keys/epochs, hardware/filter, raw cumulative loss and physical completion sensitivity remain unqualified.

## Production PCM replay and failed attempts

Selected empiricalq90/q99 draws from each holdout were reconstructed as actual finite floatPCM and replayed through automatic paired-direct and identical-K fullFFT arithmetic:32version rows. Corrected numerical assertions pass; maximum initial score discrepancy3.5934e-7, alternative7.5844e-8, stored admission threshold discrepancy0; selected first/end positions agree; automatic first-component decisions match the finite model. Max floatcast error2.3801e-7, residual relative leakage4.7554e-18, white energy relative error9.3490e-18. These selected checks do not extend the conditional uncertainty interval into full acquisition qualification. The restrictedFFT later continuation/aggregate trial count differs; only initial stored criterion/statistics are compared. Automatic replays stop at first component, with no later bits/completion; physical endpoint sensitivity is not inferred.

The initial replay diagnostic negated an unsigned length and produced negative diagnostic scores, thereby bypassing its score-gap assertion. That artifact bug was corrected using explicit long-double conversion and all four selected batches rerun; MC probability/curve algebra was unaffected. Superseded files remain intact. A stale checked review stopped a tail replay before exec; the barrier child was killed/joined, fresh review and a new label preceded retry. An initial analyzer exec path failure likewise performed no analysis; successful uniquely labeled retries are retained. No failed launch is counted as a pass. Release helper's uppercase claim syntax was rejected before publication, corrected to tested lowercase syntax and retried with a fresh review; no dependent acknowledgment occurred before successful release.

## Remaining scope

No further experiments will run in this session. Unmeasured: forced partition crossover for retained broad FFT cohorts; final5D10/D100 repeated width matrix (source3 historical rows are retained separately); default/fallback fixed receiver width performance beyond existing source3 controls; full high-Fs original acquisition/absence; broader RF/Sub9 long geometries; every joint boundary; fullbank/raw/activeguard≤0.1dB sensitivity; interference/noise-only rare-event qualification; hardware timestamps/real-time transport and cancellation qualification. Root's affected core/GUI/CLI/application results are separate functional evidence, not supplied by these timings. Manual-stage limits and further general/regression qualification remain explicit.

The timing resource was released under tight-final5-measurement-return-1; all211recorded historical/current owned child jobs joined, all timing/output writers stopped. The regression plan had no omissions. Evidence below is final; no later source/lib or trial substitution occurred.

## Evidence inventory

- /home/user/___quick/p/_cur/dataPump/pumpModem/.agent-work/artifacts/receiver-tight-envelope-measure-20261009/final5-primary-summary.json SHA256 2b880ee3db01a4197396fa5dd526ef0c35392d5dac5203bc2a46fc547e96c5c5
- /home/user/___quick/p/_cur/dataPump/pumpModem/.agent-work/artifacts/receiver-tight-envelope-measure-20261009/final5-primary-pairs.json SHA256 de5c9e599e93f03e2ff0a9e531785555d6097ec09a480e8636317b287d848251
- /home/user/___quick/p/_cur/dataPump/pumpModem/.agent-work/artifacts/receiver-tight-envelope-measure-20261009/final5-regression-summary.json SHA256 95fde104a182e478fbc5c6affe996d7edabfcc045d635a8e2d2a1f448fd4e613
- /home/user/___quick/p/_cur/dataPump/pumpModem/.agent-work/artifacts/receiver-tight-envelope-measure-20261009/final5-regression-execution.json SHA256 af56b8b01fa21da293b44bb48436b1d50f935c769d4d2f3356b73dc9f929c2a8
- /home/user/___quick/p/_cur/dataPump/pumpModem/.agent-work/artifacts/receiver-tight-envelope-measure-20261009/final5-regression-prospective-plan.json SHA256 2973f000873e9916a77dae81a1a0983885f858ce9fa3370c28eb63c3e2cf04b5
- /home/user/___quick/p/_cur/dataPump/pumpModem/.agent-work/artifacts/receiver-tight-envelope-measure-20261009/final5-arithmetic-profile-summary.json SHA256 c6fe9c905efad6b70ea08fc1fa1f62b4b8c97713e63fe46b26d0b38acc8ce4c5
- /home/user/___quick/p/_cur/dataPump/pumpModem/.agent-work/artifacts/receiver-tight-envelope-measure-20261009/final5-corrected-pcm-replay-summary.json SHA256 02e313b8dd85fef8db8e26afb6933d3b23fa2e10eec70748901cf0087d6478fc
- /home/user/___quick/p/_cur/dataPump/pumpModem/.agent-work/artifacts/receiver-tight-envelope-measure-20261009/final5-v2-live-d1000-full60.result.json SHA256 c6fd6bf61cabd38d58c6634db7a4847fd4032031ca0e549266931bad47255e14
- /home/user/___quick/p/_cur/dataPump/pumpModem/.agent-work/artifacts/receiver-tight-envelope-measure-20261009/final5-runtime-identity.json SHA256 2daa8b0895d94a223f50a5391a4b5c6dd6e47ee9a053343cbf5bf9e904bbeeb8
- /home/user/___quick/p/_cur/dataPump/pumpModem/.agent-work/artifacts/receiver-tight-envelope-measure-20261009/final5-timing-release.json SHA256 34d41ce0516177d3b9fbf997486100693c42ae2b61b7860218395da581ff89de
- /home/user/___quick/p/_cur/dataPump/pumpModem/.agent-work/artifacts/receiver-tight-envelope-measure-20261009/final5-mc-analysis2-nominal-b0.json SHA256 59f37b737b9c40ccae13f45e2c09a203c51d0180bdea42b4e586ee46d7ffe3cf
- /home/user/___quick/p/_cur/dataPump/pumpModem/.agent-work/artifacts/receiver-tight-envelope-measure-20261009/final5-mc-analysis2-nominal-b1.json SHA256 2a374b194d29e5c79a8037d747d70092ff12fd4b2ed9b317f39a0bb11f9fac23
- /home/user/___quick/p/_cur/dataPump/pumpModem/.agent-work/artifacts/receiver-tight-envelope-measure-20261009/final5-mc-analysis2-tail-positive-b0.json SHA256 00156ec24ecaab22dbf55a55453bc028466bbb00c1f6ba83b342e5a71c423e82
- /home/user/___quick/p/_cur/dataPump/pumpModem/.agent-work/artifacts/receiver-tight-envelope-measure-20261009/final5-mc-analysis2-tail-negative-b1.json SHA256 d7f711b671bf56abc8bbe75a744bd99b13938aa03ccfc80bed0d93b182175872
