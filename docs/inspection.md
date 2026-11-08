# GUI inspection and status

This page replaces the former Robust modem-flow and receiver-inspection
description. The current GUI preserves shared controls, tabs, composition and
the generic Link Planner, but Robust transmit/receive, simulation, probability,
and constellation inspection are unavailable while the canonical backend is
not connected. See the [GUI contract](gui-contract.md),
[current Robust status](robust-current.md), and [Link Planner map](link-planner.md).

The retained native Robust implementation consists of the
`IndependentApskV2` waveform, shaping, streaming sampled channel, and Helper
Generator. Python `WeakReceiver` acquisition and `PracticalTrackingReceiver`
tracking are separate stages. They are not an integrated receiver service or a
GUI feature; the GUI does not claim end-to-end reception or physical-link
qualification.

Fast and Legacy inspection and operation are described in their maintained
guides: [Fast mode](fast-mode.md), [Legacy reference](legacy-modem.md), and
[interface contracts](frontend-interfaces.md). Shared received-content
handling and security requirements are in [security](security.md).

Historical constellation and decoder measurements remain available as
[waveform research](pattern-constellation.md#receiver-interpretation). They are
source-bound findings about retired receiver implementations, not current GUI
behavior, decoding limits, or qualification evidence.
