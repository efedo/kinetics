#pragma once
// Coroutine tasks for the classic interface.
//
// The original program was written for DOS: it called getch() and waited.
// Here every function that waits for a key is a coroutine returning Task<T>,
// and a call that waited becomes `co_await f(...)`. The host resumes the
// program when a key arrives, so the same code runs inside the desktop
// event loop and the browser's animation loop, with no threads.
//
// Arguments passed by reference stay valid because every task is awaited at
// once by its caller, whose frame outlives it.

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace classic {

namespace detail {

struct PromiseBase {
    std::coroutine_handle<> continuation = std::noop_coroutine();

    struct FinalAwaiter {
        bool await_ready() noexcept { return false; }
        template <typename P>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<P> h) noexcept {
            return h.promise().continuation;
        }
        void await_resume() noexcept {}
    };

    std::suspend_always initial_suspend() noexcept { return {}; }
    FinalAwaiter final_suspend() noexcept { return {}; }
    void unhandled_exception() { std::terminate(); }
};

}  // namespace detail

template <typename T = void>
class [[nodiscard]] Task {
public:
    struct promise_type : detail::PromiseBase {
        std::optional<T> value;
        Task get_return_object() { return Task(std::coroutine_handle<promise_type>::from_promise(*this)); }
        void return_value(T v) { value = std::move(v); }
    };

    Task(Task&& o) noexcept : h_(std::exchange(o.h_, {})) {}
    Task& operator=(Task&& o) noexcept {
        if (this != &o) {
            if (h_) h_.destroy();
            h_ = std::exchange(o.h_, {});
        }
        return *this;
    }
    Task(const Task&) = delete;
    ~Task() {
        if (h_) h_.destroy();
    }

    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
        h_.promise().continuation = caller;
        return h_;
    }
    T await_resume() { return std::move(*h_.promise().value); }

private:
    explicit Task(std::coroutine_handle<promise_type> h) : h_(h) {}
    std::coroutine_handle<promise_type> h_;
};

template <>
class [[nodiscard]] Task<void> {
public:
    struct promise_type : detail::PromiseBase {
        Task get_return_object() { return Task(std::coroutine_handle<promise_type>::from_promise(*this)); }
        void return_void() {}
    };

    Task(Task&& o) noexcept : h_(std::exchange(o.h_, {})) {}
    Task& operator=(Task&& o) noexcept {
        if (this != &o) {
            if (h_) h_.destroy();
            h_ = std::exchange(o.h_, {});
        }
        return *this;
    }
    Task(const Task&) = delete;
    ~Task() {
        if (h_) h_.destroy();
    }

    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
        h_.promise().continuation = caller;
        return h_;
    }
    void await_resume() {}

    // For the top-level task, which nobody awaits: start it, and ask
    // whether it has finished.
    void start() {
        if (h_ && !h_.done()) h_.resume();
    }
    bool done() const { return !h_ || h_.done(); }

private:
    explicit Task(std::coroutine_handle<promise_type> h) : h_(h) {}
    std::coroutine_handle<promise_type> h_;
};

}  // namespace classic
