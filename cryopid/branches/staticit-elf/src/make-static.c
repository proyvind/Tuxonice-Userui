/*
 * Process state saver
 *   (C) 2004 Bernard Blackham <bernard@blackham.com.au>
 *
 * Licensed under a BSD-ish license.
 */


#include <sys/types.h>
#include <linux/user.h>
#include <sys/ptrace.h>
#include <fcntl.h>
#include <elf.h>
#include "process.h"

extern void write_stub(int fd);

void usage(char* argv0) {
    fprintf(stderr,
"Usage: %s [options] <output filename> <path to executable>\n"
"\n"
"This is used to suspend the state of a single running process to a\n"
"self-executing file.\n"
"\n"
"This program is part of CryoPID. http://cryopid.berlios.de/\n",
    argv0);
    exit(1);
}

long get_entry_addr(char* fullpath) {
    Elf32_Ehdr e;
    int fd;

    fd = open(fullpath, O_RDONLY);
    if (fd == -1) {
	perror("open");
	exit(1);
    }
    read(fd, &e, sizeof(e));
    close(fd);
    return e.e_entry;
}

pid_t make_my_process(char* fullpath) {
    long entry_addr;
    int status;
    pid_t pid, wpid;

    entry_addr = get_entry_addr(fullpath);

    switch (pid = fork()) {
	case 0:
	    /* child. ptrace and exec. */
	    if (ptrace(PTRACE_TRACEME, 0, 0, 0) == -1) {
		perror("ptrace(PTRACE_TRACEME)");
		exit(42);
	    }
	    setenv("LD_ASSUME_KERNEL", "2.4.1", 1);
	    execv(fullpath, NULL);
	    perror("execvp");
	    exit(42);
	case -1:
	    perror("fork");
	    exit(1);
    }
    /* We are parent. Trace it. */
    
    /* First wait for it to stop. */
    wpid = wait(&status);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 42)
	exit(1);
    if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGTRAP) {
	/* we're stuffed */
	ptrace(PTRACE_KILL, pid, 0, 0);
	fprintf(stderr, "Couldn't launch child for some reason.\n");
	exit(1);
    }

    /* We're good. Setup the debug registers to put a breakpoint at the
     * entry address */
    struct user *dummy = NULL; /* for addressing purposes */
    if (ptrace(PTRACE_POKEUSER, pid, &dummy->u_debugreg[0], entry_addr) == -1) {
	perror("ptrace(PTRACE_POKEUSER, DR0)");
	ptrace(PTRACE_KILL, pid, 0, 0);
	exit(1);
    }
    if (ptrace(PTRACE_POKEUSER, pid, &dummy->u_debugreg[7], 1) == -1) {
	perror("ptrace(PTRACE_POKEUSER, DR7)");
	ptrace(PTRACE_KILL, pid, 0, 0);
	exit(1);
    }
    if (ptrace(PTRACE_CONT, pid, 0, 0) == -1) {
	perror("ptrace(PTRACE_CONT)");
	ptrace(PTRACE_KILL, pid, 0, 0);
	exit(1);
    }
    /* now wait for the breakpoint to be hit */
    /* FIXME ... more error checking really needed here! */
    wpid = wait(&status);
    return pid;
}

void kill_my_process(pid_t pid) {
    ptrace(PTRACE_KILL, pid, 0, 0);
}

int main(int argc, char** argv) {
    pid_t target_pid;
    struct proc_image_t* proc_image;
    int c;
    int flags = GET_LIBRARIES_TOO;
    int fd;

    /* Parse options */
    while (1) {
	int option_index = 0;
	static struct option long_options[] = {
	    {0, 0, 0, 0},
	};

	c = getopt_long(argc, argv, "", long_options, &option_index);
	if (c == -1)
	    break;
	switch(c) {
	    case '?':
		/* invalid option */
		usage(argv[0]);
		break;
	}
    }

    if (argc - optind != 2) {
	usage(argv[0]);
	return 1;
    }

    target_pid = make_my_process(argv[optind+1]);

    if ((proc_image = get_proc_image(target_pid, flags)) == NULL)
	return 1;
    kill_my_process(target_pid);

    fd = open(argv[optind], O_CREAT|O_WRONLY|O_TRUNC, 0700);
    if (fd == -1) {
	fprintf(stderr, "Couldn't open %s for writing: %s\n", argv[optind],
	    strerror(errno));
	return 1;
    }

    write_proc_image_to_elf(proc_image, fd);
    close(fd);

    return 0;
}

/* vim:set ts=8 sw=4 noet: */
