# Three-pair first-look shadow

The [plan](plan.json) was frozen before examining the three-pair prefixes.
It tests `(3, 5, 9, 17, 33)` with equal per-direction spending of α=0.05,
only for the same 20 already-routed direct paired cases. Workloads, ordering,
timer boundaries, FFT route selection, model/sentinel checks, identical-source
comparison and qualification schedules do not change. The adoption criterion
was independently corroborated savings of at least 120 seconds of recorded
operation time, without unresolved extra sampling or decision disagreement.

| Recorded observation timers | Five-pair-first policy | Three-pair shadow | Omitted timers |
|---|---:|---:|---:|
| Previous width-driven capture, replayed stopping | 436.485 s | 290.927 s | 145.558 s |
| Independent current decision capture | 408.883 s | 321.784 s | 87.099 s |

All 20 current cases resolve within available observations: 12 at three pairs,
seven at five and narrow-HF at nine, with no pass/regression disagreement.
The current-record difference falls short of the predeclared 120-second
criterion. **Retain the five-pair default.** This is not proof that five is
globally optimal. Timer-prefix subtraction is not measured campaign wall-time
savings: setup, independent endpoints, functional checks and hardware screens
remain, and earlier stopping can change later cache/thermal conditions. No
second workload campaign or new qualification was run for this alternative.

The original [inline analysis](replay.json) is preserved, including raw-input
hashes. Its standalone implementation was not retained. Therefore
[replay.py](replay.py) independently rechecks every stopping decision and upper
bound using the actual maintained `summarize` function and archived observations.
That check found one cost-accounting discrepancy: the new Gaussian helper used
23 inner calls, whereas the original shadow applied the old capture's 21 calls.
The original reported current savings of 87.067 seconds becomes **87.099
seconds** with each capture's recorded multiplicity. This does not change the
decision. The original result is not overwritten; [verified.json](verified.json)
contains the corrected sums and discrepancy. Inner calls affect cost only and
are not counted as independent observations.

To reproduce, use the matching NumPy/SciPy environment and the adaptive helper
from the [source snapshot](../routine/matching-source-and-tests.tar.gz):

```sh
python docs/validation-data/triad-v3/2026-11-03-performance-decision/n3-shadow/replay.py
```

The script verifies the helper hash, caches decompressed reports, and reads
only the preserved old/new archives. It does not execute a receiver or measure
timing. Its output must match `verified.json`.
