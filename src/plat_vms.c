/*
 * plat_vms.c - OpenVMS/VAX implementation of the platform layer.
 *
 * Terminal I/O goes straight to the terminal driver with $QIO:
 *   - reads use IO$_READVBLK | IO$M_NOECHO | IO$M_TIMED | IO$M_NOFILTR with a
 *     zero timeout, which returns whatever single key is already in the
 *     type-ahead buffer (or SS$_TIMEOUT if none) without blocking.
 *   - writes use IO$_WRITEVBLK | IO$M_NOFORMAT so our escape sequences reach
 *     the terminal untouched by the driver's formatting.
 * Frame pacing uses LIB$WAIT; timing samples use SYS$GETTIM.
 *
 * This file is compiled only on OpenVMS; the POSIX build uses plat_posix.c.
 */
#include "plat.h"

#include <stdlib.h>
#include <ssdef.h>
#include <iodef.h>
#include <ttdef.h>
#include <descrip.h>
#include <starlet.h>
#include <lib$routines.h>

/* Terminal characteristic bits (from ttdef.h); provide fallbacks in case a
 * very old header omits the mask forms. */
#ifndef TT$M_NOECHO
#define TT$M_NOECHO  0x00000040
#endif
#ifndef TT$M_PASSALL
#define TT$M_PASSALL 0x00000020
#endif

/* I/O status block returned by $QIO. */
typedef struct {
    unsigned short status;
    unsigned short count;
    unsigned int   info;
} IOSB;

static unsigned short tt_chan = 0;

/* Original terminal characteristics, saved so we can restore on exit.
 * $QIO SENSEMODE / SETMODE use a 3-longword characteristics buffer. */
static unsigned int saved_char[3];
static int have_saved = 0;

int plat_init(void)
{
    $DESCRIPTOR(tt_dsc, "TT");
    IOSB iosb;
    int status;
    unsigned int newchar[3];

    status = sys$assign(&tt_dsc, &tt_chan, 0, 0);
    if (!(status & 1)) return -1;

    /* Sense current mode so we can restore it later. */
    status = sys$qiow(0, tt_chan, IO$_SENSEMODE, &iosb, 0, 0,
                      saved_char, sizeof(saved_char), 0, 0, 0, 0);
    if ((status & 1) && (iosb.status & 1))
        have_saved = 1;

    /* Set a raw-ish mode: NOECHO, PASSALL, no type-ahead line editing.
     * We keep the same page width/length longwords the driver reported. */
    if (have_saved) {
        newchar[0] = saved_char[0];
        newchar[1] = saved_char[1];
        newchar[2] = saved_char[2];
        /* The basic terminal characteristics (TT$M_*) occupy the low 24 bits
         * of the second characteristics longword.  NOECHO stops the driver
         * echoing keystrokes; PASSALL delivers keys raw with no line editing
         * so single-key game input works. */
        newchar[1] |= (unsigned int)TT$M_NOECHO;
        newchar[1] |= (unsigned int)TT$M_PASSALL;
        sys$qiow(0, tt_chan, IO$_SETMODE, &iosb, 0, 0,
                 newchar, sizeof(newchar), 0, 0, 0, 0);
    }

    return 0;
}

void plat_shutdown(void)
{
    if (tt_chan) {
        if (have_saved) {
            IOSB iosb;
            sys$qiow(0, tt_chan, IO$_SETMODE, &iosb, 0, 0,
                     saved_char, sizeof(saved_char), 0, 0, 0, 0);
        }
        sys$dassgn(tt_chan);
        tt_chan = 0;
    }
}

int plat_getkey(void)
{
    IOSB iosb;
    unsigned char ch = 0;
    int status;

    /* Timed read with a zero-second timeout: returns immediately with the
     * next type-ahead byte, or SS$_TIMEOUT if the buffer is empty. */
    status = sys$qiow(0, tt_chan,
                      IO$_READVBLK | IO$M_NOECHO | IO$M_TIMED | IO$M_NOFILTR,
                      &iosb, 0, 0,
                      &ch, 1, 0 /* timeout seconds */, 0, 0, 0);

    if ((status & 1) && (iosb.status & 1) && iosb.count >= 1)
        return (int)ch;
    return -1;
}

void plat_write(const char *buf, int len)
{
    IOSB iosb;
    if (len <= 0) return;
    sys$qiow(0, tt_chan, IO$_WRITEVBLK | IO$M_NOFORMAT, &iosb, 0, 0,
             (void *)buf, len, 0, 0, 0, 0);
}

void plat_wait_ms(int ms)
{
    float secs;
    if (ms <= 0) return;
    secs = (float)ms / 1000.0f;
    lib$wait(&secs, 0, 0);
}

long plat_clock_ms(void)
{
    /* SYS$GETTIM fills a 64-bit count of 100ns ticks (little-endian).
     * We only use the low longword for short relative deltas; it wraps
     * about every 429 seconds, which callers treat as unsigned. */
    unsigned int t[2];
    sys$gettim(t);
    return (long)(t[0] / 10000u);
}

void plat_seed_rng(void)
{
    unsigned int t[2];
    sys$gettim(t);
    srand(t[0]);
}
