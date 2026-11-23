# MyEngine — a C++26 rewrite of Qt

**Version 0.1** (first public release)

Qt's architecture rebuilt from the ground up on C++26 static reflection (P2996). There is no `moc`, no
`Q_OBJECT` pre-processing step and no string-based `SIGNAL`/`SLOT` connection syntax: member functions are
reflected at compile time, a perfect-hash table is generated for each class, and a member can then be
invoked by name at run time. Signals, slots, properties and the object tree are all built on that table.

## Status

Version 0.1: the core architecture is in place and covered by **83 test targets** — but treat the API as
unstable, and expect breakage between 0.x releases.

The project is organised in four layers, each its own library with its own export macros. A layer only
depends on the ones to its left:

| Layer | Depends on | What it provides |
|---|---|---|
| `metaobject` | — | Object tree, signals/slots, properties, reflected meta-info, event loop and timers, `any_t` |
| `gui` | `metaobject` | Windows, painting, fonts, images, input events |
| `ui` | `gui` | Items, widgets, layouts, controls, item views, input router, immediate-mode host |
| `audio` | `metaobject` | Output devices, push-mode streams, sound-file decoding |

### What works today

**`metaobject`** — parent/child object tree with automatic destruction; signal/slot connections resolved
through the reflected table; three connection kinds (`connect` to a slot, to a typed lambda, to an
erased variadic sink); `Direct` / `Queued` / `BlockingQueued` / `Auto` dispatch across threads;
reflected properties with notifiers; plug-in loading (shared builds only); `event_t` hierarchy, filters,
event loop and timers.

**`gui`** — `window_t` over a platform seam with an SDL3 backend and an off-screen backend (same
`paint_device_t`, so a frame can be rendered without a display); `painter_t` / `paint_engine_t` split so
a backend only implements primitives; `region_t` / `transform_t` geometry; glyph-cached text with a font
database behind a replaceable font-engine seam (built-in bitmap face and a Win32 engine); `image_t` with
BMP, PNG and JPEG loading behind a replaceable decoder; UTF-8 text layout.

**`ui`** — `item_t` (tree node: geometry, visibility, constraints) and `widget_t` (renderable) split;
box and grid layouts; a full set of controls (`push_button_t`, `check_box_t`, `radio_button_t`,
`slider_t`, `line_edit_t`, `progress_bar_t`, `label_t`, `group_box_t`, `tab_widget_t`, `scroll_bar_t`,
`scroll_view_t`) and model-driven views (`list_view_t`, `table_view_t`, `tree_view_t`); `input_router_t`
for focus and pointer routing; and an immediate-mode host (`im_host_t`) that runs a code-driven UI *inside*
the retained tree, so both styles compose in one window.

**`audio`** — device enumeration, push-mode output streams that mix on one device (a one-shot effect
overlaps a looping track), WAV decoding through SDL and MP3 through the vendored minimp3, behind the same
replaceable-decoder seam as images.

### Not there yet

Drag and drop, accessibility, high-DPI scaling policy, text shaping (bidi/complex scripts), recording,
audio pull mode, and a real OpenGL/Vulkan paint backend. `widget_t` is a tree node, not a native handle:
there are no per-widget OS windows.

## Building

The project is developed and tested in exactly one environment. Other platforms are untested as of 0.1.

| Component | Version used |
|---|---|
| OS | Windows 11 x64 |
| Environment | MSYS2, `UCRT64` (target `x86_64-w64-mingw32`) |
| Compiler | **GCC 16.2.0** (MSYS2 build), `-std=c++26 -freflection` |
| Build system | CMake 4.4.4 + Ninja 1.13.2 |
| Build type | `Release` |

The hard requirement is C++26 reflection: a stock MSYS2 GCC build does not have `-freflection`, so you
need a GCC 16.2 (or later) built with reflection enabled. CMake and Ninja come from the same UCRT64
prefix, so nothing else has to be on `PATH`.

From an **MSYS2 UCRT64** shell:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja

git clone --recursive <this repository>
cd MyEngine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build --output-on-failure
```

`external/SDL3` and `external/minimp3` are git submodules, so the clone must be recursive (or follow up
with `git submodule update --init --recursive`).

## Examples

| Example | Shows |
|---|---|
| `examples/hello_gui` | The smallest window, end to end |
| `examples/demo` | Core layer only: object tree, connections, properties, timers |
| `examples/ui_demo` | The whole ui layer: retained tree, immediate-mode host, controls, views |
| `examples/audio_demo` | Two output streams: a looping track and an overlapping one-shot click |
| `examples/plugin` | A reflected plug-in (shared builds only) |

`ui_demo` also runs headless, which is how the rendering is regression-tested:

```sh
ui_demo --self-test              # drives input through the real event loop
ui_demo --screenshot shot.bmp    # renders exactly one deterministic frame
```

## Third-party

| Component | What it is | Licence |
|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | Platform layer: windows, input, audio device I/O | zlib |
| [minimp3](https://github.com/lieff/minimp3) | Header-only MP3 decoder | CC0 |
| *The Ice Giants* | Music in `examples/audio_demo`, by **Kevin MacLeod** (incompetech.com) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| `sfx_click.wav` | A 140 ms percussive slice cut from that track | CC BY 4.0 (derived) |

Attribution: *"The Ice Giants" Kevin MacLeod (incompetech.com), licensed under Creative Commons: By
Attribution 4.0 License.* The two audio files are redistributed under that licence and are **not**
covered by this project's own licence.

Qt itself is referenced only as a design target; no Qt code is included.

## Licence

**Boost Software License 1.0** — see [LICENSE](LICENSE) for the full text, or
<https://www.boost.org/LICENSE_1_0.txt>. Permissive, no copyleft, no attribution required in binary
form: keep the licence text with any redistribution of the source.
