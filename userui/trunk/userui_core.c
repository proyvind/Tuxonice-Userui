#define _GNU_SOURCE

#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <linux/netlink.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <asm/types.h>

#include "userui.h"

#define NETLINK_SUSPEND2_USERUI 10

#define bail(x...) { perror(x); exit(1); }

static char buf[4096];
static int nlsock;
static int test_run = 0;

char software_suspend_version[32];

int console_loglevel = 1;
int suspend_action = 0;

extern struct userui_ops userui_text_ops;

static struct userui_ops *userui_ops = &userui_text_ops; /* default */

int send_message(int type, void* buf, int len) {
	struct nlmsghdr nl;
	struct iovec iovec[2];

	nl.nlmsg_len = NLMSG_LENGTH(len);
	nl.nlmsg_type = type;
	nl.nlmsg_flags = NLM_F_REQUEST;
	nl.nlmsg_pid = getpid();

	iovec[0].iov_base = &nl; iovec[0].iov_len = sizeof(nl);
	iovec[1].iov_base = buf; iovec[1].iov_len = len;

	if (writev(nlsock, iovec, (buf && len > 0)?2:1) == -1) {
		perror("writev");
		return 0;
	}
	return 1;
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
				test_run = 1;
				break;
			case 'h':
				printf("Help!\n");
				exit(1);
		}

	}

	if (optind < argc) {
		printf("Hmmm. What to do, what to do?\n");
	}
}

static void lock_memory() {
	/* Make sure we don't get swapped out or wiped out mid suspend */
	if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1)
		bail("mlockall");
}

static void get_info() {
	FILE *f = fopen("/proc/software_suspend/version", "r");
	if (f) {
	    fgets(software_suspend_version, sizeof(software_suspend_version), f);
	    fclose(f);
	    software_suspend_version[sizeof(software_suspend_version)-1] = '\0';
	    software_suspend_version[strlen(software_suspend_version)-1] = '\0';
	}

	send_message(USERUI_MSG_GET_LOGLEVEL, NULL, 0);
	send_message(USERUI_MSG_GET_STATE, NULL, 0);
	/* We'll get the reply in our message loop */
}

/* A generic signal handler to ensure we don't quit in times of desperation */
static void sig_hand(int sig) {
	printf("userui: Ack! SIG %d\n", sig);
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
	int fd;

	if ((fd = open("/dev/console", O_RDWR, 0644)) == -1)
		bail("open(\"/dev/console\")");

	if (dup2(fd, STDOUT_FILENO) == -1)
		bail("dup2(fd, STDOUT_FILENO)");
	if (dup2(fd, STDERR_FILENO) == -1)
		bail("dup2(fd, STDERR_FILENO)");

	close(fd);
}

static void open_netlink() {
	struct sockaddr_nl sanl;

	nlsock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_SUSPEND2_USERUI);
	if (nlsock < 0)
		bail("socket");

	memset(&sanl, 0, sizeof(sanl));
	sanl.nl_family = AF_NETLINK;
	if (bind(nlsock, (struct sockaddr *)&sanl, sizeof(sanl)) == -1)
		bail("bind");
}

static int send_ready() {
	int version = SUSPEND_USERUI_INTERFACE_VERSION;

	return send_message(USERUI_MSG_READY, &version, sizeof(version));
}

static void message_loop() {
	fd_set rfds;
	int n;
	struct nlmsghdr *nlh;

	while (1) {
		struct timeval tv = { 0, 1000*1000 }; /* 1 second */
		FD_ZERO(&rfds);
		FD_SET(nlsock, &rfds);

		switch (select(nlsock+1, &rfds, NULL, NULL, &tv)) {
			case -1:
				if (errno == EINTR)
					continue;
				bail("select");
			case 0:
				/* timeout for doing something interactive maybe ? */
				continue;
		}
		
		/* Data is ready. */
		if ((n = recv(nlsock, buf, sizeof(buf), 0)) == -1)
			bail("recv");

		/* Check if the socket was closed on us. */
		if (n == 0)
			return;

		nlh = (struct nlmsghdr *)buf;
		struct userui_msg_params *msg = NLMSG_DATA(nlh);

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
				userui_ops->log_level_change(console_loglevel);
				break;
			case USERUI_MSG_GET_STATE:
				suspend_action = *(int*)NLMSG_DATA(nlh);
				break;
			case USERUI_MSG_CLEANUP:
				/* Cleanup function is called upon exiting. */
				return;
			case USERUI_MSG_REDRAW:
				userui_ops->redraw();
				break;
			case USERUI_MSG_KEYPRESS:
				userui_ops->keypress(*(unsigned int*)NLMSG_DATA(nlh));
				break;
			case NLMSG_ERROR:
				{
					struct nlmsgerr *err;
					err = NLMSG_DATA(nlh);
					if (err->error == 0)
						break; /* just a netlink ack */
					printf("userui: Received netlink error: %s\n",
							strerror(-err->error));
					printf("userui: This was in response to a type %d message.\n",
							err->msg.nlmsg_type);
				}
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

	userui_ops->message(0, 0, 1, "Suspending to disk ...");
	for (i = 0; i <= 800; i++) {
		char buf[128];
		snprintf(buf, 128, "%d/%d MB", i, 800);
		userui_ops->update_progress(i, 800, buf);
		usleep(5*1000);
	}
	usleep(500*1000);
}

int main(int argc, char **argv) {
	handle_params(argc, argv);
	if (!test_run) {
		setup_signal_handlers();
		open_console();
		open_netlink();
	}
	lock_memory();
	get_info();

	userui_ops->prepare();

	if (test_run)
		do_test_run();
	else
		if (send_ready())
			message_loop();

	userui_ops->cleanup();

	close(nlsock);
	return 0;
}
