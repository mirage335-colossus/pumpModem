# Documentation topic index

Use this map to explore topics relevant to an operating, development or research
question. Each branch links existing documents in place, including previous
investigations, negative results and supporting evidence. There is no required
reading sequence or requirement to read every branch.

Descriptions distinguish implementation references and procedures from measured
studies, analytical models, proposals and historical designs. These are navigation
labels: check each document's own date, source revision, environment, assumptions
and remaining limits. Prior conclusions can be challenged; use current source,
profiling, experiments and new research where the task needs them. A model or
proposal does not establish measured performance, and dated evidence can remain
useful after settings or implementations change.

## Topic map

| Topic | Explore |
| --- | --- |
| Product scope and operation | [Project overview](../README.md), [requirements and qualification limits](requirements.md), [version scope](original-specification.md), [manuals](#command-and-interface-manuals) |
| Build, test, install and release | [Development and delivery](indexes/development.md): native/SDK/browser builds, test groups, packaging, offline use and dependency provenance |
| Robust Modem | [Waveform, receiver and weak-signal topics](indexes/robust.md): protocol, iterative search, CPU efficiency at low expected SNR, link planning, measurements and alternate-receiver research |
| Fast Modem | [Formats, profiles and investigations](indexes/fast.md): QAM/LDPC, APSK, acoustic OFDM, cable/radio paths, expected SNR, recovery and physical measurements |
| Legacy Modem | [BPSK31, BPSK125 and Olivia reference](legacy-modem.md), [Olivia interoperability fixtures](../tests/fixtures/legacy/README.md), [BPSK fixture provenance](../tests/fixtures/legacy/PSK-fixtures.md) |
| GUI, terminal, framebuffer and browser | [Interfaces and inspection](indexes/interfaces.md): shared behavior, adapters, plots, Link Planner, browser/worker protocols and QR |
| Security and compatibility | [Contracts, cryptography and security analysis](indexes/security.md): wire/receiver preservation, key and storage boundaries, receive hardening, LPI models and historical reviews |
| Validation and research evidence | [Studies, captures and release history](indexes/evidence.md): raw results, reproduction methods, independent vectors, failed attempts, warnings and certification records |
| Concurrent development and agent work | [Coordination procedures and studies](indexes/coordination.md): current workflow, helper recipes, lifecycle, temporary findings and empirical evaluations |

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
and source-tree usage. Some supporting material lives outside `docs/`; topic
pages also link those build instructions, fixtures, browser assets and dependency
guides. Source-tree links to contributor files, command sheets, browser assets,
test fixtures and dependency preparation material require a matching checkout
when that material is absent from the installed provenance subset.
Raw validation captures require a source checkout or an
[evidence-inclusive installation](offline-installation.md).

## Finding more detail and keeping the map useful

Topic entries are starting points, not exclusions. Search document filenames,
headings and text when a question crosses branches or an entry is too broad.
For a large ledger such as [validation](validation.md), locate the relevant
heading/date and read the associated record and evidence rather than assuming
that its size makes the evidence irrelevant.

When adding or substantially changing documentation, update the relevant topic
page and cross-link other useful branches. Keep existing paths and inbound links
working, retain original evidence and its limitations, and describe superseded
work accurately rather than silently dropping it from navigation. Temporary
local findings follow the [coordination workflow](agent-coordination.md); they
are separate from this maintained, distributable documentation map.
