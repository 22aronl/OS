#include "debug.h"
#include "ide.h"
#include "ext2.h"
#include "elf.h"
#include "machine.h"
#include "libk.h"
#include "config.h"
#include "future.h"
#include "vmm.h"
#include "process.h"
#include <coroutine>

Node* checkFile(const char* name, Node* node) {
    CHECK(node != nullptr);
    CHECK(node->is_file());
    Debug::printf("file %s is ok\n",name);
    return node;
}

Node* getFile(Ext2* fs, Node* node, const char* name) {
    return checkFile(name,fs->find(node,name));
}

Node* checkDir(const char* name, Node* node) {
    Debug::printf("checking %s\n",name);
    CHECK (node != nullptr);
    CHECK (node->is_dir());
    Debug::printf("directory %s is ok\n",name);
    return node;
}

Node* getDir(Ext2* fs, Node* node, const char* name) {
    return checkDir(name,fs->find(node,name));
}

Ext2* file_system;

Future<int> kernelMain(void) {
    auto d = new Ide(1);
    Debug::printf("mounting drive 1\n");
    auto fs = new Ext2(d);
    file_system = fs;
    auto root = checkDir("/",fs->root);
    auto sbin = getDir(fs,root,"sbin");
    auto init = getFile(fs,sbin,"init");
    VMM::pcb_table[SMP::me()] = new ProcessControlBlock(getCR3(), nullptr);
    Debug::printf("loading init\n");
    uint32_t e = ELF::load(init);
    Debug::printf("entry %x\n",e);
    auto userEsp = 0xF0000000 - 4;
    Debug::printf("user esp %x\n",userEsp);
    // Current state:
    //     - %eip points somewhere in the middle of kernelMain
    //     - %cs contains kernelCS (CPL = 0)
    //     - %esp points in the middle of the thread's kernel stack
    //     - %ss contains kernelSS
    //     - %eflags has IF=1
    // Desired state:
    //     - %eip points at e
    //     - %cs contains userCS (CPL = 3)
    //     - %eflags continues to have IF=1
    //     - %esp points to the bottom of the user stack
    //     - %ss contain userSS
    // User mode will never "return" from switchToUser. It will
    // enter the kernel through interrupts, exceptions, and system
    // calls.

    userEsp -= 32;
    uint32_t* new_stack_ptr = reinterpret_cast<uint32_t*>(userEsp);
    new_stack_ptr[0] = 1;
    new_stack_ptr[1] = (uint32_t)&new_stack_ptr[2];
    new_stack_ptr[2] = (uint32_t)&new_stack_ptr[4];
    new_stack_ptr[3] = 0;
    
    memcpy((void*)new_stack_ptr[2], "/sbin/init", 11);

    Debug::printf("switch to user %x %x %x\n",&switchToUser, e, userEsp);

    switchToUser(e,userEsp,0);
    Debug::panic("*** implement switchToUser in machine.S\n");
    co_return -1;
}
