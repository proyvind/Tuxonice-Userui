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

#include "linux/kernel/power/suspend_userui.h"
#include "userui.h"

#define NETLINK_SUSPEND2_USERUI 10

#define bail(x...) { perror(x); exit(1); }

static char buf[4096];
static int nlsock;

extern struct userui_ops userui_text_ops;

static struct userui_ops *userui_ops = &userui_text_ops; /* default */

static void handle_params(int argc, char **argv) {
	static char *optstring = "h";
	static struct option longopts[] = {
		{"help", 0, 0, 'h'},
	};

	int c;

	while (1) {
		int optindex;

		c = getopt_long(argc, argv, optstring, longopts, &optindex);
		if (c == -1)
			break;

		switch (c) {
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
	signal(SIGTERM, sig_hand);
	signal(SIGTERM, sig_hand);
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
	struct nlmsghdr nl;

	nl.nlmsg_len = sizeof(nl);
	nl.nlmsg_type = USERUI_MSG_READY;
	nl.nlmsg_flags = NLM_F_REQUEST;
	nl.nlmsg_pid = getpid();

	if (write(nlsock, &nl, sizeof(nl)) == -1) {
		perror("write");
		return 0;
	}
	return 1;
}

static void message_loop() {
	fd_set rfds;
	int n;
	struct nlmsghdr *nlh;
	struct timeval tv = { 0, 1000*1000 }; /* 1 second */

	while (1) {
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
			case USERUI_MSG_LOGLEVEL_CHANGE:
				userui_ops->log_level_change(*(int*)NLMSG_DATA(nlh));
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
			case NLMSG_DONE:
			default:
				printf("userui: Got unknown message %d\n", nlh->nlmsg_type);
				break;
		}
	}
}

int main(int argc, char **argv) {
	handle_params(argc, argv);
	setup_signal_handlers();
	open_console();
	open_netlink();
	lock_memory();

	userui_ops->prepare();

	if (send_ready())
		message_loop();

	userui_ops->cleanup();

	close(nlsock);
	return 0;
}
