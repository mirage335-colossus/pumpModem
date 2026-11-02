# Three-carrier experiment: inner Rate and Added Symbols

This version implements the two requested HF configurations in a separate
complex-baseband simulator. Ordinary Robust transmission, reception, GUI,
framing and physical-completion behavior are unchanged. The
[first experiment](triad-modem.md) remains available as v1; its different
Added Symbols semantics and measurements do not describe v2.

The [v2 measurement package](validation-data/triad-v2/2026-10-31/README.md)
records the two physical links, weak-signal sweeps, false admission failures,
thread/precision costs and exact reproducible commands.

## Configuration mapping

The GUI Rate is the **inner** Rate. Added Symbols multiplies the fine-symbol
rate. Dividing the ordinary fine rate among three adjacent carriers gives
`fine_rate = Rate × Added / 6` per carrier. Root-raised-cosine shaping retains
roll-off 0.25 and support of eight fine symbols on each side. Carrier centers
are `−1.25 × fine_rate`, zero, and `+1.25 × fine_rate` in complex baseband.

| Quantity | Example 1 | Example 2 |
| --- | ---: | ---: |
| Inner Rate / Added Symbols | 360 Hz / 10 | 36 Hz / 10 |
| Fine symbols/s per carrier | 600 | 60 |
| Carrier spacing | 750 Hz | 75 Hz |
| Combined ideal occupied bandwidth | 2250 Hz | 225 Hz |
| Total average transmit power | 4 W | 4 W |
| Path loss | 60 dB | 180 dB |
| Received power before modeled fading | −23.9794 dBm | −143.9794 dBm |
| Noise density | −164 dBm/Hz | −164 dBm/Hz |
| Physical C/N₀ | 140.0206 dB-Hz | 20.0206 dB-Hz |
| SNR in the ideal occupied band | 106.4988 dB | −3.5012 dB |
| Planner target C/N₀ | 40 dB-Hz | 8 dB-Hz |
| Ordinary planner integration reference | 0.355556 s/bit | 14.222222 s/bit |
| Base oscillator effective relative bound | 100 ppm | 0.0001 ppm |
| RF shift oscillator effective relative bound | 0.0001 ppm | 0.0001 ppm |
| FHSS | Fake FCC | Off |

These values come from `src/tuning.cpp`, `src/pattern_code.cpp`,
`src/modem.cpp`, `src/gui/controller.cpp`, and the maintained
[clock](clock-sync.md), [oscillator](oscillator-models.md),
[Added Symbols](spread-spectrum-controls.md), and
[channel](channel-models.md) guides. The planner's “target SNR” is C/N₀ in
dB-Hz, with an illustrative 18 dB symbol-energy target; it is not injected
sample SNR. Ordinary automatic inner lengths are respectively 64 and 256.
Their ordinary sample rates are 14400 and 6120 Hz. The separate simulator can
use a lower sample rate because its complex signal is centered at zero.

Sample noise is scaled from physical C/N₀ using the full transmitted mean
power, including cover and guards. Its variance is proportional to sample
rate; changing oversampling cannot improve the link budget. `--cn0` changes
the physical test link while retaining the configured planner target and
waveform. No received-power renormalization compensates for channel loss.
Four watts means the sum of all three carriers, not four watts each.

The lower planner target is intentional and appropriate: it designs for more
path loss than the nominal link predicts. Example 2's 20.0206 dB-Hz physical
link and 8 dB-Hz target provide about 12.02 dB of nominal design margin.
Testing the unchanged transmission with `--cn0 8` exercises that lower target,
equivalent to about 192.02 dB path loss at the same transmit power and noise
density. The stronger nominal-link test and the target-level test serve
different purposes; success at the stronger level does not establish target
receivability. The [recorded target-level tests](validation-data/triad-v2/2026-10-31/README.md#physical-links-and-marginal-sensitivity)
did run at 8 dB-Hz, but the original 1.6-second-segment receiver admitted only
5/16 correct coherent captures and 0/16 differential/amplitude-only captures.
Thus that historical campaign tested the intended target without establishing
90% or 99% correct admitted reception there. The
[current reference convention](triad-v3.md#target-snr-link-margin-and-simulation-reference)
preserves both the design margin and unnormalized channel energy changes.

## Pattern calendar

V2 uses a global integer fine-symbol calendar. Added Symbols repeats each
inner triplet, multiplies each carrier by its own keyed phase sequence, then
optionally permutes whole fine symbols within the bit's allotted positions.
The carrier-specific multipliers retain structure in adjacent-carrier
products. A common phase multiplier would cancel from those products.

Every bit has at least 64 complete inner triplets, spans at least twenty
400-ms dwells and eight seconds, and receives its own configured energy
collection time. Multiple bits share a longer frame, rather than each being
credited with the frame's full energy. The reported payload allocation is
nominal fine-coefficient duration; it is not post-channel recovered energy.
The sampled waveform's full mean power is measured separately. Without FHSS, pulse support continues
across virtual dwell boundaries; Rate need not be commensurate with a dwell.
The optional Added Symbols permutation serves the AGC-related purpose
separately from this mandatory temporal diversity. Burst waits separate
complete frames.

Keyed amplitudes are nonzero and the two bit alternatives have different
amplitude patterns. Pulse shaping and carrier summation can nevertheless
produce instantaneous envelope zeros. Sparse amplitude/phase patterns and
filtered random streams are noise-like, not mathematically Gaussian white
noise. Emission bandwidth, envelope statistics and decoder results are
measurements; they do not establish low probability of intercept or equal
sensitivity to the ordinary single-carrier modem.

Secret keys and system-clock epochs address domain-separated cryptographic
streams. The study's fixed key and epoch are reproducibility fixtures;
`--now` exercises the current system-clock path. V2 adds no nonce exchange or
authentication framing. Its exact bit count is a local experiment input,
never information extracted from received data.

## Timing, oscillators and channel

The two examples use 30 MHz RF shift and a nominal 1500 Hz audio conversion
IF. Independent audio clocks change both the fine-symbol timing and the
common IF residual. Only the RF oscillator error is multiplied by 30 MHz.
The simulator applies transmitter oscillator phase before propagation and
receiver oscillator phase afterward. The explicit realization convention
assigns each independent endpoint half the relative ppm bound; their
difference is bounded by the documented relative profile. Each endpoint's
combined base/RF phase diffusion is 0.5 degrees/√second, giving the documented
0.7071 degrees/√second relative diffusion. This is a stochastic model, not a
measured device phase-noise spectrum.

Margin 3 expands oscillator search bounds. It also selects effective shared
spread parameters of 1.5 ms delay and 0.3 Hz Doppler from the requested
0.5 ms / 0.1 Hz preset. Additional slow and fast frequency-selective
perturbation depths remain 5% each. The sampled shared channel retains full
tails, continuous state and echoes through transmit guards.

Default audio error means a causal 0–70 ms lag per station. This differs from
the built-in GUI examples' explicit `0ms` command argument. With Default
clock synchronization and Fake FCC, each station's assumed GPS calendar
error is ±10 ms. The transmit quiet interval is 55 ms per dwell edge; the
legacy ramp reference is `2 / 3600 = 0.555556 ms`. The new format additionally
keeps payload pulse support and its configured margin away from the ramp.
For a dated capture with 35 ms uncertainty, the public start interval is
−55 to +125 ms relative to the nominal epoch. Without FHSS, the study retains
the broad default start interval (six-second search plus one second and
35 ms capture uncertainty) and thirteen integer epoch alternatives. The
capture lead is public; simulated timing truth does not recenter the search.
The second example retains sixteen seconds after the actual source endpoint,
covering the full 14.07-second public arrival interval even for its earliest
possible source start. Every declared arrival alternative therefore has a
complete observation window.

Fake FCC at this frequency supplies a keyed 200-channel schedule, with
4700 Hz nominal spacing and 400-ms dwells. It does **not** physically retune
the RF channel. V2's source contains the corresponding transmit envelope;
the propagation model's physical retune switch stays off for these examples.
Real retuning, independent hop phases and wider microwave oscillator capture
are different experiments and must be labeled separately.

## Reception and validation

The v2 receiver retains channelized, matched-filtered complex samples, then
compares private templates over the public timing, clock, frequency and epoch
banks. Channelization retains four samples per fine symbol for all-time FFT
correlations. Fine frequency compensation happens on that retained tensor;
it does not rebuild an exact raw-waveform matched filter for every fine CFO.
“Exhaustive” describes the declared finite grids on this representation,
not a continuous search or exact full-waveform likelihood. Differential and
amplitude statistics are invariant to the subsequent common fine-CFO phase
rotation, so they reuse that work. Both logical and uniquely scored hypothesis
counts are reported. The candidates include coherent segment likelihoods, adjacent-carrier
differential products and amplitude-only patterns. Coherence segments may
cross dwell boundaries when there is no fresh unknown physical hop phase.
The study reports the actual search work, arithmetic, threads and workspace.
The 256 MiB default is a ceiling on receiver preparation and DSP arrays,
including candidate layouts and epoch templates. It excludes caller-owned
captures, transmission/simulation storage, thread stacks and general runtime
overhead. Allocation-free geometry planning precedes that admission check.
The measured cases do not need 75% of host memory. `--memory-mib` and
`--max-hypotheses` expose explicit limits; exceeding a limit fails rather than
silently dropping candidates. The study's default hypothesis ceiling is eight
million, sufficient for its full-pattern HF coherence comparisons.
Its empirical admission gates are not calibrated false-alarm probabilities.
Mode-aware arithmetic bounds include the fourth-order energy terms used by
pair and amplitude scoring. Automatic falls back to FP64 when necessary;
forced FP32 rejects unsafe ranges. Extreme values beyond the FP64 range are
rejected explicitly. Numeric gain checks include clean inputs scaled by
10¹⁰ and 10⁻¹²; absolute amplitude floors must not become apparent sensitivity
limits.

The backend is CPU-only, with parallel channelization, batched FFT correlations
and contiguous score tensors. It has no BLAS/GPU execution claim. The current
host exposed no OpenCL platform through `clinfo -l` and no GPU device nodes.
`--threads` permits an explicit worker count; the reported count is the actual
maximum used by a stage, not a claim that every stage keeps that many workers
busy.

Build and run only the isolated experiment:

```sh
CCACHE_DISABLE=1 ./build.sh test triad --jobs 2
build/dev/triad_examples --case all --trials 1 --threads 8 > local-evidence/example-results.jsonl
build/dev/triad_examples --case 2 --trials 8 --threads 8 --cn0 8
build/dev/triad_examples --case 2 --trials 8 --threads 8 --control null
python3 tools/triad-examples-summary.py local-evidence/example-results.jsonl
```

The explicit `--diagnostic-clean` mode uses known alignment without channel
impairments. It checks implementation consistency, not default-clock
acquisition. Normal examples retain their public search bounds. Decoder
failures and false accepts are results to report, not reasons to suppress
trials. No full ordinary-receiver regression campaign is implied by the
isolated `triad` group.

Optional case-2 `--rf-hz 5800000000` tests the same disciplined-oscillator
configuration at 5.8 GHz and widens the RF-derived search bound accordingly.
Keeping the HF spread selection there is a deliberately severe synthetic
stress, not a calibrated microwave propagation profile. `--channel-none`
provides the explicit no-spread/no-selective-perturbation baseline while
retaining noise, clock errors and the full acquisition search. These cases do
not establish v2 acquisition with an undisciplined microwave RF oscillator.
