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
#include <asm/termios.h>
#include "process.h"

long scribble_zone = 0; /* somewhere to scribble on in child */

char* backup_page(pid_t target, void* addr) {
    long* page = malloc(PAGE_SIZE);
    int i;
    long ret;
    for(i = 0; i < PAGE_SIZE/sizeof(long); i++) {
	ret = ptrace(PTRACE_PEEKTEXT, target, (void*)((long)addr+(i*sizeof(long))), 0);
	if (errno) {
	    perror("ptrace(PTRACE_PEEKTEXT)");
	    free(page);
	    return NULL;
	}
	page[i] = ret;
	if (ptrace(PTRACE_POKETEXT, target, (void*)((long)addr+(i*sizeof(long))), 0xdeadbeef) == -1) {
	    perror("ptrace(PTRACE_POKETEXT)");
	    free(page);
	    return NULL;
	}
    }

    return (char*)page;
}

int restore_page(pid_t target, void* addr, char* page) {
    long *p = (long*)page;
    int i;
    assert(page);
    for (i = 0; i < PAGE_SIZE/sizeof(long); i++) {
	if (ptrace(PTRACE_POKETEXT, target, (void*)((long)addr+(i*sizeof(long))), p[i]) == -1) {
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
	if (ptrace(PTRACE_POKETEXT, pid, d+i, s[i]) == -1) {
	    perror("ptrace(PTRACE_POKETEXT)");
	    return 0;
	}
    }
    return 1;
}

int memcpy_from_target(pid_t pid, void* dest, const void* src, size_t n) {
    /* just like memcpy, but copies it from the space of the target pid */
    /* n must be a multiple of 4, or will otherwise be rounded down to be so */
    int i;
    long *d, *s;
    d = (long*) dest;
    s = (long*) src;
    n /= sizeof(long);
    for (i = 0; i < n; i++) {
	d[i] = ptrace(PTRACE_PEEKTEXT, pid, s+i, 0);
	if (errno) {
	    perror("ptrace(PTRACE_PEEKTEXT)");
	    return 0;
	}
    }
    return 1;
}

void print_status(FILE* f, int status) {
    if (WIFEXITED(status)) {
	fprintf(f, "WIFEXITED && WEXITSTATUS == %d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
	fprintf(f, "WIFSIGNALED && WTERMSIG == %d\n", WTERMSIG(status));
    } else if (WIFSTOPPED(status)) {
	fprintf(f, "WIFSTOPPED && WSTOPSIG == %d\n", WSTOPSIG(status));
    } else {
	fprintf(f, "Unknown status value: 0x%x\n", status);
    }
}

int do_syscall(pid_t pid, struct user_regs_struct *regs) {
    long loc;
    struct user_regs_struct orig_regs;
    long old_insn;
    int status, ret;

    if (ptrace(PTRACE_GETREGS, pid, NULL, &orig_regs) < 0) {
	perror("ptrace getregs");
	return 0;
    }

    loc = scribble_zone+0x10;

    old_insn = ptrace(PTRACE_PEEKTEXT, pid, loc, 0);
    if (errno) {
	perror("ptrace peektext");
	return 0;
    }
    //printf("original instruction at 0x%lx was 0x%lx\n", loc, old_insn);

    if (ptrace(PTRACE_POKETEXT, pid, loc, 0x80cd) < 0) {
	perror("ptrace poketext");
	return 0;
    }

    /* Set up registers for ptrace syscall */
    regs->eip = loc;
    if (ptrace(PTRACE_SETREGS, pid, NULL, regs) < 0) {
	perror("ptrace setregs");
	return 0;
    }

    /* Execute call */
    if (ptrace(PTRACE_SINGLESTEP, pid, NULL, NULL) < 0) {
	perror("ptrace singlestep");
	return 0;
    }
    ret = waitpid(pid, &status, 0);
    if (ret == -1) {
	perror("Failed to wait for child");
	exit(1);
    }

    /* Get our new registers */
    if (ptrace(PTRACE_GETREGS, pid, NULL, regs) < 0) {
	perror("ptrace getregs");
	return 0;
    }

    /* Return everything back to normal */
    if (ptrace(PTRACE_SETREGS, pid, NULL, &orig_regs) < 0) {
	perror("ptrace getregs");
	return 0;
    }

    if (ptrace(PTRACE_POKETEXT, pid, loc, old_insn) < 0) {
	perror("ptrace poketext");
	return 0;
    }

    return 1;
}

int process_is_stopped(pid_t pid) {
    char buf[128];
    char mode;
    FILE *f;
    snprintf(buf, 128, "/proc/%d/stat", pid);
    f = fopen(buf, "r");
    if (f == NULL) return -1;
    fscanf(f, "%*s %*s %c", &mode);
    fclose(f);
    return mode == 'T';
}

void start_ptrace(pid_t target_pid) {
    long ret;
    int status;
    int stopped;

    stopped = process_is_stopped(target_pid);

    ret = ptrace(PTRACE_ATTACH, target_pid, 0, 0);
    if (ret == -1) {
	perror("Failed to ptrace");
	exit(1);
    }

    if (stopped) return; /* don't bother waiting for it, we'll just hang */

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

    /* Decide if it's scribble worthy - find a nice anonymous mapping */
    if (scribble_zone == 0 &&
	    !*m->filename &&
	    (m->flags & MAP_PRIVATE) &&
	    !(m->flags & MAP_SHARED) &&
	    ((m->prot & (PROT_READ|PROT_WRITE)) == (PROT_READ|PROT_WRITE))) {
	scribble_zone = m->start;
	printf("[+] Found scribble zone: 0x%lx\n", scribble_zone);
    }

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
    fprintf(stderr, "[+] Retrieving FP registers... ");

    if (ptrace(PTRACE_GETFPREGS, target_pid, 0, i387_data) == -1) {
	perror("ptrace(PTRACE_PEEKDATA): ");
	return 0;
    }

    fprintf(stderr, "Done.\n");
    return 1;
}

off_t get_file_offset(pid_t pid, int fd, off_t offset, int whence) {
    struct user_regs_struct r;

    if (ptrace(PTRACE_GETREGS, pid, 0, &r) == -1) {
	perror("ptrace(GETREGS)");
	return 0;
    }

    r.eax = __NR_lseek;
    r.ebx = fd;
    r.ecx = offset;
    r.edx = whence;

    if (!do_syscall(pid, &r)) return 0;

    /* Error checking! */
    if (r.eax < 0) {
	errno = -r.eax;
	return (off_t)(-1);
    }

    return r.eax;
}

int get_file_contents(char *filename, struct fd_entry_t *out_buf) {
    int fd;
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

int is_in_syscall(pid_t pid, void* eip) {
    long inst;
    inst = ptrace(PTRACE_PEEKDATA, pid, eip-2, 0);
    if (errno) {
	perror("ptrace(PEEKDATA)");
	return 0;
    }
    return (inst&0xffff) == 0x80cd;
}

int get_signal_handler(pid_t pid, int sig, struct k_sigaction *ksa) {
    struct user_regs_struct r;

    if (ptrace(PTRACE_GETREGS, pid, 0, &r) == -1) {
	perror("ptrace(GETREGS)");
	return 0;
    }

    r.eax = __NR_rt_sigaction;
    r.ebx = sig;
    r.ecx = 0;
    r.edx = scribble_zone+0x100;
    r.esi = sizeof(ksa->sa_mask);

    if (!do_syscall(pid, &r)) return 0;

    /* Error checking! */
    if (r.eax < 0) {
	errno = -r.eax;
	perror("target rt_sigaction");
	return 0;
    }

    memcpy_from_target(pid, ksa, (void*)(scribble_zone+0x100), sizeof(struct k_sigaction));

    //printf("sigaction %d was 0x%lx mask 0x%x flags 0x%x restorer 0x%x\n", sig, ksa->sa_hand, ksa->sa_mask.sig[0], ksa->sa_flags, ksa->sa_restorer);

    return 1;
}

int get_termios(pid_t pid, int fd, struct termios *t) {
    struct user_regs_struct r;

    if (ptrace(PTRACE_GETREGS, pid, 0, &r) == -1) {
	perror("ptrace(GETREGS)");
	return 0;
    }

    r.eax = __NR_ioctl;
    r.ebx = fd;
    r.ecx = TCGETS;
    r.edx = scribble_zone+0x50;

    if (!do_syscall(pid, &r)) return 0;

    /* Error checking! */
    if (r.eax < 0) {
	errno = -r.eax;
	perror("target ioctl");
	return 0;
    }

    memcpy_from_target(pid, t, (void*)(scribble_zone+0x50), sizeof(struct termios));

    return 1;
}

int get_fcntl_data(pid_t pid, int fd, struct fcntl_data_t *f) {
    struct user_regs_struct r;

    if (ptrace(PTRACE_GETREGS, pid, 0, &r) == -1) {
	perror("ptrace(GETREGS)");
	return 0;
    }

    r.eax = __NR_fcntl;
    r.ebx = fd;
    r.ecx = F_GETFD;

    if (!do_syscall(pid, &r)) return 0;

    /* Error checking! */
    if (r.eax < 0) {
	errno = -r.eax;
	perror("target fcntl");
	return 0;
    }
    f->close_on_exec = r.eax;

    return 1;
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
    char* pagebackup;

    proc_image = malloc(sizeof(struct proc_image_t));

    proc_image->pid = target_pid;

    /* FIXME being liberal here: */
    proc_image->maps = malloc(sizeof(struct map_entry_t)*1000);

    start_ptrace(target_pid);
    snprintf(tmp_fn, 1024, "/proc/%d/maps", target_pid);
    f = fopen(tmp_fn, "r");
    while (fgets(map_line, 1024, f)) {
	if (!get_one_vma(target_pid, map_line, &(proc_image->maps[map_count]), flags & GET_PROC_FULL_IMAGE))
	    fprintf(stderr, "     Error parsing map: %s", map_line);
	else
	    if (proc_image->maps[map_count].start >= 0x10000 &&
		    proc_image->maps[map_count].start <= 0x11000)
		fprintf(stderr, "     Ignoring map - looks like resumer.\n");
	    else if (proc_image->maps[map_count].start >= RESUMER_START &&
		    proc_image->maps[map_count].start <= RESUMER_END)
		fprintf(stderr, "     Ignoring map - looks like resumer.\n");
	    else if (proc_image->maps[map_count].start > 0xC0000000)
		fprintf(stderr, "     Ignoring map - in kernel space.\n");
	    else
		map_count++;
    }
    fclose(f);
    proc_image->num_maps = map_count;
    fprintf(stderr, "[+] Read %d maps\n", map_count);

    if (!scribble_zone) {
	fprintf(stderr, "[-] No suitable scribble zone could be found. Aborting.\n");
	proc_image = NULL;
	goto out_ptrace;
    }
    pagebackup = backup_page(target_pid, (void*)scribble_zone);

    /* Get process's user data (includes gen regs) */
    if (!get_user_data(target_pid, &(proc_image->user_data))) {
	fprintf(stderr, "Error getting user data.\n");
	proc_image = NULL;
	goto out_ptrace;
    }
    if (is_in_syscall(target_pid, (void*)proc_image->user_data.regs.eip)) {
	fprintf(stderr, "[+] Process is probably in syscall. Noting this fact.\n");
	proc_image->user_data.regs.eip-=2;
	proc_image->user_data.regs.eax = proc_image->user_data.regs.orig_eax;
    }

    /* Get FP regs */
    if (!get_i387_data(target_pid, &(proc_image->i387_data))) {
	fprintf(stderr, "Error getting user data.\n");
	proc_image = NULL;
	goto out_ptrace;
    }

    /* Get TLS info */
    int z;
    fprintf(stderr, "[+] Reading TLS data\n");
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
    printf("[+] Process's working directory: %s\n", proc_image->cwd);

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
	int tty = -1;

	stat_ptr += 2;
	sscanf(stat_ptr, "%*c %*d %*d %*d %d", &tty);
	if (tty > 0) {
	    term_dev = (dev_t)tty;
	    printf("[+] Terminal device appears to be %d:%d\n", tty >> 8, tty & 0xFF);
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
	    if (get_termios(target_pid, proc_image->fds[fd_count].fd, &proc_image->fds[fd_count].termios)) {
		proc_image->fds[fd_count].flags |= FD_TERMIOS;
	    }
	}
	if (!get_fcntl_data(target_pid, proc_image->fds[fd_count].fd, &proc_image->fds[fd_count].fcntl_data)) {
	    printf("    Couldn't get fcntl data :(\n");
	} else {
	    printf("    fcntl: close-on-exec:%d\n", proc_image->fds[fd_count].fcntl_data.close_on_exec);
	}

	/* Locate the offset of the file */
	if (S_ISREG(stat_buf.st_mode)) {
	    file_offset = get_file_offset(target_pid, proc_image->fds[fd_count].fd, 0, SEEK_CUR);
	    if (file_offset == (off_t)(-1)) {
		perror("    vampire_lseek");
		printf("    file offset not saved\n");
		proc_image->fds[fd_count].flags |= FD_OFFSET_NOT_SAVED;
	    } else {
		printf("    file offset is %ld\n", file_offset);
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
    proc_image->cmdline = (char*)malloc(65536);
    sprintf(tmp_fn, "/proc/%d/cmdline", target_pid);
    f = fopen(tmp_fn, "r"); /* XXX error checking! */
    memset(proc_image->cmdline, 0, 65536);
    proc_image->cmdline_length = fread(proc_image->cmdline, sizeof(char), 65536, f);
    proc_image->cmdline[proc_image->cmdline_length++] = '\0';
    fclose(f);

    /* Save the process's environment */
    proc_image->environ = (char*)malloc(65536);
    sprintf(tmp_fn, "/proc/%d/environ", target_pid);
    f = fopen(tmp_fn, "r"); /* XXX error checking! */
    memset(proc_image->environ, 0, 65536);
    proc_image->environ_length = fread(proc_image->environ, sizeof(char), 65536, f);
    proc_image->environ[proc_image->environ_length++] = '\0';
    fclose(f);

    /* Get process's signal handlers */
    printf("[+] Getting signal handlers: ");
    fflush(stdout);
    for (z = 1; z <= MAX_SIGS; z++) {
	if (z == SIGKILL || z == SIGSTOP) continue;
	get_signal_handler(target_pid, z, &proc_image->sigs[z-1]);
	printf(".");
	fflush(stdout);
    }
    printf("\n");

    restore_page(target_pid, (void*)scribble_zone, pagebackup);
out_ptrace:
    end_ptrace(target_pid);
    return proc_image;
}

/* vim:set ts=8 sw=4 noet: */
