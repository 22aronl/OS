#pragma once

#include "stdint.h"
#include "queue.h"
#include "pit.h"
#include "shared.h"

#include <coroutine>

// Implementation details, we use a namespace to protect against
// accidental direct use in test cases
namespace impl {

    // implementation hints, feel free to use, remove, replace, enhance, ...

    struct Event {
        Event* next = nullptr;
        virtual void doit() = 0;
        virtual ~Event() {}
    };

    template <typename Work>
    struct EventWithWork: public Event {
        const Work work;
        explicit inline EventWithWork(const Work& work): work(work) {}
        void doit() override {
            work();
        }
    };

    template <typename T>
    struct EventWithArg: public Event {
        T value;
        explicit inline EventWithArg(const T& value): value(value) {}
        void update_value(const T& v) {
            value = v;
        }

        T get_value() {
            return value;
        }

        virtual void doit() override {

        }

        virtual bool has_work() {
            return false;
        }
    };

    template <typename Work, typename T>
    struct EventWithWorkArg: public EventWithArg<T> {
        const Work work;
        explicit inline EventWithWorkArg(const Work& work, const T& value): EventWithArg<T>(value), work(work) {}

        virtual void doit() override {
            work(this->value);
        }
    };

    template <typename Work, typename T>
    struct EventWithOptionalWorkArg: public EventWithArg<T> {
        const Work work;
        bool has_work_var = true;
        EventWithOptionalWorkArg(const Work& work, const T& value): EventWithArg<T>(value), work(work) {}
        EventWithOptionalWorkArg(const T& value): EventWithArg<T>(value), work(nullptr) {
            has_work_var = false;
        }

        Work get_work() {
            return work;
        }

        virtual bool has_work() override {
            return has_work_var;
        }

        virtual void doit() override {
            work();
        }
    };

    extern Queue<Event, SpinLock> ready_queue;

    template <typename Work>
    void run_at(const uint32_t at, const Work& work) {
        if (at > Pit::jiffies) {
            auto e = new EventWithWork([at, work] {
                run_at(at, work);
            });
            ready_queue.add(e);
        } else {
            auto e = new EventWithWork(work);
            ready_queue.add(e);
        } 
    }

    extern void timed(const uint32_t at, Event* e);
}

/******************/
/* The public API */
/******************/

// Schedules some conccurent work in the future.
// No sooner than "ms" milli-seoncs
template <typename Work>
inline void go(const Work& work, uint32_t const delay=0) {
    auto e = new impl::EventWithWork(work);
    if (delay == 0) {
        impl::ready_queue.add(e);
    } else {
        timed(Pit::jiffies+delay+1, e);
    }
}

// Called in "init.cc" when a core is idle. Beware of stack overflow
extern void event_loop();

class co_delay {
    uint32_t const ms;
public:
    explicit co_delay(uint32_t ms): ms(ms) {}

    bool await_ready() {
        return false;
    }

    void await_suspend(std::coroutine_handle<void> handle) noexcept {
        go(handle,ms);
    }

    void await_resume() noexcept {
    }
};
