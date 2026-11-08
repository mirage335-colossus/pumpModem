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

The canonical Robust implementation is the native three-adjacent-carrier
`IndependentApskV2` waveform, shaping, streaming channel, and Helper Generator
stack. Python `WeakReceiver` acquisition and `PracticalTrackingReceiver`
tracking are sequential stages, not an integrated end-to-end receiver. Robust
is not connected to the GUI and remains unavailable there; existing GUI controls
are retained pending reconnection. See [current architecture and status](docs/robust-current.md).

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
Robust's existing controls do not imply that its receive/transmit backend is
currently available.

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
