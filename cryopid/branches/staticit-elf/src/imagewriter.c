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
#include <elf.h>
#include <assert.h>
#include "process.h"

int write_proc_image_to_file(struct proc_image_t* p, int fd) {
    FILE* f;
    int i;

    if (!(f = fdopen(fd, "w"))) {
	perror("fopen()");
	return 0;
    }

    fwrite(&p->pid, sizeof(p->pid), 1, f);

    fwrite(&p->cmdline_length, sizeof(p->cmdline_length), 1, f);
    fwrite(p->cmdline, p->cmdline_length, 1, f);

    fwrite(&p->environ_length, sizeof(p->environ_length), 1, f);
    fwrite(p->environ, p->environ_length, 1, f);

    fwrite(&p->user_data, sizeof(p->user_data), 1, f);
    fwrite(&p->i387_data, sizeof(p->i387_data), 1, f);

    fwrite(&p->num_maps, sizeof(p->num_maps), 1, f);
    fwrite(&p->num_tls, sizeof(p->num_tls), 1, f);

    for (i = 0; i < p->num_maps; i++) {
	fwrite(&p->maps[i], sizeof(p->maps[i]), 1, f);
	if (p->maps[i].data)
	    fwrite(p->maps[i].data, p->maps[i].length, 1, f);
    }

    for (i = 0; i < p->num_tls; i++) {
	fwrite(p->tls[i], sizeof(struct user_desc), 1, f);
    }

    fwrite(&p->num_fds, sizeof(p->num_fds), 1, f);
    for (i=0; i<p->num_fds; i++) {
	fwrite(&p->fds[i], sizeof(p->fds[i]), 1, f);
	if (p->fds[i].data > 0)
	    fwrite(p->fds[i].data, p->fds[i].data_length, 1, f);
    }

    fwrite(p->cwd, sizeof(p->cwd), 1, f);

    fwrite(p->sigs, sizeof(p->sigs), 1, f);

    fclose(f);
    return 1;
}

int write_proc_image_to_elf(struct proc_image_t* p, int fd) {
    Elf32_Ehdr eh;
    Elf32_Phdr ph;
    FILE* f;
    int i;
    int fptr;

    if (!(f = fdopen(fd, "w"))) {
	perror("fopen()");
	return 0;
    }

    fptr = 0;

    memset(&eh, 0, sizeof(eh));
    eh.e_ident[EI_MAG0] = ELFMAG0;
    eh.e_ident[EI_MAG1] = ELFMAG1;
    eh.e_ident[EI_MAG2] = ELFMAG2;
    eh.e_ident[EI_MAG3] = ELFMAG3;
    eh.e_ident[EI_CLASS] = ELFCLASS32;
    eh.e_ident[EI_DATA] = ELFDATA2LSB;
    eh.e_ident[EI_VERSION] = EV_CURRENT;
    eh.e_type = ET_EXEC;
    eh.e_machine = EM_386;
    eh.e_version = EV_CURRENT;
    eh.e_entry = p->user_data.regs.eip;
    eh.e_phoff = sizeof(eh);
    eh.e_shoff = 0;
    eh.e_flags = 0x112;
    eh.e_ehsize = sizeof(eh);
    eh.e_phentsize = sizeof(ph);
    eh.e_phnum = p->num_maps;
    eh.e_shentsize = sizeof(Elf32_Shdr);
    eh.e_shnum = 0;
    eh.e_shstrndx = SHN_UNDEF;
    fptr += fwrite(&eh, 1, sizeof(eh), f);

    unsigned int map_offset = ((sizeof(eh) + p->num_maps*sizeof(ph))+0xfff)&0xfffff000;
    memset(&ph, 0, sizeof(ph));
    for (i = p->num_maps-1; i >= 0; i--) {
	ph.p_type = PT_LOAD;
	ph.p_offset = map_offset;
	ph.p_vaddr = ph.p_paddr = p->maps[i].start;
	ph.p_filesz = ph.p_memsz = p->maps[i].length;
	ph.p_flags =
	    ((p->maps[i].prot & PROT_READ) ?PF_R:0)|
	    ((p->maps[i].prot & PROT_WRITE)?PF_W:0)|
	    ((p->maps[i].prot & PROT_EXEC) ?PF_X:0);
	ph.p_align = 0x1000;
	fptr += fwrite(&ph, 1, sizeof(ph), f);
	map_offset += (p->maps[i].length+0xfff)&0xfffff000;
    }

    char empty_page[0x1000];
    memset(empty_page, 0, sizeof(empty_page));
    for (i = p->num_maps-1; i >= 0; i--) {
	/* memory align us */
	if (fptr & 0xfff)
	    fptr += fwrite(empty_page, 1, 0x1000-(fptr&0xfff), f);
	assert((fptr & 0xfff) == 0);
	fptr += fwrite(p->maps[i].data, 1, p->maps[i].length, f);
    }
    if (fptr & 0xfff)
	fptr += fwrite(empty_page, 1, 0x1000-(fptr&0xfff), f);

    fflush(f);
    return 1;
}

/* vim:set ts=8 sw=4 noet: */
