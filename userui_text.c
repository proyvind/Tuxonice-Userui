/*
 * userui_text.c - Text mode userspace user interface module.
 *
 * Copyright (C) 2005, Bernard Blackham <bernard@blackham.com.au>
 *
 * Based on the suspend_text module from Suspend2, written by
 * Nigel Cunningham <nigel@nigel.tuxonice.net>
 * 
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 */

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include "userui.h"

/* We essentially cut and paste the suspend_text plugin */

static int barwidth = 0, barposn = -1, newbarposn = 0;
static int draw_progress_bar = 1;

/* We remember the last header that was (or could have been) displayed for
 * use during log level switches */
static char lastheader[512];
static int lastheader_message_len = 0;

static struct termios termios;
static int lastloglevel = -1;
static int video_num_lines, video_num_columns, cur_x = -1, cur_y = -1;

static int vcsa_fd = -1;

static inline void clear_display() { write(1, "\033[2J", 4); }
static inline void reset_display() { write(1, "\033c", 2); }
static inline void clear_to_eol() { write(1, "\033K", 2); }
static inline void hide_cursor() { write(1, "\033[?25l\033[?1c", 11); }
static inline void show_cursor() { write(1, "\033[?25h\033[?0c", 11); }
static inline void move_cursor_to(int c, int r) { printf("\033[%d;%dH", r, c); }

static void flush_scrollback()
{
	int i;
	for (i = 0; i <= video_num_lines; i++)
		write(1, "\n", 1);
}

static int update_cursor_pos(void)
{
	struct {
		unsigned char lines, cols, x, y;
	} screen;

	if (vcsa_fd < 0)
		return 0;
	if (lseek(vcsa_fd, 0, SEEK_SET) == -1)
		return 0;
	if (read(vcsa_fd, &screen, sizeof(screen)) == -1)
		return 0;
	cur_x = screen.x+1;
	cur_y = screen.y+1;
	return 1;
}

/*
 * Help text functions.
 */
static void update_help(int update_all)
{
	char buf[200];

	if (resuming)
		snprintf(buf, 200, "%-22s",
			(can_use_escape) ? "Esc: Abort resume" : "");
	else
		snprintf(buf, 200, "%-22s    R: %s reboot after hibernating ",
			(can_use_escape) ? "Esc: Abort hibernating" : "",
			(suspend_action & (1 << SUSPEND_REBOOT)) ?  "Disable":"Enable");
	move_cursor_to(video_num_columns - strlen(buf), video_num_lines);
	printf("%s", buf);
}

/* text_prepare_status
 * Description:	Prepare the 'nice display', drawing the header and version,
 * 		along with the current action and perhaps also resetting the
 * 		progress bar.
 * Arguments:	int printalways: Whether to print the action when debugging
 * 		is on
 * 		int clearbar: Whether to reset the progress bar.
 * 		const char *fmt, ...: The action to be displayed.
 */

static void text_prepare_status_real(int printalways, int clearbar, int level, const char *msg)
{
	int y;

	if (msg) {
		strncpy(lastheader, msg, 512);
		lastheader_message_len = strlen(lastheader);
	}

	if (console_loglevel >= SUSPEND_ERROR) {
		if (!(suspend_action & (1 << SUSPEND_LOGALL)) || level == SUSPEND_UI_MSG)
			printf("\n** %s\n", lastheader);
		return;
	}

	/* Remember where the cursor was */
	if (cur_x != -1)
		update_cursor_pos();

	/* Print version */
	move_cursor_to(0, video_num_lines);
	printf("%s", software_suspend_version);

	/* Update help text */
	update_help(0);
	
	/* Print header */
	move_cursor_to((video_num_columns - 19) / 2, (video_num_lines / 3) - 3);
	printf("T U X   O N   I C E");

	/* Print action */
	y = video_num_lines / 3;
	move_cursor_to(0, y);

	/* Clear old message */
	for (barposn = 0; barposn < video_num_columns; barposn++) 
		printf(" ");

	move_cursor_to((video_num_columns - lastheader_message_len) / 2, y);
	printf("%s", lastheader);
	
	if (draw_progress_bar) {
		/* Draw left bracket of progress bar. */
		y++;
		move_cursor_to(video_num_columns / 4, y);
		printf("[");

		/* Draw right bracket of progress bar. */
		move_cursor_to(video_num_columns - (video_num_columns / 4) - 1, y);
		printf("]");

		if (clearbar) {
			/* Position at start of progress */
			move_cursor_to(video_num_columns / 4 + 1, y);

			/* Clear bar */
			for (barposn = 0; barposn < barwidth; barposn++)
				printf(" ");

			move_cursor_to(video_num_columns / 4 + 1, y);
		}
	}

	if (cur_x == -1) {
		cur_x = 1;
		cur_y = y+2;
	}
	move_cursor_to(cur_x, cur_y);
	
	hide_cursor();

	barposn = 0;
}

static void text_prepare_status(int printalways, int clearbar, int level, const char *fmt, ...)
{
	va_list va;
	char buf[1024];
	if (fmt) {
		va_start(va, fmt);
		vsnprintf(buf, 1024, fmt, va);
		text_prepare_status_real(printalways, clearbar, level, buf);
		va_end(va);
	} else
		text_prepare_status_real(printalways, clearbar, level, NULL);
}

/* text_loglevel_change
 *
 * Description:	Update the display when the user changes the log level.
 * Returns:	Boolean indicating whether the level was changed.
 */

static void text_loglevel_change()
{
	barposn = 0;

	/* Only reset the display if we're switching between nice display
	 * and displaying debugging output */
	
	if (console_loglevel >= SUSPEND_ERROR) {
		if (lastloglevel < SUSPEND_ERROR)
			clear_display();

		show_cursor();

		if (lastloglevel > -1)
			printf("\nSwitched to console loglevel %d.\n", console_loglevel);

		if (lastloglevel > -1 && lastloglevel < SUSPEND_ERROR) {
			printf("\n** %s\n", lastheader);
		}
	
	} else if (lastloglevel >= SUSPEND_ERROR || lastloglevel == -1) {
		clear_display();
		hide_cursor();
	
		/* Get the nice display or last action [re]drawn */
		text_prepare_status(1, 0, SUSPEND_UI_MSG, NULL);
	}
	
	lastloglevel = console_loglevel;
}

/* text_update_progress
 *
 * Description: Update the progress bar and (if on) in-bar message.
 * Arguments:	U32 value, maximum: Current progress percentage (value/max).
 * 		const char *fmt, ...: Message to be displayed in the middle
 * 		of the progress bar.
 * 		Note that a NULL message does not mean that any previous
 * 		message is erased! For that, you need prepare_status with
 * 		clearbar on.
 * Returns:	Unsigned long: The next value where status needs to be updated.
 * 		This is to reduce unnecessary calls to text_update_progress.
 */
void text_update_progress(__uint32_t value, __uint32_t maximum, char *msg)
{
	__uint32_t next_update = 0;
	int bitshift = generic_fls(maximum) - 16;
	int message_len = 0;

	if (!maximum)
		return /* maximum */;

	if (value < 0)
		value = 0;

	if (value > maximum)
		value = maximum;

	/* Try to avoid math problems - we can't do 64 bit math here
	 * (and shouldn't need it - anyone got screen resolution
	 * of 65536 pixels or more?) */
	if (bitshift > 0) {
		__uint32_t temp_maximum = maximum >> bitshift;
		__uint32_t temp_value = value >> bitshift;
		newbarposn = (__uint32_t) (temp_value * barwidth / temp_maximum);
	} else
		newbarposn = (__uint32_t) (value * barwidth / maximum);
	
	if (newbarposn < barposn)
		barposn = 0;

	next_update = ((newbarposn + 1) * maximum / barwidth) + 1;

	if ((console_loglevel >= SUSPEND_ERROR) || (!draw_progress_bar))
		return /* next_update */;

	/* Remember where the cursor was */
	if (cur_x != -1)
		update_cursor_pos();

	/* Update bar */
	if (draw_progress_bar) {
		/* Clear bar if at start */
		if (!barposn) {
			move_cursor_to(video_num_columns / 4 + 1, (video_num_lines / 3) + 1);
			for (; barposn < barwidth; barposn++)
				printf(" ");
			barposn = 0;
		}
		move_cursor_to(video_num_columns / 4 + 1 + barposn, (video_num_lines / 3) + 1);

		for (; barposn < newbarposn; barposn++)
			printf("-");
	}

	/* Print string in progress bar on loglevel 1 */
	if (msg && strlen(msg) && (console_loglevel)) {
		message_len = strlen(msg);
		move_cursor_to((video_num_columns - message_len) / 2,
				(video_num_lines / 3) + 1);
		printf(" %s ", msg);
	}

	if (cur_x != -1)
		move_cursor_to(cur_x, cur_y);
	
	barposn = newbarposn;
	hide_cursor();
	
	/* return next_update; */
}

static void text_message(__uint32_t section, __uint32_t level,
		__uint32_t normally_logged, char *msg)
{
	if (section && !((1 << section) & suspend_debug))
		return;

	if (level > console_loglevel)
		return;

	text_prepare_status_real(1, 0, level, msg);
}

static void text_prepare() {
	struct winsize winsz;
	struct termios new_termios;
	/* chvt here? */

	setvbuf(stdout, NULL, _IONBF, 0);

	/* Turn off canonical mode */
	ioctl(STDOUT_FILENO, TCGETS, (long)&termios);
	new_termios = termios;
	new_termios.c_lflag &= ~ICANON;
	ioctl(STDOUT_FILENO, TCSETSF, (long)&new_termios);

	/* Find out the screen size */
	video_num_lines = 24;
	video_num_columns = 80;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &winsz) != -1 &&
			winsz.ws_row > 0 && winsz.ws_col > 0) {
		video_num_lines = winsz.ws_row;
		video_num_columns = winsz.ws_col;
	}

	/* Calculate progress bar width. Note that whether the
	 * splash screen is on might have changed (this might be
	 * the first call in a new cycle), so we can't take it
	 * for granted that the width is the same as last time
	 * we came in here */
	barwidth = (video_num_columns - 2 * (video_num_columns / 4) - 2);

	/* Open /dev/vcsa0 so we can find out the cursor position when we need to */
	vcsa_fd = open("/dev/vcsa0", O_RDONLY);
	/* if it errors, don't worry. we'll check later */

	clear_display();

	text_loglevel_change();
}

static void text_cleanup()
{
	if (vcsa_fd >= 0)
		close(vcsa_fd);

	ioctl(STDOUT_FILENO, TCSETSF, (long)&termios);

	show_cursor();
	clear_display();
	move_cursor_to(0, 0);
	/* chvt back? */
}

static void text_redraw()
{
	clear_display();
	reset_display();
	flush_scrollback();
	cur_x = -1;
}

static void text_keypress(int key)
{
	if (common_keypress_handler(key))
		return;
	switch (key) {
	}
}

static struct userui_ops userui_text_ops = {
	.name = "text",
	.prepare = text_prepare,
	.cleanup = text_cleanup,
	.message = text_message,
	.update_progress = text_update_progress,
	.log_level_change = text_loglevel_change,
	.redraw = text_redraw,
	.keypress = text_keypress,
};

struct userui_ops *userui_ops = &userui_text_ops;
