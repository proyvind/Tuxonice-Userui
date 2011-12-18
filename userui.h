#ifndef _USERUI_H_
#define _USERUI_H_

#include <sys/syscall.h>
#include <sys/types.h>
#include "suspend_userui.h"

#define USERUI_VERSION "1.1-rc2"

struct userui_ops {
	char *name;
	int (*load) ();
	void (*prepare) ();
	void (*unprepare) ();
	void (*cleanup) ();
	void (*message) (__uint32_t type, __uint32_t level,
			__uint32_t normally_logged, char *text);
	void (*update_progress) (__uint32_t value, __uint32_t maximum,
			char *text);
	void (*log_level_change) ();
	void (*redraw) ();
	void (*keypress) (int key);
	unsigned long (*memory_required) ();
	/* For extra cmdline options: */
	char *optstring;
	struct option *longopts;
	int (*option_handler) (char c);
	char *(*cmdline_options) ();
};

extern struct userui_ops userui_text_ops;

#ifdef USE_FBSPLASH
extern struct userui_ops userui_fbsplash_ops;
#define FBSPLASH_OPS (&userui_fbsplash_ops)
#else
#define FBSPLASH_OPS NULL
#endif

#ifdef USE_USPLASH
extern struct userui_ops userui_usplash_ops;
#define USPLASH_OPS (&userui_usplash_ops)
#else
#define USPLASH_OPS NULL
#endif

#define NUM_UIS 3

int send_message(int type, void* buf, int len);
int common_keypress_handler(int key);
void set_console_loglevel(int exiting);
void printk(char *msg, ...);

extern char software_suspend_version[32];
extern int can_use_escape;
extern volatile __uint32_t console_loglevel;
extern volatile __uint32_t suspend_action;
extern volatile __uint32_t suspend_debug;
extern volatile int resuming;

/* excerpts from include/linux/suspend2.h : */

/* debugging levels. */
#define SUSPEND_STATUS		0
#define SUSPEND_UI_MSG		1
#define SUSPEND_ERROR		2
#define SUSPEND_LOW	 	3
#define SUSPEND_MEDIUM	 	4
#define SUSPEND_HIGH	  	5
#define SUSPEND_VERBOSE		6

/* second status register */
enum {
	SUSPEND_REBOOT,
	SUSPEND_PAUSE,
	SUSPEND_LOGALL,
	SUSPEND_CAN_CANCEL,
	SUSPEND_KEEP_IMAGE,
	SUSPEND_FREEZER_TEST,
	SUSPEND_SINGLESTEP,
	SUSPEND_PAUSE_NEAR_PAGESET_END,
	SUSPEND_TEST_FILTER_SPEED,
	SUSPEND_TEST_BIO,
	SUSPEND_NOPAGESET2,
	SUSPEND_IGNORE_ROOTFS,
	SUSPEND_REPLACE_SWSUSP,
	SUSPEND_PAGESET2_FULL,
	SUSPEND_ABORT_ON_RESAVE_NEEDED,
	SUSPEND_NO_MULTITHREADED_IO,
	SUSPEND_NO_DIRECT_LOAD,
	SUSPEND_LATE_CPU_HUTPLUG,
	SUSPEND_GET_MAX_MEM_ALLOCD,
	SUSPEND_NO_FLUSHER_THREAD,
	SUSPEND_NO_PS2_IF_NEEDED
};

/* Debug sections  - if debugging compiled in */
enum {
	SUSPEND_ANY_SECTION,
	SUSPEND_EAT_MEMORY,
	SUSPEND_IO,
	SUSPEND_HEADER,
	SUSPEND_WRITER,
	SUSPEND_MEMORY,
};

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

/*
 * Work around random glibc bugs where getpid() caches an invalid pid.
 */
#define xgetpid() syscall(SYS_getpid)

extern char lastheader[512];
extern int video_num_lines, video_num_columns;

#define get_dlsym(SYMBOL) { \
	char *error; \
	SYMBOL = dlsym(dl_handle, "SYMBOL"); \
	if ((error = dlerror())) { \
		fprintf(stderr, "%s\n", error); \
		return 1; \
	} \
}

#endif /* _USERUI_H_ */
