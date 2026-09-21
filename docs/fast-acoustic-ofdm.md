# Fast acoustic OFDM

The acoustic capacity waveform is implemented separately in
`src/fast/acoustic_ofdm.cpp`. The existing single-carrier cable, classic Fast,
and ordinary transport waveforms are unchanged. The acoustic profile uses the
capacity source/LDPC/outer-RS codec with fixed local coding-cycle geometry. Its
profile identity has an additional `/ofdm/v3` domain and binds the constellation,
code rate, interleave depth, 48,000 Hz processing rate, FFT size, cyclic-prefix
length, pilot stride, and lower/upper occupied-band parameters. Both peers must match these
settings. Single-carrier symbol rate, carrier, RRC rolloff, pilot spacing, and
marker spacing do not control this waveform.

The default acoustic preset is 64-QAM, LDPC 3/4, depth eight, N32768/P4096,
pilot stride sixteen, 500–18,000 Hz, amplitude 0.40, and stereo output. It was
selected after a complete 5 MB physical transfer. Its 11,947 active tones
contain 747 pilots and 11,200 data tones; eight data blocks and one refresh
per steady cycle yield 56.04 kbit/s of public source capacity. The waveform's
startup and final silence remain significant for small files.

## Fixed frequency and time geometry

An illustrative geometry is an 8,192-point real OFDM transform with 4,096 prefix
samples and a requested 500–18,000 Hz band. Positive-frequency bins run from
`ceil(low * N / 48000)` through `floor(high * N / 48000)`, inclusive. Their
negative-frequency conjugates produce real PCM. DC, Nyquist, and unallocated
bins are zero. This geometry has 2,987 active positive-frequency bins, with
centres from 503.90625 to 18,000 Hz. A complete physical block lasts 256 ms.

With requested pilot stride eight, every eighth active bin is a known QPSK pilot
at this geometry. The effective stride is locally derived as
`max(2, min(requested_stride, floor(active_bins / 258)))`; smaller valid
bands use a denser comb to retain enough verification coordinates. Alternate
pilots serve independent purposes: 187 fit tracking parameters and 187 are
eligible for verification. The receiver selects 128 verification tones using
previously trained channel/noise quality. The 2,613 remaining bins carry
unit-average-energy Cartesian Gray QAM in increasing frequency order.

One fixed coding cycle contains `cycle_intervals(profile) * 2048` coded bits.
Those bits are packed continuously across data bins and OFDM blocks. The last
block discards only the mapper capacity beyond the locally fixed cycle size;
received lengths never select framing or completion. Unused bins carry known
pseudorandom QPSK and unused bits of a partial QAM label are pseudorandom. Filling
them with identical corners was experimentally found to create clipping peaks.
There are no additional source intervals for this mapper fill. The source
callback may end only at a complete coding-cycle boundary.

PCM uses one fixed scale for all blocks: nominal RMS is `amplitude / 4.5` for
unit-energy symbols. This resembles the crest ratio of the channel sounder;
`amplitude` is not a guaranteed maximum sample magnitude. There is no per-block
normalization or hard clipping. Actual generated/captured peak measurements
remain part of live validation.

## Training, tracking, and likelihoods

The fixed startup contains 16 complete known blocks. The first two repeat one
training waveform to seed relative DAC/ADC sample-clock estimation. The next
13 use independently selected QPSK phases across all active bins. Training-only
phase, gain, and delay fits align those observations before per-bin channel and
residual-variance estimation. Independent phases expose residual inter-block
echo and signal-dependent error that identical training can conceal. The final
block is held out of channel and timing fitting and verifies acquisition.

The initial candidate comes from FFT correlation against the first training
waveform. Correlation proposes a candidate; it does not itself admit a stream.
The transform window starts up to one millisecond before the strongest
correlation peak, preserving almost all of the prefix for the measured causal
room response. This precursor allowance is a measured-path design choice, not
a claim that every room has the same delay distribution.

Every data block fits common gain, phase, and phase-versus-frequency timing
correction from its tracking pilots. The independent verification pilots then
score presence against the existing channel estimate. The clock loop updates
the sample grid, using 32-tap windowed-sinc interpolation. Full sampled tests
recover exact 16-QAM bits at ±100 ppm relative clock error.

One full-band known channel-refresh block precedes each coding cycle after the
first. Its presence is checked against the **previous** channel estimate before
its known symbols update all frequency bins. The gain-aligned prior estimate
receives weight 0.75 and the new measurement weight 0.25: replacing the averaged
estimate with one noisy block degraded held-out live recordings. It carries no
source bits. This
prevents a channel fit from making its own verification word pass. Refresh is
needed because the measured speaker/microphone frequency response changed
enough over tens of seconds to defeat a one-time estimate.

The soft demapper uses separable Gray-PAM max-log distances. A deeply faded bin
has low confidence through its frequency-dependent channel/noise normalization.
Residual noise is estimated in received-bin units from nearby independent
tracking pilots, then divided by channel power; the original global normalized
floor produced overconfident errors in deep fades. Training variance supplies
a lower bound. This is an unbiased frequency-domain equalization likelihood;
the plotted unweighted constellation EVM can be dominated by a few deep nulls
and must not be presented as hardware SNR or Shannon capacity.

## Exact duration and bounded reception

Let `C` be the number of complete coding cycles, `B` the number of data blocks
per cycle, `N` the FFT size, and `P` the prefix size. For nonempty input the exact
generated sample count is:

```
(16 + C*B + C - 1) * (N + P)
```

For an empty interval source it is zero. The steady coded-bit rate includes
cycle rounding and refresh: `cycle_bits * 48000 / ((B+1)*(N+P))`. Increasing FFT
size does not necessarily improve this rate: whole-cycle mapper rounding and
refresh overhead can outweigh the lower prefix fraction. The exact modem
estimator is authoritative; single-carrier baud arithmetic does not apply.

The receiver ingests bounded chunks even when a caller supplies a whole file.
It retains only bounded acquisition history, one fixed coding-cycle buffer,
and unresolved intervals covering the finite absence window. Every complete
2,048-bit interval is delivered as soon as its fixed coordinates are available.
After a missing block, timed unknown intervals are queued and delivered only
if a later independently admitted block returns. Pending terminal absence is
discarded rather than emitted as fictitious coding cycles.

Only complete scored physical blocks contribute to absence. The receiver waits
for the entire `N+P` duration, and requires consecutive failed durations of at
least six seconds. EOF, cancellation, successful LDPC correction, the final
source flag, and a verified digest never manufacture physical completion.
Transmitters append enough silence for those complete blocks, including the
allowed sample-clock range and FFT-window offset. `end_silence_samples()` is
shared by live playback, WAV output, and the estimator; the old fixed 6.25-second
tail was insufficient at some valid large-block geometries. Production acoustic
capture has a fixed four-second queue for parallel LDPC cycle decoding. An
overrun explicitly fails the transfer rather than deleting sample time.

`tests/test_fast_acoustic.cpp` covers delayed echoes, sample-clock mismatch,
exact coded recovery, mapper peak control, false training candidates, noise,
bounded large-input handling, and EOF/physical-end behavior.

## Integrity scope

The provisional signature is 256 held-out QPSK signs with at most 16 errors,
plus a residual-energy guard. The conditional random-error model, search
accounting, limitations of a receiver-wide false-lock claim, and the separate
256-bit protection of accepted coding cycles are specified in
[the integrity analysis](fast-capacity-integrity.md). Correlation or LDPC
success alone is not an accepted byte-boundary guarantee.
