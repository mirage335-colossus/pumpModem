# Historical pilot-only Helper measurement

Current implementation and validation rules are in [Robust architecture](robust-current.md)
and [abbreviated testing](triad-performance.md). This note preserves research
ideas and source-bound evidence, not a second implementation or a recipe to
rerun retired campaigns. Historical results do not qualify changed code.

The retired pilot-only receiver consumed signed complex baseband covering a
public high-pilot search range. It was not the application's real-audio adapter
or a DATA decoder. Its useful separation was broad pilot acquisition, local
frequency/phase measurement, and a dated clock model across known hop commands.

Search-range selection relied on the [quiet-spectrum assumption](helper-carrier.md#quiet-spectrum-assumption).
An observed strongest pilot in that assumed range did not demonstrate rejection
of arbitrary competing interference. The [fixed observation and lead](helper-carrier.md#lead-in-operating-requirement)
were explicit; later tracking observations did not retroactively strengthen an
earlier frozen result.

Earlier fixed-five-offset results and later keyed randomized-hop results are
different fixture populations. In particular, total active-mean Helper power
must be converted using the actual straight-pilot power fraction; it is not all
assigned to the high pilot. Recorded point errors and conditional uncertainty
were not measured acquisition probabilities or general hardware limits.

The [Helper results](validation-data/helper/README.md) preserve the dated
measurement context. Current development uses [canonical integration](helper-integration.md)
and abbreviated checks, without the old measurement receiver or full simulations.
