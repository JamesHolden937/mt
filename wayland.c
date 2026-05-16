#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif
#include "wayland.h"
#include "render.h"
#include "vt.h"
#include "xdg-shell-protocol.h"

/* ── shared memory buffer ────────────────────────────────────────────────── */
static int shm_open_anon(size_t size) {
    int fd = memfd_create("mt-shm", MFD_CLOEXEC);
    if (fd < 0) { perror("memfd_create"); exit(1); }
    if (ftruncate(fd, (off_t)size)) { perror("ftruncate"); exit(1); }
    return fd;
}

static void buffer_release(void *data, struct wl_buffer *wl_buf) {
    (void)wl_buf;
    ((WlBuffer *)data)->busy = false;
}
static const struct wl_buffer_listener buf_listener = { buffer_release };

static void alloc_buffer(WaylandState *ws, WlBuffer *b, int w, int h) {
    size_t sz = (size_t)w * h * 4;
    if (b->wl_buf) {
        wl_buffer_destroy(b->wl_buf);
        munmap(b->data, b->size);
        close(b->fd);
    }
    b->fd   = shm_open_anon(sz);
    b->size = sz;
    b->data = mmap(NULL, sz, PROT_READ|PROT_WRITE, MAP_SHARED, b->fd, 0);
    if (b->data == MAP_FAILED) { perror("mmap"); exit(1); }

    struct wl_shm_pool *pool = wl_shm_create_pool(ws->shm, b->fd, (int32_t)sz);
    b->wl_buf = wl_shm_pool_create_buffer(pool, 0, w, h, w*4,
                                           WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    wl_buffer_add_listener(b->wl_buf, &buf_listener, b);
    b->busy = false;
}

/* ── frame callback ──────────────────────────────────────────────────────── */
static void frame_done(void *data, struct wl_callback *cb, uint32_t t) {
    (void)t;
    wl_callback_destroy(cb);
    ((WaylandState *)data)->frame_pending = false;
}
static const struct wl_callback_listener frame_listener = { frame_done };

static void request_frame(WaylandState *ws) {
    if (ws->frame_pending) return;
    ws->frame_pending = true;
    struct wl_callback *cb = wl_surface_frame(ws->surface);
    wl_callback_add_listener(cb, &frame_listener, ws);
    wl_surface_commit(ws->surface);
}

static void do_render(WaylandState *ws) {
    if (!ws->dirty || ws->frame_pending || !ws->configured) return;

    int bi = -1;
    for (int i = 0; i < 2; i++)
        if (!ws->buffers[i].busy) { bi = i; break; }
    if (bi < 0) return;

    /* Drain term-level dirty flags into both buffer slots, then clear them.
       Each changed row will be re-rendered once per buffer over two frames. */
    bool *td = ws->term->dirty[ws->term->screen];
    for (int y = 0; y < ws->term->rows; y++) {
        if (td[y]) {
            ws->buf_dirty[0][y] = true;
            ws->buf_dirty[1][y] = true;
            td[y] = false;
        }
    }

    /* build selection in reading order */
    Selection sel = { .active = ws->has_sel || ws->selecting };
    if (sel.active) {
        int x0 = ws->sel_x0, y0 = ws->sel_y0;
        int x1 = ws->sel_x1, y1 = ws->sel_y1;
        if (y0 > y1 || (y0 == y1 && x0 > x1))
            { sel.x0=x1; sel.y0=y1; sel.x1=x0; sel.y1=y0; }
        else
            { sel.x0=x0; sel.y0=y0; sel.x1=x1; sel.y1=y1; }
    }
    render_frame(ws->buffers[bi].data, ws->width, ws->buf_dirty[bi],
                 &sel, ws->term, ws->font);
    ws->buffers[bi].busy = true;

    /* Stay dirty if the other buffer still has rows pending so the event
       loop re-renders it next frame (after the compositor acks this one). */
    bool pending = false;
    for (int y = 0; y < ws->term->rows && !pending; y++)
        pending = ws->buf_dirty[1 - bi][y];
    ws->dirty = pending;

    wl_surface_attach(ws->surface, ws->buffers[bi].wl_buf, 0, 0);
    wl_surface_damage_buffer(ws->surface, 0, 0, ws->width, ws->height);
    request_frame(ws);
}

/* ── xdg-wm-base ─────────────────────────────────────────────────────────── */
static void xdg_wm_ping(void *data, struct xdg_wm_base *wm, uint32_t serial) {
    (void)data; xdg_wm_base_pong(wm, serial);
}
static const struct xdg_wm_base_listener wm_listener = { xdg_wm_ping };

/* ── xdg-surface ─────────────────────────────────────────────────────────── */
static void xdg_surface_configure(void *data, struct xdg_surface *xs, uint32_t serial) {
    WaylandState *ws = data;
    xdg_surface_ack_configure(xs, serial);
    if (!ws->configured) {
        ws->configured = true;
        term_mark_all_dirty(ws->term);
        ws->dirty = true;
        do_render(ws);
    }
}
static const struct xdg_surface_listener xdg_surface_listener = {
    xdg_surface_configure
};

/* ── xdg-toplevel ────────────────────────────────────────────────────────── */
static void toplevel_configure(void *data, struct xdg_toplevel *tl,
    int32_t w, int32_t h, struct wl_array *states) {
    (void)tl; (void)states;
    WaylandState *ws = data;
    if (w <= 0 || h <= 0) return;
    if (w == ws->width && h == ws->height) return;

    ws->width  = w;
    ws->height = h;

    int cols = w / ws->font->cell_w;
    int rows = h / ws->font->cell_h;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    term_resize(ws->term, cols, rows);
    pty_resize(ws->pty, cols, rows);
    term_mark_all_dirty(ws->term);

    for (int i = 0; i < 2; i++)
        alloc_buffer(ws, &ws->buffers[i], w, h);

    for (int i = 0; i < 2; i++) {
        free(ws->buf_dirty[i]);
        ws->buf_dirty[i] = malloc(rows);
        memset(ws->buf_dirty[i], 1, rows); /* buffer contents are stale after resize */
    }

    ws->dirty = true;
}
static void toplevel_close(void *data, struct xdg_toplevel *tl) {
    (void)tl; ((WaylandState *)data)->running = false;
}
static void toplevel_configure_bounds(void *data, struct xdg_toplevel *tl,
    int32_t w, int32_t h) { (void)data;(void)tl;(void)w;(void)h; }
static void toplevel_wm_capabilities(void *data, struct xdg_toplevel *tl,
    struct wl_array *caps) { (void)data;(void)tl;(void)caps; }
static const struct xdg_toplevel_listener toplevel_listener = {
    toplevel_configure, toplevel_close,
    toplevel_configure_bounds, toplevel_wm_capabilities,
};

/* ── key repeat timer ────────────────────────────────────────────────────── */
static void repeat_arm(WaylandState *ws, uint32_t key) {
    if (ws->rpt_fd < 0 || ws->rpt_rate <= 0) return;
    ws->rpt_key = key;
    long interval_ns = 1000000000L / ws->rpt_rate;
    struct itimerspec its = {
        .it_value    = { ws->rpt_delay / 1000,
                         (ws->rpt_delay % 1000) * 1000000L },
        .it_interval = { 0, interval_ns },
    };
    timerfd_settime(ws->rpt_fd, 0, &its, NULL);
}

static void repeat_disarm(WaylandState *ws) {
    if (ws->rpt_fd < 0) return;
    struct itimerspec its = { {0,0},{0,0} };
    timerfd_settime(ws->rpt_fd, 0, &its, NULL);
}

/* ── keyboard ────────────────────────────────────────────────────────────── */
static void kb_keymap(void *data, struct wl_keyboard *kb,
    uint32_t fmt, int fd, uint32_t size) {
    (void)kb;
    if (fmt != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { close(fd); return; }
    input_set_keymap(((WaylandState *)data)->input, fd, size);
}
static void kb_enter(void *data, struct wl_keyboard *kb,
    uint32_t serial, struct wl_surface *surf, struct wl_array *keys) {
    (void)kb;(void)serial;(void)surf;(void)keys;
    WaylandState *ws = data;
    if (ws->term->focus_events)
        pty_write(ws->pty, "\033[I", 3);
}
static void kb_leave(void *data, struct wl_keyboard *kb,
    uint32_t serial, struct wl_surface *surf) {
    (void)kb;(void)serial;(void)surf;
    WaylandState *ws = data;
    repeat_disarm(ws);
    if (ws->term->focus_events)
        pty_write(ws->pty, "\033[O", 3);
}
static void clipboard_paste(WaylandState *ws);

static void kb_key(void *data, struct wl_keyboard *kb,
    uint32_t serial, uint32_t time, uint32_t key, uint32_t key_state) {
    (void)kb;(void)serial;(void)time;
    WaylandState *ws = data;

    if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        bool ctrl, shift;
        xkb_keysym_t sym = input_keysym_mods(ws->input, key, &ctrl, &shift);
        if (ctrl && shift) {
            if (sym == XKB_KEY_c || sym == XKB_KEY_C) {
                /* Ctrl+Shift+C: copy selection */
                return; /* selection is already in clipboard on mouse release */
            }
            if (sym == XKB_KEY_v || sym == XKB_KEY_V) {
                clipboard_paste(ws);
                return;
            }
        }
    }

    char buf[32];
    int n = input_key(ws->input, key, key_state, ws->term->app_cursor, buf);
    if (n > 0) pty_write(ws->pty, buf, n);

    if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED && n > 0)
        repeat_arm(ws, key);
    else if (key_state == WL_KEYBOARD_KEY_STATE_RELEASED && key == ws->rpt_key)
        repeat_disarm(ws);
}
static void kb_modifiers(void *data, struct wl_keyboard *kb,
    uint32_t serial, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t grp) {
    (void)kb;(void)serial;
    WaylandState *ws = data;
    input_update_mods(ws->input, dep, lat, lock, grp);
    if (ws->input->state)
        ws->kb_shift = xkb_state_mod_name_is_active(ws->input->state,
                           XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
}
static void kb_repeat_info(void *data, struct wl_keyboard *kb,
    int32_t rate, int32_t delay) {
    (void)kb;
    WaylandState *ws = data;
    ws->rpt_rate  = rate;
    ws->rpt_delay = delay;
}
static const struct wl_keyboard_listener kb_listener = {
    kb_keymap, kb_enter, kb_leave, kb_key, kb_modifiers, kb_repeat_info
};

/* forward declarations so ptr_listener can be defined before seat_capabilities */
static void ptr_enter(void*, struct wl_pointer*, uint32_t, struct wl_surface*, wl_fixed_t, wl_fixed_t);
static void ptr_leave(void*, struct wl_pointer*, uint32_t, struct wl_surface*);
static void ptr_motion(void*, struct wl_pointer*, uint32_t, wl_fixed_t, wl_fixed_t);
static void ptr_button(void*, struct wl_pointer*, uint32_t, uint32_t, uint32_t, uint32_t);
static void ptr_axis(void*, struct wl_pointer*, uint32_t, uint32_t, wl_fixed_t);
static const struct wl_pointer_listener ptr_listener = {
    ptr_enter, ptr_leave, ptr_motion, ptr_button, ptr_axis,
    NULL, NULL, NULL, NULL, NULL, NULL /* frame/axis_source/axis_stop/axis_discrete/axis_value120/axis_relative_direction */
};

/* ── wl_seat ─────────────────────────────────────────────────────────────── */
static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    WaylandState *ws = data;
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !ws->keyboard) {
        ws->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(ws->keyboard, &kb_listener, ws);
    }
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !ws->pointer) {
        ws->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(ws->pointer, &ptr_listener, ws);
    }
}
static void seat_name(void *d, struct wl_seat *s, const char *n) {
    (void)d;(void)s;(void)n;
}
static const struct wl_seat_listener seat_listener = { seat_capabilities, seat_name };

/* ── mouse reporting ─────────────────────────────────────────────────────── */
static void mouse_send(WaylandState *ws, int btn, int col, int row, bool release) {
    char buf[32];
    int n;
    if (ws->term->mouse_sgr) {
        n = snprintf(buf, sizeof buf, "\033[<%d;%d;%d%c",
                     btn, col, row, release ? 'm' : 'M');
    } else {
        if (col > 223 || row > 223) return;
        buf[0] = '\033'; buf[1] = '['; buf[2] = 'M';
        buf[3] = (char)(btn + 32);
        buf[4] = (char)(col + 32);
        buf[5] = (char)(row + 32);
        n = 6;
    }
    pty_write(ws->pty, buf, n);
}

/* ── clipboard / selection ───────────────────────────────────────────────── */
static char *build_sel_text(WaylandState *ws, int *lenp) {
    int x0 = ws->sel_x0, y0 = ws->sel_y0;
    int x1 = ws->sel_x1, y1 = ws->sel_y1;
    if (y0 > y1 || (y0 == y1 && x0 > x1))
        { int t=x0;x0=x1;x1=t; t=y0;y0=y1;y1=t; }

    Term *tm = ws->term;
    int max = (y1 - y0 + 1) * (tm->cols * 4 + 1) + 1;
    char *buf = malloc(max);
    if (!buf) { *lenp = 0; return NULL; }
    int pos = 0;

    for (int y = y0; y <= y1; y++) {
        int lx = (y == y0) ? x0 : 0;
        int rx = (y == y1) ? x1 : tm->cols - 1;
        /* trim trailing spaces */
        while (rx >= lx && term_cell(tm, rx, y).cp <= ' ') rx--;
        for (int x = lx; x <= rx; x++) {
            Cell c = term_cell(tm, x, y);
            if (c.attrs & ATTR_WIDE_CONT) continue;
            uint32_t cp = c.cp ? c.cp : ' ';
            if (cp < 0x80)       { buf[pos++] = (char)cp; }
            else if (cp < 0x800) { buf[pos++]=(char)(0xC0|(cp>>6));
                                   buf[pos++]=(char)(0x80|(cp&0x3F)); }
            else if (cp <0x10000){ buf[pos++]=(char)(0xE0|(cp>>12));
                                   buf[pos++]=(char)(0x80|((cp>>6)&0x3F));
                                   buf[pos++]=(char)(0x80|(cp&0x3F)); }
            else                 { buf[pos++]=(char)(0xF0|(cp>>18));
                                   buf[pos++]=(char)(0x80|((cp>>12)&0x3F));
                                   buf[pos++]=(char)(0x80|((cp>>6)&0x3F));
                                   buf[pos++]=(char)(0x80|(cp&0x3F)); }
        }
        if (y < y1) buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    *lenp = pos;
    return buf;
}

/* data-source callbacks */
static void src_target(void *d, struct wl_data_source *s, const char *m)
    { (void)d;(void)s;(void)m; }
static void src_send(void *data, struct wl_data_source *src,
                     const char *mime, int fd) {
    (void)src;(void)mime;
    WaylandState *ws = data;
    if (ws->sel_text && ws->sel_text_len > 0)
        write(fd, ws->sel_text, ws->sel_text_len);
    close(fd);
}
static void src_cancelled(void *data, struct wl_data_source *src) {
    WaylandState *ws = data;
    wl_data_source_destroy(src);
    if (ws->data_source == src) ws->data_source = NULL;
    free(ws->sel_text); ws->sel_text = NULL; ws->sel_text_len = 0;
}
static void src_dnd_drop(void *d, struct wl_data_source *s) { (void)d;(void)s; }
static void src_dnd_finished(void *d, struct wl_data_source *s) { (void)d;(void)s; }
static void src_action(void *d, struct wl_data_source *s, uint32_t a) { (void)d;(void)s;(void)a; }
static const struct wl_data_source_listener src_listener =
    { src_target, src_send, src_cancelled, src_dnd_drop, src_dnd_finished, src_action };

static void sel_copy(WaylandState *ws) {
    if (!ws->data_mgr || !ws->data_device) return;
    free(ws->sel_text);
    ws->sel_text = build_sel_text(ws, &ws->sel_text_len);
    if (!ws->sel_text || ws->sel_text_len == 0) return;

    if (ws->data_source) {
        wl_data_source_destroy(ws->data_source);
        ws->data_source = NULL;
    }
    ws->data_source = wl_data_device_manager_create_data_source(ws->data_mgr);
    if (!ws->data_source) return;
    wl_data_source_offer(ws->data_source, "text/plain;charset=utf-8");
    wl_data_source_offer(ws->data_source, "text/plain");
    wl_data_source_add_listener(ws->data_source, &src_listener, ws);
    wl_data_device_set_selection(ws->data_device, ws->data_source, ws->ptr_serial);
}

/* OSC 52 clipboard callback (called from vt.c) */
static void wayland_set_clipboard(void *data, const char *text, int len) {
    WaylandState *ws = data;
    if (!ws || !ws->data_mgr || !ws->data_device) return;
    free(ws->sel_text);
    ws->sel_text = malloc(len + 1);
    if (!ws->sel_text) return;
    memcpy(ws->sel_text, text, len);
    ws->sel_text[len] = '\0';
    ws->sel_text_len = len;

    if (ws->data_source) {
        wl_data_source_destroy(ws->data_source);
        ws->data_source = NULL;
    }
    ws->data_source = wl_data_device_manager_create_data_source(ws->data_mgr);
    if (!ws->data_source) return;
    wl_data_source_offer(ws->data_source, "text/plain;charset=utf-8");
    wl_data_source_offer(ws->data_source, "text/plain");
    wl_data_source_add_listener(ws->data_source, &src_listener, ws);
    wl_data_device_set_selection(ws->data_device, ws->data_source, ws->ptr_serial);
}

static void clipboard_paste(WaylandState *ws) {
    if (!ws->data_offer) return;
    int fds[2];
    if (pipe(fds) < 0) return;
    wl_data_offer_receive(ws->data_offer, "text/plain;charset=utf-8", fds[1]);
    close(fds[1]);
    wl_display_flush(ws->display);

    if (ws->term->brkt_paste) pty_write(ws->pty, "\033[200~", 6);
    char buf[4096];
    int n;
    while ((n = read(fds[0], buf, sizeof buf)) > 0)
        pty_write(ws->pty, buf, n);
    close(fds[0]);
    if (ws->term->brkt_paste) pty_write(ws->pty, "\033[201~", 6);
}

/* data-device callbacks */
static void dev_data_offer(void *data, struct wl_data_device *dev,
                           struct wl_data_offer *offer) {
    (void)data;(void)dev;(void)offer;
}
static void dev_enter(void *d,struct wl_data_device *dv,uint32_t s,
    struct wl_surface *sf,wl_fixed_t x,wl_fixed_t y,struct wl_data_offer *o)
    { (void)d;(void)dv;(void)s;(void)sf;(void)x;(void)y;(void)o; }
static void dev_leave(void *d,struct wl_data_device *dv)
    { (void)d;(void)dv; }
static void dev_motion(void *d,struct wl_data_device *dv,uint32_t t,
    wl_fixed_t x,wl_fixed_t y) { (void)d;(void)dv;(void)t;(void)x;(void)y; }
static void dev_drop(void *d,struct wl_data_device *dv)
    { (void)d;(void)dv; }
static void dev_selection(void *data, struct wl_data_device *dev,
                          struct wl_data_offer *offer) {
    (void)dev;
    WaylandState *ws = data;
    ws->data_offer = offer; /* NULL means clipboard cleared */
}
static const struct wl_data_device_listener dev_listener = {
    dev_data_offer, dev_enter, dev_leave, dev_motion, dev_drop, dev_selection
};

/* ── pointer / mouse ─────────────────────────────────────────────────────── */
static void ptr_enter(void *data, struct wl_pointer *ptr,
    uint32_t serial, struct wl_surface *surf,
    wl_fixed_t sx, wl_fixed_t sy) {
    (void)ptr;(void)surf;
    WaylandState *ws = data;
    ws->ptr_in  = true;
    ws->ptr_px  = wl_fixed_to_int(sx);
    ws->ptr_py  = wl_fixed_to_int(sy);
    ws->ptr_x   = ws->ptr_px / ws->font->cell_w + 1;
    ws->ptr_y   = ws->ptr_py / ws->font->cell_h + 1;
    ws->ptr_serial = serial;
}
static void ptr_leave(void *data, struct wl_pointer *ptr,
    uint32_t serial, struct wl_surface *surf) {
    (void)ptr;(void)serial;(void)surf;
    ((WaylandState *)data)->ptr_in = false;
}
static void ptr_motion(void *data, struct wl_pointer *ptr,
    uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
    (void)ptr;(void)time;
    WaylandState *ws = data;
    int px = wl_fixed_to_int(sx), py = wl_fixed_to_int(sy);
    int cx = px / ws->font->cell_w + 1;
    int cy = py / ws->font->cell_h + 1;
    if (cx < 1) cx = 1; else if (cx > ws->term->cols) cx = ws->term->cols;
    if (cy < 1) cy = 1; else if (cy > ws->term->rows) cy = ws->term->rows;
    ws->ptr_px = px; ws->ptr_py = py;
    ws->ptr_x  = cx; ws->ptr_y  = cy;

    uint8_t mode = ws->term->mouse_mode;
    if (mode == 3 || (mode == 2 && ws->ptr_buttons))
        mouse_send(ws, 32, cx, cy, false);

    if (ws->selecting) {
        ws->sel_x1 = cx - 1; ws->sel_y1 = cy - 1;
        /* mark all rows dirty in both buffers so selection redraws */
        for (int i = 0; i < 2; i++) memset(ws->buf_dirty[i], 1, ws->term->rows);
        ws->dirty = true;
    }
}
static void ptr_button(void *data, struct wl_pointer *ptr,
    uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    (void)ptr;(void)time;
    WaylandState *ws = data;
    ws->ptr_serial = serial;

    /* translate Linux button codes */
    int btn;
    switch (button) {
    case 0x110: btn = 0; break; /* BTN_LEFT   */
    case 0x111: btn = 1; break; /* BTN_MIDDLE */
    case 0x112: btn = 2; break; /* BTN_RIGHT  */
    default: return;
    }
    bool pressed = (state == WL_POINTER_BUTTON_STATE_PRESSED);
    if (pressed) ws->ptr_buttons |=  (1u << btn);
    else         ws->ptr_buttons &= ~(1u << btn);

    uint8_t mode = ws->term->mouse_mode;
    bool shift_held = ws->kb_shift;

    if (mode >= 1 && !shift_held) {
        mouse_send(ws, btn, ws->ptr_x, ws->ptr_y, !pressed);
    } else if (btn == 0) { /* left button — selection */
        if (pressed) {
            ws->sel_x0 = ws->sel_x1 = ws->ptr_x - 1;
            ws->sel_y0 = ws->sel_y1 = ws->ptr_y - 1;
            ws->selecting = true;
            ws->has_sel   = false;
            for (int i = 0; i < 2; i++) memset(ws->buf_dirty[i], 1, ws->term->rows);
            ws->dirty = true;
        } else {
            ws->selecting = false;
            ws->has_sel = (ws->sel_x0 != ws->sel_x1 || ws->sel_y0 != ws->sel_y1);
            if (ws->has_sel) sel_copy(ws);
        }
    } else if (btn == 1 && !pressed && mode == 0) { /* middle click: paste */
        clipboard_paste(ws);
    }
}
static void ptr_axis(void *data, struct wl_pointer *ptr,
    uint32_t time, uint32_t axis, wl_fixed_t value) {
    (void)ptr;(void)time;
    WaylandState *ws = data;
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
    uint8_t mode = ws->term->mouse_mode;
    if (!mode) return;
    mouse_send(ws, value < 0 ? 64 : 65, ws->ptr_x, ws->ptr_y, false);
}
/* ptr_listener defined above seat_capabilities */

/* ── registry ────────────────────────────────────────────────────────────── */
static void registry_global(void *data, struct wl_registry *reg,
    uint32_t name, const char *iface, uint32_t ver) {
    WaylandState *ws = data;
    if (!strcmp(iface, wl_compositor_interface.name))
        ws->compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, wl_shm_interface.name))
        ws->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, xdg_wm_base_interface.name)) {
        ws->xdg_wm_base = wl_registry_bind(reg, name, &xdg_wm_base_interface,
                                             ver < 4 ? ver : 4);
        xdg_wm_base_add_listener(ws->xdg_wm_base, &wm_listener, ws);
    } else if (!strcmp(iface, wl_seat_interface.name)) {
        ws->seat = wl_registry_bind(reg, name, &wl_seat_interface, 4);
        wl_seat_add_listener(ws->seat, &seat_listener, ws);
    } else if (!strcmp(iface, wl_data_device_manager_interface.name)) {
        ws->data_mgr = wl_registry_bind(reg, name,
                            &wl_data_device_manager_interface, 3);
    }
}
static void registry_global_remove(void *d, struct wl_registry *r, uint32_t n) {
    (void)d;(void)r;(void)n;
}
static const struct wl_registry_listener reg_listener = {
    registry_global, registry_global_remove
};

/* ── init ────────────────────────────────────────────────────────────────── */
WaylandState *wayland_init(Term *t, Font *f, Input *inp, Pty *pty) {
    WaylandState *ws = calloc(1, sizeof *ws);
    ws->term = t; ws->font = f; ws->input = inp; ws->pty = pty;
    ws->running = true;

    ws->rpt_delay = 400;
    ws->rpt_rate  = 0;
    ws->rpt_fd    = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    ws->blink_fd  = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (ws->blink_fd >= 0) {
        struct itimerspec its = { {0,500000000L}, {0,500000000L} };
        timerfd_settime(ws->blink_fd, 0, &its, NULL);
    }

    ws->display = wl_display_connect(NULL);
    if (!ws->display) { fputs("cannot connect to Wayland display\n", stderr); exit(1); }

    ws->registry = wl_display_get_registry(ws->display);
    wl_registry_add_listener(ws->registry, &reg_listener, ws);
    wl_display_roundtrip(ws->display);
    wl_display_roundtrip(ws->display);

    if (!ws->compositor || !ws->shm || !ws->xdg_wm_base)
        { fputs("missing Wayland globals\n", stderr); exit(1); }

    ws->width  = f->cell_w * t->cols;
    ws->height = f->cell_h * t->rows;

    for (int i = 0; i < 2; i++)
        alloc_buffer(ws, &ws->buffers[i], ws->width, ws->height);

    for (int i = 0; i < 2; i++) {
        ws->buf_dirty[i] = malloc(t->rows);
        memset(ws->buf_dirty[i], 1, t->rows); /* all rows need first render */
    }

    /* clipboard data device */
    if (ws->data_mgr && ws->seat) {
        ws->data_device = wl_data_device_manager_get_data_device(
                              ws->data_mgr, ws->seat);
        wl_data_device_add_listener(ws->data_device, &dev_listener, ws);
    }

    /* wire all terminal callbacks before any roundtrip that could dispatch events */
    t->pty_data       = ws;
    t->pty_write      = wayland_pty_write;
    t->set_clipboard  = wayland_set_clipboard;
    t->clipboard_data = ws;

    ws->surface     = wl_compositor_create_surface(ws->compositor);
    ws->xdg_surface = xdg_wm_base_get_xdg_surface(ws->xdg_wm_base, ws->surface);
    xdg_surface_add_listener(ws->xdg_surface, &xdg_surface_listener, ws);

    ws->xdg_toplevel = xdg_surface_get_toplevel(ws->xdg_surface);
    xdg_toplevel_add_listener(ws->xdg_toplevel, &toplevel_listener, ws);

    wl_surface_commit(ws->surface);
    wl_display_roundtrip(ws->display);

    return ws;
}

/* ── pty write callback ──────────────────────────────────────────────────── */
void wayland_pty_write(void *data, const char *buf, int len) {
    pty_write(((WaylandState *)data)->pty, buf, len);
}

/* ── main event loop ─────────────────────────────────────────────────────── */
void wayland_run(WaylandState *ws) {
    int wl_fd  = wl_display_get_fd(ws->display);
    int pty_fd = ws->pty->fd;

    struct pollfd fds[4] = {
        { .fd = wl_fd,        .events = POLLIN },
        { .fd = pty_fd,       .events = POLLIN },
        { .fd = ws->rpt_fd,   .events = (ws->rpt_fd   >= 0) ? POLLIN : 0 },
        { .fd = ws->blink_fd, .events = (ws->blink_fd >= 0) ? POLLIN : 0 },
    };

    while (ws->running) {
        if (wl_display_flush(ws->display) < 0 && errno != EAGAIN) break;

        int ret = poll(fds, 4, -1);
        if (ret < 0) { if (errno == EINTR) continue; break; }

        /* cursor blink */
        if (ws->blink_fd >= 0 && (fds[3].revents & POLLIN)) {
            uint64_t exp;
            read(ws->blink_fd, &exp, sizeof exp);
            uint8_t shape = ws->term->cursor_shape;
            if (shape == 0 || shape == 1 || shape == 3 || shape == 5) {
                ws->term->cursor_blink_on = !ws->term->cursor_blink_on;
                ws->term->dirty[ws->term->screen][ws->term->cy] = true;
                ws->dirty = true;
            }
        }

        if (fds[0].revents & POLLIN) {
            if (wl_display_dispatch(ws->display) < 0) break;
        }

        if (fds[1].revents & POLLIN) {
            char buf[4096];
            int n;
            int prev_cy = ws->term->cy;
            while ((n = read(pty_fd, buf, sizeof buf)) > 0) {
                term_process(ws->term, buf, n);
                bool *d = ws->term->dirty[ws->term->screen];
                for (int y = 0; y < ws->term->rows && !ws->dirty; y++)
                    if (d[y]) ws->dirty = true;
            }
            ws->term->dirty[ws->term->screen][prev_cy] = true;
            ws->term->dirty[ws->term->screen][ws->term->cy] = true;
            ws->dirty = true;
        }

        /* key repeat timer fired */
        if (ws->rpt_fd >= 0 && (fds[2].revents & POLLIN)) {
            uint64_t expirations;
            read(ws->rpt_fd, &expirations, sizeof expirations);
            char buf[32];
            int n = input_key(ws->input, ws->rpt_key,
                              WL_KEYBOARD_KEY_STATE_PRESSED,
                              ws->term->app_cursor, buf);
            if (n > 0) pty_write(ws->pty, buf, n);
        }

        if (fds[1].revents & (POLLHUP | POLLERR))
            ws->running = false;

        if (ws->dirty && !ws->frame_pending && ws->configured)
            do_render(ws);
    }
}

void wayland_free(WaylandState *ws) {
    if (ws->rpt_fd   >= 0) close(ws->rpt_fd);
    if (ws->blink_fd >= 0) close(ws->blink_fd);
    free(ws->sel_text);
    if (ws->data_source)  wl_data_source_destroy(ws->data_source);
    if (ws->data_device)  wl_data_device_destroy(ws->data_device);
    if (ws->data_mgr)     wl_data_device_manager_destroy(ws->data_mgr);
    if (ws->pointer)      wl_pointer_destroy(ws->pointer);
    for (int i = 0; i < 2; i++) free(ws->buf_dirty[i]);
    for (int i = 0; i < 2; i++) {
        if (ws->buffers[i].wl_buf) wl_buffer_destroy(ws->buffers[i].wl_buf);
        if (ws->buffers[i].data)   munmap(ws->buffers[i].data, ws->buffers[i].size);
        if (ws->buffers[i].fd >= 0) close(ws->buffers[i].fd);
    }
    if (ws->keyboard)     wl_keyboard_destroy(ws->keyboard);
    if (ws->seat)         wl_seat_destroy(ws->seat);
    if (ws->xdg_toplevel) xdg_toplevel_destroy(ws->xdg_toplevel);
    if (ws->xdg_surface)  xdg_surface_destroy(ws->xdg_surface);
    if (ws->surface)      wl_surface_destroy(ws->surface);
    if (ws->xdg_wm_base)  xdg_wm_base_destroy(ws->xdg_wm_base);
    if (ws->shm)          wl_shm_destroy(ws->shm);
    if (ws->compositor)   wl_compositor_destroy(ws->compositor);
    if (ws->registry)     wl_registry_destroy(ws->registry);
    wl_display_disconnect(ws->display);
    free(ws);
}
