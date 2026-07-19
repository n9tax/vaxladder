/*
 * term.c - terminal capability formatting and differential screen updates.
 * See term.h for the overall idea.
 */
#include "term.h"
#include "plat.h"

#include <string.h>

/* Two full-screen buffers: `back` is what the game has drawn this frame,
 * `front` is what we believe is currently on the physical terminal. */
static char back[SCREEN_ROWS][SCREEN_COLS];
static char front[SCREEN_ROWS][SCREEN_COLS];

static Config cfg;
static int cur_row = -1, cur_col = -1;  /* where the physical cursor is now */
static int force_redraw = 1;

/* ---- Output buffering -------------------------------------------------- */
/* We accumulate a frame's worth of bytes and flush them in one write, so a
 * frame costs a single I/O to the terminal driver rather than dozens. */
static char outbuf[SCREEN_ROWS * SCREEN_COLS * 8];
static int  outlen = 0;

static void out_flush(void)
{
    if (outlen > 0) {
        plat_write(outbuf, outlen);
        outlen = 0;
    }
}

static void out_str(const char *s)
{
    while (*s) {
        if (outlen >= (int)sizeof(outbuf)) out_flush();
        outbuf[outlen++] = *s++;
    }
}

static void out_cap(const char *cap)
{
    /* Capability strings may legitimately contain embedded NULs? No - we
     * store them NUL-terminated, so a plain string emit is correct. */
    out_str(cap);
}

/* ---- Cursor addressing ------------------------------------------------- */

static void emit_number(int n)
{
    char tmp[12];
    int i = 0, j;
    if (n == 0) { out_str("0"); return; }
    while (n > 0 && i < (int)sizeof(tmp)) { tmp[i++] = (char)('0' + n % 10); n /= 10; }
    for (j = i - 1; j >= 0; j--) {
        if (outlen >= (int)sizeof(outbuf)) out_flush();
        outbuf[outlen++] = tmp[j];
    }
}

static void emit_byte(int b)
{
    if (outlen >= (int)sizeof(outbuf)) out_flush();
    outbuf[outlen++] = (char)(b & 0xff);
}

/* Move the physical cursor to 0-based (col,row) using the configured mode. */
static void move_to(int col, int row)
{
    int r = row + cfg.cm_base;   /* apply 0/1 base */
    int c = col + cfg.cm_base;

    out_cap(cfg.cm_lead);

    if (cfg.cm_mode == CM_ANSI) {
        if (cfg.cm_row_first) {
            emit_number(r); out_cap(cfg.cm_sep); emit_number(c);
        } else {
            emit_number(c); out_cap(cfg.cm_sep); emit_number(r);
        }
        out_cap(cfg.cm_term);
    } else { /* CM_OFFSET */
        if (cfg.cm_row_first) {
            emit_byte(r + cfg.cm_offset - cfg.cm_base + 0);
            emit_byte(c + cfg.cm_offset - cfg.cm_base + 0);
        } else {
            emit_byte(c + cfg.cm_offset - cfg.cm_base + 0);
            emit_byte(r + cfg.cm_offset - cfg.cm_base + 0);
        }
    }
    cur_row = row;
    cur_col = col;
}

/* ---- Public API -------------------------------------------------------- */

void term_begin(const Config *c)
{
    cfg = *c;
    memset(back, ' ', sizeof(back));
    memset(front, ' ', sizeof(front));
    cur_row = cur_col = -1;
    force_redraw = 1;

    outlen = 0;
    /* For ANSI/VT terminals, normalise how the cursor keys report:
     *   ESC [ ? 1 l  (DECCKM off) - cursor keys send CSI "ESC [ A", not SS3
     *   ESC SP F     (S7C1T)      - send 7-bit C1 controls, not 8-bit 0x9B
     * Harmless on real VT100s; skipped for VT52/ADM-3A which won't grok it.
     * (The input parser also handles the 8-bit forms in case a terminal
     * ignores these requests.) */
    if (cfg.cm_mode == CM_ANSI) {
        out_str("\033[?1l");
        out_str("\033 F");
    }
    out_cap(cfg.cap_curoff);
    out_cap(cfg.cap_clear);
    out_flush();
}

void term_end(void)
{
    outlen = 0;
    move_to(0, SCREEN_ROWS - 1);
    out_cap(cfg.cap_curon);
    out_str("\r\n");
    out_flush();
}

void term_clear(void)
{
    memset(back, ' ', sizeof(back));
}

void term_invalidate(void)
{
    force_redraw = 1;
}

void term_put(int col, int row, char ch)
{
    if (row < 0 || row >= SCREEN_ROWS || col < 0 || col >= SCREEN_COLS) return;
    back[row][col] = ch;
}

void term_puts(int col, int row, const char *s)
{
    if (row < 0 || row >= SCREEN_ROWS) return;
    while (*s && col < SCREEN_COLS) {
        if (col >= 0) back[row][col] = *s;
        col++;
        s++;
    }
}

void term_refresh(void)
{
    int row, col;

    for (row = 0; row < SCREEN_ROWS; row++) {
        col = 0;
        while (col < SCREEN_COLS) {
            if (!force_redraw && back[row][col] == front[row][col]) {
                col++;
                continue;
            }

            /* Position the cursor if it isn't already here. */
            if (cur_row != row || cur_col != col)
                move_to(col, row);

            /* Emit a run of characters, updating the physical cursor as we
             * go and stopping at the first cell that already matches. */
            while (col < SCREEN_COLS &&
                   (force_redraw || back[row][col] != front[row][col])) {
                emit_byte((unsigned char)back[row][col]);
                front[row][col] = back[row][col];
                cur_col++;
                col++;
            }
            /* Guard against the terminal auto-wrapping past column 79. */
            if (cur_col >= SCREEN_COLS) { cur_row = cur_col = -1; }
        }
    }

    force_redraw = 0;
    out_flush();
}

void term_bell(void)
{
    if (cfg.sound) {
        char bel = '\007';
        plat_write(&bel, 1);
    }
}
