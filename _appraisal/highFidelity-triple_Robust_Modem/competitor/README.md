# Native upstream modem comparisons

This optional adapter executes installed **FLDigi 4.2.06** for BPSK31 and
Olivia 8/250 and checksum-pinned **WSJT-X 3.0.2** executables for FT8 and WSPR.
It does not substitute the repository's legacy codecs for the named external
receivers. All modulation and decoding execute in those upstream native
programs. The C++ bridge applies the same `appraisal::Channel` as Robust;
Python controls processes and analyzes their returned text and receipts.

The common application text is `HELLO` for PSK31, Olivia and FT8. WSPR sends
the separately identified structured `K2HFA FN31 30` service. WSPR is not a
general text codec. FT8 free text has its own alphabet and 13-character bound.
FT8 and WSPR already exploit phase/coherent processing; neither is a QRSS
amplitude-only reference. One selected message and seed are failure-finding
observations, not a bitrate, error-rate or sensitivity certification.

## Reproduce

On the recorded Debian 13 amd64 environment, install FLDigi normally or use
the already installed `/usr/bin/fldigi`. The optional package helper downloads
13.2 MB total, verifies each archive's SHA256, and extracts WSJT-X, its data,
and Xvfb under ignored `.build/competitors/`. It makes no system installation.
Compatible system runtime libraries are required; report a missing dependency
rather than silently changing a decoder.

```sh
python3 -B _appraisal/highFidelity-triple_Robust_Modem/competitor/bootstrap.py
./_appraisal/highFidelity-triple_Robust_Modem/build.sh test
python3 -B _appraisal/highFidelity-triple_Robust_Modem/competitor/run.py \
  --setup-only \
  --output-dir _appraisal/highFidelity-triple_Robust_Modem/local-evidence/comparator-setup
python3 -B _appraisal/highFidelity-triple_Robust_Modem/competitor/run.py \
  --output-dir _appraisal/highFidelity-triple_Robust_Modem/local-evidence/comparator-campaign
```

Output directories must be new. Private Xvfb and loopback XMLRPC sockets are
required for FLDigi. The PulseAudio interposer replaces only audio transport;
the application retains its own encoder, decoder and state. Every native
8-kHz frame is delivered once and in order. The declared 8× wall-clock pacing
does not resample the signal or alter its physical sample clock. These GUI
runs are **not receiver throughput measurements**. The 25 physical seconds
of post-TX audio allow the Olivia integration/decoder latency to finish; in
channel trials that interval contains the same simulated receiver noise.
The finite input boundary then blocks the audio read. Its atomic receipt
reports source frames, requested setup-only zero padding, any final partial
audio-block alignment zeros, and their exact delivered sum. No samples arrive
after that completion receipt while XMLRPC reads the decoded text.

The default manifest has 16 episodes and a 900-second aggregate start gate:
four strong AWGN controls, four weaker quiet-HF cases, four disturbed-HF cases,
and four noise-only controls. Each native bridge and decoder command also has
a 90-second subprocess cap. Failed payloads are retained; a completed campaign
means observations were collected. The fast `--self-test` on the bridge
independently checks real/analytic tone power, noise variance, and a one-sided
Fourier-bin PSD estimate. Ordinary tests do not require the downloaded tools.
`--inspect-f32 PATH` independently inspects a clean 8-kHz/1500-Hz FLDigi
capture using native direct-DFT Hann periodograms. It reports the declared
analysis band, block count, RMS frequency offset and out-of-band fractions;
the mode-name/RPC label alone does not establish transmitted bandwidth.

## Reference planes and comparison limits

The C++ bridge reads the clean upstream TX waveform and normalizes its mean
power **once**, before propagation, over its reported first-to-last nonzero
support. This includes the codec's preamble and postamble. Reported TX energy
therefore charges synchronization, coding and other waveform overhead. There
is no channel-realization normalization or receiver-conditioned transmitter
feedback. The source file, output capture, native bridge and external decoder
hashes accompany each receipt.

For real 8/12-kHz audio, the bridge uses the canonical `AudioInput` analytic
projection and `real_pcm` output. It renders the wanted signal through the
common channel with white noise disabled, then adds native real Gaussian
receiver noise of variance `Fs*N0/2`. Thus `C/N0` uses a **one-sided real
noise density**, and `SNR(2500 Hz) = C/N0 - 33.9794 dB`. Merely converting
full-band complex white noise back to real PCM would fold two images and
introduce a factor-of-two mistake. WSPR's upstream `.c2` container is instead
proper complex 375-Hz I/Q, with `E|n|² = Fs*N0` and the upstream `I,-Q` storage
convention. Its fixed 120-second capture must contain the physical channel
tail or the bridge rejects the episode.

The fixed output WAV gain depends only on the declared noise variance, not
the realized peak. Exceeding its headroom is a reported error, never clipping
or adaptive normalization. The bridge currently rejects colored/impulsive
noise, blockers, nonlinear frontends, the VLF circuit and lunar profiles;
those would require separate audio-reference validation. A source's spectral
guard is explicit (300 Hz for the selected real-audio modes, 10 Hz for WSPR).

The receivers have different synchronization and completion contracts:

- FLDigi knows the selected mode and 1500-Hz center. AFC, squelch and RSID are
  disabled. It emits an unframed text stream; all extra characters are retained
  and counted. Finding `HELLO` within that stream is not an exact clean-message
  or physical-completion result. Noise-only character output is reported.
  The pinned 4.2.06 RPC bandwidth getters are misleading for this preset:
  the generic getter supports Hell/CW and returns zero for Olivia; the Olivia
  getter adds one to the GUI index before mapping it, reporting 500 for the
  selected 250-Hz entry. Literal readbacks remain in receipts. The exact
  [4.2.06 source archive](https://deb.debian.org/debian/pool/main/f/fldigi/fldigi_4.2.06.orig.tar.gz)
  (`4360de225d6f8cfeb9bb6a5be6ac492e52702c8f704f87609c452d3bcb1b6c08`)
  maps the named preset to eight tones and 250 Hz in both transmitter and
  receiver. Independent native periodograms of the retained TX capture put
  only 0.026% of the analyzed positive-band power outside center ±140 Hz,
  consistent with that actual bandwidth rather than the RPC's 500-Hz value.
- FT8 receives the UTC slot and its ordinary nominal-frequency settings.
  Each fresh `jt9` process has blank own/DX calls (`-c b -x b`), avoiding the
  executable's example calls as message hints. Generic FT8 a-priori processing
  remains enabled upstream and is disclosed, not misrepresented as disabled.
- WSPR uses a fresh data directory with `-H`, which the pinned binary documents
  as disabling hash-table use/update, and `-d` deeper search. The default
  coherent block demodulation remains enabled; `-B` would disable it. All
  decoded records, including duplicates or wrong-frequency records, remain
  visible.
- Robust uses its configured public calendar/address, full search bounds and
  two scored quiet-slot completion rule. Its complete-delivery results cannot
  be equated with external text-substring recovery.

Equal `C/N0` establishes equal normalized pre-fade power/noise density, not
equal energy or delivered information. Ordinary operating points must show
actual TX energy, duration, message service, quiet/decoder latency and compute.
For a predeclared equal-energy comparison, use the measured **clean TX**
energies to adjust `C/N0` by `10 log10(E_reference/E_mode)`; do not adjust it
from a faded output or an observed success. Repeated transmissions require
their actual supported combination behavior and total energy to be charged.
The default manifest alone does not establish orders-of-magnitude superiority.

`ox-campaign.json` adds eight matched static/evolving O/X cases. Use
`--reuse-sources PRIOR_RUN_DIRECTORY` to preserve the exact clean transmitter
captures and verify their original hashes. `replay.py --capture receipt.json`
reprocesses selected original receiver samples without rerendering the channel;
such repeats are not independent success trials. Its `--tier strong` selects
the optional FT8 multithread decoder at sensitivity/cycles 3 and 12 workers,
or WSPR OSD depth 5, while preserving blank FT8 calls and WSPR `-H`. The
ordinary campaign uses each executable's documented deep standard tier;
it is not labeled the strongest available configuration. Failed/time-limited
strong-tier observations must remain distinct from a decode failure.

## Provenance and lifecycle

The initial [16-episode run](../local-evidence/hf-competitor-first/run.json)
completed in 61.42 seconds and the [eight O/X comparisons](../local-evidence/hf-competitor-ox/run.json)
in 29.54 seconds. They exercised actual upstream codecs, with the following
conditional payload observations:

| Selected service | AWGN 40 dB-Hz | Quiet HF 8 dB-Hz | Disturbed HF 20 dB-Hz | Static O/X 20 dB-Hz | Evolving O/X 20 dB-Hz |
| --- | --- | --- | --- | --- | --- |
| BPSK31, `HELLO` | Found, plus extra stream text | Missing | Missing | Missing | Missing |
| Olivia 8/250, `HELLO` | Found, plus extra stream text | Missing | Missing | Found, plus extra stream text | Missing |
| FT8, `HELLO` | Decoded | Missing | Missing | Decoded | Decoded |
| WSPR, structured message | Decoded | Decoded | Missing | Decoded | Decoded |

At unit mean clean TX power, the corresponding energies were **4.10875,
8.236625, 12.6529167, and 110.592 seconds**. This large airtime difference is
why the table cannot establish a power or efficiency ranking. Both external
block decoders emitted zero decoded records on their single noise-only
observation; the unsquelched stream decoders emitted unrelated characters.

The initial FLDigi boundary continued supplying uncounted noiseless zero
frames after the saved capture while the UI was polled. Its original
receipts remain historical evidence with that limitation. The repaired finite
boundary stops that sample clock and supports matched re-evaluation of the
preserved captures; those checks must be identified separately rather than
rewriting the earlier receipts. The initial FT8/WSPR file-based observations
do not use that Pulse boundary. Re-evaluating all 24 original outputs with
the stricter exact decoded-message parser changed none of their outcomes.

The [six matched finite-boundary replays](../local-evidence/hf-competitor-eof-final/run.json)
took 34.93 seconds and preserved all selected payload outcomes: both stream
codecs recovered the strong AWGN text, missed the evolving O/X text, and
emitted unrelated characters on noise. Their trailing character counts changed,
so the original text streams were not silently relabeled. The exact final
alignment pads were 192 frames for PSK31 and 448 for Olivia (0.024 and 0.056
seconds at 8 kHz); all six receipts report zero frames after completion.
The [five stronger-tier captured-sample checks](../local-evidence/hf-competitor-strong-tier/run.json)
took 6.78 seconds: optional maximum-sensitivity multithread FT8 still missed
the selected weak quiet-HF and disturbed-HF captures, and WSPR OSD depth 5
still missed its selected disturbed capture. Both noise controls emitted no
decoded records. These are matched algorithm comparisons on existing captures,
not five new independent channel trials or proof of every optional setting.

The current bridge's [native spectrum receipt](../local-evidence/hf-competitor-olivia-spectrum.json)
binds the Olivia bandwidth check to its embedded source identity. Real-audio
AWGN controls also expose the finite canonical analytic projection's small
image residual: measured real-signal energy was approximately 1.1–1.3% above
the normalized analytic energy for the three selected audio waveforms (under
0.06 dB). Both quantities remain in the receipts; the WSPR complex-container
path does not use that projection. This is a numerical conversion limitation,
not a claimed field sensitivity difference.

The first two bridge binaries predated embedded source identities. Their
exact binary hashes and explicit, **inferred same-build** bindings to companion
native receipts are retained in [the provenance sidecar](../local-evidence/hf-competitor-provenance.json).
Current bridge receipts embed appraisal/native source and build-option hashes.

The [six FT8 cases repeated at the matched RF band midpoint](../local-evidence/hf-competitor-ft8-centered/run.json)
preserved all six payload outcomes in 13.86 seconds. Upstream `ft8sim 1500`
specifies the lowest tone; its eight-tone midpoint is `1500 + 3.5*6.25 =
1521.875 Hz`. New bridge calls use that midpoint for both downconversion and
reconstruction, preserving the original decoder audio while aligning the
physical RF reference. The first two campaigns instead placed FT8's midpoint
21.875 Hz above that reference and remain labeled accordingly. Three selected
[centered strong-tier replays](../local-evidence/hf-competitor-ft8-centered-strong/run.json)
also preserved the weak/disturbed misses and zero noise-only decodes.

The positive [Olivia static-O/X control was re-evaluated with finite EOF](../local-evidence/hf-competitor-eof-static/run.json)
and still recovered `HELLO`. The [additional O/X controls](../local-evidence/hf-competitor-ox-controls/run.json)
show why its cancellation miss must not be overinterpreted: static Olivia
at the predeclared 17 dB-Hz expected-power control also missed the payload,
so the cancellation-at-20 result does not isolate a penalty beyond the
approximately 3 dB mean single-port loss. BPSK31 recovered the payload under
both static and evolving O/X at 30 dB-Hz; its 20 dB-Hz failures are not an
unconditional model-error floor.

Finally, the predeclared [equal-transmitter-energy manifest](equal-energy.json)
and [three executed comparisons](../local-evidence/hf-competitor-equal-energy/run.json)
use the same `HELLO`, quiet-HF seed 12802, and total normalized `E/N0 =
9297.920565925`. Their powers were calculated from clean transmitter energy,
before those trials, without compensating realized fades:

| Receiver fixture | C/N0 (dB-Hz) | Clean TX energy at unit power (s) | Recorded outcome |
| --- | ---: | ---: | --- |
| Robust reference, FEC off | 8 | 1473.621101 | Physical completion; four wanted bits scored quiet; message rejected |
| BPSK31 | 33.546761 | 4.108750 | `HELLO` found, with unrelated stream characters |
| Olivia 8/250 | 30.526365 | 8.236625 | `HELLO` found, with unrelated stream characters |
| FT8 | 28.661952 | 12.652917 | Exact `HELLO` record decoded |

The Robust [complete common-text receipt](../local-evidence/hf-common-text/001-hf-quiet-hello.stdout.json)
contains 65 wire bits, 67 scored slots and a roughly 3984-second physical
calendar; the external captures are about 29.50, 33.47 and 15 seconds,
respectively, including the specified finite audio tails. These are different
latency, coding, search-prior and completion services. In particular, this
Robust fixture explicitly disables outer FEC. This selected comparison
demonstrates a current configuration's lost practical benefit; it is not an
optimal-Robust or all-conditions ranking, and it cannot establish a population
probability from one shared seed. WSPR's different structured service is
deliberately absent from this common-text table.

The subsequent [native coded HELLO follow-up](../hf-validation.md#physical-absence-capture-loss-and-coding)
also exercises the existing Robust RS20/RS60 options. At quiet HF +8 dB-Hz,
RS20 rejects seven erasures while RS60 recovers HELLO despite ten erased bits.
RS60 uses 2.830761 times the Off fixture's full transmitted energy, so its
ordinary operating-point success is separate from the common-energy table.
The [frozen RS60 energy-matched follow-up](../campaigns/hf-fec-equal-energy.json)
uses 3.480967777893 dB-Hz and the same seed, without compensating realized fades.
That [actual row](../local-evidence/hf-fec-equal-energy/report.md) fails: 100 active
bits are quiet, 25 premature physical completions fragment the transmission,
and all 26 resulting messages are rejected. Thus neither tested Off nor RS60
delivers at this common-energy operating point. The successful +8 dB-Hz RS60
row remains a separate higher-energy result, and neither outcome establishes
an optimized-codec ranking or a day-to-day success probability.

The [WSJT-X source](https://github.com/WSJTX/wsjtx/tree/v3.0.2),
[FT8 simulator](https://github.com/WSJTX/wsjtx/blob/v3.0.2/lib/ft8/ft8sim.f90),
[jt9 interface](https://github.com/WSJTX/wsjtx/blob/v3.0.2/lib/jt9.f90),
and [FLDigi documentation](https://www.w1hkj.com/FldigiHelp/) define the external
services. The pinned package receipts preserve Debian's exact binary hashes.
WSJT-X package copyright identifies GPL-3; installed FLDigi identifies GPL-3+.
Package copyright/license files remain inside the disposable extracted cache.
No external codec implementation is copied into maintained appraisal source.

Keep generated source waveforms and compact raw receipts while they support
current comparisons or unresolved setup failures. Remove obsolete duplicate
captures after preserving their diagnosis and provenance. The downloaded
package/build cache serves reproducible active testing and can be removed
when that testing ends; `bootstrap.py` reconstructs the pinned dependencies.
