# macOS platform layer — not implemented yet

This directory will hold the Objective-C++ implementations of the interfaces in
`include/core/platform.hpp` plus the `NSApplication` entry point.

See **`MACOS.md` in the repository root** for the full porting guide: expected
file list, CGEventTap notes, TCC permissions, packaging, and a suggested
implementation order. Use `src/platform/win` as the reference implementation.
