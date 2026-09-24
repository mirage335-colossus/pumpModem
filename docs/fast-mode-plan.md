# Plan: isolated fast APSK file transfer without packets

Status: original design and source audit, reviewed 2026-09-19. The implementation
now exists; [Fast mode](fast-mode.md) specifies its actual behavior and limits.
The recommendations below remain the design history, not the wire specification.
Subsequent [throughput and coding research](fast-coding-study.md) records the
revised throughput-first objective, LDPC/RS candidates, measured limits and
future optimization priorities; consult it before extending this original plan.
The subsequent user revision adds text sources and optional encryption. Public
mode uses separate checksum domains; encrypted wire vectors remain unchanged.

Implemented baseline: separate DSP/codec/GUI, fixed 2,048-coded-bit intervals,
QPSK through 256-APSK, convolutional plus interleaved RS, CBC/HMAC, four profiles,
bounded streaming files/audio and independent SNR regressions. The authenticated
salt bootstrap occupies one complete fixed coding cycle. There is no separate
supercycle marker or received cycle index: cycle phase starts with acquisition
and advances at the local cadence. Unknown sample gaps fail incomplete. LDPC,
automatic rate adaptation, arbitrary mid-file joins, hardware qualification and
spectral-mask certification remain outside this implemented baseline.
The original audit used commit `2373fba`. The
[development contract](development.md) remains authoritative for regular mode;
this proposal does not supersede the [current protocol](protocol.md).

## 1. Recommendation

Build a separate fast engine, file-transfer service and GUI controller. Keep the
existing regular engine intact. Fast uses a conventional trained, continuously
tracked APSK carrier, fixed physical alignment intervals, concatenated error
correction and IV-based authenticated encryption. It has no private spreading,
time-indexed payload keystream, single-bit pattern acquisition, weak-link search,
post-end exhaustive recovery or LPI claim.

There is no active packet transport left to remove from regular mode. The old
packet/APSK implementation described in the historical migration and security
documents is already gone. Reintroducing its header/parser or reopening its
compatibility switches would reverse the existing migration.

For fast mode, define the requested interval precisely: **a fixed sync word
followed by 2,048 inner-FEC output bits, representing 256 transmitted byte
positions**. The sync word is additional overhead. No received length, type,
version, modulation choice, FEC choice, address or filename determines a modem
allocation, next boundary or completion event. All geometry is local and fixed
for the whole transmission. Both ends must select the same profile.

This is a plan for implementation, with candidate numerical profiles below.
Rates, constellation thresholds and interruption tolerance are engineering
targets pending sampled tests and physical measurements, not measured features.

## 2. What the current code actually does

Line references below identify the audited checkout, not permanent interfaces.

| Area | Active path and consequence |
| --- | --- |
| TX selection | `src/transfer.cpp:200` selects nonempty text of **1–16 source bytes inclusive**. `wire_bits()` at 324–359 sends its exact fixed-dictionary bits, explicit raw bits, or marked fixed intervals. Preserve all three paths. |
| Long TX | `encoded_intervals()` at `src/transfer.cpp:162` invokes attachment encoding, source encoding, interval authentication/RS and marker insertion. The source, coded bytes and byte-per-bit vectors are materialized; streaming PCM does not make this a streaming file API. |
| Fixed geometry | `include/datapump/stream_codec.hpp:54` fixes 128 coded bytes; `include/datapump/boundary_sync.hpp:10` fixes the 192-bit marker and cadence. Fast must not change these constants. |
| FEC | `src/stream_codec.cpp:64–78` provides shortened GF(256) RS with at most 255 symbols. A conventional RS(256,k) cannot be obtained by increasing the existing constant. |
| Modem | `src/modem.cpp:102–104` rejects the old APSK setting. `src/streaming_modem.cpp:15–60,157–199` wraps pattern TX/RX; legacy symbol methods deliberately fail. Implement new classes instead. |
| RX progress/end | `src/pattern_receiver.cpp:887–907,1357–1362` scores complete symbols and accumulates failed durations. `finish()` at 1412 flushes without inventing completion; the drain methods publish accepted bits. |
| RX content | `src/transfer.cpp:565–600` collects fixed intervals and gates source/attachment interpretation on `burst.complete`. It then reads the sealed spool into a full vector. Fast needs its own incremental disk reader after completion. |
| Recovery | `src/recovery.cpp` is the existing bounded post-end hard-bit search. It is a regular-mode feature, not a fast FEC decoder or synchronization fallback. |
| Crypto | `src/crypto.cpp:496–568` implements epoch/offset AES-CTR streams and HMAC. GCM at 232–263 and 377–396 wraps local keyfiles; it is not an existing fast transport. |
| Key lengths | `src/crypto.cpp:435–469` validates local keyring dimensions, then parses authenticated entries. Its `payload_length` and name lengths are not received modem packet lengths. |
| Live service | `include/datapump/live.hpp` and `src/live.cpp` are regular-specific. `halt()`/`configure()` at 475–503 cancel workers, reset generations and clear state; they cannot be used as harmless GUI view switches. |
| GUI ownership | `src/gui/application.cpp:19–46` owns a regular controller and bitmap producer. `controller.cpp:147–221` owns regular session/settings, drafts, planner, estimates, recovery and records. A boolean threaded through these objects would create substantial coupling. |
| GUI header | `src/gui/screen_console.cpp:131` declares DATA PUMP; `desktop_layout.hpp:63–72` lays out the header. Add a shared title-adjacent Fast slot here, with explicit space for status. |
| Native screens | `backend_fltk.cpp:636–654` and `backend_rev.cpp:722–766` construct static shared pages/controls. Whole-interface replacement needs a generic shared screen lifecycle in both adapters. |
| Audio | `include/datapump/audio.hpp:28–39` supplies bounded PCM callbacks; `src/audio.cpp` uses S16 device samples. `src/resampler.cpp:82–84` limits differing-rate conversion to 0.42 times the smaller sample rate. |
| Simulation | `src/channel.cpp`, `simulation_estimate.cpp`, regular `transfer::simulate` and live replay model pattern reception. None is a suitable fast-mode simulator merely by changing an SNR field. |
| Build | `CMakeLists.txt:23–30` links the current runtime as one library; `cmake/GuiShared.cmake` gives both native adapters the same shared application. Add a fast target with enforceable dependencies. |

Repository searches found no active packet codec/API or received modem length
parser. Historical packet references in `docs/packetless-stream-plan.md` and
`docs/security-review.md` are explicitly marked historical. Legacy
`expected_bits`/`expected_bytes` live presentation fields do not constitute a
received-length protocol. Preserve history and accurately label it; cosmetic
renaming is not a security improvement.

Other parsers remain: bounded post-end LZMA2, attachment-prefix interpretation,
WAV/device formats, local keyrings and GUI/CLI inputs. Fixed transport geometry
removes a source of remote dimension calculations; it does not establish that
all parsing has disappeared or that buffer overflows are impossible. No exploit
in the removed packet implementation was established by this audit.

## 3. Enforce separation before implementing the waveform

Proposed ownership:

```text
shared GUI Application / mode host / exclusive audio ownership
  regular Controller -> existing live::Session -> existing transfer/pattern code
  fast::Controller   -> fast::Session -> fast::Transfer -> fast::Transmitter/Receiver

include/datapump/fast/    config, profile, crypto, fec, interval, modem, transfer
src/fast/                implementations and fast live service
src/gui/fast/            controller, state, screen, layout, plots
tests/fast/              wire vectors, sampled receiver, streaming, simulation
```

Add `datapump_fast` and a fast regression executable. Do not add fast branches to
regular `modem::Config`, `transfer::Options`, short dictionary, marker collector,
receiver, tuning or probability model. Existing CLI commands remain regular;
new explicit fast commands dispatch to a separate implementation. Do not
reinterpret existing `--rate`, `--target-snr`, `--pattern` or `--simulation` flags.

Reuse unchanged audio callbacks and narrowly reviewed utilities. The generic RS
arithmetic is useful, but currently shares a translation unit with regular
source/interval code. Initially preserve it in place behind a narrow interface,
or make a separately reviewed behavior-preserving extraction with the existing
independent vectors unchanged. Do not require a wholesale regular-engine
refactor before adding fast mode. Inspect transitive includes and link
dependencies, not just directory names.

Add dependency checks forbidding fast DSP from invoking regular pattern search,
epoch streams, LPI/probability models, short compression and exhaustive recovery.
The regular DSP must not depend on fast code. Shared application glue alone
selects the engine. Keep the fast simulator out of production GUI/live targets.

## 4. Fixed geometry, including inner coding and interruptions

There are two different dimensions; do not confuse them:

1. An **outer coding group** is two 128-byte shortened RS codewords, 256 bytes
   total. This provides convenient fixed crypto and RS positions.
2. A **physical interval** contains exactly 2,048 bits *after* inner coding,
   preceded by fixed synchronization symbols. It stays 256 byte-equivalents at
   every selected coding rate.

Inserting markers before a rate-1/2 encoder would put 512 transmitted bytes
between markers. That would not meet the proposed physical interval requirement.
Insert magic after inner encoding; remove it before inner decoding. Magic and
pilot positions have their own fixed symbol schedule and consume no payload
trellis bits. Use a public sync waveform distinctive from the regular marker.
This is the requested training/alignment sequence, not the regular mode's
noise-codeword pattern mechanism.

Define each local profile with constants for constellation mapping, RS k,
interleave depth D, inner code/puncturing, termination, marker symbols, pilots,
filter coefficients and a fixed repeating coding schedule. The schedule is a
supercycle of N physical intervals, not an additional variable envelope.
For D outer groups, calculate the exact encoded size of `2048*D` input bits
plus fixed convolutional termination. Round to N whole 2,048-bit physical
intervals using fixed known fill. Specify that fill and any mapper alignment
symbols explicitly; no received value selects how much to remove.

QPSK, 16- and 256-APSK divide 2,048-bit spans exactly by their bits/symbol.
For 64-APSK, add four fixed mapper-fill bits to produce 342 symbols; remove
those four known positions before inner decoding. The fill is distinct from
the 2,048 coded payload bits. Future 32-/128-APSK profiles likewise need
explicit fixed mapper fill; rounding cannot silently lose bits.
Freeze complete independent vectors covering puncture phase, fill, RS
interleaving, bit order, constellation labels and both kinds of boundaries.

The acquisition preamble includes a fixed-size protected session salt and known
training, with no format negotiation. A distinctive, fixed sync variant at each
supercycle start establishes interleave/trellis phase; ordinary markers retain
the 256-byte cadence. Verify sync with bounded sliding correlation, predicted
timing, constellation residual and subsequent cadence. Never scan decrypted
content for a magic value and treat it as a framing command. A magic match is
not authentication; bound false-lock trials and test payloads containing magic.

The receiver advances a local absolute symbol/coding position through a sound
effect while samples continue to arrive. Mark unreliable positions as erasures;
never delete them and shift following ciphertext. The clock/phase loops use
bounded holdover, then fixed-cadence reacquisition. A gap with unknown duration
(for example an audio overrun without a usable timestamp) must not guess the
absolute crypto/interleave ordinal. Mark the transfer incomplete if its position
cannot be established. An ordinary repeated marker alone cannot reveal how many
whole intervals were lost.

No ARQ, fragment identifiers, variable records, capability exchange, remote
offset writes or selective retransmission protocol is proposed. Recovery of
short interruptions comes from tracking, interleaving and FEC. No finite FEC
budget can recover an arbitrary outage; a damaged transfer can be repeated as
a new transmission with a new session salt.

## 5. IV block encryption without time synchronization

Use an isolated OpenSSL AES-256-CBC encrypt-then-HMAC-SHA256 implementation as the
initial concrete design. It follows the requested IV/block model and uses
fixed block-multiple ciphertext widths, with padding disabled. CBC requires
unpredictable IVs; generate a new 16-byte IV for every outer group and authenticate
it with the ciphertext. CBC alone is not integrity protection. See
[NIST SP 800-38A](https://csrc.nist.gov/pubs/sp/800/38/a/final).

Two candidate local profiles fit exactly:

| Outer group | Systematic bytes | Fixed systematic layout | RS parity |
| --- | ---: | --- | ---: |
| Higher rate: two RS(128,120) | 240 | IV 16 + ciphertext 192 + HMAC 32 | 16 total |
| More burst protection: two RS(128,112) | 224 | IV 16 + ciphertext 176 + HMAC 32 | 32 total |

Split the systematic area into two fixed halves before RS encoding. Interleave
only after both codewords are formed. On RX: inner decode, deinterleave, RS
correct, verify HMAC in constant time, then decrypt into private bounded storage.
Do not release unauthenticated plaintext or distinguish padding/authentication
errors; no variable CBC padding is needed in this design.

Use separate fast encryption and MAC keys, derived with new versioned domains
and the session salt. Bind the fixed local profile, salt, locally counted outer
group ordinal, IV and entire ciphertext into the MAC. An ordinal is a checked
local counter, not a transmitted length or remote file offset. Stop before
counter overflow. Missing groups retain their positions. Do not chain CBC state
across outer groups: losing one group must not corrupt decryption of the next.
If the initial salt cannot be recovered, this transmission remains unavailable;
late joining does not fabricate a complete file.

The existing `Crypto` object stores five derived purpose keys, not the original
master (`src/crypto.cpp:36–45`). Therefore “HKDF the existing master” is not an
implementable integration instruction. Add a narrowly scoped fast-key operation
that domain-separates derivation inside the protected key implementation, or use
a separate fast key handle. Preserve every existing regular key, keyring byte
format, derivation label, stream output and HMAC vector. Do not serialize a sixth
key into existing keyrings or expose raw regular secrets to the GUI. Review the
new derivation with independent vectors before enabling encrypted transmission.

A fresh random session salt avoids dependence on clocks; it does not by itself
reject replay of an entire earlier transmission. State replay policy explicitly
and, if required, use bounded persistent authenticated-session history. Nothing
in fast mode should alter regular cooldown, timestamp or key selection behavior.

## 6. Exact files without a transmitted size

Use streaming input/output APIs, not `Message::data` plus whole-file bit vectors.
For the initial fast source representation, encode fixed nine-bit cells:
one validity bit followed by one byte. A valid cell carries any byte, including
zero. Append a mandatory invalid zero cell even when source bytes exactly fill
the previous area, then canonical zero fill through the fixed final supercycle.
Carry cells across crypto-area boundaries in a fixed bit order. There is no
integer source length, filename length or packet payload count on the wire.

The invalid cell is an application endpoint, inside authenticated encryption;
it never stops the modem. Only after physical completion, with every required
group present and authenticated, decode cells and verify that everything after
the endpoint is canonical fill. A whole lost final group cannot silently turn a
valid prefix into a complete file. Extra full padding supercycles are rejected
by a canonical minimal-final-supercycle rule. Missing an interior group never
causes neighboring source areas to be joined.

The validity representation costs roughly 1/9 of source capacity. It is a
deliberate simple baseline for already-compressed large files. Optional streaming
compression can later replace it under a matching *local* profile. Its bounded
application decoder runs after physical completion and cannot select modem
framing; compressed codecs do contain internal grammar and must not be described
as eliminating all length parsing. Avoid speculative decompression in callbacks.

Stream corrected/authenticated fixed areas to a quota-controlled private spool.
After physical completion, transform that spool into a second bounded output
spool without reading the entire file into memory. Charge both spools, codec
scratch, interleave buffers, device queues and retained diagnostics. GUI records
hold immutable handles, counts and bounded previews, not multi-megabyte copies.
Saving uses an explicit local destination and exclusive creation. Initially use
a locally supplied filename; no remote filename needs to be transported.

Keep the observed-absence rule: six seconds covered by fully observed failed
fast symbols is the physical end. Fast uses its own presence/lock metric, not
regular iterative search. Scheduled pilots/training are accounted for in that
metric. Nearest-constellation EVM alone cannot establish signal presence:
dense constellations and AGC can make noise look close to valid points. Require
an independently validated presence statistic using known sync/pilot evidence
and measured noise, with a fixed decision horizon and whole-symbol accounting.
Freeze its thresholds and holdover behavior before claiming physical completion.
Noise-only, unrelated tones and sound-effect fixtures must demonstrate that a
decision-directed loop cannot keep resetting absence merely through low EVM.
FEC failures, MAC failures, a source endpoint, EOF, cancellation, quotas
and mode changes cannot manufacture physical completion. A fade longer than the
end threshold may split a transmission and leave the file incomplete. Even an
apparently valid terminal source cell does not authorize early final delivery.

## 7. DSP and concatenated coding

TX pipeline:

```text
file reader -> validity cells -> fixed crypto groups -> two RS words
 -> fixed-depth byte interleaver -> convolutional encoder/puncturer
 -> 256-byte physical spans + sync/pilots -> APSK -> RRC -> real PCM
```

RX uses matched filtering, interpolating clock recovery, carrier recovery,
equalization and soft APSK demapping before reversing the coding chain. Use
known training for initial gain, phase, frequency, timing and equalizer taps;
then update timing/carrier/equalizer from bounded constellation-error feedback
with periodic pilots. Freeze adaptation or reduce its gain during detected
interference so a notification sound does not train the equalizer to noise.
Resolve phase ambiguities through training, not source syntax or MAC trials.

Use fractional-delay sampling and a fractionally spaced equalizer to handle
sound-card clocks and speaker/room paths. Retain bounded filter history and
fixed computation per callback. Precompute mapping tables; use efficient FIR
kernels, bounded soft metrics, saturating path metrics and worker queues with
backpressure for file/encoding producers. RX callbacks cannot backpressure the
ADC: queue or disk overload records a timed discontinuity/incomplete result
under the local quota policy instead of blocking capture or dropping time
silently. Do not reuse the regular crest limiter without demonstrating its
effect on dense APSK EVM and spectral regrowth. Dense rings require linear
headroom; a high nominal SNR is insufficient when clipping or ALC distorts them.

Start with a conventional constraint-length-7 convolutional code and soft Viterbi
decoding, with fixed traceback and selected puncturing profiles. Freeze generator
polynomials, bit ordering and termination in the wire specification before
coding. Evaluate rates 1/2, 2/3, 3/4 and 7/8 using measured goodput. Viterbi is a
bounded trellis calculation, not the regular modem's hypothesis/recovery search.
Outer RS handles residual error bursts and byte erasures after inner decoding.
Feed unreliable APSK bits to the inner decoder as neutral soft evidence.
Propagate bounded decoder reliability, or conservatively mark the affected
decoded byte region, before supplying erasures to RS. Trellis traceback and
puncturing spread uncertainty: raw interference positions are not automatically
the corresponding RS byte erasure positions. Include that spread in burst tests.

LDPC is a block code, not a convolutional code; trellis-coded modulation is a
distinct mapping/coding approach. Evaluate a fixed-length LDPC or TCM alternative
later against the same RS/interruption tests. LDPC normally uses iterative
message passing; if “no iterative search” means no iterative decoder at all,
keep it excluded. Never conceal that computational choice. Do not stack every
candidate code at once and assume more parity increases throughput.

For D outer groups, a simple byte interleaver has `2*D` rows of 128 RS symbols.
A contiguous B-byte erasure burst in its serialized RS stream contributes at
most `ceil(B/(2*D))` erasures per row. Correction requires `2e+v <= 128-k` in
each row. This bound is in the de-inner-coded stream; convolutional error
propagation and reacquisition extend real interference, so test the complete
pipeline. Depth is a local bounded profile constant, never derived from file
size. More depth improves burst coverage while adding memory and latency.

Dense APSK mappings can use documented constellation work as a reference, such
as [ETSI EN 302 307-2](https://www.etsi.org/deliver/etsi_en/302300_302399/30230702/01.02.01_20/en_30230702v010201a.pdf).
Adopting an APSK geometry does not adopt DVB packet framing or its FEC chain,
and its published performance cannot be claimed for this different modem.

## 8. Four channel profiles and realistic rate limits

Use `B ≈ Rs*(1+alpha)` for the ideal occupied passband of a single-carrier RRC
waveform, then leave margin for finite filters and real hardware. The following
are candidate local profiles with alpha=0.20, not calibrated defaults:

| Profile | Candidate occupied audio band | Symbol rate | Modulation ladder | Gross payload-symbol ceiling |
| --- | --- | ---: | --- | ---: |
| Audio wire | 0.3–18.3 kHz at 44.1/48 kHz | 15 ksym/s | QPSK, 16/64/256 APSK | 120 kbit/s at 256 |
| IC-7100 SSB | 0.3–2.7 kHz conservative; 0.2–2.8 kHz wider after measurement | 2 or 2.167 ksym/s | 16/64/256 APSK if measured EVM permits | 16 or 17.33 kbit/s at 256 |
| IC-7100 FM | Initially 0.3–2.7 kHz on measured audio path | 2 ksym/s | QPSK, 16/64 APSK; 256 only with evidence | 4/8/12 kbit/s; 16 at 256 |
| Speakers/microphone | Initially 0.5–8.5 kHz, narrowed if needed | 6.667 ksym/s | QPSK, 16/64 APSK | 13.33/26.67/40 kbit/s |

The wire ceiling respects the existing differing-rate resampler's 18.522 kHz
passband at 44.1 kHz. Wider hardware rates/passbands need a separately validated
fast converter/profile. Mono 44.1 kHz with 256-APSK cannot provide several
hundred kbit/s of file goodput. Higher sample rates, higher constellations or
independent physical channels are later extensions, not a free software setting.

Icom documents SSB receive filters adjustable through 3.6 kHz; SSB-D defaults
can be narrow. FM IF choices are 15, 10 and 7 kHz. These are **not FM audio
bandwidths**. Its DATA2 connection distinguishes ordinary audio and 9600-bps
operation; characterize the chosen connector and filtering before offering a
wider FM profile. Use the manual's IF-filter, transmit-filter and connector
sections when recording hardware settings. The manufacturer's 2.4 kHz SSB
selectivity specification also is not a guaranteed flat 2.4 kHz passband.
[Icom full manual](https://www.icomjapan.com/support/manual/2288/),
[Icom specifications](https://www.icomjapan.com/lineup/products/IC-7100USA/).

Treat the FCC's 2.8 kHz limit for the applicable HF data bands as the requested
SSB design ceiling, not a universal VHF rule or an RRC rolloff definition.
Measure the emitted spectrum including filter tails and nonlinear regrowth;
the conservative 2.4 kHz candidate leaves margin. See
[FCC 23-93](https://docs.fcc.gov/public/attachments/FCC-23-93A1.pdf).
Part 97 generally prohibits messages encrypted to obscure meaning, subject to
its exceptions. An encrypted radio profile therefore does not imply permission
to use it on amateur frequencies; wire operation is a separate case. This
does not require changing regular encryption or adding plaintext fallback.
[47 CFR 97.113](https://www.govinfo.gov/content/pkg/CFR-2025-title47-vol5/pdf/CFR-2025-title47-vol5-sec97-113.pdf).

For FM, measure baseband response, deviation and post-demodulation noise, then
fit the modulation spectrum to both transmitter and receiver. APSK here is an
audio subcarrier through an FM link; a radio's RF limiter does not automatically
destroy its recovered audio amplitude. Audio clipping, pre/de-emphasis, filtering
and RF interference can still destroy the required constellation accuracy. Do
not infer usable APSK order from transmitter power or a nominal SNR comparison
with SSB. On SSB, determine linear drive/headroom with compression disabled and
actual EVM/spectrum measurements rather than maximum power alone.

Report gross modulation rate, coded rate and measured source goodput separately:

```text
net ≈ Rs * log2(M) * inner_rate * ciphertext_bytes/256 * source_efficiency
      * sync_and_pilot_efficiency * fixed_fill_efficiency
```

For illustration only, 120 kbit/s gross, rate 7/8, 192 cipher bytes per outer
group, 8/9 source efficiency and 90% combined sync/pilot/fill efficiency yield
about 63 kbit/s (7.9 kB/s). A 5 MiB file takes about 11.1 minutes plus startup
and six-second end observation. The same assumptions at 16 kbit/s gross yield
8.4 kbit/s and about 83 minutes. Compressed mode could improve compressible
sources; it cannot promise gains on arbitrary files. Synchronization transmitted
in robust QPSK symbols has a different time cost than the same bit count in
256-APSK: final estimates must count actual symbols, not just byte percentages.

Pick a matching fixed profile before transmission. A receiver can display a
recommendation based on EVM; it cannot silently change the sender's modulation.
Automatic negotiated rate changes would need additional protocol design and are
outside this packetless first implementation.

## 9. GUI mode switch and streaming presentation

Place **Fast** immediately beside **DATA PUMP** in the upper-left shared header.
It selects a complete fast desktop: file source, four channel profiles,
modulation/coding, key selection, level/headroom, acquisition/lock, constellation
and EVM, net rate, received byte count, correction/erasure counts and integrity.
RX total size/ETA stays unknown because no size is transmitted; TX may use its
locally known source size. A pending file stays distinct from a verified,
physically completed file.

The shared Application becomes a small mode host; the regular Controller and
its behavior remain intact behind it. Fast owns separate settings, draft/file
handles, records, workers and bitmap sources. Its UI has no weak-pattern search,
LPI estimate, weak link planner or regular simulation/recovery controls. Fast
SNR simulation is available only in regression tooling, not as a production UI
simulation selector.

Whole desktops need immutable surface generations. Both FLTK and Rev reconcile
the same shared screen description. Reject stale controls, popup actions,
bitmap results, worker events and file-dialog completions from another mode or
generation. Service IDs alone are insufficient because separate controllers can
both start numbering at one. Preserve the existing adapter boundary: native
widgets do not learn modem/profile business logic.

The view toggle can switch immediately while the current engine retains its
active reception/transmission and exclusive audio ownership. The newly visible
mode shows that ownership and keeps conflicting start controls unavailable until
the active transfer ends or is explicitly cancelled. A view switch never calls
regular `configure()` or destroys a pending reception. Continue polling an active
regular controller while hidden, preserving `0`, `00`, `001` in the same pending
row on successive progress polls. Restore the exact draft/settings/history on
return. Preserve regular key cooldown and in-progress recovery state.

Fast progress may aggregate counters for display, but accepted data and erasure
events must be available at the next poll; plots and disk workers cannot block
progress. Keep diagnostics and previews bounded. Authentication of a group is
not whole-file completion or copy/save eligibility. GUI navigation and device
ownership are distinct state machines, tested together.

## 10. Independent SNR regression simulator

Implement a deterministic sampled fast TX/channel/RX harness with SNR as the
only channel-quality input. Sweep **10, 15, ..., 120 dB: 23 values**. Define SNR
as average received signal power divided by noise power integrated across the
profile's declared receiver bandwidth. Do not inherit regular dB-Hz targets,
single-symbol probability, oscillator menus, link budgets or three-second replay.

For real sampled AWGN, if the declared one-sided noise bandwidth is B and the
sample rate is Fs, set full-Nyquist noise variance to
`Ps * 10^(-SNR_dB/10) * Fs/(2*B)` before the receive filter. Validate actual
equivalent noise bandwidth numerically; record it and report Es/N0 separately.
Do not confuse real passband, complex baseband and post-filter power conventions.

Each profile uses fixed hardware/filter assumptions. Use independent seeds and
RX state, arbitrary startup phase/sample position and chunk boundaries; never
hand TX symbol decisions, clock phase or plaintext to RX. The 120 dB point is
a floating-point/numerical stress case, not a claim about 16-bit audio hardware.
Include the actual S16 quantization path in separate hardware-realism fixtures.

The SNR sweep alone cannot establish interruption or clock-tracking behavior.
Add separate deterministic regression fixtures, not another user-facing channel
simulator: 1/10/50/100/250/500 ms additive sound effects and erasures, 50/60 Hz
hum with harmonics, narrowband interference, clipping, filter notches, fixed
clock mismatch/drift, phase slips, room impulse responses and callback overruns.
Vary each burst across marker, trellis, codeword, crypto and supercycle boundaries.
Longer-than-six-second fades must yield an incomplete file when data is lost.

Record BER before/after inner decoding, RS errors/erasures, authenticated-group
success, exact whole-file success, goodput, lock/relock time, EVM, spectrum,
CPU deadline misses, peak memory and spool high-water marks. Require independent
wire vectors as well as loopbacks. A 23-point sweep does not guarantee success
at every constellation: derive a supported profile ladder from measured file
failure rates and uncertainty. Zero observed errors is a finite test result,
not a zero-BER claim. Use small fixtures in quick CI and seeded multi-megabyte
stress suites separately.

## 11. Implementation stages and acceptance gates

| Stage | Concrete work | Gate before proceeding |
| --- | --- | --- |
| 1: isolation | Add fast types/targets, file source/sink and dependency guards; preserve old entry points. | Existing contract suites pass unchanged; fast symbols cannot reach regular signal code. |
| 2: wire/crypto | Freeze 256-byte physical cadence, supercycle maps, source endpoint, IV/HMAC/KDF and two-RS layout. | Independent byte/bit/crypto vectors, wrong-key/tamper/reorder/truncation tests; fixed allocations for hostile input. |
| 3: sampled baseline | QPSK/16-APSK, trained tracking, RRC, soft convolutional decode, RS and interleaving. | Clean/noisy sampled interoperability, physical absence/EOF distinction, exact streaming files, bounded memory. |
| 4: dense profiles | 64/256-APSK, puncturing, equalizer, optimized kernels and four measured filter profiles. | 23-point SNR sweeps, burst fixtures, rate accounting, spectral/EVM and callback-deadline evidence. |
| 5: shared GUI | Mode host, Fast desktop, audio ownership, generation-safe services and native reconciliation. | Shared and both native GUI conformance tests; active regular reception remains intact through mode switches. |
| 6: physical qualification | Wire, SSB, FM and speaker/microphone captures; long files and interruptions. | Exact file comparisons, observed spectra, measured goodput/resources and documented failure limits per connector/profile. |

Keep all independent wire vectors, physical-end and pending-row regressions
listed in [development.md](development.md). In particular, preserve dictionary
codes (`e = 001`), the inclusive 16/17-byte boundary, explicit partial bits,
regular 192+1024-bit geometry, sampled four-hour symbols, post-end-only source
interpretation, 4,096-bit diagnostic retention and original crypto vectors.
Do not weaken tests to make fast integration pass.

Add fast tests for all-zero/trailing-zero files; empty/exact-boundary sources;
missing first/final/interior groups; wrong profile; magic-like payloads; inserted
or deleted symbols; lost markers; ambiguous interleave position; counter limits;
invalid source cells; corrupted IVs/tags; disk full; endless carrier; cancellation;
and bounded memory independent of file length. Fuzz each fixed stage with
sanitizers and local quotas. Verify that no received field sets allocation size,
wire geometry, unbounded work, destination offset or physical completion.
Bounded data-dependent FEC and source decoding remain permitted.

Extend shared GUI regressions for fast-to-regular round trips, exact raw drafts,
key/settings isolation, pending-row identity, active TX/RX/recovery, stale native
callbacks and outstanding file dialogs. Extend dependency-negative fixtures
and run both FLTK and Rev conformance checks from
[GUI architecture](gui-architecture.md#verification-and-maintenance-guardrails).

## 12. Evidence and remaining decisions

This review read the development contract and traced the current source paths;
it did not execute a new DSP implementation or measure hardware. No runtime
files or existing tests are changed by this plan. Validate documentation links,
layout arithmetic and whitespace now; record actual implementation test runs in
[validation.md](validation.md) when those stages exist.

Before freezing fast wire v1, settle the exact sync/training sequence and false
lock budget; convolutional polynomials/puncturing/termination; per-profile
interleave depth and supercycle fill; authenticated salt bootstrap; fast key
derivation interface; and connector-specific filter measurements. These are
bounded design decisions inside the proposed architecture, not reasons to
modify regular mode or bring back packet lengths.
