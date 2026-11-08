# Historical amplitude-model limitations

Current implementation and validation rules are in [Robust architecture](robust-current.md)
and [abbreviated testing](triad-performance.md). This note preserves research
ideas and source-bound evidence, not a second implementation or a recipe to
rerun retired campaigns. Historical results do not qualify changed code.

The old single-carrier amplitude receiver, its probability model and planner
integration are removed. This note retains only interpretation lessons relevant
to future triad research; it is not a selectable backend or a current forecast.

An amplitude statistic requires its own reference distribution. It cannot
inherit coherent matched-filter sensitivity or a coherent receiver's calibrated
threshold. Unknown arrival windows and competing hypotheses cost resources and
admission probability; a known-alignment first-bit result does not qualify
rolling acquisition or a complete message.

Long symbol duration can make timing, search and continuation work dominate the
nominal arithmetic for one statistic. Supporting a duration in integer geometry
does not demonstrate continuous reception or real-time processing at that
setting. Report unsupported geometry instead of retaining an apparently precise
probability from a mismatched model.

The current [Link Planner](link-planner.md) keeps its controls but does not
provide these retired forecasts. Use the canonical complex three-carrier
stages and [abbreviated testing policy](triad-performance.md); do not reconstruct
the old amplitude calibration or parallel receiver.
