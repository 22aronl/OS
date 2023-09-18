#include "libc.h"

extern unsigned _end;

void handler(int signum, unsigned arg) {
    printf("*** signal %d 0x%x\n", signum, arg);
    if (signum == 1) {
        /* major page fault */
        void* map_at = (void*)((arg >> 12) << 12);
        void* p = simple_mmap(map_at, 4096);
        if (p != map_at) {
            printf("*** failed to map 0x%x\n", arg);
        } else {
            sigreturn();
        }
    }
    shutdown();
}

int main(int argc, char** argv) {
    simple_signal(handler);
    unsigned p = (unsigned) &_end;
    p = p + 4096;
    p = (p >> 12) << 12;
    unsigned volatile* ptr = (unsigned volatile*) p;
    *ptr = 666;
    printf("*** did it work? %d\n", *ptr);
    shutdown();
    return 0;
}
