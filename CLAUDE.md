# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Goal

A minimal Wayland terminal emulator optimized for low RAM, CPU, and disk footprint while remaining fast enough for daily use. Designed for tiling compositors (sway, river, etc.) — no scrollback, no window title handling. No toolkit dependencies (no GTK, Qt, SDL, etc.).

## Build

```sh
make          # build ./mt
make clean    # remove build artifacts
make run      # build and launch
```

Dependencies (install via package manager):
- `libwayland-client` — Wayland IPC
- `freetype2` — glyph rasterization
- `xkbcommon` — keyboard input handling
- `libxkbcommon-x11` is NOT needed (Wayland only)

```sh
# Arch/CachyOS
pacman -S wayland freetype2 libxkbcommon
```

## Architecture

### Stack choices and rationale

| Layer | Choice | Why |
|---|---|---|
| Language | C (C11) | No GC pauses, no runtime, tiny binary |
| Display | `wl_shm` pixel buffer | CPU software render; no GPU driver dependency, zero VRAM |
| Font | FreeType (monospace only) | No Pango/HarfBuzz/fontconfig; path provided via `-f`, one face, one size, pre-rasterized glyph cache |
| Keyboard | xkbcommon | Required for correct Wayland key events; no X11 dep |
| Subprocess | POSIX pty (`openpty`/`forkpty`) | Standard; no extra libs |

### Key modules (planned file layout)

```
main.c        — entry point, event loop
wayland.c     — wl_display, wl_surface, wl_shm buffer management, frame callbacks
input.c       — wl_seat, wl_keyboard, xkbcommon state machine
pty.c         — forkpty, read/write, SIGCHLD handling
vt.c          — VT100/xterm escape sequence parser and terminal grid state
render.c      — FreeType glyph cache + blit glyphs into wl_shm buffer
font.c        — FreeType face init, glyph pre-rasterization into atlas
```

### Resource constraints driving design decisions

- **Double-buffer wl_shm**: two pixel buffers, swap on frame callback. Never allocate a third.
- **Glyph atlas**: rasterize each (codepoint, bold, italic) triplet once at startup or on first use; store as 8-bit alpha in a flat array. No per-frame FreeType calls.
- **Terminal grid**: fixed `cols × rows` array of `Cell` structs (codepoint + attrs). Dirty-region tracking so only changed cells are re-blitted per frame.
- **No scrollback**: the grid is the only buffer. No ring buffer, no history, no search. Saves significant RAM and eliminates a class of complexity.
- **No heap allocations in the render path**: all buffers sized at init from terminal dimensions.
- **Single-threaded**: one `poll()`/`epoll` loop over `wl_display` fd and pty fd. No threads.
- **No fontconfig**: font file path is a required CLI argument (`-f /path/to/font.ttf`). Eliminates ~3–5 MB RSS and fontconfig's filesystem scan at startup. No fallback fonts.
- **No window title**: OSC 0 and OSC 2 escape sequences are parsed and silently discarded. `xdg_toplevel_set_title` is never called. Tiling compositors assign names; we don't fight them.

### VT parser

Implements a state machine covering the DEC/xterm subset: CSI, OSC, ESC sequences for color (256-color + truecolor), cursor movement, erase, bold/italic/underline. OSC 0/2 (window title) are parsed and discarded. Reference: [Paul Flo Williams' vt state machine](https://vt100.net/emu/dec_ansi_parser).

### Frame pacing

Use `wl_surface_frame()` callback to gate redraws — only redraw when Wayland requests a frame AND the terminal grid is dirty. This means zero CPU when idle.

## Development workflow

After every major change (new feature, bug fix, refactor), create a git commit. Keep commits focused — one logical change per commit. Always build and verify the binary is not broken before committing.

## Design constraints (do not violate)

- No GTK, Qt, SDL, EGL, Vulkan, or OpenGL.
- No fontconfig. Font path comes from the user, not font discovery.
- No scrollback buffer of any kind.
- No window title support (`xdg_toplevel_set_title` is never called).
- No threads.
- No heap allocation in hot paths (render loop, pty read loop).
- Binary size target: under 200 KB stripped.
- RSS target at idle: under 8 MB.
