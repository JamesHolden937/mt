#pragma once

/* font — path is a compile-time default; override with -f at runtime */
#define FONT_PATH     "/usr/share/fonts/TTF/DejaVuSansMono.ttf"
#define FONT_SIZE_PX  14

/* terminal geometry */
#define DEFAULT_COLS  80
#define DEFAULT_ROWS  24

/* colors (0x00RRGGBB) */
#define DEFAULT_FG    0x00cccccc
#define DEFAULT_BG    0x00000000

/* 16 ANSI colors */
#define ANSI_COLOR_0  0x000000u
#define ANSI_COLOR_1  0xcc0000u
#define ANSI_COLOR_2  0x00cc00u
#define ANSI_COLOR_3  0xcccc00u
#define ANSI_COLOR_4  0x0000ccu
#define ANSI_COLOR_5  0xcc00ccu
#define ANSI_COLOR_6  0x00ccccu
#define ANSI_COLOR_7  0xccccccu
#define ANSI_COLOR_8  0x555555u
#define ANSI_COLOR_9  0xff5555u
#define ANSI_COLOR_10 0x55ff55u
#define ANSI_COLOR_11 0xffff55u
#define ANSI_COLOR_12 0x5555ffu
#define ANSI_COLOR_13 0xff55ffu
#define ANSI_COLOR_14 0x55ffffu
#define ANSI_COLOR_15 0xffffffu
