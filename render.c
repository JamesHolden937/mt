#include "render.h"

static void fill_rect(uint32_t *pixels, int stride,
                      int x, int y, int w, int h, uint32_t color) {
    for (int row = y; row < y + h; row++) {
        uint32_t *p = pixels + row * stride + x;
        for (int col = 0; col < w; col++) p[col] = color;
    }
}

static void blit_glyph(uint32_t *pixels, int stride,
                        int cx, int cy, int cw, int ch,
                        const uint8_t *glyph, uint32_t fg) {
    uint32_t fr = (fg >> 16) & 0xff;
    uint32_t fg_ = (fg >>  8) & 0xff;
    uint32_t fb  =  fg        & 0xff;

    for (int row = 0; row < ch; row++) {
        uint32_t *dst = pixels + (cy + row) * stride + cx;
        const uint8_t *src = glyph + row * cw;
        for (int col = 0; col < cw; col++) {
            uint8_t a = src[col];
            if (!a) continue;
            if (a == 255) { dst[col] = fg; continue; }
            uint32_t d = dst[col];
            uint32_t dr = (d >> 16) & 0xff;
            uint32_t dg = (d >>  8) & 0xff;
            uint32_t db =  d        & 0xff;
            /* (fg*a + bg*(256-a)) >> 8  — no underflow, result always ≤ 255 */
            uint32_t r = (fr * a + dr * (256u - a)) >> 8;
            uint32_t g = (fg_ * a + dg * (256u - a)) >> 8;
            uint32_t b = (fb  * a + db * (256u - a)) >> 8;
            dst[col] = (r << 16) | (g << 8) | b;
        }
    }
}

static void draw_hline(uint32_t *pixels, int stride,
                       int cx, int cy, int w, uint32_t color) {
    uint32_t *row = pixels + cy * stride + cx;
    for (int i = 0; i < w; i++) row[i] = color;
}

static bool cell_selected(const Selection *sel, int x, int y, int cols) {
    if (!sel || !sel->active) return false;
    int pos  = y * cols + x;
    int pos0 = sel->y0 * cols + sel->x0;
    int pos1 = sel->y1 * cols + sel->x1;
    return pos >= pos0 && pos <= pos1;
}

void render_frame(uint32_t *pixels, int stride, bool *dirty,
                  const Selection *sel, Term *t, Font *f) {
    int cw = f->cell_w, ch = f->cell_h;
    Cell *cells = t->cells[t->screen];
    bool cursor_on = t->cursor_visible && t->cursor_blink_on;

    for (int y = 0; y < t->rows; y++) {
        if (!dirty[y]) continue;
        dirty[y] = false;

        for (int x = 0; x < t->cols; x++) {
            Cell c = cells[y * t->cols + x];

            /* right-half of a wide char: area already drawn by the left cell */
            if (c.attrs & ATTR_WIDE_CONT) continue;

            bool is_cursor = (cursor_on && x == t->cx && y == t->cy);
            bool selected  = cell_selected(sel, x, y, t->cols);
            uint32_t fg = c.fg, bg = c.bg;
            if (is_cursor || selected) { uint32_t tmp = fg; fg = bg; bg = tmp; }
            /* concealed cell (SGR 8): fg==bg, cursor swap leaves both equal → invisible.
               Invert bg so the cursor block/underline/beam is always visible. */
            if (is_cursor && fg == bg) bg = (~fg) & 0x00FFFFFF;

            /* wide chars occupy 2 cell-widths; normal chars 1 */
            int draw_w = (c.attrs & ATTR_WIDE) ? cw * 2 : cw;

            fill_rect(pixels, stride, x * cw, y * ch, draw_w, ch, bg);

            if (c.cp && c.cp != ' ') {
                uint8_t style = (uint8_t)(c.attrs & (ATTR_BOLD | ATTR_ITALIC));
                int slot = font_glyph(f, c.cp, style);
                if (slot >= 0)
                    blit_glyph(pixels, stride, x*cw, y*ch, cw, ch,
                               font_slot(f, slot), fg);
            }

            if (c.attrs & ATTR_UNDERLINE)
                draw_hline(pixels, stride, x*cw, y*ch + ch - 2, draw_w, fg);
            if (c.attrs & ATTR_STRIKE)
                draw_hline(pixels, stride, x*cw, y*ch + ch/2 - 1, draw_w, fg);

            /* cursor shape overlay (drawn on top of cell content) */
            if (is_cursor) {
                uint8_t shape = t->cursor_shape;
                if (shape == 3 || shape == 4) /* underline */
                    draw_hline(pixels, stride, x*cw, y*ch + ch - 2, cw, fg);
                else if (shape == 5 || shape == 6) /* beam */
                    fill_rect(pixels, stride, x*cw, y*ch, 1, ch, fg);
                /* block (0/1/2): the reversed fill_rect above is sufficient */
            }
        }
    }
}
