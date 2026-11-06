# Shared full-rate audio noise qualification

The [background-noise guide](../../../triad-background-noise.md) now places the
standard feature in `audio::PlaybackOutput`, after resampling and Helper
composition and before backend delivery. Native S16, host/browser Float32 and
the offline PCM renderer share this implementation. This record qualifies that
placement and its power normalization; it is not a receiver sensitivity or
false-admission campaign.

Compact evidence: [measurements](measurements.json), [check outcomes](checks.json),
[source hashes](source-sha256.txt), [binary hashes](artifact-sha256.txt), and
[source/check/receipt archive](evidence.tar.gz) (666,781 bytes; SHA-256
`eb036ad130d54218c31ce6664a0576805bcb7d36607ebe960de0362e83872f4c`).
The archive preserves failed checks as well as passing follow-ups. Large PCM
captures remain under the ignored local evidence directory; new transmissions
reproduce the statistical experiment with fresh keys, not identical noise.

The [earlier qualification](../2026-11-06-background-noise/README.md) is preserved.
Its 48 kHz raw-ALSA measurements remain valid for that route. Its complex
simulation injection is now explicitly named `diagnostic_iq_noise`; it did not
represent a full 48 kHz DAC noise floor.

## Digital bandwidth and normalization

Two initial fresh-noise runs and a final rebuilt-binary run each measured four
four-second cases. The clean signal was
rendered with noise Off and On, then subtracted at the final output sample rate.
All output was 48 kHz; changing the logical input rate therefore did not reduce
the noise bandwidth.

| Logical input → output | PCM | Noise setting | Additional check |
| --- | --- | --- | --- |
| 6 → 48 kHz | Float32 | 10 effective bits | Energy above the source's 3 kHz Nyquist |
| 8 → 48 kHz | S16 | 40 dB below full-scale sine | Quantized broadband noise |
| 8 → 48 kHz | Float32 | 1 mW relative to 40 W at final reference RMS 0.1 | Signal gain 0.25 must not rescale the noise reference |
| 48 → 48 kHz | S16 | 60 dB below full-scale sine | Same-rate finalizer |

Across the two initial runs:

- Maximum absolute total-power error: **0.02094 dB**.
- Maximum absolute broad-band PSD error: **0.12443 dB**. The five bands were
  0.1–2, 4–8, 8–14, 14–20 and 20–23.5 kHz, including substantial energy above
  either lower-rate source Nyquist.
- All 65,536 S16 codes, including −32768, survived same-rate Off processing
  exactly in both runs.
- Maximum absolute variance error after the actual streaming audio DDC:
  **0.07487 dB**. The prediction used the 439-tap FIR's measured coefficient
  energy, not an ideal rectangular-bandwidth ratio: variance factor
  `2 * sum(taps**2) = 0.13927234925376508`. The fixed DDC used a 1725 Hz center,
  1450 Hz pass edge, 2000 Hz stop edge and 48 → 6 kHz conversion. Startup history
  was excluded. This tests linear projection of white real PCM; it does not
  establish whitened receiver observations or a channel/admission guarantee.
- The independent run rejected an intentionally 3 kHz-band-limited negative
  control despite its exactly matching total noise power. Thus the check
  distinguishes final-rate injection from noise filtered with the source.

The frozen engineering envelopes were ±0.15 dB total power, ±0.5 dB per broad
PSD band, absolute lag-one correlation below 0.015, and ±0.25 dB DDC variance.
All real cases passed. The recorded approximate 95% mean-power intervals use
Student-t estimates from disjoint 4096-sample blocks; they are descriptive,
per-case intervals under independent sample draws, not simultaneous future
hardware guarantees. Each new transmission uses fresh AES randomness.

Both initial runs used PCM executable SHA-256
`25648ae9a007fb53ef4c676a90529b2c2a78a2a43da07328df10ed8e61c4235f`.
The qualification-script hashes differ because the independent run added the
negative control; it did not change the tested PCM executable. Reports include
actual commands, input/output hashes, expected and measured moments, band
results, DDC parameters and the executable/source hashes. Local detailed reports
are `local-evidence/triad-background-noise-fullrate/digital/report.json` and
`digital-independent/report.json` beneath the same root.

The final rebuilt PCM executable, SHA-256
`b49dbc808c94aa547d314c10f21600e4d5dc30b26a6b01217d66e33a9cce8165`, was checked
again after the additional relative-reference validation guard. Its four cases
all passed, with maximum absolute total-power error **0.01007 dB**, broad-band
PSD error **0.06730 dB** and DDC variance error **0.05070 dB**. All S16 codes
again survived Off exactly and the band-limited negative control was rejected.
The corresponding report is `digital-final/report.json` beneath the local
evidence root.

The initial reports measured about **0.91 seconds** each for qualification work;
the final report measured **1.17 seconds**, all excluding Python startup/imports.
Enabled offline rendering in the final run took **0.00531–0.01724 CPU seconds
per output-audio second**, including resampling where selected, file
validation/read/write, mixing and cryptography. These short feature diagnostics
are not a sustained receiver performance qualification or an isolated estimate
of AES cost.

## Physical native cable measurements

Four short native `triad_audio_capture` runs used ALSA `hw:1,0`, 48 kHz stereo
S16 output/capture with capture channel zero and unchanged mixer settings. Each
played six seconds of a 1737 Hz **0.05-peak** tone; the filename's `rms` suffix
is inaccurate, while the recorded source samples and `input_peak` establish the
actual level. Noise was added inside production playback, not to the input file.

Welch analysis used a central three-second interval, 1400–2100 Hz with
1650–1820 Hz excluded around the tone. The baseline tone gain was **0.841410**.
Prediction was `(2 * variance / 48000) * gain**2`, compared to the observed
added PSD after subtracting baseline PSD in linear units.

| Selected added noise | Observed floor rise | Added-PSD prediction error |
| --- | ---: | ---: |
| 40 dB below full-scale sine | 31.330 dB | −0.04167 dB |
| 60 dB below full-scale sine | 11.828 dB | +0.16457 dB |
| 10 ideal effective bits | 9.919 dB | +0.05022 dB |

All three errors were inside the ±0.5 dB cable envelope. Every enabled run
reported exactly **288,000 active noise samples** at 48 kHz in S16 format;
preroll/postroll were outside the noise generation interval. No capture or
playback discontinuity was reported, no analyzed capture sample clipped, and
tone gain changed by less than 0.002 dB. Each capture command took about eight
seconds. The added spectrum was broadly flat through 20 kHz; the 20–23.5 kHz
band was about 1.8 dB lower, retaining the physical converter/filter response.

The measured native binary SHA-256 was
`e94054895565fecde71b8b3017e52cdb0f4cd76058ff3bbe3dc751bfe33d464f`.
The local `native-cable/report.json` beneath the evidence root records all
commands, waveform/capture/metadata hashes, rate reports, selected intervals,
noise moments and spectral measurements. These are four short observations of
this headphone/microphone cable, not RF watts, independent converter noise-figure
calibration or a physical 90 dB-floor qualification.

These cable and initial digital binaries precede an additional constructor
validation rejecting relative-power reference mean square above one. That guard
does not change generation for any measured setting. This is explicit scoped
reuse of physical evidence, not an exact-final-binary cable claim. The final
rebuilt digital validation above checks the final generation code independently.

## Backend evidence and remaining scope

The finalizer is shared by native ALSA/PulseAudio/PipeWire and Windows output,
and by the host/browser provider. The physical native ALSA cable evidence above
is separate from provider-contract tests. Mock Windows and host checks can establish the finalizer placement, reported rate, stream lifetime
and gain contract, but do not establish physical Windows/browser performance.
A WebAssembly SDK was unavailable locally; this work does not claim a newly
built WebAssembly binary or a physical browser audio test.

The narrowband `live::Settings::simulation` path rejects the full-DAC setting.
For offline audio modeling, render complete noisy audio first and pass that
waveform through downstream reconstruction and channel models. Final DAC noise
on a fixture that already includes a simulated propagation channel has a
separate ordering. No general full-rate HF channel or receiver-admission
qualification is inferred from these bounded checks.

## Focused regression results

- `./build.sh test triad --cli --web-worker --build-dir build/dev-cli --jobs 2`:
  **20/20 passed**, 34.45 seconds CTest wall time.
- Final provider checks: **5/5 passed**, 23.60 seconds. These were
  `background_noise_live`, `alsa_audio_contract`, `alsa_interleave_playback`,
  `windows_audio_contract` and `host_audio`. They cover timed ALSA preroll,
  interleaved routing, the fixed-rate Windows fixture and upper-audio-band
  noise in host-provider output, in addition to the shared finalizer tests.
  The initial Windows fixture failed its resampled-duration assertion; the
  fixed-rate fixture was corrected and the final Windows contract passed.
  Both the original failure and final result are retained.
- Focused ASan/UBSan/LSan run: **4/4 passed**, 5.04 seconds, for `audio_output`,
  `background_noise`, `background_noise_cli` and `windows_audio_contract`.
  The first sandboxed attempt failed because LeakSanitizer cannot operate
  under ptrace; its log is retained. The passing untraced run kept leak checking
  enabled and did not suppress the environmental failure record.
- Python adapters: **7 background-noise, 10 ALSA, 28 duplex and 7 capture tests
  passed**. The new production-CLI background-noise check passed **1/1**.

A broader 49-test production-CLI check was **not green**: it had five failures
and one error in Helper cases. The original `providers-tests.log` preserves the
failures, separately from the corrected final provider run:

| CLI test (`test_` prefix omitted) | Observed result |
| --- | --- |
| `helper_framed_encrypted_wav_and_physical_end`, `helper_wav_callsign_and_timecode_metadata`, `helper_wav_composition_and_separation` | Fixed 550/950 Hz tones overlap the DATA separation guard |
| `helper_physical_simulation` | Helper-assisted batch reception unavailable: causal observation/narrowed acquisition not implemented there |
| `helper_only_live_simulation` | No Helper row in the simulated capture |
| `helper_simulation_diagnostics_survive_payload_failure` | JSON decoding error because stdout was empty |

The relevant guard messages existed outside the noise change, but a clean
baseline comparison was not performed; this record does not establish that all
six results are pre-existing failures. Focused feature success therefore does
not constitute a complete CLI, ordinary receiver or release certification.
These unresolved broader results must remain visible if that scope is later
qualified.

## Reproduction and cheap regression

Use the prepared NumPy/SciPy research environment and an already configured
compatible build. These commands do not access an audio device:

```sh
./build.sh configure --cli --web-worker --build-dir build/dev-cli
./build.sh test triad --cli --web-worker --build-dir build/dev-cli --jobs 2
# Reuse the configured profile; --web-worker makes the host-provider target available.
cmake --build build/dev-cli --target background_noise test_alsa_audio \
  test_windows_audio test_host_audio --parallel 2
ctest --test-dir build/dev-cli --output-on-failure \
  -R '^(background_noise_live|alsa_audio_contract|alsa_interleave_playback|windows_audio_contract|host_audio)$'
python3 tools/triad_fullrate_noise_qualification.py \
  --tool build/dev-cli/background_noise --output local-evidence/noise-fullrate-new
```

Use a fresh output directory. Routine validation should combine the focused
shared-output/provider/CLI contracts with this short bandwidth check; do not
repeat historical weak-signal, multi-day or GPU campaigns merely to qualify
noise placement. No GPU or cloud resources were needed for this work.
