#include "vmm.h"
#include "config.h"
#include "debug.h"
#include "idt.h"
#include "libk.h"
#include "machine.h"
#include "physmem.h"
#include "sys.h"

#include "events.h"
#include "loop.h"
#include "smp.h"

#define process_start_mem 0x80000000
#define process_end_mem 0xF0000000
#define shared_process_end_mem 0xF0001000
#define fault_address 0xF0000800

namespace VMM {

Atomic<bool> global_init_done = false;
uint32_t num_page_entry_tables =
    (kConfig.memSize) / (PhysMem::FRAME_SIZE) / (PhysMem::FRAME_SIZE / 4) + 1;
// potential for last index being set incorrectly / too much
uint32_t *kernel_frames;
uint32_t shared_frame;
uint32_t apic_frame;
ProcessControlBlock *pcb_table[MAX_PROCS];
bool user_preemption[MAX_PROCS] = {0};
uint32_t temp_vmm[MAX_PROCS];

void global_init() {
    kernel_frames = new uint32_t[num_page_entry_tables];

    // kernel identity mapping
    uint32_t pt = PhysMem::alloc_frame();
    uint32_t *pt_pointer = (uint32_t *)pt;
    kernel_frames[0] = pt | 0x3;
    for (uint32_t j = 1; j < PhysMem::FRAME_SIZE / sizeof(uint32_t); j++) {
        pt_pointer[j] = (j << 12) | 0x3;
    }

    for (uint32_t i = 1; i < num_page_entry_tables; i++) {
        pt = PhysMem::alloc_frame();
        pt_pointer = (uint32_t *)pt;
        kernel_frames[i] = pt | 0x3;
        for (uint32_t j = 0; j < 1024; j++) {
            pt_pointer[j] = (((i << 10) + j) << 12) | 0x3;
        }
    }

    // shared frame
    uint32_t shared = PhysMem::alloc_frame();
    shared_frame = PhysMem::alloc_frame();
    ((uint32_t *)shared_frame)[0] = shared | 0x7;

    apic_frame = PhysMem::alloc_frame();
    uint32_t *apic_pointer = (uint32_t *)apic_frame;

    apic_pointer[(kConfig.localAPIC >> 12) & 0x3ff] = ((kConfig.localAPIC >> 12) << 12) | 0x3;
    apic_pointer[(kConfig.ioAPIC >> 12) & 0x3ff] = ((kConfig.ioAPIC >> 12) << 12) | 0x3;

    global_init_done.set(true);
}

void per_core_loop() {
    global_init_done.monitor_value();
    while (!global_init_done.get()) {
        iAmStuckInALoop(true);
        global_init_done.monitor_value();
    }
}

void clean_up_vmm(uint32_t pdt) {
    uint32_t *old_pdt_pointer = (uint32_t *)pdt;
    for (uint32_t i = process_start_mem >> 22; i < (process_end_mem >> 22); i++) {
        if (old_pdt_pointer[i] & 0x1) {
            uint32_t old_pt = (old_pdt_pointer[i] >> 12) << 12;
            uint32_t *old_pt_pointer = (uint32_t *)old_pt;

            for (uint32_t j = 0; j < 1024; j++) {
                if (old_pt_pointer[j] & 0x1) {
                    PhysMem::dealloc_frame(((old_pt_pointer[j] >> 12) << 12));
                    old_pt_pointer[j] = 0;
                }
            }

            PhysMem::dealloc_frame(((old_pdt_pointer[i] >> 12) << 12));
            old_pdt_pointer[i] = 0;
        }
    }
    // Debug::printf("address a %x address b %x\n", kConfig.localAPIC >> 22, kConfig.ioAPIC >> 22);
    // uint32_t address = kConfig.localAPIC;
    // uint32_t pdi = (address >> 22);
    // if (old_pdt_pointer[pdi] & 0x1)
    //     PhysMem::dealloc_frame((old_pdt_pointer[pdi] >> 12) << 12);
    // old_pdt_pointer[pdi] = 0;

    // address = kConfig.ioAPIC;
    // pdi = (address >> 22);
    // if(old_pdt_pointer[pdi] & 0x1)
    //     PhysMem::dealloc_frame((old_pdt_pointer[pdi] >> 12) << 12);
    // PhysMem::dealloc_frame(pdt);
    // vmm_on(temp_vmm[SMP::me()]);
}

uint32_t copy_vmm_mapping(uint32_t prev_pdt) {
    uint32_t pdt = PhysMem::alloc_frame();
    uint32_t *pdt_pointer = (uint32_t *)pdt;

    for (uint32_t i = 0; i < num_page_entry_tables; i++) {
        pdt_pointer[i] = kernel_frames[i];
    }

    // shared vmm
    uint32_t va_shared = 0xF0000000;
    uint32_t pdi_shared = va_shared >> 22;
    // uint32_t pt_shared = PhysMem::alloc_frame();

    // Debug::printf("hello\n");
    // map(pdt_pointer, kConfig.ioAPIC);
    // map(pdt_pointer, kConfig.localAPIC);
    // Debug::printf("not hello\n");
    uint32_t *old_pdt_pointer = (uint32_t *)prev_pdt;
    // Debug::printf("old pdt %x\n", old_pdt_pointer);
    // Debug::printf("hi %x\n", (process_end_mem >> 22));
    // Debug::printf("pdi_shared %x\n", pdi_shared);
    for (uint32_t i = process_start_mem >> 22; i < (process_end_mem >> 22); i++) {
        if (old_pdt_pointer[i] & 0x1) {
            uint32_t pt = PhysMem::alloc_frame();
            uint32_t *pt_pointer = (uint32_t *)pt;

            uint32_t old_pt = (old_pdt_pointer[i] >> 12) << 12;
            uint32_t *old_pt_pointer = (uint32_t *)old_pt;

            // Debug::printf("old pt %x\n", old_pt_pointer);
            pdt_pointer[i] = pt | (old_pdt_pointer[i] & 0xfff);
            for (uint32_t j = 0; j < 1024; j++) {
                if (old_pt_pointer[j] & 0x1) {
                    uint32_t frame = PhysMem::alloc_frame();
                    uint32_t old_frame = (old_pt_pointer[j] >> 12) << 12;

                    memcpy((void *)frame, (void *)old_frame, PhysMem::FRAME_SIZE);
                    pt_pointer[j] = frame | (old_pt_pointer[j] & 0xfff);
                }
            }
        }
    }

    pdt_pointer[pdi_shared] = shared_frame | 0x7;

    uint32_t apic_shared = kConfig.ioAPIC >> 22;
    pdt_pointer[apic_shared] = apic_frame | 0x7;
    // ((uint32_t *)pt_shared)[0] = shared_frame | 0x7;

    return pdt;
}

void temporary_vmm_mapping() { vmm_on(temp_vmm[SMP::me()]); }

uint32_t new_vmm_mapping() {
    uint32_t pdt = PhysMem::alloc_frame();
    uint32_t *pdt_pointer = (uint32_t *)pdt;
    // Debug::printf("num_page_ %d\n", num_page_entry_tables);
    for (uint32_t i = 0; i < num_page_entry_tables; i++) {
        pdt_pointer[i] = kernel_frames[i];
    }

    // shared vmm
    uint32_t va_shared = 0xF0000000;
    uint32_t pdi_shared = va_shared >> 22;
    // uint32_t pt_shared = PhysMem::alloc_frame();
    // Debug::printf("NEW %x\n", pdi_shared);
    pdt_pointer[pdi_shared] = shared_frame | 0x7;
    // ((uint32_t *)pt_shared)[0] = shared_frame | 0x7;

    // map(pdt_pointer, kConfig.ioAPIC);
    // map(pdt_pointer, kConfig.localAPIC);
    uint32_t apic_shared = kConfig.ioAPIC >> 22;
    // Debug::printf("apic_shared %x\n", apic_shared);
    // Debug::printf("apic frame %x\n", apic_frame);
    pdt_pointer[apic_shared] = apic_frame | 0x7;
    return pdt;
}

void remove_vmm_mapping(uint32_t pdt, uint32_t addr, uint32_t size) {
    uint32_t *pdt_pointer = (uint32_t *)pdt;
    for (uint32_t i = addr >> 22; i < (addr + size) >> 22; i++) {
        if (pdt_pointer[i] & 0x1) {
            uint32_t pt = (pdt_pointer[i] >> 12) << 12;
            uint32_t *pt_pointer = (uint32_t *)pt;
            for (uint32_t j = 0; j < 1024; j++) {
                if (pt_pointer[j] & 0x1) {
                    PhysMem::dealloc_frame(((pt_pointer[j] >> 12) << 12));
                    pt_pointer[j] = 0;
                }
            }
            PhysMem::dealloc_frame(pt);
            pdt_pointer[i] = 0;
        }
    }

    uint32_t i = (addr + size - 1) >> 22;
    if (pdt_pointer[i] & 0x1) {
        uint32_t pt = (pdt_pointer[i] >> 12) << 12;
        uint32_t *pt_pointer = (uint32_t *)pt;
        for (uint32_t j = 0; j < (((addr + size) >> 12) & 0x3ff); j++) {
            if (pt_pointer[j] & 0x1) {
                // Debug::printf("page dealloced %x\n", j);
                PhysMem::dealloc_frame(((pt_pointer[j] >> 12) << 12));
                pt_pointer[j] = 0;
            }
        }
    }
}

void per_core_init() { // wait till global init is done
    per_core_loop();
    // init

    auto vmm_pd = new_vmm_mapping();
    temp_vmm[SMP::me()] = vmm_pd;
    vmm_on(vmm_pd);
    pcb_table[SMP::me()] = new ProcessControlBlock(0, nullptr);
    // TODO: clean up kernelframes when done }
}
} // namespace VMM

extern "C" void vmm_pageFault(uintptr_t va_, uintptr_t *saveState) {
    // Debug::printf("can't handle page fault at %x\n", va_);
    // Debug::printf("page table %d pc %x\n", VMM::pcb_table[SMP::me()]->sig_handler,
    // saveState[10]);

    // for (uint32_t i = 0; i < 11; i++) {
    //     Debug::printf("vmm %d val %x\n", i, saveState[i]);
    // }
    if(va_ == 0xF0002000) {
        sigreturn();
    }

    if (VMM::pcb_table[SMP::me()]->find_exist_VME(va_)) {
        // Debug::printf("soft page fault\n");
        uint32_t pdt = getCR3();
        // Debug::printf("PDT VMM %x\n", pdt);
        uint32_t *pdt_pointer = (uint32_t *)pdt;

        uint32_t pdi = (va_ >> 22);
        uint32_t pti = (va_ >> 12) & 0x3ff;

        // Add bound check
        uint32_t pt = (pdt_pointer[pdi] >> 12) << 12;
        if (!(pdt_pointer[pdi] & 0x1)) {
            // Debug::printf("pdt_pointer[pdi] & 0x1 == 0\n");
            pt = PhysMem::alloc_frame();
            pdt_pointer[pdi] = pt | 0x7; // set
        }

        // Debug::printf("pdt_pointer1 %x %x %x\n", pdi, pdt_pointer[pdi], pt);
        // Debug::printf("pdt_pointer2 %x\n", pdt_pointer[pdi]);

        uint32_t *pt_pointer = (uint32_t *)pt;
        uint32_t frame = PhysMem::alloc_frame();
        // Debug::printf("pt_pointer[pti] %x %x\n", pti, pt_pointer[pti] & 0x1);
        pt_pointer[pti] = frame | 0x7;
        return;
    }

    if (VMM::pcb_table[SMP::me()]->sig_handler != 0) {
        if (VMM::pcb_table[SMP::me()]->is_in_sig_handler) {
            *((uint32_t *)fault_address) = va_;
            // exit
            // VMM::pcb_table[SMP::me()]->close_all_sempahores();
            VMM::pcb_table[SMP::me()]->set_exit(139);
            VMM::clean_up_vmm(getCR3());
            VMM::temporary_vmm_mapping();
            event_loop();
        }

        uint32_t *userEip = &saveState[10];
        auto cur_pcb = VMM::pcb_table[SMP::me()];
        uint32_t sig_pc = cur_pcb->sig_handler;
        cur_pcb->store_registers(userEip[0], userEip[3], saveState);
        VMM::pcb_table[SMP::me()]->is_in_sig_handler = true;

        // for (int i = 0; i < 14; i++) {
        //     Debug::printf("index %d value %x\n", i, saveState[i]);
        // }

        // Debug::printf("switching from pagefualt to sig handler %x\n", userEip[3]);
        switchToUserWithParams(sig_pc, userEip[3] - 128, 1, va_);
    }

    if (VMM::pcb_table[SMP::me()]->sig_handler == 0) { // defualt handler
        // Debug::printf("default handlre\n");
        // if (va_ < process_start_mem || va_ >= shared_process_end_mem) {
        // Debug::printf("fault address %x\n", va_);
        *((uint32_t *)fault_address) = va_;
        // exit
        // VMM::pcb_table[SMP::me()]->close_all_sempahores();
        VMM::pcb_table[SMP::me()]->set_exit(139);
        VMM::clean_up_vmm(getCR3());
        VMM::temporary_vmm_mapping();
        // Debug::printf("finish setting \n");
        event_loop();
        // }

        uint32_t pdt = getCR3();
        // Debug::printf("PDT VMM %x\n", pdt);
        uint32_t *pdt_pointer = (uint32_t *)pdt;

        uint32_t pdi = (va_ >> 22);
        uint32_t pti = (va_ >> 12) & 0x3ff;

        // Add bound check
        uint32_t pt = (pdt_pointer[pdi] >> 12) << 12;
        // Debug::printf("pdt_pointer1 %x\n", pdt_pointer[pdi]);
        if (!(pdt_pointer[pdi] & 0x1)) {
            // Debug::printf("pdt_pointer[pdi] & 0x1 == 0\n");
            pt = PhysMem::alloc_frame();
            pdt_pointer[pdi] = pt | 0x7; // set
        }

        // Debug::printf("pdt_pointer2 %x\n", pdt_pointer[pdi]);

        uint32_t *pt_pointer = (uint32_t *)pt;
        uint32_t frame = PhysMem::alloc_frame();
        // Debug::printf("pt_pointer[pti] %x\n", pt_pointer[pti] & 0x1);
        pt_pointer[pti] = frame | 0x7;
        return;
    }

    // if (va_ < process_start_mem || va_ >= shared_process_end_mem) {
    // } else if (VMM::pcb_table[SMP::me()]->sig_handler == 0 ||
    //            VMM::pcb_table[SMP::me()]->find_exist_VME(va_)) {
    // }

    // if (!VMM::pcb_table[SMP::me()]->find_exist_VME(va_)) {

    // }
    Debug::panic("SHOULD NEVER REACH THINS IN\n");
    // if (va_ < process_start_mem || va_ >= shared_process_end_mem) {
    //     Debug::printf("fault address %x\n", va_);
    //     *((uint32_t *)fault_address) = va_;
    //     // exit
    //     VMM::pcb_table[SMP::me()]->close_all_sempahores();
    //     VMM::pcb_table[SMP::me()]->set_exit(139);
    //     VMM::clean_up_vmm(getCR3());
    //     VMM::temporary_vmm_mapping();
    //     // Debug::printf("finish setting \n");
    //     event_loop();
    // }

    uint32_t pdt = getCR3();
    // Debug::printf("PDT VMM %x\n", pdt);
    uint32_t *pdt_pointer = (uint32_t *)pdt;

    uint32_t pdi = (va_ >> 22);
    uint32_t pti = (va_ >> 12) & 0x3ff;

    // Add bound check
    uint32_t pt = (pdt_pointer[pdi] >> 12) << 12;
    // Debug::printf("pdt_pointer1 %x\n", pdt_pointer[pdi]);
    if (!(pdt_pointer[pdi] & 0x1)) {
        // Debug::printf("pdt_pointer[pdi] & 0x1 == 0\n");
        pt = PhysMem::alloc_frame();
        pdt_pointer[pdi] = pt | 0x7; // set
    }

    // Debug::printf("pdt_pointer2 %x\n", pdt_pointer[pdi]);

    uint32_t *pt_pointer = (uint32_t *)pt;
    uint32_t frame = PhysMem::alloc_frame();
    // Debug::printf("pt_pointer[pti] %x\n", pt_pointer[pti] & 0x1);
    pt_pointer[pti] = frame | 0x7;
    // Debug::printf("MAX kernel mem %x\n", kConfig.ioAPIC);
    // Debug::printf("MAX kernel mem %x\n", kConfig.localAPIC);
    // Debug::printf("tables %d\n", VMM::num_page_entry_tables);
    // Debug::printf("apic %x\n", VMM::apic_frame);
    // if (va_ < process_start_mem || va_ >= shared_process_end_mem) // lower bound
    // {
    // }
    // uint16_t error_code = saveState[0];
    // uint32_t error_code2 = saveState[1];
    // Debug::printf("error %x\n", error_code);
    // Debug::printf("error2 %x\n", error_code2);
}
