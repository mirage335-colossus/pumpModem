# Three-carrier v2 measurements — 31 October 2026

The [v2 experiment](../../../triad-v2.md) implements the requested inner Rate
360/36, Added Symbols ×10 configurations. Both physical examples work with
coherent and differential reception in these trials. Differential reception
reduces search CPU substantially, but its raw products lose weak-signal
sensitivity. Amplitude-only acquisition has an explicit failure with slow AGC
and interleave disabled.

This package contains **35 runs, 291 captured trials and 849 receiver
evaluations**, approximately 272 KiB before this report. The three receivers
share captures. Seeds are deliberately reused across conditions for matched
comparisons; aggregate counts are not independent statistical trials.
[manifest.json](manifest.json) records exact commands, seeds, source/binary
hashes and compressed/uncompressed evidence hashes. Source is the uncommitted
working tree based on `f30804835b54c9268f650b7bd94fa1f7e397f2a5`.
These are software measurements, not hardware measurements or certification.

## Effective examples and checks

Both examples use 4 W **total** average power, −164 dBm/Hz noise density,
encryption, clock-addressed private patterns, 30 MHz RF and a nominal 1500 Hz
conversion IF. Margin 3 selects 1.5 ms / 0.3 Hz shared spread, with additional
slow/fast selective modulation of 5% each. Independent endpoint errors preserve
the ordinary relative oscillator bounds. Default audio means 0–70 ms causal
lag, rather than the built-in GUI commands' explicit zero-lag override.

| Quantity | Example 1 | Example 2 |
| --- | ---: | ---: |
| Inner Rate / Added factor | 360 Hz / 10 | 36 Hz / 10 |
| Fine symbols/s/carrier | 600 | 60 |
| Ideal combined bandwidth | 2250 Hz | 225 Hz |
| Path loss | 60 dB | 180 dB |
| Physical C/N₀ | 140.0206 dB-Hz | 20.0206 dB-Hz |
| SNR in that band | 106.4988 dB | −3.5012 dB |
| Planner target C/N₀ | 40 dB-Hz | 8 dB-Hz |
| Frame duration / dwells, one bit | 8 s / 20 | 14.8 s / 37 |
| Nominal payload coefficient allocation | 5.0333 s/bit | 14.3333 s/bit |
| Oscillator search: common CFO / audio clock | ±0.759 Hz / ±300 ppm | ±0.30900045 Hz / ±0.0003 ppm |
| Logical search hypotheses | 806,806 | 658,905 |

The CFO grids round outward. Thirteen integer epoch alternatives and the full
public arrival windows are searched. Fine-CFO reuse is exact for the pair and
amplitude statistics on the retained coarse-filtered tensor; it is not an
exact raw-waveform matched-filter search at every fine CFO. Fake FCC uses
55-ms quiet guards, a 0.5556-ms ramp and a 5-ms extra payload margin, plus full
pulse support. Its 200-channel schedule is metadata, without physical RF
retuning. The second capture's public allowances retain every declared arrival
alternative, including when the actual source arrives early.

- `CCACHE_DISABLE=1 ./build.sh test triad --jobs 2`: **4/4 passed**.
- `ASAN_OPTIONS=detect_leaks=0 CCACHE_DISABLE=1 ./build.sh sanitize triad --jobs 2`:
  **4/4 passed** with address/undefined-behavior instrumentation. Leak detection
  was disabled and remains unverified.
- Focused checks cover exact source bits, all three noiseless receivers,
  epoch-dependent whole-cell interleave, Added Symbols Off across fractional
  multiframe calendars, per-bit allocation, burst/padding behavior, FHSS pulse
  margins and retained echoes, independent RF/IF/audio clocks, incomplete
  captures, outward-rounded arrival boundaries, memory admission, and
  FP32/FP64/thread consistency. Gain checks include inputs scaled by 10¹⁰ and
  10⁻¹² without introducing an amplitude-floor sensitivity artifact.
- The worst fixed-position 10-ms window repeated every dwell removed **3.22%**
  of the Rate-36 bit's sampled distinguishing-template energy (about 0.14 dB).
  The discrete window conservatively rounds upward to a whole sample. This is
  an energy check, not a bound on all decoder penalties.

[Release output](focused-release.log), [instrumented output](focused-asan-ubsan.log)
and [unit details](unit-details.log) retain the outcomes. Independent review
and focused checks exposed and corrected epoch/permutation disagreement,
fractional-frame cell loss, allocation before memory admission, fourth-order
arithmetic limits, and premature complete-capture rejection before these runs.

## Physical links and marginal sensitivity

Accepted, completely correct captures with 1.6-second segments and unchanged
empirical gates (score 0.03, margin 0.005):

| Condition | Trials | Coherent | Differential | Amplitude only |
| --- | ---: | ---: | ---: | ---: |
| Example 1 physical link | 4 | 4/4 | 4/4 | 4/4 |
| Example 2 physical link, 20.0206 dB-Hz | 16 | 16/16 | 16/16 | 13/16 |
| Example 2, 16 dB-Hz | 16 | 16/16 | 9/16 | 0/16 |
| Example 2, 12 dB-Hz | 16 | 16/16 | 0/16 | 0/16 |
| Example 2, 8 dB-Hz | 16 | 5/16 | 0/16 | 0/16 |
| Example 2, 4 dB-Hz | 16 | 0/16 | 0/16 | 0/16 |

At the example-2 physical link, all three receivers selected the correct bit
in every trial; amplitude reception rejected three. At 8 dB-Hz, coherent
reception still selected correct candidate bits 16/16, but only five passed
its gates. Differential and amplitude reception made 12/16 and 8/16 raw bit
errors respectively. Candidate correctness without admission is not recovered
data. Threshold calibration could change coherent admission; lowering a gate
alone does not repair the differential candidate errors.

Longer coherence did not improve accepted recovery at 8 dB-Hz: 6.4-second
segments admitted 0/8 for every receiver, and full-pattern coherence admitted
0/4. Coherent candidate bits remained correct in those trials. The explicit
no-spread/no-selective-modulation baseline, retaining noise and oscillator/
timing errors, admitted 6/8 coherent and 0/8 differential/amplitude captures
at 8 dB-Hz. These results depend on the fixed gates and channel model; they are
not optimized receiver sensitivity limits.

For context, 16/16 has a conditional Wilson 95% interval of 80.6–100%, and
4/4 has 51.0–100%. Small successful samples do not qualify a reliable link.

## Erasures, AGC, burst and microwave cases

- At 16 dB-Hz, a 10-ms erasure starting at 200 ms in every dwell admitted
  8/8 coherent, 2/8 differential and 0/8 amplitude captures. Erasing every third
  dwell gave 8/8, 3/8 and 0/8. Coherent/differential candidate bits were correct
  in all these captures. The matching first eight baseline seeds admitted
  8/8 coherent and 4/8 differential captures without erasures.
- With slow AGC at 16 dB-Hz, interleave **on** admitted 8/8 coherent, 4/8
  differential and 0/8 amplitude captures. With interleave **off**, coherent
  admitted 8/8 and differential 3/8; amplitude accepted all eight but decoded
  **three incorrectly**. These wrong accepted data are distinct from rejection.
- Matching AGC/interleave-off null and wrong-key controls each falsely admitted
  **8/8 amplitude captures**. Coherent/differential admitted none. Interleave-on
  matched controls admitted 0/8 for all three receivers in each condition.
  This exposes a serious amplitude-only acquisition limitation; it does not
  establish protection against every AGC or every search configuration.
- Four bits sharing a 58-second frame, with 32 actual workers, admitted 2/2
  coherent/differential frames and 1/2 amplitude frames. The rejected amplitude
  frame contained three candidate-bit errors. Three bits with full-frame burst
  waits occupied 74 seconds and decoded 2/2 for all receivers.
- At **5.8 GHz**, with the same disciplined RF oscillator profile and a wider
  ±2.04000045-Hz CFO bound, all three receivers admitted 4/4 at the physical
  example-2 C/N₀. The deliberately retained HF spread is a synthetic stress,
  not a microwave propagation calibration. The explicit no-spread baseline
  also admitted 4/4 for all three; the matching noise-only case admitted 0/4.
  The search covered 3,821,649 logical hypotheses. This does not establish
  undisciplined microwave RF acquisition.
- The actual system-clock epoch run admitted coherent/differential reception;
  amplitude reception selected the correct bit but rejected it. The recorded
  epoch is `1793489257` and is replayable with `--epoch`.

## False admission and resources

Default interleaved example-2 controls admitted **0/32 noise-only**, **0/16
wrong-key** and **0/16 wrong-epoch** captures for every receiver. Example-1
matched controls admitted 0/2 in each category. Longer-coherence noise controls
admitted 0/16 with 6.4-second segments and 0/8 with full coherence. Zero observed
failures is not zero false-alarm probability: 0/32 still has a Wilson 95% upper
bound of 10.7%. Controls sharing seeds are not pooled as independent trials.
The interleave-off AGC failure above must not be hidden by these defaults.

AMD Ryzen 5 PRO 5650U, 12 logical processors, GCC 14.2, Release. Median receiver
wall/process CPU seconds, eight requested workers, automatic FP32:

| Physical example | Coherent wall / CPU | Differential wall / CPU | Amplitude wall / CPU |
| --- | ---: | ---: | ---: |
| 1, four trials | 12.774 / 91.322 | 1.504 / 9.056 | 2.235 / 13.840 |
| 2, sixteen trials | 0.663 / 4.592 | 0.144 / 0.890 | 0.219 / 1.415 |

These costs compare this experiment's receivers on matched captures, not the
ordinary Robust receiver. Example 1's roughly tenfold differential CPU saving
comes with the weak-signal tradeoff measured above.

Separate matched example-1 differential timing cohort, three trials each:

| Requested / actual workers | Arithmetic | Median wall / CPU seconds | Maximum DSP MiB |
| --- | --- | ---: | ---: |
| 1 / 1 | FP32 | 4.306 / 4.306 | 12.55 |
| 8 / 8 | FP32 | 1.464 / 8.852 | 19.55 |
| 32 / 26 | FP32 | 1.706 / 13.126 | 37.55 |
| 8 / 8 | FP64 | 2.558 / 16.056 | 35.08 |

More workers did not always improve wall time. The four-bit batch used 32
actual workers: coherent median 31.425 wall / 332.226 CPU seconds and at most
75.86 MiB accounted DSP workspace. Its differential receiver used 6.752 wall /
69.320 CPU seconds. Workspace excludes caller capture/TX/simulator storage,
thread stacks and general runtime overhead. These are CPU measurements;
there is no claimed GPU/BLAS execution or single-carrier CPU-ceiling parity.

Median waveform diagnostics for examples 1/2 respectively: 99% power bandwidth
2118.75/211.875 Hz; power outside the ideal band 0.0268%/0.00113%; peak/mean
power 8.91/7.13 dB; active-envelope fourth-moment ratio 1.695/1.711. They support
the shaping/noise-like design characterization, not a regulatory mask or LPI
qualification. Sparse patterned signals are not Gaussian white noise.

All retained logs are compressed JSONL. [summary.json.gz](summary.json.gz)
contains per-condition Wilson intervals, wrong accepted signal frames,
control false accepts and resource summaries. Reproduce it with
`tools/triad-examples-summary.py`; it rejects duplicate or incomplete runs.
The ordinary Robust receiver, GUI/progress/framing regressions, hardware,
emissions certification and release promotion were outside this experiment.
