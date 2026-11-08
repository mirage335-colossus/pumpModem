# Development contract

This page covers maintained cross-component boundaries. The current Robust
architecture is documented in [robust-current.md](robust-current.md); the
[protocol](protocol.md) and [GUI contract](gui-contract.md) remain the source of
truth for shared stream and presentation behavior. Do not treat retired
single-carrier or standalone triad/Helper implementations as compatibility
requirements.

## Shared security and presentation

Restrict received content before it reaches native GUI text, clipboard requests,
CLI text output, or transmit-draft processing. The allowed-byte policy and
Developer/Shellcode exception are defined in [security](security.md#received-content-presentation)
and enforced by the shared [GUI contract](gui-contract.md#received-content-and-drafts).
Keep original payloads only in bounded receive memory for explicit user-selected
saving. Raw-bit presentation may preserve exact bits; it must not expose rejected
source bytes as text, escapes, or an unbounded output path.

Preserve input validation, selected-index checks, and trusted allocation extents
in [receive processing hardening](receive-processing-hardening.md). Keep shared
GUI behavior consistent across FLTK, Rev, terminal, framebuffer, and browser
adapters where supported. A view or adapter change does not authorize weakening
source restrictions or creating a separate receive policy.

## Fast and Legacy

Fast and Legacy are independent modem implementations. Preserve their documented
formats, exact input/output handling, and distinct validation limits in
[Fast guidance](indexes/fast.md) and the [Legacy reference](legacy-modem.md).
A Robust architecture or protocol change does not change their compatibility
contracts. Keep shared UI and audio changes covered by the applicable frontend
and hardware-independent checks in [building.md](building.md#testing-stages).

## Simulation and precision

When adding or changing sampled channel behavior, follow
[channel-models.md](channel-models.md). Preserve selected Doppler, delay-spread,
selective-amplitude effects, continuous state and complete delayed tails. None/Off
is an explicit baseline. Keep simulated metadata and software-test evidence
distinct from physical-device measurements.

There is no current Robust GPU precision control. The
[historical GPU precision research](gpu-precision-controls.md) records lessons
from removed receiver experiments. Any future arithmetic feature needs an
explicit current contract, strict references, and fallback reporting; validate
intensive Robust work only under the abbreviated policy in
[building.md](building.md#testing-stages).
