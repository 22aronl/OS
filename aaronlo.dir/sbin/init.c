#include "libc.h"

extern unsigned _end;
#define LONG_LOOP 10000

void handler(int signum, unsigned arg) {
    printf("*** signal %d 0x%x\n", signum, arg);
    if (signum == 1) {
        void* map_at = (void*)((arg >> 12) << 12);
        void* p = simple_mmap(map_at, 4096, -1, 0);
        if (p != map_at) {
            printf("*** failed to map 0x%x\n", arg);
        } else {
            printf("*** NO SIGRETURN\n");
        }
    } else if(signum == 2) {
        if(arg != 10) {
            printf("*** failed to give right argument %d\n", arg);
            shutdown();
        }

        for(int i = 0; i < LONG_LOOP; i++) { //try to force preemption in kernel
            yield();
            printf("time slow %d\n", i);
        }
        exit(100);
    }
}

int main(int argc, char** argv) {
    simple_signal(handler); //normal simple signal handler
    unsigned p = (unsigned) &_end;
    p = p + 4096;
    p = (p >> 12) << 12;
    unsigned volatile* ptr = (unsigned volatile*) p;
    *ptr = 666;
    printf("*** did it work? %d\n", *ptr);
    
    int s = sem(0);
    int id = fork();

    if(id < 0) {
        shutdown();
    } else if(id == 0) {
        up(s);
        while(1) {}
    } else if(id > 0) {
        down(s);
        printf("*** kill command\n");
        kill(10);
        int j = join();

        if(j != 100) {
            printf("*** incorrect input\n");
            shutdown();
        }

        printf("*** success\n");
        shutdown();
    }


    return 0;
}
