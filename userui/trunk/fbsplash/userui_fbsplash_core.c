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

int fb_fd, fbsplash_fd;
static char lastheader[512];
static int lastloglevel;

static inline void clear_display() { write(1, "\2332J", 3); }

static void hide_cursor() {
	//ioctl(STDOUT_FILENO, KDSETMODE, KD_GRAPHICS);
	write(1, "\033[?1c", 5);
}

static void show_cursor() {
	//ioctl(STDOUT_FILENO, KDSETMODE, KD_TEXT);
	write(1, "\033[?0c", 5);
}

static void silent_on() {
	printf("Putting it on\n");
	lseek(fb_fd, 0, SEEK_SET);
	write(fb_fd, pic.data, pic.width * pic.height * (pic.depth >> 3));
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
	hide_cursor();

	fb_fd = -1;
	fbsplash_fd = -1;
	lastloglevel = SUSPEND_ERROR; /* start in verbose mode */

	if (get_fb_settings(0))
		return;

	/* Read theme config file */
	arg_theme = "suspend2";
	config_file = get_cfg_file(arg_theme);
	if (config_file)
		parse_cfg(config_file);
	else
		return;

	arg_vc = get_active_vt();

	fb_fd = open("/dev/fb0", O_WRONLY);
	if (fb_fd == -1)
		perror("open(\"/dev/fb0\")");

	fbsplash_fd = open(SPLASH_DEV, O_WRONLY);
	if (fbsplash_fd == -1)
		perror("open(\""SPLASH_DEV"\"");

	do_config(FB_SPLASH_IO_ORIG_USER);
	do_getpic(FB_SPLASH_IO_ORIG_USER, 1, 'v');
	cmd_setstate(1, FB_SPLASH_IO_ORIG_USER);

	do_getpic(FB_SPLASH_IO_ORIG_USER, 0, 's');
}

static void fbsplash_cleanup() {
	cmd_setstate(0, FB_SPLASH_IO_ORIG_USER);
	show_cursor();

	if (fbsplash_fd >= 0)
		close(fbsplash_fd);
	if (fb_fd >= 0)
		close(fb_fd);
}

static void fbsplash_message(unsigned long type, unsigned long level, int normally_logged, char *msg) {
	printf("** %s\n", msg);
	strncpy(lastheader, msg, 512);
}

static void fbsplash_update_progress(unsigned long value, unsigned long maximum, char *fbsplash) {
	arg_progress = value * PROGRESS_MAX / maximum;
	arg_task = paint;

	if (console_loglevel < SUSPEND_ERROR)
		draw_boxes((u8*)pic.data, 's', FB_SPLASH_IO_ORIG_USER);
}

static void fbsplash_log_level_change(int loglevel) {
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
		silent_on();
	
		/* Get the nice display or last action [re]drawn */
		/* redraw_progress(); FIXME */
	}
	
	lastloglevel = console_loglevel;
}

static void fbsplash_redraw() {
}

static void fbsplash_keypress(int key) {
	switch (key) {
		case 1:
			send_message(USERUI_MSG_ABORT, NULL, 0);
			break;
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
		case 10:
			console_loglevel = key - 1;
			send_message(USERUI_MSG_SET_LOGLEVEL, &console_loglevel, sizeof(console_loglevel));
			break;
		case 11:
			console_loglevel = 0;
			send_message(USERUI_MSG_SET_LOGLEVEL, &console_loglevel, sizeof(console_loglevel));
			break;
	}
}

static unsigned long fbsplash_memory_required() {
	/* Reserve enough for another frame buffer */
	return (fb_var.xres * fb_var.yres * (fb_var.bits_per_pixel >> 3));
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
