# Local browser presentation adapters

These assets create no network transport. The C++ facade supplies every control,
page, document action, record and bitmap. `renderer.mjs` knows only presentation
primitives and platform services; DSP, message formats, receive policy, save
eligibility and command enablement remain in C++.

`protocol.mjs` implements version-1 DPW1 framing. The embedding application must
supply a bounded local callback and preserve frame order. A resolved callback
means that the receiver accepted ownership of that block, not just that another
unbounded queue retained it. Never route file frames through the ordinary UI or
audio channel. The native worker requires its separate trusted pipe for file
capabilities. Read the native protocol and sandbox documentation before launch.

## Static client

The build packages `wasm_client.mjs`, `wasm_worker.js`, the generic modules,
`audio_worklet.js`, styles, generated C++ factory and Wasm into a self-contained
HTML document. No external assets or runtime download are needed. The loader
calls `boot({root, factorySource, wasmBytes, workletSource, workerSource})` with
already loaded bytes. Generated C++ runs in one ordinary Worker. Every mutating
C++ export is serialized across Asyncify suspension; output bytes are copied
before consumption. Both directions have bounded message queues.

The normal **Enable microphone and audio** button requests microphone permission
and resumes Web Audio from that user gesture. Ordinary text/file UI works before
audio is enabled. Audio uses browser PCM, a local AudioWorklet and the C++
resampler. No speech codec or MediaRecorder is involved. Echo cancellation,
automatic gain and noise suppression are requested off; actual settings and
physical fidelity still depend on the browser/device. Foreground operation is
required. Suspension, missing capture, playback underrun or withdrawn permission
interrupts the audio session and cannot finish reception.

Playback requires a measured server/browser clock mapping. Round trips above
100 ms, stale mappings and clock changes fail audio timing qualification. The
worklet emits the first sample at the scheduled context frame and rejects a late
start or inadequate prebuffer. Completion waits for the reported output timestamp
where available, otherwise an explicitly estimated output latency criterion.
This is not a physical-device timing qualification.

Files selected with the ordinary browser picker stay local. Imports and explicit
exports have a 256 MiB bound, matching the pipe interface and admitting normal
128 MiB keyfiles. Browser memory limits may reject a large operation; low-memory
phones are not qualified by this bound. Export buffers, Wasm memory and a Blob can
coexist. A successful Blob download is described as **Download offered**, because
the browser does not prove persistent storage completion. Files/keys are not
persisted automatically across page closure.

## Hosted renderer and browser peripheral

`hosted.mjs` exports `createHostedFrontend`:

```javascript
const view = createHostedFrontend({
  container,
  rendererSource, protocolSource, styleText, workletSource, // already loaded
  send: acceptedOrderedApplicationFrame,
  handleService: authorizedServerFileService,
  status: presentHostAudioStatus
});
await view.ready;
audioButton.onclick = () => view.enableAudio();
// Feed complete, authenticated C++ output frames for this one application:
view.feed(frameBytes);
// On closing the containing application:
view.dispose();
```

The host supplies its own `send` implementation and existing web transport. No
HTTP server, WebSocket, network listener or relay helper is supplied here. In the
native worker case the host launches a separately sandboxed C++ process with real
anonymous pipes. File selection means the host's authorized server files; it is
not implemented by a browser upload widget. The host must validate paths/handles
within its existing filesystem policy, including symlinks and concurrent changes.

For a local experiment, [COMPILE-web](../COMPILE-web) uses the repository-only
`python3 tools/preview.py worker` launcher. Its trusted parent page owns the
loopback relay, child lifetime and optional `--files DIR` policy; these are not
part of the renderer, worker or installed browser assets. The same helper's
`wasm` mode simply serves the compiled static page.

The renderer is an opaque `sandbox="allow-scripts"` frame with
`connect-src 'none'`, no parent DOM access, no forms, no popup or navigation
permissions. Preloaded code is installed inside that frame; received text is
always rendered literally. A random nonce and exact `Window` identity establish
one transferred MessagePort; the parent's `null`-origin check alone would be
insufficient. Parent input accepts only viewport/event frames; the renderer has
no authority to submit audio, file-transfer frames or pipe-management commands.

Audio capture/playback belongs to the trusted containing page, because the opaque
frame cannot acquire microphone permission. The host's HTTPS origin must satisfy
browser security requirements. Its browser audio adapter still creates no
network transport; the host relays typed audio frames to the C++ pipe itself.
An audio-only peripheral can use `BrowserAudio` directly with its narrow audio
callback, without creating a renderer or receiving control/file authority.

`handleService({request,event}, signal)` is invoked only for the current service
published by C++. The adapter checks the opaque identifier, protocol generation
and event primitive against that declaration before exposing the callback. The
host chooses an authorized server object and supplies the corresponding trusted
file stream. Return `{handled:true}` once the trusted service invocation is
accepted, `{cancelled:true}` on cancellation or `{error:"..."}` on a failed
choice. The callback must honor cancellation and never use received filenames or
UI values as unconstrained host paths or commands. Asynchronous output completion
and exact bytes belong to the host's separate file service, never to the iframe.

## Verification scope

The Node suite covers generic DOM and IME behavior, late-editor withdrawal,
framing bounds, scheduled PCM, underrun and reordering. The C++ bridge suite
covers facade declaration extension, version/sequence/generation checks, literal
text, overlay input and service cancellation. These fixtures do not establish
Chromium/Firefox/WebKit rendering, microphone fidelity, real-device latency,
mobile memory capacity or GitHub Pages browser behavior. Those require browser
and device qualification in addition to the preserved native suites.
