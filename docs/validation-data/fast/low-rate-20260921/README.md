# Fast low-rate sampled validation, 2026-09-21

These are deterministic offline device-rate PCM tests, with no live audio and
no saved giant PCM arrays. They validate the single-carrier processing path,
physical framing and observed absence. They do not establish a physical radio
or speaker/microphone success probability.

Reproduce from the repository root:

```sh
cmake --build build --target test_fast_low_rate test_fast_modem --parallel 2
ctest --test-dir build --output-on-failure -V -R '^(fast_low_rate|fast_modem)$'
```

The archive records the final standalone runs before the subsequent harmless
fractional preset-rate canonicalization to 1/(2^20) Hz. The dense 500-baud case
was run separately and has since been included in the main regression. Wall
times depend on other concurrent work; they include feeding trailing noise or
silence as well as the listed signal waveform duration. The standalone
independent `test_fast_modem` vectors and sampled suite also passed.

The regular suite covers:

- 5 baud, 48 kHz, 16-QAM, marker every interval and pilots every 16 symbols.
- 15 baud, 44.1 kHz, 16-QAM, sparse markers and pilots every 64 symbols.
- 100 and 500 baud, QPSK, marker spacing 16 and pilot spacing 256; the 100-baud
  case needs a longer real tail than the historical fixed 6.25 seconds.
- 500 baud with the current 4M-QAM cable constellation and 0.02 rolloff; the
  independent result is in `dense-500-log.txt`.
- The actual SSB -10 dB expected-SNR preset, approximately 10.935 baud,
  16-QAM and LDPC 3/4. A complete 64,800-bit LDPC frame plus fixed-interval
  alignment padding traverses 32 physical intervals. It corrects 2,983 raw
  decisions in the archived run, converges in nine iterations, and recovers
  every source byte exactly. The error count includes the alignment padding;
  LDPC consumes only its complete codeword.
- Bounded construction/streaming at 1 baud, 192 kHz and 0.02 rolloff. This is a
  memory/timing/EOF test, not a complete one-baud transfer.

For the noisy test, received signal amplitude is attenuated by 0.01. Real
white PCM noise has variance
`(amplitude^2 * gain^2 / 2) * Fs / (2 * 2400 * 10^(SNR_dB/10))`.
Thus -10 dB refers to the original 2,400 Hz radio passband. Narrowing to about
12.0285 Hz gives the modeled 13 dB selected-band SNR. Exactly the same white
noise density continues throughout trailing absence. Every PCM sample is
checked against full scale. This assumes flat noise density and unchanged
received signal power when narrowing; it does not model fading or oscillator
offset.

All clean sampled cases require zero raw bit errors. Every case rejects TX
EOF and 5.5 seconds of trailing input as substitutes for physical completion,
then requires completion only after further actual noise/silence. Receiver
workspace remains below 4 MiB and does not grow with signal airtime. The
noisy case intentionally tests one physical LDPC frame rather than a complete
bootstrap/source coding cycle.

Capacity SC below 1,000 baud uses a power-of-two cascade of 63-tap half-band
filters where possible, leaving 32–64 internal samples per symbol. The long
RRC filter operates at that internal rate. TX evaluates the narrow baseband
on the same coarse grid and uses four-point interpolation before its
original-device-rate carrier. Public PCM sample counts and physical time
remain at the selected audio rate. Existing full-rate and OFDM paths remain
separate. Longer tails account for a full marker or pilot group whenever its
observation plus filter lookahead exceeds the usual 0.25-second guard.
