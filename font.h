#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#define ATLAS_SLOTS  512
#define ATLAS_HASH   512   /* must be power of 2, >= ATLAS_SLOTS */

typedef struct {
    uint32_t cp;
    uint8_t  style; /* 0=normal 1=bold 2=italic 3=bold+italic */
    uint32_t slot;  /* index into atlas */
    bool     valid;
} GlyphEntry;

typedef struct {
    FT_Library lib;
    FT_Face    face;
    FT_Face    bold_face;

    int cell_w, cell_h, baseline;

    /* atlas: ATLAS_SLOTS slots of cell_w * cell_h bytes each */
    uint8_t *atlas;
    int      atlas_used;

    /* hash table: (cp, style) → slot */
    GlyphEntry ht[ATLAS_HASH];
} Font;

Font *font_init(const char *path, int size_px);
void  font_free(Font *f);

/* Returns atlas slot index for (cp, style), loading it if needed.
   Returns -1 if the codepoint cannot be rendered. */
int font_glyph(Font *f, uint32_t cp, uint8_t style);

/* Pointer to slot data (cell_w * cell_h bytes, 8-bit alpha). */
static inline const uint8_t *font_slot(const Font *f, int slot) {
    return f->atlas + (size_t)slot * f->cell_w * f->cell_h;
}
