# Simulation estimates

The simulation controls show a **modeled whole-draft receive probability** and
two **rough compute-time estimates**. They are planning aids, not measured
confidence, certified error rates, observed hardware performance, or guarantees.
They change when the draft, channel preset, transmit geometry or receive search
changes. Nothing in this model changes transmission or receiver admission.

The fixed reference machine is an **Intel Core i9-13900H** with an
**NVIDIA GeForce RTX 4090 Laptop GPU**. The estimator never benchmarks the
computer, queries CPU/GPU identity, counts local processor threads, or measures
simulation execution. The GPU figure is hypothetical: the current numerical
search backends execute on the CPU. It describes a possible implementation
offloading receiver scoring while retaining waveform/channel generation and
projection work on the CPU. It does not promise an available GPU mode.

## Draft and channel inputs

The model takes the existing exact `transfer::Estimate`, the actual selected
transmit modem profile, independently selected receive profiles, FEC setting,
channel configuration and receive-key count. It does not encode a second draft
or allocate PCM. Short dictionary text and explicit binary input use their
exact wire-bit count and receive **no FEC benefit**, regardless of the saved FEC
choice. Longer text and attachments use the actual count of fixed 128-byte coded
intervals and their existing 192-bit markers.

Elapsed simulated media includes the estimated waveform's settling, payload,
filter padding and suppression; channel delay, an average 175 ms randomized
startup and waveform clock-rate conversion; then
the same whole-symbol absence duration used by the live simulator, plus one
second of lookahead and twice the pulse padding. In particular, a four-hour
symbol requires a fully scored four-hour absent symbol. Compute time and media
duration are different quantities. UI replay pacing is excluded.

## Probability model

The channel SNR uses the simulator's `Fs/2` noise bandwidth. Before losses,
the integrated symbol energy is

`Es/N0 (dB) = channel.snr_db + 10 log10(symbol_samples / 2)`.

The model applies a fixed 3 dB implementation margin, squared-sinc loss for
residual carrier frequency after selecting the closest of the receiver's five
frequency hypotheses using sample-quantized symbol durations, expected
coherent-energy loss from Wiener phase diffusion,
and a triangular correlation loss for pattern timing smear within a symbol.
Tone profiles omit the pattern smear term. These are analytical approximations
to the sampled channel; they do not reproduce adaptive tracking or the exact
public/private codeword correlations.

With the remaining linear energy `g`, the assumed bit error rate is
`0.5 exp(-g/2)`, the noncoherent orthogonal binary AWGN model. Symbol admission
uses a Gaussian energy-statistic approximation with mean `g` and variance
`1 + 2g`, threshold 5 for continuation, and a larger acquisition threshold
`-ln(1e-10) + 2 ln(trials + 1) + ln(10)`. Trials reflect the half-chip start
window, five frequencies and private phase groups of a matching receiver.
Other receive profiles/key families add compute work; their independent searches
do not raise that receiver's admission threshold.
The acquisition trial approximation describes the initial search geometry; it
does not reproduce the actual running trial counter during an extended scan.
These gates approximate the receiver's evidence requirements. Its real adaptive
scores are **not calibrated receive probabilities**. In particular, the model
does not turn the tuning planner's 18 dB target into an empirical success curve.

For raw/short drafts, acquisition is multiplied by the probability that **every
wire bit** is admitted and correct. For interval drafts, an independent-byte
dynamic program applies the existing Reed–Solomon capacity rule
`2 * erroneous_bytes + erased_bytes <= parity_bytes` to each 128-byte codeword.
A byte with any unadmitted bit is an erasure. The marker approximation allows
at most the existing eight erroneous/missing bits per 192-bit marker. Each
interval must survive. A union bound for runs of missing symbols covering six
seconds reduces interval-draft success for possible premature physical
termination; raw success already requires every bit to be observed.
An incompatible receive profile produces no
matching-profile confidence; an empty draft has no probability estimate.

The independence assumptions are especially approximate for oscillator drift,
correlated interference, adaptive acquisition and long receptions. Requiring
initial acquisition and every marker is conservative: the actual receiver can
recover some leading-marker damage. Additional exhaustive recovery, content
syntax, MAC validation and decoder success are not used as evidence of physical
completion or as extra confidence. A displayed value near 100% is still a model
prediction, never a guarantee or authentication claim.

## Fixed reference compute model

The model counts full-rate waveform/channel samples, receiver projection work,
FFT transforms or bounded streaming correlation lanes. It reflects half-chip
start searches, the default five frequencies, private phase/initial-symbol
searches and key/epoch/profile multiplicity. Large private symbols or FFT state
exceeding an approximate per-bank workspace use the correlation cost model.
The exact receiver's memory arbitration, reuse, bootstrap paths and template
cache behavior can select different work; these estimates do not decide whether
a receiver can allocate its workspace.

The fixed engineering budgets are:

| Assumption | Value |
| --- | ---: |
| Serial CPU work | 1.5 billion equivalent operations/s |
| Aggregate CPU scoring work | 8 billion equivalent operations/s |
| Hypothetical GPU scoring work | 80 billion equivalent operations/s |
| Host-to-device transfer | 8 billion bytes/s |
| Channel generation/resampling/noise | 400 equivalent operations/sample |
| Receiver projections | 40 equivalent operations/sample/bank |
| CPU startup allowance | 30 ms |
| GPU path startup allowance, including CPU startup | 110 ms |

FFT work uses a conventional five-operation complex FFT element/stage estimate
plus template multiplication and scoring; correlation uses 64 equivalent
operations per two-bit lane observation. These are deliberately rounded
**assumed effective budgets**, not vendor benchmark results or measured
application throughput. The CPU aggregate budget allows parallel scoring on
the reference laptop while keeping the serial channel bottleneck separate.
The GPU budget reserves substantial headroom for double-precision arithmetic,
memory access and irregular batches; it is not derived by multiplying CUDA
cores by advertised FP32 clock rates. A tenfold scoring budget never becomes a
tenfold whole-program speedup because CPU work, transfers and startup remain.
Very small jobs can consequently have a slower hypothetical GPU estimate.

The hardware names anchor a single repeatable high-end-laptop planning case.
[Intel's processor listing](https://www.intel.com/content/www/us/en/ark/products/series/230485/13th-generation-intel-core-i9-processors.html)
identifies the i9-13900H as a 14-core part with up to 5.40 GHz turbo.
[NVIDIA's laptop comparison](https://www.nvidia.com/en-us/geforce/laptops/compare/)
lists the RTX 4090 **Laptop** GPU with 9,728 CUDA cores, a 1,455–2,040 MHz boost
range and an 80–150 W subsystem range. Those specifications identify the
reference products; **they do not establish the assumed throughput rates**.
Laptop power limits, sustained thermals, compiler choices, caching and eventual
GPU implementation can change runtime substantially. Treat the displayed times
as order-of-magnitude estimates; even a factor of four is not a validated error
bound. Content compression, exceptional recovery searches and concurrent
background tasks are excluded.

The UI supplies the profile geometries of every distinct receive-key family,
including plaintext where permitted, so private epochs and search costs are
counted separately. The API's optional `receive_key_count` can instead scale
equivalent profile banks. The exact dynamically allocated and retained bank is
not reproduced by this compact model. The UI's receive-profile match concerns
waveform geometry, not the secret key's validity or successful authentication.

## Verification

`simulation_estimate` checks deterministic reference estimates, SNR and draft
length response, fixed-interval FEC versus unprotected short input, incompatible
receive profiles, additional receiver work, phase noise and full four-hour
absence accounting. These checks validate model mechanics, not its empirical
calibration. The independent protocol and physical-completion regressions remain
the authority for actual transport behavior.
