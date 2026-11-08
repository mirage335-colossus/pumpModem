# Long observation followup

This completed followup extended the broad triad receiver study with fresh
850-day simulated captures in four configurations. All 136 native exports and
receiver jobs completed: each configuration admitted 32/32 correct bits, with
no wrong-bit admissions or misses; all four nulls and four wrong-key controls
were rejected. The [independent recount](../evidence/long-summary.json) verifies
the raw scores, input hashes, source bindings, planned denominators and both
gates. The [broad-study report](../README.md#long-observations-coherence-and-computational-cost)
records per-configuration energy ranges and the complete interpretation.

The 32/32 result gives a one-sided 95% all-success lower bound of 91.06% for
each configuration under the simulated independent-path model; it does not
establish 99%, per-bit 90%, simultaneous multi-configuration qualification,
rare-event false-admission rates, or years of real GPSDO stability or
propagation. The study processed 3,755,584,070 complex samples and retained
4.25 GB of statistics. Its elapsed costs and GPU comparison are summarized in
the broad-study report; these full-span measurements remain optional research
evidence, not routine tests.

The campaign-specific runner and execution instructions are retired. Its
compact result summary is retained for the claims above; raw workspaces need
not be recreated or retained for routine development. Future intensive Robust
checks follow the [abbreviated statistical harness](../../../../triad-performance.md)
and [material lifecycle policy](../../../../development.md#material-lifecycle).
