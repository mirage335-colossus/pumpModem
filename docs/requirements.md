# Implementation coverage

This matrix is the acceptance record for version0.1, not a claim of completion of
the entire original design. Tests are in `tests/` and run with CTest.

| Requirement | Implemented behavior / current boundary |
|---|---|
| Linux and Windows portable compiled modem | C++20 CLI and native C++/FLTK GUI sharing the same transfer service, OpenSSL3; Linux ALSA dynamically optional, Windows WinMM. No Python/Tk build or runtime dependency. Linux built/tested; native Windows and hardware verification pending. |
| Offline installation transfer | CMake/CPack installs compiled executables and collects native application libraries. FLTK/OpenSSL and release C++ runtimes default to static linking; no interpreter, package manager, or dependency fetch is needed at the destination. Linux native CLI, actual GUI, and transitive shared-library relocation tested with empty PATH. See offline-installation.md for OS/CPU/glibc limits and validation.md for results. |
| Clipboard/text/screenshots/files | Text stdin/stdout and GUI clipboard copy; arbitrary binary files/images; screenshot files attachable. No native screenshot capture. |
| Interface isolation | Input only PCM WAV or analog audio, with automatic OS-default selection and optional endpoint override; no USB protocol, raw serial, Ethernet, routing, shell execution of content, or network listener. Hardware isolation is not guaranteed by software. |
| Receive cache256MB | Bounded in-memory GUI/C++ cache, replacement/eviction, explicit exclusive save. Codec workspaces have separate checks; not a total RSS promise. |
| Large keyfiles/pad | Production128MiB random header hashed to wrap an appended AES-GCM collection of1..128 named complete five-key sets. Legacy single-key files load as Default. Optional >1GiB pad remains supported by the CLI. No SSD erasure guarantee. |
| Repeatable packets | Random128-bit ID, ID CRC, authenticated/checksummed repeat flag. Shared transfer policy allows at most2s of incremental coded content airtime, or any payload of at most1byte. Preamble, framing, and fixed metadata are excluded. No64KiB cap, built-in repeater, or routed address. Repeatability does not override memory limits. |
| Packet preamble, MAC, FEC, encryption | >=5s balanced repetitive training, HMAC appended before RS, byte interleaving, entire preamble/frame/FEC encrypted. Unencrypted SHA256 integrity. Fixed RS-protected bootstrap retained with body FECoff. |
| Short-text dictionary | Fixed legacy dictionary and version2 variable-length prefix codec for payloads under256bytes. Common bytes use3bits; arbitrary bytes remain lossless. The shortest encoded form is used only when smaller than the original; no measured universal compression-ratio claim. |
| Cipher streams | AES256-CTR, HKDF per purpose/epoch, independent HMAC. Data, DSSS, and scrambler wired into modem; FHSS key purpose reserved but no RF hopping. |
| Common time search | Batch nearest-first whole-second epoch search±32768, sample timing refinement; continuous session defaults to±6seconds and bounds search at±60seconds per key. No continuous absolute nanosecond counter scheduling, time-source discipline, or billion-symbol search. |
| Timing cooldown |6s after GUI TX; sequential CLI audio TX waits6s. No coordination across separate concurrent processes or users sharing a key. |
| DSSS/scrambler/patterns | Independent keyed binary chip sequences plus differential QPSK. Exact named auto-keystream/auto-pattern/auto-tone, pattern3/4/6/8/12/16, and tone1/2/3/4/8/32/128/1024/4096/16384 modes. Tone omits the fixed-pattern sign sequence. Not a rotating high-order constellation decoder. |
| Automatic signal planning | Bandwidth and target C/N0 in dB-Hz resolve sample rate, carrier, and finite spreading using an explicit10dB symbol-energy estimate. Unsupported targets are reported; the calculation is not measured decoder sensitivity. Exact content/packet/total airtime and waveform/acquisition memory feasibility are available without allocating audio. |
| Near-best throughput | Not established. Reference1200bit/s at spreading factor1 and1200Hz nominal bandwidth before overhead; automatic weak-signal settings can be much slower. No adaptive QAM/APSK, LDPC, trellis, equalizer, resampling-clock tracking, or capacity benchmark. |
| Bandwidth | Automatic nominal1..192000Hz planning chooses48/96/192/384kHz sampling and a fitting carrier. Rectangular pulses have sidelobes; no certified occupied-bandwidth mask. |
| Weak-signal status | Exact few-bit overhead-free DBPSK and known aligned correlation; no authenticated identity, unknown-beacon discovery, day-long streaming, or normal/distress band monitor. |
| Multi-signal/band reception | Continuous rolling capture and sequential packets at the selected audio carrier. Pure band-scheduling utility tested; not connected to RF hardware. Whole-band concurrent decoding and scheduled retuning absent. |
| FHSS/SDR/IC-7100 | Not implemented. CLI rejects unsupported device types; no misleading RF controls. |
| Provisional decoding | Incomplete frames provide bounded provisional text after bootstrap correction and metadata checks; ticker replaces the row as bytes arrive and marks complete verified messages. Only verified content is eligible for normal copy/save. |
| Diagnostics | Waveform, FFT spectrum/false-color waterfall and measured differential constellation update continuously; frequency-labeled text scrolls right to left, with rate and CPU readouts. No separate synchronized scrambler constellation or CPU throttling control. |
| Simulation | GUI and CLI listen use the same continuous receiver for idle seeded noise and own transmitted waveforms; reception continues throughout simulation and resumes after real TX. CLI batch simulation also supports leading delay and fixed frequency offset. Named TXdBm/attenuation-dB presets assume -174dBm/Hz thermal density and10dB noise figure; sampled AWGN is normalized to Fs/2. Extreme presets may fail; no hardware sensitivity claim. |
| QR Level L | Vendored standards encoder, UTF8ECI, up to500Unicode scalars; live compose preview, SVG/PBM CLI, independent decode verification. |
| Explicit exclusions | No asymmetric crypto/key exchange, built-in repeaters, routable addressing, or rapid uncontrolled Doppler tracking. |
| Public release statements | Factual capability disclaimer, standard primitives, preserved source-specification wording clearly separated. No legal classification asserted. |

Further work needs an agreed interoperable waveform/performance target, a
hardware/channel test setup, and acceptance measurements. Passing deterministic
loopback tests cannot establish near-capacity performance, thermal sensitivity,
RF compliance, adversarial security, or reliable multi-day beacon detection.
