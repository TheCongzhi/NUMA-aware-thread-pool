# README

- [English](README.md)
- [简体中文](README.zh_CN.md)

This project implements a **thread pool** that runs on **macOS** and **Linux**. The behavior of the thread pool adapts depending on the underlying CPU hardware.

- **On macOS**: Since Apple Silicon uses **Unified Memory Architecture (UMA)**, the thread pool defaults to a UMA-based design.  
  (**Note:** macOS does not support NUMA architectures by default.)
- **On Linux**: The thread pool adjusts based on the system’s CPU architecture. If the system supports **NUMA**, task scheduling and memory access strategies are optimized accordingly.

What is **NUMA**? See here:

- [Non-Uniform Memory Architecture - Fedor Pikus CppNow](https://www.youtube.com/watch?v=f0ZKBusa4CI&t=6s)
- [Frank Denneman's category about NUMA](https://frankdenneman.nl/category/numa/page/3/)

## The POSIX Thread Wrapper

This project mainly provides two `.hpp` header files: one as a simple pthread wrapper library and the other as a thread pool, which varies its behavior depending on the underlying hardware.

POSIX thread API is huge, so the wrapper encapsulates a small part of POSIX thread API using RAII, providing `congzhi::Thread`, `congzhi::Mutex`, `congzhi::LockGuard`, and `congzhi::ConditionVariable` classes. With abstructions, you don't need to worry about the resource leaks. And the provided APIs not only look but also function similar to the standard libraries (std::thread, std::mutex, std::lock_guard, std::condition_variable). Ensuring the seamless usage. For those you can find in [[pthread_wrapper.hpp]].

### `congzhi::Mutex`

This class encapsulates a POSIX mutex type (`pthread_mutex_t`), ensuring proper management for locking and unlocking lock operations similar to `std::mutex`.

#### Constructors and Destructor

This class ensures RAII-style management for the POSIX mutual-exclusion lock. The constructor will initialize the mutex, and the destructor ensures it is always destroyed properly.

Similar to `std::mutex`, `congzhi::Mutex` is neither copyable nor movable.

#### Member Functions

We have four member functions under this class:

- `Lock()`: Locks the mutex. (Comparable to `std::mutex::lock()`)
- `Unlock()`: Ensures the mutex is released. (Comparable to `std::mutex::unlock()`)
- `TryLock()`: Attempts to acquire the mutex without blocking. (Comparable to `std::mutex::try_lock()`)
- `NativeHandle()`: Native handle access, allows retrieval of the underlying `pthread_mutex_t` handle. (Comparable to `std::mutex::native_handle()`)

### `congzhi::LockGuard`

This class provides a straightforward approach to RAII-based resource management. It includes only a constructor and a destructor, ensuring automatic lock acquisition (`.Lock()` operation) and release (`.Unlock()` operation).

This class is not copyable nor movable.

### `congzhi::ConditionVariable`

This class is a encapsulated synchronization tool used to block threads until a particular condition is met.

#### Constructors and Destructor

This class ensures RAII-style management for the POSIX condition variable type (`pthread_cond_t`). Neither copyable nor movable.

#### Member Functions

`congzhi::ConditionVariable` provides you these methods:

- `Wait()`: Wait for the condition variable to be notified, using a mutex.
- `WaitFor()`: Wait for the condition variable to be notified with a timeout.
- `WaitUntil()`: Wait for the condition variable to be notified until a specific time point.
- `NotifyOne()`: Notify one of any waiting thread.
- `NotifyAll()`: Notify all waiting threads.

### `congzhi::Thread`

This class offers a clear alternative to `std::thread`. Similar to the standard thread library, `congzhi::Thread` also supports submitting callable objects of any type and binding them to a thread.

#### Constructors and Destructor

This class ensures RAII-style management for the POSIX thread. It is only movable.

#### Member Functions

The `congzhi::Thread` class provides several member functions exactly like what we have in `std::thread`, these are:

- `GetThreadState()`: For testing usage, returns the state of a thread. We have an enumeration class of `ThreadState`.
- `Joinable()`: To check if the thread is joinable. (Comparable to `std::thread::joinable()`)
- `Join()`: Wait for the thread to finish execution.
- `Detach()`: Permits the thread to run independently. After execution, the thread's resources are released automatically.
- `Swap()`: Swap two threads.
- `Yield()`: Yield the corrent thread.
- `Start()`: Starts a uncreated thread with a callable object and its arguments.
- `GetId()`: Returns the id of the thread.
- `NativeHandle()`: Returns the underlying native thread handle.
- `HardwareConcurrency()`: Returns the number of concurrent threads supported. (POSIX API)

#### `congzhi::Thread::Start()`

`congzhi::Thread` offers a feature that std::thread does not: the `congzhi::Thread::Start()` function, which allows a thread to be started without constructing a new object, this is a signaficant advantage compare to `std::thread`. As demonstrated below:

```cpp
#include <iostream>
#include <thread>
#include "pthread_wrapper.hpp"

int main() {

    // This two thread class both support start a thread when construct the thread object:
    std::thread t1([]() { std::cout << "t1 says hello!\n"; } );
    congzhi::Thread t2([]() { std::cout << "t2 says hello!\n"; } );

    t1.join();
    t2.Join();

    // In std::thread, if you construct a default thread, you must create another thread and move it
    // to the default-constructed thread, like this:
    std::thread t3;
    t3 = std::thread([]() { std::cout << "t3 says hello!\n"; }); // Move assignment

    // With congzhi::Thread, you don’t need to create a temporary thread and move it. congzhi::Thread
    // provides a Start function that allows you to start a thread directly:
    congzhi::Thread t4;
    t4.Start([]() { std::cout << "t4 says hello!\n"; });

    t3.join();
    t4.Join();
    return 0;
}
```

Run this with:

```bash
g++ -pthread -std=c++17 start.cpp -o start && ./start && rm start
```

## The NUMA Thread Pool

