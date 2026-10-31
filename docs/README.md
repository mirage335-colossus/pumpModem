# Documentation topic index

Start with the maintained guides for current behavior and procedures. Topic pages
link relevant studies and historical material; check their dates, source revisions,
assumptions and limits before applying their findings.

## Topic map

| Topic | Explore |
| --- | --- |
| Product scope and operation | [Project overview](../README.md), [requirements and qualification limits](requirements.md), [version scope](original-specification.md), [manuals](#command-and-interface-manuals) |
| Helper Carrier integration | [Current integration guide](helper-integration.md): Helper contracts, inexpensive search-space narrowing and complete-stream correction, measured evidence and remaining Robust integration work. |
| Build, test, install and release | [Development and delivery](indexes/development.md): native/SDK/browser builds, test groups, packaging, offline use and dependency provenance; [shell script inventory and compatibility](shell-scripts.md) |
| Robust Modem | [Waveform, receiver and weak-signal topics](indexes/robust.md): protocol, iterative search, CPU efficiency at low expected SNR, link planning, measurements and alternate-receiver research |
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
