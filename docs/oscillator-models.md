# Oscillator references and timing assumptions

Baseband and Shift oscillator presets configure reference assumptions in the
native Robust GUI session and the compact Fast Console row. In Robust they
inform sampled-channel impairments, duration planning and public receiver search
bounds. In Fast they always constrain receiver search and deterministic Auto
profile selection; only **Simulation** and an explicitly started **Live
loopback test** inject their synthetic oscillator effects. Ordinary **Live
audio** and idle loopback monitoring use clean input. A loopback test passes the
modeled PCM through physical audio playback and capture. These settings do not
connect GPS hardware, discipline clocks or tune a radio. A selected model is
not measured device accuracy, sensitivity or complete receiver coverage. Live
Robust reception remains scoped to the configured public calendar; see
[current architecture and limits](robust-current.md).

## Independent physical quantities

Baseband reference quality concerns the ADC/DAC sample clock and the real-stream
tone. An independent RF translation oscillator adds a different frequency and
phase error. Fractional frequency offset times the affected carrier frequency
gives an absolute frequency error: a small ppm figure can matter at high RF even
when the delivered audio tone is low. Sample-rate error also accumulates timing
drift across a long observation. Static error, random phase diffusion and
clock wander are different model components.

In Robust, Carrier is the absolute frequency and Shift is the nonnegative translation
frequency. With upper-sideband convention the real-stream tone is Carrier minus
Shift, which must be positive. A 1.0015 MHz Carrier and 1 MHz Shift produce a
1.5 kHz tone; direct 1.0015 MHz sampling needs a suitable sample rate and device.
These are frequency coordinates, not hardware retuning commands.

The Shift oscillator is inactive at zero Shift. A genuinely shared
reference within a radio can link sampling and conversion errors; count that
reference once instead of adding a second independent error. Sharing within
one radio does not make the two stations identical or remove synthesizer noise.
A USB interface or a radio model name is not evidence of a common RF/audio
reference.
The Fast Console row exposes Shift frequency only as an RF coordinate for
oscillator bounds and, in Simulation or an explicit loopback test, its injected
oscillator-error model. Changing it does not translate the Fast audio carrier
or alter its passband. Its
shared-reference choice prevents counting
the same modeled reference twice; it does not describe an actual radio's wiring.
Changing the Fast profile or waveform revalidates these retained assumptions
against the new sampled audio band. If the modeled occupied band and offset
allowances do not fit, listening and transmission are disabled in every mode
with the required frequency range, available sampled band, and oscillator/channel
allowances shown. The declarations remain editable. Invalid injected test
effects additionally disable Simulation and explicit loopback; ordinary Live
does not inject them. The diagnostic does not retune the audio carrier or choose
a different source.

## Illustrative profiles

The profile values in `src/tuning_presets.cpp` are sensitivity assumptions, not
measurements or universal device specifications. GPSDO XO, TCXO and OCXO examples
separate mean locked frequency from phase diffusion. A smaller long-term offset
does not prove a smaller short-term phase fluctuation. Phase-diffusion amplitude
scales as the square root of elapsed time under the model; its variance scales
linearly. Device warm-up, holdover, temperature, independent codec crystals and
clock recovery can invalidate a selected simple profile.

GPS/PPS may synchronize a PC or estimate an audio rate without driving the
hardware sampling clock. Conversely, a GPS-disciplined oscillator does not
synchronize PC UTC. Retain independent timing uncertainty until actual provider
evidence supports a stronger claim. The [clock and audio timing contract](clock-sync.md)
explains dated sample bounds; [coherence research](weak-link-planning.md) explains
why a stable average frequency alone does not support unlimited integration.
