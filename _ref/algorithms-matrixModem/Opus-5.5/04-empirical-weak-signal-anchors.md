# Empirical anchors from existing weak-signal systems

Research date: 24 November 2026. This chapter places documented decoding
thresholds of existing weak-signal systems on one scale and compares them with
the limits in [the limits chapter](03-information-limits-and-noncoherent-penalties.md).
It summarizes two literature reviews made for this pass (WSJT-X protocol
documents, QEX papers, EbNaut notes, MIL-STD-188-110C, CCSDS, Voyager,
GNSS and SETI sources) and adds the comparison with the Matrix calculations.

Conventions: C/N0 = SNR(2500 Hz) + 33.98 dB-Hz. Eb/N0 = C/N0 − 10·log10(Rb)
with Rb the payload rate over the whole transmission and C the total power
including synchronization. Gap = Eb/N0 − (−1.59 dB). WSJT-X thresholds are the
SNR at 50% probability of decode on AWGN unless noted; other systems use other
criteria, which are stated.

## Findings

1. **The best practical noncoherent MFSK sits 5.6–6.9 dB above the Shannon
   limit on AWGN.** FST4/FST4W, Q65 short submodes, FT4 and FT8 decode 50% of
   messages at total Eb/N0 ≈ 4.0–5.3 dB. Older modes (WSPR, JT65, JT9, JT4) sit
   at 6.3–9.1 dB.
2. **The FST4 family keeps Eb/N0 constant to ±0.15 dB from 15 s to 1,800 s
   transmissions.** It scales symbol duration with the intended C/N0, exactly
   the rule this research derives independently (symbol duration from target
   C/N0; see the [design chapter](05-waveform-and-parameter-design.md)). What
   limits its long submodes on HF is Doppler spread and drift, not coding.
3. **Coherent signaling with known timing gets within about 0.6–1.7 dB.**
   EbNaut (coherent BPSK, long convolutional code, list decoding, GPS-locked
   transmitter and receiver, pre-announced time and frequency) reached
   Eb/N0 ≈ −1.0 dB at 63–74% success in the laboratory and decoded a
   transatlantic 8.8 kHz message at −56.2 dB in 2500 Hz. That path is
   phase-stable VLF; the result does not transfer to disturbed HF.
4. **Where the 6–7 dB goes** (AWGN, 50% decode, 50–77-bit messages): explicit
   sync symbols 0.7–3 dB; per-symbol noncoherent detection 3.6–4.5 dB relative
   to codeword-level ideal detection; code/decoder about 1–2.4 dB. Multi-symbol
   block detection already recovers about 0.7–1.7 dB of the noncoherent loss
   on AWGN, but only about 0.1 dB on a 10 Hz spread channel.
5. **Fading penalties are set by spread relative to tone spacing.** FT8
   (6.25 Hz tones, 160 ms symbols, structurally the Matrix default) loses 0.8,
   2.0, 2.2 and 12.2 dB on F.1487 quiet, moderate, disturbed mid-latitude and
   10 Hz high-latitude channels. FT4 (20.8 Hz tones) loses 5.3 dB at 10 Hz.
   Olivia 1000/32 (31.25 Hz tones, Walsh coded) is 6–11 dB less sensitive on
   AWGN but still copies at a 30 Hz/7 ms channel.
6. **Every 50–77-bit mode spends about 21.6–24 dB of received E/N0 per
   message.** At −10 dB-Hz, a 50-bit message at FST4W efficiency needs about
   1,450 s. This is a practical anchor for message duration estimates.
7. **A-priori ("deep search") decoding is the largest single lever for short
   amateur messages** (1.5–3.6 dB in Q65, about 2 dB in FT8 on mild channels
   and up to 10 dB on a 10 Hz channel, about 4 dB in JT65), but it works by
   shrinking the set of plausible messages. It does not apply to an encrypted,
   unpredictable payload; the Matrix analogue is prior knowledge of keys,
   schedules and timing, not message content.
8. **Synchronization, not decoding, sets the floor once decoding is strong.**
   JT65 deep search was limited by sync failure below about −28 dB. GNSS
   acquisition at 9 dB-Hz needed about 22 dB of E/N0 just to detect one known
   signal, with Doppler-rate aiding and an OCXO.

## Master comparison

Values from the WSJT-X protocol guide and QEX papers (full text), FST4/FST4W
and Q65 quick-start guides (full text), the FST4W HF paper by Griffiths et al.
(2022, full text), EbNaut notes (full text via archive), MIL-STD-188-110C
(tables read), CCSDS 130.1-G-3 (sections read), Voyager DESCANSO summary
(tables read), Psiaki 2004 (sections read), and secondary sources where marked.

| System | Rate | Threshold (definition) | C/N0 (dB-Hz) | Eb/N0 (dB) | Gap (dB) | Fading / spread notes |
| --- | --- | --- | ---: | ---: | ---: | --- |
| FST4W-120 (4-GFSK, 1.46 Hz, LDPC (240,74)) | 50 b / 109 s | −32.8 dB, 50%, AWGN | +1.2 | 4.6 | 6.2 | −24 dB at 2 Hz spread; needs spread < spacing, best ≤ 1/8 |
| FST4W-1800 (0.089 Hz tones) | 50 b / 1,792 s | −44.8 dB | −10.8 | 4.7 | 6.3 | Usable on HF only in rare cases (GPSDO groundwave) |
| FST4-15 … FST4-1800 | 77 b / 9.6–1,792 s | −20.7 … −43.2 dB | +13.3 … −9.2 | 4.2–4.5 | 5.8–6.0 | Same rule |
| FT8 (8-GFSK, 6.25 Hz, 160 ms, LDPC (174,91), BP+OSD) | 77 b / 12.6 s | −20.8 dB | +13.2 | 5.3 | 6.9 | −20.0 / −18.8 / −18.6 / −8.6 dB on 0.1 / 0.5 / 1 / 10 Hz channels |
| FT4 (4-GFSK, 20.8 Hz) | 77 b / 5.0 s | −17.5 dB | +16.5 | 4.6 | 6.2 | −17.4 / −15.8 / −15.2 / −12.2 dB on the same channels |
| Q65-15A … Q65-300A (65-FSK, QRA code) | 77 b / 12.8–294 s | −22.2 … −33.8 dB | +11.8 … +0.2 | 4.0–6.0 | 5.6–7.6 | Effective with spread up to ~10× tone spacing |
| WSPR (4-FSK, K=32 conv., Fano) | 50 b / 111 s | −31 dB (WSJT-X decoder) | +3.0 | 6.4 | 8.0 | No decode at 3 Hz spread at any SNR |
| JT65A (65-FSK, RS(63,12), soft) | 72 b / 47 s | −25 dB | +9.0 | 7.1 | 8.7 | Deep search ~4 dB more, sync-limited below −28 dB |
| JT9A / JT4A | 72 b / ~48 s | −26 / −23 dB | +8.0 / +11.0 | 6.3 / 9.1 | 7.9 / 10.7 | Wider submodes for spread |
| EbNaut (coherent BPSK, K=25 r=1/16, list Viterbi) | 5–25 chars | 63–74% at −1.0 dB (lab) | — | −1.0 | 0.6 | Phase-stable VLF; GPS-locked; known start time |
| EbNaut W4DEX→UK, 8.8 kHz | 142.5 b / 6.75 h | decoded at −56.2 dB | −22.2 | +0.1 | 1.7 | Ranked 89th in a 50,000 list |
| Olivia 32/1000 (Walsh-coded 32-FSK) | ~17.5 b/s | about −14 dB (criterion and bandwidth not stated) | ~+20 | ~7.5 | ~9 | 100% copy at −3 dB on 30 Hz/7 ms (PathSim) |
| MIL-STD-188-110C App. D WID 0 (Walsh, 75 b/s, 3 kHz) | 75 b/s | BER ≤ 10⁻⁵ | +28.8 / +33.8 | 10.0 / 15.0 | 11.6 / 16.6 | AWGN / F.1487 "Poor" (1 Hz, 2 ms) |
| CCSDS turbo r = 1/6, k = 8920 | — | BER 10⁻⁶ | — | −0.1 | 1.5 | Assumes no synchronization loss |
| Voyager 160 b/s | 160 b/s | telemetry threshold | ≈ +26.6 | 2.3 | 3.9 | Residual carrier costs another 1.25 dB |
| GPS L2CL acquisition (Psiaki 2004) | detection | P_md 1.8×10⁻⁵, P_fa 10⁻⁴ | 9 | 22 dB E/N0 over 20 s | — | Needs Doppler-rate aiding and OCXO |
| LoRa SF12/125 kHz CR 4/5 (secondary) | 293 b/s | −20 dB demodulator limit | +31.0 | 6.3 | 7.9 | — |

JS8Call's published thresholds are nominal design statements that are
inconsistent with FT8's measured performance; they are omitted. No defined
thresholds were found for ROS, STANAG 4415 (restricted), 3G/4G ALE, or a
primary WSPR-15 specification.

## Loss decomposition for the WSJT modes

A finite-message reference first. ML detection of one of 2^k orthogonal
waveforms on AWGN at 50% success needs Eb/N0 ≈ −1.9 dB (coherent) or
≈ −1.6 dB (one unknown phase per message) for k = 50–77; at 99% success it
needs about 2.2 dB more. "Gap to −1.59 dB" therefore understates the gap at
the 50% point by 0.2–0.3 dB.

| Mode | Per-symbol Es/N0 at threshold | Sync loss | Data-only Eb/N0 | Per-symbol noncoherent capacity limit at the mode's rate | Residual |
| --- | ---: | ---: | ---: | ---: | ---: |
| FST4W-120 (4-FSK) | −0.5 dB | 1.25 dB | 3.3 dB | 4.9 dB | −1.6 dB (block detection gain) |
| FT8 (8-FSK) | +5.2 dB | 1.34 dB | 4.0 dB | 2.9 dB | +1.1 dB |
| Q65-60A (64-FSK) | +4.2 dB | 1.31 dB | 3.3 dB | 1.8–2.1 dB | +1.2–1.5 dB |
| JT65A (64-FSK, RS) | +4.7 dB | 3.01 dB | 4.1 dB | 1.9–2.1 dB | +2.0–2.2 dB |

The per-symbol noncoherent limits come from Monte Carlo calculations of
symbol-wise mutual information with Bessel (unknown-phase) likelihoods made in
the two literature reviews; they agree with this pass's `mfsk_capacity.cpp`
trend (the per-symbol limit approaches −1.59 dB only as the alphabet and the
per-symbol energy grow).

Observations:

- Per-symbol noncoherent detection is the largest structural loss: 3.6 dB
  (64-FSK) to 4.5 dB (8-FSK) above codeword-level ideal detection at these
  rates. FST4W's negative residual shows that phase-coherent block detection
  across symbols beats the symbol-wise limit on AWGN; FT8's 3-symbol blocks
  gain 0.7 dB and FT4's 1.6 dB, but only 0.1 dB at 10 Hz spread.
- Q65's threshold does not scale ideally with duration: from 15 s to 300 s the
  ideal improvement is 13.6 dB but 11.6 dB is observed, so total Eb/N0 worsens
  from about 4.0 to 6.0 dB. The cause is not established; wider relative search
  grids and narrower tones are plausible contributors.

## What the anchors imply for Matrix Modem

- **A realistic first target** is to match FST4/FST4W on AWGN (total
  Eb/N0 ≈ 4.2–4.8 dB at 50% decode) while degrading less on spread channels and
  stating thresholds at 90% and 99% as well as 50%.
- **Remaining improvement on AWGN-like paths is about 4–6 dB** and lies in three
  places, all of which grow hypothesis matrices: coherent multi-symbol or
  codeword-level correlation (recovering part of the 3.6–4.5 dB per-symbol
  loss where the path is coherent), joint synchronization and decoding instead
  of sync symbols (recovering 0.7–3 dB while the added hypotheses cost less;
  about +0.3 dB per factor of 2^10 hypotheses in an ideal detector), and
  near-ML decoding of short codes (about 1 dB). This is where heavy matrix
  computation can buy real sensitivity, conditional on channel coherence.
- **On disturbed paths the anchors confirm the limits chapter.** FT8's 12 dB
  loss at 10 Hz versus FT4's 5 dB and Olivia's operation at 30 Hz show that
  wider spacing trades AWGN sensitivity for spread tolerance. The calculations
  show a further option the amateur modes do not use: long symbols with spacing
  set by the spread and a Doppler-subspace detector.
- **Equipment budgets matter.** Griffiths et al. derived explicit
  transmitter+path+receiver spectral-spread budgets for FST4W; a KiwiSDR's
  browser buffering alone added up to 0.67 Hz of spread. A Matrix receiver that
  relies on long coherent integration needs disciplined oscillators or explicit
  equipment-error search, and its qualification needs an equipment axis.

## Sources

- WSJT-X User Guide, protocol specifications. https://sourceforge.net/p/wsjt/wsjtx/ci/master/tree/doc/user_guide/en/protocols.adoc (full).
- Franke, S., Somerville, B., Taylor, J. "The FT4 and FT8 Communication Protocols." QEX July/Aug 2020. https://wsjt.sourceforge.io/FT4_FT8_QEX.pdf (full).
- Franke, Somerville, Taylor. "Quick-Start Guide to FST4 and FST4W." 2020. https://wsjt.sourceforge.io/FST4_Quick_Start.pdf (full).
- Taylor, Somerville, Franke, Palermo. "Quick-Start Guide to Q65." 2021. https://wsjt.sourceforge.io/Q65_Quick_Start.pdf (full; figure values not extracted).
- Taylor, J. "The JT65 Communications Protocol." 2005. https://wsjt.sourceforge.io/JT65.pdf (full).
- Franke, S., Taylor, J. "Open Source Soft-Decision Decoder for the JT65 (63,12) Reed-Solomon Code." QEX May/June 2016 (manuscript in the WSJT-X repository; full text, figures not viewed).
- Taylor, J., Franke, S. "SuperFox and FT8: Weak-Signal Performance." 2024. https://wsjt.sourceforge.io/SuperFox_Performance.pdf (full).
- Griffiths, G. et al. "FST4W on the HF Bands." TAPR DCC 2022. https://web.tapr.org/tech_docs/DCCpapers/2022%2002.pdf (full).
- Nicholson, P. "EbNaut Coherent BPSK," technical notes and appendix (abelian.org/ebnaut; read via an archived copy; abelian.org did not resolve).
- de Boer, P.-T. (PA3FWM). "Signal/noise ratio of digital amateur modes." 2015. https://www.pa3fwm.nl/technotes/tn09b.html (full).
- MIL-STD-188-110C w/Change 1 (2011), Tables XVI, D-II, D-LII (relevant tables read).
- CCSDS 130.1-G-3 (2020), TM Synchronization and Channel Coding — Summary of Concept and Rationale (sections read).
- Ludwig, R., Taylor, J. "Voyager Telecommunications." DESCANSO, 2002 (tables read).
- Psiaki, M. "FFT-Based Acquisition of GPS L2 Civilian CM and CL Signals." ION GNSS 2004 (sections read).
- Bombardiere, A. (K2MO). PathSim tests of HF digital modes (undated list attachment; full; criterion "error-free pangram").
- Kozlenko, M. et al. arXiv:2502.19097 (CNN demodulation of JT65A-like MFSK; abstract): about 1.5 dB short of the noncoherent MFSK limit, no gain over classical demodulation reported.
