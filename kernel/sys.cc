#include "sys.h"
#include "debug.h"
#include "events.h"
#include "idt.h"
#include "machine.h"
#include "physmem.h"
#include "stdint.h"

#include "config.h"
#include "elf.h"
#include "ext2.h"
#include "kernel.h"
#include "process.h"
#include "smp.h"
#include "vmm.h"

bool in_userspace(uint32_t userspace) { return userspace >= 0x80000000 && userspace < 0xF0001000; }

void exit(uint32_t *userEsp) {
    // Debug::printf("exit\n");
    // VMM::pcb_table[SMP::me()]->close_all_sempahores();
    VMM::pcb_table[SMP::me()]->set_exit(userEsp[1]);
    VMM::clean_up_vmm(getCR3());
    VMM::temporary_vmm_mapping();
    // Debug::printf("finish exit\n");
    event_loop();
}

extern "C" int sysHandler(uint32_t eax, uint32_t *frame) {
    // Debug::printf("sys handling %x\n", eax);
    uint32_t *userEsp = (uint32_t *)frame[12];
    uint32_t *userEip = &frame[9];
    uint32_t *userRegs = frame;

    switch (eax) {

    case 0:
        exit(userEsp);
        break;
    case 1:
        if (userEsp[1] == 1) {
            if (!in_userspace(userEsp[2]))
                return -1;
            for (uint32_t i = 0; i < userEsp[3]; i++)
                Debug::printf("%c", ((char *)userEsp[2])[i]);
            return userEsp[3];
        } else {
            Debug::panic("*** unknown write syscall %d\n", userEsp[1]);
        }
        break;
    case 2: {
        // TODO: transfer over registers
        // copy over vmm & physmem
        // pointer to new pdt
        // use switchToUser with same pc, same esp, 0 in eax in a go
        // Debug::printf("HI\n");
        // Debug::printf("forking\n");

        uint32_t pc = userEip[0];
        uint32_t esp = userEip[3];
        uint32_t vmm_pd = VMM::copy_vmm_mapping(getCR3());
        // Debug::printf("pd %x\n", VMM::pcb_table[SMP::me()]);
        ProcessControlBlock *new_pcb = new ProcessControlBlock(vmm_pd, VMM::pcb_table[SMP::me()]);
        // Debug::printf("before storing\n");
        new_pcb->store_registers(pc, esp, userRegs);
        go([new_pcb]() {
            // Debug::printf("return to child \n");
            VMM::pcb_table[SMP::me()] = new_pcb;
            vmm_on(new_pcb->vmm_pd);
            new_pcb->return_to_process(0);
            Debug::printf("should not run");
        });
        // Debug::printf("end fork\n");
        return SMP::me() + 1;
    }
    case 7:
        Debug::shutdown();
        return -1;
    case 998: { // yeild syscall

        // Debug::printf("yeild\n");
        ProcessControlBlock *cur_pcb = VMM::pcb_table[SMP::me()];
        uint32_t pc = userEip[0];
        uint32_t esp = userEip[3];

        cur_pcb->store_registers(pc, esp, userRegs);
        go([cur_pcb]() {
            VMM::pcb_table[SMP::me()] = cur_pcb;
            vmm_on(cur_pcb->vmm_pd);
            cur_pcb->return_to_process(cur_pcb->registers[1]);
        });

        event_loop();
        break;
    }
    case 999: {
        // Debug::printf("join\n");
        auto cur_pcb = VMM::pcb_table[SMP::me()];
        if (cur_pcb->children == nullptr)
            return -1;
        uint32_t pc = userEip[0];
        uint32_t esp = userEip[3];
        cur_pcb->store_registers(pc, esp, userRegs);
        // Debug::printf("finish storing\n");
        cur_pcb->get_child()->get([cur_pcb](auto v) {
            // Debug::printf("return from join\n");
            VMM::pcb_table[SMP::me()] = cur_pcb;
            vmm_on(cur_pcb->vmm_pd);
            cur_pcb->return_to_process(v);
        });
        // Debug::printf("event loop\n");
        event_loop();
        break;
    }
    case 1000: {
        // Debug::printf("EXECLING\n");
        Node *node = file_system->find_absolute((char *)userEsp[1]);
        if (node == nullptr || !node->is_file())
            return -1;
        uint32_t size = 2;
        while (userEsp[size] != 0)
            size++;

        char **characters = new char *[size - 2];
        int char_size[size - 2];
        int total_char = 0;
        for (uint32_t i = 2; i < size; i++) {
            auto len = K::strlen((char *)userEsp[i]);
            char *s = (char *)userEsp[i];
            characters[i - 2] = new char[len + 1];
            char_size[i - 2] = len + 1;
            total_char += len + 1;
            for (int j = 0; j < len + 1; j++)
                characters[i - 2][j] = s[j];
        }

        uint32_t old_vmm_table = getCR3();
        VME *old_vme = VMM::pcb_table[SMP::me()]->head;
        VMM::pcb_table[SMP::me()]->head = nullptr;
        vmm_on(VMM::new_vmm_mapping());
        uint32_t e = ELF::load(node);
        if (e == uint32_t(-1)) {
            for (uint32_t i = 0; i < size - 2; i++)
                delete[] characters[i];
            delete[] characters;
            VMM::clean_up_vmm(getCR3());
            VMM::pcb_table[SMP::me()]->close_all_VME();
            vmm_on(old_vmm_table);
            return -1;
        }
        VMM::clean_up_vmm(old_vmm_table);
        VMM::pcb_table[SMP::me()]->vmm_pd = getCR3();
        VMM::pcb_table[SMP::me()]->sig_handler = 0;
        VMM::pcb_table[SMP::me()]->is_in_sig_handler = 0;
        VMM::pcb_table[SMP::me()]->head = old_vme;
        uint32_t userEsp1 = 0xF0000000 - 4 - total_char - size * 4;
        uint32_t *new_stack_ptr = reinterpret_cast<uint32_t *>(userEsp1);

        new_stack_ptr[0] = size - 2;

        char *new_char_ar = (char *)(new_stack_ptr + size + 1);
        int current_index = 0;
        for (uint32_t i = 0; i < size - 2; i++) {
            new_stack_ptr[i + 2] = (uint32_t)(new_char_ar + current_index);
            for (int j = 0; j < char_size[i]; j++) {
                new_char_ar[current_index++] = characters[i][j];
            }
        }

        new_stack_ptr[size] = 0;

        for (uint32_t i = 0; i < size - 2; i++)
            delete[] characters[i];
        delete[] characters;

        new_stack_ptr[1] = (uint32_t)(&new_stack_ptr[1] + 1);

        if (VMM::user_preemption[SMP::me()]) {
            VMM::user_preemption[SMP::me()] = false;
            auto cur_pcb = VMM::pcb_table[SMP::me()];

            cur_pcb->registers[0] = e;
            cur_pcb->registers[5] = userEsp1;
            go([cur_pcb]() {
                VMM::pcb_table[SMP::me()] = cur_pcb;
                vmm_on(cur_pcb->vmm_pd);
                cur_pcb->return_to_process(cur_pcb->registers[1]);
            });
            event_loop();
            Debug::panic("Should have yielded and never returned\n");
        }

        switchToUser(e, userEsp1, 0);
        Debug::printf("SHOULD NOT RUN\n");
        break;
    }
    case 1001: {
        // Debug::printf("creating semaphore \n");
        return VMM::pcb_table[SMP::me()]->create_semaphore(userEsp[1]);
    }
    case 1002: {
        // Debug::printf("up %d\n", userEsp[1]);
        Semaphore *sem = VMM::pcb_table[SMP::me()]->retrieve_semaphore(userEsp[1]);
        if (sem == nullptr)
            return -1;
        // Debug::printf("up! %d\n", userEsp[1]);
        // Debug::printf("sem point up%x\n", sem);
        sem->up();
        break;
    }
    case 1003: {
        // Debug::printf("down semaphore %d\n", userEsp[1]);
        ProcessControlBlock *cur_pcb = VMM::pcb_table[SMP::me()];
        uint32_t pc = userEip[0];
        uint32_t esp = userEip[3];
        cur_pcb->store_registers(pc, esp, userRegs);
        Semaphore *sem = VMM::pcb_table[SMP::me()]->retrieve_semaphore(userEsp[1]);
        // Debug::printf("sem point %x\n", sem);
        if (sem == nullptr)
            return -1;
        int k = userEsp[1];
        sem->down([cur_pcb, k]() {
            // Debug::printf("down %d\n", k);
            VMM::pcb_table[SMP::me()] = cur_pcb;
            vmm_on(cur_pcb->vmm_pd);
            // Debug::printf("returnign to process\n");
            cur_pcb->return_to_process(0);
        });
        event_loop();
        break;
    }
    case 1004: { // signal
        // Debug::printf("*** setting page table %d\n", userEsp[1]);
        VMM::pcb_table[SMP::me()]->sig_handler = userEsp[1];
        break;
    }
    case 1005: { // void* simple_mmap(void* addr, unsigned size)
        // Debug::printf("simple mmap\n");
        uint32_t addr = userEsp[1];
        uint32_t size = userEsp[2];

        if (addr == 0) {
            if ((size & 0xFFF) != 0 || size > 0xF0000000 - 0x80000000 || size == 0)
                return 0;
            auto k = VMM::pcb_table[SMP::me()]->find_space_VME(size);
            // Debug::printf("space foudn at %x\n", k);

            return k;
        }
        // Debug::printf("addr %x size %x addr + size %x\n", addr, size, addr + size);
        if ((addr & 0xFFF) != 0 || (size & 0xFFF) != 0 || addr + size > 0xF0000000 ||
            (addr + size < 0x80000000))
            return 0;

        if (VMM::pcb_table[SMP::me()]->add_new_VME(addr, size))
            return addr;
        return 0;
        break;
    }
    case 1006: { // void sigreturn()
        // Debug::printf("SIG RETURN\n");
        if (VMM::pcb_table[SMP::me()]->is_in_sig_handler) {
            auto cur_pcb = VMM::pcb_table[SMP::me()];
            go([cur_pcb] {
                VMM::pcb_table[SMP::me()] = cur_pcb;
                VMM::pcb_table[SMP::me()]->is_in_sig_handler = false;
                vmm_on(cur_pcb->vmm_pd);
                // Debug::printf("returnign to process\n");
                cur_pcb->return_to_process(cur_pcb->registers[1]);
            });
            // VMM::pcb_table[SMP::me()]->return_to_process(VMM::pcb_table[SMP::me()]->registers[1]);
            event_loop();
        }
        break;
    }
    case 1007: { // int sem_close(int sem)
        return VMM::pcb_table[SMP::me()]->close_semaphore(userEsp[1]);
    }
    case 1008: {
        // Debug::printf("simple MUNMAP\n");
        uint32_t addr = userEsp[1];
        if (addr < 0x80000000 || addr > 0xF0000000)
            return -1;
        VME *vme = VMM::pcb_table[SMP::me()]->remove_VME(addr);
        if (vme == nullptr)
            return -1;
        // Debug::printf("this %x %x\n", vme->addr, vme->size);
        VMM::remove_vmm_mapping(VMM::pcb_table[SMP::me()]->vmm_pd, vme->addr, vme->size);
        // Debug::printf("ends\n");
        vmm_on(VMM::pcb_table[SMP::me()]->vmm_pd);
        delete vme;
        return 0;
    }
    default:
        Debug::panic("syscall %d\n", eax);
    }

    // if (VMM::user_preemption[SMP::me()]) {
    //     VMM::user_preemption[SMP::me()] = false;
    //     auto cur_pcb = VMM::pcb_table[SMP::me()];
    //     cur_pcb->store_registers(userEip[1], userEip[3], userRegs);

    //     go([cur_pcb]() {
    //         VMM::pcb_table[SMP::me()] = cur_pcb;
    //         vmm_on(cur_pcb->vmm_pd);
    //         cur_pcb->return_to_process(cur_pcb->registers[1]);
    //     });

    //     event_loop();
    //     Debug::panic("Should have yielded and never returned\n");
    // }

    return 0;
}

void SYS::init(void) { IDT::trap(48, (uint32_t)sysHandler_, 3); }
