# Link Planner

The desktop **Link Planner** tab remains available beside Console. It retains
its settings controls and presets, launch-command editing, generic link-budget
arithmetic, and plots. These are interface conveniences; choosing or loading a
setting does not start transmission.

The former Robust receive-probability, CPU-pace, clock-coverage, RAM-search,
and observer/receiver-time forecasts are unavailable. The tab does not run a
Robust receiver or prove that a candidate is receivable. Its retained controls
do not imply that Robust is connected to the GUI. See [current Robust
architecture and limits](robust-current.md).

Older model assumptions, example targets, and projection measurements are
historical single-carrier research. They do not describe the canonical native
`IndependentApskV2` transmitter/channel/Helper Generator or its staged Python
receivers. The available statistical performance estimate for Robust is limited
to the fixed-geometry kernel measurements described in
[triad-performance.md](triad-performance.md); it is not a link-planner estimate,
end-to-end receiver latency, or physical reception qualification.

The Link Planner can still help edit and move settings into the application.
Check the accepted settings after loading. Robust operation remains unavailable
in the GUI pending reconnection, and generic link-budget arithmetic is not a
substitute for receiver validation.
