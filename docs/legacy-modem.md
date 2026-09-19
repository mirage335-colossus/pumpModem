# Legacy Modem

Choose **Legacy Modem** in the top-left Modem dropdown. This is a separate
simplex radio-text console with BPSK31, BPSK125 and Olivia-4/2k. The carrier
frequency defaults to 1500 Hz. Reception starts when Legacy obtains the audio
device. The upper text area shows received characters and characters sent by
this station as they become available. The lower area holds text to transmit;
**Transmit** sends the current draft and clears its submitted text after
successful playback. TX text echoes into the transcript as audio is generated;
a playback error retains the draft because the hardware API cannot acknowledge
individual characters. Text appended while transmitting remains for the next transmission.
A waveform's standard startup and trailing idle symbols take additional time.
Reception resumes after playback closes. The waterfall uses actual audio.

Legacy uses the default audio device and the existing right-channel/mono audio
routing. Leaving Legacy closes its audio operation before another modem can
acquire the device. Its draft, selected modulation, frequency and transcript
are independent of Robust and Fast settings. The console accepts text only;
it has no file, packet, regular-interval, encryption, link-planner or simulation
controls. The 32,768-byte draft and bounded transcript are local storage limits,
not wire lengths. Olivia's intrinsic Walsh coding and PSK's standard varicode
are parts of those radio formats, not the existing Data Pump transport.

## Radio formats

The implementation uses 8000 Hz logical PCM, with the unchanged hardware audio
layer performing sample-rate conversion if necessary.

* **BPSK31:** 31.25 symbols/second, differential binary phase shifts, cosine
  transition shaping, standard PSK varicode and two-zero character delimiters.
* **BPSK125:** the same alphabet and differential signaling at 125 symbols/second.
* **Olivia-4/2k:** four tones at 500 symbols/second, Gray mapping, two interleaved
  scrambled 64-symbol Walsh lanes, with the standard overlap-shaped tones.
  At 1500 Hz the tone centers are 750, 1250, 1750 and 2250 Hz. In FLDigi select
  custom Olivia with four tones and 2000 Hz bandwidth.

The codecs are independent implementations of the published signal formats.
They do not load or embed FLDigi code. The on-air text alphabets are byte based;
matching endpoint text encodings are required for non-ASCII text. FLDigi's
Olivia extended-byte escape is supported; its reserved ASCII 0–7 and DEL
control values cannot be submitted as text. This is continuous radio text, so
received characters are not authenticated and noisy text can contain errors.

Primary references:

* [ARRL PSK31 specification](https://www.arrl.org/psk31-spec)
* [FLDigi PSK documentation](https://www.w1hkj.org/FldigiHelp/psk_page.html)
* [FLDigi Olivia documentation](https://www.w1hkj.org/FldigiHelp/olivia_page.html)
* [FLDigi source reference](https://github.com/w1hkj/fldigi)

## Regression-only channel

`tests/legacy_simulation.hpp` is a sampled-audio test fixture. It is not linked
into a production GUI, CLI or audio session. Its sole variable channel setting
is fixed SNR; a seed makes noise repeatable. It uses the
[FLDigi LinSim convention](https://www.w1hkj.org/files/test_suite/guide.html):
400–3400 Hz band-limited Gaussian noise, with S/N in dB relative to the input
audio signal power. A fixed FIR supplies that noise band. This is distinct from
FLDigi's live PSK S/N meter, which estimates a mode-dependent ratio of spectral
bins, and from Data Pump's Robust dB-Hz link model.

The fixed PSK125 regression design reference is 0 dB, chosen from the sampled
receiver's complete-text transition between −5 and +5 dB. This is a regression
anchor, not a guarantee of complete-text reception or measured hardware
sensitivity. The sweep spans −25 through +25 dB at 5 dB increments, within
25 dB of that reference, and exercises the actual sampled receiver. It records
exact-text successes on both sides of the failure region. No propagation,
power, path-loss, oscillator, weak-signal probability or LPI model is used.

## Isolation and checks

Production code lives in `include/datapump/legacy` and `src/legacy`, with its
own library, settings, receiver, transmitter and audio session. Existing modem,
transport, compression, key, crypto, physical-completion and pending-bit code
is unchanged. A build-time `legacy_boundary` check rejects imports between
Legacy and the existing modem implementations. The only shared runtime
facility is the unchanged audio hardware API. GUI mode hosting and generic
read-only text presentation are shared by the native adapters.

Run the focused tests with:

```sh
ctest --test-dir build --output-on-failure -R '^(legacy_.*|gui_legacy.*)$'
```

Independent wire vectors and FLDigi-generated waveform fixtures accompany the
sampled tests. Session tests replace hardware only, checking incremental text
and exclusive receive/transmit ownership. The existing development-contract
and Fast suites remain necessary to verify isolation. See
[validation](validation.md) for the actual test runs and their limits.
