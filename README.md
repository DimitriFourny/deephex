# DeepHex

DeepHex is a native binary editor for macOS. View hex and ASCII side by side, overwrite bytes without shifting file offsets, and inspect file structure and numeric values.

![DeepHex editor and data inspector in Terminal appearance](img/editor.png)

## What it does

- Edit hex bytes or printable ASCII, paste hex or UTF-8 data, zero selected bytes, and undo or redo changes.
- Search for hex patterns or case-sensitive UTF-8 text, move between matches, and jump to a file offset.
- Inspect binary, signed and unsigned integers, float32, and float64 in either byte order.
- Explore Mach-O and ELF headers and segments, including universal Mach-O files. Other files show an overview of zero-filled, ASCII, and binary regions.
- Save with atomic replacement, unsaved-change prompts, and a warning when the original file has changed on disk.

## How to use it

Open a file with **File → Open**, drag it into the window, or use Finder’s **Open With** menu. Recently opened files are available under **File → Open Recent**.

Click a byte in the hex or ASCII column to select where to edit. Type two hex digits to replace a byte, or type a printable character in the ASCII column. Press **Tab** to switch between columns. Drag or hold **Shift** while navigating to select a range.

The data inspector reads values starting at the first selected byte. Choose **Little endian** or **Big endian** to change the byte order. Values that would read past the end of the file appear as a dash.

Use the overview strip to navigate through the file, or double-click a region in the sidebar. The overview colors describe file contents; its outlined marker shows the visible bytes.

### Shortcuts

| Action | Shortcut / interaction |
| --- | --- |
| Open / Save / Save As | ⌘O / ⌘S / ⇧⌘S |
| Undo / Redo | ⌘Z / ⇧⌘Z |
| Copy as Hex / Paste | ⌘C / ⌘V |
| Select All | ⌘A |
| Find / Next / Previous | ⌘F / ⌘G / ⇧⌘G |
| Go to Offset | ⌘L |
| Switch hex / ASCII editing | Tab, or click the desired column |
| Move / extend selection | Arrow keys / Shift + arrows |
| Row beginning / end | Home / End |
| Page up / down | Page Up / Page Down |
| File beginning / end | ⌘Home / ⌘End |
| Navigate regions | Double-click a sidebar region, or click/drag the overview strip |
| Zero selected bytes | Edit → Zero Selected Bytes, or the editor context menu |

**Go to Offset** accepts decimal offsets or hexadecimal prefixed with `0x`. Hex search and paste accept pairs such as `DE AD BE EF`, with optional whitespace and no `0x` prefixes. Pasting in the ASCII column writes UTF-8 bytes. Selecting bytes does not change the clipboard; **Copy as Hex** copies the selection as hex pairs.

### Appearance

Choose **View → Appearance**:

- **System** follows macOS: the native light palette in light mode and Terminal in dark mode.
- **Light** keeps the light palette enabled.
- **Terminal** keeps the dark palette enabled, with cyan offsets, teal printable bytes, and amber inspector values.

**Translucent Background** is enabled by default in Terminal. Turn it off for an opaque background. Text and selection remain fully opaque, and macOS **Reduce Transparency** is respected. Window layout and appearance preferences are remembered between launches.

### Limits

DeepHex is an **overwrite-only, in-memory editor** for files up to **256 MiB**. Editing does not insert, delete, or shift bytes. Paste starts at the beginning of the selection and must fit within the existing file.

Search runs synchronously and can pause the interface on large files. Executable regions describe file offsets, not a running process’s virtual memory.

## Build on macOS

Requires **macOS 12+**, **CMake 3.21+**, and Apple’s C++17/Objective-C++ compiler.

Install Apple’s Command Line Tools if needed:

```bash
xcode-select --install
```

Install CMake and Ninja with Homebrew, then build and launch:

```bash
brew install cmake ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
open build/deephex.app
```

To open a file directly from the terminal:

```bash
./build/deephex.app/Contents/MacOS/deephex /path/to/file.bin
```

The build produces a local, unsigned `.app` bundle using Apple system frameworks. No third-party GUI runtime is required. DeepHex supports macOS only.
