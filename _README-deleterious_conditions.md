The most severe low-power successes were at **100 mW transmit power through 180 dB of simulated path loss**. The receiver decoded one bit under each of two severe fading models:

- **Severe GUI model:** one of three bit trials decoded. The model allowed up to 90 Hz common and relative Doppler, 21 ms excess delay, and 65% frequency-selective modulation. The other two bits were erased as ambiguous. [Simulation report, severe GUI result](_tests-RobustModem-HF-simulations-2026-11-18.md#severe-gui-100mw-result)
- **Gaussian fading model:** one of two bit trials decoded, with two fading taps at 0 and 7 ms, a 30 Hz full 2σ Doppler width, and 65% modulation. The other bit was erased as ambiguous. [Simulation report, Gaussian result](_tests-RobustModem-HF-simulations-2026-11-18.md#gaussian-100mw-result)

Each bit took **12,069.6 seconds—about 3 hours 21 minutes** to transmit. Both successes were for bit 1 at the same seed, tested under the two channel models. They show that a bit could be recovered in those realizations; they do **not** establish reliable reception or a complete message under severe fading.

The lowest-power **complete-message** success was **2 mW**, also through 180 dB loss, but under **stable propagation**: the receiver decoded both bit values and the one-byte message `e` in two replays, including the two scored quiet slots that completed each message. Each bit pattern lasted 962.4 seconds. The simulator bypassed propagation impairments for this result, though oscillator errors and noise remained. [Simulation report, 2 mW stable result](_tests-RobustModem-HF-simulations-2026-11-18.md#stable-2mw-complete-messages)

**No tested severe-channel setting at 2 mW recovered a bit.** These were configured-calendar simulations with no Helper and no site-specific lightning or interference measurements. [Simulation conditions](_tests-RobustModem-HF-simulations-2026-11-18.md#simulation-conditions)
