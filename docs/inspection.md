# GUI inspection and status

The native Robust GUI session supplies live Waveform, Waterfall, Receiver input
I/Q, Pattern evidence and Pattern steps views. These consume bounded actual
session observations and complete native score records. Pattern evidence shows
bit/admission evidence separately from quiet energy; steps retain public-slot
classification, completeness and stage/resource diagnostics. Simulation settings
and transmitted bits must not masquerade as receiver evidence.

**Detailed transmission inspection remains deferred.** The separate modem-flow
inspection page reports that limitation explicitly. It is not the same feature
as the connected live pattern plots, and its absence does not mean Robust
transmission, reception or simulation is unavailable. Link Planner
receive-probability and CPU forecasts also remain unqualified for the native
receiver; provisional duration planning does not establish them.

Robust uses one C++ source/Helper/complete-receiver stack linked into the GUI.
Python is a test/benchmark tool and temporary numerical reference, not an
operating receiver. Live operation has configured-calendar, keyed-ordinal and
local noise-model requirements. Functional coverage does not qualify unknown
waveform-address acquisition, native real-time capacity or physical hardware.
See the [GUI contract](gui-contract.md), [current Robust status](robust-current.md),
[validation](validation.md#native-gui-integration) and [Link Planner](link-planner.md).

Fast and Legacy inspection and operation are described in their maintained
guides: [Fast mode](fast-mode.md), [Legacy reference](legacy-modem.md), and
[interface contracts](frontend-interfaces.md). Shared received-content
handling and security requirements are in [security](security.md).

Historical constellation and decoder measurements remain available as
[waveform research](pattern-constellation.md#receiver-interpretation). They are
source-bound findings about retired receiver implementations, not current GUI
behavior, decoding limits, or qualification evidence.
