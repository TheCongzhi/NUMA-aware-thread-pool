# README

- [简体中文](README.zh_CN.md)
- [English](README.md)

本项目实现了一个**跨平台**、 **NUMA 感知**的线程池，可在 **macOS** 和 **Linux** 上运行，线程池的行为会根据操作系统所封装的底层 CPU 硬件进行调整。

- **macOS**：因为 Apple Silicon 采用**统一内存架构 (UMA)** ，且 macOS 默认不支持 NUMA 架构，所以线程池在 macOS 上的表现与普通的线程池相同（无 NUMA 适配）。
- **Linux**：线程池会根据系统的 CPU 架构调整其行为。如果底层 CPU 硬件上 NUMA 架构的（有多个 NUMA 节点），那么线程池就会表现为一个 NUMA 感知的线程池。否则将表现为一个普通的线程池。

如果您对 NUMA 架构尚不熟悉，请看：

- [Non-Uniform Memory Architecture - Fedor Pikus CppNow](https://www.youtube.com/watch?v=f0ZKBusa4CI&t=6s)
- [Frank Denneman's category about NUMA](https://frankdenneman.nl/category/numa/page/3/)

## The POSIX Thread Wrapper

本项目提供两个 `.hpp` 头文件库（header only），一个是轻量的 pthread 封装库，提供更加高级易用的 API 。还有就是 NUMA 感知的线程池库，根据底层硬件架构优化任务调度。

POSIX thread 的功能非常强大，封装库也只封装了 pthread API 中的一小部分，库提供封装好的线程类（`congzhi::Thread`）、互斥锁类（`congzhi::Mutex`）、锁卫类（`congzhi::LockGuard`）和条件变量类（`congzhi::ConditionVariable`）。这些组件对 pthread 进行了 RAII 的封装，所以不用担心资源泄漏，同时提供与 C++ 标准库相似的 API ，便于上手。这些组件定义在 [[pthread_wrapper.hpp]] 文件中。

### `congzhi::Mutex`

这个类封装了一个 POSIX 的 `pthread_mutex_t`，提供类似于 `std::mutex` 的功能。

#### Constructors and Destructor

`Mutex` 类使用 RAII 来管理 POSIX 的互斥锁，在构造时初始化，并在析构时自动销毁 POSIX mutex，确保资源能够正确释放。不允许复制或移动操作。

#### Member Functions

`congzhi::Mutex` 提供四个成员函数，分别是：

- `Lock()`: 加锁互斥锁。（相较于 `std::mutex::lock()`）
- `Unlock()`: 解锁互斥锁。（相较于 `std::mutex::unlock()`）
- `TryLock()`: 尝试加锁，不会阻塞线程。（相较于 `std::mutex::try_lock()`）
- `NativeHandle()`: 返回底层的 `pthread_mutex_t` 原始句柄。 （相较于 `std::mutex::native_handle()`）

### `congzhi::LockGuard`

锁卫类提供对任意锁类型（需提供 `.Lock()` 方法和 `.Unlock()` 方法）的加锁和解锁操作，这个类中只有一个构造函数和一个析构函数，锁卫在构造时会自动地调用锁类型中的 `.Lock()` 方法，在析构时会自动地调用锁类型中的 `.Unlock()` 方法。非常典型直接的 RAII 包装器的实践。

这个类也不可拷贝和移动。

### `congzhi::ConditionVariable`

对 `pthread_cond_t` 的封装，用于提供便捷的线程同步控制，用来阻塞线程并在条件被满足时将线程置于就绪队列。

#### Constructors and Destructor

同样的，这个类也提供 RAII 风格的资源管理。

不可拷贝和移动。

#### Member Functions

本类提供以下方法：

- `Wait()`: 使用互斥锁，等待条件被满足。（无限等待）
- `WaitFor()`: 在一个时间范围内等待线程。
- `WaitUntil()`: 等待到一个指定的时间点。
- `NotifyOne()`: 随机唤醒一个等待线程。
- `NotifyAll()`: 唤醒所有等待线程。

详见封装库。

### `congzhi::Thread`

`congzhi::Thread` 类提供类似 `std::thread` 的功能，支持线程对范型可调用对象的绑定。

#### Constructors and Destructor

这个类时对 POSIX 线程类型的封装。不可拷贝，可移动。

#### Member Functions

`congzhi::Thread` 提供多个成员函数。如下：

- `GetThreadState()`: 测试用途，返回当前线程的状态。在封装库中，我们用一个枚举类来表示线程的不同状态。
- `Joinable()`: 检查线程是否可Join。（相较于 `std::thread::joinable()`）
- `Join()`: 等待线程执行结束。（相较于 `std::thread::join()）
- `Detach()`: 准许线程独立运行，在分离线程后，线程资源就会被系统接管）
- `Swap()`: 交换两个线程。（相较于 `std::thread::swap()`）
- `Yield()`: 让当前执行线程放弃CPU。（相较于 `std::thread::yield()`）
- `Start()`: 创建一个新的线程并执行。
- `GetId()`: 获得当前线程的TID。（相较于 `std::thread::get_id`）
- `NativeHandle()`: 获得当前线程的原始句柄。（相较于 `std::thread::native_handle()`）
- `HardwareConcurrency()`: 返回系统的CPU核心数。（相较于 `std::thread::hardware_concurrency`）

#### `congzhi::Thread::Start()`

相比标准线程库，这里提供更佳灵活的线程启动方式，支持线程预先创建，并通过 `Start()` 方法动态分配任务（延迟构建）。以下是两种线程库提供的构建方式：

```cpp
#include <iostream>
#include <thread>
#include "pthread_wrapper.hpp"

int main() {

    // 两种线程都支持在构建的时候启动线程执行：
    std::thread t1([]() { std::cout << "t1 says hello!\n"; } );
    congzhi::Thread t2([]() { std::cout << "t2 says hello!\n"; } );

    t1.join();
    t2.Join();

    // 如果要默认构建一个 std::thread 对象（即不代表任何线程的对象），
    // 你只能通过构建一个临时的线程对象，然后通过移动的方式来启动：
    std::thread t3;
    t3 = std::thread([]() { std::cout << "t3 says hello!\n"; }); // Move assignment

    // congzhi::Thread 可以不用构建临时的线程对象，这里提供 Start
    // 方法直接启动一个线程。
    congzhi::Thread t4;
    t4.Start([]() { std::cout << "t4 says hello!\n"; });
    
    t3.join();
    t4.Join();
    return 0;
}
```

## The NUMA Thread Pool