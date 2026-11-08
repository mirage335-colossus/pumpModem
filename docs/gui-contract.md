# Shared GUI contract

The FLTK and Rev adapters consume the same toolkit-neutral application state,
field declarations, actions, document records, and presentation data. Application
code owns modem workflows and declares each control's value, options, and
availability; adapters own native widgets, drawing, file choosers, and platform
services. Keep user-visible values and actions consistent across adapters.
See [GUI architecture](gui-architecture.md) for shared-code structure.

## Modems and current availability

The Modem dropdown retains **Fast Modem**, **Robust Modem**, and **Legacy Modem**;
ordinary launches default to Fast. Fast and Legacy have active controllers and
keep their own settings and behavior. Robust controls, tabs, and saved settings
remain visible for future reconnection, but its transmitter and receiver are
unavailable in the GUI. Selecting Robust does not start a modem, simulation, or
receive session. See [current Robust architecture and status](robust-current.md).

The **Link Planner** retains its controls, presets, command editor, general
link-budget arithmetic, and generic plots. Robust receive-probability, CPU-pace,
clock-search, and workspace forecasts are unavailable; see the concise
[Link Planner status](link-planner.md).

## Received content and drafts

Apply the same received-text restrictions before content appears in a GUI field,
clipboard request, draft, or preview. Use the sanitized presentation, not
original received bytes; preserve the original payload only for bounded in-memory
storage and an explicit operator-selected save. Raw-bit views may retain exact
bits without turning rejected source bytes into text. See the
[received-text policy](security.md#received-content-presentation) and
[receive processing boundaries](receive-processing-hardening.md).

Keep draft edits separate from received records. A receive result must retain its
identity and status while an adapter redraws or changes pages; stale asynchronous
updates must not overwrite a newer selection. Pending content, when supplied by
an active modem, must remain visibly pending and must not gain completed-message
copy or save actions prematurely. Shared rows, selection, and safe copy behavior
belong in the application layer, not separate toolkit-specific modem logic.

## Audio and lifecycle

Audio-device selection, channel routing, playback volume, and format labels are
shared application settings. Platform audio adapters report failures through
the owning service response, preserve the selected mode, and do not silently
replace user settings. Starting or stopping one modem must not dispatch stale
work to another. Cancellation and close operations stop only the work owned by
the active application generation; delayed replies from an earlier generation
are ignored.

## Presentation and native adapters

Pages declare controls and actions once. Adapters present the same text, values,
availability, and state, subject to native glyph metrics, popup placement, and
platform dialogs. Shared colors and application chrome preferences come from
the shared theme/layout declarations. Full-window plots and bitmap content are
produced by shared code; native adapters do not compute modem or receive state.

Features using existing controls should change shared declarations and
application mapping only. Preserve selection and draft state when changing tabs
or switching active modems. A retained Robust control is not a promise of
backend support; keep its unavailable state explicit until the canonical path is
connected.
