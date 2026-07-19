/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Joe <joe@n9tax.com> */
/*
 * ladconf.c - configuration program for VAX Ladder (the LADCONF analogue).
 *
 * Writes LADDER.DAT, which the game reads at startup.  It is a simple
 * line-oriented question/answer program (like the original LADCONF), so it
 * needs no special terminal handling and runs fine over any connection.
 *
 * It lets you choose a terminal type (which fills in the cursor-addressing
 * and clear-screen escape sequences), set your movement keys, and toggle
 * sound and the default play speed.
 */
#include "ladder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int eq_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++; b++;
    }
    return *a == *b;
}

/* Read a line into buf; returns 1 on success (possibly empty), 0 on EOF. */
static int read_line(char *buf, int size)
{
    int i = 0, c;
    while ((c = getchar()) != EOF && c != '\n') {
        if (i < size - 1) buf[i++] = (char)c;
    }
    buf[i] = '\0';
    return (c != EOF || i > 0);
}

/* Prompt for a single non-space key, returning def if the user just hits
 * RETURN. */
static char ask_key(const char *label, char def)
{
    char buf[64];
    printf("  %-28s [%c] : ", label, def == ' ' ? '_' : def);
    fflush(stdout);
    if (!read_line(buf, sizeof(buf)) || buf[0] == '\0')
        return def;
    /* Allow the word "space" for the space bar. */
    if (eq_ci(buf, "space")) return ' ';
    return buf[0];
}

static int ask_yes_no(const char *label, int def)
{
    char buf[64];
    printf("%s [%s] : ", label, def ? "Y/n" : "y/N");
    fflush(stdout);
    if (!read_line(buf, sizeof(buf)) || buf[0] == '\0')
        return def;
    return (buf[0] == 'y' || buf[0] == 'Y');
}

static void choose_terminal(Config *cfg)
{
    char buf[64];
    int i;

    printf("\nSupported terminal types:\n\n");
    for (i = 0; config_preset_name(i); i++)
        printf("   %d) %s\n", i + 1, config_preset_name(i));
    printf("\nThe original Kaypro II used its built-in ADM-3A-style terminal.\n");
    printf("Most DEC terminals (VT100, VT220, VT320) are option 1.\n\n");

    for (;;) {
        int n;
        printf("Choose terminal type [1] : ");
        fflush(stdout);
        if (!read_line(buf, sizeof(buf)) || buf[0] == '\0') { n = 1; }
        else n = atoi(buf);
        if (n >= 1 && config_preset_name(n - 1)) {
            config_apply_preset(cfg, config_preset_name(n - 1));
            printf("Terminal set to %s.\n", cfg->term_name);
            return;
        }
        printf("Please enter a number from the list.\n");
    }
}

int main(void)
{
    Config cfg;
    int speed;
    char buf[64];

    /* Start from any existing file so re-running only changes what you want. */
    if (config_load(LADDER_DAT, &cfg) != 0)
        config_defaults(&cfg);

    printf("=====================================================\n");
    printf("           VAX Ladder - Terminal Setup\n");
    printf("=====================================================\n");

    choose_terminal(&cfg);

    printf("\nMovement keys (press RETURN to keep the shown default):\n");
    cfg.key_up     = ask_key("Move up",        cfg.key_up);
    cfg.key_down   = ask_key("Move down",      cfg.key_down);
    cfg.key_left   = ask_key("Move left",      cfg.key_left);
    cfg.key_right  = ask_key("Move right",     cfg.key_right);
    cfg.key_jump   = ask_key("Jump",           cfg.key_jump);
    cfg.key_pause  = ask_key("Pause",          cfg.key_pause);
    cfg.key_quit   = ask_key("Quit to menu",   cfg.key_quit);
    cfg.key_faster = ask_key("Speed up",       cfg.key_faster);
    cfg.key_slower = ask_key("Slow down",      cfg.key_slower);

    printf("\n");
    cfg.sound = ask_yes_no("Enable sound (terminal bell)?", cfg.sound);

    printf("\nDefault play speed 1 (slow) to %d (fast) [%d] : ",
           NUM_PLAY_SPEEDS, cfg.start_speed + 1);
    fflush(stdout);
    if (read_line(buf, sizeof(buf)) && buf[0] != '\0') {
        speed = atoi(buf);
        if (speed >= 1 && speed <= NUM_PLAY_SPEEDS)
            cfg.start_speed = speed - 1;
    }

    if (config_save(LADDER_DAT, &cfg) == 0) {
        printf("\nSaved %s.  Run LADDER to play.\n", LADDER_DAT);
        return 0;
    } else {
        printf("\nError: could not write %s.\n", LADDER_DAT);
        return 1;
    }
}
