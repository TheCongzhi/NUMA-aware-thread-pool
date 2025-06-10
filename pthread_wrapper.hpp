/*
* Author:       Congzhi
* Update:       2025-06-10
* Description:  A pthread wrapper for cross-platform compatibility. 
* License:      MIT License
*/
#ifndef PTHREAD_WRAPPER_HPP
#define PTHREAD_WRAPPER_HPP

#if defined(__APPLE__) || defined(__linux__)
#include <pthread.h> // For pthreads -> Thread management.
#include <unistd.h> // For sysconf -> Get number of underlying processors.
#include <sched.h> // For sched_yield -> Yield the current thread.

#include <functional>
#include <utility>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <tuple>
#include <cassert>
#include <chrono>

namespace congzhi {

class Thread {
public:
    // Thread states
    enum class ThreadState {
        JOINABLE,
        DETACHED,
        FINISHED,
        UNCREATED // Thread not created or in an invalid state
    };

    // Get the string representation of the thread state.
    const char* GetThreadState() noexcept {
        switch (thread_state_) {
            case ThreadState::JOINABLE: return "JOINABLE";
            case ThreadState::DETACHED: return "DETACHED";
            case ThreadState::FINISHED: return "FINISHED";
            case ThreadState::UNCREATED: return "UNCREATED";
            default: return "UNKNOWN";
        }
    }
private:
    pthread_t thread_handle_;
    ThreadState thread_state_;

    // Base class for thread data
    struct ThreadDataBase {
        virtual ~ThreadDataBase() = default;
        virtual void Execute() = 0;
    };

    // Concrete thread data class that holds the callable - implementation of a wrapper for a callable object.
    template <typename TFunc>
    struct ThreadData : ThreadDataBase {
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
            // Log or handle exceptions here if needed
        }
        return nullptr;
    }

    // Cleanup function to ensure proper resource management.
    void Cleanup() noexcept {
        if (thread_state_ == ThreadState::JOINABLE) {
            pthread_detach(thread_handle_);
        }
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
        const int result = pthread_create(
            &thread_handle_, 
            nullptr, 
            &ThreadEntry, 
            static_cast<void*>(data)
        );
        
        if (result != 0) {
            delete data;
            throw std::runtime_error("pthread_create failed");
        }
        
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
    void CreateThread(TFunc&& f, TArgs&&... args) {
        if (Joinable()) {
            throw std::logic_error("Thread is already running");
        }
        
        auto bound_task = [func = std::forward<TFunc>(f), 
                          tup = std::make_tuple(std::forward<TArgs>(args)...)]() mutable {
            std::apply(func, std::move(tup));
        };
        
        using task_type = decltype(bound_task);
        auto data = new ThreadData<task_type>(std::move(bound_task));
        
        const int result = pthread_create(
            &thread_handle_, 
            nullptr, 
            &ThreadEntry, 
            static_cast<void*>(data)
        );
        
        if (result != 0) {
            delete data;
            throw std::runtime_error("pthread_create failed");
        }
        
        thread_state_ = ThreadState::JOINABLE;
    }

    // Checks if the thread is joinable.
    bool Joinable() const noexcept {
        return thread_state_ == ThreadState::JOINABLE;
    }

    // Returns the id of the thread.
    pthread_t GetId() const noexcept {
        return (thread_state_ == ThreadState::JOINABLE) ? thread_handle_ : 0;
    }

    // Returns the underlying native thread handle.
    pthread_t NativeHandle() const noexcept {
        return (thread_state_ == ThreadState::JOINABLE) ? thread_handle_ : 0;
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
            throw std::runtime_error("pthread_join failed");
        }
        
        thread_state_ = ThreadState::FINISHED;
    }
    
    // Permits the thread to run independently. After execution, the thread's resources are released automatically.
    void Detach() {
        if (!Joinable()) {
            throw std::logic_error("Thread not joinable");
        }
        const int result = pthread_detach(thread_handle_);
        if (result != 0) {
            throw std::runtime_error("pthread_detach failed");
        }
        thread_state_ = ThreadState::DETACHED;
    }

    // Swap two threads.
    void Swap(Thread& other) noexcept {
        std::swap(thread_handle_, other.thread_handle_);
        std::swap(thread_state_, other.thread_state_);
    }

    // Yield the current thread.
    static void Yield() noexcept {
        sched_yield();
    }
};

class Mutex {
private:
    pthread_mutex_t mutex_handle_;
public:
    // Constructor - initializes the mutex.
    Mutex() {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_DEFAULT);
        if (pthread_mutex_init(&mutex_handle_, &attr) != 0) {
            throw std::runtime_error("Failed to initialize mutex");
        }
        pthread_mutexattr_destroy(&attr);
    }

    // Destructor - destroys the mutex.
    ~Mutex() noexcept {
        pthread_mutex_destroy(&mutex_handle_);
    }

    // Lock the mutex.
    void Lock() {
        if (pthread_mutex_lock(&mutex_handle_) != 0) {
            throw std::runtime_error("Failed to lock mutex");
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

    // Returns the native handle of the mutex.
    pthread_mutex_t* NativeHandle() noexcept {
        return &mutex_handle_;
    }
};

template <typename Mutex>
class LockGuard {
private:
    Mutex& lk_;
public:
    // Constructor - locks the mutex.
    explicit LockGuard(Mutex& lk) : lk_(lk) {
        lk_.Lock();
    }

    // Destructor - unlocks the mutex.
    ~LockGuard() noexcept {
        lk_.Unlock();
    }

    // Copying is not allowed.
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

    // Move constructor - transfers ownership of the lock.
    LockGuard(LockGuard&& other) noexcept : lk_(other.lk_) {
        other.lk_ = nullptr; // Reset the moved-from object
    }

    // Move assignment operator - transfers ownership of the lock.
    LockGuard& operator=(LockGuard&& other) noexcept {
        if (this != &other) {
            lk_.Unlock(); // Unlock current mutex before moving
            lk_ = std::move(other.lk_);
            other.lk_ = nullptr; // Reset the moved-from object
        }
        return *this;
    }
};

class ConditionVariable {
private:
    pthread_cond_t cond_handle_;
public:
    // Constructor - initializes the condition variable.
    ConditionVariable() {
        if (pthread_cond_init(&cond_handle_, nullptr) != 0) {
            throw std::runtime_error("Failed to initialize condition variable");
        }
    }

    // Destructor - destroys the condition variable.
    ~ConditionVariable() noexcept {
        pthread_cond_destroy(&cond_handle_);
    }

    // Copying is not allowed.
    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;
    
    // Move constructor - transfers ownership of the condition variable.
    ConditionVariable(ConditionVariable&& other) noexcept 
        : cond_handle_(other.cond_handle_) {
        other.cond_handle_ = PTHREAD_COND_INITIALIZER; // Reset the moved-from object
    }

    // Move assignment operator - transfers ownership of the condition variable.
    ConditionVariable& operator=(ConditionVariable&& other) noexcept {
        if (this != &other) {
            pthread_cond_destroy(&cond_handle_); // Destroy the current condition variable.
            cond_handle_ = other.cond_handle_;
            other.cond_handle_ = PTHREAD_COND_INITIALIZER; // Reset the moved-from object.
        }
        return *this;
    }

    // Wait for the condition variable to be notified, using a mutex.
    void Wait( Mutex& mtx) {
        if (pthread_cond_wait(&cond_handle_, mtx.NativeHandle()) != 0) {
            throw std::runtime_error("Failed to wait on condition variable");
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
        if (pthread_cond_timedwait(&cond_handle_, mtx.NativeHandle(), &ts) != 0) {
            throw std::runtime_error("Failed to wait for condition variable with timeout");
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
        
        if (pthread_cond_timedwait(&cond_handle_, mtx.NativeHandle(), &ts) != 0) {
            throw std::runtime_error("Failed to wait for condition variable until time point");
        }
    }

    // Notify one waiting thread.
    void NotifyOne() noexcept {
        pthread_cond_signal(&cond_handle_);
    }

    // Notify all waiting threads.
    void NotifyAll() noexcept {
        pthread_cond_broadcast(&cond_handle_);
    }

    // Returns the native handle of the condition variable.
    pthread_cond_t NativeHandle() noexcept {
        return cond_handle_;
    }

};
} // namespace congzhi

#else
#error "This pthread wrapper is only supported on Apple and Linux platforms."
#endif // __APPLE__ || __linux__
#endif // PTHREAD_WRAPPER_HPP