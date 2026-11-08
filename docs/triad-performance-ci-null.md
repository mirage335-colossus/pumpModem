# Historical CI false-block experiment

Current implementation and validation rules are in [Robust architecture](robust-current.md)
and [abbreviated testing](triad-performance.md). This note preserves research
ideas and source-bound evidence, not a second implementation or a recipe to
rerun retired campaigns. Historical results do not qualify changed code.

The full guard/workflow and cloud campaign studied here are retired. The
[November 4 CI-null record](validation-data/triad-v3/2026-11-04-ci-null/README.md)
retains the raw outcomes and original environment. It is not a prerequisite for
the new abbreviated harness or a source-matched result for this cleaned tree.

The experiment compared a build with itself across 64 scheduled allocations.
It recorded zero all-cause blocks, statistical rejections, functional/integrity
failures or infrastructure-incomplete outcomes. Three workflows nominated cases
for confirmation and stopped for futility. The one-sided 95% upper bound was
4.57% only under independent, representative allocation clusters. Physical host
placement was not verified; the conservative placement-unknown bound was 95%.

The reusable lesson is that nominal within-run error accounting, observed CI
false-block rate and slowdown-detection power are different claims. Allocation
identifiers do not prove independent hardware. Missing guard results, functional
failures and infrastructure outcomes must not disappear from an analysis.
Neither zero observed blocks nor this historical result establishes an
unconditional future false-block rate or the power of the current harness.

Do not repeat the 64-workflow experiment or restore its observer/campaign tools.
Use the current predeclared abbreviated inference checks and retain unsupported
performance comparisons as unresolved.
