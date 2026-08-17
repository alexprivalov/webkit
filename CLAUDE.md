# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

This is the **QtWebKit development fork** of WebKit — the full upstream WebKit tree plus Qt-specific
patches. `master` mirrors upstream WebKit; active work here is porting/maintaining the **Qt port**
(currently Qt6) and getting it building on **macOS** (see recent commits: Qt CMake targets, homebrew
libxml2, font-family caching, Qt deprecation fixes). When touching cross-platform code you are editing
the same files upstream ships; when touching `*Qt*` / `PlatformQt` files you are in port-specific code.

Ports live behind `PORT` (Qt, GTK, WPE, Mac, WPE, Win, JSCOnly). The Qt port is CMake-based.

## Build

The Qt port builds with CMake (Ninja). Two entry points:

```bash
# Perl wrapper (handles feature flags, prefix paths, ccache):
Tools/Scripts/build-webkit --qt --release        # or --debug
Tools/Scripts/build-webkit --qt --cmakeargs="-DENABLE_VIDEO=OFF"

# Or CMake presets directly (see CMakePresets.json):
cmake --preset qt-dev-debug && cmake --build WebKitBuild/Qt/Debug
```

Build output goes to `WebKitBuild/<Port>/<Config>/`. The top-level `Makefile` is the **Xcode/macOS
workspace** path (`make release`, `make debug`) — used for Apple ports, not the Qt CMake build.

Port build options are defined in `Source/cmake/OptionsQt.cmake`; the master feature list (all
`ENABLE_*` flags and defaults) is `Source/cmake/WebKitFeatures.cmake`. When adding a feature guard,
wire it in both places.

Notable Qt-port defaults (from `CMakePresets.json`): `ENABLE_OPENGL=OFF`, `ENABLE_INSPECTOR_UI=ON`,
`ENABLE_WEBKIT=OFF` (WebKit2 layer off — Qt uses the WebKitLegacy API), fatal warnings off.

## Test & lint

```bash
Tools/Scripts/check-webkit-style -g origin/master..   # style check on your commits (CI runs this)
Tools/Scripts/run-webkit-tests                         # layout tests (LayoutTests/)
Tools/Scripts/run-javascriptcore-tests                 # JSC correctness (JSTests/)
Tools/Scripts/run-jsc-stress-tests <path>              # JSC stress suite
Tools/Scripts/run-api-tests                            # C++ API unit tests (tests/, Tools/TestWebKitAPI)
Tools/Scripts/run-bindings-tests                       # verify IDL code generators
```

Run a single layout test: pass its path, e.g. `run-webkit-tests fast/dom/foo.html`.
Test expectations (known failures/skips per platform) live in `LayoutTests/**/TestExpectations`;
edit these rather than deleting tests, and run `Tools/Scripts/lint-test-expectations` after.

CI (`.travis.yml`) only runs `check-webkit-style` — full test suites run on the WebKit buildbots, not here.

## Architecture

WebKit is strictly layered; lower layers must not depend on higher ones. `Source/`:

- **WTF** — "Web Template Framework": the foundation. Custom containers, strings (`WTF::String`),
  smart pointers (`Ref`/`RefPtr`, `std::unique_ptr`), threading, `RunLoop`. Used everywhere.
- **bmalloc** — the memory allocator backing `FastMalloc` and `IsoHeap`.
- **JavaScriptCore** (JSC) — the JS engine: parser → bytecode → interpreter (LLInt) → JITs (Baseline,
  DFG, FTL). Also hosts the Web Inspector protocol backend. Self-contained; only depends on WTF/bmalloc.
- **WebCore** — the engine core: DOM, CSS, layout/rendering, loading, graphics, media, editing. The
  bulk of the code. Cross-platform logic with per-platform backends under `platform/` subdirs.
- **WebKitLegacy** — the original single-process embedding API (WebView). **This is what the Qt port
  uses** (WebKit2 is disabled). Qt API glue is under `Source/WebKit/qt` / `Source/WebKitLegacy`.
- **WebKit** — the modern multi-process (WebKit2) API: UIProcess / WebProcess / NetworkProcess split
  over IPC. Off for Qt.
- **WebInspectorUI**, **WebDriver**, **WebGPU** — tooling and newer subsystems.

Key cross-cutting mechanisms to know before editing WebCore/JSC:

- **Generated bindings**: JS-exposed DOM APIs are defined in `.idl` files and code-generated at build
  time (`WebCore/bindings/scripts/`). Edit the `.idl` + generator, not the generated output. Verify
  with `run-bindings-tests`.
- **Feature flags**: guarded by `ENABLE(FEATURE)` macros driven by the CMake feature list above.
- **Ref-counting is the memory model**: DOM/WebCore objects are `RefCounted`; use `Ref`/`RefPtr`, never
  raw `new`/`delete`. Prefer `Ref<>` for non-null. `WeakPtr` for non-owning refs.

## Conventions

- Follow the WebKit style guide, enforced by `check-webkit-style` and `.clang-format`. Run it before
  committing — it is the one gate CI enforces here.
- Match surrounding code exactly; WebKit has strong idioms (naming, header include order, one class per
  file, `#pragma once`). New files need the standard WebKit license header.
- Qt-port changes are conventionally prefixed `Qt: ` in the commit subject.
