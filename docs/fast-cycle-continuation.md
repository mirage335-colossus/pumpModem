# Fast decoder continuation after damaged coding cycles

The Fast source decoder previously conflated two conditions: a damaged source
area makes the whole file incomplete, and a fatal error makes further decoding
impossible. Its `failed` latch caused later physical intervals to be counted
but ignored by FEC. This was a receiver bookkeeping bug. Independently protected
groups do not require that stop, and selective retransmission is unnecessary
for decoding later groups.

The receiver now reserves every fixed source position, including rejected
positions, and advances its physical cycle and integrity ordinals through
damage. Capacity whitening therefore remains aligned with the transmitter.
Later valid areas are retained at their original positions, with explicit holes
where verification failed. Classic cycles also preserve valid groups alongside
a bad group within the same cycle. Transmitted bits, modulation, coding rates,
and throughput are unchanged.

`failed` still means that the whole file cannot be completed. A separate
`decoding_stopped` flag identifies failed bootstrap context, quota exhaustion,
nonfinite input or internal/resource errors. Those failures cannot safely be
treated as an ordinary damaged source position. There is no bootstrap retry,
alternate ordinal search or fallback between public and keyed modes.

The session, CLI and shared GUI expose continued progress and the damaged-cycle
count. The GUI says `RECEIVING / MISSING DATA` while decoding continues.
Six seconds of fully scored physical absence still controls completion; a bad
cycle, EOF, cancellation, final flag or successful checksum cannot substitute.
No complete file or Save handle is offered when areas are missing. This change
does not reconstruct missing bytes, export partial files, or add retransmission.
Retained areas belong to the decoder's lifetime. The live session releases its
decoder when reception ends and exposes progress counts, not a partial-file
handle.

Each missing area consumes its entire locally fixed source-space quota.
`spool_bytes` includes those holes; `verified_bytes` includes only successfully
checked opaque areas, including flags and padding. Neither is a payload length.
The validity map costs one byte per source area and is bounded by the same
number of local positions. The [integrity analysis](fast-capacity-integrity.md)
updates the finite verification-attempt bound accordingly.

## Replay of real speaker/microphone recordings

These are software replays of the exact default-device recordings from the
[routing diagnosis](fast-acoustic-routing-diagnosis.md), not fresh live trials.
All use LDPC 3/4, depth 8, 48 kHz, FFT 32768, prefix 4096 and pilot stride 16.
The receiver DSP and the recorded noise/disturbances were unchanged.

| Recording | Original LDPC frames attempted | Revised frames attempted | Revised damaged source cycles | Later verified source cycles | Complete file |
| --- | ---: | ---: | ---: | ---: | --- |
| 64-QAM, right only, amplitude 0.20, 100 KB | 16 | 32 | 1 | 2 after the failed cycle | No |
| 64-QAM, right only, amplitude 0.40, 100 KB | 16 | 32 | 3 | 0 | No |
| 64-QAM, amplitude 0.40 repeat, failed bootstrap | 8 | 8 | No source context | 0 | No |
| 16-QAM, right only, amplitude 0.40, 500 KB | 96 | 96 | 0 | All 11 source cycles | Yes, exact |

The reduced-amplitude recording demonstrates the practical difference. Its
bootstrap passes, the next cycle fails all eight LDPC frames, and both remaining
cycles pass LDPC and their independent SHA-256 checks. The new receiver retains
96,840 bytes of opaque verified source areas at those later positions. This
count includes substantial final-cycle padding; it is **not** 96,840 recovered
file bytes. All 1,016 physical intervals and physical end remain observed.

The more impaired 64-QAM recording still cannot decode its three source cycles,
but now actually attempts all of them. The failed-bootstrap recording correctly
remains stopped, since the salt/integrity context was never established. The
500,000-byte control still completes exactly with 96/96 converged LDPC frames
and received SHA-256
`d706ba74cd851fd10bf5ea1e91da48760f13cf86314bff66595e1b75e271c849`.

These replays isolate the failure latch from channel quality. They do not show
that stronger FEC can recover an arbitrarily destructive disturbance, or
establish a whole-file success probability for 50 MB.

## Regression coverage and reproduction

Codec tests cover public and keyed capacity/classic streams, one and two
consecutive corrupt cycles, fully erased timed cycles, classic intact groups
within a damaged cycle, exact retained bytes at their original ordinals,
quota-charged holes, fatal bootstrap/nonfinite evidence, and physical-end gating.
Independent existing wire vectors remain unchanged.

The sampled-audio session test supplies real modem PCM with a deliberately bad
middle cycle but intact markers and pilots. It verifies later checksum progress
while reception remains active, then supplies actual silence and checks that
the final incomplete reception cannot be saved. It uses a hardware fixture,
not the live default devices. Shared GUI tests distinguish missing data from
fatal decoding stops; CLI checks cover public/keyed counters and fatal wrong-key
bootstrap reporting.

Run the focused new codec cases with `build/test_fast_codec --continuation`.
Reproduce the decisive real-capture replay with:

```sh
build/fast_cable_probe --profile acoustic --capacity --mode codec \
  --replay /tmp/acoustic-routing-95-q64-mono-a20.f32 \
  --bytes 100000 --seed 701 --qam 64 --code-rate 3/4 --depth 8 \
  --amplitude .20 --ofdm-fft 32768 --ofdm-prefix 4096 --ofdm-pilots 16
```

Do not use the diagnostic `--abort-on-failure` option for continuation tests.
The replay reports an incomplete file intentionally; `--require-success` would
make that negative whole-file result exit nonzero. No new audio is emitted.
The [evidence archive](validation-data/fast/cycle-continuation-20260921/README.md)
contains replay JSON, logs, source/input hashes and the final validation record.
