#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <fcntl.h>
#include <stdarg.h>
#include <asm/termios.h>
#include "process.h"

int verbosity = 0;

int syscall_check(int retval, int can_be_fake, char* desc, ...) {
	va_list va_args;
	/* can_be_fake is true if the syscall might return -1 anyway, and
	 * we should simply check errno.
	 */
	if (can_be_fake && errno == 0) return;
	if (retval == -1) {
		char str[1024];
		snprintf(str, 1024, "Error in %s: %s\n", desc, strerror(errno));
		va_start(va_args, desc);
		vfprintf(stderr, str, va_args);
		va_end(va_args);
		//exit(1);
	}
	return retval;
}

void safe_read(int fd, void* dest, size_t count, char* desc) {
	int ret;
	ret = read(fd, dest, count);
	if (ret == -1) {
		fprintf(stderr, "Read error on %s: %s\n", desc, strerror(errno));
		//exit(1);
	}
	if (ret < count) {
		fprintf(stderr, "Short read on %s\n", desc);
		exit(1);
	}
}

int resume_image_from_file(char* fn) {
	int num_maps, num_fds;
	int fd, fd2;
	int stdinfd;
	struct map_entry_t map;
	struct fd_entry_t fd_entry;
	pid_t pid;
	struct user user_data;
	struct user_i387_struct i387_data;
	sigset_t zeromask;
	char dir[1024];
	int cmdline_length;
	char cmdline[1024];
	long* ptr;
	int i;

	sigemptyset(&zeromask);

	if (verbosity > 0)
		fprintf(stderr, "Reading image from %s...\n", fn);
	fd = open(fn, O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "Couldn't open file: %s\n", strerror(errno));
		exit(1);
	}

	safe_read(fd, &user_data, sizeof(struct user), "user data");
	safe_read(fd, &i387_data, sizeof(struct user_i387_struct), "i387 data");
	safe_read(fd, &num_maps, sizeof(int), "num_maps");

	if (verbosity > 0)
		fprintf(stderr, "Reading %d maps...\n", num_maps);
	while(num_maps--) {
		safe_read(fd, &map, sizeof(struct map_entry_t), "a map");

		if (map.filename && map.filename[0]) {
			if (verbosity > 0)
				fprintf(stderr, "Loading 0x%lx (len %d) from file %s\n",
						map.start, map.length, map.filename);
			syscall_check(fd2 = open(map.filename, O_RDONLY), 0,
					"open(%s)", map.filename);
			syscall_check((int)mmap((void*)map.start, map.length, map.prot,
					MAP_FIXED|map.flags, fd2, map.pg_off),
				0, "mmap(0x%lx, 0x%lx, %x, %x, %d, 0x%lx)",
				map.start, map.length, map.prot, map.flags, fd2, map.pg_off);
			syscall_check(close(fd2), 0, "close(%d)", fd2);
		} else {
			if (verbosity > 0)
				fprintf(stderr, "Loading 0x%lx (len %d) from image\n",
						map.start, map.length);
			syscall_check( (int)
				mmap((void*)map.start, map.length, map.prot,
					MAP_ANONYMOUS|MAP_FIXED|map.flags, -1, 0),
					0, "mmap(0x%lx, 0x%lx, %x, %x, -1, 0)",
					map.start, map.length, map.prot, map.flags);
		}
		if (map.data != NULL) {
			if (verbosity > 0)
				fprintf(stderr, "Loading %d bytes of data for map 0x%lx\n",
						map.length, map.start);
			safe_read(fd, (void*)map.start, map.length, "map data");
		}
	}

	stdinfd = 0; /* we'll use stdin for stdout/stderr later if needed */
	close(1);
	close(2);
	safe_read(fd, &num_fds, sizeof(int), "num_fds");
	if (verbosity > 0)
		fprintf(stderr, "Reading %d file descriptors...\n", num_fds);
	while (num_fds--) {
		safe_read(fd, &fd_entry, sizeof(struct fd_entry_t), "an fd_entry");
		if (fd_entry.fd == 255) continue;
		if (verbosity > 0) {
			fprintf(stderr, "fd: name=%s  fd=%d  mode=%d  flags=%d\n", fd_entry.filename, fd_entry.fd, fd_entry.mode, fd_entry.flags);
		}

		/* shuffle fd's about if they're going to clash with anything 
		 * important (stdin, or our image file)
		 */
		if (fd_entry.fd == fd) fd = dup(fd);
		if (fd_entry.fd == stdinfd) stdinfd = dup(stdinfd);

		if (fd_entry.flags & FD_IS_TERMINAL) {
			if (verbosity > 1)
				fprintf(stderr, "    this looks like our terminal, duplicating stdin\n");
			fd2 = syscall_check(dup2(stdinfd, fd_entry.fd), 0, "dup2");
		} else {
			fd2 = open(fd_entry.filename, fd_entry.mode);
			if (fd2 < 0) {
				fprintf(stderr, "Warning: couldn't restore file %s: %s\n", fd_entry.filename, strerror(errno));
				continue;
			}
			if (!(fd_entry.flags & FD_OFFSET_NOT_SAVED)) {
				if (verbosity > 1)
					fprintf(stderr, "    seeking to %lld\n", fd_entry.position);
				if (lseek(fd2, fd_entry.position, SEEK_SET) < 0) {
					fprintf(stderr, "Warning: restoring file offset %lld to file %s failed: %s\n", fd_entry.position, fd_entry.filename, strerror(errno));
				}
			}
			syscall_check(dup2(fd2, fd_entry.fd), 0, "dup2");
			syscall_check(close(fd2), 0, "close");
			fd2 = fd_entry.fd;
		}
		if (fd_entry.data_length >= 0) {
			fprintf(stderr, "Warning: restoring file contents not yet implemented (for %s)\n", fd_entry.filename);
			while (fd_entry.data_length) {
				int r = 4096;
				char buf[4096];
				if (r > fd_entry.data_length)
					r = fd_entry.data_length;
				safe_read(fd, buf, r, "junk");
				fd_entry.data_length -= r;
			}
		}
	}
	close(stdinfd);

	safe_read(fd, &cmdline_length, sizeof(cmdline_length), "cmdline_length");
	safe_read(fd, cmdline, sizeof(cmdline), "cmdline");

	safe_read(fd, dir, sizeof(dir), "working directory");
	if (verbosity > 0)
		fprintf(stderr, "Changing directory to %s\n", dir);
	syscall_check(chdir(dir), 0, "chdir %s", dir);

	close(fd);

	if (verbosity > 0)
		fprintf(stderr, "Ready to go!\n");

	/* now fork a child to ptrace us */
	switch (pid = fork()) {
		case -1:
			fprintf(stderr, "fork() failed: %s\n", strerror(errno));
			exit(1);
		default: /* am parent */
            write(0, "Execing\n", 8);
            fflush(stdout);
            {char *args[] = {"dummy", NULL};
                extern char **environ;
            //execve("/home/b/dev/swsusp/svn/cryopid/src/dummy", args, environ);
            //printf("D'oh! %s\n", strerror(errno));
			syscall_check(waitpid(pid, 0, 0), 0, "wait(child)");
			pause();
			fprintf(stderr, "Argh! Fell through. Dammit.\n");
			exit(1);}
			/* should never be hit */
		case 0: /* am child */
			pid = getppid();
            sleep(1);
			
			/* completely detach ourselves from the parent so we dont
			 * end up a zombie - nearly a UNIX-style double fork() */
			if (syscall_check(fork(), 0, "fork()") > 0) exit(0);
			syscall_check(setsid(), 0, "setsid()");
			if (syscall_check(fork(), 0, "fork()") > 0) exit(0);

			syscall_check(
					ptrace(PTRACE_ATTACH, pid, 0, 0), 1,
					"ptrace(PTRACE_ATTACH)");
			syscall_check(waitpid(pid, &i, 0), 0, "wait(child)");
			/* parent should be stopped so we do not need to wait() */
			ptr = (long*)&user_data;
			i = sizeof(user_data);
			for(i=0; i < sizeof(user_data); i+=4, ptr++) {
				ptrace(PTRACE_POKEUSER, pid, i, *ptr);
			}
            /* forcefully set %gs, because ptrace won't let us in 2.6 :( */
 			syscall_check(
 					ptrace(PTRACE_SETREGS, pid, 0, &user_data.regs), 0,
 					"ptrace(PTRACE_SETREGS)");
             long eip = user_data.regs.eip&0xfffffffc;
             long tmp1 = 
                 syscall_check(
                         ptrace(PTRACE_PEEKTEXT, pid, eip, 0), 0,
                         "ptrace(PTRACE_PEEKTEXT)");
             long tmp2 = 
                 syscall_check(
                         ptrace(PTRACE_PEEKTEXT, pid, eip+4, 0), 0,
                         "ptrace(PTRACE_PEEKTEXT)");
             syscall_check(
                     ptrace(PTRACE_POKETEXT, pid, eip, (0x73 << 8)|0x000000b8), 0,
                     "ptrace(PTRACE_POKETEXT)");
             syscall_check(
                     ptrace(PTRACE_POKETEXT, pid, eip+4, 0x00e88e00), 0,
                     "ptrace(PTRACE_POKETEXT)");
             syscall_check(
                     ptrace(PTRACE_POKEUSER, pid, 4*12, eip), 0, 
                     "ptrace(PTRACE_POKEUSER)"); /* EIP == 12 */
             int status;
             syscall_check(
                     ptrace(PTRACE_SINGLESTEP, pid, 0, 0), 0,
                     "ptrace(PTRACE_SINGLESTEP)");
             syscall_check(waitpid(pid, &status, 0), 0, "wait(child)");
             if (WIFSTOPPED(status)) printf("Signalled: %d\n", WSTOPSIG(status));
             syscall_check(
                     ptrace(PTRACE_SINGLESTEP, pid, 0, 0), 0,
                     "ptrace(PTRACE_SINGLESTEP)");
             syscall_check(waitpid(pid, &status, 0), 0, "wait(child)");
             if (WIFSTOPPED(status)) printf("Signalled: %d\n", WSTOPSIG(status));
             syscall_check(
                     ptrace(PTRACE_POKETEXT, pid, eip, tmp1), 0,
                     "ptrace(PTRACE_POKETEXT)");
             syscall_check(
                     ptrace(PTRACE_POKETEXT, pid, eip+4, tmp2), 0,
                     "ptrace(PTRACE_POKETEXT)");
             syscall_check(
                     ptrace(PTRACE_POKEUSER, pid, 4*12, user_data.regs.eip), 0, 
                     "ptrace(PTRACE_POKEUSER)"); /* EIP == 12 */
			/* let it loose! */
			syscall_check(
					ptrace(PTRACE_DETACH, pid, 0, 0), 0,
					"ptrace(PTRACE_DETACH, %d, 0, 0)", pid, 0, 0);
			_exit(0);
	}
	return 1;
}

void* get_task_size() {
	/* stolen from isec brk exploit :) */
	unsigned tmp;
	return (void*)(((unsigned)&tmp + (1024*1024*1024)) / (1024*1024*1024) * (1024*1024*1024));
}

void real_main(int argc, char** argv) {
	pid_t target_pid;
	/* Parse options */
	while (1) {
		int option_index = 0;
		int c;
		static struct option long_options[] = {
			{0, 0, 0, 0},
		};
		
		c = getopt_long(argc, argv, "v",
				long_options, &option_index);
		if (c == -1)
			break;
		switch(c) {
			case 'v':
				verbosity++;
				break;
			case '?':
				/* invalid option */
				exit(1);
				break;
		}
	}

	if (argc - optind != 1) {
		printf("Usage: %s [options] <filename>\n", argv[0]);
		exit(1);
	}

	resume_image_from_file(argv[optind]);

	fprintf(stderr, "Something went wrong :(\n");
	exit(1);
}

int real_argc;
char** real_argv;
char** new_environ;
extern char** environ;

int main(int argc, char**argv) {
	long amount_used;
	void *stack_ptr;
	void *top_of_old_stack, *bottom_of_old_stack;
	void *top_of_new_stack;
	long size_of_new_stack;
	
	int i;

	/* Take a copy of our argc/argv and environment below we blow them away */
	real_argc = argc;
	real_argv = malloc(sizeof(char*)*argc);
	for(i=0; i < argc; i++)
		real_argv[i] = strdup(argv[i]);
	real_argv[i] = NULL;

	new_environ = environ;
	for(i = 0; environ[i]; i++); /* count environment variables */
	new_environ = malloc(sizeof(char*)*i);
	for(i = 0; environ[i]; i++)
		*new_environ++ = strdup(environ[i]);
	*new_environ = NULL;
	environ = new_environ;

	/* Reposition the stack at top_of_old_stack */
	top_of_old_stack = get_task_size();
	stack_ptr = &stack_ptr;

	amount_used = top_of_old_stack - stack_ptr;

	top_of_new_stack = (void*)0x00200000;
	size_of_new_stack = PAGE_SIZE;

	syscall_check( (int)
		mmap(top_of_new_stack - size_of_new_stack, size_of_new_stack,
			PROT_READ|PROT_WRITE|PROT_EXEC,
			MAP_ANONYMOUS|MAP_FIXED|MAP_GROWSDOWN|MAP_PRIVATE, -1, 0),
		0, "mmap(newstack)");
	memset(top_of_new_stack - size_of_new_stack, 0, size_of_new_stack);
	memcpy(top_of_new_stack - size_of_new_stack,
			top_of_old_stack - size_of_new_stack, /* FIX ME */
			size_of_new_stack);
	bottom_of_old_stack = (void*)(((unsigned long)sbrk(0) + PAGE_SIZE - 1) & PAGE_MASK);
	__asm__ ("addl %0, %%esp" : : "a"(top_of_new_stack - top_of_old_stack));

	/* unmap absolutely everything above us! */
	syscall_check(
			munmap(top_of_new_stack,
				(top_of_old_stack - top_of_new_stack)),
				0, "munmap(stack)");
	
	/* Now hope for the best! */
	real_main(real_argc, real_argv);
	/* should never return */
	return 42;
}

