
#include "../userui.h"

static void fbsplash_prepare() {
}

static void fbsplash_cleanup() {
}

static void fbsplash_message(unsigned long type, unsigned long level, int normally_logged, char *fbsplash) {
}

static void fbsplash_update_progress(unsigned long value, unsigned long maximum, char *fbsplash) {
}

static void fbsplash_log_level_change(int loglevel) {
}

static void fbsplash_redraw() {
}

static void fbsplash_keypress(int key) {
}

struct userui_ops userui_fbsplash_ops = {
	.name = "fbsplash",
	.prepare = fbsplash_prepare,
	.cleanup = fbsplash_cleanup,
	.message = fbsplash_message,
	.update_progress = fbsplash_update_progress,
	.log_level_change = fbsplash_log_level_change,
	.redraw = fbsplash_redraw,
	.keypress = fbsplash_keypress,
};
