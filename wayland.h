#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>
#include "xdg-shell-protocol.h"
#include "mt.h"
#include "font.h"
#include "input.h"
#include "pty.h"

typedef struct {
    void    *data;           /* mmap'd pixel buffer */
    size_t   size;
    int      fd;
    struct wl_buffer *wl_buf;
    bool     busy;
} WlBuffer;

typedef struct WaylandState WaylandState;
struct WaylandState {
    struct wl_display    *display;
    struct wl_registry   *registry;
    struct wl_compositor *compositor;
    struct wl_shm        *shm;
    struct xdg_wm_base   *xdg_wm_base;
    struct wl_seat       *seat;
    struct wl_keyboard   *keyboard;

    struct wl_surface    *surface;
    struct xdg_surface   *xdg_surface;
    struct xdg_toplevel  *xdg_toplevel;

    WlBuffer  buffers[2];
    int       buf_idx;      /* index of the back buffer */
    bool     *buf_dirty[2]; /* per-buffer per-row dirty flags */

    int       width, height;

    /* key repeat */
    int      rpt_fd;        /* timerfd, -1 if unavailable */
    int      rpt_rate;      /* repeats/sec from compositor (0 = no repeat) */
    int      rpt_delay;     /* initial delay ms */
    uint32_t rpt_key;       /* key code being held */

    /* cursor blink */
    int      blink_fd;      /* timerfd, -1 if unavailable */

    /* keyboard modifier state (updated by kb_modifiers) */
    bool     kb_shift;

    /* mouse / pointer */
    struct wl_pointer *pointer;
    int      ptr_x, ptr_y;     /* current cell position, 1-based */
    int      ptr_px, ptr_py;   /* current pixel position */
    uint32_t ptr_buttons;      /* pressed buttons bitmask (bit 0=left,1=mid,2=right) */
    uint32_t ptr_serial;       /* serial from last pointer button event */
    bool     ptr_in;           /* pointer inside surface */

    /* text selection */
    bool     selecting;
    bool     has_sel;
    int      sel_x0, sel_y0;   /* anchor, 0-based cell coords */
    int      sel_x1, sel_y1;   /* current end, 0-based */

    /* clipboard */
    struct wl_data_device_manager *data_mgr;
    struct wl_data_device  *data_device;
    struct wl_data_offer   *data_offer;   /* current selection offer (for paste) */
    struct wl_data_source  *data_source;  /* owned source while we hold clipboard */
    char                   *sel_text;     /* UTF-8 text served by data_source */
    int                     sel_text_len;

    bool      configured;
    bool      frame_pending;
    bool      dirty;        /* any cell changed since last render */
    bool      running;

    Term  *term;
    Font  *font;
    Input *input;
    Pty   *pty;
};

WaylandState *wayland_init(Term *t, Font *f, Input *inp, Pty *pty);
void          wayland_run(WaylandState *ws);
void          wayland_free(WaylandState *ws);

/* called by vt when pty_write needed */
void wayland_pty_write(void *data, const char *buf, int len);
