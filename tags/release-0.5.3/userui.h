#ifndef _USERUI_H_
#define _USERUI_H_

#include "suspend_userui.h"

struct userui_ops {
	char *name;
	void (*prepare) ();
	void (*cleanup) ();
	void (*message) (unsigned long type, unsigned long level, int normally_logged, char *text);
	void (*update_progress) (unsigned long value, unsigned long maximum, char *text);
	void (*log_level_change) ();
	void (*redraw) ();
	void (*keypress) (int key);
	unsigned long (*memory_required) ();
};


int send_message(int type, void* buf, int len);
int common_keypress_handler(int key);
void set_console_loglevel(void);

extern char software_suspend_version[32];
extern volatile int console_loglevel;
extern volatile int suspend_action;

/* excerpts from include/linux/suspend2.h : */

/* debugging levels. */
#define SUSPEND_STATUS		0
#define SUSPEND_ERROR		2
#define SUSPEND_LOW	 	3
#define SUSPEND_MEDIUM	 	4
#define SUSPEND_HIGH	  	5
#define SUSPEND_VERBOSE		6

/* second status register */
enum {
	SUSPEND_REBOOT,
	SUSPEND_PAUSE,
	SUSPEND_SLOW,
	SUSPEND_NOPAGESET2,
	SUSPEND_LOGALL,
	SUSPEND_CAN_CANCEL,
	SUSPEND_KEEP_IMAGE,
	SUSPEND_FREEZER_TEST,
	SUSPEND_FREEZER_TEST_SHOWALL,
	SUSPEND_SINGLESTEP,
	SUSPEND_PAUSE_NEAR_PAGESET_END,
	SUSPEND_USE_ACPI_S4,
	SUSPEND_KEEP_METADATA,
	SUSPEND_TEST_FILTER_SPEED,
	SUSPEND_FREEZE_TIMERS,
	SUSPEND_DISABLE_SYSDEV_SUPPORT,
	SUSPEND_VGA_POST
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


#endif /* _USERUI_H_ */
