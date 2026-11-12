# Oscillator references and timing assumptions

Baseband and Shift oscillator presets configure reference assumptions in the
native Robust GUI session and the Fast Simulation page. In Robust they inform
sampled-channel impairments, duration planning and public receiver search
bounds; in Fast they configure the sampled channel used by **Simulation** and
the explicit **Live loopback test**. The latter passes the modeled PCM through
physical audio playback and capture; ordinary **Live audio** does not insert
these synthetic oscillator effects. They do not
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
The Fast Simulation page exposes Shift frequency only as an RF coordinate for
its oscillator-error model in Simulation and Live loopback test. Changing it
does not translate the Fast audio carrier or alter its passband. Its
shared-reference choice prevents counting
the same modeled reference twice; it does not describe an actual radio's wiring.

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
