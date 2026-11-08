# Historical Helper conditioning and cable probes

Current implementation and validation rules are in [Robust architecture](robust-current.md)
and [abbreviated testing](triad-performance.md). This note preserves research
ideas and source-bound evidence, not a second implementation or a recipe to
rerun retired campaigns. Historical results do not qualify changed code.

The retired probe studied whether Helper measurements could correct a separate
held-out signal. Simulation used complex I/Q; the physical cable example used
real audio with a diagnostic analytic adapter. Neither was an integrated
message receiver. Bright metrology probes measured correction quality and must
not be presented as weak-signal DATA decoding.

A useful experimental separation was to fit the cable/clock/noise model from
dedicated observations, then evaluate a held-out frequency. One recorded
1600-Hz probe differed from the pre-probe prediction by about 0.00142 dB in gain
and 0.00404 degrees in phase. This single comparison did not validate other
frequencies, times, RF propagation, FHSS or EME.

A measured quiet PSD already contains ADC quantization effects. A stationary
correlated-Gaussian surrogate summarizes a recording; it is not an exact replay
or a complete physical channel. Receiver selection must use samples and public
configuration, not the simulator's configured gain, clock, delay or frequency
truth. Keep diagnostic probe power distinct from total Helper power.

These lessons remain applicable to future bounded integration checks. The
[Helper evidence index](validation-data/helper/README.md) and
[cable research](triad-live-duplex.md) provide context. The standalone probe,
receiver and calibration paths are retired.
