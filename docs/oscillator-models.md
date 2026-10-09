# Oscillator references and receiver search

The **Baseband Osc** and **Shift Osc** selectors separate link power from clock quality.
They include hobbyist GPS disciplined oscillators (GPSDOs) without an oven,
temperature-compensated crystal oscillators (TCXOs) without an oven, and
oven-controlled crystal oscillators (OCXOs). The declared models determine the
receiver's static frequency and sample-clock search bounds as well as the
simulation impairments. Selecting a GPS profile does not connect to GPS hardware
or synchronize the computer.

These are **illustrative sensitivity profiles**, not measurements, typical
product specifications, or promises of reception. The numbers describe the
effective relative transmitter/receiver impairment at the modem. They are
chosen separately: the GPS profiles share one conservative locked-link frequency
residual, while phase diffusion is spread out to examine coherence sensitivity.

| CLI profile | Oscillator scenario | Relative clock offset | Phase diffusion |
| --- | --- | ---: | ---: |
| `crystal` | Existing consumer/free-running crystal case | 100 ppm | 0.5 degrees/sqrt(second) |
| `gpsdo-xo` | Hobbyist GPSDO, basic crystal, no oven | 0.0001 ppm | 0.5 degrees/sqrt(second) |
| `gpsdo-tcxo` | GPSDO with temperature compensation, no oven | 0.0001 ppm | 0.05 degrees/sqrt(second) |
| `gpsdo-ocxo` | GPSDO with an oven-controlled crystal | 0.0001 ppm | 0.005 degrees/sqrt(second) |
| `ic-7100` | IC-7100 RF reference example (Shift Osc only) | 1 ppm | 0.05 degrees/sqrt(second), assumed |

The default for each model remains `crystal`.
The common GPS residual, 0.0001 ppm (a fractional offset of 10^-10), is an
explicit link-planning assumption, not a measured or universal locked-device
specification. It is not derived from an Allan-deviation point or an unlocked
temperature-stability rating. Changing among the GPS oscillator classes therefore
changes phase sensitivity without changing static search coverage for the same
waveform and reference topology.
The hobbyist profile improves average frequency offset without assuming an
improvement in phase diffusion. Across the GPS profiles, the phase-diffusion
amplitude differs by 100 times, so its variance differs by 10,000 times. This is
a selected model range, not a universal ranking of hardware.

### IC-7100 assumptions

The IC-7100 option uses Icom's published ±0.5 ppm per-radio RF stability
(0–50°C at 430 MHz), conservatively combined as 1 ppm between two radios before
the selected search margin. Its phase diffusion is an engineering assumption,
not a published Icom measurement. The official first IF is 124.487 MHz for
SSB/CW/AM/FM/RTTY/DV; a nominal 170 MHz conversion frequency is not used to
invent an additional independent error. The RF stability specification describes
the radio's resulting RF frequency. [Icom specifications](https://www.icomjapan.com/lineup/products/IC-7100USA/)

Icom's July 2013 service manual (S-15008XZ-C1, printed3-9) identifies the
41.344 MHz X2001 reference as a **TCXO**, not an oven-controlled oscillator.
The parts and circuit sections separately identify a PCM2901 USB codec
(IC602) with a 12 MHz crystal (X601), and an internal DSP codec with a
24.576 MHz crystal (X5051). These do not justify treating USB sample timing
as shared with the RF reference or permanently GPS disciplined.
The manual specifies a five-minute warm-up for RF stability; it does not
establish a permanently powered oven or an audio timing guarantee.
[Manufacturer service manual, public mirror](https://ok2haz.ok2kld.cz/ok2haz/wp-content/uploads/2017/01/IC-7100_service_manual.pdf)

TI describes the PCM2901 as adaptive for USB playback and asynchronous for
recording, using a 12 MHz source and packet-derived audio timing. Its permitted
crystal tolerance is not a measurement of the IC-7100's effective sample rate.
Use an independent Baseband model for the USB path unless separate evidence
supports a different model. [TI PCM2901 datasheet](https://www.ti.com/lit/ds/symlink/pcm2901.pdf)

No loopback measurement is required to use these explicitly modeled assumptions.
They do not establish USB/driver/DSP latency or a millisecond end-to-end arrival
bound; those remain distinct from RF stability and PC synchronization. See
[clock synchronization](clock-sync.md) for the current timing fallback and
qualification limits.

## Real streams and clock references

All external input and output remains an ordinary **real ADC/DAC sample stream**.
The public **Carrier** is the absolute transmitted/received frequency; **Shift**
is the nonnegative RF translation or LO frequency, default **0 Hz**. Fixed upper
sideband (USB) gives the nonzero stream tone **Carrier − Shift**. Carrier must
exceed Shift. Both fields accept Hz, kHz, MHz, GHz or THz suffixes, ignoring
case and optional spaces; scientific notation and plain values in Hz also work.

| Carrier | Shift | Real stream tone |
| --- | --- | --- |
| 1.0015 MHz | 1 MHz | 1.5 kHz |
| 1.0015 MHz | 0 Hz | 1.0015 MHz |

The direct MHz case requires a real stream and sample rate that support that
MHz tone. A radio can instead supply a real 0–20 kHz or 0–1 MHz passband by its
own conversion and filtering. Direct conversion and synchronous superheterodyne
hardware need no separate decoder mode when the delivered passband and effective
reference behavior are the same. No external complex I/Q format is introduced.

**Baseband Osc** always models ADC/DAC timing and the real stream's carrier.
For a positive Shift, an independently selected **Shift Osc** profile adds the
converter's frequency and phase error. At Shift **0 Hz**, Shift Osc displays
**N/A** and is disabled. All Shift-model frequency error, phase diffusion and
linked drift are excluded, including when a legacy shared-reference setting was
loaded. Only the Baseband model remains active. The GUI remembers the chosen
Shift Osc option and restores it when Shift becomes positive.

Choose **Baseband clock** in Shift Osc when the ADC/DAC and synchronous mixers
really share one reference within each radio. This uses the selected Baseband
profile once, with linked frequency and sample-rate hypotheses. It does not
add a second independent oscillator or a second phase-diffusion contribution.
This reference topology is separate from the UTC clock controls. Shared hardware within each radio does
not imply identical clocks at the two ends of the link, or eliminate additional
synthesizer phase noise. The Baseband profile must describe the effective
relative behavior of the whole shared reference chain.

With Carrier 10.0015 MHz and Shift 10 MHz, the delivered tone is 1.5 kHz.
Changing Shift while holding Carrier fixed changes the stream tone and can
change its required sample rate. Once that tone is determined, RF translation
metadata adds no RF-rate samples or cipher work. Carrier and Shift describe
frequencies; neither control retunes physical hardware.

GPS/PPS may set computer time or calibrate a sound-card rate without actually
driving its sample clock. Those cases retain the relevant measured Baseband
uncertainty. Hardware clock discipline and software timing calibration are
different assumptions; choosing a profile provides neither service.

These controls describe reference behavior; they do not tune a radio or add a
USB radio driver. The current live frontend remains the supported real audio
device path, with its existing passband constraints. Wider simulated or
low-level real streams can use the same reference policy.

### CLI and saved settings

| Option | Meaning and default |
| --- | --- |
| `--oscillator MODEL` | Baseband profile; `crystal` |
| `--rf-oscillator MODEL` | Shift profile for an independent positive Shift; legacy shared-reference profile; `crystal` |
| `--carrier FREQUENCY` | Absolute carrier; the real stream tone is Carrier minus Shift |
| `--shift FREQUENCY` | LO/translation frequency; `0` |
| `--rf-shift FREQUENCY` | Compatibility alias for `--shift` |
| `--rf-carrier FREQUENCY` | Compatibility alias for absolute `--carrier` |
| `--search-margin N` | Effective-link accuracy multiplier; `3` |
| `--reference independent\|shared-radio` | Legacy reference spelling; `independent`; positive shared Shift uses the `--rf-oscillator` profile once for sampling and conversion |
| `--lf-reference 0` | Legacy shorthand for `shared-radio`; unrelated to setting Shift to zero |
| `--sideband upper` | Accepted compatibility no-op; lower sideband is rejected |

For an absolute 10.0015 MHz carrier, 10 MHz translation and a GPSDO/OCXO radio
reference:

```sh
./build/pump analyze-link --text a --bw 100 --symbol-seconds 128 \
  --simulation '3dBm -120dB' --carrier '10.0015 MHz' --shift '10 MHz' \
  --oscillator gpsdo-ocxo --rf-oscillator gpsdo-ocxo \
  --reference shared-radio --search-margin 3
```

The Robust Modem main controls keep **Baseband Osc**, **Shift Osc** and
**Margin** together, with **Carrier** and **Shift** adjacent in the modem row.
The editable **Clock accuracy**, **Clock region** and **Clock offset** fields
are independent UTC-arrival controls; see [clock synchronization](clock-sync.md).
They remain available with Developer mode off and with Simulation on or off.
Numerical clock/phase assumptions and requested/covered search bounds appear
under the Link planner's hideable **Model limits and references**. There is no
Sideband selector; the application uses USB.
Very small nonzero search widths retain sufficient precision or scientific
notation rather than being rounded to zero.
Fast and Legacy Modem retain their separate controls. Shift presets include
0 Hz and 1, 3.5, 7, 10, 14 and 30 MHz, with manual entry; Margin offers
1×, 2× and 3× or a custom finite value of at least 1×.

Launch import/export preserves the absolute Carrier and Shift and exports
canonical `--carrier` / `--shift` options. Frequency suffixes are case-insensitive;
the CLI also accepts `k`, `m`, `g` and `t` shorthand. Matching `--carrier` and
`--rf-carrier` aliases may be combined; conflicting values are rejected.
Choose either `--shift` or `--rf-shift`, not both. With translation enabled,
set the absolute carrier above Shift to retain the intended stream tone.
Without an explicit Carrier, the CLI retains its bandwidth-derived absolute
Carrier default; Shift must still leave a positive stream. GUI launch imports
retain omitted destination settings. Loading a legacy positive-Shift shared
reference moves its effective `--rf-oscillator` profile into Baseband Osc and
selects Shift Osc **Baseband clock**. Shared exports include that same profile
in both oscillator flags, preserving CLI behavior. At Shift zero, import keeps
the Baseband profile and excludes the inactive Shift profile; export omits
`--rf-oscillator` and uses `--reference independent`.

## Oscillator-derived static search

The default search margin is **3×**, applied once to the effective relative
accuracy bounds. It expands the acquisition allowance without increasing the
simulated impairment. If `f_m` is the stream tone (Carrier minus Shift),
`f_LO` is Shift, `f_on_air` is the absolute Carrier,
`a_B` and `a_S` the Baseband and Shift accuracy bounds in ppm, and `M` the margin, the requested
half-widths are:

| Reference | Frequency half-width | Sample-clock half-width |
| --- | --- | --- |
| Shift zero | `M * f_m * a_B * 1e-6` Hz | `M * a_B` ppm |
| Independent positive Shift | `M * (f_m * a_B + f_LO * a_S) * 1e-6` Hz | `M * a_B` ppm |
| Positive Shift using Baseband clock | `M * f_on_air * a_B * 1e-6` Hz | `M * a_B` ppm |

Shared-reference errors produce linked frequency and symbol-rate changes. The
bank searches paired hypotheses on that relationship, using the absolute
carrier. The application fixes USB;
the low-level oscillator-policy API retains its explicit sideband orientation.
Independent Baseband and Shift references retain independent timing uncertainty,
with impossible frequency/rate combinations omitted. Thus changing a field's
representation cannot introduce duplicate reference errors or a larger bank.

Frequency spacing is at most `0.25/T` for the actual sample-quantized symbol
duration `T`, refined to reach the exact declared bound. Tiny GPSDO regions
therefore do not acquire a much larger implied clock error by rounding to a
full phase bin. Independent rate spacing limits the accumulated mismatch to a
quarter chip per symbol. The bank includes the declared endpoints when resources permit;
passband headroom and finite frequency/rate/lane caps can reduce it. Requested
and covered bounds are reported separately, with incomplete coverage explicit.
See [search computation](search-compute.md#oscillator-policy-and-bounded-banks).

Phase diffusion is a separate coherence input. An unknown constant phase is
fitted by the existing complex gain inside the decoder; it does not require
an external I/Q interface or another phase-search grid. A static frequency
bank cannot correct arbitrary phase wander, Doppler trajectories or GPS servo
corrections. This policy adds no Doppler tracking or drift-following loop.

## What GPS disciplining improves

A GPSDO uses a local oscillator between corrections from its GPS reference.
Below the correction interval, the local oscillator sets short-term stability.
The GPS reference and the control loop determine behavior over longer periods;
accuracy over a long average does not imply constant phase within that average.
[NIST's GPSDO uncertainty paper](https://tf.nist.gov/general/pdf/2871.pdf)
discusses this distinction and measurements at different averaging intervals.

Low-cost equipment can have good long-term frequency accuracy while retaining
short-term phase noise. For example, the manufacturer describes the
[Leo Bodnar Mini GPS reference](https://www.leobodnar.com/shop/index.php?cPath=107&main_page=product_info&products_id=301)
as using a TCXO for short-term signal quality, with GPS determining long-term
accuracy. It specifies typical phase noise at multiple offsets, rather than a
single phase-stability number. This product is an example of a non-oven design,
not the calibration source for the `gpsdo-tcxo` profile.

Locked performance and unlocked temperature drift must be kept distinct.
[Ettus's GPSDO specifications](https://kb.ettus.com/GPSDO) explicitly mark the
TCXO and OCXO temperature-stability figures as applying when unlocked, while
listing GPS-locked timing separately. Those unlocked figures do not justify
assigning a larger constant locked-link offset simply because an oscillator
has no oven.

A raw GPS receiver timepulse is another distinct case. Its frequency may
average accurately while clock quantization introduces jitter. The
[u-blox timing application note](https://content.u-blox.com/sites/default/files/products/documents/Timing_AppNote_%28GPS.G6-X-11007%29.pdf)
describes that limitation and an external PLL with a local oscillator to improve
phase noise. The present profiles do not simulate raw timepulse quantization or
specific digital synthesis methods.

An oven does not guarantee lower phase noise at every frequency offset.
The comparable TCXO and OCXO specifications in the
[Jackson Labs LC_XO data sheet](https://www.jackson-labs.com/assets/uploads/main/LC_XO_OCXO_specsheet.pdf)
show the OCXO doing better close to the carrier, equal performance at another
offset, and the TCXO doing better farther out. Actual behavior depends on the
oscillator, synthesizer, loop design, supply, environment and measurement
interval. Hardware should therefore be represented by measured residual
behavior when those measurements are available.

## Meaning of the simulation numbers

`clock_error_ppm` is a **constant relative frequency and sample-rate offset**.
Its sample-clock contribution raises the received tone and shortens received
symbols for a positive offset. With Shift = 0, a 1,500 Hz tone and the shared
0.0001 ppm GPS assumption, that contribution is 0.00000015 Hz before any
separately configured frequency offset. A positive Shift using Baseband clock
adds a linked contribution based on Shift. Shift zero excludes that contribution
and every Shift-model phase term. Lower-sideband reversal remains a low-level
policy capability; the application uses USB. This parameter is not an Allan
deviation, a drift rate or a manufacturer's long-term accuracy specification.

`phase_noise_degrees_per_sqrt_second` controls a Wiener phase process. With
diffusion amplitude `sigma`, the RMS phase change over duration `T` is
`sigma * sqrt(T)`. Thus the three GPS profiles produce 30, 3 and 0.3 degrees
RMS phase change over one hour, respectively, before deterministic frequency
offset. Phase change here means the unwrapped random process; it is not a
bounded phase error relative to GPS time.

These values cannot be equated directly to phase noise in dBc/Hz at a 10 MHz
reference output. Such a conversion requires the full noise spectrum, noise
types, transfer through multiplication/division and the synthesizer, the
measurement bandwidth, and the modulation carrier being modeled. Nor can one
Allan-deviation point determine the Wiener diffusion parameter: different
noise processes can agree at one averaging interval and diverge elsewhere.

The profiles already describe the **relative link impairment**. They are not
per-device values to which another factor of sqrt(2) should be applied. The
GPS scenarios assume both ends are locked. The selected topology specifies
which references drive the carrier and sampling clocks. Disciplining a mixer local
oscillator alone does not discipline a separate audio DAC/ADC clock. This
assumption also does not eliminate unknown starting phase, propagation delay,
or the receiver's bounded timing and frequency search. Independent phase
diffusion amplitudes combine in quadrature only for an active independent Shift;
the shared Baseband reference is
counted once. These effective phase inputs must already include the relevant
frequency synthesis behavior; the software does not infer phase noise by
scaling an RF output specification to the audio tone.

## Limits for long observations

The present channel retains its constant clock offset plus Wiener phase
diffusion. It does not add a model of the GPS servo, phase corrections,
temperature cycles, oscillator aging, warm-up, holdover after loss of GPS,
antenna multipath, clock quantization, spurs or cycle slips. The GPS profiles
assume established lock; they do not model startup or loss of lock.

In particular, an unbounded Wiener process does not reproduce how a locked
GPSDO may constrain phase over very long intervals. These profiles support
sensitivity comparisons within the existing channel, not faithful day- or
year-long device trajectories. A lower declared accuracy narrows the requested
static search, but does not remove the receiver's finite memory limits. The
estimator must still report those limits separately.

Use the profiles with [fast link analysis](weak-link-planning.md), for example:

```sh
./build/pump analyze-link --text a --bw 100 --symbol-seconds 2000000 \
  --simulation '3dBm -200dB' --oscillator gpsdo-tcxo \
  --coherent-seconds 20000 --trials 10000
```

The CLI also accepts individual `--clock-error-ppm` and `--phase-noise`
simulation overrides after deriving the selected reference's impairments.
These describe channel truth, not a larger acquisition allowance: receiver
bounds continue to come from the declared profiles and margin. Signed clock
overrides with a positive shared Shift change both sampling error and the linked
conversion offset; independent mode changes Baseband timing while retaining an
active Shift model's conversion error. Shift zero retains only Baseband timing.
A phase override sets the final combined diffusion.
Use these controls to examine residuals inside and outside declared coverage.
In `analyze-link`, the experimental
detectors remain conditional on matched timing and clock acquisition;
`--residual-frequency-hz` describes the separate carrier error left after that
hypothetical acquisition. A favorable reference-detector result does not mean
the production receiver has acquired or decoded the signal.

Oscillator selection does not change wire bits, framing, receiver admission,
pending-bit presentation, or the fully observed absence required for physical
completion. CPU/GPU estimates keep the fixed i9-13900H and RTX 4090 Laptop
reference assumptions; they do not benchmark the selected hardware.
