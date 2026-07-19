/*
 * plat_posix.c - POSIX implementation of the platform layer.
 *
 * Used for development and testing on Unix/Linux.  Puts the terminal into
 * raw (cbreak) no-echo mode, polls single keystrokes without blocking, and
 * sleeps in milliseconds.  The OpenVMS build uses plat_vms.c instead; only
 * one of the two is compiled into any given executable.
 */
/* Request POSIX + BSD APIs (nanosleep, gettimeofday) under -std=c89. */
#define _POSIX_C_SOURCE 199309L
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1

#include "plat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <sys/time.h>

static struct termios saved_tios;
static int have_saved = 0;

int plat_init(void)
{
    struct termios raw;

    if (!isatty(STDIN_FILENO))
        return -1;

    if (tcgetattr(STDIN_FILENO, &saved_tios) < 0)
        return -1;
    have_saved = 1;

    raw = saved_tios;
    /* Raw-ish: no canonical mode, no echo, no signal chars, no CR/LF map. */
    raw.c_lflag &= ~(ICANON | ECHO | IEXTEN);
    raw.c_iflag &= ~(ICRNL | INLCR | IXON | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN]  = 0;   /* non-blocking read: return immediately         */
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0)
        return -1;

    /* Make stdin non-blocking as a belt-and-braces measure. */
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);

    return 0;
}

void plat_shutdown(void)
{
    if (have_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_tios);
        have_saved = 0;
    }
}

int plat_getkey(void)
{
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 1) return (int)c;
    return -1;
}

void plat_write(const char *buf, int len)
{
    ssize_t off = 0;
    while (off < len) {
        ssize_t n = write(STDOUT_FILENO, buf + off, (size_t)(len - off));
        if (n <= 0) break;
        off += n;
    }
}

void plat_wait_ms(int ms)
{
    struct timespec ts;
    if (ms <= 0) return;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

long plat_clock_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)(tv.tv_sec * 1000L + tv.tv_usec / 1000L);
}

void plat_seed_rng(void)
{
    srand((unsigned)(plat_clock_ms() ^ (long)getpid()));
}
