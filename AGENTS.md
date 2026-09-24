# Development requirements

Preserve the existing short-message, fixed-interval and pending-reception behavior.
Read [the development contract](docs/development.md) before changing transport,
compression, receiver progress, CLI messaging or GUI message presentation.

- Nonempty text of **1–16 source bytes inclusive** uses the existing fixed short
  dictionary with its exact codes and bit endpoint. Explicit binary input sends
  exactly its entered bits, including leading zeros and partial bytes.
- Neither short path gains a header, marker, padding, transmitted length, FEC or
  MAC. Each extra bit may cost hours or longer to transmit.
- Longer text and every attachment retain the locally configured, fixed
  **192-bit marker + 128 coded bytes** interval format. Received lengths must not
  choose modem framing, allocation or completion. Source interpretation stays
  behind physical completion.
- Only observed absence across fully scored symbols ends reception: consecutive
  failed durations must cover six seconds. An hours-long symbol must finish
  before being classified absent. EOF, cancellation, codec ends and successful
  correction/authentication do not substitute for this event.
- Expose every newly accepted bit on the next progress poll and update the same
  pending GUI row. Never batch behind a byte, interval, dictionary token or full
  message. Preserve exact prefixes within the existing diagnostic retention
  bound and distinguish pending from completed data.

These are compatibility requirements for ordinary development and refactoring.
Keep the independent wire vectors, physical-end tests and shared GUI regressions
listed in the contract; do not weaken them to accommodate an unintended change.
Documentation/test-only work must leave runtime behavior unchanged.
