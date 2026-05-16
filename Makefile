CC      = cc
PKGS    = wayland-client freetype2 xkbcommon
CFLAGS  = -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter \
          -D_GNU_SOURCE \
          $(shell pkg-config --cflags $(PKGS))
LDFLAGS = $(shell pkg-config --libs $(PKGS)) -lutil
SRCS    = main.c wayland.c input.c pty.c vt.c render.c font.c \
          xdg-shell-protocol.c
OBJS    = $(SRCS:.c=.o)
BIN     = mt

PROTO_DIR = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
XDG_XML   = $(PROTO_DIR)/stable/xdg-shell/xdg-shell.xml

.PHONY: all clean run
all: $(BIN)

$(BIN): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

xdg-shell-protocol.h: $(XDG_XML)
	wayland-scanner client-header $< $@

xdg-shell-protocol.c: $(XDG_XML)
	wayland-scanner private-code $< $@

wayland.o input.o main.o: xdg-shell-protocol.h

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(BIN) $(OBJS) xdg-shell-protocol.h xdg-shell-protocol.c

run: $(BIN)
	./$(BIN)
