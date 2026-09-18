# Link planner

Set transmit power, path loss and noise in the top bar, then open **Link
planner**, immediately after **Console**. Its headline shows the preview's RX
estimate, RX reference or clock/RAM limit. Below it are the power budget's margin
or shortfall and the modeled loss from phase drift. The power field accepts watts,
milliwatts, microwatts or dBm.

The timing preview starts at −8 dB in 1 Hz, independently of the current
transmit targets. Rate, carrier, waveform, key, oscillator and DSP
allowance follow the shared controls. Choose **Use target for short messages**
or **Use target for long messages** to apply a preview.

The short and long target dropdowns automatically align entries below −20 dB-Hz
in automatic modes. An already fitting value stays exact; otherwise selection
prefers the nearest checked weaker target, with a stronger fallback when needed.
The check includes the other transmit target and all active plaintext/key banks.
It checks clock coverage and RAM, independently of the RX probability.
Preset selections display the exact fitted value immediately. While typing,
your text stays intact and the label shows the value in use; Enter displays its
full precision. The matched RX list always uses the exact effective values.
Manual RX lists and explicit planner Apply retain their exact-input behavior.

The top-bar **Simulation** choice is **Yes / No**. Editable power, path-loss and
noise dropdowns stay beside it in both modes, sharing their values with the
planner and RX success estimate. RX success remains available without sampled
simulation; CPU/GPU computation estimates appear only with **Yes**. The planner
does not repeat the input controls, and the estimate row takes no space with
**No**. Changes to the budget in live mode update estimates without restarting
reception.

The graphs use a logarithmic vertical scale: equally spaced steps represent
multiples of time. Weaker targets generally need longer bits; automatic profile
boundaries make discrete steps before integration varies continuously.

At 3.6 kHz rate and a 1.5 kHz carrier, the automatic shaped pattern gives:

| Reference | Result |
| --- | --- |
| −8 dB-Hz | 398.107 seconds per bit; 401.116 seconds sending one bit |
| +23 dB-Hz LPI example | 569 milliseconds per bit; modeled observer/receiver time about 4.33× |
| One bit per second | Automatic profile transition near +20.45 dB-Hz |
| One day per bit | Near −31.365 dB-Hz |
| Ideal shaped passband | 375–2,625 Hz, 2.25 kHz wide |

The completion estimate adds fully scored absence covering six seconds. A
minutes-long bit requires a complete additional minutes-long absent symbol.
Processing delay comes afterward. **Use current draft** preserves the existing
wire count: exact short dictionary/raw bits, or fixed 192-bit markers and
128-coded-byte intervals for longer messages and attachments.
An empty text or raw-bit draft instead shows a **1-bit preview**, using the
short target for the shared transmission/RX/LPI estimates. It stays empty and
cannot be sent. Attachments retain their existing framing, including empty files.

The illustrative free-running crystal has a 100 ppm relative clock offset and
0.5 degrees/√second phase diffusion. At the default carrier its 0.15 Hz shift
fits the current receiver search at −8. **Clock / RAM limit** finds a target
whose receiver search covers the clock offset and fits the selected DSP RAM
allowance, including the 75% option. The milestone names the limiting condition;
the selected target reports **Wide RX search exceeds RAM** when appropriate.
The clock-only long-duration edge is near −17.33 at this geometry, but available
RAM can require a stronger target. Short profiles and sampled projection sizes
can have separate coverage or memory gaps.

**Stronger** and **Weaker** select usable targets, normally about 1 dB apart,
and skip targets that fail either check. If less than 1 dB remains, the final
usable edge is still selectable. A disabled button means no usable point was
found in that direction. The target prompt remains available for manual entry.
Selections and Apply preserve the exact target even when its label is rounded.

These gaps can be extremely narrow. At 1 Hz rate, a 1.5 kHz carrier, the hobby
GPSDO model and 4 GiB DSP allowance, exact −47 dB uses a timing that exceeds RAM.
A target only about 0.000001 dB stronger can fit, because the receiver can average
many more samples before searching. The hidden model details explain this
without adding another graph or more numbers to the main view.
The bounded search checks sample-aligned timings and changes in clock-search
and FFT geometry; other usable timings may remain undiscovered.

Memory and clock coverage are separate from successful reception.
The planner preview checks one matching RX profile, rather than the currently
unapplied receive-target list. More profiles or keys can require additional
workspace; the transmit dropdowns include those banks when choosing a fit.

The planner's **RX estimate** includes the model's signal strength,
whole-bit phase loss, residual frequency and timing error, acquisition and RAM
checks. It estimates reception of every wire bit before error correction;
the top-bar estimate uses the actual configured draft, receive bank and FEC.
For eligible long patterns, **RX reference** labels the coherent-branch estimate,
including the cost of trying the additional four-section detector. The latter's
reception gain is not yet quantified; this reference is not a proven lower bound.
The phase-loss readout retains the whole-bit penalty; the estimator separately
reports section loss without substituting it into the probability. Neither
value establishes measured sensitivity.

The dense low-target timings existed before the planner found them. At 1 Hz,
the nearby 3,162,277- and 3,162,278-second symbols both fit the 4 GiB/hobby-GPSDO
check; their target levels differ by about 0.00000137 dB. They are discrete
sample timings. The latter lasts about 37 days and loses about 17.8 dB to phase
diffusion in a whole-bit coherent fit under that oscillator model.

The receiver now also fits four fixed sections when a pattern lasts at least
16 seconds and every quarter contains at least 16 complete chips. Each section
allows a separate signal amplitude and phase; their evidence is combined only
after the whole bit is observed. The section score excludes its strongest
quarter, requiring support beyond one isolated burst. The original coherent
match remains available, and the score accounts for both the extra fitting
freedom and trying two detectors.
This changes neither the waveform nor the exact wire-bit count.
Compact banks retain the coherent-only path if the added section state cannot
fit RAM. The planner describes eligible geometry; the RX reference keeps the
choice penalty even when that fallback would avoid it.

Each quarter must still retain useful coherence. Four sections do not make a
37-day bit equivalent to repeated 100-second comparisons, and there is no special
0.01 Hz transition threshold. The live constellation's relative-phase values
remain display diagnostics. More flexible differential or drift-tracking
detectors, and the arbitrary segment lengths in the
[statistical experiments](weak-link-planning.md), are separate from this fixed
four-section implementation.

**Observer / receiver time** uses the existing [relative LPI model](lpi-estimates.md).
Each graph point holds the intended receiver's bit energy relative to noise at
18 dB, so received power decreases as bits lengthen. The ratio does not depend on the
editable actual-link budget. Its assumptions remain behind the single concise
LPI warning in the tab. See also [oscillator models](oscillator-models.md) and
[simulation estimates](simulation-estimates.md).

The default budget is +3 dBm transmit power, 170 dB path loss and −164 dBm/Hz
noise density: −167 dBm received and −3 dB-Hz actual C/N0. Changing the target
does not change that power. Groundwave, skywave, meteor scatter, sub-9 kHz and
moonbounce scenarios need their own antenna, path and noise inputs; operating
modes such as FT8 and SSB do not themselves determine path loss.

**Model limits and references** contains the detailed oscillator and LPI
assumptions and the following rough reference examples. These are illustrative
inputs supplied for comparison, not propagation predictions or automatic
presets. Groundwave and moonbounce figures use path loss in dB.

| Example | Reference |
| --- | --- |
| Sub-9 kHz, 200 ft antenna, 10 kW | 0 dBm power reference; 200 dB path loss |
| Groundwave, 1 MHz, 150 miles | 180 dB path loss |
| Groundwave, 30 MHz, 150 miles | 210 dB path loss |
| Skywave, 1–30 MHz, SSB voice | 130 dB path loss |
| Skywave, 1–30 MHz, FT8 | 160 dB path loss |
| Meteor burst | 150 dB path loss |
| Earth–Moon–Earth, 5.8 GHz | −30 dBm transmit power; 220 dB path loss |

For a familiar scale comparison, −8 dB in 1 Hz becomes
`−8 − 10 log10(2500) ≈ −42 dB` on FT8's reporting scale. The
[WSJT-X guide](https://wsjtx.github.io/wsjtx/guide-full.html) lists −21 dB as the
FT8 reference threshold, a 21 dB difference between these references. This is
a unit conversion rather than a measurement of this modem's sensitivity.
The [Kenwood TS-2000 manual](https://www.kenwood.com/usa/Support/pdf/TS-2000-Owner-Manual.PDF)
documents a 300–2700 Hz transmit passband as one representative SSB filter.
Actual filters and the finite shaped waveform's spectral tails still matter.
