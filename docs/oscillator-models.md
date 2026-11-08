# Oscillator references and timing assumptions

The retained Baseband Osc and Shift Osc controls describe reference assumptions;
they do not connect GPS hardware, discipline clocks or tune a radio. Robust
operation is [unavailable in the GUI](robust-current.md) pending reconnection.
These settings are inputs to that future connection, not current receiver search
or sensitivity guarantees.

## Independent physical quantities

Baseband reference quality concerns the ADC/DAC sample clock and the real-stream
tone. An independent RF translation oscillator adds a different frequency and
phase error. Fractional frequency offset times the affected carrier frequency
gives an absolute frequency error: a small ppm figure can matter at high RF even
when the delivered audio tone is low. Sample-rate error also accumulates timing
drift across a long observation. Static error, random phase diffusion and
clock wander are different model components.

Carrier is the absolute frequency and Shift is the nonnegative translation
frequency. With upper-sideband convention the real-stream tone is Carrier minus
Shift, which must be positive. A 1.0015 MHz Carrier and 1 MHz Shift produce a
1.5 kHz tone; direct 1.0015 MHz sampling needs a suitable sample rate and device.
These are frequency coordinates, not hardware retuning commands.

At zero Shift the Shift Osc control is inactive. A genuinely shared reference
within a radio can link sampling and conversion errors; count that reference
once instead of adding a second independent error. Sharing within one radio
does not make the two stations identical or remove synthesizer noise. A USB
interface or a radio model name is not evidence of a common RF/audio reference.

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
