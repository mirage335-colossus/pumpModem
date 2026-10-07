# Oscillator search and long-symbol validation

This record covers the October 7, 2026 implementation over source commit
`1907c1b57ad0fc2d281ede9476caba7594b26f9c`. The implementation keeps real PCM
input/output, separates RF tuning metadata from the modem tone, and searches
explicit frequency/rate pairs derived from the declared oscillator references.
See [reference models](oscillator-models.md) and
[search computation](search-compute.md).

## Qualification

Focused receiver, correlator, numerical kernel, search geometry, transfer,
estimate, CLI and shared GUI checks passed during implementation. Coverage
includes independently specified frequency/rate errors, shared-reference
mapping, exact search endpoints, strict rejection immediately outside a bound,
configuration persistence, and observed-absence completion.

Six additional CLI real-stream simulations used 10/30 MHz translation,
upper/lower sidebands, 50/1,500 Hz PCM carriers, and both signs of a 0.0001 ppm
shared-radio clock offset. Every case decoded exact `011` / `a`, reported no
missing symbols, and completed after observed absence. These are simulations,
not measurements of radio hardware or a GPS disciplining loop.

The pulse tests compare both real carrier images, partial observations,
fractional rates, encrypted templates, whole-symbol/four-section/differential
evidence, and changing input chunks against raw fits. A separate eight-symbol
fixture compares all 48 evidence rows across two half-chip origin parities and
a clipped endpoint, including the exactly sampled quarter boundary at sample
600000. Existing 64 KiB and 16 MiB memory cases remain covered. The receiver
tests retain immutable bit prefixes and require one completion for competing
frequency/rate alternatives.

The fresh Linux Release build enabled FLTK, terminal, framebuffer and native
web-worker targets. All **180 registered general cases passed** across the full
run and targeted reruns. The initial run passed 173 cases, including the complete
64-seed probability calibration (925.27 seconds) and both web-live sample rates
(137.24 seconds). Six preview/packaging fixtures initially failed because the
sandbox denied local sockets; their exact commands passed with the necessary
local access. The remaining test's tight-budget setup was corrected to exclude
the optional pulse cache from its reference allocation. It then passed in the
qualification build, preserving its fallback and dynamic-shrink assertions.
Modem and search source remained unchanged throughout these final checks.

The updated planner exposed native document clipping and focus issues. Both
backends now preserve a retained editor's viewport position across document
replacement; explicit scrolling and disabled or removed controls remain
authoritative. FLTK uses cumulative clips without exhausting its native clip
stack. Its 80-level raster regression, focused editor regression, full workflow
(218.06 seconds), full adapter suite (118.97 seconds), document suite
(0.10 seconds), and application self-check (18.10 seconds) passed. Native
fixtures were updated to find controls by identity, count actual callbacks, and
wait for the visible overlay and queued paint before capturing hover pixels.
Their interaction and pixel assertions remain intact.

Rev built with Clang 19 and passed all **38 shared GUI cases** (257.65 seconds).
Its final native workflow (430.27 seconds), complete adapter suite (130.89
seconds), platform suite (6.24 seconds), 1x/2x coordinate suites (5.26/5.32
seconds), and application self-check (34.62 seconds) passed. These native runs
used a 2400x1800 Xvfb display and software OpenGL with four rendering threads.
An earlier one-thread adapter run exhausted its 330-second outer limit; that
attempt is not counted as a pass. The successful rerun retained the same
330-second limit and every assertion.

The Rev regressions distinguish a hidden editor's stale rectangle from actual
document movement, input eligibility and focus. They also reproduce and verify
the correction for pending overscroll: clipping now uses the bounded scroll
position, preserving focus when an editor remains visible at the boundary.

These are local Linux results. Remote CI, Windows, macOS, cross-platform SDK
builds and physical radio hardware were not qualified.
Build-tool fixture passes do not turn their unavailable-platform subcases into
tested packages.

## Measured receiver work

These are single-run process CPU measurements on the development host, using
Release GCC 14.2, one DSP worker, a 100 Hz bandwidth, private pattern and Data
keys, five frequency hypotheses and one nominal rate. The receiver budget was
64 MiB and start uncertainty was 0.04 seconds. Timing covers input pushes and
draining results; waveform/noise generation and construction are excluded.
Each 32-second-symbol case observes 66 seconds of real samples and admits the
same two bits. These captures do not include physical completion.

Holding the actual input rate at 6 kHz isolates the carrier setting:

| PCM carrier | Previous CPU seconds | New CPU seconds | New peak reported workspace | Bits |
| --- | ---: | ---: | ---: | ---: |
| 50 Hz | 7.471 | 0.876 | 651,815 bytes | 2 |
| 1.5 kHz | 7.300 | 0.911 | 651,814 bytes | 2 |

This workload is about eight times faster at either carrier. The two new
times differ by about four percent; a single run does not establish a precise
carrier-dependent difference.

With the existing sample-rate planner allowed to choose different input rates:

| PCM carrier | Input rate | Previous CPU seconds | New CPU seconds | Bits |
| --- | ---: | ---: | ---: | ---: |
| 50 Hz | 400 samples/s | 0.456 | 0.296 | 2 |
| 1.5 kHz | 6,000 samples/s | 7.535 | 0.895 | 2 |

The projected private fit follows chip cadence. Input mixing, pulse projection,
and fractional kernel updates still depend on the real sample rate. RF
translation metadata does not request RF-rate samples or add cipher work.
Smaller oscillator-derived banks are an additional saving; these equal-bank
comparisons do not count that saving.

Long-symbol fractional-rate work was also measured with five explicit pairs,
each using the indicated nonzero rate. These are 20-second noise captures with
four-hour symbol geometry, so they test bounded ongoing work, not reception
of a completed four-hour bit:

| PCM carrier / input rate | Clock error | CPU seconds | Peak reported workspace |
| --- | ---: | ---: | ---: |
| 50 Hz / 400 samples/s | 0.0001 ppm | 0.091 | 882,888 bytes |
| 50 Hz / 400 samples/s | 200 ppm | 0.137 | 882,888 bytes |
| 1.5 kHz / 6,000 samples/s | 0.0001 ppm | 0.289 | 643,848 bytes |
| 1.5 kHz / 6,000 samples/s | 200 ppm | 1.167 | 643,848 bytes |

These are measured finite banks, not claims that five pairs cover every
oscillator model for a four-hour symbol. Actual requested and covered search
bounds are reported independently by the application.

## Limits

No external I/Q interface, radio driver, GPS hardware discipline, Doppler
tracking, or drift-trajectory search was added. Wide real streams can use the
reference policy; native capture retains its existing supported device and
passband limits. Phase diffusion remains separate from static offset coverage.
The planner's compute coefficients are engineering allowances rather than
these measured timings. Fractional kernel preparation is explicitly modeled
conservatively, and the current projected core is serial.
