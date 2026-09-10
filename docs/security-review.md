# Implementation security review — 2026-09-10

This is a focused source and behavior review of the version 0.1 implementation,
not an independent security audit or a claim that the original specification is
fully implemented. The review covered cryptography, keyfile handling, packet
release ordering, CLI output, GUI cache/clipboard boundaries, and QR rendering.

## Findings addressed during implementation

| Finding | Resolution |
| --- | --- |
| Arbitrary received control bytes could reach a terminal, including an OSC52 clipboard sequence. | Received text is rendered through a terminal-safe formatter when stdout is a terminal. Pipes retain exact bytes. Binary packet output requires redirection or an explicit output file. |
| Malformed UTF-8 metadata could produce invalid JSON despite a valid packet digest/MAC. | Packet metadata now requires valid UTF-8 before it can be emitted as filename, callsign, or grid JSON fields. Payload bytes remain lossless. |
| GUI clipboard copy silently replaced malformed UTF-8 text bytes. | Clipboard copy uses strict decoding and refuses binary/non-UTF-8 content; explicit file save preserves the original bytes. |
| The GUI retained large diagnostic float arrays for every cached tiny packet while accounting only payload bytes. | Diagnostic arrays are removed from cached packets; the latest received result supplies the plots. |
| Large Unicode QR messages could lose their quiet zone on the original 150-pixel canvas. | The canvas accommodates 185 modules, including the four-module quiet zone for every supported QR version. |
| The public packet header comment disagreed with the actual Reed-Solomon first root. | The comment now agrees with the implementation and protocol document: generator roots begin at alpha^0. |
| Provisional live text could be mistaken for verified content. | Pending ticker rows cannot trigger normal clipboard copy or file saves; only complete FEC and digest/MAC verification populates the received cache. |
| UTF-8 C1 controls could appear in named key labels. | Keyring names reject ASCII and C1 controls, malformed UTF-8, duplicates and out-of-bounds lengths. |

OSC52's clipboard behavior is documented in the primary
[xterm control-sequence reference](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html).
The reproduction used a generated valid unkeyed packet carrying the sequence;
the problem was its interpretation by terminal software, not a failure of the
packet digest.

## Verified security properties

* CLI report and save operations occur only after full packet decoding,
  metadata checks, and digest/HMAC verification. Acquisition correlation or
  successful Reed-Solomon correction alone does not release a file.
* A keyed receiver rejects unkeyed packets. Its HMAC input binds the versioned
  context and candidate timestamp as well as the canonical packet metadata and
  content. HMAC uses a key independently derived from the data stream keys.
* An encrypted integration check confirmed successful keyed packet decoding,
  rejection at adjacent wrong epochs, and tampered-ciphertext rejection without
  creating the requested output file or emitting plaintext. A noisy loopback
  with both cryptographic scrambling and DSSS recovered the exact message.
* Tests cover deterministic stream positions across AES block boundaries,
  purpose/time separation, frozen independently computed AES/HMAC vectors,
  altered message/tag rejection, keyfile authentication, missing/wrong pads,
  pad corruption beyond multiple streaming chunks, and a real 128MiB default
  keyfile round trip. Keyfile creation rejects existing paths and symlinks;
  POSIX tests confirm no group/other permission bits.
* QR output was independently decoded with ZXing-C++ 3.1.1 for ASCII, mixed
  scripts, embedded zero, markup-like input, 500 ASCII characters, and 500
  four-byte Unicode characters. All decoded bytes matched the supplied UTF-8.
  Decoder installation was temporary and is not a runtime dependency.
* Crypto and QR tests passed with address and undefined-behavior sanitizers.
  LeakSanitizer was disabled because it cannot run under this environment's
  ptrace arrangement.
* No network listener, automatic received-file execution, automatic attachment
  opening, or implicit received-filename write path was found. The native GUI
  calls the C++ service directly; saves require an explicit path and
  exclusive creation.

## Remaining validation boundaries

CTR keystream reuse still reveals relationships between reused plaintext
positions; the independent HMAC does not restore confidentiality. There is no
durable replay state. Possession of the complete keyfile and required pad grants
the shared identity. Large keyfiles do not guarantee physical erasure, and RAM
can still appear in swap, hibernation, or crash dumps. These are documented
properties, not issues resolved by this review.

Windows code paths, owner-only keyfile ACL behavior, non-ASCII command-line
paths, and physical WinMM audio have not been validated on a Windows host.
In particular, the current narrow command-line and file-path conversion needs
Windows testing before claiming arbitrary Unicode path support. Linux hardware
audio also requires device testing. File-save permissions outside the keyfile
API follow the platform's normal creation policy and user configuration.

The configured memory budget governs payload/cache and conservative operation
workspaces; it is not a hard cap on process RSS. The DSP implementation is a
reference modem and does not establish the sensitivity, capacity, regulatory
classification, or hardware isolation claims of the original design document.
