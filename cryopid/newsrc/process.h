#ifndef __PROCESS_H__
#define __PROCESS_H__

#include <malloc.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/mman.h>
#include <signal.h>
#include <linux/user.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <asm/ldt.h>

#define IMAGE_VERSION 0x01

struct map_entry_t {
	long start, length;
	int prot;
	int flags;
	int dev;
	long pg_off;
	long inode;
	char filename[1024];
	void* data; /* length end-start */ /* in file, simply true if is data */
};

struct fd_entry_t {
	char filename[1024];
	int fd;
	int mode;
	off_t position;
	int flags;

	int data_length;
	char *data;
};

/* Flags for fd_entry_t.flags */
#define FD_IS_TERMINAL       1
#define FD_OFFSET_NOT_SAVED  2

struct proc_image_t {
	struct user user_data;
	struct user_i387_struct i387_data;
	int num_maps;
	struct map_entry_t *maps;
	int num_tls;
    struct user_desc **tls;
    char **tlsdata;

	char cwd[1024];

	/* something about fds */
	int num_fds;
	struct fd_entry_t *fds;

	char cmdline[1024]; /* FIXME arbitrary */
	int cmdline_length;
};

struct proc_image_t* get_proc_image(pid_t target_pid, int flags);
int write_proc_image(int fd, struct proc_image_t* p);

/* flags passed to get_proc_image */
#define GET_PROC_FULL_IMAGE        0x01
#define GET_PROC_IS_STOPPED        0x02
#define GET_OPEN_FILE_CONTENTS     0x04

#define RESUMER_START 0x00100000 /* Lowest location resume will be at */
#define RESUMER_END   0x00200000 /* Highest location resume will be at */

#endif /* __PROCESS_H__ */
