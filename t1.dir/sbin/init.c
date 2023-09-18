#include "libc.h"

void handler(int signum, unsigned arg) {
    printf("*** signal %d 0x%x\n", signum, arg);
    shutdown();
}

int main(int argc, char** argv) {
    simple_signal(handler);
    int volatile *p = (int volatile *) 0x100000;
    *p = 666;
    return 0;
}
