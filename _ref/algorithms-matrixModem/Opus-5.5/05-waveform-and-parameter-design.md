# Waveform families and parameter design

Research date: 24 November 2026. This chapter converts the channel evidence and
limits into candidate waveform choices and configuration rules. Nothing here is
a qualified modem. The candidates are ranked by how well theory and the
calculations support them; experiments may reorder them, and better designs may
exist.

## Recommendations in brief

1. **Start from keyed, constant-envelope, single-tone-at-a-time signaling.**
   Under a peak power limit it is capacity-achieving at low C/N0 in Rayleigh
   fading with a Gaussian Doppler spectrum ([limits](03-information-limits-and-noncoherent-penalties.md)).
   It also gives the cleanest receiver: every hypothesis is a correlation
   against a known template, which is matrix-friendly.
2. **Decouple the three timing/frequency parameters the user's "6.25 Hz / 6.25
   baud" default ties together:**
   - *symbol duration T* from the **design C/N0** (target about 6–10 dB of
     Es/N0 per symbol on quiet paths, i.e. T ≈ 4–10 / (C/N0));
   - *tone spacing Δ* from the **Doppler category** (Δ ≈ 1/T + about twice
     the expected 2σ spread, plus the receiver's residual frequency error);
   - *total bandwidth B* from the **transceiver passband and regulation**
     (2.2 kHz default inside 450–2650 Hz; ≤ 2.8 kHz with the 26 dB mask).
   The alphabet then follows as M ≈ B/Δ.
3. **Prefer long symbols for weak links, but select duration by the modeled
   channel and design C/N0.** At the sampled −10, −6 and −2 dB-Hz points,
   2.56 s and 10.24 s are within 2.9 dB of the best duration in the 0.1–5 Hz
   Doppler rows. Stable-channel exceptions matter: at −10 dB-Hz, 2.56 s costs
   8.5 dB in AWGN and 7.3 dB in slow Rayleigh, while 10.24 s costs 3.8 dB in
   AWGN. These are excess Eb/N0 relative to each model's best duration at the
   same C/N0. The earlier 2.5–10 s / 0–3 dB claim for every Doppler category
   throughout −10 to 0 dB-Hz was too broad; discrete samples do not establish
   that continuous-range guarantee. The benefit over short symbols remains:
   at those three C/N0 points, 160 ms loses about 5–20 dB in the AWGN,
   slow-Rayleigh and 0.1–1 Hz Doppler rows. Long symbols need frequency/drift
   tracking and stable equipment, which the receiver (and optionally Helper
   guidance) must supply. See the [dated qualification](README.md#dated-qualifications-25-november-2026),
   [sampled fixed-duration results](../../../_appraisal/algorithms-matrixModem/calc/results/mfsk_summary.md#ebn0-db-with-a-fixed-symbol-duration-t--256-s-tone-spacing-1t--4-sigma-and-penalty-vs-best-t),
   the [information-limits review](../Sol-6.2/information-limits-review.md#boundaries-for-waveform-decisions),
   and the [finite-alphabet fading study](../Sol-6.2/fading-limit-and-mfsk-alternatives.md#finite-alphabet-in-a-22-khz-band).
4. **Use keyed hopping for time diversity and observer resistance, not for
   in-band frequency diversity.** In benign conditions the whole 2.2 kHz band
   fades together; diversity comes from spreading the code over many coherence
   intervals (tens of minutes to hours).
5. **Offer keyed spreading within a tile as the observer-resistant variant.**
   Replacing each pure tone by a keyed noise-like waveform of bandwidth W within
   the channel's coherence bandwidth (W·T = G dimensions) costs the keyed
   receiver no energy but reduces what an unkeyed observer sees per
   time-frequency cell by a factor of about √G
   ([observer chapter](06-observer-lpi-and-interference-perception.md)). It also
   removes the FFT shortcut, which turns the receiver into genuinely dense
   matrix work.
6. **Do not split power across simultaneous carriers at low C/N0.** In the
   square-law regime the rate from L simultaneous tones at P/L each is about
   L·(x/L)² = x²/L of a single tone's rate: three simultaneous carriers cost
   about 4.8 dB of Eb/N0 relative to one carrier hopping among the same three
   frequencies. This is a plausible structural contributor to the long transmit
   times seen with the triple Robust modem in weak, disturbed cases, though the
   repository's triple-Robust records were not re-analysed here to confirm it.
7. **Make the receiver multi-hypothesis over channel categories.** A one-way
   link cannot request reconfiguration. The receiver can run several Doppler
   subspace ranks, drift ranges and coherence assumptions in parallel and
   combine them; this is affordable and is a natural place to spend matrix
   compute.

## Parameter mapping

| User control | What it should set | Rule of thumb from this research |
| --- | --- | --- |
| Target SNR dB/1Hz (design C/N0) | Symbol duration T | Es/N0 ≈ 6–10 dB per symbol on quiet paths: T ≈ 4–10 / (C/N0 in Hz). Examples: +20 dB-Hz → 40–100 ms; +10 → 0.4–1 s; 0 → 4–10 s; −10 → 40–100 s (needs very stable equipment and drift search). |
| Doppler/delay spread tolerance (category) | Tone spacing Δ, receiver subspace ranks, drift range | Δ ≥ 1/T + 2×(2σ) + residual frequency error. With the channel chapter's categories (2σ ≈ 0.01–0.1, 0.5–2.5, 5–15 and 25–73 Hz): benign Δ ≈ 1/T + 0.2 Hz; moderate +1–5 Hz; severe +10–30 Hz; extreme +50–150 Hz. Delay spread matters only at symbol edges (τ/T) and for frequency diversity. |
| Total bandwidth | B, hence M ≈ B/Δ | 2.2 kHz default (450–2650 Hz); wider bandwidth adds alphabet size, not energy. |
| Tone bandwidth/symbol rate (user default 6.25) | Should be split into T and Δ | Keep 160 ms / 6.25 Hz only as a high-C/N0 (≈ +12 to +16 dB-Hz) preset. |
| Transmit power | C/N0 and energy efficiency | Below the knee, raising power reduces total energy (rate ∝ power²). Pick power so the design C/N0 is above the knee for the expected category if total energy, not peak power, is the binding limit. |
| FHSS | Keyed tone/tile placement per symbol | Time diversity and observer resistance; hop frequency must not enter key derivation ([keys chapter](09-keys-schedules-spectrum-and-stream-contract.md)). |
| Frequency Selective Amplitude Modulation | Simulator stress, not a transmitter feature | Model frequency-selective gain across the band; real transmitters should keep constant envelope. |
| Helper Carrier power/ratio | Separate guidance budget | Spend Helper energy only if it buys more DATA sensitivity than it costs (e.g. enables long symbols with non-GPSDO equipment). |
| Transmit noise | Observer masking experiments | Adds radiated energy; evaluate against the observer metric, not as a sensitivity aid. |

The noncoherent "knee" (loss ≤ about 4 dB vs Shannon) falls near −6, +2, +6,
+10, +14, +18 and > +20 dB-Hz for 2σ = 0.1, 0.5, 1, 2, 5, 10 and 30 Hz. A
configuration whose design C/N0 lies far below the knee of the expected
category is choosing a very long, energy-inefficient transmission, not a
marginal one.

## Candidate waveform families

### A. Keyed long-symbol MFSK (baseline)

- **Transmitter:** one constant-envelope tone per symbol, continuous phase across
  symbols where practical, raised-cosine or similar transitions to control
  emissions, tone index = keyed permutation of coded data, keyed hop offset per
  symbol across the band. Constant envelope keeps transceiver and amplifier
  behavior simple.
- **Receiver:** for each symbol window, project onto every candidate tone's
  Doppler subspace (rank r ≈ 2σT + 1 Slepian/Karhunen–Loève functions; rank 1
  for quiet channels), form subspace energies with estimator-correlator
  weights, requantize to 4 bits, and pass soft symbol metrics to the decoder.
  Run banks for several spread categories at once.
- **Evidence:** capacity-achieving at low C/N0 (limits chapter); amateur modes
  of this family (FST4/FST4W, Q65) are the best practical noncoherent systems;
  the FST4 family already scales T with design C/N0.
- **Costs and risks:** long T needs frequency and drift tracking
  (ionospheric drift 0.2–5 mHz/s, flare SFDs ~0.15 Hz/s, equipment drift);
  receiver work for regular grids is FFT-cheap, so tensor hardware is not
  needed for the basic receiver.

### B. Keyed noise-like tiles (observer-resistant variant)

- **Transmitter:** each symbol is one of M keyed pseudo-random complex
  waveforms confined to a tile of bandwidth W (inside the coherence bandwidth;
  about 10–50 Hz for delays up to a few ms) and duration T, hopped across the
  band by the key. Constant-envelope keyed phase modulation within the tile
  (keyed PSK chips filtered to W) keeps the peak-limited property.
- **Receiver:** correlate each candidate time/frequency offset against the
  keyed templates for the expected symbol (and decoys), take subspace
  energies, requantize, decode. Timing must be searched at about 1/(2W)
  resolution instead of a fraction of T.
- **Evidence:** within the flat-fading assumption, detection performance equals
  family A for the keyed receiver (matched filter with unknown phase). The
  historical single-carrier Robust modem (tone with keyed phase and amplitude
  patterning) was effectively this family with very small W and long coherent
  integration; Matrix differs by bounding coherent integration by the channel's
  coherence time and by hopping tiles.
- **Costs and risks:** more timing hypotheses; sensitivity to frequency
  selectivity if W approaches the coherence bandwidth; no FFT shortcut (dense
  matrix work, which suits the hardware goal but must be budgeted).

### C. Receiver-side joint detection over coherent blocks

Not a transmitter change: when the path is coherent across several symbols,
the receiver correlates candidate *sequences* (or short codewords) of k bits
against the received block, recovering part of the per-symbol noncoherent
loss and removing explicit sync. Published amateur evidence: FT8's 3-symbol
block detection gains 0.7 dB and FT4's 1.6 dB on AWGN, and about 0.1 dB at
10 Hz spread. Theory: unknown constant phase over the block costs only
0.1–0.7 dB versus coherent for 8–128-bit blocks. Cost grows as 2^k
([algorithm chapter](08-gemm-receiver-algorithms-and-utilization.md)): k ≈ 24–32
per block fills a petaop GPU. Expected gain is of order 0.5–2 dB on coherent
paths, near zero on spread paths.

### D. Simultaneous multi-carrier joint detection (triple Robust style)

At fixed total power, L simultaneous tones reduce the per-tone energy per
coherence cell by L. In the low-x regime that costs about 10·log10(L) dB of
Eb/N0 relative to one carrier visiting the same frequencies sequentially; in
the high-x regime the cost is small and frequency diversity can help. Joint
coherent detection across carriers requires the carriers to share phase, which
fails once their separation exceeds the coherence bandwidth. Not recommended
for weak links; retain only as a high-C/N0 option if frequency diversity is
shown to matter.

### E. Wideband DSSS filling the band

Resolves multipath into taps and spreads energy uniformly in time and
frequency, which the critical-occupancy analysis shows is overspread at low
C/N0 (useful occupancy about 17 Hz at 0 dB-Hz and 170 Hz at +10 dB-Hz). Its
interference rejection and observer resistance can be obtained more cheaply
by family B. HF field work found DSSS useful for low-rate links with poor
forecasts (Bergadà et al. 2014, reviewed in [Sol-6.2](../Sol-6.2/waveform-options.md))
at much higher C/N0 than this use case. Not recommended for the weak case.

### F. OFDM, OTFS and chirps

Multicarrier OFDM splits power across many simultaneous subcarriers (worse than
D at low x) and needs pilots; OTFS needs channel estimation of a sparse
delay–Doppler representation, which the weak signal cannot support; chirps are
excellent sounding waveforms but offer no energy advantage for data at low x
and suffer ionospheric dispersion and drift–delay coupling. All are poor fits
below the knee. They remain legitimate for high-C/N0 modes, which Fast Modem
already covers.

## The "hypothesis vs null hypothesis" construct

The user's requested core operation — a matrix correlation where a correlation
is expected versus where it is not — maps naturally onto keyed signaling:

- **Data hypotheses:** the M keyed templates (tones or tiles) that the schedule
  says are meaningful for this symbol.
- **Live null hypotheses:** additional keyed templates that the schedule says are
  *not* transmitted at this symbol (decoy tones, other tiles, unused keyed
  waveforms), processed by exactly the same matrix and quantization path. Their
  scores estimate the null distribution online, including impulsive noise,
  interference and quantization effects that Gaussian theory misses, and
  support CFAR thresholds and soft-metric normalization.
- **Wrong-key nulls:** templates derived from a different key or shift
  frequency give a direct estimate of wrong-key admission risk.
- **Quiet-slot completion:** the same null statistics define scored physical
  absence for message completion.

The extra columns cost a small fraction more matrix work and directly address
the repository's long-standing null-calibration and wrong-key issues. Whether
decoy-calibrated nulls hold up under real HF noise must be tested with captured
noise.

## Coding, interleaving and completion

- **Outer code over long spans.** Interleave across at least 3–30 minutes for
  fast-fade diversity on quiet paths, 1–3 hours for slow fading, and plan
  erasure tolerance for outages of tens of minutes (fadeouts) and about six
  hours a day (MUF closure on a fixed frequency). Multi-day absorption events
  cannot be bridged within a day.
- **Code choice.** Long messages: soft-input LDPC or similar with symbol-level
  soft metrics from the 4-bit energies (4-bit LLRs lose about 0.1 dB).
  Short blocks: codes with near-ML decoding (OSD, list decoding) whose
  candidate re-encoding batches as matrix work.
- **Stream contract.** Fixed public geometry, no received length headers, scored
  quiet-slot completion, interior losses as erasures — the repository's
  existing physical-completion contract applies unchanged
  ([keys and stream chapter](09-keys-schedules-spectrum-and-stream-contract.md)).

## Helper Carrier as separate guidance

The value of Helper guidance is largest exactly where Matrix benefits most:
long symbols on quiet paths, where the common Doppler shift, drift and sample
clock stretch must be removed to keep tones inside narrow spacing. Helper pilot
tones near the DATA band share the ionospheric common Doppler of the same path
and can supply a causal estimate of shift, drift and clock stretch with
uncertainty, which the Matrix receiver uses to narrow (not replace) its own
search. Per the user's direction there is no joint detection. Helper energy is
taken from the same transmitter budget, so the comparison must be at equal
total energy, including Helper-off and failed-Helper cases.

## What remains uncertain

- How real HF Doppler spectra (discrete modes, heavy-tailed common shift,
  nonstationarity over minutes) change the Gaussian-Rayleigh conclusions.
- Whether drift tracking can sustain 10–100 s coherent windows on real paths
  without Helper guidance, and at what search cost and false-alarm penalty.
- The emission spectrum of long-symbol keyed hopping with practical transitions
  against the 2.8 kHz / 26 dB requirement and transceiver filters.
- The best tile bandwidth W for family B under measured coherence bandwidths.
- Whether a GPU-heavy joint-detection receiver (family C) earns its cost on
  real paths.

These are experimental questions for the
[evaluation plan](11-evaluation-and-simulation-plan.md).
