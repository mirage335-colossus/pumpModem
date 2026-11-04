# Preserved sampled-family performance evidence

These are ten completed runs, including failed and inconclusive forecasts,
plus one bounded diagnostic.
Six distinct GPU cases validated a forecast across the separate qualification
attempts: Gaussian state and all five phase variants. The earlier failures remain
preserved below. The direct GPU run passed its paired comparison for all twelve
selected cases; that is separate from validating a cheap forecast. Every run's
`complete_scope_qualification` remains false. The smoke case is a functional
sampled-input case and does not establish coverage of expensive configurations.

| Exact run summary | Run wall time | Preserved outcome |
| --- | ---: | --- |
| [CPU prototype](cpu-family-qual-001/summary.json.gz) | 1,103.510 s | Gaussian and grouped acquisition: direct comparison pass. Reference phase: unqualified. Optimized phase: `ValueError: unstable or nonpositive receiver cost fit`; no completed result file was emitted. |
| [CPU staged](cpu-staged-001/summary.json.gz) | 896.688 s | Gaussian, reference phase and optimized phase: all unqualified. Optimized phase stopped because another full validation no longer fit the deadline; seven full pairs exist, with the last declared confirmation look at five. |
| [GPU direct](gpu-family-qual-002/summary.json.gz) | 645.067 s | Twelve direct comparison passes: six state/phase variants, grouped acquisition, RBPF, CFO batch, static FFT, ray frequency and the sampled smoke case. |
| [GPU staged 001](gpu-staged-001/summary.json.gz) | 211.255 s | Phase graph: validated; nine fresh full confirmation pairs. |
| [GPU staged 002](gpu-staged-002/summary.json.gz) | 394.835 s | Gaussian and optimized phase: validated with five full pairs each. Reference phase, reduction and tensor phase: unqualified after nine full pairs each. |
| [GPU staged reuse](gpu-staged-reuse-001/summary.json.gz) | 121.493 s | Gaussian and optimized phase: historically supported conditional passes. Phase graph: unqualified; its receiver checks did not fit the frozen historical envelope. |
| [GPU stage-aware reuse](gpu-stage-aware-001/summary.json.gz) | 40.721 s | Phase graph: historically supported conditional pass; nine generation and seventeen receiver calibration pairs, followed by one fresh full sentinel pair. |
| [GPU stage-aware qualification](gpu-stage-aware-qual-001/summary.json.gz) | 168.828 s | Independent new qualification: reference phase, reduction and tensor phase all validated. Their generation/receiver/full pair counts were respectively `9/5/3`, `9/17/5`, and `9/17/9`. |
| [GPU six-state routine](gpu-phase-routine-001/summary.json.gz) | 225.037 s | Gaussian, reference phase, optimized phase and reduction: historically supported conditional passes. Graph and tensor phase: unqualified; both failures remain in the archive. |
| [GPU eight-case direct routine](gpu-routine-direct-001/summary.json.gz) | 370.251 s | All eight direct comparisons passed: graph, tensor phase, grouped acquisition, RBPF, CFO batch, static FFT, ray frequency and sampled smoke. |

CPU evidence came from the recorded Ryzen 5 PRO 5650U environment and CPU mask
`2,4`. GPU evidence came from the recorded NVIDIA H200 environment. Exact
library versions, threading, shapes, seeds, commands and input hashes are in
each result's identity and receipts. Run wall times above include orchestration
and are not the per-capture timing estimand.

The validated candidate full-pipeline predictions and their independent
confirmation means were:

| Case and exact result | Forecast | Observed mean | Confirmation relative-error interval |
| --- | ---: | ---: | --- |
| [Phase graph](gpu-staged-001/phase-graph-hour/result.json.gz) | 3.268434 s | 3.273264 s | −0.0816% to +0.3771% |
| [Gaussian state](gpu-staged-002/gaussian-hour/result.json.gz) | 5.509197 s | 5.515259 s | −1.0550% to +1.2751% |
| [Optimized phase](gpu-staged-002/phase-optimized-hour/result.json.gz) | 3.716570 s | 3.711876 s | −0.4961% to +0.2436% |
| [Reference phase, new qualification](gpu-stage-aware-qual-001/phase-reference-hour/result.json.gz) | 3.752837 s | 3.759681 s | −0.5237% to +0.8884% |
| [Reduction phase, new qualification](gpu-stage-aware-qual-001/phase-reduction-hour/result.json.gz) | 3.243817 s | 3.250718 s | +0.0572% to +0.3682% |
| [Tensor phase, new qualification](gpu-stage-aware-qual-001/phase-tensor-hour/result.json.gz) | 3.242256 s | 3.255248 s | +0.2164% to +0.5851% |

The archived policy uses the declared confirmation looks `3,5,9,17`, a ±5%
equivalence band and 97.5% two-sided intervals at each look. Qualification
requires every generation, receiver and pipeline check for reference,
candidate and their paired ratio to pass. Pipeline agreement alone did not
qualify the three unsuccessful cases in `gpu-staged-002`: their receiver checks
were inconclusive. Their later independent qualification is a separate record.
The tables do not establish future single-job interval coverage, cold-start
latency, receiver sensitivity, false-admission rates or physical-device results.

The full-pipeline boundary is fresh native sampled export through a complete
receiver request and IPC in an initialized persistent worker. Worker startup
and import setup are reported separately. Staged calibration estimates native
generation from shorter physical exports and receiver work from its declared
calibration method. All complete confirmation captures retain their full
public calendar and receiver work. Hashing and evidence writes outside the
timed request are separately identified. Nested receiver stage timers must
not be added to their enclosing timers.

The phase-graph run also retained a [fresh campaign](gpu-staged-001/phase-graph-hour/campaigns/fresh-observed.json.gz):
19 independent jobs took 62.268968 s versus 62.100255 s predicted (+0.272%).
Its [cached campaign](gpu-staged-001/phase-graph-hour/campaigns/cached-observed.json.gz)
replayed one retained observation with 172 fresh receiver states: receiver time
was 57.527580 s versus 60.158406 s predicted (−4.373%). The complete cached batch
took 60.456097 s, but the receiver-only 60-second minimum was **not reached**.
Cached replay is not 172 independent receptions; future coverage was not
qualified.

The subsequent [reuse observation](gpu-staged-reuse-001/summary.json.gz) retained
its original unsuccessful graph result. Its pipeline check fit the historical
envelope, while the paired receiver residual log interval was
`[-0.123011, 0.119973]`, outside the allowance `±log(1.1)`. This was inconclusive,
not a demonstrated regression. The two conditional passes do not create new
forecast-validation evidence. These records bind to the archived
`triad_perf_staged.py` hash beginning `b11e2d8d`; a later stopping-rule change
does not retroactively change them.

The appended stage-aware runs use the separate driver hash
`9f3d660016bc56dd6e0c240239fb0bc2587b759cd8ce1eafbd12fac9ddbe2237`.
They require generation and receiver calibration precision separately before
freezing a forecast. The graph reuse record reports a narrow stopping-policy-only
migration from `b11`, an unchanged measurement-core hash
`6378161e650b8cfbe427a283d231c137085b3f47be62729f9d25384c26110b6f`,
and a required fresh full endpoint. Its passing sentinel is conditional reuse,
not independent qualification. The other three variants instead received new
full qualification; their original inconclusive records were not edited.
The new run walls do not establish comparable savings against the earlier
attempts, which used different scopes and observation counts. Full-scope
qualification remains false.

The subsequent six-state routine did not qualify all cached forecasts. Graph
reached its calibration precision target but its reference receiver sentinel
remained inconclusive. Tensor phase did not reach the receiver calibration
precision target, and its candidate and paired receiver sentinel checks were
inconclusive. Both pipeline sentinel checks fit the historical envelope; that
was insufficient to pass the required receiver checks.

The later eight-case direct routine measured complete fresh work and passed all
eight comparisons, including graph and tensor phase. Those direct results do
not retroactively qualify the failed cached forecasts. The **225.037-second
and 370.251-second observations are separate runs**; their sum is not one
measured combined routine or a comparable savings estimate. Neither report
claims complete-scope qualification or cold-latency qualification.

The [crossed CPU diagnostic](crossed-diagnostic/cpu-cross-001/result.json.gz)
ran six balanced blocks in 44.855 seconds using the
[frozen script](crossed-diagnostic/crossed.py.gz) and
[plan](crossed-diagnostic/cpu-cross-001/plan.json.gz). It crossed retained/fresh
input with call order after generation and checked identical sampled-array
hashes. The descriptive balanced fresh-minus-cached request-time difference
was +0.0388 s, with mixed signs across blocks; the following-call difference
was +0.1332 s. Six descriptive blocks do not establish an input effect or an
environmental cause for the CPU forecast variance. Raw block receipts and setup
are retained; no qualification claim was made.

Exact reusable anchors are retained under
[GPU staged 001](gpu-staged-001/staged-anchors/phase-graph-hour.staged-anchor.json.gz)
and GPU staged 002 for
[Gaussian](gpu-staged-002/staged-anchors/gaussian-hour.staged-anchor.json.gz),
[optimized phase](gpu-staged-002/staged-anchors/phase-optimized-hour.staged-anchor.json.gz)
and [phase graph](gpu-staged-002/staged-anchors/phase-graph-hour.staged-anchor.json.gz).
Their identities and applicability checks still govern reuse; archival does
not grant qualification to a changed source, binary, device or workload.
The newly qualified anchors are retained separately for
[reference phase](gpu-stage-aware-qual-001/stage-aware-anchors/phase-reference-hour.staged-anchor.json.gz),
[reduction](gpu-stage-aware-qual-001/stage-aware-anchors/phase-reduction-hour.staged-anchor.json.gz)
and [tensor phase](gpu-stage-aware-qual-001/stage-aware-anchors/phase-tensor-hour.staged-anchor.json.gz).

`*.json.gz` files decompress to the exact original bytes. Compression has no
filename or timestamp field. [preservation.json](preservation.json) indexes
those bytes and the standalone receipts omitted only because they reconstruct
**byte-for-byte** from an archived result. This avoids another copy of shared
generation calibration and full/receiver receipts; exact original results
still contain their original embedded data. Per-group cached-generation
receipts are retained once per run. Physical arrays, temporary capture trees
and executable binaries are excluded.

[matching-python-source.tar.gz](matching-python-source.tar.gz) contains all 25
recorded receiver, adapter and measurement source files, each verified against
the hashes in the results. [source-index.json](source-index.json) maps each
source member to the records that require those exact bytes. That original snapshot,
including `b11`, remains unchanged. The appended stage-aware records use the
separate [source snapshot](matching-python-source-stage-aware.tar.gz) and
[source index](source-index-stage-aware.json), with fourteen matching members
including the `9f3d6600` driver. The deterministic tars use
zero timestamps and owner IDs. These snapshots are evidence, not a replacement
for maintained source files.
The two routine runs reuse these same byte-verified source snapshots; their
additional record bindings are in the source indexes. Routine input anchors
that exactly match already archived anchors are recorded as source aliases in
the preservation index, without another copy of the anchor data.

[Native build provenance](reference-build-provenance.json) identifies the
reference commit `242ea15fa032ee117b9113a7f3cd07cc6e9f0254`, compiler options and
both binary hashes. The prior [trace reproduction record](../../2026-11-03-performance-trace/README.md#reproduction-and-further-development)
and its [raw proof archive](../../2026-11-03-performance-trace/raw-proof.tar.gz)
preserve the source base and overlays for that build. This archive does not
claim that the current checkout rebuilt those native binaries.

Run `python3 verify.py` in this directory to check [SHA256SUMS](SHA256SUMS),
decompressed bytes, exact receipt reconstruction, source bindings and recorded
native binary identities. This verification runs no simulations or benchmarks.
