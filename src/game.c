/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Joe <joe@n9tax.com> */
/*
 * game.c - VAX Ladder game core and main loop.
 *
 * A faithful C port of the mechanics from the reference implementations
 * (the Turbo Pascal, C++, and JavaScript versions in this repository).
 * The movement state machine, jump arcs, falling "Der" rocks, scoring, and
 * level-cycling difficulty ramp all follow the JavaScript reference, which
 * is the most thoroughly documented of the three.
 *
 * Rendering goes through term.c (an 80x24 differential text screen) and all
 * OS-specific I/O through plat.h, so this file is plain portable C.
 */
#include "ladder.h"
#include "term.h"
#include "plat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Movement states (see Entity.js in the reference) ------------------ */
enum {
    ST_STOPPED = 1, ST_UP, ST_LEFT, ST_DOWN, ST_RIGHT, ST_FALLING,
    ST_START_JUMP, ST_JUMP_LEFT, ST_JUMP_RIGHT, ST_JUMP_UP,
    ST_DYING, ST_DEAD, ST_SPAWNING
};

/* ---- Input actions ----------------------------------------------------- */
enum {
    A_NONE = 0, A_UP, A_DOWN, A_LEFT, A_RIGHT, A_JUMP,
    A_PAUSE, A_RESUME, A_QUIT, A_FASTER, A_SLOWER, A_STOP
};

typedef struct {
    int x, y;
    int state, next_state;   /* next_state 0 == "none" (JS undefined)       */
    int jump_step;
    int death_step;
    int spawn_step;
    int alive;               /* rock slot in use                            */
} Entity;

typedef struct { int x, y; } Point;

typedef struct {
    char layout[LEVEL_ROWS][LEVEL_COLS + 1];
    Point dispensers[MAX_DISPENSERS];
    int   num_dispensers;
    Entity player;
    Entity rocks[ROCK_LIMIT];
    int    num_rocks;
    int    time;
    int    winning;
} Field;

typedef struct {
    int score;
    int level_number;   /* 0-based; % NUM_LEVELS selects a layout           */
    int level_cycle;
    int lives;
    int next_life;
    int paused;
    int speed;          /* index into PLAY_SPEED_MS                         */
    Field field;
} Session;

static Config cfg;

/* Current on-screen "back chatter" message and how many move-frames it has
 * left before it clears (see set_message / the TAUNTS table below). */
static char msg_text[80];
static int  msg_timer = 0;

/* ---- Jump arcs (Entity.js JUMP_FRAMES) -------------------------------- */
static const int JUMP_RIGHT_DX[6] = { 1, 1, 1, 1, 1, 1 };
static const int JUMP_RIGHT_DY[6] = { -1, -1, 0, 0, 1, 1 };
static const int JUMP_LEFT_DX[6]  = { -1, -1, -1, -1, -1, -1 };
static const int JUMP_LEFT_DY[6]  = { -1, -1, 0, 0, 1, 1 };
static const int JUMP_UP_DX[6]    = { 0, 0, 0, 0, 0, 0 };
static const int JUMP_UP_DY[6]    = { -1, -1, 0, 1, 1, 0 };

static const char DEATH_FRAMES[] = {
    'p','p','b','b','d','d','q','q','p','p','b','b','d','d','q','q',
    '-','-','_','_','_','_','_'
};
#define DEATH_FRAME_COUNT (int)(sizeof(DEATH_FRAMES))

static const char SPAWN_FRAMES[] = { 'v','v','v','v','v','v','.','.','.' };
#define SPAWN_FRAME_COUNT (int)(sizeof(SPAWN_FRAMES))

static const char ROCK_DEATH_FRAMES[] = { '.',':','.' };
#define ROCK_DEATH_FRAME_COUNT (int)(sizeof(ROCK_DEATH_FRAMES))

/* ---- Random helpers ---------------------------------------------------- */
static int rnd(int n) { return (int)(rand() % n); }
static int coin(void) { return (rand() & 1); }
/* returns 1 with probability > threshold (0..1) matching JS Math.random() */
static int chance_gt(double threshold)
{
    double r = (double)rand() / ((double)RAND_MAX + 1.0);
    return r > threshold;
}

/* ---- Field terrain queries -------------------------------------------- */

static char ch_at(Field *f, int x, int y)
{
    if (y < 0) return ' ';
    if (y >= LEVEL_ROWS) return '=';        /* floor below the field        */
    if (x < 0 || x >= LEVEL_COLS) return '='; /* solid walls off-screen     */
    return f->layout[y][x];
}

static int on_solid(Field *f, int x, int y)
{
    char below = ch_at(f, x, y + 1);
    char here  = ch_at(f, x, y);
    return below == T_FLOOR || below == T_DISFLOOR || below == T_LADDER ||
           below == T_SIGN  || here == T_LADDER;
}

static int empty_space(Field *f, int x, int y)
{
    char t;
    if (x < 0 || x >= LEVEL_COLS) return 0;
    t = ch_at(f, x, y);
    return !(t == T_SIGN || t == T_FLOOR);
}

static int is_ladder(Field *f, int x, int y)   { return ch_at(f, x, y) == T_LADDER; }
static int is_statue(Field *f, int x, int y)   { return ch_at(f, x, y) == T_STATUE; }
static int is_treasure(Field *f, int x, int y) { return ch_at(f, x, y) == T_TREASURE; }
static int is_trampoline(Field *f,int x,int y) { return ch_at(f, x, y) == T_TRAMPOLINE; }
static int is_eater(Field *f, int x, int y)    { return ch_at(f, x, y) == T_EATER; }
static int is_fire(Field *f, int x, int y)     { return ch_at(f, x, y) == T_FIRE; }
static int is_disfloor(Field *f, int x, int y) { return ch_at(f, x, y) == T_DISFLOOR; }

static int can_climb_up(Field *f, int x, int y)
{
    char t;
    if (y < 0) return 0;
    t = ch_at(f, x, y);
    return t == T_LADDER || t == T_STATUE || t == T_TREASURE;
}

static int can_climb_down(Field *f, int x, int y)
{
    char t = ch_at(f, x, y);
    return t == T_LADDER || t == T_STATUE || t == T_TREASURE ||
           t == T_SPACE  || t == T_FIRE   || t == T_TRAMPOLINE;
}

/* ---- Movement state machine (Entity.js applyEntityMovement) ----------- */

static void apply_movement(Entity *e, Field *f)
{
    int repeat;

again:
    repeat = 0;

    if (e->next_state) {
        switch (e->state) {
        case ST_STOPPED:
        case ST_LEFT:
        case ST_RIGHT:
            if (e->next_state == ST_LEFT || e->next_state == ST_RIGHT ||
                e->next_state == ST_STOPPED) {
                e->state = e->next_state;
                e->next_state = 0;
            }
            break;
        case ST_UP:
        case ST_DOWN:
            if (e->next_state == ST_LEFT || e->next_state == ST_RIGHT) {
                e->state = e->next_state;
                e->next_state = 0;
            }
            break;
        case ST_JUMP_LEFT:
        case ST_JUMP_RIGHT:
        case ST_JUMP_UP:
            if (e->next_state == ST_RIGHT && e->state != ST_JUMP_RIGHT) {
                e->state = ST_JUMP_RIGHT; e->next_state = ST_RIGHT;
            }
            if (e->next_state == ST_LEFT && e->state != ST_JUMP_LEFT) {
                e->state = ST_JUMP_LEFT;  e->next_state = ST_LEFT;
            }
            if (e->next_state == ST_DOWN) {
                e->state = ST_FALLING;    e->next_state = 0;
            }
            /* UP is intentionally left queued */
            break;
        }
    }

    if (e->next_state == ST_START_JUMP) {
        if (on_solid(f, e->x, e->y)) {
            if (e->state == ST_STOPPED || e->state == ST_FALLING) {
                e->state = ST_JUMP_UP;    e->jump_step = 0; e->next_state = ST_STOPPED;
            } else if (e->state == ST_LEFT || e->state == ST_JUMP_LEFT) {
                e->state = ST_JUMP_LEFT;  e->jump_step = 0; e->next_state = ST_LEFT;
            } else if (e->state == ST_RIGHT || e->state == ST_JUMP_RIGHT) {
                e->state = ST_JUMP_RIGHT; e->jump_step = 0; e->next_state = ST_RIGHT;
            }
        }
        /* else leave START_JUMP queued to allow chain-jumping */
    } else if (e->next_state == ST_UP && is_ladder(f, e->x, e->y)) {
        e->state = ST_UP; e->next_state = 0;
    } else if (e->next_state == ST_DOWN &&
               (is_ladder(f, e->x, e->y) || is_ladder(f, e->x, e->y + 1))) {
        e->state = ST_DOWN; e->next_state = 0;
    }

    switch (e->state) {
    case ST_LEFT:
        if (!on_solid(f, e->x, e->y)) {
            e->next_state = ST_LEFT; e->state = ST_FALLING; repeat = 1; break;
        }
        if (empty_space(f, e->x - 1, e->y)) e->x--;
        else e->next_state = ST_STOPPED;
        break;

    case ST_RIGHT:
        if (!on_solid(f, e->x, e->y)) {
            e->next_state = ST_RIGHT; e->state = ST_FALLING; repeat = 1; break;
        }
        if (empty_space(f, e->x + 1, e->y)) e->x++;
        else e->next_state = ST_STOPPED;
        break;

    case ST_UP:
        if (can_climb_up(f, e->x, e->y - 1)) e->y--;
        else e->state = ST_STOPPED;
        break;

    case ST_DOWN:
        if (can_climb_down(f, e->x, e->y + 1)) e->y++;
        else e->state = ST_STOPPED;
        break;

    case ST_JUMP_RIGHT:
    case ST_JUMP_LEFT:
    case ST_JUMP_UP: {
        const int *dxs, *dys;
        int dx, dy;
        if (e->state == ST_JUMP_RIGHT)     { dxs = JUMP_RIGHT_DX; dys = JUMP_RIGHT_DY; }
        else if (e->state == ST_JUMP_LEFT) { dxs = JUMP_LEFT_DX;  dys = JUMP_LEFT_DY; }
        else                               { dxs = JUMP_UP_DX;    dys = JUMP_UP_DY; }
        dx = dxs[e->jump_step];
        dy = dys[e->jump_step];

        if (e->x + dx >= 0 && e->x + dx < LEVEL_COLS) {
            char terrain = ch_at(f, e->x + dx, e->y + dy);
            if (terrain == T_FLOOR || terrain == T_SIGN || terrain == T_DISFLOOR) {
                if (on_solid(f, e->x, e->y)) {
                    e->state = e->next_state; e->next_state = 0;
                } else {
                    if (e->state == ST_JUMP_RIGHT)     e->next_state = ST_RIGHT;
                    else if (e->state == ST_JUMP_LEFT) e->next_state = ST_LEFT;
                    else                               e->next_state = ST_UP;
                    e->state = ST_FALLING;
                }
            } else if (terrain == T_LADDER) {
                e->x += dx; e->y += dy;
                if (e->next_state == ST_UP) e->state = ST_UP;
                else                        e->state = ST_STOPPED;
                e->next_state = 0;
            } else {
                e->x += dx; e->y += dy; e->jump_step++;
                if (e->jump_step >= 6) {
                    if (e->state == ST_JUMP_RIGHT)     e->state = ST_RIGHT;
                    else if (e->state == ST_JUMP_LEFT) e->state = ST_LEFT;
                    else                               e->state = ST_UP;
                }
            }
        } else {
            if (on_solid(f, e->x, e->y)) {
                e->state = e->next_state; e->next_state = 0;
            } else {
                e->state = ST_FALLING; e->next_state = ST_STOPPED;
            }
        }
        break;
    }

    case ST_FALLING:
        if (on_solid(f, e->x, e->y)) e->state = e->next_state ? e->next_state : ST_STOPPED;
        else e->y++;
        break;
    }

    if (repeat) goto again;
}

/* ---- Player ------------------------------------------------------------ */

static void player_kill(Session *s)
{
    Entity *p = &s->field.player;
    if (p->state != ST_DYING && p->state != ST_DEAD) {
        p->state = ST_DYING;
        term_bell();
    }
}

static char player_char(Entity *p)
{
    switch (p->state) {
    case ST_RIGHT: case ST_JUMP_RIGHT: case ST_UP: case ST_DOWN: return 'p';
    case ST_LEFT:  case ST_JUMP_LEFT:  return 'q';
    case ST_FALLING: return 'b';
    case ST_DYING:
        if (p->death_step < DEATH_FRAME_COUNT) return DEATH_FRAMES[p->death_step];
        return '_';
    case ST_DEAD: return '_';
    default: return 'g';
    }
}

static void player_update(Session *s, int action)
{
    Entity *p = &s->field.player;

    if (p->state == ST_DYING) {
        p->death_step++;
        if (p->death_step >= DEATH_FRAME_COUNT) p->state = ST_DEAD;
    }
    if (p->state == ST_DYING || p->state == ST_DEAD) return;

    switch (action) {
    case A_LEFT:  p->next_state = ST_LEFT;  break;
    case A_RIGHT: p->next_state = ST_RIGHT; break;
    case A_UP:    p->next_state = ST_UP;    break;
    case A_DOWN:  p->next_state = ST_DOWN;  break;
    case A_JUMP:  p->next_state = ST_START_JUMP; break;
    default: break;
    }

    apply_movement(p, &s->field);
}

/* ---- Rocks (Rock.js) --------------------------------------------------- */

static char rock_char(Entity *r)
{
    switch (r->state) {
    case ST_SPAWNING:
        if (r->spawn_step < SPAWN_FRAME_COUNT) return SPAWN_FRAMES[r->spawn_step];
        return 'o';
    case ST_DYING:
        if (r->death_step < ROCK_DEATH_FRAME_COUNT) return ROCK_DEATH_FRAMES[r->death_step];
        return '.';
    default: return 'o';
    }
}

static void rock_update(Field *f, Entity *r)
{
    if (r->state == ST_DYING) {
        r->death_step++;
        if (r->death_step >= ROCK_DEATH_FRAME_COUNT) r->state = ST_DEAD;
    }
    if (r->state == ST_SPAWNING) {
        r->spawn_step++;
        if (r->spawn_step >= SPAWN_FRAME_COUNT) r->state = ST_FALLING;
    }
    if (r->state == ST_SPAWNING || r->state == ST_DYING || r->state == ST_DEAD)
        return;

    if (r->state == ST_STOPPED) {
        if (r->x == 0 || !empty_space(f, r->x - 1, r->y))
            r->next_state = ST_RIGHT;
        else if (r->x == LEVEL_COLS - 1 || !empty_space(f, r->x + 1, r->y))
            r->next_state = ST_LEFT;
        else
            r->next_state = coin() ? ST_LEFT : ST_RIGHT;
    }

    if (r->x == 0 && r->state == ST_LEFT)               r->state = ST_RIGHT;
    if (r->x == LEVEL_COLS - 1 && r->state == ST_RIGHT) r->state = ST_LEFT;

    if (r->state != ST_FALLING && !on_solid(f, r->x, r->y))
        r->next_state = ST_FALLING;

    if (is_ladder(f, r->x, r->y + 1) &&
        (r->state == ST_LEFT || r->state == ST_RIGHT)) {
        int roll = rnd(4);  /* LEFT, RIGHT, DOWN, DOWN */
        r->next_state = (roll == 0) ? ST_LEFT : (roll == 1) ? ST_RIGHT : ST_DOWN;
    }

    if (is_eater(f, r->x, r->y)) {
        r->state = ST_DYING;
        return;
    }

    apply_movement(r, f);
}

/* ---- Level loading ----------------------------------------------------- */

static void field_load(Field *f, int level_number)
{
    const Level *lv = &LEVELS[level_number % NUM_LEVELS];
    int x, y;

    memset(f, 0, sizeof(*f));
    f->num_dispensers = 0;
    f->num_rocks = 0;
    f->time = START_BONUS_TIME;
    f->winning = 0;

    for (y = 0; y < LEVEL_ROWS; y++) {
        memcpy(f->layout[y], lv->layout[y], LEVEL_COLS);
        f->layout[y][LEVEL_COLS] = '\0';
        for (x = 0; x < LEVEL_COLS; x++) {
            char c = f->layout[y][x];
            if (c == T_DISPENSER) {
                if (f->num_dispensers < MAX_DISPENSERS) {
                    f->dispensers[f->num_dispensers].x = x;
                    f->dispensers[f->num_dispensers].y = y;
                    f->num_dispensers++;
                }
            } else if (c == 'p') {
                f->layout[y][x] = ' ';
                f->player.x = x;
                f->player.y = y;
            }
        }
    }

    f->player.state = ST_STOPPED;
    f->player.next_state = ST_STOPPED;
    f->player.jump_step = 0;
    f->player.death_step = 0;
}

/* ---- Session helpers --------------------------------------------------- */

static int hidden_factor(Session *s)
{
    return s->level_number / NUM_LEVELS;
}

static int max_rocks(Session *s)
{
    return MAX_ROCKS + s->field.num_dispensers * DISPENSER_MAX_ROCKS +
           hidden_factor(s) * HIDDEN_FACTOR_MAX_ROCKS;
}

static int move_delay_ms(Session *s)
{
    int base = PLAY_SPEED_MS[s->speed];
    int reduce = hidden_factor(s) * HIDDEN_FACTOR_PLAY_SPEED_PCT * base / 100;
    int d = base - reduce;
    return d < 5 ? 5 : d;
}

static void add_score(Session *s, int kind)
{
    switch (kind) {
    case 1: s->score += SCORE_PER_ROCK; break;             /* rock  */
    case 2: s->score += s->field.time;  break;             /* statue */
    case 3: s->score += SCORE_PER_TREASURE_TICK; break;    /* treasure tick */
    }
    if (s->score >= s->next_life) {
        s->lives++;
        s->next_life += NEW_LIFE_SCORE;
        term_bell();
    }
}

/* Ported from PlayingField.checkIfPlayerShouldDie */
static void check_player_death(Session *s)
{
    Field *f = &s->field;
    Entity *p = &f->player;
    int i;

    if (p->state == ST_DYING || p->state == ST_DEAD) return;

    if (is_fire(f, p->x, p->y)) player_kill(s);
    if (f->time <= 0)          player_kill(s);

    for (i = 0; i < f->num_rocks; i++) {
        Entity *r = &f->rocks[i];
        if (r->state == ST_DEAD) continue;
        if (p->x != r->x) continue;
        if (p->y == r->y) {
            player_kill(s);
            r->state = ST_DEAD;    /* the rock that hit us disappears */
            break;
        } else if (p->y == r->y - 1 && empty_space(f, p->x, p->y + 1)) {
            add_score(s, 1);
        } else if (p->y == r->y - 2 && empty_space(f, p->x, p->y + 1) &&
                   empty_space(f, p->x, p->y + 2)) {
            add_score(s, 1);
        }
    }
}

static void spawn_rock(Field *f)
{
    Point *d;
    Entity *r;
    if (f->num_rocks >= ROCK_LIMIT) return;
    d = &f->dispensers[rnd(f->num_dispensers)];
    r = &f->rocks[f->num_rocks++];
    memset(r, 0, sizeof(*r));
    r->x = d->x;
    r->y = d->y;
    r->state = ST_SPAWNING;
    r->next_state = 0;
}

static void reap_dead_rocks(Field *f)
{
    int i = 0, j = 0;
    for (i = 0; i < f->num_rocks; i++) {
        if (f->rocks[i].state != ST_DEAD) {
            if (i != j) f->rocks[j] = f->rocks[i];
            j++;
        }
    }
    f->num_rocks = j;
}

/* ---- Drawing ----------------------------------------------------------- */

static void draw_field(Session *s)
{
    Field *f = &s->field;
    int y, i;
    char buf[128];

    term_clear();

    for (y = 0; y < LEVEL_ROWS; y++)
        term_puts(0, y, f->layout[y]);

    /* Rocks under the player, player on top (matches reference draw order) */
    for (i = 0; i < f->num_rocks; i++) {
        if (f->rocks[i].state == ST_DEAD) continue;
        term_put(f->rocks[i].x, f->rocks[i].y, rock_char(&f->rocks[i]));
    }
    term_put(f->player.x, f->player.y, player_char(&f->player));

    sprintf(buf, "Lads %3d    Level %3d      Score %7d      Bonus time %5d",
            s->lives, (s->level_number % NUM_LEVELS) + 1, s->score, f->time);
    term_puts(0, STATUS_ROW, buf);

    if (s->paused)
        term_puts(0, MESSAGE_ROW, "Paused - press the pause key or RETURN to continue.");
    else if (msg_timer > 0)
        term_puts(0, MESSAGE_ROW, msg_text);
}

/* ---- Input ------------------------------------------------------------- */

/* Small persistent parser state so an ESC-[ arrow sequence split across
 * two poll cycles is still recognised. */
static int esc_state = 0;   /* 0 none, 1 saw ESC, 2 saw ESC[ */
/* Rolling buffer of recent keys for cheat detection. */
static char keyhist[16];
static int  keyhist_len = 0;

/* ---- On-screen "back chatter" messages (from the original) ------------- */
static void set_message(const char *s, int frames)
{
    strncpy(msg_text, s, sizeof(msg_text) - 1);
    msg_text[sizeof(msg_text) - 1] = '\0';
    msg_timer = frames;
}

/* The two random taunts the original threw up while you played. */
static const char *TAUNTS[] = {
    "You eat quiche!",
    "Come on, we don't have all day!"
};
#define NUM_TAUNTS (int)(sizeof(TAUNTS) / sizeof(TAUNTS[0]))

/* Congratulation shown each time you clear all seven levels (1-based cycle,
 * clamped to the last entry), straight from the Turbo Pascal version. */
static const char *CYCLE_MSG[] = {
    "You really don't deserve this but...",
    "Not bad for a young Lad",
    "Amazing!  You rate!!",
    "Looks like we have a Lad-Der here",
    "Yeah! Now you are a Lad-Wiz!",
    "Wow! You are now a Lad-Guru!",
    "You are a true Lad-Master!!!"
};
#define NUM_CYCLE_MSG (int)(sizeof(CYCLE_MSG) / sizeof(CYCLE_MSG[0]))

static void keyhist_push(char c)
{
    if (keyhist_len < (int)sizeof(keyhist)) {
        keyhist[keyhist_len++] = c;
    } else {
        memmove(keyhist, keyhist + 1, sizeof(keyhist) - 1);
        keyhist[sizeof(keyhist) - 1] = c;
    }
}

static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

/* Map a single byte to an action using the configured keys. */
static int map_key(char c)
{
    char k = lc(c);
    if (k == lc(cfg.key_up))     return A_UP;
    if (k == lc(cfg.key_down))   return A_DOWN;
    if (k == lc(cfg.key_left))   return A_LEFT;
    if (k == lc(cfg.key_right))  return A_RIGHT;
    if (c  == cfg.key_jump)      return A_JUMP;
    if (k == lc(cfg.key_pause))  return A_PAUSE;
    if (k == lc(cfg.key_quit))   return A_QUIT;
    if (c  == cfg.key_faster)    return A_FASTER;
    if (c  == cfg.key_slower)    return A_SLOWER;
    /* Convenience: WASD always works too. */
    if (k == 'w') return A_UP;
    if (k == 's') return A_DOWN;
    if (k == 'a') return A_LEFT;
    if (k == 'd') return A_RIGHT;
    if (c == ' ') return A_JUMP;
    if (c == '\r' || c == '\n') return A_RESUME;
    return A_NONE;
}

/*
 * Drain all pending input.  Returns the last *movement* action seen (so the
 * newest direction wins, matching the reference's lastAction()).  Immediate
 * actions (pause/quit/speed) are reported through the out-params.
 */
static int drain_input(int *quit, int *pause_toggle, int *speed_delta,
                       int *resume)
{
    int c;
    int move_action = A_NONE;

    while ((c = plat_getkey()) >= 0) {
        char ch = (char)c;

        /* Arrow-key parser.  DEC terminals send cursor keys either as CSI
         * "ESC [ A" (ANSI cursor-key mode) or as SS3 "ESC O A" (application
         * cursor-key mode, common over SET HOST / on real VT terminals), so
         * we accept both introducers here. */
        if (esc_state == 1) {
            if (ch == '[' || ch == 'O') { esc_state = 2; continue; }
            esc_state = 0;  /* lone ESC - treat as resume */
            *resume = 1;
            /* fall through to process ch normally below */
        } else if (esc_state == 2) {
            esc_state = 0;
            switch (ch) {
            case 'A': move_action = A_UP;    break;
            case 'B': move_action = A_DOWN;  break;
            case 'C': move_action = A_RIGHT; break;
            case 'D': move_action = A_LEFT;  break;
            default: break;
            }
            continue;
        }
        if (c == 27) { esc_state = 1; continue; }   /* 7-bit ESC introducer */

        /* 8-bit C1 controls: some DEC terminals send cursor keys as a single
         * high byte - CSI = 0x9B, SS3 = 0x8F - rather than the ESC pair. */
        if (c == 0x9B || c == 0x8F) { esc_state = 2; continue; }

        /* The OpenVMS terminal driver swallows the leading ESC of an escape
         * sequence (it comes back as a read terminator with no data), so
         * cursor keys arrive as a bare "[ A" (CSI) or "O A" (SS3).  Neither
         * '[' nor 'O' is a game key, so treat them as introducers as well;
         * this is what actually makes the arrow keys work on a real VAX. */
        if (c == '[' || c == 'O') { esc_state = 2; continue; }

        keyhist_push(ch);

        switch (map_key(ch)) {
        case A_QUIT:   *quit = 1; break;
        case A_PAUSE:  *pause_toggle = 1; break;
        case A_RESUME: *resume = 1; break;
        case A_FASTER: (*speed_delta)++; break;
        case A_SLOWER: (*speed_delta)--; break;
        case A_UP: case A_DOWN: case A_LEFT: case A_RIGHT: case A_JUMP:
            move_action = map_key(ch);
            break;
        default: break;
        }
    }
    return move_action;
}

static int keyhist_has(const char *pat)
{
    /* naive substring search over the recent-key buffer */
    int n = (int)strlen(pat);
    int i, j;
    for (i = 0; i + n <= keyhist_len; i++) {
        for (j = 0; j < n; j++)
            if (lc(keyhist[i + j]) != lc(pat[j])) break;
        if (j == n) return 1;
    }
    return 0;
}

/* ---- End-of-level bonus tally ----------------------------------------- */

static void run_bonus_tally(Session *s)
{
    Field *f = &s->field;
    /* Convert remaining bonus time into score, animated. */
    while (f->time > 0) {
        int step = f->time >= 20 ? 20 : f->time;
        f->time -= step;
        s->score += step;
        if (s->score >= s->next_life) {
            s->lives++; s->next_life += NEW_LIFE_SCORE; term_bell();
        }
        draw_field(s);
        term_refresh();
        plat_wait_ms(10);
    }
    term_bell();
}

/* ---- Menus ------------------------------------------------------------- */

static void draw_centered(int row, const char *s)
{
    int col = (SCREEN_COLS - (int)strlen(s)) / 2;
    if (col < 0) col = 0;
    term_puts(col, row, s);
}

/* Returns: 1 = start game, 0 = quit. */
static int main_menu(Session *s)
{
    for (;;) {
        int quit = 0, pause = 0, sd = 0, resume = 0, act;
        int c;
        char line[80];

        term_clear();
        draw_centered(1, "L   A   D   D   E   R");
        draw_centered(3, "The 1982 CP/M arcade classic, ported to OpenVMS/VAX");
        draw_centered(5, "Climb the ladders, grab the statues, reach the treasure ($),");
        draw_centered(6, "and don't get flattened by Der rocks.");

        draw_centered(9,  "Move:  I=up  K=down  J=left  L=right  (arrows/WASD too)");
        draw_centered(10, "SPACE = jump      P = pause      + / - = speed");

        sprintf(line, "Play speed:  %d of %d   (fastest is %d)",
                s->speed + 1, NUM_PLAY_SPEEDS, NUM_PLAY_SPEEDS);
        draw_centered(13, line);

        draw_centered(16, "Press SPACE or RETURN to start");
        draw_centered(17, "Press  I  for instructions");
        draw_centered(18, "Press  Q  to quit");

        draw_centered(22, cfg.term_name);
        term_refresh();

        /* Poll for a decision */
        act = drain_input(&quit, &pause, &sd, &resume);
        (void)act;
        if (quit) return 0;
        if (sd) {
            s->speed += (sd > 0) ? 1 : -1;
            if (s->speed < 0) s->speed = 0;
            if (s->speed >= NUM_PLAY_SPEEDS) s->speed = NUM_PLAY_SPEEDS - 1;
        }
        /* Look for explicit start / instruction keys in the history */
        c = -1;
        if (resume) c = '\r';
        if (keyhist_has(" "))  { keyhist_len = 0; return 1; }
        if (keyhist_has("i")) {
            keyhist_len = 0;
            /* instructions screen */
            term_clear();
            draw_centered(1, "HOW TO PLAY LADDER");
            term_puts(6, 4,  "=   floor            H   ladder (climb up/down)");
            term_puts(6, 5,  "&   statue  - grab it for points (worth the bonus time)");
            term_puts(6, 6,  "$   treasure - touch it to finish the level");
            term_puts(6, 7,  "^   fire     - deadly, do not touch");
            term_puts(6, 8,  "V   dispenser - Der rocks fall from here");
            term_puts(6, 9,  "o   Der rock  - one touch and you're done");
            term_puts(6, 10, "-   crumbling floor - vanishes once you step off it");
            term_puts(6, 11, ".   trampoline - bounces you unpredictably");
            term_puts(6, 13, "Bonus time ticks down the whole level; run out and you die.");
            term_puts(6, 14, "You get a fresh life every 10,000 points.");
            draw_centered(20, "Press any key to return to the menu");
            term_refresh();
            /* wait for a keypress */
            for (;;) {
                if (plat_getkey() >= 0) break;
                plat_wait_ms(30);
            }
            keyhist_len = 0;
            continue;
        }
        if (c == '\r') { keyhist_len = 0; return 1; }

        plat_wait_ms(30);
    }
}

/* ---- Playing one level, one life -------------------------------------- */

/* Returns: 0 keep playing, 1 level complete, 2 died, 3 quit to menu. */
static int play_level(Session *s)
{
    Field *f = &s->field;

    field_load(f, s->level_number);
    term_bell();  /* level start */
    set_message("Get ready!", 12);

    for (;;) {
        int quit = 0, pause_toggle = 0, speed_delta = 0, resume = 0;
        int action;
        int i;

        action = drain_input(&quit, &pause_toggle, &speed_delta, &resume);

        if (quit) return 3;

        if (speed_delta) {
            s->speed += (speed_delta > 0) ? 1 : -1;
            if (s->speed < 0) s->speed = 0;
            if (s->speed >= NUM_PLAY_SPEEDS) s->speed = NUM_PLAY_SPEEDS - 1;
        }

        /* Pause handling */
        if (s->paused) {
            if (pause_toggle || resume) s->paused = 0;
            draw_field(s);
            term_refresh();
            plat_wait_ms(50);
            continue;
        } else if (pause_toggle) {
            s->paused = 1;
            draw_field(s);
            term_refresh();
            plat_wait_ms(50);
            continue;
        }

        /* Cheat: jump to a level (IDCLEVnn), or instantly win (IDKFA). */
        if (keyhist_has("idkfa")) { keyhist_len = 0; f->winning = 1; }

        /* Count down bonus time */
        if (f->time > 0) f->time--;

        /* Back chatter: age the current message, and now and then throw up
         * one of the original's random taunts. */
        if (msg_timer > 0) msg_timer--;
        else if (chance_gt(0.99)) set_message(TAUNTS[rnd(NUM_TAUNTS)], 18);

        {   /* Move player, then handle disappearing floors */
            int oldx = f->player.x, oldy = f->player.y;
            player_update(s, action);
            if (oldx != f->player.x && oldy == f->player.y) {
                if (is_disfloor(f, oldx, oldy + 1))
                    f->layout[oldy + 1][oldx] = ' ';
            }
        }

        check_player_death(s);

        for (i = 0; i < f->num_rocks; i++)
            rock_update(f, &f->rocks[i]);

        check_player_death(s);

        /* Collect statues */
        if (is_statue(f, f->player.x, f->player.y)) {
            f->layout[f->player.y][f->player.x] = ' ';
            add_score(s, 2);
        }

        /* Treasure ends the level */
        if (is_treasure(f, f->player.x, f->player.y)) {
            set_message("Hooka!", 999);   /* stays up through the bonus tally */
            f->winning = 1;
        }

        /* Trampoline bounce */
        if (is_trampoline(f, f->player.x, f->player.y)) {
            switch (rnd(5)) {
            case 0: f->player.state = ST_LEFT;  f->player.next_state = 0; break;
            case 1: f->player.state = ST_RIGHT; f->player.next_state = 0; break;
            case 2: f->player.state = ST_JUMP_UP; f->player.next_state = 0;
                    f->player.jump_step = 0; break;
            case 3: f->player.state = ST_JUMP_LEFT; f->player.next_state = ST_LEFT;
                    f->player.jump_step = 0; break;
            case 4: f->player.state = ST_JUMP_RIGHT; f->player.next_state = ST_RIGHT;
                    f->player.jump_step = 0; break;
            }
        }

        reap_dead_rocks(f);

        /* Dispense a new rock now and then */
        if (f->num_dispensers > 0 && f->num_rocks < max_rocks(s) && chance_gt(0.91))
            spawn_rock(f);

        /* Death resolution */
        if (f->player.state == ST_DEAD) {
            s->lives--;
            return 2;
        }

        draw_field(s);
        term_refresh();

        if (f->winning) {
            run_bonus_tally(s);
            return 1;
        }

        plat_wait_ms(move_delay_ms(s));
    }
}

/* ---- Game over screen -------------------------------------------------- */

static void game_over(Session *s)
{
    char line[64];
    int t;
    term_clear();
    draw_centered(9, "G A M E   O V E R");
    sprintf(line, "Final score: %d", s->score);
    draw_centered(11, line);
    sprintf(line, "You reached level %d (%s)",
            (s->level_number % NUM_LEVELS) + 1,
            LEVELS[s->level_number % NUM_LEVELS].name);
    draw_centered(12, line);
    draw_centered(15, "Press any key to return to the menu");
    term_refresh();
    for (t = 0; t < 200; t++) {  /* ~6s max, or until a key */
        if (plat_getkey() >= 0) break;
        plat_wait_ms(30);
    }
    while (plat_getkey() >= 0) ; /* drain */
}

/* ---- Cycle-complete congratulations ------------------------------------ */

/* Shown each time the player clears all seven levels.  `cycle` is 1-based
 * (the number of complete cycles just finished). */
static void show_cycle_complete(Session *s, int cycle)
{
    char line[80];
    int idx = cycle - 1;
    int t;
    if (idx < 0) idx = 0;
    if (idx >= NUM_CYCLE_MSG) idx = NUM_CYCLE_MSG - 1;

    term_clear();
    term_puts(10, 7, "YAHOO! YAHOO! YAHOO! YAHOO! YAHOO! YAHOO! YAHOO!");
    draw_centered(10, CYCLE_MSG[idx]);
    sprintf(line, "Score: %d", s->score);
    draw_centered(13, line);
    draw_centered(16, "Press any key to keep climbing");
    term_refresh();

    while (plat_getkey() >= 0) ;           /* drain any typeahead   */
    for (t = 0; t < 300; t++) {            /* wait for a key (~9s)  */
        if (plat_getkey() >= 0) break;
        plat_wait_ms(30);
    }
    while (plat_getkey() >= 0) ;
    term_invalidate();                     /* full redraw next frame */
}

/* ---- Top-level driver -------------------------------------------------- */

static void play_session(Session *s)
{
    s->score = 0;
    s->level_number = 0;
    s->level_cycle = 1;
    s->lives = INITIAL_LIVES;
    s->next_life = NEW_LIFE_SCORE;
    s->paused = 0;

    for (;;) {
        int r = play_level(s);
        if (r == 3) return;                 /* quit to menu               */
        if (r == 1) {                        /* completed level           */
            s->level_number++;
            if (s->level_number % NUM_LEVELS == 0) {
                show_cycle_complete(s, s->level_cycle);
                s->level_cycle++;
            }
            continue;
        }
        if (r == 2) {                        /* died                      */
            if (s->lives <= 0) { game_over(s); return; }
            /* replay same level */
            continue;
        }
    }
}

int main(void)
{
    Session s;

    plat_seed_rng();

    if (config_load(LADDER_DAT, &cfg) != 0) {
        /* No LADDER.DAT: fall back to sane VT100 defaults and tell the
         * player how to configure their terminal. */
        config_defaults(&cfg);
    }

    if (plat_init() != 0) {
        fprintf(stderr, "Ladder: this program must be run on a terminal.\n");
        return 1;
    }

    term_begin(&cfg);

    memset(&s, 0, sizeof(s));
    s.speed = cfg.start_speed;
    if (s.speed < 0 || s.speed >= NUM_PLAY_SPEEDS) s.speed = 1;

    while (main_menu(&s))
        play_session(&s);

    term_end();
    plat_shutdown();
    return 0;
}
