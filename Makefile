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

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
APPDIR   = $(DATADIR)/applications

PROTO_DIR = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
XDG_XML   = $(PROTO_DIR)/stable/xdg-shell/xdg-shell.xml

.PHONY: all clean run install uninstall
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

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	install -Dm644 mt.desktop $(DESTDIR)$(APPDIR)/mt.desktop
	-update-desktop-database $(DESTDIR)$(APPDIR) 2>/dev/null

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)
	rm -f $(DESTDIR)$(APPDIR)/mt.desktop
	-update-desktop-database $(DESTDIR)$(APPDIR) 2>/dev/null
