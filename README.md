# Data Pump

A C++20 audio modem for moving clipboard text, screenshots, and files between
computers. It includes a compiled CLI, a native C++/FLTK desktop console, real
waveform and sampled channel simulation, and a documented compact packet format. Received content
stays in memory until an explicit save; no network listener or routable packet
addressing is implemented.

**Status: working reference implementation, version 0.7.2.** The audio/packet/crypto
pipeline works end to end and has automated regression tests. This is not yet
the complete high-performance modem described in the supplied specification.
In particular, near-capacity adaptive modulation, multi-signal radio scanning,
RF hopping, multi-day status reception, and hardware radio integrations remain
unimplemented. See the [requirements matrix](docs/requirements.md) for precise
coverage and boundaries. No unimplemented control is presented as functioning.

The default transport now sends one bit per pattern symbol. Pattern-template
evidence alone admits timing, carrier and keystream candidates, joins symbols,
and marks signal boundaries. It does not need a preamble, packet header,
checksum or a clean phase/amplitude constellation to acquire a signal. Byte packet APIs remain available for scripting. Legacy APSK transport, its
fixed training prefix and final-symbol padding have been removed; explicit
APSK configurations are rejected.

Encrypted pattern chips use circular I/Q noise with private amplitude and phase,
removing the fixed squared-carrier signature of the previous +/-1 mapping.
The receiver still acquires from pattern evidence alone, with template-energy
normalization. The hardware prefix uses the same noise distribution and chip
cadence. This does not hide all physical bandwidth, chip-timing or burst-edge
characteristics. Tone modes force encryption and private spreading off and are
not LPI modes. The private waveform has changed; peers must use this updated build.

Patterns lasting at least 16 complete chip times now use 25% root-raised-cosine
pulses at the existing chip and payload bit rates. The 1.2 kHz nominal setting
still sends 600 chips/s, with an approximately 750 Hz shaped spectrum. A finite
16-chip filter adds eight chip times at each burst edge (26.7 ms total at
600 chips/s), and remains continuous across settling and payload symbols.
This changes the emitted waveform, while plaintext-to-ciphertext encryption,
keystream mixing, stream addresses and logical chips remain unchanged. Shorter
manual patterns and tone modes retain their previous pulses. Both peers must
match `Config::pulse_shaping` (default `true`); no negotiation field is sent.
[Pulse shaping and its measured limits](docs/modem.md#pulse-shaping) describe
the small crest-limiter loss and the remaining observable timing structure.

Automatic planning uses two codewords and a modeled 18 dB integrated
symbol-energy target. The chip floor is 16 at in-band SNR of at least 30 dB,
32 at 24 dB, and otherwise 64; automatic tones retain 64. Shorter automatic
profiles require whole chips at the actual sample clock, preserving the chip
update cadence. Compact orthogonal private bins reserve at least 32 chips;
other profiles outside the 256-sample exact-fit range retain 64. At 12 kHz and
80 dB-Hz this yields 375 gross bit/s, four times the previous fixed floor.
Both endpoints must use matching plans. [Throughput limits](docs/throughput.md)
explain the remaining bulk-transfer constraint and validation.
The model score is evidence for comparing pattern hypotheses;
it is not a calibrated false-alarm probability, measured SNR, authentication or
a demonstration of extreme weak-signal performance.

Text shorter than 16 original bytes uses the built-in bit-prefix dictionary and
sends exactly the resulting bits, without byte padding, packet fields or FEC.
For example, `e` is three transmitted payload bits. A hardware-settling prefix
lasts approximately five seconds, rounded to the nearest whole payload-symbol
duration; symbols longer than ten seconds need no prefix. The prefix helps
external gain control and muting settle, but supplies no acquisition evidence
and adds no payload bits. Keyed preamble noise bytes pass through Data-stream
encryption and every enabled Scrambler/DSSS byte mask before waveform mapping. Each layer keeps its existing purpose and epoch key, selecting a
separate CTR range with the fixed `preamble` counter pad. Payload stream
positions still start at zero after the prefix. The Binary editor preserves exact
0/1 drafts, including incomplete bytes and leading zeros, and sends those bits
directly. Larger messages and attachments retain the compact packet codec,
compression and optional FEC downstream of pattern acquisition. No dictionary
identifier is transmitted. The compression library is built statically from
vendored source; no runtime download is required.

Compact packets on the pattern transport add a repeated 96-bit recovery word
after each complete 256 encoded bytes, with or without encryption and FEC.
The 24-byte insertion costs 9.375% per full block and adds nothing below 256
encoded bytes. Markers are inserted before encryption, so keyed transmissions
encrypt every marker bit with the rest of the stream. After existing decryption,
a narrow fixed-cadence search can
restore byte alignment after a net shift of up to seven plaintext bits, leaving
the damaged region to FEC and whole-packet integrity. Pattern decoding remains
the sole source of timing and keystream alignment. Markers create no new packet
parser entry points. The word is derived at runtime from a stored label to
reduce accidental recognition in program/source transfers.
Raw bits and text below 16 original bytes remain unchanged. Both pattern peers
need the same current waveform and recovery convention; byte packet APIs
retain their existing formats. See
[byte-boundary recovery](docs/protocol.md#periodic-byte-boundary-recovery).

The desktop has **Console**, **Modem flow**, and **Transmission layout** tabs.
The inspection views show the two pattern codewords, their modeled distances,
and exact bit/symbol counts. Long patterns use a bounded illustrative prefix;
keyed previews use clearly labeled public example streams. These design plots
are separate from measured receive evidence. See
[pattern constellation geometry](docs/pattern-constellation.md) and
[the inspection views](docs/inspection.md).

The receiver keeps a short baseband history and bounded timestamped candidate
records instead of retaining a whole transmission waveform. Its frequency,
timing and keystream searches are finite. The default carrier bank spans
±1/(2T), where T is symbol duration; arbitrary drift and whole-band scanning
are not implemented. When a full symbol window exceeds the FFT workspace, a bounded correlator
searches an explicit system-clock start window instead. Tests cover a four-hour
symbol prefix with bounded storage and actual short-signal PCM recovery through
this fallback. Coverage is finite: broad unknown-start acquisition and real-time
performance across a large epoch bank are not established.

The DSP workspace dropdown is an upper limit: 25%, 50% (default), or 75% of
available RAM. It does not request that amount of history. Received messages
and files have a separate 256 MiB quota. The GUI defaults to **Bandwidth**
`2.4 kHz` and **TX SNR (dB-Hz)** `80`; the bandwidth presets include `18 kHz`.
The **RX targets (dB-Hz)** comma-list also starts at `80`. Changing TX SNR to
a valid value replaces the RX list with that single matching target; the RX
list can then be edited independently. Search varies this list while holding
the selected bandwidth and pattern/tone mode fixed. Invalid RX input resets
the entire list to `40`.

Simulation feeds the actual PCM receiver with independent carrier phase and
sample offset, and presents the result over three seconds. Decoded pattern
messages show one text row, even when their compressed bits do not fill whole
bytes. Other receptions show a byte view for whole bytes or an exact-bit row
for a partial final byte. Non-text bytes use `\xNN` escapes. Click a row
to copy, or choose **Paste as message** to load its exact bytes into Message
and inspect the first 16 bytes in Binary. Their rows
show **Pattern score**, with no checksum/FEC claim. Packet-validated results
retain their separate integrity and data-accuracy information. Physical audio
endpoints must use compatible carrier and modem settings.

## Build and run

Build with a C++20 compiler, CMake 3.21+, OpenSSL 3 development files, and the
platform's desktop development libraries. The FLTK 1.4.5 source and QR
encoder are vendored; see [dependency source and version notes](third_party/README.md).
CMake does not fetch dependencies. On Debian/Ubuntu, build
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
content as base64, exact recovered bits, validation flags and DSP diagnostics; without `--save`
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

FLTK remains the default GUI. An optional Rev backend uses the pinned upstream
`clean` branch, shared semantic controls/controller and the same bitmap plot
producers. Build it separately with `-DDATAPUMP_GUI_BACKEND=rev`; see the
[Rev build and validation notes](docs/rev-backend.md) for its C++23 toolchain,
software-OpenGL requirements and current release boundaries.

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

The console provides adjacent message and binary editors, explicit clipboard copy, file
attachment, Level L QR previews, continuous audio reception, a scrolling
frequency-labeled signal ticker with reception percentages, a waterfall and live waveform,
and adjacent I/Q and pattern-evidence plots. The pattern plot compares the receiver's
retained pattern 0 and pattern 1 scores against noise and follows simulation replay.
In pattern modes, receive I/Q stays in input coordinates as acquisition changes.
Hardware transmission shows emitted chip points and measured outgoing I/Q during
settling and between chip boundaries; the evidence panel says **RX paused during TX** because audio
input pauses during playback. Pattern scores wait for complete receiver windows.
It includes named shared-key selection and
a bounded receive cache. WAV tools remain available through the CLI. It does not
open received files or execute received content. Screenshots can be attached as
ordinary image files; direct operating-system screenshot capture is not implemented.

The **Keyfile** menu opens existing files, generates and saves a new 128 MiB
keyfile with named entries in the background, or opens the loaded file's folder.
Existing files are never overwritten. The waveform defaults to four carrier
cycles; use the mouse wheel to zoom and double-click to reset. The waterfall
retains every FFT bin through peak pooling and uses one labeled intensity scale for
its entire history. Click it to clear the history and reset that scale.

Color is enabled by default when supported: muted cyan data field values, waveform
traces, and constellation points, softer gray text, and a subdued multihue waterfall
progressing from black through dark blue, blue, cyan, green, yellow, orange, and
red to soft off-white. Every constellation point uses the same hue. Reference
marks, status labels, and signed pattern diagrams stay grayscale. Run
`datapump-gui --monochrome` for grayscale; `--color` re-enables color, and the last
of these switches wins. Color changes presentation only. Grayscale uses the
original measured waterfall intensities and original text contrast, not
desaturated false color. If FLTK cannot select an RGB visual, it uses grayscale.

The dropdown above the QR preview offers **Normal**, **Dim red**, **Dark red**,
and **Off**, with Dark red selected on every startup. The dim settings replace
the bright white QR background with dark red; Off makes the preview black. Only
the QR preview changes, and the selection is kept while you edit the message.
Monochrome displays offer Dim gray and Dark gray instead, with Dark gray selected
on startup. Choose Normal to restore the full-contrast QR code for scanning.
Click the QR code to expand it to fill the app window. Click again or press
Escape to return to its original size. The expanded view keeps the selected brightness.

Both editors are available together, with no Message/Binary source selector.
For example, entering `01000001` in Binary produces `A` in Message. Binary is
limited to 128 meaningful bits, with optional whitespace between groups. While
an attachment is selected, both message draft views are inactive; Use text
restores the synchronized draft.

The **Compression / raw bits** tab shows the fixed lowercase codebook separately
from the message's byte representation. For example, the three payload bits
`010` decode as `t`, whose ASCII byte is `01110100`. Enter any **1 to 4 bits** in
this tab to transmit that exact pattern, preserving leading zeros and incomplete
dictionary codes. The complete short codes are space=`000`, e=`001`, t=`010`,
a=`011`, o=`100`, i=`1010`, and n=`1011`; other patterns remain valid raw input.
Select a completed reception to see its original bits, choose **Copy raw bits**
to copy them, or **Use received bits** to load a 1 to 4 bit pattern for sending
again. **Paste as message** on Console continues to load decoded text and show
its byte representation in Binary. Console Binary supports longer raw drafts,
up to 128 bits.

**Callsign** and **Grid** are convenience fields for the editable greeting in
Message. Clearing Message inserts `CQ CQ CQ`, followed by ` DE ` and Callsign
when present, ` GRID ` and Grid when present, then `. Please reply. ` including
the final space. For example, Callsign `N0CALL` and Grid `AA00aa` produce
`CQ CQ CQ DE N0CALL GRID AA00aa. Please reply. `. With both fields empty, no CQ
greeting is inserted. These values are transmitted only as the visible message
text; they do not set separate packet metadata.

**Repeatable** adds `REPEATABLE-XXXXXXXX ` before that greeting or before your
message when no greeting is present. The eight-character identifier uses random
uppercase and lowercase consonants (excluding Y/y) and digits. Every message
content edit, including insertion, deletion or paste, generates a fresh identifier.
It turns off automatically for an attachment or when the message exceeds 256
payload bytes, including UTF-8, greeting and the 20-byte repeatable prefix.
With all three convenience fields empty or off, no text or spaces are inserted.
When a text transmission starts, Message clears to the current convenience text
and **Previous message - click to paste** becomes available to restore the exact
previous message for editing or retransmission, preserving its repeatable
identifier. Editing the recalled message generates a fresh identifier.
The new draft remains available while transmission runs. Packet APIs retain
explicit metadata options; default text under 16 bytes sends only dictionary
bits and therefore has no separate metadata fields.

Enter transmits audio; the send preference changes this to Ctrl+Enter. Normal
transmission is the default. Selecting a simulation preset switches the same
receiver and Transmit control to a continuous noisy channel: the plots keep
updating while idle. Transmissions run at CPU speed through noisy PCM, with
virtual airtime reported separately. The receiver runs independently through
idle noise and burst starts and derives timing, phase and spreading correlation
from its samples. Transmit start and completion do not reset its acquisition.
After computation completes, the entire transmission (including any hardware-settling prefix)
replays chronologically over three seconds. Waveform, waterfall, constellation
and pattern evidence follow the same timeline. Each frame shows fresh measured
input I/Q and the retained pattern evidence at that transmission position.
Completed raw bits, dictionary text, packet text, file entries and available
data accuracy are released at the three-second deadline. Neither
copy nor save can expose the prepared result early.
Starting another transmission or selecting Stop replay interrupts presentation
and discards its undelivered results. Earlier completed receptions remain in
memory. Calls queued during computation receive separate consecutive replays.
Delayed polling delivers due browser events in order without extending replay.
All plots return to live input afterward so new noise, lock attempts and
transmissions remain visible.
Simulation defaults to 100 ppm relative crystal error and phase diffusion of
0.5 degrees per square root second. It models carrier coherence loss and changing
symbol timing and chip correlation. Startup includes an arbitrary sample offset
and carrier phase. It does not provide an oscillator tracking loop; unsuccessful
acquisition remains an unsuccessful simulation.
Real audio reception pauses during transmission and resumes afterward.
Bandwidth and the TX target C/N0 determine binary-pattern integration length; forced pattern
and tone modes are also available. With encryption, Auto Pattern and the forced
pattern lengths use private keystream fragments, so pattern evidence also
distinguishes the receive key. Auto keystream remains an equivalent choice.
The editor shows estimated airtime. Streaming transmission uses bounded chunks; reception bounds waveform history
and retained candidate/bit records within its workspace. Compression is always chosen automatically.
Pattern-only results show a model log-evidence score, not an SNR or calibrated
confidence percentage. Short text and exact raw bits do not require packet
validation to become copyable. Packet results additionally show **Data …
pre-FEC**, measured against the verified encoded body; it excludes bootstrap
and parity bits. Hardware settling supplies no preamble-lock diagnostic. See [modem diagnostics](docs/modem.md).
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

AES-256-CTR masks every wire bit after transport recovery markers are inserted,
including the markers themselves; larger packet messages retain their
HMAC-SHA256 and Reed–Solomon processing. Short pattern-only messages have no MAC. Independent HKDF-derived keys separate
the data, MAC, DSSS, scrambler, and reserved FHSS streams. All streams use the
same candidate whole-second transmission anchor. Physical timing is refined to
the sample; this is not a nanosecond-resolution absolute-time receiver. Receive
time defaults to the start of capture; for saved WAVs use the original TX epoch
or a nearby epoch with the search window. Search is nearest-first and bounded by
±32,768 seconds when encrypted.

CTR position reuse reveals the XOR of the affected plaintext positions. A packet
MAC protects larger authenticated messages; raw bits and short dictionary text
have no cryptographic authentication. A clock search window is not replay
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

# Three payload symbols plus the hardware-settling prefix; no padding, MAC or FEC.
./build/pump status-tx --bits 010 --output status.wav
./build/pump status-rx --bits 010 --input status.wav

# Search these receive targets only, with the selected bandwidth and pattern.
./build/pump listen --receive-targets "40, 6, -6" --bw 1200 --pattern auto-pattern --json
```

With automatic pattern settings, `status-rx` discovers the raw bit string from
pattern evidence and only then compares it with `--bits`. A wrong expected
string does not steer acquisition. Its JSON reports exact bits, a model score
and `packet_validated: false`. Manual sample-rate/carrier overrides use the same
pattern transport.

The reference modem accepts nominal bandwidths from 1 Hz through 30 MHz and
forced durations of 1..16,384 chips. `--target-snr` is the desired C/N0 in dB-Hz.
Auto planning uses one bit per pattern symbol, a 16/32/64-chip floor based on
in-band SNR and an initial 18 dB integrated-energy model. Forced short patterns
preserve their duration and report unsupported automatic confidence assumptions
when they miss the applicable floor. Integration may extend
past 16,384 chips, subject to numeric, workspace and acquisition limits. This
is not measured receiver sensitivity or a capacity optimum.

Hardware sample rates do not set the modem's bandwidth or symbol rate. Audio
endpoints negotiate a supported clock and use a bounded band-limited converter
to/from the modem's internal clock. For bandwidth `B`, the internal sample rate
is `max(6000, ceil(4B))` samples/second and the carrier is `max(1500, 0.75B)` Hz.
The 6 kHz floor represents the real 1500 Hz audio carrier at narrow bandwidths;
symbol timing and integration remain based on bandwidth. Large downsampling
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
For independent wall clocks, `simulate --time TX_SECONDS --receiver-time RX_SECONDS`
searches from the receiver's epoch using `--search-seconds`; an encrypted transmitter
outside that finite window fails reception. Shared modem settings and keys remain
receiver configuration, independent of waveform synchronization.

CLI repeatable eligibility is at most two seconds of incremental encoded content
airtime, excluding preamble and fixed framing, with a minimum one-byte allowance.
There is no 64KiB eligibility rule. This allowance does not override memory limits
or establish that a setting can decode a particular channel.

The received text/file cache defaults to 256 MiB (`--cache-mb`); streaming DSP has
a separate 64 MiB workspace (`--dsp-mb`, shared by the key/epoch receiver bank).
Neither is a process RSS cap: encoded
packets, codec workspaces and caller-owned buffers can coexist. Live audio and
sampled simulation retain a short receive window rather than the entire PCM
transmission. Pattern-search workspace can grow with the longest symbol; an
unsupported configuration is rejected before allocation.
Simulation CPU work does grow with the number of samples, so long integrations
and high sample rates can take substantial time and remain cancellable.
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

Automatic acquisition regressions use public circular I/Q patterns and privately seeded
circular I/Q patterns to verify detection from independent PCM samples. They must not
force tone patterns or require tone simulation to succeed. Tone operation needs
suitable synchronization and hardware conditions, such as GNSS timing, low
frequencies or high symbol rates; see the
[regression signal policy](docs/validation.md#automatic-regression-signal-policy).

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
libraries. `-DDATAPUMP_GUI_BACKEND=fltk` selects the single compiled GUI backend;
FLTK is the default, and `rev` selects the optional Rev profile described above.
Unsupported or multiple
selections fail configuration. `datapump-gui --version` reports the compiled
backend. Use `-DDATAPUMP_BUILD_GUI=OFF` for a CLI-only build, or
`-DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE` to disable optional Python tests in a
FLTK/CLI build. Rev requires Python for resource embedding during the build.
See [offline installation](docs/offline-installation.md) for copy and verification
commands.

The GUI uses a shared instrument-panel style: fixed-width text, flat borders,
color where supported, and explicit status labels. `--monochrome` selects the
grayscale presentation without changing control types or per-element settings.
See the
[minimal GUI contract](docs/gui-contract.md) and [architecture review](docs/gui-architecture.md)
for the shared widget interface and extension workflow. FLTK and Rev both use
the same declarations, controller, record and document presentation, layout and
bitmap producers. Features using these primitives are added in shared code and
reach both backends; adapters handle native widgets, drawing and platform services.

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
