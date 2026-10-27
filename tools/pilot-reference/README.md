# Isolated continuous-pilot frequency reference

This experimental C++20 component preserves the useful unmodulated-pilot
work from Helper Carrier diagnosis. It has no application, CMake, audio-device,
Robust receiver, metadata decoder or two-tone transmitter integration. It is
not used by the GUI. The existing Helper Carrier implementation is unchanged.

`pilot_coarse.hpp` discovers the strongest guarded positive-frequency peak in
the configured expected range using chronological, nonoverlapping FFTs.
`pilot_reference.hpp` integrates real PCM in fixed 400 ms apertures, validates
cycle branches against an independently dated coarse reference, and optionally
steers a second continuous NCO using completed past estimates. A quadratic
phase fit retains at most 96 numeric observations spanning at most 36 seconds.
There is no historical PCM search or transmission-start/UTC input. The 400 ms
aperture is an estimator choice, not a clock or framing claim about the pilot.

The input must be a **continuously unmodulated sinusoid**. Punctuation, metadata,
ramps, a second tone and their extraction are outside this component. The
source's RF and audio-clock errors are observed only as one audio-frequency
reference; this component cannot separate them or supply epoch synchronization.

## Supported conditional model

The test declares a constant or locally linear deterministic carrier-frequency
trend with absolute slew at most 0.03 Hz/s. This is an explicit laboratory
supported condition, not a drift prior inferred from oscillator ppm accuracy,
not a measurement of future thermal behavior and not a new GUI setting. No
support is claimed for abrupt frequency steps, slope changes over the retained
fit, Doppler, retuning phase jumps or an interfering stronger coherent tone.
Old changed-model failures from the Helper Carrier investigation remain failures.

Optional Wiener phase has increment variance `2*sigma^2*dt` radians squared;
`sigma` is configured explicitly. The reported frequency truth is the
**deterministic carrier trend**. Wiener phase has no instantaneous derivative.
Saved thermal endpoint noise, actual-date Brownian increment tails, window
allowances and shared-PCM coarse/fine contrast are retained. Real-PCM conjugate
image and supported local chirp errors have separate aperture bounds.

The uncertainty is an engineering interval conditional on these models and the
identified FFT lobe, **not a calibrated confidence interval**. The retained
`+0.005 Hz` allowance is empirical engineering conservatism, not a derived
floating-point bound, a physical precision floor or proof of efficiency.
Periodic whole-bin FFT intervals are engineering geometry, not probabilities.

Each estimate names its actual center date, first/end support and count. At
publication its most recent aperture center is approximately 200 ms old; updates
arrive every 400 ms. Reuse never refreshes that measurement date. The optional `BlockResult.phase` is in cycles relative to the component's
fixed first seed with the actual continuous NCO phase undone; an arbitrary
constant phase remains. It is not absolute RF phase. There is no
current/future precision lease. Consumers must preserve these coordinates,
perform their own dated transport/eligibility and keep ordinary payload drain
and physical completion independent of pilot presence. A fully absent aperture
withdraws qualification; it does not imply message END.

The coarse FFT supports 12/24/48/96/192 kHz PCM so its one reusable transform
buffer spans 2.730667 seconds. At 24 kHz it occupies 524,288 bytes; other supported
rates scale proportionally. The reference object includes its 96-point numeric
history and reports its actual `sizeof`. The test also reports coarse-object and total component bytes. No raw PCM history is retained beyond
the current transform. Callers must fund this optional memory/work explicitly
before integrating it with any application budget.

## Reproduce the short component check

From this directory, with no Data Pump libraries or OpenSSL required:

```sh
task_test_dir=$(mktemp -d)
c++ -std=c++20 -O3 -DNDEBUG -ffp-contract=off -fno-fast-math \
  test_pilot.cpp -o "$task_test_dir/pilot-test"
"$task_test_dir/pilot-test"
```

The fixed small matrix uses real PCM at 24 kHz: C/N0 20/30/40 dB-Hz, constant
and +0.03 Hz/s carrier trends, one -0.03 Hz/s case, Wiener sigma 0.01, a coherent
control, different initial phase/large integer sample origin, +3000.58 Hz capture
with a wide expected range, and final pilot loss. Truth generation never seeds
the receiver. Present cases require completed-past steering and uncertainty
below 0.1 Hz by 38 seconds. Every qualified output checks actual-center trend
error, dates, cycle lifts and coarse/fine domains. Loss checks fully absent
400 ms apertures and bounded withdrawal. Failures are printed individually.

The CPU bracket includes FFT/reference processing and diagnostic callbacks;
it excludes PCM, noise and truth generation. It is component CPU per generated
audio second, **not ordinary Robust receiver CPU or live hardware throughput**.
Single deterministic seeds do not establish statistical coverage or estimator
efficiency. Pending application integration, frequency-search savings, long
payloads, attachments, physical cable operation and the future two-tone design
are all unexecuted here.

## Measured component result

One source-reviewed, single deterministic batch passed all 11 cases.
These are actual real-PCM results from the command above, using Linux CPU0,
24 kHz PCM, 38 generated seconds, and the stated compiler flags. The following
constant-trend rows use Wiener sigma 0.01 and the explicit supported locally
linear model with maximum absolute slew 0.03 Hz/s. Units below are Hz;
errors are against the deterministic trend at each estimate's actual date.

| C/N0 (dB-Hz) | Median U | Final U | Median absolute error | p90 absolute error | Maximum absolute error | Final signed error |
|---:|---:|---:|---:|---:|---:|---:|
| 20 | 0.012551 | 0.008665 | 0.001902 | 0.011954 | 0.053971 | -0.000388 |
| 30 | 0.009660 | 0.007997 | 0.001090 | 0.004291 | 0.014055 | +0.000021 |
| 40 | 0.009277 | 0.007924 | 0.000878 | 0.002524 | 0.004238 | +0.000158 |

All present cases discovered the pilot by 2.730667 s, first published a
qualified estimate at capture frontier 4.330667 s, and qualified 85 of 88
completed post-discovery apertures. Their longest qualified span was 33.6 s;
maximum retained dated observation support was 34.8 s, **not 38 s of uninterrupted
coherence**. Every present case had a valid latest completed aperture at
37.930667 s with U below 0.1 Hz. There were 81 useful qualified steered outputs
at 20 dB-Hz and 84 in the other present cases. Completed-past steering was
therefore actually exercised, not inferred from a flag or a separate early fit.

The +0.03 Hz/s cases at 20/30/40 dB-Hz finished at U 0.008666/0.007993/0.007922 Hz;
the -0.03 Hz/s, 30 dB-Hz case finished at 0.007992 Hz. The coherent 30 dB-Hz
control (sigma zero) had median/final U 0.007078/0.005779 Hz, median/p90/maximum
absolute error 0.000458/0.003432/0.016559 Hz, and final signed error -0.000204 Hz.
The retained 0.005 Hz engineering allowance remains part of these U values;
it is not a physical precision floor or a derived numerical-error bound.

The final pilot-loss case stopped the tone at 26 s. The first fully absent
aperture withdrew qualification at 26.730667 s (0.730667 s after loss), with
zero qualified fully absent apertures. This is a pilot observation result,
not message completion or physical END. The wide +3000.58 Hz capture and
large integer sample-origin cases also passed.

Across the 11 fixed cases there were zero observed interval, independent-FFT
seed, coarse/fine-domain, phase-cycle or sample-date escapes. This small batch
does **not** establish statistical coverage or estimator efficiency. Publication
is approximately 200 ms behind the current capture frontier; subsequent reuse
must retain the date and supply justified transport, not treat U as a future
lease. Arbitrary retunes, abrupt frequency/slope changes, reacquisition, other
sample rates, physical RF/cable operation, ordinary receiver acquisition work,
payload completion and the future two-tone waveform remain unqualified here.

The measured component processing bracket was 0.000834–0.000860 CPU seconds per
audio second (FFT/reference plus logging callbacks, excluding signal/noise/truth
generation). Each 38-second case performed 13 FFTs and 6,815,744 butterflies.
At 24 kHz, the transform buffer is 524,288 bytes, the tracker including numeric
history is 9,552 bytes, and the coarse object is 216 bytes: total component
storage 534,056 bytes. This is not ordinary Robust receiver CPU or its workspace.
The entire compile plus 11-case diagnostic took 2.7871 wall seconds; all owned
processes were reaped, with no main-source or tested-input mutation.

Raw evidence remains in the ignored preparation directory
`local-evidence/helper-v16/phase-bridge-prototype/strong-takeover/pilot-closeout-r1/checks/`:

- `execution-result.json`: SHA256 `b3ae62c63990f04c6af20677cd2b1441ff6ad7798d8ff58a8b2a4000420f36e8`.
- `inputs.json`: SHA256 `0c2eca29b45fd985c6b5a8480ef14757bcbd51a65c1fe21a3e9d44cb60b8d902`.
- Tested binary: SHA256 `0c993f9030afe77a207d01e52bac24f1dbe17c2f0d03673fedc6e0743e61c266`.
- Tested `pilot_reference.hpp`: SHA256 `77dc2c168621aa94e9cf1b56c5a5a15b02ccf241fcdfb1638835d8c27cc60ca1`.
- Tested `pilot_coarse.hpp`: SHA256 `c1c2282ac5965e1cf88fe8eeac7990dba8bfc7fdefbdfe5cef9d7917a963644b`.
- Tested `test_pilot.cpp`: SHA256 `284a665fb3b0dd7b5794cff7c004c1683ae4e22d2ddc506105a2296ff618cffb`.

Only this results section was appended after the batch; headers and test are
unchanged. The input manifest therefore records the pre-results README hash.
No application behavior or build registration is changed by preserving these
standalone files.
