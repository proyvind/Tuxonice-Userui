//#define _FILE_OFFSET_BITS 64

#include "process.h"
#include <stdio.h>
#include <fcntl.h>
#include <stdarg.h>
#include <linux/unistd.h>
#include <asm/ldt.h>
#include <asm/ucontext.h>
#include <linux/elf.h>

int verbosity = 0;
int tls_hack = 0;
long tls_start = 0;
void (*old_segvhandler)(int, siginfo_t*, void*) = NULL;
void *argc_ptr;

int real_argc;
char** real_argv;
char** real_environ;
extern char** environ;

int syscall_check(int retval, int can_be_fake, char* desc, ...) {
    va_list va_args;
    /* can_be_fake is true if the syscall might return -1 anyway, and
     * we should simply check errno.
     */
    if (can_be_fake && errno == 0) return retval;
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

    /* munmap resumer code except for us - except when we're needed for our
     * segvhandler */
    if (!tls_hack) {
	*cp++=0xb8;*(long*)(cp) = __NR_munmap; cp+=4; /* mov foo, %eax  */
	*cp++=0xbb;*(long*)(cp) = RESUMER_START; cp+=4; /* mov foo, %ebx  */
	*cp++=0xb9;*(long*)(cp) = RESUMER_END-RESUMER_START; cp+=4; /* mov foo, %ecx  */
	*cp++=0xcd;*cp++=0x80; /* int $0x80 */
    }

    /* set up gs */
    if (!tls_hack && r.gs != 0) {
	*cp++=0x66;*cp++=0xb8; *(short*)(cp) = r.gs; cp+=2; /* mov foo, %eax  */
	*cp++=0x8e;*cp++=0xe8; /* mov %eax, %gs */
    }

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
    asm("mov %%cs,%w0": "=q"(r.cs)); /* ensure we use the right CS for the current kernel */
    *(unsigned short*)(cp) = r.cs; cp+= 2; /* jmp cs:foo */
}

#if !set_thread_area
_syscall1(int,set_thread_area,struct user_desc*,u_info);
#endif

int set_rt_sigaction(int sig, const struct k_sigaction* ksa, const struct k_sigaction* oksa) {
    int ret;
    asm (
	    "int $0x80"
	    : "=a"(ret)
	    : "a"(__NR_rt_sigaction), "b"(sig),
	      "c"(ksa), "d"(oksa), "S"(sizeof(ksa->sa_mask))
	);
    return ret;
}

int voodoo_stage = 0;
char* voodoo_ptr = NULL;
char voodoo_backup[12];
void segv_handler(int sig, siginfo_t *si, void *ucontext) {
    struct ucontext *uc = (struct ucontext*)ucontext;
    unsigned char *pt = (unsigned char*)uc->uc_mcontext.eip;
    if (voodoo_stage == 1) {
	pt = voodoo_ptr;
	pt[0] = 0xa3;
	*(long*)(pt+1) = 0x00000000;
	pt[5] = 0x90;
	pt[6] = 0xeb;
	pt[7] = 0xf8;
	voodoo_stage++;
	return;
    } else if (voodoo_stage == 2) {
	*(long*)pt = 0x90909090;
	pt[4] = 0xa3;
	*(long*)(pt+5) = 0x00000000;
	voodoo_stage++;
	return;
    } else if (voodoo_stage == 3) {
	memcpy(voodoo_ptr, voodoo_backup, sizeof(voodoo_backup));
	voodoo_stage = 0;
	return;
    }
    if (!memcmp(pt, "\x65\x83\x3d", 3)) {
	/*
	 *  8048344:   65 83 3d 0c 00 00 00    cmpl   $0x0,%gs:0xc
	 *  804834b:   00 
	 *  804834c:   83 3d af be ad de 00    cmpl   $0x0,0xdeadbeaf
	 */
	pt[0] = 0x83;
	pt[1] = 0x3d;
	*(long*)(pt+2) = tls_start+*(char*)(pt+3);
	pt[6] = pt[7];
	pt[7] = 0x90;
	return;
    }
    if (!memcmp(pt, "\x65\x8b", 2) && (
	    pt[2] == 0x0d || /* ecx */
	    pt[2] == 0x35 || /* esi */
	    pt[2] == 0x15 || /* edx */
	    pt[2] == 0x2d || /* ebp */
	    pt[2] == 0x3d || /* edi */
	    0)) {
	/*
	 *  8048353:   65 8b 0d 00 00 00 00    mov    %gs:0x0,%ecx
	 *  804835a:   8b 0d af be ad de       mov    0xdeadbeaf,%ecx
	 */
	pt[0] = 0x8b;
	pt[1] = pt[2];
	*(long*)(pt+2) = tls_start+*(long*)(pt+3);
	pt[6] = 0x90;
	return;
    }
    if (!memcmp(pt, "\x65\x89\x3d", 3)) { /* XXX untested */
	/*
	 * 80483ab:   65 89 3d 50 00 00 00    mov    %edi,%gs:0x50
	 * 80483b2:   89 3d ef be ad de       mov    %edi,0xdeadbeef
	 */
	pt[0] = 0x89;
	pt[1] = 0x3d;
	*(long*)(pt+2) = tls_start+*(long*)(pt+3);
	pt[6] = 0x90;
	return;
    }
    if (!memcmp(pt, "\x65\xc7\x05", 3)) { /* XXX untested */
	/*
	 *  8048344:   65 c7 05 f0 01 00 00    movl   $0xffffffff,%gs:0x1f0
	 *  804834b:   ff ff ff ff
	 *  804834f:   c7 05 ef be ad de ff    movl   $0xfffffff,0xdeadbeef
	 *  8048356:   ff ff 0f
	 */
	pt[0] = pt[1];
	pt[1] = pt[2];
	*(long*)(pt+2) = tls_start+*(long*)(pt+3);
	*(long*)(pt+6) = *(long*)(pt+7);
	pt[10] = 0x90;
	return;
    }
    if (!memcmp(pt, "\xf0\x65\x0f\xb1\x0d", 5)) { /* XXX untested */
	/*
	 * 8048359:   f0 65 0f b1 0d 54 00    lock cmpxchg %ecx,%gs:0x54
	 * 8048360:   00 00
	 * 8048362:   f0 0f b1 0d ef be ad    lock cmpxchg %ecx,0xdeadbeef
	 * 8048369:   de
	 */
	pt[1] = 0x0f;
	pt[2] = pt[3];
	pt[3] = pt[4];
	*(long*)(pt+4) = tls_start+*(long*)(pt+5);
	pt[8] = 0x90;
	return;
    }
    if (!memcmp(pt, "\xf0\x65\x83\x0d", 4)) { /* XXX untested */
	/*
	 * 804836a:   f0 65 83 0d 54 00 00    lock orl $0x10,%gs:0x54
	 * 8048371:   00 10
	 * 8048373:   f0 83 0d ef be ad de    lock orl $0x10,0xdeadbeef
	 * 804837a:   10
	 */
	pt[1] = pt[2];
	pt[2] = pt[3];
	*(long*)(pt+3) = tls_start+*(long*)(pt+4);
	pt[7] = pt[8];
	pt[8] = 0x90;
	return;
    }
    if (pt[0] == 0x65 && (
		pt[1] == 0xa1 || /* mov    %gs:0x0,%eax  */
		pt[1] == 0xa3    /* mov    %eax,%gs:0x48 */
		)) {
	/*
	 * 804838c:   65 a1 00 00 00 00       mov    %gs:0x0,%eax
	 * 8048392:   a1 ef be ad de          mov    0xdeadbeef,%eax
	 *
	 * 80483c8:   65 a3 48 00 00 00       mov    %eax,%gs:0x48
	 * 80483ce:   a3 ef be ad de          mov    %eax,0xdeadbeef
	 */
	pt[0] = pt[1];
	*(long*)(pt+1) = tls_start+*(long*)(pt+2);
	pt[5] = 0x90;
	return;
    }
    if (!memcmp(pt, "\x65\x89\x51", 3)) {
	/*
	 * 80483b1:   65 89 51 00             mov    %edx,%gs:0x0(%ecx)
	 * 80483b5:   89 91 af be ad de       mov    %edx,0xdeadbeaf(%ecx)
	 *
	 * WARNING: XXX VOODOO HAPPENS HERE
	 */
	memcpy(voodoo_backup, pt, sizeof(voodoo_backup));
	pt[0] = 0x89;
	pt[1] = 0x91;
	*(long*)(pt+2) = tls_start+*(char*)(pt+3);
	pt[6] = 0xa3;
	*(long*)(pt+7) = 0x00000000; /* cause another segfault */
	voodoo_stage = 1;
	voodoo_ptr = pt;
	return;
    }
    if (old_segvhandler &&
	    old_segvhandler != (void*)SIG_IGN && old_segvhandler != (void*)SIG_DFL)
	old_segvhandler(sig, si, ucontext);
    printf("Unhandled segfault at 0x%08lx!\n", uc->uc_mcontext.eip);
    _exit(1);
}

int resume_image_from_file(int fd) {
    int num_maps, num_fds, num_tls;
    int fd2;
    struct user_desc tls;
    struct map_entry_t map;
    struct fd_entry_t fd_entry;
    pid_t pid;
    struct user user_data;
    struct user_i387_struct i387_data;
    struct k_sigaction sa;
    char dir[1024];
    int cmdline_length, environ_length;
    char *new_cmdline, *new_environ;
    int i;
    int extra_prot = 0;

    safe_read(fd, &pid, sizeof(pid), "pid");

    new_cmdline = (char*)malloc(65536);
    new_environ = (char*)malloc(65536);
    safe_read(fd, &cmdline_length, sizeof(cmdline_length), "cmdline_length");
    safe_read(fd, new_cmdline, cmdline_length, "cmdline");
    new_cmdline[cmdline_length]='\0';

    safe_read(fd, &environ_length, sizeof(environ_length), "environ_length");
    safe_read(fd, new_environ, environ_length, "environ");
    new_environ[environ_length]='\0';
    
    safe_read(fd, &user_data, sizeof(struct user), "user data");
    safe_read(fd, &i387_data, sizeof(struct user_i387_struct), "i387 data");
    safe_read(fd, &num_maps, sizeof(int), "num_maps");
    safe_read(fd, &num_tls, sizeof(int), "num_tls");

    if (num_tls > 0) {
	/* detect a TLS-capable system */
	if (set_thread_area(NULL) == -1) { /* you'd hope it did!*/
	    if (errno == ENOSYS) {
		tls_hack = 1;
		extra_prot = PROT_WRITE;
		if (verbosity > 0)
		    printf("TLS hack enabled!\n");
	    }
	}
    }

    if (verbosity > 0)
	fprintf(stderr, "Reading %d maps...\n", num_maps);

    while(num_maps--) {
	safe_read(fd, &map, sizeof(struct map_entry_t), "a map");

	if (map.filename && map.filename[0]) {
	    if (verbosity > 0)
		fprintf(stderr, "Loading 0x%08lx (len %ld) from file %s\n",
			map.start, map.length, map.filename);
	    syscall_check(fd2 = open(map.filename, O_RDONLY), 0,
		    "open(%s)", map.filename);
	    syscall_check((int)mmap((void*)map.start, map.length, map.prot|extra_prot,
		    MAP_FIXED|map.flags, fd2, map.pg_off),
		0, "mmap(0x%lx, 0x%lx, %x, %x, %ld, 0x%lx)",
		map.start, map.length, map.prot|extra_prot, map.flags, fd2, map.pg_off);
	    syscall_check(close(fd2), 0, "close(%d)", fd2);
	} else {
	    if (verbosity > 1)
		fprintf(stderr, "Creating 0x%08lx (len %ld) from image\n",
			map.start, map.length);
	    syscall_check( (int)
		mmap((void*)map.start, map.length, map.prot|extra_prot,
		    MAP_ANONYMOUS|MAP_FIXED|map.flags, -1, 0),
		    0, "mmap(0x%lx, 0x%lx, %x, %x, -1, 0)",
		    map.start, map.length, map.prot|extra_prot, map.flags);
	}
	if (map.data != NULL) {
	    if (verbosity > 0)
		fprintf(stderr, "Loading 0x%08lx (len %ld) from image\n",
			map.start, map.length);
	    safe_read(fd, (void*)map.start, map.length, "map data");
	}
    }

    if (verbosity > 0)
	fprintf(stderr, "Reading %d TLS entries...\n", num_tls);
    for(i = 0; i < num_tls; i++) {
	safe_read(fd, &tls, sizeof(struct user_desc), "tls info");

	if (!tls.base_addr) continue;

	if (verbosity > 0)
	    fprintf(stderr, "Restoring TLS entry %d (0x%lx limit 0x%x)\n",
		    tls.entry_number, tls.base_addr, tls.limit);

	if (tls_hack)
	    tls_start = tls.base_addr;
	else
	    syscall_check(set_thread_area(&tls), 0, "set_thread_area");
    }

    safe_read(fd, &num_fds, sizeof(int), "num_fds");
    if (verbosity > 0)
	fprintf(stderr, "Reading %d file descriptors...\n", num_fds);
    
    /* discard misc data */
    lseek(fd,
	    num_fds*sizeof(struct fd_entry_t) +
	    sizeof(dir),
	    SEEK_CUR);
    if (verbosity > 0)
	fprintf(stderr, "Restoring signal handlers...\n");
    for (i = 1; i <= MAX_SIGS; i++) {
	struct k_sigaction oksa;
	safe_read(fd, &sa, sizeof(sa), "sigaction");
	if (i == SIGKILL || i == SIGSTOP) continue;
	if (i == SIGSEGV && tls_hack) {
	    sa.sa_hand = (__sighandler_t)segv_handler;
	    sa.sa_flags |= SA_SIGINFO;
	    sa.sa_flags &= ~SA_ONESHOT;
	}
	if (set_rt_sigaction(i, &sa, (i==SIGSEGV)?&oksa:NULL) == -1) {
	    fprintf(stderr, "Couldn't restore signal handler %d: %s\n",
		    i, strerror(i));
	}
	if (i == SIGSEGV) {
	    old_segvhandler = (void*)oksa.sa_hand;
	}
    }

    close(fd);

    syscall_check(
	    (int)mmap((void*)0x10000, 0x1000, PROT_READ|PROT_WRITE|PROT_EXEC,
	    MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, 0, 0), 0, "mmap");
    /* put eflags onto the process' stack so we can pop it off */
    user_data.regs.esp = argc_ptr;
    printf("ESP is 0x%p\n", argc_ptr);
    user_data.regs.esp-=4;
    *(long*)user_data.regs.esp = user_data.regs.eflags;
    
    put_shell_code(user_data.regs, (void*)0x10000);

    asm volatile("jmp 0x10000");

    return 1;
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

int main(int argc, char** argv) {
    argc_ptr = &argc;

    printf("argc is at 0x%p and argv is at 0x%p\n", &argc, argv);
    printf("argc is %d\n", argc);
    int i;
    for (i = 0; i < argc; i++) {
        printf("argv[%d] is at 0x%p and = \"%s\"\n", i, argv[i], argv[i]);
    }

    int fd;
    fd = open_self();
    seek_to_image(fd);
    resume_image_from_file(fd);

    fprintf(stderr, "Something went wrong :(\n");
    return 1;
}

/* vim:set ts=8 sw=4 noet: */
