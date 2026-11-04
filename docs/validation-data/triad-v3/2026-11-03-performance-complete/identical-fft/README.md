# Identical-source FFT qualification evidence

These are source-bound, independent full-bank checks of the single-stream FFT
predictor. Both selected CPU and GPU forecasts qualified against their held-out
full workloads. This archive does **not** establish an absolute regression pass,
a complete suite pass, or reliable performance for every free-crystal run.
The CPU training interval narrowly missed its 5% precision target.

| Observation | CPU `cpu-free-001` | GPU `gpu-free-002` |
|---|---:|---:|
| Hardware | Ryzen 5 PRO 5650U; affinity CPUs 2,4; two threads per worker | NVIDIA H200; Xeon 8568Y+ host; two threads per worker |
| Arithmetic | FP32 bulk, declared higher-precision preparation/reductions | FP32 bulk, declared higher-precision preparation/reductions |
| Training observations | 33 | 5 |
| Frozen full preparation-plus-score prediction | 20.2643 s | 15.0761 s |
| Independent full observations | 5 | 3 |
| Observed full range | 19.4918–20.4912 s | 15.0453–15.0542 s |
| Observed full geometric mean | 19.9177 s | 15.0491 s |
| Timing mean interval | 19.2435–21.3392 s | 14.9652–15.1878 s |
| Timing relative half-width | 5.3046%; target not reached | 0.7407%; target reached |
| Conditional model mean-error interval | −4.8870% to +1.5729% | −0.2868% to −0.0705% |
| Measured training processing total | 85.7058 s | 9.6615 s |
| Measured independent full processing total | 99.6062 s | 45.1474 s |
| Recorded case wall time | 194.9333 s | 64.9570 s |
| Absolute historical comparison | Inconclusive; no compatible prior absolute anchor supplied | Inconclusive; no compatible prior absolute anchor supplied |

The timing interval uses the frozen 14-family, four-look policy and its
99.970238% per-interval confidence. The model interval uses 97.5% confidence per
predeclared holdout look, with the recorded four-look correction for this one
error family. They are different outcomes. The model statement is conditional
on a frozen forecast and independent stationary normal log runtimes; it is not
a proven future-run or simultaneous cross-case guarantee. The observed CPU
sample-limit stop remains in the raw record; it was not rounded into a 5% pass.

The same workload uses 23,876 complex samples, FFT length 47,916, 458 timing
positions and 992 CFO alternatives: 454,336 completed likelihood groups for a
full score. Training uses CFO banks of 1 and 124, preserving the full sampled
observation, FFT geometry and preparation. Rank-histogram totals witness actual
completed groups, in addition to nominal geometry. A nonnegative affine model
extends only the CFO count, by at most 8×. The forecast file precedes independent
full validation; the raw observation log retains every completed preparation,
score, measurement and timing look. The sampled case is an eight-second,
360-Hz-rate, 500-kHz-shift free-crystal/CB-SSB case with ×10 Added Symbols,
HF-moderate spread and Fast 10% selective modulation. Eight simulated seconds
are distinct from these measured processing times.

The predicted target is the unchanged `ReceiverService.prepare.wall_seconds`
plus `score.wall_seconds_including_finish`. It includes loading and preparation
performed by those calls, the receiver's declared CPU/GPU work, transfers and
finishing within the wrapped score. Fixture generation, process startup,
arithmetic qualification, identity hashing and evidence writing are not part of
that target. The recorded case wall starts inside `measure_identical_fft`; it
therefore includes that function's identity probes/audits and evidence overhead,
but is **not** a separately measured whole controller-process wall time. These
qualification campaigns are not routine-run timing or a separate sustained-run
qualification. No cross-architecture forecast is inferred from the CPU/GPU
comparison.

The implementation comparison is explicitly `implementation_unchanged`, with
no measured ratio or invented zero-width interval. Both checkouts loaded the
same observed six DSP module hashes and the same six pinned measurement files.
The source archive preserves those exact files, including worker SHA256
`173cd0c74731b2507f18bd28360f0b4f3f69015039a1e93752d043f454041cfb`.
The full records retain actual numerical-module and native executable-mapping
hashes, versions, thread/arithmetic controls, hardware, input hashes and geometry.
Library binaries are not copied. This is observed DSP/numerical/native closure
provenance, not exhaustive attestation of all Python stdlib and third-party
source. Changed helpers, injected controls, inputs, arithmetic or work geometry
disable this shortcut; native generation is not deduplicated by these results.

`cpu-probe-001` preserves the earlier audit setup failure, which incorrectly
treated PyTorch's dynamic `torch.ops.__file__ == "_ops.py"` as a repository file.
`namespace-audit-fix-001` preserves a real worker audit succeeding in 10.3554 s:
six repository modules, 932 numerical entries and 83 executable mappings.
The fix recognizes only the exact `torch.ops`/`_Ops` and
`torch.classes`/`_Classes` object/class pairs, hashes their actual implementation
sources, retains native mappings and reports the dynamic namespaces explicitly.
An ordinary missing application source still fails. The failed probe did not
finish its runtime audit and supplies no qualified model anchor.

`gpu-setup-failure-001` separately retains the initial upload/setup failure:
`tools/triad-long.cpp` was missing before fixture provenance could be checked.
Its case list is empty; it is not a receiver result. After restoring the required
source input, `gpu-free-002` produced the independently qualified result above.

To verify preservation without running a receiver or importing numerical libraries:

```sh
python3 docs/validation-data/triad-v3/2026-11-03-performance-complete/identical-fft/verify.py
```

With a compatible numerical Python environment available, `--replay-model` also
loads the archived measurement sources in a temporary directory and replays each
environment-compatible anchor; unavailable environments are explicitly reported.
`--model-case cpu-free-001` or `--model-case gpu-free-002` requires that selected
environment and replay to pass. It does not run a simulator or receiver.
Compressed results and observation
logs are byte-preserved; `preservation.json` binds both original and compressed
hashes. `source-index.json` maps the measured closure and labels supporting sources.
`summary.json` is a derived convenience view, not a replacement for raw proof.
The archive also includes the focused test and an explicitly labeled replay-only
`triad_perf_conditional.py` import dependency; neither is retroactively added to
the measured worker closure.

Both original saved anchor files are byte-identical to their corresponding raw
results; the frozen controller writes them only after `anchor_valid` succeeds
in the original environment. `preservation.json` records these duplicate artifact
hashes without duplicating compressed bytes. Local CPU replay passes. Cross-version
replay of the GPU result on the CPU environment does not satisfy the deliberately
exact summary equality: the reproduced timing interval endpoints differ by about
5×10⁻¹³ seconds, and replicate interval endpoints by about 1.3×10⁻¹² seconds.
The affine models themselves agree exactly. This is an arithmetic-environment
replay limitation, not a measured prediction failure; the original GPU proof is
preserved unchanged, and no tolerance was widened to call the local replay a pass.

Focused new qualification remains available through
`tools/triad_perf_identical_fft.py --mode qualify --case free-crystal`, supplying
the reference checkout, current native binary, compatible fixture cache, output,
device and thread policy. Routine use is `--mode reuse` with a compatible
`--anchor-directory`. The CPU and GPU anchors are separate; routine reuse still
requires fresh abbreviated observations, a full-bank sentinel, applicable
identity and absolute historical comparison. The parent campaign records that
subsequent reuse evidence separately. Do not rerun these historical qualification
campaigns merely to check an unrelated change.
