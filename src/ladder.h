/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Joe <joe@n9tax.com> */
/*
 * ladder.h - shared types, constants, and configuration record for
 *            VAX Ladder, a port of the 1982 CP/M game "Ladder" to
 *            OpenVMS/VAX (and portable POSIX for development).
 *
 * The game is an 80x24 character-cell arcade game.  All coordinates in
 * the game logic are 0-based cell coordinates; the terminal layer adds
 * the +1 needed for 1-based cursor addressing.
 */
#ifndef LADDER_H
#define LADDER_H

/* ---- Screen / field geometry ------------------------------------------ */
#define SCREEN_COLS   80
#define SCREEN_ROWS   24

#define LEVEL_ROWS    20        /* playfield rows: layout occupies rows 0..19 */
#define LEVEL_COLS    79        /* playfield columns 0..78                    */

#define STATUS_ROW    21        /* "Lads .. Level .. Score .. Bonus time .."  */
#define MESSAGE_ROW   23        /* pause / info messages                      */

/* ---- Gameplay tuning (ported from the reference implementations) ------- */
#define INITIAL_LIVES     5
#define START_BONUS_TIME  2000
#define MAX_ROCKS         7     /* base cap on rocks on screen                */
#define DISPENSER_MAX_ROCKS 1   /* each dispenser raises the cap by this      */
#define HIDDEN_FACTOR_MAX_ROCKS 2   /* extra rocks per completed level cycle  */
#define NEW_LIFE_SCORE    10000 /* bonus life every N points                  */
#define SCORE_PER_ROCK    200
#define SCORE_PER_TREASURE_TICK 10

#define MAX_DISPENSERS    16    /* per level                                  */
#define ROCK_LIMIT        64    /* hard array cap for live rocks              */

/* Play speeds expressed as per-move millisecond delays (index 0..4).
 * '+' / '-' move between them; higher index == faster. */
#define NUM_PLAY_SPEEDS   5
extern const int PLAY_SPEED_MS[NUM_PLAY_SPEEDS];
/* game gets 5% faster per completed level cycle */
#define HIDDEN_FACTOR_PLAY_SPEED_PCT 5

/* ---- Tile characters (as they appear in the level layouts) ------------- */
#define T_FLOOR       '='
#define T_DISFLOOR    '-'   /* disappearing floor                            */
#define T_LADDER      'H'
#define T_STATUE      '&'   /* collectible ("gift")                          */
#define T_TREASURE    '$'   /* touching it wins the level                    */
#define T_SIGN        '|'   /* solid sign post                               */
#define T_FIRE        '^'   /* trap: kills on contact                        */
#define T_EATER       '*'   /* border: destroys rocks                        */
#define T_TRAMPOLINE  '.'   /* bounces the player                            */
#define T_DISPENSER   'V'   /* rock dispenser (womb) - also where player is  */
#define T_SPACE       ' '

/* ---- Level data ------------------------------------------------------- */
typedef struct {
    const char *name;
    const char *layout[LEVEL_ROWS];   /* each string is LEVEL_COLS chars     */
} Level;

#define NUM_LEVELS 7
extern const Level LEVELS[NUM_LEVELS];

/* ---- Configuration record (LADDER.DAT) -------------------------------- */

/* Cursor-addressing styles supported by the terminal layer.
 *
 * CM_ANSI:   ESC-lead, then decimal row, sep, decimal col, term.
 *            (VT100/VT220/ANSI: lead="\033[", sep=";", term="H", 1-based)
 * CM_OFFSET: lead, then a single byte (coord+offset) for row and col.
 *            (VT52: lead="\033Y", offset=32, row first, 1-based)
 *            (ADM-3A/Kaypro: lead="\033=", offset=32, row first, 1-based)
 */
enum { CM_ANSI = 0, CM_OFFSET = 1 };

#define CAP_LEN 16

typedef struct {
    char magic[4];          /* "LDR1"                                        */

    /* Terminal capabilities */
    char term_name[16];     /* human-readable, e.g. "VT100"                  */
    int  cm_mode;           /* CM_ANSI or CM_OFFSET                          */
    int  cm_row_first;      /* 1 = row before col                           */
    int  cm_base;           /* 0 or 1: value added so top-left maps here     */
    int  cm_offset;         /* CM_OFFSET only: byte offset (e.g. 32)         */
    char cm_lead[CAP_LEN];  /* string emitted before coordinates            */
    char cm_sep[CAP_LEN];   /* CM_ANSI only: between row and col             */
    char cm_term[CAP_LEN];  /* CM_ANSI only: after col                      */
    char cap_clear[CAP_LEN];/* clear whole screen + home                     */
    char cap_curoff[CAP_LEN];/* hide cursor  (may be empty)                  */
    char cap_curon[CAP_LEN];/* show cursor  (may be empty)                   */

    /* Movement keys (single characters, lower-cased on compare) */
    char key_up, key_down, key_left, key_right;
    char key_jump, key_pause, key_quit;
    char key_faster, key_slower;

    int  sound;             /* 1 = terminal bell effects on                  */
    int  start_speed;       /* default play-speed index 0..4                 */
} Config;

/* config.c */
int  config_load(const char *path, Config *cfg);   /* 0 ok, <0 not found    */
int  config_save(const char *path, const Config *cfg);
void config_defaults(Config *cfg);                 /* VT100 defaults        */

/* Terminal presets (config.c). Returns 0 on success, <0 if unknown name. */
int  config_apply_preset(Config *cfg, const char *term_name);
const char *config_preset_name(int index);         /* NULL past the end     */

#define LADDER_DAT "LADDER.DAT"

#endif /* LADDER_H */
