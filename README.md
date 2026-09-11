# Data Pump

A C++20 audio modem for moving clipboard text, screenshots, and files between
computers. It includes a compiled CLI, a native C++/FLTK desktop console, real
waveform and accelerated channel simulation, and a documented versioned packet format. Received content
stays in memory until an explicit save; no network listener or routable packet
addressing is implemented.

**Status: working reference implementation, version 0.5.** The audio/packet/crypto
pipeline works end to end and has automated regression tests. This is not yet
the complete high-performance modem described in the supplied specification.
In particular, near-capacity adaptive modulation, multi-signal radio scanning,
RF hopping, multi-day status reception, and hardware radio integrations remain
unimplemented. See the [requirements matrix](docs/requirements.md) for precise
coverage and boundaries. No unimplemented control is presented as functioning.

Version 0.5 adds an automatic waveform timebase, a consistent waterfall color
scale, accumulated transmit-symbol diagnostics, and GUI keyfile generation.
Audio frames are whitened to reduce data-dependent constellation bias. Both ends
of an audio link need matching 0.5 settings; the packet and keyfile formats are
unchanged. Bandwidth-derived clocks and oscillator impairments remain as in 0.4.

## Build and run

Build with a C++20 compiler, CMake 3.21+, OpenSSL 3 development files, and the
platform's desktop development libraries. The complete FLTK 1.4.5 source and QR
encoder are vendored. CMake does not fetch dependencies. On Debian/Ubuntu, build
packages are `build-essential cmake libssl-dev zlib1g-dev libzstd-dev libx11-dev
libxft-dev libxext-dev libxrender-dev libxcursor-dev libxfixes-dev libxinerama-dev`.
These are build-machine requirements; a packaged installation includes its
application libraries. Python and Tk are not required to build, package, or run
the software. If Python is available, CTest can also run optional CLI integration
tests.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure

./build/datapump-gui
./build/pump simulate --text 'CQ hello from Data Pump' --snr 12 --json
./build/pump tx --input circuit.kicad_pcb --output transfer.wav
./build/pump rx --input transfer.wav --save received.kicad_pcb
```

The last command refuses to overwrite an existing file. `rx --json` returns
verified content as base64 plus metadata and DSP diagnostics; without `--save`
or `--json`, binary file content remains unsaved. Text defaults to stdout, so
normal pipelines work. Treat unencrypted SHA-256 integrity checks as corruption
detection, not sender authentication.

```sh
printf 'clipboard text' | ./build/pump tx --input - --output message.wav
./build/pump rx --input message.wav
./build/pump tx --text 'hello' --device default
./build/pump listen --json
./build/pump listen --simulation '3dBm -120dB' --text 'hello' --json
```

`listen` continuously receives from the operating system's default audio device;
`--device` selects an override. Device enumeration is optional. Linux loads the common
ALSA `libasound.so.2`; when the global default fails, it discovers a card default
and, if needed, a format-converting endpoint on that card. Windows uses the system
WinMM audio API. WAV and simulation
operation work without audio hardware or the ALSA library. No radio is keyed or
transmitted by the automated tests. Physical audio transfer and Windows hardware
operation still require device testing. Use a capture long enough to contain the
complete five-second training sequence and packet.

## Desktop console

The desktop application is a compiled executable linked to the same C++ transfer
service as the CLI. FLTK and OpenSSL are linked statically by default. For a
copyable installation, install or package a release build:

```sh
cmake --install build --prefix "$PWD/build/DataPump-portable"
./build/DataPump-portable/bin/datapump-gui --self-check
./build/DataPump-portable/bin/datapump-gui
cmake --build build --target package
```

Copy that **entire directory** to another compatible computer and use its
`bin/datapump-gui` executable (`bin/datapump-gui.exe` on Windows). Remaining
native application libraries are collected during installation. The destination
needs no interpreter, package manager, environment setup, or internet access.
Linux bundles require the same CPU architecture and a compatible desktop with
glibc at least as new as the build computer; Windows needs its own build. See
[offline installation](docs/offline-installation.md) for packaging, verification,
and operating-system requirements.

The console provides text composition and explicit clipboard copy, file
attachment, Level L QR previews, continuous audio reception, a scrolling
frequency-labeled signal ticker, a false-color waterfall and live waveform,
spectrum and constellation displays. It includes named shared-key selection and
a bounded receive cache. WAV tools remain available through the CLI. It does not
open received files or execute received content. Screenshots can be attached as
ordinary image files; direct operating-system screenshot capture is not implemented.

The **Keyfile** menu opens existing files, generates and saves a new 128 MiB
keyfile with named entries in the background, or opens the loaded file's folder.
Existing files are never overwritten. The waveform defaults to twelve carrier
cycles; use the mouse wheel to zoom and double-click to reset. The waterfall
retains every FFT bin through peak pooling and uses one labeled color scale for
its entire history. Click it to clear the history and reset that scale.

Enter transmits audio; the checkbox changes this to Ctrl+Enter. Normal
transmission is the default. Selecting a simulation preset switches the same
receiver and Transmit control to a continuous noisy channel: the plots keep
updating while idle. Transmissions run at CPU speed through noisy complex
observations and the same symbol decoder, with virtual airtime reported separately.
On completion, all plots hold a payload-midpoint sample for two seconds, including
up to 2,048 accumulated received constellation points. All plots then return to
live input so new noise, lock attempts and transmissions remain visible.
Simulation defaults to 100 ppm relative crystal error and phase diffusion of
0.5 degrees per square root second. It models carrier coherence loss and changing
symbol timing, while assuming matched chip despreading. It does not provide an
oscillator tracking loop. Raw PCM acquisition is tested separately and remains
available in CLI WAV simulation.
Real audio reception pauses during transmission and resumes afterward.
Bandwidth and target C/N0 determine constellation size and automatic integration length; forced pattern
and tone modes are also available. Auto keystream is enabled with encryption.
The editor shows estimated airtime. Streaming transmission and reception use
bounded DSP storage independent of airtime. Compression is always chosen automatically.
Provisional ticker text is distinguished from validated cache entries.
The console enforces a six-second delay after actual encrypted transmission;
simulation and unencrypted transmissions have no cooldown. CLI encrypted audio
TX also waits six seconds after playback so sequential scripts inherit the delay; independent concurrent
processes are not globally coordinated.

## Shared keys and encrypted transfers

```sh
./build/pump keygen --output shared.key --key-names 'Home,Portable,Emergency'
./build/pump keys --keyfile shared.key
./build/pump tx --text 'private clipboard' --keyfile shared.key \
  --time 1800000000 --output encrypted.wav
./build/pump rx --input encrypted.wav --keyfile shared.key \
  --time 1800000002 --search-seconds 2 --json
```

The keyfile has a 128MiB random header whose hash derives the key that encrypts
the appended named collection. Each entry stores all five purpose keys. The GUI
selects one for transmission and tries loaded keys on reception; CLI selection uses
`--key-name Portable`. Legacy single-key files load as `Default`.
Optional external pads remain a CLI feature using `--pad path` at creation and load.
Keyfiles contain only this application's symmetric key sets. Never put
signing keys or other applications' secrets in this format.

AES-256-CTR encrypts the entire known training sequence, framed content,
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

# Automatic tuning and exact airtime, without allocating audio.
./build/pump estimate --text 'CQ hello' --bw 1200 --target-snr 40 --pattern auto-pattern

# Optical transfer, UTF-8, up to500 Unicode characters.
./build/pump qr --text 'clipboard text' --output clipboard.svg
./build/pump qr --text 'clipboard text' --format pbm --output clipboard.pbm

# Exactly three DBPSK symbols; no preamble, byte padding, authentication, or FEC.
./build/pump status-tx --bits 010 --spreading 128 --output status.wav
./build/pump status-rx --bits 010 --spreading 128 --input status.wav
```

`status-rx` reports correlation with an already aligned known signal. It does not
assert validated identity or implement continuous very-slow beacon monitoring.
The reference modem accepts nominal bandwidths from 1 Hz through 30 MHz,
forced lengths of 1..16,384 chips per symbol, optional independent encrypted spreading, and 20%/
60% RS parity or no body FEC. The fixed bootstrap retains its protection even
with `--fec off`; status mode is the route for truly overhead-free few-bit data.
`--target-snr` is the desired C/N0 in dBHz. The planner maximizes modeled throughput
across 4/8/16/32/64-point phase-and-amplitude constellations with geometry-based
noise and drift margins. It caps phase density at eight positions and payload
density at six bits per symbol, preserving at least two symbols for one byte.
Automatic integration can extend beyond 16,384 chips; this is not measured
receiver sensitivity or a capacity optimum.

Hardware sample rates do not set the modem's bandwidth or symbol rate. Audio
endpoints negotiate a supported clock and use a bounded band-limited converter
to/from the modem's internal clock. For bandwidth `B`, the internal sample rate
is `max(64, ceil(4B))` samples/second and the carrier is `0.75B`. The 64 Hz floor
preserves the fixed training schedule at very narrow bandwidths. Large downsampling
ratios use bounded filter stages. Different 44.1/48/96 kHz cards can share the
same modem settings. Conversion cannot restore frequencies outside the physical
card's passband. Live GUI audio rejects a selected band that exceeds the converter's
usable passband; actual analog response remains device-dependent. The 30 MHz
planning range permits future SDR integration; no SDR device backend is implemented.
`--snr` is simulated sample-power SNR in dB. Simulation presets instead specify
transmit dBm and channel attenuation, with thermal noise at 290K and a 10dB
receiver noise figure. With the default crystal impairment, extremely long
integration can lose coherence and fail even when ideal-clock AWGN would decode.
For a deliberately ideal oscillator diagnostic, use
`--clock-error-ppm 0 --phase-noise 0` with `simulate` or `listen`. This does not
demonstrate sensitivity or clock tracking on physical hardware.

Repeatable eligibility is at most two seconds of incremental encoded content
airtime, excluding preamble and fixed framing, with a minimum one-byte allowance.
There is no 64KiB eligibility rule. This allowance does not override memory limits
or establish that a setting can decode a particular channel.

The received text/file cache defaults to 256 MiB (`--cache-mb`); streaming DSP has
a separate 64 MiB workspace (`--dsp-mb`, shared by the key/epoch receiver bank).
Neither is a process RSS cap: encoded
packets, codec workspaces and caller-owned buffers can coexist. Live audio and
accelerated simulation never allocate PCM proportional to transmission duration.
Explicit batch WAV operations still use `--memory-mb` and can reject recordings
that exceed that workspace. There is no disk-backed receive cache or chunked file
transport.

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
`DATAPUMP_BUILD_GUI` and `DATAPUMP_PORTABLE` default to `ON`. CMake installation
and CPack TGZ/ZIP archives include both native executables and collected runtime
libraries. Use `-DDATAPUMP_BUILD_GUI=OFF` for a CLI-only build, or
`-DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE` to disable the optional Python tests.
See [offline installation](docs/offline-installation.md) for copy and verification
commands.

The source is separated into packet coding, cryptography, DSP/WAV, audio devices,
runtime policy, a shared transfer service, CLI orchestration, and GUI state. See [protocol](docs/protocol.md),
[modem](docs/modem.md), [QR](docs/qr.md), and
[release disclaimer](docs/disclaimer.md). The [validation record](docs/validation.md)
lists the tests actually run. The supplied design is preserved in
[original-specification.md](docs/original-specification.md) as source material,
not as a claim that every requested feature or assertion is implemented.

Application code is dedicated to the public domain under [CC0 1.0 Universal](LICENSE).
Copyright (c) 2026 mirage335. The vendored QR encoder retains its own
MIT notice. Data Pump is based in part on the work of the FLTK project. Its
license and static-linking exception, OpenSSL notices, and collected runtime
notices accompany the installation.
