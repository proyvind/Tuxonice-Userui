#define _GNU_SOURCE

#include <sys/resource.h>
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

#define PAGE_SIZE 0x1000

#define bail_err(x) do { fprintf(stderr, x": %s\n", strerror(errno)); fflush(stderr); abort(); } while (0)
#define bail(x...) do { fprintf(stderr, ## x); fflush(stderr); abort(); } while (0)

static int nlsock = -1;
static int test_run = 0;

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

	if (writev(nlsock, iovec, (buf && len > 0)?2:1) == -1) {
		perror("writev");
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
				test_run = 1;
				break;
			case 'h':
				fprintf(stderr, "Usage: %s [-t]\n", argv[0]);
				fprintf(stderr, "\nThis userui program has been compiled with the \"%s\" module.\n", userui_ops->name);
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

	send_message(USERUI_MSG_GET_LOGLEVEL, NULL, 0);
	send_message(USERUI_MSG_GET_STATE, NULL, 0);
	/* We'll get the reply in our message loop */
}

static long my_vm_size() {
	FILE *f;
	char s[128];
	long ret;
	
	ret = -1;

	snprintf(s, 128, "/proc/%d/statm", getpid());
	if (!(f = fopen(s, "r")))
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

/* A generic signal handler to ensure we don't quit in times of desperation,
 * risking corrupting the image. */
static void sig_hand(int sig) {
	printf("userui: Ack! SIG %d\n", sig);
	sleep(60*60*1); /* 1 hour */
	_exit(1);
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
		bail_err("open(\"/dev/console\")");

	if (dup2(fd, STDOUT_FILENO) == -1)
		bail_err("dup2(fd, STDOUT_FILENO)");
	if (dup2(fd, STDERR_FILENO) == -1)
		bail_err("dup2(fd, STDERR_FILENO)");

	close(fd);

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

	return send_message(USERUI_MSG_READY, &version, sizeof(version));
}

static struct nlmsghdr *fetch_message() {
	int n;
	static char buf[4096]; /* NOTE: STATIC BUFFER HERE */

	if ((n = recv(nlsock, buf, sizeof(buf), 0)) == -1)
		bail_err("recv");

	/* Check if the socket was closed on us. */
	if (n == 0)
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
		bail("send_message");

	while (1) {
		if (!(nlh = fetch_message()))
			bail("fetch_message() EOF");

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
	struct nlmsghdr *nlh;

	while (1) {
		struct userui_msg_params *msg;

		if (!(nlh = fetch_message()))
			return; /* EOF */

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
				userui_ops->log_level_change(console_loglevel);
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
			case USERUI_MSG_KEYPRESS:
				userui_ops->keypress(*(unsigned int*)NLMSG_DATA(nlh));
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

	userui_ops->log_level_change(console_loglevel);
	userui_ops->message(0, 0, 1, "Suspending to disk ...");
	for (i = 0; i <= 1024; i+=8) {
		char buf[128];
		snprintf(buf, 128, "%d/%d MB", i, 1024);
		userui_ops->update_progress(i, 1024, buf);
		usleep(5*1000);
	}
	usleep(500*1000);
	userui_ops->cleanup();
}

int main(int argc, char **argv) {
	handle_params(argc, argv);
	if (!test_run) {
		setup_signal_handlers();
		open_console();
		open_netlink();
		get_nofreeze();
	}
	lock_memory();
	get_info();

	userui_ops->prepare();

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
