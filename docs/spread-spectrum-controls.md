# Spreading and hopping controls

The GUI retains spreading, interleave, frequency and timing controls for future
connection to the [canonical Robust backend](robust-current.md). Their presence
or saved values do not establish working Robust transmission/reception. The old
single-carrier DSSS, Fake FHSS, deferred-hop and planner implementations are
retired; their versioned calendars and compatibility recipes are not contracts
for the replacement.

## Distinct effects

Spreading changes waveform coefficients and occupied bandwidth. Interleaving
changes the assignment of those coefficients in time; it does not add energy.
Sequential hopping changes the frequency used at successive times. An envelope
that mimics hop gaps does not retune physical hardware.

The current native `Format` owns Added Symbols, interleave, three adjacent
carriers and optional dwell envelopes. RF retuning is a channel operation.
Keep complete fine-cell geometry, quiet intervals, ramps and pulse margins;
[source geometry](triad-v3.md) and [channel models](channel-models.md) describe
those boundaries. Channel echoes can persist through a transmitter quiet gap.
Neither a zero source sample nor a nominal hop guard proves zero received energy.

## Power, time and security

Keep active signal energy distinct from elapsed time, occupied bandwidth and
nominal coefficient power. Ramps and gaps change useful exposure; counting a
spreading factor twice creates fictitious processing gain. A wider nominal band
does not automatically produce independent noise or power samples.

Keyed operations need separate cryptographic purposes and stable canonical
addresses; follow [cryptography](crypto.md), not a historical waveform recipe.
Cache identity must include the applicable key, epoch, source revision and
geometry. Previewing or partitioning a stream must not rewind or reseed it.
Clock and audio uncertainty remain separate from a hopping schedule; see
[clock synchronization](clock-sync.md).

Retained controls must continue to round-trip through shared GUI settings.
Future reconnection should validate how each setting maps to the one native
format and expose unsupported combinations explicitly. No setting justifies
restoring parallel implementations or expensive full-calendar regression runs;
use the [hierarchical testing contract](triad-performance.md).
