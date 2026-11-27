# Link Planner

The **Link Planner** tab plots native Robust DATA duration, provisional DATA
detection, host DSP workspace, and any available local receiver samples against
one Target SNR axis. Console and Link Planner share the target in dB-Hz, initially
32, and save it as `--target-snr`. Power, path loss, and noise density determine
the actual C/N₀ used for the link estimate. The target selects the public
waveform and can leave the duration unchanged across a range of settings. The
plot marks the selected target, the link-budget target, and any plotted
crossings of the 1 s/s processing and 100% workspace limits. When a host
workspace quote is available, the lower end of the target axis can extend
to eight dB below the weakest fitting target found in the sweep.

Each legend entry shows a short sample of its curve beside the name and
selected value. Match the color or the dash pattern to the graph. The native
application, zLab worker and standalone WebAssembly build draw the chart and
these samples with the same C++ code. zLab runs the planner on the selected
worker, so its workspace and processing figures describe that machine.

The standalone WebAssembly app plots DATA duration and modeled detection for
the same settings. Its current browser runtime has no supported Robust receiver
workers. Workspace, measured processing time and RX samples are therefore unavailable
there, including when a RAM percentage is selected. Use the native application
or a zLab worker to run a receiver simulation.

The workspace curve quotes one owner at a time for the first DATA window and
two scored quiet windows, using the current local memory allowance, production
DSP bounds, and all `W = 2*H` workers by default. The sweep reuses the minimum
tile's spectral allowance and checks full production shaping before offering a
target action. The quote includes the nominal input cap, source allowance,
process baseline, stacks, and receiver state. Where the input cap alone is
exceeded, the plotted memory requirement is a lower bound. A selected
GPU route contributes its host workspace once initialized. The first sweep
can show a labeled CPU workspace preview while that route starts. The planner
checks a committed target again against the selected route before sampling.
The backend can initialize for planning when the audio provider cannot
schedule live playback. Later windows, additional simultaneous owners, live
provider uncertainty, and actual device allocation can add constraints. The
quote describes nominal host admission.

**Stronger** and **Weaker** normally move about 1 dB and skip targets that fail
the nominal workspace check. Committing an unsupported target keeps a checked
stronger target if available, preferring the closest one found. If no stronger
target fits, the planner tries a checked weaker target. It tests whole dB values,
adds fractional probes around changes in tile geometry near the workspace
limit, and refines observed boundaries. A bounded sweep can miss narrow fitting
intervals. The current native receiver has no amplitude-only alternative.
Loading a launch command preserves its shared on-air settings. Applying or
loading settings does not start transmission.

The immediate detection curve is the provisional fixed-energy true-candidate
DATA bound evaluated at the actual link C/N₀ for each selected geometry. It
covers the model's supported channel settings and does not include successful
acquisition, bit discrimination, or quiet completion. These limits are
described in [duration planning](triad-duration-planning.md).

**Run simulation** uses the configured geometry and receiver route. Committing
a changed Target SNR in the Link Planner or using **Stronger** or **Weaker**
starts the same simulation after workspace fitting. In live mode, the planner
allows up to five seconds for capture to release the receiver, then resumes
capture on the configured calendar when sampling ends. The Simulation selector
and saved settings stay in place. **Cancel simulation** and the Console's
**Cancel TX** both stop a planner simulation. Finite-duplex lab sessions
reserve capture for their physical episode.

The simulation streams two one-bit trials in fixed-size blocks. It uses one
trial per bit value with independent channel and noise draws. It ends when the
pair finishes, when cancelled or superseded by a settings change, or on a
receiver error. There is no elapsed-time cutoff for sampling. Long geometries
can take several minutes or more. During sampling the planner shows the
selected backend, the observed first-slot scoring stage and coarse route,
accepted modeled input, and the number of public slots scored.

A success requires the correct bit and two physically scored quiet slots.
The planner reports the sample fraction and a 95% sampling interval after
both trials. The processing line shows the slowest completed trial's elapsed
processing seconds per audio second, including source and channel work.

For an eligible first-slot native CPU frequency search in progress, the planner
can also project the cost of that span from two disjoint three-second work
windows. Each window needs at least 24 completed workspace-frequency pairs from
eight software lanes for a time range. The range applies those two rates to the
full same-shape correlation and peak-retention work. Rates within 15% qualify
the conditional time range. Wider variation is shown with the implied time at
the observed rates. Preparation, proposal selection, fine scoring and quiet
slots have their own costs.
Completed native frequency-span timing remains visible during proposal
selection and fine scoring. Lag, grouped and backend searches still show the
observed route and stage. Native frequency work can be metered when it is
executed as a fallback from the selected backend.
The planner retains up to eight sampled geometries for comparison while the
other link settings, processing route, and worker allocation match. A short
sample does not qualify sustained real-time capacity, field sensitivity, or
unknown-calendar reception. Missing completed trials or an unavailable
receiver route can leave the affected curves blank.

The maintained [GPU-assisted forecast procedure](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast)
pairs GPU-observed native work with matching local phase costs and independent
holdouts. The Link Planner's local sample does not transfer a measurement
across devices, arithmetic routes, or receiver geometries. Historical
single-carrier results retain their original scope. See the
[current Robust architecture](robust-current.md) for configured-calendar limits
and the [Link Planner checks](validation.md#native-link-planner--24-november-2026)
for the bounded local results.
