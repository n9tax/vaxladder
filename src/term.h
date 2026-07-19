/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Joe <joe@n9tax.com> */
/*
 * term.h - terminal capability formatting + 80x24 differential screen.
 *
 * The game draws into an off-screen 80x24 character buffer via term_put()
 * and term_puts().  term_refresh() then compares that buffer against what
 * is currently on the terminal and emits only the cursor moves and
 * characters needed to make the two match.  On a real serial terminal
 * (1200-9600 baud) redrawing the whole screen every frame is far too slow,
 * so this incremental update is what makes the game playable.
 *
 * All cursor addressing / clear / cursor-visibility escape sequences come
 * from the loaded Config (see ladder.h), so the same code drives VT100,
 * VT52, ADM-3A, etc.
 */
#ifndef TERM_H
#define TERM_H

#include "ladder.h"

/* Bind the terminal layer to a configuration and clear the physical
 * screen.  Call once after plat_init(). */
void term_begin(const Config *cfg);

/* Restore the cursor and leave the screen in a sane state. */
void term_end(void);

/* Clear the off-screen buffer to spaces (does not touch the terminal). */
void term_clear(void);

/* Write one character into the off-screen buffer at (col,row), 0-based. */
void term_put(int col, int row, char ch);

/* Write a NUL-terminated string starting at (col,row), clipped to the row. */
void term_puts(int col, int row, const char *s);

/* Push all pending changes to the terminal (the differential update). */
void term_refresh(void);

/* Ring the terminal bell if sound is enabled in the config. */
void term_bell(void);

/* Force the whole screen to be redrawn on the next refresh (e.g. after
 * returning from a menu drawn by other means). */
void term_invalidate(void);

#endif /* TERM_H */
