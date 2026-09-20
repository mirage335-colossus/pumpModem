# Evidence: live analog cable, 2026-09-20

Read the [study report](../../../fast-cable-live-study.md) for results, decisions
and limitations. This directory deliberately separates physical measurements,
offline DSP controls and analytical calculations. File sizes are decimal bytes.
The investigation uses the user's physical headphone-to-microphone loopback;
the default ALC257 analog input/output were inspected during the session.

## Inventory

| Artifact | What it establishes |
| --- | --- |
| [mixer-calibrated.txt](mixer-calibrated.txt) | Hardware mixer state after removing the initial approximately +60 dB microphone-path boost: Capture/Digital/Mic Boost at 0 dB. Pulse source 10% and sink 95% are host-specific controls with different percentage scales. |
| [levels-minus60.json](levels-minus60.json) | Exploratory real-device level check after reducing gain. It includes opening transients and must not be treated as a calibrated frequency-response or SNR measurement. |
| [initial-256.json](initial-256.json) | Successful 62-byte public CLI reception at 256-APSK/7/8/high-rate/depth 16; initial ALSA open warnings retained. |
| [initial-100k-256-d62.txt](initial-100k-256-d62.txt) | Early successful 100,000-byte physical CLI check at depth 62, including direct source equality; selection evidence. |
| [screen-100k/plan.json](screen-100k/plan.json) | Six exact production candidate configurations, source size, binary SHA-256, requested devices and expected airtimes. |
| [screen-100k/trials.jsonl](screen-100k/trials.jsonl) | Six physical 100 KB trials, all exact; source/received SHA-256, process status, FEC telemetry, timing and raw errors. Per-trial directories also retain separate stdout/stderr. |
| [screen-100k/summary.json](screen-100k/summary.json), [CSV](screen-100k/summary.csv) | Direct-size trial counts and separate exact one-sided 95% probability bounds. One screening success per setting is not reliability qualification. |
| [raw-baseline.json](raw-baseline.json), [raw-tight16500.json](raw-tight16500.json), [raw-tight18000.json](raw-tight18000.json), [raw-tight19200.json](raw-tight19200.json), [raw-bridge44100.json](raw-bridge44100.json) | Five real-device raw tests, 512 intervals / 1,048,576 payload bits each. Original-position wrong bits, erasures, missing intervals, alignment diagnostics, residual EVM, levels, formats and bounded queue/DSP metrics. Adjacent `.log` files retain stderr. |
| [codec-tight16500.json](codec-tight16500.json), [codec-tight18000.json](codec-tight18000.json), [codec-tight19200.json](codec-tight19200.json) | Three real-device 100 KB coded probes at faster waveforms: all exact, with 15/12/74 corrected bytes. Probe timing includes an eight-second tail and differs from CLI playback timing. |
| [matched-offline.jsonl](matched-offline.jsonl), [offline-probe.jsonl](offline-probe.jsonl), [method](offline-probe-method.md) | Five matched noiseless floating-point PCM controls and twelve exploratory controls. No physical device; no S16 clipping or live hardware-rate conversion. |
| [waveform-airtime.cpp](waveform-airtime.cpp), [CSV](waveform-airtime.csv), [JSON](waveform-airtime.json) | Twelve production-estimator calls for the four tested waveforms and three file sizes. Faster-waveform long-file times are projections, not measured delivery or reliability. |
| [airtime.csv](airtime.csv), [estimator source](airtime-estimator.cpp) | 6,912 calls to the production airtime estimator: three file sizes, public/encrypted, three constellations, three code rates, two RS strengths and depths 1–64. Predictions, not physical trials. |
| [tx-peak-bound.py](tx-peak-bound.py), [results](tx-peak-bound.json) | Conservative bound over the production pulse table's fractional phases: amplitude 0.35 bounds generated 256-APSK/0.20-rolloff PCM at 0.8948533 before resampling or external gain. It does not cover the complete analog path. |
| [reliability-model.md](reliability-model.md), [JSON](reliability-model.json), [source](reliability-model.py) | Explicit conditional cycle/shard/binomial models, retry break-even thresholds and exact simplified marker counting. No simulated or measured 50 MB success result. |
| [long-5mb/plan.json](long-5mb/plan.json), [trial](long-5mb/trials.jsonl), [summary](long-5mb/summary.json) | One exact 5,000,000-byte physical transfer at the selected coding/waveform settings, with amplitude **0.5** and both output channels: 735.879 seconds to delivery, 54,356.78 bit/s, seven corrected bytes and zero erased bytes. |
| [repeat-100k/plan.json](repeat-100k/plan.json), [trials](repeat-100k/trials.jsonl), [summary](repeat-100k/summary.json) | Fourteen fresh-seed, exact 100,000-byte files at the final amplitude **0.35**, with both output channels, zero corrected/erased bytes. The conditional one-sided 95% lower success bound is 80.7364% for 100 KB only. |
| [raw-headroom.json](raw-headroom.json), [raw-headroom44100.json](raw-headroom44100.json), [raw-tight19200-headroom.json](raw-tight19200-headroom.json) | Three real-device headroom probes, each 1,048,576 raw bits, with no TX/ADC clipping. The two 15,000/0.20 baseline probes at amplitude 0.35 had zero wrong/erased/missing bits at logical 48 kHz and 44.1 kHz. The 19,200/0.10 probe at amplitude 0.30 still had 726 wrong bits and 40 erasures. |
| [default-route-100k/plan.json](default-route-100k/plan.json), [trial](default-route-100k/trials.jsonl), [summary](default-route-100k/summary.json) | One failed 100 KB file at amplitude 0.35 using the former default right-only output route: all 710 intervals and physical completion observed, but `Too many Reed-Solomon erasures`, final EVM 6.92%, no verified file. The runner then cancelled the TX tail. This outcome is separate from the both-channel validation set. |
| [final-default-100k/plan.json](final-default-100k/plan.json), [trial](final-default-100k/trials.jsonl), [summary](final-default-100k/summary.json) | One exact 100 KB file after making both-channel output the wire default, with neither routing flag: 22.796 seconds to delivery, amplitude 0.35, zero corrected/erased bytes, complete physical reception and zero process errors. `fast-info` records `mono:false`. Kept separate from the prespecified 14-trial set. |

The fourteen 100 KB repetitions are a separate validation set at the final
cable amplitude **0.35**; their seed starts at 20260920 and all fourteen source
hashes differ. Screening, the original five raw probes, the three faster codec
trials and the 5 MB run use amplitude **0.5**. Keep these configuration bins
separate: the 5 MB result does not qualify the final amplitude and is not pooled
with the 100 KB confidence calculation. No 50 MB live file was tested, and no
5 MB file at the final amplitude was tested. Analytical probabilities are not
observed successes.
The separate right-only output check failed and must remain visible; it does
not qualify that route and is not pooled with the fourteen both-channel trials.
The final no-routing-flags check passed after changing only the wire profile's
default route to both channels. This uses the same effective route as the
fourteen validation files but is kept as a separate final configuration check.

## Reproduce the production CLI screening

Build the Release `pump` executable using the repository's normal workflow.
The [Python runner](../../../../tools/fast_cable_benchmark.py) uses only the
standard library. By default it prints a plan without opening audio:

```sh
python3 tools/fast_cable_benchmark.py --sizes 100000 5000000 50000000 \
  --candidate 256:7/8:high-rate:62
```

For the six-point physical screen, preserve the archived directory and use a
fresh output path:

```sh
python3 tools/fast_cable_benchmark.py --live --output /tmp/cable-new-screen \
  --sizes 100000 --trials 1 --stereo \
  --candidate 16:3/4:robust:16 --candidate 64:3/4:robust:16 \
  --candidate 256:3/4:robust:16 --candidate 256:7/8:robust:16 \
  --candidate 256:7/8:high-rate:16 --candidate 256:7/8:high-rate:62
```

These commands reproduce the candidate coding/geometry, but the CLI uses the
built binary's profile amplitude. After the amplitude change they transmit at
0.35, while the archived screen was at 0.5. The executable SHA-256 in each plan
identifies the tested build. Use the diagnostic probe's explicit `--amplitude`
when comparing amplitudes; do not present a rebuilt CLI run as the identical
physical configuration of the archived screen.
The archived screening, 5 MB and fourteen-repetition runs used `--stereo`
(the same signal on both output channels). The current wire profile also sends
both channels when no routing flag is supplied; other Fast profiles retain
right-only output. `--mono` explicitly selects the former right-only route,
and conflicts with `--stereo`. The failed old-route test and successful final
default-route test remain separate records.

The archived plan field `stereo` records whether `--stereo` was supplied; it
is **not** an effective-routing field. In particular, `final-default-100k`
has `stereo:false` because no flag was supplied, but its saved `fast-info`
contains `mono:false`, meaning both channels. The current runner instead
records `explicit_stereo`, `explicit_mono` and an `output_routing` object with
requested/effective routing and the evidence source. It passes an explicit
routing override to `fast-info` as well as the live commands. An older binary
without `mono` metadata and without an explicit override is recorded as
unreported, rather than guessed from flag omission.

The runner starts `fast-listen` before `fast-tx`, passes `--device default` in
both directions, and compares saved source/receive SHA-256 after physical
completion. It records the executable hash, full commands, errors, timeout,
file status, measured delivery time and independent production-format airtime
calculation. It stops remaining playback after receiver failure, and rejects
premature completion. There is no automatic simulation fallback.

Fixture bytes use SHAKE-256 with a recorded integer seed and fixed domain;
every repeat changes the seed and candidate order rotates. Production public
bootstrap salts remain random. Source and received fixture files are removed
after comparison unless `--keep-received` retains received files; hashes and
fixture-generation code permit reproduction. Output directories must be new.
The CLI does not expose raw BER, calibrated SNR or ALSA recovery counts.

Use `--sizes 5000000 --trials 1 --candidate 256:7/8:high-rate:62 --stereo` for a fresh
5 MB trial. For explicit long-file validation, `--sizes 50000000 --trials 14`
at that candidate needs more than 28 hours of transmitted audio. Fourteen
independent successes yield a one-sided 95% lower success bound of 80.7364%
for that directly tested size only. Timing and stationarity assumptions still
matter, and selection files should not be pooled into held-out validation.

## Reproduce the raw and codec probes

The [C++ probe](../../../../tools/fast_cable_probe.cpp) uses production DSP/audio
and optionally the existing production codec. It separates capture from DSP
with a bounded FIFO and retains errors/erasures at their original positions.
Build and offline instructions are in [offline-probe-method.md](offline-probe-method.md).
Without `--offline`, the probe opens real playback and capture devices:

```sh
build/fast_cable_probe --mode raw --intervals 512 --apsk 256 \
  --symbol-rate 15000 --carrier 9300 --rolloff 0.2 --sample-rate 48000 \
  --amplitude 0.5 --seed 417 --device default --stereo --quiet > /tmp/cable-new-raw.json

build/fast_cable_probe --mode codec --bytes 100000 --apsk 256 \
  --code-rate 7/8 --rs high-rate --depth 62 \
  --symbol-rate 19200 --carrier 10800 --rolloff 0.1 --sample-rate 48000 \
  --amplitude 0.5 --seed 417 --device default --stereo --quiet > /tmp/cable-new-codec.json
```

The other tighter waveforms use `(symbol-rate, carrier) = (16500, 9400)` and
`(18000, 10200)`, with rolloff 0.1. The bridge probe keeps the baseline waveform
and requests logical rate 44100; archived live negotiation reports 48000 Hz
hardware. The matching offline records contain their reproduction commands.
For the final headroom checks, use `--amplitude 0.35` at the 15,000/0.20
baseline, with logical sample rates 48000 and 44100. Use amplitude 0.30 for the
19,200/0.10 point. All archived headroom probes send both output channels.

Raw `exact=0` means raw bit decisions were not all exact; it does not mean a
coded file failed, because raw mode bypasses FEC. Raw JSON code-rate/RS/depth
fields are retained configuration, not active protection. Codec exactness
requires full original source equality and observed physical completion.
Both modes provide real silence; EOF cannot manufacture completion.
The original three `codec-tight*.json` records predate a diagnostic reporting
fix: their `source_goodput_bps` used signal duration plus six seconds, although
the probe transmitted eight seconds of tail. Preserve those original records;
use `8*source_bytes/(signal_seconds+tail_seconds)` for their playback goodput.
The current probe uses that corrected denominator. This reporting correction
does not change transmitted samples, decoding, exactness or source-byte counts.

Level fields distinguish generated TX samples, pre-transmit capture, received
signal and tail. Their signal/noise variance ratio is fullband and uncalibrated.
TX floats sometimes exceed full scale at amplitude 0.5, and live S16 conversion
clamps those excursions despite the host's 95% playback setting. Offline float
controls omit that clamp. Captured ADC clipping was absent after gain correction.
The selected default lowers only the wire profile amplitude to 0.35; other
profiles retain their prior amplitudes. It reduces signal power 3.10 dB without
changing airtime. Separate amplitude-0.35 captures measured fullband ratios
64.733 dB and 64.637 dB at logical 48 kHz and 44.1 kHz respectively; these are
not inferred from the earlier amplitude-0.5 captures.

## Checks and limits

Nine focused runner tests pass without opening sound devices:

```sh
python3 tests/test_fast_cable_benchmark.py build/pump
```

They check exact probability bounds, no cross-size extrapolation, deterministic
fixtures, process cancellation, premature completion rejection, setup failure
handling and airtime agreement with generated WAVs. They establish benchmark
behavior, including profile-default/explicit routing provenance and conflicting
routing flags, not audio-link reliability. Runtime regression results and any final
default changes belong in the repository validation record.

All statistical claims must retain their denominator and setting. The study
found an increase in raw errors and correction use when tightening the waveform;
it did not observe a whole-file failure cliff within the both-channel sweep or
establish 80% success for 50 MB. The separate right-only file failure remains
part of the evidence. No LDPC, 0.3% RS, sparse marker cadence or shorter marker was qualified
by these measurements.

## Build and regression evidence

All [29 development-contract suites](validation-contract.log) pass in
1,704.77 seconds, covering the unchanged regular short-message, physical-end,
receiver and pending-GUI contracts.

[Focused Fast checks](validation-fast-final.log) retain an initial concurrent
`fast_session` failure. The [isolated repeat](validation-session-final.log) and
[subsequent routing/session/GUI/CLI checks](validation-routing.log) pass without
relaxing assertions or deadlines. The [full SNR matrix](validation-snr.log) and
[nine benchmark checks](validation-benchmark.log) pass. See the
[validation record](../../../validation.md) for scope and the scheduling
uncertainty of the earlier session failure.

The final diagnostic [offline codec sanity check](probe-final-offline.json)
recovers 100,000 exact bytes and observes physical completion. Its goodput
uses the recorded signal duration plus the full eight-second tail; it is
not an additional live trial. New raw-probe JSON records also include the
`stereo` routing flag; historical raw records use the explicit reproduction
commands above as their routing evidence.
