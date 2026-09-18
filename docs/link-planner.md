# Link planner

Open **Link planner** to compare bit duration, transmission time and observer
observation time. The preview starts at −8 dB in 1 Hz, independently of the
current transmit targets. Rate, carrier, waveform, key, oscillator and DSP
allowance follow the shared controls. Choose **Use target for short messages**
or **Use target for long messages** to apply a preview.

The graphs use a logarithmic vertical scale: equally spaced steps represent
multiples of time. Weaker targets generally need longer bits; automatic profile
boundaries make discrete steps before integration varies continuously.

At 3.6 kHz rate and a 1.5 kHz carrier, the automatic shaped pattern gives:

| Reference | Result |
| --- | --- |
| −8 dB-Hz | 398.107 seconds per bit; 401.116 seconds sending one bit |
| −23 dB-Hz | 12,589.254 seconds per bit, about 3.5 hours |
| One bit per second | Automatic profile transition near +20.45 dB-Hz |
| One day per bit | Near −31.365 dB-Hz |
| Ideal shaped passband | 375–2,625 Hz, 2.25 kHz wide |

The completion estimate adds fully scored absence covering six seconds. A
minutes-long bit requires a complete additional minutes-long absent symbol.
Processing delay comes afterward. **Use current draft** preserves the existing
wire count: exact short dictionary/raw bits, or fixed 192-bit markers and
128-coded-byte intervals for longer messages and attachments.

The illustrative free-running crystal has a 100 ppm relative clock offset and
0.5 degrees/√second phase diffusion. At the default carrier its 0.15 Hz shift
fits the current receiver search at −8, but exceeds it at −23. The long-duration
search edge is near −17.33 at this geometry; short profiles can have separate
coverage gaps. Memory and clock coverage are separate from successful reception.
The check assumes one matching RX profile, rather than the currently unapplied
receive-target list. More profiles or keys can require additional workspace.

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

For a familiar scale comparison, −8 dB in 1 Hz becomes
`−8 − 10 log10(2500) ≈ −42 dB` on FT8's reporting scale. The
[WSJT-X guide](https://wsjtx.github.io/wsjtx/guide-full.html) lists −21 dB as the
FT8 reference threshold, a 21 dB difference between these references. This is
a unit conversion rather than a measurement of this modem's sensitivity.
The [Kenwood TS-2000 manual](https://www.kenwood.com/usa/Support/pdf/TS-2000-Owner-Manual.PDF)
documents a 300–2700 Hz transmit passband as one representative SSB filter.
Actual filters and the finite shaped waveform's spectral tails still matter.
