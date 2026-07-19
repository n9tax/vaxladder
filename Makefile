# Makefile - POSIX/Unix development build for VAX Ladder.
#
# This build is for DEVELOPING and TESTING the game on a modern Unix box.
# The real target is OpenVMS/VAX; build that with BUILD.COM or DESCRIP.MMS.
#
#   make           build ./ladder and ./ladconf
#   make run       build and run the game
#   make clean

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c89 -pedantic
SRCDIR   = src

COMMON   = $(SRCDIR)/config.c $(SRCDIR)/levels.c $(SRCDIR)/plat_posix.c
GAME_SRC = $(SRCDIR)/game.c $(SRCDIR)/term.c $(COMMON)
CONF_SRC = $(SRCDIR)/ladconf.c $(SRCDIR)/config.c $(SRCDIR)/levels.c
KEY_SRC  = $(SRCDIR)/keycode.c $(SRCDIR)/plat_posix.c

all: ladder ladconf keycode

ladder: $(GAME_SRC) $(SRCDIR)/ladder.h $(SRCDIR)/term.h $(SRCDIR)/plat.h
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $(GAME_SRC)

ladconf: $(CONF_SRC) $(SRCDIR)/ladder.h
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $(CONF_SRC)

keycode: $(KEY_SRC) $(SRCDIR)/plat.h
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $(KEY_SRC)

run: ladder
	./ladder

clean:
	rm -f ladder ladconf keycode

.PHONY: all run clean
