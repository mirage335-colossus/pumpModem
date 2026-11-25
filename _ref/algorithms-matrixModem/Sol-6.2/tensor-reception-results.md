# Tensor Core scoring in a repeated-FSK receiver

24 November 2026 · Native CUDA experiment

The [follow-up bottleneck experiment](tensor-receiver-bottlenecks.md) separates score materialization, cache-sized tiling and frontend sampling from the original speedup.

A 67,108,864-hypothesis decoder received 15-bit messages on simulated fixed-calendar, delayed and Doppler-spread FSK input. On a full RTX PRO 6000 Blackwell Server Edition, FP16 Tensor Core scoring with FP32 accumulation preserved all observed FP32 decisions. Sustained scoring reached 56–57% Tensor Core activity. A matched comparison combining Tensor scoring with a shared sliding tone projection reduced median IQ-to-decision processing from 7.05 to 2.07 ms, with 28–29% Tensor activity across full replay. The decoder still missed weak disturbed-channel signals and accepted strong interference and invalid codewords. [Source, geometry and reproduction](../../../_appraisal/algorithms-matrixModem/tensor-reception/)

## Matched GPU comparison

Both receivers searched 2,048 public timing/frequency rows and 32,768 legal codewords. The RF fixture was independently synthesized in C++. An FP32 cuFFT frontend and two-look tone likelihoods produced the same inputs for both scorers. The score was a dense matrix product between the received likelihood contrasts and codeword signs. The GPU reduced the result to a winner and returned a presence, quiet or unresolved decision. An independent CPU direct DFT and scalar codebook implementation checked 6,144 feature values and 6,144 row/codeword scores over small full grids before the performance trials.

| Features | Frame data time | Dense scores per opportunity | FP32 scorer | Tensor scorer | FP32 IQ to decision | Tensor IQ to decision | Full processing speedup |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 512 | 81.92 s | 68.72 GFLOP | 1.277 ms | 0.581 ms | 2.096 ms | 1.401 ms | 1.50× |
| 2,048 | 327.68 s | 274.88 GFLOP | 3.840 ms | 1.130 ms | 6.981 ms | 4.264 ms | 1.64× |

Times are medians from 78 and 32 pre-rendered input opportunities per arithmetic route, respectively. Each independent draw was processed by both history modes, with paired arithmetic routes and alternating route order. Timings include IQ upload, projection, evidence formation, route-specific conversion, scoring, reduction, host decisions and occupancy readback. They exclude simulator generation, online input arrival and one-time allocation. The largest observed long-shape IQ-to-decision times were 7.199 ms FP32 and 4.424 ms Tensor. Application-counted GPU allocations were about 393 MiB and 706 MiB by shape, including an explicit 256 MiB FP32 matrix of candidate scores. CUDA context and library-internal allocations are outside those counts.

| 2,048-feature sustained mode | Opportunities/s | Nominal dense score TFLOP/s | Any Tensor activity | SM activity |
| --- | ---: | ---: | ---: | ---: |
| FP32 scorer replay | 271.0 | 74.48 | 0% | 98% |
| FP16 Tensor scorer replay | 938.8 | 258.05 | 56–57% | 97% |
| FP16 full IQ-to-decision replay | 239.2 | 65.75 | 14% | 58–59% |

For telemetry, each timed replay lasted ten seconds. Rates include the full named mode. Device-activity ranges use interior one-second GPM intervals after excluding possible start/end mixtures. The FP32 control showed no Tensor activity. The FP16 scorer showed matching HMMA and any-Tensor activity, active GPU clocks of approximately 2.38–2.39 GHz and 39–41% GPM DRAM-bandwidth activity. The full replay alternated **signal and quiet IQ**, and re-executed the complete per-opportunity path. The isolated scorer reused features produced by an independently validated disturbed-channel input. GPM reports how often the SMs performed Tensor arithmetic. Nominal FLOP/s counts `2 × rows × words × features` and includes conversion and reduction in elapsed time. These metrics describe different quantities. In particular, the 258 TFLOP/s corresponds to FP16-input dense scoring, with no inference to the device's advertised FP4 peak. [NVIDIA GPM metric definitions](https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlGpmEnums.html), [NVIDIA device specifications](https://www.nvidia.com/en-us/data-center/rtx-pro-6000-blackwell-server-edition/)

## Faster tone projection fed the same Tensor scoring work

A second run compared cuFFT and a sliding Fourier recurrence on the same full GPU, code and captured inputs. At a fixed frequency bin, moving the start by four IQ samples updates the complex tone projection by removing the four samples that left, adding the four that arrived, and rotating the result to the new local phase. The implementation seeds the first projection directly, uses FP32 for both the seed and updates, and retains all 64 starts and all 32 frequency candidates. The two-tone pairs at adjacent frequency hypotheses share bins, so there are 34 distinct tone bins per feature. The recurrence reuses their projections without zero-padded FFT scratch.

| Features | cuFFT + FP32 | cuFFT + Tensor | Sliding + FP32 | Sliding + Tensor |
| ---: | ---: | ---: | ---: | ---: |
| 512 | 2.115 ms | 1.425 ms | 1.536 ms | 0.848 ms |
| 2,048 | 7.047 ms | 4.358 ms | 4.762 ms | 2.072 ms |

For the long Tensor route, the median projection stage fell from 3.222 to 0.938 ms and the scorer stayed at about 1.122 ms. With the frontend held fixed at sliding Fourier, Tensor scoring reduced total latency from 4.762 to 2.072 ms. With both changes, the overall improvement versus cuFFT plus FP32 was **3.40×**. The alternate reduced application-counted frontend allocations from 57.1 to 35.5 MiB at this shape. It passed the same independent direct-DFT and scalar-score verification. When matched across all 220 timed settings, decisions, accepted words and winning rows agreed. The largest absolute difference in maximum likelihood score was 0.001129 and in peak occupancy was 0.00586 in the experiment's noise-normalized units. Both frontends passed the quiet/resumption sequence and exhibited the same false accepts and weak-signal failures.

| Long-shape full FP16 replay | Opportunities/s | Nominal score TFLOP/s | Any Tensor activity |
| --- | ---: | ---: | ---: |
| cuFFT tone projection | 232.6 | 63.93 | 13–14% |
| Sliding tone projection | 492.6 | 135.40 | 28–29% |

The faster frontend doubled the amount of useful work delivered to Tensor Cores during full replay. This result depends on the regular start grid, short recurrence depth and small reused tone set of this fixture. Different uncertainty profiles may favor FFT, filter banks or a different recurrence. The factorization did not change reception energy or timing uncertainty.

## Receive outcomes and failure modes

The signal table counts accepted correct words. Columns share the same synthetic IQ, airtime and transmit energy. Both arithmetic routes agreed on every presence/quiet/unresolved decision and all accepted words in the 110 paired settings. All maximum-score differences stayed within the implemented numerical allowance plus the stated small test tolerance. That allowance operates on the computed FP32 features. It covers operand conversion and dot-product accumulation. The frontend's error is checked empirically against the scalar DFT. The 0.001 white-noise false-alert bound applies to the ideal statistic under the specified null model.

| Features | Constructed channel | Pre-channel C/N₀ | Two-look power | Power plus history |
| ---: | --- | ---: | ---: | ---: |
| 512 | Flat | +10 dB-Hz | 3/3 | 3/3 |
| 512 | Moderate, 1 Hz RMS and 1.875 ms echo | +8 dB-Hz | 5/5 | 5/5 |
| 512 | Disturbed, 10 Hz RMS and 6.875 ms echo | +10 dB-Hz | 2/5 | 1/5 |
| 512 | Held-out 6 Hz RMS, 4.063 ms echo and +0.8 Hz residual CFO | +10 dB-Hz | 5/5 | 5/5 |
| 512 | Disturbed | +4 dB-Hz | 0/5 | 0/5 |
| 2,048 | Moderate | +3 dB-Hz | 4/4 | 4/4 |
| 2,048 | Disturbed | +6 dB-Hz | 2/4 | 1/4 |
| 2,048 | Held-out | +5 dB-Hz | 3/4 | 3/4 |

- All 12 white-noise inputs across the two shapes were quiet. The two-look detector also called all five weak disturbed 512-feature inputs quiet, and made one false-quiet decision in each long disturbed and long held-out group. At these levels, a signal can resemble absence even when the receiver examines every configured hypothesis.
- One long disturbed input produced an accepted wrong word on both history settings. All four out-of-codebook inputs and all four narrowband-interference inputs produced accepted words. The present/quiet rule bounds these decisions only under the specified white-noise null. An additional competing-signal or codeword-validation test is needed before operational use.
- In the five-opportunity sequence, the two valid words arrived on slots 1 and 3, slot 2 was quiet, and physically scored quiet opportunities 4 and 5 ended the sequence. Both valid words were delivered and the outcome occurred at slot 5. The elapsed processing time was 7.030 ms across all five opportunities, after the IQ was available. A weak-activity episode can still end a message erroneously if it is scored quiet twice.
- The 2,048-feature example at +3 dB-Hz was moderate with about 16.4 dB of pre-channel energy per information bit. Its 0.046 bit/s data rate and 327.68 s data duration helped produce this result. The captures use a known public epoch and independently rendered finite opportunities. The channel resets between opportunities, and a capture can truncate the final delayed echo at its boundary. The test levels and models leave field performance, frequency drift, sample-clock error, extended outages, −10 dB-Hz reception and acquisition of unknown patterns unqualified.

Fixed positive history weight produced equal or lower correct counts in this screen. Under a centered Gaussian Doppler spectrum, the normalized correlation of the **80 ms window averages** of adjacent looks is about 0.886 at 1 Hz RMS, 0.179 at 6 Hz RMS and 0.094 at 10 Hz RMS. This is a single-tap reference, using the triangular average of `exp(−2π²σ²u²)` for the time differences between two rectangular windows. The broad-spread fixture has two finite sinusoidal taps and can depart from that reference. The result helps explain why a fixed 0.35 correlation is a weak default across paths. A covariance model with independently tested weights or a search that includes the energy-only alternative needs its own search-wide calibration. [HF channel envelope](hf-channels.md), [ITU-R F.1487](https://www.itu.int/rec/R-REC-F.1487-0-200005-I/_page.print)

### Why a high score can accept the wrong signal

The codeword likelihood against white noise rewards common energy in both tone projections. A separate check instead compares each winning codeword to a model that chooses either tone independently and uniformly at each feature. Let `d = q+ − q−` and `s` be each codeword sign. The modified score is the sum of `s·d/2 − log cosh(d/2)` over features. This removes the common energy term and asks how much evidence is specific to the proposed sign sequence.

As a post-hoc check, the scalar receiver re-evaluated the 64 originally accepted FP16 row/word settings on the same fixtures. It retained 31 of 46 correct accepts, zero of the two settings accepting the wrong word, and zero of the 16 interferer and out-of-codebook accepts. The 16 settings represented eight independent nuisance fixtures processed with both history choices. The screen lost 15 correct accepts, so even on these fixtures it traded away legitimate reception. It used the threshold 24.9296 and checked only the original winner. A full receiver would need to search the modified score over every legal row and word, evaluate fresh nuisance and wanted episodes, and calibrate thresholds for the actual null. [Reproduction instructions](../../../_appraisal/algorithms-matrixModem/tensor-reception/README.md)

## Interpretation for Matrix Modem

- Dense Tensor arithmetic gave a material speedup for genuine joint timing, frequency and message work whenever the chosen bank supplied enough scores. The chosen random codebook is one research detector. Structured soft FEC, a matcher that rejects interferers, and codeword confidence under the right nuisance model may deliver better complete reception with less computation and airtime.
- Two repeat windows can provide useful phase-aligned evidence. Here cross-products were computed after channelization in FP32, and Tensor Cores evaluated the subsequent codeword search. A separate [drift experiment](received-history-drift.md) examines raw received history directly. The high scorer counter does not measure raw history correlation or declare a new wire format.
- A live stream provides one new 327.76 s opportunity at a time in the long case. Both measured arithmetic routes process a queued opportunity much faster than samples arrive. Using the 2.072 ms optimized median against that frame duration gives about 0.0006% of frame time for bulk post-capture processing. High offline replay throughput is useful for development and larger public hypothesis sets. Extrapolation to a different live waveform or hardware needs measured arrival, buffering, failed acquisition and end-to-end deadlines.

Both measured builds used CUDA 12.8.93, driver 595.112.07, SM120, and a full 97,887 MiB GPU with MIG disabled. The initial cuFFT executable SHA-256 was `c6e47bf856ee6644647e9424808fec75970c42af0c90f6ea7f59c9474d85fc3d`. The same-source cuFFT rebuild and the sliding build on the second device had SHA-256 values `e197ea813647bd868d222e1eedf4fc19d67c15210d6f1f62cc1c8d0154bc8129` and `cb4a748f1dfe9c85ecca9f5429465233f55b2b7fc3e2ae838b12bce77d8fb9be`, respectively. The first two launch attempts stopped in NVCC compilation. The missing CUDA math constants include and use of a host-only C++ constant in device code were corrected before measurement. All instances used source-only SSH uploads below 24 KiB and were verified removed.
