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
#include <linux/kd.h>
#include <linux/netlink.h>
#include <linux/vt.h>
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

#define NETLINK_SUSPEND2_USERUI 10

#define PAGE_SIZE 0x1000

#define bail_err(x) do { fprintf(stderr, x": %s\n", strerror(errno)); fflush(stderr); abort(); } while (0)
#define bail(x...) do { fprintf(stderr, ## x); fflush(stderr); abort(); } while (0)

static struct termios termios_backup;
static int nlsock = -1;
static int test_run = 0;
static int need_cleanup = 0;
static int safe_to_exit = 1;

char software_suspend_version[32];

int console_loglevel = 1;
int suspend_action = 0;

#ifndef USERUI_MODULE
#define USERUI_MODULE userui_text_ops;
#endif

extern struct userui_ops *userui_ops;

int send_message(int type, void* buf, int len) {
	struct nlmsghdr nl;
	struct iovec iovec[2];

	if (nlsock < 0)
		return 0;

	nl.nlmsg_len = NLMSG_LENGTH(len);
	nl.nlmsg_type = type;
	nl.nlmsg_flags = NLM_F_REQUEST;
	nl.nlmsg_pid = getpid();

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
}

static void toggle_reboot() {
	suspend_action ^= (1 << SUSPEND_REBOOT);
	send_message(USERUI_MSG_SET_STATE, &suspend_action, sizeof(suspend_action));
	userui_ops->message(1, SUSPEND_STATUS, 1, 
			(suspend_action & (1 << SUSPEND_REBOOT) ?
				 "Rebooting enabled." :
				 "Rebooting disabled."));
}

static void toggle_pause() {
	suspend_action ^= (1 << SUSPEND_PAUSE);
	send_message(USERUI_MSG_SET_STATE, &suspend_action, sizeof(suspend_action));
	userui_ops->message(1, SUSPEND_STATUS, 1, 
			(suspend_action & (1 << SUSPEND_PAUSE) ?
				 "Pause between steps enabled." :
				 "Pause between steps disabled."));
}

static void toggle_singlestep() {
	suspend_action ^= (1 << SUSPEND_SINGLESTEP);
	send_message(USERUI_MSG_SET_STATE, &suspend_action, sizeof(suspend_action));
	userui_ops->message(1, SUSPEND_STATUS, 1, 
			(suspend_action & (1 << SUSPEND_SINGLESTEP) ?
				 "Single stepping enabled." :
				 "Single stepping disabled."));
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
			send_message(USERUI_MSG_SET_LOGLEVEL,
					&console_loglevel, sizeof(console_loglevel));
			break;
		case 0x13: /* R */
			toggle_reboot();
			break;
		case 0x19: /* P */
			toggle_pause();
			break;
		case 0x1F: /* S */
			toggle_singlestep();
			break;
		default:
			return 0;
	}
	return 1;
}

int set_progress_granularity(int n) {
	/* n is how many distinct progress statuses can be displayed */
	if (n < 1)
		n = 1;
	return send_message(USERUI_MSG_SET_PROGRESS_GRANULARITY, &n, sizeof(int));
}

static void handle_params(int argc, char **argv) {
	static char *optstring = "ht";
	static struct option longopts[] = {
		{"help", 0, 0, 'h'},
		{"test", 0, 0, 't'},
	};

	int c;

	while (1) {
		int optindex;

		c = getopt_long(argc, argv, optstring, longopts, &optindex);
		if (c == -1)
			break;

		switch (c) {
			case 't':
				test_run++;
				break;
			case 'h':
				fprintf(stderr, "Usage: %s [-t [-t]]\n\n", argv[0]);
				fprintf(stderr, "  Specifying -t once will give an demo of this module.\n");
				fprintf(stderr, "  Specifying -t twice will make the demo run as fast as it can.\n");
				fprintf(stderr, "  (useful for performance testing).\n\n");
				fprintf(stderr, "This userui program has been compiled with the \"%s\" module.\n", userui_ops->name);
				exit(1);
		}

	}

	if (optind < argc) {
		printf("Hmmm. What to do, what to do?\n");
	}
}

static void lock_memory() {
	/* Make sure we don't get swapped out */
	if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1)
		bail_err("mlockall");
}

static void get_info() {
	FILE *f = fopen("/proc/software_suspend/version", "r");
	if (f) {
	    fgets(software_suspend_version, sizeof(software_suspend_version), f);
	    fclose(f);
	    software_suspend_version[sizeof(software_suspend_version)-1] = '\0';
	    software_suspend_version[strlen(software_suspend_version)-1] = '\0';
	}

	if (!send_message(USERUI_MSG_GET_LOGLEVEL, NULL, 0) ||
		!send_message(USERUI_MSG_GET_STATE, NULL, 0)) {
		bail_err("send_message");
	}
	/* We'll get the reply in our message loop */
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
	if (ioctl(STDIN_FILENO, KDSKBMODE, K_XLATE) == -1)
		perror("fcntl(STDIN_FILENO, KDSKBMODE, K_XLATE)");
	ioctl(STDOUT_FILENO, KDSETMODE, KD_TEXT);
	write(1, "\033[?25h\033[?0c", 11);
	tcsetattr(STDIN_FILENO, TCSANOW, &termios_backup);
}

/* A generic signal handler to ensure we don't quit in times of desperation,
 * risking corrupting the image. */
static void sig_hand(int sig) {
	printf("userui: Ack! SIG %d\n", sig);

	if (need_cleanup)
		userui_ops->cleanup();

	if (test_run)
		exit(1);

	restore_console();
	if (!safe_to_exit)
		sleep(60*60*1); /* 1 hour */
	_exit(1);
}

static void keypress_signal_handler(int sig) {
	int a, b;

	if (!need_cleanup) /* We're not running yet */
		return;

	while (read(STDIN_FILENO, &a, 1) != -1) {
		b = 0;

		if (a == 0xe0) /* escaped scancode */
			read(STDIN_FILENO, &b, 1); /* FIXME - handle error */

		userui_ops->keypress((b << 8)|a);
	}
}

static void setup_signal_handlers() {
	/* In a vague numerical order ... */
	signal(SIGHUP, sig_hand);
	signal(SIGINT, sig_hand);
	signal(SIGQUIT, sig_hand);
	/* Pass on SIGILL... that does signal impending doom */
	signal(SIGABRT, sig_hand);
	signal(SIGFPE, sig_hand);
	/* Can't do much about SIGKILL */
	signal(SIGBUS, sig_hand);
	signal(SIGSEGV, sig_hand);
	signal(SIGPIPE, sig_hand);
	signal(SIGALRM, sig_hand);
	signal(SIGUSR1, sig_hand);
	signal(SIGUSR2, sig_hand);
}

static void open_console() {
	int fd_w;

	if ((fd_w = open("/dev/console", O_RDWR)) == -1)
		bail_err("open(\"/dev/console\", O_RDWR)");

	/* We're about to replace FDs 0-2, so make sure this isn't one of them. */
	if (fd_w <= 2)
		fd_w = dup2(fd_w, 42);

	if (dup2(fd_w, STDIN_FILENO) == -1)
		bail_err("dup2(fd_r, STDIN_FILENO)");
	if (dup2(fd_w, STDOUT_FILENO) == -1)
		bail_err("dup2(fd_w, STDOUT_FILENO)");
	if (dup2(fd_w, STDERR_FILENO) == -1)
		bail_err("dup2(fd_w, STDERR_FILENO)");

	close(fd_w);

}

static void prepare_console() {
	int flags;
	struct termios t;
	
	/* Backup and set our favourite termios settings */
	if (tcgetattr(STDIN_FILENO, &t) == -1) {
		perror("tcgetattr");
	} else {
		memcpy(&termios_backup, &t, sizeof(t));
		/* cfmakeraw(&t); */
		t.c_lflag &= ~(ICANON|ECHO);
		tcsetattr(STDIN_FILENO, TCSANOW, &t);
	}

	/* Make sure we clean up properly */
	atexit(restore_console);

	/* Receive raw keypresses */
	if (ioctl(STDIN_FILENO, KDSKBMODE, K_MEDIUMRAW) == -1)
		bail_err("fcntl(STDIN_FILENO, KDSKBMODE, K_MEDIUMRAW)");
	
	/* And be notified about them asynchronously */
	signal(SIGIO, keypress_signal_handler);

	if (fcntl(STDIN_FILENO, F_SETOWN, getpid()) == -1)
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

	nlsock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_SUSPEND2_USERUI);
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
		if (!non_block || errno != EAGAIN)
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

static void message_loop() {
	static char buf1[4096], buf2[4096];
	struct nlmsghdr *nlh, *nlh2;

	while (1) {
		struct userui_msg_params *msg;
		int missed_progress_events = 0;

		if (!(nlh = fetch_message(buf1, sizeof(buf1), 0)))
			return; /* EOF */

		while (nlh->nlmsg_type == USERUI_MSG_PROGRESS) {
			nlh2 = fetch_message(buf2, sizeof(buf2), 1);
			if (nlh2 == NULL)
				break;

			if (nlh2->nlmsg_type == USERUI_MSG_PROGRESS) {
				/* Ignore the message in nlh and keep on reading */
				memcpy(buf1, buf2, sizeof(buf2));
				missed_progress_events++;
				if (missed_progress_events == 20)
					break;
			}
		}

		msg = NLMSG_DATA(nlh);

		switch (nlh->nlmsg_type) {
			case USERUI_MSG_MESSAGE:
				userui_ops->message(msg->a, msg->b, msg->c, msg->text);
				break;
			case USERUI_MSG_PROGRESS:
				userui_ops->update_progress(msg->a, msg->b, msg->text);
				break;
			case USERUI_MSG_GET_LOGLEVEL:
			case USERUI_MSG_LOGLEVEL_CHANGE:
				console_loglevel = *(int*)NLMSG_DATA(nlh);
				userui_ops->log_level_change();
				break;
			case USERUI_MSG_GET_STATE:
				suspend_action = *(int*)NLMSG_DATA(nlh);
				break;
			case USERUI_MSG_CLEANUP:
				userui_ops->cleanup();
				close(nlsock);
				exit(0);
			case USERUI_MSG_REDRAW:
				userui_ops->redraw();
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
	}
}

static void do_test_run() {
	int i;
	int max = 1024;

	/* If test_run == 1, just give them an example display.
	 * If test_run >= 2, go as fast as we can (for performance timings).
	 */

	console_loglevel = 1;
	userui_ops->log_level_change();
	userui_ops->message(0, 0, 1, "Writing caches ...");

	for (i = 0; i <= max; i+=2) {
		char buf[128];
		snprintf(buf, 128, "%d/%d MB", i, max);
		userui_ops->update_progress(i, max, buf);

		if (i == 2*max/3) {
			userui_ops->message(0, 0, 0, "Doing atomic copy");
			if (test_run == 1)
				usleep(800*1000);
		}
		if (test_run == 1)
			usleep(10*1000);
	}

	if (test_run == 1)
		usleep(400*1000);

	userui_ops->cleanup();
}

int main(int argc, char **argv) {
	handle_params(argc, argv);
	setup_signal_handlers();
	open_console();
	if (!test_run) {
		open_netlink();
		get_nofreeze();
		get_info();
	}

	lock_memory();

	prepare_console();

	userui_ops->prepare();

	need_cleanup = 1;

	nice(1);

	if (userui_ops->memory_required)
		reserve_memory(userui_ops->memory_required());
	else
		reserve_memory(4*1024*1024); /* say 4MB */

	enforce_lifesavers();

	if (test_run) {
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
