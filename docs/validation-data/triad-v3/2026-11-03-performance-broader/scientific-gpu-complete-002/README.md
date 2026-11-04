# GPU complete routine 002 — incomplete

This immutable run finished in **1170.758003 s** with overall status
**incomplete**, no recorded execution errors, and no confirmed performance
regressions. Later diagnostics or route changes do not revise this result.

| Component | Recorded outcome |
| --- | --- |
| Functional checks | All 17 required named commands passed. |
| Coverage | Historical native/Python receipts matched declared sources and dependencies; not fresh or exhaustive branch coverage. |
| Sampled families | All 12 complete direct family measurements passed; child wall 336.679737 s. |
| Core native/FFT | Child wall 213.984238 s. Broad-HF and free-crystal FFT model guards remained unqualified. Native FHSS comparison remained inconclusive. |
| Planner extension | Passed; 25.361959 s, six native bundles and two retained-workspace design-bound jobs. |
| Resource extension | Passed direct measurements; 8.155103 s, native CPU execution only. |
| Lifecycle extension | Passed; 508.491737 s, cold latency and the declared fixed mixed campaign qualified. |

The native FHSS expanded ratio interval was
`[1.2852248297332862, 1.3068551011650873]`, crossing the normal profile's 1.30
decision threshold. Its result is inconclusive, not a confirmed regression.
The two unqualified FFT guards and this unresolved comparison prevent complete
warm-workload qualification. Legacy timing-width diagnostic misses remain in the
raw record. Hardware observations are screening evidence without a confidence
qualified health claim. Child durations are reported with their own boundaries;
they are not a replacement for the measured complete-run wall time.
The FFT guard results must not be interpreted as measured forecast-residual
failures. Later applicability metadata is under separate review.

`preservation.json` maps original results, forecasts, receipts, logs, exact outer
invocation, and build metadata to compact gzip payloads or embedded JSON values.
Reconstruction restores every original byte count and SHA256. Identical and
embedded receipts are retained once. Physical arrays and executables are omitted;
no new raw-array verification is claimed. Original input, work, seed, and command
receipts remain available.

`source-index.json.gz` binds 171 exact source/configuration versions at 395
recorded source identities. The measured complete wrapper is `6f90e82a`, the main
driver is `2f9378ff`, and lifecycle is `35e543ac`. This is the declared source
closure, not an inferred transitive compiler/import closure. The original
coverage receipts and qualification anchors are retained in the earlier bundles
and linked by digest in `external-inputs.json`.

Original native build provenance applies to the unchanged `triad_long`
executables. It records an earlier planner executable (`6f5f5a4b`), while this
run used `adfa35b5` for both planner sides. The final planner source overlay and
measured binary/source identities are retained; the older planner build receipt
is not presented as provenance for `adfa35b5`. The final compile/link logs are
included under `post-run/`; they were retrieved after the run and contain no
executable digest. The measured report independently records the final binary
identity.

`post-run/preservation.json` also appends a separate successful lifecycle proof
replay, taking 0.432798 s. Its source transfer is from protocol `35e543ac` to
`5dfa0492`, with current main `f59f02a9`, wrapper `fcea5f43`, and lifecycle manifest
`eee47f23`. The exact observed replay program and four source versions are
retained. This is validation of the retained raw lifecycle proof under the
reviewed transfer rule; it is not a fresh receiver measurement or complete-suite
rerun and does not revise the original incomplete outcome.

`post-run-final/` retains the later 0.479253 s accepted proof replay under lifecycle
`93de7a47` and main `afd35925`, with its exact observed program and source files.
It verifies original lifecycle report bytes `161fae4b` in the unchanged normal
context, passes cold/mixed validation, and records no stability flag. It executes
no receiver kernels and does not add a new complete-suite measurement.

All archive work used CPUs 6 and 8; no receiver or benchmark was rerun. Use
`python3 verify.py` for integrity and source-binding checks.
