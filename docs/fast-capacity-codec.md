# Fast capacity codec, version 2

The cable/acoustic-default capacity format replaces Fast v1's convolutional code, frequent
integrity groups, and nine-bit source cells. It uses compact bytes, one protected
source flag per fixed coding cycle, shortened GF(65536) Reed–Solomon, and standard
64,800-bit DVB-S2/S2X LDPC frames. The classic Fast and Regular transport paths
retain their existing wire formats. This document specifies coding geometry;
actual modem throughput and physical-link reliability require separate tests.

All dimensions come from matching local settings. No transmitted length, source
flag, checksum, or decoded content changes physical framing, allocation, or
completion. The only completion event remains six seconds of scored physical
absence. EOF and cancellation do not complete a file.

## Fixed cycle geometry

The capacity `interleave_depth` is the number of LDPC frames per coding cycle,
from 1 through 16; the cable default is four. Every LDPC frame has 64,800 coded
bits. Supported information widths are 32,400 bits (1/2), 43,200 (2/3), 48,600 (3/4), 50,400
(7/9), 57,600 (8/9), and 58,320 (9/10). The 7/9 matrix is DVB-S2X; the other
five are DVB-S2. The same fixed-cycle codec is usable by cable or explicitly
selected acoustic capacity modulation; the local channel is integrity-bound.

A cycle concatenates the information areas of all its LDPC frames. Its outer RS
word uses the largest even byte count that fits. An odd number of 3/4 frames
therefore has one additional fixed zero alignment byte, protected by LDPC and
checked after decoding. The other supported rates need no such byte.

Two consecutive bytes are one big-endian GF(65536) symbol. The primitive
polynomial is `x^16 + x^12 + x^3 + x + 1` (`0x1100b`), and the generator roots
start at alpha^0. Let `N` be the available RS symbols. The parity count is
`P = 2 ceil(3N / 2006)`, the smallest even count with parity/data at least 0.3%.
Rounding is necessary because each parity symbol is two bytes. The source area
is the systematic prefix of this shortened RS word.

For four LDPC frames:

| LDPC rate | LDPC information bytes | RS parity bytes | RS data bytes | Parity/data | Public source slots | Encrypted source slots |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1/2 | 16,200 | 52 | 16,148 | 0.3220% | 16,115 | 16,095 |
| 2/3 | 21,600 | 68 | 21,532 | 0.3158% | 21,499 | 21,471 |
| 3/4 | 24,300 | 76 | 24,224 | 0.3137% | 24,191 | 24,175 |
| 7/9 | 25,200 | 76 | 25,124 | 0.3025% | 25,091 | 25,071 |
| 8/9 | 28,800 | 88 | 28,712 | 0.3065% | 28,679 | 28,655 |
| 9/10 | 29,160 | 88 | 29,072 | 0.3027% | 29,039 | 29,023 |

“Source slots” exclude the one-byte flag. A final cycle uses one additional
source slot for its mandatory padding delimiter. The encoder emits one bootstrap
cycle, then `floor(source_bytes / source_slots) + 1` source cycles. An empty file
therefore has two cycles, and an exact source-slot boundary has an additional
empty final source cycle. This costs negligible overhead on a large file while
making loss of the final cycle detectable.

Four frames produce 259,200 coded bits. Fixed 2,048-bit modem intervals require
127 intervals, adding 896 fixed padding bits per cycle. These padding bits are
whitened along with the rest of the cycle. Physical markers and pilots are
configured separately by the modem and are not included in these bit counts.

## Compact source and integrity

The bootstrap RS data area contains a fresh 32-byte salt, a 32-byte SHA-256 or
keyed bootstrap tag, and fixed zero fill. It uses the same local coding geometry
as every other cycle. There is no received bootstrap length.

Public source cycles contain one plaintext source area followed by a 32-byte
SHA-256 digest. The digest binds the v2 domain, complete local wire profile,
transfer salt, cycle ordinal, and every byte of the source area. A public digest
checks corruption and continuity; it is not authentication against an attacker.

Keyed source cycles contain a random 16-byte IV, a fixed AES-256-CBC ciphertext
area, and a 32-byte HMAC-SHA256. The plaintext width is the largest multiple of
16 fitting after the IV and tag. Remaining RS data bytes are canonical zero
alignment fill, which the receiver checks. The HMAC covers profile, salt,
ordinal, IV, and ciphertext. Keys and tags use `DataPump/fast/capacity/v2/...`
domains distinct from v1. Keyed and public modes remain matching local choices;
there is no wire negotiation or automatic fallback.

The first plaintext byte is a flag:

- `0`: continuation. Every remaining byte is literal source data.
- `1`: final. Literal source data is followed by `0x80`, then zero fill to the
  fixed plaintext width.

Only the last source cycle may have flag 1. Earlier cycles must have flag 0. The
receiver checks flags and interprets final padding only after physical absence.
It strips the final nonzero padding delimiter, preserving arbitrary source bytes,
including trailing zeros and bytes equal to `0x80`. The delimiter is unambiguous
because the integrity-protected final area ends with exactly that delimiter and
canonical zero fill. No source length is transmitted. Invalid flags, absent
padding, an early final flag, or a missing final cycle fail closed.

## Interleaving and spectral whitening

Each LDPC frame uses the frozen code-bit permutation defined in `ldpc.cpp`.
Frames then interleave in columns with a balanced rotation:

```text
wire[column * depth + (frame + rotation[column]) % depth]
    = permuted_frame[frame][column]
```

The fixed local depth and constellation select the rotation schedule. Start
per-frame/per-QAM-bit-plane counters at zero. For each of the 64,800 columns,
inspect candidate rotations in order `(column + offset) % depth`, with offsets
zero through `depth - 1`. A candidate's cost is the sum of existing counters for
the frame/plane pairs it would assign. Choose the first minimum, then increment
those assigned counters. The plane for wire position `i` is
`(i % 2048) % bits_per_symbol`: QAM labels restart at every fixed interval.

This greedy schedule minimizes the increase in squared plane imbalance at each
column. Exhaustive checks of all 176 supported depth/order pairs (depth 1–16,
even label widths 2–22) give at most five bits of spread among frames on any
plane, and at most 0.1127% deviation from equal shares. Every D-bit column still
contains exactly one bit from each frame. A contiguous burst of L bits therefore
puts at most `ceil(L / D) + 1` bits into any frame, including partial edge columns.
Schedules are immutable and cached after initialization before audio starts.

This replaces the unrotated mapping used by early development captures. That
mapping fixed depth-four frames to different groups of five label positions in
20-bit QAM, exposing only two frames to the weakest axis LSBs. A permutation
within a single frame could not remove that inter-frame reliability imbalance.
Earlier undeployed v2 captures require their matching old implementation; current
captures use this balanced wire revision.

After interval padding, the complete coded cycle is XORed with a deterministic
public whitening mask. This prevents all-zero source, bootstrap fill, and final
fill from producing biased dense-QAM points. Whitening provides no secrecy and
adds no bits. The decoder reverses the corresponding LLR signs before decoding.

The mask uses SplitMix64, with state initialized to
`0x44504d2f76322f77 + cycle * 0xd1342543de82ef95` modulo 2^64. The bootstrap has
local cycle number zero. Each word first adds `0x9e3779b97f4a7c15`, applies the
standard two xor/shift/multiply steps with `0xbf58476d1ce4e5b9` and
`0x94d049bb133111eb`, then xor-shifts by 31. Bits are consumed least-significant
first. The cycle number is local bookkeeping, not a transmitted field. Frozen
independent masks are tested for cycles zero and one.

## Recovery limits and resource bounds

LDPC uses at most 50 sum-product iterations per frame by default, with early exit
when all checks pass. A nonconverged frame still provides its hard information
bytes to the outer RS decoder: sometimes only a few errors remain. RS recovery
must satisfy `2 * unknown_symbol_errors + known_symbol_erasures <= P`.

At four frames, 7/9 can correct at most 19 unknown 16-bit symbol errors, and 8/9
or 9/10 can correct at most 22. **The 0.3% RS layer cannot repair a wholly lost
LDPC frame.** It cleans up sparse residual errors; LDPC and interleaving must do
the main recovery. The transport presently sends hard LDPC output to RS without
claiming posterior-based erasure locations. Its standalone RS implementation is
also tested for mixed known erasures and unknown errors. Every corrected cycle
must pass its full digest or HMAC before its opaque source area is retained.

Diagnostics count LDPC frames, nonconverged frames, total iterations, and changes
to non-erased systematic hard decisions. These changes are not proof of correct
recovery until integrity succeeds. RS changed bytes are recorded separately.
A single unrecoverable cycle prevents whole-file completion, but does not stop
the decoder from verifying and retaining later cycles after a valid bootstrap.
The failed cycle occupies an explicit invalid source-area slot; later good
areas retain their original positions. Physical cycle numbering advances even
when correction or integrity fails, so later whitening masks and digest/HMAC
ordinals remain correct. This requires the modem to preserve fixed timed
positions; it does not recover an unknown cycle index or reacquire a lost
bootstrap. The classic Fast decoder likewise continues and can retain other
independently protected groups within a partly damaged coding cycle.

This continuation behavior changes no transmitted bits. There is no selective
retransmission protocol or parity spanning different coding cycles. Later good
areas do not repair the missing bytes, and any hole prevents a complete file
from being offered. Bootstrap corruption remains fatal because later areas
cannot be checked without a verified transfer context. Local quota exhaustion,
nonfinite soft evidence, resource failures and internal processing errors also
stop decoding; these are not treated as recoverable channel corruption.

`DecodeSnapshot.failed` records an incomplete or invalid transfer. It can remain
true while subsequent coding cycles are processed. `decoding_stopped` records a
fatal stop. `coding_cycles` counts complete fixed cycles presented for decoding,
including the bootstrap and a cycle rejected immediately by the local quota;
`failed_cycles` counts cycles rejected for FEC, alignment-fill or group-integrity
corruption. It does not count quota/internal failures or post-end source-syntax
rejections. These states do not substitute for observed physical absence.

The receiver retains at most its configured source-area quota, capped at
256 MiB, entirely in RAM. Before decoding a source cycle, it checks the complete
locally fixed area width against the remaining quota and allocates that slot.
Rejected areas remain zero-filled holes with explicit validity metadata; their
full width still consumes the quota. Thus a stream of bad cycles cannot obtain
unbounded decoding or integrity attempts by avoiding successful retention.
In capacity mode the smallest supported area is 3,984 bytes, so the 256 MiB
quota admits at most 67,378 source positions plus one bootstrap integrity check.
The next source cycle stops before decoding or checking its digest. See the
[conditional integrity bound](fast-capacity-integrity.md) for the arithmetic
and probability-model limits.

`spool_bytes` counts allocated fixed source positions, including holes and final
padding; `verified_bytes` sums only integrity-checked opaque source-area widths.
Both include source flags and fill and differ from the eventual file's
`source_bytes`. A separate validity byte per source area and bounded vector
capacity track the holes. Source areas remain opaque until physical completion.
For a clean reception, the receiver validates source syntax before compacting
them in place into the completed file, without a duplicate source buffer.
An incomplete reception retains the good opaque areas and their fixed holes;
it does not expose them as a complete or silently shortened file. This storage
belongs to the decoder's lifetime; the live session releases the decoder when
reception ends, retaining progress counts without a partial-file handle.

Each source-reader request is at most 16 KiB. Coding scratch depends on at most
16 local frames, not source size or received metadata. At maximum depth, retained
LLRs use about 4 MiB, while one LDPC decode owns under 3 MiB of additional scratch.
Acoustic OFDM decodes up to four frames concurrently, limited by the configured
depth and reported hardware concurrency. Thus its LDPC scratch remains below
12 MiB, in addition to per-worker LLR buffers and bounded frame outputs. The
caller acts as one worker and joins the others before processing outer RS.
Frame outputs, diagnostics and exceptions are aggregated in input order;
parallel scheduling changes no codeword or acceptance rule. Single-carrier
cable decoding retains the sequential path and one decoder's scratch lifetime.
Graphs, field tables, and interleave schedules are immutable shared data. Each
used depth/constellation pair retains a 64,800-byte rotation schedule; the entire
finite 176-profile cache is bounded at about 10.9 MiB. Decode work is synchronous
and bounded by local frame count and iteration limit; real-time scheduling and
capture FIFO headroom must be verified at high symbol rates and noisy thresholds.

## Verification

`test_fast_codec` retains its independent v1 wire vectors and tests the new
format with all six rates, public and keyed sources, empty sources, exact cycle
boundaries, arbitrary bytes and trailing zeros, maximum 16-frame geometry,
1 MiB streamed sources, memory quotas, and physical-end gating. It also covers
malformed authenticated/checksummed padding, corrupted digests, wrong keys,
missing/reordered/spliced cycles, and an erased complete 2,048-bit interval.
Continuation regressions cover one and consecutive integrity-damaged cycles,
later exact retained areas, and classic groups after an integrity failure
within the same cycle in both public and keyed modes. Additional cases cover
a fully erased cycle, quota exhaustion by holes, fatal bootstrap/nonfinite
failures, and the absence of a complete result despite continued successful
decoding.
Acoustic channel tests cover rates 1/2, 2/3 and 3/4 at depths one and four in both
integrity modes: independent continuation/final byte layout, mandatory final
cycles, deferred padding interpretation, incomplete-file integrity failure, and
rejection under an otherwise identical cable channel identity. These tests
exercise the codec without sound devices or a modulation receiver.
Additional OFDM context tests bind waveform selection, FFT size, cyclic prefix
and occupied-band settings into bootstrap integrity, while dormant OFDM fields
leave cable identity unchanged.

Independent vectors cover GF(65536) parity, source flag/byte/padding layout, and
whitening. Three complete rotation schedules have independent Python SHA-256
fingerprints; all supported schedules have exhaustive bit-plane balance and
burst-spread checks. RS tests verify the complete 65,535-element primitive-field period,
nineteen unknown symbol errors at the four-frame 7/9 geometry, 88 errors at the
maximum sixteen-frame 9/10 geometry, mixed errors and erasures, invalid dimensions
and erasures, and an over-budget failure.
A 100 KB all-zero source checks balanced whitened bits. These are coding tests;
they do not establish QAM acquisition quality, live cable performance, or a
50 MB whole-file success probability.

```sh
cmake --build build --target test_fast_codec --parallel 2
./build/test_fast_codec
```
