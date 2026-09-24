# Data Pump scope — version 001_00

Version **001_00** is the feature-complete Data Pump baseline. The
[requirements matrix](requirements.md) describes the supported software, the
[protocol](protocol.md) defines regular-mode wire behavior, and the
[Fast mode specification](fast-mode.md) defines the independent Fast transport.
These documents replace the original proposal as the product scope.

Data Pump is a civilian audio modem for transferring text, explicit bits,
screenshots and files between computers, including through audio-connected
radios. It provides regular pattern-based weak-signal operation and independent
Fast transfer modes, with shared GUI and CLI operation. The separate
[Legacy Modem](legacy-modem.md) console supports BPSK31, BPSK125 and Olivia-4/2k
radio text. The [development contract](development.md) preserves exact short-message bit counts,
fixed locally configured intervals, physical completion and incremental pending
reception.

## Operator procedures

Operators establish written procedures for the carrier frequencies, compatible
modem settings and interpretation of transmitted bits. Those procedures define
whether `0` or `1` represents status or distress, and whether the meanings use
the same carrier or different carriers. Data Pump transmits and displays the
configured message; the operational meaning and response belong to the
operators' agreed procedures.

## Remaining implementation

Automatic radio-frequency tuning to find usable HF shortwave ionospheric
propagation frequencies is not yet implemented. This is the remaining planned
capability, as an alternative to Automatic Link Establishment (ALE) and
Frequency Hopping Spread Spectrum (FHSS). The existing modem parameter planner
and bounded receive-frequency search operate within locally configured settings;
they do not provide this radio-frequency selection function.

Feature completeness describes the product scope, not a claim of universal
hardware qualification, measured sensitivity or release certification. Actual
validation evidence and its limits remain in [validation.md](validation.md).
