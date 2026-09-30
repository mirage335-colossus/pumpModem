# Build, test, install and release

[Documentation index](../README.md) · [Interfaces](interfaces.md) ·
[Coordination](coordination.md) · [Evidence and release history](evidence.md)

## Maintained procedures

| Document | Contents |
| --- | --- |
| [Build and maintenance guide](../building.md) | `build.sh`, stable profiles, focused/general tests, SDKs, platform and packaging checks; includes source navigation. |
| [COMPILE](../../COMPILE) | Repository build command entry point. |
| [COMPILE-web](../../COMPILE-web) | Browser build/preview command entry point. |
| [Offline installation](../offline-installation.md) | Portable installation, manual pages, dependency provenance and optional evidence bundles. |
| [Release procedure](../../RELEASE) | Ordered release-delivery instructions. |
| [Release guide](../releases.md) | Publication, exact source/binary assurance, reusable dependency recipes and certification policy. |
| [Validation ledger](../validation.md) | Dated checks, failures, omissions and limits; locate the relevant revision, topic or heading. |
| [Development contract](../development.md) | Compatibility and preservation tests required by implementation changes. |
| [Contributor requirements](../../AGENTS.md) | Repository instructions, testing stages and coordination requirements. |

## Dependencies and provenance

| Document | Contents |
| --- | --- |
| [Third-party inventory](../../third_party/README.md) | Vendored dependencies, provenance, licenses and local integration context. |
| [Dependency preparation and source SDKs](../../third_party/build-support/README.md) | Native dependency supplement, pinned source SDK recipes and preparation/reuse. |
| [Wasm SDK](../../third_party/build-support/wasm-sdk/README.md) | WebAssembly SDK inputs, build and provenance. |
| [Rev integration notes](../../third_party/rev/README.datapump.md) | Local integration and source provenance. |
| [XZ integration notes](../../third_party/xz/README.datapump.md) | Local compression dependency provenance. |
| [QR integration notes](../../third_party/qrcodegen/README.datapump.md) | Local QR dependency provenance. |
| [LDPC source notes](../../third_party/ldpc/README.md) | Vendored coding source context. |

Upstream manuals, notices and other documentation remain with their dependency
trees. These entry points complement those retained materials. See the
[evidence index](evidence.md) for historical commands, captured results and
release records whose revision/environment must be considered before reuse.
