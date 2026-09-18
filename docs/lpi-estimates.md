# Low-probability-of-intercept relative observation estimate

The LPI estimate compares the on-air observation an **unkeyed energy detector**
would need with the intended receiver's **one-symbol design reference**. A
result of `N:1` means N total wire-bit durations for the observer versus one
for the receiver, or `N - 1` additional durations. The observer's criterion is
modeled 90% detection with a 1% false-alarm probability for one known window.
Both listeners have the same signal power and noise density, normalized so one
receiver symbol has **18 dB Es/N0**, the existing pattern-planning reference.
This is an uncalibrated design reference, not a measured reception threshold,
a safe message length or a measurement of an adversary's capabilities.

The comparison is strictly relative. Simulation on/off, channel power, noise
figure, link attenuation and oscillator presets do not enter it. The reference
does not mean that only one of N transmitted bits would be accepted: it compares
observation durations, not a 1-in-N reception success rate. Predicting successful
admissions requires the receiver's search, coherence and evidence behavior.

Knowing a private waveform lets the intended receiver correlate against it.
An observer without that waveform can still measure the extra received energy.
Encryption does not remove that energy. Spreading can lower the signal's power
density while a receiver with the waveform collects its energy over a symbol;
the unknown-waveform observer generally needs longer integration in the weak
signal model used here. A long enough transmission can consequently be easier
to detect even though its individual chips remain weak.

## When the estimate applies

The estimate assumes encrypted private patterns. The transfer layer enables
private scrambling for every keyed non-tone transmission, including callers
whose original configuration left Scrambler off. Data masking by itself would
not make a public acquisition pattern private.

With encryption disabled, the estimate still shows the hypothetical encrypted
case for experimentation. The GUI displays **Warning: encryption off;
hypothetical only**, and CLI results mark `hypothetical_encryption: true`.
The warning remains when the weak-signal or numerical limits prevent a numeric
result. Actual public patterns and tones can be easier to detect; these figures
do not describe their detection performance.

This hypothetical calculation keeps the current sample, chip and symbol timing
and uses the same normalized one-symbol reference. For a tone, it uses the
corresponding private pattern's observation band with that timing, not the
actual tone's spectrum. It does not
select the encrypted automatic profile, supply a key, enable Scrambler or DSSS,
re-encode the draft with authentication, or change a transmission setting.
Selecting encryption later may change the automatic profile and fixed-interval
source capacity, so the result is a controlled comparison at the current
timing, not a preview of all settings that a real key selection would produce.

The ordinary encrypted automatic profile enables the private Scrambler. Its
many noise-like chips already spread one transmitted bit across a pattern.
The separate DSSS mixing switch defaults to off; enabling encryption does not
also turn on that independent switch. Either private stream changes the
unkeyed observer's knowledge, but neither adds a separate processing-gain
multiplier to this calculation. Bandwidth and actual symbol duration account
for spreading once.

Numerical detection times are restricted to a normalized observation-band SNR of
at most **-10 dB**. Stronger signals are reported as outside this weak-signal
model, without a claimed protection interval. This restriction also keeps the
time-bandwidth product large enough for the Gaussian approximation below.
An estimate exceeding the numerical range is unavailable, not infinite
protection.

The selected TX target can affect the ratio by changing the automatic symbol
geometry. Short and long draft profiles can therefore give different ratios.
At fixed waveform geometry, TX/RX target labels and simulation/live link
conditions leave the result unchanged. CLI `estimate` and `analyze-link` expose
the same model in their `lpi` result: `model` is
`relative_weak_signal_radiometer`, `cn0_basis` is
`receiver_one_symbol_reference`, and `receiver_reference_symbol_snr_db` is 18.
`reference_cn0_db_hz` is derived from that reference, not taken from a channel
preset or live measurement. `observation_ratio` and `equivalent_wire_symbols`
both give N; `additional_wire_symbols` gives `max(0, N - 1)`.

## Observation bandwidth and receiver reference

Let `F` be the internal sample rate and `R` the nominal Rate setting, in hertz.
The actual chip duration is sample-quantized:

```text
chip_samples = ceil(2 F / R)
chip_rate = F / chip_samples
T_s = symbol_sample_count / F
```

For a shaped pattern (including the private pattern assumed in a hypothetical
tone experiment), the assumed observer bandwidth is
`B = (1 + 0.25) * chip_rate`, the ideal full passband support of the
root-raised-cosine pulse. It is approximately `0.625 R`, rather than `R`.
For rectangular patterns the model uses `B = R`. These are assumed ideal
observation filters, not measured occupied bandwidths: finite pulse truncation,
limiting, sidelobes and spectral weighting can change a real detector's result.
The model treats all nominal signal power as lying within the chosen band.

`C/N0` is signal power divided by one-sided noise density, expressed in dB-Hz.
Normalize both listeners to the existing one-symbol design energy. The linear
signal-to-noise ratio in the observer's band then follows from geometry:

```text
E_ref = 10^(18 / 10)
reference_cn0_db_hz = 18 - 10 log10(T_s)
c_ref = E_ref / T_s
rho = c_ref / B = E_ref / (B T_s)
in_band_snr_db = reference_cn0_db_hz - 10 log10(B)
noise_rise_db = 10 log10(1 + rho)
```

The noise-rise value describes the average power increase in that band **at
the normalized reference**, not measured interference on the actual link.
It does not establish harmless interference to another receiver. The simulator
expresses sample SNR over its `F/2` noise bandwidth; that channel SNR and the
separate whole-draft simulation probability model are not inputs here. The LPI
noise assumptions remain the same whether simulation is enabled or disabled.

## Radiometer approximation

Assume independent Gaussian signal and noise in the chosen band, exactly known
stationary noise power, known band and known on-air observation window. For
observation duration `T`, the ideal real bandpass radiometer has approximately
`2 B T` independent real degrees of freedom. Its normalized mean-power
statistic has mean 1 and standard deviation `1/sqrt(BT)` without a signal,
and mean `1 + rho` and standard deviation `(1 + rho)/sqrt(BT)` with one.

The normal approximation therefore gives

```text
z_fa = 2.3263478740408408    # standard-normal 99th percentile
z_d  = 1.2815515655446004    # standard-normal 90th percentile
T_90 = ((z_fa + (1 + rho) * z_d) / rho)^2 / B
N = T_90 / T_s
  = (z_fa + (1 + rho) * z_d)^2 / (B T_s rho^2)
additional_symbols = max(0, N - 1)
```

This is the noise-like random-signal model. A fixed-energy deterministic signal
has a different signal-present variance and is not being substituted for it.
The actual modem uses bounded cryptographic I/Q samples and finite pulse
shaping, so neither ideal distribution is an empirical calibration of its
transmitted waveform. The Gaussian radiometer analysis and its low-SNR
approximation are developed in [Tandra's dissertation, chapter 2, equations
2.5–2.7](https://digicoll.lib.berkeley.edu/record/137682/files/EECS-2009-192.pdf).

At very low SNR the relative expression simplifies to
`N ≈ 13.0169383662 * B * T_s / E_ref^2`. Doubling the observation bandwidth
or the symbol duration approximately doubles the observation ratio. Increasing
symbol duration also lowers the normalized received power to keep the same
one-symbol energy reference. These relationships do not credit encryption with
a reduction in actual transmitted power.

## Symbols, whole bursts and examples

One wire bit occupies one modem symbol. Equivalent symbols count transmitted
bits, including marker and FEC bits on the fixed-interval paths. They do not
count source characters, source bytes or chips. Short text retains its exact
dictionary endpoint, and explicit binary input retains exactly its entered
bits. No new header, padding or symbol is transmitted for this estimate.

The symbol result is fractional because an energy detector can accumulate
evidence within a modem symbol. N is the total observer-to-receiver observation
ratio; the additional count subtracts the receiver's first duration and is
clamped at zero. Neither count promises that the preceding whole number of
symbols is undetectable. The modeled 90% detection point is also not the first
possible detection time.

The whole-burst exposure ratio is
`transmission.total_seconds / T_90`, at the same normalized reference. It
includes settling, filter tails and suppression waveforms as an
**equal-power approximation**; those waveforms
also radiate energy even though they carry no additional message bits. Filter
transients do not actually have constant power, so this ratio is approximate.
Idle time and the receiver's post-transmission absent-symbol observation are
excluded. A ratio of one means the modeled exposure duration equals `T_90`;
the ratio itself is not a detection probability or an actual link-budget
estimate. Repeated traffic can provide additional evidence when the observer
can identify and combine its on-air windows.

When encryption is off, this ratio compares the **current draft's actual
duration** with the hypothetical private-pattern detection time. It does not
include any extra intervals or different timing that enabling encryption might
require. Tone drafts keep their actual tone airtime, including their absence
of shaped pulse tails; only the detection model assumes the private waveform.

These examples use automatic shaped patterns and the normalized 18 dB
one-symbol reference at both listeners. The TX target selects symbol geometry;
reference C/N0 is then calculated from the actual sampled duration. They give
the same numbers with simulation enabled or disabled, regardless of its preset.
With no key, the same numbers carry the hypothetical-encryption warning.

| Nominal Rate | TX target, dB-Hz | Reference C/N0, dB-Hz | Assumed B | Actual seconds/symbol | Observer : receiver | Additional bit durations |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 100 Hz | -3 | -4.1442 | 62.5 Hz | 163.84 | 33.63 : 1 | 32.63 |
| 100 Hz | -6 | -7.1545 | 62.5 Hz | 327.68 | 67.11 : 1 | 66.11 |
| 1,200 Hz | -10 | -10.000001 | 750 Hz | 630.9575 | 1,547.43 : 1 | 1,546.43 |

All three use a 6,000 Hz internal clock, with 120 samples/chip at 100 Hz
and 10 samples/chip at 1,200 Hz. Symbol durations use the actual sample count,
including rounding for the third row. These calculations omit no symbol
rounding and do not assert that the intended receiver can sustain the required
coherence or acquire the waveform.

The GUI initially selects no key, so its default estimate is explicitly
hypothetical. A short profile can put the normalized in-band SNR outside the
weak-signal model, with or without a key. Changing simulated attenuation cannot
make that profile eligible: the relative reference does not use the channel's
power. A longer symbol or wider observation band can lower its normalized SNR.
Encryption alone must not create an apparent processing-gain multiplier.

## What this model cannot establish

The 1% false-alarm rate applies to one preselected observation window. Searching
many frequencies or start times and repeatedly checking growing windows changes
the total false-alarm probability. A sequential detector requires its own
threshold analysis.

A different location, antenna, noise figure, received power or observation
bandwidth breaks the equal-opportunity assumption. Spectral structure,
cyclostationary features, repeated or disclosed patterns, key compromise and
correlation methods can allow earlier detection than this particular energy
detector. Conversely, unknown or drifting noise and interference can make
energy detection substantially harder. Longer integration cannot always
overcome that uncertainty; [Tandra and Sahai describe noise uncertainty and
detector SNR walls](https://people.eecs.berkeley.edu/~sahai/Papers/DelayCoherenceDySpAN08.pdf).

Detection of a transmission, recovery of its bits, decryption of its contents
and interference to another service are different outcomes. This estimate
addresses only the first, under its stated assumptions. It changes no waveform,
receiver admission decision, physical-end condition or pending-message progress.
