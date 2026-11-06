# Bounded system-clock step following

The isolated continuous transmitter has an **opt-in, bounded slow-DATA path**:
`--follow-system-clock bounded-data`. It follows detected wall-clock steps by
smoothly changing the DATA pattern coordinate, before the final audio-center
oscillator. This is useful limited support, **not general multi-day UTC or
ADC/DAC clock discipline**. Ordinary Robust and the shared strict audio timing
contracts are unchanged. The default remains `off`.

Use the same public profile on an independent continuous receiver. Reception
keeps the physical ADC sample timeline and nominal affine templates; it receives
no private transmitter clock trace. Local wall-clock changes do not discard
samples, restart evidence or change the active key epoch. Future native sessions
already read the corrected system clock when `--now` is used.

## Operating envelope

- Independent APSK v2 DATA, **fine-symbol rate at most 0.001 Hz**, including
  Added Symbols. For example Rate 0.0036 / Added 1 has fine rate 0.0006 Hz;
  Rate 0.000006 / Added 1000 has fine rate 0.001 Hz. The GUI inner rate alone
  is not the eligibility test.
- No Helper, FHSS or Symbol Burst/whole-bit calendar. Faster gates can dominate
  timing sensitivity even when the pattern rate is low. Unsupported choices
  are rejected, rather than silently changing their waveform.
- **Total displacement from the original affine map is bounded to ±2 seconds**.
  Repeated updates do not renew that allowance. A larger net correction or an
  unavailable clock association emits a status notice and preserves the current
  continuous trajectory. The final receipt retains counts and status.
- Source rate remains between 0.9997 and 1.0003 of nominal, with acceleration
  at most 0.2 ppm/s. One +2-second change settles in 12,500 seconds (3.47 hours).
  A correction arriving during a transition replaces one queued target;
  it does not interrupt the current transition or jump its derivatives.
- The original pattern epoch, bit, keyed counters, interleave and coefficient
  values remain fixed. The final 1725/1775-Hz or other DATA-center NCO remains
  on physical sample time. Helper samples are never modified by this path.
- A public two-second endpoint allowance prevents a negative correction from
  truncating the source/tails. No bit energy is added to compensate for loss.
  The low-rate source retains its complete nominal padding, which can itself
  be long; this feature does not remove that existing source-support cost.

These eligibility guards are conservative engineering scope, not a theorem
covering every channel, codeword or possible correction history. The current
[sampled evidence](validation-data/triad-v3/2026-11-06-clock-follow/README.md)
measures positive/negative corrections, an independent rate-boundary case and
×1000 spreading. Its largest measured template loss is 0.000706 dB. It does not
qualify physical multi-day uptime or the whole configuration cross-product.

## Why the existing audio flag is insufficient

The shared `UtcTimeline` deliberately rejects wall-clock steps. Native audio
following also enforces a fixed affine-source corridor of `.01 / sample_rate`
seconds (1.67 microseconds at 6 kHz). Merely relaxing the wall-step check would
still violate that corridor and the receiver's constant-clock assumptions.
Capture timing observation dates PCM; it does not undo a transmitter's unknown
nonlinear timing change. Keep those contracts intact.

Resampling the entire audio waveform would also move the DATA center and pilot
frequencies: 300 ppm at 1725 Hz is 0.5175 Hz. The isolated source map avoids
that additional IF excursion. Its relative carrier frequencies necessarily
follow the very small source-rate adjustment.

`ControlClock` observes bracketed wall time and Python monotonic time every
250 ms at most. It detects discontinuous wall-minus-monotonic changes.
**Normal NTP slewing can be invisible on Linux**, because both clocks receive
frequency adjustments. It also does not measure the DAC clock or compensate
arbitrary audio buffering. See the [Linux clock documentation](https://man7.org/linux/man-pages/man3/clock_gettime.3.html),
[Chrony step/slew controls](https://chrony-project.org/doc/4.7/chrony.conf.html),
and [NTP discipline description](https://www.ntp.org/documentation/4.2.8-series/discipline/).
Changing to MONOTONIC_RAW alone would not solve this: a normal 20-ppm rate
difference accumulates 1.728 seconds/day and would consume this fixed envelope.
General support needs an explicitly dated audio-frame/UTC rate model.

## Timing law and energy

For a settled offset `a`, new target `b`, transition start `t0`, and duration `D`,
the map is `u(t) = t + a + (b-a) q((t-t0)/D)`, with
`q(x) = 10x^3 - 15x^4 + 6x^5` between zero and one. Outside that interval
the offset is constant. The implementation uses relative long-double time and
the original integer sample index plus fractional offset; no large floating
UTC epoch is subtracted from samples. Position, rate and acceleration are
continuous. The maximum derivatives are `max q' = 15/8` and
`max |q''| = 10/sqrt(3)`, giving the selected minimum transition duration.

With complete source support and `0.9997 <= du/dt <= 1.0003`, change of variables
bounds the *continuous-time source energy* ratio by
`1/1.0003 <= E_followed/E_original <= 1/0.9997`, about ±0.00131 dB.
This bounds time-reparameterization energy only, not sampled quadrature error,
propagation, noise, partial captures or receiver mismatch. Actual waveform
overlap is measured separately, without renormalizing away energy changes.

## Use and cheap checks

Add `--follow-system-clock bounded-data` before the native `--` separator in
[`triad_continuous.py`](../tools/triad_continuous.py) for both transmit and
receive. TX observes local clock steps. RX selects the identical public
calendar but does not steer observations or templates from local clock steps.
Keep the same key/epoch, rate, power and public bank on both ends. Physical RX
still needs its declared conservative noise variance and projection bank.
This does not supply blind acquisition or qualify the physical null.

At native level, `triad_helper_export --stream-server --stream-kind emitted
--clock-follow-bounded` enables the public profile. `clock TARGET_SECONDS`
sets an absolute target within the original ±2-second envelope; replies include
acceptance, current offset/rate and the first unrendered sample. The `basis`
command remains nominal. Bounded-tiles power preparation and fixed gain are
reused. The ordinary source protocol without this option remains unchanged.

Routine checks, after `./build.sh configure --cli` and a focused build:

```sh
CCACHE_DISABLE=1 cmake --build build/dev-cli --parallel 2 --target \
  test_triad_clock_follow triad_pattern_probe triad_helper_export
ctest --test-dir build/dev-cli --output-on-failure \
  -R '^(triad_clock_follow|triad_control_clock|triad_pattern_probe|triad_stream_source)$'
OPENBLAS_NUM_THREADS=1 python tests/test_triad_continuous.py
OPENBLAS_NUM_THREADS=1 python tests/test_triad_clock_diagnostic.py
```

The numerical Python checks need NumPy/SciPy; the optional maintained-receiver
comparison additionally needs PyTorch. The native clock test is header-only and
does not rebuild the ordinary receiver. For new correction policies, use the
native probe's `--clock-correction-seconds`, `--clock-correction-at-seconds` and
optional duration, then `triad_clock_diagnostic.py` on paired saved probes.
Do not run a days-long cable experiment or repeat historical GPU campaigns just
to check this mapping. The report contains the compact reproducible escalation
to the maintained incremental receiver.

## Remaining work

At Rate 36 / Added 10, two seconds spans 120 fine symbols. The sampled HF
diagnostic shows severe loss for an unchanged timing template, but a local
affine timing/rate fit reduces it to about 0.249 dB over a 792-second window.
That fit was initialized with the known trajectory: it is a representational
benchmark, not blind acquisition. Its approximately 288.55-ppm software rate
can be outside a GPSDO-derived oscillator grid. This suggests reusing short
affine timing fits and continuous timing evidence before adding a complicated
new receiver. A single full-bit affine grid does not represent the whole
piecewise correction, and a local GPS update does not reveal the remote trace.

General cumulative drift, NTP slews, Helper-aided timing tracking, gated
waveforms and ordinary/high-rate correction therefore remain unsupported by
this option. They require their own realistic trajectory inference and
admission accounting. Neither metadata following nor an oracle warped basis
is sufficient evidence to enable them.
