#include <malloc.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/mman.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <linux/user.h>
#include <linux/kdev_t.h>
#include <asm/ldt.h>
#include "process.h"

int write_proc_image_to_file(struct proc_image_t* p, int fd) {
	FILE* f;
	int i;

	if (!(f = fdopen(fd, "w"))) {
		perror("fopen()");
		return 0;
	}

	fwrite(&p->pid, sizeof(p->pid), 1, f);

	fwrite(&p->user_data, sizeof(p->user_data), 1, f);
	fwrite(&p->i387_data, sizeof(p->i387_data), 1, f);

	fwrite(&p->uses_tls, sizeof(p->uses_tls), 1, f);

	fwrite(&p->num_maps, sizeof(p->num_maps), 1, f);
	for (i = 0; i < p->num_maps; i++) {
		fwrite(&p->maps[i], sizeof(p->maps[i]), 1, f);
		if (p->maps[i].data)
			fwrite(p->maps[i].data, p->maps[i].length, 1, f);
	}

    fwrite(&p->num_tls, sizeof(p->num_tls), 1, f);
    for (i = 0; i < p->num_tls; i++) {
        fwrite(p->tls[i], sizeof(struct user_desc), 1, f);
    }

	fwrite(&p->num_fds, sizeof(p->num_fds), 1, f);
	for (i=0; i<p->num_fds; i++) {
		fwrite(&p->fds[i], sizeof(p->fds[i]), 1, f);
		if (p->fds[i].data > 0)
			fwrite(p->fds[i].data, p->fds[i].data_length, 1, f);
	}
	
	fwrite(&p->cmdline_length, sizeof(p->cmdline_length), 1, f);
	fwrite(p->cmdline, sizeof(p->cmdline), 1, f);

	fwrite(p->cwd, sizeof(p->cwd), 1, f);

	fwrite(p->sigs, sizeof(p->sigs), 1, f);

	fclose(f);
	return 1;
}

