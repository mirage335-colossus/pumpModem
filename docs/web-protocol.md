# Local browser-host interface, version 1

This is a binary interface over inherited anonymous pipes or local Worker
messages. It is not HTTP, CGI, FastCGI, a socket protocol or the acoustic modem
format. Its only implementation authority is the shared C++ application. See
[the architecture and security boundary](web-frontends.md) and
[browser integration](../web/README.md).

## Native invocation

The existing embedding application launches `datapump-worker` with actual
anonymous pipes for stdin and stdout, an allowed diagnostic stderr, and no other
inherited descriptors. `--file-events-fd N` optionally retains one additional
anonymous read pipe for the trusted host's file service. Use explicit descriptor
inheritance and one process per controlling session/principal. The worker checks
FIFO identity, anonymous-pipe identity and access mode; socket-backed stdio and
named pipes fail. An incomplete frame at EOF is an error.

The current worker installs a Linux seccomp filter with `no_new_privs` before
application startup. It denies socket creation, connection, listening, all
socket I/O/options, `socketcall`, x32 alternate syscall numbers, `io_uring`,
descriptor stealing with `pidfd_getfd`, `ptrace`, and BPF. Foreign syscall
architectures are killed. The policy remains inherited by new tasks/processes.
Unsupported OS/architectures fail closed; this is not a Windows sandbox claim.
Native audio/window libraries are absent from this composition. The filter is
specifically a no-socket boundary, not general filesystem or CPU containment.

`--version` and `--self-check` install the same boundary but require no streaming
pipes. Normal launch checks its pipes and uses a private temporary workspace for
imported/exported file objects. That workspace is removed on orderly exit;
abnormal process termination can leave a private directory for host cleanup.
The host owns web authentication, transport backpressure, HTTPS, file policy,
resource limits and child termination. Application packages ship no network relay.
For a temporary repository host using these pipes, see [COMPILE-web](../COMPILE-web)
and `tools/preview.py`.

## Encoding and bounds

Every frame is `DPW1` (four ASCII bytes), a little-endian `u32` type, a
little-endian `u32` payload length, then that many bytes. A payload is at most
8 MiB. Unknown fields/types, overflow and truncated framing never select a new
transport. Strings are `u32` UTF-8 byte count plus bytes, not NUL-terminated.
The decoder bounds before allocation; the bridge separately validates UTF-8,
control-specific edit limits, visibility and enablement. Integers are unsigned
unless stated otherwise; floats are IEEE float32/float64, little endian.

An `Event` payload is:

| Field | Encoding |
| --- | --- |
| Version | `u32`, currently 1 |
| UI generation, sequence, opaque target | Three `u64` values |
| Primitive kind | `u32`: edit=1, select=2, toggle=3, activate=4, preset=5, submit=6, record=7, click=8, double click=9, wheel=10, navigate=11, key=12, service=13, close=14 |
| Flags | `u32`: checked=1, cancelled=2, Ctrl=4, Shift=8, Alt=16; other bits rejected |
| Amount | Signed 32-bit integer encoded as its bits |
| Value, error | Two counted strings; at most 1 MiB and 4096 bytes respectively, with tighter declaration limits afterward |

IDs and sequences in JSON snapshots are decimal strings, preserving full `u64`
precision in JavaScript. A client uses the snapshot generation and a strictly
increasing event sequence. IDs identify only current generic declarations and
services, never pointers, feature enums or filesystem paths. A rejected admitted
event also consumes its sequence. Reconnect requires a fresh snapshot and cannot
replay old events. Only the local trusted host can request tracked file completion;
it is not a browser event flag.

## Incoming frames

| Type | Payload |
| --- | --- |
| 1 viewport | `u32 width, height`, each 240–4096 |
| 2 event | `Event` |
| 3 audio configure | `u64 audio_generation, u32 actual_sample_rate` |
| 4 capture PCM | `u64 generation, stream, first_frame; u32 count; float32[count]` |
| 5 playback ready | `u64 generation, stream` |
| 6 playback progress | `u64 generation, stream, consumed_frames; u32 drained` (0 or 1) |
| 7 audio interrupted | `u64 generation; string reason` (512-byte limit) |
| 8 playback flushed | `u64 generation, stream` |
| 9 UI reconnect | Empty |
| 10 file import begin | `Event` with basename in value, then `u64 total_bytes` |
| 11 file import chunk | `u64 target, byte_position; u32 count; byte[count]` |
| 12 file import commit | `u64 target` |
| 13 prepare explicit file export | `Event` with desired basename in value |
| 15 clock probe | `u64 nonce; float64 client_epoch_seconds` |
| 16 playback failed | `u64 generation, stream; string reason` (512-byte limit) |

On native builds, types 10–13 are accepted only from `--file-events-fd`. A type-2
successful file chooser reply containing a server path also requires that trusted
pipe and the current opaque service capability. The host must apply its own
authorized filesystem policy before supplying such a path. Ordinary stdin can
cancel/report failure for a file dialog, but cannot grant file access. Audio-only
peripherals get only the narrower audio interface from their embedding host.

Wasm file selection is local to its Worker. Its adapter streams selected file
bytes into a private virtual file and completes the same C++ chooser. Imports
are limited to 256 MiB each, 64 KiB chunks, one active import, 4096 private file
objects and 1 GiB cumulative reserved file bytes per runtime. Actual browser
memory may impose a smaller available capacity. Basenames cannot contain path
separators, colons, control bytes or dot-directory names. Paths never originate
from received audio. Existing controller file-size/format checks still apply.

## Outgoing frames

| Type | Payload |
| --- | --- |
| 101 UI snapshot | Raw UTF-8 JSON from the shared declaration bridge |
| 102 audio event | `u32 kind; u64 generation, stream, position; u32 rate, channels; float64 gain, presentation_epoch; u32 count; float32[count]` |
| 103 operation error | Counted string |
| 104 closed | `u32 application_result` |
| 105 authorized file begins | `u64 target; string basename; u64 total_bytes` |
| 106 authorized file bytes | `u64 target, byte_position; u32 count; byte[count]` |
| 107 file export ends | `u64 target; string error` (empty on complete byte delivery) |
| 108 clock reply | `u64 nonce; float64 client_epoch, server_receive_epoch, server_send_epoch` |

Output queues are bounded to 16 MiB; wholly unsent snapshots can coalesce, audio
and partial frames cannot. Audio/clock messages take priority over an entirely
unsent snapshot. Identical presentations are omitted; their revision advances
only when their contents change, including event acknowledgments and services.
A native output stall exceeding three seconds closes
the worker. File bytes are never included in ordinary snapshots. An accepted
save request first chooses an isolated output object; bytes become exportable
only after the C++ controller reports actual completion of the synchronous or
asynchronous write. A chooser cancelled before authorization starts no export;
write failure or UI reconnect withdraws pending export. There is no separate
postauthorization export-cancel operation. A UI reconnect also withdraws queued
downloads, but cannot revoke bytes already delivered to the host.
The browser describes its final step as “Download offered”; a Blob/download link
cannot prove that the user saved data persistently. The native host saves its
authorized stream to the server, not automatically to the peripheral browser.

## Audio timeline

Audio event kinds are capture start=0, capture stop=1, playback start=2, PCM=3,
playback end=4, playback cancel=5 and stopped=6. Channels are left mono=0, right mono=1,
stereo=2; supplied PCM is always one logical mono stream. Gain is applied at
the browser device boundary, after C++ rate conversion. Host rates are
8000–192000 Hz, packets at most 4096 finite samples. Capture positions must be
exactly consecutive; overflow, gaps, reordered input, suspension and missing
samples fail the stream. No synthetic silence is inserted into reception.

Each capture subscription has a distinct stream ID. The first PCM block establishes
its absolute device-frame origin; later blocks must be contiguous. Retired-stream
blocks already in flight are discarded, never counted as absence or delivered to
a new receiver.

A new nonzero audio generation must exceed the previous one and can be configured
only after capture stops and playback is flushed. Playback starts by waiting for
device readiness before requesting C++ source samples. The first PCM packet has
an absolute intended presentation epoch. Generic C++ scheduling provides delivery
lead time; the browser maps the worker clock to its audio device timeline and
rejects an unachievable start. Clock probes use bounded round-trip offset/uncertainty
measurement; stale mappings and excessive jitter fail instead of silently moving
an encrypted transmission's epoch.

Clock qualification happens before playback readiness. A noisy probe is retried
without stopping input; if no qualified mapping is available within the readiness
deadline, that output fails. Playback failure (type 16) belongs to its stream:
it wakes the C++ producer and follows the normal cancel/flush handshake while
continuous capture remains available. It never acknowledges drain or completion.
Capture discontinuity, device suspension and whole-context failure still interrupt
the audio generation. Capture batches retain the first device-frame position and
every sample; an unstarted input graph is not mistaken for an established-stream
gap, and no samples are invented during startup.

Playback buffers at most a quarter-second ahead of acknowledged consumed frames.
`playback_end` is not completion: the device must acknowledge consumption of the
exact final frame and drain. After cancellation, `playback_cancel` requires an
explicit flushed acknowledgment before another stream or generation can begin.
The application's existing key-use reservations remain in force for samples
already exposed to the audio device. Three-second readiness, capture, consumption
and flush stalls are transport errors, not acoustic absence or message completion.

The same C++ resampler and physical receiver remain in use. Neither framing nor
EOF in this interface changes the acoustic short-message, fixed-interval or
observed-absence completion rules.

A `stopped` audio event (kind 6) acknowledges an explicit interruption only after
capture exits and any playback cancellation is acknowledged as flushed. A new
browser audio configuration waits for that event. Retired-context cancellation
must be acknowledged only after the old output context is closed; it is not an
error to receive the corresponding retired capture-stop event while closing.

Bitmap objects carry bounded dimensions and exactly one base64 payload: `rgb`
contains row-major RGB bytes, or `rgbRuns` contains four-byte records (repeat
count minus one, red, green, blue). Runs span 1–256 pixels and may cross rows;
the decoded pixel count must equal width × height. The encoder selects runs only
when smaller than raw RGB. Snapshots remain independently decodable after relay
coalescing or reconnection; compression changes no image pixels or progress text.
