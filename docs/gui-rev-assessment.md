# Rev backend assessment

Status: selected next backend for proving the GUI abstraction; implementation planned.
Reviewed on 2026-09-12 against
[Rev commit `78ae485135b016247ed3bca21cad28c3ba26d0bb`](https://github.com/ryanpmcguire/Rev/tree/78ae485135b016247ed3bca21cad28c3ba26d0bb).
FLTK remains the default and only implemented backend.

Rev is the chosen next adapter because its code clarity and retained controls,
layout, styling, and graphics make it a useful implementation partner for the
[semantic contract](gui-contract.md). The aim is to prove that small shared screen
declarations, application actions, and bitmap producers work naturally with two
different toolkits. Rev is under active development; the observations below are
integration work for the reviewed revision and should be rechecked as it evolves.
They do not require postponing the abstraction experiment until the toolkit is
feature-complete.

OpenGL is an acceptable implementation detail of the Rev adapter. Software
OpenGL may perform adequately for this instrument-panel workload; measure that
before drawing a conclusion from its use of graphics contexts. The development
direction supplied for this review includes a future direct software renderer
if needed. That renderer is planned, and neither path has demonstrated performance
for DataPump yet. Keep rendering choices private to Rev so a later renderer can
use the same semantic controls and bitmap interface.

## Fit with the application

The shared semantic interface is still a draft. `src/gui/main.cpp` reads FLTK
widgets for settings, message contents, selection, and action eligibility, and
owns preparation workers, dialogs, and smoke orchestration. Start the existing
[controller and plot extraction plan](gui-architecture.md) with a small console
slice, implement that same slice in Rev, and expand both together. This gives
early evidence about the abstraction without first completing every screen or
every upstream control. Share each slice's behavior before adapting it to Rev.

Keep `live::Session`, GUI policy/state, inspection models, plot data, and theme
roles shared. Rev types, observables, style objects, and graphics contexts belong
inside its adapter. Applying controller state must suppress application events
and retain editor focus, selection, cursor, and scroll. Rev's own charts should
not replace the planned shared waveform, waterfall, and constellation producers.

| Contract area | Reviewed Rev implementation | Work needed |
| --- | --- | --- |
| Label / Action / Toggle | `Text`, `Button`, `Checkbox`, and `Radio` elements exist. | Bind named actions/values and apply the shared instrument-panel theme. |
| Choice | `Dropdown` items have separate names/values and support silent programmatic selection. | Map stable option IDs, reject duplicates, and handle removed selections consistently. |
| Row / Column / Scroll | Elements and boxes support layout and scrolling through styles. | Map semantic layout, visibility, focus, and sizing behavior. |
| Text | `TextInput` exists, but rejects carriage returns and newlines and defaults to a 500-byte limit. | Provide multiline message/binary editing, application limits, and verified UTF-8 caret/selection/deletion behavior. |
| List / Tabs | No dedicated List or Tabs control was found in the reviewed element tree. | Compose existing controls with stable record/page identity and keyboard behavior. |
| Bitmap | OpenGL `Texture` uploads 1-, 3-, or 4-channel bytes with nearest filtering available. | Add an output-only image element and rectangle updates; handle padded stride, Mono1 expansion, opaque Gray8, borrowed storage, and actual drawable-pixel sizing. |

Sources: [elements](https://github.com/ryanpmcguire/Rev/tree/78ae485135b016247ed3bca21cad28c3ba26d0bb/source/Elements),
[TextInput validation](https://github.com/ryanpmcguire/Rev/blob/78ae485135b016247ed3bca21cad28c3ba26d0bb/source/Elements/Controls/TextInput/TextInput.ixx#L145-L150),
[base text editing](https://github.com/ryanpmcguire/Rev/blob/78ae485135b016247ed3bca21cad28c3ba26d0bb/source/Elements/Text.ixx),
and [OpenGL texture implementation](https://github.com/ryanpmcguire/Rev/blob/78ae485135b016247ed3bca21cad28c3ba26d0bb/source/Graphics/OpenGL/Texture.OpenGL.ixx).
These are source-review findings, not demonstrated adapter behavior. The
[text renderer](https://github.com/ryanpmcguire/Rev/blob/78ae485135b016247ed3bca21cad28c3ba26d0bb/source/Graphics/Primitives/Text/Text.ixx#L157-L160)
explicitly skips bytes at or above 128, and base editing moves/deletes by byte.
UTF-8 display and editing therefore need implementation changes, not just
validation. The Gray8 texture swizzle also maps intensity to alpha; an adapter
must ensure opaque replacement so grayscale values retain their meaning.

Keyboard and disabled behavior also need work. Rev's
[event documentation](https://github.com/ryanpmcguire/Rev/blob/78ae485135b016247ed3bca21cad28c3ba26d0bb/source/Element/About/Element.Events.md)
does not promise Tab traversal. `Button` invokes `click` on any keydown, and the
base `setDisabled` only changes styling; individual controls enforce input gates
inconsistently. Implement declaration-order focus traversal, filter action
activation to the intended keys, and enforce unavailable controls behaviorally.
See [Button](https://github.com/ryanpmcguire/Rev/blob/78ae485135b016247ed3bca21cad28c3ba26d0bb/source/Elements/Controls/Button/Button.ixx#L64)
and [Element](https://github.com/ryanpmcguire/Rev/blob/78ae485135b016247ed3bca21cad28c3ba26d0bb/source/Element/Element.ixx#L1640-L1647).

## Integration work

- **Rendering profile.** Upstream selects OpenGL on Linux/Windows and Metal on
  macOS. Linux uses X11/GLX and explicitly checks for OpenGL 4.4 or
  `GL_ARB_buffer_storage`. CMake requires OpenGL, FreeType, GLEW, X11, Xrandr, and
  Xext on Linux. Test the software OpenGL path as well as hardware rendering;
  record the actual renderer and supported capabilities in each measurement.
  Validate virtual-display operation and failure when a suitable context is
  unavailable. These are deployment and measurement tasks, not a requirement
  that the shared GUI use GPU acceleration. See [CMake](https://github.com/ryanpmcguire/Rev/blob/78ae485135b016247ed3bca21cad28c3ba26d0bb/CMakeLists.txt)
  and [Linux capability check](https://github.com/ryanpmcguire/Rev/blob/78ae485135b016247ed3bca21cad28c3ba26d0bb/source/Native/Linux/NativeWindow.lnx.ixx#L502-L506).
- **Build integration.** Rev requests C++23 and C++ modules, while DataPump uses
  C++20. Establish a tested compiler/CMake/generator combination for the Rev
  targets without raising the core or FLTK requirements. Upstream CMake globs
  `src` and `Resources`, but this revision contains `source` and `resources`.
  Its resource script writes under `Rev/Resources/.modules` and takes the
  top-level `CMAKE_SOURCE_DIR`, so a plain `add_subdirectory` is insufficient.
  Fix source discovery and use declared resource outputs in the build tree.
  See [resource script](https://github.com/ryanpmcguire/Rev/blob/78ae485135b016247ed3bca21cad28c3ba26d0bb/scripts/Create_Resource_Modules.py).
- **Relocation and services.** `Resource::FromFile` resolves resources relative
  to source paths or `PROJECT_ROOT`; package fonts/shaders or embed them through
  a verified build path. Linux file dialogs invoke `zenity` and return failure
  when it is absent. Provide platform requests with later result events and an
  explicit dependency/fallback policy. Do not assume a native-file-chooser API
  makes the portable package self-contained. See [resources](https://github.com/ryanpmcguire/Rev/blob/78ae485135b016247ed3bca21cad28c3ba26d0bb/source/Core/Resource.ixx)
  and [Linux file dialogs](https://github.com/ryanpmcguire/Rev/blob/78ae485135b016247ed3bca21cad28c3ba26d0bb/source/OS/Linux/File.lnx.ixx#L466-L490).
- **Dependency provenance.** No project-level license file was found in the
  reviewed checkout. Resolve the project's redistribution terms and record
  dependency/font notices before vendoring. Windows CMake links checked-in x64
  FreeType/GLEW libraries; establish their build provenance and compatibility
  with DataPump's static MSVC runtime policy, preferably rebuilding from pinned
  source. Preserve the existing FLTK package's GTK/GLib audit; any different
  dependency policy belongs to the Rev profile.

## Bounded implementation sequence

1. Establish a reproducible Rev build from a pinned revision and resolve
   redistribution terms before vendoring. In parallel, extract a small FLTK
   console slice with authoritative state, a text field, choice, action, and a
   shared bitmap producer. Preserve existing behavior and tests.
2. Implement that same slice in `backend_rev.cpp` with `cmake/GuiRev.cmake`.
   Demonstrate that moving or adding an existing kind of control changes only
   the declaration and any application binding, with no adapter edits. Exercise
   silent updates, stable identity, and bitmap repaint on both toolkits early.
   Add the `rev` build selection only when a runnable adapter exists and label
   partial workflow coverage explicitly. Keep one adapter per build directory,
   Rev imports out of shared headers, and FLTK/CLI-only dependencies unchanged.
3. Expand the controller, preparation, platform requests, declarations, and pure
   pixel producers across the remaining workflows. Resolve the reviewed control
   gaps as those slices need them, preferably in Rev's reusable controls rather
   than as application-specific editor implementations. Measure software OpenGL
   on representative workloads and verify resource relocation on Linux/Windows.
4. Exercise both adapters against the same workflow: exact bits and inactive
   editors, invalid draft settings, stable selections, silent updates, stale
   preparation rejection, retained save payloads, reception during dialogs/page
   changes, clipboard, color/monochrome, and QR brightness. Verify complete versus
   tiled pixel output and packaged self-check/relocation on each supported OS.

Full support requires the console, inspection, plotting, input, and file
workflows. Freeze the first contract after FLTK and Rev demonstrate the same
semantics. Ncurses is a later portability exercise for the SSH profile.

## Rendering measurements

Compare FLTK and Rev with the same window dimensions, display scale, plot data,
history size, and update cadence. For Rev, record whether OpenGL uses software
or hardware rendering. Measure idle and active CPU use, memory, input latency,
plot frame times, and receiver-event delivery while editing, resizing, switching
pages, and advancing the waterfall. Hidden plots should avoid unnecessary work;
slower rendering must not delay reception events or restart the receiver.

Use those results to decide whether software OpenGL is adequate and where
optimization is useful. If Rev introduces a direct software renderer, repeat
the same workload and semantic/bitmap checks through the existing adapter
boundary. Shared screens and application logic should require no renderer
branches, and shared plot output must retain its measured scales and pixel
semantics. Renderer efficiency remains an empirical question throughout.

## Checks performed

The recursive clone completed in a temporary review directory. At this revision,
`git submodule status --recursive` was empty and no `.gitmodules` file was tracked.
The source findings above use the clone, which is newer than the cached GitHub
landing page seen during this review.

An unmodified configure attempt with CMake 3.31.6, Ninja 1.12.1, and GCC 14.2.0:

```sh
cmake -S /tmp/pumpmodem-rev-review -B /tmp/pumpmodem-rev-build \
  -G Ninja -DCMAKE_BUILD_TYPE=Release
```

stopped at `find_package(OpenGL REQUIRED)` because this environment lacks the
OpenGL development libraries/headers. No Rev compilation or runtime check was
completed. The source/resource path mismatches were found by inspection, not by
progressing past that dependency failure. Rev has not been vendored and the
application's build selections have not changed.
