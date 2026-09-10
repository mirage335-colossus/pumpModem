# Implementation coverage

This matrix records version 0.3 behavior and its limits. It is not acceptance of
all performance and hardware claims in the original design. Adaptive audio peers
need matching 0.3 settings; packet versions 1/2 and existing keyfiles are unchanged.
[Test execution results](validation.md) distinguish measured results from design
and source-level checks.

| Requirement | Implemented behavior / current boundary |
| --- | --- |
| Portable compiled modem | C++20 CLI and native FLTK GUI share the transfer/streaming service and OpenSSL 3. Linux ALSA is dynamically optional; Windows uses WinMM. No Python or Tk dependency. Windows hardware validation remains separate. |
| Offline installation transfer | CMake/CPack collects compiled executables and native libraries. Bundled FLTK, OpenSSL and release C++ runtimes default to static linking. Moving a release needs no interpreter or package download. OS/CPU/glibc compatibility limits are in [offline-installation.md](offline-installation.md). |
| Text, screenshots and files | Text compose and strict UTF-8 clipboard copy; binary file and saved-screenshot attachments. Received text stays in the ticker; only files/screenshots appear in the explicit-save list. No built-in screenshot capture. |
| Interface isolation | PCM or analog audio only, with OS-default device and optional override. No network listener, serial modem substitute, content execution, routing or automatic file opening. Hardware isolation is external. |
| 256 MiB receive content | Bounded RAM cache with ID replacement and eviction. Streaming DSP has an independent default 64 MiB budget; packet scratch has checked content-derived bounds. Neither number is a total process RSS guarantee. |
| Large keyfile and pad | Production 128 MiB random header wraps an authenticated collection of 1..128 named complete five-purpose keysets. Legacy keyfiles load as Default. Optional large pad support remains in CLI. No physical-erasure guarantee. |
| Repeatable messages | ID and repeat flag protected by packet integrity/authentication. Allowed when incremental encoded content takes at most 2 seconds, or original content has at most one byte. Fixed training, framing and metadata are excluded. No arbitrary 64 KiB cap or built-in repeater. |
| Training and protected packets | Independently timed five-second default preamble, even for longer payload symbols. HMAC before body RS, byte interleaving, whole training/frame/parity encryption. Plain packets use SHA-256 integrity. Bootstrap remains RS protected with body FEC off. |
| Short compression | Fixed legacy dictionary and version 2 variable-length prefix codec for content below 256 bytes. Common bytes can use 3-bit codes; arbitrary data remains lossless. Compression is selected only when shorter. |
| Cipher streams | AES-256-CTR, HKDF purpose/epoch separation and independent HMAC. Data, DSSS and Scrambler purposes are connected; FHSS purpose is reserved without RF hopping. |
| Time and key search | Finite timing hypotheses at one configured carrier. Live receiver bank covers loaded keys and whole-second candidate epochs, default ±6 seconds, API maximum ±60 seconds subject to aggregate workspace. No unlimited clock search or nanosecond time discipline. |
| Transmit policy | One active GUI TX. Six-second cooldown applies to actual encrypted output; simulation and plaintext output have no cooldown. This does not coordinate separate processes or hosts sharing a key. |
| Phase and amplitude modulation | Shared differential 4/8/16/32/64-APSK with two through eight amplitude rings, at most eight phase positions and two through six bits per symbol. Equal average power and Gray adjacency across profiles. Independent keyed spreading and named pattern/tone choices remain. |
| Automatic weak-signal duration | Bandwidth and target C/N0 determine constellation density and integration using geometric noise/drift margins. The highest modeled rate meeting the margin wins. Automatic duration can exceed 16,384 chips. Blind protected-bootstrap acquisition avoids a mandatory five-second training gate. Finite numeric/workspace/timing limits remain. |
| Long-tone memory behavior | Continuous TX/RX and accelerated simulation process bounded chunks/integrals, retaining packet content separately. They do not allocate PCM for the full duration. Legacy WAV/vector operations remain batch and can reject large waveforms. |
| Near-best throughput | Planner maximizes gross rate over the supported APSK profiles with explicit engineering margins. At least two symbols carry one byte; phase density stays bounded. Capacity optimality and calibrated sensitivity are not established. No LDPC, adaptive equalizer or oscillator drift tracker. |
| Bandwidth | Nominal 1..192000 Hz planning sets the internal clock and carrier independently of hardware. Audio negotiates the selected card's supported rate and performs bounded band-limited interpolation/decimation. The GUI exposes physical passband limits. Rectangular pulses retain sidelobes, without a certified spectral mask. |
| Few-bit status | Exact overhead-free DBPSK and known aligned correlation. No authenticated identity, unknown-beacon discovery or automatic normal/distress monitor. Long integration alone does not establish reliable day-long reception. |
| Multi-signal reception | Continuous sequential reception at the selected carrier with a finite acquisition/key/epoch bank. No whole-band simultaneous decoder or RF retuning control. |
| Provisional text | Incomplete corrected frames can produce bounded mutable previews. Pending rows remain labelled and cannot trigger copy/save. Only full packet verification supplies received content. |
| Diagnostics | Continuous waveform, FFT waterfall, amplitude-preserving constellation, frequency-labelled scrolling text, nonzero slow-rate formatting and rate/CPU/virtual-time progress. Simulation holds a payload-midpoint frame for two seconds and retains up to 2,048 received constellation points afterward. No second synchronized scrambler constellation. |
| Simulation | CPU-bounded virtual time with shared waveform/decoder and integrated AWGN. Ideal carrier and symbol timing; no wall-clock wait proportional to tone length. Idle noise continues and actual capture resumes after hardware TX. Named dBm/attenuation presets use -174 dBm/Hz plus 10 dB noise figure, normalized to Fs/2. |
| Physical channel effects | Separate raw PCM tests and batch channel cover arbitrary sample delay and fixed frequency shift. Accelerated simulation excludes arbitrary timing errors, fading, resampling drift and hardware nonlinearities. Extreme presets can fail. |
| QR Level L | Vendored encoder, UTF-8 ECI, up to 500 Unicode scalars; compose preview and SVG/PBM CLI output. Independent decoding history is recorded in validation results. |
| Explicit exclusions | No asymmetric key exchange, built-in repeater, routable address, rapid Doppler tracking, SDR/FHSS or IC-7100 control. No legal classification claim. |

Acceptance of sensitivity, capacity, RF compliance, adversarial security or
hardware isolation requires an agreed measurement procedure and intended-device
results. Deterministic software loopbacks cannot establish those properties.
