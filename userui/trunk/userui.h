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
};


int send_message(int type, void* buf, int len);

extern char software_suspend_version[32];

#endif /* _USERUI_H_ */
