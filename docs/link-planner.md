# Link Planner

The **Link Planner** tab plots native Robust DATA duration, provisional DATA
detection, host DSP workspace, and any available local receiver samples against
one Target SNR axis. Console and Link Planner share the target in dB-Hz, initially
32, and save it as `--target-snr`. Power, path loss, and noise density determine
the actual C/N₀ used for the link estimate. The target selects the public
waveform and can leave the duration unchanged across a range of settings.

The workspace curve quotes one owner at a time for the first DATA window and
two scored quiet windows, using the current local memory allowance, production
DSP bounds, and all `W = 2*H` workers by default. The sweep reuses the minimum
tile's spectral allowance and checks full production shaping before offering a
target action. The quote includes the nominal input cap, source allowance,
process baseline, stacks, and receiver state. Where the input cap alone is
exceeded, the plotted memory requirement is a lower bound. A selected
GPU route contributes its host workspace once initialized. Later windows,
additional simultaneous owners, live provider uncertainty, and actual device
allocation can add constraints. The quote describes nominal host admission
and does not certify the sustained receiver capacity.

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

With **Simulation** selected, **Sample selected target** tests a bounded raw-bit
episode with the configured native receiver and local processing route. It uses
four independent channel and noise draws, balanced across both bit values,
with a ten-second processing deadline and a one-million-sample cap per trial. A
success requires the correct bit and two physically scored quiet slots. The
planner uses the first balanced pair if the second pair does not finish. It
reports a 95% sampling interval with joint coverage for the two predeclared
sample sizes. The processing line shows the slowest completed simulation's
elapsed processing seconds per audio second, including source and channel work.
The planner retains up to eight sampled geometries for comparison while the
other link settings, processing route, and worker allocation match. A short
sample does not qualify sustained real-time capacity, field sensitivity, or
unknown-calendar reception. If the sample hits its work limit or lacks an
applicable route, the affected curves may remain blank.

The maintained [GPU-assisted forecast procedure](triad-performance.md#runpod-gpu-assisted-manual-capacity-forecast)
pairs GPU-observed native work with matching local phase costs and independent
holdouts. The Link Planner's local sample does not transfer a measurement
across devices, arithmetic routes, or receiver geometries. Historical
single-carrier results retain their original scope. See the
[current Robust architecture](robust-current.md) for configured-calendar limits
and the [Link Planner checks](validation.md#native-link-planner--24-november-2026)
for the bounded local results.
