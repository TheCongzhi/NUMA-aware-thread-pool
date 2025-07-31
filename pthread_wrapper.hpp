/*
* Author:       Congzhi
* Create:       2025-06-11
* Description:  A pthread wrapper for cross-platform compatibility. 
* License:      MIT License
*/

#ifndef PTHREAD_WRAPPER_HPP
#define PTHREAD_WRAPPER_HPP

#if defined(__APPLE__) || defined(__linux__)
#include <pthread.h> // For pthreads -> Thread management.
#include <unistd.h>  // For sysconf -> Get number of underlying processors.
#include <sched.h>   // For sched_yield -> Yield the current thread.

#include <functional>
#include <utility>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <tuple>
#include <cassert>
#include <chrono>

namespace congzhi {

class Mutex {
private:
    pthread_mutex_t mutex_handle_;
public:
    // Constructor - initializes the mutex.
    Mutex() {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_DEFAULT);
        const int result = pthread_mutex_init(&mutex_handle_, &attr);
        if (result != 0) {
            throw std::runtime_error("pthread_mutex_init failed: " + std::string(strerror(result)));
        }
        pthread_mutexattr_destroy(&attr);
    }

    // Destructor - destroys the mutex.
    ~Mutex() noexcept {
        pthread_mutex_destroy(&mutex_handle_);
    }

    // Copying and moving are not allowed.
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex(Mutex&&) = delete;
    Mutex& operator=(Mutex&&) = delete;

    // Lock the mutex.
    void Lock() {
        const int result = pthread_mutex_lock(&mutex_handle_);
        if (result != 0) {
            throw std::runtime_error("pthread_mutex_lock failed: " + std::string(strerror(result)));
        }
    }

    // Unlock the mutex.
    void Unlock() noexcept {
        pthread_mutex_unlock(&mutex_handle_);
    }

    // Try to lock the mutex.
    bool TryLock() noexcept {
        return (pthread_mutex_trylock(&mutex_handle_) == 0);
    }

    // Returns a pointer to the native handle of the mutex.
    pthread_mutex_t* NativeHandle() noexcept {
        return &mutex_handle_;
    }
};

template <typename TLock>
class LockGuard {
private:
    TLock& lk_;
public:
    // Constructor - locks the mutex.
    explicit LockGuard(TLock& lk) : lk_(lk) {
        lk_.Lock();
    }

    // Destructor - unlocks the mutex.
    ~LockGuard() noexcept {
        lk_.Unlock();
    }

    // Copying and moving is not allowed.
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
    LockGuard(LockGuard&&) = delete;
    LockGuard& operator=(LockGuard&&) = delete;
};

class ConditionVariable {
private:
    pthread_cond_t cond_handle_;
public:
    // Constructor - initializes the condition variable.
    ConditionVariable() {
        const int result = pthread_cond_init(&cond_handle_, nullptr); 
        if (result != 0) {
            throw std::runtime_error("pthread_cond_init failed: " + std::string(strerror(result)));
        }
    }

    // Destructor - destroys the condition variable.
    ~ConditionVariable() noexcept {
        pthread_cond_destroy(&cond_handle_);
    }

    // Copying is not allowed.
    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;
    // Moving is not allowed.
    ConditionVariable(ConditionVariable&&) = delete;
    ConditionVariable& operator=(ConditionVariable&&) = delete;

    // Wait for the condition variable to be notified, using a mutex.
    void Wait(Mutex& mtx) {
        const int result = pthread_cond_wait(&cond_handle_, mtx.NativeHandle());
        if (result != 0) {
            throw std::runtime_error("pthread_cond_wait failed: " + std::string(strerror(result)));
        }
    }

    // Wait with predicates, using a mutex and a predicate function.
    template <typename Predicate>
    void Wait(Mutex& mtx, Predicate pred) {
        while (!pred()) {
            const int result = pthread_cond_wait(&cond_handle_, mtx.NativeHandle());
            if (result != 0) {
                throw std::runtime_error("pthread_cond_wait failed with predicate: " + std::string(strerror(result)));
            }
        }
    }

    // Wait for the condition variable to be notified with a timeout.
    template <typename Rep, typename Period>
    void WaitFor(Mutex& mtx, const std::chrono::duration<Rep, Period>& rel_time) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts); // Get the current time
        auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(rel_time).count();
        
        ts.tv_sec += nanoseconds / 1'000'000'000;
        ts.tv_nsec += nanoseconds % 1'000'000'000;
        
        if (ts.tv_nsec >= 1'000'000'000) {
            ts.tv_sec += ts.tv_nsec / 1'000'000'000;
            ts.tv_nsec %= 1'000'000'000;
        }
        const int result = pthread_cond_timedwait(&cond_handle_, mtx.NativeHandle(), &ts);
        if (result != 0 && result != ETIMEDOUT) {
            throw std::runtime_error("pthread_cond_timedwait failed(time wait): " + std::string(strerror(result)));
        }
    }

    // Wait for the condition variable to be notified with a timeout, using a predicate.
    template <typename Predicate, typename Rep, typename Period>
    void WaitFor(Mutex& mtx, Predicate pred, const std::chrono::duration<Rep, Period>& rel_time) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts); // Get the current time
        auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(rel_time).count();

        ts.tv_sec += nanoseconds / 1'000'000'000;
        ts.tv_nsec += nanoseconds % 1'000'000'000;

        if (ts.tv_nsec >= 1'000'000'000) {
            ts.tv_sec += ts.tv_nsec / 1'000'000'000;
            ts.tv_nsec %= 1'000'000'000;
        }

        while (!pred()) {
            const int result = pthread_cond_timedwait(&cond_handle_, mtx.NativeHandle(), &ts);
            if (result != 0 && result != ETIMEDOUT) {
                throw std::runtime_error("pthread_cond_timedwait failed with predicate: " + std::string(strerror(result)));
            }
            if (result == ETIMEDOUT) {
                break; // Exit if timed out
            }
        }
    }

    // Wait for the condition variable to be notified until a specific time point.
    template <typename Clock, typename Duration>
    void WaitUntil(Mutex& mtx, const std::chrono::time_point<Clock, Duration>& abs_time) {
        struct timespec ts;
        auto time_since_epoch = abs_time.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time_since_epoch).count();
        auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(time_since_epoch).count() % 1'000'000'000;
        
        ts.tv_sec = static_cast<time_t>(seconds);
        ts.tv_nsec = static_cast<long>(nanoseconds);
        
        const int result = pthread_cond_timedwait(&cond_handle_, mtx.NativeHandle(), &ts);
        if (result != 0 && result != ETIMEDOUT) {
            throw std::runtime_error("pthread_cond_timedwait failed(wait until): " + std::string(strerror(result)));
        }
    }

    // Wait for the condition variable to be notified until a specific time point, using a predicate.
    template <typename Predicate, typename Clock, typename Duration>
    void WaitUntil(Mutex& mtx, Predicate pred, const std::chrono::time_point<Clock, Duration>& abs_time) {
        struct timespec ts;
        auto time_since_epoch = abs_time.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time_since_epoch).count();
        auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(time_since_epoch).count() % 1'000'000'000;

        ts.tv_sec = static_cast<time_t>(seconds);
        ts.tv_nsec = static_cast<long>(nanoseconds);

        while (!pred()) {
            const int result = pthread_cond_timedwait(&cond_handle_, mtx.NativeHandle(), &ts);
            if (result != 0 && result != ETIMEDOUT) {
                throw std::runtime_error("pthread_cond_timedwait failed with predicate(wait until): " + std::string(strerror(result)));
            }
            if (result == ETIMEDOUT) {
                break; // Exit if timed out
            }
        }
    }

    // Notify one waiting thread.
    void NotifyOne() noexcept{
        pthread_cond_signal(&cond_handle_);
    }

    // Notify all waiting threads.
    void NotifyAll() noexcept {
        pthread_cond_broadcast(&cond_handle_);
    }

    // Returns a pointer to the native handle of the condition variable.
    pthread_cond_t* NativeHandle() noexcept {
        return &cond_handle_;
    }

};

class Thread {
public:
    // Thread states
    enum class ThreadState {
        JOINABLE,
        DETACHED,
        FINISHED,
        UNCREATED // Thread not created or in an invalid state
    };

    // Get the string representation of the thread state, might be useful for debugging.
    // Uncomment this if you want to use it.
    const char* GetThreadState() noexcept {
        congzhi::LockGuard<congzhi::Mutex> lock(mutex_);
        switch (thread_state_) {
            case ThreadState::JOINABLE: return "JOINABLE";
            case ThreadState::DETACHED: return "DETACHED";
            case ThreadState::FINISHED: return "FINISHED";
            case ThreadState::UNCREATED: return "UNCREATED";
            default: return "UNKNOWN";
        }
    }

    // Checks if the thread is joinable.
    bool Joinable() const noexcept {
        congzhi::LockGuard<congzhi::Mutex> lock(mutex_);
        return thread_state_ == ThreadState::JOINABLE;
    }

private:
    pthread_t thread_handle_; // Native thread handle.
    ThreadState thread_state_; // Current state of the thread.
    mutable congzhi::Mutex mutex_; // A mutable mutex for thread safety(M&M rule).

    // Base class for thread data
    struct ThreadDataBase {
        virtual ~ThreadDataBase() = default;
        virtual void Execute() = 0;
    };

    // Concrete thread data class that holds the callable - implementation of a wrapper for a callable object.
    template <typename TFunc>
    struct ThreadData : ThreadDataBase {
        std::unique_ptr<Thread> thread_ptr_; // Pointer to the thread object.
        TFunc callable_;
        ThreadData(TFunc&& func) 
            : callable_(std::forward<TFunc>(func)) {}
        void Execute() override {
            callable_();
        }
    };

    // Thread entry point function.
    static void* ThreadEntry(void* arg) {
        std::unique_ptr<ThreadDataBase> data(static_cast<ThreadDataBase*>(arg));
        try {
            data->Execute();
        } catch (...) {
            // Log or handle exceptions here if needed.
        }
        return nullptr;
    }

    // Cleanup function to ensure proper resource management.
    void Cleanup() noexcept {
        if (thread_state_ == ThreadState::JOINABLE) {
            pthread_detach(thread_handle_);
        }
        congzhi::LockGuard<congzhi::Mutex> lock(mutex_);
        thread_state_ = ThreadState::FINISHED;
    }

public:
    // Constructor - creates a thread with a callable object and its arguments.
    template <typename TFunc, typename... TArgs>
    explicit Thread(TFunc&& f, TArgs&&... args) 
        : thread_handle_(0), thread_state_(ThreadState::UNCREATED) {
        
        auto bound_task =  [func = std::forward<TFunc>(f), tup = std::make_tuple(std::forward<TArgs>(args)...)]() mutable {
                                std::apply(func, std::move(tup));
                           };
        
        using task_type = decltype(bound_task);
        auto data = new ThreadData<task_type>(std::move(bound_task));
        
        // Create a new thread.
        const int result = (pthread_create(
            &thread_handle_, 
            nullptr, 
            &ThreadEntry, 
            static_cast<void*>(data)
        ));
        if (result != 0) {
            delete data;
            throw std::runtime_error("pthread_create failed: " + std::string(strerror(result)));
        }
        congzhi::LockGuard<congzhi::Mutex> lock(mutex_);
        thread_state_ = ThreadState::JOINABLE;
    }
    
    // Default constructor - initializes an uncreated thread.
    Thread() noexcept : thread_handle_(0), thread_state_(ThreadState::UNCREATED) {}
    
    // Destructor - make sure the thread is cleaned up properly.
    ~Thread() noexcept {
        if (Joinable()) {
            Cleanup();
        }
    }

    // Copying is not allowed.
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    
    // Move constructor - transfers ownership of the thread.
    Thread(Thread&& other) noexcept 
        : thread_handle_(other.thread_handle_), 
          thread_state_(other.thread_state_) {
        other.thread_handle_ = 0;
        congzhi::LockGuard<congzhi::Mutex> lock(other.mutex_);
        other.thread_state_ = ThreadState::UNCREATED;
    }
    
    // Move assignment operator - transfers ownership of the thread.
    Thread& operator=(Thread&& other) noexcept {
        if (this != &other) {
            if (Joinable()) {
                Cleanup();
            }
            
            thread_handle_ = other.thread_handle_;
            thread_state_ = other.thread_state_;
            
            other.thread_handle_ = 0;
            other.thread_state_ = ThreadState::UNCREATED;
        }
        return *this;
    }

    // Starts a uncreated thread with a callable object and its arguments.
    template <typename TFunc, typename... TArgs>
    void Start(TFunc&& f, TArgs&&... args) {
        if (Joinable()) {
            throw std::logic_error("Thread is already running");
        }
        
        auto bound_task = [func = std::forward<TFunc>(f), 
                          tup = std::make_tuple(std::forward<TArgs>(args)...)]() mutable {
            std::apply(func, std::move(tup));
        };
        
        using task_type = decltype(bound_task);
        auto data = new ThreadData<task_type>(std::move(bound_task));
        if (!data) {
            throw std::bad_alloc();
        }
        
        const int result = pthread_create(
            &thread_handle_, 
            nullptr, 
            &ThreadEntry, 
            static_cast<void*>(data)
        );
        if (result != 0) {
            delete data;
            throw std::runtime_error("pthread_create failed: " + std::string(strerror(result)));
        }
        congzhi::LockGuard<congzhi::Mutex> lock(mutex_);
        thread_state_ = ThreadState::JOINABLE;
    }
    
    // Returns the id of the thread.
    pthread_t GetId() const noexcept {
        congzhi::LockGuard<congzhi::Mutex> lock(mutex_);
        return (thread_state_ == ThreadState::JOINABLE) ? thread_handle_ : 0;
    }

    // Returns a pointer to the underlying native thread handle.
    pthread_t* NativeHandle() noexcept {
        congzhi::LockGuard<congzhi::Mutex> lock(mutex_);
        return (thread_state_ == ThreadState::JOINABLE) ? &thread_handle_ : 0;
    }

    // Returns the number of concurrent threads supported.
    static unsigned int HardwareConcurrency() noexcept {
        int n = sysconf(_SC_NPROCESSORS_ONLN);
        return (n > 0) ? static_cast<unsigned int>(n) : 0;
    }

    // Wait for the thread to finish execution.
    void Join() {
        if (!Joinable()) {
            throw std::logic_error("Thread not joinable");
        }
        const int result = pthread_join(thread_handle_, nullptr);
        if (result != 0) {
            throw std::runtime_error("pthread_join failed: " + std::string(strerror(result)));
        }
        congzhi::LockGuard<congzhi::Mutex> lock(mutex_);
        thread_state_ = ThreadState::FINISHED;
    }
    
    // Permits the thread to run independently. After execution, the thread's resources are released automatically.
    void Detach() {
        if (!Joinable()) {
            throw std::logic_error("Thread not joinable");
        }
        const int result = pthread_detach(thread_handle_);
        if (result != 0) {
            throw std::runtime_error("pthread_detach failed" + std::string(strerror(result)));
        }
        congzhi::LockGuard<congzhi::Mutex> lock(mutex_);
        thread_state_ = ThreadState::DETACHED;
    }

    // Swap two threads.
    void Swap(Thread& other) noexcept {
        if (this == &other) {
            return;
        }
        // Use branchless programming to avoid deadlocks.
        // Ensure that the mutexes are locked in a consistent order to prevent deadlocks.
        Thread* threads[2] = { &other, this };
        auto first = threads[bool(this < &other)];
        auto second = threads[!bool(this < &other)];

        congzhi::LockGuard<congzhi::Mutex> lock1(first->mutex_);
        congzhi::LockGuard<congzhi::Mutex> lock2(second->mutex_);
        std::swap(thread_handle_, other.thread_handle_);
        std::swap(thread_state_, other.thread_state_);
    }

    // Yield the current thread.
    static void Yield() noexcept {
        sched_yield();
    }
};
} // namespace congzhi

#else
#error "This pthread wrapper is only supported on Apple and Linux platforms."
#endif // __APPLE__ || __linux__
#endif // PTHREAD_WRAPPER_HPP