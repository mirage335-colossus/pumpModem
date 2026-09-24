# Simulation oscillator profiles

The simulation oscillator selector separates link power from clock quality.
It includes hobbyist GPS disciplined oscillators (GPSDOs) without an oven,
temperature-compensated crystal oscillators (TCXOs) without an oven, and
oven-controlled crystal oscillators (OCXOs). Selecting a GPS profile changes
the simulated clock and phase impairments; it does not connect to GPS hardware
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

The default remains `crystal`, preserving the previous simulation assumptions.
The common GPS residual, 0.0001 ppm (a fractional offset of 10^-10), is an
explicit link-planning assumption, not a measured or universal locked-device
specification. It is not derived from an Allan-deviation point or an unlocked
temperature-stability rating. Changing GPS oscillator class therefore changes
phase sensitivity without changing clock-search coverage for the same waveform.
The hobbyist profile improves average frequency offset without assuming an
improvement in phase diffusion. Across the GPS profiles, the phase-diffusion
amplitude differs by 100 times, so its variance differs by 10,000 times. This is
a selected model range, not a universal ranking of hardware.

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
Positive offset raises the received carrier and shortens received symbols. At
a 1,500 Hz carrier, the shared 0.0001 ppm GPS assumption adds 0.00000015 Hz before any separately configured
frequency offset. This parameter is not an Allan deviation, a drift rate or a
manufacturer's long-term accuracy specification.

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
GPS scenarios assume both ends are locked and the relevant references drive
both the simulated carrier and sampling clocks. Disciplining an RF local
oscillator alone does not discipline a separate audio DAC/ADC clock. This
assumption also does not eliminate unknown starting phase, propagation delay,
or the receiver's existing timing and frequency search.

## Limits for long observations

The present channel retains its constant clock offset plus Wiener phase
diffusion. It does not add a model of the GPS servo, phase corrections,
temperature cycles, oscillator aging, warm-up, holdover after loss of GPS,
antenna multipath, clock quantization, spurs or cycle slips. The GPS profiles
assume established lock; they do not model startup or loss of lock.

In particular, an unbounded Wiener process does not reproduce how a locked
GPSDO may constrain phase over very long intervals. These profiles support
sensitivity comparisons within the existing channel, not faithful day- or
year-long device trajectories. A low residual offset also does not expand the
receiver's finite search or remove its memory limits. The estimator must still
report those limits separately.

Use the profiles with [fast link analysis](weak-link-planning.md), for example:

```sh
./build/pump analyze-link --text a --bw 100 --symbol-seconds 2000000 \
  --simulation '3dBm -200dB' --oscillator gpsdo-tcxo \
  --coherent-seconds 20000 --trials 10000
```

The CLI also accepts individual `--clock-error-ppm` and `--phase-noise`
overrides after applying the selected profile. Use these to examine measured
or alternative residual assumptions. In `analyze-link`, the experimental
detectors remain conditional on matched timing and clock acquisition;
`--residual-frequency-hz` describes the separate carrier error left after that
hypothetical acquisition. A favorable reference-detector result does not mean
the production receiver has acquired or decoded the signal.

Oscillator selection does not change wire bits, framing, receiver admission,
pending-bit presentation, or the fully observed absence required for physical
completion. CPU/GPU estimates keep the fixed i9-13900H and RTX 4090 Laptop
reference assumptions; they do not benchmark the selected hardware.
