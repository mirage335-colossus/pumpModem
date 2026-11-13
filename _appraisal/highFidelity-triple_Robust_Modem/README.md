# High-fidelity triple Robust Modem appraisal

Status, 13 November 2026: **research and experiment design only**. There is no
experiment executable, new channel implementation, simulation result or modem
qualification in this directory yet.

This is the contained home for future experiment code, build integration and
experiment/test operating documentation. All such new material belongs here.
The literature synthesis and possible implementation discussion live separately
in [_ref/highFidelity-triple_Robust_Modem/Astra-6](../../_ref/highFidelity-triple_Robust_Modem/Astra-6/README.md).

Read the [proposed experiment protocol](experiment-protocol.md) before adding
an implementation. It defines scenario evidence, independent channel checks,
bounded sampling, native receiver integration and honest result categories.
The requested families are:

- Sub-9 kHz with a 200 ft / 60.96 m top-loaded vertical; earth-electrode and
  magnetic-loop alternatives are separate branches.
- Groundwave around 1 MHz.
- Groundwave around 30 MHz, with competing propagation mechanisms identified.
- Skywave over 1–30 MHz.
- Passive Earth–Moon–Earth, emphasizing 5.8 GHz.

“Standalone” means a contained, separately invoked appraisal that links the
canonical C++ modem. It does not authorize a copied modem, an alternate Python
DSP chain, changes to production defaults, or a claim that more detailed
simulation is automatically more realistic. There are no new build/run commands
to execute at this stage.

Keep eventual generated captures, builds and raw trial outputs temporary under
this directory's explicitly ignored output paths, adding those rules here
when the paths are first needed. Do not introduce outputs before they serve a
declared question. A retained dataset needs provenance, license/access notes,
purpose and a cleanup/review trigger. Preserve failed and unresolved evidence
until its question is resolved, then distill findings and remove redundant
outputs. Do not copy whole external source trees or accumulate old campaigns.
