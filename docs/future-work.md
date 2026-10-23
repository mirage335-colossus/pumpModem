# Future work

These are unfinished goals, not active work orders or evidence of completed
behavior. Recheck current source, maintained technical documentation and
validation records before starting any item. Follow the current project
contracts and validation requirements. Detailed design and historical evidence
remain in the linked plans.

## Offline Debian Bookworm builds on native ARM64

Qualify a complete Linux rebuild from retained local inputs on native Debian
Bookworm for both x86_64 and ARM64. The remaining gap recorded on 6 October
2026 was the ARM64 build host: it used Ubuntu 22.04 and online preparation,
while the retained native Bookworm SDK route covered x86_64. Portable ARM64
runtime checks did not establish an offline ARM64 source build.

Retain exact SDK/tool/dependency inventories, checksums, provenance and licenses;
make missing local inputs fail clearly; preserve the working x86_64 route. Cover
FLTK, Rev, terminal, framebuffer, native web worker, Wasm SDK/browser payload and
any required Python bootstrap. Measure compiler restoration separately from
cold SDK/compiler reconstruction.

Acceptance requires clean native Bookworm environments on both architectures,
network disabled at the OS/container boundary, fresh build/HOME/cache
directories, and configuration, compilation and packaging using declared local
inputs. Bind results to exact source, SDK recipes, dependency hashes and release
inventory. Also run copied portable TGZ/ZIP payloads without source or SDK access
on Bookworm and the supported newer glibc distro matrix. Preserve relocation,
dependency closure, ELF/CPU baseline, GUI/CLI and certification gates, including
x86_64 glibc 2.36 and ARM64 glibc 2.35 floors. Document required display/audio
facilities and drivers; do not extend qualification claims beyond tested systems.

Start with the release/SDK workflows, `tools/release.py`, `tools/build-sdk.py`,
SDK installation helpers, `cmake/GuiRev.cmake`, and the maintained build,
release and validation documentation.

## Receiver efficiency for long symbols

The 7 October 2026 goal remains to reduce actual receiver CPU cost for long
symbols split across scheduled bursts by reusing input projections, public pulse
statistics and template work across timing/frequency/clock families and rolling
banks. Use reusable sufficient statistics or calendar-aware tiled correlation
only where complete execution profiles support them; keep sparse-bank direct
paths efficient and all caches/delayed work within the DSP workspace.

Acceptance holds waveform bandwidth, observation duration and required
hypotheses unchanged, including equivalent physical links on different audio
carriers. Preserve waveform bytes, cryptographic addressing, detector/noise
accounting, incremental bit publication and physical-absence completion. Measure
whole preparation/execution CPU, memory and latency and compare matched original
PCM; kernel speed or planner estimates alone do not establish a receiver gain.
Continue from [search-compute.md](search-compute.md) and
[scheduled-hops.md](scheduled-hops.md), including their linked positive and
negative results.

## Practical Sched_Hop processing headroom

The detailed runbook is retained in [sched-hop-implementation-runbook.md](sched-hop-implementation-runbook.md).
Its original plan baseline is 15 October 2026, commit
`7a9640764e2a0249e5dbd64f039cd8df987350e7`; the runbook remains a historical
technical proposal. The practical sustained-reception rescue is unfinished.
Consult [scheduled-hops.md](scheduled-hops.md) and
[validation.md](validation.md) for current maintained behavior and accepted
results before resuming the plan.

Acceptance is a matched, useful, optimized continuous complex-reception case
where scheduled reception creates real physical processing time and enough
existing acceleration to keep overhead below that saved time. Hold target SNR,
waveform geometry, input rate, workers and workspace fixed. Retain required
hypotheses, guards and detector behavior; include every rolling bank, diagnostics,
physical absence and final drain. Report CPU and wall service, actual duty/energy,
backlog slope and peak, latency, memory, exact detector agreement and sensitivity.
The plan's M3/G3 continuous control and M5/G3 and internal M5/G4 comparisons
remain useful diagnostics; G4's single possible placement does not satisfy a
secret-calendar objective. Do not infer complete rescue from a faster kernel,
planner coefficient, one-bank result or failed prefix.
