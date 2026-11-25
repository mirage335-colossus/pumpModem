# Other propagation paths through the same lens

Research date: 24 November 2026. HF skywave is the hardest combined case, but
the user also lists sub-9 kHz with a 200 ft antenna, 1 MHz and 30 MHz
groundwave, and Earth–Moon–Earth. The repository already contains detailed
research on each ([Fable-5.2](../../highFidelity-triple_Robust_Modem/Fable-5.2/README.md),
[Astra-6](../../highFidelity-triple_Robust_Modem/Astra-6/README.md)). This
chapter does not repeat it; it applies the energy-per-coherence-cell parameter
x = (C/N0)·T_c from the [limits chapter](03-information-limits-and-noncoherent-penalties.md)
to say how each path changes the Matrix design.

| Path | Coherence and spread (qualitative, from prior chapters) | Noise | Consequence for Matrix |
| --- | --- | --- | --- |
| Sub-9 kHz, 200 ft antenna | Earth–ionosphere waveguide or near-field; phase very stable (EbNaut decodes with hours-long coherent BPSK); day/night transitions shift phase and level | Impulsive sferics dominate; blanking 25–35% of samples gained ~20 dB at VLF in EbNaut practice | Very high x even at tiny C/N0: long coherent symbols (seconds to minutes) and codeword-level coherent correlation are where computation pays most. Antenna efficiency (λ/594 for 61 m at 8.27 kHz) dominates the budget. Impulse blanking before 4-bit quantization is essential. ([F10](../../highFidelity-triple_Robust_Modem/Fable-5.2/10-sub9khz-propagation.md), [F11](../../highFidelity-triple_Robust_Modem/Fable-5.2/11-sub9khz-antennas-links-and-practice.md), [F12](../../highFidelity-triple_Robust_Modem/Fable-5.2/12-sub9khz-noise.md)) |
| 1 MHz groundwave | Daytime groundwave stable; at night skywave adds a fading component (MF night fades of minutes) | High man-made and atmospheric noise (Fa ≈ 54–77 dB by day, higher at night) | Daytime: long symbols, near-AWGN. Night: Rician/Rayleigh mixture with slow fades; time diversity across nights or day-only operation. ([F20](../../highFidelity-triple_Robust_Modem/Fable-5.2/20-groundwave-1mhz.md)) |
| 30 MHz groundwave | Short-range surface/space wave; stable except for local motion and multipath | Low natural noise; man-made noise site-dependent | Long coherent windows feasible; path loss rather than coherence limits. ([F25](../../highFidelity-triple_Robust_Modem/Fable-5.2/25-groundwave-30mhz.md)) |
| 1–30 MHz skywave | Benign to extreme categories ([channel chapter](01-hf-channel-moderate-to-worst.md)) | P.372 external noise ([link budget](02-noise-and-link-budget.md)) | The primary design case. |
| Earth–Moon–Earth | Lunar libration spreads the echo in Doppler (frequency- and geometry-dependent) and in delay up to ~11.6 ms; Faraday and spatial polarization effects at lower bands | Sky and system temperature | Often low x: amateur EME modes (Q65, JT65) use wide tones and averaging for this reason. Matrix's spacing-from-spread rule and Doppler-subspace receiver apply directly; long-symbol coherent gains are limited by libration spread. ([F40](../../highFidelity-triple_Robust_Modem/Fable-5.2/40-eme-lunar-scattering-and-geometry.md), [F41](../../highFidelity-triple_Robust_Modem/Fable-5.2/41-eme-systems-practice-and-environment.md), [Astra-6 EME](../../highFidelity-triple_Robust_Modem/Astra-6/40-earth-moon-earth-5p8ghz.md)) |

Two cross-path observations follow:

- **Stable paths reward computation more than disturbed ones.** Where x is
  large (VLF, groundwave by day, quiet one-hop skywave), longer coherent windows,
  drift search and codeword-level correlation recover many dB. Where x is small
  (disturbed skywave, many EME cases), the peak-limited capacity bound caps
  what any receiver can do, and energy or power is the lever.
- **One waveform family can serve all paths if its parameters follow the
  rules in the [design chapter](05-waveform-and-parameter-design.md):** symbol
  duration from the design C/N0, spacing from the spread category, bandwidth
  from the equipment, and a receiver bank that covers the expected categories.
