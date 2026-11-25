# Keys, schedules, spectrum and the stream contract

Research date: 24 November 2026. This chapter records how the user's
cryptographic, scheduling and stream requirements fit existing repository
infrastructure and the physical design. It is a design note, not a protocol
specification or a legal opinion. Regulatory detail is in
[Sol-6.2's keys and spectrum chapter](../Sol-6.2/stream-keys-and-spectrum.md),
which checked the current eCFR text; it is summarized, not repeated.

## Findings

1. **The shared keyfile and both cipher lanes already exist.** Keyfile version
   4 (`DPMKEY04`) stores independent 64-byte Serpent and AES roots per named
   entry, generated from operating-system randomness in two rounds with
   optional audio mixed into the Serpent lane only. The dual-lane
   confidentiality stream XORs AES-256-CTR and Serpent-256-CTR keystreams with
   per-lane HKDF-SHA512 epoch keys and random-access offsets
   ([crypto](../../../docs/crypto.md)). Matrix can reuse both without new key
   formats, satisfying "keyfile format shared with the other modems" and
   "AES-256 + Serpent cascade with 512 bits of entropy" (two independent 512-bit
   roots). **See the [25 November correction](#dated-correction-to-the-entropy-claim-25-november-2026):
   the root lengths do not establish the entropy claim.**
2. **A stream cipher adds no bits.** XORing a keystream addressed by
   (key, epoch, ordinal, offset) onto fixed-geometry coded or source bits
   transmits no IV or nonce; the address is derived from the configured
   calendar and system clock. This matches "encryption must not add additional
   bits". The price is the one the user accepts: two transmissions with the
   same key and address leak the XOR of their plaintexts.
3. **Schedule streams should be domain-separated from Robust's.** Matrix
   schedules (keyed tone permutation, hop offsets, keyed tile waveforms, decoy
   selection) should use AES purpose streams under new Matrix labels, with a
   context that includes the configured nominal shift frequency rounded to the
   operator-configured multiple of 10 Hz and excludes hop frequencies, measured
   frequency, Doppler and receiver search candidates — the same principle the
   canonical Robust waveform already applies to its rounded carrier context.
4. **Encryption is not integrity.** Data Pump's first priority is integrity.
   CRC or FEC success is error detection, not authentication. If tamper
   resistance is required, the existing `confidentiality_mac` (XOR of
   independently keyed HMAC-SHA256 tags) on fixed-geometry intervals is the
   available construction, and its tag bits are framing overhead the design must
   budget. Whether a Matrix profile carries a tag is a decision for the user;
   it does not conflict with the "no added bits for encryption" rule.
5. **Amateur operation constrains secrecy and possibly hopping.** U.S. amateur
   stations generally may not transmit messages encoded to obscure meaning
   (47 CFR 97.113(a)(4)); a public all-zero key is not secrecy and keeps a
   profile publicly decodable. Keyed pseudo-random hopping may raise the
   question of whether the emission is spread spectrum, which is authorized
   only in specific bands (§97.305). The 2.8 kHz figure is an authorized
   bandwidth for listed data bands (§97.307(f)), not a 26 dB mask; the user's
   26 dB target can be kept as an engineering measurement. These must be
   settled per operating profile before transmitting.

## Dated correction to the entropy claim (25 November 2026)

The first finding above correctly identifies the existing `DPMKEY04` format
and its independent 64-byte Serpent and AES roots as compatible root material
for reuse. Its wording that this satisfies “512 bits of entropy” (two
independent 512-bit roots) is unsupported: the 64-byte lengths describe stored
root sizes, not measured min-entropy or a demonstrated security strength. The
repository's [keyfile documentation](../../../docs/crypto.md#keyfile-version-4)
explicitly says that audio is supplementary input, not an entropy estimate,
and that delay between OS draws does not prove independent entropy. The later
Sol-6.2 [keys and spectrum research](../Sol-6.2/stream-keys-and-spectrum.md#entropy-claims-and-key-sizes)
also distinguishes root length from entropy and security strength; see
[NISTIR 8427](https://csrc.nist.gov/pubs/ir/8427/final) for the distinction
between bitstring length and full entropy. This corrects the earlier research claim, not the
cryptographic implementation: no code or source-entropy assessment is changed
here. The documented cipher lanes, root sizes, derivation details and reuse
risks above remain as recorded.

## Proposed key and schedule derivation (sketch)

Using existing primitives (HKDF purpose keys from the AES root; AES-CTR
addressed by purpose, whole-second epoch and byte offset):

| Purpose | Key source | Context (illustrative labels) | Use |
| --- | --- | --- | --- |
| Tone/tile permutation | AES root → Matrix purpose key | `datapump/matrix/v1/permutation`, rounded shift Hz, profile id | Map coded symbols to keyed tone or tile indices per symbol |
| Hop offset | AES root → Matrix purpose key | `datapump/matrix/v1/hop`, rounded shift Hz | Keyed placement of the active tile/tone set within the band; hop value never feeds back into key context |
| Keyed tile waveforms | AES root → Matrix purpose key | `datapump/matrix/v1/tile`, rounded shift Hz | Pseudo-random chips for family B |
| Decoys / null templates | AES root → Matrix purpose key | `datapump/matrix/v1/decoy` | Keyed non-transmitted templates for live null calibration |
| Confidentiality | Serpent and AES roots (dual lane) | `confidentiality_child("matrix/v1/…")` with epoch and ordinal | XOR onto payload bits; no transmitted nonce |

The labels above are placeholders to show domain separation; a real
specification needs independent vectors like the existing ones. Epochs are
whole seconds from the system clock; ordinals are non-transmitted slot
counters from the configured public calendar. Clock error enters only as a
public-hypothesis search over neighbouring epochs, as in the current Robust
contract ([clock sync](../../../docs/clock-sync.md)).

### Wrong-key admission filtering

Because every template is keyed by the rounded shift frequency, a signal from
another operator using a different shift (or key) correlates like noise. The
receiver can also correlate against wrong-key templates as explicit nulls
(see the "hypothesis vs null" construct in the
[design chapter](05-waveform-and-parameter-design.md)) and reject candidates
whose wrong-key scores are comparable to their right-key scores. Separating
operators by rounded shift does not identify senders and does not guarantee
rejection of every nearby signal; it reduces accidental admission.

## Spectrum and emissions

- **Passband.** Keep all tones/tiles plus transition skirts inside the
  configured total bandwidth (default 450–2650 Hz audio, i.e. 2.2 kHz) so
  typical SSB filters pass them; the edge tones need guard margins for
  transition sidelobes and Doppler.
- **Constant envelope** keeps amplifier behavior predictable; symbol transitions
  need smooth phase-continuous or tapered shaping, whose sidelobes must be
  measured against the occupied-bandwidth definition (0.5% power outside each
  edge, 47 CFR 2.202(a)) and the user's 26 dB engineering target.
- **Long symbols** shrink individual tone bandwidths, so most emission concerns
  come from transitions and hops, not from the tones themselves.
- **Helper tones** are additional emissions with their own placement and
  identification considerations.

## Stream contract

The repository's existing receive contracts apply unchanged: physical
completion only by scored quiet slots; no geometry, allocation or completion
derived from received lengths or headers; interior missing slots as erasures;
bounded storage and opaque output; and the
[security](../../../docs/security.md) and
[receive-processing hardening](../../../docs/receive-processing-hardening.md)
rules for displayed text and saved files. One-way operation without
retransmission is assumed throughout this research, so all margin must be
chosen before transmission begins.

## Sources

- Repository: [crypto and keyfiles](../../../docs/crypto.md), [clock sync](../../../docs/clock-sync.md), [security](../../../docs/security.md), [receive processing hardening](../../../docs/receive-processing-hardening.md), [physical completion](../../../docs/robust-manual-capacity.md#physical-completion-and-security).
- 47 CFR §§2.202, 97.113, 97.305, 97.307, 97.309 (eCFR, as checked in [Sol-6.2](../Sol-6.2/stream-keys-and-spectrum.md)).
