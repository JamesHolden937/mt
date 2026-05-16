#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "config.h"

/* cell attribute bits */
#define ATTR_BOLD      (1u<<0)
#define ATTR_ITALIC    (1u<<1)
#define ATTR_UNDERLINE (1u<<2)
#define ATTR_REVERSE   (1u<<3)
#define ATTR_INVISIBLE (1u<<4)
#define ATTR_STRIKE    (1u<<5)
#define ATTR_WIDE      (1u<<6)  /* left half of a double-width character */
#define ATTR_WIDE_CONT (1u<<7)  /* right-half placeholder, skip rendering */

#define TAB_MAX 512             /* max columns tracked for tab stops */

typedef struct {
    uint32_t cp;
    uint32_t fg;
    uint32_t bg;
    uint16_t attrs;  /* ATTR_* flags; uint16_t keeps Cell at 16 bytes */
} Cell;

typedef enum {
    VT_NORMAL, VT_ESC, VT_ESC_CS0, VT_ESC_CS1,
    VT_CSI, VT_CSI_INTERM, VT_CSI_IGNORE, VT_OSC, VT_DCS,
} VtState;

#define MAX_PARAMS 16
#define MAX_OSC    512

typedef struct Term Term;
struct Term {
    int cols, rows;

    Cell *cells[2];
    bool *dirty[2];
    int   screen;

    /* cursor */
    int      cx, cy;
    uint32_t fg, bg;
    uint16_t attrs;
    bool     cursor_visible;
    uint8_t  cursor_shape;    /* DECSCUSR: 0/1/2=block 3/4=underline 5/6=beam */
    bool     cursor_blink_on; /* toggled by blink timer; hide cursor when false */
    bool     autowrap;
    bool     pending_wrap;

    /* saved cursor (per screen) */
    int      saved_cx[2], saved_cy[2];
    uint32_t saved_fg[2], saved_bg[2];
    uint16_t saved_attrs[2];

    /* scroll region (0-based, inclusive) */
    int scroll_top, scroll_bot;

    /* terminal modes */
    bool app_cursor;    /* DECCKM: arrows send ESC O x instead of ESC [ x */
    bool insert_mode;   /* IRM: shift right on character put */
    bool origin_mode;   /* DECOM (?6): cursor row relative to scroll region */
    uint8_t mouse_mode; /* 0=off 1=X10(1000) 2=btn(1002) 3=all(1003) */
    bool mouse_sgr;     /* ?1006: SGR-encoded mouse events */
    bool brkt_paste;    /* ?2004: bracketed paste mode */
    bool focus_events;  /* ?1004: send focus in/out to pty */

    /* character sets: 0=ASCII, 1=DEC special graphics */
    uint8_t charset_g[2];
    int     charset_gl; /* active charset slot: 0=G0, 1=G1 */

    /* last printed codepoint, for REP (CSI b) */
    uint32_t last_cp;

    /* tab stop bitset */
    uint8_t tab_stops[TAB_MAX / 8];

    /* utf-8 accumulator */
    uint32_t utf8_cp;
    int      utf8_rem;

    /* vt parser */
    VtState vt_state;
    int     params[MAX_PARAMS];
    int     nparams;
    int     cur_param;
    bool    have_param;
    char    csi_mod;    /* CSI private-param prefix: '?', '>', '<', '=' or '\0' */
    char    csi_interm; /* CSI intermediate byte, e.g. SP for DECSCUSR */
    char    osc_buf[MAX_OSC];
    int     osc_len;
    int     osc_utf8_rem; /* bytes remaining in current UTF-8 sequence inside OSC */

    /* pty write callback (set by main) */
    void *pty_data;
    void (*pty_write)(void *data, const char *buf, int len);

    /* OSC 52 clipboard callback (set by wayland layer) */
    void (*set_clipboard)(void *data, const char *text, int len);
    void *clipboard_data;
};

static inline Cell *term_cells(Term *t)  { return t->cells[t->screen]; }
static inline bool *term_dirty(Term *t)  { return t->dirty[t->screen]; }
static inline Cell  term_cell(Term *t, int x, int y)
    { return t->cells[t->screen][y * t->cols + x]; }
static inline Cell *term_cellp(Term *t, int x, int y)
    { return &t->cells[t->screen][y * t->cols + x]; }

uint32_t palette256(int n);
