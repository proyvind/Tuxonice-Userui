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

int main(int argc, char** argv) {
	pid_t target_pid;
	struct proc_image_t* proc_image;
	int c;
	int flags = 0;
    int fd;
	
	/* Parse options */
	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{"full-image", 0, 0, 'f'},
			{"file-contents", 0, 0, 'c'},
			{0, 0, 0, 0},
		};
		
		c = getopt_long(argc, argv, "fc",
				long_options, &option_index);
		if (c == -1)
			break;
		switch(c) {
			case 'f':
				flags |= GET_PROC_FULL_IMAGE;
				break;
			case 'c':
				flags |= GET_OPEN_FILE_CONTENTS;
				break;
			case '?':
				/* invalid option */
				exit(1);
				break;
		}
	}

	if (argc - optind != 2) {
		printf("Usage: %s [options] <pid> <filename>\n", argv[0]);
		return 1;
	}

	target_pid = atoi(argv[optind]);
	if (target_pid <= 1) {
		fprintf(stderr, "Invalid pid: %d\n", target_pid);
		return 1;
	}

	proc_image = get_proc_image(target_pid, flags);
    fd = open(argv[optind+1], O_CREAT|O_WRONLY|O_TRUNC, 0700);
    if (fd == -1) {
        fprintf(stderr, "Couldn't open %s for writing: %s\n", argv[optind+1],
                strerror(errno));
        return 1;
    }

    write_stub(fd);
	write_proc_image_to_file(proc_image, fd);

	return 0;
}
