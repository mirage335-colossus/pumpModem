# Documentation topic index

Start with the maintained guides for current behavior and procedures. Topic pages
link relevant studies and historical material; check their dates, source revisions,
assumptions and limits before applying their findings.

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

## Topic map

| Topic | Explore |
| --- | --- |
| Product scope and operation | [Project overview](../README.md), [requirements and qualification limits](requirements.md), [version scope](original-specification.md), [manuals](#command-and-interface-manuals) |
| Helper Carrier integration | [Current integration guide](helper-integration.md): transmission facts, inexpensive search-space narrowing and complete-stream correction, [retained experimental results](validation-data/helper/README.md) and remaining Robust integration work. |
| Simulation channel models | [Doppler/delay spread and additional frequency-selective amplitude modulation](channel-models.md): current sampled/PCM/Live integration, FHSS echoes, Helper composition, defaults and model limits. |
| Build, test, install and release | [Development and delivery](indexes/development.md): native/SDK/browser builds, test groups, packaging, offline use and dependency provenance; [shell script inventory and compatibility](shell-scripts.md) |
| GPU arithmetic and precision controls | [Stage allocation and dropdown experiments](gpu-precision-controls.md): measured cost versus numerical benefit, explicit bulk overrides, higher-precision exceptions, fallbacks and acceptance procedure; [current Search arithmetic](search-arithmetic.md). |
| Robust Modem | [Waveform, receiver and weak-signal topics](indexes/robust.md): protocol, iterative search, CPU efficiency at low expected SNR, link planning, measurements and alternate-receiver research |
| Experimental three-carrier Robust | [V3 streaming study](triad-v3.md): long observations, joint channel likelihoods, finite-horizon detection and conditional duration planning; [noise calibration and configuration diversity](triad-noise-calibration.md); [GPU optimization protocol](triad-gpu-optimization.md), [measured campaign](validation-data/triad-v3/2026-11-01-optimization/README.md), [tensor follow-up](validation-data/triad-v3/2026-11-01-tensor/README.md), [exhaustive native FP4 campaign](validation-data/triad-v3/2026-11-01-fp4/README.md) and [consumer GPU / RTX 4090 extension](validation-data/triad-v3/2026-11-01-consumer-gpu/README.md); [V2 configuration examples](triad-v2.md) and [v1 prototype and evidence](triad-modem.md). |
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
