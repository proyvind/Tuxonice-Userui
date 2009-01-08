/*
 * userui_usplash_core.c - usplash userspace user interface module.
 *
 * Copyright (C) 2005-2007, Bernard Blackham <bernard@blackham.com.au>
 * 
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 */

#include <linux/vt.h>
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "usplash_backend.h"
#include "libusplash.h"
#include "../userui.h"

#define MIN_ADVANCE_PERCENT 5

static int usplash_ready = 0;
static int userui_usplash_xres = 0;
static int userui_usplash_yres = 0;
static int userui_usplash_verbose = 1;

static void read_usplash_conf() {
    char s[1024];
    FILE *f;

    f = fopen("/etc/usplash.conf", "r");
    if (f == NULL)
	return;

    while (fgets(s, sizeof(s), f)) {
	char *p, *varname, *varvalue;

	/* Oh, how I long for a regex. */
	/* m/^\s*([a-z]+)\s*=\s*([0-9]+)\s*$/ */

	/* Trim leading whitespace */
	p = s;
	while (isspace(*p))
	    p++;

	/* Match [a-z]+ */
	if (*p == '\0' || !isalpha(*p))
	    continue;
	varname = p;
	while (isalpha(*p))
	    p++;

	/* Match \s*= and null-terminate */
	if (*p == '=')
	    *p++ = '\0';
        else if (isspace(*p)) {
	    *p++ = '\0';
	    while (isspace(*p))
		p++;
	    if (*p != '=')
		continue;
	    p++;
	} else
	    continue;

	/* Snip more whitespace */
	while (isspace(*p))
	    p++;

	/* Match [0-9]+ */
	if (*p == '\0' || !isdigit(*p))
	    continue;
	varvalue = p;
	while (isdigit(*p))
	    p++;

	/* Null terminate value */
	if (*p != '\0')
	    *p++ = '\0';

	/* Trim trailing whitespace */
	while (isspace(*p))
	    p++;

	/* Make sure we hit the end. */
	if (*p != '\0')
	    continue;

	/* Yay! Now see if it's useful. */
	if (strcmp(varname, "xres") == 0 && userui_usplash_xres == 0)
	    userui_usplash_xres = atoi(varvalue);
	else if (strcmp(varname, "yres") == 0 && userui_usplash_yres == 0)
	    userui_usplash_yres = atoi(varvalue);
    }

    fclose(f);
}

static void userui_usplash_prepare() {
    struct termios t;
    int have_termios;
    struct rlimit r;

    /* Read the usplash.conf file (not quite bash-like, but hopefully good
     * enough. */
    read_usplash_conf();

    /* Set RLIMIT_NPROC now to prevent svgalib from forking. */
    r.rlim_cur = r.rlim_max = 0;
    setrlimit(RLIMIT_NPROC, &r);

    /* Save termios as usplash does silly things with it.  (specifically, makes
     * \n send SIGQUIT). We should already have all the termios we need.
     */
    have_termios = (-1 != tcgetattr(STDIN_FILENO, &t));

    if (usplash_setup(userui_usplash_xres, userui_usplash_yres,
			userui_usplash_verbose)) {
	usplash_restore_console();
	if (have_termios)
	    tcsetattr(STDIN_FILENO, TCSANOW, &t);
	fprintf(stderr, "Failed to initialise usplash!\n");
	//exit (2);
	return;
    }

    if (have_termios)
	tcsetattr(STDIN_FILENO, TCSANOW, &t);

    clear_screen();
    clear_progressbar();
    clear_text();

    usplash_ready = 1;
}

static void userui_usplash_cleanup() {
    if (!usplash_ready)
	return;

    usplash_done();
    usplash_restore_console();
}

static void userui_usplash_message(__uint32_t section, __uint32_t level,
		__uint32_t normally_logged, char *msg) {
    if (!usplash_ready)
	return;

    if (section && !((1 << section) & suspend_debug))
	return;

    if (level > console_loglevel)
	return;

    draw_text(msg, strlen(msg));
}

static void userui_usplash_update_progress(__uint32_t value, __uint32_t maximum,
		char *msg) {
    static __uint32_t prev_maximum = -1;
    static __uint32_t old_percent = -1;

    __uint32_t percent;

    if (!usplash_ready)
	return;

    if (maximum == (__uint32_t)(-1))
	value = maximum = 1;

    if (value > maximum)
	value = maximum;

    if (maximum > 0)
	percent = value * 100 / maximum;
    else
	percent = 100;

    if (prev_maximum == -1)
	prev_maximum = maximum;
    else {
	__uint32_t adv = percent - old_percent;
	if (prev_maximum == maximum && percent != 100 &&
	        0 < adv && adv < MIN_ADVANCE_PERCENT)
	    return;
    }

    draw_progressbar(percent);
    old_percent = percent;
}

static void userui_usplash_log_level_change(int loglevel) {
    if (!usplash_ready)
	return;
}

static void userui_usplash_redraw() {
    if (!usplash_ready)
	return;
    clear_screen();
}

static void userui_usplash_keypress(int key) {
    if (common_keypress_handler(key))
	return;
    switch (key) {
    }
}

static int usplash_option_handler(char c)
{
    switch (c) {
	case 'x':
	    sscanf(optarg, "%d", &userui_usplash_xres);
	    break;
	case 'y':
	    sscanf(optarg, "%d", &userui_usplash_yres);
	    break;
	case 'q':
	    userui_usplash_verbose = 0;
	    break;
	default:
	    return 0;
    }
    return 1;
}

static char *usplash_cmdline_options()
{
    return 
"  -x <x-resolution>, --xres <x-resolution>\n"
"     Specifies the X resolution in pixels.\n"
"  -y <y-resolution>, --yres <y-resolution>\n"
"     Specifies the Y resolution in pixels.\n"
"  -q, --quiet\n"
"     Enables usplash's quiet mode (no text is shown).\n";
}

static struct option userui_usplash_longopts[] = {
    {"xres", 1, 0, 'x'},
    {"yres", 1, 0, 'y'},
    {"quiet", 0, 0, 'q'},
    {NULL, 0, 0, 0},
};

static struct userui_ops userui_usplash_ops = {
    .name = "usplash",
    .prepare = userui_usplash_prepare,
    .cleanup = userui_usplash_cleanup,
    .message = userui_usplash_message,
    .update_progress = userui_usplash_update_progress,
    .log_level_change = userui_usplash_log_level_change,
    .redraw = userui_usplash_redraw,
    .keypress = userui_usplash_keypress,

    /* cmdline options */
    .optstring = "x:y:q",
    .longopts  = userui_usplash_longopts,
    .option_handler = usplash_option_handler,
    .cmdline_options = usplash_cmdline_options,
};

struct userui_ops *userui_ops = &userui_usplash_ops;
