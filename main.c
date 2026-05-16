#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "config.h"
#include "mt.h"
#include "vt.h"
#include "font.h"
#include "pty.h"
#include "input.h"
#include "wayland.h"

static void usage(void) {
    fputs("usage: mt [-f <font.ttf>] [-s <px>] [-c <cols>] [-r <rows>] [-- cmd [args...]]\n", stderr);
    exit(1);
}

static void sigchld(int sig) {
    (void)sig;
    int status;
    waitpid(-1, &status, WNOHANG);
}

int main(int argc, char *argv[]) {
    const char *font_path = FONT_PATH;
    int font_size = FONT_SIZE_PX;
    int cols = DEFAULT_COLS, rows = DEFAULT_ROWS;

    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-f") && i+1 < argc) { font_path = argv[++i]; }
        else if (!strcmp(argv[i], "-s") && i+1 < argc) { font_size = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-c") && i+1 < argc) { cols = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-r") && i+1 < argc) { rows = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--")) { i++; break; }
        else { usage(); }
    }

    char *const *cmd_argv = (i < argc) ? (char *const *)&argv[i] : NULL;

    signal(SIGCHLD, sigchld);

    Font  *font  = font_init(font_path, font_size);
    Term  *term  = term_new(cols, rows);
    Input *input = input_new();
    Pty    pty   = pty_spawn(cols, rows, cmd_argv);

    WaylandState *ws = wayland_init(term, font, input, &pty);
    wayland_run(ws);
    wayland_free(ws);

    input_free(input);
    term_free(term);
    font_free(font);
    return 0;
}
