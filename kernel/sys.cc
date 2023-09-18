#include "sys.h"
#include "stdint.h"
#include "idt.h"
#include "debug.h"
#include "machine.h"
#include "events.h"
#include "physmem.h"
#include "machine.h"

extern "C" int sysHandler(uint32_t eax, uint32_t *frame) {
    //auto userEsp = (uint32_t*) frame[3];
    //auto userEip = frame[0];
    
    switch(eax) {

    case 7:
        Debug::shutdown();
        return -1;
        
    default:
        Debug::panic("syscall %d\n",eax);
    }

    return 0;
}

void SYS::init(void) {
    IDT::trap(48,(uint32_t)sysHandler_,3);
}
