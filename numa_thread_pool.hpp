/*
* Author:       Congzhi
* Update:       2025-06-15
* Description:  A cross-platform thread pool based on congzhi::Thread.
* License:      MIT License
*/

#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#if defined(__APPLE__) || defined(__linux__)

#include "pthread_wrapper.hpp"

#ifdef __linux__
#include <numa.h>
#include <numaif.h>
#endif

#include <vector>
#include <queue>
#include <functional>
#include <atomic>
#include <stdexcept>

namespace congzhi {

// NUMA awareness functions (Linux only)
#ifdef __linux__

// Check if NUMA is supported.
constexpr bool is_numa_supported() {
    return numa_available() == 0;
}

// Get the number of NUMA nodes available.
constexpr int get_numa_node_count() {
    return numa_max_node() + 1;
}

// Set the CPU affinity, binding a thread to a specific NUMA node.
constexpr void bind_thread_to_numa_node(int node) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    numa_node_to_cpus(node, &cpuset);
    
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        throw std::runtime_error("Failed to bind thread to NUMA node");
    }
}
#endif

// Thread pool base class - provides basic thread pool functionality.
class ThreadPoolBase {
public:
    explicit ThreadPoolBase(size_t num_threads) : stop(false) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this]() {
                while (true) {
                    std::function<void()> task;
                    
                    {
                        LockGuard<Mutex> lock(queue_mutex);
                        condition.Wait(lock, [this]() { return stop || !tasks.empty(); });
                        
                        if (stop && tasks.empty()) return;
                        
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    
                    task();
                }
            });
        }
    }

    ~ThreadPoolBase() {
        {
            LockGuard<Mutex> lock(queue_mutex);
            stop = true;
        }
        
        condition.NotifyAll();
        for (Thread& worker : workers) {
            worker.Join();
        }
    }

    template<class F, class... TArgs>
    void enqueue(F&& f, TArgs&&... args) {
        auto task = std::bind(std::forward<F>(f), std::forward<TArgs>(args)...);
        
        {
            LockGuard<Mutex> lock(queue_mutex);
            if (stop) throw std::runtime_error("Enqueue on stopped ThreadPool");
            
            tasks.emplace([task]() mutable { task(); });
        }
        
        condition.NotifyOne();
    }

protected:
    std::vector<Thread> workers;
    std::queue<std::function<void()>> tasks;
    Mutex queue_mutex;
    ConditionVariable condition;
    std::atomic<bool> stop;
};

// NUMA-aware thread pool class - extends ThreadPoolBase for NUMA support.
#ifdef __linux__
class NumaThreadPool : public ThreadPoolBase {
public:
    explicit NumaThreadPool(size_t threads_per_node = 1) {
        int node_count = get_numa_node_count();
        if (node_count == 0) {
            throw std::runtime_error("No NUMA nodes available");
        }

        for (int node = 0; node < node_count; ++node) {
            for (size_t i = 0; i < threads_per_node; ++i) {
                workers.emplace_back([this, node]() {
                    bind_thread_to_numa_node(node);

                    while (true) {
                        std::function<void()> task;
                        
                        {
                            lock_guard<mutex> lock(queue_mutex);
                            condition.wait(lock, [this]() { return stop || !tasks.empty(); });
                            
                            if (stop && tasks.empty()) return;
                            
                            task = std::move(tasks.front());
                            tasks.pop();
                        }
                        
                        task();
                    }
                });
            }
        }
    }
};
#endif

class ThreadPool {
public:
    explicit ThreadPool(size_t threads_per_node = 1) {
#ifdef __linux__
        if (is_numa_supported()) {
            numa_pool = std::make_unique<NumaThreadPool>(threads_per_node);
        } else {
#endif
            size_t num_threads = Thread::HardwareConcurrency();
            base_pool = std::make_unique<ThreadPoolBase>(num_threads);
#ifdef __linux__
        }
#endif
    }

    template<class F, class... TArgs>
    void enqueue(F&& f, TArgs&&... args) {
#ifdef __linux__
        if (numa_pool) {
            numa_pool->enqueue(std::forward<F>(f), std::forward<Args>(args)...);
        } else {
#endif
            base_pool->enqueue(std::forward<F>(f), std::forward<TArgs>(args)...);
#ifdef __linux__
        }
#endif
    }

private:
#ifdef __linux__
    std::unique_ptr<NumaThreadPool> numa_pool;
#endif
    std::unique_ptr<ThreadPoolBase> base_pool;
};

} // namespace congzhi

#else
#error "This thread pool is only supported on Linux and macOS platforms."
#endif // __APPLE__ || __linux__
#endif // THREAD_POOL_HPP