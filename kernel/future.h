#pragma once

#include "semaphore.h"

// A future represents a value of type "T" that will be ready in the "future"
// Small and memory safe. Intended to be passed by value
template <typename T>
class Future {
    struct State {
        Semaphore sem{0};
        volatile T value{};
    };
    Shared<State> state;
public:
    Future(): state{Shared<State>::make()} {}
    Future(Shared<State> state): state(state) {}
    Future(Future const& rhs): state{rhs.state} {}
    Future& operator=(Future const& rhs) {
        state = rhs.state;
        return *this;
    }

    // Sets the value, should be called at most once a given semaphore instance
    // Calling it:
    //     0 times --> callers of get will block indefinitely
    //     1 times --> callers of get will all eventaully unblock
    //     >1 times --> undefined
    void set(const T& v) {
        state->value = v;
        state->sem.up();
    }

    // Schedules work to run when the value is ready (set has been called)
    // "work" must be a callable object with signature:
    //      void(T)
    template <typename Work>
    void get(const Work& work) {
        state->sem.down([work, state=state] {
            state->sem.up();
            work(state->value);
        });
    }

    //////////////////////////////////
    // The C++ coroutines interface //
    //////////////////////////////////

    // The promise, manages the internal coroutine state
    struct promise_type {
        Shared<State> state = Shared<State>::make();

        // Run until first blocking attempt
        std::suspend_never initial_suspend() noexcept {
            return {};
        }

        // Let the world know that we're done then proceed to cleanup
        std::suspend_never final_suspend() noexcept {
            state->sem.up();
            return {};
        }

        // This is what gets returned to the caller
        Future<T> get_return_object() noexcept {
            return Future<T>(state);
        }

        // Value is ready
        void return_value(T const& value) noexcept {
            state->value = value;
        }

        // handle uncaught exceptions
        void unhandled_exception() noexcept {
        }
    };

    // The awaitable API
    bool await_ready() noexcept {
        // Try to be efficient, no need to save state and suspend if the value is ready
        auto out = state->sem.await_ready();
        if (out) {
            state->sem.up();
        }
        // false => suspend by saving state then calling await_suspend
        return out;
    }

    // await_ready returned false, we know we need to suspend ... even if the
    // value raced ahead of us and became ready
    void await_suspend(std::coroutine_handle<void> handle) noexcept {
        state->sem.down([handle,state=state] {
            state->sem.up();
            handle();
        });
    }

    // This is what gets returned to someone who calls co_await
    T await_resume() noexcept {
        return state->value;
    }
};


