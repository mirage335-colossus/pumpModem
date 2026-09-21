# Continued acoustic experiments

These files extend the first-session sounder and modem archive. Live trials use
real default playback/capture, unchanged mixer levels, stereo output, and the
production S16 capture path. Offline controls never open an audio device.

Each archived probe JSON retains the full local settings, source/received hashes,
physical completion, coding statistics, timing, FIFO high-water mark, signal
levels, and hashes/paths of the larger PCM and exact transmitted-bit files in
`/tmp`. Failed experiments are retained alongside successes. `exact: 1` requires
full source-byte equality, observed physical end, and no audio/FIFO error.
`mode: raw` does not claim a coded file transfer. All sizes are decimal bytes.

## Reproduction

A live trial can be repeated with `build/fast_cable_probe`, reading its options
from the JSON. For example, the successful smoothed-refresh control used:

```
build/fast_cable_probe --profile acoustic --capacity --mode codec --bytes 500000 \
  --qam 64 --code-rate 7/9 --depth 4 --amplitude .40 --ofdm-fft 32768 \
  --ofdm-prefix 4096 --ofdm-pilots 4 --stereo --seed 542 --require-success
```

The capture device is physical: do not run multiple live trials concurrently.
Add `--replay /tmp/CAPTURE.f32` for a receiver-only replay. This requires the
same wire revision and every integrity-bound local parameter; it does not
reproduce the physical channel or qualify a new waveform.

Exact-bit diagnostics use the capture and the **saved original** unpacked bit
fixture, including the original randomized bootstrap:

```
build/acoustic_known_symbols /tmp/CAPTURE.f32 /tmp/ORIGINAL-bits.u8 \
  /tmp/RESULT 64 4 32768 4096 4
```

The final arguments are QAM, depth, FFT, prefix, and requested pilot stride.
The diagnostic does not fit a channel to known payload bits. Block CSVs retain
raw BER, true-label EVM, residual estimates, timing, and the sum of bitwise
logistic-information metrics. They use the actual clipped LLRs at unit scale;
they are not an independently established Shannon upper bound.

## Receiver controls and wire revisions

- `ofdm-v2-*` trials used `/ofdm/v2`, sixteen startup blocks, fixed pilot stride
  eight, and replacement of the channel estimate at each cycle refresh. The
  committed `ec091f7` source retains that revision.
- `v2-guard-*` changes only the prefix at N16384 with raw complete cycles.
- `v3-*` binds the configurable pilot stride in `/ofdm/v3`. Dense/sparse 200 KB
  trials still replaced the channel estimate at refresh.
- `acoustic-ofdm-before-refresh-filter.cpp` freezes that v3 receiver.
- `acoustic-ofdm-smoothed-refresh.cpp` freezes the accepted 75% prior / 25%
  new-estimate receiver used by subsequent 500 KB and larger trials.
- `acoustic-ofdm-linear-tracking.cpp` is the discarded 80%-weight linear
  interpolation of complex tracking-pilot residuals, with verification pilots
  measuring its residual. It reduced information in both controls.
- `*-fixedh-blocks.csv` retained the previous channel multiplied by the fitted
  common gain, rather than taking the full-band refresh measurement.
- `*-sinc-blocks.csv` replaced linear residual interpolation with a 16-neighbor
  Hann-windowed sinc kernel. The kernel was modulated to a causal delay centre
  `min(prefix, N/tracking_pilot_spacing)/2`, still weighted 80%. It also reduced
  information and was omitted from production.
- `*-dd-blocks.csv` starts from smoothed refresh and adds a 15% data-tone channel
  update only when every demapped bit has absolute LLR above three. The update
  divides the received tone by its nearest QAM label and fitted common
  gain/delay. Its small information gain did not repair the failed cycles; this
  experimental decision-directed update was omitted from production.

`known` CSVs describe the receiver active when each recording was made; `smooth`
CSVs are controlled replay using the accepted refresh average. The N32768 raw
recording was made with the discarded linear tracker, so `no-tracking` is its
independent replacement-refresh receiver control.

LDPC scheduling logs compare serial orchestration against the acoustic-only
bounded parallel implementation, including successful noisy correction,
uncorrectable frames, ordered exceptions, and depth sixteen. CPU timing is
host-specific and not an audio-link reliability claim.
