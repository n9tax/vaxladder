/*
 * config.c - LADDER.DAT load/save and built-in terminal presets.
 *
 * The configuration record (see ladder.h) describes both the terminal
 * capabilities (how to move the cursor, clear the screen, hide/show the
 * cursor) and the player's movement keys and options.  This mirrors what
 * the original CP/M LADCONF program produced in its LADDER.DAT file.
 *
 * The file is a small fixed binary record.  VAX and the x86 dev box are
 * both little-endian, so the raw struct is portable between them; if you
 * ever move LADDER.DAT between differing architectures, just re-run
 * LADCONF instead.
 */
#include "ladder.h"
#include <stdio.h>
#include <string.h>

const int PLAY_SPEED_MS[NUM_PLAY_SPEEDS] = { 120, 100, 90, 50, 30 };

/* Small helper so preset tables stay readable. */
static void cap(char *dst, const char *src)
{
    memset(dst, 0, CAP_LEN);
    if (src) {
        strncpy(dst, src, CAP_LEN - 1);
        dst[CAP_LEN - 1] = '\0';
    }
}

/* ---- Built-in terminal presets ---------------------------------------- */

/*
 * Each preset fills in the terminal-capability half of the Config.  The
 * key bindings / options are left untouched so a preset can be re-applied
 * without clobbering the player's key choices.
 */
struct preset {
    const char *name;
    int   cm_mode;
    int   cm_row_first;
    int   cm_base;
    int   cm_offset;
    const char *cm_lead;
    const char *cm_sep;
    const char *cm_term;
    const char *cap_clear;
    const char *cap_curoff;
    const char *cap_curon;
};

/* ESC == "\033" */
static const struct preset PRESETS[] = {
    /* VT100 / VT220 / any ANSI terminal.  1-based decimal ESC[r;cH.        */
    { "VT100",  CM_ANSI,   1, 1, 0, "\033[", ";", "H",
      "\033[2J\033[H", "\033[?25l", "\033[?25h" },

    { "VT220",  CM_ANSI,   1, 1, 0, "\033[", ";", "H",
      "\033[2J\033[H", "\033[?25l", "\033[?25h" },

    /* Generic ANSI, no DEC private cursor hide (leave cursor visible).     */
    { "ANSI",   CM_ANSI,   1, 1, 0, "\033[", ";", "H",
      "\033[2J\033[H", "", "" },

    /* VT52 native mode.  ESC Y <row+31+1> <col+31+1>, home = ESC H,        */
    /* clear-to-end-of-screen = ESC J.                                      */
    { "VT52",   CM_OFFSET, 1, 1, 32, "\033Y", "", "",
      "\033H\033J", "", "" },

    /* Lear-Siegler ADM-3A / Kaypro built-in terminal.  ESC = <r+32><c+32>. */
    /* Clear screen is Ctrl-Z (0x1A).                                       */
    { "ADM3A",  CM_OFFSET, 1, 1, 32, "\033=", "", "",
      "\032", "", "" },

    /* Heath/Zenith H19 (ANSI-ish but offset cursor addressing via ESC Y).  */
    { "H19",    CM_OFFSET, 1, 1, 32, "\033Y", "", "",
      "\033E", "", "" },
};
#define NUM_PRESETS (int)(sizeof(PRESETS) / sizeof(PRESETS[0]))

const char *config_preset_name(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return NULL;
    return PRESETS[index].name;
}

int config_apply_preset(Config *cfg, const char *term_name)
{
    int i;
    for (i = 0; i < NUM_PRESETS; i++) {
        if (strcmp(PRESETS[i].name, term_name) == 0) {
            const struct preset *p = &PRESETS[i];
            memset(cfg->term_name, 0, sizeof(cfg->term_name));
            strncpy(cfg->term_name, p->name, sizeof(cfg->term_name) - 1);
            cfg->cm_mode      = p->cm_mode;
            cfg->cm_row_first = p->cm_row_first;
            cfg->cm_base      = p->cm_base;
            cfg->cm_offset    = p->cm_offset;
            cap(cfg->cm_lead,   p->cm_lead);
            cap(cfg->cm_sep,    p->cm_sep);
            cap(cfg->cm_term,   p->cm_term);
            cap(cfg->cap_clear, p->cap_clear);
            cap(cfg->cap_curoff, p->cap_curoff);
            cap(cfg->cap_curon,  p->cap_curon);
            return 0;
        }
    }
    return -1;
}

/* ---- Defaults --------------------------------------------------------- */

void config_defaults(Config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    memcpy(cfg->magic, "LDR1", 4);

    config_apply_preset(cfg, "VT100");

    /* WASD-style plus arrows are handled separately by the game; these are
     * the canonical single-key bindings written into LADDER.DAT. */
    cfg->key_up     = 'i';
    cfg->key_down   = 'k';
    cfg->key_left   = 'j';
    cfg->key_right  = 'l';
    cfg->key_jump   = ' ';
    cfg->key_pause  = 'p';
    cfg->key_quit   = 'q';
    cfg->key_faster = '+';
    cfg->key_slower = '-';

    cfg->sound       = 1;
    cfg->start_speed = 1;
}

/* ---- Persistence ------------------------------------------------------ */

int config_load(const char *path, Config *cfg)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) return -1;
    n = fread(cfg, 1, sizeof(*cfg), f);
    fclose(f);
    if (n != sizeof(*cfg) || memcmp(cfg->magic, "LDR1", 4) != 0)
        return -2;
    return 0;
}

int config_save(const char *path, const Config *cfg)
{
    FILE *f = fopen(path, "wb");
    size_t n;
    if (!f) return -1;
    n = fwrite(cfg, 1, sizeof(*cfg), f);
    fclose(f);
    return (n == sizeof(*cfg)) ? 0 : -1;
}
