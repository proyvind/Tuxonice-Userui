#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <stdarg.h>

#include "userui.h"

static struct termios termios_save, termios;
static int num_rows, num_cols;

static inline void clear_display() { write(1, "\2332J", 3); }
static inline void clear_to_eol() { write(1, "\233K", 2); }
static inline void hide_cursor() { write(1, "\233?1c", 4); }
static inline void show_cursor() { write(1, "\233?0c", 4); }
static inline void move_to(int c, int r) { printf("\233%d;%dH", r, c); }

static void print_header() {
	move_to((num_cols - 31) / 2, (num_rows / 3) - 3);
	printf("S O F T W A R E   S U S P E N D");
	move_to(0, num_rows);
	printf("%s", software_suspend_version);
}

static void set_status(char *s, ...) {
	va_list ap;

	move_to(0, num_rows-5);
	clear_to_eol();
	va_start(ap, s);
	vprintf(s, ap);
	va_end(ap);
}

static void text_prepare() {
	struct winsize winsz;
	/* chvt here? */

	setvbuf(stdout, NULL, _IONBF, 0);

	ioctl(STDOUT_FILENO, TCGETS, (long)&termios);
	termios_save = termios;
	termios.c_lflag &= ~ICANON;
	ioctl(STDOUT_FILENO, TCSETSF, (long)&termios);

	ioctl(STDOUT_FILENO, TIOCGWINSZ, &winsz);

	num_rows = winsz.ws_row;
	num_cols = winsz.ws_col;

	hide_cursor();
	clear_display();
	print_header();
}

static void text_cleanup() {
	ioctl(STDOUT_FILENO, TCSETSF, (long)&termios_save);
	set_status("Cleaning up userui...");

	show_cursor();
	clear_display();
	move_to(0, 0);
	/* chvt back? */
}

static void text_message(unsigned long type, unsigned long level, int normally_logged, char *text) {
	set_status("%lu, %lu, \t%s", type, level, text?text:"");
}

static void text_update_progress(unsigned long value, unsigned long maximum, char *text) {
	set_status("%lu/%lu,  \t%s", value, maximum, text?text:"");
}

static void text_log_level_change(int loglevel) {
	set_status("Loglevel changed to %d", loglevel);
}

static void text_redraw() {
	set_status("Redraw");
}

static void text_keypress(int key) {
	set_status("Got key %d", key);
	if (key == 1) { /* Escape */
		send_message(USERUI_MSG_ABORT, NULL, 0);
	}
}

struct userui_ops userui_text_ops = {
	.name = "text",
	.prepare = text_prepare,
	.cleanup = text_cleanup,
	.message = text_message,
	.update_progress = text_update_progress,
	.log_level_change = text_log_level_change,
	.redraw = text_redraw,
	.keypress = text_keypress,
};
