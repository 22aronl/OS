#include "semaphore.h"
#include "debug.h"

using namespace impl;

void Semaphore::up() {
    lock.lock();
    auto e = waiting.remove();
    if (e != nullptr) {
        ASSERT(count == 0);
        ready_queue.add(e);
        lock.unlock();
    } else {
        count += 1;
        lock.unlock();
    }
}