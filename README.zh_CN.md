# README

- [English](README.md)
- [简体中文](README.zh_CN.md)

本项目实现了一个**跨平台**、 **NUMA 感知**的线程池，可在 **macOS** 和 **Linux** 上运行，线程池的行为会根据操作系统所封装的底层 CPU 硬件进行调整。

- 在 **macOS** 上：因为 Apple Silicon 采用**统一内存架构 (UMA)** ，且 macOS 默认不支持 NUMA 架构，所以线程池在 macOS 上的表现与普通的线程池相同（无 NUMA 适配）。
- 在 **Linux** 平台上：线程池会根据系统的 CPU 架构调整其行为。如果 CPU 有多个 NUMA 节点，那么线程池就会表现为一个 NUMA 感知的线程池。否则将表现为一个普通的线程池。

在 NUMA 感知的线程池中，线程池中的所有线程都会被绑定到一个 NUMA 节点上，以最大化**内存局部性**，提升**缓存命中率**，减少**远程内存访问开销**，从而优化系统的整体性能。

如果您对 NUMA 架构尚不熟悉，请看：[Non-Uniform Memory Architecture - Fedor Pikus CppNow](https://www.youtube.com/watch?v=f0ZKBusa4CI&t=6s)

## The POSIX Thread Wrapper

本项目提供两个 .hpp 头文件库，一个是轻量级的 pthread 封装库，提供更加高级易用的 API 。还有就是 NUMA 感知的线程池库，根据底层硬件架构优化任务调度。

封装的线程（`congzhi::Thread`）、互斥锁（`congzhi::Mutex`）、条件变量（`congzhi::ConditionVariable`）等均定义在 [[pthread_wrapper.hpp]] 文件中。这些组件对 pthread 进行了 RAII 的封装，防止资源泄漏，同时提供类似标准库的 API ，便于上手。

### `congzhi::Thread`

`congzhi::Thread` 类提供类似 `std::thread` 的功能，支持线程对范型可调用对象的绑定。同时提供更佳灵活的线程启动方式，支持线程预先创建，并通过 `Start()` 方法动态分配任务（延迟构建）。以下是两种线程库提供的构建方式：

```cpp
#include <iostream>
#include <thread>
#include "pthread_wrapper.hpp"

int main() {

    // 两种线程都支持在构建的时候启动线程执行：
    std::thread t1([]() { std::cout << "t1 says hello!\n"; } );
    congzhi::thread t2([]() { std::cout << "t2 says hello!\n"; } );


    // 如果要默认构建一个 std::thread 对象（即不代表任何线程的对象），
    // 你只能通过构建一个临时的线程对象，然后通过移动的方式来启动：
    std::thread t3;
    t3 = std::thread([]() { std::cout << "t3 says hello!\n"; }); // Move assignment
    t3.join();

    // congzhi::Thread 可以不用构建临时的线程对象，这里提供 Start
    // 方法直接启动一个线程。
    congzhi::Thread t4;
    t4.Start([]() { std::cout << "t4 says hello!\n"; });
    t4.Join();
    return 0;
}
```