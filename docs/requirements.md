# Implementation coverage

This matrix is the acceptance record for version0.1, not a claim of completion of
the entire original design. Tests are in `tests/` and run with CTest.

| Requirement | Implemented behavior / current boundary |
|---|---|
| Linux and Windows portable compiled modem | C++20 CLI and native C++/FLTK GUI sharing the same transfer service, OpenSSL3; Linux ALSA dynamically optional, Windows WinMM. No Python/Tk build or runtime dependency. Linux built/tested; native Windows and hardware verification pending. |
| Offline installation transfer | CMake/CPack installs compiled executables and collects native application libraries. FLTK/OpenSSL and release C++ runtimes default to static linking; no interpreter, package manager, or dependency fetch is needed at the destination. Linux native CLI, actual GUI, and transitive shared-library relocation tested with empty PATH. See offline-installation.md for OS/CPU/glibc limits and validation.md for results. |
| Clipboard/text/screenshots/files | Text stdin/stdout and GUI clipboard copy; arbitrary binary files/images; screenshot files attachable. No native screenshot capture. |
| Interface isolation | Input only PCM WAV or explicitly selected analog audio device; no USB protocol, raw serial, Ethernet, routing, shell execution of content, or network listener. Hardware isolation is not guaranteed by software. |
| Receive cache256MB | Bounded in-memory GUI/C++ cache, replacement/eviction, explicit exclusive save. Codec workspaces have separate checks; not a total RSS promise. |
| Large keyfiles/pad | Production128MiB random header; streamed whole optional >1GiB pad; authenticated AES-GCM wrapped symmetric master; independent runtime keys. No SSD erasure guarantee. |
| Repeatable packets | Random128-bit ID, ID CRC, authenticated/checksummed repeat flag, CLI+GUI64KiB limit. No built-in repeater or routed address. |
| Packet preamble, MAC, FEC, encryption | >=5s balanced repetitive training, HMAC appended before RS, byte interleaving, entire preamble/frame/FEC encrypted. Unencrypted SHA256 integrity. Fixed RS-protected bootstrap retained with body FECoff. |
| Short-text dictionary | Fixed versioned dictionary bit codec for text under256bytes; only used when it reduces size. |
| Cipher streams | AES256-CTR, HKDF per purpose/epoch, independent HMAC. Data, DSSS, and scrambler wired into modem; FHSS key purpose reserved but no RF hopping. |
| Common time search | Nearest-first whole-second epoch search±32768, sample timing refinement. No continuous absolute nanosecond counter scheduling, time-source discipline, or billion-symbol search. |
| Timing cooldown |6s after GUI TX; sequential CLI audio TX waits6s. No coordination across separate concurrent processes or users sharing a key. |
| DSSS/scrambler | Independent keyed binary chip sequences plus differential QPSK. Not a rotating high-order pattern constellation decoder. |
| Near-best throughput | Not established. Reference1200bit/s at default settings before overhead. No adaptive QAM/APSK, LDPC, trellis, equalizer, resampling-clock tracking, or capacity benchmark. |
| Bandwidth | Nominal1.2/2.4/22.05/24kHz presets, sampled waveform. Rectangular pulses have sidelobes; no certified occupied-bandwidth mask. |
| Weak-signal status | Exact few-bit overhead-free DBPSK and known aligned correlation; no authenticated identity, unknown-beacon discovery, day-long streaming, or normal/distress band monitor. |
| Multi-signal/band reception | Single acquired packet at selected audio carrier per capture. Pure band-scheduling utility tested; not connected to RF hardware. Whole-band concurrent decoding and scheduled retuning absent. |
| FHSS/SDR/IC-7100 | Not implemented. CLI rejects unsupported device types; no misleading RF controls. |
| Provisional decoding | GUI shows operation progress, then validated content. No speculative text replacement or live decoding before footer. |
| Diagnostics | Post-capture waveform, spectrum derived by GUI, differential constellation, training SNR/correlation, timing offset and rate. No live waterfall, scrambler-constellation plot, or CPU utilization control. |
| Simulation | Actual modulation/demodulation with seeded measured-power AWGN, leading sample delay, fixed frequency offset. No fabricated dBm/path-loss/extreme sensitivity presets. |
| QR Level L | Vendored standards encoder, UTF8ECI, up to500Unicode scalars; live compose preview, SVG/PBM CLI, independent decode verification. |
| Explicit exclusions | No asymmetric crypto/key exchange, built-in repeaters, routable addressing, or rapid uncontrolled Doppler tracking. |
| Public release statements | Factual capability disclaimer, standard primitives, preserved source-specification wording clearly separated. No legal classification asserted. |

Further work needs an agreed interoperable waveform/performance target, a
hardware/channel test setup, and acceptance measurements. Passing deterministic
loopback tests cannot establish near-capacity performance, thermal sensitivity,
RF compliance, adversarial security, or reliable multi-day beacon detection.
