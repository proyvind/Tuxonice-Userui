#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "userui.h"

/* excerpts from include/linux/bitops.h */
/*
 * fls: find last bit set.
 */

static __inline__ int generic_fls(int x)
{
	int r = 32;

	if (!x)
		return 0;
	if (!(x & 0xffff0000u)) {
		x <<= 16;
		r -= 16;
	}
	if (!(x & 0xff000000u)) {
		x <<= 8;
		r -= 8;
	}
	if (!(x & 0xf0000000u)) {
		x <<= 4;
		r -= 4;
	}
	if (!(x & 0xc0000000u)) {
		x <<= 2;
		r -= 2;
	}
	if (!(x & 0x80000000u)) {
		x <<= 1;
		r -= 1;
	}
	return r;
}

/* Now we essentially cut and paste the suspend_text plugin */

static int barwidth = 0, barposn = -1, newbarposn = 0;
static int draw_progress_bar = 1;

/* We remember the last header that was (or could have been) displayed for
 * use during log level switches */
static char lastheader[512];
static int lastheader_message_len = 0;

static struct termios termios;
static int lastloglevel = -1;
static int video_num_lines, video_num_columns;

static inline void clear_display() { write(1, "\2332J", 3); }
static inline void clear_to_eol() { write(1, "\233K", 2); }
static inline void hide_cursor() { write(1, "\233?1c", 4); }
static inline void restore_cursor() { write(1, "\233?0c", 4); }
static inline void move_cursor_to(int c, int r) { printf("\233%d;%dH", r, c); }
static inline void unblank_screen_via_file() { write(1, "\23313]", 4); }

/* text_prepare
 * Description:	Prepare the 'nice display', drawing the header and version,
 * 		along with the current action and perhaps also resetting the
 * 		progress bar.
 * Arguments:	int printalways: Whether to print the action when debugging
 * 		is on
 * 		int clearbar: Whether to reset the progress bar.
 * 		const char *fmt, ...: The action to be displayed.
 */

static void text_prepare_status_real(int printalways, int clearbar, const char *msg)
{
	int y;

	if (msg) {
		strncpy(lastheader, msg, 512);
		lastheader_message_len = strlen(lastheader);
	}

	if (console_loglevel >= SUSPEND_ERROR) {
		if (printalways)
			printf("\n** %s\n", lastheader);
		return;
	}

	barwidth = (video_num_columns - 2 * (video_num_columns / 4) - 2);

	/* Print version */
	move_cursor_to(0, video_num_lines);
	printf("%s", software_suspend_version);

	/* Print header */
	move_cursor_to((video_num_columns - 31) / 2, (video_num_lines / 3) - 3);
	printf("S O F T W A R E   S U S P E N D");

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
	
	hide_cursor();

	barposn = 0;
}

static void text_prepare_status(int printalways, int clearbar, const char *fmt, ...)
{
	char buf[1024];
	va_list va;
	va_start(va, fmt);
	vsnprintf(buf, 1024, fmt, va);
	text_prepare_status_real(printalways, clearbar, buf);
	va_end(va);
}

/* text_loglevel_change
 *
 * Description:	Update the display when the user changes the log level.
 * Returns:	Boolean indicating whether the level was changed.
 */

static void text_loglevel_change(int loglevel)
{
	/* Calculate progress bar width. Note that whether the
	 * splash screen is on might have changed (this might be
	 * the first call in a new cycle), so we can't take it
	 * for granted that the width is the same as last time
	 * we came in here */
	barwidth = (video_num_columns - 2 * (video_num_columns / 4) - 2);
	barposn = 0;

	/* Only reset the display if we're switching between nice display
	 * and displaying debugging output */
	
	if (console_loglevel >= SUSPEND_ERROR) {
		if (lastloglevel < SUSPEND_ERROR)
			clear_display();

		printf("\nSwitched to console loglevel %d.\n", console_loglevel);

		if (lastloglevel < SUSPEND_ERROR) {
			printf("\n** %s\n", lastheader);
		}
	
	} else if (lastloglevel >= SUSPEND_ERROR) {
		clear_display();
	
		/* Get the nice display or last action [re]drawn */
		text_prepare_status(1, 0, NULL);
	}
	
	lastloglevel = console_loglevel;
}

/* text_update_progress
 *
 * Description: Update the progress bar and (if on) in-bar message.
 * Arguments:	UL value, maximum: Current progress percentage (value/max).
 * 		const char *fmt, ...: Message to be displayed in the middle
 * 		of the progress bar.
 * 		Note that a NULL message does not mean that any previous
 * 		message is erased! For that, you need prepare_status with
 * 		clearbar on.
 * Returns:	Unsigned long: The next value where status needs to be updated.
 * 		This is to reduce unnecessary calls to text_update_progress.
 */
void text_update_progress(unsigned long value, unsigned long maximum,
		char *msg)
{
	unsigned long next_update = 0;
	int bitshift = generic_fls(maximum) - 16;
	int message_len = 0;

	if (!barwidth)
		barwidth = (video_num_columns - 2 * (video_num_columns / 4) - 2);

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
		unsigned long temp_maximum = maximum >> bitshift;
		unsigned long temp_value = value >> bitshift;
		newbarposn = (int) (temp_value * barwidth / temp_maximum);
	} else
		newbarposn = (int) (value * barwidth / maximum);
	
	if (newbarposn < barposn)
		barposn = 0;

	next_update = ((newbarposn + 1) * maximum / barwidth) + 1;

	if ((console_loglevel >= SUSPEND_ERROR) || (!draw_progress_bar))
		return /* next_update */;

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
	if ((msg) && (console_loglevel)) {
		message_len = strlen(msg);
		move_cursor_to((video_num_columns - message_len) / 2,
				(video_num_lines / 3) + 1);
		printf(" %s ", msg);
	}

	barposn = newbarposn;
	hide_cursor();
	
	/* return next_update; */
}

static void text_message(unsigned long section, unsigned long level,
		int normally_logged,
		char *msg)
{
	/* FIXME - should we get at these somehow? */
	/* if ((section) && (!TEST_DEBUG_STATE(section)))
		return; */

	if (level == SUSPEND_STATUS) {
		text_prepare_status_real(1, 0, msg);
		return;
	}
	
	if (level > console_loglevel)
		return;

	printf("%s\n", msg);
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
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &winsz);
	video_num_lines = winsz.ws_row;
	video_num_columns = winsz.ws_col;

	clear_display();
}

static void text_cleanup() {
	ioctl(STDOUT_FILENO, TCSETSF, (long)&termios);

	restore_cursor();
	clear_display();
	move_cursor_to(0, 0);
	/* chvt back? */
}

static void text_redraw() {
}

static void text_keypress(int key) {
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
		case 19:
			/* Toggle reboot */
			suspend_action ^= (1 << SUSPEND_REBOOT);
			text_prepare_status(1, 0, "Rebooting %sabled.",
					(suspend_action&(1<<SUSPEND_REBOOT))?"en":"dis");
			send_message(USERUI_MSG_SET_STATE, &suspend_action, sizeof(suspend_action));
			send_message(USERUI_MSG_GET_STATE, NULL, 0);
			break;
//		case 112:
//			/* During suspend, toggle pausing with P */
//			suspend_action ^= (1 << SUSPEND_PAUSE);
//			suspend2_core_ops->schedule_message(1);
//			break;
//		case 115:
//			/* Otherwise, if S pressed, toggle single step */
//			suspend_action ^= (1 << SUSPEND_SINGLESTEP);
//			suspend2_core_ops->schedule_message(3);
//			break;
		default:
			text_prepare_status(1, 0, "Got key %d", key);
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
