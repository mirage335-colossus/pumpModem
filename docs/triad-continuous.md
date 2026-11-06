# Continuous three-carrier transmission and reception

The isolated v3 study now has a bounded pull interface for long transmissions
and an incremental likelihood receiver. The implementation removes the
3,600-second duration check from native source generation. It does not make
every long file export, blind search, physical receiver, or duration forecast
work automatically. The [sampled and cable qualification](validation-data/triad-v3/2026-11-06-continuous/README.md)
demonstrates bounded fixed-bank operation, with the remaining limits below.

## What streams

[`tools/triad-helper-export.cpp`](../tools/triad-helper-export.cpp) accepts
`--stream-server --stream-kind emitted|channel`. It prints a shaping plan and a
fixed stream descriptor before servicing bounded requests. `next COUNT` returns
one contiguous chunk and validity bytes; `basis FIRST COUNT N` produces a
bounded group of keyed DATA templates at public candidate coordinates. The
source and selected channel retain their phase, keyed tile, oscillator,
filter, delay and tail state across calls. Requests use checked integer sample
coordinates. Templates use separate cached `PatternSource` instances and do not
advance the transmitted source. The 64 MiB service workspace and per-request
row/column limits are enforced by the native process.

`--stream-kind emitted` is the clean DATA/Helper transmit waveform; it does not
add channel noise. `channel` runs the complete emitted waveform through the
selected continuous sampled channel and returns its capture and tails. Do not
use a channel-simulated observation as the DAC transmit signal.

[`tools/triad_incremental_receiver.py`](../tools/triad_incremental_receiver.py)
keeps bounded input-block and Gaussian channel-likelihood state. It accumulates
evidence for a **fixed public candidate bank from the first block**. The caller
may decide only at the declared observation endpoint; the component itself
never admits a bit. Incomplete input cannot pass. It
does not retain a multi-day observation or template array. The candidate basis
and all declared channel models must be decided before looking at reception
scores; prefix scores are diagnostics, not extra admission attempts.

[`tools/triad_continuous.py`](../tools/triad_continuous.py) connects this source
to the incremental receiver and offers simulation, native audio playback, and
native audio capture modes. Audio is transferred in bounded PCM chunks through
the shared backend. Playback can use the final full-audio-rate background-noise
options. Capture requires a conservative declared complex noise variance and a
fixed analytic-sideband projection bank. No frequency or bit decision is made
from capture truth.

## Duration and power limits

There is no general 3,600-second cap on source or channel duration. A plan-only
query can check long integer geometry without generating samples. File exports
remain subject to the explicit 2 GiB artifact limit and other geometry/sample
constraints. The exact `whole-active` DATA calibration still has a finite
4,194,304-source-sample work budget. It is not a way to normalize a multi-day
signal by precomputing the complete bit.

Streaming long transmissions can use `--power-reference bounded-tiles`. This
samples a bounded set of keyed tiles, estimates one DATA gain before playback,
and keeps that gain fixed for the entire transmission. It does not normalize
each chunk. The estimate is not an exact whole-bit power integral or a
confidence bound: keyed tiles are distinct and a long transmission can differ
from the calibration tiles. The native receipt reports the tile sample budget
and between-tile variation. The Helper gain and waveform stay fixed. Inspect
the calibrated power and clipping reports for the actual rate, spreading,
burst, and FHSS configuration.

An automatic forecast is not proof that a corresponding transmission can be
received. The forecast selects an energy allocation under its declared channel
and search model. The continuous receiver separately needs a feasible fixed
bank, sufficient memory, adequate real-time throughput, and a suitable noise
model. Long search grids can be far larger than this bounded receiver accepts;
do not prune them using simulator truth.

## Receiver scope and admission

The first incremental Gaussian receiver is a scheduled-bank experiment. It
does not yet implement general blind multi-day acquisition or continuous
multi-day clock, frequency, and Helper tracking. A pilot estimate from the
38-second lead alone is not a validated frequency bound for a bit lasting days.
The caller must supply the public timing, frequency, clock, epoch, delay/Doppler
and power hypotheses. Every used hypothesis retains its original fixed-bank
prior weight. The `conditional_model_pass` result is conditional on that bank
and the chosen Gaussian channel evolution model. It does not qualify Helper or
physical reception as an operational admission.

With Helper active, DATA-only alternatives use the same fixed DFT projection
for observations and templates, with an explicitly declared DATA passband and
excluded Helper band. Finite-block leakage and channel sidebands remain. A
zero-signal null does not automatically cover a strong residual Helper tone,
non-Gaussian audio, clipping, or unmodeled noise covariance. Reports therefore
do not qualify the physical false-admission rate. A missing or invalid block
must be represented as unobserved rows; it must not be filled with observed
zeros to preserve a white-noise assumption. The adapter only enables conditional
Gaussian admissions when the native descriptor explicitly supports that null;
Helper, physical capture and the additional diagnostic I/Q noise path remain
unqualified. Missing capability metadata fails closed.
Simulation also rejects a receiver noise-variance override smaller than the
known native thermal variance; a smaller assumed noise floor would invalidate
the conditional false-admission argument.
The raw-row projection supports arbitrary erasures. The DFT projection supports
whole missing blocks; a partially missing DFT block is explicitly rejected until
its colored-noise covariance is implemented. GPU execution of this incremental
adapter has not been qualified.

## Basic command shape

Use a fresh report path. The fixed-bank JSON uses one row per public
`[epoch offset seconds, arrival seconds, residual frequency Hz, clock ppm]`
coordinate, optional `[delay seconds, Doppler Hz]` taps, and (with Helper) fixed
projection bands. Arrival is an additional offset from the nominal source
calendar; the adapter adds the public DATA offset and simulated capture lead.
For hardware capture, include the public analog-latency interval in the
bank.

For example, this bank declares a single known alignment and one static tap:

```json
{"coordinates": [[0, 0, 0, 0]], "taps": [[0, 0]], "stationary_power": [1]}
```

Save it as `local-evidence/triad-bank.json`. A corresponding long diagnostic is:

```sh
python tools/triad_continuous.py --mode simulate \
  --bank local-evidence/triad-bank.json \
  --report local-evidence/triad-continuous.json \
  --coherences inf --block-seconds 1 \
  -- --case hf --sample-rate 6000 --seconds 86400 --power-reference bounded-tiles \
     --rate 36 --added-symbols 10 --helper off --cn0 -16 \
     --base-oscillator gpsdo-ocxo --shift-oscillator gpsdo-ocxo \
     --oscillator-effects off --arrival-seconds 0 \
     --spread-preset none --selective-perturbation off --key-seed 72001
```

This deliberately simple alignment/channel is a throughput diagnostic, not a
bank for unknown radio conditions. Do not run a full day as a routine regression:
the qualified two-hour run and inexpensive equivalence guards cover this path.

For hardware, select `--mode transmit` or `--mode receive`; capture requires
`--noise-variance`. Use `--limit-seconds` only for a prefix diagnostic: the
report remains incomplete and cannot admit the bit. Do not put the operational
key on a command line. Native streaming requires either a raw 32-byte key file
or an explicit `--key-seed`, which is for reproducible test fixtures only. If
no epoch is supplied, streaming binds the keyed source to the system clock at
session creation and keeps that epoch for the complete bit.

The finite exporter retains its existing fixture and output workflow. For a
multi-day plan, streaming is the appropriate route because it does not need to
write a duration-sized capture or template bank.

## Qualification and remaining limits

On the Ryzen 5 PRO 5650U, a complete 7,265-second observation at 6 kHz took
171.6 seconds wall time and 216.4 combined CPU seconds, including preparation.
An actual 39.2-day reciprocal-rate sampled capture matched the batch likelihood
exactly in FP64 and within 7.75e-6 log-evidence units in FP32. A short independent
cable check completed all frames with strict discontinuity reporting and
selected the correct bit. See the [evidence and cheap regression commands](validation-data/triad-v3/2026-11-06-continuous/README.md).

The source and receiver have bounded-memory APIs, but real-time operation is a
measured property of a complete configuration on a named CPU and audio device.
For each claim, measure setup, sustained processing CPU seconds per received
second, wall backlog, peak memory, emitted/captured sample counts, missed audio
frames, and end-of-stream status. Test both short equivalence to the finite
path and actual long sample coordinates with channel state preserved. A sampled
multi-day simulation does not establish multi-day radio reliability or device
uptime. The current record does not yet qualify full-rate multi-day hardware
operation, blind acquisition/tracking, 99% success, or an operational physical
false-admission rate. Consult the [duration forecast guide](triad-duration-planning.md)
and [v3 study](triad-v3.md) for their separate assumptions and historical
measurements.
