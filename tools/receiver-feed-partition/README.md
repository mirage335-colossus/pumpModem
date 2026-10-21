# Foreground receiver work diagnostic

This follow-up to `gui-stop-interval` divides the source thread's receiver work
without changing the original GUI controller test, its stop deadline, receiver
coverage, or input processing. The overlay applies only to a private source copy
at `8c54277ca9ec7bf35dee65126ec0d78f3a484f74`.

The prior standard-runner wave, Actions run `39648615451`, passed both original
strict GUI tests serially and in parallel. Its passing instrumented EU captures
spent 6.91–8.23 seconds in receiver feeding, including 6.88–7.97 seconds of source
thread CPU. The 64 enclosed stop blocks retained 22 owners without a rising CPU
trend. These results do not explain the earlier full Debug stop failure.

The new wave builds only `test_gui_controller`, once, using the same Debug
GCC 11 ASan/UBSan configuration on a standard Ubuntu runner. It runs the complete
no-argument test under its existing 600-second outer limit. The original
10-second stop and source-tail assertions still decide application success.
Prior original strict results are reused; no duplicate original build is made.

Bounded phase and owner counters distinguish input/binning, FFT preparation and
scoring, track continuation, publication, and residual source work. Worker CPU
must not be attributed to a caller's thread clock. Output is sealed after the
controller joins, alongside the existing stop markers and captured PCM. Count,
clock, storage, closure, and output errors invalidate measurement completeness.

Read phase costs only within the requested EU stop window. Generated samples
include prefetch and differ from transmitted samples; bank ingress is not an
individual receiver's processing frontier. A delayed completion-event frontier
does not certify execution of every unfinished-symbol calculation.

The snapshots are a diagnostic partition, not qualification. Random fixtures
are not waveform matched to earlier runs, so their elapsed differences cannot
establish an instrumentation or production performance effect. Recorded clock
cost calibration is partial overhead evidence and is never subtracted from
test latency. A dominant phase selects the next code path to inspect; a large
unexplained remainder requires better measurement before a production change.

The next refinement follows run `39661504253`: its original controller passed
with an EU stop of 9.267672915 seconds. Across 64 fully enclosed stop blocks,
FFT scoring used 6.691242846 of 8.708780651 seconds of foreground thread CPU.
Work stayed near 23 scoring calls per input block without a rising trend; no
single receiver identity dominated. That identifies a work region, not waste.

The refined overlay snapshots 22 existing acquisition work counters once per
concrete raw/projected push, records the receiver's input/bin/acquisition
coordinates and its last observed execution geometry, and adds exclusive
source-thread template, FFT kernel, dispatch, evidence and table-preparation
costs. Worker threads remain outside this recorder. FFT kernel cost includes
input transforms as well as template/inverse transforms; nested mode wrappers
mean scope call counts are not transform counts. The existing work counters
supply execution counts. Last geometry is a snapshot at the end of the push,
not a claim that optional scratch/cache state was constant throughout it.
Acquisition `next_start * bin_samples` marks completed acquisition batches;
it does not certify unfinished-symbol continuation or physical absence.

Decision rule: disproportionate repeated template work or rejected narrow
ranges selects that specific path for a controlled correction; sustained
necessary transforms with reconciled counts support reassessing hosted resource
scheduling. Broad residuals, changed coverage or incomplete capture require
repairing the measurement. No conclusion follows merely from a passing test.
The original stop deadline and every assertion remain in force. Expected cost
is one standard runner for about 25–40 minutes; no additional baseline build.
