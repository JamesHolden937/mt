#pragma once
#include <sys/types.h>

typedef struct {
    int   fd;   /* pty master */
    pid_t pid;
} Pty;

Pty  pty_spawn(int cols, int rows, char *const argv[]);
void pty_resize(Pty *p, int cols, int rows);
void pty_write(Pty *p, const char *buf, int len);
