/***
 * TODO:
 *  - Restore EFLAGS.
 *  - Restore FPU state.
 *  - Abuse the dynamic linker.
 *  - Incorporate a C bootstrapper.
 *  - mmap files in.
 */
#include <elf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "process.h"

#define BOOTSTRAP_LOC 0x10000

const char test_prog[] = {
	0xb8,0x01,0x00,0x00,0x00, /* mov ax, 1  */
	0xbb,0x2a,0x00,0x00,0x00, /* mov bx, 42 */
	0xcd,0x80,      /* int 0x80   */
};

char* make_shell_code(struct user_regs_struct r) {
    extern void restore_stuff(char c);
    char *code = (char*)malloc(0x1000);
    char *cp = code;
    memset(code, 0, sizeof(code));
    memcpy(code+0x500, restore_stuff, 0x500);
    code[0xffc] = 'A';
	*cp++ = 0xbc; *(long*)(cp) = 0x10ff0; cp+=4; /* mov sp, 0x11000 */
    //*cp++ = 0xe8; *(long*)(cp) = 0x001480a0-0x10000-(cp-code+4); cp+=4; /* call 0x10500 */
	*cp++ = 0x1e;                              /* push cs      */
	*cp++ = 0x0f; *cp++ = 0xa9;                /* pop gs       */
	*cp++ = 0xb8; *(long*)(cp) = r.eax; cp+=4; /* mov ax, foo  */
	*cp++ = 0xbb; *(long*)(cp) = r.ebx; cp+=4; /* mov bx, foo  */
	*cp++ = 0xb9; *(long*)(cp) = r.ecx; cp+=4; /* mov cx, foo  */
	*cp++ = 0xba; *(long*)(cp) = r.edx; cp+=4; /* mov dx, foo  */
	*cp++ = 0xbe; *(long*)(cp) = r.esi; cp+=4; /* mov si, foo  */
	*cp++ = 0xbf; *(long*)(cp) = r.edi; cp+=4; /* mov di, foo  */
	*cp++ = 0xbd; *(long*)(cp) = r.ebp; cp+=4; /* mov bp, foo  */
	*cp++ = 0xbc; *(long*)(cp) = r.esp; cp+=4; /* mov sp, foo  */
	*cp++ = 0xea;
    *(unsigned long*)(cp) = r.eip; cp+= 4;
    *(unsigned short*)(cp) = r.cs; cp+= 2; /* jmp cs:foo */
    return code;
};

Elf32_Ehdr* make_elf_header(struct proc_image_t* pi) {
	const unsigned char def_hdr[EI_NIDENT] =
		{0x7f, 'E', 'L', 'F', ELFCLASS32, ELFDATA2LSB, EV_CURRENT,
            0, 0, 0, 0, 0, 0, 0, 0, 0};
	Elf32_Ehdr *ehdr = (Elf32_Ehdr*)malloc(sizeof(Elf32_Ehdr));
	memcpy(ehdr->e_ident, def_hdr, EI_NIDENT);
	ehdr->e_type = ET_EXEC;
	ehdr->e_machine = EM_386;
	ehdr->e_version = EV_CURRENT;
	ehdr->e_entry = BOOTSTRAP_LOC;
	ehdr->e_phoff = sizeof(Elf32_Ehdr);
	ehdr->e_shoff = 0;
	ehdr->e_flags = 0;
	ehdr->e_ehsize = sizeof(Elf32_Ehdr);
	ehdr->e_phentsize = sizeof(Elf32_Phdr);
	ehdr->e_phnum = pi->num_maps+1; /* 1 for the bootstrapper, 1 for stub */
	ehdr->e_shentsize = 0;
	ehdr->e_shnum = 0;
	ehdr->e_shstrndx = 0;
	return ehdr;
}

Elf32_Phdr* make_phdrs(struct proc_image_t *pi) {
	Elf32_Phdr *phdrs = (Elf32_Phdr*)malloc(sizeof(Elf32_Phdr)*(pi->num_maps+2));
    Elf32_Phdr *phdr = phdrs;
    int i;
    int offset;
    offset = 0x1000;
    /* first one */
	phdr->p_type = PT_LOAD;
	phdr->p_offset = offset;
	phdr->p_vaddr = BOOTSTRAP_LOC;
	phdr->p_paddr = BOOTSTRAP_LOC;
	phdr->p_filesz = 0x1000;
	phdr->p_memsz = 0x1000;
	phdr->p_flags = PF_R|PF_W|PF_X;
	phdr->p_align = 0x1000;
    offset += 0x1000;
    /* second one (stub) */
    phdr++;
	phdr->p_type = PT_LOAD;
	phdr->p_offset = offset;
	phdr->p_vaddr = 0x00148000;
	phdr->p_paddr = 0x00148000;
	phdr->p_filesz = 0x2000;
	phdr->p_memsz = 0x2000;
	phdr->p_flags = PF_R|PF_W|PF_X;
	phdr->p_align = 0x1000;
    offset += 0x2000;
    /* and the rest */
    for(i=0; i < pi->num_maps; i++) {
        phdr++;
        phdr->p_type = PT_LOAD;
        phdr->p_offset = offset;
        phdr->p_vaddr = pi->maps[i].start;
        phdr->p_paddr = pi->maps[i].start;
        phdr->p_filesz = pi->maps[i].length;
        phdr->p_memsz = pi->maps[i].length;
        phdr->p_flags = 0;
        if (pi->maps[i].prot & PROT_READ) phdr->p_flags |= PF_R;
        if (pi->maps[i].prot & PROT_WRITE) phdr->p_flags |= PF_W;
        if (pi->maps[i].prot & PROT_EXEC) phdr->p_flags |= PF_X;
        phdr->p_align = 0x1000;
        /* warning: There is an assumption here that all memory maps will be
         * a multiple of the page size (0x1000 == 4KB) */
        offset += phdr->p_filesz;
    }
	return phdrs;
}

char* get_stub() {
    int fd;
    char* s = (char*)malloc(0x2000);
    Elf32_Ehdr ehdr;
    Elf32_Phdr phdr;
    memset(s, 0, sizeof(s));
    if ((fd = open("stub", O_RDONLY))==-1) {
        perror("open(stub)");
        exit(1);
    }
    read(fd, &ehdr, sizeof(ehdr));
    lseek(fd, ehdr.e_phoff, SEEK_SET);
    read(fd, &phdr, sizeof(phdr));
    lseek(fd, phdr.p_offset, SEEK_SET);
    if (phdr.p_filesz > 0x2000) fprintf(stderr, "STUB TOO BIG! FIXME!\n");
    read(fd, s, 0x2000);
    close(fd);
    return s;
}

char zeros[0x1000];

int main(int argc, char** argv) {
	pid_t target_pid;
	struct proc_image_t* proc_image;
	int c;
	int flags = 0;
	int elf_fd;
    int i;
    char *bootstrapper, *stub;
	
	/* Parse options */
	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{"full-image", 0, 0, 'f'},
			{"file-contents", 0, 0, 'c'},
			{"stopped", 0, 0, 's'},
			{0, 0, 0, 0},
		};
		
		c = getopt_long(argc, argv, "fso:",
				long_options, &option_index);
		if (c == -1)
			break;
		switch(c) {
			case 'f':
				flags |= GET_PROC_FULL_IMAGE;
				break;
			case 's':
				flags |= GET_PROC_IS_STOPPED;
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
		printf("Invalid pid: %d\n", target_pid);
		return 1;
	}

	memset(zeros, 0, sizeof(zeros));
	if ((elf_fd = open(argv[optind+1], O_CREAT|O_WRONLY|O_TRUNC, 0755))==-1) {
        perror("open");
        exit(1);
    }
    proc_image = get_proc_image(target_pid, flags);

    bootstrapper = make_shell_code(proc_image->user_data.regs);
    stub = get_stub();
    
	write(elf_fd, make_elf_header(proc_image), sizeof(Elf32_Ehdr));

	write(elf_fd, make_phdrs(proc_image), sizeof(Elf32_Phdr)*(proc_image->num_maps+2));
	write(elf_fd, zeros, sizeof(zeros)-sizeof(Elf32_Ehdr)-(sizeof(Elf32_Phdr)*(proc_image->num_maps+2)));

    write(elf_fd, bootstrapper, 0x1000);

    write(elf_fd, stub, 0x1000);

    for (i = 0; i < proc_image->num_maps; i++) {
        struct map_entry_t *map = &proc_image->maps[i];
        if (write(elf_fd, map->data, map->length) == -1) {
            fprintf(stderr, "Error writing map %d (from 0x%lx length %d is 0x%lx here): %s\n", i+1, map->start, map->length, map->data, strerror(errno));
        }
    }

	close(elf_fd);
	return 0;
}
