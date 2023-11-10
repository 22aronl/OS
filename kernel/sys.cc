#include "sys.h"
#include "debug.h"
#include "events.h"
#include "idt.h"
#include "machine.h"
#include "physmem.h"
#include "stdint.h"

#include "bb.h"
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

void sigreturn() {
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
        // if (userEsp[1] == 1) {
        //     if (!in_userspace(userEsp[2]))
        //         return -1;
        //     for (uint32_t i = 0; i < userEsp[3]; i++)
        //         Debug::printf("%c", ((char *)userEsp[2])[i]);
        //     return userEsp[3];
        // } else {
        //     Debug::panic("*** unknown write syscall %d\n", userEsp[1]);
        // }
        // break;
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
        Node* node;
        if(((char*)userEsp[1])[0] == '/')
            node = file_system->find_absolute((char *)userEsp[1]);
        else
            node = file_system->find_relative(VMM::pcb_table[SMP::me()]->cur_path, (char *)userEsp[1]);


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
        int fd = userEsp[3];
        uint32_t offset = userEsp[4];

        if (addr == 0) {
            if ((size & 0xFFF) != 0 || size > 0xF0000000 - 0x80000000 || size == 0)
                return 0;
            addr = VMM::pcb_table[SMP::me()]->find_space_VME(size);
            // Debug::printf("space foudn at %x\n", k);
        }
        // Debug::printf("addr %x size %x addr + size %x\n", addr, size, addr + size);
        if ((addr & 0xFFF) != 0 || (size & 0xFFF) != 0 || addr + size > 0xF0000000 ||
            (addr + size < 0x80000000))
            return 0;

        if (!VMM::pcb_table[SMP::me()]->add_new_VME(addr, size))
            return 0;
        
        if(fd == -1)
            return addr;
            
        if ((offset & 0xFFF) != 0)
            return 0;

        return VMM::pcb_table[SMP::me()]->file_mmap(addr, size, fd, offset);

        break;
    }
    case 1006: { // void sigreturn()
        // Debug::printf("SIG RETURN\n");
        sigreturn();
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
    case 1020: { // void chdir(char* path)
        char *path = (char *)userEsp[1];
        // Debug::printf("chdir %s\n", path);
        VMM::pcb_table[SMP::me()]->update_path(path);
        break;
    }
    case 1021: { // int open(char* path)
        char *path = (char *)userEsp[1];
        return VMM::pcb_table[SMP::me()]->open(path);
    }
    case 1022: { // int close(fd)
        int fd = userEsp[1];
        return VMM::pcb_table[SMP::me()]->close(fd);
    }
    case 1023: { // int len(fd)
        int fd = userEsp[1];
        return VMM::pcb_table[SMP::me()]->len(fd);
    }
    case 1024: { // int n = read(fd, void* buffer, unsigned count)
        int fd = userEsp[1];
        void *buffer = (void *)userEsp[2];
        uint32_t count = userEsp[3];
        // Debug::printf("reading %d\n", fd);

        auto cur_pcb = VMM::pcb_table[SMP::me()];
        FileDescriptor* file = cur_pcb->fd_table[fd];
        if(file == nullptr)
            return -1;
        uint8_t perm = file->permissions;
        // Debug::printf("[permission %d %d %x %x %d %d\n", perm, perm & 0b11, (uint32_t)buffer, (uint32_t)buffer + count, in_userspace((uint32_t)buffer), in_userspace((uint32_t)buffer + count));
        if ((perm & 0b11)!=0b11 || !in_userspace((uint32_t)buffer) ||
            !in_userspace((uint32_t)buffer + count))
            return -1;
        // Debug::printf("storing regs\n");
        cur_pcb->store_registers(userEip[0], userEip[3], userRegs);
        // cur_pcb->store_registers(0, userEip[3], userRegs);
        // TODO: add segfault handler case in user for buffer in VMM seg handler
        if (perm & 0b1000) { // if is file
            // Debug::printf("permiision\n");
            // Debug::printf("offset %d buffer %x\n", file.offset, buffer);
            // Debug::printf("offset %d num %d\n", file.offset, ((Node *)file.node)->size_in_bytes());
            file->lock.lock();
            int64_t val = ((Node *)file->node)->read_all(file->offset, 1, (char *)buffer);
            file->offset += 1;
            file->lock.unlock();
            if(val == 0)
                return -1;
            return val;
        } else {
            ((BoundedBuffer<void *> *)file->node)->get([cur_pcb, buffer](auto v) {
                VMM::pcb_table[SMP::me()] = cur_pcb;
                vmm_on(cur_pcb->vmm_pd);
                ((void **)buffer)[0] = v;
                cur_pcb->return_to_process(1);
            });
            event_loop();
            // return -1;
        }
    }
    case 1:
    case 1025: { // int n = write(fd, void* buffer, unsigned count)
        int fd = userEsp[1];
        void *buffer = (void *)userEsp[2];
        uint32_t count = userEsp[3];
        // if(fd != 1)
        //     Debug::printf("write %d\n", fd);

        auto cur_pcb = VMM::pcb_table[SMP::me()];
        FileDescriptor* file = cur_pcb->fd_table[fd];
        if(file == nullptr)
            return -1;
        uint8_t perm = file->permissions;
        // Debug::printf("permission %d\n", perm);
        if ((perm & 0b101)!=0b101 || !in_userspace((uint32_t)buffer) ||
            !in_userspace((uint32_t)buffer + count))
            return -1;

        if (count == 0)
            return 0;

        if (perm & 0b010000) {
            // Debug::printf("perm %s\n", (char *)userEsp[2]);
            // Debug::printf("%c", ((char *)userEsp[2])[0]);
            if (!in_userspace(userEsp[2]))
                return -1;
            for (uint32_t i = 0; i < userEsp[3]; i++)
                Debug::printf("%c", ((char *)userEsp[2])[i]);
            return userEsp[3];
            // return 1;
        } else {
            // Debug::printf("wr\n");
            // ((BoundedBuffer<void *> *)file.node)->put((((void **)userEsp[2])[0]), [](){});
            cur_pcb->store_registers(userEip[0], userEip[3], userRegs);
            ((BoundedBuffer<void *> *)file->node)->put((((void **)userEsp[2])[0]), [cur_pcb]() {
                // Debug::printf("putting return\n");
                VMM::pcb_table[SMP::me()] = cur_pcb;
                vmm_on(cur_pcb->vmm_pd);
                // Debug::printf("not these\n");
                cur_pcb->return_to_process(1);
            });
            event_loop();
            return 1;
        }
    }
    case 1026: { // int rc = pipe(int* write_fd, int* read_fd)
        int *write_fd = (int *)userEsp[1];
        int *read_fd = (int *)userEsp[2];
        return VMM::pcb_table[SMP::me()]->pipe(write_fd, read_fd);
    }
    case 1027: { // int kill(unsigned v)
        uint32_t v = userEsp[1];
        return VMM::pcb_table[SMP::me()]->kill_child(v);
    }
    case 1028: { // int dup(int fd)
        int fd = userEsp[1];
        return VMM::pcb_table[SMP::me()]->dup(fd);
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
