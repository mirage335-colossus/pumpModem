# Fragment integrity under missed physical presence

The [separated-screen holdout](hf-phase-screens.md#complete-receiver-outcomes)
transmitted `B` but accepted a space after two interior active slots were
classified as quiet. Every admitted DATA bit was correct. More accurate
presence inference may prevent that particular split, but integrity cannot
depend solely on detecting every transmitted bit through a fade.

The native FEC-off short codec represents `B` as the 13 bits
`11111 01000010`. Its interior bits 7–9 are `000`, the complete short code
for a space. Once the observations have been reduced to that fragment and
two scored quiet slots, the codec has no checksum or framing evidence that
distinguishes the fragment from an intended space. This is a structural
ambiguity in the unprotected format, not an assertion that the original
radio samples contain no further usable information. Delaying completion
by an arbitrary fixed number of slots would change the physical-completion
contract and would not eliminate arbitrarily long interior fades.

## Exact native-input ambiguity under an outage

The [native completion probe](src/hf_completion_ambiguity_probe.cpp) now tests
the stronger claim at the actual receiver input. Canonical FEC-off messages
`" "` and `" B"` share the space code `000`, public epoch/ordinal sequence,
waveform key and seeded noise. Both pass through the native TX/RX chain. A
constructed zero-voltage outage suppresses the longer transmission after its
third bit through the two scored quiet apertures. This is an identifiability
witness, not a measured fade probability or a typical ionosphere model.

The two **540,003-sample complex prefixes are bit-for-bit identical**, with
SHA-256 `bc8058066ebf53a301bea032673a65d20a5af246033ba166ec091246188d08b8`.
Both complete native receivers score `DATA 0, DATA 0, DATA 0, QUIET, QUIET`,
accept a space, and remain open: `finish_input` is never called. All five
slots are complete and valid. The longer source subsequently resumes; its
later received differential signal energy is 19,769.808 sample-sum units
against the identically seeded short-message/noise control. Both receivers
use 24 software workers on 12 logical hardware threads. The probe passes
all 17 assertions in 2.965 seconds. [Exact receipt](local-evidence/hf-3d-preflight/completion-ambiguity-recovery.stdout.json)

This separates a structural limitation from a search weakness. Any receiver
given that same prefix, calendar and key must make the same inference about
both histories. Additional compute cannot distinguish them at that point.
Requiring zero premature acceptance under arbitrary complete outages while
also accepting this legitimate short message at the same finite deadline is
therefore impossible for this unprotected framing. That is narrower than a
claim that every realistic fade is information-destroying: partial fades can
retain information that improved presence inference should exploit.

The limiting interpretation extends conditionally to a deep but nonzero fade.
For fixed, fully known received means `mu0` and `mu1` over the **whole prefix**,
let `E = ||mu1 - mu0||^2` and let proper complex white Gaussian noise have
`nu = E[|n[k]|^2]` per complex sample (each real quadrature has variance
`nu/2`). With equal priors, the optimum binary average error is
`Q(sqrt(E/(2*nu)))`, equivalently `0.5*erfc(sqrt(E/(4*nu)))`.
The log-likelihood ratio has means `+/- E/nu` and variance `2*E/nu`.
Thus even the known-mean benchmark tends continuously to chance as the
received difference energy vanishes; at zero energy the distributions coincide.
This is the same Gaussian likelihood calculation already used by the
[known-channel diagnostics](hf-phase-screens.md), not a new numerical receiver.
Include every sample on which the hypotheses differ; a selected fade window
cannot stand in for the whole prefix. The benchmark assumes known channel,
timing, noise law and context, and supplies neither a natural fade probability
nor attainable blind performance. Colored noise and unknown channel states
require their own likelihood; additional integrity coding changes the allowed
message hypotheses rather than making this unprotected pair distinguishable.

The existing protected-codec fragment checks below remain relevant, but they
do not establish a universal false-acceptance guarantee or equal-energy
sensitivity. Integrity checks must remain independent of physical completion;
received lengths must not select DSP geometry or replace scored quiet slots.
No production framing or defaults were changed by this experiment.

The first probe attempt lacked the leading samples required by native coarse
preparation and failed before reception. A common emitted pre-roll and public
filter halo corrected the setup; full-worker workspace quotes now fail early
with the actual geometry error. This harness failure and its correction are
retained separately from the successful ambiguity witness. The executable is
included in the regular `high_fidelity_hf_completion_ambiguity` CTest group.

## Native component regression

[`test_hf_fragment_integrity.cpp`](tests/test_hf_fragment_integrity.cpp) uses
the canonical encoder, decoder and `QuietSlotMessageReceiver`. It supplies
synthetic already-scored DATA, ambiguous and quiet events; it does not
simulate DSP or qualify waveform acquisition, global waveform ordinals or
a real fading channel. Completion still requires the normal two scored
quiet slots. No received length changes DSP geometry or selects completion.

The new focused CTest passed in 0.03 seconds on the local CPU. It checks:

- Complete `B`, `e` and `AB` controls in Off, RS20 and RS60 modes.
- The FEC-off `B` fragment accepting the wrong space, retained as an expected
  failure witness rather than silently redefining it as success.
- Rejection of that same three-bit fragment under both protected modes.
- Every nonempty proper contiguous substring of the protected `B` codeword:
  2,627 for RS20 and 4,655 for RS60, including prefixes and suffixes. All
  7,282 are physically completed but rejected by the codec.
- Recovery of `B` with one complete packed byte marked erased inside each
  protected frame. Ambiguous events retain an erasure; they are not two
  quiet events and do not establish premature completion.

The enumeration is a finite deterministic property of these two codewords,
not a general false-acceptance probability. The public CRC32 in protected
short frames supplies error detection, not sender authentication, and no
claim about arbitrary malicious fragments follows. These tests use the
existing native implementation without changing production defaults.

## Exposure and energy cannot be hidden in the comparison

The canonical encoded sizes of `B` are 13, 72 and 96 bits for Off, RS20 and
RS60 respectively. Native plan receipts with requested bit duration 8 s
produce an actual 11.6 s support and the following total captures, including
physical quiet scoring:

| Mode | DATA bits | Scored slots | Samples at 1,920 Hz | Represented seconds |
| --- | ---: | ---: | ---: | ---: |
| Off | 13 | 15 | 1,636,175 | 852.174479 |
| RS20 | 72 | 74 | 8,432,975 | 4,392.174479 |
| RS60 | 96 | 98 | 11,197,775 | 5,832.174479 |

These are AWGN planning calls, not communications observations. The native
public calendar has at least a 60-second recurrence even with Helper off;
shorter bit support does not compress that calendar. Consequently, merely
shortening bits cannot fit these protected messages inside the existing
1,600-second separated-screen field qualification. Repeating or wrapping its
spatial cut would invent new propagation, and is not an acceptable extension.
A protected waveform comparison needs a separately admitted physical horizon
or a different explicitly qualified exposure. It must report the longer
transmission, energy and changed fade exposure rather than crediting a free
coding gain. Codec rejection is an integrity improvement, not payload recovery.

## Frozen protected waveform comparison

The [three-row cohort](campaigns/hf-lens-protected-integrity.json) uses the
existing analytic lens holdout, whose independently bounded far-field
continuation supports a longer source without repeating a finite patch.
It preserves requested 64-second bits, `B`, channel/noise seeds 69002/79002
and the original wanted 0 dB-Hz. The actual support remains 69.6 seconds;
the protected waveform has 72 DATA bits instead of 13. At the same per-bit
power this is a substantial energy increase, not an equal-energy comparison.
The original early physical fade and public calendar remain in place, while
the coded bit values and later exposure differ. It is not a replay of the
separated-screen fragment failure.

Native AWGN planning at that duration gives 12,749,135 samples, 74 scored
slots and 6,640.174479 seconds. The actual lens plan independently admits
12,749,245 samples and 6,640.231771 seconds, including its additional causal
support and two declared quiet slots. The fixed sample cap is 14 million. Wanted receive has 600
seconds; same-calendar noise and bright wrong-key controls have 900 seconds
each, with 2,400 seconds aggregate. The new inference candidate remains off.
Every completed receipt must validate the physical lens table, full public
calendar, incident/selected energy and complete decoder endpoint. A timeout
remains incomplete evidence. Negative controls require both no false DATA
and no accepted message; codec rejection does not conceal a false-DATA defect.

The much shorter 8-second request was rejected as the primary waveform
comparison: besides its energy change, the 60-second calendar places much
of the holdout's critical encounter near 219 seconds in silence. Retaining
the original duration is the more discriminating test. A separately
qualified slower-advection MPS exposure remains possible if a subsequent
question specifically requires protected reception through that field;
enlarging its spatial grid is unnecessary for this first integrity check.

## Complete protected-waveform result

All three frozen rows completed and passed strict receipt validation in
525.20 seconds total. Each processed all 74 public slots and the complete
12,749,245-sample capture.

| Row | Native wall time | False DATA | Accepted message |
| --- | ---: | ---: | --- |
| Wanted `B`, RS20, 0 dB-Hz | 101.94 s | 0 | Correct complete `B` |
| Noise, same long calendar | 210.20 s | 0 | None |
| Bright wrong key, +10 dB-Hz | 212.62 s | 0 | None |

Wanted reception still misses active slot 2. The native decoder repairs its
one erased byte containing one missing bit, then validates and delivers the
exact byte `[66]`. It records 71 observed DATA bits, no wrong DATA bits, no
premature completion and trailing scored quiet slots 72–73. Thus
`correct_decoded_complete=true` while the stricter raw-clean
`correct_complete=false` remains visible. This is an actual complete native
signal-chain result, not just rejection of a synthetic fragment.

The physical lens table matches the archived unprotected holdout exactly.
Measured incident DATA energy rises from 1,747,066.93 to 9,676,053.51 sample
units, a factor of 5.53846; selected received DATA energy rises by 5.45064.
Represented duration increases from 1,330.231771 to 6,640.231771 seconds,
a factor of 4.99179. The same C/N0 and early physical exposure do not make
this an equal-energy coding gain. It demonstrates that the existing protected
mode can deliver this longer message through the retained lens failure,
with quiet controls on that longer calendar. It does not repair or qualify
the distinct separated-screen `B` event.

The [campaign report](local-evidence/hf-lens-protected-integrity/report.md),
frozen input and raw native receipts bind to appraisal source
`b242408d6da2e3f08ac80e21d4ccc474b4ca41a56364b3d7d2318cbce3bad4ab`
and unchanged canonical native source
`dd2c9e267ae030db4393f815275ec615e8d6425732545bbdaf5d0705243e8e26`.
Retain these current wanted/control receipts for comparison with the
unprotected inference candidate and its energy cost; summarize and remove
superseded replicas once that comparison and the integrity-policy question
are resolved.

The focused build/test receipts and condensed exact planning provenance live in
[`local-evidence/hf-integrity-preflight/`](local-evidence/hf-integrity-preflight/).
The redundant raw preflight slot arrays were removed after the protected
waveform comparison completed; `planning-summary.json` retains their commands,
source identities, original receipt hashes and geometry. Retain that summary
while investigating the accepted-fragment failure. The permanent regression
remains useful while the unprotected short format and these protected formats
remain supported.
