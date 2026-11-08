# Robust computational checks

Robust's maintained statistical timing tool is
[`tools/triad_abbreviated.py`](../tools/triad_abbreviated.py), with focused tests
in `tests/test_triad_abbreviated.py`. It times small units of retained canonical
kernels and estimates a repeated-unit cost for the same fixed geometry. It does
not estimate end-to-end receiver latency, physical reception probability, or
radio qualification.

Use the tool only for a declared computational question. Before running it,
record the source revision and dirty state, build/configuration, device, kernel
and fixed geometry, covered path strata, target unit count, output location,
uncertainty decision, and what the result can decide. Keep cheap deterministic
correctness checks separate from performance measurements.

Treat the JSON output as temporary evidence for that question, not a growing
benchmark archive. Retain only the current compact result while it supports an
active decision or validation claim; record reusable conclusions, negative
findings, assumptions and provenance in maintained documentation, then remove
raw and superseded outputs. Keep any unfinished comparison only with a stated
purpose, next action and cleanup trigger.

The harness measures four retained kernels: FFTScreen preparation and scoring
separately, GaussianStateFilter update, and SparseTimingForward. Its fixed
hierarchy uses 3, 5, then 9 outer trials, with 1- and 2-unit training
measurements and a 4-unit holdout. The maximum is 252 kernel measurements.
`--target-units` sets only the extrapolation target; it does not increase work.
Run the least stage that supports the predeclared decision and proceed only when
the predeclared uncertainty criterion requires it. The holdout checks the stated
fixed-geometry repeated-unit assumption. The estimate uses the harness's
conditional log-t model; it does not provide physical or general sampling-bound
qualification. It does not validate extrapolation
across different algorithms, memory or concurrency regimes, sample rates,
search sizes, configurations, devices, receiver stages, or physical channels.
Do not generalize its estimate beyond the measured kernel and declared units.

First run the inexpensive assertions:

```sh
python3 -m unittest discover -s tests -p test_triad_abbreviated.py
```

Example measurement:

```sh
python3 tools/triad_abbreviated.py \
  --output local-evidence/triad-abbreviated.json \
  --device cpu --target-units 1000
```

For a matched comparison, provide `--baseline previous.json` from an active,
compatible comparison and remove it when that comparison closes. Keep candidate
and baseline source, configuration, inputs, device and execution conditions
matched; record their provenance in the output. RunPod may accelerate this same
abbreviated process, but does not authorize more computation.

Do not run or retain full-observation, sustained, multi-day, exhaustive-sweep,
or whole heavy-calibration campaigns as routine checks or as a fallback for an
inconclusive abbreviated result. Resolve uncertainty with the next predeclared
small stage, a cheaper discriminating check, or report the unresolved result.
Historical anchors apply only when source, configuration, inputs and measured
computational regime match; they do not qualify changed code. Never describe an
extrapolated estimate as a direct end-to-end or physical measurement.
