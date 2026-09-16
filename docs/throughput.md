# Throughput and pattern confidence

Automatic planning previously imposed 64 chips on every bit, even when the
selected link had ample signal energy. The chip clock is nominally `B/2`, so
12 kHz bandwidth could carry only 93.75 gross bit/s. That was a planner and
waveform restriction, not the capacity of a 12 kHz channel.

The planner now uses 16 chips with at least 30 dB modeled in-band SNR, 32 chips
with at least 24 dB, and the previous 64-chip floor otherwise. It still lengthens
integration to meet the existing 18 dB symbol-energy model. Automatic tones
retain their prior floor. Shortened profiles must contain only whole chips at
the actual sample clock. If 16 and 32 chips both fail that alignment check,
automatic selection retains 64. This avoids adding periodic short-chip holds
at fractional bandwidths or custom clocks; the 12 kHz/48 kHz plan is aligned.
Private profiles whose carrier permits compact orthogonal receive bins reserve
at least 32 chips; boundary-bit probes did not support 16 chips on that path.
Other profiles outside the bounded 256-sample exact-fit range retain 64 chips.
At 12 kHz the following rates exclude source and interval coding, alignment
markers, the rounded hardware-settling prefix, filter tails and suppression noise:

| Target C/N0 | In-band SNR | Chips/bit | Gross bit/s |
| ---: | ---: | ---: | ---: |
| 40 dB-Hz | -0.79 dB | 64 | 93.75 |
| 65 dB-Hz | 24.21 dB | 32 | 187.5 |
| 80 dB-Hz | 39.21 dB | 16 | 375 |

The high-SNR improvement is fourfold. It remains well short of the requested
multi-kilobit bulk-transfer rate: one MiB of uncompressed payload alone takes
about 6.2 hours at 375 bit/s, before coding and other overhead. In comparison,
the ideal band-limited AWGN capacity `B log2(1 + SNR_B)` is approximately
156 kbit/s at 12 kHz and 80 dB-Hz. That theoretical bound assumes an appropriate
code and channel; it is not a modem benchmark or a throughput promise.
See [MIT's communication-systems lecture notes](https://ocw.mit.edu/courses/16-36-communication-systems-engineering-spring-2009/a49d9e2954b440def3794fe73e79756c_MIT16_36s09_lec13_14.pdf).

## What remains intact

The transmitter's circular I/Q mapping, private amplitude distribution, chip
cadence for eligible shortened profiles, purpose-separated keys, absolute keystream addressing, Data encryption
and prefix counter separation are unchanged. A shorter symbol still consumes
fresh private chips. Raw and dictionary messages retain exact bit counts;
keyed interval authentication and boundary recovery still follow pattern acquisition.
The [development contract](development.md) requires preserving these paths and
per-bit pending reception through future throughput work.

Private symbols of at most 256 samples with nonorthogonal carrier bins now use
the existing exact two-real-basis sample fit used by short public patterns.
Orthogonal private bins retain their compact search and a 32-chip automatic floor.
The exact fit prevents a half-chip integration
bin from mixing adjacent chips and capping evidence for an otherwise strong
short signal. The evidence count remains capped at four real dimensions per
chip. The noise threshold, alternative margin, search penalties, disjoint
chain evidence and requirement to confirm pending tails have not been lowered.
The long-symbol correlator and weak-link duration model are unchanged.

The floor is deliberately conservative. An eight-chip `101` capture at 12 kHz,
48 kHz PCM, 36 dB sample-power SNR, channel seed 13, 100 ppm relative clock error
and 0.5 degrees/sqrt(second) phase diffusion lost its first public bit or final
private bit in the sampled-channel probe. Clean PCM alone did not expose this
failure. At 12 kHz, sixteen-chip patterns passed the corresponding sampled cases and
the lower 30 dB in-band gate. Manual shorter patterns remain available.

These are model and regression results, not calibrated detection probabilities
or a physical interception assessment. Existing band occupancy, pulse shape,
chip timing and burst edges remain observable. Public unkeyed rows repeat and
make no LPI claim. Updated automatic plans require matching endpoint settings;
the receiver does not silently substitute an older 64-chip high-SNR plan.

## CPU and live throughput

Gross bit rate describes airtime; a receiver must also process samples fast
enough. The short-pattern sample fit searches more timing positions per second.
It now reuses exact initial template transforms and current-symbol references
when spare workspace permits. Cached values are specific to the receiver's
key, epoch and profile. The optional caches are accounted for and discarded
before they would reduce payload capacity or prevent a workspace reduction.
Regression tests compare cached and uncached hypotheses and scores exactly.

Iterative pattern scoring uses a shared persistent worker pool, defaulting to
all but one CPU available to the process (at least one, respecting Linux CPU
affinity). FFT hypotheses and clock-window fit accumulation run concurrently;
trial counting, tie-breaking, admission and physical completion retain their
original order. Each worker has private mutable template state. Parallel scratch
is charged to the receiver workspace and released before it would displace
payload storage or prevent a workspace reduction. Scratch is retained only
during processing, preserving idle receiver-bank capacity. Available independent
hypotheses and spare workspace can limit simultaneous workers. `PatternSearch::worker_threads = 1`
selects serial scoring for comparisons; zero selects the automatic default.

A wide key/epoch bank can still fall behind real time. The benchmark below
measures generated noise through the ordinary streaming receiver with one epoch
or thirteen epochs. Its final optional argument selects scoring workers. Run it
without other CPU-heavy jobs. It excludes audio-device behavior, and live startup
may require additional epoch coverage for the settling interval.

```sh
cmake --build build --target benchmark_receiver
./build/benchmark_receiver 12000 80 0 1
./build/benchmark_receiver 12000 80 0 0
./build/benchmark_receiver 12000 80 6 1
./build/benchmark_receiver 12000 80 6 0
```

On the 12-logical-CPU Ryzen 5 PRO 5650U development host, Release measurements
on 2026-09-16 used five-second runs after warm-up:

| Bandwidth / C/N0 target | Keyed epochs | One worker | Automatic (11 workers) | Speedup |
| --- | ---: | ---: | ---: | ---: |
| 12,000 Hz / 80 dB-Hz | 1 | 8.81x real time | 16.55x real time | 1.88x |
| 12,000 Hz / 80 dB-Hz | 13 | 0.71x real time | 1.25x real time | 1.77x |
| 1,200 Hz / 40 dB-Hz | 1 | 56.11x real time | 102.53x real time | 1.83x |

The thirteen-epoch case exceeded real-time throughput with automatic workers in
this run. These generated-noise measurements are CPU results, not successful
file-transfer or interception tests; speedup depends on profile and hardware.

## Higher information density

More labels per multi-chip pattern can preserve the private waveform's circular
distribution: multiply each fresh private chip by a label-dependent unit phase
rotation. Each label must have distinct internal transitions, so fitting an
unknown common phase and amplitude cannot make it identical to another label.
This is a viable direction for denser codewords without introducing amplitude
rings or public acquisition pilots. More constellation points do not by
themselves increase minimum separation at fixed energy.

That change needs a complete receiver implementation and validation before it
can be enabled. The present receiver has exactly two templates, binary search
penalties, one-bit stream offsets and one-bit continuation records. A denser
alphabet must account for every label in acquisition and chain confidence,
preserve stream offsets and exact final bits, and fit its timing/key/epoch
search into practical CPU and memory budgets. Byte-aligned interval-only profiles
with 2, 4 or 8 bits per codeword are one possible route that avoids tail padding;
raw and short dictionary profiles could retain binary codewords. The additional
profile search would itself need confidence accounting. No such profile is
enabled by this update.

## Challenging links

The existing energy-duration law still increases integration tenfold for each
10 dB reduction in C/N0 after the finite chip factors are exhausted. Path loss
and C/N0 describe different quantities. For example, the existing model's
70 dBm transmitter, -250 dB attenuation and -164 dBm/Hz receiver noise density
give C/N0 = -16 dB-Hz. Its 18 dB energy target requires about 2,512 seconds per
bit, or 2.1 hours for three bits. This is an integration estimate; coherent
oscillators, adequate search coverage and an actual channel measurement remain
necessary to establish that link.

## Validation

`test_pattern_receiver` covers unsynchronized short patterns, sampled channel
clock/phase impairments, noise-only and wrong-key rejection, shared carrier
projection and existing weak-chain/tail admission. `test_pattern_code` checks
short private waveforms across Scrambler/DSSS combinations for unchanged stream
coordinates, circular statistics, variable amplitude and absence of the former
squared-carrier invariant. `test_pattern_transfer` exercises exact few-bit
payloads and authenticated marker-bearing files through the ordinary sampled
receiver, and estimates one-MiB streaming airtime without allocating its PCM.
A one-MiB estimate is not a completed one-MiB reception experiment.

The full-size test also checks the unpacked-bit storage limit separately from
packed interval-codec workspace. Each 1,024 coded bits gains a 192-bit marker,
giving 1,216 transmitted bits per interval: 18.75% marker overhead relative to
coded bits, separate from source fill, FEC and keyed HMAC. Applying a packed-byte
allowance directly to those 0/1 elements could
reject a file at its advertised content quota. Transmit, recovery, batch receive
and live receiver banks now share the checked bit-capacity calculation. Content
admission and actual DSP allocation ceilings remain separate and unchanged.

The full live-session suite passes, including growing file reception, loaded
key banks, epoch changes, delayed listeners, weak-link simulation and
cancellation. Focused address/undefined-behavior sanitizer runs pass for planner
clock guards, exact cache equivalence and eviction, and one-MiB capacity/overflow
cases. Leak detection was disabled because the sandbox's tracing environment
does not support LeakSanitizer; these runs do not establish leak freedom.
