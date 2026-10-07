# Oscillator references and receiver search

The LF/audio and RF oscillator selectors separate link power from clock quality.
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

## Real streams and clock references

All external input and output remains an ordinary **real ADC/DAC sample stream**.
The modem carrier is the nonzero tone within that stream, such as 1.5 kHz.
An RF LO/translation frequency is separate metadata; it never replaces the
modem tone or controls the DSP sample-rate planner. A radio can supply a real
0–20 kHz or 0–1 MHz passband by its own conversion and filtering. Direct
conversion and synchronous superheterodyne hardware need no separate decoder
mode when the delivered passband and effective reference behavior are the same.

The **independent audio** reference uses the LF/audio model for ADC/DAC timing
and the modem tone. When RF translation is active, the RF model adds the
converter's independent frequency and phase error. RF shift **0 Hz**, the
default, makes that extra conversion contribution inactive.

The **shared radio** reference uses the RF model once for the radio's ADC/DAC
and synchronous mixer references. The LF=0 indication means there is no
additional independent PC sound-card oscillator; it does not put the modem
tone at DC or remove sampling uncertainty. A shared reference within each radio
does not imply identical clocks at the two ends of the link, or eliminate
additional synthesizer phase noise. The RF model must describe the effective
relative behavior of the whole radio reference chain.

RF shift denotes the LO/tuning frequency. With a 10 MHz LO and a 1.5 kHz
modem tone, the physical carrier is 10.0015 MHz for the upper sideband, or
9.9985 MHz for the lower sideband. Entering the equivalent physical RF carrier
instead of its LO decomposes to the same policy and search. The frequency
orientation is required because lower-sideband conversion reverses the
RF offset's sign in the delivered real stream.

GPS/PPS may set computer time or calibrate a sound-card rate without actually
driving its sample clock. Those cases retain the relevant measured LF/audio
uncertainty. Hardware clock discipline and software timing calibration are
different assumptions; choosing a profile provides neither service.

These controls describe reference behavior; they do not tune a radio or add a
USB radio driver. The current live frontend remains the supported real audio
device path, with its existing passband constraints. Wider simulated or
low-level real streams can use the same reference policy.

### CLI and saved settings

| Option | Meaning and default |
| --- | --- |
| `--oscillator MODEL` | LF/audio profile; `crystal` |
| `--rf-oscillator MODEL` | RF/shared-radio profile; `crystal` |
| `--rf-shift HZ` | RF LO/translation frequency; `0` |
| `--rf-carrier HZ` | Alternative physical RF carrier, normalized to an LO using the modem tone and sideband |
| `--search-margin N` | Effective-link accuracy multiplier; `3` |
| `--reference independent\|shared-radio` | Reference topology; `independent` |
| `--lf-reference 0` | Shorthand for `shared-radio`, without changing the actual modem tone |
| `--sideband upper\|lower` | Frequency orientation; `upper` |

For a 10 MHz LO and a GPSDO/OCXO radio reference:

```sh
./build/pump analyze-link --text a --bw 100 --symbol-seconds 128 \
  --simulation '3dBm -120dB' --rf-shift 10000000 \
  --rf-oscillator gpsdo-ocxo --reference shared-radio --search-margin 3
```

The GUI keeps the RF shift, margin, reference and sideband controls in the
scrollable Link planner. Shift choices include 0 Hz, 1, 3.5, 7, 10, 14 and
30 MHz, with custom values. Margin choices include 1×, 2× and 3×, with custom
values. Launch import/export preserves these settings and exports the
normalized LO representation. Both RF entry forms describe the same search;
they cannot be supplied together.

## Oscillator-derived static search

The default search margin is **3×**, applied once to the effective relative
accuracy bounds. It expands the acquisition allowance without increasing the
simulated impairment. If `f_m` is the modem tone, `f_LO` the declared RF shift,
`a_LF` and `a_RF` the accuracy bounds in ppm, and `M` the margin, the requested
half-widths are:

| Reference | Frequency half-width | Sample-clock half-width |
| --- | --- | --- |
| Independent audio | `M * (f_m * a_LF + f_LO * a_RF) * 1e-6` Hz | `M * a_LF` ppm |
| Shared radio | `M * f_on_air * a_RF * 1e-6` Hz | `M * a_RF` ppm |

Shared-reference errors produce linked frequency and symbol-rate changes. The
bank searches paired hypotheses on that relationship, using the physical RF
carrier and sideband orientation rather than dividing by an LF=0 indicator.
Independent audio and RF references retain independent timing uncertainty,
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
symbols for a positive offset. With RF shift = 0, a 1,500 Hz tone and the shared
0.0001 ppm GPS assumption, that contribution is 0.00000015 Hz before any
separately configured frequency offset. Shared-radio RF translation adds a
linked, sideband-dependent contribution; lower-sideband conversion can make
the total PCM frequency offset negative. This parameter is not an Allan
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
which references drive the carrier and sampling clocks. Disciplining an RF local
oscillator alone does not discipline a separate audio DAC/ADC clock. This
assumption also does not eliminate unknown starting phase, propagation delay,
or the receiver's bounded timing and frequency search. Independent phase
diffusion amplitudes combine in quadrature; the shared radio reference is
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
overrides in shared-radio mode change both sampling error and the linked RF
offset; independent mode changes LF timing while retaining the RF model's
conversion error. A phase override sets the final combined diffusion.
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
