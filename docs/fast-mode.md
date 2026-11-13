# Fast text and file transfer

Fast Modem is a separate streaming QAM/LDPC and classic APSK modem. Ordinary
desktop launches select it with Speakers / mic · short and listen continuously.
The modem dropdown lists Fast Modem, Robust Modem and Legacy Modem. Explicit Robust simulation or
settings launches retain their workflow. Console opens first; Developer mode
reveals Modem details with manual symbol rate, constellation and coding controls.
Channel profile and Expected SNR sit below the diagnostic plots. Cable defaults
to an expected 36 dB SNR; speakers/microphone defaults to 3 dB and its short
profile to −6 dB. Switching channel
profiles reapplies that profile's default SNR and waveform settings.
The six receiver-assumption fields appear together in the compact Console row.
Their drafts are retained independently for each channel profile.

In ordinary **Live audio**, enter a Message or Attach file, then transmit. Use
text restores the retained draft. Sending pauses listening and resumes it
afterward, including after
cancelling transmission. Cancel applies to transmission; listening needs no
separate control. Encryption is optional and initially off;
enabling it requires a key. Both peers need matching local settings and source
format, including encryption and the same key material when enabled. Save becomes
available only after physical completion and integrity checks. Encrypted transfers
authenticate; public transfers only check checksums. Existing destinations are
never overwritten.

Text is limited to 32,768 source bytes, including UTF-8 bytes. The GUI accepts
valid UTF-8 without NUL; files preserve arbitrary binary data. Text and files use
XZ compression. File sources begin with the bounded application prefix
`#ATTACHMENT### filename.ext #ATTACHMENT### ` before compression; text sources
have no added prefix. Only a valid prefix at byte zero identifies an attachment.
Its filename is a suggested Save basename, never a destination path. The
prefix is interpreted after decompression and physical completion, and is
excluded from saved file bytes. XZ metadata, including decoded block
sizes, is interpreted only after physical completion; it never chooses modem
framing, allocation or completion. UTF-8, newlines and file bytes keep their exact
values after compression and bounded post-completion decompression.
The GUI Signals browser shows received signals only. A single click copies a
completed text message; Paste as message restores it to the composer. Files in
memory lists completed attachments with their filenames, excluding text messages.
Pending rows remain unavailable for copy/save. QR brightness offers Normal, Dim,
Dark and Off. The CLI reports counts and integrity.
Switching Text/File retains both drafts. Loading a key does not enable
encryption; switching encryption off retains the key for later use.

Fast settings, key selection, text/file drafts and history remain independent
of other modem settings and drafts. Robust uses its own native session and
configured-calendar controls. Shared mode handoff coordinates audio ownership:
starting Fast hardware waits for the Robust device to close and refuses to
interrupt an active Robust transmission. Switching away requests suspension
of Robust reception; an active transmission can continue while its interface
is hidden.

An active Fast transmission or duplex loopback test continues when switching
to another modem; receive-only Fast listening stops and releases its audio path.
Regular controls remain disabled until Fast releases the audio device; idle
regular capture resumes when Fast is inactive and the regular view is selected.
Starting Fast allows up to two seconds for asynchronous device release, then
reports if audio remains unavailable. Automatic listening retries after a short
delay; a blocked transmission can be retried when regular work is idle.

Fast offers optional encryption and authentication, but no LPI claim, spreading, pattern
codewords, time/key search or single-bit recovery. Text and files use
[streaming XZ source compression](fast-xz.md). Its known
training, markers, pilots, occupied spectrum and transmission duration are public.

## Fast simulation

The Fast **Simulation** page offers **Live audio**, **Simulation**, and
**Live loopback test**. Live audio is the default and uses the selected audio
device for ordinary transmission and reception with clean application PCM.
Receiver declarations still determine the local search bounds and automatic
profile in Live mode; they do not inject synthetic effects into the audio.
Simulation sends the Console message or file through the native Fast sampled
channel and receiver entirely in software, without opening hardware audio.
Its results are labeled separately from live audio.

The compact Console row declares baseband and shift oscillators, Doppler and
delay spread, selective AM, receiver search margin, and RF shift coordinate.
The declarations are always validated against the receiver's supported bounds.
For **Speakers / mic · short** and **IC-7100 SSB**, the initial declaration uses
HF-worst spread and 65% selective AM; other profiles start with clean spread
and AM declarations. Each profile retains its own declaration draft. Invalid
receiver declarations block new Live listening/transmission, Simulation, and
explicit loopback starts. An existing monitor or already-admitted restart keeps
its last validated settings and live plots while the draft error remains visible.

Auto resolves a deterministic local waveform from Expected SNR and the declared
receiver assumptions. Manual timing/modulation/coding retains explicit waveform
settings. Switching between Live, Simulation, and Live loopback changes effect
injection and audio ownership, not the transmitted format. No measured channel
result feeds back into this selection; both peers need matching waveform
settings.

When Fast is selected in **Live loopback test** mode, ordinary reception stays
active while idle. Starting a bounded test closes that receive session before
opening the duplex test; after completion or cancellation, receive-only
monitoring resumes. Only one session owns the audio device at a time. Switching
tabs does not stop capture. Switching to another modem stops idle Fast
monitoring for the shared audio handoff, while an active bounded test can finish.

**Run loopback test** sends the selected nonempty Console text or file through
the selected profile, device, output routing and encryption. Encrypted tests
require the selected key. The explicitly started test injects the declared
oscillator, spread and selective-AM effects into the waveform before physical
playback and capture. Idle receive-only monitoring in Live loopback mode remains
clean, like ordinary Live. The complete source is prepared and its encoded
airtime checked before audio opens.
The test uses the selected TX volume and keeps the existing **75-second audio**
and **110-second elapsed-time** maxima, including source preparation. Capture
must start within **5 seconds**. A source/profile
combination exceeding the audio budget is rejected, never truncated or replaced
with a fixture. After the complete emitted source and its canonical quiet tail,
the receiver has up to **2 seconds of captured audio** to establish physical
completion. This is a failure deadline, not a synthetic completion signal.
Playback continues with silence while the receiver finishes, avoiding an audio
server drain wait. Progress separates audio samples queued for playback from
samples captured at the receiver; the playback count includes silence and does
not confirm speaker output. If the shared audio service is still busy,
handoff waits at most **2 seconds**; cancellation remains available and is
acknowledged after owned audio work closes. Capture starts before playback once
the handoff succeeds. Console text and attachment drafts remain intact after
sending.
Test status, counters and captured receiver plots are labeled **LOOPBACK TEST**.
A result enters Signals or Files only after scored physical absence, decoding,
integrity and exact comparison with the selected source all pass. These rows
retain **LOOPBACK TEST** provenance and use the ordinary restricted text
presentation, copy, paste and explicit attachment-save actions. A failed,
cancelled, timed-out or mismatched test does not publish a completed result.
Public mode checks integrity but does not authenticate a sender; encrypted mode
requires successful authentication. The Console drafts remain intact. The last
test outcome stays visible while idle monitoring resumes, alongside the status
of the current audio capture. Cancel stops the bounded test without manufacturing
a completed reception; loopback-mode monitoring then resumes if Fast remains
selected.

Transmit power, path loss, and receiver noise density (or Off) remain Simulation
link-budget controls. They are separate from the Console receiver declarations.
Shift frequency is an RF coordinate used for oscillator bounds and test
injection; it never changes the Fast audio carrier or passband. The declarations
and injection have different jobs:

- Transmit power, path loss and receiver noise apply only in software
  Simulation. Signal samples use normalized PCM units: transmit power and path
  loss determine the equivalent received level relative to separately added
  receiver noise. They do not turn down playback at the DAC. Simulation noise
  is added at the modeled receiver input, so common receive gain does not change
  the modeled link ratio.
- In Live loopback mode, transmit power, path loss and noise are disabled and
  retained. They do not change physical transmit audio or captured receiver
  input. An explicitly started loopback test copies the selected oscillator,
  spread and selective-AM effects into the test waveform before playback; the
  copied search margin is fixed at one and is not an injected impairment. Plots
  continue to show actual captured audio.
- Live loopback uses the selected TX volume, like ordinary transmission. It
  does not change the system mixer or apply an automatic gain adjustment.

Simulation preserves modeled delayed tails and runs a scored quiet interval for
completion.
Changing a channel profile or declaration revalidates the receiver envelope and
deterministic Auto profile. Unsupported receiver bounds disable new listening
and transmit actions while keeping declarations editable. Separately invalid
test-channel or link-budget settings disable Simulation and explicit loopback;
they do not insert effects into ordinary Live PCM. Diagnostics report the
required frequency range, available sampled band, and oscillator/channel
allowances.
It reuses native spread and selective-AM kernels without using the Robust
receiver. A loopback pass requires physical absence, integrity verification and
exact reception of the selected text bytes or file bytes and filename. An audio/time limit, capture gap or
early receiver end is incomplete. A cable test exercises that local audio path
with synthetic propagation effects; it does not establish RF propagation or
transceiver qualification. See the [current measured outcomes and limits](fast-radio-capacity.md#propagation-and-differential-carrier-qualification).

The opt-in native FLTK check `build/dev/test_fltk_adapter --fast-live-loopback`
requires the connected physical cable and an available display. It exercises
the actual button, received Signals, copy, encrypted text, attachment saving,
and live plots across tab changes. It is not part of unattended CTest. The
`gui_fast_loopback` regression exercises the shared Application with mocked
audio hardware and the production modem; it is not physical-cable evidence.

The bounded native probe runs the ordered editable manual cases, including
encryption, exact attachment comparison and scored physical completion:

```sh
./build.sh configure
cmake --build build/dev --target fast_channel_probe --parallel 4
python3 tools/fast_manual_cases.py --list
python3 tools/fast_manual_cases.py --output local-evidence/fast-channel/manual-simulation.json
# Physical cable only; select the actual connected playback/capture route:
python3 tools/fast_manual_cases.py --live --device default --tx-gain 1.0 \
  --output local-evidence/fast-channel/manual-live.json
```

The probe's deterministic encryption keys and source are test fixtures, never
operating keys. `--case-index N` selects rows after validating the entire list;
duplicates remain separate. Shortened or changed coding settings require
explicit `--profile-override` and are recorded separately from the defaults.
An audio/wall limit, missing acquisition or failed authentication is not a pass.
See the [current measured outcomes and limits](fast-radio-capacity.md#propagation-and-differential-carrier-qualification).

## Live signal plots

The Fast interface shows waveform, waterfall and constellation plots while
receiving, transmitting or testing. Each plot identifies TX or RX. The waveform
uses the latest 1,024 actual PCM samples with a fixed ±1 scale; its caption gives
the visible time span and RMS audio level in dBFS, with the sample rate on wider
plots. **CLIPPING** indicates recent samples reaching full scale. While waiting
for synchronization, the tracking readout also shows input RMS and peak levels.
The waterfall uses a 512-point Hann-window
FFT of recent audio, with frequency increasing left to right, newest rows at the
top, and amplitude in dBFS. These are audio measurements, not RF power or SNR.
During ordinary Live reception and Live loopback mode, the waveform and
waterfall follow captured PCM independently of receiver acquisition and decoding,
including before and after a bounded test. Constellation age follows the sample
time of its receiver observations; **Last** means new audio is arriving while
the displayed points are older. During ordinary Live reception, the readout is
the current input level in dBFS, not SNR. During and after an eligible loopback
test, it can show the last loopback signal-SNR estimate, with its reference-band
width and capture age. That estimate compares received payload energy near
decoded receiver progress with a valid microphone-noise baseline recorded
before playback; quiet post-test input does not produce a low or negative SNR.
These displays are measurements, not a guarantee of successful reception.
Loopback plots show the captured input, including the physical audio path, rather
than the generated playback waveform. Input I/Q and equalized constellation
points remain receiver observations and can lag while receiver work is pending.

Before the first received payload symbols, the constellation panel shows
**RX input I/Q**, labeled **Unsynchronized**. These are up to 512 actual
matched-filter observations on a free-running display clock, with the current
automatic amplitude scale shown in the caption. Noise and unrecognized signals
also produce points; this view does not indicate symbol lock or successful
reception. It helps distinguish a blank capture from audio that has not acquired.

Once payload symbols arrive, the panel switches to **RX equalized constellation**:
up to 512 actual equalized I/Q values before symbol slicing. TX points are the
transmitted mapper values. Training and pilots are excluded from these payload
plots. No ideal constellation or hard decisions are substituted for RX samples.

For two-instance speaker/microphone checks, select **Speakers / microphone** at
both ends, leave **Live audio** selected and verify **Live RX** before transmitting.
Use matching constellation, coding and encryption settings. An elevated waterfall with an **Unsynchronized**
input cloud means audio is being captured but Fast has not acquired it. Check
the selected input, audio levels and matching settings; the waterfall alone
cannot establish that the channel preserves the waveform well enough to decode.

Display snapshots publish at most ten times a second, and the waterfall retains
at most 96 rows. A new transfer resets the plots; stopping retains the last
capture, labelled **Retained**. **Stalled** marks an active session with no new
captured PCM for more than two seconds, even if decoder diagnostics keep changing. Switching modem interfaces preserves
an active transfer and its plots. Plotting uses independent bounded storage;
samples, display failures and GUI timing never feed modem decisions or physical
completion. Both native backends render the same immutable plot snapshots.

Fast uses an 800-logical-pixel default window height and a 720-logical-pixel
minimum. Console, Simulation and Modem details share the compact layout at
smaller heights; profile, audio and action controls remain accessible across tabs.

## Channel profiles

The raw API/CLI profiles below include the fixed SSB standard and the high-SNR
cable, FM and acoustic reference points. The GUI resolves its Expected SNR (36 dB cable, 3 dB acoustic,
−6 dB acoustic short) into the corresponding lower-order settings; the CLI does likewise when passed
`--expected-snr`. See [SNR presets](fast-snr-presets.md).

The high-SNR **Audio cable · QAM / LDPC** reference uses **4,194,304-QAM,
LDPC 8/9, four LDPC frames per coding cycle, approximately 0.3% outer RS,
and one full marker every 16 intervals**. It sends 17,647.0588 symbols/s with
2% root-raised-cosine rolloff, occupying an ideal 18 kHz band from 300 to
18,300 Hz around a 9,300 Hz carrier. The default sample rate is 48 kHz and
output amplitude is 0.30. The historical trial below used both headphone
outputs; current routing defaults to left mono.

On 20 September 2026, the earlier four-pilot version of this clean-cable
operating point completed an exact live **50,000,000-byte
transfer in 1,290.664 seconds (21m 30.664s)**, including device startup and an
eight-second real silence tail. The signal occupied 1,281.087 seconds; all
6,980 LDPC frames converged and the complete source SHA-256 matched. The output
sample peak was 0.8922 with no digital clipping, no capture overrun, and a
maximum capture backlog of 0.304 seconds, below that cable profile's one-second FIFO.
**One successful whole-file trial does not establish an 80% success rate.**
Earlier 1,048,576-QAM / LDPC 9/10 development trials also transferred 5 MB
exactly; one used 139.043 seconds of signal with all 696 LDPC frames converged.
Those earlier 5 MB trials used a shorter preamble. Current capacity single-carrier
estimates include 2,048 training symbols and sixteen-symbol pilot words; the
historical timings do not qualify this revised geometry. The
[capacity cable study](fast-capacity-live-study.md) records wire revisions,
measured trials, failures and limits.

| Profile | Format | Symbols/s | Carrier | Ideal shaped support | Default constellation | Gross mapper rate |
| --- | --- | ---: | ---: | --- | --- | ---: |
| `wire` | Capacity | 17,647.0588 | 9,300 Hz | 300–18,300 Hz | 4,194,304-QAM | 388.24 kbit/s |
| `wire --format classic` | Classic | 15,000 | 9,300 Hz | 300–18,300 Hz | 256-APSK | 120 kbit/s |
| `ssb` | Capacity | 2,181.8182 | 1,500 Hz | 300–2,700 Hz | QPSK | 4.36 kbit/s |
| `fm` | Capacity | 2,181.8182 | 1,500 Hz | 300–2,700 Hz | 64-QAM | 13.09 kbit/s |
| `acoustic` | Capacity OFDM | 1.302 blocks/s | Multicarrier | 501–18,000 Hz | 16-QAM | 52.10 kbit/s coded |
| `acoustic --format classic` | Classic | 500 | 1,800 Hz | 1,500–2,100 Hz | QPSK | 1 kbit/s |

The acoustic raw API/CLI reference uses a 32,768-point FFT, an 85.33 ms cyclic prefix,
pilot stride 16, LDPC 3/4, depth 8, nominal waveform amplitude 0.40, and
single-channel output (the historical measurement used the right channel). Its steady source rate is 38.80 kbit/s; the table's OFDM coded
rate already includes the guard, pilots, refresh blocks, and mapper rounding.
A real 5,000,000-byte transfer with the earlier 64-QAM/stereo preset completed
exactly in 746.47 seconds, but later tests reproduced 64-QAM decoding failures
on a stationary path. See the [routing diagnosis](fast-acoustic-routing-diagnosis.md)
for the default change and the [acoustic live study](fast-acoustic-live-study.md)
for earlier measurements and their limits. Select `--qam 64 --stereo` explicitly
to reproduce the earlier preset; matching QAM is required at both peers.
The GUI channel selector offers **Audio cable · QAM / LDPC**, **IC-7100 SSB ·
2.4 kHz**, **IC-7100 FM · voice band**, **Speakers / microphone**, and
**Speakers / mic · short**. The separate [short acoustic profile](fast-acoustic-short.md)
uses smaller fixed coding blocks. Its historical preset search compared 10.5-
and 12.5-second transfer targets; these remain family-selection heuristics,
not latency bounds. The current compact single-carrier preset applies a
selected-band fade cap and can take substantially longer. Single-carrier
choices retain the full 2,048-symbol
capacity training and sixteen pilots after each group of up to 32 payload
symbols. The short profile is the initial GUI selection at −6 dB expected SNR.
Its current single-carrier preset is QPSK at about 231 baud, with a 1,944-bit
rate-2/3 LDPC frame and depth one; it targets a 12 dB selected-band margin. The
FEC rate is part of the matching peer configuration. Use the actual-source
airtime estimate; the short `fox` estimate is about 32.67 seconds. Older
short-transfer timings used different training, pilot and fade-margin geometry.
See the [short-profile tradeoff and test limits](fast-acoustic-short.md).
All four classic presets remain available through the CLI with
`--format classic`.

The SSB capacity default uses **QPSK, one 16,200-bit LDPC frame per coding
cycle, depth 1, and 16 known pilots per 16 payload symbols**. The selected
DVB-S2 short-table code is labeled **1/2**, but its actual information size is
**7,200 bits**: the information fraction is 4/9 before outer coding and source
protection. The FM default uses 64-QAM and four rate-3/4 frames per cycle,
with sixteen pilots per 64 payload
symbols. Both use 10% RRC rolloff, amplitude 0.30, and a 64-symbol marker
every four intervals, after a 2,048-symbol preamble. SSB capacity uses the v3
profile identity for its sixteen-symbol pilot sequence. This is a matching
one-way transmission format; receiver-local channel fitting does not negotiate
or change the transmitted format. The [radio study](fast-radio-capacity.md)
records throughput, spectrum, sampled tests and the limits of qualification.

Every capacity single-carrier profile transmits the same length of public
training and sixteen-symbol pilot words, while baud, constellation, coding,
marker geometry and pilot spacing remain matching local settings. The native
receiver facade with validated codec feedback uses the **resilient-ssb** model
directly for the exact 48 kHz standard geometry above and the explicit 48 kHz,
4,096-QAM, 64,800-bit LDPC 8/9, depth-1 variant with the same carrier, baud,
rolloff and marker/pilot cadence.

Other representable capacity single-carrier profiles admit competing local
delay/Doppler-model and coherent startup fits when they use **48 kHz audio,
800–2,500 symbols/s, QPSK or 16-QAM, rolloff at least 0.10, and at most 64 data
symbols between pilot words**, with room for the translated band and acquisition
offset allowance. Startup reports **single-carrier-acquisition**; acquisition
evidence selects one continuing receiver. The model reports **resilient-ssb**
for SSB or **delay-doppler-sc** otherwise. The coherent path retains independent
public-marker or fresh-pilot evidence before continuing alone.

Wideband, extremely slow, high-order and other settings outside that bounded
model domain use the trained, pilot-supervised coherent receiver, reported as
**standard-ssb** or **standard-fast**. OFDM retains its separate receiver.
Receiver selection does not negotiate, alter the transmitted waveform, or
require a return channel. Shared training is not a claim that every profile
supports the manual HF propagation conditions; neither this dispatch policy
nor the explicit high-order variant qualifies all manual settings.

For the wide **wire** profile, bounded receiver-local recovery can run after
ordinary acquisition fails. A 64-symbol public short carrier-frequency-offset
(CFO) unwrap resolves the long-fit ±5.6 Hz ambiguity. A candidate fits 21
feedforward and 24 feedback taps from the public preamble, then continues only
when independent 128-marker sign prediction and measured feedforward
presence/quiet checks pass. Feedback energy cannot prove its own signal presence.
This is a local receiver choice, with no waveform or profile adaptation and no
transmitter negotiation.

An actual-button physical cable run at **Auto 25 dB** used the wire profile's
256-QAM, LDPC 7/9, depth 4, 17,647.0588 baud, and nominal 300–18,300 Hz band.
With HF Best (0.5 ms / 0.1 Hz), crystal baseband and IC-7100 shift at 30 MHz, it
received exact public `fox` at 100% selected TX volume in about 10.58 seconds
overall. The result used the receiver-only fallback above; it did not alter the
waveform or supply channel truth to the receiver. The streaming path passed chunk-boundary
and single-erasure regressions, and rejected twelve preamble-only
repetitions. These checks bound self-acquisition and guard behavior for this
case, not communication reliability.

The corresponding **Auto 36 dB**, 4096-QAM HF Best case still failed all four
bootstrap LDPC frames after lock. A bounded diagnostic localized this failure to
error propagation through feedback decisions; correct-symbol feedback recovered
the frame only as a diagnostic control, not as a usable receiver method. Keep
this higher-order case unqualified. A dense SSB inverse at wire's 0.02 rolloff
needs about ±320 symbols of support, exceeding the 384-row bound, and was
rejected. See [current Fast validation](validation.md#fast-live-channel-and-cable-receiver-follow-up--12-november-2026)
for the evidence and its limits.

**Expected SNR** uses original-band assumptions: cable 65 dB over 18 kHz,
acoustic 13 dB over 17.5 kHz, and radio 20 dB over 2.4 kHz. Each menu extends
40 dB downward, with notable 3/10 dB steps. Weak settings narrow bandwidth
and reduce rate; the lowest acoustic settings switch from OFDM to a narrow
single carrier. The menu neither measures nor negotiates SNR. Both ends need
the same settings. SSB's nominal 20 dB choice retains the short QPSK standard;
other SSB choices use long LDPC frames. Every capacity single-carrier choice
retains public training and recurring pilot words. Estimated rates are not
measured throughput or receiver qualification, and presets do not adapt the
transmission to receiver feedback.
See [SNR presets](fast-snr-presets.md) for the noise model,
synchronization limits and qualification status.

Manual rate/QAM/LDPC/depth changes mark the SNR selector **Manual**.
The rate menu shows actual symbols/s; OFDM values are **per tone** and include
block duration. Rate **Auto** with a manual constellation restores timing while
retaining that constellation and coding. Selecting an expected-SNR value
restores its complete modeled preset. Each channel retains its selections
when switching away and back; controls are frozen during active transfers.

Capacity mode offers square Gray-labelled QAM orders from 4 through 4,194,304
in powers of four, with LDPC rates 1/2, 2/3, 3/4, 7/9, 8/9 and 9/10. This includes
4096-QAM and 16384-QAM for links needing more margin. The GUI offers depths
1, 4, 8 and 16; the CLI accepts every depth from 1 through 16. A depth is the
number of LDPC frames in one locally fixed cycle. Marker spacing and pilot
spacing are additional matching CLI settings. There is no over-air negotiation,
automatic transmitted-format fallback, or adaptive bit loading.

The CLI's `--profile wire --format classic` preserves the earlier cable preset: 256-APSK,
convolutional 7/8, RS(128,120), depth 62, amplitude 0.35, and 20% rolloff.
Explicit classic SSB/FM retain convolutional 3/4, robust RS, depth 16 and amplitude 0.5;
classic acoustic retains QPSK, convolutional 3/4, robust RS, depth 5 and amplitude
0.35. All classic profiles offer QPSK and 16/64/256-APSK. Their pulse span
remains 16 symbols. The [earlier cable study](fast-cable-live-study.md) and
44.14 kbit/s sampled 2 MiB benchmark describe this classic implementation,
not the capacity default.

Dense QAM requires low residual distortion, accurate timing and a linear audio
path. A high sine-wave SNR does not establish those properties by itself.
The [physical SNR study](cable-snr-live-study.md) measured about 79 dB on one
input and 81.5 dB with both inputs averaged, under its stated tone conditions.
Those figures are separate from modem decoding margin. Receiver EVM is
decision-directed tracking information; it must not be relabelled as an
independent SNR measurement.

Avoid software and ADC clipping. The tested cable used zero microphone boost;
actual gain settings depend on the connected hardware. Output defaults to left
mono for every profile. `--mono` selects left-only output, `--right-mono` selects
right-only output, and `--stereo` selects both. Routing and playback
level are local choices and need not match between peers.

The CLI accepts 44.1–192 kHz sample rates. Audio negotiation and the bounded
resampler bridge device and modem sample rates; device sample rate is excluded
from the integrity context. The nominal 18.3 kHz upper edge fits the converter's
18.522 kHz conservative passband for 44.1/48 kHz conversion, but the very dense
cable default is not qualified across arbitrary independent clocks or sound
cards. Device passband validation does not replace measurement.

The radio presets reserve 300 Hz at each edge of a nominal 3 kHz audio path.
Their 2.4 kHz shaped width leaves room below the 2.8 kHz HF data bandwidth
guideline requested for this design. The FCC order applies to specified HF
bands; it is not a blanket VHF bandwidth rule.
[FCC 23-93](https://docs.fcc.gov/public/attachments/FCC-23-93A1.pdf).
The IC-7100's filters, transmit bandwidth, FM data path and selected input affect
the actual response. These presets do not configure the radio or certify its
emitted spectrum. Consult the [Icom full manual](https://www.icomjapan.com/support/manual/2288/).
Part 97 generally prohibits encryption intended to obscure meaning, subject to
its exceptions; enabling encryption is not permission for an amateur-band
transmission. [47 CFR 97.113](https://www.govinfo.gov/content/pkg/CFR-2025-title47-vol5/pdf/CFR-2025-title47-vol5-sec97-113.pdf).

## Airtime, payload rate and theoretical capacity

The GUI and `fast-info --estimate-bytes N` calculate airtime from fixed local
geometry, including bootstrap, complete source cycles, QAM label fill, markers,
pilots, the pulse tail and physical-end silence. The usual 6.25-second silence
is extended when slow symbols or OFDM blocks require longer complete
observations. Capacity source data is
packed as eight-bit bytes; there is no ninth validity bit per byte. Active TX
percentage counts generated samples and stays below 100% until playback drains.
Device queues can make audible playback lag the producer. A file changed after
inspection can invalidate its estimate.

Use `fast-info --profile wire --estimate-bytes N` for the selected build's
cable estimate. The sixteen-symbol pilot words change recurring airtime from
the earlier four-pilot format; earlier cable estimates are not current-format
throughput measurements. At the preserved classic cable preset, the
public 50 MB estimate remains 7,275.599 seconds, about 2h 01m; encryption raises
it to 7,882.072 seconds. Classic throughput is retained as a reference, rather
than a physical capacity limit.

Shannon-Hartley capacity is `C = B log2(1 + S/N)`. For an ideal 18 kHz Gaussian
noise channel this gives approximately 239.18 kbit/s at 40 dB and 358.77 kbit/s
at 60 dB. These examples assume the stated **in-band signal/noise power ratio**.
They are not guarantees derived from a full-scale single-tone hardware test.
`fast-info` reports these 40/60 dB examples alongside the older assumed-30 dB
example. With capacity profiles it also reports the selected expected SNR,
its original reference bandwidth, assumed selected-band SNR, and whether manual
options changed the preset. Use, for example,
`pump fast-info --profile acoustic --expected-snr -10 --estimate-bytes 100000`.
Uniform QAM, finite coding, training, tracking error, nonlinearities,
and burst disturbances all affect achievable throughput.

The [coding study](fast-coding-study.md) records the earlier design analysis.
LDPC, sparse outer RS, compact source bytes and sparse full markers are now
implemented. The [codec specification](fast-capacity-codec.md) gives exact
ratios and overhead, and the [live capacity study](fast-capacity-live-study.md)
separates measured results from projections.

Speaker/microphone capacity mode uses a separate
[OFDM waveform](fast-acoustic-ofdm.md) with a cyclic prefix, frequency-specific
equalization and noise estimates, independent training, and channel refreshes.
The [acoustic study](fast-acoustic-live-study.md) records real speaker/microphone
measurements, failed and successful candidates, and the gap between conditional
channel capacity and delivered file throughput. Cable settings do not describe
the acoustic channel.

## Fixed intervals, without received lengths

Every physical interval still contains exactly **2,048 inner-coded bits**.
There are no modem packet lengths, payload counts, addresses or negotiated
profiles. Allocation and boundaries follow matching local settings. Attachment
filenames belong to the post-completion source interpretation, never modem
framing. Source completion flags never choose a modem boundary.

All capacity single-carrier profiles start with **2,048 known QPSK training symbols**
and use **sixteen-symbol known QPSK pilot words**. The cable reference's
full 64-symbol public QPSK marker precedes interval zero and every 16th interval
thereafter by default. Its length is preserved; the frequency of insertion is
reduced. Sixteen known QPSK pilots follow every group of up to 256 data symbols.
At the default 22-bit QAM order, each interval has 94 data symbols and one
sixteen-symbol pilot group; the last mapper symbol has 20 fixed fill bits outside
the 2,048-bit codec span. An interval therefore uses 110 symbols without a
marker or 174 with one. The 2% RRC pulse has a 640-symbol finite span to reduce
filter truncation error in dense QAM.

The SSB standard keeps that 2,048-symbol preamble and the 64-symbol marker,
inserting the marker every four intervals and sixteen known pilots after each
group of up to sixteen data symbols. Compact single-carrier acoustic formats
use the same capacity training and sixteen pilots after groups of up to 32
payload symbols; their 192-symbol marker retains 64 fit symbols and 128 held-out
verification symbols. Short OFDM retains its separate training geometry; see
[short acoustic transfers](fast-acoustic-short.md).

Square QAM uses independent Gray-labelled amplitude axes and unit mean symbol
energy. Demapping computes bit likelihoods from nearest alternative axis levels,
without scanning millions of constellation points. Coherent single-carrier
reception uses longer fractional interpolation, a 21-tap fractionally spaced equalizer,
training-based timing/phase/frequency fits and pilot/decision tracking. The
bounded delay/Doppler backend uses the native learned-channel forward model
and interval inversion within the admission limits described above. These SSB
LDPC matrices are from DVB-S2;
this application is not a DVB wire protocol.

## Capacity coding and source bytes

The cable reference cycle combines four 64,800-bit LDPC 8/9 frames. Their
information areas total 28,800 bytes. A shortened GF(65536) RS word uses **88 parity bytes
and 28,712 systematic bytes**, so parity/data is **0.30649%**. It can correct up
to 22 unknown 16-bit symbol errors. This outer code repairs sparse LDPC residuals;
it cannot recover a wholly lost LDPC frame.

One SHA-256 digest or HMAC-SHA256 protects the entire source cycle. Public mode
has a 28,680-byte plaintext area including its source flag; encrypted mode has
28,656 plaintext bytes, plus IV and HMAC outside that area. Each cycle carries:

- A one-byte continuation (`0`) or final (`1`) flag.
- Ordinary eight-bit source bytes.
- In the final cycle only, a mandatory `0x80` delimiter followed by zero fill.

Only the last cycle may be final. The delimiter preserves arbitrary binary
content, including trailing zeros and source bytes equal to `0x80`. A source
exactly filling a continuation cycle requires a separate empty final cycle.
Loss of a whole final cycle therefore cannot validate a shorter prefix. Flags
and padding are interpreted only after physical completion.

The LDPC code-bit permutation and balanced rotation between frames distribute
both bursts and unequal QAM bit reliability. Every depth-sized column contains
one bit from each frame, and all frames receive nearly equal exposure to every
QAM bit plane. A public deterministic whitening mask covers the entire coded
cycle, bootstrap and interval fill, avoiding dense-QAM bias from zero data.
Whitening adds no bits and provides no secrecy. Four coded frames occupy 127
physical intervals, including 896 fixed padding bits before whitening.

LDPC decoding has a bounded iteration limit, followed by RS correction and
mandatory integrity verification. Diagnostics report frame count, nonconverged
frames, iterations and changed systematic hard decisions. Changed decisions are
not confirmed corrections until integrity succeeds. Exact geometry, algorithms,
independent vectors and memory limits are in the
[capacity codec specification](fast-capacity-codec.md).

## Classic format and reference regressions

Classic transmission retains its 128-symbol preamble, a full marker before
every interval, and four QPSK pilots after up to 32 data symbols. Its interval
lengths, including markers/pilots, are 1,216 symbols for QPSK, 640 for 16-APSK,
450 for 64-APSK and 352 for 256-APSK. The last 64-APSK symbol has four fixed fill
bits. Gray-labelled ring geometry and independent wire vectors are unchanged.

| Classic RS choice | Codewords per group | Systematic bytes | Encrypted source area | Public source area |
| --- | --- | ---: | ---: | ---: |
| Robust | 2 × RS(128,112) | 224 | 176 | 192 |
| High rate | 2 × RS(128,120) | 240 | 192 | 208 |

Classic high-rate RS costs 6.67% parity/data, or 6.25% of coded RS bytes; robust
RS doubles the correction budget. A depth-D cycle interleaves 2D RS rows, then
uses K=7 convolutional coding with generators 0171 and 0133. Rates 1/2, 3/4 and
7/8 use puncture pairs `[11]`, `[11,10,01]` and
`[11,10,10,10,01,01,01]`. Each cycle starts in state/phase zero and includes six
zero trellis tail bits. The receiver uses soft Viterbi and RS erasure correction.

Classic source cells remain nine bits: `1` followed by the eight source bits.
The endpoint is a nine-bit zero cell followed by zero fill. This is 12.5%
source expansion; it does not apply to capacity mode. The classic cable preset
has 71 physical intervals per cycle and depth 62; radio depth 16 at rate 3/4
uses 22 intervals, and acoustic depth 5 uses seven.

Historical sampled interruption tests recover selected 1/10/50 ms erasures
and a 10 ms additive effect, while a 500 ms destructive interruption fails
closed. These tests pin classic settings; they are not dense-QAM guarantees.
Classic acoustic reception keeps its separate 21-tap trained equalizer,
coherence threshold and conservative soft-evidence handling. Radio/classic
cable retain their five-tap equalizer. Existing classic peers can use
`--format classic`; both sides must match their complete local settings.

## Integrity, physical completion and bounded storage

Both formats use fresh 32-byte transfer salts and a fixed bootstrap cycle,
protected by the selected FEC. A bootstrap has no length or profile fields.
Public mode uses SHA-256; encrypted mode encrypts with Serpent-256-CBC followed
by AES-256-CBC, without cipher padding, and uses encrypt-then-HMAC-SHA256. Each
group carries a fresh unpredictable 16-byte outer IV; the inner IV is derived
separately from it. Tags bind the
local profile, salt, locally counted ordinal, IV and ciphertext and are verified
before decryption. Missing, reordered or mixed-transfer groups cannot silently
join source prefixes. Public checksums detect corruption but can be rewritten
by an adversary; they do not authenticate.

For public distribution of signed programs, carry the original signed artifact
and verify its publisher signature separately after saving it. Fast's public
checksum is not a publisher signature. Received attachments remain inert data;
the application does not execute them.

Encrypted capacity uses `DataPump/fast/capacity/v2/serpent-aes-v4/...`
domains. SSB capacity binds the `datapump/fast/v3` profile context; other
capacity single-carrier formats bind `datapump/fast/v4`, including the explicit
sixteen-symbol pilot count. SSB v3 already binds that count and, for its short
default, the 16,200-bit frame geometry. OFDM retains `datapump/fast/v2` with its
`/ofdm/v3` subdomain and separate training geometry. These profile contexts do
not rename the capacity cipher domains. Both peers need the updated v4
single-carrier format; SSB v3, OFDM and classic identities are unchanged.
Encrypted classic uses `DataPump/fast/v1/serpent-aes-v4/...` and its classic
`datapump/fast/v1` profile context. Public
checksum domains remain separate. The cipher layers derive their keys from
separate Serpent and AES roots. Bootstrap authentication XORs the full
HMAC-SHA256 outputs under independently derived lane keys. The same two-lane
construction roots the salted group authentication key; each group then carries
a full HMAC-SHA256 under that key. Capacity context
also binds marker and pilot spacing. Device sample rate and local output level
are excluded because they do not choose peer wire geometry. Public/encrypted
mode remains a matching local choice; a failed key or tag never triggers a
public fallback. The encrypted cascade requires upgraded peers on both ends;
earlier encrypted Fast suites are not automatically selected.

Integrity-checked source areas accumulate in bounded RAM without interpretation.
**Six seconds of fully scored physical absence** is the only end event.
EOF, cancellation, quotas, a valid digest, or a final flag cannot substitute for
it. WAV transmission includes 6.25 seconds of real silence after the pulse tail.
Only then can flags/padding be interpreted and an explicit Save handle appear.
Incomplete cycles, missing finals, invalid fill, integrity failure and exhausted
storage leave no completed source.

An uncorrectable source cycle leaves a hole at its fixed position; the decoder
continues FEC and integrity checks on later cycles. Locally counted cycle and
group ordinals advance through the hole, preserving whitening and independent
group verification. Classic cycles also retain intact groups alongside a bad
group. The GUI reports missing data while its verified-group count can continue
to increase. This does not reconstruct the missing bytes or enable saving an
incomplete file. Failed bootstrap context, invalid numeric input, quota and
internal errors still stop source decoding. See the
[continuation regression record](fast-cycle-continuation.md).

Failed marker groups retain their timed positions as erasures. Trailing absent
intervals are held as a bounded count and delivered only if a later marker
resumes the stream, avoiding fictitious source data during end silence.
A capture overrun with unknown missing time fails the transfer. Capture runs
separately from DSP/FEC with a bounded queue: eight seconds for profiles
admitted to the delay/Doppler model, including competing startup acquisition,
four seconds for acoustic OFDM, and one second for other single-carrier
profiles. Full-size wire LDPC adds half a second per locally selected frame:
three seconds at depth four, bounded at nine seconds for depth sixteen. This
absorbs the observed approximately two-second four-frame decode burst on a
damaged cable signal. The GUI and cable probe share this
queue policy. Decoding near its iteration limit requires measured scheduling
headroom; a larger queue does not establish sustained real-time capacity.
There is no automatic retransmission, arbitrary mid-file join or recovery
across unknown time gaps.

Receive source-area storage is capped at 256 MiB and compacts in place after
physical completion. Missing areas consume their full fixed logical space in
that quota, preventing corrupt streams from creating unlimited positions or
integrity attempts. A separately bounded validity map uses one byte per area.
Capacity requires the encoded XZ bytes plus one flag per cycle
and final-cycle fill; classic additionally needs its 1.125 encoded-source expansion.
PCM, FEC, diagnostics and shared code tables have separately bounded storage;
the source quota is not a process RSS limit. Source reads and waveform generation
use bounded chunks. Production TX first prepares its XZ source in bounded RAM
so compression cannot stall playback; post-end RX decompression uses a separate
source-output quota and 64 MiB codec memory cap. See [Fast XZ](fast-xz.md).
Text retains its separate 32,768-byte local limit.

Reception creates no temporary receive files, disk caches or autosaves. Only
explicit Save, requested CLI destinations and keyfile creation write output.
Operating-system swap, hibernation, crash dumps and graphics caches are outside
that application-level guarantee. There is no persistent replay database: a
previous complete authenticated transfer can be replayed. The local WAV reader
bounds RIFF chunks without using their lengths as modem framing; WAV output is
S16 PCM with an explicit classic-RIFF 4 GiB limit.

## CLI examples

```sh
./build/pump keygen --output keys.bin --key-names Fast
./build/pump fast-info --profile wire --estimate-bytes 50000000

# The wire profile selects the dense QAM/LDPC capacity default.
# Public text: no keyfile is needed on either peer.
./build/pump fast-tx --profile wire --text 'Hello from Fast mode' --output text.wav
./build/pump fast-rx --profile wire --input text.wav --json

# Files and text can both use optional encryption.
./build/pump fast-tx --profile wire \
  --keyfile keys.bin --key-name Fast --input source.bin --output transfer.wav
./build/pump fast-rx --profile wire \
  --keyfile keys.bin --key-name Fast --input transfer.wav --save received.bin --json

./build/pump fast-listen --profile wire --keyfile keys.bin --device default \
  --save received.bin
./build/pump fast-tx --profile wire --keyfile keys.bin --device default \
  --input source.bin

# Select a less dense capacity constellation, or the preserved classic format.
./build/pump fast-info --profile wire --qam 4096 --code-rate 7/9
./build/pump fast-info --profile wire --qam 16384 --code-rate 8/9
./build/pump fast-info --profile wire --format classic --apsk 256 --code-rate 7/8
```

`fast-tx` takes exactly one of `--text TEXT` and `--input FILE`. An empty text
argument is accepted. Without `--keyfile`, Fast selects public mode; providing
`--keyfile` enables encryption and preserves existing encrypted commands.
`--encrypt` requires a keyfile, while `--no-encryption` explicitly selects public
mode and skips loading any supplied key options. These flags conflict.
`--key-name` or `--pad` without a keyfile is an error unless `--no-encryption`
explicitly selects public mode. Invalid keys never cause a public fallback.

`fast-listen` handles one reception. Ctrl+C or `--seconds N` cancels without
manufacturing completion. `--format capacity|classic`, `--qam`, `--apsk`,
`--code-rate`, `--rs`, `--interleave`, `--sample-rate`, `--quota-mb`, `--mono`
and `--stereo` select local settings. Capacity requires `--rs 0.3%`, accepts
`--marker-spacing 1..16` and `--pilot-spacing 16..1024`, and supports LDPC rates
3/4, 7/9, 8/9 and 9/10. `--apsk` selects classic unless contradicted by an explicit
format. Current-format named keyfiles and optional pad-backed keyfiles remain available;
older keyfile versions are rejected.
`fast-info` reports local geometry and selected protection without loading a key.
Completed receive output contains metadata only; JSON distinguishes
`encrypted`, `authenticated`, and `checksum_groups`. The former `text_preview`
and `preview_truncated` fields and preview API have been removed. Live transmit
progress goes to stderr; JSON includes `estimated_seconds` and `transmit_fraction`.
Receive exit code 2
means incomplete; unsuccessful saves report an error. No radio PTT/CAT control
is implemented.

## SNR-only regression tool

`fast_regression` is built only with `BUILD_TESTING` and explicitly pins the
classic APSK/convolutional format. It is separate from the
regular channel simulator, link planner and probability estimates, and is not
installed as a user simulation interface. With no `--snr`, it runs 10 through
120 dB inclusive in 5 dB increments. Fixtures use deterministic test-only
salt/IV generation for reproducible waveforms; production uses OpenSSL random
generation and exposes no setting for this fixture generator.

```sh
./build/fast_regression --profile wire --apsk 16 --code-rate 3/4 \
  --rs robust --depth 16 --amplitude 0.5 --bytes 1024 --seed 417 > wire.csv
./build/fast_regression --profile ssb --bytes 1024 --seed 417 > ssb.csv
./build/fast_regression --profile fm --bytes 1024 --seed 417 > fm.csv
./build/fast_regression --profile acoustic --bytes 1024 --seed 417 > acoustic.csv
./build/fast_regression --profile wire --apsk 256 --code-rate 7/8 \
  --rs robust --depth 16 --amplitude 0.5 --bytes 2097152 --snr 60 --seed 417 --require-success > large.csv
```

SNR refers to signal power versus noise in the declared shaped bandwidth
`B = symbol_rate × (1 + rolloff)`. The tool measures the generated waveform's
mean power before adding noise, then sets real full-band AWGN variance to
`Ps × 10^(-SNR/10) × Fs/(2B)`. CSV includes measured matched-filter noise
bandwidth, Es/N0, exact-file comparison, physical completion, EVM, correction
counts, source goodput including end silence, scratch/spool bytes and CPU/wall
time. Callback timing is reported separately from mean real-time ratio;
occasional FEC work exceeds one PCM chunk duration and requires the live queue.

The recorded four-profile sweep contains 92 seeded sampled tests. The previous
classic wire and classic SSB presets use 16-APSK, fail closed at 10 dB and recover
exact files at 15–120 dB;
FM and acoustic default QPSK recover at every tested point. A single seed and
file per point do not estimate a reliable BER/PER curve. Separate DSP tests
cover fractional start phase, 44.1 kHz, sample-clock offset, additive impulses,
hum, phase slips, missing markers and non-signal input.

When Python is available, CTest's `fast_snr` runs this entire matrix and checks
exact completion at the established passing points, all 23 levels, argument
bounds and option-order independence. The original wire settings remain explicit
in this matrix; its separate 30 dB bulk test also pins the classic cable preset.
Capacity coding/QAM tests and live trials are separate from this matrix.
Improvements at lower SNR are allowed;
any completed file with incorrect bytes is always a failure.

Measured results and CSV artifacts are recorded in [validation](validation.md).
No physical DAC/ADC, loudspeaker, IC-7100, FM/SSB RF, emissions-mask or Windows
audio qualification follows from these software tests. High-SNR sweeps up to
120 dB are numerical regressions, not claims about 16-bit converter dynamic range.

## Code and compatibility boundary

Fast codec, DSP, session, WAV and CLI code live in `include/datapump/fast` and
`src/fast`; the shared Fast GUI controller and screen live in `src/gui/fast`.
`datapump_fast` reuses only generic crypto/keyfile, Reed–Solomon and audio
services. A build and CTest dependency guard rejects imports of regular modem,
pattern, tuning, simulation or recovery code and regular dependencies on Fast.

The regular runtime change is explicit idle audio suspension/resumption for
exclusive hardware ownership. Regular pending symbols are never completed or
cancelled to start Fast. The shared GUI owns the mode switch and independently
polls both controllers, routes native service replies to their original owner
and rejects stale hidden-mode callbacks. Both native adapters use the shared
screen contract. The [regular development contract](development.md) remains
mandatory and is tested alongside the new Fast suites.
