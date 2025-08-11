# NUMA感知的线程池

- [简体中文](README.zh_CN.md)
- [English](README.md)

使用 C++17 实现了一个含有 NUMA 感知的线程池（仅限 Linux）和非 NUMA 感知的线程池（macOS 也可以用）的线程池库以及一个封装 POSIX 标准线程的封装库，旨在能够在 Linux 和 macOS 平台上发挥多核心处理器的最大性能。因为线程池支持 NUMA 架构，相比于其他的线程池实现，NUMA 感知的线程池能够最大化利用 NUMA 系统中节点内存局部性和跨 NUMA 节点延迟的问题。

## 核心组件

本仓库包含三个主要组件：

1. POSIX 线程封装库 (`pthread_wrapper.hpp`) – 采用 C++ 的 RAII 封装 POSIX 线程源语，提供类似于标准库的更安全的接口，同时保留底层操作的性能。这个库封装有**线程**、**互斥锁**、**锁卫**和**条件变量**等。

2. NUMA 封装库 (`numa_wrapper.hpp`) – 提供对 NUMA 拓扑结构对检测和 CPU/内存等 NUMA 相关资源等管理工具，以实现最佳的 NUMA 数据局部性。（仅限 Linux）

3. 线程池库 (`thread_pool.hpp`) – 提供灵活且高性能的线程池。在 Linux 上指出 NUMA 感知调度的线程池，在 Linux/macOS 上提供标准线程池供使用。

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

- `congzhi::Thread`: 基于 `pthread` 的可移动 RAII 线程封装，提供安全易用的线程接口，功能类似于 `std::thread`，但相比之下，`congzhi::Thread`支持通过`ThreadState`枚举线程安全地显示获取线程的状态，同时集成专用的 `ThreadAttribute` 类来为 `pthread` 配置特定属性（`congzhi::Thread` 底层就是 `pthread`）。在析构时，自动分离可连接的线程以防止资源泄漏（类似于std::jthread）。此外，它提供了 `congzhi::Thread::Start()` 方法，允许线程的延迟启动。以下是几个使用 `congzhi::Thread` 的示例：

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

### 2. NUMA Wrapper
