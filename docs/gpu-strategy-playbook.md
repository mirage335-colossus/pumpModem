# GPU and Tensor Core strategy playbook

This page says which GPU and Tensor Core strategies to try next, how to
derive new ones from measurements, and how to test them. It puts the decision
rules, formulas, checklists and the status of every strategy in one place.
The [GPU performance guide](triad-gpu-optimization.md) holds the full
measurements and route details, the [precision guide](gpu-precision-controls.md)
the arithmetic contracts, and [validation](validation.md) the dated records.
When this page and a linked record differ, the record wins; correct this page.

## Start here

Kinds: **T** uses matrix units (Tensor Cores, Vulkan cooperative matrices,
RDNA 3 WMMA); **G** raises GPU use without them (ports, residency,
batching, fusion, traffic, overlap, admission); **C** is CPU or host work.
*Modeled* figures come from fixed-geometry cost models, *inferred* from code
or arithmetic, *derived* from vendor peaks or receipt counts, *indicative*
from single or loaded runs; they order work and qualify nothing. Compare
times only within one record. Then read the [orientation](#orientation),
[catalog](#strategy-catalog), [eligibility procedure](#eligibility-procedure),
[strategy generation](#generating-new-strategies), [test recipe](#test-recipe)
and [per-GPU guidance](#per-gpu-guidance). Untried rearrangements that move
more of the pipeline onto the GPU are in
[Concepts to try later](#concepts-to-try-later-move-the-pipeline-onto-the-gpu).

### Ten facts not to get wrong

1. **Tensor Core work on the challenging slots is about zero, by
   construction.** The 72 Hz/5× control, 36 Hz/10× holdout and HF −22.5 run
   segmented timing, which exists only with a varying passband; that
   withholds the device bank, so they never reach dense FP4
   (`opencl_coarse_score_rows` 0). HF ran a CUDA build: no dense FP4 or
   family route, zero IMMA and HMMA calls. Only a new eligible contraction or device synthesis
   (lever 12) changes this ([map](#tensor-core-opportunity-map-for-the-challenging-slots)).
2. **Most measured wins are G or C.** T wins, all on easy or non-segmented
   work: dense FP4 frontier (matched slot 2.2–2.5 → 0.49–0.82 s, mostly by
   moving bank preparation to the device), ring layout (426 → 1,215 TFLOP/s),
   resident NVFP4 fine (0.55–0.60 → 0.08–0.11 s), CUDA INT8 lag matrix
   (PRO 6000 backlog coarse 4.45 → 3.69 s). Larger G wins: device gain rank
   (holdout 688 → 281 s), Vulkan segmented timing (72 Hz/5× stage 45.7–46.3 →
   5.7–6.0 s), FP32 per-row kernels, half cuFFT (packed SIMT, no `HMMA`).
3. **Automatic (`--backend gpu`, GUI Default (fastest)) by build.** Vulkan
   with the OpenCL `sm_120a` module on SM 12.0 (PRO 6000; RTX 5090
   unmeasured): dense FP4 for eligible sessions, segmented timing in its
   envelope, the device gain rank, resident NVFP4 fine; drift and varying
   native. CUDA: cuFFT segmented timing on SM120 only, gated half cuFFT and
   INT8 lag matrix, every family native. Other Vulkan GPUs: the FFT search
   (Vega also packed direct). Both backends built: CUDA first
   ([routes](triad-gpu-optimization.md#what-automatic-selection-runs)).
4. **Exactness classes decide formats** ([classes](#exactness-classes)). E1,
   outputs equal native: the gain rank, where only TF32×3 or BF16×3 could
   certify. E2: FP64 device varying. E3, proposals rescored natively: dense
   FP4, segmented timing, drift proposals, half cuFFT, INT8 lag matrix. E4,
   evidence under a measured budget: resident NVFP4 fine, FP32 mapping,
   proposed `fp32-template`. State and pre-isolation work keep precision.
5. **What decides a default:** complete slot and backlog service on the
   challenging slots at full `W = 2*H`. An easy-slot loss is no veto, an
   easy-slot gain no justification, a universal FP64 tolerance or fixed dB
   cutoff no veto ([contract](gpu-precision-controls.md#automatic-mode-prioritizes-computational-ceiling)).
6. **Where the time is** ([table](#where-the-time-goes-now)). HF (CUDA,
   3,707 s): CUDA segmented 1,626 s (44%); native drift 899, varying 401,
   signed-lag 386 s (46%). Holdout (281 s): native drift 60.9 and varying
   89.9 s (54%), about 110 s unsplit coarse (39%), gain rank 3.05 s.
   72 Hz/5× (17.75–19.76 s): varying 5.32–5.47, drift 2.18–2.22, rank
   1.34–1.47 s, other coarse 8.0–9.7 s.
7. **Choose the lever from measured work.** The family receipts resolved
   the varying slowdown on two 72 Hz shapes. The later Vulkan holdout spent
   88.89 of 169.48 s in segmented timing. Open attribution includes the
   3–4× in-slot penalty of CUDA segmented transforms, the comparable large-
   quota Vulkan split, and the HF drift ridge share (assumed 85–95%).
   [Measured ranges](validation.md#vulkan-fine-stage-attribution-and-staging--21-november-2026).
8. **Margins are host-specific.** Native stages scaled inversely at W = 80
   on the 40.8-CPU Xeon (signed-lag 2.4–4.6× slower than on the 5650U).
   Never divide across records, count the simulator in a slot or read
   lane-summed sub-timers as wall time.
9. **Units:** operations = 2 × multiply-adds; FFT flops ≈ 5 × row-stages;
   ceiling `1/(1−s)`; `R = 1.6·m/log2 n` (padded form in [step 5](#eligibility-procedure));
   dense products `4 × rays × arrivals × rows` per workspace and frequency;
   the Server Edition's FP4 peak is 1.87 PFLOP/s (*derived*).
10. **NVFP4 on Blackwell is already the default where eligible** (dense
    frontier, resident fine), as AGENTS.md asks. Challenging slots need
    device synthesis (lever 12); an explicit `--vulkan-precision nvfp4` run
    there is neither attribution nor a test of the automatic route, at most
    a feasibility estimate of the products ([synthesis row](#proposed-and-untried)).

### From an idea to build, measure first or stop

1. Name the stage and the challenging slots it changes ([stage table](#the-procedure-applied-to-each-stage)).
   It never runs on one: rank it on its own target plans.
2. It runs native on the target build: a device port or per-stage selection
   (G) comes before any format.
3. Share `s` from one record; under 5% of every challenging slot: **stop**
   as a lead (prerequisite or regression item).
4. In the [catalog](#strategy-catalog) as rejected or historical: **stop**
   unless its "reconsider when" now holds.
5. Unsplit span or unexplained in-slot gap: **measure first** (a
   [pod item](#one-hour-pod-plan-worked-example) or lever 1).
6. T only: M, N ≥ about 64 after batching, `R` below the achieved
   Tensor/FFT ratio, above the ridge point or on chip; else recast as G or
   **stop**.
7. The class must allow the format: E1 split forms only, E3 the
   planted-candidate rule, E4 the screen then a measurement.
8. Optimistic saving below the [build threshold](#default-thresholds-and-stop-rules)
   (T 5% of one challenging slot and 1.5× on the matrix part; G, C 5%):
   **stop**.
9. The cheapest falsifying check ([list](#proposed-and-untried)) fails:
   **stop** and record it.
10. **Build**, gate locally, then device tests, defaults unchanged and
    alternated pairs on the pod; decide on slot totals.

### Current top strategies by kind

Lever numbers are identifiers of the [GPU guide](triad-gpu-optimization.md#next-levers-in-order),
not an order. For ranked next steps on the RTX PRO 6000 use the
[per-GPU list](#rtx-pro-6000-blackwell); within each kind below, rows follow
it. For a GPU-wide request that stresses Tensor Cores keep that order and
label each item from the [opportunity map](#tensor-core-opportunity-map-for-the-challenging-slots).
Rows: [proposed table](#proposed-and-untried).

| Kind | Strategy (lever) | Reaches | First step |
| --- | --- | --- | --- |
| G | No-code measurements: remainder split, A E E A gain rank, varying batch width, quota against affinity | All three | [Pod items](#one-hour-pod-plan-worked-example) 1–5 |
| G | Kernel trace of the CUDA segmented stage | HF 1,626 s | Pod item 6 |
| G | Same-pod CUDA against Vulkan automatic routes, then per-stage selection or CUDA parity (7) | HF: 1,686 s of CPU families | Holdout shape, one source |
| G | Drift and varying device redesigns (2, 3) | All three | Pod items 3, 5; ridge benchmark |
| G | Segmented per-call or traffic levers chosen by the trace; envelope (5, 6) | HF; 72 Hz/1× for lever 5 | Per-call first if kernels are under half the span |
| G | Gain-rank gap (9), fused rank (10) | 72 Hz/5× about 0.7–0.9 s | Pod item 2 with lever 1's batch width |
| T | Device synthesis of varying-passband banks for dense FP4 (12) | Holdout, 72 Hz/5×; HF after lever 7 | Pod item 1 |
| T | Large dense plans (11) | None of the three | IC-7100 product interval |
| T | Fused matrix-unit segmented FFT; varying DFT-to-modes GEMM; split gain-rank DFT; Ada dense port (13) | At most about 200 s at HF, or under 0.1 s, or none | Not leads today |
| C | Native-stage scaling at large `W` (4); exact CPU drift and varying work | Holdout about −52 s (*modeled*) | Pod item 4 |

## Common mistakes

Recurring misreadings, each followed by the fact and its source.

**Routes and what ran**

- HF's families "ran on the device" → HF ran CUDA automatic, which keeps
  drift, varying and signed-lag on the CPU ([routes](triad-gpu-optimization.md#what-automatic-selection-runs)).
- A CUDA build "runs" the gain rank, dense FP4, resident fine or a family →
  none of them.
- Automatic CUDA segmented timing "on Ada" → SM120 only; SM89 needs
  `--cuda-segmented-timing on`, unqualified ([Ada](#rtx-4090-rtx-4090-laptop-and-rtx-4060-ti-16-gb)).
- "Dense FP4 is the only Tensor route" → CUDA also runs INT8 IMMA lag
  matrices in their gate, FP16 HMMA explicitly, NVFP4 in SM120 `TensorFine`.
- Half cuFFT "uses Tensor Cores" → its SM120 functions hold `HADD2`,
  `HFMA2`, `HMUL2`, no `HMMA` ([CUDA record](validation.md#current-cuda-precision-and-tensor-dispatch)).
- Zero `cuda_matrix_lt_*_calls` on Vulkan "means no Tensor work" → CUDA-only
  counters; read `opencl_coarse_score_rows`, `opencl_resident_nvfp4_modes`.
- Plain `--backend vulkan` "is automatic" → segmented timing and gain rank
  decline; 256 MiB per session, no chirp-Z.
- Lever 5 "changes the 72 Hz/5× control" → it widens to 72 Hz/1× shapes;
  the control already runs 156 device sessions.
- Vega "accelerates only the packed dot" → also FP32 VkFFT and FP16-storage
  FFT searches ([Vega](gpu-precision-controls.md#vulkan-on-vega-integrated-graphics)).

**Shares, records and hosts**

- "Gain rank 43.7–55.5 → 17.8–19.8 s" as the stage → 72 Hz/5× slots; the
  stage went 26.7–32.6 → 0.57–0.68 s explicit, 1.34–1.47 s automatic
  ([21 Nov](validation.md#blackwell-qualification-of-the-fine-stage-device-routes--21-november-2026)).
- HF's 2,220 s coarse "is the frequency search" → segmented 1,626, rank 386,
  other 208 s; the search is inside the 208 s.
- The holdout's 110 s remainder "is CPU-family time" → drift and varying are
  in `fine_seconds` ([bounds item 2](triad-gpu-optimization.md#what-bounds-the-challenging-slots)).
- HF's simulator 404 s "in the slot" → the stages add to 3,706 s without it.
- 5.7–6.0 s (20 Nov) divided by 17.75–19.76 s (21 Nov) → within 20 Nov:
  5.97–5.99 s of an 18.42–20.49 s slot.
- 21 November times "as targets on a new pod" → pod CPUs differ; compare
  within one run.
- "NVFP4 fine costs 0.04 s" → that is the whole matched fine stage; the
  products took 2.0 ms; 0.0046% of peak (19 Nov) predates resident tiles.
- "63 ns per drift load was measured" → HF drift seconds ÷ a modeled load
  count, ridge share assumed 85–95%; the Xeon ran 5–12 ns ([drift](triad-gpu-optimization.md#drift-proposals)).
- "Build a slope-blocked ridge" → blocks of 16 are in the tree; time them on
  an HF slot or EPYC 9555-class CPU ([lever 2](triad-gpu-optimization.md#next-levers-in-order)).
- Drift's mutex and 51–67% host render "are measured causes" → the render
  share is from the 5650U; both are inferred until lever 1.
- Radeon 2 ms per gain-rank block "for Blackwell" → NVIDIA: 0.07 ms in the
  slot, 0.22 ms in the oracle.

**Eligibility and precision**

- Proposal-only (E3) gain ranks → E1 only: separation equals one map's
  evidence difference to about 1e-13 ([rejected](#rejected-or-stopped)).
- The gain rank's band argument applied to varying → varying is E4: BF16 is
  excluded by `u·E` 0.71 > `f·D` 0.31 nats; `fp32-template` first.
- A screen "passes" or "fails" a format → a lower-bound `E` only excludes;
  a measured excess calls a rerun or sensitivity trial ([E4](#eligibility-procedure)).
- The E4 screen applied to segmented timing → its ranks are E3 proposals;
  FP4 and FP8 are allowed by class.
- Bit separation as a quiet slot's margin → presence and quiet energies bind.
- `dense-cost` "declines two-ray plans" → one-ray automatic only; HF Worst
  3.6 Hz two-ray is admitted, unmeasured.
- A padded length in `1.6·m/log2 n` → use the padded form of step 5.
- Gain-rank Tensor DFT bounded by HF's native 386 s → it touches only the
  device transform share (3.05 s, 1.34–1.47 s); HF after lever 7, about 29 s.
- Gain-rank `R` = 8.8–17 → that is a per-lag rescoring form; against the
  256-point overlap-save tiles that the native and device ranks both use at
  HF, R ≈ 5.5.
- Dense synthesis's "2 s of FP4 products" as a saving → it is the dense
  route's cost; the addressable time is the frequency-search share.
- FP16-accumulate partials "on the PRO 6000" → they pay only on GeForce.
- SM120 FP8/INT8 dense frontier "is lever 13" → it serves banks below the
  1/16 guard; lever 13 is the Ada INT8/FP8/INT4 port.
- 80 ns "per Gaussian update" → per Markov chain-state; Gaussian updates are
  about 3% of varying CPU (*modeled*).
- "Test `fp32-template` before building" → it has no kernels; check with pod
  items 3 and 5; its gate is E4, not E2's 1e-12.
- Drift "has no Tensor path because `m = n`" → that excludes a dense DFT;
  drift FFTs run native and are 15–30% of drift (lever 2 first).
- RTX 4060 Ti INT8 scaled from the RTX 4090's peak → the 4090 lost to
  packing and launches (0.3 ms of arithmetic, 2.3 s loss).

**Segmented timing**

- Frequencies per block "257" (the gate minimum) → HF families score 15,733
  and 3,935, about 7,019 on average ([shapes](#worked-examples)).
- "Up to 54 phases" → at most two ([definition](gpu-precision-controls.md#current-implementation)).
- "Bandwidth-bound" as a fact → pass count unverified; FFT kernels may be
  400 of 1,626 s; trace first.
- Pinned segmented uploads "as a new lever" → about 94 GB of the slot's
  249 GB, 1.7 s pinned, under 1% of the stage (*derived*).
- The 249 GB "belongs to the affine route" → it is the slot-wide
  `upload_bytes`.
- Vulkan in-slot "2% of peak, 1.2–1.7 TB/s" → CUDA figures; Vulkan ran
  0.71 µs per 16,384-point inverse transform including the rest of the
  segmented stage. The [command profile](validation.md#inside-vulkan-segmented-timing)
  separates the GPU stages on component shapes and samples the 72 Hz/5× slot.
- A direct (Hankel) correlation GEMM "replaces the inverse FFTs" → about
  1.7e18 flops at HF, dominated even at FP4 ([rejected](#rejected-or-stopped)).

**Receipts, tests and memory**

- `peak_reserved_bytes` read alike on both backends → CUDA lease counter,
  Vulkan allocation peak ([memory fields](#size-memory-on-a-pod)).
- `opencl_coarse_score_rows` "needs profiling" → always present; only
  `opencl_*_seconds` need `--opencl-profile on`.
- `mean_busy_hardware_fraction` from `admission` → stream only; use
  `process_cpu_seconds ÷ wall_seconds`.
- `--segmented-benchmark` for a CUDA change → Vulkan-only, run only on the
  Radeon; CUDA has `test_triad_tensor --cuda --segmented-only` and slot pairs.
- Lever 1 "measured on a pod" → it is code.
- `varying_coarse_seconds` "never recorded" → 20 Nov 72 Hz/5× runs kept
  5.97–5.99 s; the flipped runs and holdout lack it.
- `--vulkan-precision nvfp4` to attribute a challenging slot → it forces
  the dense search on FFT-search sessions.
- `--memory-limit-bytes` as `W ×` a per-owner figure, `--slot-owners` for
  `admission` → omit the limit; `admission` ignores owners.
- `--tensor-precision automatic --tensor-coarse-math fft` "must be added" →
  they are the probe defaults.
- HF's half-cuFFT length "unrecorded" → `plan`'s
  `coarse_full_prior.correlation_fft_rows` is 16,777,216, above 2^20.

**Ranking and pod planning**

- Lever numbers as the order → the [PRO 6000 list](#rtx-pro-6000-blackwell);
  lever 7 is its item 3 and HF's first step.
- An easy-slot lever ranked by a challenging share (Vega packed dot, the
  matched slot's FP4 loads) → rank it by its own target.
- A G lever (fused gain rank, segmented traffic, batching) called a Tensor
  Core strategy, or the reverse.
- Device synthesis "reaches HF" → only on the Vulkan route after lever 7,
  and HF's bank exceeds the 32-bit kernel extents (*inferred*).
- Lever 9 "drops the quarter lease" → it changes admission charges; leases
  must not wait ([lever 9 row](#proposed-and-untried)).
- "Never run the holdout twice" → about 5 minutes each, a few per pod hour.
- A code candidate on the no-code pod plan → use the
  [code variant](#one-hour-pod-with-one-code-candidate).
- A pod that flips nothing "records nothing" → it records the new splits
  ([step 10](#test-recipe)).

## Fact lookup

| Fact | Value or rule | Where |
| --- | --- | --- |
| Challenging slots | 72 Hz/5× control `af364b67…`, holdout `8a6c8ab2…` (both quiet), HF −22.5 (extrapolation only) | [Qualification slots](triad-gpu-optimization.md#qualification-slots) |
| Stage seconds | Per slot and host | [Where the time goes now](#where-the-time-goes-now); `admission` → `phases[]` entry `native_slot` → `work.{coarse,fine,varying_coarse,drift,varying_fine}_seconds`, `work.signed_lag_gain.proposal_seconds` |
| Stage nesting | Segmented span and rank inside coarse; drift and varying inside fine | [Attribute the time](triad-gpu-optimization.md#attribute-the-time) |
| Coarse sub-phases, Gram ratio | `coarse_prepare`, `coarse_tiles` (`minimum_eigenvalue_ratio`, `tile_width`, `route`), `coarse_selection` | `slot` and `continuation` `phases[]`; `admission` omits them |
| Busy CPUs | `process_cpu_seconds ÷ wall_seconds` of the phase | `admission` `phases[]`; `mean_busy_hardware_fraction` only in `message` and `backlog` `result.stream` |
| `W` | Twice the whole CPUs of quota or affinity: 80 at 40.8, 26 at 13.6 | `allocation.workers`, `allocation.hardware_threads`; [CPU allocation](development.md#receiver-cpu-allocation) |
| Host memory quote | Holdout 21,475,814,336 B at W = 26; HF 144.4 GB at 26, 204.6 GB at 54; device-route quotes on top | `plan` → `configuration.receiver_slot_workspace_bytes`; [Size memory](#size-memory-on-a-pod) |
| Device memory | `peak_reserved_bytes` differs by backend | [Memory fields](#size-memory-on-a-pod) |
| Selected backend, qualification | `cuda` or `vulkan`; both fields true on the SM 12.0 class | `identity.selected_accelerator_backend`; `describe --backend gpu` → `identity.backend_receipt.vulkan_{segmented,gain_rank}_qualified_device` |
| Counters always present | `opencl_coarse_{score_rows,issued_products,useful_products}`, `opencl_resident_nvfp4_modes`, `vulkan_*` sessions, declines and fallbacks, coarse `vulkan_device_command_seconds`, `cuda_*` witnesses | `identity.backend_receipt` |
| Only with `--opencl-profile on` | `opencl_coarse_{product,packing,selection,synthesis,render,filter,gram,rescore}_seconds`, `opencl_resident_*_seconds`; explicit `--backend vulkan` only | [Attribute the time](triad-gpu-optimization.md#attribute-the-time) |
| CUDA Tensor witnesses | `cuda_matrix_lt_imma_calls`, `cuda_matrix_lt_hmma_calls`, `cuda_matrix_padded_real_macs`; CUDA keeps host `wall_seconds` only | `identity.backend_receipt` |
| Segmented shape | Tracked family `work.varying_timing_{blocks,frequencies}`; forward FFTs = nonempty blocks; inverse ÷ forward = frequencies per block; rows per block = `projected_template_rows` ÷ blocks | `cuda_segmented_*`, `vulkan_segmented_*`; `plan` → `coarse_full_prior` ([shapes](#worked-examples)) |
| Frequency-search length | HF 16,777,216; holdout 2,097,152; the half-cuFFT gate admits 4,096–2^20 | `plan` → `coarse_full_prior.correlation_fft_rows` |
| Gain-rank fields | Route, `device_fft_row_stages`, band ratio, upload bytes; no batch width yet | `signed_lag_gain.*`, `vulkan_gain_rank_*` |
| E4 margin fields | Decision, presence, separation, quiet energies, evidence | [E4 screen](#eligibility-procedure) step 1 |
| FP4 Gram guard | Two-ray reduced precision needs `minimum_eigenvalue_ratio ≥ 1/16`; else `ill-conditioned-template` and the FFT session | [Dense search](triad-gpu-optimization.md#dense-fp4-coarse-search-and-device-template-bank) |
| Operation counts | Ops = 2 × products; FFT flops ≈ 5 × row-stages; dense products `4 × rays × arrivals × rows` (matched 1.21e9) | [Template](#generating-new-strategies), [unit conversion](#worked-examples) |
| Dense against FFT | `R = 1.6·m/log2 n`; padded `8·n_in·m/(c·5·n_pad·log2 n_pad)`; direct segmented correlation `8·A·r/(5·n·log2 n)` | [Step 5](#eligibility-procedure) |
| Vendor peaks, ridge points | Dense, FP32 accumulation; Server Edition FP4 1.87 PFLOP/s (*derived*) | [Hardware facts](#hardware-facts-for-decisions) |
| Achieved rates | FP4 products 1.14–1.16 PFLOP/s; cuFFT 0.95–1.26 µs per 32,768-point transform in a slot, 0.30 µs isolated; Vulkan 0.71 µs at 16,384; pinned upload 54.7 GB/s; no achieved TF32, FP16 or FP8 GEMM rate | [Achieved rates](#hardware-facts-for-decisions) |
| Thresholds, noise band | Build: T 5% and 1.5×, G and C 5%; same-build spreads about 11% (72 Hz/5×) and 18% (72 Hz/1×); two binaries 5–21% | [Defaults](#default-thresholds-and-stop-rules) |
| Pod durations | Setup about 20 min; 72 Hz/5× 18–20 s automatic; holdout 281 s plus about 10 s of first simulation | [Test recipe](#test-recipe) step 7 |
| Component tests | Emulated OpenCL tests; `test_triad_vulkan` modes; `test_triad_tensor --cuda --segmented-only` (CUDA) | [Test recipe](#test-recipe) step 3 |
| Instruction evidence | `ptxas -v`, `cuobjdump -sass`, CUPTI module capture, RADV shader dumps | [Test recipe](#test-recipe) step 9 |

## Orientation

### Goal and constraints

- **Objective.** Raise complete-receiver service on challenging slots so that
  input backlog cannot accumulate
  ([automatic-mode contract](gpu-precision-controls.md#automatic-mode-prioritizes-computational-ceiling)).
  Decide on complete slot and backlog service of the challenging slots. An
  easy-slot loss cannot veto a challenging-slot gain, and an easy-slot gain
  cannot justify a default.
- **Fixed work.** Keep every public hypothesis, the configured calendar and
  keyed ordinals, scored quiet-slot completion, bounded workspace and exact
  explicit limits. Doing less required work is not a speedup.
- **Worker budget.** `W = 2*H`: twice the effective hardware threads, that is
  twice the whole CPUs of the quota or affinity (80 at a 40.8-CPU quota, 26
  at 13.6). Never lower it to make a device route win
  ([CPU allocation](development.md#receiver-cpu-allocation)).
- **One native implementation.** Device work lives inside the C++ receiver
  through the Vulkan/OpenCL or CUDA backend; Python only launches and
  analyses ([native processing](development.md#native-processing-and-integration)).
- **Report what ran.** Requested and executed arithmetic and every fallback.
  A CPU fallback is not GPU evidence; half storage is not packed FP16
  arithmetic; an INT8 contraction is not an INT8 FFT.
- **Prefer lower precision** where its class allows it. Higher precision
  needs a measured reception, range or conditioning reason, or the absence of
  any plausible computational gain.

### Exactness classes

Every device or arithmetic change belongs to one class. The guides name E1 to
E3; this page adds E4 for the evidence-level routes they qualify without a
label.

| Class | Meaning | Gate | Examples |
| --- | --- | --- | --- |
| E1 | Published outputs equal native. Lower precision only proposes; an exact certification publishes or the native code reruns. | Identical lags, evidence and decisions; certification margins (`band_ratio` below 1) and fallback counts reported. | Signed-lag gain rank: FP32 device totals, FP64-rescored frontier ([ranks](triad-gpu-optimization.md#signed-lag-ranks)). |
| E2 | Same precision as native, different operation order. | Norm-relative 1e-12 per statistic group; absolute 1e-8 nats on the named fixtures, rescaled above about 1e6 nats of evidence. | FP64 varying statistics on the device ([varying](triad-gpu-optimization.md#varying-statistics)). |
| E3 | Lower precision chooses proposals, frontiers or candidates; every score the receiver uses is recomputed at native precision. Selection may differ at near ties. | Identical decisions on predeclared strata; selection differences reported; a planted-candidate retention rule for frontiers. | Dense FP4 frontier, Vulkan and CUDA segmented timing, drift device proposals, CUDA half cuFFT and INT8 lag matrices. |
| E4 | Lower precision feeds evidence or sufficient statistics directly, with wider accumulation. | A predeclared evidence budget against the decision margin on challenging strata, nonfinite and range fallbacks, identical decisions. | Resident NVFP4 fine projection (evidence within 0.1% of native), FP32 per-row mapper and renderer (statistics within 2.1e-7, evidence within 5.5e-9), FP32 partial rescoring (within 1.8e-7), the proposed `fp32-template` varying route. |
| State | Persistent state, recursions, decisions and codec. | Native precision. | Gaussian updates, Markov recursions, quiet-slot and bit decisions. |

Work before Helper/DATA channel isolation (raw composite FIRs, Helper FFTs)
keeps its current precision: a quiet DATA passband does not bound the
composite input's dynamic range.

### Where the time goes now

`1/(1−s)` is the most a stage can speed up its slot if it took zero time
(Amdahl). Shares are of the slot's wall time.

| Slot (input) | Route that ran it | Slot | Largest stages, share and ceiling | Tensor Core work |
| --- | --- | --- | --- | --- |
| HF 100 mW 36 Hz/10× Target −22.5, the hardest stored receipt | CUDA automatic, RTX PRO 6000, EPYC 9555 pod at 13-CPU affinity, W = 26 | 3,707 s | CUDA FP32 segmented timing 1,626 s (44%, 1.78×); native CPU drift 899 s (24%), varying fine 401 s (11%) and signed-lag ranks 386 s (10%), together 46% (1.83×); other coarse 208 s, other fine 186 s. The stages add up to the slot (1,626 + 386 + 208 + 899 + 401 + 186 = 3,706 s); the simulator's 404 s is outside it and is not a share. Device memory: `peak_reserved_bytes` 99.2 GB is lease accounting that reached its `workspace_limit_bytes` (initial free memory less 2 GiB headroom) by design; the allocator's physical peak was 38.6 GB reserved and 31.5 GB live. | None. CUDA builds have no dense FP4 search, resident fine route or fine-stage family, so drift, varying and signed-lag ran on the CPU. The affine half cuFFT declined the shape (`plan` `correlation_fft_rows` 16,777,216, above its 2^20 gate). The slot-wide `upload_bytes` was 249 GB, about 94 GB of it segmented per-block copies (*derived*, [worked example](#worked-examples)). |
| 36 Hz/10× 100 mW holdout, Target −12.9897 (`8a6c8ab2…`) | Vulkan/OpenCL automatic with the gain rank, Xeon 6952P, W = 80 | 281 s (688 s before the gain-rank flip) | Native drift 60.9 s and varying fine 89.9 s, 54% (2.16×); about 110 s of unsplit coarse work, 39% (1.64×), whose `varying_coarse_seconds` was not kept (the next automatic run splits it without new code); signed-lag 3.05 s, 1%. About 17.5 of 40.8 CPUs busy; `peak_reserved_bytes` 51.8 GB, on Vulkan the peak of charged Vulkan allocations, OpenCL buffers excluded ([memory fields](#size-memory-on-a-pod)); `vulkan_peak_leased_bytes` was not kept. | Not recorded (`opencl_coarse_score_rows` was not kept); segmented sessions cannot reach the dense search. |
| 72 Hz/5× control, 2 mW (`af364b67…`) | Same | 17.75–19.76 s | Varying fine 5.32–5.47 s (about 29%), drift 2.18–2.22 s (12%), signed-lag 1.34–1.47 s (7%), 8.0–9.7 s other coarse including device segmented timing. Within the 20 November record (EPYC 9335, before the gain-rank flip) the segmented span was 5.97–5.99 s of 11.06–12.93 s coarse and 18.42–20.49 s slot, 29–32%; do not divide figures across the two hosts. | Zero: `opencl_coarse_score_rows` 0 with one frequency fallback; device work is FP32 VkFFT. |
| 72 Hz/1× HF Worst, 0.2 W (`7e8ef8fa…`) | Same | 6.56 s (5.58–6.61 s before) | Coarse 5.52 s, mostly native segmented timing outside the envelope (4.58 s on the 20 November host); fine 1.03 s | Not recorded; none in segmented timing. |
| GPSDO base, `ic-7100` shift, 3.6 Hz one-ray plan, Spread None (`8e38aa8c…`; 3,377 arrivals, 44,966 rows, 134,690 frequencies) | Vulkan/OpenCL dense FP4, EPYC 9335, 24 workers | Coarse 19.16 s (27.1 s with FP64 rescoring; 93.7 s on the FFT route) | Two runs, not one record. The profiled FP64-rescoring run (coarse 26.97 s) timed FP4 products 11.35 s, FP64 rescoring 12.68 s and about 2.95 s of host work and round trips over 28,082 dispatches; bank rendering and filtering 5.6 ms. The unprofiled FP32-partial run timed only coarse 19.16 s. Products are therefore about 59% of it (ceiling 2.4×) and FP32 rescoring about 4.9 s, both *inferred* across the two runs. The full command was not kept. | About 1.12 PFLOP/s of useful operations during the product interval (*inferred*: 2 × 6.4e15 useful multiply-adds ÷ 11.35 s); issued products, padding included, were not recorded for this plan. |
| Matched 3.6 Hz severe (`b4d8fa5a…`), easy | Same, 24 workers | 0.060–0.068 s | Fine 0.039–0.043 s, coarse 0.020–0.024 s; FP4 products 2.6 ms | 2.6 ms coarse and 2.0 ms fine products. Not a priority. |
| 72 Hz/1× and 72 Hz/5× on a Ryzen 5 PRO 5650U | Vega Vulkan, 24 workers | 22.9–23.9 s and 250.9–251.6 s | Coarse 20.7–21.7 s and 240 s, dominated by native segmented timing. The segmented stage itself (`varying_coarse_seconds`) was 20.2–20.3 s of a 23.5–23.6 s 72 Hz/1× slot on the 20 November idle host, about 86% (ceiling about 7×); the 72 Hz/5× stage was not split | None; Vega has no matrix hardware. |

Why the challenging slots do about zero Tensor Core work is fact 1 of
[Start here](#ten-facts-not-to-get-wrong); sources:
[Where the time goes](triad-gpu-optimization.md#where-the-time-goes),
[What bounds the challenging slots](triad-gpu-optimization.md#what-bounds-the-challenging-slots),
[cost model](validation.md#cost-model-of-the-cpu-families).

### Tensor Core opportunity map for the challenging slots

Seconds that matrix units could address on the three challenging slots
(72 Hz/5× control, holdout, HF), after [eligibility steps 3–7](#eligibility-procedure).
Today the answer is about zero: no post-isolation contraction on those
slots passes steps 3–7 with more than about 0.1 s of work. Each row is the
only path known for its stage.

| Candidate (kind) | Slots it reaches | Tensor seconds now | Prerequisite chain, in order | Addressable at most | Status |
| --- | --- | --- | --- | --- | --- |
| Dense FP4 frequency search of varying-passband and midpoint-gain sessions (T, through device synthesis, G) | Holdout and 72 Hz/5× on the Vulkan/OpenCL route. HF only after lever 7 moves it to that route, and its 12,904,486-row bank (312 packed rows × 25.8 M) exceeds the kernel's 32-bit extents, so the bank must be split or the extents widened (*inferred* from `Geometry` in the [dense session](../src/triad_opencl_nvfp4_coarse.cpp)) | 0: the bank is withheld (`varying-passband`, `midpoint-gain`) | 1. Split the coarse remainder (`varying_coarse_seconds`, then `slot` sub-phases) and find the frequency search's share. 2. Estimate the dense time from the `plan` receipt's `coarse_full_prior` (step 5 below): `2 × 4 × rays × arrival_points × projected_template_rows × workspaces × frequency_points ÷ achieved rate`, plus synthesis and rescoring. Do not use the 1.21e9 `dense-cost` bound: it gates one-ray automatic sessions only, and these plans are two-ray. Holdout: 4 × 2 × 10,553 × 1,348,486 = 1.14e11 products per workspace and frequency (94× `dense-cost`), × 78 × 131 = 1.2e15, about 2 s of FP4 products at 1.15 PFLOP/s (*modeled*). The code admits that plan (*inferred*): `OpenClNvfp4Coarse::supports` takes up to 2^32/4 rows and arrivals, the 32-bit extents allow up to 197 tile frequencies, and `Geometry` needs about 2.5 GB of bank plus about 41 MB per tile frequency (1.3 GB at the default 32, 5.4 GB for all 131) and synthesis buffers; `make_coarse` then declines a geometry above the workspace limit, global memory or the largest allocation. 3. Synthesize their banks on the device (part of [lever 12](triad-gpu-optimization.md#next-levers-in-order)). 4. E3 planted-arrival sweep | The frequency-search part of about 110 s (holdout, at most 1.64×) and of the 208 s other coarse (HF, CUDA): share unmeasured. The 2 s above is the dense route's cost, not a saving: the gain is the frequency-search time it replaces, net of synthesis and rescoring | Untried. The only path by which the existing FP4 kernel reaches a challenging slot. |
| Matrix-unit (factored) FFT for segmented timing (T) | All three | 0 | 1. Kernel trace of the CUDA stage ([worked example](#worked-examples)). 2. FP16 storage with scaling (lever 6). 3. A fused shared-memory factorization that removes DRAM passes, written per route: on CUDA a custom kernel with `mma.sync` HMMA (the rejected form used cuBLASLt FP16); on Vulkan cooperative matrices (FP16 16×8×16 and 16×16×16, BF16, FP8, INT8; no receiver kernel yet) or the `sm_120a` OpenCL module on SM 12.0 | The FFT kernels are about 400 s of HF's 1,626 s if the isolated cuFFT cost holds (*inferred*). If the kernel is DRAM-bound, a matrix-unit FFT saves at most what halving bytes saves, about 200 s; the isolated batch (33.5 MB) may have stayed in L2, so the trace or a cold-buffer timing must settle the pass count before using either bound. The other roughly 1,200 s of the HF stage is G work under the trace's stop rule. Vulkan in a slot: 0.71 µs per 16,384-point transform; 32,768-component [profile](validation.md#inside-vulkan-segmented-timing) | The unfused three-stage form is rejected (1.87–4.01× slower). |
| Varying lane-A DFT-to-modes GEMM (T) | All three | 0 | Split host batches into device calls → `fp32-template` → device rendering → fused render and contraction ([lever 3](triad-gpu-optimization.md#next-levers-in-order)) | About 5.9e12 flops at HF over 516,614 blocks (*modeled*), about 0.05 s at the FP32 SIMT peak: matrix units save under 0.1 s of 401 s. Scaled by blocks: holdout 54,374 blocks, about 6e11 flops, about 5 ms; the 72 Hz/5× control under a tenth of the holdout's (4,310 blocks in a stored receipt of its arguments on an earlier source) | Not a Tensor lead on any of the three; the device port (G) is the lever. |
| Split-precision Tensor DFT for the gain rank (T) | Holdout and 72 Hz/5× on the Vulkan route; HF only after CUDA parity (lever 7) | 0 | Fused FP32 kernel (lever 10) → measure transform against epilogue → TF32×3 or BF16×3 inside the E1 band | The transform share of 3.05 s (holdout, 1%) and of 1.34–1.47 s (72 Hz/5×, 7%) | Unpromising; E1 excludes FP16, BF16 and FP4 ([walk-through](#worked-eligibility-walk-throughs)). |
| Large dense plans (lever 11, T) | None of the three: non-segmented large plans (GPSDO/IC-7100 3.6 Hz one-ray; HF Worst 3.6 Hz two-ray, unmeasured) | IC-7100: 11.35 s of products in the profiled run; about 59% of the unprofiled 19.16 s coarse stage (*inferred* across the two runs) | Operand sharing, tile pipelining, frontier width, device 1/16 check | Up to about 4 s of products and 3 s of round trips (*modeled*) | Proposed. |
| Drift (none) | — | 0 | — | 0: the ridge is a gather, rendering is elementwise, and the slope and dechirp FFTs keep every bin (`m = n`), which rules out a dense DFT. A factored matrix-unit FFT is not analysed: the FFTs run native (lever 2 comes first), are 15–30% of drift CPU time (*modeled*), and a 2^21–2^25-point transform (about 17–270 MB of FP32 complex values) cannot stay in one SM's shared memory, so it keeps global-memory passes (*inferred*) | Not a Tensor lead; reconsider after lever 2 and the segmented trace. |

Levers 1, 2 and 4–10 of the [lever list](triad-gpu-optimization.md#next-levers-in-order)
raise GPU use or CPU capacity without matrix units (G or C). Lever 3's last
step, lever 11, the device synthesis above and lever 13's INT8, FP8 or INT4
ports are the only T items. A model asked for Tensor Core strategies on the
challenging slots should answer with this table, say that the larger
measured gains are G levers, and order items by the
[PRO 6000 list](#rtx-pro-6000-blackwell).

### Hardware facts for decisions

Vendor peaks are dense rates (no structured sparsity) at the vendor's boost
clock, from NVIDIA's architecture tables:
[RTX PRO Blackwell](https://www.nvidia.com/content/dam/en-zz/Solutions/design-visualization/quadro-product-literature/pdf/NVIDIA-RTX-Blackwell-PRO-GPU-Architecture-v1_1.pdf#page=46)
(Table 4), [GeForce RTX Blackwell](https://images.nvidia.com/aem-dam/Solutions/geforce/blackwell/nvidia-rtx-blackwell-gpu-architecture.pdf#page=47)
(Appendix A), [Ada](https://images.nvidia.com/aem-dam/Solutions/geforce/ada/nvidia-ada-gpu-architecture.pdf#page=30)
(Appendices A and B), and the product pages linked in
[representative hardware](triad-performance.md#representative-receiver-hardware).
They are ceilings, not receiver rates. Tensor rates are with FP32
accumulation, the form the receiver uses.

| GPU | SMs, boost | FP4 / FP8 / FP16 = BF16 / TF32 / INT8 Tensor (TFLOP/s, TOPS) | FP32 SIMT | FP64 | DRAM, L2, link |
| --- | --- | --- | --- | --- | --- |
| RTX PRO 6000 Blackwell Workstation Edition, SM 12.0 | 188, 2,617 MHz | 2,015.2 / 1,007.6 / 503.8 / 251.9 / 1,007.6 | 126.0 | 1/64 of FP32 (GB202), about 2.0 | 1,792 GB/s, 128 MB, PCIe 5.0; 96 GB |
| RTX PRO 6000 Server Edition, the measured device | 188, reported maximum SM clock 2,430 MHz | About 0.93 × the row above (*derived* from the clock ratio): FP4 about 1.87 PFLOP/s, the denominator for a percentage of peak on this card (older tables divide by a rounded 2 PFLOP/s, about 7% lower). NVIDIA's 4 PFLOP/s Server figure corresponds to the sparse rate. | about 117 (*derived*) | about 1.8 (*derived*) | Pinned upload measured at 54.7 GB/s; 97,887 MiB reported; L2 presumably the GB202's 128 MB (*inferred*; query `cudaDevAttrL2CacheSize`) |
| RTX 5090, SM 12.0 | 170, 2,407 MHz | 1,676 / 419 / 209.5 / 104.8 / 838 | 104.8 | 1/64 (GB202) | 1,792 GB/s, 96 MB, PCIe 5.0; 32 GB |
| RTX 4090, SM 8.9 | 128, 2,520 MHz | no FP4 / 330.3 / 165.2 / 82.6 / 660.6 (INT4 1,321.2) | 82.6; packed FP16 (non-Tensor `HFMA2`) also 82.6 | about 1/64 | 1,008 GB/s, 72 MB, PCIe 4.0; 24 GB |
| RTX 4090 Laptop, SM 8.9 | 76 (9,728 CUDA cores), 1,455–2,040 MHz, 80–150 W | Not published as dense rates; with the Ada ratios at 2,040 MHz about — / 160 / 80 / 40 / 320 (*derived*), lower when power-limited | about 40 at 2,040 MHz (*derived*: 2 × cores × clock) | about 1/64 | 16 GB GDDR6, 256-bit; bandwidth and L2 not stated (query `cudaDevAttrL2CacheSize`) |
| RTX 4060 Ti 16 GB, SM 8.9 | 34 (4,352 CUDA cores), 2.54 GHz | Not published as dense rates; with the Ada ratios about — / 88 / 44 / 22 / 176 (*derived*) | 22 ("shader TFLOPS") | about 1/64 | 16 GB GDDR6, 128-bit; bandwidth and L2 not stated (query `cudaDevAttrL2CacheSize`) |
| Radeon Vega gfx90c (Ryzen 5 PRO 5650U) | 7 CUs, 1.8 GHz | No matrix hardware and no accelerated 8-bit dot. Packed FP16 vector 3.2256 TFLOPS (*derived*). | 1.6128 (*derived*) | Not established: no exact-device source; treat device FP64 as slow and measure | Shared DDR4; two DDR4-3200 channels would give 51.2 GB/s (conditional) ([ceilings](triad-performance.md#radeon-arithmetic-ceilings)) |
| Radeon 780M (Ryzen 7 8840U), RDNA 3 | 12 graphics cores | WMMA 16×16×16: FP16 and BF16 with FP32 or same-type accumulation, INT8 and INT4 with INT32 ([GPUOpen](https://gpuopen.com/learn/wmma_on_rdna3/)). No peak claimed. | — | — | Shared memory |
| Intel UHD 630 (i9-9900K) | — | No XMX matrix hardware. | — | — | No test host |

NVIDIA lists equal non-Tensor FP32 and packed-FP16 peaks for the RTX PRO
6000 Workstation Edition (126 TFLOP/s each) and RTX 5090 (104.8 each). The
Server Edition product specification lists 120 TFLOP/s FP32 and 1,597 GB/s
DRAM bandwidth. Use the measured device clock for the table's conditional
Server Edition projections. The Ada L40 lists 90.5 TFLOP/s FP32, 48 GB and
864 GB/s, with comparable packed FP16 throughput inferred from the Ada
execution design. It lacks Blackwell FP4 Tensor Core support. Its FP32 rate
alone does not predict time for a plan dominated by memory traffic, FP64
statistics or host staging.
[RTX PRO architecture table](https://www.nvidia.com/content/dam/en-zz/Solutions/design-visualization/quadro-product-literature/NVIDIA-RTX-Blackwell-PRO-GPU-Architecture-v1.0.pdf#page=45),
[Server Edition specifications](https://www.nvidia.com/en-us/data-center/rtx-pro-6000-blackwell-server-edition/),
[L40 specifications](https://www.nvidia.com/content/dam/en-zz/Solutions/gtcs22/design-visualization/quadro-product-literature/rtx-6000-l40-linecard-nvidia-us-2653097-r7-web.pdf).

Ratios that decide a format (vendor peaks):

- **RTX PRO 6000:** FP4 : FP8 (or INT8) : FP16 : TF32 = 8 : 4 : 2 : 1, and FP16
  Tensor is 4× FP32 SIMT. FP16 with FP32 accumulation runs at the full FP16
  rate.
- **RTX 5090:** FP4 is 8× FP16 and 4× FP8 with FP32 accumulation; FP16 Tensor
  with FP32 accumulation is only 2× FP32 SIMT. GeForce cards halve the
  FP32-accumulate FP8 and FP16 rates, so FP4 is relatively more attractive on
  a 5090 than on a PRO 6000.
- **Ada (4090, and by the same SM design the Laptop and 4060 Ti):** INT8 :
  FP8 : FP16 : TF32 : FP32 SIMT = 8 : 4 : 2 : 1 : 1 with FP32 accumulation
  (both the RTX 4090 and RTX 4080 rows of the Ada table give these ratios).
  INT4 has the highest peak (1,321.2 TOPS on the RTX 4090, 16× FP32 SIMT,
  INT32 accumulation, software scales only) and INT8 the highest 8-bit peak
  (8×); keep both as E3 candidates. There is no FP4 and no hardware block
  scaling.
  With FP16 accumulation GeForce Ada doubles FP16 and FP8: the RTX 4090
  lists 330.3 and 660.6 TFLOP/s, against 165.2 and 330.3 with FP32
  accumulation (vendor peaks). That suits proposal-only (E3) partial sums
  with FP32 outer sums, as the historical K32 partials did; never evidence.
  TF32 equals FP32 SIMT (82.6) and BF16×3 (55) is below it. Packed FP16
  SIMT (`HFMA2`, the arithmetic of half cuFFT) also equals FP32 (82.6
  TFLOP/s on the RTX 4090), so half cuFFT gains on Ada come from halved
  traffic, not arithmetic (*inferred*).
- **Split precision costs three products.** TF32×3 never beats FP32 SIMT
  peak on these GPUs (84 against 126 TFLOP/s on the PRO 6000, about a third
  of FP32 on GeForce), and BF16×3 only on the PRO 6000 (168 against 126).
  Use a split form only where the matrix form also removes traffic or fuses
  an epilogue.
- **Ridge points** (peak flops per DRAM byte): PRO 6000 FP32 70, FP16 Tensor
  281, FP4 1,125; RTX 5090 FP32 58, FP4 935; RTX 4090 FP32 82, INT8 655. A
  kernel below its ridge point is bandwidth-bound unless its working set
  stays in L2.
- **Which interface reaches which format.** E2M1 block-scaled products only
  through the OpenCL `sm_120a` PTX module (dense search, resident fine) or
  CUDA cuBLASLt on SM120. FP16 and INT8 matrix products through CUDA
  cuBLASLt (HMMA, IMMA). In a Vulkan build outside the SM 12.0 OpenCL
  module, cooperative matrices (FP16, BF16, FP8, INT8; no FP4 or
  microscaling) are the only matrix route, and no receiver kernel uses them
  yet; on the PRO 6000 FP8 E4M3 and FP16 cooperative matrices executed with
  exact outputs under the worker filter
  ([record](validation.md#blackwell-nvfp4-fine-projection--19-november-2026)),
  and the Vulkan test identity reported `vulkan_integer_dot8_accelerated`
  true ([record](validation.md#blackwell-qualification-of-the-fine-stage-device-routes--21-november-2026);
  [recipe](#rtx-4090-rtx-4090-laptop-and-rtx-4060-ti-16-gb)). Vega has
  packed FP16 vector arithmetic only.

Achieved rates on the RTX PRO 6000 Server Edition, with their scope:

| Measurement | Achieved | Scope and source |
| --- | --- | --- |
| Dense FP4 coarse products, ring layout, inside the receiver | 1.14–1.16 PFLOP/s, about 61–62% of the derived 1.87 PFLOP/s peak; 1.215–1.29 PFLOP/s standalone, 0.92 at one frequency or 4,096 rows | Product event interval only ([FP4 product throughput](triad-gpu-optimization.md#fp4-product-throughput)) |
| Same kernel with every load hoisted (wrong products) | 1.73–1.76 PFLOP/s | Diagnostic upper bound of the layout |
| First product layout | 426 TFLOP/s | Limited by its instruction mix |
| cuBLASLt NVFP4 resident GEMM | 11.4 TFLOP/s at 288×96×3072; 458 at 1024×1152×3072; 1,309 at 8192×8192×4096 | Graph replays, separate process ([controls](triad-gpu-optimization.md#same-card-resident-and-staged-controls)) |
| Resident OpenCL NVFP4 fine projection | 19.0, 36.9, 51.1 and 66.3 TFLOP/s at 288, 576, 1,152 and 4,608 lanes | Matrix event; staged 1.16–2.03 TFLOP/s |
| Matched slot average of the FP4 products | 44–50 TFLOP/s | 3.0e12 issued operations over a 0.060–0.068 s slot |
| FP32 cuFFT segmented timing in a slot, N = 32,768 (CUDA route) | 3.9–5.2e11 FFT row-stages/s, about 2.0–2.6 TFLOP/s at 5 flops per row-stage, about 2% of FP32 peak; 0.95–1.26 µs per inverse transform. The three-pass traffic model (1.2–1.7 TB/s) is unverified: neither the pass count nor L2 residency is measured. | HF receipts, `varying_coarse_seconds` span, which can overlap host preparation, so a lower bound on the device rate; 0.7–1.4e11 on an RTX PRO 4500 |
| FP32 cuFFT inverse batches, isolated (same Server Edition) | 16,384 points: 18.40 µs per batch of 128 (0.144 µs each), 16.38 µs per 33 (0.50 µs each). 32,768 points: 38.88 µs per 128 (0.30 µs each), 30.69 µs per 95 (0.32 µs each); about 1.6e12 row-stages/s at full batches | [Dispatch](triad-gpu-optimization.md#hardware-and-library-dispatch). A 128 × 32,768 batch is 33.5 MB, so one read and one write in 38.88 µs is 1.73 TB/s, about the 1,792 GB/s DRAM peak, and three passes would be 5.2 TB/s. The batch fits in the presumed 128 MB L2, and repeated isolated batches may have stayed there, so this does not establish the DRAM pass count (*inferred*); a cold-buffer isolated timing or the kernel trace decides it. In a slot the same transform costs 3–4× more, unattributed. Tail batches cost more per transform. |
| Vulkan segmented timing in a slot, N = 16,384 | 3.2e11 row-stages/s, 0.71 µs per transform (8,075,106 transforms in 5.715 s), about 5× the isolated cuFFT figure | 72 Hz/5× control (Vulkan route, whole segmented wall time). Separate command regions are reported in the [Blackwell profile](validation.md#inside-vulkan-segmented-timing), which measures 32,768 only as a component. |
| Vulkan segmented agreement fixture (`test_triad_vulkan --segmented`) | 9.5–10.7 ms per call of 32 blocks × 257 frequencies at 16,384 points, 19.9–21.2 ms of 32 × 261 at 32,768 (1.2–1.3 and 2.4–2.5 µs per transform) | Single short calls including staging, PRO 6000 device-test outputs of 21 November; indicative only |
| Gain rank on the device | In-slot about 0.07 ms per block (3.05 s over 42,141 holdout blocks); oracle 0.516 s for 2,340 padded HF blocks (0.22 ms, component rate). The Radeon's about 2 ms per HF oracle block is Vega-only. | `device_fft_row_stages` of the holdout was not kept; record it to get a row-stage rate |
| Pinned host upload | 54.7 GB/s (38.0 GB/s at 288 lanes, 57.0 at 4,608) | Upload events; PCIe 5.0 ×16 is about 63 GB/s. No pageable rate is recorded; the CUDA segmented copies come from unpinned host vectors (no `cudaHostRegister` in the tree) |
| cuBLASLt or MMA GEMMs in TF32, FP16, BF16 or FP8 | Not recorded on this card | Only NVFP4 GEMMs and the FP4 product kernel have achieved rates; plan with at most 60% of the derived peak until a bounded sweep at receiver shapes measures one |
| Fixed cost per call | 8.752 µs matrix event against a 119.62 µs pinned staged call | 19 November fine projection |
| Vega packed FP16 direct dot | 450,758,880 complex terms in 0.0634 s, about 57 GFLOP/s at 8 flops per term, about 1.8% of the packed peak (*derived*); the earlier fused trial gave 47.0, 47.6 and 54.5 GF/s for FP32 direct, half-storage FP32 direct and packed direct | [Packed direct](validation.md#packed-direct-correlation-follow-through); the record does not separate traffic, scheduling, synchronization, instruction mix and clock as the limit |
| Vega explicit segmented timing | 0.22, 0.79 and 0.76 s per workspace against 0.20, 0.62 and 0.36 s on 12 CPU threads (`--segmented-benchmark`'s three shapes) | [Vega](gpu-precision-controls.md#vulkan-on-vega-integrated-graphics); indicative |

Use these rates in eligibility step 5 only at the same transform length,
backend and memory regime. The N = 32,768 rates do not transfer to
n ≤ 4,096, where L2 residency, tail batches and per-call cost dominate;
measure that n first.

## Eligibility procedure

Apply the steps in order to one stage. Stop at the first step that rules it
out and record why; that record is a finding, not a veto of other
formulations. Apply every step; the
[walk-throughs](#worked-eligibility-walk-throughs) show all ten on two stages.

0. **Which slots does it run on?** Name the qualification slots where the
   stage executes, from the "On challenging slots" column of the
   [stage table](#the-procedure-applied-to-each-stage). A stage that never
   runs on the 72 Hz/5× control, the holdout or the HF slot (the dense FP4
   search, template-bank layout and conditioning levers, the 47-mode fine
   projection) must name its non-segmented target plans and their share of
   service instead, and cannot be ranked by a challenging-slot share.
1. **Where does it run on the target build?** Read the
   [route table](triad-gpu-optimization.md#what-automatic-selection-runs).
   If the stage runs on the CPU there (drift, varying fine and signed-lag
   ranks on every CUDA build; segmented timing on CUDA SM89 and on every
   Vulkan device outside the SM 12.0 class), the first candidate is a device
   route or per-stage backend selection, not a new precision.
2. **Amdahl ceiling.** `s` = stage seconds ÷ slot seconds on each challenging
   slot; ceiling `1/(1−s)`. Use stage timers (`coarse_seconds`,
   `varying_coarse_seconds`, `signed_lag_gain.proposal_seconds`,
   `fine_seconds`, `drift_seconds`, `varying_fine_seconds`), not lane-summed
   sub-timers, and take stage and slot from the same record and host. A
   stage below about 5% of every challenging slot is a prerequisite or
   regression item, not a lead. The matched fixture never ranks first.
   Within a stage, bound the part a lever touches the same way: a free
   transform leaves the gain rank's exp epilogue, and a free ridge leaves
   drift's rendering.
3. **Output role.** Classify with the [exactness classes](#exactness-classes):
   proposal-only output that is rescored or certified (E1, E3) may use any
   format whose frontier keeps the true candidate; evidence-bearing output
   (E4) needs a budget (step 9); state and decisions stay native;
   pre-isolation work keeps its precision.
4. **Structure.** A contraction `[M×K]·[K×N]` with operand reuse and M, N of
   at least about 64 after batching can use matrix units. Elementwise work or
   a GEMV with N = 1 (rendering, warp, mapping, rotation), recurrences
   (simulator, Markov updates) and gathers without a shared operand (the drift
   ridge, per-peak rescoring) cannot. An FFT goes to step 5.
5. **Dense versus FFT.** For `m` retained outputs of an `n`-point complex
   transform, a dense DFT costs `n·m` complex multiply-adds, 8 real flops
   each, and a radix-2 FFT about `5·n·log2(n)` real flops (a chirp-Z
   transform about two FFTs of its padded length when its kernel spectrum is
   precomputed, as the varying route's is, and three otherwise). The ratio is
   `R = 1.6·m / log2(n)`. A dense form wins on arithmetic only if
   (achieved Tensor rate ÷ achieved FFT rate) exceeds R. On the PRO 6000 the
   FP4 products reach 1.14–1.16 PFLOP/s against 2.0–2.6 TFLOP/s for FP32
   cuFFT in the HF slot at N = 32,768, a ratio of about 440–580, and about
   145 against the isolated cuFFT rate (about 8 TFLOP/s); for FP16, FP8 or INT8 assume
   at most the same 60% of peak until measured. Full dense `n = 4,096` gives
   R ≈ 546, no margin even for FP4. The guides' earlier
   `0.4·n/log2(n)` (about 140× at 4,096) corresponds to counting a complex
   multiply-add as two operations; the conclusion is the same. Pruning
   outputs lowers R in proportion to m.
   - *Padded or chirp-Z transforms.* For `n_in` inputs, `m` outputs and a
     padded length `n_pad`: `R = 8·n_in·m / (c·5·n_pad·log2 n_pad)`, with
     `c = 2` when the kernel spectrum is precomputed and 3 otherwise.
     Varying projection: 200 inputs, 33 modes, a 512-point pair:
     52,800 ÷ 46,080 ≈ 1.15. Putting n = 512 into `1.6·m/log2 n` (5.9) is
     the wrong formula for a padded transform.
   - *Compare with the transform production runs.* The native CPU gain
     rank counted 6.46e6 FFT row-stages per HF block (`fft_row_stages`
     2,606,889,103,360 over 403,266 blocks), about 32 MFLOP, against
     22.3 M complex multiply-adds (178 MFLOP) for the dense per-lag DFT:
     R ≈ 5.5 against the native route (*inferred*). Those row-stages are
     already 256-point overlap-save tiles with 32-row blocks: 47 chunks and
     3,151 transforms per unsampled block reproduce the receipt's count
     exactly ([shapes](triad-gpu-optimization.md#signed-lag-ranks)). The
     device route uses the same tile and chunk capacity at HF (from the
     code), so R ≈ 5.5 holds against it too; `device_fft_row_stages` of an
     HF-shape run would confirm it.
     The 64-point per-lag DFT (R = 8.8 at 33 bins, 17 at 64) is the
     rescoring form.
   - *Direct correlation instead of an inverse FFT.* Segmented timing reads
     A lags of a correlation over r template rows per block, so a direct
     (Hankel) form costs `8·A·r` flops per frequency against one
     `n`-point inverse FFT: `R = 8·A·r / (5·n·log2 n)`, 0.0343·r at
     n = 32,768 and A = 10,553. The plan sizes `n = bit_ceil(r + A − 1)`
     (`make_segmented` in the [coarse receiver](../src/triad_receiver_coarse.cpp)),
     so r ≥ 5,833 at HF's n; its families have r ≈ 20,714 and 7,333, so
     R ≈ 710 and 252 (*derived*). The dense DFT of the product spectrum,
     `1.6·m/log2 n` with m = 10,553, gives R ≈ 1,126.
   - *Use the achieved FFT rate at the same n* ([achieved rates](#hardware-facts-for-decisions)).
   - *Coarse frontier.* Dense products per workspace and frequency are
     `4 × rays × arrivals × rows`; the FFT route grows with (rows +
     arrivals)·log(rows + arrivals) plus host bank preparation. Dense won
     the IC-7100 plan (3,377 arrivals, 44,966 rows: 27.1 s, 19.2 s with FP32
     partial rescoring, against 93.7 s) by removing host preparation, not by
     arithmetic. The `dense-cost` bound admits one-ray sessions up to the
     matched search's 1.21e9 products per workspace and frequency; the
     14,071-arrival × 187,194-row plan stays on the FFT route (dense would
     issue 3.7e17 products).
6. **Shape and batch.** Tensor kernels need wide work:
   - output tiles or blocks of at least 2–4 waves of the SMs (188, 170, 128,
     76 or 34); a 128×128 CUTLASS tile gives only 3 tiles at 288×96, and 864
     warps fill at most 9.6% of the PRO 6000's 9,024 resident warp slots;
   - widening the fine projection from 288 to 4,608 lanes raised the resident
     rate 3.5× (19.0 to 66.3 TFLOP/s) and cuBLASLt 15.5× (11.4 to 177.2);
   - for pack-inclusive library GEMMs, multiply-adds per packed operand
     element `M·N·K / (M·K + K·N)`: 683 won 1.49× with both packs (RTX 4090
     FP16, 2048×1024×4096), and 241 lost to FP32 (4096×256×512: 0.0636 ms
     cached and 0.1802 ms with packing against 0.0512 ms)
     ([history](https://github.com/mirage335-colossus/pumpModem/blob/933cdfe9/docs/validation-data/triad-v3/2026-11-01-consumer-gpu/README.md#L115));
   - in a hand-written MMA loop a 4-byte operand load cost about half a
     product's issue time; aim for at most 3 loads per 8 products, as the ring
     layout does;
   - instruction shapes: E2M1 `m16n8k64` with block scales (sm_120a only),
     FP16 `m16n8k16`, TF32 `m16n8k8`, INT8 `m16n8k32`; Vulkan cooperative
     matrices on the PRO 6000 report FP16 16×8×16 and 16×16×16, BF16
     16×16×16, FP8 E4M3 and E5M2 16×16×32, INT8 16×8×32 and 16×16×32,
     and no FP4
     ([probe](validation.md#receive-previews-and-clean-low-rate-admission--19-november-2026));
   - worked check, the varying lane-A projection: M = 33 modes pads to 48
     for `m16` (31% waste), or 66 real rows as 2×2 real blocks padded to 80;
     K = 200 or 239 rows pads to 208 or 240 for `k16`; N = blocks × 216
     fixed-affine template columns (54 states × 4 columns on the HF receipt,
     *modeled*; tracked states read shifted bins and need their own rows).
     With 48 × 128 output tiles and one tile per SM per wave, 2–4 waves of
     188 SMs need 376–752 tiles, N ≈ 48,000–96,000, that is 223–446 blocks
     per call. The HF per-block lease was never measured. Transferred from
     the 72 Hz/1× shape (about 23.1 MB per block in FP64, where the default
     256 MiB explicit lease admits 6 of 52 blocks), a batch halved until it
     fits ends at about 6–11 blocks: N = 1,296–2,376, 11–19 tiles, 6–10%
     of one wave; 223 blocks would need about 5.2 GB. So a call must batch
     hundreds of blocks: splitting one host batch into device calls and
     smaller per-block leases come before any matrix form.
7. **Roofline.** Arithmetic intensity `I` = flops ÷ unavoidable DRAM bytes.
   Below the ridge point the stage is bandwidth-bound: lower-precision
   arithmetic does not help, fewer bytes do (fusion, pruned outputs, half
   storage). A working set inside L2 (for example the one-ray IC-7100 bank,
   78 × 44,966 × 8 B ≈ 28 MB, against the Workstation Edition's 128 MB;
   *inferred*) is bound by latency and occupancy, not DRAM. Optimistic
   time = unaffected work + max(ops ÷ peak, bytes ÷ bandwidth) + necessary
   synchronization ([bound method](triad-performance.md#radeon-arithmetic-ceilings)).
   - Worked roofline, the stacked DFT-to-modes GEMM: each complex template
     element is read once and used in 33 complex multiply-adds (264 flops):
     33 flop/B with FP32 complex templates (8 B), 66 with FP16 (4 B),
     against ridges of 70 (FP32 SIMT) and 281 (FP16 Tensor). It is
     bandwidth-bound, so matrix units add nothing unless templates are
     rendered and contracted on chip (shared memory or registers) without a
     DRAM round trip, as the dense FP4 search's device bank is.
   - Tensor FFTs: on a bandwidth-bound stage a matrix-unit FFT can win only
     by removing DRAM passes (fused shared-memory radix stages) or bytes
     (FP16 storage); its arithmetic rate is irrelevant below the ridge. A
     transform factored as `n = n1·n2·…` with dense stage matrices costs
     about `8·Σn_i·n` real flops against `5·n·log2 n` (32 × 32 × 32:
     768·n against 75·n, about 10×). Check the baseline's DRAM passes
     first: isolated FP32 cuFFT at N = 32,768 timed like one read and one
     write of its batch, but that batch may have been L2-resident
     ([achieved rates](#hardware-facts-for-decisions)), so measure the
     passes with a cold-buffer timing or the kernel trace.
8. **Host traffic and per-call cost.**
   - Link bound: maximum rate = flops per batch × link bandwidth ÷ bytes per
     batch. The staged fine input moved 3,458,304 B for 169.87 MFLOP, which
     caps it at about 3.1 TFLOP/s at 63 GB/s however fast the kernel is.
   - Per call: device work per call at the achieved rate should be at least
     about 10× the fixed cost of a call (8.75 µs event, 119.6 µs staged pinned
     call, a Vulkan submission plus fence). Below that, batch, fuse, keep data
     resident or remove serialization first.
   - Serialization: a lane-summed timer far above stage wall × running lanes
     means lanes wait (mutex, single queue, phase barrier).
   - Residency before precision: host-prepared columns left the same FP4
     products' coarse stage at 1.6–1.8 s at 24 workers; the device bank cut
     the upload from 82,178,320 to 473,952 B per slot, and the stage now
     takes 0.020–0.024 s.
9. **Precision ladder and budget.** Try, lowest first, the formats the class
   allows: FP4 (block-scaled E2M1), INT8 or FP8, FP16 or BF16, TF32,
   split precision (TF32×3 about 2^-22 relative, BF16×3 about 2^-16, from
   the split widths; *inferred*), FP32 SIMT, FP64. Required guards:
   power-of-two scaling; the 1/16 Gram
   eigenvalue-ratio guard for two-ray reduced precision; bounded partial sums
   (INT8 at most 512 complex samples per INT32 partial, `1024·127² < 2^24`;
   FP32 partial sums of 64 rows added in FP64; K32 partials for FP8);
   subnormal limits (FP16 at 2^20 rows, about 0.14% of a row's L2 norm);
   nonfinite and range fallbacks.

   Formats (standard definitions; unit roundoff `u = 2^-(mantissa bits + 1)`):

   | Format | Mantissa bits | u | Largest; smallest normal | Scale | Accumulator here |
   | --- | --- | --- | --- | --- | --- |
   | E2M1 (NVFP4) | 1 | 2^-2 | 6; 1 (subnormal 0.5) | One scale per 8 complex samples (16 values), in hardware on SM120 | FP32 |
   | FP8 E4M3 | 3 | 2^-4 | 448; 2^-6 | Software, per block or tensor | FP32; FP16 at twice the rate on GeForce |
   | FP8 E5M2 | 2 | 2^-3 | 57,344; 2^-14 | Same | Same |
   | INT8 | — | Step = block maximum ÷ 127 | ±127 | Software, per block or partial | INT32, at most 512 complex samples per partial |
   | INT4 (Ada, RDNA 3) | — | Step = block maximum ÷ 7 | −8 to 7 | Software | INT32 |
   | FP16 | 10 | 2^-11 ≈ 4.9e-4 | 65,504; 6.1e-5 | Power-of-two scaling required | FP32 |
   | BF16 | 7 | 2^-8 ≈ 3.9e-3 | FP32 range | None needed | FP32 |
   | TF32 | 10 | 2^-11 | FP32 range | None | FP32; half the FP16 rate on the PRO 6000 |
   | TF32×3, BF16×3 | — | About 2^-22 and 2^-16 (*inferred*) | FP32 range | Split into high and low parts | FP32; three products |
   | FP32 | 23 | 2^-24 ≈ 6.0e-8 | 3.4e38; 1.2e-38 | — | FP32, or FP32 partials added in FP64 |
   | FP64 | 52 | 2^-53 ≈ 1.1e-16 | — | — | FP64 |

   Accumulation: with FP32 accumulators, input rounding dominates. A K-term
   sum of products of rounded operands errs by about `u_in·√K` times the
   RMS term for random-sign errors and at most `2·u_in·Σ|term|`; bounded
   partials (64-row FP32 sums added in FP64) keep the accumulator's own
   `u_acc·√K` small. Use a measured statistic or evidence error instead of
   `u` whenever one exists: the NVFP4 fine projection's projected modes had
   a worst per-lane relative RMS error of 0.148, yet its evidence stayed
   within 0.1% of native, because errors average over modes and blocks.

   Budgets:
   - E1: lower precision may only choose the frontier inside the existing
     exact certification, which falls back on `band-violated`,
     `frontier-overflow` or `near-tie`; a proposal-qualified (E3) form is
     not allowed for the gain rank, because its bit separation equals one
     map's evidence difference to about 1e-13. Check feasibility from the
     band before building ([worked check](#worked-eligibility-walk-throughs));
   - E3: the planted-candidate rule of the dense search (keep every arrival
     the exact frontier keeps at noiseless scores 24 and 16, at least 90%
     over the sweep down to 8) and identical decisions on the strata;
   - E4: the screen below, then measured evidence differences on the
     challenging strata. A worst-case bound above the margin is not a veto
     (the varying FFT bound summed over HF blocks gives 10–20 nats);
   - sensitivity: judge a measured dB loss proportionally to the computational
     gain (under 1 dB may be worthwhile, 1.5 dB for a multiple-times speedup,
     about 3 dB less acceptable), not by a fixed cutoff
     ([precision rules](gpu-precision-controls.md#automatic-mode-prioritizes-computational-ceiling)).

   **E4 screen: how to measure the margin.**
   1. Read from the `admission` receipt, in the `work` object of its
      `native_slot` phase: `decision`, `operational_log_e`,
      `operational_threshold`, `operational_passes_conditional_bound`,
      `operational_bit_evidence` (two values),
      `operational_bit_log_separation`,
      `operational_minimum_bit_log_separation` (5.293 at margin 3),
      `quiet_energy`, `quiet_threshold`, `varying_quiet_energy`,
      `varying_quiet_threshold`, `varying_bit_evidence`, `varying_blocks`,
      `varying_components`, `signed_lag_gain.bit_evidence` and
      `signed_lag_gain.top2_gap`; `result.policies` gives the stricter
      admission policies' decisions, whose thresholds count too where they
      matter.
   2. Binding margin: the smallest move that crosses any decision boundary
      (`classify_operational_scores` in the [receiver](../src/triad_receiver.cpp)).
      - *data or ambiguous with presence passing*
        (`operational_log_e ≥ operational_threshold`): `D` = the smaller of
        the separation distance `|separation − minimum|` and the presence
        margin `operational_log_e − operational_threshold`, in nats. An
        error `δ` in each bit's evidence can move the separation by up to
        `2δ`.
      - *quiet*: presence must stay failed, margin
        `operational_threshold − operational_log_e` in nats; `quiet_energy`
        (from the fine observation projection) must stay at or below
        `quiet_threshold`; and on time-varying slots `varying_quiet_energy`
        (from the varying quiet-aperture projection, `score_varying_quiet`)
        must stay at or below `varying_quiet_threshold`, or the slot turns
        ambiguous. The energies are sums of nonnegative terms, so use the
        relative margins `m = (threshold − energy) ÷ threshold` and compare
        them with relative errors directly.
      - *ambiguous with presence failing*: the presence and quiet-energy
        boundaries.

      Stored receipts:

      | Receipt | Decision | Presence: log-e / threshold | Separation / minimum | Energy margins (quiet, varying quiet) | Binding margin | Varying evidence, blocks |
      | --- | --- | --- | --- | --- | --- | --- |
      | HF −22.5, seed 940600, bit 0 | ambiguous | 152.0 / 11.0 (passes by 141.0) | 4.068 / 5.293 | Not a boundary (presence passes) | D = 1.225 nats (separation) | 182.2 / 178.2 over 516,614 |
      | HF −21, seed 940601, bit 1 | data | 114.8 / 11.3 (passes by 103.5) | 6.353 / 5.293 | Not a boundary | D = 1.059 nats (separation) | 138.6 / 145.0 over 362,534 |
      | HF −21, seed 940603, bit 0 | ambiguous | 103.1 / 11.3 (passes by 91.8) | 0.281 / 5.293 | Not a boundary | D = 5.012 nats (separation) | 132.2 / 132.4 over 362,534 |
      | Holdout input −12.9897 (explicit CUDA, earlier source) | quiet | −4.99 / 13.10 (fails by 18.1) | 1.652 / 5.293, not binding while presence fails | 743,568 / 744,161: 0.080%; 1,461,979 / 1,465,136: 0.215% | 0.080% relative (fine observation energy), 0.215% (varying quiet energy); presence 18.1 nats | 22.9 / 21.3 over 54,374 |
      | 72 Hz/5× control | quiet | Not kept | Not kept; the 20 November summaries give a two-value bit evidence 8.10 / 9.16 (field not named) | Not kept | — | Not kept |

      Sources: `local-evidence/hf-100mw-extended-20261118/admission-*.json`
      and `local-evidence/lowrate-2mw-runpod/100mw-36hz10x-target-m12p9897-seed940600-bit0.json`
      ([cost model](validation.md#cost-model-of-the-cpu-families) inputs).
      Record the same fields for the 72 Hz/5× control and the current
      holdout in the next validation entry. Two of the three challenging
      slots are quiet, so their energy margins, not bit separation, usually
      bind.
   3. Magnitude `E`: the net per-hypothesis evidence (about 180 nats at HF,
      22 at the holdout) is only a lower bound on the size of the terms
      before cancellation, `Σ|per-block term|`, which no receipt records.
      Add that counter before any screen can count as a pass. The quiet
      energies have no cancellation, so their own values are the magnitude.
   4. Screen: `u_eff·E ≤ f·D` for an evidence boundary and `u_eff ≤ f·m`
      for an energy boundary, with `u_eff` the measured relative error of
      the statistic (unit roundoff only when nothing is measured) and a
      predeclared fraction `f`, default 0.25. With a lower-bound `E` the
      screen can rank a format as likely too coarse but cannot pass one.
      Record one of four results, none of which is a fail or a stop:
      **excluded** (an evidence boundary exceeded with a lower-bound `E`:
      rank it last, try it only with a sensitivity trial); **not excluded**
      (below it: no pass until `Σ|per-block term|` is recorded); **inside
      budget** (an energy boundary, whose magnitude is known, met by
      `u_eff`); **measure first** (an energy boundary that `u_eff` nearly
      meets or exceeds).
      - HF −22.5, net E ≈ 182 nats, D = 1.225, f·D = 0.31: FP32 1.1e-5,
        FP16 0.09, NVFP4 at the fine projection's 0.1% (a different shape)
        0.18, BF16 0.71 nats. BF16 is excluded; FP32, FP16 and NVFP4 are not
        excluded. Counting both bits' errors (`2δ`), NVFP4 reaches 0.36 and
        is excluded under that count.
      - Holdout, quiet: f·m = 0.020% for anything that changes the fine
        observation energy and 0.054% for the varying quiet energy. FP32
        (about 1e-7) is inside budget; FP16's unit roundoff (0.049%, roughly
        doubled by squaring) and NVFP4's 0.1% are not, so both are measure
        first: measure the energy error of a lower-precision varying projection on quiet slots
        before ranking it. Presence (f × 18.1 = 4.5 nats against u·E of at
        most 0.09) does not bind.
      - A 1.8e5-nat component fixture says nothing here: its margin is not
        recorded.
   5. The screen only orders formats. Measured changes within `f` of the
      binding margin on every challenging stratum are a numerical
      diagnostic, not a sensitivity result. A change above it does not stop
      the candidate: it triggers a native rerun of that slot or a
      predeclared complete-receiver sensitivity trial that weighs the
      measured dB loss against the measured gain on the challenging strata
      ([precision rules](gpu-precision-controls.md#automatic-mode-prioritizes-computational-ceiling)).
      Keep the per-block fallbacks of the E4 gate in every case.

### The procedure applied to each stage

Each verdict starts with one Tensor label: **already Tensor** (a matrix
route runs), **no** (the structure rules matrix units out), **unpromising**
(eligible, but the bound or class leaves under about 0.1 s or under 5%),
**uncertain pending** a named measurement, or **not reached** (never runs
on a challenging slot).

| Stage, shape | Runs now | On challenging slots | Class | R and bound | Verdict and next lever |
| --- | --- | --- | --- | --- | --- |
| Coarse frontier products: `4 × rays × arrivals × rows` real products per workspace and frequency (matched: 8 × 3,377 × 44,971 = 1.21e9, which is 1.42e12 ÷ (78 × 15)) | Dense FP4 on the SM 12.0 Vulkan/OpenCL route; never on CUDA builds | No. Segmented sessions withhold the bank (`varying-passband`, `midpoint-gain`); the 72 Hz/5× control scored `opencl_coarse_score_rows` 0 | E3 | Step 5's coarse-frontier rule; products at about 61% of peak; on large one-ray plans the products (11.35 s) and rescoring dominate | **Already Tensor; not reached** on challenging slots. Default. Next: [lever 11](triad-gpu-optimization.md#next-levers-in-order) on large non-segmented plans; to reach challenging slots, device synthesis of varying-passband sessions ([map](#tensor-core-opportunity-map-for-the-challenging-slots)). |
| Fine 47-mode projection: 1,500 rows to 47 modes, real `288 × 96 × 3072` per submission | Resident NVFP4 on SM 12.0; cuFFT with fused gather on CUDA SM120; ATen FFT and gather on SM89 | No: only the isolated 1,500-row, 47-mode, two-path geometry | E4 | R ≈ 7.1 (*inferred*: `1.6·47/log2 1,500`, an unpadded n; 6.8 at n = 2,048, and 5.0/c by the padded formula), far below the achieved Tensor/FFT ratio, so dense wins | **Already Tensor; not reached.** Default. Remaining cost is owner work and the window wait ([remaining fine costs](triad-gpu-optimization.md#remaining-fine-costs)), on the easy slot only. |
| Segmented timing: per block one forward FFT, then per tile of up to 128 frequencies a product kernel, an in-place inverse FFT and a ranks kernel; 16,384 and 32,768 points; 10,553 of 32,768 lags read at HF | CUDA FP32 cuFFT on SM120; Vulkan inside the envelope on SM 12.0; native elsewhere | Yes: 1,626 s of HF (44%); 29–32% of the 72 Hz/5× control (20 November); part of the holdout's 110 s | E3 (proposal-level ranks: the E4 screen and the varying evidence values do not apply; FP4 and FP8 are allowed by class under the planted-candidate and identical-decision gates) | Dense DFT of the product spectrum R ≈ 1,126; direct (Hankel) correlation R ≈ 710 and 252 for HF's two families (step 5), dominated ([rejected](#rejected-or-stopped)); about 1.6–4.7 flop per byte depending on passes, far below the FP32 ridge of 70. Isolated cuFFT costs 0.30 µs per 32,768-point transform (possibly L2-resident), which would be about 400 s at HF; in the slot 0.95–1.26 µs | **Uncertain pending the kernel trace** (a fused factorization only). Attribute the 3–4× in-slot penalty first with a kernel trace ([worked example](#worked-examples)). Then per-call levers (launches, synchronization, uploads, graph replay) or traffic levers (fused load, pruned output, shared column spectra over a block's frequency shifts, 15,733 in HF's widest family and about 7,019 per block on average, FP16 storage), whichever dominates; widen the envelope ([levers 5 and 6](triad-gpu-optimization.md#next-levers-in-order)). |
| Signed-lag direct rank: per HF block the native rank counts 6.46e6 FFT row-stages in 256-point overlap-save tiles (3,151 transforms per unsampled block; 16,384-point spans only on the 256 sampled blocks), and the device rank uses the same tile; a dense form needs a 64-point DFT at 33 bins per lag, 22.3 M complex multiply-adds per block, 403,266 blocks, 2.8e11 exp cells | Vulkan FP32 on SM 12.0; native on CUDA builds and other devices | Yes. HF: 386 s native on the CUDA build (10%). Vulkan route: 3.05 s of the holdout (1%), 1.34–1.47 s of the 72 Hz/5× control (7%) | E1 | R ≈ 5.5 against the native rank's counted FFT work, and against the device route, which uses the same T = 256 tile at HF (from the code; `device_fft_row_stages` of an HF-shape run would confirm it); 8.8–17 applies only to a per-lag rescoring form; the per-cell exp epilogue is about 7 of 12 CPU-ms per HF block (*modeled*) and no DFT removes it; unfused four kernels move about 10 TB at HF (*modeled*) | **Unpromising** (E1: split forms only). CUDA builds: a device route or per-stage selection ([lever 7](triad-gpu-optimization.md#next-levers-in-order)) first. Vulkan route: explain why automatic is 2× slower than explicit (lever 9 row), then the fused FP32 kernel (lever 10). Tensor DFT: under E1 only split forms can certify ([walk-through](#worked-eligibility-walk-throughs)). |
| Drift ridge: a sheared sum over a 1,762 × 16,384 FP64 table per lane | Native everywhere (device explicit only) | Yes, inside drift: HF 899 s, holdout 60.9 s, 72 Hz/5× 2.18–2.22 s | E3 | No contraction (a gather); DRAM and cache bound | **No.** About 15% of drift lane-seconds on the 5650U 72 Hz/1× control; the HF ridge share is assumed to be 85–95%, which implies about 63 ns per load (receipt seconds ÷ a modeled load count, not timed); that latency was not reproduced (5–12 ns on the Xeon), so run the ridge benchmark on an EPYC 9555-class CPU first ([lever 2](triad-gpu-optimization.md#next-levers-in-order)). Slope blocks of 16, in the tree since 21 November, cut a load from 6.4–6.8 to 0.45 ns on one 5650U thread and ran 13–14× faster per thread on the Xeon (*indicative*). |
| Drift dechirp and slope FFTs: 2^21–2^25 points | Native; explicit FP32 VkFFT device route is slower | Yes, inside drift | E3 | Dense DFT impossible: `drift_peaks` keeps a stable top eight over all public bins and the ridge reads the whole power table, so `m = n`. A factored matrix-unit FFT is not analysed (map row). FFTs are 15–30% of drift CPU time (*modeled*), so FFTs alone give at most 1.43× | **No** (dense); factored form not a lead until lever 2. Per-lane sessions, batched atoms, device rendering, one observation upload (lever 2). FP16 or BF16 slope FFTs are eligible behind an FP64 tie guard (untried; they halve bytes only). |
| Drift render and products | Host, FP64 | Yes, inside drift: over half of drift lane-seconds, 51–67% on the 5650U at both 72 Hz shapes | E3 input | Elementwise | **No.** Device rendering with FP32 per-row arithmetic and FP64 or integer coordinates (lever 2). |
| Varying projections: 200 or 239 rows to 27–33 modes per block, 516,614 HF blocks; lane A has 216 fixed-affine template columns per HF block (*modeled*) | Native FP64; explicit FP64 device route is slower | Yes: HF 401 s (11%), holdout 89.9 s (32%), 72 Hz/5× 5.32–5.47 s (29%) | E2 now; `fp32-template` would be E4 | R ≈ 1.15 against the 512-point chirp-Z pair (step 5). Executed varying CPU by lane: fixed-affine 65%, raw tracker 23%, independent gain 11%; by operation: FFTs 84–93%, the rest 7–16%, which at the 84% end is Markov 9%, rendering 4%, Gaussian 3% (*modeled* at 1.25 ns per FP64 row-stage, 2.5 ns per rendered basis row, 80 ns per Markov chain-state). On the device: per-call, occupancy and upload bound (562 GB FP64 per HF slot, *modeled*) | **Unpromising** on all three slots. [Lever 3](triad-gpu-optimization.md#next-levers-in-order) in its order. The stacked GEMM shares one DFT-to-modes matrix `[33 × 200]` across blocks and fixed-affine templates, but it is bandwidth-bound (step 7) and its 5.9e12 HF flops take about 0.05 s at the FP32 peak, so matrix units add under 0.1 s ([walk-through](#worked-eligibility-walk-throughs)). |
| Affine frequency search (CUDA) | Half cuFFT on SM89 and SM120 inside its gate; FP32 cuFFT otherwise | HF: inside the 208 s other coarse, declined as `cuda-fp16-fft-unqualified-shape` | E3 | Gate: 4,096–2^20 rows, at least two arrivals, Gram ratio at least 1/16, work volume at least 2^19; HF's `correlation_fft_rows` is 16,777,216. Half cuFFT is packed-half SIMT (`HADD2`, `HFMA2`, `HMUL2`) and half storage, not Tensor work | **No** (a G route). Qualifying longer transforms needs block-floating scaling against the subnormal bound ([lever 7](triad-gpu-optimization.md#next-levers-in-order)). |
| CUDA lag-group matrix: up to 4,096 complex rows (32 workspaces × 64 arrivals × 2 taps), K ≤ 8,192 | INT8 automatic within its gate; FP16 explicit | Zero IMMA and HMMA calls on the HF and holdout-input CUDA receipts (RTX PRO 6000 CUDA builds) (`cuda_matrix_lt_imma_calls`, `cuda_matrix_lt_hmma_calls`); the 72 Hz/5× control was not run on CUDA | E3 | A different algorithm from the FFT; packing and workspace bound | **Already Tensor; not reached.** Keep the gate; the RTX 4090 backlog regressed 3.47% ([worked example](#worked-examples)). |
| Rendering, warp and mapping | FP32 per-row kernels with FP64 or integer coordinates on SM 12.0 | Only for dense and resident sessions, so no | E4 | Elementwise; FP64 SIMT at 1/64 was the bound | **No.** Done on SM 12.0; the module is `sm_120a` PTX, so Ada needs a port, which pays only together with a ported dense search or fine route. |
| Fine Grams and statistics: 1×1 and 2×2 Grams | Native FP64; CUDA direct FP64 | Yes, small | State-adjacent | About 0.3% of fine worker time | **No.** Stopped: an FP16 Gram took 184 µs against 119 µs batched and 33 µs direct FP64. |
| Gaussian updates, Markov recursions, decisions | Native | Yes | State | Recurrences | **No.** Never reduced. A linear-domain scaled HMM recursion is a CPU candidate for the Markov part. |
| Simulator | CPU | Outside the slot (HF: 404 s beside a 3,706 s slot) | Generator | Recurrences, no contraction | **No.** Outside receiver service; `--capture-cache` removes it from repeated runs. |
| Helper and raw composite FIRs | Native | Yes | Pre-isolation | — | **No.** Not eligible. |

### Formats tried by stage

"—" means not tried and not proposed.

| Stage | FP4 | INT8, FP8 | FP16, BF16 | TF32, split precision | FP32 |
| --- | --- | --- | --- | --- | --- |
| Coarse frontier (Vulkan/OpenCL) | Default (dense E2M1); not reached by segmented sessions | Untried; the Ada port candidate (INT8, FP8 or INT4 with software block scales) | Untried | Untried | FP32 partial rescoring default |
| Frequency search of varying-passband sessions | Not reached: bank withheld | — | — | — | Vulkan FP32 FFT (default) |
| Coarse lag matrix (CUDA) | Removed: 5.03% score error | INT8 automatic within its gate; FP8 only historical (K32 partials) | FP16 explicit (HMMA) | Split-BF16 and TF32×3 untried | FFT default |
| Affine broad search (CUDA) | — | Not applicable: an INT8 contraction is not an FFT | Half cuFFT automatic within its gate: packed-half SIMT and storage, not Tensor | — | cuFFT fallback |
| Lag-domain DFT (historical Triton) | — | — | Scaled FP16: transforms 1.33–1.88× faster, acquisition slower | TF32×3 0.78×, rounded TF32 1.08×, whole trace 1.89% slower | ZoomFFT kept |
| Segmented timing | Allowed by class (E3); dense DFT and direct Hankel forms dominated at HF shapes | Same | Factored HMMA inverse 1.87–4.01× slower; FP16 storage untried | Direct Hankel GEMM 3.5–10× slower than the in-slot FFT at 60% of the TF32 peak (*derived*) | Default (cuFFT, VkFFT) |
| Signed-lag rank | Excluded: E1 band would exceed the frontier (*inferred*) | Excluded, same reason | Excluded under E1: the band grows about 8,000× (FP16) and 65,000× (BF16) and overflows the 64-lag frontier (*inferred*) | TF32×3 and BF16×3 could certify (*inferred*); their peaks are 0.67× and 1.3× FP32 SIMT on the PRO 6000, so re-evaluate on achieved rates after the fused kernel | Default device route (E1) |
| Drift FFTs | Dense DFT impossible (`m = n`) | — | FP16 or BF16 storage eligible behind an FP64 tie guard; untried | — | Explicit device route, slower |
| Varying projections | Not excluded by the lower-bound HF screen at the fine projection's 0.1% (a different shape); measure first against the holdout's 0.054% varying quiet-energy budget; unmeasured at 200/239 rows | — | FP16 not excluded on HF, measure first against the holdout's quiet-energy budget; BF16 excluded by the HF screen (rank last); untried | — | `fp32-template`, no kernels yet |
| Fine 47-mode projection | Default (resident NVFP4) | — | — | — | Resident FFT control; cuFFT on CUDA SM120 |
| Fine Grams | — | — | FP16 Gram stopped (184 against 33 µs direct FP64) | — | FP64 kept |
| Rendering and mapping | Not eligible (elementwise) | — | — | — | Default (`fp32-rn4-v1`) |
| Vega direct correlation | — | No INT8 dot hardware | Packed FP16 default in its envelope; VkFFT FP16 storage | — | FP32 direct fallback |

### Worked eligibility walk-throughs

Every step on two stages of the challenging slots. Copy this form for a
new stage; a skipped step is the usual way a candidate is mis-ranked.

| Step | Varying lane-A projection | Signed-lag gain rank |
| --- | --- | --- |
| 0. Slots | All three: HF 401 s, holdout 89.9 s, 72 Hz/5× 5.32–5.47 s | All three. HF: 386 s native on the CUDA build. Vulkan route: holdout 3.05 s, 72 Hz/5× 1.34–1.47 s |
| 1. Route | Native on every build; the explicit FP64 Vulkan route was slower on the Xeon at W = 80 (3.16–3.60 against 0.18–0.30 s; 13.2 against 4.97–5.23 s). First a device route that wins (lever 3), not a format | CUDA builds have no route: for HF, lever 7 comes first. SM 12.0 Vulkan: the FP32 device rank is the default |
| 2. Amdahl | Holdout 32% (1.47×), 72 Hz/5× 29% (1.4×), HF 11% (1.12×). Lane A is about 65% of varying CPU (*modeled*), so a lane-A-only lever caps the holdout at about 1.26× | Vulkan route: 1% and 7% (1.01× and 1.08×), so a prerequisite item, except the automatic-versus-explicit gap (about 0.7–0.9 s, up to about 5% of the 72 Hz/5× slot). HF on CUDA: 10% (1.12×) of native CPU time, which a Tensor DFT cannot address: it reaches HF only after lever 7, where the device rank models at about 29 s (under 1%). Within the stage the exp epilogue is about 7 of 12 CPU-ms per HF block (*modeled*): a free transform gives at most about 1.7× on the CPU |
| 3. Class | Evidence-bearing: E2 (FP64 device) or E4 (`fp32-template`) | E1: decision-identical publication. E3 (similar lags) is rejected: bit separation equals one map's evidence difference to about 1e-13 |
| 4. Structure | `[33 × 200]·[200 × N]` with one DFT-to-modes matrix shared across blocks and fixed-affine templates; lane B (tracked states) reads shifted bins, lane C needs full DFTs | Per lag a 64-point DFT at 33 bins of that lag's products: `[33 × 64]·[64 × L]`, L = 10,553 lags, one matrix shared across lags and blocks |
| 5. Dense versus FFT | R ≈ 1.15 against the 512-point chirp-Z pair (`c = 2`); both forms are small | R ≈ 5.5 against the native rank's counted FFT work (*inferred*), which is already 256-point overlap-save tiles (3,151 transforms per unsampled HF block). The device route uses the same tile at HF (from the code), so the same R applies. 8.8–17 applies only to a per-lag rescoring form |
| 6. Shape | M 33 → 48, K 200 → 208, N = blocks × 216. Two to four waves need 223–446 blocks per call; at the 72 Hz/1× shape's 23.1 MB per block the 256 MiB lease admits about 6–11 (N = 1,296–2,376, 6–10% of a wave); the HF per-block lease is unmeasured | M 33 → 48, K = 64, N = 10,553 per block, bit and frequency: wide enough |
| 7. Roofline | 33 flop/B (FP32 templates), 66 (FP16) against ridges 70 and 281: bandwidth-bound; matrix units help only with on-chip rendering | 33 flop/B if the products are stored; they can be formed on chip in a fused kernel. The per-cell exp epilogue stays SIMT work |
| 8. Host traffic, per call | 562 GB of FP64 templates per HF slot (*modeled*), about 10 s at 54.7 GB/s; FP32 halves it, device rendering removes it. At 6–11 blocks a call lane A is about 68–125 MFLOP, 0.5–1.0 µs at the FP32 peak, far below 10 × 8.75 µs: per-call bound | Staged inputs of about 1 KB per unsampled HF block; the unfused four kernels move about 10 TB at HF (*modeled*): device-traffic bound, so the fused kernel comes first |
| 9. Precision | `fp32-template` first: FP32 is not excluded on HF (1.1e-5 against f·D = 0.31 nats) and is far inside the holdout's quiet-energy budget (about 1e-7 against 0.054%). FP16 is not excluded on HF but is measure first against the holdout budget; BF16 is excluded by the HF screen (`u·E` ≈ 0.71 nats against `f·D` = 0.31). The gain rank's band argument does not apply here. Then measure the evidence and quiet-energy changes | Bands scale with relative error (*inferred*). NVIDIA oracle HF bands 4.0e-3 and 2.3e-3, top-two gap 3.47 on bit 1, frontier = lags within 2·band of the best plus the top 8, at most 64. FP16 or TF32 (about 8,000× FP32): bands about 33 and 19, so 2·band far exceeds the gap and the frontier overflows; BF16, FP8, INT8 and FP4 are worse. TF32×3 (about 4×) certifies easily; BF16×3 (about 256×): 2·band about 2.0 and 1.2, within the 3.47 gap by a factor of about 3. Only split forms qualify, at peaks of 0.67× and 1.3× FP32 SIMT |
| Test | P1 `test_triad_vulkan --varying`; P2 on the holdout and 72 Hz/5× control (both quiet): relative change of `varying_quiet_energy` and `quiet_energy` at most 0.25 × their relative margins (0.054% and 0.020% on the holdout) and \|Δ `operational_log_e`\| at most 0.25 × the presence margin, with per-block fallbacks; on HF data or ambiguous receipts \|Δ separation\| ≤ 0.25·D; a change above budget triggers a native rerun or a predeclared sensitivity trial, not a stop; P3 no declines or fallbacks on explicit runs; P4 stage faster in both pairs on both slots, and neither slot slower beyond the predeclared noise band; P5 flipped automatic runs identical | P1 `--gain-rank`; P2 identical lags and evidence, `band_ratio` below 1; P3–P5 as for varying |
| Verdict | G lever: batching, leases, rendering on the device. Matrix units add under 0.1 s at HF | G levers: lever 7 on CUDA, the gap row (lever 9), the fused kernel (lever 10). A Tensor DFT only after fusion, and only in a split form |

## Strategy catalog

Status words: **default** (automatic route), **explicit** (option only),
**conditional** (kept under a stated condition), **rejected**, **historical**
(Git only), **proposed** (ranked, not built), **untried** (documented
candidate, no lever yet). Times are from the linked records on an RTX PRO
6000 unless named.

### Defaults

| Strategy | Effect, hardware | Why it worked | Reconsider or extend | Detail |
| --- | --- | --- | --- | --- |
| Dense FP4 frontier search with a device template bank | Matched slot 2.2–2.5 → 0.49–0.82 s when introduced; upload 82,178,320 → 473,952 B | Removed host bank preparation (59–90% of a slot) and made the products a contraction; exact FP32 rescoring keeps every used score | Large plans: [lever 11](triad-gpu-optimization.md#next-levers-in-order) | [Dense search](triad-gpu-optimization.md#dense-fp4-coarse-search-and-device-template-bank) |
| Ring layout of the FP4 product kernel | 426 → 1,215–1,223 TFLOP/s | 16 loads per 4 products became 3 per 8 | Two workspace pairs per block | [Throughput](triad-gpu-optimization.md#fp4-product-throughput) |
| One-ray dense admission within `dense-cost` | 36 Hz Spread None coarse 0.19–0.42 → 0.012–0.013 s; IC-7100 plan 94 → 27 s | Same products without the FFT route's host preparation | Replace the bound by an FFT cost estimate from `correlation_fft_rows` | [One-ray admission](triad-gpu-optimization.md#one-ray-admission-and-dense-cost) |
| FP32 partial rescoring (64-row FP32 sums added in FP64) | 2.64 → 0.89 ms per matched slot; IC-7100 27.1 → 19.2 s | FP64 SIMT runs at 1/64 rate | — | [Dense search](triad-gpu-optimization.md#dense-fp4-coarse-search-and-device-template-bank) |
| FP32 per-row mapping and rendering with FP64 or integer coordinates | Mapping 24.3 → 5.1 ms, tile rendering 6.0 → 2.5 ms | Same reason | Port to `sm_89` for Ada | [Where FP4 fits](triad-gpu-optimization.md#where-fp4-fits-and-the-larger-levers) |
| Warp fused into the FIR; lane-independent restructure | 55.9 → 8.7 ms; lane rendering 6.9 → 1.9 ms; dispatches 34 → 22 at 24 workers (49 → 37 at four) | Removed stored raw columns and repeated FP64 work | A smaller stage with more resident blocks (untried) | [Remaining costs](triad-gpu-optimization.md#remaining-costs-after-the-dense-search) |
| Resident NVFP4 fine projection, device tiles, windows of 16 | Fine 0.55–0.60 → 0.08–0.11 s; uploads 92 → 7.6 MB; submissions 405 → 51. The earlier resident route: 1.483 → 0.526 s, uploads 2.798 GB → 74.76 MB | Descriptors instead of mapped columns; fewer round trips | Windows above 16 did not help | [Device tiles](triad-gpu-optimization.md#device-rendered-fine-tiles-and-resident-windows) |
| Vulkan segmented timing inside the envelope (SM 12.0 class) | 72 Hz/5× stage 45.7–46.3 → 5.7–6.0 s, slot 59.0 → 18.4–20.5 s | FFT work moved off the CPU | Envelope widening ([lever 5](triad-gpu-optimization.md#next-levers-in-order)) | [Segmented route](gpu-precision-controls.md#current-implementation) |
| Signed-lag gain rank on the device (E1) | Holdout slot 688 → 281 s, stage 415 → 3.05 s. 72 Hz/5× slot 43.7–55.5 → 17.8–19.8 s; stage 26.7–32.6 → 0.57–0.68 s explicit (native → device), 1.34–1.47 s on the flipped automatic route | Large transform work per call; native rank scaled badly at W = 80 | Fallback rate or `band_ratio` near 1 | [Gain rank](triad-gpu-optimization.md#blackwell-qualification-of-the-gain-rank) |
| CUDA FP32 cuFFT segmented timing (SM120 only) | 2.38× and 3.74× full-slot in paired severe trials | Same as Vulkan | Qualify on SM89 | [Dispatch](triad-gpu-optimization.md#hardware-and-library-dispatch) |
| CUDA half cuFFT broad search (SM89, SM120, gated) | 340.82 → 213.36 ms (1.597×) on one broad case; neutral (0.979) at NFFT 131,072 with workspace 7.57 GB → 581.7 MB; a 63 MiB message stream 0.72% slower overall, ready interval 1.109× faster | Half arithmetic and storage halve traffic | Longer transforms ([lever 7](triad-gpu-optimization.md#next-levers-in-order)) | [CUDA record](validation.md#current-cuda-precision-and-tensor-dispatch) |
| CUDA INT8 lag matrix (automatic within its gate; FP16 explicit) | Full-stage FP16 0.407/0.372 → 0.328/0.308 s and INT8 0.306/0.336 → 0.251/0.263 s (active/noise); summed coarse in a 25-slot backlog 4.45 → 3.69 s (PRO 6000) | Prepared operands and cuBLASLt IMMA kernels | RTX 4090 backlog 3.47% slower ([worked example](#worked-examples)); reserved/live workspace with three owners 533/364 MB on the PRO 6000 and 438/222 MB on the RTX 4090, against 107/20 MB for the FFT | [CUDA record](validation.md#current-cuda-precision-and-tensor-dispatch) |
| CUDA exact arrival-peak frontier on the device | 40-byte frontier per workspace and frequency instead of a full FP64 row | Less readback | — | [Matrix processing](gpu-precision-controls.md#stage-boundaries-and-optional-matrix-processing) |
| Host and device overlap (CUDA FIR fill, fine observation work one dispatch ahead) | RTX 4090: large workload −10.39%, fresh seed −13.61%, failed Helper −3.58%, holdout ready interval −11.76%, the original small ready interval +5.77% (slower). PRO 6000: large workload −13.9%, fresh seed −22.0%, the original small backlog +5.7% whole stream and +14.6% ready interval (slower), a fresh holdout −13.6% whole and −9.53% ready. Mixed, inconclusive for backlog | CPU preparation hidden behind device work | Overlap with segmented timing ([lever 8](triad-gpu-optimization.md#next-levers-in-order)) | [Overlap](validation.md#current-receiver-overlap-qualification) |
| Vega packed FP16 direct correlation (RADV, PCI `1002:1638`, one ray, one arrival, at most 131,072 rows) | Dot 2.88× (0.0634 against 0.1824 s); slot 1.027× | Real `v_pk_*_f16` arithmetic; the dot was a small slot share | Extend to small arrival counts | [Vega](gpu-precision-controls.md#vulkan-on-vega-integrated-graphics) |
| Vega FP32 and FP16-storage VkFFT frequency search | Broad coarse phase 1.269× against the historical FP64 CPU path, not the current automatic CPU; against the then-current automatic CPU the FP32 path was 1.148× on service (1.066× on full invocation); a narrow case 39.6% slower | Device FFT on broad work | — | [Vega](gpu-precision-controls.md#vulkan-on-vega-integrated-graphics) |
| AVX2 CPU fine route for one-tap shapes of at most 1,024 rows and 40 modes on CUDA builds | CPU about 4–6× faster than the GPU on sampled 750- and 1,023-row clears (the record does not name the device) | Small transforms lose to launch cost | — | [CUDA record](validation.md#current-cuda-precision-and-tensor-dispatch) |

### Explicit or conditional

| Strategy | Effect, hardware | Why | Reconsider when | Detail |
| --- | --- | --- | --- | --- |
| Drift device route, FP32 VkFFT (E3) | Slower: 0.36 → 0.81–1.65 s (72 Hz/1×), 2.23–2.26 → 2.96–3.28 s (72 Hz/5×), Xeon W = 80; close to native on a 13.6-CPU pod | The latter control recorded 4.21–4.32 accumulated lane-seconds of session lock wait, 0.49 s of device commands and about 1.00 GB of logical readback | After [lever 2](triad-gpu-optimization.md#next-levers-in-order), or timed at the holdout or HF shape | [Drift](triad-gpu-optimization.md#drift-proposals) |
| Varying device route, FP64 (E2) | Older routes lost to native. Cached CPU staging reduced the later explicit 72 Hz/5× phase from 27.14–27.47 s to 2.05–2.31 s on a full Blackwell at W = 30; native took 2.49 s | Host reads of freshly rendered templates in mapped memory dominated. The 172-block batch, logical traffic and device arithmetic were identical after the repair | Wider service, owner-memory and backlog qualification before changing automatic selection | [Attribution and paired repair](validation.md#vulkan-fine-stage-attribution-and-staging--21-november-2026) |
| Vulkan segmented outside the envelope (72 Hz/1× shapes) | Stage 4.58 → 0.59 s; slot 5.58–6.61 → 2.61–2.97 s | Device FFTs | Qualify near-tie proposal changes (2 of 8 changed on the Radeon, clock siblings 0.0003 ppm apart) | [Lever 5](triad-gpu-optimization.md#next-levers-in-order) |
| Radeon explicit segmented timing | Single 72 Hz/1× slot 10% slower but 29 against 258 process CPU-s | Shared DDR4; CPU faster at 32k | Multi-owner backlog, or a deterministic CPU/GPU split of workspaces | [Vega](gpu-precision-controls.md#vulkan-on-vega-integrated-graphics) |
| Radeon gain rank | Declined; about 2 ms per HF oracle block (*indicative*) | Unfused form is memory-bound on Vega | After a fused local-memory kernel | [Ranks](triad-gpu-optimization.md#signed-lag-ranks) |
| CUDA `--cuda-segmented-timing on` on SM89 | The explicit request is accepted on any CUDA device (code); unmeasured on SM89 | — | Qualify on the 72 Hz/5× control and holdout | [Ada guidance](#rtx-4090-rtx-4090-laptop-and-rtx-4060-ti-16-gb) |
| Global warp table instead of the register stash | Byte-identical; 18 MB per map | Stash fits registers (235 registers, no spills) | If the stash spills | [Remaining costs](triad-gpu-optimization.md#remaining-costs-after-the-dense-search) |
| Resident copies of signed-lag inputs | Host packing not measurable; 0.3 GB more | — | If packing shows on a fast device | [Ranks](triad-gpu-optimization.md#signed-lag-ranks) |
| Native fine processing on CPU-rich hosts | Resident fine faster on the 20 November host; order reversed between hosts at 24 workers | Host-specific | Matched comparison on a CPU-rich host | [Precision guide](gpu-precision-controls.md#automatic-mode-prioritizes-computational-ceiling) |
| Counter-based GPU simulator | Changes every capture hash | Outside receiver service | With another capture-changing change | [Lever 12](triad-gpu-optimization.md#next-levers-in-order) |

### Rejected or stopped

| Strategy | Outcome, hardware | Why it failed | Reconsider when | Detail |
| --- | --- | --- | --- | --- |
| Factored three-stage HMMA inverse FFT (cuBLASLt FP16, FP32 accumulation) for segmented timing | 1.87–4.01× slower than FP32 cuFFT (SM120): 73.70 against 18.40 µs (128 × 16,384) and 138.21 against 38.88 µs (128 × 32,768), including packing, twiddles and reordering; the factorization and pass count were not recorded | Each stage round-trips global memory, while the isolated cuFFT baseline timed like one read and one write per batch (possibly L2-resident); dense stages cost about `8·Σn_i·n` flops | A fused shared-memory factorization that removes DRAM passes, timed with full and tail batches and all preparation. tcFFT reported 1.10–3.03× on A100 (GPL-3.0: evidence, not code to copy) ([dispatch](triad-gpu-optimization.md#hardware-and-library-dispatch)) | [Dispatch](triad-gpu-optimization.md#hardware-and-library-dispatch) |
| Native cuBLASLt FP4 coarse lag-matrix analogue | 5.03% score error against a 1% gate; repairs changed a unique noise peak | Single FP4 products too coarse for scores the receiver used | A design where FP4 only proposes (as the dense search does) | [Matrix processing](gpu-precision-controls.md#stage-boundaries-and-optional-matrix-processing) |
| TF32×3 and rounded TF32 lag-domain DFTs (Triton, PRO 6000) | Isolated 0.78× and 1.08× of ZoomFFT; whole trace 1.89% slower; DATA acquisition 0.935 → 1.23 s | Dense DFT work against an efficient FFT | Smaller retained-output counts | [Lag-search conclusion](validation.md#tensor-core-lag-search-conclusion) |
| Scaled FP16 lag-domain DFT | Isolated 1.33–1.88× faster; acquisition 3.543 → 4.103 s | Surrounding costs rose | Measure surrounding costs first | [Retained lessons](gpu-precision-controls.md#retained-lessons) |
| FP16 Gram for small fine Grams | 184 µs against 119 µs batched FP64 and 33 µs direct FP64 | Tiny matrices; Grams are 0.3% of fine time | Large batched Grams only | [Qualification limits](gpu-precision-controls.md#existing-qualification-and-remaining-limits) |
| Dense FP4 forms of FFT-structured stages | R ≈ 546 at n = 4,096 | Dense arithmetic growth | Small n or pruned outputs, proposal-only | [Eligibility step 5](#eligibility-procedure) |
| Direct time-domain (Hankel) correlation GEMM for segmented timing: per phase M = A lags, K = r rows, N = frequencies of that phase (*derived*, not built) | At HF (r ≈ 20,714 and 7,333) `8·A·r` flops per frequency, about 1.7e18 in all: about 1,450 s even at the measured 1.15 PFLOP/s FP4 rate, against the 1,626 s stage. Per frequency at 60% of the derived peaks: TF32 4.4–12.4 µs, FP16 2.2–6.2 µs, FP4 0.54–1.52 µs, against 1.25 µs in the slot and 0.30 µs isolated | `R ≈ 0.0343·r` is far above break-even at these r, and the in-slot FFT penalty is unattributed | r under about 1,000 rows or far fewer arrivals; E3 gates. `cuda_segmented_timing_oracle` in [`test_triad_tensor.cpp`](../tests/test_triad_tensor.cpp) is the existing time-domain reference | [Step 5](#eligibility-procedure) |
| Similar-lag (proposal-qualified) gain ranks | Bit separation equals one rank map's evidence difference to about 1e-13 | Decisions would change | Not at those shapes | [Ranks](triad-gpu-optimization.md#signed-lag-ranks) |
| Separate processes sharing one device | 3.0 s against 0.45–0.81 s per slot | Device contention | Never for one receiver | [Remaining costs](triad-gpu-optimization.md#remaining-costs-after-the-dense-search) |
| Fine windows above 16 blocks | No gain on the matched slot | Owner and device each wait for the other | Another geometry | [Remaining fine costs](triad-gpu-optimization.md#remaining-fine-costs) |
| FP32 fine tile storage | Would lose the C128 presence witness | Correctness | — | [Precision guide](gpu-precision-controls.md#automatic-mode-prioritizes-computational-ceiling) |
| Cross-slot template bank caching | No hit possible | Banks are keyed by pattern epoch and symbol ordinal, so no two calendar slots share a bank; a session already reuses one bank across its frequency tiles | — | [Dense search](triad-gpu-optimization.md#dense-fp4-coarse-search-and-device-template-bank) |
| Historical RTX 4090 FP16 receiver replay; FP8 observation quantization | Replay slower than FP32 (batch 8 had 23.5% lower throughput than batch 1); FP8 added misses | Pack and launch costs; quantizing observations | Counterexamples only | [What to measure separately](triad-gpu-optimization.md#what-to-measure-separately) |

### Historical results in Git

These ran retired Python/Triton receivers. They show techniques, not current
qualification ([inventory](triad-gpu-optimization.md#verified-git-history-inventory)).

| Technique | Result | Current status |
| --- | --- | --- |
| NVFP4 correlation with fused reconstruction | 1.347× on a full PRO 6000 batch; the RTX 5090 favoured FP32 | E2M1 now runs in the dense search and fine projection |
| Split-BF16 and TF32×3 phase-state contractions | 1.040× and 1.013× complete on RTX 5090 | Absent; would fit the prepared coarse matrix boundary (untried) |
| Fused FP16 packing and tiled products | 1.49× kernel on an RTX 4090 (Threadripper PRO 3975WX host, 10.2-CPU quota); complete receiver lost, and FP16 replay ran 2.3% below FP32 (61.708 against 63.149 captures/s) | Packing implemented in the CUDA matrix path |
| K32 partials with FP32 outer sums | FP8 E4M3/E5M2 maximum error 21.85× and 26.62× smaller at an unchanged 0.15 ms, RTX 4090 | Bounded partials implemented for INT8; the pattern for FP16-accumulate partials on GeForce |
| Persistent CUDA graphs | 2.80× FP32 and 4.00× NVFP4 on RTX 5090, 1.27× on PRO 6000, against eager | Graph replay absent (untried) |
| Cached direct-GEMM acquisition bank | 4.89× against FFT on a small 288-hypothesis bank (PRO 6000) | Shape-specific; the dense search is the current analogue |
| Two-GPU trial sharding | 1.96–1.98× for independent jobs | Test orchestration, not receiver parallelism |

### Proposed and untried

Lever numbers are those of [Next levers](triad-gpu-optimization.md#next-levers-in-order);
kinds as defined in [Start here](#start-here). "Reaches"
names the challenging slots the lever can change. Stop rules are this page's
defaults for a predeclaration; change one only before the runs, with a
reason ([defaults](#default-thresholds-and-stop-rules)).

Cheapest falsifying check before building, by lever: items of the
[one-hour pod plan](#one-hour-pod-plan-worked-example) need no code. Item 1
(split the remainders) decides lever 6 against lever 12; item 2 (A E E A)
whether lever 9's gap is real; item 3 (varying batch width) lever 3's first
step; item 4 (quota against affinity) lever 4; item 5 (holdout family
routes) the drift and varying verdicts (levers 2, 3); item 6 (kernel trace)
per-call against traffic levers (5, 6, graph replay, L2). Without a pod:
the ridge benchmark on an EPYC 9555-class CPU (lever 2), `plan` receipts
for dense estimates and kernel extents (levers 11, 12), and reading the
code for an unimplemented route (`fp32-template`).

| Strategy | Kind | Reaches | Expected effect | Prerequisite and qualification | Default stop rule |
| --- | --- | --- | --- | --- | --- |
| Kernel trace of the CUDA segmented stage at an HF-like shape: product, inverse, ranks, finish, copies, synchronization and idle time | G | HF; holdout on CUDA | Explains the 3–4× gap between in-slot (0.95–1.26 µs) and isolated (0.30 µs) cost per transform | Nsight Systems permitted on the pod (untested); the [worked example](#worked-examples) lists the kernel sequence | One trace. If FFT kernels are under half of the span, rank per-call levers above traffic levers |
| Same-pod comparison of the CUDA and Vulkan/OpenCL automatic routes at holdout shape, then per-stage selection or CUDA parity (lever 7) | G | HF and holdout | On a CUDA build the three CPU families are 1,686 s of HF (46%, ceiling 1.83×), plus the 208 s other coarse; the slot-wide upload was 249 GB, about 94 GB of it segmented copies (*derived*) | Same source, same pod, both automatic routes, the qualification slots ([which route for HF](#worked-examples)) | Keep one backend per build if no stage differs by more than 5% of a challenging slot |
| Drift redesign: per-lane command buffers or sessions, batched atoms (40–48 per sign at HF), device rendering, one 827 MB observation upload (lever 2) | G | All | HF drift 899 s → about 100–150 s with the slope-blocked ridge already in the tree (21 November; its HF effect is *modeled*, unmeasured), about 20–40 s with device rendering (*modeled*). Both depend on the 63 ns per load, which is the receipt's seconds ÷ a modeled load count (ridge share assumed 85–95%), not timed | First run the ridge benchmark on an EPYC 9555-class CPU ([source and build](triad-gpu-optimization.md#next-levers-in-order)). Exact CPU changes still to make: a survivor slope-list cache, a shared direct-path render for both signs, concurrent survivor passes. Then E3 at proposal level on the holdout and the 72 Hz/5× control | Stop before device rendering if the explicit device stage is not faster in both pairs on the 72 Hz/5× control after per-lane sessions |
| Varying redesign: split one host batch into several device calls, `fp32-template`, device rendering, wider gathers, dense DFT-to-modes (lever 3) | G, then T for the last step | All | HF varying 401 s → about 30–60 s (*modeled*) | E4 with per-block FP64 fallbacks and a native rerun near the margin; the [E4 screen](#eligibility-procedure) | Skip the matrix step while the device projection phase is under 5% of the device stage. If a measured change of `fp32-template` exceeds 0.25 × the binding margin ([E4 screen](#eligibility-procedure)) on a challenging stratum, run the predeclared sensitivity trial; stop only if its dB loss outweighs the measured gain |
| Native-stage scaling at large `W` (lever 4) | C | Holdout, 72 Hz/5× | Holdout about −52 s (*modeled*) | Thread-state samples on a large-quota pod beside a 13-CPU affinity run | If the 13-CPU run's per-block varying time matches the full-quota run within 10%, quota contention is not the cause; sample threads instead |
| Widen the Vulkan segmented envelope to 72 Hz/1×-class shapes (lever 5) | G | None of the three (the 72 Hz/1× slot is easy); a no-change check on the others | 72 Hz/1× slot about halved | E3 near-tie qualification | Stop on any decision change on a stratum |
| Segmented traffic cuts: fused load, pruned inverse output, shared column spectra, FP16 storage, L2-sized batches (lever 6) | G | All | FP16 storage alone at most about 2× on the FFT-kernel part (*inferred*); the kernel part may be about a quarter of the HF span | The kernel trace above; E3 guards and new receipt counters | Stop if the change gains under 1.2× on the stage in a slot pair. `--segmented-benchmark` (N = 32,768 shape) exists in Vulkan and has run on Radeon and [Blackwell](validation.md#inside-vulkan-segmented-timing); a CUDA change is judged on slot pairs, with `test_triad_tensor --cuda --segmented-only` for correctness |
| Overlap fine-family work with the device segmented stage (lever 8) | G | Holdout, HF | About 23 of the holdout's 40.8 CPUs are idle on average | Backlog intervals | Stop if busy CPUs during the segmented stage rise by fewer than 5 |
| Automatic gain rank 2× slower than explicit: 1.34–1.47 against 0.57–0.68 s on the 72 Hz/5× control (bounds item 6, lever 9) | G, C | 72 Hz/5× (about 0.7–0.9 s, up to about 5% of the slot), holdout | Candidate causes (code, effect unmeasured): an automatic session takes the widest batch (at most 64 blocks) under a quarter of the shared workspace, tens of GB on the 96 GB card (*inferred*), then halves while `Shared::try_acquire` refuses, which it does whenever any owner is queued or the workspace is full; an explicit-workspace session admits without any shared lease (`VulkanGainRank::try_admit`). So contention halving or queue contention is likelier than the quarter bound. Admission charges also differ: VkFFT's temporary buffer quoted at the product buffer's size; the backend-wide cache charged owners + 1 times (59,088,896 B, about 4.8 GB at 80 owners); a 128 MiB compilation bound per family. Lever 9 changes those charges; optional leases must still never wait | Build it together with lever 1's gain-rank batch-width field, beside `vulkan_gain_rank_upload_bytes`, `vulkan_peak_leased_bytes` and `vulkan_peak_owner_lease_bytes` on automatic and explicit runs; no result change. Borderline against the G threshold (0.7–0.9 s of 17.75–19.76 s) and below the same-build noise band, so judge on the stage | If both routes admit the same batch width and the gap remains, test queue contention instead |
| Fused gain-rank kernel; native audit set; fixed rank on the device; FP32 AVX2 CPU rank (lever 10) | G | Vulkan route: holdout 1%, 72 Hz/5× 7%; HF only on the Vulkan route or after lever 7 | HF rank about 6 → 0.6 s by a traffic model, but the holdout's 3.05 s scaled by blocks gives about 29 s (*modeled*). NVIDIA: oracle 0.22 ms and in-slot 0.07 ms per block; Vega about 2 ms per block, not a Blackwell figure | E1 unchanged | Below 5% of every challenging slot on the Vulkan route: build it as the Radeon prerequisite or for HF after lever 7, not as a Blackwell lead |
| Large dense plans: two workspace pairs per block, pipelined dense tiles, independent dense tile width, wider arrival frontier, device check of the 1/16 guard, the HF Worst 3.6 Hz two-ray plan (lever 11) | T | None of the three: large non-segmented plans | IC-7100: up to about 4 s of the 11.35 s products (profiled run) and about 3 s of round trips (*modeled*) | E3; planted-arrival sweep on the device | Stop when the product interval reaches 1.44 PFLOP/s (within 1.2× of the 1.73 hoisted bound), or products fall under 5% of every target plan's coarse stage |
| Device synthesis of varying-passband and midpoint-gain banks, so their frequency search can use the dense FP4 route (part of lever 12) | T via G | All three | Unknown: the frequency-search share of the coarse remainder is unmeasured. The holdout's two-ray plan models at about 2 s of FP4 products against about 110 s of coarse remainder (*modeled*, [map](#tensor-core-opportunity-map-for-the-challenging-slots)) | Split the remainder first; estimate each plan's dense time with step 5 (`2 × 4 × rays × arrivals × rows × workspaces × frequencies ÷ achieved rate`, plus synthesis and rescoring), not with `dense-cost`, which gates one-ray automatic sessions only; the 1/16 conditioning guard; E3 sweep. Proxy without code: an explicit `--backend vulkan --vulkan-precision nvfp4 --opencl-profile on` holdout run scores the same sessions' host-prepared columns with the dense products, so if its Gram ratio passes 1/16 and the workspace admits it (bank about 2.5 GB plus about 41 MB per tile frequency, [map](#tensor-core-opportunity-map-for-the-challenging-slots)), `opencl_coarse_product_seconds` estimates the product time. It is a feasibility estimate, not attribution: host preparation replaces synthesis and the route differs from automatic; no such run has completed. HF needs lever 7 first | Stop if that frequency search is under 5% of every challenging slot, or the step-5 estimate including synthesis and rescoring is not below the measured frequency-search time |
| Orthonormalized (whitened) two-ray FP4 bank: pack `Ψ = Φ·G^(-1/2)` so the score `‖Ψ^H y‖²` no longer amplifies FP4 error by 1/λmin | T | Ill-conditioned two-ray plans (5 µs, 0.001 Hz spread class); none of the three | Could widen FP4 admission below the 1/16 guard | The FP32 partial rescoring was qualified only on 14 well-conditioned synthesis geometries and the matched fixture (ratio 0.999996): qualify FP32 rescoring at low ratios or select FP64 rescoring in code for the admitted low-ratio sessions (`--opencl-coarse-rescore fp64` is only the explicit control: the probe accepts it only with `--backend vulkan --vulkan-precision nvfp4`); an SNR × ratio planted-arrival grid ([conditioning](triad-gpu-optimization.md#dense-fp4-coarse-search-and-device-template-bank)) | Stop if the grid loses a planted arrival the exact frontier keeps at noiseless scores 24 or 16 |
| FP8 or INT8 dense frontier on SM120 for two-ray banks below the 1/16 guard, which now fall back to the Vulkan FFT session (`ill-conditioned-template`) | T | Ill-conditioned two-ray plans; none of the three | FP8 E4M3 has two more mantissa bits than E2M1 at half the FP4 peak (1,007.6 against 2,015.2 TFLOP/s on the Workstation Edition) | Measure the guard on the device first (lever 11); the same SNR × ratio grid as the whitened bank | Stop if the whitened FP4 bank already passes the grid |
| CUDA L2 persistence (`cudaAccessPolicyWindow`) or L2-sized product tiles for segmented timing | G | HF, holdout on CUDA | Unknown; tests whether concurrent owners' 32 MiB tiles evict each other from L2 | The kernel trace; owners × tile bytes against the L2 size | Stop if a single-owner run shows no in-slot penalty |
| Library fusion: cuFFT load and store callbacks (LTO), cuFFTDx device FFTs fusing product, inverse and ranks; VkFFT convolution, zero-padding and omitted-output options | G | All | Removes the product write and a read per tile (*inferred*) | Count kernels per plan in a trace first; E3 | Stop if the fused tile is not faster than the separate kernels at the production tile width |
| Command-buffer or CUDA graph replay of fixed per-tile sequences | G | All (segmented: about 10 M tiles at HF, *inferred*) | Fewer fixed per-dispatch costs; historically 1.27× on the PRO 6000 | Stable buffers and owner-coordinated capture ([graph boundary](triad-gpu-optimization.md#native-disposition-of-the-historical-techniques)) | Stop if launch gaps are under 10% of the traced span |
| Per-owner Vulkan queues; plan builds outside the queue lock | G | All on the Vulkan route | Concurrent owners on one device | Multi-owner device check with `vulkan_fft_plan_seconds` | Stop if plan seconds are under 1% of the slot |
| Precision-decomposed Tensor DFT for the gain rank (TF32×3 or BF16×3) | T | As the fused kernel | Untried and unpromising: split peaks are 0.67–1.3× FP32 SIMT, R ≈ 5.5, and the exp epilogue is untouched | After the fused FP32 kernel; E1 band ratio | Stop if the transform is under half of the fused kernel's time |
| Split-BF16 or TF32×3 on the CUDA prepared coarse matrix boundary | T | Lag-group sessions only | Untried (historically 1.040× and 1.013× complete on an RTX 5090) | Library shapes, all split-product costs, scratch admission | Stop below 1.2× on the matrix stage |
| Vulkan cooperative-matrix kernels (FP16, BF16, FP8, INT8) for post-isolation contractions | T | Depends on the contraction; the only matrix route of a Vulkan build on Ada and RDNA 3 | Untried in the receiver; FP8 E4M3 and FP16 executed exactly on the PRO 6000 | [Recipe](#rtx-4090-rtx-4090-laptop-and-rtx-4060-ti-16-gb); same gates as CUDA | Stop if the contraction is under 5% of every target slot |
| INT8, FP8 or INT4 port of the dense frontier search for Ada (software block scales, INT32 or FP32 partials) | T | None of the three (dense sessions) | 4090 INT8 is about 0.33× and INT4 about 0.66× the PRO 6000 dense FP4 peak | Class identification for SM89; planted-arrival sweep; port the FP32 per-row kernels with it | Stop if the planted-arrival rule fails |
| FP16-accumulate partials on GeForce (FP16 or FP8 MMA at twice the FP32-accumulate rate) | T | E3 proposal-only contractions | 2× on the matrix part (vendor peaks) | K-bounded partials with FP32 outer sums, as the K32 result | Never for evidence-bearing outputs |
| GPU whole-grid innovation likelihoods | G | All | Open experiment | State class: FP64 | — |
| Deterministic CPU/GPU workspace split on Vega | G | Vega slots | Adds the two rates (untried) | Decisions independent of load | Stop if the slot is not faster in all three local pairs |
| Linear-domain scaled HMM recursion | C | All | About 4× on the Markov part (*modeled*) | Own qualification | — |
| Drift exact FP64 pass across its retained peaks at once: each tile row's observation and template samples loaded once for up to 16 (bit, peak) phase recurrences in SIMD lanes, instead of one pass per peak | C | All; the host keeps this pass on every route | HF: 31.4 G peak-rows per slot, modeled at about 25–40 s on eight lanes, the drift bound left after device rendering ([shapes](triad-gpu-optimization.md#drift-proposals)) | Per-peak operations and order unchanged, so bit-identical: digest and evidence equal on the drift strata. Read `drift_exact_seconds` on a challenging slot first | Stop if `drift_exact_seconds` is under 5% of drift lane-seconds on every challenging slot |

## Concepts to try later: move the pipeline onto the GPU

Untried rearrangements, recorded for later work. None is measured as a
whole. Each must still pass the [eligibility procedure](#eligibility-procedure)
and the [test recipe](#test-recipe).

**Why consider it.** Several conditions point the same way:

- **Spare SIMT throughput.** The RTX PRO 6000, RTX 5090 and similar parts
  have about 100–126 TFLOP/s of FP32 and non-Tensor FP16 vector arithmetic
  ([hardware facts](#hardware-facts-for-decisions); the L40 is also in this
  class, but check its vendor datasheet before quoting a figure). That
  capacity is largely idle today: Tensor Core work on the challenging slots
  is about zero.
- **The CPU families are slow on the hard slots.** They run at about
  1.7e10 FLOP/s on the HF slot and leave CPUs idle during device stages:
  about 3.8 of 13 CPUs were busy on the HF slot.
- **Transfers dwarf the input.** The receiver's input is a low-rate
  baseband stream: the 36 Hz/10× holdout is about 10.9 million samples at
  6 kHz over 1,812 s, tens of MB
  (`local-evidence/lowrate-2mw-runpod/100mw-36hz10x-target-m12p9897-seed940600-bit0.json`).
  The work derived from it is far larger:
  - the HF slot uploaded 249 GB of banks and templates;
  - a device varying route at HF would upload about 562 GB of FP64
    templates (*modeled*);
  - on the Radeon's 72 Hz/1× slot, the explicit drift route uploaded
    117.7 MB of host-rendered products and the varying route 564 MB.

Generating derived data where it is consumed removes most of this traffic,
along with the per-call round trips that made the drift and varying device
routes lose.

**Concepts:**

1. **A device-resident slot context.** Upload the raw input once per slot,
   or stream it as it arrives. Keep these on the device across the coarse
   and fine stages: channelized and whitened series, keyed template banks,
   spectra, power tables and intermediate frontiers. The host keeps
   scheduling, ordered commits, the codec and physical completion.

   Today each family session uploads its own inputs:
   - `src/triad_vulkan_core.*` holds the shared device and leases;
   - `src/triad_vulkan_{gain,drift,varying}.cpp` hold the families;
   - the OpenCL route is in `src/triad_opencl_*`;
   - the CUDA route is `TensorCoarse` and `TensorFine` in
     `src/triad_tensor_*`.

   A context needs one lease covering its lifetime and an explicit
   multi-owner policy, because several slot owners share one device.
2. **Generate derived data on the device.** Candidates:
   - keyed template bank synthesis, which already exists for the dense FP4
     coarse search; extend it to varying-passband and midpoint-gain banks,
     the only path by which the existing FP4 kernel can reach a challenging
     slot ([opportunity map](#tensor-core-opportunity-map-for-the-challenging-slots));
   - drift template rendering and products;
   - varying template rendering;
   - cubic warps, Doppler rotations and observation channelization.

   Use FP32 per-row arithmetic with FP64 or integer phase and coordinates,
   as the `fp32-rn4-v1` mapper does.
3. **Device stage graphs instead of host calls.** Submit one batched or
   persistent workload per stage, with device-side work queues over atoms,
   blocks and lanes, instead of one host call per atom or block batch. The
   drift session serializes every lane's calls, and the varying route
   multiplies small batches; both are per-call overhead.
4. **Use SIMT and Tensor units at the same time.** Run FFTs, rendering,
   ridge sums and per-row math in FP32 or FP16 SIMT, and contractions such
   as frontier products and small-n DFT-to-modes GEMMs on Tensor Cores. Put
   them on separate queues or streams so they overlap. Today the Vulkan core
   has one compute queue with global barriers.
5. **Keep the exact parts on the device too.** FP64 runs at 1/64 rate,
   about 1.8–2 TFLOP/s on the RTX PRO 6000. That still covers the three CPU
   families' FP64 arithmetic at HF in roughly 15–30 s (*modeled*), and it
   removes the round trips for certification, rescoring and evidence.
   Exactness classes still apply: the gain rank stays E1.
6. **Produce analysis data on the device.** Compute diagnostics on the
   device and transfer summaries rather than raw intermediates:
   - evidence components, per-block statistics, spectra, histograms and
     ridge maps.

   Full dumps, if wanted, go behind an explicit option with charged
   workspace.
7. **A GPU simulator for backlog and test throughput.** The CPU simulator
   took 404 s of the HF run. A counter-based generator such as Philox for
   noise and the channel model on the device would cut that. It changes
   every capture hash, so do it together with another hash-changing change.
   Keep the native simulator as the reference with a statistical
   cross-check, and use `--capture-cache` meanwhile.
8. **Overlap CPU and GPU work.** Run ready fine-family CPU work beside the
   device segmented stage, within the bounded executor and ordered commits,
   so CPUs are not idle while the device works and the device is not idle
   while the CPU works.
9. **One kernel set, chosen per stage.** CUDA and Vulkan/OpenCL builds
   currently get different stage sets
   ([what automatic runs](triad-gpu-optimization.md#what-automatic-selection-runs)).
   Unify the kernels, or select a backend per stage.

**Constraints that do not change.** Every public hypothesis, the configured
calendar and keyed ordinals, physical quiet-slot completion, `W = 2*H`, a
charged lease for every byte of device and host memory, explicit fallback
reporting, and the [exactness classes](#exactness-classes).

**Where to start.** The ordered, bounded steps are in the
[implementation plan](gpu-pipeline-plan.md). In short:
1. Measure first: per-family device time and upload bytes, CPU busy
   fraction, `varying_coarse_seconds` on the holdout
   ([lever 1](triad-gpu-optimization.md#next-levers-in-order)).
2. Prototype the slot context on one family. Drift is the obvious one:
   render, products, ridge and slopes on the device, with FP64 evidence.
3. Time it on the 36 Hz/10× holdout and the 72 Hz/5× control against the
   native stage, in one binary.

## Generating new strategies

Turn a measurement into a candidate with this map. Each family names the
signal and the receipt field that shows it.

| Lever family | Signal (field) | Applies here | Evidence | Caveat |
| --- | --- | --- | --- | --- |
| Attribute before acting | In-slot rate far below the isolated rate of the same kernel (segmented: 0.95–1.26 against 0.30 µs per transform); unsplit spans (`varying_coarse_seconds` absent) | CUDA segmented stage; holdout coarse remainder; fine-stage families | — | Profiling changes timing: decide on unprofiled runs |
| Port the stage to the device | Stage runs native on the target build (`*_decline_reason`, route strings, `native_*_sessions`) | CPU families on CUDA builds; segmented on SM89 and non-SM 12.0 Vulkan | Gain rank: holdout 688 → 281 s | A port that keeps host preparation can lose (drift, varying) |
| Residency and device-side preparation | Large uploads (`*_upload_bytes`), host render or pack share | Drift products (117.7 MB on 72 Hz/1×), varying templates (562 GB per HF slot, *modeled*), HF's slot-wide 249 GB (segmented copies about 94 GB, *derived*) | Device bank 82 MB → 474 KB; resident fine 2.798 GB → 74.76 MB | Device memory and lease charges |
| Batch small calls, widen launches | Work per call ≪ 10× fixed cost (`planned_calls`, `varying_batches`, dispatches) | Drift atoms, varying batches, dense tiles | 288 → 4,608 lanes: 3.5×; windows of 16: 405 → 51 submissions | Batches must not reorder public state updates |
| Remove serialization | Lane-summed timers ≫ wall × lanes; `varying_wait_seconds`; low busy CPUs | Drift session mutex, one Vulkan queue, plan builds under the queue lock | 43–48 drift lanes behind one mutex; Radeon `varying_wait_seconds` 0.67 → 13.6–18.6 lane-seconds (*indicative*) | Per-lane contexts multiply twiddle tables (270.5 MB per 2^25-point plan) |
| Fuse kernels, cut memory passes | Bytes/s near DRAM bandwidth while flops ≪ peak; several kernels per tile | Segmented product, inverse and ranks; unfused gain rank; L2-sized fused batches | Fused warp and FIR: 55.9 → 8.7 ms | Mechanisms: cuFFT load and store callbacks (LTO), cuFFTDx device FFTs inside one kernel, VkFFT's convolution, zero-padding and omitted-output options, a custom last pass. A library may already fuse passes: count kernels per plan in a trace first. Register pressure; check `ptxas` spills |
| Prune outputs | Only m of n outputs read | Segmented: 10,553 of 32,768 lags | — | Pruned FFT libraries are limited: a cuFFT store callback that drops unread lags, VkFFT's omitted-output option, or a custom pruned last radix pass |
| Transform as matrix product | Small n or m, post-isolation, R below the achieved Tensor/FFT ratio | Fine projection (done), varying modes, gain rank | Fine NVFP4 projection | E4 budget for evidence-bearing outputs |
| Precision ladder with exact rescoring or certification | Output proposes candidates | Frontiers, ranks, proposals | FP4 frontier with FP32 rescoring; FP32 gain rank with FP64 certification | Wider frontiers or bands raise fallbacks |
| Lower-precision storage | Bandwidth-bound and class allows | Segmented spectra, varying templates | Half cuFFT workspace 7.57 GB → 581.7 MB | Storage is not arithmetic |
| Reformulate for low precision | Error amplified by conditioning, range or cancellation (`ill-conditioned-template`, range fallbacks, large terms that cancel) | Two-ray FP4 bank (orthonormalize: `c^H G^-1 c` is basis-invariant, so pack `Ψ = Φ·G^(-1/2)`), centering, block-floating or power-of-two scales per segment, compensated or bounded partial sums | INT8 partials of at most 512 samples; FP32 partial sums of 64 rows | A lever that admits ill-conditioned inputs must also qualify the FP32 partial rescoring there, or select FP64 rescoring in code for those sessions (or keep them declined); `--opencl-coarse-rescore fp64` is only the explicit control and cannot change the automatic route |
| L2 residency | Working set near the L2 size; several owners on one device | Segmented tiles: 33.5 MB each against a presumed 128 MB L2; one-ray IC-7100 bank about 28 MB | — | Concurrent owners × tile bytes, not one tile, must fit; CUDA `cudaAccessPolicyWindow` sets persistence |
| Tile and layout for MMA shapes | Kernel below 50% of peak in its product interval; many loads per product | FP4 products on large plans | Ring layout 426 → 1,215 TFLOP/s | Re-verify bit identity of results |
| Overlap host and device | Busy CPUs well below the quota during device stages | Holdout: 17.5 of 40.8; HF: 3.8 of 13 | Mixed: RTX 4090 −13.6% to +5.8%, PRO 6000 −22.0% to +14.6% (slower on the original small backlog); inconclusive for backlog | Ordered commits and bounded executor |
| Per-stage backend selection | Each backend lacks routes the other has | CUDA lacks families and dense FP4; Vulkan lacks some CUDA paths | — | Needs both routes qualified |
| Admission and memory | Owners or batches capped by quotes (`workspace_peak_bytes`, `vulkan_peak_leased_bytes`) | Cache charged per owner; VkFFT temporary | — | No result change; still record leases |
| Replay fixed sequences | Many identical fixed-shape submissions | 28,082 dense dispatches; drift atoms | Historical graphs 1.27–4.00× | Host control flow must not depend on results inside the capture |
| Exact CPU work reduction | CPU work the device route also needs | Shift runs, slope blocks, shared direct-path render | Drift −16% to −33% on the 5650U | Must stay bit-identical |

**Rules for any new kernel.**

- GLSL FP64 has no `sin`, `cos`, `exp` or `log`: reduce the argument in FP64
  or fixed point and evaluate in FP32, as `drift_dechirp` does. Vulkan bounds
  FP32 `exp` only to 3 + 2·|x| ULP, so keep its arguments at or below zero,
  as the gain rank's two-pass log-mean-exp does.
- FP64 runs at about 1/64 rate on the NVIDIA parts; Vega's rate is not
  established. Prefer FP32 per-row arithmetic with FP64 or integer
  coordinates; an FP64 kernel pays only where it removes round trips,
  barriers or host passes.
- Results must not depend on batch width or lane count: accumulate in a fixed
  order (the gain rank adds block values in block order), and test batch
  invariance bit for bit where the family has such a test (gain rank: a
  3-block batch equals a 39-block one; drift: 0 of 1,138,688 bits differ).
  Segmented timing has no bit-for-bit width test: its test compares a
  time-domain oracle, the native FP32 scorer and FP64 within tolerances,
  and the CUDA plan already halves its tile under memory pressure. For a
  segmented tile or batch change use the E3 comparison (identical decisions,
  selection differences reported), not bit identity, as the stop rule.
- A matrix-unit FFT on a bandwidth-bound stage must cut DRAM passes or bytes
  ([step 7](#eligibility-procedure)); do not retry the unfused three-stage
  HMMA form.
- Check `ptxas -v` registers and spills for the OpenCL PTX
  (`coarse_warp_fir` 235 and `coarse_fp4_scores` 106 registers, no spills).
  Register stashes stay in registers only with `__forceinline__` and
  constant indices. Byte identity of a rewritten FIR relies on NVCC's default
  `--fmad=true` contracting multiply-adds the same way; a flag change needs
  the device comparison again.
- Device sessions take optional leases that never wait, halve their batch
  until a lease fits, and otherwise fall back with a named reason
  ([fine-stage ports](triad-gpu-optimization.md#fine-stage-device-ports)).
- A new OpenCL kernel entry needs its row in the PTX embedding table; a new
  Vulkan shader an index in its family's registry range.

**Template: from a measurement to a candidate.**

1. Choose the challenging slot and the stage with the largest share; write
   its ceiling `1/(1−s)`.
2. Count operations and bytes from receipts: FFT flops ≈ 5 × FFT
   row-stages; FFT bytes ≈ passes × 2 × element bytes × points; contraction
   flops = 2 × multiply-adds (`opencl_coarse_issued_products`,
   `cuda_matrix_padded_real_macs`). Derive shapes from counts, never from
   gate minimums such as radius 128 or 257 frequencies: for segmented timing
   blocks = forward FFTs (one per nonempty block), frequencies per block =
   inverse ÷ forward FFTs, budget per block = stage seconds ÷ blocks (HF:
   185,874 blocks, about 7,019 frequencies, 8.75 ms), rows per block =
   `projected_template_rows` ÷ blocks of the family.
3. Achieved rate = operations ÷ stage seconds. Compare with the peak of the
   arithmetic actually used and with bandwidth.
4. Name the bound: host preparation, transfer, per-call, serialization,
   bandwidth, latency or occupancy, or arithmetic.
5. Pick the lever family for that bound from the table above.
6. Estimate the optimistic gain and apply the
   [default thresholds](#default-thresholds-and-stop-rules).
7. Assign the exactness class and the lowest eligible format.
8. Plan the smallest experiment and its stop rule ([test recipe](#test-recipe)).

### Default thresholds and stop rules

Predeclare these unless a stated reason, written before the runs, changes
them. They are this page's planning defaults; the acceptance rules are the
measured pass rules of the [test recipe](#test-recipe).

| Kind | Build or run when | Accept when | Stop when |
| --- | --- | --- | --- |
| T | Optimistic modeled saving at least 5% of one challenging slot, at least 1.5× on the stage's matrix part, and the eligibility steps pass | Pods: P1–P5 of the predeclaration template (two pairs per cell). Locally: all three pairs agree in direction and the median moves by more than 3%. Plus the class gate | The first P1 or P2 failure (for E4, an evidence-budget excess triggers the sensitivity trial instead of a stop); under 1.2× on the component at production shape; Tensor-addressable work under 5% of every challenging slot |
| G | Optimistic modeled saving at least 5% of one challenging slot | Same | The device stage is not faster in both pairs on the challenging slot, or that slot is slower beyond the predeclared noise band in either pair |
| C | Same | The local three-pair rule | All three pairs do not agree |
| Attribution | It answers a named open cause | Receipts identical except the new fields | The field exists in every receipt it should |
| E4 budget | — | Measured change at most `f` × the binding margin ([E4 screen](#eligibility-procedure)) on every challenging stratum, `f` = 0.25; a numerical diagnostic, not a sensitivity result | Never stop on the budget alone: a stratum above it triggers a native rerun or a predeclared complete-receiver sensitivity trial, and the candidate stops only if that trial's dB loss outweighs the measured gain on the challenging strata |

Lever-specific stop rules are in the [proposed table](#proposed-and-untried).
Decide on slot totals; stage timers are shares. Never rerun to chase a pass.

**Default noise band.** Same-build automatic runs on one host spread
17.75–19.76 s on the 72 Hz/5× control (two runs, about 11%) and
5.58–6.61 s on the 72 Hz/1× slot (two runs, about 18%), and two binaries
of equivalent source differed 5–21% in Vulkan coarse seconds (code
placement, [paired timing](triad-performance.md#paired-timing-and-load-gating)).
Predeclare the noise band from the same-build spread of the run's own
pairs, or these figures if none exists. Two pairs cannot resolve a slot
effect under about 10%: judge such a lever on its stage time (faster in
both pairs) with the slot inside the noise band, or add pairs.

### Worked examples

- *HF segmented timing: attribute before choosing a lever.* Counted work:
  1,304,726,202 inverse FFTs of 32,768 points in 1,626 s, 1.25 µs each,
  about 3.9e11 row-stages/s (2.0 TFLOP/s, about 2% of FP32 peak). Isolated
  FP32 cuFFT on the same device costs 0.30 µs per transform in batches of
  128, so the FFT kernels account for about 400 s. One read and one write
  of a 33.5 MB batch in 38.88 µs would be 1.73 TB/s, about DRAM bandwidth,
  but that batch fits in the presumed 128 MB L2 and repeated isolated
  batches may have stayed there, so the DRAM pass count is unknown
  (*inferred*). The three-pass DRAM model (about 1.3 TB/s in the slot) is
  unverified, and the 3–4× in-slot penalty is unattributed. Settle the
  passes with the kernel trace or a cold-buffer isolated timing before
  using the 400 s or the 200 s matrix-unit bound. The
  CUDA sequence ([backend](../src/triad_tensor_backend.cpp),
  `score_segment_plan`): per block, host-to-device copies of the block's
  observation spectra and raw column from host vectors, a column kernel and
  one forward FFT; per tile of up to 128 frequencies a product kernel that
  writes tile × N complex FP32 values (32 MiB at 128 × 32,768), the in-place
  inverse cuFFT and a ranks kernel that reads the arrival lags and updates
  FP32 tracking states; a finish kernel per block; a device synchronization
  every 4 blocks. Each CUDA owner context has its own pooled stream, while
  Vulkan owners share one queue with global barriers. The tile is
  `min(128, frequencies, free capacity ÷ transform bytes)`, halved until the
  plan fits, and no receipt records it. Candidate causes: about 3 kernels
  plus cuFFT's per tile over about 10 million tiles at HF (*inferred*);
  copies from host vectors that the code never pins (no `cudaHostRegister`
  in the tree) and the periodic synchronization; tail
  tiles (a session scores `2·radius + 1` frequencies per block, about 7,019
  on average at HF from 1,304,726,202 inverse and 185,874 forward FFTs, and
  the widths vary by session, so tails occur per session; a 15,733-wide
  session would end in a 117-wide tail; at 16,384 points a 33-wide tail
  cost 0.50 µs per transform against 0.14 µs); concurrent owners'
  32 MiB tiles contending for L2; and a span that overlaps host
  preparation. Family: attribute with a kernel trace first, then per-call
  levers (graph replay, fewer synchronizations, pinned copies) or traffic
  levers (callbacks or fused device FFTs, pruning, FP16 storage), whichever
  the trace shows. Not Tensor Core work unless a fused factorization cuts
  passes.
- *Segmented shapes on the stored receipts* (*derived* from the receipt
  counts and `make_segmented`: `n = bit_ceil(r + A − 1)`,
  `phases = max(1, ceil(2r/n))`). HF: `cuda_segmented_sessions` 156 =
  78 workspaces × 2 families; the tracked family has 623 blocks
  (`varying_timing_blocks`) × 15,733 frequencies
  (`varying_timing_frequencies`), r ≈ 20,714 rows, P = 2; the faster family
  about 1,760 blocks × 3,935 frequencies, r ≈ 7,333, P = 1; both n = 32,768,
  A = 10,553, from `plan`'s `projected_template_rows` 12,904,486. These
  reproduce the 185,874 forward and 1,304,726,202 inverse FFTs exactly. If
  the tile stayed at 128 frequencies (it halves under memory pressure and is
  not recorded), that is 123 and 31 tiles per block (tails of 117 and 95),
  about 97–373 launches per block and about 10.2 M tiles. Per nonempty
  block the CUDA route uploads `P·n·8` B of observation spectra plus `r·16`
  B of FP64 raw column: about 94 GB of the slot's 249 GB `upload_bytes`,
  about 1.7 s at the pinned 54.7 GB/s and still under 1% of the stage at a
  fifth of that rate, so a pinned or double-buffered segmented upload falls
  below the 5% build rule at HF. Holdout input (explicit CUDA, earlier
  source): 202 blocks × 3,935 and 569 × 1,313 frequencies, r ≈ 6,676
  (n = 32,768) and 2,370 (n = 16,384), P = 1. The 72 Hz/5× control's tracked
  family is 54 × 985 ([precision guide](gpu-precision-controls.md#vulkan-on-vega-integrated-graphics));
  record `vulkan_segmented_{forward,inverse}_ffts` for its full shape on
  the next pod.
- *Drift on the 72 Hz/1× slot.* 3.65e9 row-stages ≈ 12 ms of device work
  against a 0.45–1.3 s loss; 43–48 lanes behind one mutex; host rendering
  51–67% of native drift lane-seconds on the 5650U. Bound: serialization and
  per-call. Family: per-lane sessions, device rendering, batching.
- *IC-7100 one-ray dense plan.* Products 11.35 s in the profiled run
  (about 59% of the unprofiled 19.16 s coarse stage, *inferred* across the
  two runs) at about 1.12 PFLOP/s useful (*inferred*; issued products not
  recorded) against 1.73 PFLOP/s products-only: up to about 4 s from
  layout. About 2.95 s outside products and rescoring over 28,082
  dispatches in the profiled run, about 0.1 ms per dispatch (*inferred*):
  per-call. Families: operand sharing, pipelined tiles, wider dense tiles,
  replay.
- *Dense products, unit conversion.* `opencl_coarse_issued_products` counts
  real multiply-adds as issued, padding included; operations = 2 ×
  products. Time per workspace and frequency = 2 × products ÷ achieved
  rate. Matched: 2 × 1.21e9 ÷ 1.15e15 ≈ 2.1 µs, × 78 workspaces × 15
  frequencies ≈ 2.5 ms, as measured (2.6 ms). HF Worst 3.6 Hz two-ray:
  4 × 2 × 7,036 × 93,614 = 5.27e9 products, about 9.2 µs per workspace and
  frequency; its workspace and frequency counts are not recorded, so read
  them from `plan` before estimating the plan.
- *RTX 4090 explicit INT8 message* (five slots, three owners): 91,955,920,896
  padded multiply-adds in 216 IMMA calls are about 0.3 ms at the 660.6 TOPS
  peak, yet the stream took 2.3 s longer than the FFT route. Bound: packing,
  launches and workspace, not matrix arithmetic (*inferred*). Family: fewer,
  larger calls, or keep the FFT there
  ([CUDA record](validation.md#current-cuda-precision-and-tensor-dispatch)).
- *RTX 4090 automatic INT8 backlog regression (3.47% slower over 25 slots).*
  Pack intensity `M·N·K / (M·K + K·N) = M·N / (M + N)` from the
  per-dimension maxima of the explicit INT8 message receipt (1,664 rows,
  inner 2,713, 213 columns; independent fields, not necessarily one call):
  at most about 189 with both operands packed per call and 213 with a
  prepared right operand (*inferred*; intensity rises with every dimension,
  so maxima bound any call). Both are below the 241 that lost to FP32 for
  FP16 on the same card (683 won),
  and INT8's twice-FP16 rate moves the crossover higher still. The gate's
  proxy, padded multiply-adds at most 30 paired-FFT row stages
  (`select_automatic_matrix` in the [CUDA backend](../src/triad_tensor_backend.cpp)),
  records no calibration device; recalibrate it per architecture (INT8
  660.6 against 1,007.6 TOPS, 1,008 against 1,792 GB/s). Test: the record
  compared two binaries with the same 25-slot, three-owner `backlog`
  arguments (`local-evidence/tensor-auto-policy/cap-backlog/`); repeat
  that with N the current INT8 gate and C a build whose gate excludes SM89
  (so C runs the FFT there), alternated N C C N for three pairs. If C's
  backlog total is faster beyond noise in every pair, narrow the SM89 gate
  (for example a minimum pack intensity near 700, or more rows) or keep the
  FFT there; otherwise keep the INT8 gate and record the original 3.47% as
  unreproduced. Record either outcome in
  [validation](validation.md#current-cuda-precision-and-tensor-dispatch).
- *Which route for HF.* On a CUDA build the three CPU families are 1,686 s
  of the 3,706 s slot (46%, ceiling 1.83×) beside 1,626 s of CUDA segmented
  timing. The Vulkan/OpenCL route would bring the device gain rank (the
  holdout's stage fell 415 → 3.05 s; scaled by blocks about 29 s at HF,
  *modeled*), but its segmented rate at 32,768 points is unmeasured in a
  slot (the short agreement fixture took 2.4–2.5 µs per transform, against
  0.95–1.26 µs for CUDA in the slot; indicative). Device memory is not
  the constraint shown: the HF CUDA receipt's 99.2 GB `peak_reserved_bytes`
  is its lease reaching the workspace limit, and the allocator peaked at
  38.6 GB reserved and 31.5 GB live. If Vulkan segmented timing were twice as
  slow there, it would add about 1,600 s, more than the about 360 s the
  device rank saves. So the first step is the same-pod comparison of both
  automatic routes at holdout shape, then per-stage selection (lever 7).

## Test recipe

1. **Attribute first.** Use the
   [instrument table](triad-gpu-optimization.md#attribute-the-time).
   `--backend gpu` cannot be profiled. Decide on unprofiled automatic runs.
   CUDA records host `wall_seconds` only.
   - *Dense or resident sessions only* (the matched fixture, the IC-7100
     plan): attribute on an explicit reproduction (`--backend vulkan
     --vulkan-precision nvfp4 --opencl-profile on`, plus
     `--vulkan-fine-projection resident-nvfp4` for the fine stage).
   - *Challenging slots: never attribute with `nvfp4`.* (This rule is about
     attribution; NVFP4 is already the automatic default where eligible,
     and a feasibility estimate for lever 12 is a separate use, see the
     [synthesis row](#proposed-and-untried).) It makes the dense search
     mandatory for every session, so varying-passband sessions, which the
     automatic route runs on the Vulkan FFT search, either score
     host-prepared columns on a different coarse route or fail ("exceeds
     its device workspace", "no route"), and the OpenCL intervals do not
     cover segmented or family work. Attribute there with unprofiled
     `--backend gpu` receipts (`varying_coarse_seconds` and the family
     sub-timers), with explicit `--backend vulkan --require-accelerator on
     --vulkan-segmented-timing device --vulkan-gain-rank device` stage
     routes, or with the kernel trace.
   - *Already emitted; keep them in every summary:* `varying_coarse_seconds`,
     `varying_{projection,statistics,recursion,wait}_seconds`,
     `varying_batches`, `varying_batch_blocks`,
     `drift_{render,ridge,ridge_search,slope,exact}_seconds`,
     `vulkan_fft_plan_seconds`, `vulkan_shader_compile_seconds`,
     `vulkan_device_command_seconds` (coarse command buffers only) and the
     evidence fields of the [E4 screen](#eligibility-procedure).
   - *Needs code ([lever 1](triad-gpu-optimization.md#next-levers-in-order)):*
     timestamp query pools in the fine-stage family contexts, per-family
     device intervals, call and download counts, session-mutex wait and the
     gain-rank batch width. Until then fine-stage device time is unmeasured.

   Where each field is:

   | Field | Command and JSON path | Note |
   | --- | --- | --- |
   | Stage seconds, evidence, decision, `signed_lag_gain.*`, `varying_*`, `drift_*` | `admission`: the `phases` entry named `native_slot`, its `work` object | — |
   | Process CPU | `admission`: `phases[].process_cpu_seconds`; busy CPUs = that ÷ the same phase's `wall_seconds` | `mean_busy_hardware_fraction` exists only in `message` and `backlog` `result.stream` |
   | Backend counters: `vulkan_*`, `opencl_*`, `cuda_*`, `peak_reserved_bytes`, `upload_bytes`, `vulkan_gain_rank_band_ratio` | `identity.backend_receipt` | `opencl_coarse_score_rows`, `opencl_coarse_issued_products` and `opencl_resident_nvfp4_modes` are always present, automatic runs included; only the `opencl_*_seconds` intervals need `--opencl-profile on` with `--backend vulkan` |
   | Selected backend | `identity.selected_accelerator_backend` (`cuda` or `vulkan`) | `--backend gpu` runs only |
   | Coarse sub-phases, `minimum_eigenvalue_ratio`, fine `times` | `slot` and `continuation`: `phases[]` entries `coarse_prepare`, `coarse_tiles` (each `work` holds `minimum_eigenvalue_ratio`, `tile_width`, `route`, `fft_precision`), `coarse_selection`, then `fine_prefix` and `fine_continuation` (or `fine_work`) and `fine_finish`, with fine timings at `work.times` | `admission` omits them |
   | Host quote | `plan`: `configuration.receiver_slot_workspace_bytes` | The CPU route's quote; device-route host quotes come on top |
2. **Estimate before building** with the [eligibility procedure](#eligibility-procedure).
   First run the cheapest falsification: read the code or receipt that
   decides feasibility (does the input repeat between slots, is the stage
   arithmetic- or traffic-bound, does the route exist on this build).
3. **Components.** Emulated OpenCL tests locally
   (`triad_opencl_nvfp4_emulated`, `triad_opencl_coarse_session_emulated`
   and its one-ray variant, `triad_opencl_fine_session_emulated` and its
   deferred variant), `./build.sh test triad`, then the device tests
   (`test_triad_opencl_nvfp4`, `test_triad_opencl_fine_session`,
   `test_triad_vulkan` in its default, `--segmented`, `--workspace-admission`,
   `--host-scoring`, `--core`, `--gain-rank`, `--drift` and `--varying`
   modes) and `ptxas -v` registers and spills. On CUDA builds the
   segmented counterpart is `test_triad_tensor --cuda --segmented-only`
   (`cuda_segmented_timing_oracle`, a time-domain reference without FFTs,
   plus pinned frequency tiles); there is no CUDA segmented benchmark mode.
   Component rates
   (`--segmented-benchmark`, the gain-rank oracle's `device_call_seconds`)
   are not production rates. `--segmented-benchmark` prints only
   `schema`, `fft_rows`, `arrivals`, `blocks`, `frequencies`, `phases`,
   `tracking_hop`, `vulkan_seconds_per_workspace`,
   `native_seconds_per_workspace_all_threads`, `native_threads`,
   `device_name` and `timing_note` for three
   fixed shapes (16,384 points with 257 and 515 frequencies, 32,768 with
   261), through one session; it writes no receipts, cannot show
   multi-owner L2 or queue contention, and a tile-width sweep needs source
   edits in both backends (`segmented_tile_limit` in Vulkan and the 128 in
   CUDA `segment_plan`). It has run on the Radeon and on
   [Blackwell](validation.md#inside-vulkan-segmented-timing).
   `--device-memory-limit-bytes` is a probe option, which
   `--backend gpu` rejects.
4. **Predeclare** before renting or timing, in this shape:

   ```text
   Question:   one sentence.
   Change:     route, arithmetic or kernel; exactness class E1|E2|E3|E4.
   Strata:     easy (matched or 72 Hz/1x), regression only;
               challenging (72 Hz/5x control); holdout (36 Hz/10x).
   Binary:     one probe for every route; any reference built on the same
               host with the same toolchain; --capture-cache DIR.
   Order:      alternated pairs, e.g. N C C N, or N G D V A A V D G N for
               the three families; two pairs per cell on a pod, three locally.
   Pass (all): P1 named component tests pass;
               P2 the class gate (table below);
               P3 zero declines and fallbacks on explicit device runs;
               P4 device stage faster in both pairs on the challenging slot,
                  and that slot not slower beyond the predeclared noise
                  band in either pair;
               P5 flipped automatic runs complete with identical decisions
                  and no new fallbacks.
   Easy slot:  regression check only; an easy-slot loss never vetoes a
               challenging-slot gain (report it).
   Stop:       time budget; stop at the first P1 or P2 failure (E4: a
               budget excess calls the predeclared sensitivity trial); no reruns
               to chase a pass.
   Deviations: written down before the runs they affect.
   ```

5. **Gates by class.**

   | Class | Gate |
   | --- | --- |
   | E1 | Identical lags, evidence and decisions; `band_ratio` below 1 and recorded; fallback reasons counted |
   | E2 | 1e-12 norm-relative per statistic group; 1e-8 nats absolute on the named fixtures, rescaled above about 1e6 nats |
   | E3 | Identical decisions on every stratum; selection differences against an FP64 control reported (for drift, the retained digest); frontier retention sweep for new frontiers |
   | E4 | Predeclared evidence budget; per-block fallbacks for range, nonfinite values and fragile pivots; identical decisions; native rerun near the margin |

6. **Time and compare.** Follow
   [paired timing](triad-performance.md#paired-timing-and-load-gating) and
   [Compare routes](triad-gpu-optimization.md#compare-routes): explicit device
   against native answers whether the device is faster for a stage; the
   flipped against the unflipped automatic build answers whether the default
   gets faster. Do both before a flip. Decide on slot totals; stage timers
   are shares. Pods use P1–P5 with two alternated pairs per cell (the device
   stage faster in both pairs on the challenging slot, that slot not slower
   beyond the predeclared noise band). Locally a change counts only if all three pairs agree in
   direction and the median moves by more than 3%. A holdout run costs about
   5 minutes automatic and 12 unflipped, which caps holdout cells at a few
   per pod hour; extrapolate HF from the holdout instead of rerunning it.
7. **Pod checklist.** Identify the device with
   `nvidia-smi --query-gpu=name,memory.total,compute_cap,driver_version,mig.mode.current --format=csv`
   and record CPU model, quota, affinity, cgroup memory (`memory.max`) and
   `W`; deploy the Vulkan/OpenCL build (`PUMP_LAB_MODE=vulkan`) and run every
   probe through the `filtered` launcher of
   [Reproduce the matched Blackwell runs](triad-gpu-optimization.md#reproduce-the-matched-blackwell-runs):
   a plain SSH shell can select the GLX-based ICD and fail Vulkan
   initialization. Check `filtered "$probe" describe --backend vulkan
   --require-accelerator on` first. Then run `filtered "$probe" describe
   --backend gpu` and require `identity.backend_receipt.vulkan_segmented_qualified_device`
   and `vulkan_gain_rank_qualified_device` to be true: if OpenCL did not
   open, automatic runs still select `vulkan` but quietly keep segmented
   timing and the gain rank native (`unqualified-device`), and a pod hour
   times the wrong route. On every `--backend gpu` receipt check that
   `identity.selected_accelerator_backend` is `vulkan` (a build with CUDA
   selects CUDA first), and on the 72 Hz/5× control that
   `vulkan_segmented_sessions` and `vulkan_gain_rank_sessions` are nonzero
   before using its timings. Size memory as in
   [Size memory on a pod](#size-memory-on-a-pod). Planning figures from the records: stock-image setup about
   nine minutes; a fresh Ninja build of the deployed receiver about 15 s with
   13 jobs; the 72 Hz/1×
   slot 6–7 s, the 72 Hz/5× control 18–20 s automatic (38–56 s unflipped),
   the holdout 281 s automatic (688 s unflipped) plus about 10 s of
   simulation on its first run and about 0.2 s with `--capture-cache`; the
   last two qualification pods ran 0.47 h and 0.81 h. Steps, memory pitfalls
   and slot arguments: [bounded device qualification](triad-gpu-optimization.md#run-a-bounded-device-qualification)
   and [qualification slots](triad-gpu-optimization.md#qualification-slots).
8. **Measurements that need no code change**, ranked with commands,
   memory settings, durations and the lever each outcome re-ranks, in the
   [one-hour pod plan](#one-hour-pod-plan-worked-example). Prepare and gate
   code candidates locally before renting (step 3, `triad_gpu_bundle.py`,
   `plan` at the target `W`), so pod time goes to device gates and pairs
   ([open measurements](triad-gpu-optimization.md#open-measurements)).
9. **Utilization figures.** Issued-equivalent Tensor throughput =
   2 × `opencl_coarse_issued_products` ÷ the product interval of the explicit
   profiled route; the slot average divides by slot seconds instead
   (3.0e12 ÷ 2.6 ms ≈ 1.15 PFLOP/s; ÷ 0.064 s ≈ 47 TFLOP/s). Counter-based
   Tensor duty and occupancy need hardware counters, which rented pods
   refused (`ERR_NVGPUCTRPERM`); no bypass was attempted. On an owned host an
   administrator can allow them
   ([NVIDIA](https://developer.nvidia.com/nvidia-development-tools-solutions-err_nvgpuctrperm-permission-issue-performance-counters)).
   Permitted evidence so far: OpenCL event intervals, CUPTI resource
   callbacks naming loaded kernels, and static opcode counts from
   `cuobjdump -sass`. To show which instructions a kernel contains:
   - OpenCL PTX: `ptxas -arch=sm_120a -o kernels.cubin triad_opencl_nvfp4.ptx`,
     then count `OMMA` (E2M1), `DFMA`, `DADD`, `DMUL`, `F2F.F32.F64` and `I2F`
     in `cuobjdump -sass kernels.cubin`;
   - CUDA libraries: no in-tree tool exists and the earlier capture's
     commands were not kept (its file holds only symbols, module hashes and
     opcode counts). Register a CUPTI resource callback for module loads
     (`CUPTI_CBID_RESOURCE_MODULE_LOADED` hands over the cubin), write each
     cubin, match the timed kernel symbols, then count `HMMA`, `IMMA`,
     `HADD2`, `HFMA2` and `HMUL2` with `cuobjdump -sass`
     ([CUDA record](validation.md#current-cuda-precision-and-tensor-dispatch),
     [CUPTI](https://docs.nvidia.com/cupti/));
   - RADV: run with `RADV_DEBUG=shaders,nocache` and
     `MESA_SHADER_CACHE_DISABLE=true` and search the dumped shaders for
     `v_pk_mul_f16` or `v_pk_fma_f16`, as the Radeon precision screen did.

   Static opcodes prove what a kernel contains, not dynamic issue. No
   utilization figure is a pass threshold; decide on complete service.
10. **Record and flip.** [Decide, flip and record](triad-gpu-optimization.md#decide-flip-and-record):
    flip constants, the validation entry (device, host, quota, `W`,
    toolchain, hashes, rules, deviations, limits), the route table and the
    evidence directory with its purpose and cleanup trigger
    ([material lifecycle](development.md#material-lifecycle)). A pod that
    flips nothing still records its new splits (for example
    `varying_coarse_seconds`, the `W` = 80 `plan` quote, the E4 fields, lease
    and allocation peaks) in a validation entry, and updates
    [Where the time goes now](#where-the-time-goes-now), the
    [opportunity map](#tensor-core-opportunity-map-for-the-challenging-slots)
    and the GPU guide's [open measurements](triad-gpu-optimization.md#open-measurements).

The recurring misreadings are collected in [Common mistakes](#common-mistakes).

### Size memory on a pod

- Omit `--memory-limit-bytes`. The default is cgroup-aware available
  memory minus `min(2 GiB, max(512 MiB, 10%))`
  ([development contract](development.md#overlap-within-a-slot)); the
  Xeon 6952P pod's `memory.max` was 187,999,997,952 B. Never set it to
  `W ×` a per-owner figure (`80 × 21.5 GB` is about 1.7 TB).
  `--slot-owners` (default `--workers`) matters only for `message` and
  `backlog`; `admission` ignores it.
- The probe's host check (`complete_probe_slot` in the
  [probe](../tools/triad-receiver-probe.cpp)) requires
  `receiver_slot_workspace` at the run's `--workers` to fit in the host
  limit less the controller footprint and `W − 1` worker stacks; otherwise
  the run ends "Native full-slot probe exceeds its local memory lease". The
  quote grows with `--workers`, more slowly than in proportion (HF −22.5:
  144.4 GB at W = 26, 204.6 GB at W = 54). The holdout's was
  21,475,814,336 B at W = 26. At W = 80 a 64 GB limit failed this check
  with a 48 GiB explicit device workspace, which the same check charges
  (next bullet); the W = 80 quote was not kept, so the failure's
  composition is unknown. Run `plan` at W = 80 for the holdout (pod plan
  item 0).
- Device routes add host quotes that `plan` omits (it always quotes the CPU
  route):
  - an explicit Vulkan coarse session's whole device ceiling: with
    `--device-memory-limit-bytes` there is no automatic frequency plan, so
    the session quotes the explicit workspace plus its 128 MiB compilation
    bound on the host (`coarse_host_bytes` in the
    [Vulkan backend](../src/triad_vulkan_backend.cpp));
  - on the SM 12.0 route the larger caches, 59,088,896 B per owner and
    once more in the reservation;
  - the gain rank's 6.8–72 MB per owner (71.6 MB on the holdout);
  - each family's 128 MiB compilation bound; on discrete GPUs the
    host-read buffers.

  So the host limit must cover the controller footprint,
  `receiver_slot_workspace(W)`, `W − 1` stacks, any explicit device
  ceiling and those quotes. Run `plan` at the pod's `W` for each slot and
  record its quote.
- Device memory fields mean different things per backend (code:
  `Shared::charge` in the [Vulkan core](../src/triad_vulkan_core.hpp), the
  allocator statistics in the [CUDA backend](../src/triad_tensor_backend.cpp)):

  | Field | CUDA build | Vulkan/OpenCL build |
  | --- | --- | --- |
  | `peak_reserved_bytes` | Lease counter: each lease takes `min(preferred, free, ceiling)`, so it can reach `workspace_limit_bytes` by design (HF 99.2 GB: initial free 101.4 GB less 2 GiB) | Peak of charged Vulkan buffer and library allocations, at their allocation extents, charged before `vkAllocateMemory`; OpenCL buffers are leased, not charged (holdout 51.8 GB) |
  | `allocator_peak_reserved_bytes`, `allocator_peak_live_bytes` | The ATen CUDA caching allocator's physical peaks (HF 38.6 and 31.5 GB) | Equal to `peak_reserved_bytes` |
  | `vulkan_peak_leased_bytes`, `vulkan_peak_owner_lease_bytes` | — | Peak sum of all leases, Vulkan and OpenCL sessions, and the largest one; on automatic runs with `--require-accelerator on` the probe checks allocator peak ≤ peak leased ≤ `vulkan_shared_workspace_limit_bytes` |
  | `workspace_peak_bytes` | Admitted lease quote, not a measured peak | Same |

  Before deciding that a device port does not fit, or that a smaller card
  needs narrower batches, record these fields and measure the smallest
  explicit `--device-memory-limit-bytes` at which the slot completes with
  the same routes and no fallbacks.
- A 4 GiB explicit workspace is too small for the holdout's coarse
  session: for explicit holdout runs try `--device-memory-limit-bytes` of
  16–32 GiB with no host limit, and remember that the host quote includes
  it. On the 72 Hz/5× control a 9 GB host limit refused W = 80, 64 GB
  worked, and explicit runs used 4 GiB of device workspace.
- A varying batch takes about 23.1 MB per block in FP64 on the 72 Hz/1×
  shape, so the 256 MiB default admits 6 of its 52 blocks. Other shapes'
  per-block leases are unmeasured; at the same figure the 72 Hz/5×
  control's 172-block host batch needs about 4 GB and 256 MiB admits about
  10 (172 halved four times).

### One-hour pod plan (worked example)

For one full RTX PRO 6000 at about a 40-CPU quota, with no code change, in
the order that settles the most open causes per minute. Every probe runs
through `filtered`; arguments are those of
[Qualification slots](triad-gpu-optimization.md#qualification-slots).

```bash
# filtered: the launcher defined in Reproduce the matched Blackwell runs.
probe=/workspace/pump-lab/build/lab/triad_receiver_probe
W=80   # twice the quota's whole CPUs (40.8-CPU quota)
common=(--case hf --encrypt on --helper off --path-loss-db 180 --noise-dbm-hz -164
  --timing-policy broad --base-oscillator gpsdo-xo --shift-oscillator gpsdo-xo
  --spread-preset hf-worst --selective-perturbation each-65 --channel-model gui --margin 3
  --burst-wait-tiles 0 --rf-hz 30000000 --seed 940600 --bit 0 --stratum active)
control=(--data-power-w .002 --fhss on --rate 72 --added-symbols 5 --added-interleave on
  --target-cn0 4 --max-samples 40000000 --deadline-ms 1800000)
hold=(--data-power-w .1 --fhss on --rate 36 --added-symbols 10 --added-interleave on
  --target-cn0 -12.9897 --max-samples 26000000 --deadline-ms 1800000)
cache=$(mktemp -d)
run() { filtered "$probe" admission "${common[@]}" "$@" --capture-cache "$cache"; }
explicit=(--backend vulkan --require-accelerator on --vulkan-segmented-timing device)
filtered "$probe" plan "${common[@]}" "${hold[@]}" --workers "$W" > holdout-plan.json  # item 0
run "${control[@]}" --backend gpu > control-auto-1.json           # item 1
run "${hold[@]}" --backend gpu > holdout-auto-1.json              # item 1
run "${control[@]}" --backend gpu > control-auto-2a.json          # item 2: A
run "${control[@]}" "${explicit[@]}" --vulkan-gain-rank device \
  --device-memory-limit-bytes 4294967296 > control-gain-1.json     # item 2: E
# item 2: repeat E (control-gain-2.json), then A (control-auto-2b.json)
```

| Item | Minutes | Runs | Read | Outcome → decision |
| --- | --- | --- | --- | --- |
| 0. Setup | About 20 | Stock image (about 9 min); `nvidia-smi` identity, CPU, quota, `memory.max`; deploy; Ninja build (about 15 s per binary); `describe` under the filter; the route check (`describe --backend gpu`); `plan` for both slots at the pod's `W` | `selected_accelerator_backend`, `vulkan_segmented_qualified_device` and `vulkan_gain_rank_qualified_device`, `receiver_slot_workspace_bytes` (the holdout's W = 80 quote was never kept); setup duration (not recorded before) | Stop the menu if `describe` fails or either qualified field is false. The device tests ("identity gate" of the [bounded qualification](triad-gpu-optimization.md#run-a-bounded-device-qualification), step 5) are needed only for a code candidate |
| 1. Split the remainders | About 7 | 72 Hz/5× automatic twice (about 20 s each), holdout automatic once (about 5 min, plus about 10 s of first simulation) | `coarse_seconds`, `varying_coarse_seconds`, `signed_lag_gain.proposal_seconds`, `vulkan_segmented_sessions` and declines, `vulkan_device_command_seconds`, `vulkan_fft_plan_seconds`, the varying and drift sub-timers, `opencl_coarse_score_rows`, `opencl_coarse_fallback_counts`, the E4 fields | Segmented span most of the remainder: lever 6 rises and the kernel trace is next. Otherwise the remainder is preparation and frequency search: split it with `slot` sub-phases on the next pod and rank the varying-passband synthesis row |
| 2. Automatic against explicit gain rank | About 3 | 72 Hz/5× in the order automatic, explicit, explicit, automatic (A E E A), the explicit runs with `explicit` plus `--vulkan-gain-rank device` and a 4 GiB device workspace; two alternated pairs. Pairing with item 1's earlier runs instead is indicative only | Gain-rank stage, `vulkan_gain_rank_upload_bytes`, `vulkan_peak_leased_bytes`, band ratio | Gap reproduced: the gap row (lever 9) rises; gone: the earlier gap was load or host noise |
| 3. Varying batch width | About 3 | 72 Hz/5× with `explicit` plus `--vulkan-varying-fine device --vulkan-varying-arithmetic fp64`, device workspace 8 GiB (8589934592) against 4 GiB, one pair | `varying_batches`, `varying_batch_blocks`, `varying_{projection,statistics,recursion,wait}_seconds` | Wider batches and a faster stage: batch narrowing was a cause, lever 3's first step is confirmed |
| 4. Quota against affinity | About 2 | 72 Hz/5× automatic restricted to 13 CPUs by affinity (start the shell under `taskset -c 0-12`; W becomes 26) against the full quota (W = 80), two pairs | Varying fine and drift per block, process CPU | Equal per-block time: not quota contention; lever 4 moves to thread-state samples |
| 5. Holdout family routes | About 15, optional. One explicit varying pair at W = 54 took 192–193 s per slot | Start from the [48 GiB device, 100 GB host, gain-rank device recipe](validation.md#vulkan-fine-stage-attribution-and-staging--21-november-2026). Verify the quote at the pod's actual W, then compare `--vulkan-drift device` or a new varying candidate with its native control | Family and complete-slot times, payloads, leases, declines, fallbacks and public evidence | The repaired varying route saved 4.79 s in the stage and 0.60 s overall in one pair. Require a reproducible complete-service or workspace advantage before changing automatic selection. |
| 6. Kernel trace | About 3, if permitted | The untested Nsight Systems command of [open measurements](triad-gpu-optimization.md#open-measurements) on one automatic 72 Hz/5× run, with the relay's `gpu_environment()` exported; the no-socket filter cannot wrap `nsys`, so this run is a diagnostic outside the worker boundary and its timings are not qualification timings | Kernel intervals and idle gaps per family | Gives family device time without lever 1's code |
| 7. Close | About 5 | Copy receipts with UUIDs removed to `local-evidence/`, terminate, verify absence | — | Record the new splits even without a flip ([step 10](#test-recipe)) |

Stop rule: skip item 5 when items 0–4 overrun, never extend the pod to
rerun an item, and record every deviation before the run it affects.

### One-hour pod with one code candidate

Use this when a candidate is built and gated locally (step 3,
`./build.sh test triad`, its device tests added to
[`triad_gpu_bundle.py`](../tools/triad_gpu_bundle.py), the bundle built,
`plan` at the target `W`). Choose a candidate whose cheapest check (above)
already supports it; if the attribution it depends on is missing, spend
the pod on the no-code plan instead, or ship the candidate with its own
attribution field (lever 9 with lever 1's gain-rank batch width).

| Item | Minutes | Runs | Outcome → decision |
| --- | --- | --- | --- |
| 0. Setup | About 20 | Deploy the reference and candidate with `--mode gpu --profile simulation` on the same host and confirm `PUMP_LAB_MODE=vulkan` for both. Use each revision's returned path, build both probes with the same toolchain and record both source manifests and probe hashes ([reproduce](triad-gpu-optimization.md#reproduce-the-matched-blackwell-runs)) | Hash mismatch or build failure: stop |
| 1. Device tests | About 5 | On the candidate, filtered, standard input from `/dev/null`: `test_triad_opencl_nvfp4`, `test_triad_opencl_fine_session`, `test_triad_vulkan` default, `--segmented`, `--workspace-admission`, `--host-scoring`, `--core`, `--gain-rank`, `--drift`, `--varying`; `ptxas -v` registers and spills for PTX changes | First failure: stop |
| 2. Defaults unchanged | About 3 | Reference against candidate on `--backend gpu`, matched fixtures `b4d8fa5a…`, `2b9ca9df…`, `eaa4ae2a…`, `4a3603c8…` in both orders | Any result, evidence or capture-hash difference not predeclared: stop |
| 3. Pairs | About 5 | N C C N automatic on the 72 Hz/5× control, two alternated pairs, reading the no-code item 1 fields and the lease fields | Decide on the stage and the [noise band](#default-thresholds-and-stop-rules) |
| 4. Holdout | About 11, optional | One N C pair on the holdout (about 5 minutes each) | Indicative only; a second pair does not fit this hour |
| 5. Close | About 5 | As item 7 above | Record either outcome |

This replaces items 1–6 of the no-code plan; keep item 1's runs only if
they come from the reference binary.

## Per-GPU guidance

### RTX PRO 6000 Blackwell

The only qualified device class. Runs every SM 12.0 default above. This is
the order to give for ranked next steps on this card; the GPU guide's lever
numbers are identifiers. For a request that stresses Tensor Cores keep this
order and label each item T, G or C from the
[opportunity map](#tensor-core-opportunity-map-for-the-challenging-slots):
items 1–5 are G or C (lever 3's last step aside), item 6 holds the only T
items with a target.
1. the no-code items of the [one-hour pod plan](#one-hour-pod-plan-worked-example)
   (split the coarse remainders, the automatic-versus-explicit gain rank,
   the varying batch width, quota against affinity);
2. lever 1's code (family device intervals, mutex wait, batch width) and
   the kernel trace of the CUDA segmented stage;
3. the same-pod comparison of the CUDA and Vulkan/OpenCL automatic routes
   at holdout shape, which decides where HF runs (lever 7,
   [worked example](#worked-examples));
4. the drift and varying redesigns (levers 2 and 3);
5. the segmented per-call or traffic levers that the trace selects, and the
   envelope (levers 5 and 6);
6. the large dense plans (lever 11) and the varying-passband synthesis row,
   the only T items with a known target.

Levers 4 (native scaling), 8 (overlap), 9 (gain-rank admission) and 10
(fused rank) are ranked by what items 1 and 2 show: quota against
affinity, busy CPUs, the A E E A gap and the batch width.

FP16 Tensor is only 4× FP32 SIMT and FP4 16×, so a contraction that cannot
use FP4 needs an arithmetic share well above its surrounding traffic to pay
off (the DFT-to-modes GEMM does not have one: [step 7](#eligibility-procedure)).
Device memory has not been shown to bound HF ports on this card: the HF
CUDA receipt's 99.2 GB `peak_reserved_bytes` is its lease reaching the
workspace limit, and the allocator peaked at 38.6 GB reserved and 31.5 GB
live of 97,887 MiB. Size a drift or varying port at HF from the measured
minimum lease ([memory sizing](#size-memory-on-a-pod)).

### RTX 5090

Inherits every SM 12.0 route unmeasured: OpenCL reports it as compute
capability 12.0. Measure it first among the other generations. Differences
that can move verdicts: 32 GB of device memory (size owners and
`--device-memory-limit-bytes` with `plan`); FP16 and FP8 Tensor rates halved
under FP32 accumulation (FP16 209.5 against 419 TFLOP/s with FP16
accumulation, where the PRO 6000 keeps 503.8) while FP4 stays at 1,676; and a
desktop host with fewer CPUs, so device-versus-native margins differ from the
40.8-CPU pod's. Run the identity gate and the qualification slots on the
automatic route before trusting any inherited default.

### RTX 4090, RTX 4090 Laptop and RTX 4060 Ti 16 GB

Ada, SM 8.9: FP8, FP16, BF16, TF32, INT8 and INT4 Tensor arithmetic, no FP4
and no hardware block scaling. A build with both backends tries CUDA first,
so automatic runs reach the Vulkan routes only when CUDA is unavailable.

| Stage | CUDA build, automatic | Vulkan build, automatic | Explicit flags for a candidate | Envelope (code) | Witness |
| --- | --- | --- | --- | --- | --- |
| Affine broad frequency search | Half cuFFT inside its gate, else FP32 cuFFT | FP32 VkFFT | `--backend cuda --tensor-coarse-math fp16-fft`; `--backend vulkan --vulkan-precision fp32` or `fp16-storage` | NFFT 4,096–2^20, at least two arrivals, Gram ratio at least 1/16, NFFT × admitted frequency tile × min(template-bank workspaces, 8) × taps ≥ 2^19 | Half-FFT counters and `cuda-fp16-fft-*` reasons |
| Lag-group matrix | INT8 inside its gate | Native CPU | `--backend cuda --tensor-coarse-math int8` or `fp16` | One group, one tap, K 16–8,192, F 8–256, at least 1,664 rows, tile at least 26, A at least 32, padded cost at most 30 paired-FFT row stages, power-of-two convolution | `cuda_matrix_lt_imma_calls`, `cuda_matrix_lt_hmma_calls`, `cuda_matrix_padded_real_macs` |
| Segmented timing | Native (the gate needs compute capability 12.0) | Native (`unqualified-device`) | `--backend cuda --cuda-segmented-timing on`; `--backend vulkan --vulkan-segmented-timing device --require-accelerator on` | 16,384 or 32,768 points, at least 4,096 arrivals and 32 blocks, radius at least 128, at most two phases, hop at most 4 | `cuda_segmented_*`, `vulkan_segmented_*` |
| Signed-lag rank | Native | Native (`unqualified-device`) | `--backend vulkan --vulkan-gain-rank device` | At most 64 shift runs; audit blocks present | `vulkan_gain_rank_*`, `signed_lag_gain.proposal_route` |
| Drift, varying | Native | Native | `--vulkan-drift device`; `--vulkan-varying-fine device --vulkan-varying-arithmetic fp64` | Shader FP64 and INT64 | `vulkan_drift_*`, `vulkan_varying_*` |
| Fine projection | ATen FFT and an index gather; AVX2 CPU for one-tap shapes of at most 1,024 rows and 40 modes at 26 or more workers | Native (the `sm_120a` module cannot load) | `--tensor-fine-projection` (explicit CUDA qualification; check the executed route) | — | Route strings |
| Dense FP4 frontier, resident fine, FP32 per-row kernels | Absent | Absent | None | `sm_120a` PTX only | `opencl_*` zero |

The code accepts the explicit Vulkan `device` routes on Ada, but no Vulkan
route has run on an Ada device, so every Ada timing below is a first
measurement; automatic selection also needs a class identification without
the OpenCL module. The Vulkan varying family is FP64 (E2), which runs at
about 1/64 rate on Ada. Device-versus-native margins are host-specific: the
"device slower" drift and varying verdicts came from the 40.8-CPU pod at
W = 80, so re-time them on the desktop or laptop host.

Measured on the RTX 4090: half cuFFT broad search 0.2053/0.2050 →
0.1939/0.1893 s (active/noise); automatic INT8 complete message 7.04 →
6.85 s, the IF450/F192 holdout (not the 36 Hz/10× qualification holdout)
7.688 → 7.528 s, but the 25-slot backlog 3.47% slower
([worked example](#worked-examples)); an explicit INT8 five-slot message
32.99 → 35.28 s stream wall; INT8 workspace with three owners 438/222 MB
reserved/live against 107/20 MB for the FFT; overlap gains above; the
`fp32-rn4-v1` mapping contract passes the RTX 4090 suite; component lag
matrices ran 3.52× (FP16) and 3.50× (INT8) faster than the FFT at a
two-tap geometry, which the one-tap automatic INT8 gate excludes, so they
do not calibrate it
([CUDA record](validation.md#current-cuda-precision-and-tensor-dispatch),
[hardware checks](validation.md#representative-receiver-hardware-checks)).
No HF, holdout or 72 Hz/5× slot has run on an RTX 4090.
Historical RTX 4090 results (retired receivers): FP16 replay 2.3% slower
than FP32 and batch 8 23.5% below batch 1; fused FP16 packing 1.49× on the
kernel while the receiver lost; K32 FP8 partials cut error 21.85–26.62× at
an unchanged 0.15 ms. No SASS or CUPTI capture exists for SM89, so whether
cuBLASLt used HMMA or IMMA there is unproven. Half cuFFT is not a Tensor
route: on SM120 its functions hold packed `HFMA2`, `HADD2` and `HMUL2`, and
Ada's packed FP16 SIMT rate equals FP32, so its gain is halved traffic.
Capture both as in [test recipe step 9](#test-recipe); on an owned host an
administrator can also allow hardware counters.

First experiments, in order:
1. Qualify `--backend cuda --cuda-segmented-timing on` on the 72 Hz/5×
   control and the holdout, then widen the `automatic_sm120` condition in
   `TensorCoarse::plan_segmented` ([backend](../src/triad_tensor_backend.cpp)).
   Segmented timing is the largest challenging-slot stage; expect it to
   scale with memory bandwidth if it is bandwidth-bound (the 4090 has
   1,008 GB/s against 1,792), which the kernel trace must confirm. Once it
   runs on SM89 its follow-on G levers are those of the
   [proposed table](#proposed-and-untried), each with its stop rule: the
   kernel trace, graph replay (historically 1.27× on the PRO 6000 and
   2.80–4.00× on the RTX 5090), cuFFT callbacks or cuFFTDx fusion, pruning
   the inverse output to 10,553 of 32,768 lags, FP16 spectra storage, and L2
   persistence or L2-sized tiles (a 128 × 32,768 tile is 33.5 MB per owner
   against the 4090's 72 MB L2; query the others' L2).
2. On a Vulkan build, time the explicit `--vulkan-gain-rank device` and
   `--vulkan-segmented-timing device` routes on the same slots now; the
   gain rank was the largest measured Blackwell win (holdout 688 → 281 s).
3. Re-test the INT8 backlog regression and recalibrate the SM89 gate
   ([worked example](#worked-examples)).
4. cuFFT forms of the gain rank and the other families for CUDA builds
   (lever 7).
5. An INT8, FP8 or INT4 (`m16n8k64` with software block scales) form of the
   dense frontier search, with INT32 or FP32 partials, porting the FP32
   per-row kernels to `sm_89` with it; rescale the `dense-cost` bound by the
   peak ratio (4090 INT8 about 0.33× and INT4 about 0.66× PRO 6000 dense
   FP4). The per-row kernels alone gain nothing without a dense search or
   resident route to feed. It reaches none of the three challenging slots
   (their segmented sessions withhold the bank) until device synthesis of
   varying-passband banks (lever 12) exists, so it fails the T build
   threshold until then.
6. FP16-accumulate partials for proposal-only contractions (2× the
   FP32-accumulate rate on GeForce), K-bounded with FP32 outer sums.
7. Check resizable BAR in a Vulkan build: every Vulkan buffer must be
   host-visible and coherent, so without it the automatic workspace may be
   small; record `vulkan_shared_workspace_limit_bytes`.

A Vulkan cooperative-matrix kernel, the only matrix route of a Vulkan build
here, needs: the `VK_KHR_cooperative_matrix` extension (NVIDIA also exposes
a flexible-shape variant), a shape query with `vulkaninfo` on the device
(the PRO 6000 reported FP16 16×8×16 and 16×16×16, BF16 16×16×16, FP8 E4M3
and E5M2 16×16×32, INT8 16×8×32 and 16×16×32, no FP4), GLSL
`GL_KHR_cooperative_matrix` compiled by glslang 15.1.0 or later (it
compiled and validated an FP16 shader there), a shader index in its
family's registry range of the Vulkan core, a first target that is a
post-isolation contraction with a share of at least 5% of a target slot
(an INT8 or FP8 dense frontier with software block scales, or the fine
DATA correlations), the E3 planted-arrival gate and an executed-arithmetic
witness. Radeon Vega has no matrix hardware, so no cooperative-matrix route
is expected there.

Memory: on the PRO 6000 the HF slot's CUDA allocator peaked at 38.6 GB
reserved and 31.5 GB live, more than a 24 or 16 GB card holds; its 99.2 GB
`peak_reserved_bytes` is lease accounting that grows to the workspace
limit. Whether these cards need narrower batches and more calls depends on
the minimum lease the slot completes with, which is unmeasured
([memory sizing](#size-memory-on-a-pod)). Run `plan` at the target `W`,
record `peak_reserved_bytes`, `allocator_peak_reserved_bytes`,
`allocator_peak_live_bytes`, `workspace_peak_bytes`,
`vulkan_shared_workspace_limit_bytes` and, on a Vulkan build,
`vulkan_peak_leased_bytes` (there `peak_reserved_bytes` is the allocation
peak, not a lease counter), and check a narrowed batch against
the 10× fixed-cost rule of [step 8](#eligibility-procedure).

Unpublished figures: NVIDIA states no memory bandwidth or L2 size for the
4090 Laptop or the 4060 Ti 16 GB (256- and 128-bit buses). No in-tree
bandwidth tool exists: measure with a device-to-device copy benchmark such
as the CUDA samples' `bandwidthTest --dtod` or NVIDIA's
[nvbandwidth](https://github.com/NVIDIA/nvbandwidth), query
`cudaDevAttrL2CacheSize`, and record the link and sustained clocks
with `nvidia-smi --query-gpu=clocks.sm,power.draw,temperature.gpu,pcie.link.gen.current,pcie.link.width.current --format=csv -lms 500`
during the run. Laptop results need the OEM power configuration recorded.
The 4060 Ti has the narrowest bus of these targets, so traffic-cutting
levers likely rank higher there (*inferred*).

### Radeon Vega (Ryzen 5 PRO 5650U)

Vulkan through RADV. Packed FP16 vector arithmetic at twice FP32, no matrix
units, no accelerated 8-bit dot, an FP64 rate with no exact-device source,
and memory shared with the CPU. Automatic runs the FP32 VkFFT search,
FP16-storage FFTs and the packed direct route in its envelope (RADV, PCI
`1002:1638`, one ray, one arrival, at most 131,072 rows); segmented timing
and the families stay native.

On the 72 Hz slots the coarse stage is 87–96% of the slot (20.7–21.7 s of
22.9–23.9 s at 72 Hz/1×, 240 s of 250.9–251.6 s at 72 Hz/5×). Native
segmented timing dominates it: on the 20 November idle host the segmented
stage (`varying_coarse_seconds`) was 20.2–20.3 s of a 23.5–23.6 s 72 Hz/1×
slot, about 86% (ceiling about 7×); the 72 Hz/5× stage was not split. The
packed and FP16-storage envelopes do not reach segmented timing, so
packed-dot tuning has an Amdahl ceiling near 1.0 there and is a
regression-level item. In order:
1. Segmented timing, three levers: a multi-owner backlog of the explicit
   route, which used 29 against 258 process CPU-s on the 72 Hz/1× slot (10%
   slower alone) and 260 against 2,860 CPU-s on the 72 Hz/5× control on a
   loaded host (an open measurement); a deterministic CPU/GPU split of workspaces; FP16
   storage of segmented spectra and a fused shifted-product load (shared
   DDR4, 51.2 GB/s conditional; the stage is far below any ridge).
2. Splitting varying host batches into device calls (the unified-memory
   envelope cut batches from 52 to 3 blocks, 4 to 55 calls).
3. A gated 72 Hz/5× pair of the explicit `--vulkan-gain-rank device` route
   (no gated Radeon timing exists), then the fused local-memory gain-rank
   kernel (about 45 s at HF on Vega, *modeled*), the prerequisite for a
   Radeon gain-rank class.
4. Removing the drift session mutex.

Packed dot: 2.88× on the dot (0.0634 against 0.1824 s) and 1.027× on the
slot, combining storage and arithmetic. In a four-arm trial with mixed
storage (half-storage FFT 1.037 s, FP32 direct 0.578 s, FP16-storage FP32
direct 0.643 s, packed 0.573 s), packed tied FP32-storage FP32 direct
(paired 0.975–1.078×) and beat the same-storage FP16-storage FP32 direct
1.122×; most of the gain came from removing the FFT. The fused-trial rates were
47.0, 47.6 and 54.5 GF/s, and the record leaves the limit undetermined
(traffic, scheduling, synchronization, instruction mix or clock): measure
with RADV ISA dumps and timing before choosing a lever. The
`test_triad_vulkan` modes `--precision-direct-fp16`,
`--precision-direct-fp16-storage`, `--precision-direct-fp32`,
`--precision-fp16-storage` and `--precision-fp32` run those controls at 16,
256 and 65,536 rows ([build guide](building.md#vulkan-and-opencl-gpu-builds)).
The Q15 and Q7 screens were CPU AVX2 dot tests; they do not rule out other
integer formulations, but gfx90c has no accelerated 8-bit dot instruction
([screens](validation.md#bounded-integer-and-radeon-screens)). Details:
[Vega](gpu-precision-controls.md#vulkan-on-vega-integrated-graphics),
[local Vulkan](triad-performance.md#local-radeon-vulkan-processing).

### Radeon 780M (Ryzen 7 8840U)

RDNA 3 has WMMA matrix instructions (FP16, BF16, INT8, INT4 at 16×16×16), but
no route or measurement exists, and the packed direct route's PCI gate admits
only `1002:1638`. First: query `vulkaninfo` for cooperative-matrix shapes
under RADV, then measure the existing FP32 VkFFT route before any matrix
work ([dispatch](triad-gpu-optimization.md#hardware-and-library-dispatch)).

### Intel UHD 630 (i9-9900K)

No matrix hardware. The host-scoring compatibility route runs FP32
frequency-search work without device FP64 or INT64. No test host exists, so
nothing here is measured; do not assign an XMX path from the vendor name.
