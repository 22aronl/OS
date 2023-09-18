#pragma once

#include "stdint.h"
#include "atomic.h"
#include "queue.h"
#include "events.h"

#include <coroutine>

class Semaphore {
    uint64_t count;
    SpinLock lock{};
    Queue<impl::Event, NoLock> waiting{};

public:
    explicit Semaphore(const uint32_t count): count(count) {}
    Semaphore(Semaphore&) = delete;
    Semaphore& operator=(Semaphore& rhs) = delete;

    void up();

    template <typename Work>
    void down(const Work& work) {
        auto e = new impl::EventWithWork(work);
        lock.lock();
        if (count > 0) {
            count --;
            lock.unlock();
            impl::ready_queue.add(e);
        } else {
            waiting.add(e);
            lock.unlock();
        }
    }

    ///////////////////////////////////
    // A sempahore is also awaitable //
    ///////////////////////////////////

    // Check if we can do it quickly -- a strict performance optimization.
    bool await_ready() noexcept {
        lock.lock();
        if (count > 0) {
            count --;
            lock.unlock();
            return true;
        } else {
            lock.unlock();
            return false;
        }
    }

    // Called to block when await_ready returns false
    // notice that we don't hold the lock so we have to check again
    void await_suspend(std::coroutine_handle<> handle) noexcept {
        down(handle);
    }

    void await_resume() noexcept {
    }
};
