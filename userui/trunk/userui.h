#ifndef _USERUI_H_
#define _USERUI_H_

#include "linux/kernel/power/suspend_userui.h"

struct userui_ops {
	char *name;
	void (*prepare) ();
	void (*cleanup) ();
	void (*message) (unsigned long type, unsigned long level, int normally_logged, char *text);
	void (*update_progress) (unsigned long value, unsigned long maximum, char *text);
	void (*log_level_change) (int loglevel);
	void (*redraw) ();
	void (*keypress) (int key);
	unsigned long (*memory_required) ();
};


int send_message(int type, void* buf, int len);

extern char software_suspend_version[32];
extern int console_loglevel;
extern int suspend_action;

/* excerpts from include/linux/suspend.h : */

/* debugging levels. */
#define SUSPEND_STATUS		0
#define SUSPEND_ERROR		2
#define SUSPEND_LOW	 	3
#define SUSPEND_MEDIUM	 	4
#define SUSPEND_HIGH	  	5
#define SUSPEND_VERBOSE		6
/* second status register */
#define SUSPEND_REBOOT			0
#define SUSPEND_PAUSE			2
#define SUSPEND_SLOW			3
#define SUSPEND_NOPAGESET2		7
#define SUSPEND_LOGALL			8
#define SUSPEND_CAN_CANCEL		11
#define SUSPEND_KEEP_IMAGE		13
#define SUSPEND_FREEZER_TEST		14
#define SUSPEND_FREEZER_TEST_SHOWALL	15
#define SUSPEND_SINGLESTEP		16
#define SUSPEND_PAUSE_NEAR_PAGESET_END	17
#define SUSPEND_USE_ACPI_S4		18
#define SUSPEND_KEEP_METADATA		19
#define SUSPEND_TEST_FILTER_SPEED	20
#define SUSPEND_FREEZE_TIMERS		21
#define SUSPEND_DISABLE_SYSDEV_SUPPORT	22
#define SUSPEND_RETRY_RESUME		23
#define SUSPEND_VGA_POST		24


#endif /* _USERUI_H_ */
