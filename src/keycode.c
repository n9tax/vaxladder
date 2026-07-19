/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Joe <joe@n9tax.com> */
/*
 * keycode.c - a tiny key-code reporter for diagnosing terminal input.
 *
 * It reads keys through the exact same raw-input path the game uses
 * (plat_init / plat_getkey), so whatever it prints is precisely what
 * LADDER receives after the terminal driver has had its say.  Press some
 * keys - especially the arrow keys - and it prints each byte as decimal,
 * hex, and (if printable) its character, plus a note for the ones that
 * matter for cursor keys.  Press  q  to quit.
 *
 * Build it alongside the game (see Makefile / BUILD.COM).
 */
#include "plat.h"

#include <stdio.h>
#include <string.h>

static void emit(const char *s)
{
    plat_write(s, (int)strlen(s));
}

static const char *note_for(int b)
{
    switch (b) {
    case 27:  return "  <- ESC (7-bit introducer)";
    case 91:  return "  <- '[' (CSI final of ESC [)";
    case 79:  return "  <- 'O' (SS3 final of ESC O)";
    case 155: return "  <- 0x9B  8-bit CSI";
    case 143: return "  <- 0x8F  8-bit SS3";
    case 65:  return "  <- 'A' (Up, if after an introducer)";
    case 66:  return "  <- 'B' (Down)";
    case 67:  return "  <- 'C' (Right)";
    case 68:  return "  <- 'D' (Left)";
    case 15:  return "  <- 0x0F (SS3 with high bit stripped by a 7-bit line?)";
    default:  break;
    }
    return "";
}

int main(void)
{
    char line[128];
    int c;
    int quit = 0;

    if (plat_init() != 0) {
        fprintf(stderr, "keycode: must be run on a terminal.\n");
        return 1;
    }

    emit("\r\n");
    emit("Key-code reporter.  Press keys (try the arrow keys).\r\n");
    emit("Each line is one received byte.  Press  q  to quit.\r\n");
    emit("--------------------------------------------------------\r\n");

    while (!quit) {
        c = plat_getkey();
        if (c < 0) { plat_wait_ms(20); continue; }

        if (c >= 32 && c < 127) {
            sprintf(line, "byte: dec=%-3d  hex=0x%02X  char='%c'%s\r\n",
                    c, c, (char)c, note_for(c));
        } else {
            sprintf(line, "byte: dec=%-3d  hex=0x%02X%s\r\n",
                    c, c, note_for(c));
        }
        emit(line);

        if (c == 'q' || c == 'Q') quit = 1;
    }

    emit("bye.\r\n");
    plat_shutdown();
    return 0;
}
