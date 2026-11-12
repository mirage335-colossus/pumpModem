# Data Pump

**Build:** `./build.sh` · **Test:** `./build.sh test contract` ·
**Package:** `./build.sh package`. See the [build guide](docs/building.md) for
profiles, dependencies, tests, and release boundaries. Contributors should read
[AGENTS.md](AGENTS.md).

Browse the [documentation index](docs/README.md) for maintained contracts,
current architecture, Fast and Legacy references, security, interfaces, and
validation evidence. Use dated studies only within their recorded source,
configuration, input, and measurement scope.

Data Pump is a C++20 audio modem application with a command-line interface, a
native desktop console, optional terminal/framebuffer/browser frontends, audio
channel simulation, and bounded in-memory receive handling. Communication uses
PCM or analog audio interfaces. Saves are explicit.

## Robust modem status

Start with the [Robust operating guide and experimental lessons](docs/indexes/robust.md)
for peer setup, transmission format, sensitivity assumptions, hardware choices
and useful alternatives with their measured limits.

Robust uses one compiled C++ three-adjacent-carrier `IndependentApskV2` stack
linked into the GUI: DATA and Helper generation, continuous channel simulation,
complete receiver processing and native audio. In-process worker owners share
buffers and one aggregate CPU budget. Python is a testing/benchmarking tool,
not an application runtime.

Live operation requires peers to share a configured calendar, keyed ordinal,
waveform/FEC settings and key, plus a declared local input-noise model. The GUI
accepts text up to 1 MiB, preserving the short profile through 16 bytes and using
fixed marker/codeword intervals for longer text. Exact raw bits and configured
Off/RS20/RS60 text FEC use the same native session. Unknown-start acquisition,
physical-device qualification and native real-time capacity remain unqualified. The former Robust audio CLI
commands remain retired. See [current architecture](docs/robust-current.md),
[GUI controls](docs/gui-contract.md) and [validation](docs/validation.md#native-codec-and-measurement-migration).

To edit the informal [Robust test list](_tests-RobustModem.md), open the
[standalone editor](tools-endUser/robust-test-list-editor.html) in a browser, select the
list file, and download the edited result. Use the dropdowns for normal edits;
choose “Custom value…” only to enter an override. Reorder blocks with their drag
handles or Move up/Move down buttons before downloading. The editor runs locally
and does not run or validate modem tests.

The retired single-carrier and standalone triad/Helper receivers are not
maintained as parallel implementations. Protocol revisions use Git tags and
separate binary releases. Computationally intensive Robust checks use the
[abbreviated statistical policy](docs/triad-performance.md), with small
predeclared stages, explicit uncertainty, matched provenance, and no full or
sustained campaign fallback. RunPod accelerates that same method.

## Other modems

**Fast Modem** is an independent QAM/LDPC and APSK text/file-transfer interface.
It has its own transfer formats, audio behavior, coding, and regression scope.
See the [Fast documentation index](docs/indexes/fast.md) for current settings and
measured limits. General audio/radio qualification remains bounded by the linked
evidence.

**Legacy Modem** provides BPSK31, BPSK125, and Olivia-4/2k text communication.
Its modulation, settings, and checks are independent of Fast. See the
[Legacy reference](docs/legacy-modem.md).

## Build and run

```sh
./build.sh
./build/dev/datapump-gui
./build/dev/pump --version
./build.sh test contract
```

The default build produces the native GUI and CLI. Use `./build.sh --cli` for a
CLI-only build. See the [build guide](docs/building.md) for optional frontends,
test groups, package profiles, and offline installation.

The shared GUI supports Console, transmission drafts, audio selection, and
modem-specific views. See the [GUI contract](docs/gui-contract.md),
[architecture](docs/gui-architecture.md), and [frontend interfaces](docs/frontend-interfaces.md).
Robust live pattern plots show native receiver evidence. Detailed transmission
inspection and Link Planner receive/CPU forecasts retain their stated limits.

## Documentation

- [Topic index](docs/README.md)
- [Development contract](docs/development.md)
- [Security and received-content rules](docs/security.md)
- [Protocol and codecs](docs/protocol.md)
- [Current validation](docs/validation.md)
- [Manuals](docs/offline-installation.md#manual-pages)
- [Release disclaimer](docs/disclaimer.md)

Application code is dedicated to the public domain under [CC0 1.0 Universal](LICENSE).
Vendored dependencies retain their own notices, included with installations.
