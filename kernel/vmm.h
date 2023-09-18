#ifndef _VMM_H_
#define _VMM_H_

namespace VMM {

    // Called (on the initial core) to initialize data structures, etc
    extern void global_init();

    // Called on each core to do per-core initialization
    extern void per_core_init();

}

#endif
