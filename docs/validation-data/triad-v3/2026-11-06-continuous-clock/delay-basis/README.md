# Fixed delay-basis alternative

Follow-up to the parent receiver-pair experiment; see its README for waveform,
noise, rate-controller, physical-null and map-qualification limits. The original
results are retained. `protocol.json` was frozen before processing these outputs.

One public arrival at−30ms and13 delayed replicas at0..60ms in5ms steps represent
an effective±30ms relative-delay envelope. Each tap has prior power1/13; the
receiver marginalizes the complete13-dimensional Gaussian state and the declared
0.1/0.5/1s coherence alternatives. Block length remains0.5s. No actual clock
correction trajectory is supplied to the receiver. This envelope is limited to
this110s diagnostic; it does not cover arbitrary duration at±1000ppm.

| Seed | Stable log-e | Corrected log-e | Decision |
|---|---:|---:|---|
|94002(previous failure)|159.700|16.976|Correct bit, still below22|
|94003(fresh validation)|155.644|35.668|Correct bit, above22|

The earlier single-delay bank gave6.664 corrected log-e on seed94002. Thus the
extra delay basis helps that case, but it does not reliably remove the failure.
It also reduces stable-channel evidence because additional channel dimensions
and a broader prior have a cost. No log-e ratio is interpreted as a sensitivity
loss in dB. The changing carrier phase within each0.5s block remains an unmodeled
mechanism; the present comparison alone does not uniquely attribute residual
loss to it.

All four raw bit choices are correct. One of two corrected diagnostics misses.
There are only two seeds, one already seen during initial diagnosis; no false
admission, success-probability or sensitivity-bound qualification follows.
Operational admissions remain false. No production changes or additional search
were performed.

Campaign wall time40.816s; per-receiver CPU ranged2.430–3.131s for109s input. Raw results retain exact counts and counters.
