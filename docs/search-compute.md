# Batched iterative search

The iterative pattern receiver executes numerical batches on the CPU. This
supports large logical work grids without creating one operating-system thread,
template cache or transform buffer per hypothesis.

## Compute boundaries

| Path | Input and output | Host responsibilities |
| --- | --- | --- |
| FFT acquisition, `src/pattern_fft_batch.hpp` | Indexed symbol/phase/frequency jobs, shared spectrum and energy rows, immutable pattern parameters or prepared templates; flat pairs of scores indexed by job and start position | Search enumeration, trial counts, thresholds, peak selection, tracking, admission and publication |
| Long-symbol correlation, `src/pattern_correlator_batch.hpp` | Indexed numerical lanes containing coordinates and fits, immutable pattern parameters, shared block projection rows; updated fits for each lane | Symbol completion, phase selection, peer ownership, reception state and publication |
| CPU execution, `src/search_parallel.hpp` | Contiguous logical ranges with a chosen grain size | A shared, bounded persistent worker pool, worker-private caches and exception collection |

Logical indices do not encode a CPU worker number. Range dispatch allocates no
state proportional to the number of jobs; tests cover 100,003 jobs and sparse
ranges spanning `SIZE_MAX`. CPU concurrency still defaults to all but one
available logical CPU, with at least one. Available work and workspace can lower
the actual concurrency.

CPU correlation chooses its range grain from both lane count and worker count,
targeting at least 16 ranges per worker when enough lanes exist, capped at 16
lanes per range. Small banks therefore retain enough independently schedulable
work, including when only some start-time hypotheses have become active. The
normal public 1 Hz bank has 75 lanes; its previous fixed grain of 16 exposed only
five parallel jobs despite an 11-worker limit.

FFT job capacity now depends on the search bank and spare workspace, rather
than a small multiple of the CPU count. Each CPU worker retains only one
transform buffer and, when required, one mutable pattern cache. Jobs have
disjoint output slices. A backend can reorder or tile them while the host
collects scores in the original symbol/phase/frequency/start order. Prepared
template views stay valid until the synchronous backend call completes.

The long-symbol coordinator groups up to 64 original processing blocks and
tiles up to 65,536 numerical lanes at a time, reducing dispatch and host scans
between blocks. Both bounds can be reduced by spare workspace. Each lane
processes its blocks in the original order. The original oscillator restarts,
prefix sums and floating-point addition sequence remain intact; combining all
the samples into one new oscillator block would change scores.

Batching ends **before any hypothesis can complete a symbol**. The original
scalar coordinator handles that boundary in its original hypothesis order.
This also prevents batching across a caller's progress poll. No bit waits for
an arbitrary batch to fill, and partial silence cannot finish reception early.
One-worker and tight-workspace operation retain the original scalar path.

All temporary arrays are bounded by spare DSP workspace. They are released
before they can displace payload growth and at the end of a push, preserving
idle receiver-bank capacity. Batch records contain no received messages, trial
counters or publication callbacks. Copied pattern seeds are cleared when the
batch view is destroyed. CPU pattern caches must be constructed from the same
configuration as the supplied immutable pattern parameters.

## CPU validation and reproduction

The CPU reference tests exercise 16,387 FFT jobs and 10,019 correlator lanes,
worker-count and tile-size equivalence, reordered FFT jobs, malformed spans,
cancellation and reuse. Receiver-level tests preserve exact scores, next-poll
prefixes, physical absence, tight workspace and idle footprints.

For a reproducible large-bank CPU workload, build and run:

```sh
cmake --build build --target benchmark_correlator
./build/benchmark_correlator 1200 -10 1 2048 .25 1
./build/benchmark_correlator 1200 -10 0 2048 .25 1
```

This selects 11,265 hypotheses with approximately 631-second symbols, feeds
2,048 deterministic noise samples, and reports wall time, process CPU time and
idle memory. Arguments select bandwidth, target C/N0, workers, sample count,
clock uncertainty and compact mode. C/N0 selects the receiver geometry; this
partial-symbol workload measures computation, not decoding or detection
probability. Run paired comparisons without other CPU-heavy work.

The separate post-reception hard-bit recovery engine and its resumable search
are outside these DSP interfaces. Wire formats, source decoding and GUI message
presentation are also unchanged; see the [development contract](development.md).
