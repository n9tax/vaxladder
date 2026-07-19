/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Joe <joe@n9tax.com> */
/*
 * plat.h - platform abstraction.
 *
 * Everything OS-specific lives behind this interface.  There are two
 * implementations:
 *   plat_vms.c    - OpenVMS/VAX, using $QIO to the terminal driver and LIB$WAIT
 *   plat_posix.c  - POSIX (termios), used for development/testing on Unix
 *
 * The interface is deliberately tiny: raw single-key polling, raw byte
 * output, and a millisecond sleep.  All escape-sequence formatting and
 * screen buffering is done above this layer (see term.c), so this file
 * knows nothing about terminal types.
 */
#ifndef PLAT_H
#define PLAT_H

/* Put the controlling terminal into raw, no-echo mode and remember the
 * previous state so plat_shutdown() can restore it.  Returns 0 on success. */
int  plat_init(void);

/* Restore the terminal to its original state. */
void plat_shutdown(void);

/* Non-blocking read of one input byte from the terminal.
 * Returns the byte value (0..255) if a key is waiting, or -1 if none. */
int  plat_getkey(void);

/* Write exactly len raw bytes to the terminal (no translation). */
void plat_write(const char *buf, int len);

/* Sleep for approximately ms milliseconds. */
void plat_wait_ms(int ms);

/* Milliseconds since some fixed point (monotonic-ish); used for timing. */
long plat_clock_ms(void);

/* Seed / draw from the platform RNG helper (portable wrapper). */
void plat_seed_rng(void);

#endif /* PLAT_H */
