# Data Pump

A C++20 audio modem for moving clipboard text, screenshots, and files between
computers. It includes a compiled CLI, a native C++/FLTK desktop console, real
waveform and sampled channel simulation, and a documented fixed-interval byte stream. Received source data uses bounded
storage, including a temporary spool while reception continues; permanent saves
are explicit. No network listener or routable addressing is implemented.

**Status: working reference implementation, version 0.7.2.** The audio/stream/crypto
pipeline works end to end and has automated regression tests. This is not yet
the complete high-performance modem described in the supplied specification.
In particular, near-capacity adaptive modulation, multi-signal radio scanning,
RF hopping, multi-day status reception, and hardware radio integrations remain
unimplemented. See the [requirements matrix](docs/requirements.md) for precise
coverage and boundaries. No unimplemented control is presented as functioning.

The transport sends one bit per pattern symbol. Pattern evidence alone admits
timing, carrier and keystream candidates. Nonempty text of up to 16 source bytes and
explicit bit drafts use raw transmission. Longer byte streams use a fixed 128-byte coding
interval after each alignment marker, with no packet header, received length,
or metadata parser. Legacy APSK and packet APIs have been removed; peers must use
the same local source/FEC profile. Short text uses the fixed bit dictionary.

**Development requirement:** preserve tiny dictionary/raw-bit messages, fixed
intervals and per-bit pending reception. Each bit may cost hours or longer;
neither added framing nor waiting for a whole message is harmless. See the
[message behavior contract and regression checks](docs/development.md).

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

Nonempty text of up to 16 source bytes uses the fixed short dictionary,
with no marker, padding, FEC or MAC: `e` sends exactly `001`. Explicit Binary/status input
also supports individual bits: `001` sends exactly three payload symbols.
Text longer than 16 bytes and attachments use the fixed interval format. Each 192-bit
alignment marker precedes 128 coded bytes, for 1,216 symbols per interval. RS20
uses 106 systematic bytes plus 22 parity bytes; RS60 uses 80 plus 48. Only
encrypted intervals reserve 32 systematic bytes for HMAC-SHA256. Public intervals
have no digest or checksum. Missing timed bits retain erasure positions so RS can
repair interior losses and a missing final coded bit without shifting later bytes.

For interval-coded sources, the default codec emits raw LZMA2 with a fixed 4 MiB dictionary,
including incompressible input, then pads the final data area with zeros.
It transmits no original size. Corrected bytes enter a bounded spool; decompression
runs only after iterative search establishes the physical stream end. A locally
selected uncompressed profile uses fixed validity/byte cells to preserve exact
bytes and trailing zeros. The bundled compression library needs no runtime download.

Explicit Binary/status input still sends exact raw bits, including leading zeros
and non-byte lengths, without markers, FEC, MAC or dictionary encoding. The
approximately two-second settling waveform and pulse tails carry no payload.
Accepted bits appear incrementally while reception is pending; they do not wait
for a complete byte or interval. The six-second whole-symbol absence rule is
the sole completion condition for raw and interval-coded receptions alike.
Marker recognition uses bounded fixed-cadence searches and a conservative `2^-84`
random-input evidence budget, independently of authentication. Unknown slots add
no marker confidence. See the [fixed stream protocol](docs/protocol.md).

The desktop has **Console**, **Modem flow**, **Transmission layout**, and
**Compression / raw bits** tabs.
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

Private patterns select the whole second at each symbol's scheduled start and
hold it for the complete pattern, including hours-long symbols. Symbols
beginning in the same second consume successive stream positions; the first
symbol beginning in a later second uses the newer timestamp. Data encryption
follows the same schedule. Fixed-size seek caches generate all streams on demand.
An already-running receiver can recognize a sufficiently confident symbol
despite obscuring noise in earlier symbols. Failed slots retain unknown positions. Complete-symbol iterative search ends a
stream when consecutive failed durations cover at least six seconds; one completed
failure suffices when the symbol itself lasts six seconds or more. EOF and codec
completion never substitute for observed absence. Later symbols can be acquired
independently.

The DSP workspace dropdown is an upper limit: 25%, 50% (default), or 75% of
available RAM. It does not request that amount of history. Received messages
and files have a separate 256 MiB quota. The GUI defaults to **Rate**
`3.6 kHz`, **Carrier** `1.5 kHz`, and **TX SNR (dB-Hz)** `60`.
Rate is the nominal chip-rate planning parameter, not a measured occupied
bandwidth: the default produces 1,800 chips/s and an ideal shaped spectrum of
375–2,625 Hz, including the 25% RRC rolloff. Rate presets still include `18 kHz`.
Changing Rate selects its default carrier: `1.5 kHz` for `3.6 kHz`, otherwise
`max(1500, 0.75 × rate)` Hz. The Carrier dropdown offers only the current rate's
default carrier; manual frequency entry remains available.
The **RX targets (dB-Hz)** comma-list also starts at `60`. Changing TX SNR to
a valid value replaces the RX list with that single matching target; the RX
list can then be edited independently. Search varies this list while holding
the selected rate, carrier, and pattern/tone mode fixed. Invalid RX input resets
the entire list to `60`.

Beside the gross bitrate, diagnostics show the **[Shannon-Hartley limit](https://disalw3.epfl.ch/teaching/signals_instruments_systems/ay_2025-26/lecture/SIS_25-26_W07_lecture.pdf#page=33)** for the
selected TX target and nominal bandwidth. This is ideal Gaussian-noise channel
capacity: `B * log2(1 + 10^(C/N0_dBHz / 10) / B)` bit/s, with `B` in Hz. At the
GUI defaults (3.6 kHz, 60 dB-Hz), it is about **29.2 kbit/s**. Actual payload
throughput depends on the waveform and coding overhead. CLI `estimate` reports
the same value as `shannon_capacity_bps`, using `--bw` and `--target-snr` (default
60 dB-Hz, including manual modem profiles); values beyond the numeric range are
reported as `null` (GUI: `Unavailable`).

Simulation feeds the actual PCM receiver with independent carrier phase and
sample offset and replays its presentation over three seconds. Signal progress
remains separate from source content: decompression waits for the physical
complete-symbol absence rule. Completed bytes use safe text/byte views and can be
copied or explicitly saved. Pattern evidence, RS corrections, unknown slots and
keyed authentication remain separate statuses; public source validation is not
authentication. Physical audio endpoints need matching local modem/source settings.

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
normal pipelines work. Public Reed–Solomon correction does not authenticate the
sender.

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
complete rounded two-second hardware-settling prefix and payload.

Transmit audio defaults to the right channel on stereo outputs and the sole
channel on mono outputs. Use `--no-mono` to send the same audio to both stereo
channels. The GUI's **Mono** toggle, below **Audio device**, controls the same
routing and starts enabled. Mono-only devices remain usable with either setting;
WAV output and simulation keep their existing single-channel waveform.

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

The **Compression / raw bits** tab shows the full dictionary bitstream for text
of 1–16 bytes: `quick brown` shows 70 bits. Its editor accepts up to 208 exact
bits and previews the expected dictionary text; `010` previews `t`. Editing this
field selects exact raw transmission. One- and two-bit inputs and incomplete
codes remain usable raw messages without invented characters or padding.
After physical completion, eligible short receptions show their dictionary
text and retain the exact bits for copying or retransmission. **Paste as message**
loads decoded bytes into Message. Console Binary keeps its 128-bit editing limit.

**Callsign** and **Grid** are convenience fields for the editable greeting in
Message. Clearing Message inserts `CQ CQ CQ`, followed by ` DE ` and Callsign
when present, ` GRID ` and Grid when present, then `. Please reply. ` including
the final space. For example, Callsign `N0CALL` and Grid `AA00aa` produce
`CQ CQ CQ DE N0CALL GRID AA00aa. Please reply. `. With both fields empty, no CQ
greeting is inserted. These values are transmitted only as the visible message
text; they do not create transmitted metadata fields.

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
The new draft remains available while transmission runs. Convenience fields and
reception identities are local application state, not modem fields.

Enter transmits audio; the send preference changes this to Ctrl+Enter. Normal
transmission is the default. Selecting a simulation preset switches the same
receiver and Transmit control to a continuous noisy channel: the plots keep
updating while idle. Transmissions run at CPU speed through noisy PCM, with
virtual airtime reported separately. The receiver runs independently through
idle noise and burst starts and derives timing, phase and spreading correlation
from its samples. Transmit start and completion do not reset its acquisition.
After computation completes, the entire transmission (including any hardware-settling prefix and the three-second echo-suppression noise)
replays chronologically over three seconds. Waveform, waterfall, constellation
and pattern evidence follow the same timeline. Each frame shows fresh measured
input I/Q and the retained pattern evidence at that transmission position.
Completed raw bits, source text, byte entries and available
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
and retained candidate/bit records within its workspace. The selected compressed
source profile always encodes LZMA2; it does not switch formats by source size.
Pattern results show a model log-evidence score, not an SNR or calibrated
confidence percentage. Source results separately report RS correction and keyed
authentication. Data pre-FEC accuracy compares observed bits with corrected bits;
unknown bits are counted separately and never treated as measured errors. RS
repair counts include data, keyed HMAC and parity bytes, including erased bytes
whose zero placeholder already matched the repaired value. Hardware settling supplies no lock
diagnostic. See [modem diagnostics](docs/modem.md).
Sequential hardware sends need the complete-symbol absence guard after waveform
and filter tails so reception can finish. This rule applies to public and keyed
streams; separate processes and hosts are not globally coordinated.

Ordinary received text and binary content remain messages. Selecting an attachment
forces Repeatable off and prepends `#ATTACHMENT### fileName.ext ###ATTACHMENT# `
to the source before compression. Only that explicit prefix creates a received
file entry. The application recognizes it after physical completion and source
decoding; the modem has no filename or length parser. Save remains an explicit
local action. Filenames are bounded UTF-8 basenames, with no path separators.

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

AES-256-CTR masks every wire bit, including markers, source bytes, HMAC and RS
parity. Only encrypted byte intervals carry HMAC-SHA256; exact raw-bit signaling
has no MAC. Independent HKDF-derived keys separate Data, MAC, DSSS, Scrambler and
reserved FHSS purposes. Symbol-start epoch/ordinal addresses bind interval MACs
and preserve cipher positions through missing symbols and drains. Automatically
timed hardware output schedules the first payload symbol on a whole second.
Receive time defaults to capture start; recordings require a suitable epoch and
finite search window. This is not a nanosecond-resolution absolute-time receiver.

Both peers need the same current waveform, fixed-interval format and local
source/FEC profile. Existing keyfiles remain usable. Reusing a key and schedule
position reveals XOR relationships between plaintexts and permits replay within
accepted clock windows. A canonical address is not a fresh nonce or durable replay
state. Independent interval MACs authenticate received segments, not an intended
whole-stream length. Large keyfiles do not guarantee SSD erasure or protect keys
from someone holding the complete keyfile and required pad. See
[cryptography](docs/crypto.md) and [security boundaries](docs/security.md).

## Scriptable tools

```sh
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
and `content_validated: false`. Manual sample-rate/carrier overrides use the same
pattern transport.

The reference modem accepts nominal bandwidths from 1 Hz through 30 MHz and
forced durations of 1..16,384 chips. `--target-snr` is the desired C/N0 in dB-Hz,
defaulting to 60 (60 dB/1Hz). Large messages and attachments default to 60%
Reed–Solomon parity overhead (`--fec 60`).
Auto planning uses one bit per pattern symbol, a 16/32/64-chip floor based on
in-band SNR and an initial 18 dB integrated-energy model. Forced short patterns
preserve their duration and report unsupported automatic confidence assumptions
when they miss the applicable floor. Integration may extend
past 16,384 chips, subject to numeric, workspace and acquisition limits. This
is not measured receiver sensitivity or a capacity optimum.

Hardware sample rates do not set the modem's bandwidth or symbol rate. Audio
endpoints negotiate a supported clock and use a bounded band-limited converter
to/from the modem's internal clock. With rate parameter `B` and selected carrier
`fc`, automatic planning uses `Fs = ceil(max(64, 4B, 4fc))` samples/second. The CLI
retains its default `B=1200` and carrier rule `max(1500, 0.75B)` Hz; the GUI
uses the carrier control described above. The usual 6 kHz floor represents the
1500 Hz carrier at narrow rates; symbol timing and integration remain based on
`B`. Both transmit and receive planning use the selected carrier before choosing
pattern lengths. Large downsampling
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
Neither is a process RSS cap: encoded source areas, codec workspaces and
caller-owned buffers can coexist. Live audio and
sampled simulation retain a short receive window rather than the entire PCM
transmission. Pattern-search workspace can grow with the longest symbol; an
unsupported configuration is rejected before allocation.
Simulation CPU work does grow with the number of samples, so long integrations
and high sample rates can take substantial time and remain cancellable.
Explicit batch WAV operations still use `--memory-mb` and can reject recordings
that exceed that workspace. Corrected source intervals drain to a quota-limited temporary-file spool until
physical completion; it is separate from the application receive cache.

## Development and portability

Start with the [message behavior preservation contract](docs/development.md).
It records the required short-text/binary paths, physical-end boundary and
pending GUI behavior, with focused regression commands.

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

The source is separated into fixed interval coding, cryptography, DSP/WAV, audio devices,
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
