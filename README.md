# NUMA-Aware Thread Pool

- [English](README.md)
- [简体中文](README.zh_CN.md)

A modern C++17 thread pool library featuring a Linux‑only NUMA‑aware pool—built to maximize memory locality and minimize cross‑node latency—plus a portable non‑NUMA pool, designed for optimal performance accross multi-core architectures on Linux and macOS. With explicit support for NUMA architectures, this implementation addresses the unique challenges for memory locality and cross-NUMA-node latency in multi CPU socket systems(NUMA systems).

## Core Components

This repository contains three main components:

1. POSIX Thread Wrapper (`pthread_wrapper.hpp`) – RAII‑style C++ wrapper for POSIX threading primitives, offering standard‑library‑like interfaces for threads, mutexes, lock guards, and condition variables while preserving the performance of low‑level pthread operations.

2. NUMA Wrapper (`numa_namespace.hpp`) – Utilities to detect NUMA topology and manage CPU/memory binding for optimal data locality (Linux‑only).

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

- `congzhi::Thread`: A movable RAII thread wrapper built on top of `pthread_t` (POSIX threads), providing a safe and easy-to-use thread usage interface that acts like `std::thread`. Compared to `std::thread`, `congzhi::Thread` offers explicit thread state tracking via the `ThreadState enum`, integrates a dedicated `ThreadAttribute` class for declarative configuration of pthread-specific properties, and automatically detaches/joins/terminates/cancels joinable threads in its destructor to prevent resource leaks (in a way similar to `std::jthread`). Most importantly, it provides the `congzhi::Thread::Start()` method, which allows the delayed start of a thread. I'll put several examples for you on how to use `congzhi::Thread`. (Note here, variadic argument not available, you have to bind auguments yoursleves)

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

```cpp
// Different destructor actions of congzhi::Thread class

#include <iostream>
#include "../pthread_wrapper.hpp"

int main()
{
    congzhi::Mutex mutex;

    // Create a thread with default destructor action (JOIN)
    {
        congzhi::LockGuard<congzhi::Mutex> lock(mutex);
        congzhi::Thread thread1([]()
                                { std::cout << "Thread 1 is running." << std::endl; });
        thread1.Join();                                                 // Explicitly join the thread
        std::cout << "The thread's state is: "<< thread1.GetThreadState() // Get the state after joining, should be TERMINATED
                  << "\nFinishes its job? " << (thread1.IsCompleteExecution() ? "Yes" : "No") // Is thread finishes its job after Join()? Should be Yes
                  << std::endl; 
    }

    // Create a thread with JOIN action
    // Main thread will wait for thread2 to finish before exiting
    {
        congzhi::LockGuard<congzhi::Mutex> lock(mutex);
        congzhi::Thread thread2([]()
                                { std::cout << "Thread 2 is running." << std::endl; }, congzhi::DtorAction::JOIN);
        congzhi::this_thread::SleepFor(std::chrono::milliseconds(100));
        std::cout << "The thread's state is: "<< thread2.GetThreadState() // Should be joinable, bacause the dtor han't been called.
                  << "\nFinishes its job? " << (thread2.IsCompleteExecution() ? "Yes" : "No") // Is thread finishes its job after 100ms sleep? Should be Yes
                  << std::endl; 
    }

    // Create a thread with DETACH action
    // Main thread will detach thread3 when this stack frame returns, detached threads would cleaned up automatically when it finishes, main thread won't wait for it
    {
        congzhi::LockGuard<congzhi::Mutex> lock(mutex);
        congzhi::Thread thread3([]()
                                { std::cout << "Thread 3 is running." << std::endl; }, congzhi::DtorAction::DETACH);
        congzhi::this_thread::SleepFor(std::chrono::milliseconds(100));
        std::cout << "The thread's state is: "<< thread3.GetThreadState() // Should be joinable, bacause the dtor han't been called.
                  << "\nFinishes its job? " << (thread3.IsCompleteExecution() ? "Yes" : "No") // Is thread finishes its job after 100ms sleep? Should be Yes
                  << std::endl;     
    }

    // Create a thread with CANCEL action
    // Beware!! Caller should set cancel point or the action would block forever!!
    {
        congzhi::LockGuard<congzhi::Mutex> lock(mutex);
        congzhi::Thread thread4([]()
                                {            
            while (true) {
                std::cout << "Thread 4 is running." << std::endl;
                congzhi::this_thread::SleepFor(std::chrono::milliseconds(10));
            } }, congzhi::DtorAction::CANCEL);
        congzhi::this_thread::SleepFor(std::chrono::milliseconds(100)); // Ensure thread4 has started and running
        thread4.Cancel();
        std::cout << "The thread's state is: "<< thread4.GetThreadState() // Get the state after cancellation, should be TERMINATED
                  << "\nFinishes its job? " << (thread4.IsCompleteExecution() ? "Yes" : "No") // Is thread finishes its job after cancellation? Should be No
                  << std::endl; 
    }

    // Create a thread with TERMINATE action
    // In this way, the thread must handle SIGTERM signal to exit gracefully
    // If there's no signal handler registered, the default action will terminate the entire process
    {
        congzhi::LockGuard<congzhi::Mutex> lock(mutex);
        congzhi::Thread thread5([]() {
            // Register signal handler for SIGTERM
            struct sigaction sa;
            sa.sa_handler = [](int) {
            std::cout << "Thread 5 exiting gracefully." << std::endl;
            pthread_exit(nullptr);
            };
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;
            sigaction(SIGTERM, &sa, nullptr);

            // The thread's main loop
            while (true) {
                std::cout << "Thread 5 is running." << std::endl;
                congzhi::this_thread::SleepFor(std::chrono::milliseconds(10));
        } }, congzhi::DtorAction::TERMINATE);

        congzhi::this_thread::SleepFor(std::chrono::milliseconds(100)); // Ensure thread5 has started and running
        try
        {
            thread5.Terminate(); // Terminate the thread
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error terminating thread: " << e.what() << std::endl;
        }
        std::cout << "The thread's state is: "<< thread5.GetThreadState() // Get the state after termination, should be TERMINATED
                  << "\nFinishes its job? " << (thread5.IsCompleteExecution() ? "Yes" : "No") // Is thread finishes its job after cancellation? Should be No
                  << std::endl; 
    }
    std::cout << "All threads have been cleaned up." << std::endl;
    return 0;
}
```

```cpp
#include <iostream>
#include "../pthread_wrapper.hpp"

int main() {
    congzhi::ThreadAttribute attr; // Declare a ThreadAttribute object at first

    // Set arrtibutes
    attr.SetStackSize(4 * 1024 * 1024); // Stack size in bytes
    attr.SetScope(congzhi::Scope::SYSTEM); // or Scope::PROCESS
    attr.SetDetachState(congzhi::DetachState::JOINABLE); // or DetachState::Detached
    attr.SetSchedulingPolicy(congzhi::SchedulingPolicy::DEFAULT, 0); // or SchedulingPolicy::FIFO/Scheduling::RR
    std::cout << "Thread attributes set successfully." << std::endl;

    congzhi::Thread t1(
        [&](){
            congzhi::this_thread::SleepFor(std::chrono::seconds(1));
            std::cout << "Thread is running with custom attributes." << std::endl;
            std::cout << "Thread ID: " << pthread_self() << std::endl;
            std::cout << "Thread stack size:" << attr.GetStackSize() << std::endl;
            std::cout << "Thread scope: " << (attr.GetScope() == congzhi::Scope::System ? "STSTEM" : "PROCESS") << std::endl;
            std::cout << "Thread detach state: " << (attr.GetDetachState() == congzhi::DetachState::Detached ? "DETACHED" : "JOINABLE") << std::endl;
            std::cout << "Thread scheduling policy: " << (attr.GetSchedulingPolicy() == congzhi::SchedulingPolicy::Default ? "DEFAULT" : "Custom") << std::endl;
        },
        congzhi::DtorAction::DETACH, // DtorAction
        attr
    );
    congzhi::this_thread::SleepFor(std::chrono::seconds(2));
    try {
        t1.Join(); // The thread IS joinable, but since we set dtor action to DtorAction::DETACH, so there's a logic error
    } catch (const std::logic_error& e) {
        std::cerr << "Logic error: " << e.what() << std::endl;
    }
    return 0;
}
```

### 2. NUMA Namespace

There are no much functions under this namespace. But it is good for you to check NUMA availablity, bind thread localty on NUMA systems, allocate NUMA node specific memory, and operating more specific NUMA operations.

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
