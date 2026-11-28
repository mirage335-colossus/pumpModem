

Matrix Modem is a new standalone modem, attempting to learn from the lessons of Fast Modem, Robust Modem (single carrier), and triple Robust Modem.

**Research status.** This is the original design brief. Read the [FSK proposal](matrixModem-FSK.md) alongside the [receiver design review](../_ref/algorithms-matrixModem/fsk-reconciliation.md) for the measured limitations of four-bit processing, narrow-band fading, codeword recovery, false presence and complete receiver service.

As a standalone modem, Matrix Modem should be clearly seprated in the code, and development of Matrix Modem should not change the code of othe other modems.

- Fast Modem is excellent, achieving very near the Shannon limit of bitrate throughput under such extraordinarily challenging conditions as acoustic echo in a large room over physical speakers and microphone. However, Fast Modem does not seem likely to communicate at such low transmit power the carrier would be much less than the noise floor of the bandwidth which GPS DO cannot narrow - although impressive, Fast Modem was not designed as a weak-signal modem.

- Robust Modem (single carrier) is an older implementation in the history of the repository, since removed from active code. A tone was patterned with cryptographically random phase and amplitude shifts, historical search was used to detect the effect on the noise floor. The usable signal-to-noise ratio achievable was impressive, however, the computational ceiling was uncomfortably close for practical use cases, and usability under deleterious (eg. ionospheric) conditions seemed doubtful.

- The triple Robust Modem was an attempt to improve both the computational ceiling and tolerance for deleterious (eg. ionospheric) conditions, through joint detection between multiple carriers, but although simulations do show communications through moderate deleterious conditions is technically possible, the development process was prohibitively difficult, the transmit time required for the combination of reasonably high path loss and deleterious conditions seems much longer duration than necessary, also nearly prohibitive, and the computation is neither as efficient nor as GPU-compatible as hoped.


The goal of Matrix Modem is a vastly more straightforward implementation, much better GPU compatibility, and tolerance for deleterious (eg. ionospheric conditions). 




The expected trouble with this project is just getting some algorithm that can mostly use the multi-petaflop Tensor Core GPU hardware, which previous development efforts have been unable to use 1% of.

This use case is not concerned with computational or compute power efficiency, rather it is well worth all available computational inefficiency to achieve high utilization of the multi-petaflop matrix arithmetic hardware towards achieving a higher computational ceiling to receive difficult signals with limited received coherence, time, energy, etc.

Previous attempts to receive waveforms using GPU acceleration were abysmal. Not only did these not make good use of matrix hardware, but also other operations were only able to use 1/32 worth of a warp, etc.

In the end, a 32core CPU was able to outperform, at about a dozen TFLOPS, the theoretical maximum capability of a mere few TFLOPS, that a RTX PRO 6000 high-end GPU could do for the arithmetic needed.


INT4 arithmetic precision will suffice.


Prefer design choices, reasonable compromises, tradeoffs, etc, to begin with, to make excellent use of the matrix multiplication hardware.


Computational ceiling mostly due to poor utilization has been the bottleneck of previous implementations.

Worse, slow simulation time has drastically slowed down development.

Both transmitter and receiver must be designed to maximize computational ceiling and maximize simulation speed.


- Based on a Tensor Core compatible matrix multiplication operation with a hypothesis and a null hpothesis (eg. where a correlation is expected, vs where a correlation is not expected).


- Spectrum is quiet except for intended signal, so after baseband (eg. audio) is filtered and direct downconverted, all subsequent processing (including the narrowed and filtered baseband itself) will be INT4, etc, precision (depending on hardware support).

- Higher-precision (eg. FP4, INT8, INT16, FP16, FP32) will NOT be used by the software, as it is preferable to emulate lower precision and keep the code lean to ensure easy development for lower precision arithmetic hardware.

- GPU Compatibility
  - RTX 4090 Laptop
  - RTX 4090
  - RTX 4060 Ti 16GB
  - RTX 5090
  - RTX PRO 6000
  - Radeon Graphics iGPU from AMD Ryzen 5 PRO 5650U
  - Intel Iris Xe Graphics iGPU from Intel i9-13900H and similar era Intel UHD Graphics
  - Newer iGPUs

- Full utilization (near 100% busy, 0% idle) of the 'Tensor Core' GPU hardware, especially for the RTX 4090, RTX 5090, RTX PRO 6000, is a high priority.

- C++ for the application, number crunching, etc.

- Overlapping thread scheduler, with more software threads than hardware threads, parallel processing to prepare next batches of parallel processing while previous batches of parallel processing are ongoing.


- Computational ceiling must be maximized.

- Processing should be split up into parallel operations at a very early opportunity.

- Transmit time needed, transmit energy needed, and SNR after path loss, should all be minimized.



- Low-Probability-of-Intercept, which can also help with Low-Perception-of-Interference, is desirable, but maximizing this is not necessary.

- Observer/receiver time should be determined experimentally from the relative likelihood of transmitted paterns recognizable without the encryption key as not background noise (eg. amplitude rise in the noise floor), occurring in a random noise background, at the receiver side (after any path loss, deleterious conditions, etc). The concern is not for whether the transmitter is observable by a close neighbor, but whether it will be noticeable (eg. as perceived interference) at the receiver location and beyond.



- No bidirectional communication or specified length packet processing. Data Pump reassembles bits that were divided in transist (eg. by a modem), without exact bits from other computers meaningfully processed except displaying messages with strictly limited character sets or saving files. This is to prevent the automatic spread of malware, and such safe transfer of data between computers is the primary purpose of Data Pump software.

- Very slow communications also does not practically afford the time for follow up to request  reconfiguration of the transmitter, so the configuration will be well chosen with sufficient margin for a purpose and not changed until communications are completed.

- It is accepted that key reuse can occur in the case of multiple operators transmitting simultaneously. Bidirectional communication or planning, etc, to coordinate safe absolute prevention of key reuse is prohibitively impractical due to more than days of transmit time in important cases, and the more than minimal processing required for such a protocol would be unsafe for Data Pump software to provide given the primary purpose is integrity, and next after that availability, more so than non-catastrophic confidentiality. Operator discipline is expected to reasonably manage this risk. Occasional leaking of some information about short messages is not expected catastrophic. Operators are expected to use short messages to coordinate not to simultaneously transmit large files. Key reuse should not otherwise occur.



- Encryption will always be used, either with the published (part of the software) amateur zeroed key, or with the loaded encryption keyfile, per the user's choice.

- System clock may be used to generate encryption keystreams.

- AES-256 will be used to generate schedules for what transmitter alphabet is meaningful, etc, FHSS hop schedules, etc.

- An AES-256 + Serpent cascade must be used for the confidentiality encryption, and must be provided with 512bits of entropy, preferably originating from such an independent source of randomness as separate audio transmission.

- Keystreams may use the system clock, a non-transmitted CTR, etc. Encryption must NOT add additional bits to the transmission.



- Keystreams keys should differ by shift frequency rounded to the nearest manually configured multiple of 10Hz, but should not differ by FHSS hop frequency. This is for possible use to filter interference as wrong-key admission.



- Keyfile format, generation, etc, should be shared with the other modems.



- To maximize computational ceiling and maximize sensitivity, underlying symbols should have straightforward tolerance to such deleterious conditions as doppler/delay spread, such as using tones without unnecessary phase or amplitude shifts, tones more widely spaced than most typical doppler spread, tones longer duration most typical delay spread, etc.

- Prefer tolerance for deleterious conditions somewhat worse than 50% failure rate, configuration options should preferably amount to a tolerance of a category of typical conditions, not a threshold set at 50% failure rate, but not necessarily tolerant of an entirely different category of conditions.

- Deleterious conditions tolerance should be configurable, maybe default to something similar to 6.25Hz bandwidth and 6.25 symbol rate.



- Helper Carrier is, for this modem, a separate system. The two pilot tone frequency, phase, and timing search narrowing, should be used, if at all, as standalone search narrowing with similar benefit to GPS DO. In the interest of simplicity to ease the development of of the modem itself, do not attempt joint detection between the helper carrier tones and the modem signals. Use at most the guidance and baseband stretching/squeezing from Helper Carrier, to then use the receiver software algorithm independently.



- Total bandwidth should be configurable. A 2.2kHz (450Hz-2650Hz) bandwidth may be preferable, but such a limit is not subject to the 26dB attenuation of the 2.8kHz limit, rather this configurable total bandwidth has more to do with avoiding failure due to typical SSB transceiver built-in frequency filters. 

- The project targets up to 2.8 kHz and requests 26 dB out-of-band attenuation relative to mean in-band power. Treat the 26 dB figure as an engineering target with a specified measurement bandwidth, transmit filter and averaging interval. US Part 97 sets a 2.8 kHz authorized bandwidth for specified emissions on applicable bands. Spectrum containment alone does not qualify the private-key, hopping or one-way profiles for amateur operation. [47 CFR §97.307](https://www.ecfr.gov/current/title-47/section-97.307) · [operating assumptions](../_ref/algorithms-matrixModem/fsk-reconciliation.md#operating-assumptions)



- Data Pump is not yet deployed in the field. Improved implementations should replace existing implementations outright, functionality that will not be used with the newest implementation should be removed.



- HF communications at low transmit power (2mW-100mW), high path loss (>150dB), and typical deleterious (eg. ionospheric) conditions, is an important use case.

- There are some other interesting use cases, but what transmitter power, path loss, deleteritious conditions, are or should be made useful, is much less definitive, and the HF case remains the best example of the combined challenges (eg. transmit power, path loss, deleterious conditions) worse than what any other desirable communications will need to achieve.
  - Sub-9kHz 200ft Antenna
  - Groundwave 1MHz
  - Groundwave 30MHz
  - Skywave 1-30MHz
  - Earth-Moon-Earth



- Some of these configuration options may be relevant, as may be others.

  - FHSS

  - Wrong Key Admission Filtering

  - Keyfile
  - Encryption key entry

  - Baseband Oscillator
  - Shift Oscillator
  - Doppler/delay spread
  - Frequency Selective Amplitude Modulation

  - Helper Carrier Power
  - Helper Carrier Ratio Limit
  - Helper Carrier Callsign

  - Transmit noise

  - Simulation
  - Transmit Power
  - Path loss

  - Simulation Deleterious Conditions Difficulty x(eg. 0.1, 0.2, 0.5, 1, 2, 5, 10)

  - Clock sync, Region, Offset, Audio error
  - Oscillator and Spread Search Margin x(eg. 1x, 3x)

  - SNR Path Loss Margin (eg. 3dB, 6dB, 10dB, 13dB, 16dB, 20dB)

  - Shift
  - Doppler

  - Tone bandwidth (ie. auto, no other options yet)
  - Tone symbol rate (ie. auto, no other options yet)

  - Total bandwidth (eg. 2.2kHz)

  - Audio device
  - Live/Duplex


Tone bandwidth and symbol rate (if relevant - ie. if modem is actually FSK) should follow the doppler/delay spread requirements.





- Please estimate theoretical limits, contemplate whether other approaches, etc, have been fundamentally ruled out, etc, before attempting an experimental implemenation.

- Please regularly make good use of the findings already presented at _ref/ .

- Please regularly continue, cross-check, and reconcile, implementation findings, more research, etc.








All new code, test documentation, etc, for these experiments should be contained under the directory _appraisal/algorithms-matrixModem/ .

All new such research documentation, etc, should be contained under the directory _ref/algorithms-matrixModem/ .






