# Independent triad patterns, APSK spreading and AGC

The default isolated v3 waveform is now `independent-apsk-v2`. Its two bit
alternatives have independently keyed amplitude and phase sequences. Added
Symbols uses two-phase, two-radius 4-APSK factors, followed by one common
three-carrier power normalization. Ordinary Robust formats are unchanged.
The [qualification record](validation-data/triad-v3/2026-11-06-independent-patterns/README.md)
separates measured sensitivity, envelope behavior and remaining limitations.

## Derivation and compatibility

The old `legacy-v1` amplitude sequence for bit 1 was a fixed cyclic permutation
of bit 0. That relation remains reproducible in the explicitly selected legacy
mode, but is removed from the default. Legacy phase alternatives were already
different; the defect was the predictable amplitude relationship, particularly
for an amplitude-based receiver. Neither a phase inversion nor a carrier
permutation is now the construction of the other bit's codeword.

The new HMAC-derived child-key context contains the fixed revision prefix,
length-framed purpose, epoch, symbol ordinal, tile number, bit alternative and
lattice selection. Integer fields are big-endian 64-bit values. Each purpose
then supplies a separate AES stream; three successive addresses identify the
carrier lanes. The streams for inner amplitude, inner phase, Added radius and
Added sign are distinct, including between bit alternatives. Unbiased rejection
sampling selects the six permutations of the three base powers `{0.4,1,1.6}`.
Base phases remain uniformly selected from eight phases. Cover and permutation
streams are separately keyed and remain common where protected pulse boundaries
require that the cover carry no bit information.

This uses the existing HMAC/AES implementation, not a new cipher. The relevant
design principle is separation of pseudorandom key material by context; see
[NIST's PRF key-derivation guidance](https://csrc.nist.gov/pubs/sp/800/108/r1/upd1/final).
The custom context serialization is documented here; this is not a claim of
NIST certification. Independence assumes the security of the underlying keyed
primitives. Statistical tests detect implementation mistakes, not prove that
assumption.

Encryption, clock-derived epochs, exact bit count, keyed interleaving and
random access remain. No authentication bits or framing overhead were added.
Removing the fixed flip transform does **not** authenticate against replay or
reuse of a previously learned waveform under the retained key/epoch reuse
policy.

Select old recordings explicitly with `--pattern-revision legacy-v1`; new
generation defaults to `--pattern-revision independent-apsk-v2`. Both native
CLIs serialize revision and depth in their public source descriptions, including
power calibration. The continuous source handshake checks them before reading
samples. Template caches and regenerated observations must agree. Self-contained
old recordings with their own old basis remain readable; do not reinterpret
their arrays as new-waveform evidence. Historical sensitivity and runtime
qualification is revision-specific.

## Four-point factors and power balance

For depth `d`, the Added factor alphabet is

\[
\{-\sqrt{1+d},-\sqrt{1-d},+\sqrt{1-d},+\sqrt{1+d}\},\quad 0<d<1.
\]

Default `d=0.25` gives radii 0.8660 and 1.1180. Radius and sign use independently
keyed streams. Added Symbols off (`added_symbols=1`) bypasses these factors.
`--added-amplitude-depth` is an explicit waveform parameter and part of template
identity. The initial comparison of 0.10, 0.25 and 0.50 found no material complex
separation or spectral advantage requiring a deeper default.

After multiplying base pattern and Added factor, the complete coefficient
vector is multiplied by

\[
g_k=\sqrt{3/\sum_{c=0}^2 |p_{c,k}d_{c,k}|^2}.
\]

This preserves every carrier phase and inter-carrier amplitude ratio while
making summed **coefficient** power exactly three. The existing carrier gain
then divides total nominal DATA power among the three carriers. All coefficients
remain nonzero; at the default depth their minimum possible radius is about
0.5035 before the existing carrier gain. Final absolute carrier radii have more
than four values: these are **4-APSK spreading factors followed by common triad
normalization**, not a claim that the complete shaped waveform is a four-point
constellation.

Normalization occurs once per keyed fine cell, before pulse shaping. The
coefficient tiles cache it; there is no new per-sample cryptography or division
by composite waveform magnitude. Pulse support, shaping planner, protected
symbol placement, source duration, interleaving boundaries, missed-hop allocation,
FHSS ramps, burst waits, Helper waveform/lead and power split are unchanged.
In particular, the normalization never acts on Helper tones or channel output.

## What the short-window requirement can and cannot mean

A radio sees the actual composite waveform, not a sum of separately detected
carrier powers. We therefore measure both. For locally constant coefficients,
windowed composite power contains pair terms multiplied by
`sinc((fi-fj)*W)`, with `sinc(x)=sin(pi*x)/(pi*x)`. Three nonzero fixed tones
cannot have an exactly constant envelope: the outer-pair term at twice the
carrier spacing cannot be cancelled by either adjacent pair. The distinction
between phase choices, crest factor and a prescribed multitone spectrum is also
discussed in [Boyd's multitone study](https://web.stanford.edu/~boyd/papers/pdf/multitone_low_crest.pdf).

At 0.00075 Hz spacing, the adjacent beat period is 1,333 seconds. A 100-ms window
retains 0.999999991 of that pair term; even 800 ms retains 0.999999408. Changing
the cryptographic pattern does not remove this physical limitation. Faster
modulation/wider spacing or a different constant-envelope waveform would be a
material bandwidth/receiver change. None is silently introduced here.

The diagnostic uses a quarter of the smallest base amplitude step as a strict
engineering screen: `0.25 * 10*log10(1.6) = 0.5103 dB` of window RMS relative to
its observed median. It reports extrema and fractions, not a future guarantee.
For FHSS/bursts, it separately accounts for the known intentional envelope;
duty correction is a measurement baseline, not rescaling the waveform or
restoring energy lost in a missed hop. Actual gaps remain actual gaps.

The sampled Rate-36/Added-10 cases pass that screen at 150 ms; the FHSS case
passes at 400 ms. High spreading and the high-rate case pass below 100 ms.
Low rates can fail at 800 ms by many dB. See the record for exact settings and
the distinction between dense and sparsely placed windows. **Do not claim
universal sub-800-ms AGC compatibility from constant coefficient power.**

The IC-7100's documented SSB AGC time settings include 0.1 seconds, but they do
not specify the complete detector/control transfer law; see the
[manufacturer's manual](https://www.icomfrance.com/uploads/files/produit/not-IC-7100_advanced-instructions-en.pdf).
Our one-pole power-detector experiment is an illustrative distortion check,
not a calibrated IC-7100 model. A common positive AGC gain preserves simultaneous
amplitude ratios before filtering, but filtering and time-varying gain need not
commute. Deep frequency-selective fading and radio behavior still require their
own complete-waveform tests.

## Noise-like appearance and cheap development checks

Neither four-point APSK nor exact power balancing inherently improves LPI.
Balancing imposes statistical dependence. The study reports finite-record
spectra, moments, power-modulation lines, codeword separation and envelope
statistics. These show no large new spectral penalty in the tested cases;
they do not establish indistinguishability from Gaussian noise or a detection
advantage against an adversary. Do not relabel a source-only periodogram as a
physical channel or Helper sensitivity test.

Build using the repository wrapper and reuse the compatible build profile.
For a source edit, start with:

```sh
ctest --test-dir build/dev-cli \
  -R '^triad_(v3_source|pattern_probe|stream_source)$' --output-on-failure
OPENBLAS_NUM_THREADS=2 PYTHONPATH=tools:tests python -m unittest \
  test_triad_pattern_metrics test_triad_continuous
build/dev-cli/triad_pattern_probe --output local-evidence/pattern-probe \
  --rate 36 --added 10 --window-seconds 8
python tools/triad_pattern_metrics.py local-evidence/pattern-probe \
  --output local-evidence/pattern-metrics.json
```

Use a Python environment with NumPy/SciPy for metrics and the existing PyTorch
environment for receiver checks. Native source/probe tests use standard-library
Python or C++. The probe renders the actual source, carrier envelopes and
coefficient vectors in bounded windows, including multi-day coordinates.
Run full channel/receiver/Helper checks when those paths or their input statistics
change; do not repeat historical sensitivity campaigns for unrelated edits.
The [performance policy](triad-performance.md#required-efficient-regression-workflow)
still applies. A changed waveform is a changed qualification input, not ordinary
CPU variation or permission to reuse unmatched templates.
