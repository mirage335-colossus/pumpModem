# Independent installed-FLDigi PSK recordings

These files contain audio produced by the installed **FLDigi 4.2.06** binary,
Debian package `4.2.06-1+deb13u1`, on 2026-09-19. DataPump's encoder was not used
to produce them. Both decode to these exact 27 ASCII bytes, with no newline:

```text
CQ de N0CALL Test 123 % Z p
```

The recordings are mono, signed 16-bit little-endian PCM at 8,000 samples/second,
with a 1,500 Hz audio carrier. They retain the reference transmitter's standard
idle preamble, carrier postamble and output buffering. No additional text,
DataPump framing, normalization, resampling or noise was added. The captured
left-channel float samples were clipped to [-1, 1], multiplied by 32760 and
rounded with Python's `round`. The original peak amplitude was approximately
0.251188636; this is FLDigi's default transmit attenuation.

| File | Samples | SHA-256 |
| --- | ---: | --- |
| `bpsk31-fldigi-4.2.06.s16le` | 69,632 | `91331b265f992b89b8662514b6a864b71b912bc27943f0ea90a1dcf7816d96b4` |
| `bpsk125-fldigi-4.2.06.s16le` | 31,232 | `53d44613c3e3ae2d3864c91c3926952474c8b755872d17da60d90e32d5ba2a62` |

The reference executable `/usr/bin/fldigi` had SHA-256
`75265d69717b23ab4067b181f80960abbccc4946326c7b6eeecf41d53a142d8d`.
The original float capture hashes were respectively
`fc18e13c19ea38b5d0a5edf390a2539099ae841585c49faacb6a05f3aa48e33c` and
`2cdab3b75b18f1d951808edc281d43155dc02e17e833fffe61bcc608537c5a66`.

`test_legacy_psk.cpp` decodes the checked-in recordings with single-sample and
uneven chunks, and with an arbitrary start delay, attenuation and additive noise.
The normal build/tests need neither FLDigi nor a display/audio service.

## Optional reproduction

`psk_reference.py` and `psk_reference_audio.c` are original test harness code;
they contain no FLDigi implementation. On Linux they run the installed program
with a temporary configuration, private Xvfb display and a fake PulseAudio audio
boundary. Input is silence; output is captured locally. No physical audio or
radio device is used. XMLRPC selects BPSK31/BPSK125, sets the carrier to 1,500 Hz,
disables AFC/TXID and queues the text followed by FLDigi's `^r` return-to-receive
command. The shim leaves all reference modulation and text encoding untouched.

From the repository root, with FLDigi 4.2.06, Xvfb and a C compiler installed:

```sh
python3 tests/fixtures/legacy/psk_reference.py /tmp/reproduced-psk
```

Use `--fldigi /path/to/fldigi`, `--xvfb /path/to/Xvfb` or
`--display-number 450` to override their locations/display. A different FLDigi
version, configuration default or audio buffering implementation can produce
different valid PCM; keep the pinned recordings rather than silently replacing
them to accommodate a decoder change.

Separate verification on the same date passed **both directions** against the
installed program for both modes: DataPump PCM decoded to the exact text in
FLDigi, and FLDigi PCM decoded to the exact text in DataPump. A newline-bearing
sample also passed with FLDigi's ordinary transmitted LF-to-CRLF conversion
preserved. This is sampled software interoperability evidence, not a physical
radio or fading-channel performance claim.

The production implementation independently follows the
[PSK31 specification](https://www.arrl.org/psk31-spec) and
[FLDigi's public wire table](https://github.com/w1hkj/fldigi/blob/master/src/psk/pskvaricode.cxx).
