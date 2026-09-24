# Acoustic low-SNR receiver recovery, 2026-09-21

Baseline repository revision: `95da9ee44a3af0a088fe79acb886ea8e69ea48fe`.
The baseline probe executable was frozen before editing the receiver.
The [diagnosis](../../../fast-acoustic-low-snr-recovery.md) explains the bug,
correction and interpretation of these results.

## Real audio setup and short trials

Default output: `alsa_output.pci-0000_07_00.6.analog-stereo`.
Default input: `alsa_input.pci-0000_07_00.6.analog-stereo`, a physical microphone
source, not a monitor. PipeWire 1.4.2; production S16 audio API at 48 kHz;
mono/right-channel playback. Speaker volume was 100%, microphone 27%, neither
muted. No system mixer setting was changed. No synthetic impairment was added
to the live captures. Ambient conditions were not controlled statistically.

Baseline high-level capture command:

```sh
/tmp/acoustic-minus10-baseline \
  --profile acoustic --capacity --single-carrier --mode raw --intervals 8 \
  --qam 16 --code-rate 3/4 --depth 1 \
  --symbol-rate 73.089804649353027 --carrier 1800 --rolloff .2 \
  --amplitude .4 --marker-spacing 4 --pilot-spacing 64 --tail 9 \
  --capture-save /tmp/acoustic-minus10-before.f32
```

The lower-level baseline trial used the identical command with
`--amplitude .1257078722` and a different capture destination. Baseline JSON
and progress logs are retained as `baseline-before.*` and `baseline-lowamp.*`.
The probe deliberately continued recording after the old receiver's premature
physical-end decision, preserving the subsequent live waveform for replay.

The corrected receiver processed those same recordings using
`build/fast_cable_probe`, replacing `--capture-save` with `--replay` and the
matching recording path. Final-runtime results are `replay-before-fixed.*`
and `replay-lowamp-fixed.*`. Timestamp fields identify the first processing
chunk reporting acquisition/completion, so replay timestamps have approximately
50 ms resolution. They are capture-relative, including one-second pre-roll.

Raw trials bypass the codec deliberately: a nonzero raw-bit error does not
establish LDPC or whole-source failure. `exact=0` on these two recordings
therefore reflects remaining raw errors, not a failed protected message.
Reported full-band signal/pre-roll ratios are uncalibrated level comparisons;
they are not the selected-band demodulator SNR or measured negative-SNR margin.

## Complete protected-source live test

```sh
build/fast_cable_probe \
  --profile acoustic --capacity --single-carrier --mode codec --bytes 60 \
  --qam 16 --code-rate 3/4 --depth 1 \
  --symbol-rate 73.089804649353027 --carrier 1800 --rolloff .2 \
  --amplitude .1257078722109418 --marker-spacing 4 --pilot-spacing 64 \
  --tail 11 --capture-save /tmp/acoustic-minus10-codec-fixed.f32 \
  --require-success
```

This sends a deterministic 60-byte source through the actual bootstrap, LDPC,
RS and integrity path. The diagnostic adds 11 seconds of real trailing capture;
the application helper's required tail is shorter and is included in the
527.513-second normal airtime estimate. Completion still requires scored
physical absence. The full recording is also replayed through the final build.

`live-codec-fixed.json` and its progress log record success: 64/64 intervals,
60 exact source bytes, matching SHA-256, two converged LDPC frames, 714 changed
bit decisions, no failed cycles. Physical end was reported at capture time
526.869 seconds; capture ran for 531.284 seconds. There was no overrun, reported
device error or PCM clipping. Live testing used the pilot-presence and output
power fixes; the subsequent phase-recovery refinements are additionally checked
by final-build replay of exactly the same audio.
`replay-codec-final.json` confirms the same 60 bytes, all 64 intervals and 714
corrected bit decisions with the final receiver. Its 526.9-second completion
timestamp agrees with the live event within replay chunk resolution.

## Regressions and evidence limits

`fast_pilot_presence` exercises a short fade at the reported preset, a permanent
180-degree phase reversal and a two-second fade at five baud. It checks timed
erasures, exact later interval positions, recovery before another full marker,
noise after signal, insufficient partial absence, EOF and noise-only controls.
An additional five-baud fixture changes phase halfway through a scheduled full
marker. The earlier receiver falsely ended at 884.941 simulated seconds;
the corrected receiver keeps four erased intervals in position and recovers
eight exact intervals. Independent silence/noise replacements still complete.
`test-fast-marker-baseline.log` and `test-fast-marker-fixed.log` retain this
negative control and final result.
`fast_output_power` measures real settled transmitter PCM for OFDM, ordinary
single carrier and the low-rate interpolator. Their powers agree within
0.007 dB; a deliberate reversion to amplitude 0.4 fails by about 10.05 dB.

The `offline-*` files preserve a streaming complete-source reproducer and
before/after results, including the original geometry error. They are sampled
synthetic controls, not live-channel reliability measurements.

`capture-sha256.json` identifies raw float recordings and the frozen baseline
binary retained in `/tmp` for this session. Large PCM recordings are not
committed. Compact JSON/logs and reproducible source are retained here. One
successful physical transfer cannot establish a file-success probability or
qualification at a calibrated −10 dB reference-band SNR.

The integrated Fast, shared-GUI and selected ordinary compatibility tests
passed 31/31 in 343.95 seconds. After the final scheduled-marker refinement,
the six affected modem/radio/preset/power suites passed again in 37.16 seconds.
The final marker fixture was also rebuilt and checked separately after its
last test-source edit. Both FLTK and Rev GUI executables and the CLI rebuilt.
No native UI adapter changed; native rendering checks were not repeated.
