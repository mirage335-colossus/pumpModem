# Fast capacity synchronization and accepted byte alignment

The capacity format separates **provisional waveform synchronization** from
**integrity-confirmed coding alignment** and **completed source bytes**. The
probability model below applies to accidental acceptance of an incorrect byte
alignment or corrupted source area. It is not a bound on the DSP's raw marker
candidate event, and it is not an authentication claim for public mode.

## Three different decisions

1. The DSP proposes timing from the full 64-symbol QPSK word. Capacity mode
   additionally requires exact agreement with all 128 quadrant sign bits after
   fitting a common phase. The detector also refines timing and sometimes clock
   period. This provisional lock selects only the already configured, fixed
   2,048-bit interval geometry.
2. Before the codec retains any source area, its first complete fixed coding
   cycle must decode and pass the bootstrap check: a 32-byte salt, a full
   32-byte SHA-256 or keyed tag binding the local profile and salt, and canonical
   zero fill. Every later fixed cycle must pass its own full 32-byte digest or
   HMAC binding the profile, transfer salt, local cycle ordinal and complete
   protected area. A failed bootstrap, LDPC/outer-code processing exception,
   integrity failure or quota failure is terminal for that decoder instance.
   There is no scan through alternate byte offsets or alternate decoded source
   interpretations after a failure.
3. Even integrity-confirmed source areas remain opaque and unavailable as a
   completed file until the DSP observes six seconds of fully scored absence.
   Only then does the codec interpret continuation/final flags and padding.
   Every earlier source cycle must have continuation flag zero; the last must
   have final flag one and a canonical `0x80` delimiter followed by zero fill.
   Missing, extra, reordered or truncated cycles cannot turn a protected prefix
   into a completed file. EOF, cancellation and a valid final flag do not
   generate the physical-absence event.

The implementation's `ModemProgress::acquired` reports the first decision, not
integrity confirmation. Completed bytes require all three decisions. A checksum
or HMAC does not improve the claimed probability of the raw waveform detector;
it supplies an additional acceptance condition before a putative alignment can
produce accepted bytes. See the [capacity coding format](fast-capacity-codec.md)
for exact source, cryptographic and FEC geometry.

## Modeled accidental acceptance bound

Use the following explicit model: at an incorrect alignment, a proposed digest
or tag is not deliberately constructed to match the different decoded protected
area, and a full 256-bit verification acts as an ideal verification value. Each
incorrect check then has conditional accidental acceptance probability at most
`2^-256`. This is a random-corruption / random-hash model, not a mathematical
proof about every possible binary file or every implementation of SHA-256.

For `Q` checks, the union bound is `Q * 2^-256`. Independence between whole
trials is unnecessary if that per-check conditional bound holds; merely counting
checks does not establish the bound when its input assumptions fail.

The actual implementation gives a finite per-decoder budget without adding a
new search counter:

- There is exactly one bootstrap attempt. A failed decoder stays failed;
  subsequent intervals do not initiate another bootstrap or another alignment.
- The retained source-area quota is capped at 256 MiB (`2^28` bytes).
- Across all supported rates, depths 1–16 and public/keyed modes, the smallest
  retained source area is 6,000 bytes: keyed 3/4 with one LDPC frame.
- At most `floor(2^28 / 6000) = 44,739` source areas can be retained. Integrity is
  checked before the quota test, so conservatively include one additional
  source check that could trigger the quota failure, plus the bootstrap.

There are therefore at most **44,741 integrity checks per decoder**, fewer than
`2^16`. Under the stated model, accidental acceptance of any incorrect protected
area is bounded by **less than `2^-240` per decoder**. Acceptance of a completed
wrong file requires such an acceptance and also the final-format checks, so its
bound is no larger. This is comfortably below the requested `2^-80` target for
an **accepted binary alignment**, without claiming that provisional marker
correlation alone meets that target.

For a separately bounded collection of at most `2^32` checks, the same model
would give `2^-224`. The application does not impose that number as a lifetime
limit across arbitrarily many newly created receivers; there is no claim of a
constant probability bound over unlimited listening/restarts.

The budget follows `StreamDecoder::Impl::cycle`, its terminal `fail` state,
the local `quota`, and `StreamDecoder::finish` in
[`src/fast/codec.cpp`](../src/fast/codec.cpp). DSP EOF only records EOF;
[`src/fast/session.cpp`](../src/fast/session.cpp) passes a true physical-end flag
to the codec only when the DSP has observed physical completion and reception
has not been cancelled. No received length or source flag selects allocation
or coding boundaries.

## What is not established by this bound

The 128 transmitted marker sign bits do not automatically provide 128 bits of
receiver-wide false-lock evidence. A common phase is fitted; timing and clock
coordinates are selected from the waveform; and payload windows can contain
known pilots and correlated coded bits. The implementation has no separately
certified aggregate probability bound for those raw provisional locks. Neither
the nominal marker length nor an independent-fair-sign calculation should be
quoted as a full-receiver result.

Public SHA-256 is **not authentication**. Someone who intentionally constructs
a valid public bootstrap and recomputes the public cycle checksums can transmit
an accepted public file. That is outside the accidental-corruption model. Keyed
mode adds HMAC authentication, subject to its cryptographic assumptions; neither
mode implements an anti-replay database, so replaying an intact valid transfer
is not excluded by this analysis. A valid encoded transfer deliberately embedded
in another waveform is likewise not an independent random candidate.

The existing regression tests exercise corrupted digests, wrong keys, spliced
or reordered cycles, malformed protected padding, missing final cycles, memory
quotas and physical-end gating. Such tests check the acceptance logic. They
cannot experimentally measure probabilities as small as `2^-80` or `2^-240`,
and the numerical bound does not establish successful delivery probability on
a noisy cable or resistance to denial of service through provisional locks.
