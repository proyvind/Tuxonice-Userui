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
static inline void clear_to_eol() { write(1, "\033K", 2); }
static void hide_cursor() { write(1, "\033[?25l\033[?1c", 11); }
static void show_cursor() { write(1, "\033[?25h\033[?0c", 11); }
static inline void move_cursor_to(int c, int r) { printf("\033[%d;%dH", r, c); }
static inline void unblank_screen_via_file() { write(1, "\033[13]", 5); }

static int update_cursor_pos(void) {
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

/* text_prepare_status
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

	/* Remember where the cursor was */
	if (cur_x != -1)
		update_cursor_pos();

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

	if (cur_x == -1) {
		cur_x = 1;
		cur_y = y+2;
	}
	move_cursor_to(cur_x, cur_y);
	
	hide_cursor();

	barposn = 0;
}

static void text_prepare_status(int printalways, int clearbar, const char *fmt, ...)
{
	va_list va;
	char buf[1024];
	if (fmt) {
		va_start(va, fmt);
		vsnprintf(buf, 1024, fmt, va);
		text_prepare_status_real(printalways, clearbar, buf);
		va_end(va);
	} else
		text_prepare_status_real(printalways, clearbar, NULL);
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

		printf("\nSwitched to console loglevel %d.\n", console_loglevel);

		if (lastloglevel < SUSPEND_ERROR) {
			printf("\n** %s\n", lastheader);
		}
	
	} else if (lastloglevel >= SUSPEND_ERROR) {
		clear_display();
		hide_cursor();
	
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
	if ((msg) && (console_loglevel)) {
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

	/* Calculate progress bar width. Note that whether the
	 * splash screen is on might have changed (this might be
	 * the first call in a new cycle), so we can't take it
	 * for granted that the width is the same as last time
	 * we came in here */
	barwidth = (video_num_columns - 2 * (video_num_columns / 4) - 2);

	set_progress_granularity(barwidth);

	/* Open /dev/vcsa0 so we can find out the cursor position when we need to */
	vcsa_fd = open("/dev/vcsa0", O_RDONLY);
	/* if it errors, don't worry. we'll check later */

	clear_display();
}

static void text_cleanup() {
	if (vcsa_fd >= 0)
		close(vcsa_fd);

	ioctl(STDOUT_FILENO, TCSETSF, (long)&termios);

	show_cursor();
	clear_display();
	move_cursor_to(0, 0);
	/* chvt back? */
}

static void text_redraw() {
	clear_display();
	cur_x = -1;
}

static void text_keypress(int key) {
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
