
#include "userui.h"

static void dummy_prepare() {
}

static void dummy_cleanup() {
}

static void dummy_message(unsigned long type, unsigned long level, int normally_logged, char *dummy) {
}

static void dummy_update_progress(unsigned long value, unsigned long maximum, char *dummy) {
}

static void dummy_log_level_change(int loglevel) {
}

static void dummy_redraw() {
}

static void dummy_keypress(int key) {
}

struct userui_ops userui_dummy_ops = {
	.name = "dummy",
	.prepare = dummy_prepare,
	.cleanup = dummy_cleanup,
	.message = dummy_message,
	.update_progress = dummy_update_progress,
	.log_level_change = dummy_log_level_change,
	.redraw = dummy_redraw,
	.keypress = dummy_keypress,
};
