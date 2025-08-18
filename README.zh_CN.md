# NUMA感知的线程池

- [简体中文](README.zh_CN.md)
- [English](README.md)

使用 C++17 实现了一个含有 NUMA 感知的线程池（仅限 Linux）和非 NUMA 感知的线程池（macOS 也可以用）的线程池库，旨在在 Linux 和 macOS 平台上发挥多核心处理器的最大性能。因为线程池支持 NUMA 架构，相比于其他的线程池实现，NUMA 感知的线程池能够最大化利用 NUMA 系统中节点内存局部性和跨 NUMA 节点延迟的问题。

## 核心组件

本仓库包含三个主要组件：

1. POSIX 线程封装库 (`pthread_wrapper.hpp`) – 采用 C++ 的 RAII 封装 POSIX 线程源语，提供类似于标准库的更安全的接口，同时保留底层操作的性能。这个库封装有**线程**、**互斥锁**、**锁卫**和**条件变量**等。

2. NUMA 封装库 (`numa_namespace.hpp`) – 提供对 NUMA 拓扑结构对检测和 CPU/内存等 NUMA 相关资源等管理工具，以实现最佳的 NUMA 数据局部性。（仅限 Linux）

3. 线程池库 (`thread_pool.hpp`) – 提供灵活且高性能的线程池。在 Linux 上支持 NUMA 感知调度的线程池，在 Linux/macOS 上提供标准线程池供使用。

### 1. POSIX 线程封装库 (`pthread_wrapper.hpp`)

这个库封装 POSIX 线程库中的数据结构，提供安全、现代的 C++ 类。库提供下面这些类：

- `congzhi::Mutex`: 对 `pthread_mutex_t` 的 RAII 管理。 在构造函数中初始化互斥锁，并在析构函数中销毁，确保了即使异常发生也能正确地清理资源。这个类提供 `Lock()`, `Unlock()`, `TryLock()`, 和 `NativeHandle()` 方法来控制互斥锁。下面是一个在多线程环境下使用 `congzhi::Mutex` 来保护临界区的简单示例:

```cpp
#include <iostream>
#include <thread>
#include "../pthread_wrapper.hpp"

void LockForSeconds(congzhi::Mutex* mutex, int seconds) {
    mutex->Lock();
    std::cout << "线程 " << std::this_thread::get_id() << " 获取锁，将持有 " << seconds << " 秒。" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    mutex->Unlock();
    std::cout << seconds << " 秒后，线程 " << std::this_thread::get_id() << " 释放锁。" << std::endl;
}

void TryLocking(congzhi::Mutex* mutex) {
    while (!mutex->TryLock()) {
        std::cout << "线程 " << std::this_thread::get_id() << " 获取锁失败，重试中..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "线程 " << std::this_thread::get_id() << " 成功获取锁！" << std::endl;
    mutex->Unlock();
}

int main() {
    congzhi::Mutex mutex;
    std::cout << "启动线程演示congzhi::Mutex互斥锁..." << std::endl;
    std::thread t1(LockForSeconds, &mutex, 3);
    std::thread t2(TryLocking, &mutex);

    t1.join();
    t2.join();
    std::cout << "两个线程均已完成。" << std::endl;

    return 0;
}
```

- `congzhi::LockGuard`: 典型的 RAII 辅助类，通过作用域绑定 `Lock()` 和 `Unlock()` 方法，在构造时锁定关联的 `TLock` （任意类型的锁），然后在析构时调用 `TLock::Unlock()` 自动解锁。下面是一个很简单的例子：

```cpp
#include <iostream>
#include "../pthread_wrapper.hpp"
class AnyLock {
public:
  void Lock() {
    std::cout << "已锁定" << std::endl;
  }
  void Unlock() {
    std::cout << "已解锁" << std::endl;
  }
};

int main() {
  AnyLock any_lock;
  // Using congzhi::LockGuard with AnyLock
  congzhi::LockGuard<AnyLock> lock_guard(any_lock);
  return 0;
}
```

- `congzhi::ConditionVariable`: : 基于对 `pthread_cond_t` 的封装来提供等待/通知机制，使得线程可以阻塞到特定条件被满足。同样的，该类支持 RAII 风格的资源管理，在构造函数中初始化，并在析构函数中销毁对象并释放资源。提供 `Wait()`, `WaitFor()`, `WaitUntil()`, `NotifyOne()`, `NotifyAll()` 方法。有了条件变量，我们就可以使用它来实现一个简单的屏障：

```cpp
#include <iostream>
#include "../pthread_wrapper.hpp"
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

// 使用条件变量的MOBA游戏示例，只有当所有玩家准备就绪后，游戏才开始。（屏障）
std::atomic<int> ready_players = 0;
const int k_total_players = 10;

void Player(congzhi::ConditionVariable* cond_system, // 系统到玩家的通知
            congzhi::ConditionVariable* cond_player, // 玩家到系统的通知
            congzhi::Mutex* mutex) {

    std::this_thread::sleep_for(std::chrono::milliseconds(100 + rand() % 500));
    
    {
        congzhi::LockGuard<congzhi::Mutex> lock(*mutex);
        ready_players++;
        std::cout << "玩家 " << std::this_thread::get_id() 
                  << " 已准备就绪！（" << ready_players << "/" << k_total_players << "）" << std::endl;
    }
    
    cond_player->NotifyOne(); // 一个玩家准备就绪后通知系统
    
    congzhi::LockGuard<congzhi::Mutex> lock(*mutex); // 等待其他玩家
    cond_system->Wait(*mutex, [](){ 
        return ready_players == k_total_players; 
    });
    
    std::cout << "玩家 " << std::this_thread::get_id() << " 进入游戏！" << std::endl;
}

void System(congzhi::ConditionVariable* cond_system, // 系统到玩家的通知
            congzhi::ConditionVariable* cond_player, // 玩家到系统的通知
            congzhi::Mutex* mutex) {
    congzhi::LockGuard<congzhi::Mutex> lock(*mutex);
    std::cout << "系统等待所有玩家准备就绪..." << std::endl;
    
    // 等待所有玩家准备就绪
    cond_player->Wait(*mutex, [](){ 
        return ready_players == k_total_players; 
    });
    
    std::cout << "\n所有玩家准备就绪！游戏开始..." << std::endl;
    cond_system->NotifyAll();
}

int main() {
    congzhi::Mutex mutex;
    congzhi::ConditionVariable cond_system;  // 系统到玩家的通知
    congzhi::ConditionVariable cond_player;  // 玩家到系统的通知

    std::vector<std::thread> threads;

    threads.emplace_back(System, &cond_system, &cond_player, &mutex);    
    
    for (int i = 0; i < k_total_players; ++i) {
        threads.emplace_back(Player, &cond_system, &cond_player, &mutex);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "\n游戏成功启动！" << std::endl;
    return 0;
}
```

- `congzhi::Thread`: 基于 `pthread_t` 的可移动 RAII 线程封装，提供安全易用的线程接口，功能类似于 `std::thread`，但相比之下，`congzhi::Thread`支持通过`ThreadState`枚举线程安全地显示获取线程的状态，同时集成专用的 `ThreadAttribute` 类来为 `pthread` 配置特定属性（`congzhi::Thread` 底层就是 `pthread_t`）。在析构时，自动分离/加入/终止/取消可连接的线程以防止资源泄漏（类似于`std::jthread`）。此外，它提供了 `congzhi::Thread::Start()` 方法，允许线程的延迟启动。下面我会提供几个使用 `congzhi::Thread` 的示例。（由于线程析构行为和线程属性的参数是后面加的（带默认值），所以现在可变参数并不可用，所以你需要自己手动地绑定参数）

```cpp
// 关于Start()方法：

#include <iostream>
#include <thread>
#include "../pthread_wrapper.hpp"

int main () {
  // 立即执行的线程启动，两种实现都支持
  std::thread std_t1([]() {std::cout << "线程1正在运行。" << std::endl;});
  congzhi::Thread congzhi_t1([]() {std::cout << "Congzhi线程1正在运行。" << std::endl;});
  std_t1.join();
  congzhi_t1.Join();

  // 移动构造，非延迟启动，两种实现都支持
  std::thread std_t2;
  std_t2 = std::thread([]() {std::cout << "线程2正在运行。" << std::endl;});
  congzhi::Thread congzhi_t2;
  congzhi_t2 = congzhi::Thread([]() {std::cout << "Congzhi线程2正在运行。" << std::endl;});
  std_t2.join();
  congzhi_t2.Join();

  // 延迟启动，无需移动构造，std::thread不支持
  congzhi::Thread congzhi_t3;
  congzhi_t3.Start([]() {std::cout << "Congzhi线程3正在运行。" << std::endl;});
  congzhi_t3.Join();
}
```

```cpp
// congzhi::Thread 线程类不同的析构实现，这里仅作不同析构函数析构时的行为示例。因为析构函数中也是调用这些函数。你可以把主线程中的等待和显式调用方法取掉观察程序执行行为。

#include <iostream>
#include "../pthread_wrapper.hpp"

int main()
{
    congzhi::Mutex mutex;

    // 创建线程，使用默认的线程析构方式（JOIN）
    {
        congzhi::LockGuard<congzhi::Mutex> lock(mutex);
        congzhi::Thread thread1([]()
                                { std::cout << "Thread 1 is running." << std::endl; });
        thread1.Join();                                     // 提前显示加入线程
        std::cout << "The thread's state is: "<< thread1.GetThreadState() // 线程已经 Join() 了，所以线程的状态是 TERMINATED 了
                  << "\nFinishes its job? " << (thread1.IsCompleteExecution() ? "Yes" : "No") // 当前的主线程会阻塞等待 thread1 执行结束，所以这里的线程执行完毕了，所以这里应当是 Yes
                  << std::endl; 
    }

    // 使用 JOIN 的析构行为创建线程
    // 析构时，当前线程就会阻塞等待 thread2 加入
    {
        congzhi::LockGuard<congzhi::Mutex> lock(mutex);
        congzhi::Thread thread2([]()
                                { std::cout << "Thread 2 is running." << std::endl; }, congzhi::Thread::DtorAction::JOIN);
        congzhi::this_thread::SleepFor(std::chrono::milliseconds(100)); // 确保线程执行完毕
        std::cout << "The thread's state is: "<< thread2.GetThreadState() // 因为直到析构函数才会 Detach 线程，所以这里仍是 JOINABLE 的状态
                  << "\nFinishes its job? " << (thread2.IsCompleteExecution() ? "Yes" : "No") // 因为我们确保了线程执行完毕，所以线程已经执行完毕了，这里应当是 Yes
                  << std::endl; 
    }

    // 使用 DETACH 的析构行为创建线程
    // 析构时当前线程就会将析构行为的线程分离。分离后，线程资源将被操作系统接管，主线程不需要等待线程完成，也不必担心线程资源泄漏问题
    {
        congzhi::LockGuard<congzhi::Mutex> lock(mutex);
        congzhi::Thread thread3([]()
                                { std::cout << "Thread 3 is running." << std::endl; }, congzhi::Thread::DtorAction::DETACH);
        congzhi::this_thread::SleepFor(std::chrono::milliseconds(100)); // 确保线程执行完毕
        std::cout << "The thread's state is: "<< thread3.GetThreadState() // 由于直到析构函数才会 Detach 线程，所以这里的状态仍然是 JOINABLE
                  << "\nFinishes its job? " << (thread3.IsCompleteExecution() ? "Yes" : "No") // 因为我们确保了线程执行完毕，所以线程已经执行完了，所以这里应当是 Yes
                  << std::endl; 
    }

    // 使用 CANCEL 的析构行为创建线程
    // 在这种情况下，线程执行函数中必须提供取消点以确保线程取消，否则死锁给你看
    {
        congzhi::LockGuard<congzhi::Mutex> lock(mutex);
        congzhi::Thread thread4([]()
                                {            
            while (true) {
                std::cout << "Thread 4 is running." << std::endl;
                congzhi::this_thread::SleepFor(std::chrono::milliseconds(10));
            } }, congzhi::Thread::DtorAction::CANCEL);
        congzhi::this_thread::SleepFor(std::chrono::milliseconds(100)); // 确保 thread4 先运行
        thread4.Cancel();
        std::cout << "The thread's state is: "<< thread4.GetThreadState() // 由于线程被取消掉了，所以这里应当输出 TERMINATED
                  << "\nFinishes its job? " << (thread4.IsCompleteExecution() ? "Yes" : "No") // 因为线程在执行过程中被取消，所以线程没有执行完毕，这里是 No
                  << std::endl; 
    }

    // 使用 TERMINATE 的析构行为创建线程，在析构时会像目标线程发送信号
    // 在这种方式下，线程执行函数中必须注册信号服务例程来优雅退出（可以在服务例程中释放线程申请的资源）。
    // 如果不注册信号服务例程，那么信号就会终止整个进程
    {
        congzhi::LockGuard<congzhi::Mutex> lock(mutex);
        congzhi::Thread thread5([]() {
            // 注册服务例程，服务例程中输出一句话之后 exit()
            struct sigaction sa;
            sa.sa_handler = [](int) {
            std::cout << "Thread 5 exiting gracefully." << std::endl;
            pthread_exit(nullptr);
            };
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;
            sigaction(SIGTERM, &sa, nullptr);

            // 线程的主循环
            while (true) {
                std::cout << "Thread 5 is running." << std::endl;
                congzhi::this_thread::SleepFor(std::chrono::milliseconds(10));
        } }, congzhi::Thread::DtorAction::TERMINATE);

        congzhi::this_thread::SleepFor(std::chrono::milliseconds(100)); // 确保 thread5 先运行
        try
        {
            thread5.Terminate(); // 终止线程
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error terminating thread: " << e.what() << std::endl;
        }
        std::cout << "The thread's state is: "<< thread5.GetThreadState() // 获取终止后的线程状态，应当为 TERMINATED
                  << "\nFinishes its job? " << (thread5.IsCompleteExecution() ? "Yes" : "No") // 由于线程执行的是死循环，所以线程不可能完成执行。这里应当是 No
                  << std::endl; 
    }
    std::cout << "All threads have been cleaned up." << std::endl; // 全部例子完成
    return 0;
}
```

```cpp
#include <iostream>
#include "../pthread_wrapper.hpp"

int main() {
    congzhi::ThreadAttribute attr; // 声明一个线程属性对象

    // 设置属性
    attr.SetStackSize(4 * 1024 * 1024); // 以字节为单位的栈大小（默认 8MB）
    attr.SetScope(congzhi::Scope::SYSTEM); // 线程竞争 CPU 的范围（默认系统内竞争），也可以设置 Scope::PROCESS，与进程内的线程竞争
    attr.SetDetachState(congzhi::DetachState::JOINABLE); // 默认线程是可加入（也可以设置 DetachState::DETACHED，规定只能分离线程）
    attr.SetSchedulingPolicy(congzhi::SchedulingPolicy::DEFAULT, 0); // 设置线程的调度策略和线程优先级（还可以设置 SchedulingPolicy::FIFO/Scheduling::RR）
    std::cout << "Thread attributes set successfully." << std::endl;

    congzhi::Thread t1(
        [&](){
            congzhi::this_thread::SleepFor(std::chrono::seconds(1));
            std::cout << "Thread is running with custom attributes." << std::endl;
            std::cout << "Thread ID: " << pthread_self() << std::endl;
            std::cout << "Thread stack size:" << attr.GetStackSize() << std::endl;
            std::cout << "Thread scope: " << (attr.GetScope() == congzhi::Scope::System ? "SYSTEM" : "PROCESS") << std::endl;
            std::cout << "Thread detach state: " << (attr.GetDetachState() == congzhi::DetachState::Detached ? "DETACHED" : "JOINABLE") << std::endl;
            std::cout << "Thread scheduling policy: " << (attr.GetSchedulingPolicy() == congzhi::SchedulingPolicy::Default ? "DEFAULT" : "Custom") << std::endl;
        },
        congzhi::DtorAction::DETACH, // 设置线程的析构行为是 DETACH
        attr
    );
    congzhi::this_thread::SleepFor(std::chrono::seconds(2));
    try {
        t1.Join(); // 虽然线程可加入，但是我们设置了线程的析构行为是 DETACH，所以强行加入会导致逻辑错误
    } catch (const std::logic_error& e) {
        std::cerr << "Logic error: " << e.what() << std::endl;
    }
    return 0;
}
```

### 2. NUMA 命名空间

在这个命名空间下的函数并不太多，也没有经过 RAII 的封装，所以你暂时可能需要自己管理一下 NUMA 节点上的内存申请和释放。命名空间下的函数足够我们去检查 NUMA 节点是否可用、绑定线程在特定节点上、申请特定 NUMA 节点上的内存等。

```cpp
#include <iostream>
#include "../pthread_wrapper.hpp"
#include "../numa_namespace.hpp"


int main () {
    if (!congzhi::numa::IsNumaSupported()) {
        std::cout << "NUMA is not supported on this machine." << std::endl;
    }
    std::cout << "The number of NUMA nodes available on the system: " << congzhi::numa::NumaNodeCount() << std::endl
              << "And the number of total CPU count available on the system: " << congzhi::numa::GetTotalCpuCount() << std::endl;
    
    auto node_mask = congzhi::numa::GetNumaNodeMask();
    congzhi::numa::PrintNumaNodeMask(node_mask);
    
    std::cout << "\nI can check the info of NUMA node for you.\n"
                  "Which node would you like to check? (node num starts at 0)\n"
                  "Node: ";
    int num;
    std::cin >> num;
    try {
        // The node available is the online node that total memory is non-zero.
        std::cout << "\nNode " << num << " online?   \t" << (congzhi::numa::IsNumaNodeOnline(num) ? "Online" : "Not Online")
                << "\nNode " << num << " available? \t" << (congzhi::numa::IsNumaNodeAvailable(num) ? "Available" : "Not Available")
                << "\nCPU num on node "   << num << ":\t" << congzhi::numa::GetCpuCountOnNode(num);
        auto mem_info = congzhi::numa::GetNodeMemoryInfo(num);
        std::cout << "\nLocal memory info of node " << num << ":" 
                << "\n\tTotal memory:\t" << mem_info.total_ / (1024 * 1024) << "MB"
                << "\n\tUsed  memory: \t" << mem_info.used_ / (1024 * 1024) << "MB"
                << "\n\tFree  memory: \t" << mem_info.free_ / (1024 * 1024) << "MB"
                << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

}
```

```cpp
#include <iostream>
#include "../pthread_wrapper.hpp"
#include "../numa_namespace.hpp"


void MonitorMemory(const int numa_node) {
    for (;;) {
        auto mem_info = congzhi::numa::GetNodeMemoryInfo(numa_node);
        std::cout << "\nLocal memory info of node " << numa_node << ":" 
                << "\n\tTotal memory:\t" << mem_info.total_ / (1024 * 1024) << "MB"
                << "\n\tUsed  memory: \t" << mem_info.used_ / (1024 * 1024) << "MB"
                << "\n\tFree  memory: \t" << mem_info.free_ / (1024 * 1024) << "MB"
                << std::endl;
        
        congzhi::this_thread::SleepFor(1000);
    }    
}

int main () {
    const int numa_node = 0;
    try {
        auto bound_task = std::bind(MonitorMemory, 0);
        congzhi::Thread t(
            bound_task, 
            congzhi::DtorAction::CANCEL
        );
        
        // Allocate memory
        const size_t size = static_cast<size_t>(512 * 1024 * 1024);
        std::cout << "Allocating " << size / (1024 *1024) << " MB\n";
        void* memory = congzhi::numa::AllocateMemoryOnNode(size, 0);
        std::cout << "Memory allocated at " << memory << "\n";
        congzhi::this_thread::SleepFor(2000);

        // Access memory
        char* char_mem = static_cast<char*>(memory);
        for(size_t i = 0; i < size; i += 4096) {
            char_mem[i] = i;
        }
        std::cout << "Memory initialized\n";
        congzhi::this_thread::SleepFor(2000);

        // Free memory
        congzhi::numa::FreeMemory(memory, size);
        std::cout << "Memory Freed.\n";
        congzhi::this_thread::SleepFor(2000);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}
```
