# Fast source compression

Production Fast Modem text and file transfers encode one XZ stream before the
independent Fast coding and modulation layers. Text and files use identical
source bytes, with no type or filename prefix. XZ preserves arbitrary bytes,
including NUL, trailing zeros, partial UTF-8 sequences in files, and empty files.

The encoder uses LZMA2, preset 6 with a fixed 4 MiB dictionary, and the XZ CRC32
check. This check is additional to Fast's existing protected source areas;
public transfers remain unauthenticated and encrypted transfers still require
matching key material. Both production peers must use this source format.
The fixed bootstrap digest/HMAC binds the locally selected source format by
appending `/source/xz/v1` after the existing profile context and before the salt.
This changes no dimensions and transmits no extra bits. Raw and XZ modes reject
each other even when an old literal source happens to be a valid XZ file.
There is no fallback to an uncompressed stream and no compression toggle.
Earlier production Fast captures with literal sources require their matching
older source implementation.

Coding geometry, marker placement, integrity groups, and source endpoints are
unchanged. The XZ index contains block sizes, but these never become a modem
length or an allocation request. XZ metadata never changes physical framing or allocation while
receiving. The decoder first retains and verifies opaque fixed source areas,
including explicit holes, under its local quota. It interprets source endpoints
and decompresses only after the modem observes six seconds of fully scored
physical absence. An XZ end marker, source checksum, EOF, cancellation or
successful correction never produces that event. Invalid, truncated, trailing,
or concatenated XZ streams cannot become completed files.

Each source read is at most 16 KiB. Encoding uses bounded codec workspace and
prepares the compressed source in RAM before opening audio. Its storage is
limited to the XZ worst-case bound for the configured source quota, which
includes a small container allowance for incompressible input. No original
file copy, waveform or complete bit-vector is retained. This prevents variable
compression time from stalling live playback. Decoding
has a 64 MiB codec memory limit, independent of any dictionary requested by the
XZ source, and expands into a separately bounded output buffer. Both the opaque
receive spool and decompressed output are bounded by the configured source
quota, at most 256 MiB each. They may coexist during post-end decoding, plus
bounded codec scratch and vector reallocation overhead; no temporary files
are created. A quota violation leaves
the transfer incomplete and unavailable for saving. The transmit quota limits
original source bytes; the receive spool quota separately charges complete
opaque coding areas, including padding, flags, and classic nine-bit expansion.
An incompressible file exactly at the transmit quota can therefore need a larger
receive quota, as with the existing raw coding format. No received source size
increases either local quota.

Actual transmission compresses the source once; the retained bytes determine
exact airtime and feed playback. This includes the XZ container, local coding
fill, modem framing and physical absence tail. Source byte counts and source
goodput refer to original bytes. Editing a source file after preparation does
not change the transmitted bytes or airtime. A compose view may use the XZ
worst-case bound while file content has not yet been compressed. The displayed modem bitrate remains independent of compression
ratio and per-message startup/end overhead.

The low-level `StreamEncoder`/`StreamDecoder` raw coding API and raw geometry
estimator remain available for independent Fast wire vectors and coding studies.
Production `Session` and WAV transfer APIs explicitly wrap sources in XZ and
select `SourceEncoding::xz` in both the encoder and decoder. The decoder waits
for physical completion before decompression. This preserves the frozen
coding vectors without importing or modifying regular modem compression.

`fast_compression` tests independently generated XZ bytes, fragmented readers,
random/incompressible sources, empty and trailing-zero content, source and output
quotas, bounded prepared-source storage, cancellation, malformed/trailing
containers, raw/XZ integrity-domain mismatches, and deferred decompression
in public/encrypted classic/capacity modes. `fast_files` and `fast_session` cover
production WAV and live-session integration and physical end gating.

## Streaming size bound

The prepared-source and compose estimate bound is
`N + 6 × (floor(N / 1024) + 1) + 128`, for at most 256 MiB of input. It applies
to the pinned encoder with one LZMA2 filter, CRC32, one XZ block, and only
`LZMA_RUN`/`LZMA_FINISH` actions. It is not liblzma's single-call buffer bound;
that API explicitly excludes multi-call encoders.

In the pinned `lzma2_encoder.c`, every chunk's body is no larger than its
input: a nonshrinking compressed chunk becomes an uncompressed chunk. Headers
are at most six bytes. Every nonfinal chunk consumes at least 1,024 bytes:
`lzma_encoder.c` ends chunks only near its 2 MiB input threshold or at
`65536 - (OPTS + 1) = 61439` output-plus-pending bytes. In
`rangecoder/range_encoder.h`, at most 53 range symbols encode each source
symbol (which consumes at least one byte); each normalization adds at most
one byte, with five final flush bytes. Thus even 1,024 source bytes cannot
reach the output threshold (`53 × 1024 + 5 < 61439`). Ordinary input/output
buffer boundaries do not flush a chunk. Only the last chunk may be shorter.
The remaining 128-byte allowance covers the single XZ block/stream headers,
CRC32, padding, one index record, footer and LZMA2 end marker.

This bound must be revisited if the bundled encoder, filter chain, block policy
or flushing actions change. A 32 MiB incompressible regression exceeds the
former single-call bound, remains within this streaming bound, and decodes
exactly at its original-source quota.
