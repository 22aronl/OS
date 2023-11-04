#include "elf.h"
#include "machine.h"
#include "debug.h"
#include "vmm.h"

uint32_t ELF::load(Node* file) {
    ElfHeader e;
    // Debug::printf("mem %x\n", &e);
    file->read_all(0, 64, (char *)&e);
    // check header
    if (!(e.maigc0 == 0x7f && e.magic1 == 'E' && e.magic2 == 'L' && e.magic3 == 'F')) {
        // Debug::printf("*** not an ELF Header file\n");
        return -1;
    }

    if (e.cls != 1) {
        // Debug::printf("*** not a 32-bit ELF file\n");
        return -1;
    }

    // handle encoding??

    // header version
    if (e.header_version != 1) {
        // Debug::printf("*** not a version 1 ELF file\n");
        return -1;
    }

    // machine
    if (e.machine != 3) {
        // Debug::printf("*** not an i386 ELF file\n");
        return -1;
    }

    // version
    if (e.version != 1) {
        // Debug::printf("not a version 1 ELF file\n");
        return -1;
    }

    // Debug::printf("ENTRY %x\n", e.entry);
    // Debug::printf("End %x\n", kConfig.memSize);
    // if(e.entry < 0x00600000 || e.entry > kConfig.memSize)
    //     return -1;

    // if(e.phoff + e.phnum * e.phentsize > kConfig.memSize)
    //     return -1;

    // Debug::printf("phnum %d shnum %d\n", e.phnum, e.shnum);
    // bool flag_entry = false;
    ProgramHeader ph;
    // for (uint16_t i = 0; i < e.phnum; i++) {
    //     // Debug::printf("hi\n");
    //     file->read_all(e.phoff + i * e.phentsize, sizeof(ProgramHeader), (char *)&ph);
    //     // Debug::printf("type %x\n", ph.type);
    //     if(ph.type == 1) {
    //         //Debug::printf("match\n");
    //         // if (ph.vaddr < 0x00600000 || ph.vaddr > kConfig.memSize)
    //         //     return -1;

    //         // Debug::printf("address %x %x %x\n", ph.vaddr, ph.vaddr + ph.filesz, e.entry);

    //         // if(ph.vaddr <= e.entry && e.entry <= ph.vaddr + ph.filesz)
    //         //     flag_entry = true;

    //     }
    // }

    // if(!flag_entry)
    //     return -1;
    // Debug::printf("hello \n");
    for(uint16_t i = 0; i < e.phnum; i++) {
        file->read_all(e.phoff + i * e.phentsize, sizeof(ProgramHeader), (char *)&ph);
        if(ph.type == 1) {
            // Debug::printf("s");
            // Debug::printf("vaddr mem %x %x\n", ph.vaddr, e.phoff + i * e.phentsize);
            // Debug::printf("ph address %x, size %x\n", ph.vaddr, ph.memsz);
            // Debug::printf("mem %x %x %x\n", ((ph.memsz >> 12) << 12), (((ph.memsz & 0xFFF) != 0) << 12), ((ph.memsz >> 12) << 12) + (((ph.memsz & 0xFFF) != 0) << 12));
            VMM::pcb_table[SMP::me()]->add_new_VME(ph.vaddr, ((ph.memsz >> 12) << 12) + (((ph.memsz & 0xFFF) != 0) << 12));
            file->read_all(ph.offset, ph.filesz, (char *)ph.vaddr);

            // char *mem_ar = (char *)ph.vaddr;
            // for (uint32_t cur_mem = ph.filesz; cur_mem < ph.memsz; cur_mem++) {
            //     mem_ar[cur_mem] = 0;
            // }
        }
    }

    // Debug::printf("end loading \n");
    // Debug::printf("hello %x\n", e.entry);

    // Debug::printf("section \n");
    // SectionHeader sectionh;
    // for (uint16_t i = 0; i < e.shnum; i++) {
    //     file->read_all(e.shoff + i * e.shentsize, sizeof(SectionHeader), (char *)&sectionh);
    //     Debug::printf("loc %d size %d", e.shoff + i * e.shentsize, sectionh.sh_size);

    //     if (sectionh.sh_size != 0) {
    //         file->read_all(sectionh.sh_offset, sectionh.sh_size, (char *)sectionh.sh_addr);
    //     }
    // }
    VMM::pcb_table[SMP::me()]->add_new_VME(0xEF000000, 0x1000000);
    // Debug::printf("END ELF LOAD\n");

    return e.entry;
}
