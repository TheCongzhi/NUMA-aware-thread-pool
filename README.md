# NUMA-Aware Thread Pool

- [English](README.md)
- [简体中文](README.zh_CN.md)

A modern C++17 thread pool library featuring a Linux‑only NUMA‑aware pool—built to maximize memory locality and minimize cross‑node latency—plus a portable non‑NUMA pool and POSIX pthread wrappers, designed for optimal performance accross multi-core architectures on Linux and macOS. With explicit support for NUMA architectures, this implementation addresses the unique challenges for memory locality and cross-NUMA-node latency in multi CPU socket systems(NUMA systems).

## Core Components

This repository contains three main components:

1. POSIX Thread Wrapper (`pthread_wrapper.hpp`) – RAII‑style C++ wrapper for POSIX threading primitives, offering standard‑library‑like interfaces for threads, mutexes, lock guards, and condition variables while preserving the performance of low‑level pthread operations.

2. NUMA Wrapper (`numa_wrapper.hpp`) – Utilities to detect NUMA topology and manage CPU/memory binding for optimal data locality (Linux‑only).

3. Thread Pool (`thread_pool.hpp`) – Flexible, high‑performance pool with NUMA‑aware scheduling for Linux, and a standard mode for Linux/macOS..

### 1. POSIX Thread Wrapper (`pthread_wrapper.hpp`)

Encapsulates POSIX threading into safe, modern C++ classes. There are some key classes in the scope:

- `congzhi::Mutex`: RAII-style lock management for pthread_mutex_t. Initializes the mutex in the constructor and destroys it in the destructor, ensuring proper cleanup even in the presence of exceptions. Provides `Lock()`, `Unlock()`, `TryLock()`, and `NativeHandle()` methods for explicit control over locking. Protecting critical sections in multithreaded applicatinos, here's an easy example:

```cpp
// examine congzhi::Mutex

#include <iostream>
#include <thread>
#include "../pthread_wrapper.hpp"

void LockForSeconds(congzhi::Mutex* mutex, int seconds) {
    mutex->Lock();
    std::cout << "Thread " << std::this_thread::get_id() << " acquired lock for " << seconds << " seconds." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    mutex->Unlock();
    std::cout << "Lock released after " << seconds << " seconds by thread" << std::this_thread::get_id() << std::endl;
}

void TryLocking(congzhi::Mutex* mutex) {
    while (!mutex->TryLock()) {
        std::cout << "Thread " << std::this_thread::get_id() << " failed to acquire lock, trying again..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "Thread " << std::this_thread::get_id() << " acquired the lock!" << std::endl;
    mutex->Unlock();
}

int main() {
    congzhi::Mutex mutex;
    std::cout << "Starting threads to demonstrate mutex locking..." << std::endl;
    std::thread t1(LockForSeconds, &mutex, 3);
    std::thread t2(TryLocking, &mutex);

    t1.join();
    t2.join();
    std::cout << "Both threads completed." << std::endl;

    return 0;
}
```

- `congzhi::LockGuard`: Scope‑bounded `Lock`/`Unlock` helper. Locks the associated `TLock` (any types of lock) upon construction and automatically unlocks it upon destruction, ensuring exception‑safety.

```cpp
#include <iostream>
#include "../pthread_wrapper.hpp"
class AnyLock {
public:
  void Lock() {
    std::cout << "Locked" << std::endl;
  }
  void Unlock() {
    std::cout << "Unlocked" << std::endl;
  }
};

int main() {
  AnyLock any_lock;
  // Using congzhi::LockGuard with AnyLock
  congzhi::LockGuard<AnyLock> lock_guard(any_lock);
  return 0;
}
```

- `congzhi::ConditionVariable`: : Provides wait/notify synchronization built on top of `pthread_cond_t`, enabling threads to efficiently block until a certain condition is met. The object is initialized in the constructor and destroyed in the destructor, ensuring RAII-style resource management. `Wait()`, `WaitFor()`, `WaitUntil()`, `NotifyOne()`, `NotifyAll()` methods are provided. Here's an example applying `congzhi::ConditionVariable` to complete a barrier:

```cpp
#include <iostream>
#include "../pthread_wrapper.hpp"
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

// MOBA game example using Condition Variables, only when all players are ready, the game starts. (Barrier)
std::atomic<int> ready_players = 0;
const int k_total_players = 10;

void Player(congzhi::ConditionVariable* cond_system, // system->player
            congzhi::ConditionVariable* cond_player, // player->system
            congzhi::Mutex* mutex) {

    std::this_thread::sleep_for(std::chrono::milliseconds(100 + rand() % 500));
    
    {
        congzhi::LockGuard<congzhi::Mutex> lock(*mutex);
        ready_players++;
        std::cout << "Player " << std::this_thread::get_id() 
                  << " ready (" << ready_players << "/" << k_total_players << ")" << std::endl;
    }
    
    cond_player->NotifyOne(); // one player notifies the system due to readiness
    
    congzhi::LockGuard<congzhi::Mutex> lock(*mutex); // wait for other players
    cond_system->Wait(*mutex, [](){ 
        return ready_players == k_total_players; 
    });
    
    std::cout << "Player " << std::this_thread::get_id() << " enters game!" << std::endl;
}

void System(congzhi::ConditionVariable* cond_system, // system->player
            congzhi::ConditionVariable* cond_player, // player->system
            congzhi::Mutex* mutex) {
    congzhi::LockGuard<congzhi::Mutex> lock(*mutex);
    std::cout << "System waiting for all players..." << std::endl;
    
    // 
    cond_player->Wait(*mutex, [](){ 
        return ready_players == k_total_players; 
    });
    
    std::cout << "\nAll players ready! Starting game..." << std::endl;
    cond_system->NotifyAll();
}

int main() {
    congzhi::Mutex mutex;
    congzhi::ConditionVariable cond_system;  // system->player
    congzhi::ConditionVariable cond_player;  // player->system

    std::vector<std::thread> threads;

    threads.emplace_back(System, &cond_system, &cond_player, &mutex);    
    
    for (int i = 0; i < k_total_players; ++i) {
        threads.emplace_back(Player, &cond_system, &cond_player, &mutex);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "\nGame started successfully!" << std::endl;
    return 0;
}
```

- `congzhi::Thread`: A movable RAII thread wrapper built on top of `pthread` (POSIX threads), providing a safe and easy-to-use thread usage interface that acts like `std::thread`. Compared to `std::thread`, `congzhi::Thread` offers explicit thread state tracking via the `ThreadState enum`, integrates a dedicated `ThreadAttribute` class for declarative configuration of pthread-specific properties, and automatically detaches joinable threads in its destructor to prevent resource leaks (in a way similar to `std::jthread`). Most importantly, it provides the `congzhi::Thread::Start()` method, which allows the delayed start of a thread. I'll put several examples for you on how to use `congzhi::Thread`.

```cpp
// About the Start() method:

#include <iostream>
#include <thread>
#include "../pthread_wrapper.hpp"

int main () {
  // Starting threads with immediate execution, both implementations support it
  std::thread std_t1([]() {std::cout << "Thread 1 is running." << std::endl;});
  congzhi::Thread congzhi_t1([]() {std::cout << "Congzhi Thread 1 is running." << std::endl;});
  std_t1.join();
  congzhi_t1.Join();

  // Move construction, no delayed start, both implementations support it
  std::thread std_t2;
  std_t2 = std::thread([]() {std::cout << "Thread 2 is running." << std::endl;});
  congzhi::Thread congzhi_t2;
  congzhi_t2 = congzhi::Thread([]() {std::cout << "Congzhi Thread 2 is running." << std::endl;});
  std_t2.join();
  congzhi_t2.Join();

  // Delayed start, no move construction needed, std::thread does not support it
  congzhi::Thread congzhi_t3;
  congzhi_t3.Start([]() {std::cout << "Congzhi Thread 3 is running." << std::endl;});
  congzhi_t3.Join();
}
```

### 2. NUMA Wrapper
