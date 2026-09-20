# Fast text and file transfer

Fast Modem is a separate streaming APSK modem. Select **Fast Modem** from the
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

Defaults use 48 kHz samples, 20% root-raised-cosine rolloff, a 16-symbol pulse
span, rate-3/4 convolutional coding and robust RS. Interleave depth is 16 except
for acoustic (5). These defaults target files of 100 KB (100,000 bytes) and larger. The CLI
also accepts 44.1–192 kHz sample rates. Device passband validation is separate
from these nominal waveform settings.

| Profile | Symbol rate | Carrier | Ideal shaped audio support | Default constellation | Gross rate at 256-APSK |
| --- | ---: | ---: | --- | --- | ---: |
| `wire` | 15,000/s | 9,300 Hz | 300–18,300 Hz | 16-APSK | 120 kbit/s |
| `ssb` | 2,000/s | 1,500 Hz | 300–2,700 Hz | 16-APSK | 16 kbit/s |
| `fm` | 2,000/s | 1,500 Hz | 300–2,700 Hz | QPSK | 16 kbit/s |
| `acoustic` | 500/s | 1,800 Hz | 1,500–2,100 Hz | QPSK | 4 kbit/s |

The acoustic preset now favors proximity speaker/microphone links: QPSK at
500 symbols/s with a 600 Hz occupied band, output amplitude 0.35 and interleave
depth 5 to spread short erasure bursts while limiting startup/padding waits. Both peers must use this revised
preset; it does not match the former 6,666.667-symbol/s acoustic setting.

All profiles offer QPSK and 16/64/256-APSK. These are local choices, without
over-air rate negotiation or automatic fallback. Denser constellations require
better residual EVM and a linear path; a large nominal SNR alone is insufficient.
Gross rates exclude all synchronization, FEC, encryption, source-cell and end
silence overhead. The measured 2 MiB wire example below achieves about
44.14 kbit/s of file bytes at 256-APSK, rate 7/8 and robust RS.

The radio presets reserve 300 Hz at each edge of a nominal 3 kHz audio path.
Their 2.4 kHz shaped width leaves room below the 2.8 kHz HF data bandwidth
guideline requested for this design. The FCC order applies to specified HF
bands; it is not a blanket VHF bandwidth rule.
[FCC 23-93](https://docs.fcc.gov/public/attachments/FCC-23-93A1.pdf).
The IC-7100's SSB filter and transmit bandwidth settings, FM data path and
selected input affect the actual response. These presets target its audio
passband; they do not configure the radio or certify its emitted spectrum.
Use the [Icom full manual](https://www.icomjapan.com/support/manual/2288/)
for the chosen connection and filter settings. Finite RRC tails, radio filters,
AGC, pre/de-emphasis, clipping and sound-card response require device measurement.
Part 97 generally prohibits encryption intended to obscure meaning, subject to
its exceptions; the encrypted preset is not permission for an amateur-band
transmission. [47 CFR 97.113](https://www.govinfo.gov/content/pkg/CFR-2025-title47-vol5/pdf/CFR-2025-title47-vol5-sec97-113.pdf).

## Airtime, payload rate and theoretical capacity

For future optimization, see the [Fast coding and capacity study](fast-coding-study.md).
It records the throughput-first objective, LDPC/RS and constellation tradeoffs,
interleave/padding effects, three channel comparisons and near-Shannon
extrapolations. Its proposed codes, shaping and marker cadence are not implemented
settings; this document continues to describe the current wire format.

The GUI estimates airtime from the exact local source size, nine-bit source
cells, bootstrap, complete interleave cycles, sync/pilots, pulse tail and 6.25
seconds of end silence. Active TX percentage counts generated audio samples,
including that silence; it stays below 100% until playback drains. Device queues
can make audible playback lag the producer. A source file changed after inspection
can invalidate the estimate. An empty draft estimate is an empty source transfer.

The GUI exposes interleave depths 1, 4, 5, 16 and 64. Depth 16 remains the cable/radio default (acoustic uses 5);
64 reduces padding loss for long cable transfers at the expense of larger
coding cycles and short-message latency. Both peers must match. At 256-APSK,
7/8 coding and high-rate RS, the long public-data ceiling is about 53.1 kbit/s
at depth 16 and 54.5 kbit/s at depth 64, before start/end overhead. The 120
kbit/s gross figure is mapper capacity; markers, pilots, coding, checksums and
nine-bit source cells consume the remainder.

The GUI and `fast-info` report Shannon-Hartley capacity, `C = B log2(1 + S/N)`,
using occupied bandwidth `B = symbol_rate × (1 + rolloff)` and an explicitly
assumed 30 dB in-band SNR. This is an ideal Gaussian-noise capacity example,
not measured channel SNR, a throughput promise or a model of room echoes.

Both ends can independently use 44.1 or 48 kHz hardware. Audio negotiation and
the existing bandlimited resampler bridge device and modem sample rates; local
device sample rate is excluded from the matching integrity context. The cable
profile's 18.3 kHz upper edge fits the 18.522 kHz conservative passband of a
44.1/48 kHz conversion. No higher baud rate is forced on a 44.1 kHz device.

## Fixed intervals, without packets or received lengths

The modem consumes exactly **2,048 coded bits (256 bytes)** per physical interval.
A fixed 64-symbol public QPSK synchronization word precedes every interval.
There are no transmitted packet lengths, payload counts, filenames, message
types, addresses, negotiated profiles or variable modem headers. Receiver
allocation and coding boundaries depend only on the locally validated profile.
The synchronization word is the requested regular alignment magic; it does not
delimit a variable-size object.

The waveform starts with 128 known QPSK training symbols. Every 32 data symbols,
and after a final shorter group, four known QPSK pilots maintain phase tracking.
The coded bit span remains exactly 2,048 positions. At 64-APSK only, the final
six-bit mapper symbol has four locally fixed zero fill bits outside that span.

| Constellation | Data symbols per interval | Total symbols including marker and pilots |
| --- | ---: | ---: |
| QPSK | 1,024 | 1,216 |
| 16-APSK | 512 | 640 |
| 64-APSK | 342 | 450 |
| 256-APSK | 256 | 352 |

Constellations have Gray-labelled rings, normalized to mean symbol energy one.
Ring populations are `[4]`, `[4,12]`, `[4,12,20,28]`, and
`[4,12,20,28,36,44,52,60]`. Multi-ring radii start at 1 and increase by 1.8,
except 16-APSK's outer radius is 2.85. This is a project wire format, not a claim
of DVB compatibility. The frozen marker uses the implementation's xorshift32
sequence seeded with `0x65a39c17`; training permutes it and rotates by pi/2.
Independent wire fingerprints and mapper tests protect these constants.

The bulk defaults retain robust RS: its 16 parity bytes per codeword correct
up to eight unknown byte errors (or 16 erasures), twice the high-rate choice.
High-rate RS increases source capacity by only 8–9%; rate 3/4 inner coding
provides the larger throughput gain while retaining that outer correction budget.
Rate 1/2 remains available for difficult links and 7/8 for cleaner links.
No setting is universally optimal for an unmeasured channel.

At the default code rate, depth 16 rounds to 22 physical intervals and depth 5
to seven. Tests compare exact airtime against continuous interleaving at the
same FEC rate: cycle rounding, bootstrap and final fill together stay below 10%
for every source of at least 100,000 bytes, encrypted and public, in all profiles.
Exact estimates are also checked at 100,000 bytes, 100 KiB, 1 MiB and 16 MiB.
This is interleave overhead, not total protocol overhead or a measured error rate.
A 16-byte encrypted Fast message takes less than 45 seconds in every profile,
including the existing 6.25-second silence; no special short-message framing is
introduced. Estimated 16-byte airtimes are 8.14 s (wire), 20.40 s (SSB),
33.07 s (FM) and 40.58 s (acoustic). Both peers must select the same revised defaults. To communicate
with previous defaults select rate 1/2 and depth 16 (acoustic: 4).

The standalone `fast_regression` tool now defaults to 100,000 source bytes;
use `--bytes` explicitly for smaller diagnostic fixtures.

## Coding and interruption handling

Each fixed outer group contains two shortened Reed–Solomon codewords:

| Local RS choice | Codewords | Systematic bytes | Encrypted source area | Public source area |
| --- | --- | ---: | ---: | ---: |
| Robust | 2 × RS(128,112) | 224 | 176 | 192 |
| High rate | 2 × RS(128,120) | 240 | 192 | 208 |

Encrypted groups contain a 16-byte IV, the fixed ciphertext source area and a
32-byte HMAC-SHA256 tag. Public groups contain the fixed plaintext source area
and a 32-byte SHA-256 checksum. Both use identical physical coding geometry.

An interleave cycle contains `D` outer groups (`D=16` for cable/radio, `D=5` for acoustic, CLI range
1–64). Its `2D` RS rows are transmitted column-first. A K=7 convolutional code
uses generators 0171 and 0133, with selectable rates 1/2, 3/4 or 7/8.
The puncture pairs are `[11]`, `[11,10,01]` and
`[11,10,10,10,01,01,01]`, respectively. Each cycle starts in state and puncture
phase zero, ends with six zero trellis bits, and fills the last physical
interval with zero coded bits. At depth 16 this consumes 33, 22 or 19 physical
intervals, respectively. No cycle size is received from the channel.

Reception performs soft Viterbi decoding, deinterleaving, RS correction using
unreliable-byte erasures, then authentication before decryption when encryption
is on, or public checksum verification when it is off. Fixed cycle
decoding bounds work and memory and limits propagation of inner-code errors.
There is no LDPC decoder, iterative source recovery or retransmission protocol.

The receiver uses matched filtering, fractional sample interpolation, marker
timing/phase/frequency fits, carrier and timing loops and pilot/decision error
feedback. Cable and radio profiles retain the five-tap fractionally spaced LMS
equalizer. Acoustic uses 21 half-symbol-spaced taps, trained over each known
marker with bounded normalized LMS passes before payload slicing. Its raw
marker coherence threshold is 0.55 (other profiles retain 0.72), allowing
moderate echoes to acquire before equalization. This remains an independent
known-marker presence check, not a constellation-error or source-validity gate.
Acoustic QPSK allows squared decision error up to 0.7 in its tracking loop;
the denser constellations retain their tighter cutoffs. Coherent acoustic pilots
with high residual error downweight their group's soft evidence to 20%;
incoherent pilots still erase it. Neither adjustment changes correction or
integrity acceptance limits. Low constellation error alone cannot keep a
reception alive on silence or a tone. These operations are wholly within the
Fast modem and never call regular search or symbol recovery.

Failed markers retain their timed interval positions as erasures. Trailing
failed intervals are held as a bounded count and only delivered to the codec
if a later marker resumes the stream. This avoids appending fictitious coded
data during end silence. A capture overrun with unknown missing sample time
fails the transfer instead of concatenating discontinuous audio. A separate
capture worker and a one-second bounded queue keep FEC work out of the
capture callback.

Burst repair has finite limits determined by depth, code rate and symbol rate.
Tests recover the selected 1/10/50 ms erasure fixtures and a 10 ms additive sound
effect; a 500 ms destructive interruption leaves no completed file. These are
specific regressions, not guarantees for every sound, phase slip or hardware
dropout. Initial acquisition must include the integrity-checked bootstrap cycle;
there is no arbitrary mid-file join or unknown-time-gap recovery.

## Cryptography and exact source bytes

With encryption enabled, Fast uses OpenSSL AES-256-CBC with padding disabled and a fresh unpredictable
16-byte IV per fixed outer group. Encrypt-then-HMAC-SHA256 covers the versioned
Fast domain, local profile, transfer salt, locally counted group ordinal, IV
and ciphertext. Tags are compared before decryption. The ordinal is not a
transmitted integer. Corruption, missing or reordered groups cannot silently
shift the accepted source.

The first complete coding cycle is a fixed bootstrap: a fresh 32-byte salt,
32-byte keyed bootstrap tag and canonical zero fill, protected by the same
RS/interleaver/convolutional geometry. It carries no lengths or profile fields.
Its tag binds `DataPump/fast/v1/bootstrap`, the local profile and salt using
the unchanged keyfile authentication primitive.

Key derivation is HKDF-SHA256 with
`IKM = Crypto.mac("DataPump/fast/v1/root")`, the transfer salt as extract salt,
and separate expand labels `DataPump/fast/v1/AES-256-CBC` and
`DataPump/fast/v1/HMAC-SHA256`, each followed by the profile context.
Each output is 32 bytes. Labels have no terminating NUL. The context starts
with `datapump/fast/v1`, followed by big-endian 64-bit channel, constellation,
code-rate enum, RS choice and interleave depth, then the big-endian IEEE-754
bit patterns of symbol rate, carrier and rolloff. Device sample rate and local
output amplitude are excluded. See `profile_id()` and the independent crypto
vectors for the exact encoding. Regular epoch streams and key derivation are
unchanged; Fast never calls `Crypto::stream`.

With encryption disabled, there is no AES, IV or secret key. The same fixed
bootstrap cycle contains a fresh 32-byte salt, a public SHA-256 checksum and
canonical zero fill. The checksum hashes the concatenation of
`DataPump/fast/v1/public/bootstrap`, the profile context above and the salt.
Each public group's checksum hashes `DataPump/fast/v1/public/group`, the profile
context, salt, big-endian 64-bit locally counted group ordinal and the complete
fixed plaintext source area. These checks detect corruption and accidental
mixing, but an adversary can rewrite them: they provide no authentication.
The receiver records public `checksum_groups` separately from
`authenticated_groups`, and its `authenticated` flag remains false.

Protection mode is local configuration, never a received flag or guessed from
the stream. A public receiver rejects an encrypted bootstrap and vice versa;
an encrypted receiver never retries as public after a key or tag failure. The
existing encrypted byte format and independent wire vectors remain unchanged.

Source bytes use fixed nine-bit cells: one valid bit followed by the byte's
eight bits, MSB first. A mandatory all-zero invalid cell follows the final
byte; the remainder of the minimum final coding cycle is zero. This preserves
empty text/files and all leading/trailing zero bytes without transmitting a source
length, compression header or filename. It costs one bit per source byte.

Integrity-checked source areas accumulate in bounded RAM during reception, without source
interpretation. Only the DSP's observed physical end enables cell interpretation
and an explicit save handle. The invalid source cell cannot end the modem.
Missing endpoints, extra fill cycles, nonzero fill, incomplete coding cycles,
failed authentication/checksums and storage exhaustion all leave the source incomplete.

Physical end requires six seconds of fully scored absence. WAV transmission
adds 6.25 seconds of actual silence after the filter tail. EOF, cancellation,
quota exhaustion, a valid tag or the source endpoint cannot substitute for it.
The 256 MiB maximum receive buffer stores encoded source areas in RAM. After
physical completion, source cells compact in place into the completed file;
there is no second output spool. Allow 1.125 times source size plus final-cycle
fill. PCM, FEC and diagnostic scratch have separate bounded storage. Outgoing
files and waveforms stream; text has its separate 32,768-byte local limit.
No temporary receive files, disk caches, autosaves or persistent receive history
are created. FLTK file-chooser preference writes are disabled as well. Only explicit Save, CLI `--save`/`--output`, and requested keyfile
creation write files. Operating-system swap, hibernation and crash dumps are
outside this application-level guarantee; RAM here is not locked against paging.
Graphics drivers may maintain their own shader caches; these are not receive buffers.
Robust receive source storage is also now held in RAM, with a shared 256 MiB
maximum across candidates; its physical-end and short-message rules are unchanged.
Legacy retains its bounded text transcript in memory.

There is no persistent replay database: a complete previous authenticated
transfer can be replayed. Packet removal reduces untrusted framing complexity;
it does not by itself prove memory safety or cryptographic security. Sound
device drivers, the explicitly selected WAV container reader and keyfile reader
still have their own validated formats. The WAV reader bounds RIFF chunks and
never uses their sizes as modem lengths. Output WAV files use S16 PCM and the
classic RIFF 4 GiB size limit, with an explicit error on overflow.

## CLI examples

```sh
./build/pump keygen --output keys.bin --key-names Fast
./build/pump fast-info --profile wire --apsk 256 --code-rate 7/8

# Public text: no keyfile is needed on either peer.
./build/pump fast-tx --profile wire --text 'Hello from Fast mode' --output text.wav
./build/pump fast-rx --profile wire --input text.wav --json

# Files and text can both use optional encryption.
./build/pump fast-tx --profile wire --apsk 256 --code-rate 7/8 \
  --keyfile keys.bin --key-name Fast --input source.bin --output transfer.wav
./build/pump fast-rx --profile wire --apsk 256 --code-rate 7/8 \
  --keyfile keys.bin --key-name Fast --input transfer.wav --save received.bin --json

./build/pump fast-listen --profile wire --keyfile keys.bin --device default \
  --save received.bin
./build/pump fast-tx --profile wire --keyfile keys.bin --device default \
  --input source.bin
```

`fast-tx` takes exactly one of `--text TEXT` and `--input FILE`. An empty text
argument is accepted. Without `--keyfile`, Fast selects public mode; providing
`--keyfile` enables encryption and preserves existing encrypted commands.
`--encrypt` requires a keyfile, while `--no-encryption` explicitly selects public
mode and skips loading any supplied key options. These flags conflict.
`--key-name` or `--pad` without a keyfile is an error unless `--no-encryption`
explicitly selects public mode. Invalid keys never cause a public fallback.

`fast-listen` handles one reception. Ctrl+C or `--seconds N` cancels without
manufacturing completion. `--rs robust|high-rate`, `--interleave`, `--sample-rate`, `--quota-mb`,
`--stereo` and named/pad-backed existing keyfiles are available in the CLI.
`fast-info` reports local geometry and selected protection without loading a key.
Completed receive output contains metadata only; JSON distinguishes
`encrypted`, `authenticated`, and `checksum_groups`. The former `text_preview`
and `preview_truncated` fields and preview API have been removed. Live transmit
progress goes to stderr; JSON includes `estimated_seconds` and `transmit_fraction`.
Receive exit code 2
means incomplete; unsuccessful saves report an error. No radio PTT/CAT control
is implemented.

## SNR-only regression tool

`fast_regression` is built only with `BUILD_TESTING`. It is separate from the
regular channel simulator, link planner and probability estimates, and is not
installed as a user simulation interface. With no `--snr`, it runs 10 through
120 dB inclusive in 5 dB increments. Fixtures use deterministic test-only
salt/IV generation for reproducible waveforms; production uses OpenSSL random
generation and exposes no setting for this fixture generator.

```sh
./build/fast_regression --profile wire --bytes 1024 --seed 417 > wire.csv
./build/fast_regression --profile ssb --bytes 1024 --seed 417 > ssb.csv
./build/fast_regression --profile fm --bytes 1024 --seed 417 > fm.csv
./build/fast_regression --profile acoustic --bytes 1024 --seed 417 > acoustic.csv
./build/fast_regression --profile wire --apsk 256 --code-rate 7/8 \
  --bytes 2097152 --snr 60 --seed 417 --require-success > large.csv
```

SNR refers to signal power versus noise in the declared shaped bandwidth
`B = symbol_rate × (1 + rolloff)`. The tool measures the generated waveform's
mean power before adding noise, then sets real full-band AWGN variance to
`Ps × 10^(-SNR/10) × Fs/(2B)`. CSV includes measured matched-filter noise
bandwidth, Es/N0, exact-file comparison, physical completion, EVM, correction
counts, source goodput including end silence, scratch/spool bytes and CPU/wall
time. Callback timing is reported separately from mean real-time ratio;
occasional FEC work exceeds one PCM chunk duration and requires the live queue.

The recorded four-profile sweep contains 92 seeded sampled tests. Wire and SSB
default 16-APSK fail closed at 10 dB and recover exact files at 15–120 dB;
FM and acoustic default QPSK recover at every tested point. A single seed and
file per point do not estimate a reliable BER/PER curve. Separate DSP tests
cover fractional start phase, 44.1 kHz, sample-clock offset, additive impulses,
hum, phase slips, missing markers and non-signal input.

When Python is available, CTest's `fast_snr` runs this entire matrix and checks
exact completion at the established passing points, all 23 levels, argument
bounds and option-order independence. Improvements at lower SNR are allowed;
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
