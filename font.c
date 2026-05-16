#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ft2build.h>
#include FT_SYNTHESIS_H
#include "mt.h"
#include "font.h"

static uint32_t ht_hash(uint32_t cp, uint8_t style) {
    uint32_t k = cp * 4 + style;
    k = (k ^ (k >> 16)) * 0x45d9f3b;
    k = (k ^ (k >> 16)) * 0x45d9f3b;
    return k & (ATLAS_HASH - 1);
}

static int ht_find(Font *f, uint32_t cp, uint8_t style) {
    uint32_t h = ht_hash(cp, style);
    for (int i = 0; i < ATLAS_HASH; i++) {
        uint32_t idx = (h + i) & (ATLAS_HASH - 1);
        if (!f->ht[idx].valid) return -1;
        if (f->ht[idx].cp == cp && f->ht[idx].style == style)
            return (int)f->ht[idx].slot;
    }
    return -1;
}

static void ht_insert(Font *f, uint32_t cp, uint8_t style, uint32_t slot) {
    uint32_t h = ht_hash(cp, style);
    for (int i = 0; i < ATLAS_HASH; i++) {
        uint32_t idx = (h + i) & (ATLAS_HASH - 1);
        if (!f->ht[idx].valid) {
            f->ht[idx] = (GlyphEntry){ cp, style, slot, true };
            return;
        }
    }
}

static FT_Face load_face(FT_Library lib, const char *path, int size_px) {
    FT_Face face;
    if (FT_New_Face(lib, path, 0, &face)) { fprintf(stderr, "FT: cannot open %s\n", path); exit(1); }
    FT_Set_Pixel_Sizes(face, 0, size_px);
    return face;
}

Font *font_init(const char *path, int size_px) {
    Font *f = calloc(1, sizeof *f);
    FT_Init_FreeType(&f->lib);
    f->face = load_face(f->lib, path, size_px);

    /* cell metrics from the font */
    FT_Size_Metrics *m = &f->face->size->metrics;
    f->cell_h   = (int)(m->height >> 6);
    f->cell_w   = (int)(m->max_advance >> 6);
    f->baseline = (int)(m->ascender >> 6);

    if (f->cell_h < 1) f->cell_h = size_px + 4;
    if (f->cell_w < 1) f->cell_w = size_px / 2 + 1;

    f->atlas = calloc((size_t)ATLAS_SLOTS * f->cell_w * f->cell_h, 1);
    return f;
}

void font_free(Font *f) {
    FT_Done_Face(f->face);
    if (f->bold_face) FT_Done_Face(f->bold_face);
    FT_Done_FreeType(f->lib);
    free(f->atlas);
    free(f);
}

int font_glyph(Font *f, uint32_t cp, uint8_t style) {
    int cached = ht_find(f, cp, style);
    if (cached >= 0) return cached;
    if (f->atlas_used >= ATLAS_SLOTS) return -1;

    FT_Face face = f->face;
    FT_UInt gi = FT_Get_Char_Index(face, cp);
    if (!gi && cp != ' ') return -1;

    FT_Int32 load_flags = FT_LOAD_RENDER; /* FT_LOAD_TARGET_NORMAL: 8-bit grayscale AA */
    if (FT_Load_Glyph(face, gi, load_flags)) return -1;

    if ((style & ATTR_BOLD) && !f->bold_face)
        FT_GlyphSlot_Embolden(face->glyph);
    if (style & ATTR_ITALIC)
        FT_GlyphSlot_Oblique(face->glyph);

    FT_Bitmap *bm = &face->glyph->bitmap;
    int bx = face->glyph->bitmap_left;
    int by = f->baseline - face->glyph->bitmap_top;

    int slot = f->atlas_used++;
    uint8_t *dst = f->atlas + (size_t)slot * f->cell_w * f->cell_h;

    for (int row = 0; row < (int)bm->rows; row++) {
        int dy = by + row;
        if (dy < 0 || dy >= f->cell_h) continue;
        for (int col = 0; col < (int)bm->width; col++) {
            int dx = bx + col;
            if (dx < 0 || dx >= f->cell_w) continue;
            uint8_t a;
            if (bm->pixel_mode == FT_PIXEL_MODE_GRAY)
                a = bm->buffer[row * bm->pitch + col];
            else if (bm->pixel_mode == FT_PIXEL_MODE_MONO)
                a = (bm->buffer[row * bm->pitch + (col >> 3)] & (0x80 >> (col & 7))) ? 255 : 0;
            else
                a = 0;
            dst[dy * f->cell_w + dx] = a;
        }
    }

    ht_insert(f, cp, style, (uint32_t)slot);
    return slot;
}
