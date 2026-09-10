# Data Pump

A C++20 audio modem for moving clipboard text, screenshots, and files between
computers. It includes a compiled CLI, a thin Python/Tk desktop console, real
waveform simulation, and a documented versioned packet format. Received content
stays in memory until an explicit save; no network listener or routable packet
addressing is implemented.

**Status: working reference implementation, version 0.1.** The audio/packet/crypto
pipeline works end to end and has automated regression tests. This is not yet
the complete high-performance modem described in the supplied specification.
In particular, near-capacity adaptive modulation, multi-signal radio scanning,
RF hopping, multi-day status reception, and hardware radio integrations remain
unimplemented. See the [requirements matrix](docs/requirements.md) for precise
coverage and boundaries. No unimplemented control is presented as functioning.

## Build and run

The CLI needs a C++20 compiler, CMake 3.20+, and OpenSSL 3 development files.
There is no dependency download during CMake configuration. QR encoding is
vendored with its MIT license. On Debian/Ubuntu, the usual build packages are
`build-essential cmake libssl-dev`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure

./build/pump simulate --text 'CQ hello from Data Pump' --snr 12 --json
./build/pump tx --input screenshot.png --kind screenshot --output transfer.wav
./build/pump rx --input transfer.wav --save received.png
```

The last command refuses to overwrite an existing file. `rx --json` returns
verified content as base64 plus metadata and DSP diagnostics; without `--save`
or `--json`, binary file content remains unsaved. Text defaults to stdout, so
normal pipelines work. Treat unencrypted SHA-256 integrity checks as corruption
detection, not sender authentication.

```sh
printf 'clipboard text' | ./build/pump tx --input - --output message.wav
./build/pump rx --input message.wav
./build/pump devices
./build/pump tx --text 'hello' --device default
./build/pump rx --device default --seconds 30 --json
```

Live audio explicitly requires `--device`. Linux dynamically loads the common
ALSA `libasound.so.2`; Windows uses the system WinMM audio API. WAV and simulation
operation work without audio hardware or the ALSA library. No radio is keyed or
transmitted by the automated tests. Physical audio transfer and Windows hardware
operation still require device testing. Use a capture long enough to contain the
complete five-second training sequence and packet.

## Desktop console

For a copyable installation, make a portable bundle on a working computer:

```sh
python3 tools/bundle_portable.py --pump build/pump --output build/DataPump-portable
./build/DataPump-portable/datapump-gui --self-check
./build/DataPump-portable/datapump-gui
```

Copy that **entire directory** to another compatible computer and use its
`datapump-gui` launcher (`datapump-gui.cmd` on Windows). The bundle carries its
own Python, standard library, Tk, Tcl/Tk resources, and native application
dependencies. The destination needs no Python installation, package manager,
virtual environment, or internet access. Linux bundles require the same CPU
architecture and a compatible Linux desktop with glibc at least as new as the
build computer; Windows needs its own Windows bundle. See
[offline installation](docs/offline-installation.md) for packaging, local runtime
overlays, verification, and operating-system dependencies.

For development directly from source:

```sh
python3 gui/datapump_gui.py --pump build/pump
```

Source execution and bundle creation need an existing Python 3.10+ runtime with
Tk available locally. An already unpacked matching runtime can supply Tk when
it is absent from the build computer's Python installation. No runtime package
is fetched by the bundler. The CLI itself needs no Python.
The console provides text composition and explicit clipboard copy, file and
screenshot attachment, Level L QR previews, audio capture/playback, WAV import/
export, seeded simulation, waveform/spectrum/constellation diagnostics, shared
key selection, and a bounded receive cache. It does not open received files or
execute received content. Captured screenshots can be attached as ordinary image
files; direct operating-system screenshot capture is not implemented.

Enter transmits audio; the checkbox changes this to Ctrl+Enter. Normal
transmission is the default, and simulation is a separate explicit button.
Encrypted pattern and DSSS controls are disabled until a keyfile is selected.
The console runs one modem operation at a time and enforces a six-second delay
after successful transmission. CLI audio TX also waits six seconds after
playback so sequential scripts inherit the delay; independent concurrent
processes are not globally coordinated.

## Shared keys and encrypted transfers

```sh
./build/pump keygen --output shared.key
./build/pump tx --text 'private clipboard' --keyfile shared.key \
  --time 1800000000 --output encrypted.wav
./build/pump rx --input encrypted.wav --keyfile shared.key \
  --time 1800000002 --search-seconds 2 --json
```

The keyfile has a 128MiB random header. To bind it to an existing random pad
larger than 1GiB, supply `--pad path` at creation and every subsequent load.
Keyfiles contain only this application's symmetric master secret. Never put
signing keys or other applications' secrets in this format.

AES-256-CTR encrypts the entire balanced training sequence, framed content,
HMAC-SHA256 tag, and Reed–Solomon bytes. Independent HKDF-derived keys separate
the data, MAC, DSSS, scrambler, and reserved FHSS streams. All streams use the
same candidate whole-second transmission anchor. Physical timing is refined to
the sample; this is not a nanosecond-resolution absolute-time receiver. Receive
time defaults to the start of capture; for saved WAVs use the original TX epoch
or a nearby epoch with the search window. Search is nearest-first and bounded by
±32,768 seconds when encrypted.

CTR position reuse reveals the XOR of the affected plaintext positions; the
independent MAC still protects authenticity. A clock search window is not replay
prevention. Large keyfiles do not guarantee SSD erasure. The format does not
protect a secret from someone who obtains its entire keyfile and required pad.
See [cryptography](docs/crypto.md) and [security boundaries](docs/security.md).

## Scriptable tools

```sh
# Packet bytes on stdout; useful for testing and external scripting.
printf 'hello' | ./build/pump pack --input - | ./build/pump unpack --input -

# Optical transfer, UTF-8, up to500 Unicode characters.
./build/pump qr --text 'clipboard text' --output clipboard.svg
./build/pump qr --text 'clipboard text' --format pbm --output clipboard.pbm

# Exactly three DBPSK symbols; no preamble, byte padding, authentication, or FEC.
./build/pump status-tx --bits 010 --spreading 128 --output status.wav
./build/pump status-rx --bits 010 --spreading 128 --input status.wav
```

`status-rx` reports correlation with an already aligned known signal. It does not
assert validated identity or implement continuous very-slow beacon monitoring.
The reference modem supports 1.2/2.4/22.05/24kHz nominal bandwidth settings,
1..16,384 chips per symbol, optional independent encrypted spreading, and 20%/
60% RS parity or no body FEC. The fixed bootstrap retains its protection even
with `--fec off`; status mode is the route for truly overhead-free few-bit data.
`--snr` is simulated sample-power SNR in dB, not a dB/Hz receiver squelch setting.

The default256MiB memory budget is checked by each codec's workspace estimator;
it is not an operating-system process RSS cap. Finite captures, decoded packets,
and caller-owned buffers can coexist. Large files and long recordings may be
rejected before processing. There is no disk-backed receive cache, chunked file
transport, or streaming multi-day integration in this release.

## Development and portability

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DDATAPUMP_SANITIZERS=ON
cmake --build build-sanitize --parallel
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-sanitize --output-on-failure
```

Leak detection is disabled above for environments where ptrace prevents
LeakSanitizer from running; on a normal host enable it. Unit tests retain checks
in release builds. Tests cover Reed–Solomon correction, hostile framing, memory
limits, crypto vectors and tampering, large keyfiles, Unicode/QR, noisy waveform
acquisition, frequency/sample offsets, WAV parsing, CLI file transfer, and GUI
cache/cooldown behavior. Seeded tests are reproducible within the same C++
standard-library implementation.

For Windows, use a C++20 Visual Studio toolchain and OpenSSL 3 (for example the
`openssl:x64-windows-static` vcpkg port). Configure with your vcpkg toolchain and
`VCPKG_TARGET_TRIPLET=x64-windows-static`, then build Release and run CTest with
`-C Release`. Static OpenSSL can also be requested on Linux with
`-DOPENSSL_USE_STATIC_LIBS=TRUE`; a fully static libc binary is not assumed.
Windows code is maintained alongside Linux code but cannot be hardware-verified
by the Linux test environment. MSVC builds embed a UTF-8 process manifest for
Windows10 version1903 or newer; non-ASCII paths on older Windows are unsupported.
Ordinary CPack packages and `cmake --install build` install the executable and
GUI source launcher; these require host runtimes and do not contain Python/Tk.
Use the [portable bundler](docs/offline-installation.md) for an installation that
can be copied to compatible computers without fetching dependencies.

The source is separated into packet coding, cryptography, DSP/WAV, audio devices,
runtime policy, CLI orchestration, and GUI state. See [protocol](docs/protocol.md),
[modem](docs/modem.md), [QR](docs/qr.md), and
[release disclaimer](docs/disclaimer.md). The [validation record](docs/validation.md)
lists the tests actually run. The supplied design is preserved in
[original-specification.md](docs/original-specification.md) as source material,
not as a claim that every requested feature or assertion is implemented.

Licensed under MIT; the vendored QR encoder retains its own MIT copyright notice.
