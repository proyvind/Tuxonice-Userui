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
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>

#include "splash.h"
#include "../userui.h"

int fb_fd, fbsplash_fd = -1, no_silent_image = 0;
char *progress_text;
static char rendermessage[512];
static int lastloglevel;
static unsigned long cur_value, cur_maximum, last_pos;
static void *base_image;
static char *frame_buffer;
static int base_image_size;
static struct termios termios;

static void fbsplash_log_level_change();

static inline void clear_display() { int result; result = write(1, "\033c", 2); }
static inline void move_cursor_to(int c, int r) { printf("\033[%d;%dH", r, c); }
static void hide_cursor() {
	int result;
	ioctl(STDOUT_FILENO, KDSETMODE, KD_GRAPHICS);
	result = write(1, "\033[?25l\033[?1c", 11);
}
static void show_cursor() {
	int result;
	ioctl(STDOUT_FILENO, KDSETMODE, KD_TEXT);
	result = write(1, "\033[?25h\033[?0c", 11);
}

static void reset_silent_img() {
	if (!base_image || !silent_img.data)
		return;
	memcpy((void*)silent_img.data, base_image, base_image_size);
	strncpy(rendermessage, lastheader, 512);
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

static int fbsplash_load() {
	fb_fd = -1;
	last_pos = 0;

	/* Kick start our TTF library */
	if (TTF_Init() < 0) {
		printk("Couldn't initialise TTF.\n");
	}

	/* Find out the FB size */
	if (get_fb_settings(0)) {
		printk("Couldn't get fb settings.\n");
		return 1;
	}

	arg_vc = get_active_vt();
	arg_mode = 's';

	/* Read theme config file */
	if (arg_theme == NULL)
		arg_theme = DEFAULT_THEME;
	config_file = get_cfg_file(arg_theme);
	if (!config_file) {
		printk("Couldn't load config file %s.\n", arg_theme);
		return 1;
	} else
		printk("Using configuration file %s.\n", config_file);

	parse_cfg(config_file);

	/* Prime the font cache with glyphs so we don't need to allocate them later */
	TTF_PrimeCache("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -.", global_font, TTF_STYLE_NORMAL);

	boot_message = rendermessage;

	fb_fd = open_fb();
	if (fb_fd == -1) {
		printk("Couldn't open framebuffer device.\n");
		return 1;
	}

	if (fb_fix.visual == FB_VISUAL_DIRECTCOLOR)
		set_directcolor_cmap(fb_fd);

	fbsplash_fd = open(SPLASH_DEV, O_WRONLY); /* Don't worry if it fails */

	do_getpic(FB_SPLASH_IO_ORIG_USER, 1, 'v'); /* Don't worry if it fails */
	if (do_getpic(FB_SPLASH_IO_ORIG_USER, 0, 's') == -1)
		no_silent_image = 1; /* We do care if this fails. */

	/* These next two touch the kernel and are needed even for silent mode, to
	 * get the colours right (even on 32-bit depth displays funnily enough. */
	do_config(FB_SPLASH_IO_ORIG_USER);
	cmd_setstate(1, FB_SPLASH_IO_ORIG_USER);

	/* copy the silent pic to base_image for safe keeping */
	if (!no_silent_image) {
		base_image_size = silent_img.width * silent_img.height * (silent_img.depth >> 3);
		base_image = malloc(base_image_size);
		if (!base_image) {
			printk("Couldn't get enough memory for framebuffer image.\n");
			return 1;
		}
		memcpy(base_image, (void*)silent_img.data, base_image_size);
	}

	frame_buffer = mmap(NULL, fb_fix.line_length * fb_var.yres,
			PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
	if (frame_buffer == MAP_FAILED)
		frame_buffer = NULL;

	printk("Framebuffer support initialised successfully.\n");
	return 0;
}

static void fbsplash_unprepare() {
	clear_display();
	show_cursor();
}

static void fbsplash_cleanup()
{
	clear_display();
	cmd_setstate(0, FB_SPLASH_IO_ORIG_USER);

	ioctl(STDOUT_FILENO, TCSETSF, (long)&termios);
	show_cursor();

	free((void*)silent_img.data);
	silent_img.data = NULL;

	free(silent_img.cmap.red);
	silent_img.cmap.red = NULL;

	if (!no_silent_image) {
		free(base_image);
		base_image = NULL;
	}

	free(config_file);
	config_file = NULL;

	if (frame_buffer) {
		munmap(frame_buffer, fb_fix.line_length * fb_var.yres);
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
	int y;
	int img_line_length = fb_var.xres * ((fb_var.bits_per_pixel + 7) >> 3);

	if (!silent_img.data)
		return;

	if (frame_buffer) {
		/* Try mmap'd I/O if we have it */
		for (y = 0; y < fb_var.yres; y++) {
			memcpy(frame_buffer + y * fb_fix.line_length,
					silent_img.data + (y * img_line_length),
					img_line_length);
		}
	} else if (fb_fd != -1) {
		for (y = 0; y < fb_var.yres; y++) {
			int result;
			lseek(fb_fd, y * fb_fix.line_length, SEEK_SET);
			result = write(fb_fd, silent_img.data + (y * img_line_length), img_line_length);
		}
	}
}

static void fbsplash_update_silent_message() {
	if (!silent_img.data || !base_image)
		return;

	reset_silent_img();
	update_fb_img();
}

static void fbsplash_message(u32 type, u32 level, u32 normally_logged, char *msg) {
	strncpy(lastheader, msg, 512);
	if (console_loglevel >= SUSPEND_ERROR) {
		if (!(suspend_action & (1 << SUSPEND_LOGALL)) || level == SUSPEND_UI_MSG)
			printf("\n** %s\n", msg);
	} else
		fbsplash_update_silent_message();
}

static void fbsplash_redraw() {
	if (console_loglevel >= SUSPEND_ERROR) {
		printf("\n** %s\n", lastheader);
		return;
	}

	reset_silent_img();
	update_fb_img();
}

static void fbsplash_update_progress(u32 value, u32 maximum, char *msg) {
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
		u32 temp_maximum = maximum >> bitshift;
		u32 temp_value = value >> bitshift;
		tmp = (u32) (temp_value * PROGRESS_MAX / temp_maximum);
	} else
		tmp = (u32) (value * PROGRESS_MAX / maximum);

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
	render_objs((u8*)silent_img.data, NULL, 's', FB_SPLASH_IO_ORIG_USER, 0);
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
			printf("\n** %s\n", lastheader);
	
	} else if (lastloglevel >= SUSPEND_ERROR) {
		hide_cursor();

		/* Get the nice display or last action [re]drawn */
		fbsplash_redraw();
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

static int fbsplash_option_handler(char c)
{
	switch (c) {
		case 'T':
			arg_theme = strdup(optarg);
			return 1;
		default:
			return 0;
	}
}

static char *fbsplash_cmdline_options()
{
	return 
"\n"
"  FBSPLASH:\n"
"  -T <theme name>, --theme <theme name>\n"
"     Selects a given theme from " THEME_DIR " (default: "DEFAULT_THEME") for fbsplash support.\n";
}

static struct option userui_fbsplash_longopts[] = {
	{"theme", 1, 0, 'T'},
	{NULL, 0, 0, 0},
};

static void fbsplash_prepare()
{
	move_cursor_to(0,0);
	clear_display();
	hide_cursor();

	fbsplash_redraw();
}

struct userui_ops userui_fbsplash_ops = {
	.name = "fbsplash",
	.load = fbsplash_load,
	.prepare = fbsplash_prepare,
	.unprepare = fbsplash_unprepare,
	.cleanup = fbsplash_cleanup,
	.message = fbsplash_message,
	.update_progress = fbsplash_update_progress,
	.log_level_change = fbsplash_log_level_change,
	.redraw = fbsplash_redraw,
	.keypress = fbsplash_keypress,
	.memory_required = fbsplash_memory_required,

	/* cmdline options */
	.optstring = "T:",
	.longopts  = userui_fbsplash_longopts,
	.option_handler = fbsplash_option_handler,
	.cmdline_options = fbsplash_cmdline_options,
};
