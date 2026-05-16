#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "mt.h"
#include "font.h"

/* Linear (stream-order) selection in cell coordinates, both inclusive, 0-based.
   Normalised so (y0,x0) <= (y1,x1) in reading order. */
typedef struct {
    bool active;
    int  x0, y0, x1, y1;
} Selection;

void render_frame(uint32_t *pixels, int stride, bool *dirty,
                  const Selection *sel, Term *t, Font *f);
