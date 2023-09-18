#include "libc.h"

#define ASSERT(c) do {                                       \
    if (!(c)) {                                              \
        printf("*** failure at %s:%d\n",__FILE__, __LINE__); \
        exit(-1);                                            \
    }                                                        \
} while (0)
    

int checked_fork(char* file, int line) {
    int id = fork();
    if (id < 0) {
        printf("*** fork failed at %s:%d\n", file, line);
    }
    return id;
}

#define FORK() checked_fork(__FILE__, __LINE__)

int main(int argc, char** argv) {
    printf("*** (1) checking normal exit\n");
    if (FORK() == 0) {
        /* child */
        printf("*** child1\n");
        exit(42);
    }
    printf("*** parent1 %d\n", join());

    printf("*** (2) checking segmentation violation\n");
    if (FORK() == 0) {
        /* child */
        printf("*** child2\n");
        *((int*) 666) = 13;
    }
    printf("*** parent2 %d\n", join());
    printf("*** va2 %d\n", *((int*) 0xF0000800));

    printf("*** (3) checking preemption\n");
    if (FORK() == 0) {
        /* child */
        printf("*** child3\n");
        if (FORK() == 0) {
            /* grand child */
            while(1);
        }
        exit(777);
    }
    printf("*** parent3 %d\n", join());

    printf("*** (4) checking semaphores\n");
    unsigned int s = sem(0);
    printf("*** s = %d\n", s);
    ASSERT(s >= 0);
    if (FORK() == 0) {
        /* child */
        printf("*** child4\n");
        exit(up(s));
    }
    int rc = down(s);
    printf("*** parent4, rc=%d\n", rc);
    printf("*** parent4, join=%d\n", join());

    printf("*** parent4, bad sem descriptor -> %d\n", down(1000));

    rc = sem_close(s);
    printf("*** parent4, first sem_close(%d) -> %d\n", s, rc);

    rc = sem_close(s);
    printf("*** parent4, second sem_close(%d) -> %d\n", s, rc);

    shutdown();
    return 0;
}
