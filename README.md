# mt — minimal Wayland terminal

A small, fast terminal emulator for Wayland **tiling** compositors (sway, river, Hyprland, etc.).
No toolkit dependencies, no scrollback, no window title management.
CPU software rendering via `wl_shm` — no GPU requirement.

mt is built for tiling compositors: it has no resize handles, no decorations of its own,
and no UI affordances beyond the grid. On floating compositors (GNOME, KDE, weston) it
requests `xdg_toplevel.set_maximized` on startup so the window fills the available
area instead of opening at a tiny `DEFAULT_COLS × DEFAULT_ROWS`. Tiling compositors
generally ignore this hint and tile the window normally.

## Goals

| Metric | Target |
|---|---|
| Binary (stripped) | < 100 KB |
| RSS at idle | < 8 MB |
| Dependencies | wayland-client, freetype2, xkbcommon |

## Build

```sh
# Arch / CachyOS
pacman -S wayland freetype2 libxkbcommon

make        # produces ./mt
make clean
make run
```

`xdg-shell-protocol.{c,h}` are checked in; `wayland-scanner` is not required for a plain build.

## Install

```sh
sudo ./install.sh              # installs to /usr/local/bin and /usr/local/share/applications
PREFIX=~/.local ./install.sh   # user-local install, no sudo needed
sudo make install              # same as install.sh via make
sudo make uninstall
```

The `.desktop` entry installed to `$PREFIX/share/applications/mt.desktop` makes `mt` visible
to rofi, wofi, dmenu, and any launcher that indexes XDG application directories.

## Usage

```
mt [-f <font.ttf>] [-s <size_px>] [-c <cols>] [-r <rows>] [-- cmd [args...]]
```

All flags are optional. The default font path and size are set in `config.h` at compile time.

```sh
mt                                                          # use compiled-in defaults
mt -f /usr/share/fonts/TTF/DejaVuSansMono.ttf -s 14
mt -f /usr/share/fonts/TTF/JetBrainsMono-Regular.ttf -s 13 -- tmux
```

## Keyboard

| Key | Action |
|---|---|
| Ctrl+Shift+C | Copy selection to clipboard |
| Ctrl+Shift+V | Paste from clipboard |
| Middle click | Paste from clipboard |

Standard xterm key sequences for arrows, F1–F12, modifier combos, and application cursor mode.

## Configuration

Edit `config.h` and rebuild:

```c
#define FONT_PATH     "/usr/share/fonts/TTF/DejaVuSansMono.ttf"
#define FONT_SIZE_PX  14
#define DEFAULT_COLS  80
#define DEFAULT_ROWS  24
#define DEFAULT_FG    0x00cccccc
#define DEFAULT_BG    0x00000000
```

The 16 ANSI palette colors are also in `config.h`.

## Architecture

```
main.c          entry point, argument parsing
wayland.c       Wayland connection, wl_shm buffers, frame pacing, input dispatch
input.c         xkbcommon state machine, key-to-sequence translation
pty.c           forkpty, non-blocking read/write, SIGWINCH via TIOCSWINSZ
vt.c            VT/xterm escape parser (CSI/OSC/ESC), terminal grid, UTF-8 decoder
render.c        dirty-row blitter, glyph compositing into wl_shm pixel buffer
font.c          FreeType face init, glyph atlas (hash table + flat alpha array)
config.h        compile-time defaults
```

**Rendering**: two `wl_shm` pixel buffers, double-buffered. Only dirty rows are re-blitted per frame. Zero CPU when idle (gated on `wl_surface_frame` callback).

**Font**: one monospace face, one size, rasterized to an 8-bit alpha atlas on first use. Synthetic bold (`FT_GlyphSlot_Embolden`) and italic (`FT_GlyphSlot_Oblique`).

**Cursor**: blinking block at 500 ms interval via `timerfd`; blink phase resets on any keypress so the cursor is never hidden mid-input. Shape controlled by DECSCUSR.

**VT**: covers the DEC/xterm subset — 256-color and truecolor SGR, cursor movement, erase, scroll region, insert/delete, alternate screen (?1047/?1049), mouse reporting (X10/button/all, SGR), OSC 52 clipboard, bracketed paste, focus events, DECSCUSR cursor shapes, DEC special graphics charset.

## Known limitations

- No scrollback. The grid is the only buffer.
- No window title (`xdg_toplevel_set_title` is never called).
- No PRIMARY selection (Wayland `zwp_primary_selection` not implemented; middle-click pastes from clipboard only).
- Combining characters are discarded (precomposed NFC input works fine).
- No mouse cursor shape (text-beam cursor not set on pointer enter).

## License

GNU General Public License v3 (GPL-3.0-or-later). See [LICENSE](LICENSE).
