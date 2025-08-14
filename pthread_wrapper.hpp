/**
 * @file pthread_wrapper.hpp
 * @brief A cross-platform wrapper for pthread, providing C++-style thread management and synchronization primitives.
 * @author Congzhi
 * @date 2025-06-11
 * @license MIT License
 * 
 * This wrapper encapsulates pthread mutexes, condition variables, and threads,
 * providing RAII-style resource management and interfaces similar to C++ standard library.
 * Supported platforms: Linux and macOS.
 */

#ifndef PTHREAD_WRAPPER_HPP
#define PTHREAD_WRAPPER_HPP

#if defined(__APPLE__) || defined(__linux__)

#include <pthread.h> // For pthreads -> Thread management.
#include <unistd.h>  // For sysconf -> Get number of underlying processors.
#include <sched.h>   // For sched_yield -> Yield the current thread.
#include <signal.h> // For congzhi::Thread::DtorAction::TERMINATE (pthread_kill)
#include <time.h>


#include <cstring>
#include <functional>
#include <utility>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <tuple>
#include <cassert>
#include <chrono>

namespace congzhi {


/**
 * @brief Mutex types supported by the implementation.
 */
enum class MutexType {
    DEFAULT, // Default mutex type.
    RECURSIVE, // Recursive mutex type.
    // Error checking mutex type.
};

/**
 * @brief A lightweight wrapper around pthread mutex for mutual exclusion.
 *
 * This class encapsulates a POSIX mutex and provides basic locking operations exactly like std::mutex.
 * Non-copyable and non-movable to ensure safe usage.
 */
class Mutex {
private:
    pthread_mutex_t mutex_handle_;
public:
    /**
     * @brief Constructs and initializes the mutex.
     * @param type The type of mutex to create.
     * @throws std::runtime_error if initialization fails.
     */
    explicit Mutex(MutexType type = MutexType::DEFAULT) {
        pthread_mutexattr_t attr;
        const int res = pthread_mutexattr_init(&attr);
        if (res != 0) {
            throw std::runtime_error("pthread_mutex_init failed: " + std::string(strerror(res)));
        }

        int mutex_type = (type == MutexType::RECURSIVE) ? PTHREAD_MUTEX_RECURSIVE : PTHREAD_MUTEX_DEFAULT;
        const int res2 = pthread_mutexattr_settype(&attr, mutex_type);
        const int res3 = pthread_mutex_init(&mutex_handle_, &attr);
        int err = (res2 != 0) ? res2 : res3;
        if (err != 0) {
            pthread_mutexattr_destroy(&attr);
            throw std::runtime_error("pthread_mutex_init with types failed: " + std::string(strerror(err)));
        }
        pthread_mutexattr_destroy(&attr);
    }
    
    /**
     * @brief Destroys the mutex.
     */
    ~Mutex() noexcept {
        pthread_mutex_destroy(&mutex_handle_);
    }

    /// @brief Deleted copy constructor.
    Mutex(const Mutex&) = delete;
    /// @brief Deleted copy assignment operator.
    Mutex& operator=(const Mutex&) = delete;
    /// @brief Deleted move constructor.
    Mutex(Mutex&&) = delete;
    /// @brief Deleted move assignment operator.
    Mutex& operator=(Mutex&&) = delete;
    
    /**
     * @brief Locks the mutex.
     * @throws std::runtime_error if locking fails.
     */
    void Lock() {
        const int res = pthread_mutex_lock(&mutex_handle_);
        if (res != 0) {
            throw std::runtime_error("pthread_mutex_lock failed:" + std::string(strerror(res)));
        }
    }

    /**
     * @brief Unlocks the mutex.
     */
    void Unlock() noexcept {
        pthread_mutex_unlock(&mutex_handle_);
    }

    /**
     * @brief Attempts to lock the mutex without blocking.
     * @return true if the lock was acquired, false otherwise.
     */
    bool TryLock() noexcept {
        return (pthread_mutex_trylock(&mutex_handle_) == 0);
    }

    /**
     * @brief Returns a non-const pointer to the native pthread mutex handle.
     * @return Pointer to the native pthread mutex handle.
     * @warning Direct manipulation of the native handle is discouraged, which may break RAII guarantees. 
     */
    pthread_mutex_t* NativeHandle() noexcept {
        return &mutex_handle_;
    }

    /**
     * @brief Returns a const pointer to the native pthread mutex handle.
     * @return Const pointer to the native pthread mutex handle.
     */
    const pthread_mutex_t* NativeHandle() const noexcept {
        return &mutex_handle_;
    }
};

/**
 * @brief A wrapper around pthread read-write lock for shared and exclusive access.
 *
 * This class encapsulates a POSIX read-write lock and provides locking operations for both shared and exclusive access.
 * Non-copyable and non-movable to ensure safe usage.
 */
class SharedMutex {
private:
    pthread_rwlock_t rwlock_handle_;
public:
    /**
     * @brief Constructs and initializes the shared mutex.
     * @throws std::runtime_error if initialization fails.
     */
    explicit SharedMutex() {
        const int res = pthread_rwlock_init(&rwlock_handle_, nullptr);
        if (res != 0) {
            throw std::runtime_error("pthread_rwlock_init failed: " + std::string(strerror(res)));
        }
    }

    /**
     * @brief Destroys the shared mutex.
     */
    ~SharedMutex() noexcept {
        pthread_rwlock_destroy(&rwlock_handle_);
    }

    /// @brief Deleted copy constructor. 
    SharedMutex(const SharedMutex&) = delete;
    /// @brief Deleted copy assignment operator.
    SharedMutex& operator=(const SharedMutex&) = delete;
    /// @brief Deleted move constructor.
    SharedMutex(SharedMutex&&) = delete;
    /// @brief Deleted move assignment operator.
    SharedMutex& operator=(SharedMutex&&) = delete;

    /**
     * @brief Locks the mutex for exclusive access.
     * @throws std::runtime_error if locking fails.
     */
    void Lock() {
        const int res = pthread_rwlock_wrlock(&rwlock_handle_);
        if (res != 0) {
            throw std::runtime_error("pthread_rwlock_wrlock failed: " + std::string(strerror(res)));
        }
    }

    /**
     * @brief Unlocks the mutex from exclusive access.
     */
    void Unlock() noexcept {
        pthread_rwlock_unlock(&rwlock_handle_);
    }

    /**
     * @brief Attempts to lock the mutex for exclusive access without blocking.
     * @return true if the lock was acquired, false otherwise.
     */
    bool TryLock() noexcept {
        return (pthread_rwlock_trywrlock(&rwlock_handle_) == 0);
    }

    /**
     * @brief Locks the mutex for shared access.
     * @throws std::runtime_error if locking fails.
     */
    void LockShared() {
        const int res = pthread_rwlock_rdlock(&rwlock_handle_);
        if (res != 0) {
            throw std::runtime_error("pthread_rwlock_rdlock failed: " + std::string(strerror(res)));
        }
    }

    /**
     * @brief Unlocks the mutex from shared access.
     */
    void UnlockShared() noexcept {
        pthread_rwlock_unlock(&rwlock_handle_);
    }

    /**
     * @brief Attempts to lock the mutex for shared access without blocking.
     * @return true if the lock was acquired, false otherwise.
     */
    bool TryLockShared() noexcept {
        return (pthread_rwlock_tryrdlock(&rwlock_handle_) == 0);
    }

    /**
     * @brief Returns a non-const pointer to the native pthread rwlock handle.
     * @return Pointer to the native pthread rwlock handle.
     * @warning Direct manipulation of the native handle is discouraged, which may break RAII guarantees.
     */
    pthread_rwlock_t* NativeHandle() noexcept {
        return &rwlock_handle_;
    }

    /**
     * @brief Returns a const pointer to the native pthread rwlock handle.
     * @return Const pointer to the native pthread rwlock handle.
     */
    const pthread_rwlock_t* NativeHandle() const noexcept {
        return &rwlock_handle_;
    }
};

/**
 * @brief A scoped lock guard for RAII-style mutex management.
 *
 * Acquires the lock on construction and releases it on destruction.
 * Prevents copying and moving to ensure exclusive ownership.
 *
 * @tparam TLock A lockable type that provides Lock() and Unlock().
 */
template <typename TLock>
class LockGuard {
private:
    TLock& lk_;
public:
    /**
     * @brief Constructs the guard and locks the mutex.
     * @param lk Reference to the lockable object.
     */    
    explicit LockGuard(TLock& lk) : lk_(lk) {
        lk_.Lock();
    }

    /**
     * @brief Unlocks the mutex on destruction.
     */
    ~LockGuard() noexcept {
        lk_.Unlock();
    }

    /// Deleted copy constructor.
    LockGuard(const LockGuard&) = delete;
    /// Deleted copy assignment operator.
    LockGuard& operator=(const LockGuard&) = delete;
    /// Deleted move constructor.
    LockGuard(LockGuard&&) = delete;
    /// Deleted move assignment operator.
    LockGuard& operator=(LockGuard&&) = delete;
};

/**
 * @brief Enum representing the status of a wait operation in class ConditionVarible.
 */
enum class WaitStatus {
    NONTIMEOUT, ///< Wait completed without timeout.
    TIMEOUT    ///< Wait timed out.
};

/**
 * @brief A wrapper around pthread condition variable for thread synchronization.
 *
 * Supports waiting with or without predicates, and with timeouts.
 * Non-copyable and non-movable.
 */
class ConditionVariable {
private:
    pthread_cond_t cond_handle_;

    #ifdef __linux__
    static constexpr clockid_t clock_type = CLOCK_MONOTONIC;
    using InternalrClock = std::chrono::steady_clock;
    #else // __APPLE__ OSX has some buggy features to MONOTONIC
    static constexpr clockid_t clock_type = CLOCK_REALTIME;
    using InternalClock = std::chrono::system_clock;
    #endif

    /**
     * @brief Get the current time point using the cv's clock.
     * @return timespec representing current time point.
     */
    static timespec Now() noexcept {
        timespec ts;
        clock_gettime(clock_type, &ts);
        return ts;
    }

    /**
     * @brief Waits on the condition variable until it is signaled or the specified absolute timeout expires.
     * @param mtx Reference to the Mutex object associated with the condition variable.
     * @param abs_time The absolute timeout (as a timespec) specifying the maximum time to wait.
     * @return WaitStatus::NONTIMEOUT if signaled, WaitStatus::TIMEOUT if the timeout expires.
     * @throws std::runtime_error if pthread_cond_timedwait fails for reasons other than timeout.
     */
    WaitStatus TimedWait(Mutex& mtx, const timespec& abs_time) {
        const int res = pthread_cond_timedwait(&cond_handle_, mtx.NativeHandle(), &abs_time);
        if (res == 0) {
            return WaitStatus::NONTIMEOUT;
        } else if (res == ETIMEDOUT) {
            return WaitStatus::TIMEOUT;
        } else {
            throw std::runtime_error("pthread_cond_timedwait failed: " + std::string(strerror(res)));
        }
    }

public:
    /**
     * @brief Constructs and initializes the condition variable.
     * @throws std::runtime_error if initialization fails.
     */
    ConditionVariable() {
        #ifdef __linux__
        pthread_condattr_t attr;
        const int clock_res = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        if (clock_res != 0) {
            pthread_condattr_destroy(&attr);
            throw std::runtime_error("pthread_condattr_setclock failed: " + std::string(strerror(clock_res)));
        }
        
        const int init_res = pthread_cond_init(&cond_handle_, &attr); 
        pthread_condattr_destroy(&attr);
        if (init_res != 0) {
            throw std::runtime_error("pthread_cond_init failed: " + std::string(strerror(init_res)));
        }
        #endif

        #ifdef __APPLE__
        const int init_res = pthread_cond_init(&cond_handle_, nullptr);
        if (init_res != 0) {
            throw std::runtime_error("pthread_cond_init failed: " + std::string(strerror(init_res)));
        }
        #endif
    }

    /**
     * @brief Destroys the condition variable.
     */
    ~ConditionVariable() noexcept {
        pthread_cond_destroy(&cond_handle_);
    }

    /// Deleted copy constructor.
    ConditionVariable(const ConditionVariable&) = delete;
    /// Deleted copy assignment operator.
    ConditionVariable& operator=(const ConditionVariable&) = delete;
    /// Deleted move constructor.
    ConditionVariable(ConditionVariable&&) = delete;
    /// Deleted move assignment operator.
    ConditionVariable& operator=(ConditionVariable&&) = delete;

    /**
     * @brief Waits for notification using the given mutex.
     * 
     * Atomically unlocks the mutex and blocks until notified.
     * Reacquires the mutex before returning.
     * 
     * @param mtx Mutex to unlock while waiting.
     * @throws std::runtime_error if wait fails.
     */
    void Wait(Mutex& mtx) {
        const int res = pthread_cond_wait(&cond_handle_, mtx.NativeHandle());
        if (res != 0) {
            throw std::runtime_error("pthread_cond_wait failed: " + std::string(strerror(res)));
        }
    }

    /**
     * @brief Waits until the predicate returns true.
     *
     * @tparam Predicate A callable returning bool.
     * @param mtx Mutex to unlock while waiting.
     * @param pred Predicate to evaluate.
     * @throws std::runtime_error if wait fails.
     */
    template <typename Predicate>
    void Wait(Mutex& mtx, Predicate pred) {
        while (!pred()) {
            Wait(mtx);        
        }
    }

    /**
     * @brief Waits for notification or timeout.
     * Uses CLOCK_MONOTONIC (Linux) or CLOCK_REALTIME (macOS) to avoid system clock issues.
     * 
     * @tparam Rep Duration representation.
     * @tparam Period Duration period.
     * @param mtx Mutex to unlock while waiting.
     * @param rel_time Relative timeout duration.
     * @return WaitStatus indicating if time's out or not.
     * @throws std::runtime_error if wait fails.
     */
    template <typename Rep, typename Period>
    WaitStatus WaitFor(Mutex& mtx, const std::chrono::duration<Rep, Period>& rel_time) {
        return WaitUntil(mtx, InternalClock::now() + rel_time);
    }

    /**
     * @brief Waits for predicate or timeout.
     * @tparam Predicate A callable returning bool.
     * @tparam Rep Duration representation.
     * @tparam Period Duration period.
     * @param mtx Mutex to unlock while waiting.
     * @param pred Predicate to evaluate.
     * @param rel_time Relative timeout duration.
     * @return WaitStatus indicating if time's out or not.
     * @throws std::runtime_error if wait fails.
     */
    template <typename Predicate, typename Rep, typename Period>
    WaitStatus WaitFor(Mutex& mtx, Predicate pred, const std::chrono::duration<Rep, Period>& rel_time) {
        return WaitUntil(mtx, pred, InternalClock::now() + rel_time);    
    }

    /**
     * @brief Waits until a specific time point or until notified.
     * @note The time point must be based on the same clock as the condition variable:
     *       std::chrono::steady_clock on Linux, std::chrono::system_clock on macOS.
     * @tparam Duration Duration type of the time point.
     * @param mtx Mutex to unlock while waiting.
     * @param abs_time Absolute time point to wait until.
     * @return WaitStatus indicating if time's out or not.
     * @throws std::runtime_error if wait fails.
     */
    template <typename Duration>
    WaitStatus WaitUntil(Mutex& mtx, const std::chrono::time_point<InternalClock, Duration>& abs_time) {
        // Convert the time point to timespec
        auto time_since_epoch = abs_time.time_since_epoch();
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(time_since_epoch);
        auto nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(time_since_epoch) % 1'000'000'000;
        
        struct timespec ts;
        ts.tv_sec = static_cast<time_t>(sec);
        ts.tv_nsec = static_cast<long>(nsec);
        
        return TimedWait(mtx, ts);
    }

    /**
     * @brief Waits until predicate returns true or timeout.
     * @tparam Predicate A callable returning bool.
     * @tparam Duration Duration type.
     * @param mtx Mutex to unlock while waiting.
     * @param pred Predicate to evaluate.
     * @param abs_time Absolute time point to wait until.
     * @return WaitStatus indicating if time's out or not.
     * @throws std::runtime_error if wait fails.
     */
    template <typename Predicate, typename Duration>
    WaitStatus WaitUntil(Mutex& mtx, Predicate pred, const std::chrono::time_point<InternalClock, Duration>& abs_time) {
        while (!pred()) {
            auto status = WaitUntil(mtx, abs_time);
            if (status == WaitStatus::TIMEOUT) {
                return pred() ? WaitStatus::NONTIMEOUT : WaitStatus::TIMEOUT;
            }
        }
        return WaitStatus::NONTIMEOUT;
    }

    /**
     * @brief Notifies one waiting thread.
     */
    void NotifyOne() noexcept{
        pthread_cond_signal(&cond_handle_);
    }

    /**
     * @brief Notifies all waiting threads.
     */
    void NotifyAll() noexcept {
        pthread_cond_broadcast(&cond_handle_);
    }

    /**
     * @brief Returns a pointer to the native pthread condition handle.
     * @return A non-const pointer to the native pthread condition variable handle.
     * @warning Direct manipulation of the native handle is discouraged, which may break RAII guarantees.
     */
    pthread_cond_t* NativeHandle() noexcept {
        return &cond_handle_;
    }

    /**
     * @brief Returns a const pointer to the native pthread condition handle.
     * @return Const pointer to the native pthread condition variable handle.
     */
    const pthread_cond_t* NativeHandle() const noexcept {
        return &cond_handle_;
    }
};

/**
 * @brief Enum representing the scope of threads.
 */
enum class Scope {
    PROCESS, // Process scope. The CPU race is happening within a single process.
    SYSTEM   // SYSTEM scope. The CPU race is happening across all threads in the system.
};

/**
 * @brief Enum representing the detach state of threads.
 */
enum class DetachState {
    JOINABLE, // Thread is joinable and can be waited on.
    DETACHED   // Thread is detached and cannot be waited on (pthread_join invalid).
};

/**
 * @brief Enum representing the scheduling policy for threads.
 * 
 * Priority values for scheduling policies:
 * - SCHED_OTHER: Default scheduling policy (0 priority).
 * - SCHED_FIFO: Platform specific.
 * - SCHED_RR: Platform specific.
 */
enum class SchedulingPolicy {
    DEFAULT, // Default scheduling policy (Completely Fair Scheduler for Linux and Core Foundation Scheduler for macOS).
    FIFO,    // First In First Out scheduling policy.
    RR // Round Robin scheduling policy.
};

/**
 * @brief A wrapper around pthread attributes for thread creation.
 * This auxiliary class provides a way to set thread attributes.
 */
class ThreadAttribute {
private:
    pthread_attr_t attr_handle_;
public:
    /**
     * @brief Constructs and initializes the thread attributes with default settings.
     * @throws std::runtime_error if initialization fails.
     */
    ThreadAttribute() {
        const int res = pthread_attr_init(&attr_handle_);
        if (res != 0) {
            throw std::runtime_error("pthread_attr_init failed: " + std::string(strerror(res)));
        }
    }

    /**
     * @brief Destroys the thread attributes.
     */
    ~ThreadAttribute() noexcept {
        pthread_attr_destroy(&attr_handle_);
    }

    /// Deleted copy constructor.
    ThreadAttribute(const ThreadAttribute&) = delete;
    /// Deleted copy assignment operator.
    ThreadAttribute& operator=(const ThreadAttribute&) = delete;
    /// Deleted move constructor.
    ThreadAttribute(ThreadAttribute&&) = delete;
    /// Deleted move assignment operator.
    ThreadAttribute& operator=(ThreadAttribute&&) = delete;

    /**
     * @brief Sets the stack size for threads created with these attributes.
     * @param size The stack size in bytes. 8MB in default.
     * @throws std::runtime_error if setting stack size fails.
     */
    void SetStackSize(size_t size = (8 * 1024 * 1024)) {
        const int res = pthread_attr_setstacksize(&attr_handle_, size);
        if (res != 0) {
            throw std::runtime_error("pthread_attr_setstacksize failed: " + std::string(strerror(res)));
        }
    }

    /**
     * @brief Gets the stack size for threads created with these attributes.
     * @return The stack size in bytes.
     * @throws std::runtime_error if getting stack size fails.
     */
    size_t GetStackSize() const {
        size_t size;
        const int res = pthread_attr_getstacksize(&attr_handle_, &size);
        if (res != 0) {
            throw std::runtime_error("pthread_attr_getstacksize failed: " + std::string(strerror(res)));
        }
        return size;
    }

    /**
     * @brief Sets the scheduling policy and priority for threads created with these attributes.
     * @param policy The scheduling policy to set (default is SchedulingPolicy::DEFAULT, which is SCHED_OTHER).
     * @param priority The priority level (default is 0).
     * @throws std::runtime_error if setting scheduling policy or priority fails.
     */
    void SetSchedulingPolicy(SchedulingPolicy policy = SchedulingPolicy::DEFAULT, int priority = 0) {
        int policy_value;
        switch (policy) {
            case SchedulingPolicy::DEFAULT: {
                policy_value = SCHED_OTHER; // Default scheduling policy
                const int res = pthread_attr_setschedpolicy(&attr_handle_, policy_value);
                if (res != 0) {
                    throw std::runtime_error("pthread_attr_setschedpolicy failed: " + std::string(strerror(res)));
                }
                if (priority != 0) {
                    throw std::invalid_argument("Priority must be 0 for SCHED_OTHER");
                }
                break;
            }
            case SchedulingPolicy::FIFO: {
                policy_value = SCHED_FIFO; // First In First Out scheduling policy
                const int res = pthread_attr_setschedpolicy(&attr_handle_, policy_value);
                if (res != 0) {
                    throw std::runtime_error("pthread_attr_setschedpolicy failed: " + std::string(strerror(res)));
                }
                if (priority < sched_get_priority_min(SCHED_FIFO) || 
                    priority > sched_get_priority_max(SCHED_FIFO)) {
                    throw std::out_of_range("Priority out of range for SCHED_FIFO");
                }
                struct sched_param param;
                param.sched_priority = priority;
                const int res2 = pthread_attr_setschedparam(&attr_handle_, &param);
                if (res2 != 0) {
                    throw std::runtime_error("pthread_attr_setschedparam failed: " + std::string(strerror(res2)));
                }
                break;
            }
            case SchedulingPolicy::RR: {
                policy_value = SCHED_RR; // Round Robin scheduling policy
                const int res = pthread_attr_setschedpolicy(&attr_handle_, policy_value);
                if (res != 0) {
                    throw std::runtime_error("pthread_attr_setschedpolicy failed: " + std::string(strerror(res)));
                }
                if (priority < sched_get_priority_min(SCHED_RR) || 
                    priority > sched_get_priority_max(SCHED_RR)) {
                    throw std::out_of_range("Priority out of range for SCHED_RR");
                }
                struct sched_param param;
                param.sched_priority = priority;
                const int res2 = pthread_attr_setschedparam(&attr_handle_, &param);
                if (res2 != 0) {
                    throw std::runtime_error("pthread_attr_setschedparam failed: " + std::string(strerror(res2)));
                }
                break;
            }
            default:
                throw std::invalid_argument("Invalid scheduling policy");
        }
    }

    /**
     * @brief Gets the scheduling policy of threads created with these attributes.
     * @return The scheduling policy (SchedulingPolicy::Default, SchedulingPolicy::FIFO, or SchedulingPolicy::RR).
     * @throws std::runtime_error if getting scheduling policy fails.
     */
    SchedulingPolicy GetSchedulingPolicy() const {
        int policy;
        const int res = pthread_attr_getschedpolicy(&attr_handle_, &policy);
        if (res != 0) {
            throw std::runtime_error("pthread_attr_getschedpolicy failed: " + std::string(strerror(res)));
        }
        switch (policy) {
            case SCHED_OTHER: return SchedulingPolicy::DEFAULT;
            case SCHED_FIFO: return SchedulingPolicy::FIFO;
            case SCHED_RR: return SchedulingPolicy::RR;
            default: throw std::runtime_error("Unknown scheduling policy");
        }
    }

    /**
     * @brief Sets the scope of threads created with these attributes.
     * @param scope The scope to set (PROCESS or SYSTEM).
     * @note PROCESS scope may not be supported on most systems.
     * @throws std::runtime_error if setting scope fails.
     */
    void SetScope(Scope scope) {
        int scope_value = (scope == Scope::PROCESS) ? PTHREAD_SCOPE_PROCESS : PTHREAD_SCOPE_SYSTEM;
        const int res = pthread_attr_setscope(&attr_handle_, scope_value);
        if (res != 0) {
            throw std::runtime_error("pthread_attr_setscope failed: " + std::string(strerror(res)));
        }
    }

    /**
     * @brief Gets the scope of threads created with these attributes.
     * @return The scope (PROCESS or SYSTEM).
     * @throws std::runtime_error if getting scope fails.
     */
    Scope GetScope() const {
        int scope_value;
        const int res = pthread_attr_getscope(&attr_handle_, &scope_value);
        if (res != 0) {
            throw std::runtime_error("pthread_attr_getscope failed: " + std::string(strerror(res)));
        }
        return (scope_value == PTHREAD_SCOPE_PROCESS) ? Scope::PROCESS : Scope::SYSTEM;
    }

    /** 
     * @brief Sets the detach state of threads created with these attributes.
     * @param detached DetachState indicating whether the thread is joinable or detached (JOINABLE in default).
     * @throws std::runtime_error if setting detach state fails.
     */
    void SetDetachState(DetachState detached = DetachState::JOINABLE) {
        int detach_state = (detached == DetachState::DETACHED) ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE;
        const int res = pthread_attr_setdetachstate(&attr_handle_, detach_state);
        if (res != 0) {
            throw std::runtime_error("pthread_attr_setdetachstate failed: " + std::string(strerror(res)));
        }
    }

    /**
     * @brief Gets the detach state of threads created with these attributes.
     * @return DetachState indicating whether the thread is joinable or detached.
     * @throws std::runtime_error if getting detach state fails.
     */
    DetachState GetDetachState() const {
        int detach_state;
        const int res = pthread_attr_getdetachstate(&attr_handle_, &detach_state);
        if (res != 0) {
            throw std::runtime_error("pthread_attr_getdetachstate failed: " + std::string(strerror(res)));
        }
        return (detach_state == PTHREAD_CREATE_DETACHED) ? DetachState::DETACHED : DetachState::JOINABLE;
    }

    /**
     * @brief Returns a pointer to the native pthread attribute handle.
     * @return Pointer to the native pthread attribute handle.
     * @warning Direct manipulation of the native handle is discouraged, which may break RAII guarantees.
     */
    pthread_attr_t* NativeHandle() noexcept {
        return &attr_handle_;
    }

    /**
     * @brief Returns a const pointer to the native pthread attribute handle.
     * @return Const pointer to the native pthread attribute handle.
     */
    const pthread_attr_t* NativeHandle() const noexcept {
        return &attr_handle_;
    }
};

/**
 * @brief Enum representing the state of a thread.
 */
enum class ThreadState {
    JOINABLE,   // Thread is running and can be joined.
    DETACHED,   // Thread is detached, running independently.
    TERMINATED, // Thread is terminated.
    INVALID     // Thread has not been created or is in an invalid state.
};

/*
 * Thread State Machine:
 *
 *                +-----------+
 *                |  INVALID  |
 *                +-----------+
 *                      |
 *                      | Start();
 *                      v
 *                +------------+
 *                |  JOINABLE  | <-----、
 *                +------------+        \
 *                /            \         |
 *       Detach()/              \ Join() | Start()
 *              /                \       |
 *             v                  v      |
 *      +------------+        +------------+
 *      |  DETACHED  | -----> | TERMINATED |
 *      +------------+        +------------+
 */

/**
 * @brief Enum representing the action to take on thread destruction.
 */
enum class DtorAction {
    JOIN,      // Join the thread on destruction. JOINABLE -> TERMINATED
    DETACH,    // Detach the thread on destruction. JOINABLE -> DETACHED
    CANCEL,    // Request thread cancellation on destruction. JOINABLE/DETACHED -> TERMINATED
    TERMINATE  // Terminate the thread on destruction. JOINABLE/DETACHED -> TERMINATED
};

/**
 * @brief A cross-platform wrapper around pthreads for thread management.
 *
 * Provides a high-level interface for creating, starting, managing, and synchronizing threads.
 * Supports move semantics, thread state tracking, and RAII-style cleanup.
 */
class Thread {
private:
    /**
     * @brief Internal thread properties managed by shared pointer.
     */
    struct ThreadProperty {
        pthread_t thread_handle {0}; // Native thread handle.
        ThreadState thread_state {DefaultThreadState}; // Current state of the thread.
        DtorAction dtor_action {DefaultDtorAction}; // Action to take on destruction.
        mutable congzhi::Mutex mutex; // A mutable mutex for thread safety(M&M rule).
        bool is_complete{false};
    };
    
    std::shared_ptr<ThreadProperty> property_ = std::make_shared<ThreadProperty>();

public:
    static constexpr ThreadState DefaultThreadState = ThreadState::INVALID; // Default thread state (Invalid).    
    static constexpr DtorAction DefaultDtorAction = DtorAction::JOIN; // Default destructor action (JOIN).
    
    /**
     * @brief Get the current thread state as a string.
     * @return Returns a std::string representation of the current thread state. 
     */
    const std::string GetThreadState() noexcept {
        auto property = property_;
        congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
        switch (property_->thread_state) {
            case ThreadState::JOINABLE: return std::string("JOINABLE");
            case ThreadState::DETACHED: return std::string("DETACHED");
            case ThreadState::TERMINATED: return std::string("TERMINATED");
            case ThreadState::INVALID: return std::string("INVALID");
            default: return std::string("UNKNOWN");
        }
    }

    /**
     * @brief Checks if the thread is finishes its job.
     * @return true if the thread finishes the jon, false otherwise.
     */
    bool IsCompleteExecution() const noexcept {
        auto property = property_;
        congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
        return property->is_complete;
    }

    /**
     * @brief Checks if the thread is joinable thread-safely.
     * @return true if the thread is joinable, false otherwise.
     */
    bool IsJoinable() const noexcept {
        auto property = property_;
        congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
        return property->thread_state == ThreadState::JOINABLE;
    }

    /**
     * @brief Checks if the thread is detached thread-safely.
     * @return true if the thread is detached, false otherwise.
     */
    bool IsDetached() const noexcept {
        auto property = property_;
        congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
        return property->thread_state == ThreadState::DETACHED;
    }

    /**
     * @brief Checks if the thread is invalid thread-safely.
     * @return true if the thread is invalid, false otherwise.
     */
    bool IsInvalid () const noexcept {
        auto property = property_;
        congzhi::LockGuard<congzhi::Mutex> lock (property->mutex);
        return property->thread_state == ThreadState::INVALID;
    }

    /**
     * @brief Sets the action of congzhi::Thread destructor thread-safely.
     * @param action The DtorAction to set.
     */
    void SetDtorAction(DtorAction action) noexcept {
        auto property = property_;
        congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
        if (property->thread_state == ThreadState::INVALID || property->thread_state == ThreadState::TERMINATED) {
            property->dtor_action = action;
        }
    }

    /**
     * @brief Gets the action of congzhi::Thread destructor thread-safely.
     * @return The current DtorAction.
     */
    DtorAction GetDtorAction() const noexcept {
        auto property = property_;
        congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
        return property->dtor_action;
    }
private:
    /**
     * @brief Abstract base class for encapsulating thread task data.
     *
     * Used to store and execute callable objects in a type-erased manner.
     */
    struct ThreadDataBase {
        std::weak_ptr<ThreadProperty> property_;
        explicit ThreadDataBase(std::weak_ptr<ThreadProperty> property) : property_(property) {}
        virtual ~ThreadDataBase() = default;
        virtual void Execute() = 0;
    };

    /**
     * @brief Concrete implementation of ThreadDataBase for storing a specific callable.
     *
     * @tparam TFunc Type of the callable object.
     */
    template <typename TFunc>
    struct ThreadData : public ThreadDataBase {
        TFunc callable_;
        ThreadData(TFunc&& func, std::weak_ptr<ThreadProperty> property)
            : ThreadDataBase(property), 
              callable_(std::forward<TFunc>(func)) {    
        }
        
        void Execute() override {
            callable_();
        }
    };

    /**
     * @brief Entry point function for the thread.
     *
     * Converts the raw pointer to a ThreadDataBase, executes the task,
     * and handles any exceptions internally.
     *
     * @param arg Pointer to ThreadDataBase.
     * @return nullptr
     */
    static void* ThreadEntry(void* arg) {
        std::unique_ptr<ThreadDataBase> data(static_cast<ThreadDataBase*>(arg));
        try {
            data->Execute();
        } catch (...) {
            // Log or handle exceptions here if needed.
        }

        // update property if it's not expired
        if (auto property = data->property_.lock()) {
            congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
            if (property->thread_state == ThreadState::DETACHED) {
                property->thread_state = ThreadState::TERMINATED;
            }
            property->is_complete = true;
        } else {
        }
        return nullptr;
    }

    /**
     * @brief Cleanup the current thread based on the destructor action.
     */
    void Cleanup() noexcept {
        auto property = property_; // keep state alive
        
        ThreadState current_state;
        DtorAction current_action;

        {
            congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
            current_state = property->thread_state;
            current_action = property->dtor_action;
            
            if (current_state == ThreadState::TERMINATED || 
                current_state == ThreadState::INVALID || 
                current_state == ThreadState::DETACHED) {
                return; // Already terminated or detached, nothing to do.
            }
        }
        try {
            switch (current_action) {
                case DtorAction::JOIN: 
                    Join();
                    break;
                case DtorAction::DETACH:
                    Detach();
                    break;
                case DtorAction::CANCEL:
                    Cancel();
                    break;
                case DtorAction::TERMINATE:
                    Terminate();
                    break;
            }
        } catch (...) {
            // ignore cleanup errors
        }
    }

public:
    /**
     * @brief Constructs a thread and starts execution with the given callable and arguments.
     * @tparam TFunc Callable type.
     * @tparam TArgs Argument types.
     * @param f Callable object.
     * @param args Arguments to pass to the callable.
     * @param dtor_action Action to take on thread destruction.
     * @param attr Thread attributes for creation.
     * @throws std::runtime_error if thread creation fails.
     */
    template <typename TFunc, typename... TArgs>
    explicit Thread(TFunc&& f, TArgs&&... args, 
                    DtorAction dtor_action = DefaultDtorAction,
                    const ThreadAttribute& attr = ThreadAttribute()) {
        Start(std::forward<TFunc>(f), std::forward<TArgs>(args)..., dtor_action, attr);
    }
    
    /**
     * @brief Default constructor. Initializes an uncreated thread.
     */
    Thread() noexcept = default;
    
    /**
     * @brief Destructor. Cleans up the thread if joinable.
     */
    ~Thread() noexcept {
        Cleanup();
    }

    /// Deleted copy constructor.
    Thread(const Thread&) = delete;
    /// Deleted copy assignment operator.
    Thread& operator=(const Thread&) = delete;
    
    /**
     * @brief Move constructor. Transfers ownership of the thread.
     * @param other Thread to move from.
     */
    Thread(Thread&& other) noexcept {
        congzhi::LockGuard<congzhi::Mutex> lock(other.property_->mutex);
        property_ = other.property_;
        other.property_ = std::make_shared<ThreadProperty>();    
    }
    
    /**
     * @brief Move assignment operator. Transfers ownership of the thread.
     * @param other Thread to move from.
     * @return Reference to this thread.
     */
    Thread& operator=(Thread&& other) noexcept {
        if (this != &other) {
            Cleanup(); // Clean up current thread before moving    

            congzhi::LockGuard<congzhi::Mutex> lock(other.property_->mutex);            
            property_ = other.property_;
            other.property_ = std::make_shared<ThreadProperty>();
        }
        return *this;
    }

    /**
     * @brief Starts the thread with the given callable and arguments.
     * @tparam TFunc Callable type.
     * @tparam TArgs Argument types.
     * @param f Callable object.
     * @param args Arguments to pass to the callable.
     * @param dtor_action Action to take on thread destruction.
     * @param attr Thread attributes for creation.
     * @throws std::logic_error if thread is already running.
     * @throws std::runtime_error if thread creation fails.
     */
    template <typename TFunc, typename... TArgs>
    void Start(TFunc&& f, TArgs&&... args, 
               DtorAction dtor_action = DefaultDtorAction,
               const ThreadAttribute& attr = ThreadAttribute()) 
    {
        auto property = property_;    
        congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);

        // you cannot restart a running thread
        if (property->thread_state == ThreadState::JOINABLE ||
            property->thread_state == ThreadState::DETACHED) {
            throw std::logic_error("Thread is already running");
        }

        //auto bound_task = std::bind(std::forward<TFunc>(f), std::forward<TArgs>(args)...);
        auto bound_task = [func = std::forward<TFunc>(f), 
                          tup = std::make_tuple(std::forward<TArgs>(args)...)]() mutable {
            std::apply(std::move(func), std::move(tup));
        };
        
        using task_type = decltype(bound_task);
        auto data = new ThreadData<task_type>(std::move(bound_task), property_);
    
        const int res = pthread_create(
            &property->thread_handle, 
            attr.NativeHandle(), 
            &ThreadEntry, 
            static_cast<void*>(data)
        );

        if (res != 0) {
            delete data;
            throw std::runtime_error("pthread_create failed: " + std::string(strerror(res)));
        }
    
        property->thread_state = ThreadState::JOINABLE;
        property->dtor_action = dtor_action; // Set the destructor action
    }
    
    /**
     * @brief Returns the native thread ID.
     * @return pthread_t ID if joinable, otherwise 0.
     */
    pthread_t GetId() const noexcept {
        auto property = property_;
        congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
        return (property->thread_state == ThreadState::JOINABLE) ? property->thread_handle : 0;
    }

    /**
     * @brief Returns a pointer to the native thread handle.
     * @return Pointer to pthread_t if joinable, otherwise nullptr.
     * @warning Direct manipulation of the native handle is discouraged, which may break RAII guarantees.
     */
    pthread_t* NativeHandle() noexcept {
        auto property = property_;
        congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
        return (property->thread_state == ThreadState::JOINABLE) ? &property->thread_handle : nullptr;
    }

    /**
     * @brief Returns a const pointer to the native thread handle.
     * @return Const pointer to pthread_t if joinable, otherwise nullptr.
     */
    const pthread_t* NativeHandle() const noexcept {
        auto property = property_;
        congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
        return (property->thread_state == ThreadState::JOINABLE) ? &property->thread_handle : nullptr;
    }

    /**
     * @brief Returns the number of concurrent threads supported by the system.
     * @return Number of hardware threads.
     */
    static unsigned int HardwareConcurrency() noexcept {
        int n = sysconf(_SC_NPROCESSORS_ONLN);
        return (n > 0) ? static_cast<unsigned int>(n) : 0;
    }

    /**
     * @brief Waits for the thread to finish execution. And update the thread state.
     * @throws std::logic_error if thread is not joinable.
     * @throws std::runtime_error if join fails.
     */
    void Join() {
        auto property = property_;
        pthread_t handle;
        {
            // only JOINABLE state can join
            congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
            if (property->thread_state != ThreadState::JOINABLE) {
                throw std::logic_error("Thread not joinable");
            }
            if (property->dtor_action == DtorAction::DETACH) {
                throw std::logic_error("Thread not joinable (DETACH dtor action)");
            }
            handle = property->thread_handle;
        }
        const int res = pthread_join(handle, nullptr);
        if (res != 0) {
            throw std::runtime_error("pthread_join failed: " + std::string(strerror(res)));
        }
        congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
        property->thread_state = ThreadState::TERMINATED;
    }
    
    /**
     * @brief Detaches the thread, allowing it to run independently. And update the thread state.
     * @throws std::logic_error if thread is not joinable.
     * @throws std::runtime_error if detach fails.
     */
    void Detach() {
        auto property = property_;
        pthread_t handle;
        {
            congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
            if (property->thread_state != ThreadState::JOINABLE) {
                throw std::logic_error("Thread not detachable");
            }
            handle = property->thread_handle;
        }

        const int res = pthread_detach(handle);
        if (res != 0) {
            throw std::runtime_error("pthread_detach failed: " + std::string(strerror(res)));
        }
        congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
        property->thread_state = property->is_complete ? ThreadState::TERMINATED : ThreadState::DETACHED;
    }

    /**
     * @brief Cancels the thread execution. And update the thread state.
     * @throws std::logic_error if thread is not created.
     * @throws std::runtime_error if cancel fails.
     */
    void Cancel() {
        auto property = property_;
        pthread_t handle;
        {
            congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
            if (property->thread_state != ThreadState::JOINABLE &&
                property->thread_state != ThreadState::DETACHED) {
                throw std::logic_error("Thread not cancelable");
            }
            handle = property->thread_handle;
        }
        const int cancel_res = pthread_cancel(handle);
        if (cancel_res != 0) {
            throw std::runtime_error("pthread_cancel failed: " + std::string(strerror(cancel_res)));
        }

        // only when joinable
        if (property->thread_state == ThreadState::JOINABLE) {
            const int join_res = pthread_join(handle, nullptr);
            if (join_res != 0) {
                throw std::runtime_error("pthread_join after cancel failed: " + std::string(strerror(join_res)));
            }
        }
        
        congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
        property->thread_state = ThreadState::TERMINATED;
    }

    /**
     * @brief Terminates the thread execution gracefully and updates the thread state.
     * @throws std::logic_error if the thread is not running.
     * @throws std::runtime_error if termination fails.
     * @note This method sends a SIGTERM signal to the thread using pthread_kill.
     *       The target thread should register a signal handler for SIGTERM to handle graceful termination.
     *       If the thread does not handle SIGTERM, the default behavior may terminate the thread,
     *       but it will not terminate the entire process unless the signal is unhandled and critical.
     */
    void Terminate() {
        auto property = property_;
        pthread_t handle;
        {
            congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
            if (property->thread_state != ThreadState::JOINABLE &&
                property->thread_state != ThreadState::DETACHED) {
                throw std::logic_error("Thread not running, cannot terminate");
            }
            handle = property->thread_handle;
        }

        const int term_res = pthread_kill(handle, SIGTERM);
        if (term_res != 0) {
            if (term_res == ESRCH) {
                throw std::logic_error("Thread already dead");
            }
            throw std::runtime_error("pthread_kill (SIGTERM) failed: " + std::string(strerror(term_res)));
        }

        if (property->thread_state == ThreadState::JOINABLE) {
            #ifdef __APPLE__
            constexpr int max_retries = 10;
            constexpr struct timespec delay = {0, 10'000'000}; // 10ms
            
            for (int i = 0; i < max_retries; i++) {
                int res = pthread_kill(handle, 0);
                
                if (res == ESRCH) {
                    // thread's dead
                    congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
                    property->thread_state = ThreadState::TERMINATED;
                    return;
                }
                nanosleep(&delay, nullptr);
            }
            
            #endif

            // macOS do not support pthread_tryjoin_np()
            #ifdef __linux__
            // Wait for the thread to finish after sending SIGTERM.
            constexpr int max_retries = 10;
            constexpr struct timespec delay = {0, 10'000'000}; // 10ms
            for (int i = 0; i < max_retries; i++) {
                int join_res = pthread_tryjoin_np(handle, nullptr);
                if (join_res == 0) {
                    congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
                    property->thread_state = ThreadState::TERMINATED;
                    return;
                } else if (join_res != EBUSY) {
                    // non-busy error
                    break;
                }
                nanosleep(&delay, nullptr);
            }
            #endif
        
            const int join_res = pthread_join(handle, nullptr);
            if (join_res != 0) {
                if (join_res == ESRCH) {
                    congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
                    property->thread_state = ThreadState::TERMINATED;
                }
                throw std::runtime_error("pthread_join after SIGTERM failed: " + std::string(strerror(join_res)));
            }
                congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
                property->thread_state = ThreadState::TERMINATED;
        } else {
            // detach state
            congzhi::LockGuard<congzhi::Mutex> lock(property->mutex);
            property->thread_state = ThreadState::TERMINATED;
        }    
    }

    /**
     * @brief Swaps this thread with another.
     * @param other Thread to swap with.
     */
    void Swap(Thread& other) noexcept {
        if (this == &other) {
            return;
        }
        std::swap(property_, other.property_);
    }

    /**
     * @brief Yields execution to another thread.
     */
    static void Yield() noexcept {
        sched_yield();
    }
};


namespace this_thread {
    /**
     * @brief Yields execution to allow other threads to run.
     * This is a static method that yields the current thread's execution.
     */
    void Yield() noexcept {
        sched_yield();
    }
    
    /**
     * @brief Gets the ID of the current thread.
     * @return The pthread_t ID of the current thread.
     */
    pthread_t GetId() noexcept {
        return pthread_self();
    }

    /**
     * @brief Sleeps for a specified duration.
     * @tparam Rep Duration representation type.
     * @tparam Period Duration period type.
     * @param sleep_duration Duration to sleep, specified in milliseconds.
     * @throws std::invalid_argument if sleep_duration is negative.
     * @throws std::runtime_error if nanosleep fails.
     */
    template <typename Rep, typename Period>
    void SleepFor(const std::chrono::duration<Rep, Period>& sleep_duration) {
        if (sleep_duration < std::chrono::milliseconds(0)) {
            throw std::invalid_argument("Sleep duration cannot be negative");
        }
        
        auto sleep_time = std::chrono::duration_cast<std::chrono::nanoseconds>(sleep_duration).count();
        if (sleep_time < 0) {
            throw std::invalid_argument("Sleep duration cannot be negative");
        }
        if (sleep_time == 0) {
            Yield();
            return;
        }
        
        struct timespec ts;
        ts.tv_sec = sleep_time / 1'000'000'000; // Convert nanoseconds to seconds
        ts.tv_nsec = sleep_time % 1'000'000'000; // Remaining nanoseconds
        
        for (;;) {
            int res = nanosleep(&ts, &ts);
            if (res == 0) {
                return; // Sleep completed successfully
            }
            if (errno == EINTR) {
                // Interrupted by a signal, retry nanosleep
                continue;
            }
            // If we reach here, an error occurred
            throw std::runtime_error("nanosleep failed: " + std::string(strerror(res)));
        }
    }

    /**
     * @brief Sleeps for a specified duration in milliseconds.
     * @param ms Duration to sleep in milliseconds.
     * @throws std::invalid_argument if ms is negative.
     * @throws std::runtime_error if nanosleep fails.
     */
    void SleepFor(int ms) {
        if (ms < 0) {
            throw std::invalid_argument("Sleep duration cannot be negative");
        }
        SleepFor(std::chrono::milliseconds(ms));
    }

    /**
     * @brief Sleeps until a specific time point.
     * @tparam Clock Clock type.
     * @tparam Duration Duration type of the time point.
     * @param abs_time Absolute time point to sleep until.
     * @throws std::invalid_argument if abs_time is in the past.
     * @throws std::runtime_error if nanosleep fails.
     * @note Absolute time only sopported on Linux.
     */
    template <typename Clock, typename Duration>
    void SleepUntil(const std::chrono::time_point<Clock, Duration>& abs_time) {
        auto now = Clock::now();
        if (abs_time <= now) {
            return; // No need to sleep, the time point is in the past
        }
        
        auto sys_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            abs_time - now + std::chrono::system_clock::now()
        );

        #ifdef __linux__
            // the time duration from now to 1st, January, 1970
            auto duration_since_epoch = sys_time.time_since_epoch();
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration_since_epoch);
            auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration_since_epoch - seconds);

            struct timespec ts;
            ts.tv_sec = seconds;
            ts.tv_nsec = nanoseconds;

            int res = clock_nanosleep(CLOCK_REALTIME, TIME_ABSTIME, &ts, nullptr);

            if (res != 0) {
                if (res = EINTR) {
                    return;
                }
                throw std::runtime_error("clock_nanosleep failed: " + std::string(strerror(res)));
            }
        #endif
        
        // NOT SAFE, use relative time here
        #ifdef __APPLE__
        auto now_sys = std::chrono::system_clock::now();
        if (sys_time > now_sys) {
            auto rel_time = sys_time - now_sys;
            SleepFor(rel_time);
        }
        return;
        #endif
    }
} // namespace this_thread
} // namespace congzhi

#else
#error "This pthread wrapper is only supported on Apple and Linux platforms."
#endif // __APPLE__ || __linux__
#endif // PTHREAD_WRAPPER_HPP

