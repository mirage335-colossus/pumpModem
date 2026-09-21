# Fast expected-SNR and symbol-rate controls

The **Expected SNR** dropdown selects constellation, LDPC, interleave depth,
waveform bandwidth and symbol timing together. **Symbol rate** allows an
explicit timing override. These are local presets: no probe, SNR measurement,
negotiation, or automatic change during a transmission is performed. Configure
both peers identically. Manual rate, QAM, LDPC or depth changes make the SNR
selector **Manual**; each channel retains its selections when switching views.

## Reference bandwidth

| Channel | Nominal assumption | Menu endpoint | Original reference bandwidth |
| --- | ---: | ---: | ---: |
| Audio cable | 65 dB | 25 dB | 18,000 Hz |
| Speakers/microphone | 13 dB | −27 dB | 17,500 Hz |
| IC-7100 SSB | 20 dB | −20 dB | 2,400 Hz |
| IC-7100 FM | 20 dB | −20 dB | 2,400 Hz |

Menus include the endpoints and notable 3/10 dB values. For example, acoustic
offers 13, 10, 6, 3, 0, −3, −6, −10, −13, −16, −20, −23, −26 and −27 dB.
13 dB is an operating assumption motivated by earlier decoder analysis, not a
measurement of ambient room noise. The original information-equivalent metric
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

Nominal presets preserve the qualified cable/acoustic settings and the new
sampled radio default. Lower settings compare implemented square-QAM/LDPC
combinations using estimated 50 MB source throughput, including actual cycle,
marker, pilot and padding overhead. They do not optimize a received channel
response or promise globally optimal coding.

Low-order decoder thresholds use the archived
[LDPC/QAM margin analysis](fast-acoustic-margin-targets.md); higher-order
thresholds use Gaussian capacity plus a 2.5 dB implementation/shaping allowance.
The sampled radio 64-QAM 3/4 result requires a larger allowance. A further
3 dB engineering allowance is used when selecting reduced-SNR modes. These
are modeled settings, not statistical error-rate or whole-file guarantees.
The unchanged nominal radio setting is separately based on its sampled
18/20 dB results; it does not claim the same 3 dB allowance.

A production-decoder check caught a failure that the capacity approximation
missed: 1,048,576-QAM with LDPC 1/2 failed all four screened frames at 36 dB,
even though its calculated bit information exceeded the code rate. Auto
therefore restricts rate 1/2 to QPSK and 16-QAM; denser half-rate combinations
remain available manually. This is one reason an information bound alone is
insufficient to select a practical mode. The replacement at 36 dB is
4096-QAM/LDPC 7/9, passing four frames with about 3.46% lower estimated
throughput. All corrected cable menu settings passed four production-code
AWGN frames each. The [104-frame screen and exact sources](validation-data/fast/wire-snr-screen-20260921/README.md)
include deliberate failed candidates and additional nearby points; these
decoder tests do not constitute full acoustic or cable waveform trials.

Synchronization imposes an additional floor. The current single-carrier
capacity marker requires exact agreement on 128 signs. Its presets retain
at least about **13 dB in the selected band**, even when LDPC payload symbols
could decode at lower SNR. OFDM has 256 independently checked marker signs
with an error allowance; its selection floor is **6 dB**. Marker length and
false-match policy have not been weakened to make the menu look faster.

Acoustic presets first consider narrower OFDM. The current FFT and independent
marker design require at least 512 active bins and a 1 kHz configured band.
Below that supported geometry the selector uses a narrow single carrier near
1.8 kHz. Its output amplitude is normalized to preserve the nominal OFDM
average PCM power. Radio keeps its 1.5 kHz carrier. These restrictions cause a sizable
throughput drop at the OFDM-to-single-carrier transition. Lower-SNR acquisition,
longer coherent training, and a new multiblock marker design could reduce this
gap in a future wire revision; stronger LDPC alone would not fix acquisition.

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
omitted. Changing symbol rate does not silently change QAM, LDPC or depth.

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

## Verification

Preset regressions cover the entire menu span, valid bandwidths, bounded
throughput below the reference Shannon limit, monotonic menu throughput,
exact nominal settings and rate IDs, overrides, and invalid inputs. Shared
GUI tests cover channel retention, active-transfer controls, mode isolation
and layout. Sampled radio tests and their limitations are recorded in the
[radio profile study](fast-radio-capacity.md). New low-rate DSP tests exercise
streamed audio without allocating a full long recording. Clean 5/15-baud
waveforms recover exact interval bits. A radio −10 dB reference-SNR fixture
uses 16-QAM at 10.935 baud in a 12.0285 Hz band: a complete 64,800-bit LDPC
frame recovers exact source bytes in nine decoding iterations despite raw
bit errors. The same full-band white-noise density continues throughout the
real trailing silence, and EOF does not complete reception. That run processes
1,837.68 seconds of sampled 48 kHz audio with about 1.32 MB of RX workspace.
It tests one corrected frame, not a complete file or a success probability.
No new live audio or RF qualification is implied by adding these controls.
The final [integration logs](validation-data/fast/snr-presets-20260921/README.md)
record the completed Fast/GUI and native checks.

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
