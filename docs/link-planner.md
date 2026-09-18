# Link planner

Open **Link planner**, immediately after **Console**, to start with transmit
power, path loss and noise. The headline shows whether that budget meets the
selected target, its margin or shortfall, and any receiver clock/RAM limit.
Transmit power is shown in watts, milliwatts or microwatts alongside dBm.

The timing preview starts at −8 dB in 1 Hz, independently of the current
transmit targets. Rate, carrier, waveform, key, oscillator and DSP
allowance follow the shared controls. Choose **Use target for short messages**
or **Use target for long messages** to apply a preview.

The top-bar **Simulation** choice is **Yes / No**. With **No**, editable power,
path-loss and noise dropdowns appear beside it. They share their values with the
planner and RX success estimate. RX success remains available without sampled
simulation; CPU/GPU computation estimates appear only with **Yes**. Changes to
the budget in live mode update estimates without restarting reception.

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
can have separate coverage or memory gaps. Memory and clock coverage are
separate from successful reception. The check assumes one matching RX profile,
rather than the currently unapplied receive-target list. More profiles or keys
can require additional workspace.

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
presets. The groundwave figures specify power rather than path loss; the
moonbounce entry uses the confirmed interpretation of 220 dB path loss.

| Example | Reference |
| --- | --- |
| Sub-9 kHz, 200 ft antenna, 10 kW | 0 dBm power reference; 200 dB path loss |
| Groundwave, 1 MHz, 150 miles | −180 dBm power reference |
| Groundwave, 30 MHz, 150 miles | −210 dBm power reference |
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
