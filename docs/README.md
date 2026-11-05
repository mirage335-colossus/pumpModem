# Documentation topic index

Start with the maintained guides for current behavior and procedures. Topic pages
link relevant studies and historical material; check their dates, source revisions,
assumptions and limits before applying their findings.

**Implementing or integrating the new three-carrier Robust modem:** start with
the [development workflow](triad-development-workflow.md). It maps the efficient
simulation tools, evidence-backed receiver choices, small runnable diagnostics,
workspace reuse and proportionate testing before larger sampled campaigns.
The [Helper-assisted triad integration](triad-helper.md) documents production
Helper composition, the requested HF/EME configurations, acquisition reuse and
scoped admission controls, separately from ordinary Robust integration.
The [Helper-enabled coverage map](triad-helper-coverage.md) separates broad
configuration feasibility, sampled reception evidence and remaining fixture limits.
The [extended Helper qualification](validation-data/triad-v3/2026-11-05-helper-extended/README.md)
records the rate-aware frontend, high-rate/maximal-spreading tests, exact
fixed-fixture domain census and quantitative multi-day Helper boundaries.
Its sampled campaigns are occasional qualification, not routine CI workloads.
Use [rate-aware shaping and Helper placement](triad-shaping.md) before raising
the DATA or pilot frequency: it combines inexpensive spectral planning with
paired waveform and bidirectional interference checks.
For isolated regression computation, follow the required
[efficient regression workflow](triad-performance.md#required-efficient-regression-workflow)
and its [confidence and material-slowdown policy](triad-performance.md#confidence-and-material-slowdown):
use validated predictive chains, cover distinct paths, and spend more only when
uncertainty or a material slowdown warrants it. Shared and ordinary Robust
changes retain their applicable requirements.
Use the [isolated Live/Duplex cable suite](triad-live-duplex.md) to characterize
the combined audio path and compare held-out waveforms with a frozen measured
model. Its coverage grades distinguish complete physical patterns, actual-rate
components, simulated-channel replay and unsupported or unidentified behavior.
Use the [isolated receiver performance suite](triad-performance.md) for fast
CPU CI guards, GPU development checks and separate source-bound prediction
qualification. `triad_perf_guard.py` screens broadly and confirms suspected
slowdowns; noisy or incomplete timing does not by itself block CI. A green guard
means no confirmed regression, not full qualification. `triad_perf_complete.py`
retains the stronger mean-runtime, historical forecast and sustained checks.
The [coverage and prediction matrix](triad-performance-matrix.md) maps maintained
paths to cheap checks, full workloads, anchors and unresolved regimes. Neither
entry point replays historical sensitivity campaigns by default or qualifies
ordinary Robust performance. The [current qualification record](validation-data/triad-v3/2026-11-04-performance-completion/README.md)
records 33 CPU/36 GPU measurement routes, roughly 5.5/7.7-minute routines,
independently validated sustained forecasts and the limits of the workflow-wide
blocking policy. Earlier failed models and statistical controls remain preserved.
The current [broad configuration study](validation-data/triad-v3/2026-11-03-broad/README.md)
separates unsupported configurations, capture limits, incomplete acquisition and
receiver failures. It also records corrected high-C/N₀ numerical failures;
consult it before reusing earlier noise-admission results.
The [configuration/acquisition extension](validation-data/triad-v3/2026-11-03-extension/README.md)
adds zero-shift/shared-clock support, independent modulation timescales,
frequency reuse from retained complex blocks, and the TCXO phase-continuity
diagnostic. Use its measured scope and remaining failures when selecting a
receiver; long exports remain optional research, not default regressions.
For wide timing banks, use the newer
[sampled FFT acquisition study](validation-data/triad-v3/2026-11-03-fft-timing/README.md).
It reuses one actual noisy sampled capture across a complete integer-arrival
and sample-phase CFO bank, with fixed clock/epoch and a static two-tap model.
Its CPU timing comparison and [continuity followup](validation-data/triad-v3/2026-11-03-fft-timing/continuity/README.md)
do not establish GPU acceleration or general reception reliability.

**Developing Robust simulation software:** start with the
[Doppler/delay spread and frequency-selective amplitude modulation guide](channel-models.md).
It documents the shared APIs, model presets, processing order, bypass rules,
reproducibility and focused integration checks for new simulation paths.

**GPU optimization or numerical precision controls:** start with the
[precision-control experiment guide](gpu-precision-controls.md). It separates
costly bulk arithmetic from sensitive preparation/reductions, records measured
precision tradeoffs, and explains what a dropdown can force versus permit.
Current CPU control behavior and experimental GPU policies are kept distinct.
The [GPU evidence map](validation-data/triad-v3/precision-controls/README.md)
provides archived measurements, direct test logs and reproduction checks.

For isolated extremely weak, long-duration three-carrier work, the
[sub-9-kHz study](validation-data/triad-v3/2026-11-01-sub9/README.md) records
sampled observations lasting up to 38 days, receiver comparisons and the
limits of their model-conditional reception and false-admission claims.
The maintained [v3 guide](triad-v3.md) also covers reciprocal-integer sample
clocks, a ten-Julian-year representation/planner ceiling, and the deterministic
CFO batch receiver. The subsequent [−60 dB-Hz study](validation-data/triad-v3/2026-11-02-minus60/README.md)
records 850–3,000-day sampled holdouts, lower-rate rounding costs, joint
ray-frequency inference, measured GPU acceleration and remaining coherence limits.
Its qualifications apply only to the declared models and acquisition conditions.
The [selective-modulation follow-up](validation-data/triad-v3/2026-11-02-selective/README.md)
examines stronger selective amplitude variation with Doppler spread, including
energy accounting, receiver alternatives and independent sampled boundary tests.

## Topic map

| Topic | Explore |
| --- | --- |
| Product scope and operation | [Project overview](../README.md), [requirements and qualification limits](requirements.md), [version scope](original-specification.md), [manuals](#command-and-interface-manuals) |
| Helper Carrier integration | [Current integration guide](helper-integration.md): transmission facts, inexpensive search-space narrowing and complete-stream correction, [retained experimental results](validation-data/helper/README.md) and remaining Robust integration work. |
| Simulation channel models | [Doppler/delay spread and additional frequency-selective amplitude modulation](channel-models.md): current sampled/PCM/Live integration, FHSS echoes, Helper composition, defaults and model limits. |
| Build, test, install and release | [Development and delivery](indexes/development.md): native/SDK/browser builds, test groups, packaging, offline use and dependency provenance; [shell script inventory and compatibility](shell-scripts.md) |
| GPU arithmetic and precision controls | [Stage allocation and dropdown experiments](gpu-precision-controls.md): measured cost versus numerical benefit, explicit bulk overrides, higher-precision exceptions, fallbacks and acceptance procedure; [current Search arithmetic](search-arithmetic.md). |
| Robust Modem | [Waveform, receiver and weak-signal topics](indexes/robust.md): protocol, iterative search, CPU efficiency at low expected SNR, link planning, measurements and alternate-receiver research |
| Experimental three-carrier Robust | [Development workflow](triad-development-workflow.md): efficient simulation, receiver selection and scoped validation; [V3 streaming study](triad-v3.md): long observations, joint channel likelihoods, finite-horizon detection and conditional duration planning; [noise calibration and configuration diversity](triad-noise-calibration.md); [GPU optimization protocol](triad-gpu-optimization.md), [measured campaign](validation-data/triad-v3/2026-11-01-optimization/README.md), [tensor follow-up](validation-data/triad-v3/2026-11-01-tensor/README.md), [exhaustive native FP4 campaign](validation-data/triad-v3/2026-11-01-fp4/README.md) and [consumer GPU / RTX 4090 extension](validation-data/triad-v3/2026-11-01-consumer-gpu/README.md); [V2 configuration examples](triad-v2.md) and [v1 prototype and evidence](triad-modem.md). |
| Scheduled Robust bursts | [Symbol_Burst](symbol-burst.md): complete symbols with guarded backlog time; [Sched_Hop](scheduled-hops.md): keyed active/null scheduling, split long symbols, dilution, frequency-search widening, workspace and qualification limits; fullscreen configured search-band waterfall |
| Fast Modem | [Formats, profiles and investigations](indexes/fast.md): QAM/LDPC, APSK, acoustic OFDM, cable/radio paths, expected SNR, recovery and physical measurements |
| Legacy Modem | [BPSK31, BPSK125 and Olivia reference](legacy-modem.md), [Olivia interoperability fixtures](../tests/fixtures/legacy/README.md), [BPSK fixture provenance](../tests/fixtures/legacy/PSK-fixtures.md) |
| GUI, terminal, framebuffer and browser | [Interfaces and inspection](indexes/interfaces.md): shared behavior, adapters, plots, Link Planner, browser/worker protocols and QR |
| Security and compatibility | [Contracts, cryptography and security analysis](indexes/security.md): wire/receiver preservation, key and storage boundaries, receive hardening, LPI models and historical reviews |
| Future prompts | [Human-maintained prompts](../FURTHER-agents.txt): offline ARM64 rebuilds and receiver efficiency. |
| Validation and supporting evidence | [Evidence index](indexes/evidence.md): current qualification, relevant studies, captures and release history |

## Command and interface manuals

| Manual | Interface |
| --- | --- |
| [pump(1)](man/pump.1) | Robust command-line operation and analysis |
| [pump-fast(1)](man/pump-fast.1) | Fast command-line operation |
| [datapump-gui(1)](man/datapump-gui.1) | Native graphical application |
| [datapump-tui(1)](man/datapump-tui.1) | Terminal application |
| [datapump-fb(1)](man/datapump-fb.1) | Framebuffer application |
| [datapump-worker(1)](man/datapump-worker.1) | Worker process |

See [reading manuals offline](offline-installation.md#manual-pages) for installed
and source-tree usage. Some linked build instructions, fixtures, browser assets
and dependency guides live outside `docs/` and require a matching checkout.
Selected inputs can be included in an [offline installation](offline-installation.md);
retired studies are available through [Git history links](indexes/evidence.md).

## Keeping the map useful

Search filenames, headings and text when a topic crosses branches. When changing
documentation, update the relevant topic page, preserve existing links and
evidence limits, and describe superseded work accurately.
