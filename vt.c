#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "vt.h"
#include "mt.h"

/* ── 256-colour palette ──────────────────────────────────────────────────── */
static const uint32_t ansi16[16] = {
    ANSI_COLOR_0,  ANSI_COLOR_1,  ANSI_COLOR_2,  ANSI_COLOR_3,
    ANSI_COLOR_4,  ANSI_COLOR_5,  ANSI_COLOR_6,  ANSI_COLOR_7,
    ANSI_COLOR_8,  ANSI_COLOR_9,  ANSI_COLOR_10, ANSI_COLOR_11,
    ANSI_COLOR_12, ANSI_COLOR_13, ANSI_COLOR_14, ANSI_COLOR_15,
};

uint32_t palette256(int n) {
    if (n < 16) return ansi16[n];
    if (n < 232) {
        n -= 16;
        int b = n % 6; n /= 6;
        int g = n % 6; n /= 6;
        int r = n % 6;
        return ((r ? r * 40 + 55 : 0) << 16)
             | ((g ? g * 40 + 55 : 0) <<  8)
             |  (b ? b * 40 + 55 : 0);
    }
    uint32_t v = (n - 232) * 10 + 8;
    return (v << 16) | (v << 8) | v;
}

/* ── DEC special graphics charset (replaces 0x60–0x7E) ──────────────────── */
static const uint32_t dec_special[31] = {
    /* 0x60 */ 0x25C6, /* ◆ diamond */
    /* 0x61 */ 0x2592, /* ▒ checker */
    /* 0x62 */ 0x2409, /* HT symbol */
    /* 0x63 */ 0x240C, /* FF symbol */
    /* 0x64 */ 0x240D, /* CR symbol */
    /* 0x65 */ 0x240A, /* LF symbol */
    /* 0x66 */ 0x00B0, /* ° degree   */
    /* 0x67 */ 0x00B1, /* ± plus-minus */
    /* 0x68 */ 0x2424, /* NL symbol  */
    /* 0x69 */ 0x240B, /* VT symbol  */
    /* 0x6A */ 0x2518, /* ┘ */
    /* 0x6B */ 0x2510, /* ┐ */
    /* 0x6C */ 0x250C, /* ┌ */
    /* 0x6D */ 0x2514, /* └ */
    /* 0x6E */ 0x253C, /* ┼ */
    /* 0x6F */ 0x23BA, /* ⎺ scan 1 */
    /* 0x70 */ 0x23BB, /* ⎻ scan 3 */
    /* 0x71 */ 0x2500, /* ─ */
    /* 0x72 */ 0x23BC, /* ⎼ scan 7 */
    /* 0x73 */ 0x23BD, /* ⎽ scan 9 */
    /* 0x74 */ 0x251C, /* ├ */
    /* 0x75 */ 0x2524, /* ┤ */
    /* 0x76 */ 0x2534, /* ┴ */
    /* 0x77 */ 0x252C, /* ┬ */
    /* 0x78 */ 0x2502, /* │ */
    /* 0x79 */ 0x2264, /* ≤ */
    /* 0x7A */ 0x2265, /* ≥ */
    /* 0x7B */ 0x03C0, /* π */
    /* 0x7C */ 0x2260, /* ≠ */
    /* 0x7D */ 0x00A3, /* £ */
    /* 0x7E */ 0x00B7, /* · middle dot */
};

/* ── Unicode helpers ─────────────────────────────────────────────────────── */
static bool is_wide(uint32_t cp) {
    if (cp < 0x1100) return false;
    if (cp <= 0x115F) return true;  /* Hangul Jamo */
    if (cp == 0x2329 || cp == 0x232A) return true;
    if (cp >= 0x2E80 && cp <= 0x303E) return true;
    if (cp >= 0x3040 && cp <= 0xA4CF) return true;
    if (cp >= 0xA960 && cp <= 0xA97C) return true;
    if (cp >= 0xAC00 && cp <= 0xD7A3) return true;
    if (cp >= 0xF900 && cp <= 0xFAFF) return true;
    if (cp >= 0xFE10 && cp <= 0xFE19) return true;
    if (cp >= 0xFE30 && cp <= 0xFE6F) return true;
    if (cp >= 0xFF01 && cp <= 0xFF60) return true;
    if (cp >= 0xFFE0 && cp <= 0xFFE6) return true;
    if (cp >= 0x1B000 && cp <= 0x1B12F) return true;
    if (cp >= 0x1F200 && cp <= 0x1FFFF) return true;
    if (cp >= 0x20000 && cp <= 0x2FFFD) return true;
    if (cp >= 0x30000 && cp <= 0x3FFFD) return true;
    return false;
}

static bool is_combining(uint32_t cp) {
    if (cp >= 0x0300 && cp <= 0x036F) return true;
    if (cp >= 0x0610 && cp <= 0x061A) return true;
    if (cp >= 0x064B && cp <= 0x065F) return true;
    if (cp >= 0x1AB0 && cp <= 0x1AFF) return true;
    if (cp >= 0x1DC0 && cp <= 0x1DFF) return true;
    if (cp >= 0x20D0 && cp <= 0x20FF) return true;
    if (cp >= 0xFE20 && cp <= 0xFE2F) return true;
    if (cp == 0x200B || cp == 0x200C || cp == 0x200D) return true;
    if (cp == 0x00AD || cp == 0xFEFF) return true;
    return false;
}

/* ── tab stop helpers ────────────────────────────────────────────────────── */
static bool tab_get(const Term *t, int x) {
    if (x < 0 || x >= TAB_MAX) return false;
    return (t->tab_stops[x >> 3] >> (x & 7)) & 1;
}
static void tab_set(Term *t, int x) {
    if (x >= 0 && x < TAB_MAX) t->tab_stops[x >> 3] |= (uint8_t)(1u << (x & 7));
}
static void tab_clr(Term *t, int x) {
    if (x >= 0 && x < TAB_MAX) t->tab_stops[x >> 3] &= (uint8_t)~(1u << (x & 7));
}
static void tab_defaults(Term *t) {
    memset(t->tab_stops, 0, sizeof t->tab_stops);
    for (int x = 0; x < TAB_MAX; x += 8) tab_set(t, x);
}

/* ── allocation helpers ─────────────────────────────────────────────────── */
static Cell blank_cell(Term *t) {
    return (Cell){ .cp = ' ', .fg = t->fg, .bg = t->bg, .attrs = 0 };
}

static void fill_cells(Cell *c, Cell v, int n) {
    for (int i = 0; i < n; i++) c[i] = v;
}

/* break a wide character that straddles position x on row y */
static void break_wide_at(Term *t, int x, int y) {
    if (x < 0 || x >= t->cols) return;
    Cell *c = term_cellp(t, x, y);
    Cell blank = blank_cell(t);
    if (c->attrs & ATTR_WIDE) {
        c->attrs &= (uint16_t)~ATTR_WIDE;
        if (x + 1 < t->cols) *term_cellp(t, x + 1, y) = blank;
    } else if (c->attrs & ATTR_WIDE_CONT) {
        c->attrs &= (uint16_t)~ATTR_WIDE_CONT;
        if (x > 0) {
            Cell *main = term_cellp(t, x - 1, y);
            if (main->attrs & ATTR_WIDE) *main = blank;
        }
    }
}

/* ── scroll within region ────────────────────────────────────────────────── */
static void scroll_up(Term *t, int top, int bot, int n) {
    if (n <= 0 || top > bot) return;
    Cell *cells = t->cells[t->screen];
    bool *dirty = t->dirty[t->screen];
    int move = bot - top - n + 1;
    if (move > 0)
        memmove(&cells[top * t->cols], &cells[(top + n) * t->cols],
                (size_t)move * t->cols * sizeof(Cell));
    Cell blank = blank_cell(t);
    for (int y = bot - n + 1; y <= bot; y++)
        fill_cells(&cells[y * t->cols], blank, t->cols);
    for (int y = top; y <= bot; y++) dirty[y] = true;
}

static void scroll_down(Term *t, int top, int bot, int n) {
    if (n <= 0 || top > bot) return;
    Cell *cells = t->cells[t->screen];
    bool *dirty = t->dirty[t->screen];
    int move = bot - top - n + 1;
    if (move > 0)
        memmove(&cells[(top + n) * t->cols], &cells[top * t->cols],
                (size_t)move * t->cols * sizeof(Cell));
    Cell blank = blank_cell(t);
    for (int y = top; y < top + n; y++)
        fill_cells(&cells[y * t->cols], blank, t->cols);
    for (int y = top; y <= bot; y++) dirty[y] = true;
}

/* ── cursor ──────────────────────────────────────────────────────────────── */
static void clamp_cursor(Term *t) {
    int min_y = t->origin_mode ? t->scroll_top : 0;
    int max_y = t->origin_mode ? t->scroll_bot : t->rows - 1;
    if (t->cx < 0) t->cx = 0;
    if (t->cy < min_y) t->cy = min_y;
    if (t->cx >= t->cols) t->cx = t->cols - 1;
    if (t->cy > max_y) t->cy = max_y;
}

static void newline(Term *t) {
    if (t->cy == t->scroll_bot)
        scroll_up(t, t->scroll_top, t->scroll_bot, 1);
    else if (t->cy < t->rows - 1)
        t->cy++;
}

/* ── erase helpers ───────────────────────────────────────────────────────── */
static void erase_cells(Term *t, int x, int y, int n) {
    if (n <= 0) return;
    if (x + n > t->cols) n = t->cols - x;
    Cell *row = &t->cells[t->screen][y * t->cols + x];
    Cell blank = blank_cell(t);
    for (int i = 0; i < n; i++) row[i] = blank;
    t->dirty[t->screen][y] = true;
}

static void erase_line(Term *t, int y) {
    erase_cells(t, 0, y, t->cols);
}

/* ── print a codepoint at cursor ─────────────────────────────────────────── */
static void term_putcp(Term *t, uint32_t cp) {
    /* DEC special graphics character set mapping */
    if (t->charset_g[t->charset_gl] == 1 && cp >= 0x60 && cp <= 0x7E)
        cp = dec_special[cp - 0x60];

    /* combining marks: discard (no compositing support yet) */
    if (is_combining(cp)) return;

    bool wide = is_wide(cp);

    /* resolve any pending wrap */
    if (t->pending_wrap && t->autowrap) {
        if (wide) {
            /* pad last column so a wide char always starts on an even pair */
            Cell *pad = term_cellp(t, t->cx, t->cy);
            *pad = blank_cell(t);
            t->dirty[t->screen][t->cy] = true;
        }
        t->cx = 0;
        newline(t);
        t->pending_wrap = false;
    }

    /* if wide char would start in the very last column, wrap it cleanly */
    if (wide && t->cx == t->cols - 1 && t->cols > 1 && t->autowrap) {
        *term_cellp(t, t->cx, t->cy) = blank_cell(t);
        t->dirty[t->screen][t->cy] = true;
        t->cx = 0;
        newline(t);
    }

    /* break any wide char we are about to overwrite */
    break_wide_at(t, t->cx, t->cy);
    if (wide && t->cx + 1 < t->cols)
        break_wide_at(t, t->cx + 1, t->cy);

    /* insert mode: shift cells right */
    if (t->insert_mode) {
        Cell *row = &t->cells[t->screen][t->cy * t->cols];
        int n = wide ? 2 : 1;
        if (t->cx + n < t->cols)
            memmove(&row[t->cx + n], &row[t->cx],
                    (size_t)(t->cols - t->cx - n) * sizeof(Cell));
        t->dirty[t->screen][t->cy] = true;
    }

    /* write the cell */
    Cell *c = term_cellp(t, t->cx, t->cy);
    c->cp    = cp;
    c->fg    = (t->attrs & ATTR_REVERSE) ? t->bg : t->fg;
    c->bg    = (t->attrs & ATTR_REVERSE) ? t->fg : t->bg;
    c->attrs = (uint16_t)(t->attrs & ~(ATTR_REVERSE | ATTR_WIDE | ATTR_WIDE_CONT));
    if (t->attrs & ATTR_INVISIBLE) c->fg = c->bg;
    t->dirty[t->screen][t->cy] = true;
    t->last_cp = cp;

    if (wide && t->cx + 1 < t->cols) {
        c->attrs |= ATTR_WIDE;
        Cell *c2 = term_cellp(t, t->cx + 1, t->cy);
        c2->cp    = 0;
        c2->fg    = c->fg;
        c2->bg    = c->bg;
        c2->attrs = (uint16_t)((c->attrs & ~ATTR_WIDE) | ATTR_WIDE_CONT);
    }

    int advance = wide ? 2 : 1;
    if (t->cx >= t->cols - advance)
        t->pending_wrap = true;
    else
        t->cx += advance;
}

/* ── utf-8 decoder ───────────────────────────────────────────────────────── */
static int utf8_feed(Term *t, unsigned char byte) {
    if (byte < 0x80) {
        t->utf8_rem = 0;
        return (int)byte;
    }
    if (byte < 0xC0) {
        if (t->utf8_rem > 0) {
            t->utf8_cp = (t->utf8_cp << 6) | (byte & 0x3F);
            if (--t->utf8_rem == 0) return (int)t->utf8_cp;
        } else {
            t->utf8_rem = 0; /* discard stray continuation byte */
        }
        return -1;
    }
    if (byte < 0xE0)      { t->utf8_rem = 1; t->utf8_cp = byte & 0x1F; }
    else if (byte < 0xF0) { t->utf8_rem = 2; t->utf8_cp = byte & 0x0F; }
    else                  { t->utf8_rem = 3; t->utf8_cp = byte & 0x07; }
    return -1;
}

/* ── CSI parameter helpers ───────────────────────────────────────────────── */
static int param(Term *t, int i, int def) {
    if (i >= t->nparams) return def;
    return t->params[i] ? t->params[i] : def;
}

static void push_param(Term *t) {
    if (t->nparams < MAX_PARAMS)
        t->params[t->nparams++] = t->cur_param;
    t->cur_param = 0;
    t->have_param = false;
}

/* ── CSI dispatch ────────────────────────────────────────────────────────── */
static void csi_dispatch(Term *t, char final) {
    if (t->have_param || t->nparams == 0) push_param(t);

    switch (final) {
    /* cursor movement */
    case 'A': t->cy -= param(t,0,1); t->pending_wrap=false; clamp_cursor(t); break;
    case 'B': t->cy += param(t,0,1); t->pending_wrap=false; clamp_cursor(t); break;
    case 'C': t->cx += param(t,0,1); t->pending_wrap=false; clamp_cursor(t); break;
    case 'D': t->cx -= param(t,0,1); t->pending_wrap=false; clamp_cursor(t); break;
    case 'E': t->cy += param(t,0,1); t->cx=0; t->pending_wrap=false; clamp_cursor(t); break;
    case 'F': t->cy -= param(t,0,1); t->cx=0; t->pending_wrap=false; clamp_cursor(t); break;
    case 'G': t->cx = param(t,0,1)-1; t->pending_wrap=false; clamp_cursor(t); break;
    case '`': t->cx = param(t,0,1)-1; t->pending_wrap=false; clamp_cursor(t); break; /* HPA */
    case 'd': t->cy = param(t,0,1)-1 + (t->origin_mode ? t->scroll_top : 0); t->pending_wrap=false; clamp_cursor(t); break; /* VPA */
    case 'e': t->cy += param(t,0,1); t->pending_wrap=false; clamp_cursor(t); break;  /* VPR */
    case 'a': t->cx += param(t,0,1); t->pending_wrap=false; clamp_cursor(t); break;  /* HPR */
    case 'H': case 'f':
        t->cy = param(t,0,1)-1 + (t->origin_mode ? t->scroll_top : 0);
        t->cx = param(t,1,1)-1;
        t->pending_wrap = false;
        clamp_cursor(t);
        break;

    /* tab navigation */
    case 'I': { /* CHT: cursor forward tab */
        int n = param(t,0,1);
        for (int i = 0; i < n; i++) {
            int nx = t->cx + 1;
            while (nx < t->cols - 1 && !tab_get(t, nx)) nx++;
            t->cx = nx;
        }
        t->pending_wrap = false;
        break;
    }
    case 'Z': { /* CBT: cursor backward tab */
        int n = param(t,0,1);
        for (int i = 0; i < n; i++) {
            int nx = t->cx - 1;
            while (nx > 0 && !tab_get(t, nx)) nx--;
            if (nx < 0) nx = 0;
            t->cx = nx;
        }
        t->pending_wrap = false;
        break;
    }

    /* repeat last character */
    case 'b': {
        int n = param(t,0,1);
        for (int i = 0; i < n && t->last_cp; i++)
            term_putcp(t, t->last_cp);
        break;
    }

    /* erase */
    case 'J': {
        int p = param(t,0,0);
        if (p == 0 || p == 3) {
            erase_cells(t, t->cx, t->cy, t->cols - t->cx);
            for (int y = t->cy+1; y < t->rows; y++) erase_line(t, y);
        } else if (p == 1) {
            for (int y = 0; y < t->cy; y++) erase_line(t, y);
            erase_cells(t, 0, t->cy, t->cx + 1);
        } else if (p == 2) {
            for (int y = 0; y < t->rows; y++) erase_line(t, y);
        }
        break;
    }
    case 'K': {
        int p = param(t,0,0);
        if (p == 0) erase_cells(t, t->cx, t->cy, t->cols - t->cx);
        else if (p == 1) erase_cells(t, 0, t->cy, t->cx + 1);
        else erase_line(t, t->cy);
        break;
    }
    case 'X': erase_cells(t, t->cx, t->cy, param(t,0,1)); break;

    /* tab clear */
    case 'g': {
        int p = param(t,0,0);
        if (p == 0) tab_clr(t, t->cx);
        else if (p == 3) memset(t->tab_stops, 0, sizeof t->tab_stops);
        break;
    }

    /* insert/delete lines */
    case 'L': scroll_down(t, t->cy, t->scroll_bot, param(t,0,1)); break;
    case 'M': scroll_up(t,   t->cy, t->scroll_bot, param(t,0,1)); break;

    /* delete/insert characters */
    case 'P': {
        int n = param(t,0,1);
        Cell *row = &t->cells[t->screen][t->cy * t->cols];
        int keep = t->cols - t->cx - n;
        if (keep > 0) memmove(&row[t->cx], &row[t->cx+n], (size_t)keep*sizeof(Cell));
        erase_cells(t, t->cols - n, t->cy, n);
        break;
    }
    case '@': {
        int n = param(t,0,1);
        Cell *row = &t->cells[t->screen][t->cy * t->cols];
        int keep = t->cols - t->cx - n;
        if (keep > 0) memmove(&row[t->cx+n], &row[t->cx], (size_t)keep*sizeof(Cell));
        erase_cells(t, t->cx, t->cy, n);
        break;
    }

    /* scroll */
    case 'S': scroll_up(t,   t->scroll_top, t->scroll_bot, param(t,0,1)); break;
    case 'T': scroll_down(t, t->scroll_top, t->scroll_bot, param(t,0,1)); break;

    /* set scroll region (DECSTBM) */
    case 'r':
        if (t->csi_mod == '\0') {
            t->scroll_top = param(t,0,1) - 1;
            t->scroll_bot = param(t,1,t->rows) - 1;
            if (t->scroll_top < 0) t->scroll_top = 0;
            if (t->scroll_bot >= t->rows) t->scroll_bot = t->rows - 1;
            if (t->scroll_top >= t->scroll_bot) {
                t->scroll_top = 0; t->scroll_bot = t->rows - 1;
            }
            t->cx = 0; t->cy = 0;
        }
        break;

    /* save/restore cursor */
    case 's':
        if (t->csi_mod == '\0') {
            t->saved_cx[t->screen] = t->cx; t->saved_cy[t->screen] = t->cy;
            t->saved_fg[t->screen] = t->fg; t->saved_bg[t->screen] = t->bg;
            t->saved_attrs[t->screen] = t->attrs;
        }
        break;
    case 'u':
        if (t->csi_mod == '\0') {
            t->cx    = t->saved_cx[t->screen];
            t->cy    = t->saved_cy[t->screen];
            t->fg    = t->saved_fg[t->screen];
            t->bg    = t->saved_bg[t->screen];
            t->attrs = t->saved_attrs[t->screen];
            clamp_cursor(t);
        }
        break;

    /* SGR */
    case 'm': {
        int i = 0;
        if (t->nparams == 1 && t->params[0] == 0) goto sgr_reset;
        do {
            int p0 = t->params[i];
            switch (p0) {
            case 0: sgr_reset:
                t->fg = DEFAULT_FG; t->bg = DEFAULT_BG; t->attrs = 0; break;
            case 1: t->attrs |=  ATTR_BOLD;      break;
            case 2: t->attrs |=  ATTR_DIM;        break;
            case 3: t->attrs |=  ATTR_ITALIC;    break;
            case 4: t->attrs |=  ATTR_UNDERLINE;  break;
            case 5: case 6: /* blink — ignore */   break;
            case 7: t->attrs |=  ATTR_REVERSE;   break;
            case 8: t->attrs |=  ATTR_INVISIBLE; break;
            case 9: t->attrs |=  ATTR_STRIKE;    break;
            case 22: t->attrs &= (uint16_t)~(ATTR_BOLD | ATTR_DIM); break;
            case 23: t->attrs &= (uint16_t)~ATTR_ITALIC;    break;
            case 24: t->attrs &= (uint16_t)~ATTR_UNDERLINE;  break;
            case 27: t->attrs &= (uint16_t)~ATTR_REVERSE;   break;
            case 28: t->attrs &= (uint16_t)~ATTR_INVISIBLE;  break;
            case 29: t->attrs &= (uint16_t)~ATTR_STRIKE;    break;
            case 39: t->fg = DEFAULT_FG; break;
            case 49: t->bg = DEFAULT_BG; break;
            case 38: case 48: {
                int target = (p0 == 38);
                if (i+1 < t->nparams && t->params[i+1] == 5 && i+2 < t->nparams) {
                    uint32_t c = palette256(t->params[i+2]);
                    if (target) t->fg = c; else t->bg = c;
                    i += 2;
                } else if (i+1 < t->nparams && t->params[i+1] == 2 && i+4 < t->nparams) {
                    uint32_t c = ((uint32_t)t->params[i+2] << 16)
                               | ((uint32_t)t->params[i+3] <<  8)
                               |  (uint32_t)t->params[i+4];
                    if (target) t->fg = c; else t->bg = c;
                    i += 4;
                }
                break;
            }
            default:
                if (p0 >= 30 && p0 <= 37) t->fg = ansi16[p0-30];
                else if (p0 >= 40 && p0 <= 47) t->bg = ansi16[p0-40];
                else if (p0 >= 90 && p0 <= 97) t->fg = ansi16[p0-90+8];
                else if (p0 >= 100 && p0 <= 107) t->bg = ansi16[p0-100+8];
                break;
            }
        } while (++i < t->nparams);
        break;
    }

    /* device attributes */
    case 'c':
        if (t->csi_mod == '>') {
            if (t->pty_write) /* DA2: VT100-like, version 276 */
                t->pty_write(t->pty_data, "\x1b[>0;276;0c", 11);
        } else {
            if (t->pty_write) /* DA1: VT220 with ANSI color */
                t->pty_write(t->pty_data, "\x1b[?62;1;22c", 11);
        }
        break;

    /* device status report */
    case 'n':
        if (t->csi_mod == '\0' && param(t,0,0) == 6 && t->pty_write) {
            char buf[32];
            int len = snprintf(buf, sizeof buf, "\x1b[%d;%dR", t->cy+1, t->cx+1);
            t->pty_write(t->pty_data, buf, len);
        }
        break;

    /* cursor visibility / private/standard modes */
    case 'h': case 'l': {
        bool set = (final == 'h');
        if (t->csi_mod == '?') {
            for (int i = 0; i < t->nparams; i++) {
                switch (t->params[i]) {
                case 1:   t->app_cursor = set; break;
                case 6:
                    t->origin_mode = set;
                    t->cx = 0;
                    t->cy = set ? t->scroll_top : 0;
                    t->pending_wrap = false;
                    break;
                case 7:   t->autowrap = set; break;
                case 12:  /* cursor blink — ignore */ break;
                case 25:
                    t->cursor_visible = set;
                    t->dirty[t->screen][t->cy] = true;
                    break;
                case 1000: t->mouse_mode = set ? 1 : 0; break;
                case 1002: t->mouse_mode = set ? 2 : 0; break;
                case 1003: t->mouse_mode = set ? 3 : 0; break;
                case 1004: t->focus_events = set; break;
                case 1006: t->mouse_sgr = set; break;
                case 2004: t->brkt_paste = set; break;
                case 47: case 1047:
                    if (set && t->screen == 0) {
                        t->screen = 1;
                        for (int y = 0; y < t->rows; y++) erase_line(t, y);
                        t->cx = 0; t->cy = 0;
                        term_mark_all_dirty(t);
                    } else if (!set && t->screen == 1) {
                        t->screen = 0;
                        term_mark_all_dirty(t);
                    }
                    break;
                case 1049:
                    if (set && t->screen == 0) {
                        t->saved_cx[0] = t->cx; t->saved_cy[0] = t->cy;
                        t->screen = 1;
                        for (int y = 0; y < t->rows; y++) erase_line(t, y);
                        t->cx = 0; t->cy = 0;
                        term_mark_all_dirty(t);
                    } else if (!set && t->screen == 1) {
                        t->screen = 0;
                        t->cx = t->saved_cx[0]; t->cy = t->saved_cy[0];
                        term_mark_all_dirty(t);
                    }
                    break;
                /* mouse and paste handled above */
                }
            }
        } else {
            for (int i = 0; i < t->nparams; i++) {
                switch (t->params[i]) {
                case 4:  t->insert_mode = set; break; /* IRM */
                case 20: /* LNM — ignore */ break;
                }
            }
        }
        break;
    }

    default: break;
    }
}

/* ── ESC dispatch ────────────────────────────────────────────────────────── */
static void esc_dispatch(Term *t, char c) {
    switch (c) {
    case 'M': /* RI — reverse index */
        if (t->cy == t->scroll_top)
            scroll_down(t, t->scroll_top, t->scroll_bot, 1);
        else if (t->cy > 0)
            t->cy--;
        break;
    case '7': /* DECSC — save cursor */
        t->saved_cx[t->screen] = t->cx; t->saved_cy[t->screen] = t->cy;
        t->saved_fg[t->screen] = t->fg; t->saved_bg[t->screen] = t->bg;
        t->saved_attrs[t->screen] = t->attrs;
        break;
    case '8': /* DECRC — restore cursor */
        t->cx    = t->saved_cx[t->screen];
        t->cy    = t->saved_cy[t->screen];
        t->fg    = t->saved_fg[t->screen];
        t->bg    = t->saved_bg[t->screen];
        t->attrs = t->saved_attrs[t->screen];
        clamp_cursor(t);
        break;
    case 'H': /* HTS — set tab stop at current column */
        tab_set(t, t->cx);
        break;
    case 'Z': /* DECID — respond same as DA1 */
        if (t->pty_write)
            t->pty_write(t->pty_data, "\x1b[?62;1;22c", 11);
        break;
    case 'c': /* RIS — full reset */
        t->cx = t->cy = 0;
        t->fg = DEFAULT_FG; t->bg = DEFAULT_BG; t->attrs = 0;
        t->cursor_visible = true; t->autowrap = true;
        t->scroll_top = 0; t->scroll_bot = t->rows - 1;
        t->app_cursor = false; t->insert_mode = false;
        t->charset_g[0] = t->charset_g[1] = 0; t->charset_gl = 0;
        tab_defaults(t);
        for (int y = 0; y < t->rows; y++) erase_line(t, y);
        term_mark_all_dirty(t);
        break;
    case '=': t->app_keypad = true;  break; /* DECKPAM */
    case '>': t->app_keypad = false; break; /* DECKPNM */
    default: break;
    }
}

/* ── OSC dispatch ────────────────────────────────────────────────────────── */
static int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int base64_decode(char *buf, int len) {
    int r = 0, w = 0;
    while (r + 3 < len) {
        int a = b64val(buf[r]), b = b64val(buf[r+1]);
        int c = (buf[r+2] != '=') ? b64val(buf[r+2]) : -1;
        int d = (buf[r+3] != '=') ? b64val(buf[r+3]) : -1;
        r += 4;
        if (a < 0 || b < 0) break;
        buf[w++] = (char)((a << 2) | (b >> 4));
        if (c < 0) break;
        buf[w++] = (char)(((b & 0xf) << 4) | (c >> 2));
        if (d < 0) break;
        buf[w++] = (char)(((c & 0x3) << 6) | d);
    }
    return w;
}

static void osc_dispatch(Term *t) {
    if (t->osc_len < 2) return;
    /* OSC 10/11: query default fg/bg color */
    if (t->osc_len >= 4 && t->osc_buf[2] == ';' && t->osc_buf[3] == '?' && t->pty_write) {
        uint32_t c = 0;
        int cmd = 0;
        if (t->osc_buf[0] == '1' && t->osc_buf[1] == '0') { c = DEFAULT_FG; cmd = 10; }
        else if (t->osc_buf[0] == '1' && t->osc_buf[1] == '1') { c = DEFAULT_BG; cmd = 11; }
        if (cmd) {
            char resp[48];
            int n = snprintf(resp, sizeof resp,
                "\033]%d;rgb:%02x%02x/%02x%02x/%02x%02x\007", cmd,
                (c>>16)&0xff,(c>>16)&0xff,
                (c>> 8)&0xff,(c>> 8)&0xff,
                 c     &0xff, c     &0xff);
            t->pty_write(t->pty_data, resp, n);
            return;
        }
    }
    /* OSC 52: set clipboard — format "52;Pc;Pd" where Pd is base64 text */
    if (t->osc_buf[0] == '5' && t->osc_buf[1] == '2' && t->set_clipboard) {
        char *semi = memchr(t->osc_buf + 2, ';', t->osc_len - 2);
        if (semi && semi + 1 < t->osc_buf + t->osc_len) {
            char *data = semi + 1;
            int dlen = t->osc_len - (int)(data - t->osc_buf);
            int decoded = base64_decode(data, dlen);
            if (decoded > 0)
                t->set_clipboard(t->clipboard_data, data, decoded);
        }
    }
}

/* ── main byte processor ─────────────────────────────────────────────────── */
static void term_feed_byte(Term *t, unsigned char b) {
    /* DEL — never printable */
    if (b == 0x7f) return;

    /* C0 controls in most states */
    if (b < 0x20 && b != 0x1b && t->vt_state != VT_OSC && t->vt_state != VT_DCS) {
        switch (b) {
        case '\r': t->cx = 0; t->pending_wrap = false; return;
        case '\n': case '\x0b': case '\x0c': newline(t); return;
        case '\x08':
            if (t->cx > 0) { t->cx--; t->pending_wrap = false; }
            return;
        case '\x09': { /* HT — horizontal tab */
            int nx = t->cx + 1;
            while (nx < t->cols - 1 && !tab_get(t, nx)) nx++;
            t->cx = nx;
            t->pending_wrap = false;
            return;
        }
        case '\x07': return; /* BEL — ignore */
        case '\x0e': t->charset_gl = 1; return; /* SO — G1 active */
        case '\x0f': t->charset_gl = 0; return; /* SI — G0 active */
        default: return;
        }
    }

    /* 8-bit C1 controls (0x80–0x9F) when not accumulating a UTF-8 sequence */
    if (b >= 0x80 && b <= 0x9F && t->utf8_rem == 0
            && t->vt_state != VT_OSC && t->vt_state != VT_DCS) {
        switch (b) {
        case 0x8D: esc_dispatch(t, 'M'); break; /* RI */
        case 0x9B: /* CSI */
            t->vt_state = VT_CSI;
            t->nparams = 0; t->cur_param = 0;
            t->have_param = false; t->csi_mod = 0;
            break;
        case 0x9D: /* OSC */
            t->vt_state = VT_OSC;
            t->osc_len = 0;
            break;
        case 0x9C: t->vt_state = VT_NORMAL; break; /* ST */
        case 0x90: t->vt_state = VT_DCS;    break; /* DCS */
        case 0x9E: case 0x9F: case 0x98:           /* PM, APC, SOS */
            t->vt_state = VT_DCS; break;
        default: break;
        }
        return;
    }

    switch (t->vt_state) {
    case VT_NORMAL:
        if (b == 0x1b) { t->vt_state = VT_ESC; return; }
        {
            int cp = utf8_feed(t, b);
            if (cp >= 0) term_putcp(t, (uint32_t)cp);
        }
        break;

    case VT_ESC:
        t->vt_state = VT_NORMAL;
        if (b == '[') {
            t->vt_state  = VT_CSI;
            t->nparams   = 0;
            t->cur_param = 0;
            t->have_param = false;
            t->csi_mod   = 0;
        } else if (b == ']') {
            t->vt_state = VT_OSC;
            t->osc_len  = 0;
        } else if (b == 'P') {
            t->vt_state = VT_DCS;
        } else if (b == '_' || b == '^' || b == 'X') {
            t->vt_state = VT_DCS; /* APC, PM, SOS — consume until ST */
        } else if (b == '(') {
            t->vt_state = VT_ESC_CS0;
        } else if (b == ')') {
            t->vt_state = VT_ESC_CS1;
        } else if (b == '\\') {
            /* ST — terminates any prior OSC/DCS; state already VT_NORMAL */
        } else {
            esc_dispatch(t, (char)b);
        }
        break;

    case VT_ESC_CS0:
        t->charset_g[0] = (b == '0') ? 1 : 0; /* '0'=DEC special, else ASCII */
        t->vt_state = VT_NORMAL;
        break;

    case VT_ESC_CS1:
        t->charset_g[1] = (b == '0') ? 1 : 0;
        t->vt_state = VT_NORMAL;
        break;

    case VT_CSI:
        /* private-parameter prefix bytes */
        if ((b == '?' || b == '>' || b == '<' || b == '=')
                && !t->nparams && !t->have_param && t->csi_mod == 0) {
            t->csi_mod = (char)b;
            break;
        }
        if (b >= '0' && b <= '9') {
            t->cur_param = t->cur_param * 10 + (b - '0');
            t->have_param = true;
            break;
        }
        if (b == ';') { push_param(t); break; }
        if (b >= 0x40 && b <= 0x7e) {
            csi_dispatch(t, (char)b);
            t->vt_state = VT_NORMAL;
        } else if (b >= 0x20 && b <= 0x2f) {
            t->csi_interm = (char)b;
            t->vt_state = VT_CSI_INTERM;
        }
        break;

    case VT_CSI_INTERM:
        if (b >= 0x40 && b <= 0x7e) {
            if (t->have_param || t->nparams == 0) push_param(t);
            if (t->csi_interm == ' ' && b == 'q') /* DECSCUSR */
                t->cursor_shape = (uint8_t)param(t, 0, 0);
            t->vt_state = VT_NORMAL;
        }
        break;

    case VT_CSI_IGNORE:
        if (b >= 0x40 && b <= 0x7e) t->vt_state = VT_NORMAL;
        break;

    case VT_OSC:
        /* 0x9C is 8-bit ST, but also a valid UTF-8 continuation byte (0x80–0xBF).
           Only treat it as ST when we are not inside a multi-byte UTF-8 sequence. */
        if (b == '\x07' || (b == 0x9c && t->osc_utf8_rem == 0)) {
            osc_dispatch(t);
            t->osc_len = 0;
            t->osc_utf8_rem = 0;
            t->vt_state = VT_NORMAL;
        } else if (b == 0x1b) {
            osc_dispatch(t); /* ESC \ (ST) terminates OSC */
            t->osc_len = 0;
            t->osc_utf8_rem = 0;
            t->vt_state = VT_ESC;
        } else {
            if (t->osc_len < MAX_OSC - 1)
                t->osc_buf[t->osc_len++] = (char)b;
            /* track UTF-8 sequence length so continuation bytes aren't mistaken for ST */
            if ((b & 0xC0) == 0x80) {
                if (t->osc_utf8_rem > 0) t->osc_utf8_rem--;
            } else if ((b & 0xE0) == 0xC0) { t->osc_utf8_rem = 1; }
            else if ((b & 0xF0) == 0xE0)   { t->osc_utf8_rem = 2; }
            else if ((b & 0xF8) == 0xF0)   { t->osc_utf8_rem = 3; }
            else                            { t->osc_utf8_rem = 0; }
        }
        break;

    case VT_DCS:
        if (b == 0x9c) t->vt_state = VT_NORMAL;
        else if (b == 0x1b) t->vt_state = VT_ESC;
        break;
    }
}

/* ── public API ──────────────────────────────────────────────────────────── */
Term *term_new(int cols, int rows) {
    Term *t = calloc(1, sizeof *t);
    t->cols = cols; t->rows = rows;
    for (int s = 0; s < 2; s++) {
        t->cells[s] = calloc((size_t)cols * rows, sizeof(Cell));
        t->dirty[s] = calloc((size_t)rows, sizeof(bool));
    }
    t->fg = DEFAULT_FG; t->bg = DEFAULT_BG;
    t->cursor_visible = true;
    t->cursor_blink_on = true;
    t->autowrap = true;
    t->scroll_top = 0; t->scroll_bot = rows - 1;
    t->charset_g[0] = t->charset_g[1] = 0;
    t->charset_gl = 0;
    tab_defaults(t);
    for (int s = 0; s < 2; s++) {
        t->screen = s;
        for (int y = 0; y < rows; y++) erase_line(t, y);
    }
    t->screen = 0;
    return t;
}

void term_free(Term *t) {
    for (int s = 0; s < 2; s++) { free(t->cells[s]); free(t->dirty[s]); }
    free(t);
}

void term_mark_all_dirty(Term *t) {
    for (int y = 0; y < t->rows; y++) t->dirty[t->screen][y] = true;
}

void term_process(Term *t, const char *buf, int len) {
    for (int i = 0; i < len; i++)
        term_feed_byte(t, (unsigned char)buf[i]);
}

void term_resize(Term *t, int new_cols, int new_rows) {
    if (new_cols == t->cols && new_rows == t->rows) return;

    for (int s = 0; s < 2; s++) {
        Cell *old = t->cells[s];
        Cell *nw  = calloc((size_t)new_cols * new_rows, sizeof(Cell));
        bool *nd  = calloc((size_t)new_rows, sizeof(bool));

        int copy_rows = new_rows < t->rows ? new_rows : t->rows;
        int copy_cols = new_cols < t->cols ? new_cols : t->cols;
        for (int y = 0; y < copy_rows; y++) {
            memcpy(&nw[y * new_cols], &old[y * t->cols],
                   (size_t)copy_cols * sizeof(Cell));
            Cell blank = { ' ', DEFAULT_FG, DEFAULT_BG, 0 };
            for (int x = copy_cols; x < new_cols; x++)
                nw[y * new_cols + x] = blank;
            nd[y] = true;
        }
        Cell blank = { ' ', DEFAULT_FG, DEFAULT_BG, 0 };
        for (int y = copy_rows; y < new_rows; y++) {
            for (int x = 0; x < new_cols; x++)
                nw[y * new_cols + x] = blank;
            nd[y] = true;
        }
        free(old); free(t->dirty[s]);
        t->cells[s] = nw;
        t->dirty[s] = nd;
    }

    t->cols = new_cols;
    t->rows = new_rows;
    t->scroll_top = 0;
    t->scroll_bot = new_rows - 1;
    if (t->cx >= new_cols) t->cx = new_cols - 1;
    if (t->cy >= new_rows) t->cy = new_rows - 1;
}
