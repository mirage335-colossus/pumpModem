# Fast expected-SNR and symbol-rate controls

The **Expected SNR** dropdown selects constellation, LDPC, interleave depth,
waveform bandwidth and symbol timing together. Auto resolves this local profile
deterministically from Expected SNR and the declared receiver assumptions.
**Symbol rate** allows an explicit timing override. No measurement, negotiation
or change during a transmission is performed. Configure both peers with matching
resolved waveform settings and declarations. Manual rate, QAM, LDPC or depth changes make the SNR
selector **Manual**. Switching channel profiles restores the selected channel's
default SNR and its corresponding waveform settings. Speakers/mic · short is the
initial GUI channel; output routing selections remain retained.

Expected SNR remains a transmitter/receiver configuration assumption, not a
measurement of a live room or a feedback control. The GUI's separate measured
signal-SNR value is published during or after an eligible loopback test when its pre-test noise
baseline is comparable to the decoded payload observation. It reports the
reference bandwidth and observation age; it is omitted when the baseline,
occupied-band resolution, quietness or clipping checks do not support a
comparison. Ordinary Live and software Simulation do not produce a calibrated
physical SNR value.

## Reference bandwidth

| Channel | Default assumption | Menu range | Original reference bandwidth |
| --- | ---: | ---: | ---: |
| Audio cable | 36 dB | 65 to 25 dB | 18,000 Hz |
| Speakers/microphone | 3 dB | 13 to −27 dB | 17,500 Hz |
| Speakers/mic · short | −6 dB | 13 to −27 dB | 17,500 Hz |
| IC-7100 SSB | 20 dB | 20 to −20 dB | 2,400 Hz |
| IC-7100 FM | 20 dB | 20 to −20 dB | 2,400 Hz |

Menus include the endpoints and notable 3/10 dB values. For example, acoustic
offers 13, 10, 6, 3, 0, −3, −6, −10, −13, −16, −20, −23, −26 and −27 dB.
The default assumptions are 36 dB for an audio cable, 3 dB for
speakers/microphone and −6 dB for speakers/mic · short. The earlier 65/13 dB
settings remain available; these values are operating assumptions, not measurements of ambient room noise. The original information-equivalent metric
must not be mistaken for independently measured band-integrated SNR.

The user-authorized weak presets may narrow bandwidth. SNR remains referenced
to **total received signal power divided by noise power in the original band**.
With flat noise density and unchanged total signal power, selecting a band
`B_selected` from `B_reference` gives

```
SNR_selected_dB = SNR_reference_dB + 10 log10(B_reference / B_selected)
```

Negative menu values therefore do not imply negative SNR at a narrow QAM
decision point. A real speaker or radio may have different gain and noise in
the selected band; movement, resonances, fading, distortion and interferers
can invalidate this white-noise model. The UI displays both bandwidths.

## Selection and limits

Every capacity single-carrier preset transmits 2,048 public training symbols
and recurring sixteen-symbol pilot words. Resilience is part of the waveform
at every SNR selection; it is not a separate default or optional preset family.
The automatic profile uses declared receiver bounds even when the current mode
does not inject a modeled channel. For Speakers / mic · short and IC-7100 SSB,
the initial bounds declare HF-worst spread and 65% selective AM; other channel
profiles start with clean spread/AM declarations. Per-channel drafts persist.
Channel bandwidth, QAM, coding and pilot spacing still follow the selected
profile. IC-7100 SSB at 20 dB uses QPSK with 7,200 information bits per
16,200-bit LDPC frame and eight physical intervals. Lower SSB selections use
64,800-bit LDPC, so their different coding overhead can give higher estimated
bulk throughput near the top of the menu. See the scoped simulation and cable
results in the [radio profile study](fast-radio-capacity.md).

On eligible narrow profiles, the receiver can compare a delay/Doppler model
with coherent acquisition from the same received waveform. Compact acquisition
retains its independent marker verification; noncompact coherent acquisition
requires fresh pilot evidence before selecting that receiver. This choice is
local to reception: it does not negotiate a format or change the transmitter.
It does not mean every rate, bandwidth or constellation uses the same receiver
algorithm. The bounded modeled path covers QPSK/16-QAM at 48 kHz processing,
800–2,500 symbols/s, rolloff at least 0.10 and pilot spacing at most 64, provided
the translated fit band fits the audio band. Existing fixed SSB modes retain
their dedicated receiver paths.

Lower settings compare implemented square-QAM/LDPC
combinations using estimated 50 MB source throughput, including actual cycle,
marker, pilot and padding overhead. They do not optimize a received channel
response or promise globally optimal coding. Changing the declaration to a clean
spread/AM profile can restore the faster pre-cap Auto family, at the cost of
declaring less receiver coverage. Simulation and explicit loopback alone inject
the selected synthetic effects; ordinary Live remains clean. Mode changes do
not change the selected wire format. Higher-SNR wide OFDM and explicit Manual
waveforms retain their existing geometry.

The separate [short acoustic profile](fast-acoustic-short.md) compares complete
transfer time and modeled throughput for a 60-byte encoded fixture, including
keyed bootstrap, startup and physical absence. The historical 10.5- and
12.5-second targets remain family-selection heuristics, not latency bounds; the
current selected-band fade cap can produce substantially longer transfers.
Candidates include short DVB LDPC, small QC LDPC and compact convolutional
coding, with six-block OFDM or the shared 2,048-symbol single-carrier training.
Short single-carrier presets send sixteen pilots after every 32 payload
symbols. The current 17.5 kHz-reference fade cap is about 231.130 baud, or
277 Hz occupied bandwidth. At −6 dB it uses QPSK with a 1,944-bit rate-2/3
frame at depth one and provides 12 dB modeled selected-band margin. At −10 dB
it keeps 231 baud, selects rate 1/2 and provides about 8 dB margin. The −13 dB
preset uses about 183 baud and −16 dB about 92 baud; both use rate 1/2 because
post-cap selected-band SNR is below 9 dB. The canonical 60-byte keyed fixture
strengthens coding only when the physical interval count stays the same. The
short `fox` estimate at the −6 dB cap is about 32.67 seconds; the −10 dB
half-keyed attachment estimate is about 40 seconds. The rate cap reduces baud
by about 74.9% from the earlier 920-baud default; the lower code rate adds
further per-symbol overhead. The earlier coding-only 13.7% public-throughput
change therefore does not describe the total airtime cost. Use the current
source estimate.

The earlier 920-baud −6 dB geometry passed its original GUI cases but failed
the joint noise/HF-worst/65%-AM case. A 9 dB margin at 461 baud passed only the
original case and failed holdouts; an 8 dB margin at 580 baud failed, as did a
diagnostic 1/2-rate, depth-two 920-baud preset. Those prototypes were removed.
The current 231-baud stock gates pass three fixed joint −6 dB cases at rate 2/3
and three fixed joint −10 dB cases at rate 1/2. This does not establish joint HF-worst
performance below −10 dB. Calibrated flat/echo checks at −13/−16 and earlier-
geometry physical tests at measured strong signal levels remain separate
evidence; they do not qualify weak acoustic reception. The scoped API, GUI and
simulation contract checks pass 3/3. Current ordinary Live `fox` and default
public/keyed text and binary loopback GUI checks pass over the actual speakers
and microphone at measured strong signal levels. An earlier −10 dB-preset
physical alignment failure remains unexplained; those later passes do not
establish its cause or qualify actual weak acoustic sensitivity.
The [validation record](validation.md) gives exact cases and limits.

The compact acoustic-short delay/Doppler receiver path covers QPSK at 48 kHz
from 180 baud to below 800 baud, with up to ±100 Hz frequency offset. A separate
coherent acquisition extension covers targeted reference-AWGN at about 90 baud;
this does not lower the delay/Doppler floor or qualify physical HF-worst
reception. Higher-SNR wide OFDM and explicit manual settings keep their existing
geometry.
At slower baud rates the receiver resolves long-startup CFO phase aliases with
the public repeated 64-symbol sequence. This acquisition change preserves the
independent marker check and does not change the presets.
The local presets never use measured link SNR to adjust transmit power or
symbol rate; both peers must use matching settings. Use the current
transmission estimate for the selected waveform and actual source. Earlier
920-baud timings do not apply to the fade-capped presets.

Low-order decoder thresholds use the archived
[LDPC/QAM margin analysis](fast-acoustic-margin-targets.md); higher-order
thresholds use Gaussian capacity plus a 2.5 dB implementation/shaping allowance.
The sampled radio 64-QAM 3/4 result requires a larger allowance. A further
3 dB engineering allowance is used when selecting reduced-SNR modes. These
are modeled settings, not statistical error-rate or whole-file guarantees.
The nominal FM selection was based on sampled 18/20 dB results; those earlier
four-pilot-word results do not qualify its revised sixteen-pilot waveform.
SSB uses the scoped manual channel qualification described above.

A production-decoder check caught a failure that the capacity approximation
missed: 1,048,576-QAM with LDPC 1/2 failed all four screened frames at 36 dB,
even though its calculated bit information exceeded the code rate. Auto
therefore restricts rate 1/2 to QPSK and 16-QAM; denser half-rate combinations
remain available manually. This is one reason an information bound alone is
insufficient to select a practical mode. The replacement at 36 dB is
4096-QAM/LDPC 7/9, passing four frames with about 3.46% lower estimated
throughput. All corrected cable menu settings passed four production-code
AWGN frames each. The [104-frame screen and exact sources](https://github.com/mirage335-colossus/pumpModem/blob/5b343de5ac5a3e33ac404b796c9ade0ba90d9409/docs/validation-data/fast/wire-snr-screen-20260921/README.md)
include deliberate failed candidates and additional nearby points; these
decoder tests do not constitute full acoustic or cable waveform trials.

Synchronization imposes an additional floor. Ordinary narrow single-carrier
selection retains a conservative **13 dB selected-band target** inherited from
the earlier exact-marker receiver. The short acoustic acquisition model's **6 dB
target** remains a baseline assumption, while the current fade-capped preset
adds margin: about 12 dB selected-band SNR at −6 dB reference and 8 dB at −10 dB.
These are selection assumptions, not measured sensitivity limits for the
revised receiver. Independent acquisition evidence remains required even when
LDPC payload symbols could decode at lower SNR. OFDM acquisition has 256
independently checked marker signs with at most 16 disagreements; its selection
floor is **6 dB**. After
acquisition, maintenance at the established block ordinal permits at most
32 disagreements with the same residual-energy guard. Tracking fits still
exclude the current verification tones, and maintenance does not search a new
boundary. The conditional independent-fair-sign probabilities are approximately
`2^-172.84` per acquisition check and `2^-120.36` per maintenance check, not
receiver-wide guarantees. The [integrity analysis](fast-capacity-integrity.md)
distinguishes these from the unchanged accepted-byte integrity bound.

Acoustic presets first consider narrower OFDM. The current FFT and independent
marker design require at least 512 active bins and a 1 kHz configured band.
Below that supported geometry the selector uses a narrow single carrier near
1.8 kHz. Its output amplitude is normalized to preserve the nominal OFDM
average PCM power. Radio keeps its 1.5 kHz carrier. These restrictions cause a sizable
throughput drop at the OFDM-to-single-carrier transition. Acquisition and marker
geometry constrain this transition in addition to payload error correction.

Each OFDM candidate now halves its interleave depth until its known-channel
refresh interval fits the nominal 13-block, 9.984-second interval, or depth
one is reached. This happens before candidates are compared for throughput.
The nominal Auto 13 dB setting is unchanged. Compared with the previous
depth-eight settings, the affected menu points are:

| Expected SNR | New depth | Refresh interval | Estimated public 50 MB throughput loss |
| --- | ---: | ---: | ---: |
| 6 dB | 4 | 9.984 s | 3.88% |
| 3 dB | 2 | 9.984 s | 7.87% |
| 0 dB | 1 | 10.752 s | 13.84% |

These costs use the exact unencrypted 50,000,000-byte transmission estimate,
including framing and end silence; they are not measured goodput. Still
narrower depth-one presets can exceed ten seconds between refreshes. Manual
depth eight remains available. Restart both peers with the updated application
and reselect matching Auto settings: an older running application retains its
old profiles, and depth is part of the matching wire geometry. The associated
receiver change expands pilot-only timing tracking from ±6 to ±24 samples,
retaining a 0.15-sample search grid. The
[OFDM recovery diagnosis](fast-acoustic-ofdm-recovery.md) records the evidence
and remaining limitations.

Extremely weak presets reach roughly 1–2 symbols/s. Their 2,048-symbol
single-carrier preamble alone can last many minutes, and the 64,800-bit LDPC
frames make even tiny sources slow. Airtime estimates include this cost and
actual silence long enough to finish complete absence observations. EOF and
cancellation still do not complete reception. This is a consequence of the
existing bulk-file code, not hidden short-message latency.

Narrow carriers also require correspondingly precise frequency alignment.
The presets do not add a wide RF frequency search; a radio offset harmless to
the nominal 2.4 kHz waveform can prevent acquisition at one or two baud.

## Manual timing and CLI

Single-carrier choices report symbols/s, including the exact fractional
nominal rates and lower rates down to one symbol/s. OFDM choices report
symbols/s **per tone**, plus block duration; they select FFT size while
preserving the current cyclic prefix and passband. Invalid combinations are
omitted. Changing symbol rate retains QAM, coding rate and depth. An SSB
change outside the fixed short-frame geometry selects the 64,800-bit frame.
Matching explicit settings preserve the short frame, and the Fast details
show the resulting frame size and estimated rate.

With an automatic SNR selection, rate **Auto** uses that preset's timing.
With manual settings, it restores channel-default timing for the current
waveform and retains manual coding. Expected-SNR selection always restores
the entire preset. For reproducible command-line use:

```
pump fast-info --profile acoustic --expected-snr -10 --estimate-bytes 100000
pump fast-info --profile ssb --expected-snr 10 --estimate-bytes 50000000
pump fast-info --profile wire --expected-snr 40 --qam 4096
```

Explicit CLI values override the preset independently of argument order.
`--expected-snr` requires capacity format. `--format classic` preserves the
earlier waveforms. The JSON report gives `expected_snr_db`,
`snr_reference_bandwidth_hz`, `selected_band_snr_db_assumed`,
`reference_band_shannon_capacity_bps` and `snr_preset_unmodified`.

Non-SSB capacity single carriers now use the v4 profile context, including the
sixteen-symbol pilot-word revision and selected LDPC rate. Update both
single-carrier peers together and select matching settings. SSB v3, acoustic
OFDM and classic Fast retain their existing wire identities.

## Verification

Preset regressions cover the entire menu span, valid bandwidths, modeled
throughput below the reference Shannon limit, the SSB short-to-long frame
boundary, public training/pilot geometry, exact nominal settings and rate IDs,
overrides, and invalid inputs. Shared
GUI tests cover channel retention, active-transfer controls, mode isolation
and layout. Sampled radio tests and their limitations are recorded in the
[radio profile study](fast-radio-capacity.md). Low-rate DSP tests exercise
streamed audio without allocating a full long recording, including exact
interval recovery and absence completion. In the earlier low-rate validation,
clean 5/15-baud waveforms recovered exact interval bits. A radio −10 dB reference-SNR fixture
uses 16-QAM at 10.935 baud in a 12.0285 Hz band: a complete 64,800-bit LDPC
frame recovers exact source bytes in nine decoding iterations despite raw
bit errors. The same full-band white-noise density continues throughout the
real trailing silence, and EOF does not complete reception. That run processes
1,837.68 seconds of sampled 48 kHz audio with about 1.32 MB of RX workspace.
It tests one corrected frame, not a complete file or a success probability.
Those historical measurements do not qualify the revised receiver or changed
capacity waveform. Current checks and remaining live-test limits belong in
the [validation record](validation.md). The earlier
[integration logs](https://github.com/mirage335-colossus/pumpModem/blob/5b343de5ac5a3e33ac404b796c9ade0ba90d9409/docs/validation-data/fast/snr-presets-20260921/README.md)
record their source-specific Fast/GUI and native checks.

The reported premature completion at acoustic −10 dB was subsequently
reproduced on a physical speaker/microphone link and corrected. A failed pilot
no longer prevents later good pilots from restoring reception; the narrow
acoustic fallback's unintended 10 dB output-power increase was also removed.
See the [diagnosis and measured comparisons](fast-acoustic-low-snr-recovery.md).

Subsequent [physical cable level calibration](fast-cable-level-calibration.md)
retained amplitude 0.30 for cable and radio single-carrier profiles. Their
settled output power already stays constant as bandwidth narrows; no extra
bandwidth-dependent attenuation was justified by the connected cable tests.
The radio audio waveform was tested through that cable, not through an IC-7100.
