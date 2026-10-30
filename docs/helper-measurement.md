# Independent Helper pilot measurement

`HelperMeasurement` is a pilot-only experimental receiver in
[`helper_measurement.hpp`](../include/datapump/helper_measurement.hpp). It is
separate from the live and ordinary Robust receivers. Its input is signed
complex baseband samples covering the complete declared high-pilot search
interval; it is not an adapter for the application's real audio input. The
caller supplies the aperture's first sample time and signed RF shift coefficient
for each known dwell. This interface does not retune hardware, establish the
FHSS calendar, decode the auxiliary channel, or apply corrections to Robust.

The receiver locates the strongest high tone in the configured search range,
then finds the low tone near its expected separation. It fits their frequencies
and phases over each supplied settled aperture and combines accepted observations
in a dated linear model. The high-pilot assumption is the quiet-spectrum
assumption in [Helper Carrier](helper-carrier.md#quiet-spectrum-assumption),
not a general interference-identification guarantee. Search bounds and the low
pilot's required region must fit the complex capture Nyquist interval. No
simulation truth is used to center the search. The receiver stores at most 256
accepted apertures and rejects overlapping or out-of-order observations.

For nominal pilot frequency `f`, signed shift coefficient `F`, and estimates at
the snapshot time, the fitted model is

```text
observed f = a * f + b
a = 1 + audio_ppm * 1e-6
b = F * shift_ppm * 1e-6
```

The two pilots permit a relative audio-frequency scale and a common
displacement to be separated. The quantities describe the link: absent external
references, individual transmitter and receiver clocks and unmodeled Doppler
cannot be separately identified. The linear model also estimates the rates of
these two quantities, but their extrapolation assumes the same evolution
continues. The returned covariance is conditional on the fitted model and
declared common Wiener phase diffusion and measurement noise; it is not an
operational confidence guarantee. `predict` retains the full frequency and
drift covariance at a requested date and separately reports the uncertainty of
an independent future aperture's common phase process. A comparison with a new
measurement must also account for that aperture's process and thermal error;
the fitted mean's uncertainty alone is not a bound on the new residual.
For a supplied common phase diffusion amplitude `q` in radians/√s, the model's
frequency variance for a rectangular aperture of duration `T` is
`3 q² / (10 π² T)`, under a small-phase, high-SNR linearization. The requested
example diffusion levels fall within this small-phase regime, but the
configuration permits larger values for which this approximation is not
validated against nonlinear phase wrapping or the acquisition threshold. The
model does not cover independent audio-clock frequency acceleration, abrupt
oscillator changes, or unspecified propagation effects.

For a signal obeying the same model, a conceptual correction is
`y(t) = x(t/a) * exp(-i*2*pi*b*t/a)`: output samples step through the input by
`1/a`, giving an output/input sample-count ratio of `a`, and the mixer frequency
in output-time coordinates is `-b/a`. This is an example correction, not an
implemented live signal path. A hop changes `F` and can introduce a new random
phase. `helper_hop_phase_change` estimates the observed phase discontinuity at
a known boundary using the two adjoining local fits; it does not predict a
future independent phase. Its accuracy is conditional on boundary timing and
the phase evolution between the observed apertures.

## Simulation and checks

[`test_helper_measurement.cpp`](../tests/test_helper_measurement.cpp) and its
independent physical-time source
[`helper_measurement_sim.hpp`](../tests/helper_measurement_sim.hpp) generate
two simultaneous tones at 550 and 2400 Hz, with the low pilot at −3 dB power.
The fixture supplies known 400 ms dwell and hop commands and samples the central
360 ms, with random phase changes on retunes. It includes separate transmit and
receive audio and shift-clock parameters, linear drift, common Brownian phase
diffusion within an aperture, Gaussian thermal noise, and configurable transmit
power and path loss. The common phase process is advanced through the discarded
boundary intervals. A synthetic 950 Hz auxiliary at −9 dB adds representative
gating, reversal and symbol transitions; it is not the actual metadata waveform
and is not decoded. The fixture does not simulate radio filters, nonlinearities,
or physical hardware. Those omissions limit any sensitivity or operational claim.

The noise density is `10 log10(k T * 1000) + NF` dBm/Hz with temperature `T`
in kelvin, Boltzmann constant `k`, and noise figure `NF` in dB. With unit high
pilot power and linear `C/N0`, each component of complex Gaussian noise has
variance `Fs_physical / (2 C/N0)`, where the physical ADC rate includes its
clock error; changing capture rate therefore does not change the modeled noise
density. The example settings use 290 K, 10 dB noise figure, and
40 dB path loss. A 24 kHz complex capture spans −12 to +12 kHz and contains
the free-running HF prior, not merely a truth-centered narrow window. The
simulation runs independent scenarios on separate threads.

The three shift profiles use the illustrative relative-link assumptions in
[oscillator models](oscillator-models.md): free-running crystal 100 ppm and
0.5 degrees/√s, IC-7100 example 1 ppm and assumed 0.05 degrees/√s, and GPSDO
OCXO 0.0001 ppm and 0.005 degrees/√s. The IC-7100 RF assumption does not
specify its audio clock. These are models, not measured devices. The example
uses an independent crystal audio prior, +37 ppm transmit and −13 ppm receive
audio clocks, 0.015 ppm/s transmit audio drift, and shift drift of 0.00005 times
the selected shift offset per second. The configured shift offsets are 80,
0.8, and 0.00008 ppm respectively. The frequency hops use ±1.2 MHz around
30 MHz, or ±2 MHz around 5.8 GHz, and intermediate commands.

Build and run the focused fixture after configuring a compatible `build/dev`:

```sh
CCACHE_DISABLE=1 cmake --build build/dev --target test_helper_measurement --parallel 2
build/dev/test_helper_measurement --feasibility-only
build/dev/test_helper_measurement --suite
```

The feasibility command performs only the very short weak-signal probe. The
suite adds noiseless controls, three 36-second FHSS scenarios with three seeds
each, one 36-second non-FHSS control, observed hop measurements, and a held-out
corrected aperture. A short-probe result does
not establish a 36-second sensitivity boundary. The suite is part of the
`contract` group; the individual executable does not replace that group or the
applicable broader validation described in [building](building.md#testing-stages).

## Example results after 36 seconds

In the focused FHSS simulation, three independent seeds per profile acquired
all 90 apertures. The high pilot was transmitted at −54 dBm, received at
−94 dBm after 40 dB path loss, and had modeled C/N0 of 69.975 dB-Hz; the
low pilot was 3 dB weaker. Effective residual-based C/(noise plus simulated
auxiliary interference) was approximately 53.62 dB-Hz for the high pilot and
50.62 dB-Hz for the low pilot. The selected frequency ranges and modeled errors
below are for these stated conditions, not general reception guarantees.
`DATA 1500` denotes the frequency predicted by the affine model at a nominal
1500 Hz tone and the center RF shift, rather than a decoded Robust signal.

| Shift oscillator example | Initial high-pilot search half-width | Conditional DATA 1500, 3σ half-width | Observed hop phase-change RMS error | Observed hop frequency-change RMSE |
| --- | ---: | ---: | ---: | ---: |
| 30 MHz free-running crystal | 9360.72 Hz | 0.003268–0.003269 Hz | 0.338–0.358° | 0.00351–0.00369 Hz |
| 30 MHz IC-7100 example | 94.32 Hz | 0.002860–0.002861 Hz | 0.070–0.094° | 0.000757–0.000909 Hz |
| 5.8 GHz GPSDO OCXO | 2.4606 Hz | 0.002861–0.002862 Hz | 0.070–0.081° | 0.000711–0.000994 Hz |

These are ranges across three seeds, and the initial widths include a 3× model
margin and the largest commanded hop shift. The conditional widths describe
the fitted instant and model; they are not demonstrated search bounds for later
reception. All ten conditional intervals across the nine FHSS runs and the
non-FHSS control covered the corresponding simulated DATA 1500 frequency, and
all ten held-out measured residuals fell within their conditional intervals.
Ten trials cannot establish calibrated coverage or a safe acquisition range.
The FHSS phase statistics are
errors for the observed adjacent hop discontinuities; they confer no knowledge
of the next random phase.

An earlier no-auxiliary run with a thermal-only covariance covered the fitted
DATA frequency in eight of nine cases; the IC-7100 seed 732 missed by
0.0003451 Hz against a 0.0003365 Hz conditional half-width. That diagnostic
exposed the need to represent common phase diffusion. It used different
simulation and estimator settings and is not part of the nine revised runs
above.

The non-FHSS control used the 30 MHz free-running profile and seed 799. All
90 apertures were accepted. The DATA 1500 estimated displacement was
+2404.4026 Hz with −0.000559294 Hz error and conditional 3σ half-width
0.00327370 Hz. The held-out correction used a −2404.2556 Hz mixer and an
output/input sample-count ratio of 1.0000499; its high-pilot residual was
−0.00275361 Hz with conditional 3σ half-width 0.0141048 Hz. The adjacent
aperture phase-change RMS error was 0.3454°, and the frequency-change RMSE
was 0.003406 Hz; these are continuous-pilot measurements, without random hops.

One reproducible example for each profile illustrates the signed correction.
The predicted DATA 1500 displacement is at the center shift. The mixer and
sample ratio below apply to the following held-out hop, 28.8 MHz for the HF
examples and 5.798 GHz for the microwave example. The phase is separately
reacquired after that hop.

| Example seed | Relative audio error | Relative shift error | DATA 1500 displacement | Held-out mixer correction | Output/input sample-count ratio | Held-out residual and conditional 3σ half-width |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Free-running, 731 | +50.027477 ppm | +80.144292 ppm | +2404.4038 Hz | −2308.0863 Hz | 1.00005 | −0.0017971 Hz; ±0.0141366 Hz |
| IC-7100, 732 | +49.948428 ppm | +0.80148543 ppm | +24.119486 Hz | −23.082105 Hz | 1.0000499 | +0.0000682 Hz; ±0.0118633 Hz |
| GPSDO, 733 | +49.74358 ppm | +0.000080449853 ppm | +0.54122452 Hz | −0.46645751 Hz | 1.0000497 | +0.0005594 Hz; ±0.0117951 Hz |

The DATA 1500 fitted frequency errors for these three seeds were +0.00068645,
+0.0004060, and +0.0005814 Hz, respectively. The held-out residuals include
model extrapolation and 32-tap windowed-sinc sample interpolation; their
conditional widths include the new aperture's process and thermal errors.

The short feasibility probe used 0.8 seconds and a high-pilot C/N0 of
29.975 dB-Hz; it acquired two of two apertures in one seed, both pilot
frequencies within 2 Hz. This small result is
neither a sensitivity limit nor evidence that the same acquisition rate persists
over 36 seconds. Ten 36-second scenarios ran with three scenario threads in
approximately 6 seconds in the reported local run; timing is specific to that
host and build.
