# Equipment and front-end effects

- **Part of.** [High-fidelity weak-signal channels for the triple Robust Modem](README.md). Evidence grades, access-status words and conventions are defined in [00-method-and-modem-context](00-method-and-modem-context.md).
- **Scope.** Literature review only. Nothing was built or run on the modem, and no repository file outside this directory was changed.
- **Status.** Prepared on 13 November 2026 by one research pass, then checked against its sources by a second, independent pass. Sources that could not be opened are marked by access status; values not read in a source are not presented as verified.
- **Related chapters.** sub-9 kHz antennas [11](11-sub9khz-antennas-links-and-practice.md); Earth–Moon–Earth systems [41](41-eme-systems-practice-and-environment.md); noise [50](50-noise-and-interference-mf-hf.md); executable noise/equipment scenario specifications [85](85-scenarios-noise-and-equipment.md); simulation numerics [60](60-simulation-methods-and-validation.md).

**Summary.** Undisciplined references, not propagation, limit coherent integration in most scenarios. A fractional error y maps to y·ν0: 100 ppm is 0.8 Hz at 8 kHz; a ±0.5 ppm TCXO is 0.5 Hz at 1 MHz, 15 Hz at 30 MHz and ±2.9 kHz at 5.76 GHz; a 1e-10 GPSDO error is still 0.58 Hz at 5.76 GHz [E]. NIST tabulates σy(1 s) of 1e-8 to 1e-9 for TCXOs and 1e-12 for OCXOs [B]; ten GPSDOs calibrated at NIST spanned 1e-12 to 5e-10 at 1 s, at most 1e-11 at 1 min and 0.5–7e-13 at one day [P]. Beyond 100 s, measured free-running examples are 2e-9 at 10³ s and 1e-8 at 10⁴ s for a TCXO in a room, and 2e-13 to 8.5e-12 at 10³ s for two OCXOs [G]. Older versions of one hobby GPSDO showed a persistent offset of −9.6e-11, a 2020 version about 4e-13 [G]. Audio clocks of 21 devices measured against GPS ranged from −98 to +417 ppm (median +7 ppm; dedicated recorders and an interface −40 to +23 ppm), and an uncompensated crystal moves about 1 ppm over a 30–90 min room-temperature cycle; in direct-audio VLF the sample clock is the carrier reference [G]. PC time: about 1 ms to 30–50 ms (NTP, depending on network path), 1 µs (kernel PPS), 1 s (GPS NMEA alone); a GPS PPS recorded in a spare audio channel times the samples to better than 50 ns [P/S/G]. MIL-STD-188-141B bounds group-delay variation to 1.0 ms and AGC attack/release to ≤30 ms/0.8–1.2 s (voice) or ≤10/≤25 ms (data) [S]; measured transceiver receive delays span 2–165 ms, about 2 s ± 50 ms for a network SDR [G]. LO noise at 10 kHz spans −99 to −155 dBc/Hz, narrow-spaced (2–5 kHz) dynamic range 45–110 dB, direct-sampling ADC clipping about −10 dBm, transmit IMD3 −25 to −40 dBc [G/S]; transmit phase noise 100 Hz from the carrier is about −122 dBc/Hz at 14 MHz and −80 dBc/Hz at 1296 MHz for two current transceivers [G]. Amateur 8.97 kHz antennas reported Q ≈ 70 (about 130 Hz bandwidth), 280–460 Ω loss and 11–12 kV corona limits [G]; a 1 % capacitance change then rotates radiated phase by 35° (Q = 70) to 63° (Q = 200) [E], and LF practice retunes day-to-day weather changes with a few per cent of loading inductance [G]. At 5.76 GHz typical system noise is 110 K, a 2 m dish has a 1.8° beam, and GPS-locked, Doppler-corrected microwave EME signals stayed within a few hertz at 1.3–2.3 GHz and within 20 Hz (median) at 24 GHz [G]. The simulator's oscillator presets apply one phase diffusion in degrees per √s at every carrier, whereas the measured references imply per-bit phase wander proportional to the carrier frequency (see the comparison with the presets). Close-in (<100 Hz) phase noise, AGC phase behaviour and audio-stack dropout statistics are not publicly characterised.

Reference keys such as [L2016] point to the reference list at the end, which gives full bibliographic data, access status and grade. Grades: [S] standard, [P] research paper or preprint (peer-review status checked separately), [T] laboratory report, [B] textbook/handbook, [G] grey (manufacturer, amateur, software documentation), [E] this chapter's own derivation (formula and arithmetic shown; the arithmetic was machine-checked).

## Physical mechanisms

### 1. Frequency and phase of references

- **Model.** Fractional frequency y(t) = y0 (static offset) + D·t (ageing/thermal drift) + random part. The random part is conventionally a sum of five power-law processes, Sy(f) = Σ hα f^α for α = −2…+2 below a cut-off fh ([TF538] eq. 6, Table 1; [SP1065] §3.2; [S]/[T]). Phase and time spectra follow from Sφ(f) = (ν0/f)²·Sy(f) and Sx(f) = Sφ(f)/(2πν0)² ([TF538] eqs. 3–5), and the phase-noise quantity quoted by laboratories is L(f) = ½·Sφ(f) ([RV2023] eq. 4, citing IEEE Std 1139).
- **Signature on an Allan-deviation plot.** White/flicker PM: σy ∝ τ^−1; white FM: τ^−1/2; flicker FM: flat "floor"; random-walk FM: τ^+1/2 ([TF538] Table 1; [L2016] Fig. 8).
- **What the modem sees** [E]. (i) A static offset Δf = y0·ν0 gives a coherent-integration loss |sinc(Δf·T)| over T: −3.9 dB at Δf·T = 0.5, a null at Δf·T = 1. (ii) Linear drift D·ν0 (Hz/s) gives quadratic phase π·D·ν0·t². (iii) Random wander gives an rms phase of order φrms ≈ 2π·ν0·τ·σy(τ) after τ (exact for white FM, where σx(τ) = τ·σy(τ); the standard time deviation, TDEV = τ·MDEV/√3 per [SP1065], is somewhat smaller). (iv) A Gaussian phase error of rms σφ reduces the coherent sum by exp(−σφ²/2): −1.1 dB at 0.5 rad, −4.3 dB at 1 rad.
- **Multiplication.** Multiplying a reference by N multiplies phase excursions by N, so Sφ and L(f) rise by 20·log10 N: +55.2 dB from 10 MHz to 5760 MHz, +9.5 dB from 10 to 30 MHz [E; scaling rule as in [RV2023] §III].
- **GPSDO structure.** Below the loop's correction interval the output is the free-running local oscillator; around the loop time constant (100–1000 s in the unit of [L2016] Fig. 7) there is a stability "bump"; beyond it the output inherits GPS and keeps improving with averaging ([L2016] §5; [L2008] §2–3; [N8UR]). Steering can add frequency or phase steps ([L2008] §4).

### 2. Sample clocks and operating-system audio paths

- A sound card's crystal sets both the audio frequency scale and the timing scale. For a radio-based system the audio tone (e.g. 1.5 kHz) is scaled by the sample-clock error; for **direct-audio transmission below 9 kHz the DAC clock is the transmitter's carrier reference and the ADC clock is the receiver's local oscillator**, so the full ppm error appears at the carrier [E].
- Input and output converters do not always share a clock ([DL4YHF-EBN] "Beware" note; [BAUDLINE]), so calibrating the ADC does not calibrate the DAC.
- Operating systems may insert sample-rate conversion: the legacy Windows APIs convert formats automatically, and WASAPI shared mode requires the stream to match the device's user-selected rate ([MS-WASAPI]; [G]). Buffer under/overruns delete or insert samples, i.e. step the phase of every tone and the symbol timing ([SUB9] DF6NM report; [IZ7SLZ]).

### 3. Time of day and latency

Symbol timing against a UTC calendar is offset by: PC clock error; audio-stack buffering; receiver DSP/filter delay; transmitter key-up (attack) delay; and, for network SDRs, transport buffering. These are mostly deterministic biases of 1–200 ms with jitter of a few to tens of milliseconds (values below). They matter at 16.7 ms chips and are negligible at 6 s and 60 s chips [E].

### 4. Linear distortion in the SSB/SDR audio chain

IF crystal or DSP filters impose passband ripple and group delay. A minimum-phase band-pass filter's delay peaks toward the band edges; for an n-pole Butterworth band-pass of bandwidth B the mid-band delay is τ0 = 1/(π·B·sin(π/2n)), i.e. 0.68 ms for n = 8, B = 2.4 kHz and 3.3 ms for B = 500 Hz [E; derived from the pole positions]. Linear-phase DSP filters have constant delay (pure latency) that grows as bandwidth narrows or skirts steepen. A 0.06–225 Hz-wide signal placed mid-passband sees almost no differential delay; the practical effects are the absolute latency and the tone-dependent phase/amplitude if carriers sit near a passband edge.

### 5. Gain control and nonlinear noise processing

AGC multiplies signal-plus-noise by a time-varying gain g(t) set by the strongest in-passband energy (or, in direct-sampling radios, sometimes by wideband energy ahead of the ADC). It preserves instantaneous SNR but (a) amplitude-modulates the wanted carriers at the envelope rate of noise bursts or interferers, (b) breaks the stationarity assumed by integrators and soft metrics, and (c) with fast attack and slow decay lets a single impulse suppress the passband for the decay time ("capture"/"pumping") ([KF6DX]; [SM5BSZ]; [AB4OJ-AGC]; [DCC18]; [G]). Noise blankers gate or interpolate the signal for about a millisecond per impulse and, in analogue form, cross-modulate strong adjacent signals ([AB4OJ-AGC]). Adaptive noise reduction is a nonlinear, signal-dependent filter.

### 6. Receiver noise and dynamic range

Receiver noise figure matters only where external noise is lower (upper HF, quiet sites; 5.76 GHz always). Oscillator phase noise converts a strong signal at offset Δf into in-channel noise of density Pint + L(Δf) (reciprocal mixing) [E]. Third-order intermodulation, blocking and, in direct-sampling SDRs, ADC full-scale clipping by the sum of all signals reaching the converter bound the usable input range ([AB4OJ] §4; [DC4KU]).

**Stage order and calibrated reference planes.** A reproducible hardware scenario must state whether its input is (a) incident field in dBµV/m, with antenna factor/pattern/polarization and effective bandwidth, (b) available power at the antenna connector in dBm or dBm/Hz with impedance and mismatch convention, or (c) digital complex samples in full-scale units. Keep these planes explicit: field/noise/interferers → antenna and mismatch/loss → preselector → pre-LNA passive loss → LNA gain and added noise → RF gain/attenuator and nonlinear blocking/intermodulation → mixer plus LO phase noise/reciprocal mixing → IF/channel response and group delay → AGC/blanker/noise reduction → ADC clipping/quantization → calibrated I/Q samples. Overload and intermodulation may occur in each susceptible LNA, mixer, amplifier or converter before subsequent filtering; a single downstream nonlinear block is not universally equivalent. The exact order is device-specific and must follow a schematic or measurement; this is a declared SDR reference chain, not a claim that every transceiver implements it identically. Apply each noise source at its physical stage and refer reported noise back to the stated plane. Do not add receiver noise at both the antenna and I/Q input, subtract an interference signal before a nonlinear stage that it already overloaded, or use antenna-field measurements as connector power without antenna calibration. A blocker removed by the preselector may be harmless downstream; one removed only in digital processing can still have caused compression, intermodulation, reciprocal mixing or clipping upstream.

Initialize every correlated state explicitly. For colored noise, oscillator wander, AGC and event processes, record the state vector, covariance/cross-spectrum, initialization method, warm-up or stationary-draw method, and random seed. Do not silently start a colored process or AGC at zero and treat the transient as a physical quiet interval. Preserve continuous state across chunks and observation gaps; represent an actual missing observation as invalid data, not zero-valued noise; during a propagation outage with a valid capture, continue to deliver valid receiver noise and interference. Calibrate in the same geometry, gain/filter/AGC settings and estimator used for the comparison. Independent observables should include antenna-port or field spectrum and APD, blocker amplitude/timing and compression products, channel-filter amplitude/group delay, gain-control trajectory, ADC overrange count, sample calibration, and post-stage wanted-signal amplitude/phase. A decoded outcome is a separate end-to-end observation and cannot alone identify which stage caused a loss.

### 7. Transmit chain

ALC and speech compression act on the envelope, so they distort any multi-carrier or keyed-amplitude waveform. PA nonlinearity generates odd-order products; **for three equally spaced carriers the third-order products 2f2−f1, 2f2−f3 and f1+f3−f2 fall exactly on the carriers themselves** [E], so the in-band consequence is co-channel self-interference at the IMD level (not a new spectral line), plus out-of-band splatter. N equal simultaneous carriers have a peak-envelope-to-average ratio of 10·log10 N (4.8 dB for three, 6.0 dB with an equal pilot) [E].

### 8. Sub-9 kHz direct-audio transmit and receive hardware

The transmitting antenna is a small capacitance resonated by a large series inductor: a series RLC with X = 1/(2πfC), Q = X/Rtotal, 3 dB bandwidth f/Q, envelope/group delay Q/(πf), terminal voltage I·X. Radiated field is proportional to antenna current, whose phase relative to the drive is −arctan(2Q·δ) for a fractional resonance shift δ = −½·ΔC/C (or −½·ΔL/L) and whose amplitude falls as 1/√(1+(2Qδ)²) [E]. Anything that changes C (wire sway, sag, rain/ice on insulators, nearby vegetation) or L and R (coil temperature) therefore phase- and amplitude-modulates the radiated carrier. Voltage is limited by corona. Receivers are E-field probes into JFET/op-amp stages or loops into transformer-coupled op-amps ([KC3EEY]); their impairments are power-line harmonics, sferic peaks against converter full-scale, and the sample-clock issues above.

### 9. 5.76 GHz EME hardware

The transverter LO translates a 144/432 MHz (or 28 MHz) IF; its fractional error appears multiplied to 5.76 GHz. Doppler is removed by stepping a synthesiser from computed ephemerides, so residual error has a model part (ephemeris, station coordinates, clock) and an implementation part (step size, update rate, whether the radio accepts changes while transmitting) ([WSJTX]; [VK7MO]; [OK2AQ]). Noise temperature adds (L−1)·290 K for every loss L ahead of the LNA [E]. Pointing error costs gain as about 12·(ε/θ3dB)² dB (Gaussian-beam approximation: 10·log10(e)·4·ln2 = 12.04) [E], and when the beam is narrower than the Moon it also shifts the received spectrum toward one limb's Doppler ([VK7MO]).

## Measured and reported values

### A. Reference oscillators

**A1. Oscillator classes** ([L2002] Table 17.5; [B], NIST author; read).

| Class | σy(1 s) | Noise floor (τ range) | Ageing per year | Offset after warm-up |
|---|---|---|---|---|
| Quartz TCXO | 1e-8 to 1e-9 | 1e-9 (1–100 s) | 5e-7 | 1e-6 |
| Quartz OCXO | 1e-12 | 1e-12 (1–100 s) | 5e-9 | 1e-8 to 1e-10 |
| Rubidium | 5e-11 to 5e-12 | 1e-12 (1e3–1e5 s) | 1e-10 | 5e-10 to 5e-12 |
| Caesium beam | 5e-11 to 5e-12 | 1e-14 (1e5–1e7 s) | none | 5e-12 to 1e-14 |

The same source notes that even the best quartz oscillators are hard to hold within 1e-10 without adjustment. [L2008] Table 1 gives, for calibration-laboratory devices: rubidium 5e-9 to 5e-12 (1-day offset), 5e-12 at 1 day, ageing <1e-10 to 5e-10 per year, phase noise −90 to −130 dBc/Hz at 10 Hz; GPSDO 1e-12 to 5e-14 (1-day offset), 1e-10 to 1e-12 at 1 s, 8e-13 to 5e-14 at 1 day, −90 to −140 dBc/Hz at 10 Hz ([P]; values from specification sheets and NIST measurements).

Vig's tutorial ([VIG] ch. 7, [T]; read) gives, for wide-temperature-range devices, a TCXO accuracy of 2e-6 per year including environmental effects, ageing 5e-7 per year, temperature stability 5e-7 over −55…+85 °C and σy(1 s) of 1e-9; for an OCXO the same quantities are 1e-8, 5e-9, 1e-9 and 1e-12. Its chart of short-term stability ranges (slide 7-5, read by eye) puts quartz between about 3e-13 and 3e-11 from 1 s to 100 s, rising roughly as τ^½ to about 1e-12–1e-10 at 10³ s, 3e-12–3e-10 at 10⁴ s and 1e-11–1e-9 at 10⁵ s. The band evidently represents oven-class quartz: TCXOs, at 1e-9 for 1 s in the same tutorial's table, lie above it.

**A1a. Measured free-running quartz beyond 100 s** ([G]; values read by eye from the published plots, about ±20 %).

| Device and conditions | 10 s | 100 s | 10³ s | 10⁴ s | Source |
|---|---|---|---|---|---|
| Crystek CHOX20 TCXO (10 MHz), 12 h run begun about 30 min after power-up, in a room whose temperature changed by no more than a couple of degrees; temperature coefficient about 1.6e-8 per °C at room temperature, 2.2e-8 per °C in a hot-air test | 3e-10 | 5e-10 | 2e-9 | 1e-8 | [N8UR-EXC] (plot dated 2009) |
| Precision quartz TCXO (±100 ppb), still air; under light airflow its ADEV rose to about 2e-9 near 2 s (vendor: "up to 38 times" degradation) | 3e-11 | 3.5e-11 | 2e-10 | — | [SITIME] 2021, vendor paper |
| Inexpensive surplus OCXO (Isotemp OCXO130-100), 3 days against a rubidium standard | 1e-11 | 9e-12 | 8.5e-12 | 1.6e-11 (6e-11 at 6e4 s) | [N8UR] part 3, 2022 |
| High-grade OCXO (Oscilloquartz 8607), 14 days against a hydrogen maser | 1.3e-13 | 1e-13 | 2e-13 | 4.5e-13 (1.8e-12 at 1e5 s) | [N8UR] part 3, 2022 |
| GPS timing receiver PPS (u-blox NEO-M8T) against caesium, 3 days; slope τ^−1 | 1e-9 | 1e-10 | 1e-11 | 1e-12 | [N8UR] part 3, 2022 |

The inexpensive OCXO and the GPS receiver cross near 1300 s; [L2016] §4 describes applying a properly designed GPSDO's corrections at intervals shorter than the period after which the uncorrected oscillator becomes less stable than GPS. The measured TCXO is 5–10 times better at 10³–10⁴ s than the assumed TCXO row of table A6, in a stable room.

**A2. GPSDOs measured against UTC(NIST)** ([L2016] Table 2; overlapping ADEV; ten units from ten manufacturers; [P]; read).

| Unit (LO) | 1 s | 1 min | 1 h | 1 day |
|---|---|---|---|---|
| Best quartz (HPZ) | 1e-12 | 2e-12 | 1e-12 | 4e-13 |
| Worst short-term quartz (BR8) | 5e-10 | 9e-12 | 4e-12 | 1e-13 |
| Rubidium examples (ERT, TS6) | 6e-12 | 1e-12, 8e-13 | 6e-13, 6e-12 | 0.7e-13, 4e-13 |
| Range over all ten | 1e-12 to 5e-10 | 8e-13 to 1e-11 | 3e-13 to 6e-12 | 0.5e-13 to 7e-13 |

Supporting statements: every unit reached at least 1e-11 at 1 min and 6e-12 at 1 h ([L2016] §5); seven GPSDOs spanned 6e-14 to 7e-13 at one day, and two rubidium-based units on one antenna differed tenfold (38 ns vs 588 ns peak-to-peak phase over 80 days) ([L2008] Figs. 8–10). One-week holdover: frequency accuracy 3e-12 to 1e-9, time offset <3 µs to 637 µs ([L2008] Table 2). One-day time uncertainty of a GPS-disciplined clock (k = 2): 8 ns best, 60 ns typical, 1005 ns worst, dominated by uncalibrated antenna-cable delay and antenna coordinates ([L2016] Table 4).

**A3. Hobby-grade GPSDOs (2015–2026 grey data).**

| Observation | Value | Conditions | Source, grade |
|---|---|---|---|
| Mean frequency offset over 5 days vs hydrogen maser | BG7TBL "2019-08-03": −9.62e-11; Leo Bodnar two-output: −1.55e-11; HP Z3805A: +2.8e-15 | units warmed 24 h; maser accurate to about 5e-14 | [N8UR] 2022, [G] |
| Short-term ranking | BG7TBL (OCXO) better than Bodnar (TCXO) below about 25 s; BG7TBL hump "over an order of magnitude worse" in the loop-transition region | same run; plot values not tabulated in the post | [N8UR], [G] |
| Offset by hardware version | "2017-10-09" and "2019-08-03" BG7TBL versions both offset; "2020-01-10" version about 4e-13; the post suspects a frequency-lock rather than phase-lock loop in the older units | three units of one nominal model | [N8UR] 2022, [G] |
| Locked GPSDO (Trimble Thunderbolt), mailing-list description | better than 1e-10 99 % of the time and 1e-11 90 % of the time, checked with a 1–10 s counter gate | not a measurement report | [TIMENUTS] 2015, [G] |
| 24 h statistics of six units under 200 USD | σ of 10 MHz readings 293–2808 µHz (2.9e-11 to 2.8e-10); max−min 2.6–50.9 mHz (2.6e-10 to 5.1e-9) | Keysight 53220A, lab at about 20 °C, indoor antenna; reference partly in holdover, so the author did not use the ADEV figures | [REEVE] 2023, [G] |
| Manufacturer phase noise, TCXO-based GPS clock, 10 MHz output | −70/−100/−125/−143/−150 dBc/Hz at 1/10/100/1 k/10 k Hz; long-term "theoretically approaches 1e-12" | "equal or better than" | [BODNAR], [G] |

**A4. Equipment-level frequency accuracy actually specified or observed.**

| Item | Value | Source, grade |
|---|---|---|
| HF radio carrier accuracy requirement | within ±30 Hz (tactical) or ±10 Hz (others) over not less than 30 days, 2–29.9999 MHz; tuning increments 100 Hz max (objective 10 Hz) single-channel, 10 Hz (objective 1 Hz) multichannel | [MS141B] §5.2.2–5.2.3, [S] |
| Short-term phase requirement | probability <1 % that phase changes by more than 5° between two successive 10 ms (or 13.33 ms) periods | [MS141B] §5.2.4, [S] |
| ITU transmitter tolerances "achievable now" | fixed 9–50 kHz: 100 ppm; 1.6065–4 MHz fixed SSB 20 Hz, FSK/data 10 Hz; 4–29.7 MHz fixed 10 ppm, SSB 20 Hz, telegraphy/data 10 Hz | [SM1045] Table 1, [S] |
| Modem acceptance tests named "Doppler shift" and "Doppler sweep" | ±75 Hz static, and a 3.5 Hz/s triangular sweep between ±75 Hz | [MS110C] App. C §C.6.3–C.6.4, App. D §D.6.4–D.6.5, [S] |
| Manufacturer frequency-stability specifications, HF/VHF transceivers | IC-7300: less than ±0.5 ppm (−10…+60 °C); IC-7100: ±0.5 ppm (0–50 °C, after 5 min warm-up); IC-7410: ±0.5 ppm (0–50 °C) | [ICOM-7300], [G]; [RIGPIX] listings, [G] |
| Stated requirement for the longest LF/MF mode of a weak-signal suite | transmitter and receiver frequency stability better than 0.089 Hz over an 1800 s transmission (the 0.089 Bd keying rate), plus Doppler spread below 0.089 Hz | [WSJTX] §6.5, [G] |
| Amateur HF radio calibrated against WWV (worked example) | 24.6 Hz at 20 MHz = 1.23 ppm | [WSJTX] §14.1, [G] |
| VHF/UHF IF transceiver (IC-9700, no external reference), 15 s on/15 s off at full power from cold | drift read by eye from the plotted curves of Fig. A: about 34 Hz at 1296 MHz, 15 Hz at 432 MHz and 4 Hz at 144 MHz (2.6–3.5e-8 on every band), reached within about 10 min; "negligible" thereafter; within one transmission 2 Hz during warm-up, 1 Hz or less afterwards (1296 MHz) | [ARRL-9700] Lab Notes, 2020, [G] |
| 5.7 GHz transverter (SG Lab v1.4) | LO accuracy ±0.5 ppm at 20 °C; ±0.5 ppm over −20…+70 °C; 10 MHz reference input | [SGLAB] 2025, [G] |
| SDR with stock 66.666 MHz clock on a heater | initial drift about 30 ppb (2.5 mHz at 77.5 kHz); drift reappears when room heating switches on | [DL4YHF-EBN] 2015, [G] |
| "Good free-running OCXO" | under ±2 ppb per day ageing | [DL4YHF-EBN], [G] |
| Professional VLF broadcast transmitters, carrier offset inferred from phase slope at a GPS-timed receiver, Nov 2023–Dec 2024 | GQD 22.1 kHz: 0; NAA 24.0 kHz: 2.86e-5 Hz (1.2e-9); NRK 37.5 kHz: 1.41e-4 Hz (3.8e-9); NSY 45.9 kHz: 2.07e-4 Hz (4.5e-9); values change with operator adjustments | [T2026] Table 1 and §2 (preprint), [P, not yet peer-reviewed] |

**A5. Sound-card and USB-audio sample clocks** (all [G] unless marked; no peer-reviewed measurement study was obtained).

| Observation | Value | Source |
|---|---|---|
| Typical absolute error | "mainstream audio cards" 50–100 ppm; PC clocks 40+ ppm; one card (ENS1371) under 5 ppm except +11.8/+24.5 ppm at 22.05/44.1 kHz input | [BAUDLINE] 2005 |
| Audio clocks of 21 consumer devices measured against a GPS PPS (the "audio" rows of the table; Spectrum Lab) | −98 to +417 ppm, median +7 ppm, 14 of 21 within ±25 ppm; dedicated recorders and an interface: Edirol UA-5 −3.2, M-Audio Microtrack −40.4, Sony PCM-M10 +8.6, Zoom R16 +23.5 ppm; one laptop −98 ppm; the "video" row of one tablet −554 ppm; ADC and DAC usually share a clock, but not in every device | [PROTYPOSIS] (undated table; it refers to a paper "An Analysis of Time Drift in Hand-Held Recording Devices", not obtained) |
| Example sample rates in the manual of a VLF toolkit | a 96 kHz card running at 96011.47 S/s (+119 ppm against an NTP-disciplined system clock); a 192 kHz card at 192003.39 S/s (+17.7 ppm against a GPS PPS) [E, from the logged rates] | [VLFRX] §Soundcards, §Timing system setup |
| Sampling-rate offsets in acoustic-sensor-network research | smartphones −500 to +400 ppm; several ppm change within a few minutes of power-up or wake-up; models used ±100 ppm offsets with 1.25 ppm steady-state wander | [GSH2021] §1, §3, [P] (arXiv version); its offset and warm-up figures are secondary for [PROTYPOSIS] and [DL4YHF-CAL] |
| Temperature coefficient (mailing-list estimate) | 0.1–1 ppm/°C, "most likely" 0.5 ppm/°C; a room moving 1–3 °C per hour then gives a wobble of about 1 ppm with a 30–90 min period | [TIMENUTS] (B. Camp, 2015) |
| Notebook internal codec, forced cooling | 1.5 ppm shift within 30 min (15 mHz at 10 kHz) | [DL4YHF-CAL] |
| USB interface (E-MU 0202) from cold | initial rate more than 2 ppm from final value; smoother than internal card | [DL4YHF-CAL] appendix |
| After warm-up, normal room | under 1 ppm drift; ±0.5 ppm plot scale adequate; 5 ppm search window "sufficient" indoors | [DL4YHF-CAL] |
| PPS-based rate measurement noise | about 30 ns per 1 s interval standard deviation (3e-8 per second, averaging down), Garmin GPS 18x + E-MU 0202 at 192 kS/s | [DL4YHF-CAL] |
| ADC and DAC clocks | not guaranteed to be the same source; some cards "very far off" nominal | [DL4YHF-EBN]; [DL4YHF-CAL]; [BAUDLINE] |
| Practice in VLF software | PPS into a sound-card channel for sample-rate and timestamp calibration; NTP/PPS for coarse time; continuous rate tracking "due to PC temperature" | [KC3EEY]; [IZ7SLZ]; [DL4YHF-CAL] |
| PPS sample-clock correction in vlfrx-tools (`vttime`) | the PPS, slowed by an RC network over a few hundred samples, occupies one audio channel; its centroid locates each second mark to a small fraction of a sample, and the stream is resampled to an exact, UT-synchronous rate; timing jitter "below 50 nS", often better than 30 nS; timestamps "better than 100 nS" with a good PPS; input timestamps must be within 0.5 s or the second is ambiguous; without PPS, timestamps from an ntpd-disciplined system clock are about ±50 ms; the absolute offset of the PPS centroid is calibrated "to about the nearest sample" | [VLFRX] §Signal streams, §vttime, §Timestamps and alignment, §Timing system setup |
| Stated requirement for a 6.75 h coherent message at 8822 Hz | ≤15° phase error → 4.72 µs → 1.94e-10 (1.7 µHz) | [DL4YHF-EBN] §2 |
| Receiver LO for 137 kHz coherent reception | "stable and known to better than 0.1 mHz" (7e-10) | [IZ7SLZ] |

**A6. Derived absolute errors and phase wander** [E].

Absolute offset y·ν0:

| y | 8 kHz | 1 MHz | 30 MHz | 5.76 GHz |
|---|---|---|---|---|
| 1e-4 (100 ppm) | 0.8 Hz | 100 Hz | 3 kHz | 576 kHz |
| 2e-5 (20 ppm) | 0.16 Hz | 20 Hz | 600 Hz | 115 kHz |
| 5e-7 (0.5 ppm) | 4 mHz | 0.5 Hz | 15 Hz | 2.88 kHz |
| 1e-8 | 80 µHz | 10 mHz | 0.3 Hz | 57.6 Hz |
| 1e-9 | 8 µHz | 1 mHz | 30 mHz | 5.76 Hz |
| 1e-10 | 0.8 µHz | 0.1 mHz | 3 mHz | 0.576 Hz |
| 1e-11 | 0.08 µHz | 10 µHz | 0.3 mHz | 0.058 Hz |

Time τc at which φrms = 2π·ν0·τ·σy reaches 1 rad for a flat σy (flicker floor): τc = 1/(2π·ν0·σy).

| σy floor | 8 kHz | 1 MHz | 30 MHz | 5.76 GHz |
|---|---|---|---|---|
| 1e-9 (TCXO class) | 2.0e4 s | 159 s | 5.3 s | 0.028 s |
| 1e-10 | 2.0e5 s | 1590 s | 53 s | 0.28 s |
| 1e-11 (GPSDO at 1 min, upper bound of [L2016]) | 2.0e6 s | 1.6e4 s | 531 s | 2.8 s |
| 1e-12 (OCXO/Rb/best GPSDO) | 2.0e7 s | 1.6e5 s | 5300 s | 28 s |

Accumulated rms phase 2π·ν0·τ·σy(τ) in radians **at 1 MHz** (multiply by 0.008 for 8 kHz, 30 for 30 MHz, 5760 for 5.76 GHz); starred σy values lie outside the cited tables and are assumptions:

| Class (σy at 10 s, 100 s, 10³ s, 10⁴ s) | 10 s | 100 s | 10³ s | 10⁴ s |
|---|---|---|---|---|
| TCXO (1e-9, 1e-9, 1e-8*, 1e-7*) | 0.063 | 0.63 | 63 | 6300 |
| OCXO (1e-12, 1e-12, 3e-12*, 1e-11*) | 6.3e-5 | 6.3e-4 | 0.019 | 0.63 |
| Good GPSDO (1e-12, 2e-12, 1e-12, 1e-12; "HPZ"-like) | 6.3e-5 | 1.3e-3 | 6.3e-3 | 0.063 |
| Poor GPSDO (1e-10, 1e-11, 5e-12, 5e-12; "BR8"-like) | 6.3e-3 | 6.3e-3 | 0.031 | 0.31 |
| Measured TCXO (3e-10, 5e-10, 2e-9, 1e-8; table A1a) | 0.019 | 0.31 | 13 | 630 |
| Measured precision TCXO, still air (3e-11, 3.5e-11, 2e-10, —) | 1.9e-3 | 0.022 | 1.3 | — |
| Measured inexpensive OCXO (1e-11, 9e-12, 8.5e-12, 1.6e-11) | 6.3e-4 | 5.7e-3 | 0.053 | 1.0 |
| Measured high-grade OCXO (1.3e-13, 1e-13, 2e-13, 4.5e-13) | 8.2e-6 | 6.3e-5 | 1.3e-3 | 0.028 |

The measured rows (table A1a) bracket the assumed TCXO and OCXO rows: the measured TCXO in a stable room is 5–10 times better than the starred TCXO values at 10³–10⁴ s, the inexpensive OCXO is about 3 times worse than the starred OCXO value at 10³ s and close to it at 10⁴ s, and the high-grade OCXO is 15–22 times better.

A locked GPSDO's phase is also bounded through its time error x: φ = 2π·ν0·x. For x = 5 ns (the worst-case time-stability term in [L2016] Table 4) this is 2.5e-4 rad at 8 kHz, 0.03 rad at 1 MHz, 0.94 rad at 30 MHz and 181 rad at 5.76 GHz; the 38–588 ns peak-to-peak wander of [L2008] Fig. 9 scales likewise. Hence carrier-phase coherence over hours is plausible at 8 kHz and 1 MHz with disciplined references, marginal at 30 MHz, and not available at 5.76 GHz from the reference alone.

Worked examples: (a) the 1.5 ppm-in-30-min sound-card drift of [DL4YHF-CAL] is D = 8.3e-10 /s; at 8 kHz the phase error π·D·ν0·t² is 0.08 rad after 60 s, 7.5 rad after 600 s and about 700 rad after 96 min, so a free-running sound card cannot support multi-chip coherence at 60 s chips although one chip is safe. (b) Tolerance to an unsearched static offset for chip-coherent detection is Δf ≪ 1/(2·Tchip): 8 mHz (1e-6 of 8 kHz) for 60 s chips, 83 mHz (8e-8 of 1 MHz) for 6 s chips, 30 Hz (1e-6 of 30 MHz, 5e-9 of 5.76 GHz) for 16.7 ms chips; full-bit coherence tightens these to 87 µHz (1.1e-8 of 8 kHz) for the 96 min bit, 0.87 mHz (8.7e-10 of 1 MHz) for the 576 s bit, 43 mHz (1.4e-9 of 30 MHz) for the 11.6 s bit of rows 4, 5 and 7 and 1.9 mHz (6.5e-11) for the 257 s bit of row 6, and 19 mHz (3.3e-12 of 5.76 GHz) for the 26.4 s bit of row 8. (c) The [BODNAR] phase-noise figures multiplied to 5.76 GHz (+55.2 dB) give −15/−45/−70/−88/−95 dBc/Hz at 1/10/100/1 k/10 k Hz and an integrated 1 Hz–10 kHz phase of 0.18 rad (10°) rms, dominated by 1–10 Hz; this ignores the transverter PLL's own noise, for which no datasheet could be retrieved.

### B. Time of day and latency

| Item | Value | Conditions | Source, grade |
|---|---|---|---|
| NTP over the public Internet | "about one millisecond" typical limit from path asymmetry | NIST summary statement | [NL2015] abstract, [P] |
| NTP over LAN | 1σ under 10 µs (crossover cable), under 15 µs (one hop), about 20 µs (two hops); round-trip delay briefly above 10 ms on routed path | 20 days, client with clock board and tuned BIOS | [NL2015] Table II, [P] |
| Network NTP, "typical accuracy" | about ±30 ms; RFC 5905 "a few tens of milliseconds"; worse on cable modems/asymmetric routes | practitioner table | [GPSD] Table 2, [G] |
| NTPv4 design statements | primary servers precise within "a few tens of microseconds"; secondary servers and clients on fast LANs within "a few hundred microseconds" (polls up to 1024 s); servers and clients "within a few tens of milliseconds" with polls up to 36 h; the synchronisation distance, root dispersion plus half the root (round-trip) delay, "represents the maximum error due to all causes" | protocol specification | [RFC5905] §1, §4, [S] |
| Windows Time service, Windows 10/Server 2016 and later | 1 s, 50 ms or 1 ms "under the right operating conditions": 1 s needs at most 100 ms cumulative one-way latency to the source; 50 ms at most stratum 5 and six hops; 1 ms under 0.1 ms latency to the time source and at most four hops | vendor support boundary | [MS-W32TIME], [G] |
| Sound-card stream timestamps from an ntpd-disciplined clock | about ±50 ms | VLF toolkit, without PPS | [VLFRX] §Timestamps and alignment, [G] |
| GPS NMEA sentences without PPS | not much better than 1 s; 170 ms wobble (SiRF); delay stable to 10 ms for hours then jumps 200 ms | serial/USB receivers | [GPSD], [G] |
| NMEA timestamps one whole second late on some receivers | integer-second ambiguity needing a manual option | Spectrum Lab, noticed 2015 | [DL4YHF-CAL], [G] |
| PPS on a PC | ±1 µs kernel PPS; ±5 µs user-space with about 20 µs jitter; USB 1.1 ±1 ms; USB 2.0 ±100 µs | 2 GHz-class x86 | [GPSD], [G] |
| Software requirement of a widely used weak-signal suite | clock within ±1 s; must be monotonic and "smoothly continuous", no step corrections; default Windows time service "usually not adequate" | all modes of that suite | [WSJTX] §2–3, [G] |
| Receive latency, antenna to audio | IC-7300: 3.2–3.7 ms SSB, 8 ms (500 Hz), 14–17 ms (250 Hz) | pulse method | [AB4OJ] Table 17, [G] |
| Receive latency, other radios | Grape-1 low-IF 0.0 ms; IC-7000 2.0 ms; IC-7300 2.7 ms; IC-7610 4.0 ms; IC-705 15.0 ms; KiwiSDR 2024 ms ± 50 ms run-to-run | 1 kHz burst on 10 MHz AM carrier, PPS-triggered | [N8OBJ] 2022, [G] |
| "Receive processing delay" in ARRL Lab tables | IC-9700 8–10 ms; IC-705 15 ms; FT-710 25 ms; FLEX-6400M 165 ms (default settings; the review states that turnaround time depends on a filter-sharpness option) | SSB | [ARRL-9700], [ARRL-705], [ARRL-710], [ARRL-6400], [G] |
| Receive-to-transmit delay ("TX delay", SSB) | IC-7300 14 ms (search snippet); FT-710 21 ms; FLEX-6400M 47 ms; IC-9700 70 ms; IC-705 79 ms | | ARRL Lab tables, [G] |
| Transmitter attack/release requirement | RF to 90 % within 25 ms of keying (objective 10 ms); to 10 % within 10 ms of unkeying | | [MS141B] §5.3.5, [S] |
| End-to-end absolute delay requirement | ≤10 ms (objective 5 ms) over 300–3050 Hz, back-to-back | several of the DSP transceivers in the rows above exceed it | [MS141B] §5.2.8, [S] |

Sample-clock error also slews symbol timing: 100 ppm is 0.58 s over a 96 min bit (1 % of a 60 s chip) and 2.6 ms over 26 s (0.16 of a 16.7 ms chip) [E].

### C. MF/HF receive chain

**C1. Passband amplitude and delay.** [MS141B] §5.2.7.1: amplitude within 3 dB total from f0+300 to f0+3050 Hz; group delay not to vary by more than 1.0 ms over 80 % of the passband (575–2775 Hz), measured transmitter audio input to receiver audio output. §5.2.7.2 (four-channel ISB equipment): within 2 dB (objective 1 dB) 250–3100 Hz; group-delay distortion ≤1500 µs at 370–750 Hz and 3000–3100 Hz, ≤1000 µs at 750–3000 Hz, ≤150 µs across any 100 Hz between 570 and 3000 Hz [S]. Measured −6 dB audio passbands of current radios in SSB: 330–2882 Hz (IC-705, 2.4 kHz), 108–2894 Hz (FT-710, 3 kHz), 90–2500 Hz (FLEX-6400M, 2.4 kHz) ([G], ARRL Lab), so the low-frequency edge differs by more than 200 Hz between models. A forum recollection of manufacturer graphs for a 500 Hz 8-pole crystal filter gives about 2 ms mid-band rising to 4 ms near the edge ([G], search snippet only), the same order as the 3.3 ms mid-band Butterworth estimate above. One weak-signal suite ships a tool that measures the receiver's phase response from decoded MSK144 frames and plots the resulting group-delay variation "in ms"; its documented example improves the eye opening from 1.2 to 1.6 on a ±2 scale, and it states that linear-phase software-defined receivers need no such equalisation ([WSJTX] §14.3, [G]). [MS110C] §C.7/D.7 states that radio filters "will have a significant impact" on QAM modem performance, and its embedded-modem tests add 3 dB to the required SNR to allow for filters, TGC and AGC (§D.6; App. C adds 2 dB, 3 dB at 9600 bit/s and above, and one more dB for mismatched makes) [S]. No measured group-delay curves for current amateur transceivers were found.

**C2. AGC.** [MS141B] §5.4.3: audio output constant within 3 dB for RF input from −103 dBm to +13 dBm (fixed) or 0 dBm (tactical); non-data attack ≤30 ms, release 800–1200 ms; data service attack ≤10 ms, release ≤25 ms [S]. [MS110C] recommends the slow ("nondata") AGC setting for its QAM waveforms and a pre-key delay to let turn-on transients settle [S]. Amateur measurements ([G]): AGC threshold 0.1–31 µV across the Sherwood table ([SH]); IC-7300 threshold −99 dBm preamp off, −106/−109 dBm with preamps ([AB4OJ] Table 8); recovery about 100 ms without clamping after 12.5–100 ns, 16 V-peak pulses at 600 ms spacing with AGC "fast" (0.1 s) ([AB4OJ] Table 11); a DSP transceiver's decay about 5 dB/s in SLOW and "many hundreds of dB/s" in FAST, threshold programmable 0.35–191 µV, default about 3 µV ([KF6DX]). With a steady in-passband carrier about 1 kHz from two WSPR signals in a 2.8 kHz passband, decodes were unaffected up to −20 dBm and lost 3–4 dB at −15 dBm (10 dB less at the radio, S9+25 dB indicated with its 20 dB attenuator in), a range the author puts at about 75 dB; the same paper warns that impulse noise "capturing AGC" is a design flaw of most DSP radios and worse for short sequences ([DCC18]). [SM5BSZ] argues that for weak-signal work the optimum is AGC off with fixed gain about 12 dB below audio saturation.

**C3. Noise blankers and noise reduction** ([G]). DSP blankers use a delay line of about 1 ms and shaped gating; analogue gating modulates strong neighbours ([AB4OJ-AGC]). With the IC-7300 blanker on, the pulses above produced no AGC deflection ([AB4OJ] test 7). Measured "noise reduction": 12 dB (IC-705), 11.5 dB at S7 (FT-710), 12 dB (FLEX-6400M), 13 dB SINAD change from a 6 dB SINAD tone (IC-7300) — all for tones at or above the noise in an SSB bandwidth; engaging NR alters the effective AGC threshold ([KF6DX]). Nothing found on NR/NB behaviour for signals far below the noise.

**C4. Noise figure and sensitivity** ([G] unless marked). ARRL Lab noise figures at 14 MHz, preamp off/1/2: IC-705 18/9/6 dB; FT-710 20/11/5 dB; FLEX-6400M 33/18/9 dB (0/16/32 dB preamp). Sherwood noise floors range from −108 dBm (direct-sampling, no preamp) to −146 dBm ([SH]). Requirement: 10 dB SINAD for −111 dBm (objective −121 dBm) in 3 kHz ([MS141B] §5.4.1.7, [S]). Whether these exceed external noise must be judged against the ITU-R P.372 values of the noise chapter ([50](50-noise-and-interference-mf-hf.md)); the comparison is closest near 30 MHz with preamplifiers off.

**C5. Phase noise and reciprocal mixing** ([G]).

| Quantity | Best | Typical modern | Poor/legacy | Source |
|---|---|---|---|---|
| LO noise at 10 kHz offset | −155 dBc/Hz (TS-890S), −154 (FTdx-101D) | −137 to −150 | −99 to −115 (e.g. KWM-380 −99, FT-One −99, TS-430S −102, R-2000 −105, G90 −113) | [SH], table updated 1 Aug 2026 |
| Phase noise at 1/2/5/10/20 kHz, IC-7300 | | −127/−136/−142/−144/−146 dBc/Hz | | [AB4OJ] Table 6 |
| RMDR at 2 kHz (500 Hz bandwidth) | 118 dB (FLEX-6400M, 14 MHz) | 110 dB (IC-705, 14 MHz); 111–116 dB (FT-710 at 50, 3.5 and 14 MHz) | 45–75 dB narrow-spaced dynamic range for older sets | ARRL Lab; [SH] |
| Transmit composite noise at 10 kHz | −147 dBc/Hz (FT-710), −141 (K3S) | −128 to −138 (IC-7610, IC-705, FLEX-6400M −129) | −120 (FTdx-3000) | ARRL Lab; [NC0B21] |

Conversion used by both laboratories: phase noise = −(RMDR + 27 dB) for a 500 Hz bandwidth ([SH]; [AB4OJ]). Standard masks exist ([MS141B] Figs. 4–5) but their break-points could not be recovered from the text layer. **The receiver tables stop at 1 kHz offset.** ARRL Lab transmit phase-noise plots begin at 100 Hz; read by eye, the FT-710 is about −122 dBc/Hz at 100 Hz and −136 dBc/Hz at 1 kHz (14 MHz, 100 W), and the IC-9700 about −100, −91 and −80 dBc/Hz at 100 Hz and −114, −104 and −93 dBc/Hz at 1 kHz on 144, 432 and 1296 MHz ([ARRL-710] Fig. C; [ARRL-9700] Fig. 6; [G]). The IC-9700's band-to-band steps match the 20·log10 N rule (+9.5 and +19.1 dB for ×3 and ×9) [E]. Nothing below 100 Hz, where a 0.06–225 Hz-wide signal is most sensitive, was found. Example [E]: a −33 dBm signal 2 kHz away adds noise of −169 dBm/Hz with −136 dBc/Hz (below an 18 dB-NF receiver's −156 dBm/Hz) but −133 dBm/Hz with −100 dBc/Hz, 23 dB above it.

**C6. Blocking, intermodulation, ADC clipping, spurs** ([G] unless marked). Narrow-spaced third-order dynamic range 45–110 dB (2 kHz spacing; the lowest entries, for older sets, at 5 kHz), wide-spaced up to 111 dB, blocking 49 to >151 dB ([SH]); "100 dB is the middle of the pack" today versus mid-70s 15–20 years earlier ([NC0B21]). Direct-sampling ADC clip (overflow) level, IC-7300: −10 dBm preamp off and with dither, −23 dBm and −27 dBm with preamps ([AB4OJ] Table 3); −8/−24/−28 dBm and +10 dBm with attenuator on another sample ([DC4KU] Table 3), who notes overload is usually the sum of all signals rather than one and judges a brief overload harmless to reception, whereas [AB4OJ] §4 states that a clipping ADC "can no longer process signals". Noise power ratio 72–78 dB over 1.9–16.4 MHz ([AB4OJ] Table 16), 77 dB ([DC4KU]). Dither ("IP+") cost 10–13 dB of MDS on that model ([AB4OJ] Table 2 note). Aliasing rejection 91 dB at one test frequency; receiver "birdies" mainly 0.5–3.6 MHz ([AB4OJ]). Requirements: internally generated spurs ≤−112 dBm on 99 % of channels; image/IF rejection ≥70–80 dB; desensitisation by a signal 5 % off-frequency ≤1 dB at +90/+100 dB; in-channel two-tone IMD (110 Hz spacing) ≥35 dB down ([MS141B] §5.4.1–5.4.2, [S]).

### D. MF/HF transmit chain

| Item | Value | Conditions | Source, grade |
|---|---|---|---|
| Two-tone IMD3 requirement | ≥30 dB (fixed) or ≥24 dB (tactical) below either tone at rated PEP | tones ≥300 Hz apart | [MS141B] §5.3.1.2, [S] |
| Acceptable IMD level | −25 dB or less for single-channel SSB telephony; −35 dB or less for multichannel telegraph/ISB/privacy classes | relative to one of two equal tones | [SM326] §1.2.3, [S] |
| 100 W transceiver, 13.8 V PA, IMD3/5/7/9 relative to one tone | −40/−35/−44/−57 dBc (3.6 MHz); −39/−34/−43/−56 (14.1); −33/−33/−44/−54 (28.1); −25/−30/−34/−46 (50.1) | IC-7300, 100 W PEP, tones 700 and 1700 Hz; add −6 dB if referred to PEP | [AB4OJ] Table 21, [G] |
| ARRL Lab IMD3 below PEP | IC-705 −40 typical, −34 worst (10 W); FT-710 −32 to −38 (100 W), −34 to −40 at 50 W; FLEX-6400M −41 typical | | [G] |
| Reference points quoted by one tester | Collins 32S-3 −36 dBc; TS-990S −34 dBc; Elecraft K3 at 100 W −27 dBc (caption attribution inferred from slide order); FT-1000 Mk V class A at 75 W −42 dB | 20 m | [NC0B21], [G] |
| Solid-state linear amplifiers, IMD3/5/7/9 below PEP | ACOM 500S at 500 W: −35/−39/−62/−58 dB; RM Italy BLA600: −30/−36/−53/−59 dB; several 1.5 kW units about −30 dB | 14 MHz | [ARRL-500S] 2024; [ARRL-6400] 2019; [NC0B21], [G] |
| ALC characteristic required | above threshold, +20 dB audio gives under +1 dB RF output | i.e. a hard leveller | [MS141B] §5.3.4, [S] |
| ALC behaviour observed | no overshoot with white noise or speech; no flat-topping of a two-tone envelope at 100 W PEP | IC-7300 via USB audio | [AB4OJ] tests 19–21, [G] |
| Drive-setting advice | reduce audio until RF output "falls slightly"; tone must be free of clicks/glitches while the PC does other work | | [WSJTX] §5 "Transmitter Audio Level", [G] |
| In-band transmitter noise required | ≥75 dBc (fixed) or ≥65 dBc (tactical) below PEP in 1 Hz; carrier suppression ≥50/40 dBc | | [MS141B] §5.3.1.1, §5.3.3, [S] |
| Thermal behaviour, continuous carrier | amplifier heatsink 27 → 42 °C (3 min) → 55 °C (10 min key-down), back to 28 °C after 10 min; another amplifier rated for continuous 500 W; an accessory tuner derated from 120 W to 30 W for 100 % duty modes; a 2 W 5.7 GHz transverter sagging to 1.85 W in 10–15 s | | [ARRL-6400]; [ARRL-500S]; [ARRL-9700] (tuner review); [SGLAB], [G] |
| Synthesiser step | 1 Hz CAT steps assumed by Doppler control; some radios 10 Hz; many do not accept frequency changes while transmitting | | [WSJTX] §9.1; [VK7MO], [G] |

Hum, ground-loop and audio-interface data: nothing quantitative found beyond a reviewer's remark on hum pickup in powered speakers ([ARRL-6400]). Quantisation [E]: an ideal 16-bit converter at 48 kS/s has a noise density of −141.9 dBFS/Hz (98.1 dB over the Nyquist band); a 3 kHz noise band recorded at −30 dBFS rms is 77 dB above it, at −80 dBFS still 27 dB above. The concern is not SNR but dither: if total input noise falls below roughly half an LSB rms (−93 dBFS for 16 bits, −45 dBFS for 8 bits), quantisation ceases to be noise-like and a sub-LSB tone is distorted or lost (classical quantisation theory; [B], recollection, not re-verified).

### E. Sub-9 kHz direct-audio systems

**E1. Transmit side** (amateur reports, [G], unless marked).

| Item | Value | Source |
|---|---|---|
| Kite antenna, 8970 Hz, 180 m insulated wire, effective height about 50 m | C ≈ 900 pF; total circuit resistance about 280 Ω (coil 140 Ω), about twice that at another site with only short ground pegs; Rrad 3.5 mΩ; 0.6 A (about 100 W) from a class-B car-audio amplifier via a ferrite transformer → 1.3 mW EMRP | [SUB9], DF6NM, 2011 |
| Voltage limits | corona on 0.4 mm enamelled wire at about 12 kV; corona in a multilayer coil at 0.55 A (11 kV), layer-to-layer limit about 3.7 kV | [SUB9], DF6NM |
| Fixed antenna, 8970 Hz | coil 550 mH, 320 Ω DC; antenna 473 pF (+100 pF needed); 50 kV target would need 1.2 kW; ERP under 1 mW; planned carriers up to 24 h, GPS-locked | [SUB9], DK7FC, 2011 |
| Interruptions in a 3 h kite session | rain, low wind, a coil falling over, broken connection, wire touching wet grass as the kite moved | [SUB9], DF6NM |
| Phase discontinuities | two "major phase jumps" seen by a distant receiver, one while the carrier was off, "perhaps due to soundcard buffering issues"; sample rate locked to DHO38 (itself 3 ppb above nominal) | [SUB9], DF6NM |
| Rules of thumb at LF | vertical wire about 6 pF/m, horizontal top-load about 5 pF/m (up to 15 pF/m for multi-wire); amateur loss resistance 30–150 Ω at 136 kHz; tens of kilovolts on the coil; corona above about 30 kV | [ON7YD] 2004 |
| Retuning practice at LF | the loading coil must be adjustable "to correct for the day-to-day variations of the antenna capacitance (due to changing weather conditions)"; these are "rather small" and corrected by a few per cent of inductance | [ON7YD] §2.18.3 |
| Earth-electrode resistance of an earth-mode VLF antenna (17 m rod spacing) | 94 Ω before rain, 83 Ω after (−12 % [E]) | [SUB9], M0DTS, 2011 |
| Large professional VLF antennas | bandwidth "50–100 Hz"; voltages up to 250 kV; 10–50 % efficiency; "bandwidth resistors" or dynamic tuning for MSK | [WIKI-VLF], secondary for Watt (1967) pp. 14, 58; the bandwidth sentence is unsourced there |
| US Navy design criteria (1972) | top-hat voltage of 250 kV the "present operating limit"; at the lower VLF frequencies most antennas limited by "insufficient bandwidth" to low-rate ICW; saturable-reactor synchronous tuning at Cutler and multistage passive networks studied so that 50 Hz-shift FSK becomes possible; bandwidth–efficiency product b·η = 320π³f⁴he²C/c² | [NAVELEX] pp. 3-36–3-37, [T] |

Derived [E]: the 2011 kite system has X = 19.7 kΩ, Q = 70, bandwidth 127 Hz, envelope delay 2.5 ms; the fixed system (R ≈ 460 Ω implied by 1.2 kW at 1.61 A) Q = 67, 133 Hz. For a 61 m vertical at 8.0 kHz with 6 pF/m (C ≈ 366 pF, X = 54.4 kΩ, L ≈ 1.08 H): Q = 544/181/91 and bandwidth 15/44/88 Hz for Rtotal = 100/300/600 Ω; 100 W would put 54/31/22 kV on the wire, so with an 11–12 kV corona limit current is held to about 0.2 A whatever the amplifier. Rrad = 160π²(heff/λ)² = 1.05 mΩ (heff = 30.5 m) to 4.2 mΩ (heff = 61 m, heavy top-loading). **Bandwidths of a few hertz would need Rtotal well under 100 Ω; reported amateur systems are an order of magnitude wider.** The Navy bandwidth–efficiency product reproduces these figures: b·η = 1.5e-4 Hz for the 61 m example, giving 15/44/88 Hz at η = Rrad/Rtotal for 100/300/600 Ω, and 1.6e-3 Hz with η = 1.25e-5 for the kite, giving 129 Hz [E, from [NAVELEX] p. 3-36]. Detuning sensitivity:

| Q | ΔC/C = 0.1 % | 0.3 % | 1 % |
|---|---|---|---|
| 70 | 4.0°, −0.02 dB | 11.9°, −0.19 dB | 35.0°, −1.7 dB |
| 200 | 11.3°, −0.17 dB | 31.0°, −1.3 dB | 63.4°, −7.0 dB |
| 600 | 31.0°, −1.3 dB | 60.9°, −6.3 dB | 80.5°, −15.7 dB |

No measurements of ΔC/C versus wind, humidity or temperature for such antennas were found; the table states sensitivity, not occurrence. The retuning practice reported by [ON7YD] bounds the day-to-day, weather-driven change at a few per cent of capacitance (a resonance correction ΔL/L equals −ΔC/C), the range in which the table gives 35° or more for Q ≥ 70; 2 % at Q = 70 is 54° and −4.7 dB [E]. Within a session, kite and wire movement changed the circuit more abruptly (row "Interruptions" above).

**E2. Receive side.**

- Research-grade reference: AWESOME digitises 16-bit at 100 kS/s with GPS time "precise to 100 nanoseconds" ([SBM2020], secondary for [C2010]); the LF AWESOME upgrade covers 0.5–470 kHz with sensitivity "as low as 0.03 fT/√Hz" and timing error of 15–20 ns ([C2018] abstract, [P]); a site description of an AWESOME receiver quotes "absolute timing accuracy of 15-20 ns and 96 dB of dynamic range", citing [C2018] ([T2026] §2). [C2010]'s abstract lists sensitivity, frequency and phase response, timing accuracy and cross-modulation as characterised; its full text was not obtained (the open repository copy refused automated access), so the 2010 timing figure is known only through [SBM2020].
- Hobby-grade: USB interface with a 24-bit, 192 kHz codec of 114 dB dynamic range; E-field probe of 1–3 m into a JFET or op-amp front end; isolated power and audio transformers; PPS into a spare audio channel through an RC shaping network, used by vlfrx-tools for sample alignment and timestamps ([KC3EEY], [G]). The vlfrx-tools manual states timing jitter below 50 ns, often better than 30 ns, once the stream is resampled to the PPS (table A5; [VLFRX], [G]), comparable with the research receivers above.
- Power-line interference: harmonics of 50/60 Hz extend to many kilohertz; removal is complicated by the time-varying fundamental and grid-to-grid differences; adaptive-filter and least-squares subtraction were compared, least-squares generally better, adaptive better for fast frequency changes ([CSI2010], abstract only, [P]). Hobby software uses a tracking hum filter ([KC3EEY]). Practitioners call power-line interference "still a huge problem, and … getting worse" ([KC3EEY] 2020).
- Harmonic wander [E]: the n-th harmonic moves n times the mains deviation; near 8 kHz n ≈ 160–165 (50 Hz) or 133–138 (60 Hz), so ±0.02/±0.05/±0.2 Hz mains deviation moves the nearest harmonics by about ±3/±8/±33 Hz. The deviation figures are assumptions (not sourced here). A carrier therefore needs to sit several tens of hertz from 50·n and 60·n to stay clear, and any comb or tracking notch must be much narrower than its spacing from the carrier to leave a 0.06 Hz signal's amplitude and phase untouched.

### F. 5.76 GHz EME hardware

| Item | Value | Source, grade |
|---|---|---|
| Typical system noise budget at 5760 MHz | CMB 3 K, atmosphere 3 K, Moon 13 K, sidelobes 30 K → Ta 50 K; receiver 60 K (0.82 dB NF) → Ts 110 K | [HB2010] Table 30.8, [G] |
| Example CW-EME station | 2 m dish, 39.2 dBi, 1.8° beamwidth, 60 W | [HB2010] Table 30.9, [G] |
| Pointing target | about ¼ of the half-power beamwidth or better (0.75 dB by the 12·(ε/θ)² rule) | [HB2010] §30.12.1, [G]; loss [E] |
| Stability target | CW: drift ≤10 Hz over a minute or so at the operating frequency; digital: several times smaller; transverter crystals may need compensation or phase-locking | [HB2010] §30.11.3, [G] |
| Low-cost transverter | ±0.5 ppm LO (±2.9 kHz at 5.76 GHz [E]); NF 1.5 dB (split) or 2.4 dB (shared port), i.e. 120 K/214 K [E]; 2 W | [SGLAB], [G] |
| Same transverter in the field, internal LO, about 5 min after power-up | "only a few hertz lower" than a Kuhne transverter; no frequency movement noticed during long contacts (terrestrial, 5.76 GHz) | [SGLAB] review, [G] |
| Kit/assembled transverter data sheet (DEM 5760-144) | NF < 1.2 dB (92 K [E]), gain > 22 dB, output > 2.2 W; with a GPS-locked 10 MHz reference the synthesised LO "becomes frequency stable and accurate to within a few Hertz" | [DEM5760], manufacturer, [G] |
| Frequency precision stated for 10 GHz digital EME | operation "on precise frequency in order of tens Hertz" needs the microwave transverter locked to GPS or an atomic standard; an unlocked IF transceiver leaves "hundreds Hz" of uncertainty; a DDS-based IF converter with sub-hertz steps applies the Doppler correction | [OK2AQ15] 2015, [G] |
| Observed residual on a 1296 MHz EME path with receive-side Doppler tracking | peak 4 Hz from nominal, "consistent with plausible dial calibration errors" | [ECHO2022], [G] |
| Waveguide data sheet (commercial copper-alloy WR-137 section) | insertion loss 0.1 dB/ft (0.33 dB/m) maximum, VSWR 1.03:1, specified for 5.85–8.2 GHz, i.e. above 5.76 GHz | [FAIRVIEW], manufacturer, [G] |
| Achieved with GPS locking and automatic Doppler correction | 1296/2301 MHz signals "stay locked to within a few Hz"; at 24 GHz median error after correction under 20 Hz (8e-10), variations under 40 Hz when tracking was accurate | [VK7MO] 2013, [G] |
| Doppler computation accuracy claimed | setting "to within a few Hz" at 10 GHz (2–3e-10) | [OK2AQ] 2016, [G] |
| Doppler magnitude and rate at 10–24 GHz | up to 50 kHz; more than 200 Hz per minute | [VK7MO], [G] |
| Pointing-induced frequency shift | about 100 Hz at 24 GHz when a 2.4 m dish drifted toward the limb while limb-to-limb spread was 200 Hz | [VK7MO], [G] |
| Control-loop parameters | computer-steered LO with 1 Hz resolution, loop period 0.5 s (0.1 s minimum); 1 Hz CAT steps in WSJT-X; radios that cannot retune while transmitting get one setting computed for mid-period | [OK2AQ]; [WSJTX] §9.1, [G] |
| Amateur dish efficiency | measured 23 % to under 10 % for some 10 GHz dishes against a textbook 55 %; 45–50 % "lucky" for small dishes | [W1GHZ] ch. 4, [G] |
| Power amplifiers | TWTs at 24 GHz run about 12 kV with supplies "susceptible to breakdowns with humidity"; solid-state 10–38 W there | [VK7MO], [G] |

Derived [E] (5.76 GHz, λ = 5.20 cm): beamwidth 70λ/D = 3.0°/1.5°/1.2°/0.61°/0.36° for 1.2/2.4/3/6/10 m dishes, so only dishes larger than about 7 m are narrower than the 0.5° Moon. Pointing loss for 0.2° error: 0.05/0.2/0.3/1.3/3.6 dB for those sizes. Earth-rotation Doppler: up to 8.9 kHz one-way at the equator; maximum rate 0.65 Hz/s one-way, 1.30 Hz/s (78 Hz/min) for a self-echo. Station-coordinate error δr gives a one-way Doppler error up to ω·δr·ν0/c = 0.14 Hz per 100 m, i.e. up to 4.6 Hz for the ±3.3 km east–west half-width of a 6-character locator at 45° latitude (5′ × 2.5′ cell; recollection of the locator definition). A clock error of 1 s/0.1 s/30 ms gives up to 0.65/0.065/0.02 Hz one-way. A radio that cannot retune during a 60 s transmission is left with up to ±19.5 Hz one-way at the period ends. Staircase tracking in 1 Hz (10 Hz) steps leaves a ±0.5 Hz (±5 Hz) sawtooth plus whatever phase transient the synthesiser produces at each step. Losses ahead of the LNA add (L−1)·290 K = 6.8/13.7/35/75 K for 0.1/0.2/0.5/1.0 dB and also multiply the receiver's own contribution by L. Theoretical TE10 attenuation at 5.76 GHz: WR-137 0.065 dB/m (copper) to 0.10 dB/m (6061 aluminium); WR-159 0.042–0.064 dB/m (textbook TE10 conductor-loss formula with guide dimensions 34.85 × 15.80 mm and 40.39 × 20.19 mm and conductivities 5.8e7 and 2.5e7 S/m, all from memory and not re-verified; roughness adds to these). The one waveguide data sheet obtained, for a commercial copper-alloy WR-137 section, allows up to 0.33 dB/m over 5.85–8.2 GHz, five times the copper theory, and 5.76 GHz lies below its specified band [FAIRVIEW]; 0.33 dB ahead of the LNA adds 23 K [E]. The 70λ/D rule reproduces the 1.8° that [HB2010] lists for a 2 m dish.

## Moderate → adverse → worst-case parameter table

Sources are listed in the order moderate; adverse; worst, with grades. "[E]" cells are derivations given above. The worst-case column mixes independent effects; no source reports them occurring together.

| Parameter | Moderate | Adverse | Worst case / outage | Sources (grade) |
|---|---|---|---|---|
| Reference fractional offset (carrier and sample clock) | 1e-11 to 1e-10 (locked GPSDO; hobby units up to 1e-10) | 0.5–1.2 ppm (TCXO and transceiver specifications; calibrated HF radio example) | 20–100 ppm (uncalibrated crystal or sound card; ITU 9–50 kHz tolerance 100 ppm); some handheld recording devices 270–420 ppm | [L2016] P, [N8UR] G; [SGLAB] G, [ICOM-7300] G, [WSJTX] G; [BAUDLINE] G, [SM1045] S, [PROTYPOSIS] G |
| σy over chip-to-bit times | 1e-12 (OCXO, Rb, best GPSDO; 1–100 s); 1e-13–1e-11 measured for two OCXOs at 10–10⁴ s | 1e-11 at 1 min, 6e-12 at 1 h (upper GPSDO range); 1e-9 (TCXO floor); 2e-9 at 10³ s and 1e-8 at 10⁴ s (measured TCXO) | 5e-10 at 1 s (worst GPSDO); 1e-8 (TCXO at 1 s); about 1 ppm wander over a 30–90 min room-temperature cycle for an uncompensated crystal, with no ADEV data | [L2002] B, [N8UR] G; [L2016] P, [L2002] B, [N8UR-EXC] G; [L2016] P, [L2002] B, [DL4YHF-CAL] G, [TIMENUTS] G |
| Slow drift | 5e-9 per year (OCXO); under 2 ppb/day | under 1 ppm wander after warm-up (sound card) | 1.5 ppm in 30 min; over 2 ppm from cold; about 3e-8 within 10 min (VHF/UHF transceiver from cold) | [L2002] B, [DL4YHF-EBN] G; [DL4YHF-CAL] G; [DL4YHF-CAL] G, [ARRL-9700] G |
| Audio-stream continuity | continuous, PPS-referenced (30 ns/s rate noise) | isolated phase jumps (two in a 3 h session) | unknown rate under load; ADC and DAC on different clocks; hidden OS resampling | [DL4YHF-CAL] G; [SUB9] G; [DL4YHF-EBN] G, [MS-WASAPI] G |
| Time-of-day error | 8–60 ns (GPS clock; PPS in an audio channel) to ±1–5 µs (PPS on a PC) | 1 ms (NTP over WAN, typical) to ±30–50 ms (Internet NTP; sound-card timestamps from the system clock) | about 1 s (NMEA only; the ±1 s software tolerance); whole-second error | [L2016] P, [VLFRX] G, [GPSD] G; [NL2015] P, [RFC5905] S, [GPSD] G, [VLFRX] G; [GPSD] G, [WSJTX] G, [DL4YHF-CAL] G |
| Receive latency | 0–4 ms | 15–25 ms | 165 ms (sharp DSP filters); 2 s ± 50 ms (network SDR) | [N8OBJ] G, [AB4OJ] G; [ARRL-705] G, [ARRL-710] G; [ARRL-6400] G, [N8OBJ] G |
| Transmit key-up delay | ≤10 ms (objective) | 14–25 ms | 47–79 ms | [MS141B] S; [MS141B] S, ARRL G; ARRL G |
| Passband amplitude variation | under 1 dB mid-passband | 2–3 dB (standard limits) | −6 dB at 90–330 Hz and 2.5–2.9 kHz edges | [MS141B] S; [MS141B] S; ARRL G |
| Group-delay variation within ≤3 kHz | about 0 (linear-phase DSP: latency only) | ≤1.0 ms over the central 80 % | 2–4 ms toward edges of narrow crystal filters | [E]; [MS141B] S; [E] and forum snippet G |
| AGC state | off/fixed gain, or noise below threshold | slow: attack ≤30 ms, release 0.8–1.2 s (about 5 dB/s) | fast: attack ≤10 ms, release ≤25–100 ms, impulse capture | [SM5BSZ] G; [MS141B] S, [KF6DX] G; [MS141B] S, [AB4OJ] G, [DCC18] G |
| Receiver noise figure (14 MHz) | 5–9 dB (preamplifier) | 18–20 dB | 33 dB (direct sampling, no preamplifier) | ARRL Lab G |
| LO noise at 10 kHz | −145 to −155 dBc/Hz | −125 to −140 | −99 to −115 | [SH] G |
| Dynamic range at narrow spacing (2 kHz; 5 kHz for some older sets) | 100–110 dB | 80–95 dB | 45–75 dB | [SH] G, [NC0B21] G |
| Front-end overload | ADC clip −8 to −10 dBm | −23 to −28 dBm with preamplifiers | summed signals above clip: no reception while clipped (a brief overload judged harmless by [DC4KU]) | [AB4OJ] G, [DC4KU] G |
| Transmit IMD3 (relative to one tone) | −36 to −40 dBc | −30 dBc (standard, fixed) | −24 to −25 dBc (tactical limit; ITU acceptability), worse if over-driven | [AB4OJ] G, [NC0B21] G; [MS141B] S; [MS141B] S, [SM326] S |
| Transmit power sag, heating | none (continuous-rated) | 0.3 dB in 10–15 s; heatsink +28 K in 10 min | an antenna tuner derated to a quarter power for continuous duty; protective shutdown not quantified | [ARRL-500S] G; [SGLAB] G, [ARRL-6400] G; [ARRL-9700] G |
| Sub-9 kHz antenna Q (bandwidth) | 70 (about 130 Hz) | 180–200 (40–45 Hz) | above 500 (15 Hz or less), hypothetical low-loss | [SUB9] G with [E]; [E]; [E] |
| Radiated phase and amplitude error from detuning | 4° (ΔC/C 0.1 %, Q 70) | 12–31° (0.3 %) | 35–63°, −1.7 to −7 dB (1 %); carrier off (corona, rain, mechanical) | [E]; [E]; [E], [SUB9] G |
| Antenna voltage ceiling | 11–12 kV (corona observed) | 30 kV | 50 kV target; flashover | [SUB9] G; [ON7YD] G; [SUB9] G |
| Wander of nearest mains harmonic near 8 kHz | ±3 Hz | ±8 Hz | ±33 Hz | [E], assumed mains deviations |
| 5.76 GHz LO error | 0.06–0.6 Hz (1e-11 to 1e-10) | 58 Hz (1e-8) | ±2.9 kHz (±0.5 ppm) | [E] from [L2016] P, [N8UR] G; [E]; [SGLAB] G |
| Residual frequency error after Doppler correction, 5.76 GHz | under 5 Hz (scaled from "a few Hz" at 10 GHz, under 20 Hz median at 24 GHz) | ±20 Hz (one setting per 60 s transmission); ±5 Hz (10 Hz steps) | uncorrected: to ±8.9 kHz one-way, 78 Hz/min self-echo rate | [VK7MO] G, [OK2AQ] G with [E]; [E]; [E] |
| System noise temperature, 5.76 GHz | 110 K | about 150–200 K (0.5–1 dB loss ahead of LNA) | over 260 K (2.4 dB transverter alone plus 50 K antenna) | [HB2010] G; [E]; [SGLAB] G with [E] |
| Pointing error | ≤¼ beamwidth (0.75 dB) | ½ beamwidth (3 dB) | off the Moon; limb pointing shifts the spectrum | [HB2010] G; [E]; [VK7MO] G |
| Dish aperture efficiency | 55 % | 45–50 % | 10–23 % | [W1GHZ] G |

## How the literature models or simulates this

- **Oscillator noise.** The accepted model is the five-term power law with time-domain images given by σy²(τ) = h−2·(2π)²τ/6 + h−1·2·ln2 + h0/(2τ) + h1·[1.038 + 3·ln(2πfhτ)]/((2π)²τ²) + h2·3fh/((2π)²τ²) ([TF538] eq. 23; [SP1065] §7.1 gives the same coefficients; [IEEE1139]). Inverting per term gives simulation levels from a measured or specified ADEV: h0 = 2τσy², h−1 = σy²/(2·ln2), h−2 = 6σy²/((2π)²τ) [E]. [RV2023] is a current open tutorial with conversion charts and compares three analysis tools (Stable32, AllanTools, SigmaTheta).
- **Generating the noise.** [SP1065] §8: white noise via Box–Muller; even exponents by integrating/differencing; flicker by RC-ladder filtering or FFT shaping (any integer α from −2 to +2); add deterministic offset, drift and sinusoid; it calls the Kasdin–Walter method ([KW1992]) "a good method". [K1995] generalises this to any α with a fractional-difference filter; a secondary description gives the autoregressive recursion a0 = 1, ak = (k − 1 − α/2)·ak−1/k (p. 822, eq. 116) and notes that truncating the recursion flattens the spectrum below a corner frequency ([PINK], secondary). Earlier models: [BA1966]. Known limits ([PINK] and [E]): flicker and random-walk processes are non-stationary, so any finite generator fixes an implicit low-frequency cut-off; FFT methods are periodic; hours-long runs are more practical if the phase process is generated at a decimated rate and interpolated.
- **Disciplined oscillators.** No standard generative model was found; the literature describes them empirically (local-oscillator noise at averaging times shorter than the loop time constant, GPS timing noise with a −1 ADEV slope at longer times, a bump between, occasional steps) ([L2016]; [L2008]; [N8UR]). Validated domain: locked operation with good sky view; failures: holdover (3e-12 to 1e-9 over a week), firmware frequency-lock offsets ([L2008]; [N8UR]).
- **HF radio as a channel element.** Standards treat the radio by bounds, not models: amplitude mask, ≤1.0 ms delay variation, AGC time constants, ALC slope, IMD and phase-jitter limits ([MS141B]); modem standards add test offsets (±75 Hz, 3.5 Hz/s) and a 3 dB allowance for embedded radios' filters and AGC ([MS110C]). AGC is described in the grey literature as a peak detector with fixed attack, programmable hang and a decay in dB/s, plus a threshold ([KF6DX]; [SM5BSZ]); no public model reproduces a specific commercial radio. Receiver delay distortion is handled in practice by measuring and equalising it per station ([WSJTX] §14.3).
- **Converters.** Direct-sampling receivers are characterised by clip level and noise power ratio ([AB4OJ]; [DC4KU]); an ideal clipper plus quantiser with optional dither reproduces those two observables but not converter spurs.
- **Power amplifiers.** Two-tone IMD figures calibrate a memoryless odd-order polynomial; travelling-wave tubes are classically modelled by Saleh's AM/AM–AM/PM functions ([S1981], bibliographic record only); a solid-state counterpart commonly attributed to Rapp was not located. Memoryless models omit thermal and bias-memory effects.
- **Resonant antenna.** Series RLC with slowly varying C(t), R(t); the quasi-static phase/amplitude formulas hold while variations are slow compared with Q/(πf) (2–20 ms here) [E]. Professional practice for MSK (bandwidth resistors, dynamic tuning) is described through [WIKI-VLF] and, for synchronous saturable-reactor tuning and the bandwidth–efficiency product, [NAVELEX] pp. 3-36–3-37.
- **Hum removal.** Adaptive-filter and least-squares harmonic subtraction with explicit tracking of the fundamental ([CSI2010], abstract).
- **Doppler control.** Compute from ephemeris and station coordinates, apply by stepped retuning ([WSJTX]; [OK2AQ]; [VK7MO]); published accuracy statements are operational ("within a few Hz"), not error budgets.

## What a high-fidelity simulation would need

These are options with their limits, not a prescribed design; better approaches may exist.

**Inputs common to all scenarios**

- Per-end reference description: class, static offset, drift law, ADEV at 1 s–10⁴ s (tables A1, A1a, A2 and A3), and whether a frequency/drift search is assumed in the receiver. Option A: static offset plus linear drift only (cheap; misses random wander that limits multi-chip coherence). Option B: add power-law phase noise fitted to ADEV (needs care with non-stationarity and run length). Option C: replay measured phase records of real devices (most faithful; none are public for sound cards or amateur transceivers).
- Sample-clock model applied to both the audio waveform and symbol timing, separately for ADC and DAC; optional rate-tracking residual (30 ns/s white) and discrete sample slips. Slip statistics are unknown, so they can only be parameterised, not calibrated.
- Timing chain: clock offset drawn per class (tens of ns; µs; 1–30 ms; about 1 s plus optional whole-second error), fixed latencies per radio, key-up delay.

**MF/HF through a transceiver**

- Audio passband as a filter with measured edge frequencies and either minimum-phase (crystal-like) or linear-phase (DSP) delay. Limit: real delay curves are unpublished; the standard's 1.0 ms bound is a ceiling, not a typical.
- AGC as a switchable block (off; slow; fast; threshold relative to band noise) driven by the composite of wanted signal, external noise including impulses, and in-passband interferers supplied by the noise and interference models of the noise chapters ([12](12-sub9khz-noise.md), [50](50-noise-and-interference-mf-hf.md)). Phase modulation by AGC is uncharacterised and can only be included as a sensitivity parameter.
- Reciprocal mixing and overload: adjacent-signal ensemble convolved with an LO-noise mask from table C5; ADC clip level against total input power; optional third-order products. Limit: sub-kilohertz phase noise must be extrapolated.
- Transmit: carrier-set quantisation (1–100 Hz), IMD3/IMD5 polynomial, ALC modelled as a slow leveller to demonstrate why it must stay inactive, power sag.
- Validation observables: decode sensitivity versus static offset and sweep rate (the ±75 Hz and 3.5 Hz/s tests); noise-power-ratio and two-tone IMD figures; step response of gain to a pulse (about 100 ms recovery, fast setting); measured latencies.

**Sub-9 kHz direct audio**

- Carrier and timing both from the sound-card clock model; this is the dominant term unless both ends are PPS- or GPSDO-referenced.
- Antenna as series RLC with stochastic C(t) (slow thermal/humidity trend plus wind-rate fluctuation) and R(t); voltage ceiling converting requested power into a current limit; outage process for rain/corona/mechanical interruptions. Limit: no measured statistics exist for ΔC/C; only sensitivities (table E1) and the practitioners' few-per-cent bound on day-to-day changes [ON7YD] are defensible.
- Receiver: converter full-scale versus sferic peaks from the noise model; hum harmonics with a wandering fundamental; an optional tracking-notch stage whose notch width and adaptation rate are explicit so its effect on a 0.06 Hz-wide signal can be measured rather than assumed.
- Validation observables: received phase versus time for a GPS-locked carrier (amateur grabber practice at 8.97 kHz; [T2026] phase-slope method), spectral line width in sub-millihertz FFTs, antenna-current phase against drive.

**5.76 GHz EME**

- LO error = reference error × 5.76e9 at each end, plus IF-radio error; multiplied phase noise from the reference mask (+55 dB) with a placeholder for PLL in-band noise.
- Doppler-control block: computed Doppler with selectable coordinate, clock and ephemeris errors; update period and step size; whether retuning is allowed during transmission; a phase transient per step (unknown, parameter).
- Noise temperature budget (sky, Moon, spillover, feed loss, LNA) and pointing error process relative to beamwidth; for beams narrower than the Moon, pointing must also weight the lunar Doppler profile supplied by the EME chapters ([40](40-eme-lunar-scattering-and-geometry.md), [41](41-eme-systems-practice-and-environment.md)).
- Validation observables: own-echo frequency error and drift across a transmission; Sun/Moon noise ratios; echo level versus deliberate pointing offset.

## Comparison with the modem's presets

The oscillator presets (`src/tuning_presets.cpp:8-19`) pair a relative two-end frequency bound with a Wiener phase diffusion: free-running crystal 100 ppm and 0.5°/√s; CB SSB/ATS Mini 40 ppm (±20 ppm per radio) and 0.5°/√s; IC-7100 1 ppm (±0.5 ppm per radio) and 0.05°/√s; three GPSDO classes at 1e-10 with 0.5, 0.05 and 0.005°/√s. The receiver searches the bound times Margin, 3 by default. The source itself calls the IC-7100 diffusion "an illustrative assumption" and the GPSDO bound a "shared locked-link residual assumption".

- **Frequency bounds.** The IC-7100 bound matches the manufacturer figures found (±0.5 ppm for the IC-7100, IC-7410 and IC-7300, and for a 5.7 GHz transverter LO); the 1.23 ppm calibration example of [WSJTX] exceeds one end's share but lies within the Margin. The crystal bound covers one device at the 50–100 ppm typical of sound cards, or two dedicated recorders at −40 to +23 ppm; two sound cards of opposite sign, one card at +119 ppm and handheld devices at +274 to +417 ppm need the Margin or more ([BAUDLINE], [VLFRX], [PROTYPOSIS]). No source on CB or ATS Mini radios was examined. The GPSDO bound covers locked laboratory units ([L2016], [L2008]) and the worst hobby offset (−9.6e-11 [N8UR]); at 5.76 GHz it is 0.58 Hz, 1.7 Hz with the Margin, below the few-hertz Doppler-correction residuals of table F.
- **Phase diffusion.** The simulated Wiener phase is an absolute rate, not scaled by the carrier (`include/datapump/triad_simulation.hpp:10-13`), whereas a reference of given σy wanders in phase in proportion to ν0. The rms phase across one bit is k·√T for a preset and 2π·ν0·T·σy(T) for the measured references of tables A1a and A2 (exact for white FM, otherwise an order of magnitude; σy interpolated log-log) [E]:

| Row (carrier; bit T) | Presets 0.5 / 0.05 / 0.005 °/√s | Measured TCXO | Inexpensive OCXO | High-grade OCXO | Good GPSDO ("HPZ") |
|---|---|---|---|---|---|
| 1 (8 kHz; 5,760 s) | 38° / 3.8° / 0.38° | 110° | 0.23° | 0.006° | 0.015° |
| 2, 3 (1 MHz; 576 s) | 12° / 1.2° / 0.12° | 300° | 1.8° | 0.035° | 0.28° |
| 4, 5, 7 (30 MHz; 11.6 s) | 1.7° / 0.17° / 0.017° | 39° | 1.2° | 0.016° | 0.19° |
| 6 (30 MHz; 257 s) | 8.0° / 0.80° / 0.080° | 2,400° | 24° | 0.37° | 4.3° |
| 8 (5.76 GHz; 26.4 s) | 2.6° / 0.26° / 0.026° | 2×10⁴° | 520° | 6.4° | 95° |

  At 8 kHz every GPSDO preset exceeds the wander of a locked GPSDO (0.015°, or 0.014–0.17° from its 5–60 ns time error), while the crystal preset is milder than a TCXO and far milder than an uncompensated sound-card crystal, whose 1 ppm, 30–90 min wobble [TIMENUTS] is a phase excursion of 7–22 rad at 8 kHz [E]. From 1 MHz upwards the order reverses: the IC-7100 preset gives 0.17° per 30 MHz bit against 39° for the TCXO, and the GPSDO-OCXO preset 0.026° per EME bit against 6–95° for good references multiplied to 5.76 GHz. The manual's rows 2 and 3 are at 500 kHz, which halves that row's measured columns. One diffusion figure per class therefore cannot represent a reference at all the modem's carriers.
- **Spread presets.** At 8 kHz an unlocked sound-card clock moves the carrier by ±4 mHz (±0.5 ppm) to 12 mHz (the 1.5 ppm cooling test), more than the Sub9 best (0.001 Hz) and moderate (0.005 Hz) Doppler bounds; with GPSDO-referenced clocks (1e-10) the equipment term is below 1 µHz [E]. At 5.76 GHz the Doppler-correction residuals of table F (a few hertz achieved; ±5 Hz with 10 Hz steps; up to ±19.5 Hz one-way with one setting per 60 s transmission) are of the order of the EME focused bound of 6 Hz. The Helper's upper pilot, at 2,400 Hz or higher, lies within about 100–500 Hz of the measured upper −6 dB edges of SSB filters (2,500–2,894 Hz, table C1) or beyond them, where a minimum-phase filter's attenuation and group delay change fastest.
- **Front end and level.** Section 5 of [00](00-method-and-modem-context.md) lists converter quantisation and clipping, a one-pole low-pass, one gain step, an overload-recovery gain, one receiver-coordinate tone and Bernoulli single-sample impulses. The converter model can reproduce full-scale clipping (table C6) and quantisation, but not a direct-sampling radio's dither penalty or spurs; a one-pole low-pass reproduces neither the 90–330 Hz lower passband edge nor band-edge group delay (table C1); the overload-recovery gain resembles impulse capture by a fast AGC (about 100 ms recovery, table C2) but acts only after converter overload, not above an AGC threshold such as −99 dBm; one fixed tone covers a steady in-passband carrier, not mains harmonics wandering by ±3 to ±33 Hz (for the mains deviations assumed in E2). Absent and possibly relevant here: transmit IMD3 of −25 to −40 dBc on the carriers themselves, audio-stream phase jumps, the resonant transmitting antenna (Q 70–600) and its detuning, and latencies of 2 ms to 2 s, of which a fixed part fits the simulator's constant timing offset. The −164 dBm/Hz default corresponds to 10 dB above kT0: receiver-only noise at 14 MHz spans −169 to −141 dBm/Hz for the measured noise figures (table C4), and the 5.76 GHz system temperatures of table F (110–264 K) give −178.2 to −176.4 dBm/Hz, 12–14 dB below the default [E]. The constant simulated level omits the transmit power sag (0.3 dB in 10–15 s) and the detuning amplitude loss (to −7 dB at 1 % and Q = 200).

## Contradictions, open questions, and things not found

- **Internet time accuracy.** NIST states that WAN asymmetry "typically limits" NTP to about one millisecond ([NL2015]); the GPSD documentation tabulates about ±30 ms and cites "a few tens of milliseconds" ([GPSD]). The figures are reconciled by path and configuration rather than by one being wrong: the protocol bounds the error by root dispersion plus half the round-trip delay, and itself states "a few hundred microseconds" for clients on fast LANs but "a few tens of milliseconds" with polls of up to 36 h ([RFC5905] §1, §4); a vendor's support boundary promises 1 ms only with under 0.1 ms latency to the time source and at most four hops, and 50 ms within six hops ([MS-W32TIME]); sound-card timestamps from an ntpd-disciplined clock are about ±50 ms ([VLFRX]). NIST's millisecond describes a well-configured client on a short WAN path; ±30–50 ms describes typical Internet clients and their audio paths. Both are kept in table B.
- **"Inherently accurate" GPSDOs.** [L2008]/[L2016] expect any locked GPSDO to be within 1e-12 at one day; [N8UR] measured a persistent −9.6e-11 offset in the 2017 and 2019 versions of one hobby model, attributed to its control loop, and about 4e-13 for its 2020 version.
- **Antenna bandwidth.** The expectation of "a few hertz to tens of hertz" is not supported by the amateur sub-9 kHz reports found, which imply about 130 Hz (Q ≈ 70); the "50–100 Hz" quoted for large professional antennas is an unsourced encyclopaedia sentence for higher VLF frequencies, although the Navy criteria state that most VLF antennas at the lower VLF frequencies lacked the bandwidth for 50 Hz-shift FSK ([NAVELEX] p. 3-36). Narrower bandwidths require much lower loss than reported.
- **Assumed and measured long-term quartz stability.** The starred TCXO values of table A6 (1e-8 at 10³ s, 1e-7 at 10⁴ s) are 5–10 times worse than the one TCXO measured in a room (table A1a), whereas a precision TCXO's ADEV rose from about 4e-11 in still air to about 2e-9 near 2 s under light airflow ([SITIME], read from the plot); the environment, not the oscillator class alone, sets the value.
- **Brief ADC overload.** [AB4OJ] states that a clipping ADC "can no longer process signals"; [DC4KU] judges brief overloads of the same radio harmless to reception. Neither quantifies the effect on a long coherent integration.
- **AGC and weak-signal digital modes.** [DCC18] finds no harm from AGC with a steady in-passband carrier up to about 75 dB stronger; [SM5BSZ] and the cautionary text of [MS110C] recommend AGC off or slow. These are consistent once static and impulsive interference are distinguished, but no quantitative test with impulsive noise and long integrations was found.
- **Latency figures** for the same model differ by method (IC-7300: 2.7 ms in [N8OBJ], 3.2–3.7 ms in [AB4OJ]); ADC clip level differs by sample or method (−10 vs −8 dBm).
- **IMD reference conventions** differ by 6 dB (relative to one tone in [AB4OJ], [SM326], [MS141B]; relative to PEP in ARRL tables); values above keep each source's convention.
- **Not found or not obtained:** full text of [C2010] (the open repository copy refused automated access) and [CSI2010] (access blocked); MIL-STD-188-141 revisions C (2011) and D (2017), whose catalogue host refused automated access, so whether their radio requirements differ from the revision B values quoted here was not checked; IEEE Std 1139 text; Kasdin's full paper; Kuhne/DB6NT 5.7 GHz transverter and LNA data sheets (only a manual extract without values was seen); synthesiser data sheets for LO phase noise, including the DigiLO of [DEM5760]; any frequency-accuracy or Doppler-residual measurement made at 5.76 GHz itself (values in table F are from 1.3–24 GHz or are manufacturer statements); DUBUS and EME-conference papers on 6 cm station engineering; a peer-reviewed measurement study of sound-card clocks (the acoustic-sensor literature located quotes the grey measurements of table A5); the paper behind [PROTYPOSIS]; receiver-design textbook data on AGC (Rohde et al. is known only through [RV2023]'s reference list); the author of [DCC18], whom neither the paper, the conference file listing nor the 2018 programme names. The vlfrx-tools documentation, unreachable at its author's site, was read in an archived copy ([VLFRX]); of the Navy VLF criteria only pp. 3-1–3-2, 3-36–3-37 and 3-40–3-42 of a scanned copy were read ([NAVELEX]), and they contain no data on detuning by wind or ice.
- **Not publicly characterised (as far as could be established):** phase noise of amateur transceivers below 100 Hz offset (ARRL transmit plots begin at 100 Hz) and of transverters at any offset; phase shift accompanying AGC or ALC gain changes; noise-reduction and noise-blanker behaviour for signals far below the noise; phase continuity of synthesisers during CAT frequency steps; dropout and resampling statistics of operating-system audio paths; ADEV of sound-card clocks (only rate logs and spot offsets exist); capacitance and loss fluctuation of electrically small VLF antennas in wind and rain (only the few-per-cent retuning practice of [ON7YD]); microphonic, wind-induced and 1/f noise of E-field probes; sound-card low-frequency response; sferic peak levels relative to converter full-scale; AM–PM and thermal phase drift of amateur microwave amplifiers; LNA gain stability; mount tracking jitter.
- **Coverage.** 2015–2026 measurements in this chapter are mostly grey literature ([N8UR], [REEVE], [PROTYPOSIS], [SITIME], ARRL Lab); no peer-reviewed comparison of hobby-grade GPSDOs, of USB sound-card clocks or of close-in SDR phase noise was located.

## References

Access: "read" = supporting passage read in full text; "abstract" = abstract or search snippet only; "secondary" = quoted through the named source; "record" = bibliographic record verified only.

**Standards [S]**
- [MS141B] MIL-STD-188-141B, *Interoperability and Performance Standards for Medium and High Frequency Radio Systems*, US DoD, 1 Mar 1999, §5.2–5.4. https://hflink.com/standards/ALE_standard_188_141B.pdf — read. (Revisions C, 2011, and D, 22 Dec 2017, exist; neither was obtained, the catalogue host having refused automated access.)
- [MS110C] MIL-STD-188-110C w/Change 1, *Interoperability and Performance Standards for Data Modems*, 3 Jan 2012, App. C §C.6–C.7, App. D §D.6–D.7. https://www.sigidwiki.com/images/c/c8/MIL-STD-188_110C_CHG_NOTICE-1.pdf — read.
- [SM1045] Rec. ITU-R SM.1045-1 (07/1997), *Frequency tolerance of transmitters*, Table 1. https://www.itu.int/dms_pubrec/itu-r/rec/sm/R-REC-SM.1045-1-199707-I!!PDF-E.pdf — read.
- [SM326] Rec. ITU-R SM.326-7 (11/1998), *Determination and measurement of the power of amplitude-modulated radio transmitters*, §1.2.3. https://www.itu.int/dms_pubrec/itu-r/rec/sm/R-REC-SM.326-7-199811-I!!PDF-E.pdf — read.
- [TF538] Rec. ITU-R TF.538-4 (07/2017), *Measures for random instabilities in frequency and time (phase)*, eqs. 3–6, 23, Tables 1–2. https://www.itu.int/dms_pubrec/itu-r/rec/tf/R-REC-TF.538-4-201707-I!!PDF-E.pdf — read.
- [IEEE1139] IEEE Std 1139-2022, *Standard Definitions of Physical Quantities for Fundamental Frequency and Time Metrology — Random Instabilities*. https://standards.ieee.org/ieee/1139/7585/ — secondary (via [RV2023]; the 1999 edition is cited by [SP1065]).
- [RFC5905] D. Mills, J. Martin (Ed.), J. Burbank, W. Kasch, *Network Time Protocol Version 4: Protocol and Algorithms Specification*, RFC 5905, IETF Standards Track, June 2010, §1, §4. https://www.rfc-editor.org/rfc/rfc5905.txt — read.

**NIST and peer-reviewed [T]/[P]/[B]**
- [SP1065] W. J. Riley, *Handbook of Frequency Stability Analysis*, NIST SP 1065, 2008, §3.2, §7, §8. https://nvlpubs.nist.gov/nistpubs/Legacy/SP/nistspecialpublication1065.pdf — read (scanned text) [T].
- [L2002] M. A. Lombardi, "Fundamentals of Time and Frequency", ch. 17 in *The Mechatronics Handbook*, CRC Press, 2002, Table 17.5. https://tf.nist.gov/general/pdf/1498.pdf — read [B].
- [L2008] M. A. Lombardi, "The Use of GPS Disciplined Oscillators as Primary Frequency Standards for Calibration and Metrology Laboratories", *Measure* 3(3), 56–65, 2008. https://tf.nist.gov/general/pdf/2297.pdf — read [P].
- [L2016] M. A. Lombardi, "Evaluating the Frequency and Time Uncertainty of GPS Disciplined Oscillators and Clocks", *NCSLI Measure J. Meas. Sci.* 11(3–4), 30–44, 2016, doi:10.1080/19315775.2017.1316696. https://tf.nist.gov/general/pdf/2871.pdf — read [P].
- [NL2015] A. N. Novick, M. A. Lombardi, "Practical Limitations of NTP Time Transfer", Proc. 2015 Joint IFCS–EFTF, 570–574, doi:10.1109/FCS.2015.7138909. https://tf.nist.gov/general/pdf/2776.pdf — read [P].
- [RV2023] E. Rubiola, F. Vernotte, "The Companion of Enrico's Chart for Phase Noise and Two-Sample Variances", *IEEE Trans. Microw. Theory Techn.* 71(7), 2996–3025, 2023, doi:10.1109/TMTT.2023.3238267. https://arxiv.org/abs/2201.07109 — read (arXiv version) [P].
- [K1995] N. J. Kasdin, "Discrete simulation of colored noise and stochastic processes and 1/f^α power law noise generation", *Proc. IEEE* 83(5), 802–827, 1995, doi:10.1109/5.381848 — abstract (Princeton record https://collaborate.princeton.edu/en/publications/discrete-simulation-of-colored-noise-and-stochastic-processes-and/); algorithm detail secondary via [PINK] [P].
- [KW1992] N. J. Kasdin, T. Walter, "Discrete simulation of power law noise", Proc. 1992 IEEE Frequency Control Symp., 274–283, doi:10.1109/FREQ.1992.270003 — record; cited by [SP1065] [P].
- [BA1966] J. A. Barnes, D. W. Allan, "A statistical model of flicker noise", *Proc. IEEE* 54(2), 176–178, 1966, doi:10.1109/PROC.1966.4630 — record; cited by [SP1065] [P].
- [C2010] M. B. Cohen, U. S. Inan, E. W. Paschal, "Sensitive Broadband ELF/VLF Radio Reception With the AWESOME Instrument", *IEEE Trans. Geosci. Remote Sens.* 48(1), 3–17, 2010, doi:10.1109/TGRS.2009.2028334 — abstract; specifications secondary via [SBM2020]; full text not obtained (an open repository copy refused automated access) [P].
- [C2018] M. B. Cohen et al., "Broadband longwave radio remote sensing instrumentation", *Rev. Sci. Instrum.* 89, 094501, 2018, doi:10.1063/1.5041419 — abstract [P].
- [CSI2010] M. B. Cohen, R. K. Said, U. S. Inan, "Mitigation of 50–60 Hz power line interference in geophysical data", *Radio Sci.* 45(6), 2010, doi:10.1029/2010RS004420 — abstract [P].
- [T2026] P. Teysseyre, C. Briand, M. Cohen, "Real-time detection of solar flares from ground-based VLF data", arXiv:2605.11822, 12 May 2026 (submitted to *J. Space Weather Space Clim.*). https://arxiv.org/abs/2605.11822 — read; preprint, not yet peer-reviewed.
- [SBM2020] C. Y. Sung, P. Brown, R. Marshall, "A Two Year Survey for VLF Emission from Fireballs", arXiv:2002.03870, 2020. https://arxiv.org/abs/2002.03870 — read (instrument description only) [P, preprint version].
- [S1981] A. A. M. Saleh, "Frequency-Independent and Frequency-Dependent Nonlinear Models of TWT Amplifiers", *IEEE Trans. Commun.* 29(11), 1715–1720, 1981, doi:10.1109/TCOM.1981.1094911 — record [P].
- [VIG] J. R. Vig, *Quartz Crystal Resonators and Oscillators for Frequency Control and Timing Applications — A Tutorial*, SLCET-TR-88-1 (Rev. 8.5.2.0), US Army Communications-Electronics Research, Development & Engineering Center, January 2004, ch. 7, slides 7-1 and 7-5. Copy hosted by a manufacturer: https://www.pletronics.com/wp-content/uploads/2025/08/Vig_tutorial.pdf — read (table as text; chart read by eye) [T].
- [NAVELEX] Naval Electronic Systems Command, *Naval Shore Electronics Criteria: VLF, LF, and MF Communications Systems*, NAVELEX 0101,113, August 1972, ch. 3 "VLF Communication Equipment", pp. 3-36–3-37 (also 3-40–3-42). Scanned copy, archived from http://www.navy-radio.com/manuals/0101-1xx/0101_113-03.pdf (Internet Archive snapshot of 3 Jun 2025; the original host refused automated access) — read in part, by eye [T].
- [GSH2021] T. Gburrek, J. Schmalenstroeer, R. Haeb-Umbach, "On Synchronization of Wireless Acoustic Sensor Networks in the Presence of Time-Varying Sampling Rate Offsets and Speaker Changes", arXiv:2110.12820, 25 Oct 2021 (carries an IEEE copyright notice), §1, §3. https://arxiv.org/abs/2110.12820 — read [P, preprint version].

**Grey literature [G]**
- [SH] R. Sherwood (NC0B), "Receiver Test Data", Sherwood Engineering, table updated 1 Aug 2026. http://www.sherweng.com/table.html — read.
- [NC0B21] R. Sherwood, "Transceiver Performance for the HF Contest & DX Operator", Contest University 2021 slides. https://www.contestuniversity.com/wp-content/uploads/2021/05/Transceiver-Performance-for-the-HF-Contest-and-DX-Operator-CTU-2021-NC0B.pdf — read.
- [DCC18] "Dynamic Range tests of WSJT-X and WSPR on 630m using AGC with IC-7300", ARRL/TAPR Digital Communications Conference proceedings, 2018, pp. 77–78 (author not named in the paper, in the conference file listing or in the 2018 programme; the paper describes a station in Colorado). https://files.tapr.org/meetings/DCC_2018/DCC2018-09.pdf — read.
- [ARRL-705] ARRL Lab, Icom IC-705 product review, *QST*, Feb 2021. https://www.arrl.org/files/file/ProductReviewsForDeb/2021/0221.pdf — read.
- [ARRL-710] ARRL Lab, Yaesu FT-710 AESS product review, *QST*, Aug 2023. https://www.arrl.org/files/file/ProductReviewsForDeb/2023/pr082023.pdf — read.
- [ARRL-6400] ARRL Lab, FlexRadio FLEX-6400M and RM Italy BLA600 reviews, *QST*, Feb 2019. https://www.arrl.org/files/file/ProductReviewsForDeb/2019/PR0219.pdf — read.
- [ARRL-9700] ARRL Lab (B. Allison, WB1GCM, Lab Notes), Icom IC-9700 review; mAT-125E tuner review, *QST*, Jan 2020. https://arrl.org/files/file/ProductReviewsForDeb/2020/0120.pdf — read.
- [ARRL-500S] P. Villeneuve (VA2PV) and ARRL Lab, ACOM 500S review, *QST*, Apr 2024. https://www.arrl.org/files/file/ProductReviewsForDeb/2024/pr042024.pdf — read.
- [AB4OJ] A. Farson (VA7OJ/AB4OJ), "IC-7300 User Evaluation & Test Report", Iss. 5, 27 Nov 2020. https://www.qsl.net/ab4oj/icom/ic7300/7300notes.pdf — read.
- [AB4OJ-AGC] A. Farson, "AGC and Noise Blanker in an IF-DSP Receiver". https://www.qsl.net/ab4oj/icom/ic7700/agc_nb.html — read.
- [DC4KU] W. Schnorrenberg (DC4KU), "IC-7300 Test Report", 17 Jul 2019. https://www.qsl.net/ab4oj/icom/ic7300/ic7300_dc4ku.pdf — read.
- [KF6DX] D. T. Smith (KF6DX), "Digital Automatic Gain Control for Radio Transceivers", 2003. https://www.qsl.net/n5na/pages/digitalagc/digitalagc.html — read.
- [SM5BSZ] L. Åsbrink (SM5BSZ), "AGC in receivers", 16 Feb 2011. https://sm5bsz.com/lir/agc/agc.htm — read.
- [N8OBJ] J. Gibbons (N8OBJ), "A Succinct Study of the Radio Processing Delays in Modern Receivers", v1.0, 22 Jun 2022. https://groups.google.com/group/hamsci-grape/attach/12f9b2a0e967a/RadioDelayTesting.pdf?part=0.1 — read.
- [WSJTX] J. H. Taylor et al., *WSJT-X User Guide*, version 2.7.0. https://wsjt.sourceforge.io/wsjtx-doc/wsjtx-main-2.7.0.html — read.
- [VK7MO] R. Moncur (VK7MO), J. Taylor (K1JT), "Small Station EME at 10 and 24 GHz: GPS Locking, Doppler Correction, and JT4", PDF dated 16 Dec 2013. https://wsjt.sourceforge.io/small_station_eme.pdf — read.
- [OK2AQ] M. Kasal (OK2AQ), C. Suckling (G3WDG), "A New Method for Microwave EME Doppler Shift Compensation", PDF dated 14 May 2016. https://www.radio.feec.vutbr.cz/esl/files/EME/Doc/A%20New%20Method%20for%20Microwave%20EME%20Doppler%20Shift%20Compensation.pdf — read.
- [HB2010] J. Taylor (K1JT), EME section of ch. 30 "Space Communications", *The ARRL Handbook*, 2010 edition, Tables 30.8–30.9, §30.11–30.12. https://wsjt.sourceforge.io/Hbk_2010_Ch30_EME.pdf — read.
- [W1GHZ] P. Wade (N1BWT/W1GHZ), *Microwave Antenna Book*, ch. 4 "Parabolic Dish Antennas", 1994/1998. http://www.w1ghz.org/antbook/chap4.pdf — read.
- [SGLAB] "SG Lab 5700 MHz transverter version 1.4" (specification table reproduced by a reseller/reviewer), 13 Feb 2025, updated 17 Apr 2025. https://3fs.net.au/sg-lab-5700-mhz-transverter-version-1-4/ — read.
- [BODNAR] Leo Bodnar Electronics, "Low-jitter GPS-locked precision frequency reference" product page. https://www.leobodnar.com/shop/index.php?main_page=product_info&cPath=107&products_id=234 — read.
- [N8UR] J. Ackermann (N8UR), "Testing the BG7TBL GPSDO (and a couple of others)", 16 Jul 2022, https://blog.febo.com/?p=636; "Oscillators, GPS, and GPSDOs (Part 3)", 24 Aug 2022, https://blog.febo.com/?p=891 — read (text; the two ADEV plots of part 3 read by eye; the plots of the first post not read).
- [N8UR-EXC] J. Ackermann (N8UR), "HPSDR Project 'Excalibur' Frequency Reference", febo.com, page last updated 28 Nov 2020 (ADEV plot dated 18 Apr 2009). https://febo.com/pages/excalibur/ — read (text; ADEV plot read by eye).
- [SITIME] SiTime Corporation, "Synchronization System Performance Benefits of Precision MEMS Super-TCXOs under Environmental Stress Conditions", white paper, May 2021, Fig. 3. https://www.sitime.com/sites/default/files/gated/Synch-System-Benefits-MEMS-Super-TCXO-under-Environmental-Stress.pdf — read (vendor material; plot read by eye).
- [REEVE] W. D. Reeve, "Comparison of Inexpensive 10 MHz GNSS Disciplined Oscillators", rev. 1.5, 2 Feb 2023. https://www.reeve.com/Documents/Articles%20Papers/Reeve_GDOComp.pdf — read.
- [DL4YHF-CAL] W. Büscher (DL4YHF), Spectrum Lab manual, "Sample Rate- and Frequency Calibration", last modified 16 May 2021. https://www.qsl.net/dl4yhf/speclab/frqcalib.htm — read.
- [DL4YHF-EBN] W. Büscher, "SL Application: Audio File Recorder for EbNaut", last modified 14 Dec 2015. https://www.qsl.net/dl4yhf/speclab/EbNaut_Rcvr.htm — read.
- [IZ7SLZ] "Notes on EbNaut decode process" (c. 2019). https://qsl.net/iz7slz/DOWNLOADS/0%20Notes_on_EbNaut.pdf — read.
- [BAUDLINE] E. Olson, "Sample rate stability", Baudline application note, 16 Aug 2005. https://baudline.com/solutions/sample_rate/index.html — read.
- [TIMENUTS] B. Camp, "Re: [time-nuts] Beginners GPS locked frequency counter question", time-nuts mailing list, 31 Oct 2015. https://www.mail-archive.com/time-nuts@febo.com/msg75619.html — read.
- [PROTYPOSIS] M. Guggenberger, "High-Precision Audio Drift Measurements with GPS", protyposis.net (undated; example-measurement table). https://protyposis.net/clockdrift/high-precision-audio-drift-measurements-with-gps/ — read.
- [VLFRX] P. Nicholson, *VLF Receiver Toolkit — Notes, for version 0.9p of vlfrx-tools*, PDF dated 16 Mar 2024, sections "Signal streams", "vttime", "Soundcards", "Timestamps and alignment" and "Timing system setup". Archived copy of http://abelian.org/vlfrx-tools/vlfrx-tools-0.9p.pdf (Internet Archive snapshot of 6 Aug 2025; the author's site did not resolve) — read.
- [RIGPIX] RigPix database, Icom IC-7100 and IC-7410 specification listings. https://rigpix.com/icom/ic7100.htm, https://rigpix.com/icom/ic7410.htm — read (secondary for the manufacturer's specifications).
- [ICOM-7300] Icom America, IC-7300 product page, specifications. https://icomamerica.com/lineup/products/IC-7300/ — read.
- [MS-W32TIME] Microsoft Learn, "Support boundary for high accuracy time" (Windows Time service), page dated 12 Feb 2026. https://learn.microsoft.com/en-us/windows-server/networking/windows-time-service/support-boundary — read.
- [DEM5760] Down East Microwave, "DEM Part Number 5760-144 and 5760-144LP: 5.7 GHz Transverters and Transverter Kits", product description (undated), linked from https://downeastmicrowave.com/product-p/5760-144.htm. http://01895fa.netsolhost.com/PDF/5760_PD.pdf — read.
- [OK2AQ15] M. Kasal (OK2AQ), "MW EME Doppler Shift Compensation without Compromise", PDF dated 19 Nov 2015. https://www.radio.feec.vutbr.cz/esl/files/EME/Doc/MW%20Digital%20EME%20Doppler%20Shift%20Compensation%20without%20Compromise.pdf — read.
- [ECHO2022] B. Atkins (KA1GT), C. Suckling (DL3WDG), J. Taylor (K1JT), "Echo Mode in WSJT-X 2.6.0", PDF dated 6 Oct 2022. https://wsjt.sourceforge.io/Echo_Mode_in_WSJT-X_2.6.0.pdf — read.
- [FAIRVIEW] Fairview Microwave, data sheet SMF137S-12, "WR-137 Waveguide Section 12 Inch Length Straight Using UG-344/U Flange With a 5.85 GHz to 8.2 GHz Frequency Range in Commercial Grade", rev. 1.1. https://fairviewmicrowave.com/content/dam/infinite-electronics/product-assets/fairview-microwave/product-datasheets/SMF137S-12.pdf — read.
- [GPSD] G. E. Miller, E. S. Raymond, "GPSD Time Service HOWTO". https://gpsd.gitlab.io/gpsd/gpsd-time-service-howto.html — read.
- [MS-WASAPI] Microsoft Learn, "Device Formats" (Core Audio), 2018, page updated 2025. https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-formats — read.
- [SUB9] "Sub 9kHz Amateur Radio — VLF Amateur News 2011", site associated with G3XBM (reports by M. Vester DF6NM and S. Schäfer DK7FC). https://sites.google.com/site/sub9khz/vlf-amateur-news-2011 — read.
- [ON7YD] R. Strobbe (ON7YD), "Antennas for 136 kHz", last updated 8 Jul 2004. https://wireless.org.uk/on7yd/index.htm — read.
- [WIKI-VLF] Wikipedia, "Very low frequency" (antenna section; cites A. D. Watt, *VLF Radio Engineering*, Pergamon, 1967, and NAVELEX 0101-113, 1972). https://en.wikipedia.org/wiki/Very_low_frequency — read as secondary; Watt not seen.
- [KC3EEY] J. Rizzo (KC3EEY), N. Frissell, "Three-Channel VLF Reception System using a Raspberry Pi, Soundcard, and GNSS Receiver", HamSCI Workshop 2024 poster, https://www.hamsci.org/sites/default/files/pages/hamsci_workshop_2024/posters/P19_Rizzo_3ChannelVLF.pdf; J. Rizzo, "VLF Audio Module for the Tangerine SDR", TAPR DCC 2020, https://files.tapr.org/meetings/DCC_2020/KC3EEY/TAPR%20-%20VLF%20Module%20for%20the%20Tangerine%20SDR%20Jonathan%20Rizzo%20KC3EEY.pdf — read.
- [PINK] "PinkNoise" generator documentation at sampo.kapsi.fi (author name not verified; describes Kasdin's eq. 116). https://sampo.kapsi.fi/PinkNoise/ — read; used only as a secondary description of [K1995].
