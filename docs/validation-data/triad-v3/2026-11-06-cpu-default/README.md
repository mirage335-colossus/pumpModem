# Automatic receiver CPU allocation

The maintained isolated receiver entry points now use the process's available
CPU budget by default. Detection reuses `build_capacity.py` affinity and cgroup
traversal without its compile-job CPU reserve or memory heuristic. Readable
ancestor quotas constrain the result; inaccessible/hidden limits are reported,
not inferred. Fractional quotas are rounded down with one thread as minimum.

On this Ryzen 5 PRO 5650U the detected allocation is12logical CPUs. Broad
acquisition receives12FFT workers for one scheduled job, or two independent
workspaces with6FFT workers each for multiple jobs. Automatic workspace
replication is capped at two. An odd allocation uses one workspace to retain
the whole FFT budget. Explicit `--workers`/`--threads` remain exact overrides;
explicit overcommit is reported. Available capacity is not a promise of100%
utilization in serial or small operations.

The two-thread small-matrix limit is **specific to the measured broad
acquisition paths**. Continuous fixed/tracking and physical-profile receivers
retain their full automatic linear-algebra budget; no performance gain for
those other families is inferred from the HF measurement. Low-level receiver
constructors select their FFT count without mutating process-global libraries;
the owning controller configures numerical execution before starting workers.

`threadpoolctl==3.6.0` is now a declared receiver dependency and is pinned in
the isolated CI environment. Environment, PyTorch and loaded BLAS/OpenMP pools
are configured once in the controller. Each scoring worker applies the same
constant limit through a cached controller once, since OpenMP limits can be
thread-local. Initializer failures produce explicit scorer errors rather than
running with uncontrolled pools or hanging the queue. Runtime receipts retain
actual pool sizes. The policy follows the current
[threadpoolctl threading guidance](https://github.com/joblib/threadpoolctl#usage-for-python-threads-restricting-controlled-library-thread-pool-sizes)
and [PyTorch initialization requirement](https://docs.pytorch.org/docs/stable/generated/torch.set_num_threads.html).

## Measured policy selection

All runs used the saved independent HF bit-00 from the
[preceding cadence campaign](../2026-11-06-realtime-optimization/README.md),
including fresh native keyed templates, workspace preparation and complete
Helper/acquisition/refinement. They exclude initial Python imports, saved-audio
DDC and TX/channel generation. Measurements are individual comparisons, not
confidence-qualified hardware forecasts.

| One-job allocation | CPU seconds | Wall seconds |
| --- | ---: | ---: |
| Explicit2 FFT /2 linear algebra | 13.498 | 11.137 |
| Naive12 FFT /12 linear algebra | 82.989 | 17.039 |
| Selected automatic12 FFT /2 linear algebra | 14.706 | 12.134 |

Indiscriminately increasing threads caused a serious regression: refinement
preparation alone rose from1.88CPU/1.53wall seconds to57.10CPU/6.60wall seconds.
The selected policy retains all available FFT capacity while avoiding that
small-matrix overhead. It is **not a measured speedup over explicit2threads**
on this particular short transform workload. SciPy describes FFT workers as a
maximum; small/one-dimensional work can use fewer.
[SciPy FFT](https://docs.scipy.org/doc/scipy/reference/generated/scipy.fft.fft.html)

The separate final two-job automatic6FFT/2linear-algebra run offered jobs at
11.6-second intervals and completed in **22.780wall /28.450CPU seconds**:
1.226CPU seconds per offered DATA second, no overloads, two correct conditional
admissions, maximum queue wait0.404ms, maximum completion latency12.333seconds,
and628.0MiB parent peak RSS. Both final runs retained unchanged source identities.
Bit, score, margin, bit-score vector and operational decision matched the saved
baseline exactly. These are independent-window computational tests, not a new
continuous-message, physical admission, GPU or sensitivity qualification.

An intermediate naive two-worker6/6 experiment changed a source identity
during execution. Its raw failed identity check is retained; it is not used
as an accepted timing anchor. The final runs are the source-stable evidence.

## Final checks and reuse

The corrected focused suite passed **72 tests in0.508seconds**. The real-library
two-worker probe also passed. No native DSP code changed, so no native rebuild,
GPU allocation or cable campaign was needed for this policy change.

Focused checks cover affinity/ancestor quotas, unknown-limit reporting,
automatic/explicit allocations, library initialization, thread-local worker
limits, failed initialization, policy identities and caller propagation.
In particular, frozen qualification `threads_per_process` and HF noise
measurement `--threads` still reach their FFT receivers; changing a library
default must not silently expand those historical workloads.

A tiny final real-library probe initializes both actual streaming workers and
verifies PyTorch/OpenMP and both NumPy/SciPy BLAS pools report the resolved count.
The later worker initializer and gate-identity metadata do not change the
already-observed numerical settings or samples of the final timing runs. Their
limited transfer is explicit; original source hashes remain in the receipts.
No additional full DSP rerun is needed just to attach a newer metadata hash.

Run the cheap tests in the maintained numerical environment:

```sh
PYTHONPATH=tools:tests python3 -m unittest test_triad_cpu test_triad_stream_scheduler test_triad_stream_run test_triad_weak_receiver test_triad_realtime_receiver test_triad_continuous test_triad_physical_run test_triad_receiver_cpu_limits
```

The archive retains raw reports, inputs by identity, original and current
measurement scripts, tests and a source patch. Large sampled fixtures remain
in the preceding study. For a focused timing follow-up, extract at the repository
root and use `measure_current.py LABEL JOBS THREADS LINEAR_LIMIT`, where
`THREADS=auto` and `LINEAR_LIMIT=2` select the broad default; `none` disables the
automatic linear-algebra cap. Use a fresh label and an external process-group
timeout. Do not rerun historical campaigns for unchanged budget logic.

The first final test invocation included two nonexistent test module names;
that setup failure is retained separately from the corrected suite. The
initial package installation also encountered sandbox DNS restrictions; the
authorized installation into the existing temporary environment succeeded.
