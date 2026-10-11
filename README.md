# DeepHex

A native Qt 6 binary editor for macOS. Explore hex and ASCII side by side, edit bytes without shifting file offsets, and inspect executable structure and numeric values.

The compact [favicon](favicon.ico) replaces the old logo and is also used for the application and macOS bundle icon. Its vector source is [`img/favicon.svg`](img/favicon.svg).

![DeepHex binary editor in Terminal appearance, captured from the native macOS window](img/editor.png)

## What's new in 2.0

- A resizable workbench with native macOS controls, system/light/dark/terminal appearance, a data inspector, and a navigable file-region sidebar.
- An overview color legend generated from the displayed regions: zero-filled, ASCII text, and binary distribution bins, or named executable headers/segments. The outlined marker identifies the visible bytes; the legend wraps with the window and follows appearance changes.
- A virtualized hex/ASCII editor with real vertical and horizontal scrollbars, trackpad scrolling, grouped bytes, keyboard navigation, and drag selection with edge scrolling.
- Hex nibble editing and printable ASCII editing, undo/redo, explicit Copy as Hex, strict hex or UTF-8 paste, and zero-fill selection. A two-digit hex edit is one undo step.
- Save and Save As use atomic replacement. Unsaved-change prompts protect opening another file and closing the window; saving warns if the original changed on disk.
- Hex and case-sensitive UTF-8 search, next/previous matches with wraparound, and Go to Offset.
- Signed/unsigned 8-, 16-, 32-, and 64-bit values, binary, float32, and float64 with selectable byte order. Values unavailable at EOF are shown as a dash rather than read past the buffer.
- Bounds-checked Mach-O and ELF region parsing: 32-/64-bit, both byte orders, and universal Mach-O architectures. Malformed or unrecognized files fall back to a zero/text/binary distribution overview.
- Drag-and-drop opening, recent files, command-line opening, Finder “Open With” support, and remembered window layout and appearance.

## Build on macOS

Requires CMake 3.21+, Qt 6.8+, and a C++17 compiler. Install Apple's Command Line Tools if needed (`xcode-select --install`), then:

```bash
brew install qt cmake ninja
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build --parallel
open build/deephex.app
```

Open a file directly from the terminal:

```bash
./build/deephex.app/Contents/MacOS/deephex /path/to/file.bin
```

The build produces a local `.app` bundle, not a signed/notarized distributable. Qt libraries remain provided by Homebrew. For another Qt installation, set `CMAKE_PREFIX_PATH` to its installation prefix. On non-macOS platforms the executable is `build/deephex`.

## Appearance

Choose **View → Appearance → Terminal** for a terminal-inspired charcoal workbench: cyan offsets and headers, teal printable bytes, amber inspector values, muted zero bytes, and subtle purple dividers. Text, caret, and selection colors remain opaque; only the background surfaces are tinted.

On macOS, **Translucent Background** enables native `NSVisualEffectView` behind-window material rather than lowering the whole window's opacity. The material covers the entire native frame, including the title bar; lightweight charcoal overlays tint the editor, sidebar, and controls without covering the effect with nearly opaque backgrounds. It is enabled by default for Terminal and can be turned off for an opaque version of the same palette. macOS **Reduce Transparency** automatically replaces both the material and the clear native backing with opaque backgrounds without forgetting your preference; changing that accessibility setting updates an open window. Unsupported platforms use opaque backgrounds and disable the translucency control.

**System** remains the default. System, Light, and Dark retain their normal palettes. The chosen preset and translucency preference persist between launches; switching them preserves bytes, selection, and undo history. The preview above is a native, window-isolated macOS capture; it does not include the desktop backdrop.

## Editing

| Action | macOS shortcut / interaction |
| --- | --- |
| Open / Save / Save As | ⌘O / ⌘S / ⇧⌘S |
| Undo / Redo | ⌘Z / ⇧⌘Z |
| Copy as Hex / Paste | ⌘C / ⌘V |
| Select All | ⌘A |
| Find / Next / Previous | ⌘F / ⌘G / ⇧⌘G |
| Go to Offset | ⌘L |
| Switch hex / ASCII editing | Tab, or click the desired column |
| Move / extend selection | Arrow keys / Shift + arrows; Home, End, Page Up, Page Down |
| File beginning / end | ⌘Home / ⌘End |
| Navigate regions | Double-click a sidebar region, or click/drag the overview strip |
| Zero bytes | Edit → Zero Selected Bytes, or the editor context menu |

Go to Offset accepts decimal or `0x`-prefixed hexadecimal. Hex search/paste accepts pairs such as `DE AD BE EF`, with optional whitespace, but no `0x` prefixes. ASCII typing accepts printable ASCII; pasting in that column writes UTF-8 bytes. Selecting bytes does not modify the clipboard.

### Deliberate limits

This is an **overwrite-only, in-memory editor**, supporting files up to **256 MiB**. Editing does not insert, delete, or shift bytes. Pasting starts at the beginning of the selection and must fit within the file; it selects the pasted bytes without resizing the file. The inspector reads forward from the first selected byte, regardless of selection length. Search runs synchronously and can pause the UI on large files. Executable maps describe file offsets, not a live process's virtual memory.

## Verification

Behavior regression tests cover exact overview coverage, executable byte order and segment offsets, malformed input fallback, overview navigation bounds, empty files, nibble undo/redo and clean-state transitions, atomic overwrite/zero undo, cancel/discard handling without changing disk contents, and preserving document selection and undo/clean state when changing appearance.

```bash
ctest --test-dir build --output-on-failure
```

Tests use Qt's offscreen platform and isolated temporary files/settings. Disable the test target with `-DBUILD_TESTING=OFF` if only the application is needed.
