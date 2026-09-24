# Weak signals, bandwidth and integration

A known waveform can be detected even when individual chips are buried in
noise. The receiver correlates the observed waveform with its expected complex
amplitude and phase pattern, accumulating evidence over a payload symbol.
Individual chip phases need not be separately detectable. This is already the
principle of DataPump's two pattern codewords; adding another tiny-phase alphabet
would not by itself solve its acquisition limitations.

The nominal Rate parameter is a bandwidth parameter, not payload bits/s.
At 100 Hz the current waveform produces 50 chips/s. Thousands of chips can
represent one payload bit. The supported rate range now begins at 0.01 Hz;
decimal entries and 0.01/0.1 Hz presets are available in the GUI.

## Fast phase changes and narrow spectra

The transmitter controls its own waveform, not the phase of the surrounding
noise or every signal in a frequency interval. A receiver can nevertheless
confirm a faint known pattern by correlating against it.

Fast, small phase changes create small sidebands at the modulation frequencies.
For example, a small sinusoidal phase perturbation of depth beta has first
sidebands with power proportional to beta squared. Reducing the phase depth
reduces their power; it does not move them closer to the carrier. Arbitrary
100-per-second phase changes therefore cannot be squeezed into a strictly
1 Hz spectrum merely by making the changes tiny. See the phase-modulation
derivation in [Interferometer Techniques, section 3.2](https://dcc.ligo.org/public/0122/P1500233/001/Interferometer_Techniques.pdf).

At fixed received power, white-noise density and integration duration, narrowing
the waveform does not create additional collected signal energy. It changes
the displayed in-band SNR, interference exposure, timing resolution and search
cost. A wider known pattern spreads power across frequency and can reject
unrelated interference. The energy versus bandwidth distinction is developed
in [Messerschmitt, sections 3.3–3.4](https://arxiv.org/pdf/1111.0547).

## What the simulation budgets mean

The presets use a noise density of -164 dBm/Hz: -174 dBm/Hz thermal density plus
10 dB noise figure. Their actual C/N0 is independent of the TX design target.

| Preset | Actual C/N0 | SNR in 100 Hz | Ideal duration for 18 dB Es/N0 |
| --- | ---: | ---: | ---: |
| 3 dBm / -170 dB | -3 dB-Hz | -23 dB | 126 seconds/bit |
| 3 dBm / -200 dB | -33 dB-Hz | -53 dB | 1.46 days/bit |
| 3 dBm / -230 dB | -63 dB-Hz | -83 dB | 3.99 years/bit |

These are energy calculations using `Es/N0 = C/N0 + 10 log10(T)`, not empirical
detection thresholds or achievable rates. The existing 18 dB planner target
is an engineering starting point. Search trials and channel imperfections
require margin. Another 10 dB of loss costs ten times the coherent duration
for the same integrated energy.

For the -170 dB preset, a design target around the actual -3 dB-Hz requests
minutes per bit; -61 dB-Hz instead requests about 919 days per bit. The latter
does not describe the preset's actual signal strength. At 100 Hz, Auto-pattern
with target -3 selects 163.84 seconds per bit; target -6 selects 327.68 seconds.
At 1 Hz, target -6 selects 256 seconds. Exact `a` remains `011`, with three
payload symbols and no added acquisition header or parity. Completion also
requires a fully observed absent symbol at these durations. Waveform settling,
filter tails and suppression add time beyond those four symbols.

## Implemented search and its limits

Application receive paths request expanded clock search for pattern symbols
lasting at least 16 seconds. The carrier lattice retains spacing `0.25/T`
and requests approximately plus/minus 200 ppm of the selected carrier. There
are at most 4097 distinct offsets; once that cap is reached, the covered span
shrinks instead of leaving coarse gaps between long-integration hypotheses.
The model reports the requested expanded span and withholds a percentage when
the simulated carrier lies outside it. If memory forces a narrower runtime
fallback, the model still describes the requested search and gives no fallback
probability. Short profiles and tones retain their local bank of up to five
offsets. Implicit pattern offsets outside the waveform's valid passband
headroom are omitted even for short symbols; the center is always retained.
Low-level callers can also retain that small local bank.

Each expanded pattern offset has a nominal-clock hypothesis and a hypothesis
that scales pattern time with the carrier. Thus an independent frequency shift
and a shared sample-clock error can be tested separately. Receiver templates
and predicted next-symbol positions use that rate; carrier correction alone
does not prevent a long pattern from drifting across chip boundaries. Front-end
projection bins are kept short enough to retain the searched offsets.

At a matching start time, these clock/carrier hypotheses compete before
admission. The sampled regressions verify rejection of previously admitted
bit-selection harmonics that appeared as a second message at another carrier.
Simultaneous signals with the same pattern and overlapping timing compete for
this receiver.

When retaining every transformed template would exceed RAM, the FFT search
generates rows in bounded scratch space. Expanded live banks sharing memory
also stream those rows so early keys and epochs cannot consume the budget with
caches. The coupled search requires that its core FFT state fit: independent
per-lane admission in the long-symbol correlator would let a faster wrong
hypothesis publish before a slower competing fit finishes.

When that core cannot fit, application callers can try the original local,
nominal-clock search and its existing long-symbol correlator, which must itself
fit the available workspace. Live status reports this narrower search. This preserves bounded
long-symbol operation; it does not provide the expanded clock coverage. The
simulation model withholds a receive percentage when the requested wide search
exceeds its modeled allowance. Low-level automatic expanded requests reject
insufficient workspace unless the caller opts into the local fallback.
Explicitly supplied frequency banks remain strict and never select that fallback.

The FFT scorer observes the longest candidate duration before deciding, excludes
already-used observations from subsequent fits and counts only fully scored
failed-symbol durations toward the six-second absence policy. Additional search
alternatives increase the evidence penalty. Weak candidates cannot move across
the whole carrier bank and borrow an established signal to publish a duplicate
suffix. Accepted bits still drain at the next progress poll.

Frequency uncertainty, independently drifting sample clocks, time-varying
Doppler, oscillator phase diffusion and interfering signals remain distinct
problems. This is a bounded search over constant clock/carrier hypotheses,
not an unrestricted drift tracker. Arbitrarily low C/N0 cannot be made useful
by selecting arbitrarily long symbols. In particular, years of coherent
integration are not established by a test of numerical long-symbol coordinates.

The implemented receiver requires phase predictability over its coherent
integration windows. [ESA's baseband processing description](https://gssc.esa.int/navipedia/index.php/Baseband_Processing)
explains the integration, clock/Doppler and noncoherent accumulation tradeoffs.

Low transmitted spectral density helps reduce interference. Reception below
this receiver's noise floor does not establish the same condition at another
receiver with a different path, bandwidth, integration time or known template.
Public patterns are reproducible; using them does not establish a low
probability of interception. Neither the simulation nor its estimated receive
percentage is a measurement of interference or interception probability.

For lower-power cases such as 3 dBm/-200 dB, the bounded `analyze-link`
command compares coherent and segmented statistical reference detectors
without generating long PCM recordings. See [fast planning](weak-link-planning.md).
Sampled cases and actual checks are recorded in [validation](validation.md).
