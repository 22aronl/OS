#ifndef _VMM_H_
#define _VMM_H_

#include "stdint.h"
#include "process.h"

#define MAX_CORES 16 //find if there is a constant for this sommewhere else

namespace VMM {

extern ProcessControlBlock* pcb_table[MAX_PROCS];
extern bool user_preemption[MAX_PROCS];
// Called (on the initial core) to initialize data structures, etc
extern void global_init();

// Called on each core to do per-core initialization
extern void per_core_init();
extern uint32_t new_vmm_mapping();
extern uint32_t copy_vmm_mapping(uint32_t prev_pdt);
extern void clean_up_vmm(uint32_t pdt);
extern void temporary_vmm_mapping();
extern void remove_vmm_mapping(uint32_t pdt, uint32_t addr, uint32_t size);
} // namespace VMM

#endif
