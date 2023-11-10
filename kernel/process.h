// #pragma once
#ifndef _PROCESS_H_
#define _PROCESS_H_

#include "atomic.h"
#include "bb.h"
#include "ext2.h"
#include "future.h"
#include "kernel.h"
#include "machine.h"
#include "stdint.h"
// #include "vmm.h"

class ProcessControlBlock;

class ChildrenExitCode {
  public:
    ProcessControlBlock *future;
    ChildrenExitCode *next;
    ChildrenExitCode(ProcessControlBlock *future) : future(future), next(nullptr) {}
    ChildrenExitCode(ProcessControlBlock *future, ChildrenExitCode *next)
        : future(future), next(next) {}
};

class SharedSemaphore {
  public:
    Semaphore *sem;
    Atomic<uint32_t> ref{1};
    SharedSemaphore(Semaphore *sem) : sem(sem) {}

    ~SharedSemaphore() { delete sem; }

    void increment() {
        // Debug::printf("increment\n");
        this->ref.add_fetch(1);
        // Debug::printf("value is %d\n", this->ref.get());
    }

    bool decrement() {
        auto n = this->ref.add_fetch(-1);
        // Debug::printf("decrement %d\n", n);
        return n == 0;
    }
};

class SemaphoreHolder {
  public:
    uint32_t index;
    SharedSemaphore *sem;
    SemaphoreHolder *left;
    SemaphoreHolder *right;
    SemaphoreHolder(uint32_t index, SharedSemaphore *sem)
        : index(index), sem(sem), left(nullptr), right(nullptr) {}
    SemaphoreHolder(uint32_t index, SharedSemaphore *sem, SemaphoreHolder *left)
        : index(index), sem(sem), left(left), right(nullptr) {
        this->left->right = this;
    }

    ~SemaphoreHolder() {
        if (sem->decrement()) {
            // Debug::printf("deleting internal\n");
            delete sem;
        }
    }
};

class VME {
  public:
    uint32_t addr;
    uint32_t size;
    VME *left;
    VME *right;
    VME(uint32_t addr, uint32_t size) : addr(addr), size(size), left(nullptr), right(nullptr) {}
    VME(uint32_t addr, uint32_t size, VME *left)
        : addr(addr), size(size), left(left), right(nullptr) {}
    VME(uint32_t addr, uint32_t size, VME *left, VME *right)
        : addr(addr), size(size), left(left), right(right) {}
};

class FileDescriptor {
  public:
    uint8_t permissions; // is_input[5], is_output[4], is_file [3], is_writable [2], is_readable
                         // [1], is_valid[0]
    uintptr_t node;
    uint32_t offset;
    FileDescriptor() : permissions(0), node(0), offset(0) {}
    FileDescriptor(uintptr_t node, uint8_t permissions)
        : permissions(permissions), node(node), offset(0) {}
};

class ProcessControlBlock {
  public:
    uint32_t vmm_pd;
    uint32_t registers[10]; // pc (orig), eax, ecx, edx, ebx, esp (orig), ebp, esi, edi, eflags
    uint32_t sig_registers[10];
    ProcessControlBlock *parent;
    Future<uint32_t> *exit_code;
    ChildrenExitCode *children;
    SemaphoreHolder *sem_holder;
    uint32_t num_semaphores;
    uint32_t sig_handler;
    bool is_in_sig_handler;
    uint32_t sem_index;
    VME *head;
    Node *current_node;
    FileDescriptor fd_table[10];
    Node *cur_path;
    bool is_kill;
    bool is_first_kill; // first time entering store handler
    uint32_t kill_message;

    ProcessControlBlock(uint32_t vmm_pd, ProcessControlBlock *parent)
        : vmm_pd(vmm_pd), parent(parent), children(nullptr), sig_handler(0),
          is_in_sig_handler(false), is_kill(false), is_first_kill(false) {
        exit_code = new Future<uint32_t>();
        if (parent != nullptr) {
            parent->add_child(this);
            this->sem_holder = copy_sem_parent(parent);
            this->sem_index = parent->sem_index;
            this->head = copy_vme(parent->head);
            this->is_in_sig_handler = parent->is_in_sig_handler;
            this->sig_handler = parent->sig_handler;
            this->num_semaphores = parent->num_semaphores;
            this->cur_path = parent->cur_path;
            copy_fd_table(parent);
        } else {
            this->sem_holder = nullptr;
            sem_index = 0;
            head = nullptr;
            this->num_semaphores = 0;
            if (file_system != nullptr)
                this->cur_path = file_system->root;

            fd_table[0].permissions = 0b100001;
            fd_table[1].permissions = 0b010101;
            fd_table[2].permissions = 0b010101;
        }
    }

    int kill_child(uint32_t v) {
        if (this->children == nullptr || this->children->future->is_kill) {
            return -1;
        }
        this->children->future->kill_message = v;
        this->children->future->is_kill = true;
        this->children->future->is_first_kill = true;
        return 0;
    }

    int file_mmap(uint32_t addr, uint32_t size, int fd, uint32_t offset) {
        FileDescriptor file = this->fd_table[fd];
        uint8_t perm = file.permissions;
        if (!(perm & 0b001011))
            return 0;

        Node *node = (Node *)file.node;
        node->read_all(offset, size, (char *)addr);
        // TODO: Probably wrong
        return addr;
    }

    void copy_fd_table(ProcessControlBlock *parent) {
        for (int i = 0; i < 10; i++) {
            this->fd_table[i].node = parent->fd_table[i].node;
            this->fd_table[i].permissions = parent->fd_table[i].permissions;
            this->fd_table[i].offset = parent->fd_table[i].offset;
        }
    }

    int dup(int fd) {
        int new_fd = find_available_fd();
        if (!(this->fd_table[fd].permissions & 0b1) || new_fd == -1)
            return -1;

        this->fd_table[new_fd].node = this->fd_table[fd].node;
        this->fd_table[new_fd].permissions = this->fd_table[fd].permissions;
        this->fd_table[new_fd].offset = this->fd_table[fd].offset;
        return 0;
    }

    int pipe(int *write_fd, int *read_fd) {
        int fd_for_write = find_available_fd();

        if (fd_for_write == -1)
            return -1;
        this->fd_table[fd_for_write].permissions = 0b0101;

        int fd_for_read = find_available_fd();

        if (fd_for_read == -1) {
            this->fd_table[fd_for_write].permissions = 0;
            return -1;
        }

        this->fd_table[fd_for_read].permissions = 0b0011;

        BoundedBuffer<void *> *bb = new BoundedBuffer<void *>(100);

        this->fd_table[fd_for_write].node = (uintptr_t)bb;
        this->fd_table[fd_for_read].node = (uintptr_t)bb;

        *write_fd = fd_for_write;
        *read_fd = fd_for_read;

        return 0;
    }

    void update_path(char *path) {
        if (path[0] == '/') {
            this->cur_path = file_system->find_absolute(path);
        } else {
            this->cur_path = file_system->find_relative(this->cur_path, path);
        }
    }

    int find_available_fd() {
        for (int i = 3; i < 10; i++) {
            if (!(this->fd_table[i].permissions & 0b1))
                return i;
        }
        return -1;
    }

    int open(char *path) {
        int fd = find_available_fd();
        if (fd == -1)
            return -1;

        Node *open_fd = nullptr;
        if (path[0] == '/') {
            open_fd = file_system->find_absolute(path);
        } else {
            open_fd = file_system->find_relative(this->cur_path, path);
        }

        if (open_fd == nullptr)
            return -1;

        this->fd_table[fd].node = (uintptr_t)open_fd;
        this->fd_table[fd].permissions =
            0b0001 | (open_fd->is_file() << 3) | (open_fd->is_file() << 1);
        return fd;
    }

    int close(int fd) {
        if (fd > 9)
            return -1;
        FileDescriptor file_d = this->fd_table[fd];
        uint8_t file_perm = file_d.permissions;
        if (!(file_perm & 0b1001))
            return -1;
        file_d.permissions = 0;
        // todo: clean up Node but Im lazy
        return 0;
    }

    uint32_t len(int fd) {
        FileDescriptor file_d = this->fd_table[fd];
        uint8_t file_perm = file_d.permissions;
        if (!(file_perm & 0b1001))
            return -1;

        return ((Node *)file_d.node)->size_in_bytes();
    }

    uint32_t read_file(uint32_t fd, void *buffer, uint32_t count) {
        // TODO:
        return 0;
    }

    VME *copy_vme(VME *old_vme) {
        if (old_vme == nullptr)
            return nullptr;
        VME *head = new VME(old_vme->addr, old_vme->size);
        VME *temp = head;

        while (old_vme->right != nullptr) {
            // Debug::printf("old addr %x\n", old_vme);
            old_vme = old_vme->right;
            // Debug::printf("ad %x\n", old_vme);
            temp->right = new VME(old_vme->addr, old_vme->size, temp);
            temp = temp->right;
        }

        temp = head;
        while (temp != nullptr) {
            // Debug::printf("node %x %x %x\n", temp->addr, temp->size, temp->right);
            temp = temp->right;
        }

        return head;
    }

    VME *remove_VME(uint32_t addr) {
        // Debug::printf("removing address %x\n", addr);
        if (head == nullptr)
            return nullptr;

        if (addr < head->addr)
            return nullptr;

        VME *temp = head;
        while (temp != nullptr) {
            // Debug::printf("addr %x tempaddr %x %x\n", addr, temp->addr, temp->addr + temp->size);
            if (addr >= temp->addr && addr < temp->addr + temp->size) {
                if (temp->right == nullptr && temp->left == nullptr) {
                    this->head = nullptr;
                } else if (temp->left == nullptr) {
                    this->head = temp->right;
                    temp->right->left = nullptr;
                } else if (temp->right == nullptr) {
                    temp->left->right = nullptr;
                } else {
                    temp->left->right = temp->right;
                    temp->right->left = temp->left;
                }
                // Debug::printf("remove vme new head %x %x\n", head->addr, temp->addr);
                return temp;
            }
            temp = temp->right;
        }

        return nullptr;
    }

    bool find_exist_VME(uint32_t addr) {
        // Debug::printf("finding existing VME %x\n", addr);
        if (head == nullptr)
            return false;

        if (addr < head->addr)
            return false;

        VME *temp = head;
        while (temp != nullptr) {
            // Debug::printf("ADDR %x %x, SIZE %x, addr + size %x\n", temp, temp->addr, temp->size,
            //   temp->addr + temp->size);
            if (addr >= temp->addr && addr < temp->addr + temp->size) {
                return true;
            }
            temp = temp->right;
        }
        return false;
    }

    uint32_t find_space_VME(uint32_t size) {
        // Debug::printf("size %x %d\n", size, SMP::me());
        uint32_t start_addr = 0x80000000;
        uint32_t end_addr = 0xF0000000;

        if (head == nullptr) {
            // Debug::printf("head is null\n");
            head = new VME(start_addr, size);
            return start_addr;
        }

        VME *temp = head;
        if (start_addr + size < head->addr) {
            head = new VME(start_addr, size);
            head->right = temp;
            temp->left = head;
        }

        while (temp->right != nullptr) {
            // Debug::printf("temp %x right %x addr %x %x %x\n", temp, temp->right, temp->addr,
            //   temp->size, temp->right->addr);
            if (temp->right->addr - (temp->addr + temp->size) >= size) {
                VME *next = temp->right;
                temp->right = new VME(temp->addr + temp->size, size, temp, next);
                next->left = temp->right;
                // Debug::printf("found new space of %x + %x\n", temp->addr, temp->size);
                return temp->addr + temp->size;
            }
            temp = temp->right;
        }

        if (end_addr - temp->addr - temp->size < size)
            return 0;

        temp->right = new VME(temp->addr + temp->size, size, temp);
        // Debug::printf("found new space of %x + %x2\n", temp->addr, temp->size);
        return temp->addr + temp->size;
    }

    bool add_new_VME(uint32_t addr, uint32_t size) {
        // Debug::printf("adding new VMe %x %x %x cur process %x\n", addr, size, addr+size,
        // SMP::me());
        if (head == nullptr) {
            head = new VME(addr, size);
            // Debug::printf("inserted %x\n", head);
            return true;
        } else if (addr <= head->addr) {
            if (addr + size >= head->addr)
                return false;
            VME *temp = head;
            head = new VME(addr, size);
            temp->left = head;
            head->right = temp;
            return true;
        } else {
            VME *temp = head;
            // Debug::printf("vme %x %x %x\n", temp, temp->right, temp->addr);
            while (addr > temp->addr) {
                // Debug::printf("vme %x %x\n", temp, temp->right);
                if (temp->right == nullptr) {
                    if (temp->addr + temp->size > addr) // potential off by one failure
                        return false;
                    temp->right = new VME(addr, size, temp);
                    return true;
                }
                temp = temp->right;
            }
            VME *prev = temp->left;
            // Debug::printf("prev %x\n", prev);
            if (addr < prev->addr + prev->size || addr + size > temp->addr) {
                return false;
            }

            temp->left = new VME(addr, size, prev, temp);
            prev->right = temp->left;

            // temp = head;
            // while (temp != nullptr) {
            //     Debug::printf("node %x %x %x\n", temp->addr, temp->size, temp->right);
            //     temp = temp->right;
            // }

            // Debug::printf("ending add\n");
            return true;
        }
    }

    SemaphoreHolder *copy_sem_parent(ProcessControlBlock *parent) {
        SemaphoreHolder *par_sem = parent->sem_holder;
        if (par_sem == nullptr)
            return nullptr;
        par_sem->sem->increment();
        SemaphoreHolder *child_sem = new SemaphoreHolder(par_sem->index, par_sem->sem);
        SemaphoreHolder *tail = child_sem;
        par_sem = par_sem->right;
        while (par_sem != nullptr) {
            par_sem->sem->increment();
            SemaphoreHolder *tmp = new SemaphoreHolder(par_sem->index, par_sem->sem, tail);
            tail = tmp;
            par_sem = par_sem->right;
        }

        // SemaphoreHolder *parent_temp = parent->sem_holder;

        // while (parent_temp != nullptr) {
        //     Debug::printf("parent %d %x %x %x %d\n", parent_temp->index, parent_temp,
        //     parent_temp->sem,
        //                   parent_temp->sem->sem, parent_temp->sem->ref.get());
        //     parent_temp = parent_temp->right;
        // }

        // SemaphoreHolder *temp = child_sem;
        // while (temp != nullptr) {
        //     Debug::printf("temp %d %x %x %x %d\n", temp->index, temp, temp->sem, temp->sem->sem,
        //     temp->sem->ref.get()); temp = temp->right;
        // }

        return child_sem;
    }

    void close_all_sempahores() {
        SemaphoreHolder *next_sem = this->sem_holder;
        while (next_sem != nullptr) {
            SemaphoreHolder *cur_sem = next_sem;
            next_sem = next_sem->right;
            // Debug::printf("deleting semaphore %d %x\n", cur_sem->index, cur_sem->sem);
            delete cur_sem;
        }
    }

    void close_all_VME() {
        VME *current = head;
        while (current != nullptr) {
            VME *temp = current;
            current = current->right;
            delete temp;
        }
    }

    void close_all_children() {
        ChildrenExitCode *current = children;
        while (current != nullptr) {
            ChildrenExitCode *temp = current;
            current = current->next;
            delete temp;
        }
    }

    uint32_t close_semaphore(uint32_t index) {
        // Debug::printf("closing sem %d\n", index);
        SemaphoreHolder *cur_sem = this->sem_holder;
        while (cur_sem != nullptr) {
            // Debug::printf("cur index %d\n", cur_sem->index);
            if (cur_sem->index == index) {
                // Debug::printf("DELETING SEMAPHORE\n");
                if (cur_sem == this->sem_holder)
                    this->sem_holder = this->sem_holder->right;

                if (cur_sem->right != nullptr)
                    cur_sem->right->left = cur_sem->left;

                if (cur_sem->left != nullptr)
                    cur_sem->left->right = cur_sem->right;

                delete cur_sem;

                // if(this->sem_holder == nullptr)
                //     Debug::printf("nullptr sem remianing\n");

                // Debug::printf("closed\n");
                this->num_semaphores--;
                return 0;
            }
            cur_sem = cur_sem->right;
        }
        return -1;
    }

    uint32_t create_semaphore(uint32_t size) {
        if (this->num_semaphores == 100)
            return -1;
        Semaphore *sem = new Semaphore(size);
        // Debug::printf("semcreated %x\n", sem);
        SharedSemaphore *shared_sem = new SharedSemaphore(sem);
        // Debug::printf("creationg of new sem %d %x %d\n", this->sem_index, shared_sem,
        // shared_sem->ref.get());
        SemaphoreHolder *holder = new SemaphoreHolder(this->sem_index, shared_sem);
        holder->right = this->sem_holder;
        if (this->sem_holder != nullptr)
            this->sem_holder->left = holder;
        this->sem_holder = holder;
        // Debug::printf("index created at %d\n", this->sem_index);
        this->num_semaphores++;
        return this->sem_index++;
    }

    Semaphore *retrieve_semaphore(uint32_t index) {
        // Debug::printf("index %d\n", index);
        SemaphoreHolder *cur_sem = this->sem_holder;
        while (cur_sem != nullptr) {
            if (cur_sem->index == index) {
                // Debug::printf("current index %d %x %x\n", cur_sem->index, cur_sem->sem,
                //   cur_sem->sem->sem);
                return cur_sem->sem->sem;
            }
            cur_sem = cur_sem->right;
        }
        // Debug::printf("nullptr\n");
        return nullptr;
    }

    void store_registers(uint32_t pc, uint32_t esp, uint32_t *user_registers) {
        if (this->is_in_sig_handler) {
            sig_registers[0] = pc;
            sig_registers[1] = user_registers[7]; // eax
            sig_registers[2] = user_registers[6];
            sig_registers[3] = user_registers[5]; // edx
            sig_registers[4] = user_registers[4];
            sig_registers[5] = esp; // esp
            sig_registers[6] = user_registers[2];
            sig_registers[7] = user_registers[1];
            sig_registers[8] = user_registers[0]; // edi
            sig_registers[9] = user_registers[8];
        } else {
            registers[0] = pc;
            registers[1] = user_registers[7]; // eax
            registers[2] = user_registers[6];
            registers[3] = user_registers[5]; // edx
            registers[4] = user_registers[4];
            registers[5] = esp; // esp
            registers[6] = user_registers[2];
            registers[7] = user_registers[1];
            registers[8] = user_registers[0]; // edi
            registers[9] = user_registers[8];
        }
    }

    void exit_process();

    void return_to_process(uint32_t eax) {
        // Debug::printf("returning process sig %d\n", this->is_in_sig_handler);
        if (this->is_first_kill) {
            this->is_first_kill = false;
            this->is_in_sig_handler = true;
            uint32_t sig_pc = this->sig_handler;
            if (sig_pc == 0) 
                exit_process();
            else
                switchToUserWithParams(sig_pc, registers[6] - 128, 2, this->kill_message);
        }

        if (this->is_in_sig_handler) {
            return_to_process_(eax, sig_registers);
            Debug::panic("*** THIS SHOULD NOT OUTPUT\n");
        }

        // for(int i = 0; i < 10; i++)
        //     Debug::printf("reg %d val %x\n", i, registers[i]);

        return_to_process_(eax, registers);
    }

    void set_exit(uint32_t exit_num) {
        // Debug::printf("exit s\n");
        // Debug::printf("meml oc %x\n", &this->exit_code);
        this->exit_code->set(exit_num);

        close_all_sempahores();
        close_all_children();
        close_all_VME();

        // Debug::printf("end set exit\n");
    }

    void add_child(ProcessControlBlock *future) {
        if (this->children == nullptr) {
            this->children = new ChildrenExitCode(future);
        } else {
            this->children = new ChildrenExitCode(future, this->children);
        }
    }

    Future<uint32_t> *get_child() {
        ChildrenExitCode *cur = this->children;
        this->children = this->children->next;
        Future<uint32_t> *fut = cur->future->exit_code;
        delete cur;
        return fut;
    }
};

#endif