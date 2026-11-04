# Production ALSA stereo capture correction, 2026-11-04

The production ALSA backend now supports the stereo-required `hw:1,0` capture
configuration used by this Realtek ALC257 cable fixture. Its logical capture API
remains mono. This follow-up supersedes the earlier unsupported native-backend
limitation without changing the provenance of the original ALSA-utils campaign.

## Changes and causal evidence

Capture prefers one channel when available, then tries two at the same endpoint
and rate. Stereo input is converted by selecting **channel zero**, with the same
S16/32768 scaling, before monitoring or resampling. It does not average channels
or select a channel from signal amplitude. Buffer storage accounts for interleaved
samples; reads, callbacks and timing remain counted in frames. Stream metadata
reports hardware/logical channel counts and the selected capture channel.

The first physical trial of that correction negotiated 48 kHz / two channels
but delivered no PCM before the 15-second startup deadline. A separate
0.418-second experiment on installed **libasound 1.2.14** found:

| Quantity | Observed value |
|---|---:|
| Hardware buffer / period | 4800 / 960 frames |
| Convenience API start threshold | 4800 frames |
| Application read size | 2400 frames |
| Initial reads | Eight `EAGAIN` results, still PREPARED |
| Only change on the same open handle | Capture start threshold set to one frame |
| Next behavior | RUNNING, then 935 frames returned |

The measured behavior agrees with the convenience API's full-buffer threshold
and raw capture's request-size start condition. See the
[ALSA implementation](https://github.com/alsa-project/alsa-lib/blob/v1.2.14/src/pcm/pcm.c)
and [PCM interface documentation](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m.html).
The production fix changes **capture's start threshold only**, after any timing
period refinement. Buffer size, period, wakeup threshold and playback policy
are preserved. The automatic-start setting remains effective after recovery
prepares the capture stream again. Configuration failures stop before format or
PCM callbacks and release the device and parameter storage.

## Verification

The corrected production API captured the existing complete 3.6-kHz-rate triad
stimulus at a 1.725-kHz audio center through the physical cable:

- Stimulus duration: **11.484 seconds**; native command wall time: **13.521 seconds**.
- Capture: **two hardware channels, one logical channel, selected channel zero**,
  at 48 kHz. Playback also negotiated two hardware channels.
- **637,269 logical samples**, exactly equal to the corresponding prefix of the
  selected-channel provider monitor. The longer provider stream contains the
  remainder of the final read block.
- No reported capture/playback discontinuity; no near-full-scale samples.
- Existing frozen cable profile, with no refit: **+0.00355 dB** payload energy
  prediction error and **−47.58 dB** residual power relative to the capture.
- The independent public measurement markers supplied transport alignment;
  this is not a new operational admission or blind-acquisition claim.

All six focused suites passed in **20.32 seconds**: `alsa_audio_contract`,
`alsa_interleave_playback`, `windows_audio_contract`, `audio_provider_timing`,
`resampler`, and `triad_audio_capture_guards`.

New deterministic cases cover mono preference, stereo-only fallback, opposing
channel samples, partial reads, resampling and frame accounting, actual buffer
workspace, threshold-gated PREPARED startup, overrun/reprepare startup, and
fail-closed setter/application errors. Existing playback and Windows contract
checks remain intact. The initial new-test compile failure and its two corrected
oracle assumptions are preserved; they are not production successes. Earlier
tests passed while the stub ignored startup thresholds, which is why the
physical and threshold-gated checks were necessary.

## Reproduction and limits

Build through the supported profile, then run only the relevant targets:

```sh
./build.sh configure --cli --build-dir build/dev-cli -- -DBUILD_TESTING=ON
cmake --build build/dev-cli --target triad_audio_capture test_alsa_audio test_windows_audio test_audio_provider_timing test_resampler --parallel 2
ctest --test-dir build/dev-cli -R '^(alsa_audio_contract|alsa_interleave_playback|windows_audio_contract|audio_provider_timing|resampler|triad_audio_capture_guards)$' --output-on-failure --parallel 2
```

The [compact follow-up evidence](native-stereo-evidence.tar.gz) contains frozen
identities, original failure, same-handle diagnostic, successful native receipt,
analysis recipe, final source snapshots and test log. Raw PCM remains under
`local-evidence/triad-native-stereo-002/`, with hashes in the evidence. The frozen
stimulus/profile are reused from the original cable campaign. Do not overwrite
captures or refit the model to make a backend check pass.

This establishes the tested Linux ALSA configuration. It does not add stereo
capture negotiation to Windows, qualify every hardware/plugin configuration,
repeat the full 24-case campaign using native capture, or constitute release
certification. No mixer changes, GUI suspension, radio transmission or unrelated
ordinary-receiver algorithm campaign was performed.
