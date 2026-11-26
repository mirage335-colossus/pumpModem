# Reconciliation with prior work and open questions

Research date: 24 November 2026. This chapter cross-checks these findings
against the earlier Matrix research ([Sol-6.2](../Sol-6.2/README.md)), the
repository's history with Fast, single-carrier Robust and triple Robust, and
the literature's internal disagreements, and lists what remains unknown.

## Agreement and corrections relative to Sol-6.2

| Topic | Sol-6.2 | This pass | Status |
| --- | --- | --- | --- |
| Receive noise density | Illustrative −174/−164/−154 dBm/Hz; headline uses −164 | ITU-R P.372 medians give about −144 (quiet rural, 10 MHz) to −130 dBm/Hz (rural, 7 MHz) | **Corrected**: across these noise-density endpoints, the earlier −164 dBm/Hz headline overstates C/N0 by about 20–34 dB. For quiet rural, 2 mW/150 dB gives about −7 dB-Hz at 7.1 MHz (N0 ≈ −139.9 dBm/Hz) and −3 dB-Hz at 10.1 MHz (N0 ≈ −143.7 dBm/Hz), not +17; at 180 dB the corresponding values are −37 and −33 dB-Hz. |
| Blackwell INT4 | "INT4 listed for 8.9, 10.0, 10.3 and 12.x"; Blackwell supports INT4 Tensor input | INT4 listed only up to 8.9; RTX Blackwell whitepapers omit INT4; the 4-bit route is block-scaled FP4 | **Corrected** (see [precision chapter](07-matrix-hardware-and-precision.md)) |
| FP4 as integer arithmetic | "not integer arithmetic" | Integer-valued E2M1 operands with FP32 accumulation are exact within ~2^24 (model and exact-emulation evidence; verify per device) | **Refined** |
| Vega 5650U | No matrix unit; cites Vega 7nm ISA | gfx90c lacks even INT8/INT4 dot instructions; the cited ISA is gfx906 | **Corrected** |
| Representative targets | 8840U/780M and 9900K/UHD 630 absent | Added | **Extended** |
| 160 ms symbols in Doppler | Coherence loss table; suggests shorter apertures in severe profiles | Agreed for 160 ms; but long symbols with spread-based spacing and subspace detection are near-optimal at low C/N0 in every category | **Extended** |
| Noncoherent pessimism vs Durisi optimism | Noted as conditional | Resolved by the energy-per-coherence-cell parameter and Lozano–Porrat critical occupancy | **Extended** |
| Single 2.2 kHz stream has little GPU work | Noted | Quantified: < 0.01% of a 4090 for live reception; full use needs codeword-level correlation or batched simulation | **Agreed, quantified** |
| 4-bit quantization | 0.049 dB local Fisher loss at 16 levels | 0.05–0.09 dB inputs, ≤ 0.04 dB templates, ≤ 0.1 dB energy requantization | **Agreed** |
| Regulation, key semantics, stream contract | Detailed | Reused | **Agreed** |

## Prior modems

- **Fast Modem.** The repository's acoustic study computed a conditional
  Gaussian-equivalent capacity and reported about 43% of it (Sol-6.2's
  reconciliation). Fast is an excellent high-C/N0 modem. At the weak-link
  operating points here (x ≪ 1 in disturbed channels), splitting a fixed sum
  of tone powers incurs the quoted penalty only in the independently fading,
  additive-rate comparison. Applying that result to Fast's OFDM requires its
  actual channel covariance, receiver statistic and composite RF peak-power
  constraint; the calculation does not rule out OFDM as a class. The acoustic
  evidence does not qualify weak-HF operation. The two modems address different
  tested regimes, consistent with the user's framing; see the
  [prior-modem comparison](../Sol-6.2/prior-modems-and-other-paths.md#fast-modem-an-acoustic-strength-and-a-boundary).
- **Single-carrier Robust (historical).** A tone with keyed phase and
  amplitude patterning concentrates all power in one narrow band — the right
  choice for high energy per coherence cell — and gives observer resistance. It
  is essentially family B with narrow tiles. The limits suggest its weak points
  were (a) coherent integration and search extended beyond the channel's
  coherence and drift limits, which multiplies hypotheses without gain on
  disturbed paths, and (b) the computational structure. This is an inference
  from the user's description and the theory; the removed implementation was
  not re-examined.
- **Triple Robust.** Three equal-power simultaneous carriers at fixed summed
  tone power cut the energy per coherence cell per carrier by 4.8 dB. With
  independently fading components whose rates add, the square-law combined
  rate falls by about the same factor relative to one carrier visiting the
  three frequencies in turn. This remains a plausible structural contributor
  to long transmit times in weak, disturbed cases where that model applies;
  it was not verified against the repository's triple-Robust evidence in this
  pass. Correlated fading, a joint channel statistic and the composite waveform's
  actual peak-envelope constraint require a separate matched comparison. See
  the [conditional design analysis](05-waveform-and-parameter-design.md#d-simultaneous-multi-carrier-joint-detection-triple-robust-style).

## Disagreements inside the literature

- **FST4 "spread below spacing" rule vs long symbols with spread-based spacing.**
  The amateur rule holds for single-bin detectors with spacing ≈ 1/T. With
  spacing decoupled from 1/T and a subspace detector, the capacity analysis
  favours long symbols even under spread. Field evidence for the decoupled
  design does not yet exist.
- **Quiet-channel Doppler spread.** Modern GPSDO measurements (median 0.025 Hz
  on one 1,558 km path) are far below F.1487's 0.1 Hz "quiet" figure, but the
  estimator is undefined and the data are one season on few paths.
- **Equipment vs path spread.** Receiver processing and oscillators added
  0.07–0.7 Hz on amateur systems, comparable to or larger than benign
  ionospheric spread.
- **Solar-cycle dependence** of spread-F is station- and mechanism-dependent,
  with opposite signs reported.
- **Absorption vs SNR**: noise also falls during fadeouts at noise-limited sites.
- **Learned receivers**: competitive when the classical model is mismatched or
  expensive, behind matched filtering when the waveform is known.
- **Blackwell 4-bit support**: vendor tables say no INT4; PTX accepts the
  instruction; sm_120 SASS is unpublished.

## Open questions

1. Real HF Doppler spectra versus the Gaussian-Rayleigh model: how much better
   do discrete-mode and specular channels do, and how fast do they change?
2. Achievable coherent window on real paths with drift tracking, with and
   without Helper guidance and disciplined oscillators.
3. Category-mismatch losses (tone overlap) for fixed transmitter spacing.
4. Decoy-calibrated null behavior on captured HF noise with impulses and
   interferers.
5. Measured gain of codeword-level correlation on real coherent paths, against
   its GPU cost.
6. Emission spectrum and transceiver-filter behavior of long-symbol keyed
   hopping with practical transitions.
7. Whether integer-valued E2M1 lowering is exact on each Blackwell driver, and
   whether 4-bit `mma.sync` on sm_120 is emulated.
8. Measured mW-level path availability: no peer-reviewed WSPR/FST4W study was
   found; Matrix could produce this data itself.
9. Fade-duration, drift-rate and Rician K distributions for modern HF paths
   (absent from the literature found).
10. Whether observer resistance claims hold against multi-antenna or
    continuous-time observers on real noise.

## Limits of this research pass

- Literature searches were broad but not systematic; several reviews exhausted
  their search budgets, and some sources were read only as abstracts or
  snippets (marked in each chapter). Better data or algorithms may exist.
- All performance numbers are model calculations under stated assumptions
  (Rayleigh fading, Gaussian Doppler spectra, ideal coding and interleaving,
  perfect synchronization unless stated). No Matrix waveform or receiver was
  implemented, simulated at sample level, or measured on hardware.
- Hardware peaks are vendor figures or derived from them; achieved throughput
  will be lower.
