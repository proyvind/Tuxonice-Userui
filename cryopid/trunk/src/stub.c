//#define _FILE_OFFSET_BITS 64

#include "process.h"
#include <stdio.h>
#include <fcntl.h>
#include <stdarg.h>
#include <asm/termios.h>
#include <linux/unistd.h>
#include <asm/ldt.h>
#include <linux/elf.h>

int verbosity = 0;
int do_pause = 0;
int want_pid = 0;
int new_cs = 0;
int translate_pids = 0;

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
		exit(1);
	}
	return retval;
}

void safe_read(int fd, void* dest, size_t count, char* desc) {
	int ret;
	ret = read(fd, dest, count);
	if (ret == -1) {
		fprintf(stderr, "Read error on %s: %s\n", desc, strerror(errno));
		exit(1);
	}
	if (ret < count) {
		fprintf(stderr, "Short read on %s\n", desc);
		exit(1);
	}
}

void put_shell_code(struct user_regs_struct r, char* code) {
	char *cp = code;
	memset(code, 0, sizeof(code));
	code[0xffc] = 'A';
    /* set up a temporary stack for use */
	*cp++=0xbc;*(long*)(cp) = (long)code+0x0ff0; cp+=4; /* mov 0x11000, %esp */

    /* munmap resumer code except for us */
	*cp++=0xb8;*(long*)(cp) = __NR_munmap; cp+=4; /* mov foo, %eax  */
	*cp++=0xbb;*(long*)(cp) = RESUMER_START; cp+=4; /* mov foo, %ebx  */
	*cp++=0xb9;*(long*)(cp) = RESUMER_END-RESUMER_START; cp+=4; /* mov foo, %ecx  */
    *cp++=0xcd;*cp++=0x80; /* int $0x80 */

    /* set up gs */
	*cp++=0x66;*cp++=0xb8; *(short*)(cp) = r.gs; cp+=2; /* mov foo, %eax  */
	*cp++=0x8e;*cp++=0xe8; /* mov %eax, %gs */

    /* restore registers */
	*cp++=0xb8;*(long*)(cp) = r.eax; cp+=4; /* mov foo, %eax  */
	*cp++=0xbb;*(long*)(cp) = r.ebx; cp+=4; /* mov foo, %ebx  */
	*cp++=0xb9;*(long*)(cp) = r.ecx; cp+=4; /* mov foo, %ecx  */
	*cp++=0xba;*(long*)(cp) = r.edx; cp+=4; /* mov foo, %edx  */
	*cp++=0xbe;*(long*)(cp) = r.esi; cp+=4; /* mov foo, %esi  */
	*cp++=0xbf;*(long*)(cp) = r.edi; cp+=4; /* mov foo, %edi  */
	*cp++=0xbd;*(long*)(cp) = r.ebp; cp+=4; /* mov foo, %ebp  */
	*cp++=0xbc;*(long*)(cp) = r.esp; cp+=4; /* mov foo, %esp  */

    *cp++=0x9d; /* pop eflags */

    /* jump back to where we were. */
	*cp++=0xea;
	*(unsigned long*)(cp) = r.eip; cp+= 4;
    if (new_cs) r.cs = new_cs;
	*(unsigned short*)(cp) = r.cs; cp+= 2; /* jmp cs:foo */
}

#if !set_thread_area
_syscall1(int,set_thread_area,struct user_desc*,u_info);
#endif

int set_rt_sigaction(int sig, const struct k_sigaction* ksa) {
    int ret;
    asm (
            "int $0x80"
            : "=a"(ret)
            : "a"(__NR_rt_sigaction), "b"(sig),
              "c"(ksa), "d"(0), "S"(sizeof(ksa->sa_mask))
        );
    return ret;
}

int resume_image_from_file(int fd) {
	int num_maps, num_fds, num_tls;
	int fd2;
	int stdinfd;
	struct user_desc tls;
	struct map_entry_t map;
	struct fd_entry_t fd_entry;
	pid_t pid;
	struct user user_data;
	struct user_i387_struct i387_data;
    struct k_sigaction sa;
	char dir[1024];
	int cmdline_length;
	char cmdline[1024];
	long* ptr;
	int i, j;

	safe_read(fd, &pid, sizeof(pid), "pid");
    if (want_pid) {
        if (kill(pid, 0) == 0 || errno == EPERM) {
            fprintf(stderr, "Pid is already taken. Refusing to do anything :(\n");
            exit(1);
        }
        /* grab it! */
        if (verbosity > 0)
            fprintf(stderr, "PID is free ... going for it!\n");
        int happy = 0;
        switch(fork()) {
            case 0: break;
            case -1: perror("wait"); exit(1);
            default:
                     pause();
                     _exit(0);
        }
        while (!happy) {
            switch(fork()) {
                case 0:
                    setpgid(getpid(), getppid());
                    if (getpid() == pid) happy = 1;
                    break;
                case -1:
                    perror("fork");
                    exit(1);
                default:
                    sleep(1);
                    _exit(0);
            }
        }
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
				fprintf(stderr, "Loading 0x%08lx (len %d) from file %s\n",
						map.start, map.length, map.filename);
			syscall_check(fd2 = open(map.filename, O_RDONLY), 0,
					"open(%s)", map.filename);
			syscall_check((int)mmap((void*)map.start, map.length, map.prot,
					MAP_FIXED|map.flags, fd2, map.pg_off),
				0, "mmap(0x%lx, 0x%lx, %x, %x, %d, 0x%lx)",
				map.start, map.length, map.prot, map.flags, fd2, map.pg_off);
			syscall_check(close(fd2), 0, "close(%d)", fd2);
		} else {
			if (verbosity > 1)
				fprintf(stderr, "Creating 0x%08lx (len %d) from image\n",
						map.start, map.length);
			syscall_check( (int)
				mmap((void*)map.start, map.length, map.prot,
					MAP_ANONYMOUS|MAP_FIXED|map.flags, -1, 0),
					0, "mmap(0x%lx, 0x%lx, %x, %x, -1, 0)",
					map.start, map.length, map.prot, map.flags);
		}
		if (map.data != NULL) {
			if (verbosity > 0)
				fprintf(stderr, "Loading 0x%08lx (len %d) from image\n",
						map.start, map.length);
			safe_read(fd, (void*)map.start, map.length, "map data");
		}
	}

	safe_read(fd, &num_tls, sizeof(int), "num_tls");
	if (verbosity > 0)
		fprintf(stderr, "Reading %d TLS entries...\n", num_tls);
    if (do_pause) sleep(1);
	for(i = 0; i < num_tls; i++) {
        short savegs;
		safe_read(fd, &tls, sizeof(struct user_desc), "tls info");

        if (!tls.base_addr) continue;

		if (verbosity > 0)
			fprintf(stderr, "Restoring TLS entry %d (0x%lx limit 0x%lx)\n",
					tls.entry_number, tls.base_addr, tls.limit);

        syscall_check(set_thread_area(&tls), 0, "set_thread_area");
	}

	stdinfd = 0; /* we'll use stdin for stdout/stderr later if needed */
	if (verbosity == 0) {
		close(1);
		close(2);
	}
	safe_read(fd, &num_fds, sizeof(int), "num_fds");
	if (verbosity > 0)
		fprintf(stderr, "Reading %d file descriptors...\n", num_fds);
	while (num_fds--) {
		safe_read(fd, &fd_entry, sizeof(struct fd_entry_t), "an fd_entry");
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

	if (verbosity > 0)
		fprintf(stderr, "Restoring signal handlers...\n", dir);
    for (i = 1; i <= MAX_SIGS; i++) {
        safe_read(fd, &sa, sizeof(sa), "sigaction");
        if (i == SIGKILL || i == SIGSTOP) continue;
        sa.sa_hand = SIG_IGN;
        if (set_rt_sigaction(i, &sa) == -1) {
            fprintf(stderr, "Couldn't restore signal handler %d: %s\n",
                    i, strerror(i));
        }
    }

	close(fd);

	syscall_check(
			(int)mmap((void*)0x10000, 0x1000, PROT_READ|PROT_WRITE|PROT_EXEC,
			MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, 0, 0), 0, "mmap");
    /* put eflags onto the process' stack so we can pop it off */
    user_data.regs.esp-=4;
    *(long*)user_data.regs.esp = user_data.regs.eflags;
    
	put_shell_code(user_data.regs, (void*)0x10000);

	if (verbosity > 0)
		fprintf(stderr, "Ready to go!\n");

    if (translate_pids) {
        extern int supervise_me(pid_t);
        supervise_me(pid);
    }

	if (do_pause)
		sleep(2);
	asm("jmp 0x10000");

	return 1;
}

void* get_task_size() {
	/* stolen from isec brk exploit :) */
	unsigned tmp;
	return (void*)(((unsigned)&tmp + (1024*1024*1024)) / (1024*1024*1024) * (1024*1024*1024));
}

void seek_to_image(int fd) {
	Elf32_Ehdr e;
	Elf32_Shdr s;
	int i;
	char* strtab;

	syscall_check(lseek(fd, 0, SEEK_SET), 0, "lseek");
	safe_read(fd, &e, sizeof(e), "Elf32_Ehdr");
	if (e.e_shoff == 0) {
		fprintf(stderr, "No section header found in self! Bugger.\n");
		exit(1);
	}
	if (e.e_shentsize != sizeof(Elf32_Shdr)) {
		fprintf(stderr, "Section headers incorrect size. Bugger.\n");
		exit(1);
	}
	if (e.e_shstrndx == SHN_UNDEF) {
		fprintf(stderr, "String section missing. Bugger.\n");
		exit(1);
	}
	
	/* read the string table */
	syscall_check(lseek(fd, e.e_shoff+(e.e_shstrndx*e.e_shentsize), SEEK_SET), 0, "lseek");
	safe_read(fd, &s, sizeof(s), "string table section header");
	syscall_check(lseek(fd, s.sh_offset, SEEK_SET), 0, "lseek");
	strtab = malloc(s.sh_size);
	safe_read(fd, strtab, s.sh_size, "string table");

	for (i=0; i < e.e_shnum; i++) {
		long offset;

		syscall_check(
				lseek(fd, e.e_shoff+(i*e.e_shentsize), SEEK_SET), 0, "lseek");
		safe_read(fd, &s, sizeof(s), "Elf32_Shdr");
		if (s.sh_type != SHT_PROGBITS || s.sh_name == 0)
			continue;

		/* We have potential data! Is it really ours? */
		if (memcmp(strtab+s.sh_name, "cryopid.image", 13) != 0)
			continue;

		if (s.sh_info != IMAGE_VERSION) {
			fprintf(stderr, "Incorrect image version found (%d)! Keeping on trying.\n", s.sh_info);
			continue;
		}

		/* Woo! got it! */
		syscall_check(
				lseek(fd, s.sh_offset, SEEK_SET), 0, "lseek");

		safe_read(fd, &offset, 4, "offset");

		syscall_check(
				lseek(fd, offset, SEEK_SET), 0, "lseek");

		return;
	}
	fprintf(stderr, "Program image not found! Bugger.\n");
	exit(1);
}

int open_self() {
	int fd;
	if (verbosity > 0)
		fprintf(stderr, "Reading image...\n");
	fd = open("/proc/self/exe", O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "Couldn't open self: %s\n", strerror(errno));
		exit(1);
	}
	return fd;
}

void real_main(int argc, char** argv) {
	int fd;
	/* Parse options */
	while (1) {
		int option_index = 0;
		int c;
		static struct option long_options[] = {
			{0, 0, 0, 0},
		};
		
		c = getopt_long(argc, argv, "vpPc:t",
				long_options, &option_index);
		if (c == -1)
			break;
		switch(c) {
			case 'v':
				verbosity++;
				break;
			case 'p':
				do_pause = 1;
				break;
            case 'P':
                want_pid = 1;
                break;
            case 'c':
                new_cs = atoi(optarg);
                break;
            case 't':
                translate_pids = 1;
                break;
			case '?':
				/* invalid option */
				fprintf(stderr, "Unknown option on command line.\n");
				exit(1);
				break;
		}
	}

	if (argc - optind) {
		fprintf(stderr, "Extra arguments not expected!\n");
		fprintf(stderr, "Usage: %s [options]\n", argv[0]);
		exit(1);
	}

	fd = open_self();
	seek_to_image(fd);
	resume_image_from_file(fd);

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
