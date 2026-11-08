# Helper-assisted triad coverage lessons

Current implementation and validation rules are in [Robust architecture](robust-current.md)
and [abbreviated testing](triad-performance.md). This note preserves research
ideas and source-bound evidence, not a second implementation or a recipe to
rerun retired campaigns. Historical results do not qualify changed code.

The historical broad study distinguished a necessary-constraint configuration
census from sampled reception. A factorized catalog covering billions of
configurations is not billions of reception tests, nor proof that all feasible
combinations decode. The [broad report](validation-data/triad-v3/2026-11-05-helper-broad/README.md)
and [extended report](validation-data/triad-v3/2026-11-05-helper-extended/README.md)
retain their exact inputs, exclusions and failed cases.

Separate evidence layers remain useful: integer/source geometry; passband and
sampling feasibility; pulse/guard support; public acquisition-prior coverage;
noise-model/admission validity; and actual sampled or physical reception.
Necessary energy bounds and interference/placement checks can cheaply reject
impossible cases without becoming positive reception evidence.

Changing Rate, Added Symbols, shaping, Helper placement, sample rate or channel
width can reach a different computational path. Cover distinct paths with small
representative strata instead of enumerating a Cartesian product. Preserve the
complete transmitted waveform and declared effects in every selected case;
quiet guards cannot erase propagated echoes.

Historical occasional sampled campaigns are evidence only. Their drivers,
large factor sweeps and qualification workflows are removed. New intensive
checks follow [the minimal hierarchy](triad-performance.md), with unresolved
regions reported explicitly rather than escalated to a full campaign.
