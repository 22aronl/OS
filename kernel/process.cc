#include "process.h"
#include "vmm.h"

void ProcessControlBlock::exit_process() {
    VMM::pcb_table[SMP::me()]->set_exit(this->kill_message);
    VMM::clean_up_vmm(getCR3());
    VMM::temporary_vmm_mapping();
    // Debug::printf("finish exit\n");
    event_loop();
}