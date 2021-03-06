/*
 * userui_skeleton.c - Skeleton userspace user interface module.
 *
 * Copyright (C) 2005, Bernard Blackham <bernard@blackham.com.au>
 * Copyright (C) 2006-2009, Nigel Cunningham <nigel@tuxonice.net>
 * 
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 */

#include "userui.h"

static void skeleton_prepare() {
}

static void skeleton_cleanup() {
}

static void skeleton_message(unsigned long type, unsigned long level, int normally_logged, char *skeleton) {
}

static void skeleton_update_progress(unsigned long value, unsigned long maximum, char *skeleton) {
}

static void skeleton_log_level_change(int loglevel) {
}

static void skeleton_redraw() {
}

static void skeleton_keypress(int key) {
}

static struct userui_ops userui_skeleton_ops = {
	.name = "skeleton",
	.prepare = skeleton_prepare,
	.cleanup = skeleton_cleanup,
	.message = skeleton_message,
	.update_progress = skeleton_update_progress,
	.log_level_change = skeleton_log_level_change,
	.redraw = skeleton_redraw,
	.keypress = skeleton_keypress,
};

struct userui_ops *userui_ops = &userui_skeleton_ops;
