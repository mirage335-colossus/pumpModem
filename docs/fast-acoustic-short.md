# Fast acoustic short transfers

Select **Speakers / mic · short** for short speaker/microphone transfers, or use
`--profile acoustic-short --expected-snr 3` in the CLI. This separate profile
keeps the acoustic OFDM receiver, XZ compression and fixed local coding cycles,
with less training and smaller minimum coding blocks. The existing channel
profiles and initial **Speakers / microphone** selection are unchanged.

The default expected SNR is 3 dB over the original 17.5 kHz reference band.
The selected band remains approximately 504–9,270 Hz, giving an assumed 6 dB
in-band SNR under the same constant-power, white-noise model used by the
existing acoustic presets. The new profile optimizes complete airtime for
2,800 encoded bytes, allowing space for a typical 2.5 KiB file, its filename
prefix and XZ overhead. This is a local preset calculation; received lengths
never choose framing, allocation or completion.

| Default at 3 dB | Existing acoustic | Acoustic short |
| --- | ---: | ---: |
| FFT / cyclic prefix, samples | 32,768 / 4,096 | 8,192 / 512 |
| Training blocks | 16 | 6 |
| Constellation | QPSK | QPSK |
| LDPC frame / nominal rate | 64,800 bits / 2/3 | 16,200 bits / 3/4 |
| Frames per coding cycle | 2 | 1 |
| Net public source rate, including cycle overhead | about 8.6 kbit/s | about 8.0 kbit/s |
| Total airtime for the tested 2.5 KiB attachment | 39.176 s | 11.794 s |

The 2,560-byte incompressible attachment fixture compresses to 2,660 bytes
including its in-band filename envelope. Both public and encrypted transfers
fit one bootstrap and two data cycles: 24 locally fixed 2,048-bit intervals.
The total consists of 1.088 s of training, 4.171 s of data and refresh blocks,
and 6.535 s of end silence and observation guards. A short text fitting one
data cycle takes 10.343 s. These are exact generated-waveform times; audio
device startup can add delay. Larger or unusually long-named attachments may
need another complete cycle; the composer estimates their actual encoded size.

The short profile supports DVB-S2 short LDPC tables C4, C6 and C7, conventionally
named 1/2, 2/3 and 3/4. Their actual information lengths are 7,200, 10,800 and
11,880 bits per 16,200-bit frame. Capacity and airtime calculations use those
actual lengths. Existing 64,800-bit codewords, permutations, channel defaults
and profile identities stay unchanged. The new frame size and training count
are bound into the short profile's integrity context. Both peers must select
the same profile and settings.

Training retains two clock-seed blocks, three independent channel-fitting
blocks and a final held-out verification block. Acquisition still checks 256
independent signs with the existing error allowance, and every coding cycle
keeps its established tracking/verification pilots. No source data is exposed
before physical completion. EOF, cancellation and successful FEC or integrity
checks cannot replace six seconds of fully observed absent symbols.

The shorter cyclic prefix accommodates less reverberation: 10.67 ms instead of
85.33 ms. Only the short profile uses a 2 dB decoder allowance in its preset
search, retaining the 6 dB OFDM acquisition floor; the other profiles keep their
existing allowances. These choices trade some channel tolerance for startup
and padding savings. Lower SNR selections may take much longer, and the weakest
settings retain the narrow single-carrier fallback. The ten-to-twelve-second
target applies to the default 3 dB selection.

`fast_acoustic_short` tests generated audio through fixed-density 3 dB noise,
a 5 ms echo at amplitude 0.30, and up to ±100 ppm clock error. Public UTF-8
text, encrypted 2.5 KiB text, and public/encrypted incompressible attachments
recover exactly. Partial absence, EOF, incomplete training and noise-only
controls are also checked. These deterministic sampled tests do not establish
a whole-file success probability or qualify a physical speaker/microphone path.
The [validation record](validation-data/fast/acoustic-short-20260921/README.md)
includes independent LDPC vectors and integration results.
