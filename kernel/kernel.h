#ifndef _KERNEL_H_
#define _KERNEL_H_

#include "future.h"
#include "ext2.h"

extern Ext2 * file_system;
Future<int> kernelMain(void);

#endif
