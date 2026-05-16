#pragma once
#include "mt.h"

Term *term_new(int cols, int rows);
void  term_free(Term *t);
void  term_resize(Term *t, int cols, int rows);
void  term_process(Term *t, const char *buf, int len);
/* mark every row dirty (e.g. after resize or full reset) */
void  term_mark_all_dirty(Term *t);
