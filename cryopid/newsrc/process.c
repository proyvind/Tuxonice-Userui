/*
 * Process state saver
 *   (C) 2004 Bernard Blackham <bernard@blackham.com.au>
 *
 * Licensed under a BSD-ish license.
 */

/* large file support */
//#define _FILE_OFFSET_BITS 64

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
#include <asm/unistd.h>
#include <asm/ptrace.h>
#include <assert.h>
#include "process.h"

/* Somewhere in the child process to scribble on */
#define PROCESS_START 0x08048000 /* FIXME choose me dynamically */

char* backup_page(pid_t target, void* addr) {
	long* page = malloc(PAGE_SIZE);
	int i;
	long ret;
	for(i = 0; i < PAGE_SIZE/sizeof(long); i) {
		ret = ptrace(PTRACE_PEEKTEXT, target, (void*)((long)addr+(i*sizeof(long))), 0);
		if (errno) {
			perror("ptrace(PTRACE_PEEKTEXT)");
			free(page);
			return NULL;
		}
		page[i] = ret;
	}

	return (char*)page;
}

int restore_page(pid_t target, void* addr, long* page) {
	assert(page);
	int i;
	for (i = 0; i < PAGE_SIZE/sizeof(long); i++) {
		if (ptrace(PTRACE_POKETEXT, target, (void*)((long)addr+(i*sizeof(long))), page[i]) == -1) {
			perror("ptrace(PTRACE_POKETEXT)");
			free(page);
			return 0;
		}
	}
	free(page);
	return 1;
}

int memcpy_into_target(pid_t pid, void* dest, const void* src, size_t n) {
	/* just like memcpy, but copies it into the space of the target pid */
	/* n must be a multiple of 4, or will otherwise be rounded down to be so */
	int i;
	long *d, *s;
	d = (long*) dest;
	s = (long*) src;
	for (i = 0; i < n / sizeof(long); i++) {
		if (ptrace(PTRACE_POKETEXT, pid, d+(i*sizeof(long)), s[i]) == -1) {
			perror("ptrace(PTRACE_POKETEXT)");
			return 0;
		}
	}
	return 1;
}

int memcpy_from_target(pid_t pid, void* dest, const void* src, size_t n) {
	/* just like memcpy, but copies it into the space of the target pid */
	/* n must be a multiple of 4, or will otherwise be rounded down to be so */
	int i;
	long *d, *s;
	d = (long*) dest;
	s = (long*) src;
	for (i = 0; i < n / sizeof(long); i++) {
		d[i] = ptrace(PTRACE_PEEKTEXT, pid, s+(i*sizeof(long)), 0);
		if (errno) {
			perror("ptrace(PTRACE_PEEKTEXT)");
			return 0;
		}
	}
	return 1;
}

int do_syscall(pid_t pid, struct user_regs_struct *regs, void* loc) {
	/* Execute a given syscall using the given memory location (all 2 bytes)
	 * of it. We don't care about the page we're overwriting. It is the
	 * caller's responsibility to make sure it was backed up!
	 */
	unsigned int returned_data1, returned_data2;
	struct user_regs_struct orig_regs;
    long new_insn;
	off_t result;

	new_insn = 0x000080CD; /* "int 0x80" */

	if (ptrace(PTRACE_GETREGS, pid, NULL, &orig_regs) < 0) {
		perror("ptrace getregs");
		return 0;
	}

	if (ptrace(PTRACE_POKETEXT, pid, loc, new_insn) < 0) {
		perror("ptrace poketext");
		return 0;
	}

	/* Set up registers for ptrace syscall */
	if (ptrace(PTRACE_SETREGS, pid, NULL, &regs) < 0) {
		perror("ptrace setregs");
		return 0;
	}

	/* Execute call */
	if (ptrace(PTRACE_SYSCALL, pid, NULL, NULL) < 0) {
		perror("ptrace syscall");
		return 0;
	}
	if (ptrace(PTRACE_SYSCALL, pid, NULL, NULL) < 0) {
		perror("ptrace syscall");
		return 0;
	}

	/* Get our new registers */
	if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) < 0) {
		perror("ptrace getregs");
		return 0;
	}

	/* Return everything back to normal */
	if (ptrace(PTRACE_SETREGS, pid, NULL, &orig_regs) < 0) {
		perror("ptrace getregs");
		return 0;
	}

	return 1;
}

void start_ptrace(pid_t target_pid, int is_stopped) {
	long ret;
	int status;

	ret = ptrace(PTRACE_ATTACH, target_pid, 0, 0);
	if (ret == -1) {
		perror("Failed to ptrace");
		exit(1);
	}

	if (is_stopped) return; /* don't bother waiting for it, we'll just hang */

	ret = waitpid(target_pid, &status, 0);
	if (ret == -1) {
		perror("Failed to wait for child");
		exit(1);
	}
	if (!WIFSTOPPED(status)) {
		fprintf(stderr, "Failed to get child stopped.\n");
	}
}

void end_ptrace(pid_t target_pid) {
	long ret;

	ret = ptrace(PTRACE_DETACH, target_pid, 0, 0);
	if (ret == -1) {
		perror("Failed to detach");
		exit(1);
	}
}

int get_one_vma(pid_t target_pid, char* map_line, struct map_entry_t* m, int get_library_data) {
	/* Precondition: target_pid must be ptrace_attached */
	char *ptr1, *ptr2;
	int dminor, dmajor;

	assert(m);
	memset(m, 0, sizeof(struct map_entry_t));

	/* Parse a line that looks like one of the following: 
	    08048000-080ab000 r-xp 00000000 03:03 1309106    /home/b/dev/sp/test
	    080ab000-080ae000 rw-p 00062000 03:03 1309106    /home/b/dev/sp/test
	    080ae000-080db000 rwxp 00000000 00:00 0 
	    40000000-40203000 rw-p 00000000 00:00 0 
	    bfffe000-c0000000 rwxp 00000000 00:00 0 
	*/

	ptr1 = map_line;
	if ((ptr2 = strchr(ptr1, '-')) == NULL) {
		fprintf(stderr, "No - in map line!\n");
		return 0;
	}
	*ptr2 = '\0';
	m->start = strtoul(ptr1, NULL, 16);

	ptr1 = ptr2+1;
	if ((ptr2 = strchr(ptr1, ' ')) == NULL) {
		fprintf(stderr, "No end of end in map line!\n");
		return 0;
	}
	*ptr2 = '\0';
	m->length = strtoul(ptr1, NULL, 16) - m->start;

	m->prot = 0;
	ptr1 = ptr2+1;

	if (ptr1[0] == 'r')
		m->prot |= PROT_READ;
	else if (ptr1[0] != '-')
		fprintf(stderr, "Bad read flag: %c\n", ptr1[0]);

	if (ptr1[1] == 'w')
		m->prot |= PROT_WRITE;
	else if (ptr1[1] != '-')
		fprintf(stderr, "Bad write flag: %c\n", ptr1[1]);

	if (ptr1[2] == 'x')
		m->prot |= PROT_EXEC;
	else if (ptr1[2] != '-')
		fprintf(stderr, "Bad exec flag: %c\n", ptr1[2]);

	m->flags = MAP_FIXED;
	if (ptr1[3] == 's')
		m->flags |= MAP_SHARED;
	else if (ptr1[3] != 'p')
		fprintf(stderr, "Bad shared flag: %c\n", ptr1[3]);
	else
		m->flags |= MAP_PRIVATE;

	ptr1 = ptr1+5; /* to pgoff */
	if ((ptr2 = strchr(ptr1, ' ')) == NULL) {
		fprintf(stderr, "No end of pgoff in map line!\n");
		return 0;
	}
	*ptr2 = '\0';
	m->pg_off = strtoul(ptr1, NULL, 16);

	if ((signed long)m->pg_off < 0) {
		m->flags |= MAP_GROWSDOWN;
	}

	ptr1 = ptr2+1;
	if ((ptr2 = strchr(ptr1, ':')) == NULL) {
		fprintf(stderr, "No end of major dev in map line!\n");
		return 0;
	}
	*ptr2 = '\0';
	dmajor = strtoul(ptr1, NULL, 16);

	ptr1 = ptr2+1;
	if ((ptr2 = strchr(ptr1, ' ')) == NULL) {
		fprintf(stderr, "No end of minor dev in map line!\n");
		return 0;
	}
	*ptr2 = '\0';
	dminor = strtoul(ptr1, NULL, 16);
	
	m->dev = MKDEV(dmajor, dminor);

	ptr1 = ptr2+1;
	if ((ptr2 = strchr(ptr1, ' ')) != NULL) {
		*ptr2 = '\0';
		m->inode = strtoul(ptr1, NULL, 10);

		ptr1 = ptr2+1;
		while (*ptr1 == ' ') ptr1++;
		if (*ptr1 != '\n') { /* we have a filename too to grab */
			ptr2 = strchr(ptr1, '\n');
			if (ptr2) *ptr2 = '\0';
			strcpy(m->filename,ptr1);
		} else {
			m->flags |= MAP_ANONYMOUS;
		}
	} else {
		m->inode = strtoul(ptr1, NULL, 10);
	}

	/* we have all the info we need, regurgitate it for confirmation */
	fprintf(stderr, "Map: %08lx-%08lx %c%c%c%c %08lx %02x:%02x %-10ld %s\n",
			m->start, m->start + m->length,
			(m->prot & PROT_READ)?'r':'-',
			(m->prot & PROT_WRITE)?'w':'-',
			(m->prot & PROT_EXEC)?'x':'-',
			(m->flags & MAP_SHARED)?'s':'p',
			m->pg_off,
			MAJOR(m->dev), MINOR(m->dev),
			m->inode,
			m->filename);

	if (get_library_data) {
		/* forget the fact it came from a file. Pretend it was just
		 * some arbitrary anonymous writeable VMA.
		 */
		memset(m->filename, 0, sizeof(m->filename));
		m->inode = 0;
		m->prot |= PROT_WRITE;

		m->flags &= ~MAP_SHARED;
		m->flags |= MAP_PRIVATE;
		m->flags |= MAP_ANONYMOUS;
	}

	/* Now to get data too */
	if (((m->prot & PROT_WRITE) && (m->flags & MAP_PRIVATE))
			|| (m->prot & MAP_ANONYMOUS)) {
		/* We have a memory segment. We should retrieve its data */
		long *pos, *end;
		long *datapos;
		/*fprintf(stderr, "Retrieving %ld bytes from segment 0x%lx... ",
				m->length, m->start); */
		m->data = malloc(m->length);
		datapos = m->data;
		end = (long*)(m->start + m->length);

		for(pos = (long*)m->start; pos < end; pos++, datapos++) {
			*datapos = 
				ptrace(PTRACE_PEEKDATA, target_pid, pos, datapos);
			if (errno != 0) perror("ptrace(PTRACE_PEEKDATA)");
		}

		/*fprintf(stderr, "done.\n"); */
	} else {
		m->data = NULL;
	}

	return 1;
}

int get_user_data(pid_t target_pid, struct user *user_data) {
	long pos;
	int* user_data_ptr = (int*)user_data;

	/* We have a memory segment. We should retrieve its data */
	for(pos = 0; pos < sizeof(struct user)/sizeof(int); pos++) {
		user_data_ptr[pos] =
			ptrace(PTRACE_PEEKUSER, target_pid, (void*)(pos*4), NULL);
		if (errno != 0) {
			perror("ptrace(PTRACE_PEEKDATA): ");
		}
	}

	return 1;
}

int get_i387_data(pid_t target_pid, struct user_i387_struct* i387_data) {
	/* We have a memory segment. We should retrieve its data */
	fprintf(stderr, "Retrieving FP registers... ");

	if (ptrace(PTRACE_GETFPREGS, target_pid, 0, i387_data) == -1) {
		perror("ptrace(PTRACE_PEEKDATA): ");
		return 0;
	}

	fprintf(stderr, "Done.\n");
	return 1;
}

off_t get_file_offset(pid_t tpid, int fd, off_t offset, int whence)
{
	/* XXX FIXME TODO TODO FIXME XXX XXX FIXME TODO TODO FIXME XXX */
	unsigned int orig_insn, new_insn;
	unsigned int saved_data1, saved_data2;
	unsigned int returned_data1, returned_data2;
	struct user_regs_struct orig_regs, new_regs;
	off_t result;

	new_insn = 0x000080CD; /* "int 0x80" */

	if ((orig_insn = ptrace(PTRACE_PEEKTEXT, tpid, 0x08048000, &orig_insn)) < 0)
		perror("ptrace peektext");
	if (ptrace(PTRACE_POKETEXT, tpid, 0x08048000, new_insn) < 0)
		perror("ptrace poketext");
	saved_data1 = ptrace(PTRACE_PEEKDATA, tpid, 0x08048008, &saved_data1);
	saved_data2 = ptrace(PTRACE_PEEKDATA, tpid, 0x0804800c, &saved_data2);

	if (ptrace(PTRACE_GETREGS, tpid, NULL, &orig_regs) < 0)
		perror("ptrace getregs");

	/* Set up registers for ptrace syscall */
	memcpy(&new_regs, &orig_regs, sizeof(new_regs));
	new_regs.eax = 140;    /* llseek a.k.a. lseek64 */
	new_regs.orig_eax = 140;
	new_regs.ebx = fd;
	new_regs.ecx = (unsigned long)(offset >> 32);
	new_regs.edx = (unsigned long)(offset & 0xFFFFFFFF);
	new_regs.esi = 0x08048008;  /* random location in data segment */
	new_regs.edi = whence;
	new_regs.eip = 0x08048000;

	/* Execute call */
	if (ptrace(PTRACE_SETREGS, tpid, NULL, &new_regs) < 0)
		perror("ptrace setregs");
	if (ptrace(PTRACE_SYSCALL, tpid, NULL, NULL) < 0)
		perror("ptrace syscall");
	if (ptrace(PTRACE_SYSCALL, tpid, NULL, NULL) < 0)
		perror("ptrace syscall");

	/* Get data back */
	returned_data1 = ptrace(PTRACE_PEEKDATA, tpid, 0x08048008, &returned_data1);
	returned_data2 = ptrace(PTRACE_PEEKDATA, tpid, 0x0804800c, &returned_data2);
	ptrace(PTRACE_GETREGS, tpid, NULL, &new_regs);

	/* Error checking! */
	if (new_regs.eax != 0) {
		errno = -new_regs.eax;
		return (off_t)(-1);
	}

	/* Return everything back to normal */
	ptrace(PTRACE_POKETEXT, tpid, 0x08048000, orig_insn);
	ptrace(PTRACE_POKEDATA, tpid, 0x08048008, saved_data1);
	ptrace(PTRACE_POKEDATA, tpid, 0x0804800c, saved_data2);
	ptrace(PTRACE_SETREGS, tpid, NULL, &orig_regs);

	/* Put the returned offset back together */
	result = ((off_t)(returned_data1) | ((off_t)(returned_data2) << 32));
	return result;
}

int get_file_contents(char *filename, struct fd_entry_t *out_buf)
{
	int fd;
	FILE *f;
	int length, nread;
	struct stat stat_buf;
	char *buf;

	if (stat(filename, &stat_buf) < 0)
		return -1;
	if (!S_ISREG(stat_buf.st_mode))
		return -1;
	if ((fd = open(filename, O_RDONLY)) < 0)
		return -1;

	length = stat_buf.st_size;
	buf = malloc(length);
	if (buf == NULL)
		return -1;

	nread = 0;
	for (;;) {
		if (read(fd, buf+nread, length-nread) < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			free(buf);
			close(fd);
			return -1;
		}
	}
	close(fd);
	
	out_buf->data_length = length;
	out_buf->data = buf;

	return length;
}

struct user_desc *get_tls_info(pid_t pid, int entry_num) {
    struct user_desc *u = malloc(sizeof(struct user_desc));
    memset(u, 0, sizeof(struct user_desc));
    u->entry_number = entry_num;
    if (ptrace(PTRACE_GET_THREAD_AREA, pid, entry_num, u) == -1) {
        free(u);
        return NULL;
    }
    return u;
}

/* FIXME: split this into several functions */
struct proc_image_t* get_proc_image(pid_t target_pid, int flags) {
	FILE *f;
	char tmp_fn[1024], fd_filename[1024];
	char map_line[1024];
	char stat_line[1024];
	char *stat_ptr;
	struct proc_image_t *proc_image;
	struct stat stat_buf;
	dev_t term_dev;
	int map_count = 0;
	int fd_count = 0;
	off_t file_offset;
	DIR *proc_fd;
	struct dirent *fd_dirent;

	proc_image = malloc(sizeof(struct proc_image_t));

	/* FIXME being liberal here: */
	proc_image->maps = malloc(sizeof(struct map_entry_t)*1000);

	//if (kill(target_pid, SIGSTOP) == -1) perror("kill");
	start_ptrace(target_pid, flags & GET_PROC_IS_STOPPED);
	snprintf(tmp_fn, 1024, "/proc/%d/maps", target_pid);
	f = fopen(tmp_fn, "r");
	while (fgets(map_line, 1024, f)) {
		if (!get_one_vma(target_pid, map_line, &(proc_image->maps[map_count]), flags & GET_PROC_FULL_IMAGE))
			fprintf(stderr, "Error parsing map: %s", map_line);
		else
			if (proc_image->maps[map_count].start >= 0x10000 &&
					proc_image->maps[map_count].start <= 0x11000)
				fprintf(stderr, "Ignoring map - looks like resumer.\n");
			else if (proc_image->maps[map_count].start >= RESUMER_START &&
					proc_image->maps[map_count].start <= RESUMER_END)
				fprintf(stderr, "Ignoring map - looks like resumer.\n");
			else if (proc_image->maps[map_count].start > 0xC0000000)
				fprintf(stderr, "Ignoring map - in kernel space.\n");
            else
				map_count++;
	}
	fclose(f);
	proc_image->num_maps = map_count;
	fprintf(stderr, "Read %d maps\n", map_count);

	/* Get process's user data (includes gen regs) */
	if (!get_user_data(target_pid, &(proc_image->user_data))) {
		fprintf(stderr, "Error getting user data.\n");
		return NULL;
	}

	/* Get FP regs */
	if (!get_i387_data(target_pid, &(proc_image->i387_data))) {
		fprintf(stderr, "Error getting user data.\n");
		return NULL;
	}

    /* Get TLS info */
    int z;
    proc_image->num_tls = 0;
    proc_image->tls = malloc(sizeof(struct user_desc*)*256);
    for (z = 0; z < 256; z++) {
        proc_image->tls[proc_image->num_tls] = get_tls_info(target_pid, z);
        if (proc_image->tls[proc_image->num_tls]) proc_image->num_tls++;
    }

	/* Find the current directory of our victim */
	snprintf(tmp_fn, 1024, "/proc/%d/cwd", target_pid);
	memset(proc_image->cwd, 0, sizeof(proc_image->cwd));
	readlink(tmp_fn, proc_image->cwd, sizeof(proc_image->cwd)-1);
	printf("Process's working directory: %s\n", proc_image->cwd);

	/* Get the process's terminal device */
	/* This involves parsing /proc/stat :( */
	term_dev = 0;
	snprintf(tmp_fn, 1024, "/proc/%d/stat", target_pid);
	memset(stat_line, 0, sizeof(stat_line));
	f = fopen(tmp_fn, "r");
	fgets(stat_line, 1024, f);
	fclose(f);
	stat_ptr = strrchr(stat_line, ')');
	if (stat_ptr != NULL) {
		int tty = -1, tmp;

		stat_ptr += 2;
		sscanf(stat_ptr, "%c %d %d %d %d", &tmp, &tmp, &tmp, &tmp, &tty);
		if (tty > 0) {
			term_dev = (dev_t)tty;
			printf("Terminal device appears to be %d:%d\n", tty >> 8, tty & 0xFF);
		}
	}

	/* Get a list of open FDs */
	/* FIXME: hard-coded limits are nasty but easy */
	proc_image->fds = malloc(sizeof(struct fd_entry_t)*1000);
	snprintf(tmp_fn, 1024, "/proc/%d/fd", target_pid);
	proc_fd = opendir(tmp_fn);
	for (;;) {
		fd_dirent = readdir(proc_fd);
		if (fd_dirent == NULL)
			break;
		if (fd_dirent->d_type != DT_LNK)
			continue;

		proc_image->fds[fd_count].flags = 0;

		/* find the file's mode from the permission bits */
		snprintf(tmp_fn, 1024, "/proc/%d/fd/%s", target_pid, fd_dirent->d_name);
		lstat(tmp_fn, &stat_buf);
		if ((stat_buf.st_mode & S_IRUSR) && (stat_buf.st_mode & S_IWUSR))
			proc_image->fds[fd_count].mode = O_RDWR;
		else if (stat_buf.st_mode & S_IWUSR)
			proc_image->fds[fd_count].mode = O_WRONLY;
		else
			proc_image->fds[fd_count].mode = O_RDONLY;

		/* work out what file this FD points to */
		memset(fd_filename, 0, sizeof(fd_filename));
		readlink(tmp_fn, fd_filename, sizeof(fd_filename)-1);

		proc_image->fds[fd_count].fd = atoi(fd_dirent->d_name);
		strcpy(proc_image->fds[fd_count].filename, fd_filename);

		printf("fd %d: points to %s, mode %d\n", proc_image->fds[fd_count].fd, proc_image->fds[fd_count].filename, proc_image->fds[fd_count].mode);

		/* stat the file this time, not the link */
		if (stat(tmp_fn, &stat_buf) < 0) {
			perror("    stat");
			printf("    bleh, can't actually read this file :(\n");
			continue;
		}
		if (S_ISCHR(stat_buf.st_mode) && (stat_buf.st_rdev == term_dev)) {
			printf("    this looks like our terminal...\n");
			proc_image->fds[fd_count].flags |= FD_IS_TERMINAL;
		}

		/* Locate the offset of the file */
		if (S_ISREG(stat_buf.st_mode)) {
			file_offset = get_file_offset(target_pid, proc_image->fds[fd_count].fd, 0, SEEK_CUR);
			if (file_offset == (off_t)(-1)) {
				perror("    vampire_lseek");
				printf("    file offset not saved\n");
				proc_image->fds[fd_count].flags |= FD_OFFSET_NOT_SAVED;
			} else {
				printf("    file offset is %lld\n", file_offset);
				proc_image->fds[fd_count].position = file_offset;
			}
		} else {
			printf("    not a regular file - offset not saved\n");
			proc_image->fds[fd_count].flags |= FD_OFFSET_NOT_SAVED;
		}

		/* Initialise data; we might end up filling it in properly
		   later with file contents though.
		*/
		proc_image->fds[fd_count].data_length = -1;
		proc_image->fds[fd_count].data = NULL;

		/* Maybe slurp up the contents of the file too.
		   FIXME do we /really/ want to keep it all in memory?
		   Oh well, this'll do for now.
		 */
		if (flags & GET_OPEN_FILE_CONTENTS) {
			int l;
			l = get_file_contents(tmp_fn, &proc_image->fds[fd_count]);
			printf("    read %d bytes from %s\n", l, tmp_fn);
		}

		fd_count++;
	}
	closedir(proc_fd);
	proc_image->num_fds = fd_count;
	printf("process has %d FD's open\n", fd_count);
	
	/* Save the process's command line */
	sprintf(tmp_fn, "/proc/%d/cmdline", target_pid);
	f = fopen(tmp_fn, "r");
	memset(proc_image->cmdline, 0, sizeof(proc_image->cmdline));
	proc_image->cmdline_length = fread(proc_image->cmdline, sizeof(char), 1024, f);
	fclose(f);

	end_ptrace(target_pid);
	return proc_image;
}

