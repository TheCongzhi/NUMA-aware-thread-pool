# README

- [English](README.md)
- [简体中文](README.zh_CN.md)

This project implements a **thread pool** that runs on **macOS** and **Linux**. The behavior of the thread pool adapts depending on the underlying CPU hardware.

- **On macOS**: Since Apple Silicon uses **Unified Memory Architecture (UMA)**, the thread pool defaults to a UMA-based design.  
  (Note: **macOS does not support NUMA architectures by default**.)

- **On Linux**: The thread pool adjusts based on the system’s CPU architecture. If the system supports **NUMA**, task scheduling and memory access strategies are optimized accordingly.

What is **NUMA**? See here: [Non-Uniform Memory Architecture - Fedor Pikus CppNow](https://www.youtube.com/watch?v=f0ZKBusa4CI&t=6s)

## The POSIX Thread Wrapper

This project mainly provides two header (.hpp) files: one as a simple pthread wrapper library and the other as a thread pool, which varies its behavior depending on the underlying hardware.

The wrapper encapsulates a small part of POSIX thread API using RAII, providing `congzhi::Thread`, `congzhi::Mutex`, `congzhi::LockGuard`, and `congzhi::ConditionVariable` classes. With these abstructions, you don't need to worry about the resource leaks. And the provided APIs not only look but also function similar to the standard libraries (std::thread, std::mutex, std::lock_guard, std::condition_variable). Ensuring the seamless usage.

### `congzhi::Thread`

The `congzhi::Thread` class supports submitting callable objects of any type and binding them to a thread. The main difference between this `std::thread` and `congzhi::Thread` is how they construct the thread object:

```cpp
#include <iostream>
#include <thread>
#include "pthread_wrapper.hpp"

int main() {

    // This two thread class both support start a thread when construct the thread object:
    std::thread t1([]() { std::cout << "t1 says hello!\n"; } );
    congzhi::thread t2([]() { std::cout << "t2 says hello!\n"; } );


    // In std::thread, if you construct a default thread, you must create another thread and move it
    // to the default-constructed thread, like this:
    std::thread t3;
    t3 = std::thread([]() { std::cout << "t3 says hello!\n"; }); // Move assignment
    t3.join();

    // With congzhi::Thread, you don’t need to create a temporary thread and move it. congzhi::Thread
    // provides a Start function that allows you to start a thread directly:
    congzhi::Thread t4;
    t4.Start([]() { std::cout << "t4 says hello!\n"; });
    t4.Join();
    return 0;
}
```
