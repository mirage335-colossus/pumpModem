# Acoustic Auto 3/0 dB: OFDM premature-end recovery

The reported error, **“Fast stream ended within fixed coding geometry” while
transmission was still playing**, can result from OFDM losing tracking and
mistaking the continuing signal for physical absence. The error is the codec's
description of truncated physical input; it is not a source-length mismatch.

The earlier acoustic -10 dB correction affected single-carrier reception.
Auto 3 dB and 0 dB use OFDM and had a separate version of the problem. The
receiver and automatic interleave depths are now corrected. Restart both peers
with the updated executable and select the same Auto preset on each; an
already-running receiver retains its old code and settings.

## Reproduced failure mechanism

Both reported presets use QPSK, LDPC 2/3, a 32,768-point FFT and a 4,096-sample
cyclic prefix. Each complete physical block lasts 0.768 seconds. Previously,
both also retained eight LDPC frames per coding cycle, despite their much
smaller number of data tones.

Every OFDM block fits common gain, phase and timing from tracking pilots, then
checks separate verification pilots. Previously the same acquisition-strength
limit of 16 disagreements among 256 sign bits controlled continued reception,
timing updates and channel refresh. After a disturbance, rejected blocks could
not correct the timing estimate. Its search covered only +/-6 samples. Eight
consecutive failed checks then declared physical end after 6.144 seconds,
even when useful known-pilot evidence remained in the continuing waveform.

Channel refresh was also tied to the coding cycle. Narrowing without reducing
interleave depth stretched that interval from about 10 seconds to 36.864 seconds
at Auto 3 dB and 74.496 seconds at Auto 0 dB. This left the receiver using an
old frequency response long after a room or timing change.

Sampled production-waveform tests reproduced this with unchanged additive
noise and a disturbance introduced 20 seconds into transmission:

| Original depth-eight fixture | Original premature end | Intended signal duration |
| --- | ---: | ---: |
| Auto 0 dB, added 0.5-amplitude echo delayed 5 ms | 39.253 s | 86.016 s |
| Auto 3 dB, permanent 16-sample timing delay | 26.197 s | 48.384 s |
| Auto 0 dB, +100 ppm sample-clock step | 27.733 s | 86.023 s |

Noise power is referenced to nominal transmitted power in the original
17.5 kHz band, with the same noise throughout the real trailing absence.
The added echo creates frequency-selective changes while increasing total
received power; the noise floor stays fixed. Stationary noise and a stationary
strong echo did not reproduce premature completion in the screened fixtures.

## Receiver correction

- Acquisition still requires the original independent training check with at
  most **16/256** sign disagreements. It establishes the fixed block and cycle
  coordinates.
- Maintaining those already established coordinates permits **32/256** sign
  disagreements, retaining the gain limits and residual-energy guard. This
  does not search for a new boundary or fit a verification block to itself.
  Usable soft payload evidence can reach LDPC instead of discarding a whole
  block because a few extra verification signs are noisy.
- Tracking searches **+/-24 samples**, retaining 0.15-sample grid spacing and
  interpolation. Only the independent verification check authorizes a timing
  update. At 48 kHz this covers a half-millisecond displacement in either
  direction, allowing recovery after the old window has fallen behind.
- Actual missing blocks retain neutral erasures in their original positions.
  Six seconds of fully scored absence remain necessary; EOF, source flags and
  successful correction still cannot end reception.

Under the explicitly conditional independent-fair-sign model, 32/256 gives
about `2^-120.36` per maintenance check. This is not the old acquisition bound
or a receiver-wide probability claim. Initial acquisition remains 16/256,
and the full coding-cycle SHA-256/HMAC checks are unchanged. The
[integrity analysis](fast-capacity-integrity.md) distinguishes those decisions.

## Automatic depth and throughput

The preset selector now halves OFDM interleave depth until a refresh fits the
nominal 13-block cadence, or one LDPC frame remains. It compares candidate
throughput only after applying that constraint, using the actual fixed-cycle
geometry. A one-frame minimum can still require longer refresh intervals.

| Expected-SNR selection | Old → new depth | Old → new refresh interval | Public 50 MB rate, old → new | Rate loss |
| --- | --- | --- | --- | ---: |
| 6 dB | 8 → 4 | 19.200 → 9.984 s | 17,898 → 17,203 bit/s | 3.88% |
| 3 dB | 8 → 2 | 36.864 → 9.984 s | 9,326 → 8,592 bit/s | 7.87% |
| 0 dB | 8 → 1 | 74.496 → 10.752 s | 4,616 → 3,977 bit/s | 13.84% |

These rates are airtime estimates for 50,000,000 source bytes, including
coding, framing, padding and final silence, not measured acoustic goodput.
Short messages improve substantially: the 60-byte estimates fall from
92.94 to 39.18 seconds at 3 dB and from 168.20 to 40.71 seconds at 0 dB.
The nominal 13 dB and 10 dB settings, and negative-SNR menu settings, retain
their previous geometry. QAM, LDPC rate, bandwidth and output amplitude are
unchanged at each affected menu point. Depth two is now an explicit GUI choice;
manual depth eight remains available.

Shallower interleaving has a tradeoff: it spreads a disturbance over less time.
The improvement here comes from keeping channel estimates current. In the
0 dB echo test, the corrected receiver with the old depth eight stayed locked
but one bootstrap LDPC frame still failed. At the new depth one, a complete
keyed 20,000-byte source recovered exactly. The new depth-two 3 dB case also
recovered the complete keyed source under the same controlled echo change.

## Physical-device evidence and limits

The user confirmed that a real speaker and microphone were connected to the
default devices. Tests used production S16 audio at 48 kHz, right-only output,
95% output volume and the existing 27% microphone-volume setting. No mixer
setting changed. Capture used the physical input, not a monitor source.

One unmodified live baseline at each preset recovered the 60-byte source,
so these captures do **not** identify the exact trigger of the user's failed
receptions. For a controlled comparison, a permanent 16-sample delay was added
at capture time 21 seconds to the saved 0 dB recording. The original receiver
then ended at 27.25 seconds with 26 of 508 intervals and the reported geometry
error. The corrected receiver recovered all 508 intervals and exact source
SHA-256 from that same modified recording, completing at 167.80 seconds.
That is a real recording with an artificial impairment, not an untouched
live failure.

All four fresh live transfers using the new presets recovered exact sources:

| Preset | Source | Received intervals | Failed LDPC frames | Physical completion, capture time |
| --- | ---: | ---: | ---: | ---: |
| 3 dB, depth 2 | 60 bytes | 128/128 | 0 | 38.762 s |
| 0 dB, depth 1 | 60 bytes | 64/64 | 0 | 40.277 s |
| 3 dB, depth 2 | 20,000 bytes | 192/192 | 0 | 48.724 s |
| 0 dB, depth 1 | 20,000 bytes | 160/160 | 0 | 72.532 s |

There were no audio-device errors or capture overruns. Maximum queued capture
was 0.385 seconds, below the production four-second OFDM allowance. The
[reproduction records](validation-data/fast/acoustic-ofdm-recovery-20260921/README.md)
contain each result, source hashes, timing, queue occupancy and the retained
capture hashes. The menu labels describe expected reference-band SNR; no
calibrated 0 dB or 3 dB acoustic noise condition was imposed on these live runs.

Regressions preserve the original depth-eight failures independently of the
new defaults, and cover full keyed files at the new depths, explicit erased
coordinates, corrupted training, noise-only input, partial silence, actual
noisy absence, and EOF. The correction addresses a reproduced software failure
mechanism; it does not guarantee delivery through arbitrary fades, movement or
disturbance, or establish a statistical file-success rate.

The integrated Fast, shared-GUI and selected ordinary-compatibility group passed
**33/33 in 311.33 seconds**, including the original 92-case Fast SNR matrix and
all eight new OFDM recovery cases. Both GUI backends and the CLI rebuilt.
No native adapter or ordinary modem algorithm changed; native rendering and
the unchanged long ordinary receiver-probability calibration were not repeated.
