# Fast text and file transfer

Fast Modem is a separate streaming QAM/LDPC and classic APSK modem. Select **Fast Modem** from the
dropdown beside **DATA PUMP** to switch the entire desktop interface. The other
choice, **Robust Modem**, is the existing regular interface and remains the
default. Select a channel profile,
constellation and coding. Choose **Text** and enter a message, or choose **File**
and select a source file, then transmit. Select **Listen** on the receiving
computer. The **Encryption** checkbox is optional and starts off; enabling it
requires loading a key. Both peers need matching local settings, including
encryption on/off and, when enabled, the same key material (entry names are only
local labels). Save becomes available only after
physical completion and integrity checks. Encrypted transfers authenticate;
unencrypted transfers only check public checksums. Existing destinations are
never overwritten.

Text is limited to 32,768 source bytes, including UTF-8 bytes. The GUI accepts
valid UTF-8 without NUL; files preserve arbitrary binary data. Text and files use
exactly the same source format: no message type, text encoding tag or source
length is sent. UTF-8, newlines and all file bytes keep their exact values.
Received content is never rendered as text or binary in the GUI or CLI. Save
preserves all original bytes; reception status shows counts and integrity only.
Switching Text/File retains both drafts. Loading a key does not enable
encryption; switching encryption off retains the key for later use.

Regular mode retains its existing short dictionary, exact raw bits, 192-bit
markers and 128-coded-byte intervals, private noise waveforms, time-indexed
encryption, iterative search and incremental pending reception. Fast settings,
key selection, text/file drafts and history are independent of regular settings and
drafts. Switching views does not cancel a regular reception. Starting Fast
hardware waits for the regular audio device to close and refuses to interrupt
an admitted pending reception or transmission. A regular simulation can continue
while its interface is hidden.

An active Fast transfer also continues when switching back. Regular controls
remain disabled until Fast releases the audio device; idle regular capture
resumes when Fast is inactive and the regular view is selected. Starting Fast
allows up to two seconds for asynchronous device release, then reports that
the operator must retry when regular work is idle.

Fast offers optional encryption and authentication, but no LPI claim, spreading, pattern
codewords, time/key search, single-bit recovery or source compression. Its known
training, markers, pilots, occupied spectrum and transmission duration are public.

## Live signal plots

The Fast interface shows waveform, waterfall and constellation plots while
**Listen** or **Transmit** is active. Each plot identifies TX or RX. The waveform
uses the latest 1,024 actual PCM samples with a fixed ±1 scale; its caption gives
the visible time span and RMS audio level in dBFS, with the sample rate on wider
plots. **CLIPPING** indicates recent samples reaching full scale. While waiting
for synchronization, the tracking readout also shows input RMS and peak levels.
The waterfall uses a 512-point Hann-window
FFT of recent audio, with frequency increasing left to right, newest rows at the
top, and amplitude in dBFS. These are audio measurements, not RF power or SNR.

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
both ends, start **Listen** before transmitting, and use matching constellation,
coding and encryption settings. An elevated waterfall with an **Unsynchronized**
input cloud means audio is being captured but Fast has not acquired it. Check
the selected input, audio levels and matching settings; the waterfall alone
cannot establish that the channel preserves the waveform well enough to decode.

Display snapshots publish at most ten times a second, and the waterfall retains
at most 96 rows. A new transfer resets the plots; stopping retains the last
capture, labelled **Retained**. **Stalled** marks an active session with no new
display frame for more than two seconds. Switching modem interfaces preserves
an active transfer and its plots. Plotting uses independent bounded storage;
samples, display failures and GUI timing never feed modem decisions or physical
completion. Both native backends render the same immutable plot snapshots.

## Channel profiles

The default **Audio cable · QAM / LDPC** profile uses **4,194,304-QAM,
LDPC 8/9, four LDPC frames per coding cycle, approximately 0.3% outer RS,
and one full marker every 16 intervals**. It sends 17,647.0588 symbols/s with
2% root-raised-cosine rolloff, occupying an ideal 18 kHz band from 300 to
18,300 Hz around a 9,300 Hz carrier. The default sample rate is 48 kHz and
output amplitude is 0.30. Both headphone outputs carry the same signal.

This clean-cable operating point completed an exact live **50,000,000-byte
transfer in 1,290.664 seconds (21m 30.664s)**, including device startup and an
eight-second real silence tail. The signal occupied 1,281.087 seconds; all
6,980 LDPC frames converged and the complete source SHA-256 matched. The output
sample peak was 0.8922 with no digital clipping, no capture overrun, and a
maximum capture backlog of 0.304 seconds, below production's one-second FIFO.
**One successful whole-file trial does not establish an 80% success rate.**
Earlier 1,048,576-QAM / LDPC 9/10 development trials also transferred 5 MB
exactly; one used 139.043 seconds of signal with all 696 LDPC frames converged.
Those earlier trials used a shorter preamble. Current estimates include the
2,048-symbol preamble described below. The
[capacity cable study](fast-capacity-live-study.md) records wire revisions,
measured trials, failures and limits.

| Profile | Format | Symbols/s | Carrier | Ideal shaped support | Default constellation | Gross mapper rate |
| --- | --- | ---: | ---: | --- | --- | ---: |
| `wire` | Capacity | 17,647.0588 | 9,300 Hz | 300–18,300 Hz | 4,194,304-QAM | 388.24 kbit/s |
| `wire --format classic` | Classic | 15,000 | 9,300 Hz | 300–18,300 Hz | 256-APSK | 120 kbit/s |
| `ssb` | Classic | 2,000 | 1,500 Hz | 300–2,700 Hz | 16-APSK | 8 kbit/s |
| `fm` | Classic | 2,000 | 1,500 Hz | 300–2,700 Hz | QPSK | 4 kbit/s |
| `acoustic` | Classic | 500 | 1,800 Hz | 1,500–2,100 Hz | QPSK | 1 kbit/s |

Capacity mode offers square Gray-labelled QAM orders from 4 through 4,194,304
in powers of four, with LDPC rates 3/4, 7/9, 8/9 and 9/10. This includes
4096-QAM and 16384-QAM for links needing more margin. The GUI offers depths
1, 4, 8 and 16; the CLI accepts every depth from 1 through 16. A depth is the
number of LDPC frames in one locally fixed cycle. Marker spacing and pilot
spacing are additional matching CLI settings. There is no over-air negotiation,
automatic fallback, or adaptive bit loading.

**Audio cable · classic APSK** preserves the earlier cable preset: 256-APSK,
convolutional 7/8, RS(128,120), depth 62, amplitude 0.35, and 20% rolloff.
SSB/FM retain convolutional 3/4, robust RS, depth 16 and amplitude 0.5;
acoustic retains QPSK, convolutional 3/4, robust RS, depth 5 and amplitude
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
actual gain settings depend on the connected hardware. Cable output drives
both channels by default; the other presets use right-only output. `--mono`
selects right-only output and `--stereo` selects both. Routing and playback
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
pilots, the pulse tail and 6.25 seconds of end silence. Capacity source data is
packed as eight-bit bytes; there is no ninth validity bit per byte. Active TX
percentage counts generated samples and stays below 100% until playback drains.
Device queues can make audible playback lag the producer. A file changed after
inspection can invalidate its estimate.

Calculated public-source airtimes for the current cable default, including its
2,048-symbol preamble and 6.25-second silence, are:

| Source size | Airtime | File-byte throughput |
| --- | ---: | ---: |
| 60 bytes | 7.871 s | Startup/end dominated |
| 100,000 bytes | 10.074 s | 79.41 kbit/s |
| 5,000,000 bytes | 135.597 s | 294.99 kbit/s |
| 50,000,000 bytes | 1,287.337 s, or 21m 27.337s | 310.72 kbit/s |

The encrypted 50 MB estimate is 1,288.071 seconds. These are duration estimates,
not reliability measurements. At the preserved classic cable preset, the
public 50 MB estimate remains 7,275.599 seconds, about 2h 01m; encryption raises
it to 7,882.072 seconds. Classic throughput is retained as a reference, rather
than a physical capacity limit.

Shannon-Hartley capacity is `C = B log2(1 + S/N)`. For an ideal 18 kHz Gaussian
noise channel this gives approximately 239.18 kbit/s at 40 dB and 358.77 kbit/s
at 60 dB. These examples assume the stated **in-band signal/noise power ratio**.
They are not guarantees derived from a full-scale single-tone hardware test.
`fast-info` reports these 40/60 dB examples alongside the older assumed-30 dB
example. Uniform QAM, finite coding, training, tracking error, nonlinearities,
and burst disturbances all affect achievable throughput.

The [coding study](fast-coding-study.md) records the earlier design analysis.
LDPC, sparse outer RS, compact source bytes and sparse full markers are now
implemented; probabilistic constellation shaping and adaptive loading remain
future work. The [codec specification](fast-capacity-codec.md) gives exact
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
There are no received packet lengths, payload counts, filenames, message types,
addresses or negotiated profiles. Allocation and boundaries follow matching
local settings. Source completion flags never choose a modem boundary.

Capacity transmission starts with **2,048 known QPSK training symbols**. A full
64-symbol public QPSK marker precedes interval zero and every 16th interval
thereafter by default. Its length is preserved; the frequency of insertion is
reduced. Four known QPSK pilots follow every group of up to 256 data symbols.
At the default 20-bit QAM order, each interval has 103 data symbols and one
four-symbol pilot group; the last mapper symbol has 12 fixed fill bits outside
the 2,048-bit codec span. An interval therefore uses 107 symbols without a
marker or 171 with one. The 2% RRC pulse has a 640-symbol finite span to reduce
filter truncation error in dense QAM.

Square QAM uses independent Gray-labelled amplitude axes and unit mean symbol
energy. Demapping computes bit likelihoods from nearest alternative axis levels,
without scanning millions of constellation points. Capacity reception uses
longer fractional interpolation, a 21-tap fractionally spaced equalizer,
training-based timing/phase/frequency fits and pilot/decision tracking. The
LDPC matrices are from DVB-S2/S2X; this application is not a DVB wire protocol.

## Capacity coding and source bytes

The default cycle combines four 64,800-bit LDPC 8/9 frames. Their information
areas total 28,800 bytes. A shortened GF(65536) RS word uses **88 parity bytes
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
Public mode uses SHA-256; encrypted mode uses AES-256-CBC without cipher padding,
fresh unpredictable 16-byte IVs, and encrypt-then-HMAC-SHA256. Tags bind the
local profile, salt, locally counted ordinal, IV and ciphertext and are verified
before decryption. Missing, reordered or mixed-transfer groups cannot silently
join source prefixes. Public checksums detect corruption but can be rewritten
by an adversary; they do not authenticate.

Capacity uses `DataPump/fast/capacity/v2/...` domains and a `datapump/fast/v2`
profile context; classic retains `DataPump/fast/v1/...` and its frozen context.
HKDF-derived encryption and authentication keys are separate. Capacity context
also binds marker and pilot spacing. Device sample rate and local output level
are excluded because they do not choose peer wire geometry. Public/encrypted
mode remains a matching local choice; a failed key or tag never triggers a
public fallback. Regular encryption and key derivation remain unchanged.

Integrity-checked source areas accumulate in bounded RAM without interpretation.
**Six seconds of fully scored physical absence** is the only end event.
EOF, cancellation, quotas, a valid digest, or a final flag cannot substitute for
it. WAV transmission includes 6.25 seconds of real silence after the pulse tail.
Only then can flags/padding be interpreted and an explicit Save handle appear.
Incomplete cycles, missing finals, invalid fill, integrity failure and exhausted
storage leave no completed source.

Failed marker groups retain their timed positions as erasures. Trailing absent
intervals are held as a bounded count and delivered only if a later marker
resumes the stream, avoiding fictitious source data during end silence.
A capture overrun with unknown missing time fails the transfer. Capture runs
separately from DSP/FEC with a bounded one-second queue; decoding near its
iteration limit requires measured scheduling headroom. There is no automatic
retransmission, arbitrary mid-file join or recovery across unknown time gaps.

Receive source-area storage is capped at 256 MiB and compacts in place after
physical completion. Capacity requires source bytes plus one flag per cycle
and final-cycle fill; classic additionally needs its 1.125 source expansion.
PCM, FEC, diagnostics and shared code tables have separately bounded storage;
the source quota is not a process RSS limit. Source reads and waveform generation
stream. Text retains its separate 32,768-byte local limit.

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
format. Existing named/pad-backed keyfiles remain available.
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
classic wire preset and current SSB preset use 16-APSK, fail closed at 10 dB and recover
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
