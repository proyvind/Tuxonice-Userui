#include <sys/ioctl.h>
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

int fb_fd;
static char lastheader[512];
static int lastloglevel;
static unsigned long cur_value, cur_maximum, last_pos;
static int video_num_lines, video_num_columns;

static inline void disable_utf8() { write(1, "\033%@", 3); }
static inline void clear_display() { write(1, "\2332J", 3); }
static inline void move_cursor_to(int c, int r) { printf("\233%d;%dH", r, c); }
static void hide_cursor() { write(1, "\033[?1c", 5); }
static void show_cursor() { write(1, "\033[?0c", 5); }

static void update_fb_img() {
	move_cursor_to(0,0);
	if (!silent_img.data)
		return;
	lseek(fb_fd, 0, SEEK_SET);
	write(fb_fd, silent_img.data, silent_img.width * silent_img.height * (silent_img.depth >> 3));
}

static void silent_off() {
	clear_display();
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

	hide_cursor();

	fb_fd = -1;
	last_pos = 0;
	lastloglevel = SUSPEND_ERROR; /* start in verbose mode */

	/* Find out the screen size */
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &winsz);
	video_num_lines = winsz.ws_row;
	video_num_columns = winsz.ws_col;

	/* Find out the FB size */
	if (get_fb_settings(0))
		return;

	/* Read theme config file */
	arg_theme = DEFAULT_THEME;
	config_file = get_cfg_file(arg_theme);
	if (config_file) {
		parse_cfg(config_file);
		free(config_file);
	} else
		return;

	arg_vc = get_active_vt();

	fb_fd = open("/dev/fb0", O_WRONLY);
	if (fb_fd == -1) {
		fb_fd = open("/dev/fb/0", O_WRONLY);
		if (fb_fd == -1)
			perror("open(\"/dev/fb0\")");
	}

	do_getpic(FB_SPLASH_IO_ORIG_USER, 0, 's');

	/* Allow for the widest progress bar we might have */
	set_progress_granularity(fb_var.xres);

	disable_utf8();
	clear_display();
}

static void fbsplash_cleanup() {
	cmd_setstate(0, FB_SPLASH_IO_ORIG_USER);
	show_cursor();

	if (fb_fd >= 0)
		close(fb_fd);

	if (silent_img.data)
		free((void*)silent_img.data);

	if (silent_img.cmap.red)
		free(silent_img.cmap.red);
}

static void fbsplash_put_message_silent() {
	printf("\2330;0H" /* move cursor to 0,0 */
			"\2330K"  /* clear to EOL */
			"\2330;%dH" /* move cursor to 0,<pos> */
			"%s",
			(video_num_columns-strlen(lastheader)) / 2,
			lastheader);
}

static void fbsplash_message(unsigned long type, unsigned long level, int normally_logged, char *msg) {
	strncpy(lastheader, msg, 512);
	if (console_loglevel >= SUSPEND_ERROR)
		printf("\n** %s\n", msg);
	else
		fbsplash_put_message_silent();
}

static void fbsplash_redraw() {
	if (console_loglevel < SUSPEND_ERROR) {
		update_fb_img();
		fbsplash_put_message_silent();
	} else {
		printf("\n** %s\n", lastheader);
		return;
	}

	if (!silent_img.data)
		return;

	arg_progress = PROGRESS_MAX;
	render_objs('s', (u8*)silent_img.data, FB_SPLASH_IO_ORIG_USER);
	arg_progress = 0;
	render_objs('s', (u8*)silent_img.data, FB_SPLASH_IO_ORIG_USER);
}

static void fbsplash_update_progress(unsigned long value, unsigned long maximum, char *msg) {
	int bitshift, tmp;

	if (!maximum)
		return;

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

	if (tmp < last_pos)
		fbsplash_redraw();

	last_pos = tmp;
	arg_progress = tmp;

	if (silent_img.data) {
		render_objs('s', (u8*)silent_img.data, FB_SPLASH_IO_ORIG_USER);
		update_fb_img();
	}
}

static void fbsplash_log_level_change() {
	/* Only reset the display if we're switching between nice display
	 * and displaying debugging output */
	
	if (console_loglevel >= SUSPEND_ERROR) {
		if (lastloglevel < SUSPEND_ERROR)
			silent_off();

		printf("\nSwitched to console loglevel %d.\n", console_loglevel);

		if (lastloglevel < SUSPEND_ERROR) {
			printf("\n** %s\n", lastheader);
		}
	
	} else if (lastloglevel >= SUSPEND_ERROR) {
		update_fb_img();
	
		/* Get the nice display or last action [re]drawn */
		fbsplash_update_progress(cur_value, cur_maximum, NULL);
		fbsplash_put_message_silent();
	}
	
	lastloglevel = console_loglevel;
}

static void fbsplash_keypress(int key) {
	switch (key) {
		case 48:
		case 49:
		case 50:
		case 51:
		case 52:
		case 53:
		case 54:
		case 55:
		case 56:
		case 57:
			console_loglevel = key - 48;
			send_message(USERUI_MSG_SET_LOGLEVEL, &console_loglevel, sizeof(console_loglevel));
			break;
	}
}

static unsigned long fbsplash_memory_required() {
	/* Shouldn't need any more. */
	return 0;
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
