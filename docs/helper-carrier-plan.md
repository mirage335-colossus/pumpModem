# Retained Helper integration lessons

Current implementation and validation rules are in [Robust architecture](robust-current.md)
and [abbreviated testing](triad-performance.md). This note preserves research
ideas and source-bound evidence, not a second implementation or a recipe to
rerun retired campaigns. Historical results do not qualify changed code.

The former product/Live integration plan is retired with the single-carrier
receiver. The useful separation of responsibilities remains: one native
Generator owns the transmitted waveform, a receiver reports measured frequency,
phase and timing evidence, and DATA acquisition decides whether that evidence
narrows its public search. Auxiliary identifier decoding is a separate outcome.

A complete corrected baseband stream was the goal of earlier conditioning
experiments. Missing or uncertain Helper evidence must remain explicit; it
cannot justify inventing samples, hiding incomplete observations or reporting
a successful DATA reception. Frequency benefit can survive unresolved phase
transfer, particularly under selective or changing propagation.

The [current integration note](helper-integration.md) replaces the old work plan.
The [retained Helper results](validation-data/helper/README.md) preserve dated
successes, failed alternatives and model assumptions. They are design examples,
not a requirement to reinstate the old complete-stream implementation.
