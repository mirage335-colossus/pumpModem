# Historical specification

This document records the original proposal. The implemented wire format and
stream-ending rules are defined in [protocol.md](protocol.md); old packet and
short-dictionary descriptions below are superseded.


Data Pump




# Purposes


- Transfer of clipboard text, screenshots, and files, without BadUSB, etc, over audio, or audio connected radios with computer control (eg. IC-7100).

- Offer a de-facto standard near-best performance general purpose modem achieving near best throughput without situation specific tweaking, improving orders-of-magnitude general purpose gaps left by FLDigi, WSJT, etc.


- Receiving transmissions from all possible frequencies within a frequency band, changing frequency bands on a schedule if receiver bandwidth is limited, instead of relying on Automatic-Link-Establishment.

- Sub-9kHz messaging.

- ISM band frequency-hopping or >500kHz modulation, 900MHz or 5.8GHz (eg. Troposcatter, Earth-Moon-Earth), messaging, text messaging.

- VHF (eg. business band), text messaging and file transfer.

- Amateur radio (eg. HF, Meteor Burst, Earth-Moon-Earth, VHF, Troposcatter) text messaging and file transfer (eg. CQ, QRS).


- Automatic doppler tracking for rapid uncontrolled frequency shifting is NOT available, especially not by default. For the data pump use case, between nearby computers, similar to WiFi, doppler shift does not occur. For mobile radio commmunication while moving, it is more sensible to use a remote link to the slow communications fixed-location backhaul, and that remote link can use FM, line-of-sight VHF, satellites, somewhat higher power, directional antennas, temporary antennas, or dedicated line-of-sight spread-spectrum communications. For microwave troposcatter or Earth-Moon-Earth, as long as the transmitted frequency does not change greatly during the transmission, while the received frequency may seem slighty out of place, it will be recognized as a single signal.





# Suggestions

- Minimize dependencies. This should work as a single compiled portable binary on MSWindows and Linux, or at least, easily compiled on Linux without fragile distribution specific or not very widely common dependencies. Less dependencies is best.


- This is not a toy. Tunables such as bandwidth and symbol mode are not present for amateurs to experiment with the difference between how well amplitude or frequency shift holds up to some particularly curious noise. Plenty of tools exist to do that already. The modulation scheme is chosen to get close to a point of diminishing returns maximizing throughput at an acceptable error rate without unnecessary fragility, complexity, or electronic design strangeness for a hardware implementation (ie. Phase/Amplitude Differential Shift Constellation Decoder hardware) of the modem.

- Skilled operators using their own equipment, for non-commercial purposes or at least not for devices the users intend to market (which would require a license under Part15), are expected to operate this as open-source software. It is typically their responsibility to know what they consider spread-spectrum or not, whether technical requirements are met, etc.

- Lots of complicated buttons to push to make problems go away, is less desirable than having fewer buttons with the most impact, and showing decoding in-progress before validation unless a non-default setting is checked to hide that. Visibility, transparency, tunables with actual impact, and good source code behind that. Everyone has the option to send AI digging into the code for deeper answers if necessary.

- Too much automation can get in the way of skilled operators, or introduce enough complexity to add additional weaknesses. The 'auto' defaults are expected to just work. Setting the squelch SNR is inappropriate, the quality of the modem should ensure the user will rarely if ever see any mere noise decoded, much less a swamp, while nevertheless achieving within a few decibels of best possibility sensitivity. Perfection is the goal for accurate copy/paste, responsive radiotelegraphy should always at least be able to correct errors after the error-correcting footer arrives.

- Replay attacks are not a significant concern, but the kind of vulnerabilities SSL has had over the years, or waiting too long to display more recent information, definitely is.

- Absolutely preventing any incident of key reuse in this situation is not viable. Sub-9kHz band transmissions as an example can take an entire day to transmit just three bits receivable with decent confidence, and there is no harm in occasional loss of confidentiality of these messages. Rather, the point is to firmly know when a beacon has gone offline for an entire week and needs replacement, etc. Iterative search at higher throughputs, sometimes with much less powerful older computer CPUs, also still necessitates reasonable compromises.

- Disclosure due to key reuse must be restricted to reused keystream positions, without key recovery or prediction of other positions.

- Learning some plaintext or CTR keystream must not give an outsider the ability to produce a valid authenticator for attacker-chosen content.

- Suitably constructed, independently keyed MAC should be used such that the authenticator's security does not require a fresh per-message nonce. HMAC’s inputs are a key and a message; its security is intended to withstand an attacker observing authentic message/tag pairs. Published analyses establish its pseudorandom-function and MAC properties under assumptions about the underlying hash construction.


- Please avoid unnecessary 'non-standard cryptography' unless it is actually necessary to both risk deviating from widely accepted practices and incurring a need to notify the BIS, consider EAR, and other similar regulations, just to push to GitHub.

- Please avoid implementing features that would cause any difficulty releasing this software or specifications, etc, as a public open-source resource in the US.

- Please ensure the disclaimer which is intended to address issues regarding open-source public release remain accurate,  such that public open-source release remains reasonably unencumbered.





# Computer I/O Protection

- Preventing BadUSB, deeper equivalents of BadUSB made possible by the presence of occasional internal bus corruption, similar misuse of sometimes programmable microcontroller interfaces (eg. UART), etc, is the most important purpose of a Data Pump.

- USB, Serial Port, Parallel Port, UART, SPI, I2C, SD card readers, Ethernet, SATA, PCI-E, GPIO - data or subsequently corrupted data across such interfaces could be misinterpreted as keyboard keystrokes, commands, or packets routable to internal network services, etc, which is a serious malware vector.

- Audio, Software-Defined-Radio, other Analog-Digital-Converter with attenuation to prevent saturation, Shielded Serial every-other-bit-randomizer filters, can prevent raw data from appearing on trusted peripherial busses (eg. USB keyboard), internal busses (eg. PCI-E USB device), programmable UART, programmable GPIO, routable network interfaces (eg. reaching internal network services), or complex driver processing (eg. buffer overflows in routable packet processing) until the data has been decoded by software, preventing data or subsequently corrupted data from misinterpretation as keyboard keystrokes, commands, or packets routable to internal network services, etc, which is a serious malware vector.

- Data must not be received through signaling interfaces which carry risk of BadUSB.

- Software modem processing, using an Audio, Software-Defined-Radio, other Analog-Digital-Converter device, with attenuation to prevent saturation, is preferred, as the raw bits received on the protected computer through USB, etc, are completely different than what may be transmitted by malware attempting to spread, and thus BadUSB cannot occur.

- Hardware modem processing, in which angle/phase/frequency or amplitude shifts have already been converted to digital bits, must be followed by Shielded Serial every-other-bit-randomizer filters, to ensure bits received on the protected computer through USB, etc, are completely different than what may be transmitted by malware attempting to spread, to prevent BadUSB.


# Cache

- Received data is kept in memory and not saved to file. GUI provides copy/paste of short text, offers to save downloaded files, etc.

- Default data memory buffer is 256MB .


# Keyfiles

- Keyfiles must consist of a large (ie. 128MB) header of random data, which is hashed to generate a symmetric encryption/decryption key for the appended actual keys (eg. cascade AES256-Twofish256-Serpent256 keys, MAC keys, separate data and spread-spectrum keys). This is to improve the chances of successfully erasing enough of a key, particularly from Solid-State-Disks with wear leveling.

- If a very large (ie. >1GB) pad keyfile is present, then all keyfiles also hash the contents of this to generate the encryption/decryption keys for actual keys. This is to improve the chances of successfully erasing enough of the keys on a computer, if the individual keyfiles might not be large enough to guarantee this for some Solid-State-Disks with wear leveling.

- Signature and signature verification keys, used with unencrypted messages, or keyfiles used by other software that may add additional encryption, authentication, signatures, such as GPG, shall NEVER use these keyfiles.



# Repeatable Packets

- Packets are NEVER routeable, does NOT include address information.

- Repeatable packets must have a checksummed unique randomly generated packet identifier and a request to repeat flag. This prevents frequent corruptions from frequently generating more packets to repeat.

- Repeaters are not implemented as part of the data pump, rather, the GUI enforces limits when 'repeatable' is enabled. Users desiring repeaters are expected to script a repeater using the CLI functionality.



# Packet Format - Very Slow Status Messages

- Very Slow Status Messages consist of only a call sign, which may be as brief as a few bits. Throughput to transmit these few bits is imperative - sub-9kHz band transmissions can already take almost an entire day to transmit just three bits, transmission time more than one day is unacceptable.

- End-users are expected to choose two frequency ranges for such call signs - one of which signals normal presence, the other signals distress. This choice is arbitrary, and up to the end-user. Coordination of such frequency ranges for non-encrypted use is expected to happen, if at all, as a voluntary community concensus.

- Unencrypted unknown (new) callsigns are only detectabe if either the callsign itself is low-entropy content (eg. frequently repeating), or if the symbols are decoded much above the noise floor.

- Known callsigns can be filtered for, and shown to the user upon any detection.

- Encrypted callsigns are known.

- Encrypted Very Slow Status Messages consisting of only a few bits are possible due to the use of ciphers as stream ciphers.

- MAC-then-Encrypt or other authentication is NOT used - Very Slow Status Messages do not have the needed throughput, and blind tampering of such slow short messages is practically inconsequential.



# Packet Format - Text Messaging, File Transfer

- Signing, if used, would be part of the user input text, files, etc, corrected by the Reed-Solomon error correction, and never signing the preamble.

- DC Balanced repetitive text preamble length corresponding to at least 5 seconds to push the receiver Automatic Gain Correction, Constellation Decoder, etc, to an appropriate range.

- Preamble is not signed.

- Preamble is not corrected by the Reed-Solomon error correction.

- Message Authentication Code (MAC) appended to data (ie. strongly unforgeable MAC-then-Encrypt).

- Reed-Solomon error correction appended to data.

- Encryption (ie. symmetric Authenticated Encryption possibly with an asymmetric layer), if used, encrypts all plaintext, including the preamble, Reed-Solomon error correction, etc - there must not be any non-ciphertext in the transmission.

- Statistical confidence in a valid message (ie. modem digital squelch) is inferred, after encryption if used, from the Reed-Solomon error correction.



# Compression

Messages, Text Messages, and File Transfers, below 256bytes of text, use a predefined compression dictionary, optimized for the shortest messages with the most common alphanumeric characters and words having the fewest bits.






# Encryption

- Received symbols are buffered, the best match is iteratively found by comparing scrambler constellation decoder Signal-Noise-Ratio for the best possible result across a buffer of up to billions of symbols (the entire drift buffer length, as much as 32768seconds worth). Any real-time display until signal reception has ceased, either on determined by very high statistical confidence or by error correcting code, will replace or delete any unvalidated text message or available file to download, so only the best (or if error correction is used, only perfectly copied), result will remain.


- Keystream generators begin with the current CTR values must generate identical keystream output for the same key. This should be achieved by starting the first CTR value on a second with zero nanoseconds, subsequent CTR values must use the same consistent accounting for the timing of subsequent samples per the symbol or bit rate in the transmitted data.

- Iterative search is used, configurable, up to +/-32768seconds, to find matching decryptable signals in noise. All cipher keystreams are shifted in time simultaneously by the same number of nanoseconds.



- Symmetric encryption uses a block cipher or block cipher cascade in CTR mode, with a non-repeating Iternational Atomic Time (TAI) or approximation (eg. UNIX time based CTR counter), and key derivation, ensuring statistically independent keys are used for different keystreams, such as different purposes or different times.

- Asymmetric encryption will NEVER be implemented here - without very substantial random padding, using the public key to encrypt predictable timestamps amounts to, at best, a mere symmetric cipher, with the public key having become just as sensitive as the private key. Due to the need to transmit or establish very large public keys, orders of magnitude more computation, and the larger attack surface of a much more complicated codebase, asymmetric encryption is only appropriate if the high-throughput for file transfers is available, and beter implemented by other programs (eg. GPG, SSH) interacting with the data pump software CLI.

- Asymmetric encryption for Low-Probability-of-Intercept key exchange by pure ciphertext asymmetric encryption, will NEVER be implemented here. Fundamentally, without encrypted patterns for only intended participants to decode as bits, at least one participant must either retransmit multiple times to overcome errors, or use a very positive Signal-Noise-Ratio (eg. long tones or repeating patterns), or use clearly visible error correction patterns (eg. Reed-Solomon). Implementing a trusted repeater to receive and iteratively decrypt after transmitting its own public keys, is complex, fragile scripting, which should NEVER be part of data pump software, and is better implemented by a separate software project scripting around the data pump software CLI.


- Encryption is used for the data pump to generate cryptographically random keystreams to mix for all encryption, decryption, and Low-Probability-of-Intercept purposes. 

- Four keystreams are generated:

  - Frequency Hopping Spread-Spectrum (FHSS) - Defaults to zeros (no hopping).

  - Direct Sequence Spread-Spectrum (DSSS) - Iteratively synchronizeable by shifting.
  
  - Scrambler Table - Reconfigures the scrambler constellation decoder which converts baseband (ie. phase/amplitude) symbol long patterns to bits.
  
  - Data Encryption - Relatively slow rate per-bit decryption of the actual data. This is the only actual encryption of data for confidentiality rather than Low-Probability-of-Intercept.


- To prevent a likely case of frequent accidental overlap and substantial cipher keystream reuse, due to a leap second or clock synchronization, GUI and script wrappers are expected to have a configurable delay (default 6seconds) before another transmission is allowed.


- If it does not weaken security to do so, separate keys should be used for such purposes as Encrypt and MAC. The keyfile format can decrypt many keys, and key derivation functions can also produce more statistically independent keys. Key derivation and such is not computationally expensive, and the basic encryption operation from there for iterative search is computationally efficient.


- Occasional text messages being statistically comprehensible due to rare accidental keystream reuse due to a group of persons using the same shared key transmitting simultaneously, should be operationally inconsequential. Transferring large files simultaneously should be avoidable by through some discipline among participants to coordinate to only transfer one such file at a time. Much of the benefit of the encryption is more to prevent any tampered raw data from reaching a computer as another vector for malware. By itself, the rare leakage of some information about a text message, or possibly that actual message, should be a minor risk compared to the prohibitive cost of sending additional random padding at very slow throughput and the exponentially large computational cost of iterating through random padding decryption on top of clock drift.

- On the rare case that text message transmitters do use the same key, resulting in ECB-like vulnerabilities, it is important the ciphers chosen will not be compromised such that other communication is similarly insecure (eg. that the underlying key or keystreams from incrementing CTR values, etc, cannot be inferred). It is critically important that the cipher chosen (eg. AES-256) must not have disadvantages from ECB mode other than lack of diffusion, as occiasional accidental keystream reuse is expected as an unavoidable likely outcome, and this must not be catastrophic.

- Replay attacks are already prevented by the limited clock drift search window. At higher throughputs, newer information with less drift will be presented to users more readily. At lower throughputs, the entire clock drift search window will have expired before the message has been completely transmitted to replay.


- Symmetric ciphers used must have a key length of at least 256-bits, and comparable reputation for security to the three AES finalists - AES256, Twofish256, Serpent256.


- FHSS could use a repeating pseudorandom function instead of a cryptographically random bitstream, but there is no situation in which it would make sense to do so - the only use for FHSS is to specifically meet regulatory requirements for ISM bands, and encryption in ISM bands is allowed.




# Modem

- Received symbols are buffered, the best match is iteratively found by comparing scrambler constellation decoder Signal-Noise-Ratio for the best possible result across a buffer of up to thousands of symbols. Any real-time display until signal reception has ceased, either on determined by very high statistical confidence or by error correcting code, will replace or delete any unvalidated text message or available file to download, so only the best (or if error correction is used, only perfectly copied), result will remain.

Modem has five foundational layers.

  - Frequency Hopping Spread-Spectrum (FHSS) - Requires computer controlled hardware radio front-end retuning (eg. IC-7100, SDR). Defaults to zeros (no hopping). Alternatie setting is 0.4s dwell and 75 channels.

  - Direct Sequence Spread-Spectrum (DSSS) - Iteratively synchronizeable by shifting.
  
  - Phase/Amplitude Differential Shift Constellation Decoder (eg. QAM-256, APSK-256).
  
  - Scrambler Table Pattern Constellation Decoder - Converts baseband (ie. phase/amplitude) symbol long patterns to bits (eg. 00100010=0 vs 11011101=1 if not rotating through encrypted patterns for Low-Probability-of-Intercept).
  
  - Data Encryption - Relatively slow rate per-bit decryption of the actual data. This is the only actual encryption of data for confidentiality rather than Low-Probability-of-Intercept.


Frequency Hopping Spread-Spectrum (FHSS), Direct Sequence Spread-Spectrum (DSSS), Scrambler Table Pattern Constellation Decoder, Data Encryption - each of these are scheduled symbols or assigned lookups based on encryption keystreams. All encryption keystreams will have the same nanoseconds drift, which may be adjusted if encryption is enabled.

Phase/Amplitude Differential Shift Constellation Decoder, will have a number of nanoseconds drift. If encryption is enabled, this drift may or may not be useful to set the nanoseconds drift adjustment of the encryption keystreams as well.

Iterative search, if decryption is used, may find the best match, the greatest distance between constellation points at the Scramble Table Pattern Constellation Decoder, in up to +/-32768seconds if encryption is used. If encryption is not used, the configurable limit should be at most +/-120seconds.

Always, whether encryption is enabled or not, the Scramble Table Pattern Constellation decoder is the benchmark for drift adjustment. The best drift adjustment will always be achieved when the Signal-Noise-Ratio at the Constellation Plot of the Scramble Table Pattern Constellation is best.


Signal-Noise-Ratio must NEVER be low at the Scrambler Table Pattern Constellation Decoder, and this can be used as squelch. Low Signal-Noise-Ratio at the Phase/Amplitude Differential Shift Constellation Decoder is a normal situation.


Scrambler table may be set to '0' vs '1' for minimum duration phase/amplitude shifts, or set to '0000' vs '1111', etc, for very narrowband dwelling on long symbols similar to long simple tones. One limitation is that if a long single symbol is used, this must not exceed the clock drift (aka. phase noise) such that the shifted phase is buried in this randomness. Very Slow Status Messages (eg. Sub-9kHz, low-power Earth-Moon-Earth, low-power Troposcatter) can be unusable is so configured. Such proposed patterns as 00100010 vs 11011101 are tentative, and should be replaced with the most ideally performing alternatives, possibly a much more complex lookup table. Obviously the encrypted lookup table must rotate through pseudorandom, this can be a subset of pseudorandom patterns.



- Additional layers, such as an interleaver, convolutional decoder, trellis coding, etc. Messages as short as a few bits should be able to use these features if possible. If these layers require messages longer than a few bits, it should be configurable to disable this extra overhead.

- Convolutional decoding, etc, should be used if this will be able to improve error suppression to the point that the overall throughput can be pushed higher by not overwhelming the Reed-Solomon error correction, which is still needed to prevent clipboard text errors, or broken transferred files, due to interruptions by strong noise sources (eg. lightning strikes).

- Soft decisions may be inherent to the iterative search. Decoders that drift along with inputs can drift too far and get stuck making bad decisions for a while, whereas iterative search may be able to better find the most optimum synchronization.






# GUI

- GUI is a front-end only. All functions used for modem, soundcard, QR encoding, etc, must be available as command line flags from other binaries (eg.  echo test | modem --tx --snr=-60dB/1Hz --bw=1MHz --device-type=sdr --device=default --keyfile_symmetric=recipient.pub  ).

- FHSS, DSSS, are automatically greyed out if encryption is not enabled.

- Bandwidth and SNR Ratio setting controls the auto keystream/pattern/tone length for the scrambler.

- QR Code shown, updated on every keystroke into the input box for the proposed text message, is an alternative data pump technique, relying on the inherent optical data diode and some opportunity for human observation to prevent malware misuse. QR Code need not be usable for more than 500character length text.


- Simulation is loopback, and very important. The noise floor should be realistic, maybe an order of magnitude worse than typical radio receiver or audio amplifier thermal noise.



```

< callsign (optional) > < grid locator (optional) > <repeatable yes/no> <HF/ALE Cycle dropdown checkboxes: none, 10m, 20m, 40m, 80m, 160m> <simulation toggle checkbox: no (default), 3dBm -6dB, 3dBm -60dB, 3dBm -90dB, 3dBm -120dB, 3dBm -170dB, 3dBm -200dB, 3dBm -230dB, 50dBm -200dB, 50dBm -270dB, 70dBm -250dB >

<hide until full decrypt dropdown: off (recommended), on (paranoid)>

<selectable encrypted recipients list: none, etc> <distress range dropdown: none, lowFreq, midFreq, highFreq> <status range dropdown: none, lowFreq, midFreq, highFreq> <signal browser window - click to copy> <file list> 

<text message> <file upload> <QR Code (Level L) showing pasted text> <tx toggle dropdown: on Enter (recommended), on Ctrl+Enter>

< spectrum/amplitude waterfall > <waveform> <Highest SNR Phase/Amplitude Differential Shift Constellation Plot> <Highest SNR Scrambler Table Pattern Constellation Decoder Plot>

<received/transmitted bit rate> <timing search diagnostics> <CPU usage>

<device type dropdown: audio, SDR, IC-7100> <device: default, audio HDMI, audio speakers, etc> <bw dropdown: 1.2kHz, 2.4kHz, 22.05kHz, 24kHz> <snr dropdown: 6dB/1Hz, -6dB/Hz, -60dB/1Hz> <FHSS dropdown: off (recommended), 0.4sDwell 75channel (ISM)> <Scrambler dropdown: auto keystream (recommended, greyed out if encryption is disabled), auto pattern, auto tone, force 3bit pattern, force 4bit pattern, force 6bit pattern, force 8bit pattern, force 12bit pattern, force 16bit pattern, force 1bit tone, force 2bit tone, force 3bit tone, force 4bit tone, force 8bit tone, force 32bit tome, force 128bit tone, force 1024bit tone, force 4096bit tone, force 16384bit tone> <overhead dropdown: auto (default), force off> <error correction dropdown: reed-solomon 20%, reed-solomon 60%, off>  <parity dropdown: low density parity auto, trellis 2d, trellis 4d, none>



```









# Disclaimer

Data Pump is an independently developed civilian modem project for text and file transfer, with deliberately limited interfaces and functionality. It excludes routed addressing, built-in repeaters, asymmetric key exchange, and rapid uncontrolled Doppler tracking. Those exclusions document the project actually being developed; they are not presented as proof that no military user could ever use it.

No applicable USML classification has been established merely by identifying the project’s LPI, scrambling, or interference-resistance functions. The relevant inquiry must address the actual modem and the complete control entry. Its documented public technical sources also provide substantive grounds for applying the public-information exclusion, independently of whether the disclosed techniques could serve military purposes.


Substantive limitations inherent to the project are a poor choice for the C3, C4, or C4ISR purposes it was clearly not designed for, not developed to implement those systems, and deliberately omitting certain capabilities. It should be clear this software is intended for some amount of long-term confidentiality, long-term availability, and especially long-term integrity, for which a large attack surface or minimalist implementation is unacceptable. Those concerns about very high quality enduring security, are nonexistent and conflict with the needs of a short-term real-time use case. Different priorities and deliberately different functionality.

The project was developed as a narrowly scoped civilian data-transfer modem, not to implement or integrate a military command, control, identification, or tactical-networking system.

- Routable addressing in particular is a serious malware risk for civil technology development workstations which other computers are loaded with software from, but responsive routable addressing is highly desirable and not a serious risk to mobile end-users able to physically get to a few downstream misbehaving computers. Scripting around this modem to support routable packets would be doing that packet by packet, developing specialized software for this more forgiving use case would be simpler.

- Uncontrolled Doppler tracking in particular could be essential for C3, C4, or C4ISR purposes, the usability issues of needing to use separate interfaces for mobile personnel or accepting some responsiveness delay due to scripted repeaters could be prohibitive. Specialized software for this more forgiving use case would be simpler to develop - analog FM radio is often sufficient.

- Asymmetric encryption, more elaborate key exchange arrangements, built-in repeaters, are relegated to separate applications.

Already publicly known. Does nothing new either in components or in the assembled form. This modem is optmized for usability for particular use cases and for throughput for those use cases.

* FLDigi and some other software is already useful as a data pump, but merely slower due to being less optimized for this case.
* LPI approach of using a cryptographic cipher to generate any of these spreading codes, has been widely described and understood in public literature.
* Pattern symbols are essentially a scrambler/descrambler, and the use of cryptographic codes to change the scrambler/descrambler of a radio modem is already publicly known. In this case this is done to resemble possibly relevant working principles and any weak-signal benefits, etc, of Olivia MFSK.
* Published patents seem to exist, in particular US6252962B1, "Featureless covert communication system".
* Actual implementations of encryption based scrambling with clear functional resemblence have apparently been published, as at least a description sufficient to implement the GNU Radio based software, from the 2012 paper "Obfuscating IEEE 802.15.4 Communication Using Secret Spreading Codes," by Muntwyler, Lenders, Legendre, and Plattner.

Not technical data per § 120.33 Technical data .

* Each point under 22 CFR 120.33(a) either requires a clear relation to defense articles, defense services, or an invention secrecy order.

A blanket reading that captures every civilian noise-tolerant or low-detectability modem, by itself, would seem to also capture a flashlight with a directional light filtering covered used in a paintball game, which seems an unjustified conclusion, particularly given the overall text around Category XI(a)(5) .


No applicable USML classification has been established, and the relevant techniques have substantial, identifiable public disclosures.







