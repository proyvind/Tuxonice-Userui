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
#include "process.h"

extern void write_stub(int fd);
extern int write_proc_image_to_file(struct proc_image_t* p, int fd);

void usage(char* argv0) {
    fprintf(stderr,
"Usage: %s [options] <output filename> <pid>\n"
"\n"
"This is used to suspend the state of a single running process to a\n"
"self-executing file.\n"
"\n"
"    -f      Include libraries in the image of the file for a full image.\n"
"    -c      Save the contents of open files into the image. (Broken!)\n"
"\n"
"This program is part of CryoPID. http://cryopid.berlios.de/\n",
    argv0);
    exit(1);
}

int main(int argc, char** argv) {
    pid_t target_pid;
    struct proc_image_t* proc_image;
    int c;
    int flags = 0;
    int get_children = 0;
    int fd;

    /* Parse options */
    while (1) {
	int option_index = 0;
	static struct option long_options[] = {
	    {"full-image", 0, 0, 'f'},
	    {"file-contents", 0, 0, 'c'},
	    {"sub-processes", 0, 0, 's'},
	    {0, 0, 0, 0},
	};

	c = getopt_long(argc, argv, "fcs", long_options, &option_index);
	if (c == -1)
	    break;
	switch(c) {
	    case 'f':
		flags |= GET_PROC_FULL_IMAGE;
		break;
	    case 'c':
		flags |= GET_OPEN_FILE_CONTENTS;
		break;
	    case 's':
		get_children = 1;
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

    target_pid = atoi(argv[optind+1]);
    if (target_pid <= 1) {
	fprintf(stderr, "Invalid pid: %d\n", target_pid);
	return 1;
    }

    if ((proc_image = get_proc_image(target_pid, flags)) == NULL) {
	return 1;
    }
    fd = open(argv[optind], O_CREAT|O_WRONLY|O_TRUNC, 0700);
    if (fd == -1) {
	fprintf(stderr, "Couldn't open %s for writing: %s\n", argv[optind],
	    strerror(errno));
	return 1;
    }

    write_stub(fd);
    write_proc_image_to_file(proc_image, fd);

    return 0;
}

/* vim:set ts=8 sw=4 noet: */
