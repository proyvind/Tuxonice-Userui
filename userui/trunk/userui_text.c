#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>

#include "userui.h"

static struct termios termios_save, termios;

static void text_prepare() {
	printf("userui: Preparing userui...\n");
	ioctl(STDOUT_FILENO, TCGETS, (long)&termios);
	termios_save = termios;
	termios.c_lflag &= ~ICANON;
	ioctl(STDOUT_FILENO, TCSETSF, (long)&termios);
}

static void text_cleanup() {
	ioctl(STDOUT_FILENO, TCSETSF, (long)&termios_save);
	printf("userui: Cleaning up userui...\n");
}

static void text_message(unsigned long type, unsigned long level, int normally_logged, char *text) {
	printf("userui: %lu, %lu, \t%s\n", type, level, text?text:"");
}

static void text_update_progress(unsigned long value, unsigned long maximum, char *text) {
	printf("userui: %lu/%lu,  \t%s\n", value, maximum, text?text:"");
}

static void text_log_level_change(int loglevel) {
	printf("userui: Loglevel changed to %d\n", loglevel);
}

static void text_redraw() {
	printf("userui: Redraw\n");
}

static void text_keypress(int key) {
	printf("userui: Got key %d\n", key);
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
