/*
 * userui_core.c - Core of all userspace user interfaces.
 *
 * Copyright (C) 2005, Bernard Blackham <bernard@blackham.com.au>
 * 
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 */

#define _GNU_SOURCE

#include <asm/types.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <linux/kd.h>
#include <linux/netlink.h>
#include <linux/vt.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "userui.h"

#define PAGE_SIZE 0x1000

#define bail_err(x) do { fprintf(stderr, x": %s\n", strerror(errno)); fflush(stderr); abort(); } while (0)
#define bail(x...) do { fprintf(stderr, ## x); fflush(stderr); abort(); } while (0)

static struct termios termios_backup;
static int have_termios_backup = 0;
static int raw_keypresses = 0;
static int nlsock = -1;
static int test_run = 0;
static int running = 0;
static int need_cleanup = 0;
static int safe_to_exit = 1;
static volatile int need_loglevel_change = 0;
static int debugging_enabled = 0;
static int powerdown_method = 0;

char software_suspend_version[32];
int can_use_escape = 0;

static FILE *printk_f = NULL;
static int saved_console_loglevel = -1;
volatile int console_loglevel = 1;
volatile int suspend_action = 0;
volatile int suspend_debug = 0;

volatile int resuming = 0;

extern struct userui_ops *userui_ops;

static int netlink_socket_num = 0;

static char* descriptions[] = {
	"General",
	"Freezer",
	"Memory eating",
	"Pageset",
	"I/O",
	"Bmap",
	"Image header",
	"Image writer",
	"Memory",
	"Extent",
	"Spinlock",
	"Memory pool",
	"Range paranoia",
	"Nosave",
	"Integrity"
};

static void open_misc() {
	if ((printk_f = fopen("/proc/sys/kernel/printk", "r+")) == NULL)
		perror("open(/proc/sys/kernel/printk)");
	else
		setvbuf(printk_f, (char*)NULL, _IONBF, 0);
}

void set_console_loglevel(int exiting) {
	if (!printk_f)
		return;
	fseek(printk_f, 0, SEEK_SET);
	fprintf(printk_f, "%d\n", console_loglevel);

	if (!exiting)
		userui_ops->log_level_change();
}

void get_console_loglevel() {
	if (!printk_f)
		return;
	fseek(printk_f, 0, SEEK_SET);
	fscanf(printk_f, "%d", &console_loglevel);
}

int send_message(int type, void* buf, int len) {
	struct nlmsghdr nl;
	struct iovec iovec[2];

	if (nlsock < 0)
		return 0;

	memset(&nl, 0, sizeof(nl));

	nl.nlmsg_len = NLMSG_LENGTH(len);
	nl.nlmsg_type = type;
	nl.nlmsg_flags = NLM_F_REQUEST;
	nl.nlmsg_pid = xgetpid();

	memset(&iovec, 0, sizeof(iovec));

	iovec[0].iov_base = &nl; iovec[0].iov_len = sizeof(nl);
	iovec[1].iov_base = buf; iovec[1].iov_len = len;

	if (writev(nlsock, iovec, (buf && len > 0)?2:1) == -1)
		return 0;

	return 1;
}

static void request_abort_suspend() {
	if (test_run) {
		exit(0);
	}
	send_message(USERUI_MSG_ABORT, NULL, 0);
	resuming = 1;
}

static void toggle_reboot() {
	suspend_action ^= (1 << SUSPEND_REBOOT);
	send_message(USERUI_MSG_SET_STATE, (int*)&suspend_action, sizeof(suspend_action));
	userui_ops->message(0, SUSPEND_UI_MSG, 1, 
			(suspend_action & (1 << SUSPEND_REBOOT) ?
				 "Rebooting enabled." :
				 "Rebooting disabled."));
}

static void toggle_debug_state(key) {
	int bit = 0;
	char message[80];

	if (key >= 59 && key <= 68)
		bit = key - 59;

	if (key == 87 || key == 88)
		bit = key - 77;
	
	suspend_debug ^= (1 << bit);

	send_message(USERUI_MSG_SET_DEBUG_STATE, (int*)&suspend_debug,
			sizeof(suspend_debug));
	sprintf(message, "%s messages %s", descriptions[bit],
			(suspend_debug & (1 << bit)) ?
			"enabled" : "disabled");
	userui_ops->message(0, SUSPEND_UI_MSG, 1, message);
}

static void toggle_poweroff(void) {
	char message[80];
	static int other_powerdown_method = 0;

	/* 
	 * Toggle between 3 and the original powerdown_method. If the
	 * original method is 3, toggle between that and zero (safest).
	 *
	 * This may have no effect in the end, though, because rebooting
	 * always trumps powering down.
	 */
	if (powerdown_method != 3) {
		other_powerdown_method = powerdown_method;
		powerdown_method = 3;

		if ((suspend_action & (1 << SUSPEND_REBOOT)))
			sprintf(message, "Would suspend to ram but rebooting is enabled.");
		else
			sprintf(message, "Suspend to ram on completion.");
	} else {
		powerdown_method = other_powerdown_method;
		if ((suspend_action & (1 << SUSPEND_REBOOT)))
			sprintf(message, "Would power off but rebooting is enabled.");
		else
			sprintf(message, "Power off on completion.");
	}

	send_message(USERUI_MSG_SET_POWERDOWN_METHOD, (int*)&powerdown_method,
			sizeof(powerdown_method));

	userui_ops->message(0, SUSPEND_UI_MSG, 1, message);
}

static void toggle_pause() {
	if (!debugging_enabled)
		return;

	suspend_action ^= (1 << SUSPEND_PAUSE);
	send_message(USERUI_MSG_SET_STATE, (int*)&suspend_action, sizeof(suspend_action));
	userui_ops->message(0, SUSPEND_UI_MSG, 1, 
			(suspend_action & (1 << SUSPEND_PAUSE) ?
				 "Pause between steps enabled." :
				 "Pause between steps disabled."));
}

static void toggle_singlestep() {
	if (!debugging_enabled)
		return;

	suspend_action ^= (1 << SUSPEND_SINGLESTEP);
	send_message(USERUI_MSG_SET_STATE, (int*)&suspend_action, sizeof(suspend_action));
	userui_ops->message(0, SUSPEND_UI_MSG, 1, 
			(suspend_action & (1 << SUSPEND_SINGLESTEP) ?
				 "Single stepping enabled." :
				 "Single stepping disabled."));
}

static void toggle_log_all() {
	int temp = suspend_action | (1 << SUSPEND_LOGALL);

	if (!debugging_enabled)
		return;

	/* Log this message always */
	send_message(USERUI_MSG_SET_STATE, (int*)&temp, sizeof(temp));
	suspend_action ^= (1 << SUSPEND_LOGALL);
	userui_ops->message(0, SUSPEND_UI_MSG, 1, 
			(suspend_action & (1 << SUSPEND_LOGALL) ?
				 "Logging everything enabled." :
				 "Logging everything disabled."));
	send_message(USERUI_MSG_SET_STATE, (int*)&suspend_action, sizeof(suspend_action));
}

static void notify_space_pressed() {
	send_message(USERUI_MSG_SPACE, NULL, 0);
}

int common_keypress_handler(int key) {
	switch (key) {
		case 0x01: /* Escape */
			request_abort_suspend();
			break;
		case 0x02: /* 1 */
		case 0x03: /* 2 */
		case 0x04: /* 3 */
		case 0x05: /* 4 */
		case 0x06: /* 5 */
		case 0x07: /* 6 */
		case 0x08: /* 7 */
		case 0x09: /* 8 */
		case 0x0a: /* 9 */
		case 0x0b: /* 0 */
			console_loglevel = (key - 1)%10;
			set_console_loglevel(0);
			break;
		case 0x13: /* R */
			toggle_reboot();
			break;
		case 0x18: /* O */
			toggle_poweroff();
			break;
		case 0x19: /* P */
			toggle_pause();
			break;
		case 0x1F: /* S */
			toggle_singlestep();
			break;
		case 0x26: /* L */
			toggle_log_all();
			break;
		case 0x39: /* Spacebar */
			notify_space_pressed();
			break;
		case 0x3B: /* F1 */
		case 0x3C: /* F2 */
		case 0x3D: /* F3 */
		case 0x3E: /* F4 */
		case 0x3F: /* F5 */
		case 0x40: /* F6 */
		case 0x41: /* F7 */
		case 0x42: /* F8 */
		case 0x43: /* F9 */
		case 0x44: /* F10 */
		case 0x57: /* F11 */
		case 0x58: /* F12 */
			toggle_debug_state(key);
			break;
		default:
			return 0;
	}
	return 1;
}

static void handle_params(int argc, char **argv) {
	static char global_optstring[] = "htc:";
	static struct option global_longopts[] = {
		{"help", 0, 0, 'h'},
		{"test", 0, 0, 't'},
		{"channel", 1, 0, 'c'},
		{NULL, 0, 0, 0},
	};

	int len;
	char *optstring, *s1, *s2;
	struct option *longopts, *o1, *o2;
	int c;

	/* Create optstring */
	len = sizeof(global_optstring)-1;
	if (userui_ops->optstring)
		len += strlen(userui_ops->optstring);
	len++; /* NULL terminator */

	s1 = optstring = (char*)malloc(len);
	for (s2 = global_optstring; (*s1++ = *s2++); );
	if (userui_ops->optstring)
		for (s1--, s2 = userui_ops->optstring; (*s1++ = *s2++););

	/* Create longopts */
	len = sizeof(global_longopts)/sizeof(global_longopts[0])-1;
	if (userui_ops->longopts) {
		o1 = userui_ops->longopts;
		while ((o1++)->name) len++;
	}
	len++; /* NULL terminator */

	o1 = longopts = (struct option*)malloc(len * sizeof(struct option));
	for (o2 = global_longopts; o2->name; o1++, o2++)
		memcpy(o1, o2, sizeof(*o1));
	if (userui_ops->longopts)
		for (o2 = userui_ops->longopts; o2->name; o1++, o2++)
			memcpy(o1, o2, sizeof(*o1));
	memset(o1, 0, sizeof(*o1));

	while (1) {
		int optindex;

		c = getopt_long(argc, argv, optstring, longopts, &optindex);
		if (c == -1)
			break;

		switch (c) {
			case 'c':
				sscanf(optarg, "%d", &netlink_socket_num);
				break;
			case 't':
				test_run++;
				break;
			case 'h':
				fprintf(stderr,
"Usage: %s [options]\n"
"\n"
"  -t, --test\n"
"     Specifying -t once will give an demo of this module.\n"
"     Specifying -t twice will make the demo run as fast as it can.\n"
"     (useful for performance testing).\n"
"%s"
"\n"
"Suspend2 UserUI version %s (%s module)\n",
					argv[0],
					(userui_ops->cmdline_options)?userui_ops->cmdline_options():"",
					USERUI_VERSION, userui_ops->name);
				exit(1);
			default:
				if (userui_ops->option_handler) {
					userui_ops->option_handler(c);
				}
		}

	}

	if (optind < argc) {
		fprintf(stderr, "Invalid parameters: ");
		while (optind < argc)
			fprintf(stderr, "%s ", argv[optind++]);
		fprintf(stderr, "\n");
		/* Don't actually exit. Just complain. */
	}

	free(optstring);
	free(longopts);
}

static void lock_memory() {
	/* Make sure we don't get swapped out */
	if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1)
		bail_err("mlockall");
}

static void get_info() {
	FILE *f = fopen("/proc/suspend2/version", "r");
	if (f) {
	    fgets(software_suspend_version, sizeof(software_suspend_version), f);
	    fclose(f);
	    software_suspend_version[sizeof(software_suspend_version)-1] = '\0';
	    software_suspend_version[strlen(software_suspend_version)-1] = '\0';
	}

	f = fopen("/proc/suspend2/enable_escape", "r");
	if (f) {
	    fscanf(f, "%d", &can_use_escape);
	    fclose(f);
	}

	if (!send_message(USERUI_MSG_GET_STATE, NULL, 0)) {
		bail_err("send_message");
	}
	
	if (!send_message(USERUI_MSG_GET_DEBUG_STATE, NULL, 0)) {
		bail_err("send_message");
	}
	
	if (!send_message(USERUI_MSG_GET_DEBUGGING, NULL, 0)) {
		bail_err("send_message");
	}

	if (!send_message(USERUI_MSG_GET_POWERDOWN_METHOD, NULL, 0)) {
		bail_err("send_message");
	}
	
	/* We'll get the replies in our message loop */

	get_console_loglevel();
	saved_console_loglevel = console_loglevel;
}

static long my_vm_size() {
	FILE *f;
	long ret;
	
	ret = -1;

	if (!(f = fopen("/proc/self/statm", "r")))
		goto out;

	if (fscanf(f, "%ld", &ret) == 1)
		ret *= PAGE_SIZE;

out:
	if (f)
		fclose(f);
	return ret;
}

static void reserve_memory(unsigned long bytes) {
	struct rlimit r;
	long vm_size;

	/* round up to the next page */
	bytes = (bytes + PAGE_SIZE - 1) & ~PAGE_SIZE;

	/* find out our existing VM usage */
	if ((vm_size = my_vm_size()) == -1)
		return;

	/* Give an upper limit on our maximum address space so that we don't starve
	 * Software Suspend of memory.
	 * FIXME - this never actually gets reported to software suspend! Perhaps
	 * suspend_userui should look at our RLIMIT_AS ?
	 */
	r.rlim_cur = r.rlim_max = vm_size + bytes;

	setrlimit(RLIMIT_AS, &r);
}

static void enforce_lifesavers() {
	struct rlimit r;

	r.rlim_cur = r.rlim_max = 0;

	/* Set RLIMIT_NOFILE to prevent files being opened. */
	setrlimit(RLIMIT_NOFILE, &r);

	/* Set RLIMIT_NPROC to prevent process forks. */
	setrlimit(RLIMIT_NPROC, &r);

	/* Never core dump - that's bad too. */
	setrlimit(RLIMIT_CORE, &r);
}

static void restore_console() {
	ioctl(STDIN_FILENO, KDSKBMODE, K_XLATE);
	ioctl(STDOUT_FILENO, KDSETMODE, KD_TEXT);
	write(1, "\033[?25h\033[?0c", 11);

	if (saved_console_loglevel >= 0) {
		console_loglevel = saved_console_loglevel;
		set_console_loglevel(1);
	}

	if (need_cleanup) {
		userui_ops->cleanup();
		need_cleanup = 0;
	}

	if (have_termios_backup)
		tcsetattr(STDIN_FILENO, TCSANOW, &termios_backup);
}

/* A generic signal handler to ensure we don't quit in times of desperation,
 * risking corrupting the image. */
static void sig_hand(int sig) {
	restore_console();

	printf("userui: Ack! SIG %d\n", *(int*)sig);

	if (test_run)
		exit(1);

	if (!safe_to_exit)
		sleep(60*60*1); /* 1 hour */
	_exit(1);
}

static char ascii_to_raw(char a) {
	/* Table from drivers/char/keyboard.c */
	const unsigned char raw_to_ascii[] =
        "\000\0331234567890-=\177\t"                    /* 0x00 - 0x0f */
        "qwertyuiop[]\r\000as"                          /* 0x10 - 0x1f */
        "dfghjkl;'`\000\\zxcv"                          /* 0x20 - 0x2f */
        "bnm,./\000*\000 \000\201\202\203\204\205"      /* 0x30 - 0x3f */
        "\206\207\210\211\212\000\000789-456+1"         /* 0x40 - 0x4f */
        "230\177\000\000\213\214\000\000\000\000\000\000\000\000\000\000" /* 0x50 - 0x5f */
        "\r\000/";                                      /* 0x60 - 0x6f */
	static unsigned char ascii_to_raw[sizeof(raw_to_ascii)];
	static int ascii_to_raw_built = 0;

	if (!ascii_to_raw_built) {
		int i;
		memset(ascii_to_raw, 0, sizeof(ascii_to_raw));
		for (i = 0; i < sizeof(raw_to_ascii); i++)
			if (raw_to_ascii[i])
				ascii_to_raw[tolower(raw_to_ascii[i])] = i;
		ascii_to_raw_built = 1;
	}

	return ascii_to_raw[(int)a];
}

static void keypress_signal_handler(int sig) {
	static char next_is_escaped = 0;
	unsigned char a, b;

	if (!running) /* We're not running yet */
		return;

	while (1) {
		if (next_is_escaped) {
			if (read(STDIN_FILENO, &b, 1) <= 0)
				break;
			else {
				if (!raw_keypresses)
					a = ascii_to_raw(a);
				next_is_escaped = 0;
				userui_ops->keypress((a << 8)|b);
			}
		} else {
			if (read(STDIN_FILENO, &a, 1) <= 0)
				break;
			else {
				if (!raw_keypresses)
					a = ascii_to_raw(a);
				if (a == 0xe0) {
					next_is_escaped = 1;
					continue;
				}
				userui_ops->keypress(a);
			}
		}
	}
}

static void install_sighand(int signum, sighandler_t handler) {
	struct sigaction act;
	act.sa_handler = handler;
	sigfillset(&act.sa_mask);
	act.sa_flags = SA_RESTART;
	sigaction(signum, &act, NULL);
}

static void setup_signal_handlers() {
	/* In a vague numerical order ... */
	install_sighand(SIGHUP, sig_hand);
	install_sighand(SIGINT, sig_hand);
	install_sighand(SIGQUIT, sig_hand);
	/* Pass on SIGILL... that does signal impending doom */
	install_sighand(SIGABRT, sig_hand);
	install_sighand(SIGFPE, sig_hand);
	/* Can't do much about SIGKILL */
	install_sighand(SIGBUS, sig_hand);
	install_sighand(SIGSEGV, sig_hand);
	install_sighand(SIGPIPE, sig_hand);
	install_sighand(SIGALRM, sig_hand);
	install_sighand(SIGUSR1, sig_hand);
	install_sighand(SIGUSR2, sig_hand);
}

static void open_console() {
	int fd;

	if ((fd = open("/dev/console", O_RDWR)) == -1)
		bail_err("open(\"/dev/console\", O_RDWR)");

	/* We're about to replace FDs 0-2, so make sure this isn't one of them. */
	if (fd <= 2)
		fd = dup2(fd, 42);

	if (dup2(fd, STDIN_FILENO) == -1)
		bail_err("dup2(fd_r, STDIN_FILENO)");
	if (dup2(fd, STDOUT_FILENO) == -1)
		bail_err("dup2(fd, STDOUT_FILENO)");
	if (dup2(fd, STDERR_FILENO) == -1)
		bail_err("dup2(fd, STDERR_FILENO)");

	close(fd);
}

static void prepare_console() {
	int flags;
	struct termios t;
	
	/* Backup and set our favourite termios settings */
	if (tcgetattr(STDIN_FILENO, &t) == -1) {
		perror("tcgetattr");
	} else {
		memcpy(&termios_backup, &t, sizeof(t));
		have_termios_backup = 1;

		/* t.c_lflag &= ~(ICANON|ECHO|IXOFF|IGNBRK|BRKINT|); */
		t.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
		t.c_lflag &= ~(ISIG|ICANON|ECHO);
		t.c_cc[VTIME] = 0;
		t.c_cc[VMIN] = 0;
		tcsetattr(STDIN_FILENO, TCSANOW, &t);
	}

	/* Make sure we clean up properly */
	atexit(restore_console);

	/* Want to receive raw keypresses */
	if (ioctl(STDIN_FILENO, KDSKBMODE, K_MEDIUMRAW) == 0)
		raw_keypresses = 1;
	
	/* And be notified about them asynchronously */
	install_sighand(SIGIO, keypress_signal_handler);

	if (fcntl(STDIN_FILENO, F_SETOWN, xgetpid()) == -1)
		bail_err("fcntl(STDIN_FILENO, F_SETOWN)");

	flags = O_RDONLY | O_ASYNC | O_NONBLOCK;
	if (fcntl(STDIN_FILENO, F_SETFL, flags) == -1)
		bail_err("fcntl(STDIN_FILENO, F_SETFL)");

	/* Set outputs to non-buffered */
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
}

static void open_netlink() {
	struct sockaddr_nl sanl;

	nlsock = socket(PF_NETLINK, SOCK_DGRAM, netlink_socket_num);
	if (nlsock < 0)
		bail_err("socket");

	memset(&sanl, 0, sizeof(sanl));
	sanl.nl_family = AF_NETLINK;
	if (bind(nlsock, (struct sockaddr *)&sanl, sizeof(sanl)) == -1)
		bail_err("bind");
}

static int send_ready() {
	int version = SUSPEND_USERUI_INTERFACE_VERSION;

	safe_to_exit = 0;

	return send_message(USERUI_MSG_READY, &version, sizeof(version));
}

static struct nlmsghdr *fetch_message(void* buf, int buf_size, int non_block) {
	static char local_buf[4096];
	long flags = 0;
	int n;

	if (!buf) {
		buf = (void*)local_buf;
		buf_size = sizeof(local_buf);
	}

	if (non_block) {
		if ((flags = fcntl(nlsock, F_GETFL)) == -1)
			return NULL;
		flags |= O_NONBLOCK;
		if (fcntl(nlsock, F_SETFL, flags) == -1)
			return NULL;
	}

	if ((n = recv(nlsock, buf, buf_size, 0)) == -1) {
		if (!non_block && errno != EAGAIN && errno != EINTR)
			bail_err("recv");
	}

	if (non_block) {
		flags &= ~O_NONBLOCK;
		fcntl(nlsock, F_SETFL, flags);
	}

	/* Check if the socket was closed on us, or no data was read from a
	 * non-blocking fd. */
	if (n <= 0)
		return NULL;

	return (struct nlmsghdr *)buf;
}

static void report_nl_error(struct nlmsghdr *nlh) {
	struct nlmsgerr *err;
	err = NLMSG_DATA(nlh);
	if (err->error == 0)
		return; /* just a netlink ack */
	fprintf(stderr, "userui: Received netlink error: %s\n",
			strerror(-err->error));
	fprintf(stderr, "userui: This was in response to a type %d message.\n",
			err->msg.nlmsg_type);
}

static void get_nofreeze() {
	struct nlmsghdr *nlh;
	struct userui_msg_params *msg;

	if (!send_message(USERUI_MSG_NOFREEZE_ME, NULL, 0))
		bail_err("send_message");

	while (1) {
		if (!(nlh = fetch_message(NULL, 0, 0)))
			bail_err("fetch_message() EOF");

		msg = NLMSG_DATA(nlh);

		switch (nlh->nlmsg_type) {
			case USERUI_MSG_NOFREEZE_ACK:
				return;
			case NLMSG_ERROR:
				report_nl_error(nlh);
				break;
		}
	}
}

static void unblank_screen() {
	int i = 4 /* TIOCL_UNBLANKSCREEN - see console_ioctl(4) */;
	ioctl(1, TIOCLINUX, &i);
}

static void message_loop() {
	static char buf1[4096];
	struct nlmsghdr *nlh;

	while (1) {
		struct userui_msg_params *msg;

		if (!(nlh = fetch_message(buf1, sizeof(buf1), 0)))
			return; /* EOF */

		msg = NLMSG_DATA(nlh);

		switch (nlh->nlmsg_type) {
			case USERUI_MSG_MESSAGE:
				userui_ops->message(msg->a, msg->b, msg->c, msg->text);
				break;
			case USERUI_MSG_PROGRESS:
				userui_ops->update_progress(msg->a, msg->b, msg->text);
				break;
			case USERUI_MSG_GET_STATE:
				suspend_action = *(int*)NLMSG_DATA(nlh);
				break;
			case USERUI_MSG_GET_DEBUG_STATE:
				suspend_debug = *(int*)NLMSG_DATA(nlh);
				break;
			case USERUI_MSG_IS_DEBUGGING:
				debugging_enabled = *(int *)NLMSG_DATA(nlh);
				break;
			case USERUI_MSG_GET_POWERDOWN_METHOD:
				powerdown_method = *(int *)NLMSG_DATA(nlh);
				break;
			case USERUI_MSG_CLEANUP:
				userui_ops->cleanup();
				send_message(USERUI_MSG_CLEANUP, NULL, 0);
				close(nlsock);
				exit(0);
			case USERUI_MSG_REDRAW:
				resuming = 1;
				userui_ops->redraw();
				unblank_screen();
				break;
			case NLMSG_ERROR:
				report_nl_error(nlh);
				break;
			case NLMSG_DONE:
				break;
			default:
				printf("userui: Received unknown message %d\n", nlh->nlmsg_type);
				break;
		}

		if (need_loglevel_change) {
			need_loglevel_change = 0;
			userui_ops->log_level_change();
		}
	}
}

static void do_test_run() {
	int i;
	int max = 1024;

	/* If test_run == 1, just give them an example display.
	 * If test_run >= 2, go as fast as we can (for performance timings).
	 */

	userui_ops->log_level_change();
	userui_ops->message(0, 0, 1, "Freezing processes ...");
	if (test_run == 1)
		usleep(200*1000);
	userui_ops->message(0, 0, 1, "Preparing image ...");
	if (test_run == 1)
		usleep(200*1000);
	userui_ops->message(0, 0, 1, "Writing caches ...");

	for (i = 0; i <= max; i+=2) {
		char buf[128];
		snprintf(buf, 128, "%d/%d MB", i, max);
		userui_ops->update_progress(i, max, buf);

		if (i == 2*max/3) {
			userui_ops->message(0, 0, 0, "Doing atomic copy ...");
			if (test_run == 1)
				usleep(800*1000);
			userui_ops->redraw();
		}
		if (i == 2 + 2*max/3) {
			userui_ops->message(0, 0, 0, "Writing kernel data ...");
			if (test_run == 1)
				usleep(800*1000);
		}
		if (test_run == 1)
			usleep(10*1000);

		if (need_loglevel_change) {
			need_loglevel_change = 0;
			userui_ops->log_level_change();
		}
	}

	if (test_run == 1)
		usleep(400*1000);

	userui_ops->cleanup();
}

int main(int argc, char **argv) {
	handle_params(argc, argv);
	setup_signal_handlers();
	open_console();
	open_misc();
	if (!test_run) {
		open_netlink();
		get_nofreeze();
		get_info();
	}

	lock_memory();

	prepare_console();

	userui_ops->prepare();

	need_cleanup = 1;
	running = 1;

	nice(1);

	if (userui_ops->memory_required)
		reserve_memory(userui_ops->memory_required());
	else
		reserve_memory(4*1024*1024); /* say 4MB */

	enforce_lifesavers();

	if (test_run) {
		safe_to_exit = 0;

		do_test_run();
		return 0;
	}

	if (send_ready())
		message_loop();

	/* The only point we ever reach here is if message_loop crashed out.
	 * If this is the case, we should spin for a few hours before exiting to
	 * ensure that we don't corrupt stuff on disk (if we're past the atomic
	 * copy).
	 */
	sleep(60*60*1); /* 1 hours */
	_exit(1);
}
