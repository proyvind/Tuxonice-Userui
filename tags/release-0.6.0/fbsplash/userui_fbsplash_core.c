/*
 * userui_fbsplash_core.c - fbsplash-based userspace user interface module.
 *
 * Copyright (C) 2005 Bernard Blackham <bernard@blackham.com.au>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 */

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>

#include "splash.h"
#include "../userui.h"

int fb_fd, fbsplash_fd = -1;
char *progress_text;
static char lastmessage[512];
static char rendermessage[512];
static int lastloglevel;
static unsigned long cur_value, cur_maximum, last_pos;
static int video_num_lines, video_num_columns;
static void *base_image;
static char *frame_buffer;
static int base_image_size;

static inline void clear_display() { write(1, "\033c", 2); }
static inline void move_cursor_to(int c, int r) { printf("\033[%d;%dH", r, c); }
static void hide_cursor() {
	ioctl(STDOUT_FILENO, KDSETMODE, KD_GRAPHICS);
	write(1, "\033[?25l\033[?1c", 11);
}
static void show_cursor() {
	ioctl(STDOUT_FILENO, KDSETMODE, KD_TEXT);
	write(1, "\033[?25h\033[?0c", 11);
}

static void reset_silent_img() {
	if (!base_image || !silent_img.data)
		return;
	memcpy((void*)silent_img.data, base_image, base_image_size);
	strncpy(rendermessage, lastmessage, 512);
	render_objs((u8*)silent_img.data, NULL, 's', FB_SPLASH_IO_ORIG_USER, 0);
	rendermessage[0] = '\0';
}

static void silent_off() {
	/* Do we really need this? 
	if (frame_buffer)
		msync(frame_buffer, base_image_size, MS_SYNC);
	fdatasync(fb_fd); */
	show_cursor();
}

static int get_active_vt() {
	int vt, fd;
	struct vt_stat vt_stat;

	vt = 62; /* default */

	if ((fd = open("/dev/tty0", O_RDONLY)) == -1)
		goto out;

	if (ioctl(fd, VT_GETSTATE, &vt_stat) == -1)
		goto out;

	vt = vt_stat.v_active - 1;

out:
	if (fd >= 0)
		close(fd);

	return vt;
}

static void fbsplash_prepare() {
	struct winsize winsz;

	fb_fd = -1;
	last_pos = 0;
	lastloglevel = SUSPEND_ERROR; /* start in verbose mode */

	/* Find out the screen size */
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &winsz);
	video_num_lines = winsz.ws_row;
	video_num_columns = winsz.ws_col;

	/* Kick start our TTF library */
	if (TTF_Init() < 0) {
		fprintf(stderr, "Couldn't initialise TTF.\n");
	}

	/* Find out the FB size */
	if (get_fb_settings(0))
		return;

	arg_vc = get_active_vt();
	arg_mode = 's';

	/* Read theme config file */
	arg_theme = DEFAULT_THEME;
	config_file = get_cfg_file(arg_theme);
	if (!config_file)
		return;

	parse_cfg(config_file);

	/* Prime the font cache with glyphs so we don't need to allocate them later */
	TTF_PrimeCache("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -.", global_font, TTF_STYLE_NORMAL);

	boot_message = rendermessage;

	fb_fd = open_fb();
	if (fb_fd == -1)
		return;

	fbsplash_fd = open(SPLASH_DEV, O_WRONLY); /* Don't worry if it fails */

	do_getpic(FB_SPLASH_IO_ORIG_USER, 1, 'v'); /* Don't worry if it fails */
	if (do_getpic(FB_SPLASH_IO_ORIG_USER, 0, 's') == -1)
		return; /* We do care if this fails. */

	/* These next two touch the kernel and are needed even for silent mode, to
	 * get the colours right (even on 32-bit depth displays funnily enough. */
	do_config(FB_SPLASH_IO_ORIG_USER);
	cmd_setstate(1, FB_SPLASH_IO_ORIG_USER);

	/* copy the silent pic to base_image for safe keeping */
	base_image_size = silent_img.width * silent_img.height * (silent_img.depth >> 3);
	base_image = malloc(base_image_size);
	if (!base_image) {
		fprintf(stderr, "Couldn't get enough memory for framebuffer image.\n");
		return;
	}
	memcpy(base_image, (void*)silent_img.data, base_image_size);

	frame_buffer = mmap(NULL, base_image_size, PROT_READ | PROT_WRITE,
			MAP_SHARED, fb_fd, 0);
	if (frame_buffer == MAP_FAILED) {
		frame_buffer = NULL;
	}

	move_cursor_to(0,0);
	clear_display();

	lastloglevel = console_loglevel;
}

static void fbsplash_cleanup() {
	cmd_setstate(0, FB_SPLASH_IO_ORIG_USER);
	show_cursor();

	free((void*)silent_img.data);
	silent_img.data = NULL;

	free(silent_img.cmap.red);
	silent_img.cmap.red = NULL;

	free(base_image);
	base_image = NULL;

	free(config_file);
	config_file = NULL;

	if (frame_buffer) {
		munmap(frame_buffer, base_image_size);
		frame_buffer = NULL;
	}

	if (fb_fd >= 0) {
		close(fb_fd);
		fb_fd = -1;
	}

	if (fbsplash_fd >= 0) {
		close(fbsplash_fd);
		fbsplash_fd = -1;
	}

	free_fonts();

	TTF_Quit();
}

static void update_fb_img() {
	if (!silent_img.data)
		return;

	/* Try mmap'd I/O first */
	if (frame_buffer) {
		memcpy(frame_buffer, silent_img.data, base_image_size);
	} else if (fb_fd != -1) {
		lseek(fb_fd, 0, SEEK_SET);
		write(fb_fd, silent_img.data, base_image_size);
	}
}

static void fbsplash_update_silent_message() {
	if (!silent_img.data || !base_image)
		return;

	reset_silent_img();
	update_fb_img();
}

static void fbsplash_message(unsigned long type, unsigned long level, int normally_logged, char *msg) {
	strncpy(lastmessage, msg, 512);
	if (console_loglevel >= SUSPEND_ERROR)
		printf("\n** %s\n", msg);
	else
		fbsplash_update_silent_message();
}

static void fbsplash_redraw() {
	if (console_loglevel >= SUSPEND_ERROR) {
		printf("\n** %s\n", lastmessage);
		return;
	}

	reset_silent_img();
	update_fb_img();
}

static void fbsplash_update_progress(unsigned long value, unsigned long maximum, char *msg) {
	int bitshift, tmp;

	if (console_loglevel >= SUSPEND_ERROR)
		return;

	if (!silent_img.data || !base_image)
		return;

	if (!maximum) /* just updating the screen */
		goto render;

	if (value < 0)
		value = 0;

	if (value > maximum)
		value = maximum;

	bitshift = generic_fls(maximum) - 16;

	/* Try to avoid math problems */
	if (bitshift > 0) {
		unsigned long temp_maximum = maximum >> bitshift;
		unsigned long temp_value = value >> bitshift;
		tmp = (int) (temp_value * PROGRESS_MAX / temp_maximum);
	} else
		tmp = (int) (value * PROGRESS_MAX / maximum);

	cur_value = value;
	cur_maximum = maximum;

	if (tmp < last_pos) { /* we need to blank out the progress bar */
		arg_progress = 0;
		reset_silent_img();
	}

	last_pos = tmp;
	arg_progress = tmp;

	if (msg && console_loglevel == 1)
		progress_text = msg;

render:
	render_objs((u8*)silent_img.data, NULL, 's', FB_SPLASH_IO_ORIG_USER, 1);
	update_fb_img();

	progress_text = NULL;
}

static void fbsplash_log_level_change() {
	/* Only reset the display if we're switching between nice display
	 * and displaying debugging output */

	if (console_loglevel >= SUSPEND_ERROR) {
		if (lastloglevel < SUSPEND_ERROR)
			silent_off();

		printf("\nSwitched to console loglevel %d.\n", console_loglevel);

		if (lastloglevel < SUSPEND_ERROR)
			printf("\n** %s\n", lastmessage);
	
	} else if (lastloglevel >= SUSPEND_ERROR) {
		/* Get the nice display or last action [re]drawn */
		fbsplash_redraw();
		hide_cursor();
	}
	
	lastloglevel = console_loglevel;
}

static void fbsplash_keypress(int key) {
	if (common_keypress_handler(key))
		return;
	switch (key) {
		case 0x3c: /* F12 */
			if (console_loglevel < SUSPEND_ERROR) {
				console_loglevel = 5;
				set_console_loglevel(0);
				fbsplash_log_level_change();
			}
			break;
	}
}

static unsigned long fbsplash_memory_required() {
	/* FIXME */
	return 4*1024*1024;
}

static struct userui_ops userui_fbsplash_ops = {
	.name = "fbsplash",
	.prepare = fbsplash_prepare,
	.cleanup = fbsplash_cleanup,
	.message = fbsplash_message,
	.update_progress = fbsplash_update_progress,
	.log_level_change = fbsplash_log_level_change,
	.redraw = fbsplash_redraw,
	.keypress = fbsplash_keypress,
	.memory_required = fbsplash_memory_required,
};

struct userui_ops *userui_ops = &userui_fbsplash_ops;
