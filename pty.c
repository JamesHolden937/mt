#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "pty.h"

Pty pty_spawn(int cols, int rows, char *const argv[]) {
    struct winsize ws = { .ws_col = cols, .ws_row = rows };
    Pty p;
    p.pid = forkpty(&p.fd, NULL, NULL, &ws);
    if (p.pid < 0) { perror("forkpty"); exit(1); }
    if (p.pid == 0) {
        /* child */
        setenv("TERM", "xterm-256color", 1);
        char *shell = getenv("SHELL");
        if (!shell) shell = "/bin/sh";
        char *args[] = { shell, NULL };
        char *const *exec_args = argv ? argv : args;
        execvp(exec_args[0], exec_args);
        perror("execvp");
        _exit(1);
    }
    /* make master non-blocking */
    int flags = fcntl(p.fd, F_GETFL, 0);
    fcntl(p.fd, F_SETFL, flags | O_NONBLOCK);
    return p;
}

void pty_resize(Pty *p, int cols, int rows) {
    struct winsize ws = { .ws_col = cols, .ws_row = rows };
    ioctl(p->fd, TIOCSWINSZ, &ws);
}

void pty_write(Pty *p, const char *buf, int len) {
    while (len > 0) {
        int n = write(p->fd, buf, len);
        if (n < 0) { if (errno == EINTR) continue; return; }
        buf += n; len -= n;
    }
}
