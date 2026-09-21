# Fast acoustic reception diagnosis — 21 September 2026 UTC

The user reported unsuccessful reception with the newly selected speaker/microphone
OFDM profile. Two synchronization faults and a noisy-startup channel-estimation
fault were reproduced and repaired. None is established as the cause of that particular attempt: no recording or
receiver status from the failed attempt was available. The current live path
also successfully received ordinary transmissions before the fixes.

## Devices and reproduction conditions

The default devices remained the ALC257 internal speakers and internal microphone
on `pci-0000_07_00.6`, with 48 kHz hardware audio. Playback was **75%**, compared
with 90% in the earlier bulk study; microphone volume remained **27%**. Both
ports were unmuted. No device selection or mixer volume was changed for this
investigation. The running GUI executable was verified as `build/datapump-gui`,
the current OFDM build, excluding the stale alternative build directories as an
explanation for that running instance.

The ordinary acoustic settings remain 64-QAM, LDPC 3/4, depth eight,
N32768/P4096, pilot stride 16, 500–18,000 Hz, amplitude 0.40, stereo output.
The fixes change receiver estimation only: waveform, profile identity, source
coding, throughput and six-second physical-absence completion are unchanged.

## Confirmed synchronization faults

1. **Startup gain settling invalidated valid training.** Channel estimates from
   training blocks 2–14 were aligned to block 2's gain and timing. The final
   independent training block was checked against that reference without fitting
   the common gain and timing that the data receiver already supports. In a
   noiseless regression, a 1.9× gain ramp passed, while 2.1× (+6.44 dB) and 3×
   (+9.54 dB) ramps never acquired. The receiver now fits common gain, phase and
   delay on the final block's *other* tones. All 128 marker tones remain held out
   of those fits and are selected using earlier training. The 256 sign-bit,
   maximum-16-error marker and residual guard remain in place.
2. **A valid training body at capture start could be discarded.** The detector
   subtracted its precursor allowance from the correlation peak, then rejected
   a negative/near-zero candidate. Cropping only the first 4096-sample cyclic
   prefix reproduced failure despite retaining all training bodies. The receiver
   now clamps the allowance to available PCM. This case recovers exactly;
   losing actual first-body samples still does not acquire.
3. **Quiet startup blocks received excessive weight in the channel estimate.**
   After normalizing training blocks to common gain, the old equal average gave
   the same weight to a noisy quiet block and a clearer loud block. The estimator
   now references fitting block 14, the last block before independent verification,
   and uses squared relative gain as inverse-noise weights. This is weighted least
   squares under stationary additive input noise, without a fitted tuning factor.
   Predictive noise is estimated consistently from weighted residuals and the
   summed weights. Estimated gains, noise that scales with microphone gain, and
   speaker distortion make this an approximate model; data-pilot residuals still
   supply an independent noise floor. The gain-ramp recording that still failed with the acquisition
   fixes alone now decodes all 32 LDPC frames and all 100,000 source bytes exactly.

Both synchronization regressions now recover all expected intervals with zero wrong hard decisions
in the independent before/after reproducer. Regressions also cover a falling
startup gain, delayed echoes, clock mismatch, corrupted/truncated training,
missing blocks, bounded storage, and physical-end/EOF distinctions.

A separate deterministic 30,000-byte coded regression combines a 0.2-to-1 startup
gain ramp, a delayed reflection and additive noise. With noise RMS 0.006, the
acquisition-fixed but unweighted receiver fails all four bootstrap LDPC frames;
the weighted receiver recovers all 30,000 bytes and all 12 LDPC frames. Increasing
noise RMS to 0.007 or 0.008 gives the same before/after success distinction.

## Live observations

- **Unmodified receiver, ordinary 100,000-byte transfer at 75%:** exact success
  in 48.342 seconds, all 32 LDPC frames converged, 1016/1016 intervals, no queue
  overflow. Source and received SHA-256:
  `4562a35db92d6b0af236736ac9f4e46a7c5e3d78530f7bf8ae307afb6b2ca5f9`.
- **Acquisition-fix receiver, full shared GUI application path, ordinary 60-byte text:**
  exact success in 35.290 seconds from starting the receiver, including a
  two-second lead before transmission. Two application instances used the normal
  audio-ownership handoff, Fast controls, real ALSA capture/playback, pending
  presentation, physical completion and explicit Save service. The native widget
  adapter was not driven. Saved text was compared byte for byte.
- **Controlled gain stress:** the probe multiplied the transmitted startup by a
  linear gain ramp from 0.2 to 1 over all 16 training blocks. The rest of the
  waveform used the unchanged normal amplitude; hardware volume was unchanged.
  The old receiver, replaying this *same real microphone recording with matching
  settings*, never acquired. The synchronization fixes acquired and admitted all
  1016 intervals. Three coding cycles converged, but six LDPC frames in the last
  cycle failed and the final source was correctly rejected. This trial is a
  demonstrated synchronization improvement, **not a successful file transfer**.
- **Weighted-estimator replay of that same stress recording:** exact 100,000-byte
  recovery, zero failed LDPC frames, and 389 total decoder iterations (715 before
  weighting). Received SHA-256 matches the original transmitted source:
  `28788cfa4bbc0b39d951a0856eeda6cbe0905e5b54c1038fec898ee2c326d071`.
  This result is an offline re-decode of real audio, not another physical trial.
- **Successful-recording controls:** the weighted receiver still recovers the
  ordinary 75%-volume 100 KB recording (32/32 frames; 156 versus 157 iterations)
  and the earlier 5 MB recording (840/840 frames; 4724 versus 4723 iterations),
  with their original source hashes. These are also replay controls, not new
  live transfers.
- **Fresh final receiver, 500,000-byte live gain-ramp trial:** exact recovery in
  **103.632 seconds**, 96/96 LDPC frames converged, 3048/3048 intervals, 519 total
  LDPC iterations and eleven verified source cycles. Maximum DSP chunk was
  0.266 seconds and capture backlog 0.256 seconds; no FIFO overflow, audio error
  or digital signal clipping was reported. Source and received SHA-256:
  `080e44c65b23be96cf81dbfed118b711e3ce41c72aa5a33e4ff4d0811cc03c50`.
  This was another physical transfer, using a new seed, the same 0.2-to-1 startup
  ramp, unchanged 75%/27% mixer levels and the normal acoustic coding settings.
- **Final weighted receiver, full shared GUI application path:** a further
  ordinary 60-byte live transmission completed and saved exactly in
  **35.285 seconds**, including the receiver's two-second lead. Both application
  processes returned success, with 508 received intervals and a verified source
  cycle; bytes became available only after physical completion.

Simply rescaling the old predictive-noise floor after channel refresh was also
tested and discarded: it reduced failed frames from six to one but did not
recover the file. It also conflated channel-estimate uncertainty with additive
input noise. The adopted change improves the training estimator instead.

A first application harness run mistakenly checked Save between the worker's
completion and the next 40 ms presentation poll. It reported failure while
showing an outdated pending snapshot. The harness was corrected to let the final
presentation poll run. This was a diagnostic-harness error, not evidence of a
GUI receiver defect; its original logs are retained separately.

## GUI coverage and operating behavior

The existing sampled GUI test exercised only cable mode. It now exercises both
cable and acoustic defaults, requiring the same exact Save result, physical
completion, pending state and live/retained diagnostics. Its capture mock now
uses real 50 ms pacing: previously it injected 50 ms chunks every 6 ms, causing
an unrelated synthetic cable FIFO overrun. The enlarged two-profile test passed.

Each GUI instance handles one audio direction at a time. Select **Listen** on
another receiving instance before starting transmission, use matching profiles
and coding settings, and leave it running through completion. Acoustic training
alone lasts 12.288 seconds; a short message takes about 33 seconds through its
end silence. Received bytes become available through **Save received file**.
Listen handles one reception; start it again for the next transmission. Later
coding cycles do not repeat the initial acquisition preamble.

The independent capture FIFO detects DSP backlog overflow. Existing ALSA recovery
can still conceal a device-level discontinuity; no such discontinuity was
established in these trials. Therefore empty audio-error fields must not be
read as proof that hardware never recovered from a gap.

## Final validation

The Release GUI, CLI and probe were rebuilt. All **12 selected CTest targets**
passed in 127.78 seconds: `fast_acoustic`, `fast_files`, `fast_session`,
`fast_transfer`, `fast_telemetry`, `fast_boundary`, `fast_cli`, `gui_fast`,
`gui_fast_live`, `gui_controller`, `gui_application` and `gui_self_check`.
These cover acoustic DSP, source recovery, WAV/live-session completion, shared
GUI behavior and the independent Fast boundary. Native adapter code was unchanged.

The running GUI must be restarted to load the rebuilt receiver. No automatic
restart was performed, preserving the user's open application state.

Detailed logs, before/after outputs, harness source, exact settings and hashes of
retained raw PCM/bits are in
[`validation-data/fast/acoustic-reception-20260921/`](validation-data/fast/acoustic-reception-20260921/).
