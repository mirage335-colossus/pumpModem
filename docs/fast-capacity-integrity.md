# Fast capacity synchronization and accepted byte alignment

The capacity format separates **provisional waveform synchronization** from
**integrity-confirmed coding alignment** and **completed source bytes**. The
probability model below applies to accidental acceptance of an incorrect byte
alignment or corrupted source area. It is not a bound on the DSP's raw marker
candidate event, and it is not an authentication claim for public mode.

## Three different decisions

1. The selected DSP proposes waveform timing using locally fixed training.
   Single-carrier capacity uses the full 64-symbol QPSK word and requires
   agreement with all 128 quadrant sign bits after fitting a common phase.
   Acoustic OFDM uses 16 training blocks: two repeated blocks seed the sample
   clock, 13 independently phased full-band blocks fit the channel and noise,
   and a separately seeded final block verifies the fitted channel. Its 128
   verification tones supply 256 sign bits, allowing at most 16 disagreements
   plus an energy residual check. The final block's other tones fit common
   gain, phase and timing; its verification tones remain excluded from those
   fits, and their selection uses only earlier training. Subsequent OFDM blocks separate tracking
   pilots from verification pilots; tracking fits do not use the verification
   tones. A full-band refresh block between coding cycles must pass the presence
   check using the previous channel estimate before updating that estimate.
   The [OFDM waveform specification](fast-acoustic-ofdm.md) defines these blocks.
   These provisional locks select only already configured, fixed 2,048-bit
   interval coordinates. They do not expose decoded bytes or authorize source
   completion.
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
  retained source area is 3,984 bytes: keyed 1/2 with one LDPC frame.
- At most `floor(2^28 / 3984) = 67,378` source areas can be retained. Integrity is
  checked before the quota test, so conservatively include one additional
  source check that could trigger the quota failure, plus the bootstrap.

There are therefore at most **67,380 integrity checks per decoder**, fewer than
`2^17`. Under the stated model, accidental acceptance of any incorrect protected
area is bounded by **less than `2^-239` per decoder**. Acceptance of a completed
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

For OFDM, an illustrative **conditional independent-fair-sign model** gives
`sum(comb(256, k), k=0..16) / 2^256 = 2^-172.8418516` for one 256-sign check.
The residual-energy check can only reduce acceptance, so omitting it is
conservative within that model. A separately bounded collection of at most
`2^64` such checks would give less than `2^-108.84` by a union bound **if the same
conditional per-check model remained valid for every selected candidate**.
The OFDM receiver now enforces a nonwrapping PCM coordinate limit of
`2^64 - 1 - 8*(FFT_size + prefix_samples)` per instance. Each completed search
advances by at least half an FFT; after acquisition, verification advances by
whole OFDM blocks. With the validated local geometry these together permit
fewer than `2^64` verification checks before the input limit. This is a finite
per-instance accounting bound, not a bound across unlimited receiver restarts.
This arithmetic is not evidence that real noise, encoded payload, deterministic
pilots and adaptively selected windows satisfy those assumptions. In
particular, training-only selection of strong tones avoids fitting the current
verification block, but does not prove independent verification signs for
every structured waveform or overlapping candidate. The accepted-byte claim
therefore continues to use the explicit 256-bit integrity model above, not an
unqualified raw OFDM false-lock claim. A finite counter or search budget by
itself does not establish a probability model.

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
cannot experimentally measure probabilities as small as `2^-80` or `2^-239`,
and the numerical bound does not establish successful delivery probability on
a noisy cable or resistance to denial of service through provisional locks.
