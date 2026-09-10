# Implementation security review — 2026-09-10

This is a focused source and behavior review updated for the version 0.2 implementation,
not an independent security audit or a claim that the original specification is
fully implemented. The review covered cryptography, keyfile handling, packet
release ordering, CLI output, GUI cache/clipboard boundaries, and QR rendering.
Version 0.2's incremental DSP and differential 16-APSK waveform are a new audio
implementation; the packet and keyfile formats are unchanged. Historical test
results below do not establish validation of the new waveform. Current execution
results are tracked separately in [validation.md](validation.md).

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
| Text packets appeared in the received-file list. | The list projects only verified file/screenshot kinds; text stays available for exact ticker clipboard copy. File selection follows packet ID despite cache changes. |
| Unit-circle constellation normalization hid transmitted amplitude information. | Shared 16-APSK carries amplitude and phase; measured amplitudes are retained and the GUI uses one common I/Q display scale. This is a signal-display correction, not an authentication mechanism. |
| Long-tone waveform allocation confused content limits with DSP limits. | Streaming uses an independent bounded workspace and incrementally emitted/received data. Packet/cache limits remain content-based; batch PCM/WAV allocation remains separate. Regression tests explicitly distinguish streaming and batch eligibility. |
| A very fast simulated transmission could finish between GUI polls and leave the UI busy. | A persistent terminal-state marker records completion/cancellation; ordered event serials preserve pending-before-final observations. |

OSC52's clipboard behavior is documented in the primary
[xterm control-sequence reference](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html).
The reproduction used a generated valid unkeyed packet carrying the sequence;
the problem was its interpretation by terminal software, not a failure of the
packet digest.

## Prior verification evidence and retained boundaries

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

## Version 0.2 checks and remaining validation boundaries

The new GUI policy tests cover file-kind filtering, strict clipboard eligibility,
one active transmission, and cooldown only for actual encrypted output. The
regression suite covers long tones without duration-sized PCM, fractional-carrier
24 kHz PCM reception, automatic integration beyond the previous ceiling and
independent five-second training. Adding those tests is not a claim that they
have passed on every target; execution evidence belongs in validation.md.

The accelerated simulator assumes ideal carrier/symbol timing and adds noise to
integrated observations. It shares waveform mapping and receiver decisions with
PCM, but does not model arbitrary acquisition timing, drift, fading, multipath or
hardware nonlinearities. Real reception uses a finite timing/key/epoch bank,
bounded by configured workspace. Fixed five-second training does not gain energy
when payload symbols become longer; blind protected-bootstrap acquisition avoids
requiring a training match, with complete packet verification still mandatory.
No acquired-score, constellation appearance
or simulation pass constitutes packet authenticity or measured sensitivity.

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

The default 256 MiB content/cache limits are separate from the default 64 MiB
streaming DSP workspace. Packet coding scratch has content-derived checks;
batch PCM/WAV still requires complete vectors. These limits are not a hard cap
on process RSS or CPU consumption. The DSP implementation is a
reference modem and does not establish the sensitivity, capacity, regulatory
classification, or hardware isolation claims of the original design document.
