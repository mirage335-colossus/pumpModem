# Proposed minimal GUI contract

Status: design draft. These declarations, backends, and switches are proposed;
the application currently uses FLTK. This document defines control behavior
before choosing any drawing or framebuffer interface.

The maintenance rule is: adding or moving an ordinary control changes its
declaration and, for new functionality, its state/action mapping. It does not
require reading or editing a backend. Keep the vocabulary small and keep its
meaning consistent across desktop and compact layouts.

## Declaration vocabulary

Use compiled, typed C++ declarations with stable helper signatures. No runtime
markup parser, generic property dictionary, or toolkit-specific properties are
needed. One short screen declaration should show its hierarchy and bindings.

| Kind | Meaning |
| --- | --- |
| Label | Read-only text. |
| Action | Invoke a named application operation. Usually a button. |
| Toggle | Edit a boolean value. |
| Choice | Select one stable option ID from a closed set. |
| Text | Edit UTF-8 text; single/multiline and optional preset suggestions. |
| List | Select a record by stable ID; activation is a separate action. |
| View | Reserve space for a named plot or diagram. Rendering is separate. |
| Row / Column | Lay out children in declaration order. |
| Tabs | Display one named page; pages have IDs and titles. |
| Scroll | Provide vertical scrolling for one child. |

An illustrative declaration, not an implemented API:

```cpp
page(Page::console, "Console",
  column(
    row(
      choice(Id::source, "Source", State::source, Options::sources),
      text(Id::bandwidth, "Bandwidth", State::bandwidth,
           suggestions(Options::bandwidths))),
    text(Id::message, "Message", State::message, multiline(6)),
    row(
      action(Id::transmit, "Transmit", Command::transmit),
      action(Id::cancel, "Cancel TX", Command::cancel)),
    view(Id::waveform, ViewModel::waveform, grow(1))))
```

Reordering children moves controls. Bindings identify application values and
commands; labels are literal text. A keyfile command menu is a presentation of
Actions, not a Choice whose selected value unexpectedly executes a command.

## Behavior that every adapter preserves

1. **Stable identity.** Controls, options, pages, and list records have explicit
   IDs. Labels, positions, and array indices never identify application values.
   Duplicate IDs in their declared scope are errors. Literal labels must not be
   interpreted as toolkit menu paths, accelerators, or formatting syntax.
2. **State and input.** Application state is authoritative. Applying state never
   emits an input event. User events are delivered serially to the controller:
   Activate, Toggle, Select, Edit, Submit, and SelectPage. Explicitly selecting
   the already-selected choice still emits Select; the current keyfile failure
   acknowledgement needs this. The controller decides whether it has an effect.
3. **Collections.** Replacing options or records preserves selection by ID when
   possible; otherwise selection is empty until the controller chooses a value.
   No implicit selection of the first item. Disabled items cannot be newly
   selected by user input; an existing selection or controller update is allowed.
4. **Text.** Choice stays closed; editable presets belong to Text. A suggestion
   replaces the field's text and emits Edit only if the text changed. Text
   declares a UTF-8 byte limit;
   over-limit edits are rejected with feedback, never silently truncated. Edit
   reports text changes; Submit is explicit. Multiline Enter inserts a newline
   unless a declared submit binding matches it. The composer declares its
   Enter/Ctrl+Enter policy and reserves Shift+Enter for a newline. Validation and
   numeric/unit parsing live in shared application code.
5. **Layout.** Row/Column use ordered children, minimum sizes, standard gaps, and
   integer stretch weights. Backend font measurements inform minimum sizes.
   Declaration order also defines focus traversal. Hidden children occupy no
   space; disabled children retain space and produce no input actions. Tiny
   displays use an explicit compact layout; controls are not scaled below usable
   sizes. The application does not specify pixel coordinates for ordinary controls.
6. **Ownership.** Static declarations live for the UI lifetime. Dynamic update
   data is borrowed only during the update call; adapters copy anything retained.
   Event data remains valid for its dispatch call; the controller copies anything
   retained. Apply changes without recreating unchanged controls or resetting an
   editor's focus, cursor, selection, or scroll position. A changed text value may
   clamp positions to valid boundaries. Toolkit objects stay inside adapters.
7. **Capabilities.** Backend availability and profile requirements are checked
   before startup. An unsupported operation needs a declared alternative or a
   visibly unavailable action. Required controls cannot silently disappear.

Optional platform services such as file pickers and clipboard operations are
requested by application commands, outside the declaration vocabulary. Custom
View rendering must not become an escape hatch for implementing ordinary controls
that bypass this contract.

## Backends and command-line selection

Candidate adapters are FLTK, wxWidgets, LVGL, and a small widget implementation
hosted by SDL or an MCU display library. SDL is the platform/rendering layer of
its adapter; the widget implementation must also be specified. wxWidgets can map
the declarations to its [platform controls](https://wxwidgets.org/about/).
Arduino-compatible drawing libraries can host a compact adapter, but drawing
primitives alone do not supply control behavior.

Proposed initial GUI switches:

```text
datapump-gui --list-gui-backends
datapump-gui --gui-backend=wx --gui-layout=desktop
datapump-gui --gui-backend=sdl --gui-layout=compact
```

Only compiled adapters appear in the list. An unavailable explicit backend or
unsupported layout fails with a clear error and available choices. Omitting the
backend selects the build's documented default. Parse help and selection before
initializing any GUI library. Small builds can include exactly one adapter;
desktop builds can offer several through the same switches. Runtime plugin
loading is unnecessary. MCU firmware can select the same defaults at build time
when there is no command line.

The initial supported rendering configuration should avoid application GL/EGL
contexts. For the SDL window-surface path, disable
[framebuffer acceleration](https://wiki.libsdl.org/SDL3/SDL_HINT_FRAMEBUFFER_ACCELERATION)
before acquiring the surface. The operating system's compositor/display path
remains a separate deployment concern.

GUI launch settings should reuse the same typed setting definitions, defaults,
and validators as the controls. Command-line initialization updates shared state;
it must not simulate clicks or implicitly activate Transmit. Introduce individual
setting switches when their shared bindings exist.

## First implementation boundary

Extract the existing FLTK controller and declarations first; retain current
behavior. Then implement a second adapter and confirm that moving/adding a
control touches no adapter code. Test stable selection after reorder/removal,
literal labels, silent state updates, repeated selection, text submission,
visibility, and unavailable backend handling. Freeze the first contract only
after both adapters demonstrate the same semantics.

Keep the declaration vocabulary and one complete example together in a short
public header. Separate the screen declarations, application action/state
mapping, and backend internals. A routine GUI edit should need the first two
plus that header; graphics implementation details should not enter its context.
