# ModernUI

**A Retained-Mode 2D Composition Framework and Reference Applications for Windows Classroom Interaction**

---

## Abstract

ModernUI is a compact C++ framework for building per-pixel-transparent, desktop-wide overlay
applications on Windows. It combines a retained-mode scene graph with tile-based dirty-region
management, a widget composition model in which every control owns an isolated child canvas,
Flexbox layout, a CSS-compatible styling layer, a tween/keyframe/spring animation subsystem, a
Windows 11 Fluent Design control library, and a Blend2D rendering backend hosted in a layered
Win32 window. Two reference applications demonstrate the framework in realistic instructional
settings: *Annotation*, a full-screen ink overlay with asynchronous OCR, AI-generated teaching
cards, and PowerPoint/WPS presentation awareness; and *CameraPro*, a Media Foundation-based
document camera with ink annotation, image post-processing, snapshot capture, and OCR. The
repository is intended both as an engineering baseline and as a teaching artifact for courses on
interactive systems, computer graphics, and Windows desktop architecture.

**Keywords:** retained-mode UI, dirty-region rendering, per-pixel alpha, layered window, ink
rendering, optical character recognition, Windows desktop.

---

## 1. Introduction

Classroom and presentation workflows impose requirements that conventional widget toolkits
handle poorly: the interface must float above arbitrary third-party applications, remain
transparent outside its own content, capture pointer input without stealing focus or blocking
the underlying application, and sustain low-latency freehand drawing at display refresh rates.
ModernUI addresses these requirements with a deliberately small, fully source-available stack
written in C++17 (applications use C++20) and targeted at the MSYS2 UCRT64 toolchain
(`x86_64-w64-mingw32`).

The framework is organised as a set of independent static libraries with explicit dependency
boundaries. Rendering is delegated to Blend2D, input is abstracted behind a backend-agnostic
event queue, and the scene graph is completely decoupled from the renderer. Applications consume
only the public headers of the modules they require, and each application in this repository is a
standalone CMake project that reuses the framework sources without modifying them.

---

## 2. System Architecture

```
┌───────────────────────────────────────────────────────────────────────────┐
│  Applications                                                             │
│  Annotation (overlay, ink, OCR, AI cards, Office bridge)                  │
│  CameraPro  (document camera, ink, post-processing, snapshot, OCR)        │
│  examples/  (blend2d_demo, ui_demo, fluent_demo, style_demo)              │
├───────────────────────────────────────────────────────────────────────────┤
│  src/app             App facade: window + backend + view + main loop      │
├───────────────────────────────────────────────────────────────────────────┤
│  src/std_widgets     Windows 11 Fluent Design control library             │
│  src/ui              View / Widget composition, Flexbox, .ui loader       │
│  src/style           StyleSheet, CSS parser, selector matching            │
│  src/animation       Tween, keyframes, spring, sprite animation           │
├───────────────────────────────────────────────────────────────────────────┤
│  src/canvas          Retained-mode scene graph + dirty-region manager     │
├───────────────────────────────────────────────────────────────────────────┤
│  src/backend_blend2d Blend2D renderer + Win32 layered window (PRGB32)     │
│  src/input           Global low-level mouse hook (dedicated thread)       │
│  src/event           Backend-agnostic event queue                         │
├───────────────────────────────────────────────────────────────────────────┤
│  3rd/                Blend2D (vendored), RapidOCRLite (vendored), nanosvg │
└───────────────────────────────────────────────────────────────────────────┘
```

### 2.1 Canvas and scene graph

`src/canvas` implements a retained-mode canvas. The scene graph is stored as a
`std::variant`-based shape union with recursive groups; built-in primitives include rectangles,
rounded rectangles, circles, ellipses, arcs, pies, rings, stars, polygons, lines, polylines,
paths, text, images, SVG, and blur. Shapes are addressed by identifier, and every mutation
marks the affected geometry dirty. Groups carry an absolute clip rectangle and a scroll offset,
so clipping and scrolling are scene-graph properties rather than transient drawing state.
Text measurement is delegated to the backend through a callback, and measured bounds are cached
by the canvas. Angular conventions (degrees, clockwise positive, 0° along +X) are documented in
the header.

### 2.2 Dirty-region management

Dirty rectangles are collected, aligned to a 32 × 32 pixel tile grid, deduplicated, and adjacent
tiles are merged back into larger rectangles. The scene graph is flattened once per frame, after
which each dirty region tests only the shapes that intersect it. This avoids the quadratic
behaviour of repeated full traversals and keeps partial updates proportional to the modified
area rather than the total scene size. The backend receives a `CanvasInfo` description and a list
of dirty entries, and may render either the complete frame or individual tiles.

### 2.3 Widget composition and layout

`src/ui` defines the `Widget` and `View` abstractions. Each widget owns a private child canvas in
local coordinates whose origin is the widget's top-left corner. A widget never writes geometry
into its parent's canvas and never contacts the renderer directly; composition is performed by
the `View`, which copies only those child canvases that have been marked dirty. This design
isolates invalidation, simplifies clipping, and allows controls to be implemented without
knowledge of their absolute screen position.

Layout follows a practical subset of the CSS Flexible Box specification (`Flexbox`), with
intrinsic sizing, grow/shrink factors, gaps, padding, alignment, and wrapping. The `.ui` loader
translates an XML-style layout description into a widget tree and binds named callbacks, while
`StyleEngine` applies CSS selectors and state pseudo-classes to the tree.

### 2.4 Style system

`src/style` provides a per-widget `StyleSheet` with typed entries (background, foreground,
border, radius, font size, dimensions, and so on) plus an open-ended map for custom properties
such as `badge-color` or `icon-size`. Values may reference variables (`var(--name)` with
fallbacks) or other entries (`{entry-name}`), resolved lazily. Each value records its origin
(default, stylesheet, or inline), allowing controls to distinguish explicit CSS overrides from
compiled-in defaults. The CSS parser supports type/id/class selectors, descendant matching,
state pseudo-classes, variables, and standard declarations; unrecognised properties are retained
for widget-specific interpretation.

### 2.5 Animation

`src/animation` supplies interpolation for scalars, colors, and 2D vectors; `Tween<T>` for
delayed, looping, and ping-pong transitions with easing; `KeyframeTrack` for multi-segment
timelines with per-segment easing; `SpringValue` for physically damped motion suited to drag
rebound and magnetic snapping; and `SpriteAnimation` for atlas-based frame playback. All times
are expressed in seconds and are advanced by the view's `tick(dt)` call.

### 2.6 Rendering backend

`src/backend_blend2d` hosts a `WS_EX_LAYERED | WS_POPUP` window backed by a top-down 32-bit DIB
surface in premultiplied BGRA (`PRGB32`). Blend2D draws directly into that surface, and
`UpdateLayeredWindow` presents it with per-pixel alpha (`AC_SRC_ALPHA`). The window owns message
pumping, resizing, per-monitor-V2 DPI handling, and translation of Win32 messages into the
backend-agnostic event queue. Surface reallocation increments a serial number so the renderer can
rebind its `BLImage`. SVG shapes are parsed and cached by the backend (nanosvg); text metrics are
provided through the canvas callback; blur shapes are sampled, convolved, and cached entirely
inside the renderer.

### 2.7 Input and eventing

`src/event` defines an event queue and a renderer-independent event vocabulary covering resize,
move, mouse, wheel, keyboard, character, focus, DPI, and hotkey events. The window thread pushes
events; the application main loop polls them.

`src/input` provides an optional global input source. A dedicated thread installs a `WH_MOUSE_LL`
hook and runs its own message loop. The hook callback performs only an atomic policy read, a
lock-free single-producer/single-consumer enqueue, and an immediate return; it performs no
allocation, locking, or drawing, which avoids the `LowLevelHooksTimeout` mechanism that silently
removes slow hooks. Mouse movement is always passed through; button events can be selectively
consumed by the application. Screen coordinates are converted to client coordinates by the
application facade.

### 2.8 Application facade

`src/app` exposes a chainable `App` interface that binds window creation, renderer
initialisation, view construction, style loading, hotkey registration, and the main loop. Options
include virtual-desktop fullscreen coverage (all monitors, including negative coordinates),
click-through, no-activate, tool-window behaviour, frame interval, global hotkeys, and custom
fonts. Lower-level objects (`window()`, `renderer()`, `view()`, `events()`, `mouseHook()`) remain
accessible when an application needs direct control.

---

## 3. Reference Applications

### 3.1 Annotation

Annotation is a full-virtual-desktop overlay with a floating tool column. It exercises the
framework's hook input path, per-pixel transparency, and high-frequency ink updates.

**Tools and capabilities**

| Area | Description |
| --- | --- |
| Ink engine | Exponential smoothing, Catmull-Rom/cubic Bézier resampling, velocity-derived simulated pressure, variable-width contours with entry/exit taper. |
| Tools | Pen, highlighter, pixel eraser (`DST_OUT`), laser pointer, pointer passthrough, region screenshot, OCR, AI cards. |
| Pages and history | One rasterised bitmap per annotation page; undo/redo restores bitmap snapshots bounded by each stroke's bounding box, so erased content is preserved consistently. |
| OCR | Asynchronous PP-OCRv6 detection/recognition through RapidOCRLite; results are mapped back to overlay client coordinates and can be copied to the clipboard. |
| AI teaching cards | DeepSeek chat-completions client (WinHTTP worker thread) requesting an XML card document; cards are rendered on the scene graph, draggable, expandable, scrollable, and closable. Card icons are selected from the shared icon set. |
| Presentation awareness | COM automation attaches to a running PowerPoint or WPS Presentation instance, polls the slideshow slide index and slide count on an MTA worker thread, and uses the page number for annotation pagination. |
| Screenshot | Captures the screen region beneath the overlay, composites the annotation bitmap, and exports to the clipboard and/or a PNG file. |
| Layout | Right-hand floating toolbar: collapsed 68-pixel icon column; expanded property sidebar with colour, width, eraser size, UI scale, and ink parameters; clicking an already-selected tool toggles its property panel. |

**Hotkeys**

| Hotkey | Action |
| --- | --- |
| `F9` | Show or hide the annotation layer |
| `F10` | Toggle pointer-passthrough mode |
| `Ctrl+Z` | Undo |
| `Ctrl+PageUp` / `Ctrl+PageDown` | Previous / next annotation page |
| `Ctrl+Shift+O` | Copy OCR result |
| `Ctrl+Shift+Q` | Quit |

### 3.2 CameraPro

CameraPro is a presentation-oriented document camera. All visual content is placed on a fixed
1920 × 1080 logical stage; the camera image is contained proportionally, and ink is drawn in
stage coordinates, so strokes remain aligned with the image under any view zoom or pan.

**Capture and processing pipeline**

| Stage | Description |
| --- | --- |
| Capture | Media Foundation source reader on a dedicated worker thread; format negotiation and pixel conversion happen off the UI thread. Output is top-down BGRA; only the latest frame is retained, so latency does not accumulate. |
| Post-processing | In-place CPU chain: automatic exposure compensation, brightness/contrast LUT, saturation, unsharp-mask sharpening, horizontal mirroring. |
| Interactions | Freeze frame, zoom, pan, fit-to-screen, snapshot, ink tools (pen, highlighter, eraser, laser), OCR, and undo. |
| Snapshot | Captures the processed frame (with annotations) to a PNG file. |

**Hotkeys**

| Hotkey | Action |
| --- | --- |
| `F9` | Toggle the UI chrome |
| `F10` | Freeze / unfreeze the live image |
| `Ctrl+Z` | Undo |
| `Ctrl+PageUp` / `Ctrl+PageDown` | Previous / next page |
| `Ctrl+0` | Fit to screen |
| `Ctrl+S` | Save snapshot |
| `Ctrl++` / `Ctrl+-` | Zoom in / out |
| `Ctrl+Shift+O` | Copy OCR result |
| `Ctrl+Shift+Q` | Quit |

---

## 4. Repository Layout

```
ModernUI/
├── CMakeLists.txt            Root build: framework libraries + example programs
├── src/
│   ├── canvas/               Retained-mode scene graph and dirty-region manager
│   ├── event/                Backend-agnostic event queue (header-only)
│   ├── input/                Global WH_MOUSE_LL hook with lock-free queue
│   ├── animation/            Tween, keyframe, spring, sprite animation
│   ├── style/                StyleSheet, CSS parser, .ui parser
│   ├── ui/                   Widget/View, Flexbox, StyleEngine, UiLoader
│   ├── std_widgets/          Windows 11 Fluent Design control library
│   ├── backend_blend2d/      Blend2D renderer and layered Win32 window
│   └── app/                  Application facade
├── examples/
│   ├── blend2d_demo/         Layered window + canvas + Blend2D validation
│   ├── ui_demo/              Custom controls and Flexbox layout
│   ├── fluent_demo/          Fluent control gallery and sprite animation
│   └── style_demo/           CSS and .ui styling (with assets/)
├── Annotation/               Full-screen annotation application (standalone CMake project)
│   └── models/               PP-OCRv6 detection/recognition ONNX models and dictionary
├── CameraPro/                Document camera application (standalone CMake project)
│   ├── icons/                Application icon set
│   └── models/               PP-OCRv6 detection/recognition ONNX models and dictionary
├── icons/                    Shared semantic icons (annotation and AI cards)
├── installer/                Inno Setup script for the combined suite
├── 3rd/
│   ├── blend2d/              Vendored Blend2D source tree
│   ├── RapidOCRLite/         Vendored OCR runtime (static CPU build)
│   └── nanosvg.h             Single-header SVG parser used by the backend
├── .gitattributes
├── .gitignore
└── README.md
```

Generated directories (`build/`, `dist/`) are not tracked. The OCR models under `models/` are
part of the repository so that both applications are runnable without additional downloads.

---

## 5. Building

### 5.1 Prerequisites

- Windows 10 or later, x86-64.
- MSYS2 with the UCRT64 environment.
- CMake 3.20 or later (the root project itself requires 3.16).
- Ninja (recommended) or MinGW Makefiles.
- Inno Setup 7 (optional, only for producing the installer).

Install the toolchain from an MSYS2 UCRT64 shell:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc \
    mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-ninja
```

### 5.2 Framework and examples

```sh
cmake -S . -B build -G Ninja
cmake --build build -j
```

The root build produces the static libraries (`modernui::canvas`, `modernui::event`,
`modernui::input`, `modernui::animation`, `modernui::style`, `modernui::ui`,
`modernui::std_widgets`, `modernui::backend_blend2d`, `modernui::app`) and four example
executables. Pass `-DMODERNUI_BUILD_EXAMPLES=OFF` to build only the framework.

### 5.3 Applications

Annotation and CameraPro are independent CMake projects. Each adds the parent directory as a
sub-build with examples disabled, links the framework statically, and links RapidOCRLite as a
static CPU library.

```sh
cmake -S Annotation -B Annotation/build -G Ninja
cmake --build Annotation/build -j

cmake -S CameraPro -B CameraPro/build -G Ninja
cmake --build CameraPro/build -j
```

Executables are written to `Annotation/build/modernui_annotation.exe` and
`CameraPro/build/CameraPro.exe`.

### 5.4 Installer

After both applications have been built:

```powershell
& "C:\Program Files\Inno Setup 7\ISCC.exe" "D:\ModernUI\installer\CameraProSuite.iss"
```

The script collects both executables, the icon sets, the shared OCR models, and any runtime
DLLs found in the build directories, then writes
`dist/CameraPro_Annotation_Setup_0.1.0.exe`.

### 5.5 Runtime assets

Asset lookup prefers the executable's directory and falls back to the source tree during
development. An installed layout therefore places `icons/` and `models/` next to the
executable. Both applications require Microsoft YaHei (`msyh.ttc`), falling back to SimHei or
Segoe UI.

The AI card feature is optional. To enable it, place a UTF-8 file named `API_KEY.txt` containing
a DeepSeek API key next to the Annotation executable. The key is read at runtime and is never
compiled into the binary. `API_KEY.txt` is excluded by `.gitignore`; it must not be committed.

---

## 6. Runtime Requirements

- Windows 10 (build 10.0) or later, x86-64.
- Universal C Runtime (UCRT), included with supported Windows versions.
- Media Foundation (CameraPro capture).
- GDI+ (screenshot encoding).
- WinHTTP and COM/OLE automation (AI client and Office bridge).
- No third-party runtime DLLs: the MinGW build links `libgcc`, `libstdc++`, and `winpthread`
  statically.

---

## 7. Implementation Notes and Constraints

- **Coordinates.** Shape coordinates are absolute canvas pixels; a group's scroll offset is
  accumulated for descendants. The overlay window uses per-monitor-V2 DPI awareness so that
  virtual-screen and hook coordinates remain physical pixels.
- **Surface lifetime.** Recreating the layered-window DIB increments `surfaceSerial()`, signalling
  the renderer to rebind its image. Canvas dimensions must be updated on resize events.
- **Hook latency.** The low-level hook callback must stay allocation-free and lock-free; any
  blocking work is deferred to the application thread.
- **Presentation bridge.** WPS Presentation is identified by the `KWPP.Application` ProgID;
  Microsoft PowerPoint uses `PowerPoint.Application`. The current slide is read through
  `SlideShowWindow.View.Slide.SlideIndex`, which both implementations support. WPS does not
  support `View.GotoSlide` for external page changes, so the bridge observes rather than drives
  the slideshow.
- **OCR models.** Detection, recognition, and dictionary files are loaded lazily on a worker
  thread when OCR is first requested. Missing files produce an explicit diagnostic.
- **Release configuration.** The root project defaults to Release with `-O3` and the
  `x86-64-v2` baseline (SSE4.2, POPCNT) to avoid illegal instructions on older CPUs; both
  applications default to Release with `-O3`.
- **Verification.** The repository contains no automated test suite. Correctness is validated by
  building the framework and exercising the example programs and the two applications.

---

## 8. Third-Party Components

| Component | Role | License | Location |
| --- | --- | --- | --- |
| Blend2D | 2D vector rasterisation and compositing | Zlib | `3rd/blend2d` |
| RapidOCRLite | OCR runtime (detection and recognition) | Apache-2.0 | `3rd/RapidOCRLite` |
| PP-OCRv6 small models | OCR detection/recognition weights and dictionary | Apache-2.0 (PaddleOCR) | `Annotation/models`, `CameraPro/models` |
| nanosvg | SVG parsing and rasterisation | Zlib | `3rd/nanosvg.h` |

The vendored trees retain their original license files. DeepSeek is used only as an external
network service through the documented HTTP API.

---

## 9. Licensing

No license has yet been declared for the original source code in this repository. Until a
`LICENSE` file is added, the original code should be treated as all rights reserved. Vendored
components remain governed by their respective licenses listed in Section 8.

---

## 10. References

1. P. Kobalicek and F. Yzerman. *Blend2D: 2D Vector Graphics Powered by a JIT Compiler.*
   <https://blend2d.com>
2. PaddlePaddle Authors. *PaddleOCR: Awesome Multilingual OCR Toolkit.*
   <https://github.com/PaddlePaddle/PaddleOCR>
3. RapidAI. *RapidOCR: A cross-platform OCR deployment toolkit.*
   <https://github.com/RapidAI/RapidOCR>
4. W3C. *CSS Flexible Box Layout Module Level 1.* <https://www.w3.org/TR/css-flexbox-1/>
5. Microsoft. *Fluent Design System.* <https://fluent2.microsoft.design/>
6. Microsoft. *Layered Windows (UpdateLayeredWindow).* Microsoft Learn, Win32 API documentation.
7. Microsoft. *Media Foundation.* Microsoft Learn, Win32 API documentation.
8. Microsoft. *LowLevelMouseProc callback function.* Microsoft Learn, Win32 API documentation.
9. DeepSeek. *DeepSeek API Documentation.* <https://api-docs.deepseek.com/>
