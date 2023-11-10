#pragma once

#include "semaphore.h"
#include "atomic.h"
#include "events.h"

// A bounded buffer is a generalization of a channel
// that has a buffer size "n > 0"
template <typename T>
class BoundedBuffer {
    T* buffer;
    Atomic<uint32_t> head;
    Atomic<uint32_t> tail;
    const uint32_t size;

    Semaphore s1;
    Semaphore s2;

public:
    // construct a BB with a buffer size of n
    BoundedBuffer(uint32_t n): head(0), tail(0), size(n), s1(0), s2(n) {
        buffer = new T[n];
    }
    BoundedBuffer(const BoundedBuffer&) = delete;

    ~BoundedBuffer() {
        delete[] buffer;
    }

    // When room is available in the buffer
    //    - put "v" in the next slot
    //    - schedule a call "work()"
    // Returns immediately
    template <typename Work>
    void put(T v, const Work& work) {
        s2.down([this, v, work] {
            // Debug::printf("putting %d at index %d\n", v, tail.get());
            uint32_t t = tail.fetch_add(1);
            buffer[t % size] = v;
            s1.up();
            //tail.set((tail.get() + 1) % size);
            impl::ready_queue.add(new impl::EventWithWork(work));
        });
    }

    // When the buffer is not empty
    //    - remove the first value "v"
    //    - schedule a call to "work(v)"
    // Returns immediately
    template <typename Work>
    void get(const Work& work) {
        s1.down([this, work] {
            uint32_t h = head.fetch_add(1);
            T v = buffer[h % size];
            // Debug::printf("grabbing %d from %d\n", v, head.get());
            //head.set((head.get() + 1) % size);
            s2.up();
            impl::ready_queue.add(new impl::EventWithWorkArg(work, v));
        });
        
    }
};

