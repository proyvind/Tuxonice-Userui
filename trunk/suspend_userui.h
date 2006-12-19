#ifndef _SUSPEND_USERUI_H_
#define _SUSPEND_USERUI_H_

#define SUSPEND_USERUI_INTERFACE_VERSION 6

enum {
	USERUI_MSG_BASE = 0x10,

	/* Userspace -> Kernel */
	USERUI_MSG_READY = 0x10,
	USERUI_MSG_ABORT = 0x11,
	USERUI_MSG_SET_STATE = 0x12,
	USERUI_MSG_GET_STATE = 0x13,
	USERUI_MSG_GET_DEBUG_STATE = 0x14,
	USERUI_MSG_SET_DEBUG_STATE = 0x15,
	USERUI_MSG_NOFREEZE_ME = 0x16,
	USERUI_MSG_SPACE = 0x18,
	USERUI_MSG_GET_DEBUGGING = 0x19,
	USERUI_MSG_GET_POWERDOWN_METHOD = 0x1A,
	USERUI_MSG_SET_POWERDOWN_METHOD = 0x1B,

	/* Kernel -> Userspace */
	USERUI_MSG_MESSAGE = 0x21,
	USERUI_MSG_PROGRESS = 0x22,
	USERUI_MSG_CLEANUP = 0x24,
	USERUI_MSG_REDRAW = 0x25,
	USERUI_MSG_KEYPRESS = 0x26,
	USERUI_MSG_NOFREEZE_ACK = 0x27,
	USERUI_MSG_IS_DEBUGGING = 0x28,

	USERUI_MSG_MAX,
};

struct userui_msg_params {
	unsigned long a, b, c, d;
	char text[255];
};

#endif /* _SUSPEND_USERUI_H_ */
