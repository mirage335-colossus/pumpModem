# Link planner

Set transmit power, path loss and noise in the top bar, then open **Link
planner**, immediately after **Console**. Its headline shows the preview's RX
estimate or clock/RAM limit. A limited fallback model is labeled **RX reference**.
Below it are the power budget's margin or shortfall and whole-bit phase loss.
The power field accepts watts,
milliwatts, microwatts or dBm.

The timing preview starts at −8 dB in 1 Hz, independently of the current
transmit targets. Edit **Target SNR (dB-Hz)** in the page's **Stronger / Weaker**
controls row. This native dropdown scrolls with the planner; the top-bar
oscillator's numeric clock notes remain hidden while the planner is open.
Rate, carrier, waveform, key, oscillator and DSP allowance follow the shared
controls. Choose **Use target for short messages** or **Use target for long
messages** to apply a preview.

The compact **Launch command** box sits between the target controls and CPU
graph, with **Load** beneath it. It lets you copy the preview's settings or
paste a command to load. It includes the link budget, oscillator,
rate, carrier, waveform and DSP allowance; the preview target applies to both
short and long messages. Click **Load** to apply the settings and select
**Simulation: No**. Omitted options keep their current values. Loading preserves
your message and never starts transmission or executes the command.

The box supports selection, copy, paste and scrolling. Enter adds a line;
Tab moves to **Load**. Invalid commands leave the settings and pasted text
intact. Routine updates also preserve your text; changing a relevant GUI
setting regenerates the command from the accepted values.

For example:

```text
./datapump-gui --auto-pattern --tx-dbm 3 --path-loss-db 170 --noise-dbm-hz -164 --oscillator crystal --target-snr -8 --rate 3600 --carrier 1500 --dsp-workspace 50%
```

Windows exports start with `.\datapump-gui.exe`; Unix exports use
`./datapump-gui`. Pasted commands may use either executable path, including a
quoted path with spaces. **Load** ignores the executable token and reads only
the settings options.

In automatic modes, every target entry checks clock coverage and RAM: the short
and long transmit dropdowns, each value in the independent RX list, and the
Planner target. An already fitting value stays exact; otherwise selection
prefers the nearest checked weaker target, with a stronger fallback when needed.
Transmit entries include the other transmit target and active plaintext/key
banks. RX entries include the other receive targets and those same banks. The
independent Planner target checks one matching receive profile. These checks
do not depend on RX probability or CPU pace. Fixed modes retain their exact
values and fixed waveform geometry.

While typing, your text stays intact and an adjusted label shows the value in
use. A preset or Enter displays the exact accepted value or RX list. Editing RX
or Planner targets does not retune transmission. Changing a transmit target
updates the matched RX list from both exact effective transmit values. Explicit
planner Apply preserves the selected target's full precision.

The top-bar **Simulation** choice is **Yes / No**. Editable power, path-loss and
noise dropdowns stay beside it in both modes, sharing their values with the
planner and RX estimate. The estimate remains available without sampled
simulation; simulation CPU/GPU times appear only with **Yes**. The planner
does not repeat the input controls, and the estimate row takes no space with
**No**. Changes to the budget in live mode update estimates without restarting
reception.

**Time per bit** combines two scales: the solid line uses logarithmic time on
the left; dashed **RX 1 bit** uses 0–100% on the right. Power, path, noise and
oscillator settings stay fixed along the RX curve. Unsupported targets leave
gaps rather than implying zero probability; tiny rounding gaps may use an
independently checked usable target within 0.001 dB. The selected point follows the
current preview; intermediate values interpolate bounded model samples.
The lines' visual crossing is not a detection threshold or an optimum.
Weaker targets generally need longer bits; automatic profile boundaries make
discrete steps before integration varies continuously. **Observer / receiver
time** retains its separate logarithmic ratio scale and existing LPI model.

The headline's **CPU estimate** is available in either simulation mode. It
compares receiver processing with incoming audio for a one-bit preview,
including the final silence check, on the reference Intel Core i9-13900H.
Green means less than 0.5 seconds of modeled work per audio second; yellow
means 0.5–1, and red means 1 or more. These are planning indicators, not measured
CPU utilization or a guarantee of timely reception. Search bursts and other
receive targets can consume additional headroom. RX probability remains
conditional on completing the work. Expanded details also compare the complete
one-bit simulation CPU time, including synthetic channel generation, with the
bit's transmit duration.

A compact CPU graph sits beside the command box, on the right. These columns
wrap beneath the target controls on narrower pages and stack when needed.
The graph uses the same stronger-to-weaker target range as **Time per bit**,
with a logarithmic vertical
scale of processing seconds per audio second. The reference line marks one
second of work per second of audio; higher values indicate falling behind on
the reference computer. The selected marker matches the headline. Clock/RAM
gaps stay visible, and the curve uses the same one-bit assumptions at every target.

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
found in that direction. The Planner target dropdown accepts manual entry.
Selections and Apply preserve the exact target even when its label is rounded.

These gaps can be extremely narrow. At 1 Hz rate, a 1.5 kHz carrier, the hobby
GPSDO model and 4 GiB DSP allowance, exact −47 dB uses a timing that exceeds RAM.
A target only about 0.000001 dB stronger can fit, because the receiver can average
many more samples before searching. The hidden model details explain this
without adding another graph or more numbers to the main view.
The bounded search checks sample-aligned timings and changes in clock-search
and FFT geometry; other usable timings may remain undiscovered.

Memory and clock coverage are separate from successful reception.
The three GPSDO choices share a 0.0001 ppm residual-frequency assumption, so
their clock/RAM coverage is identical; their phase-diffusion assumptions differ.
This channel model does not simulate GPS phase corrections.
The planner preview checks one matching RX profile, rather than the currently
unapplied receive-target list. More profiles or keys can require additional
workspace; the transmit dropdowns include those banks when choosing a fit.

The planner's **RX estimate** includes signal strength, phase drift, residual
frequency and timing error, acquisition and RAM checks. Eligible long patterns
model both coherent and four-section reception, including their shared noise,
competing bit patterns and detector-choice penalty. It estimates reception of
every wire bit before error correction; the top-bar estimate uses the actual
configured draft, receive bank and FEC. Expanded **Model limits** includes a
coherent-only comparison from the same statistical model, plus section phase
loss. The primary phase readout describes the whole-bit coherent loss, not the
combined detector's complete penalty. Neither value is measured sensitivity.

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
fit RAM. **RX reference** labels a limited model when a combined estimate is
unavailable; it is not a proven lower bound. The statistical estimate uses 4096
deterministic matched-statistic trials, without generating audio or executing
the complete adaptive receiver search.

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
