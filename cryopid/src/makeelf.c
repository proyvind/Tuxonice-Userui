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
    char *code = (char*)malloc(0x1000);
    memset(code, 0, sizeof(code));
	code[0]  = 0xb8; *(long*)(code+1)  = r.eax; /* mov ax, foo  */
	code[5]  = 0xbb; *(long*)(code+6)  = r.ebx; /* mov bx, foo  */
	code[10] = 0xb9; *(long*)(code+11) = r.ecx; /* mov cx, foo  */
	code[15] = 0xba; *(long*)(code+16) = r.edx; /* mov dx, foo  */
	code[20] = 0xbe; *(long*)(code+21) = r.esi; /* mov si, foo  */
	code[25] = 0xbf; *(long*)(code+26) = r.edi; /* mov di, foo  */
	code[30] = 0xbd; *(long*)(code+31) = r.ebp; /* mov bp, foo  */
	code[35] = 0xbc; *(long*)(code+36) = r.esp; /* mov sp, foo  */
	code[40] = 0xea;
    *(unsigned long*)(code+41) = r.eip;
    *(unsigned short*)(code+45) = r.cs; /* jmp cs:foo */
    return code;
};

Elf32_Ehdr* make_elf_header(struct proc_image_t* pi) {
	const unsigned char def_hdr[EI_NIDENT] =
		{0x7f, 'E', 'L', 'F', 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
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
	ehdr->e_phnum = pi->num_maps+1; /* 1 for the bootstrapper */
	ehdr->e_shentsize = 0;
	ehdr->e_shnum = 0;
	ehdr->e_shstrndx = 0;
	return ehdr;
}

Elf32_Phdr* make_phdrs(struct proc_image_t *pi) {
	Elf32_Phdr *phdrs = (Elf32_Phdr*)malloc(sizeof(Elf32_Phdr)*(pi->num_maps+1));
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
	phdr->p_flags = PF_R|PF_X;
	phdr->p_align = 0x1000;
    offset += 0x1000;
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

char zeros[0x1000];

int main(int argc, char** argv) {
	pid_t target_pid;
    char* output_file = NULL;
	struct proc_image_t* proc_image;
	int c;
	int flags = 0;
	int elf_fd;
    int i;
    char* bootstrapper;
	
	/* Parse options */
	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{"full-image", 0, 0, 'f'},
			{"file-contents", 0, 0, 'c'},
			{"stopped", 0, 0, 's'},
            {"output-file", 1, 0, 'o'},
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
            case 'o':
                output_file = optarg;
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

    if (output_file == NULL) {
        output_file = (char*)malloc(100);
        snprintf(output_file, 100, "%d.pid", target_pid);
    }

	memset(zeros, 0, sizeof(zeros));
	if ((elf_fd = open(output_file, O_CREAT|O_WRONLY|O_TRUNC, 0755))==-1) {
        perror("open");
        exit(1);
    }
    proc_image = get_proc_image(target_pid, flags);

    bootstrapper = make_shell_code(proc_image->user_data.regs);
    
	write(elf_fd, make_elf_header(proc_image), sizeof(Elf32_Ehdr));

	write(elf_fd, make_phdrs(proc_image), sizeof(Elf32_Phdr)*(proc_image->num_maps+1));
	write(elf_fd, zeros, sizeof(zeros)-sizeof(Elf32_Ehdr)-(sizeof(Elf32_Phdr)*(proc_image->num_maps+1)));

    write(elf_fd, bootstrapper, 0x1000);

    for (i = 0; i < proc_image->num_maps; i++) {
        struct map_entry_t *map = &proc_image->maps[i];
        if (write(elf_fd, map->data, map->length) == -1) {
            fprintf(stderr, "Error writing map %d (from 0x%lx length %d is 0x%lx here): %s\n", i+1, map->start, map->length, map->data, strerror(errno));
        }
    }

	close(elf_fd);
	return 0;
}
