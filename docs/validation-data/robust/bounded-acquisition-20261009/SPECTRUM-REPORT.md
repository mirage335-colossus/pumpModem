# Versioned outer DSSS digital envelope and spectrum measurements

2026-10-09; session `outer-envelope-spectrum-20261009`. All changes/artifacts local.
This is a deterministic transmitter-waveform diagnostic, **not receiver sensitivity,
false-acceptance, receiver CPU, ASRC/device/RF, acoustic or release qualification**.

## Measured finding

For the primary DSSS1000 capture, actual V2 changes guarded payload OOB from
−30.145 to −48.218dBc (18.073dB), with observed payload radial limiting falling
from3.8265% to0%. The longest declared high-envelope run falls from0.60s to none.
Permutation disperses the held coefficient magnitudes; the .5 headroom removes
the remaining measured limiter regrowth. Backoff-only V1 also reaches its own
linear spectrum; V2 additionally disperses envelope runs and improves peak
requirements. Permutation alone still clips and does not reach the linear
spectrum. Whole-bit coefficient power variation remains.

At unchanged downstream gain, actual V2 digital real power is5.798–5.844dB lower
than V1 in the three primary cases (5.742dB in the separate partial-chip case).
This is a material output-power cost, not evidence of below0.1dB sensitivity loss.

## Exact inputs and source identity

Frozen V1 HEAD `9ee268e23449fbdc43c66a792fd1321b359fcbe3`, manifest SHA256
`5043c915d8054b1519b28ef03db9e65f473b516f5858f35d2a671fb67732c4c2`, library
`c3022e9c11fbd6f2ef717ac3e0a2fdb1971931e58bd3ba0f981d5b777221d8c0`.

Candidate is root’s immutable `tight-candidate-pilot-1`, actual dirty source
archive SHA256 `1e62a9f1f0d403a670f599d06a3e7a341fd8312732b86cd9edd56f1b6ab567ac`,
manifest `fd5a0705f491080295853c469f7d21b76e26ca9d50df3861d46fee6762d49be8`,
library `056a9ec5ff13047bb06cef7f9dfe0f3485809ee68df2e17437f6cbbbfb215585`.
Its waveform source `pattern_code.cpp` is
`fb2d611e309f01935f402f6e36a4cb7d1db1aeb2d5fd1f34099c58986013e1dc`.
Root continued receiver-progress/planner work separately. The pilot is sufficient
for this frozen waveform scope; these results do not qualify those later changes.

All cases use Fs40000Hz, real carrier7500Hz, requested fine bandwidth10000Hz,
fine chip8samples/0.2ms,64 inner chips per bit, finite RRC shaping and surrounding
noise. Primary synthetic bytes are `(3*i+7) mod256` and `(13*i+29) mod256`,
epoch1789312671, phase0, repeating payload bits001. No selected/user key is used.
The separate partial-chip case uses bytes `(3*i+19) mod256`, `(13*i+71) mod256`,
epoch1789312672 and stream-address phase137samples. That phase is an integer
stream-address phase, not a fractional resampler impairment.

|Case|Inner rate|Bits|Symbol samples / seconds|Complete fine chips + partial samples|Training|Padding each side|Suppression|Total samples|
|---|---:|---:|---:|---:|---:|---:|---:|---:|
|d10|1000Hz|300|5120 / 0.128000|640 + 0|81920|64|120000|1738048|
|d100|100Hz|30|51200 / 1.280000|6400 + 0|102400|64|120000|1758528|
|d1000|10Hz|3|512000 / 12.800000|64000 + 0|0|64|120000|1656128|
|d1000-partial|10Hz|3|512003 / 12.800075|64000 + 3|0|64|120000|1656137|

Primary payloads are38.4s. The partial case has three512003sample symbols,
including a final3sample chip in each symbol. Alternating503/2400/1/4093/127sample
read spans cross chip, symbol, pulse-tail, prefix and suppression boundaries.

## Controls and assertions

The factorial comparison is V1/V2 × pre-limiter gain1/.5. V2 includes its
version-specific permutation **and rotation domain**; “permutation” below is
shorthand for this full version change without headroom. The diagnostic .5-only
V1 and unity-gain V2 controls are generated from the exact same recorded complex
pre-limiter samples through the existing limiter and oscillator.

An isolated copy adds one recording callback at the actual padded render limiter
input. No pulse substitute, independent oscillator reconstruction or reset at bit
boundaries is used. Checks passed for all samples, including surroundings:

- Frozen V1 and unmodified candidate V1 real float PCM are byte-identical.
- Instrumented and unmodified candidate V1/V2 real float PCM are byte-identical.
- Complete-chip coefficient norm multisets match exactly per bit, V1 versus V2.
- The final partial coefficient power remains equal and is checked separately.
- Entire bursts finish at their exact expected sample counts; every padded-render
  sample is recorded once.
- Hann-weighted spectral integration agrees with time-domain power to at most
  1.67e−16 absolute error in saved payload results.

`render-verification.json`, `provenance.json`, `commands.json`, the exact probes
and immutable original/instrumented PCM preserve reproducibility. No assertion
or failed case was skipped.

## Envelope and whole-bit variation

The primary envelope statistic is a40ms rectangular power average,10ms hop,
using actual pre-limiter complex power. “High” means greater than twice that
capture’s global payload mean (+3.0103dB). Run duration is consecutive-window
count × hop; window-support span is30ms longer for a nonempty40ms run. It is
a descriptive overlapping-window measure, not an independent-event probability.
A separate within-bit normalized view excludes windows crossing a bit boundary;
it does not replace global normalization or hide whole-bit variation.

|Case|V1 longest equal coefficient-power run|V2 run|V1 high40ms windows|V1 longest high run|V2 high windows/run|V1→V2 longest high4ms run|Coefficient whole-bit CV, retained|
|---|---:|---:|---:|---:|---:|---:|---:|
|d10|4.0ms|0.6ms|0.000%|0.000s|0.000% / 0.000s|0.018→0.002s|11.083%|
|d100|40.0ms|0.6ms|6.281%|0.120s|0.000% / 0.000s|0.062→0.001s|11.332%|
|d1000|200.0ms|0.8ms|12.510%|0.600s|0.000% / 0.000s|0.600→0.000s|5.179%|
|d1000-partial|400.0ms|0.8ms|15.272%|0.400s|0.000% / 0.000s|0.400→0.000s|4.134%|

Backoff-only leaves every normalized envelope run unchanged. V2 redistributes
the same per-bit coefficient-radius multiset; it creates no new independent
amplitude draws. Finite-RRC shaped energy is c*Gc, so its small per-bit energy
changes need not be exactly zero. The complete D1000 coefficient bit means
remain0.8366965,0.9486886,0.8813284 in both versions. There are only three bits and
192 underlying coarse draws in that primary case; overlapping samples/windows
cannot establish population tails, security or general whole-bit fingerprint loss.

## Limiter and spectral stages

Spectra use40ms Hann windows (1600samples),10ms hops, FFT8000.5Hz padded bins
interpolate a25Hz native Fourier grid; Hann ENBW is about37.5Hz. For the current
chip rate/RRC rolloff, ideal real positive-frequency support is4375..10625Hz.
Primary guarded OOB integrates below4175Hz or above10825Hz (200Hz guard each
side); far OOB is below3500/above11500Hz. The guard is a diagnostic exclusion,
not a regulatory mask. It excludes adjacent-band power: actual V2 unguarded
ideal-support OOB is about−44.4dBc, versus about−48.2dBc guarded. Neither metric
separates finite-pulse/table leakage from window leakage. Aggregate powers are
summed before taking dBc.

The following rows use payload only. Radial clipping counts complex samples
above limiter radius.992, including samples whose real projection is below1.

|Case|Control|Real mean power|Limiter occupancy|Complex energy removed|Linear guarded OOB|Limited guarded OOB|
|---|---|---:|---:|---:|---:|---:|
|d10|v1-unity|0.144257323|5.1120%|4.6641%|-48.214dBc|-29.385dBc|
|d10|v1-backoff|0.037828503|0.0000%|0.0000%|-48.214dBc|-48.214dBc|
|d10|v2-permutation|0.148493032|3.5528%|1.8646%|-48.222dBc|-33.708dBc|
|d10|v2-full|0.037828324|0.0000%|0.0000%|-48.222dBc|-48.222dBc|
|d100|v1-unity|0.144151150|5.2699%|4.9960%|-48.218dBc|-29.065dBc|
|d100|v1-backoff|0.037932880|0.0000%|0.0000%|-48.218dBc|-48.218dBc|
|d100|v2-permutation|0.148879108|3.5490%|1.8790%|-48.225dBc|-33.654dBc|
|d100|v2-full|0.037932592|0.0000%|0.0000%|-48.225dBc|-48.225dBc|
|d1000|v1-unity|0.130701267|3.8265%|3.9756%|-48.195dBc|-30.145dBc|
|d1000|v1-backoff|0.034028351|0.0000%|0.0000%|-48.195dBc|-48.195dBc|
|d1000|v2-permutation|0.134305643|2.3943%|1.3262%|-48.218dBc|-35.152dBc|
|d1000|v2-full|0.034028276|0.0000%|0.0000%|-48.218dBc|-48.218dBc|
|d1000-partial|v1-unity|0.162480192|7.1724%|6.2117%|-48.238dBc|-28.127dBc|
|d1000-partial|v1-backoff|0.043309943|0.0000%|0.0000%|-48.238dBc|-48.238dBc|
|d1000-partial|v2-permutation|0.168594226|5.3948%|2.6828%|-48.219dBc|-32.042dBc|
|d1000-partial|v2-full|0.043310145|0.0000%|0.0000%|-48.219dBc|-48.219dBc|

Actual V2’s maximum measured pre-limiter payload radius is0.7274/0.7501/0.7238
forD10/100/1000, and0.7731 in the partial case. Zero observed payload limiting in these
captures is not a universal no-limiter guarantee for every key/message/geometry.
Permutation-only maxima are1.455/1.500/1.448/1.546 and still exceed.992.

Full-burst Welch guarded OOB (including prefix, full pulse tails and suppression)
is V1→actualV2: D10−29.712→−48.227dBc, D100−29.439→−48.229dBc,
D1000−30.343→−48.217dBc, partial−28.341→−48.218dBc. This uses the same
windowed estimator, not a full-length rectangular FFT. V2 boundary-straddling
payload windows retain−48.198/−48.198/−48.150/−48.033dBc respectively; there are
only6 primary-D1000 and8 partial-case boundary windows, so do not infer rare-event
or population tails. Full-burst Welch supplies aggregate coverage, not a separate
isolated pulse-tail or worst-transient bound.
Whole-burst clipping occupancy was not separately summarized; payload occupancy
above and full-burst PCM/spectral checks have distinct scopes.

## Equal gain, payload average power and peak conditions

|Case|Actual V2 power at equal downstream gain|Required global gain for equal real RMS|Equal-RMS real peak|Equal-RMS analytic peak|Equal-real-peak average-power cost|Equal-analytic-peak cost|
|---|---:|---:|---:|---:|---:|---:|
|d10|-5.8132dB|5.8132dB / ×1.95281|1.3651|1.4205|-2.7730dB|-3.1184dB|
|d100|-5.7981dB|5.7981dB / ×1.94941|1.4218|1.4622|-3.1267dB|-3.3697dB|
|d1000|-5.8444dB|5.8444dB / ×1.95984|1.4179|1.4184|-3.1028dB|-3.1060dB|
|d1000-partial|-5.7421dB|5.7421dB / ×1.93689|1.4507|1.4975|-3.3015dB|-3.5769dB|

The .5 amplitude itself is6.0206dB pre-limiter power backoff. Its reduction
relative to the already-limited V1 output is smaller because V1 already discards
energy. Equal-RMS rescaling is an analytical downstream comparison, applied once
to the complete payload; it requires additional physical gain and headroom.
The common gain is calculated from complete-payload RMS, not separate bit/window
normalization; it could be applied to the whole transmission. The real peaks
above1 show why this is not free at an unchanged fullscale DAC.
Applying gain before another limiter can recreate distortion. Equal-peak scaling
instead keeps average-power deficits of roughly2.77–3.30dB using the real peak.

ForD1000 actualV2 equal-RMS guarded absolute OOB is1.9702e−6 versus V1’s
1.26366e−4, while dBc is unchanged by global rescaling. Relative spectral
improvement therefore persists under equal-average-power normalization, subject
to the required extra physical headroom. Each variant is compared with its own
linear waveform: D1000 real squared normalized correlation is0.9963493 for V1,
0.9993091 for unity V2, and1 for actual V2 in this zero-clipping payload. These
are conditional distortion statistics, not required C/N0 differences.

## Bounded work and memory

Only prebuilt frozen libraries were linked; no application/shared build writes.
Artifacts compile with g++ C++20/O3/ffp-contract=off. Renders/analysis use one
allowed CPU0, with numerical thread counts1. Three successful compiles took
about7.41s total wall. The five complete four-case render jobs took1.390,1.427,
1.483,1.799 and1.938s wall (baseline, candidateV1, candidateV2, instrumentedV1,
instrumentedV2); analysis took16.004s wall/15.928s child CPU and225316KiB peakRSS.
Uninstrumented process peakRSS was17588/17544/18432KiB; instrumented≈84MiB.
These are fixture resource accounts, **not repeatable paired receiver benchmarks
or a claimed transmitter/receiver speedup**.

Reported bounded transmitter working bytes (two V2 maps charged upfront):

|Case|V1|V2|Additional|
|---|---:|---:|---:|
|d10|13244|62084|48840|
|d100|12974|107894|94920|
|d1000|10835|566555|555720|
|d1000-partial|10835|566555|555720|

The increase in these geometries is8*N +43720bytes forN complete fine chips.
Receiver-bank memory has separate accounting and was not measured here.

The runner initially encountered missing `/usr/bin/time`, an unavailableCPU24
(allowed CPUs0–11), and system Python without NumPy. Those setup failures are
retained in `commands.json` and attempt logs. The private wrapper now uses native
affinity/child resource accounting and bundled Python/NumPy2.3.5. Successful
compiles/renders were reused after verified resume; failed attempts did not skip
assertions. Process groups have bounded timeout/interruption cleanup.

All jobs joined and timing was returned immediately as
`outer-envelope-spectrum-timing-1`, manifest SHA256
`e5abef2a281b1887b5b041b9f70a524f57c8dd88c84b4bd3e078bb66490dcdee`.

## Remaining scope

No threshold-region detection/BER curves, C/N0 sensitivity uncertainty, full-bank
false-acceptance tests, interference/oscillator impairments, ASRC/int16 stage,
device/RF passband/nonlinearity, acoustic clicks or broader platform/SDK/CI
qualification ran in this task. Full required checks and sensitivity qualification
remain with root’s manual-testing checkpoint and later explicitly requested stage.
The observed spectral/envelope improvement must not be substituted for those
results or for a demonstrated below0.1dB cumulative sensitivity loss.
