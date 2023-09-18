#include "vmm.h"
#include "machine.h"
#include "idt.h"
#include "libk.h"
#include "config.h"
#include "debug.h"
#include "physmem.h"
#include "sys.h"


namespace VMM {

uint32_t the_shared_frame;
uint32_t* the_common_pd;

uint32_t* get_pt(uint32_t* pd, uint32_t va) {
    uint32_t pdi = va >> 22;
    auto pde = pd[pdi];
    if ((pde&1) == 0) {
        pde = PhysMem::alloc_frame() | 7;
        pd[pdi] = pde;
    }
    return (uint32_t*)((pde >> 12) << 12);
}

void map(uint32_t* pd, uint32_t va, uint32_t pa, uint32_t bits) {
    uint32_t pti = (va >> 12) & 0x3ff;
    auto pt = get_pt(pd, va);
    pt[pti] = ((pa >> 12) << 12) | bits | 1;
}

void global_init() {
    the_shared_frame = PhysMem::alloc_frame();
    the_common_pd = (uint32_t*)PhysMem::alloc_frame();

    auto mem_size = kConfig.memSize;

    for (uint32_t va = 4096; va < mem_size; va += 4096) {
        map(the_common_pd, va, va, 3);
    }
    map(the_common_pd, kConfig.ioAPIC, kConfig.ioAPIC, 3);
    map(the_common_pd, kConfig.localAPIC, kConfig.localAPIC, 3);
    map(the_common_pd, 0xF0000000, the_shared_frame, 7);
}

void per_core_init() {
    
}

} /* namespace vmm */

extern "C" void vmm_pageFault(uintptr_t va_, void* context) {
    Debug::panic("can't handle page fault at %x\n",va_);
}
