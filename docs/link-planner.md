# Link Planner

The desktop **Link Planner** tab remains available beside Console. It retains
its settings controls and presets, launch-command editing, generic link-budget
arithmetic, and plots. These are interface conveniences; choosing or loading a
setting does not start transmission. Console and Link Planner edit the same
single Target SNR, initially 32 dB-Hz, saved as `--target-snr`.

The former Robust receive-probability, CPU-pace, clock-coverage, RAM-search,
and observer/receiver-time forecasts are unavailable. The tab does not run a
Robust receiver or prove that a candidate is receivable. Robust operation is
connected separately through the native GUI session. Its provisional duration
model is not a receive-probability, CPU-capacity or field-sensitivity guarantee.
See [current Robust architecture and limits](robust-current.md).

Older model assumptions, example targets, and projection measurements are
historical single-carrier research. They do not describe the canonical native
`IndependentApskV2` C++ source/Helper/complete-receiver stack. Retained historical
complete-work and fixed-geometry performance estimates keep their original
source/runtime scope in [triad-performance.md](triad-performance.md). They do not
qualify the native GUI runtime, provide a Link Planner forecast, or establish
physical reception.

The Link Planner can still help edit and move settings into the application.
Check the accepted settings after loading. Generic link-budget arithmetic does
not replace receiver validation or the explicit calendar/noise setup required
for live Robust operation. Retired Robust audio CLI commands remain unavailable;
the launch-command editor edits GUI settings rather than invoking those commands.
