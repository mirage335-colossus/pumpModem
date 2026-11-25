# Citations recovered from retired guidance

Recorded 25 November 2026. Between 10 September and 10 November 2026, maintained
guides, tools and the README cited the scientific and technical sources below.
The guides were later rewritten or retired, mostly in commit `419e6bae`
(7 November 2026, "Remove obsolete Robust modem code and heavy test
infrastructure"). By 25 November the sources in the topic sections were cited
nowhere else in the repository. Under the [research-material rule](../../docs/development.md#research-material)
citations to the scientific literature are not deleted, so this record keeps
each source with the claim it supported.

Each entry gives the verified bibliographic identity, the link the guide used,
and the claim as the guide stated it, with the guide's path and the commit that
removed the citation. Read the parent of that commit to see the original text,
for example `git show 419e6bae^:docs/weak-signal.md`. The claims describe
designs that may since have been retired. They are recorded as the guides stated
them and were not re-derived here. Identities were checked on 25 November 2026
against Crossref, the arXiv API, the NASA Technical Reports Server, PubMed, or
the linked document's first page. Where a link failed that day, the entry says
so.

**Method and limits.** The search covered every URL, DOI and arXiv identifier
on a removed line of `git log -p` for `docs/`, `tools/`, `tests/` and
`README.md`, and kept those absent from the current tree. It cannot find a
citation given only as text, without an identifier. Software and API
documentation, source-code links and specification pages for formats and
toolkits were left out. They supported implementation details, not research
findings. The [last section](#works-still-cited-elsewhere) lists sources from
the same retired guides that the repository still cites under another link or
edition.

Research citations lost from `_ref/` in the 13 November rewrites were restored
to their own chapters, and their broken DOIs were repaired. See the
[chapter 87 audit-start checks](../highFidelity-triple_Robust_Modem/Fable-5.2/87-evidence-remediation-audit.md#primary-source-checks-recorded-at-audit-start)
and the [chapter 80 plan-start checks](../highFidelity-triple_Robust_Modem/Fable-5.2/80-completion-plan.md#6-source-checks-recorded-at-plan-start).

## Detection, information limits and low probability of intercept

- **Tandra, R., Sahai, A. (2005).** "Fundamental limits on detection in low SNR
  under noise uncertainty." *Proc. 2005 Int. Conf. Wireless Networks,
  Communications and Mobile Computing*, vol. 1, 464–469.
  doi:[10.1109/WIRLES.2005.1549453](https://doi.org/10.1109/WIRLES.2005.1549453).
  Author copy: <https://people.eecs.berkeley.edu/~sahai/Papers/Rahul_WirelessCom05.pdf>.
  *Supported* (`docs/link-planner.md`, removed in `419e6bae`): known-waveform
  detection differs from energy-only detection. A receiver holding the private
  template accumulates correlation, while an observer without it is limited to
  energy detection. That asymmetry was part of the LPI rationale. The later
  2008 "SNR walls" paper and Tandra's 2009 thesis are still cited (last section).
- **Messerschmitt, D. G. (2012).** "Interstellar communication: the case for
  spread spectrum." *Acta Astronautica* 81(1), from p. 227.
  doi:[10.1016/j.actaastro.2012.07.024](https://doi.org/10.1016/j.actaastro.2012.07.024);
  arXiv:[1111.0547](https://arxiv.org/pdf/1111.0547).
  *Supported* (`docs/weak-signal.md`, `419e6bae`): at fixed received power,
  noise density and integration time, narrowing the waveform collects no extra
  signal energy. It changes displayed in-band SNR, interference exposure, timing
  resolution and search cost. A wider known pattern spreads power across
  frequency and can reject unrelated interference (§§3.3–3.4).
- **Ghozlan, H., Kramer, G. (2017).** "Models and information rates for Wiener
  phase noise channels." *IEEE Trans. Inf. Theory* 63(4), 2376–2393.
  doi:[10.1109/TIT.2017.2662698](https://doi.org/10.1109/TIT.2017.2662698);
  arXiv:[1503.03130](https://arxiv.org/pdf/1503.03130).
  *Supported* (`docs/weak-link-planning.md`, `419e6bae`): phase fluctuations
  within one observation matter. With the default 0.5°/√s Wiener phase model and
  no residual frequency, a 1.46-day coherent symbol lost about 4.80 dB of
  expected coherent energy. As duration grows, one uncorrected coherent filter
  approaches expected energy `4r/σ²`, only 14.20 dB at −33 dB-Hz. That is a limit
  of that receiver and model, not an impossibility theorem.
- **EPFL, *Signals, Instruments, and Systems*, week 7 lecture (2025–26),
  p. 33.** <https://disalw3.epfl.ch/teaching/signals_instruments_systems/ay_2025-26/lecture/SIS_25-26_W07_lecture.pdf#page=33>.
  *Supported* (`README.md`, `419e6bae`): the GUI and CLI showed the
  Shannon–Hartley limit `B·log2(1 + 10^(C/N₀/10)/B)` for the selected target and
  bandwidth. At the short-message defaults, 3.6 kHz and 32 dB-Hz, it is about
  1.89 kbit/s. It is ideal Gaussian-channel capacity, not achievable throughput.
- **MIT OpenCourseWare, 16.36 *Communication Systems Engineering*, Spring 2009,
  lectures 13–14.** <https://ocw.mit.edu/courses/16-36-communication-systems-engineering-spring-2009/a49d9e2954b440def3794fe73e79756c_MIT16_36s09_lec13_14.pdf>.
  *Supported* (`docs/throughput.md`, `419e6bae`): ideal band-limited AWGN
  capacity `B·log2(1 + SNR_B)` is about 156 kbit/s at 12 kHz and 80 dB-Hz. The
  modem's high-SNR mode carried 375 bit/s, so 1 MiB took about 6.2 hours before
  coding overhead. The bound assumes a suitable code and channel and is not a
  benchmark.

## Phase noise, oscillators and timing

- **Bond, C., Brown, D., Freise, A., Strain, K. A. (2016).** "Interferometer
  techniques for gravitational-wave detection." *Living Reviews in Relativity*
  19(1). doi:[10.1007/s41114-016-0002-8](https://doi.org/10.1007/s41114-016-0002-8).
  The guide linked LIGO document P1500233:
  <https://dcc.ligo.org/public/0122/P1500233/001/Interferometer_Techniques.pdf>.
  *Supported* (`docs/weak-signal.md`, `419e6bae`): a small sinusoidal phase
  modulation of depth β gives first sidebands with power proportional to β².
  Reducing the depth lowers their power but does not move them toward the
  carrier. Arbitrary phase changes 100 times a second therefore cannot be
  squeezed into a 1 Hz spectrum by making them tiny (§3.2).
- **Kumar, R. (1987).** "Optimum filters and smoothers design for carrier phase
  and frequency tracking." JPL Publication 87-10, NASA-CR-182654.
  <https://ntrs.nasa.gov/citations/19880010295>.
  *Supported* (`docs/helper-carrier.md`, `419e6bae`): linear smoother predictions
  are limited near phase-locked-loop threshold.
- **JPL, *DSMS Telecommunications Link Design Handbook* 810-005, module 207
  Rev. A, "34-m and 70-m Telemetry Reception" (13 June 2003), §5.3.5.**
  <https://deepspace.jpl.nasa.gov/dsndocs/810-005/207/207A.pdf>.
  *Supported* (`docs/helper-carrier.md`, `419e6bae`): tracking loss is modeled
  through phase error under stated conditions. With JPL 87-10, this supported
  model-aware measurement, not transplanted DSN limits or a physical SNR floor.
- **Vilnrotter, V. A., Hinedi, S., Kumar, R. (1988).** "A comparison of frequency
  estimation techniques for high-dynamic trajectories." JPL Publication 88-21,
  NASA-CR-184865. <https://ntrs.nasa.gov/api/citations/19890011016/downloads/19890011016.pdf>.
  *Supported* (`docs/helper-eme-measurement.md`, `419e6bae`): coherent and
  segmented frequency estimators rest on different information models. For
  comparison only, an ideal high-C/N₀ calculation with 90 independent 0.36 s
  phases and both pilots gave a three-sigma scale of about 0.095 Hz at
  15 kW and 220 dB path loss, or about 0.30 Hz at 1.5 kW, with audio scale and
  frequency rate known. Unknown rate, model mismatch, fading, acquisition and
  finite SNR can make actual performance worse.
- **NIST Time and Frequency Division, "Properties of oscillator signals",
  section three.** <https://tf.nist.gov/phase/Properties/three.htm>.
  *Supported* (`docs/helper-carrier.md`, `419e6bae`): offset, drift and random
  instability are different quantities, and static ppm is not a drift rate.
  Systematic behavior is separate from random noise.
- **Novick, A., Lombardi, M. A., Matsakis, D., Clark, J.** "Measurement of
  transient environmental effects in GPS-disciplined clocks." NIST publication
  record: <https://www.nist.gov/publications/measurement-transient-environmental-effects-gps-disciplined-clocks>.
  *Supported* (`docs/weak-link-planning.md` and `docs/pulse-moment-validation.md`,
  `419e6bae`): GPS discipline improves long-term time and frequency accuracy,
  but short-interval stability depends on the local oscillator, the control loop
  and environmental transients. A GPSDO label cannot promise useful coherence at
  0.001 Hz or over days.
- **Lombardi, M. A. (2016).** "Evaluating the frequency and time uncertainty of
  GPS disciplined oscillators and clocks." NIST publication record:
  <https://www.nist.gov/publications/evaluating-frequency-and-time-uncertainty-gps-disciplined-oscillators-and-clocks>.
  *Supported* (`docs/triad-v3.md`, `419e6bae`): methods exist for evaluating an
  actual GPSDO's frequency and time uncertainty. A simulation preset name does
  not supply that evaluation for a month-long link. Other Lombardi works (2002,
  2008) are cited in Fable-5.2 chapter 55.
- **u-blox, "Timing: considerations with u-blox 6 GPS receivers", application
  note GPS.G6-X-11007.**
  <https://content.u-blox.com/sites/default/files/products/documents/Timing_AppNote_%28GPS.G6-X-11007%29.pdf>.
  *Supported* (`docs/oscillator-models.md`, `419e6bae`): a raw GPS timepulse can
  be accurate on average while clock quantization adds jitter. An external PLL
  with a local oscillator improves phase noise.
- **Ettus Research knowledge base, "GPSDO".** <https://kb.ettus.com/GPSDO>
  (returned an automated-access check, HTTP 403, on 25 November 2026).
  *Supported* (`docs/oscillator-models.md`, `419e6bae`): the TCXO and OCXO
  temperature-stability figures apply when unlocked, and GPS-locked timing is
  listed separately. Unlocked figures do not justify a larger constant locked
  offset for an oscillator without an oven.
- **Jackson Labs, LC_XO OCXO/TCXO specification sheet.**
  <https://www.jackson-labs.com/assets/uploads/main/LC_XO_OCXO_specsheet.pdf>
  (HTTP 404 on 25 November 2026).
  *Supported* (`docs/oscillator-models.md`, `419e6bae`): an oven does not
  guarantee lower phase noise at every offset. In comparable specifications the
  OCXO was better close to the carrier, equal at another offset, and worse
  farther out. Hardware should be represented by measured residual behavior.
- **Leo Bodnar Electronics, "Mini Precision GPS Reference Clock" product page.**
  <https://www.leobodnar.com/shop/index.php?cPath=107&main_page=product_info&products_id=301>.
  *Supported* (`docs/oscillator-models.md`, `419e6bae`): the maker uses a TCXO
  for short-term signal quality, with GPS setting long-term accuracy, and
  specifies phase noise at several offsets rather than one number. Fable-5.2
  chapter 55 cites a different Bodnar product page.
- **NTP Project, "Clock Discipline Algorithm", NTP 4.2.8 documentation.**
  <https://www.ntp.org/documentation/4.2.8-series/discipline/>.
  *Supported* (`docs/triad-clock-follow.md`, `ad49bcba`): normal NTP slewing can
  be invisible to a wall-minus-monotonic check on Linux, because both clocks
  receive the frequency adjustment. A normal 20 ppm rate difference accumulates
  1.728 s per day, so general support needs a dated audio-frame/UTC rate model.

## Acquisition, tracking and estimation

- **Rabiner, L. R. (1989).** "A tutorial on hidden Markov models and selected
  applications in speech recognition." *Proc. IEEE* 77(2), 257–286.
  doi:[10.1109/5.18626](https://doi.org/10.1109/5.18626). Course copy:
  <https://www.cs.cmu.edu/~durand/03-711/Readings/Rabiner89.pdf>.
  *Supported* (`docs/triad-clock-follow.md`, `419e6bae`): the tracker's
  recursion was the standard HMM likelihood. Pruning removes weighted paths and
  never renormalizes, so its evidence cannot exceed the full declared likelihood
  ratio under the Gaussian null. This protects the false-alarm argument, not
  sensitivity: long fades or fast corrections can discard the true path.
- **Yu, W., Zheng, B., Watson, R., Lachapelle, G. (2007).** "Differential
  combining for acquiring weak GPS signals." *Signal Processing* 87(5),
  824–840. doi:[10.1016/j.sigpro.2006.08.004](https://doi.org/10.1016/j.sigpro.2006.08.004).
  The guide linked <https://www.sciencedirect.com/science/article/abs/pii/S0165168406002696>.
  *Supported* (`docs/weak-link-planning.md`, `419e6bae`): losing whole-symbol
  coherence does not erase every useful phase transition. A differential receiver
  can compare neighboring matched sections without a reliable decision on each
  transition. A sum of section energies discards those phase relationships, so
  its results do not bound a differential receiver. Sol-6.2 cites a different
  differential-combining source, Dovis and Ta (2012).
- **Divsalar, D., Simon, M. K. (1994).** "Maximum-likelihood differential
  detection of uncoded and trellis coded amplitude phase modulation over AWGN
  and fading channels — metrics and performance." *IEEE Trans. Commun.* 42(1),
  76–89. doi:[10.1109/26.275303](https://doi.org/10.1109/26.275303).
  The guide linked <https://ieeexplore.ieee.org/document/275303>.
  *Supported* (`docs/triad-modem.md`, `419e6bae`): block differential detection
  is a useful alternative to raw noisy products. Its published AWGN and fading
  results do not establish another format's performance. Their 1990
  multiple-symbol paper is still cited (last section).
- **Rogstad, D. H. (1991).** "Suppressed carrier full-spectrum combining."
  <https://ntrs.nasa.gov/citations/19920005021>.
  *Supported* (`docs/triad-v3.md`, `419e6bae`): squaring loss of a
  cross-correlation tracking loop in suppressed-carrier arraying. With the
  Scharf–Friedlander and variable-projection references, it motivated benchmarks
  and alternatives, not triad performance.
- **NIST Applied and Computational Mathematics Division (2012).** *Summary of
  Activities for Fiscal Year 2011*, NISTIR 7856, with O'Leary and Rust's
  variable-projection work.
  <https://nvlpubs.nist.gov/nistpubs/ir/2012/nist.ir.7856.pdf>.
  *Supported* (`docs/triad-v3.md`, `419e6bae`): eliminating linear parameters by
  variable projection while optimizing the nonlinear ones, with numerical
  diagnostics and the risk of local minima.
- **Guo, W., Niu, X., Guo, C., Cui, J. (2017).** "A new FFT acquisition scheme
  based on partial matched filter in GNSS receivers for harsh environments."
  *Aerospace Science and Technology* 61, 66–72.
  doi:[10.1016/j.ast.2016.11.017](https://doi.org/10.1016/j.ast.2016.11.017).
  *Supported* (`docs/triad-realtime.md`, `419e6bae`): partial-matched-filter FFT
  acquisition is an established parallel timing search. Like the GNSS-SDR and
  Psiaki–Jung references beside it, it motivated computational structure, not
  sensitivity or false-admission claims.
- **Septier, F., Delignon, Y., Menhaj-Rivenq, A., Garnier, C. (2008).**
  "Non-pilot-aided sequential Monte Carlo method to joint signal, phase noise,
  and frequency offset estimation in multicarrier systems." *EURASIP J. Adv.
  Signal Process.* 2008:612929.
  doi:[10.1155/2008/612929](https://doi.org/10.1155/2008/612929).
  The guide linked <https://link.springer.com/article/10.1155/2008/612929>.
  *Supported* (`docs/triad-noise-calibration.md`, `419e6bae`): a candidate next
  comparison was Rao–Blackwellized joint phase and channel inference, where each
  oscillator trajectory carries its own Gaussian tap posterior. That is an
  architecture hypothesis, not a demonstrated gain. Particle depletion,
  likelihood-estimator variance and runtime need measuring, and
  likelihood-based admission needs correctly normalized proposals and
  resampling.
- **Gappmair, W., Bergmann, M. (2015).** "Estimation of carrier and channel
  parameters in time-selective fading channels." *IET Communications* 9(12),
  1474–1478. doi:[10.1049/iet-com.2015.0080](https://doi.org/10.1049/iet-com.2015.0080).
  The guide linked <https://ietresearch.onlinelibrary.wiley.com/doi/10.1049/iet-com.2015.0080>.
  *Supported* (`docs/triad-stochastic.md`, `419e6bae`): physical motivation and
  limits of spectral channel models. Its Rician, negligible-multipath
  assumptions do not make a two-ray estimator with random selective modulation
  optimal.
- **ESA Navipedia, "Baseband Processing".**
  <https://gssc.esa.int/navipedia/index.php/Baseband_Processing>.
  *Supported* (`docs/1200hz-weak-link-planning.md`,
  `docs/eme-amplitude-receiver.md` and three other weak-signal guides,
  `419e6bae`): coherent and noncoherent
  accumulation trade sensitivity, including squaring loss. A partial window can
  guide accumulation but must not replace a fully scored symbol when deciding
  absence.
- **Tyler, D. E. (1987).** "Statistical analysis for the angular central Gaussian
  distribution on the sphere." *Biometrika* 74(3), 579–589.
  doi:[10.1093/biomet/74.3.579](https://doi.org/10.1093/biomet/74.3.579).
  The guide linked <https://academic.oup.com/biomet/article-abstract/74/3/579/238632>.
  *Supported* (`docs/validation-data/triad-v3/2026-11-01-state/README.md`,
  `345e8d70`): for a fixed covariance shape `R`, direction `u = y/‖y‖` and `n`
  complex samples, `LR = det(R)⁻¹ (uᴴR⁻¹u)⁻ⁿ` removes a common unknown scale. It
  follows from integrating the Gaussian radial density, not from inserting a
  noise estimate. It does not handle arbitrary colored interference.
- **Wróblewska, J. (2025).** "A note on some extensions of the matrix angular
  central Gaussian distribution." *Central European Journal of Economic
  Modelling and Econometrics*. <https://journals.pan.pl/Content/136752/mainFile.pdf>.
  *Supported* (`tools/triad_noise_likelihood.py`, `419e6bae`): primary reference
  (Theorem 4 with r = 1) for the scale-invariant noise likelihood. The tool's
  streaming information-form reuse and family allocation were the project's own
  derivations.

## Statistics and benchmarking method

- **Kalibera, T., Jones, R. (2012).** "Quantifying performance changes with
  effect size confidence intervals." Technical report, University of Kent.
  <https://www.cs.kent.ac.uk/pubs/2012/3233/content.pdf>.
  *Supported* (`docs/triad-performance.md`, `419e6bae`): performance ratios need
  uncertainty that reflects each level of execution variability.
- **Kalibera, T., Jones, R. (2013).** "Rigorous benchmarking in reasonable
  time." *Proc. 2013 Int. Symp. Memory Management*, 63–74.
  doi:[10.1145/2464157.2464160](https://doi.org/10.1145/2464157.2464160).
  Repository copy: <https://kar.kent.ac.uk/33611/45/p63-kaliber.pdf>.
  *Supported* (`docs/triad-performance.md`, `419e6bae`): distinguish repeated
  measurements within a process from independent executions, and allocate
  replication where variance and cost justify it. Many iterations within one
  worker do not replace fresh outer contexts.
- **Ofenbeck, G., Steinmann, R., Caparros, V., Spampinato, D. G., Püschel, M.
  (2014).** "Applying the roofline model." *2014 IEEE Int. Symp. Performance
  Analysis of Systems and Software (ISPASS)*, 76–85.
  doi:[10.1109/ISPASS.2014.6844463](https://doi.org/10.1109/ISPASS.2014.6844463).
  Copy: <https://spiral.ece.cmu.edu/pub-spiral/pubfile/ispass-2013_177.pdf>.
  *Supported* (`docs/triad-performance.md`, `e0c3f50a`): measured roofline
  analysis separates compute-bound and memory-bound regimes. Peak FLOP rates do
  not predict complete acquisition time.
- **Lan, K. K. G., DeMets, D. L. (1983).** "Discrete sequential boundaries for
  clinical trials." *Biometrika* 70(3), 659–663.
  doi:[10.1093/biomet/70.3.659](https://doi.org/10.1093/biomet/70.3.659).
  *Supported* (`docs/triad-performance.md`, `419e6bae`): alpha spending is a
  principled alternative when a variable schedule of repeated looks is needed.
  The suite used a fixed look schedule with conservative multiplicity
  adjustment instead.
- **Shafer, G. (2021).** "Testing by betting: a strategy for statistical and
  scientific communication." *J. R. Stat. Soc. A* 184(2), 407–431.
  doi:[10.1111/rssa.12647](https://doi.org/10.1111/rssa.12647). Author copy:
  <https://www.glennshafer.com/assets/downloads/articles/article104_jrss_shafer-testingbybetting-with-discussion-response.pdf>.
  *Supported* (`docs/triad-physical-admission.md`, `419e6bae`): a score with null
  expectation at most one, with Markov's inequality, gives a fixed-endpoint bound.
  A union over the permitted daily attempts needs no independence between them.
  It is not a license for data-dependent stopping or prior selection. The 2023
  Ramdas–Grünwald–Vovk–Shafer review is cited separately in the resilience
  research.
- **Hoeffding, W. (1963).** "Probability inequalities for sums of bounded random
  variables." *J. Amer. Statist. Assoc.* 58(301), 13–30.
  doi:[10.1080/01621459.1963.10500830](https://doi.org/10.1080/01621459.1963.10500830).
  The guide linked RPI course notes:
  <https://www.cs.rpi.edu/academics/courses/spring06/random/hoefding.pdf>.
  *Supported* (`docs/frequency-axis-probability.md`, `419e6bae`): the
  bounded-draw inequality, used beside exact binomial intervals and Bonferroni
  allocation.
- **Gadgil, S., IISc MA261 *Probability Models and Statistics*, notes ch. 8.**
  <https://math.iisc.ac.in/~gadgil/MA261/notes/chapter-8.html>.
  *Supported* (`docs/protocol.md`, `4dd23c51`): the union bound. For one
  hypothesis with `n` observed marker bits and at most `e` mismatches, the
  false-match probability was at most `V(n,e)/2ⁿ`, with `V(n,e)` the cumulative
  binomial count. Charging starts, deletion runs and slots
  (`H = 144,615` hypotheses per slot) and requiring at least 84 evidence bits
  limited false marker acceptance to `2⁻⁸⁴` per recovery call under a
  random-input model. One charged slot, 112 surviving bits and one mismatch gave
  87 evidence bits; two mismatches gave 81 and failed.
- **NIST/SEMATECH *e-Handbook of Statistical Methods* and NIST Dataplot
  reference pages.** Section titles as served on 25 November 2026:
  - §1.3.6.6.18 Binomial distribution:
    <https://itl.nist.gov/div898/handbook/eda/section3/eda366i.htm>
    (`docs/protocol.md`, `4dd23c51`; the cumulative binomial at p = 1/2 above).
  - §1.3.6.6.11 Gamma distribution:
    <https://itl.nist.gov/div898/handbook/eda/section3/eda366b.htm>
    (`docs/lpi-estimates.md`, `419e6bae`; the Gamma radiometer model).
  - §7.2.2.1 Confidence interval approach (mean):
    <https://www.itl.nist.gov/div898/handbook/prc/section2/prc221.htm>
    and the paired-observation page
    <https://www.itl.nist.gov/div898/handbook/prc/section3/prc311.htm>
    (`docs/triad-performance.md`, `419e6bae`; Student-t inference for the
    geometric-mean runtime ratio).
  - §7.2.3.1 Confidence interval approach:
    <https://www.itl.nist.gov/div898/handbook/prc/section2/prc231.htm>
    (`tools/triad_noise_likelihood.py` and `docs/triad-noise-calibration.md`,
    `419e6bae`; copies in validation evidence until `345e8d70`).
  - §7.2.4.1 Confidence intervals:
    <https://www.itl.nist.gov/div898/handbook/prc/section2/prc241.htm>
    (`docs/frequency-axis-probability.md`, `docs/amplitude-probability.md` and
    `docs/robust-manual-capacity.md`, last removed in `6bc9b0e3`; exact binomial
    intervals).
  - §7.4.7.3 Bonferroni's method:
    <https://itl.nist.gov/div898/handbook/prc/section4/prc473.htm>, also linked as
    <https://www.itl.nist.gov/div898/handbook/prc/section4/prc473.htm>
    (`docs/frequency-axis-probability.md` and `docs/triad-performance.md`,
    `419e6bae`; one workflow budget spent as
    `α_suite / (hypotheses × jobs × looks)`).
  - Dataplot "Exact binomial confidence limits":
    <https://www.itl.nist.gov/div898/software/dataplot/refman2/auxillar/exacbici.htm>
    (`docs/sched-hop-implementation-runbook.md`, `a858f6f3`). With zero harmful
    paired discordances the one-sided 95% upper bound is `1 − 0.05^(1/n)`:
    about 17.1% at n = 16, 4.57% at 64, 1.16% at 256 and 0.292% at 1,024. These
    bound a discordance probability, not a dB loss.
  - Dataplot "Prediction limits (mean)":
    <https://www.itl.nist.gov/div898/software/dataplot/refman1/auxillar/predlimi.htm>
    (`docs/triad-performance.md`, `419e6bae`). Mean uncertainty scales as
    `s/√n`, while a single future run keeps `s·√(1 + 1/n)`. More repetitions
    shrink the first but not the scatter, the abbreviated-to-full prediction
    error, or drift to another build or host.
  - Dataplot "Sign test":
    <https://www.itl.nist.gov/div898/software/dataplot/refman1/auxillar/signtest.htm>
    (`docs/triad-performance.md`, `419e6bae`; exact sign test with ties counted
    as nonpositive).
- **NIST *Digital Library of Mathematical Functions*, §8.25 "Methods of
  computation" (incomplete gamma functions).** <https://dlmf.nist.gov/8.25>.
  *Supported* (`docs/lpi-estimates.md`, `419e6bae`): numerical evaluation of the
  Gamma observer model, checked against independent high-precision reference
  points.

## Lunar, EME and HF practice

- **Vilnrotter, V., Lee, D., Mukai, R., Cornish, T., Tsao, P. (2007).**
  "Three-antenna Doppler-delay imaging of the crater Tycho for uplink array
  calibration applications." *IPN Progress Report* 42-169D, 15 May 2007.
  <https://tda.jpl.nasa.gov/progress_report/42-169/169D.pdf>.
  *Supported* (`docs/helper-eme-measurement.md`, `419e6bae`): distinct Doppler
  and delay signatures arise across the lunar surface. A single flat noisy tone
  does not model that propagation.
- **Wade, P. (W1GHZ) (2017).** "Small Dishes and Digital EME."
  *Microwavelengths* column, *QST*, April 2017, reporting the Microwave Update
  2016 demonstration of 10 GHz EME with a 1 m dish.
  <https://www.arrl.org/files/file/Technology/microwave/Small%20Dishes%20and%20Digital%20EME.pdf>.
  *Supported* (`docs/pulse-moment-validation.md`, `419e6bae`): EME needs a
  Doppler and libration channel model. The report's 10 GHz example is not a
  measured 5.8 GHz channel.
- **WSJT-X User Guide.** EME guidance:
  <https://wsjt.sourceforge.io/wsjtx-main_en.html>
  (`docs/eme-amplitude-receiver.md`, `419e6bae`). Full guide:
  <https://wsjtx.github.io/wsjtx/guide-full.html>
  (`docs/link-planner.md`, `419e6bae`).
  *Supported*: −8 dB in 1 Hz is about −42 dB on FT8's 2,500 Hz reporting scale,
  21 dB above the guide's −21 dB FT8 reference threshold. That is a unit
  conversion, not a sensitivity measurement. The EME guidance motivated
  integration tradeoffs without validating this modem.

## Signal design and acoustics

- **Boyd, S. (1986).** "Multitone signals with low crest factor." *IEEE Trans.
  Circuits Syst.* 33(10), 1018–1022.
  doi:[10.1109/TCS.1986.1085837](https://doi.org/10.1109/TCS.1986.1085837).
  Author copy: <https://web.stanford.edu/~boyd/papers/pdf/multitone_low_crest.pdf>.
  *Supported* (`docs/triad-patterns.md`, `419e6bae`): phase choices, crest factor
  and a prescribed multitone spectrum are distinct. Windowed composite power has
  pair terms weighted by `sinc((fᵢ − fⱼ)W)`. Three nonzero fixed tones cannot
  have an exactly constant envelope, because the outer pair's term cannot be
  cancelled by either adjacent pair.
- **Farina, A. (2007).** "Advancements in impulse response measurements by sine
  sweeps." *Audio Engineering Society 122nd Convention*, Vienna.
  <https://www.angelofarina.it/Public/papers/226-AES122.pdf>.
  *Supported* (`docs/triad-live-duplex.md`, `419e6bae`): background on
  sound-card measurement and clock mismatch for the live duplex suite.
- **Yost, W. A. (1996).** "Pitch of iterated rippled noise." *J. Acoust. Soc.
  Am.* 100(1), 511–518. doi:[10.1121/1.415873](https://doi.org/10.1121/1.415873).
  The guide linked <https://pubmed.ncbi.nlm.nih.gov/8675844/>.
  *Supported* (`docs/link-planner.md`, `419e6bae`): temporal repetition can give
  noise a pitch. Longer public patterns repeat less often and may sound less
  growling, a plausible tendency rather than a validated 8× threshold.
- **Boucher, M. A., Christian, A. W., Krishnamurthy, S., Tracy, T., Begault,
  D. R., Shepherd, K., Rizzi, S. A. (2024).** "Toward a psychoacoustic annoyance
  model for urban air mobility vehicle noise." NASA Technical Memorandum.
  <https://ntrs.nasa.gov/citations/20240003202>.
  *Supported* (`docs/link-planner.md`, `419e6bae`): noise annoyance depends on
  several sound qualities, including loudness, roughness and tonality. The
  planner's observer ratio models energy detection, not sound character,
  annoyance or complaints.
- **Analog Devices application note AN-922.**
  <https://www.analog.com/en/resources/app-notes/an-922.html> (no response
  within 25 s on 25 November 2026).
  *Supported* (`docs/modem.md`, `831cb262`): pulse shaping and the receiver
  filter must be designed together. Rectangular pulses keep their sidelobes, and
  public whitening neither adds entropy nor guarantees low probability of
  intercept.
- **Analog Devices application note AN-282.**
  <https://www.analog.com/media/en/technical-documentation/application-notes/an-282.pdf>
  (no response within 40 s on 25 November 2026).
  *Supported* (`docs/triad-background-noise.md`, `419e6bae`): the ideal
  converter convention for the background-noise generator.

## Cryptographic and radio standards

The surviving cryptographic sources (SP 800-38A, RFC 2104 and RFC 5869) are in
[docs/crypto.md](../../docs/crypto.md) and the Sol-6.2 stream-key study.

- **NIST SP 800-38G Rev. 1, *Recommendation for Block Cipher Modes of
  Operation: Methods for Format-Preserving Encryption*, second public draft.**
  <https://csrc.nist.gov/pubs/sp/800/38/g/r1/2pd>.
  *Supported* (`docs/helper-carrier.md`, `c23f28ce`): FF1-encrypted time with a
  20-bit minimum domain of at least 1,048,576 values, consistent with the
  draft's larger minimum domain.
- **NIST SP 800-108 Rev. 1 (Update 1), *Recommendation for Key Derivation Using
  Pseudorandom Functions*.** <https://csrc.nist.gov/pubs/sp/800/108/r1/upd1/final>.
  *Supported* (`docs/triad-patterns.md`, `419e6bae`): pseudorandom key material
  is separated by context. The custom context serialization was not a
  certification claim.
- **47 CFR §15.247.**
  <https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.247>.
  *Supported* (`docs/spread-spectrum-controls.md` and `docs/helper-measurement.md`, `419e6bae`): band-specific
  channel-count, spacing, bandwidth, power and occupancy rules. There is no
  single universal hopping pattern, and passing a dropdown preset is not
  certification.
- **47 CFR §95.965.**
  <https://www.ecfr.gov/current/title-47/chapter-I/subchapter-D/part-95/subpart-D/section-95.965>.
  *Supported* (`docs/oscillator-models.md`, `419e6bae`): a US CB carrier, or SSB
  reference frequency, must stay within ±50 ppm of the channel center. That is a
  transmitter requirement, not a crystal rating or a receiver specification.
- **ETSI EN 300 328 V2.2.2**,
  <https://www.etsi.org/deliver/etsi_en/300300_300399/300328/02.02.02_60/en_300328v020202p.pdf>;
  **ETSI EN 300 220-2 V3.3.1**,
  <https://www.etsi.org/deliver/etsi_en/300200_300299/30022002/03.03.01_60/en_30022002v030301p.pdf>;
  **ETSI EN 300 440 V2.2.1**,
  <https://www.etsi.org/deliver/etsi_en/300400_300499/300440/02.02.01_60/en_300440v020201p.pdf>;
  **Commission Implementing Decision (EU) 2025/105**,
  <https://eur-lex.europa.eu/eli/dec_impl/2025/105/oj/eng>.
  *Supported* (`docs/spread-spectrum-controls.md`, `419e6bae`; EN 300 440 also in `docs/helper-measurement.md`):
  European rules are band-specific. The guide used EN 300 220-2 Table 4 band V,
  clause 4.4.11 and Annex C for sub-GHz; EN 300 328 clauses 4.3.1.4–4.3.1.7 for
  adaptive 2.4 GHz; and EN 300 440 clause 4.2.6 with Annex row 61 of the EU
  decision for 5.8 GHz short-range devices. A 200-channel, 0.4 s illustration is
  not universally compliant with their occupancy, duty-cycle or return-time
  conditions.

## Radio and converter documentation

- **Icom IC-7100 service manual, July 2013 (S-15008XZ-C1).** Public mirror:
  <https://ok2haz.ok2kld.cz/ok2haz/wp-content/uploads/2017/01/IC-7100_service_manual.pdf>.
  *Supported* (`docs/oscillator-models.md`, `419e6bae`): the 41.344 MHz X2001
  reference is a TCXO, not an oven oscillator. A PCM2901 USB codec (IC602) has
  its own 12 MHz crystal (X601), and the internal DSP codec a 24.576 MHz crystal
  (X5051). The manual specifies a five-minute warm-up for RF stability. Nothing
  shows the sound card sharing the RF reference's phase.
- **Icom IC-7100 instruction manual, printed pp. 5-6 and 6-6.**
  <https://www.icomamerica.com/api/download.php?fl=JTJGdXBsb2FkcyUyRnN1cHBvcnQlMkZtYW51YWwlMkZJQy03MTAwX0VOR19GTV81LnBkZg%3D%3D&post_id=2288>.
  *Supported* (`docs/spread-spectrum-controls.md`, `419e6bae`): SSB transmit
  defaults are 300–2700 Hz (MID) and 100–2900 Hz (WIDE). SSB-D receive filter
  defaults are 1.2 kHz, 500 Hz and 250 Hz and must be widened for a wider
  waveform. Nominal containment does not establish flat response.
- **Icom IC-7100 advanced instructions.**
  <https://www.icomfrance.com/uploads/files/produit/not-IC-7100_advanced-instructions-en.pdf>.
  *Supported* (`docs/triad-patterns.md`, `419e6bae`): documented SSB AGC time
  settings include 0.1 s, but the full detector and control law is not
  specified. Filtering and time-varying gain need not commute.
- **Kenwood TS-2000 owner's manual.**
  <https://www.kenwood.com/usa/Support/pdf/TS-2000-Owner-Manual.PDF>.
  *Supported* (`docs/link-planner.md`, `419e6bae`): a 300–2700 Hz transmit
  passband as one representative SSB filter.
- **Texas Instruments PCM2901 datasheet.** <https://www.ti.com/lit/ds/symlink/pcm2901.pdf>.
  *Supported* (`docs/oscillator-models.md` and `docs/clock-sync.md`,
  `419e6bae`): the codec is adaptive for USB playback and asynchronous for
  recording, using a 12 MHz source and packet-derived audio timing. Its crystal
  tolerance is not a measurement of a radio's effective sample rate.

## Works still cited elsewhere

These sources were cited by the same retired guides and remain cited, so they
needed no recovery. The table keeps the link each retired guide used. Where the
current citation uses a different link or edition, it is noted.

| Work | Retired guide and its link | Current citation |
| --- | --- | --- |
| Tandra, R. (2009), PhD thesis, UC Berkeley EECS-2009-192, ch. 2, eqs. 2.5–2.7 | `docs/lpi-estimates.md`: <https://digicoll.lib.berkeley.edu/record/137682/files/EECS-2009-192.pdf> | Resilience research, `robust-nonstationary-channel-research.md`, entry TS08 |
| Zheng, Tse and Médard, low-SNR channel coherence | `docs/triad-stochastic.md`: <https://web.stanford.edu/~dntse/papers/lizhong_low_snr_revised.pdf> | Opus-5.5 chapter 03 and reviews; Sol-6.2 fading-limit study |
| Divsalar and Simon (1990), multiple-symbol differential detection of MPSK | `docs/triad-v3.md`: <https://ntrs.nasa.gov/citations/19900046187> | Opus-5.5 noncoherent and GEMM reviews, doi:10.1109/26.48887 |
| Grünwald, de Heide and Koolen, safe testing | `docs/triad-weak-streaming.md`: <https://arxiv.org/abs/1906.07801> | `docs/symbol-burst.md`, published JRSS-B version |
| Howard, Ramdas, McAuliffe and Sekhon (2021), confidence sequences | `docs/triad-weak-streaming.md`: <https://arxiv.org/abs/1810.08240> | Fable-5.2 chapters 60, 80 and 86 |
| NIST SP 1065, *Handbook of Frequency Stability Analysis* | `docs/helper-carrier.md` (§5.4: overlapping polls are not independent trials; effective degrees of freedom depend on noise type and correlation): <https://tsapps.nist.gov/publication/get_pdf.cfm?pub_id=50505>; `docs/triad-multiday-tracking.md`: <https://www.nist.gov/publications/handbook-frequency-stability-analysis> | Fable-5.2 chapter 55 |
| ITU-R P.372-18 | `docs/triad-v3.md`: <https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.372-18-202609-I!!SUM-HTM-E.htm> | Several `_ref/` and `_appraisal/` studies |
| ITU-R SM.328 | `docs/triad-modem.md`, edition 11: <https://www.itu.int/dms_pubrec/itu-r/rec/sm/R-REC-SM.328-11-200605-S%21%21PDF-E.pdf> | `docs/symbol-burst.md`, edition 12 |
| NIST SP 800-38A | `docs/triad-background-noise.md`: <https://nvlpubs.nist.gov/nistpubs/legacy/sp/nistspecialpublication800-38a.pdf> | `docs/crypto.md`; Sol-6.2 stream-key study |
| RFC 2104 (HMAC) | `docs/packetless-stream-plan.md`: <https://www.rfc-editor.org/rfc/rfc2104.html> | `docs/crypto.md`; Sol-6.2 stream-key study |
| RFC 5869 (HKDF), §3.2 | `docs/symbol-burst-implementation-plan.md`: <https://www.rfc-editor.org/rfc/rfc5869.html#section-3.2> | `docs/crypto.md`; Sol-6.2 stream-key study |
