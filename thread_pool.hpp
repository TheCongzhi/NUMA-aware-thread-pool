/*
* Author:       Congzhi
* Create:       2025-06-11
* Description:  A cross-platform thread pool based on congzhi::Thread.
* License:      MIT License
*/

#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#if defined(__APPLE__) || defined(__linux__)

#include "pthread_wrapper.hpp"

// linux specific includes for NUMA support
#ifdef __linux__
#include <numa.h> // NUMA support library
// #include <numaif.h> linux syscall interface
#endif

#include <vector>
#include <queue>
#include <unordered_map>
#include <functional>
#include <memory>
#include <utility>
#include <future>
#include <type_traits>
#include <atomic>
#include <stdexcept>
#include <chrono>

namespace congzhi {

// congzhi::ThreadPool, interface for different thread pool implementations.
class ThreadPool {
public:
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void Enqueue(std::function<void()> task) = 0;
    template<typename F, typename... Args>
    auto Submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {

        using RetType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<RetType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        auto fut = task->get_future();
        Enqueue([task]() {(*task)();});
        return fut;
    }
    virtual bool IsRunning() const = 0;
    virtual size_t WorkerCount() const = 0;
    virtual size_t TaskCount() const = 0;
    virtual ~ThreadPool() = default;
};

class NormalThreadPool : public ThreadPool {
private:
    struct Worker {
        congzhi::Thread thread;
        std::atomic<bool> is_valid{false};
        std::atomic<bool> should_exit{false};
        std::atomic<bool> is_idle{false};
        std::chrono::steady_clock::time_point idel_time_start;
    };
    mutable congzhi::Mutex worker_mutex_;

    const size_t min_threads_{congzhi::Thread::HardwareConcurrency()};
    const size_t max_threads_{congzhi::Thread::HardwareConcurrency() * 2};
    const size_t expand_factor_{2};
    const std::chrono::seconds idle_threshold_{10}; // For threads pool expansion and contraction.
    
    std::vector<std::unique_ptr<Worker>> workers_;
    std::atomic<size_t> valid_thread_count_{0};
    std::queue<std::function<void()>> tasks_;
    std::atomic<bool> running_{false};
    mutable congzhi::Mutex mutex_;
    congzhi::ConditionVariable cond_var_;

    // Monitoring thread for idle threads.
    std::unique_ptr<congzhi::Thread> monitor_thread_;
    std::atomic<bool> monitoring_{false};

    void WorkerLoop(size_t worker_index) {
        {
            congzhi::LockGuard<congzhi::Mutex> lock(worker_mutex_);
            if (worker_index >= workers_.size() || !workers_[worker_index]) {
                throw std::out_of_range("Worker index out of range");
            }
        }
        auto& worker = workers_[worker_index];
        worker->is_valid = true;
        valid_thread_count_.fetch_add(1);
        while (!worker->should_exit && running_) {
            std::function<void()> task;
            worker->is_idle = true;
            worker->idel_time_start = std::chrono::steady_clock::now();
            {
                congzhi::LockGuard<congzhi::Mutex> lock(mutex_);
                cond_var_.Wait(mutex_, [this, &worker]() {
                    return !tasks_.empty() || worker->should_exit || !running_;
                });
            }
            worker->is_idle = false;
            {
                congzhi::LockGuard<congzhi::Mutex> lock(mutex_);
                if (!tasks_.empty()) {
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
            }
            if (task) {
                task();
            }
        }
        worker->is_valid = false;
        worker->is_idle = false;
        valid_thread_count_.fetch_sub(1);
    }

    // Expansion logic.
    void ExpandThreads() {
        auto current_valid = valid_thread_count_.load();
        if (current_valid >= max_threads_) return;
        auto threads_target_count = (((current_valid * expand_factor_) < max_threads_) ? (current_valid * expand_factor_) : (max_threads_));
        auto threads_need_count = threads_target_count - current_valid;
        if (threads_need_count <= 0) return;
        
        congzhi::LockGuard<congzhi::Mutex> lock(worker_mutex_);
        auto create_thread_count = 0;
        
        for (auto i = 0; i < workers_.size() && create_thread_count < threads_need_count; ++i) {
            if (!workers_[i]->is_valid) {
                workers_[i]->should_exit = false;
                workers_[i]->thread.Start([this, index = i]() {
                    WorkerLoop(index);
                });
                ++create_thread_count;
            }
        }
        while (create_thread_count < threads_need_count && workers_.size() < max_threads_) {
            auto new_worker = std::make_unique<Worker>();
            new_worker->should_exit = false;
            new_worker->thread.Start([this, index = workers_.size()]() {
                WorkerLoop(index);
            });
            workers_.push_back(std::move(new_worker));
            ++create_thread_count;
        }
    }
    void ShrinkThreads() {
        auto current_valid = valid_thread_count_.load();
        if (current_valid <= min_threads_) return;
        auto threads_need_remove = current_valid - min_threads_;
        if (threads_need_remove <= 0) return;

        auto now = std::chrono::steady_clock::now();
        auto removed_threads_count = 0;
        congzhi::LockGuard<congzhi::Mutex> lock(worker_mutex_);
        for (auto i = 0; i <workers_.size() && removed_threads_count < threads_need_remove; ++i) {
            auto& worker = workers_[i];
            if (worker->is_valid && worker->is_idle) {
                auto idle_time = std::chrono::duration_cast<std::chrono::seconds>(now - worker->idel_time_start);
            
                if (idle_time >= idle_threshold_) {
                    worker->should_exit = true; // Mark the thread for exit.
                    ++removed_threads_count;
                }
            }
        }
        cond_var_.NotifyAll(); 
    }
    void MonitorLoop() {
        while(monitoring_) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            if (running_) {
                ShrinkThreads();
            }
            
        }
    }
public:
    NormalThreadPool() { workers_.reserve(min_threads_); }
    // Copying and moving are not allowed.
    NormalThreadPool(const NormalThreadPool&) = delete;
    NormalThreadPool& operator=(const NormalThreadPool&) = delete; 
    NormalThreadPool(NormalThreadPool&&) = delete;
    NormalThreadPool& operator=(NormalThreadPool&&) = delete;

    void Start() override {
        if (running_) {
            throw std::runtime_error("Thread pool is already running");
        }
        running_ = true;

        congzhi::LockGuard<congzhi::Mutex> lock(worker_mutex_);
        for(auto i = 0; i < min_threads_; ++i) {
            auto new_worker = std::make_unique<Worker>();
            new_worker->thread.Start([this, index = workers_.size()]() {
                WorkerLoop(index);
            });
            workers_.push_back(std::move(new_worker));
        }
        monitoring_ = true;
        monitor_thread_ = std::make_unique<congzhi::Thread>();
        monitor_thread_->Start([this]() {
            MonitorLoop();
        });
    }

    void Stop() override {
        if (!running_) {
            throw std::runtime_error("Thread pool is not running");
        }
        monitoring_ = false;
        if (monitor_thread_ && monitor_thread_->Joinable()) {
            monitor_thread_->Join();
            monitor_thread_.reset();
        }
        running_ = false;
        cond_var_.NotifyAll(); // Notify all threads to wake up and exit.

        for (auto& worker : workers_) {
            if (worker && worker->thread.Joinable()) {
                worker->thread.Join(); // Wait for all worker threads to finish.
                worker->should_exit = true; // Mark the thread for exit.
            }
        }
        workers_.clear();
        valid_thread_count_ = 0;
    }

    void Enqueue(std::function<void()> task) override {
        if (!running_) {
            throw std::runtime_error("Thread pool is not running");
        }
        // Enqueue the task queue.
        {
            congzhi::LockGuard<congzhi::Mutex> lock(mutex_);
            tasks_.emplace(std::move(task));
        }
        cond_var_.NotifyOne(); // Notify one worker thread to wake up and process the task.
        auto task_count = tasks_.size();
        auto current_valid = valid_thread_count_.load();
        if (task_count > current_valid * expand_factor_) {
            ExpandThreads();
        }
    }

    bool IsRunning() const override {
        return running_;
    }

    size_t WorkerCount() const override {
        return workers_.size();
    }
    size_t TaskCount() const override {
        congzhi::LockGuard<congzhi::Mutex> lock(mutex_);
        return tasks_.size();
    }
};

// NUMA awareness functions (Linux only)
#ifdef __linux__

// NUMA-aware thread pool class - extends ThreadPoolBase for NUMA support.
class NumaThreadPool : public ThreadPool {
public:
    // Check NUMA system support.
    bool IsNumaSupported() {
        return numa_available() == 0;
    }

    // Get the number of NUMA nodes available.
    int NumaNodeCount() {
        return numa_max_node() + 1;
    }

    // Set the CPU affinity, binding a thread to a specific NUMA node.
    void BindThreadToNumaNode(int node) {
        if (node < 0 || node >= NumaNodeCount()) {
            throw std::out_of_range("Invalid NUMA node index");
        }
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        
        numa_node_to_cpus(node, &cpuset);
        
        if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
            throw std::runtime_error("Failed to bind thread to NUMA node");
        }
    }
private:

public:
    void Start() override {
        if (!IsNumaSupported()) {
            throw std::runtime_error("NUMA is not supported on this system");
        }
        if (running_) {
            throw std::runtime_error("Thread pool is already running");
        }
        // Implementation for starting the NUMA-aware thread pool.
    }

    void Stop() override {
        if (!IsNumaSupported()) {
            throw std::runtime_error("NUMA is not supported on this system");
        }
        if (!running_) {
            throw std::runtime_error("Thread pool is not running");
        }
        // Implementation for stopping the NUMA-aware thread pool.
    }

    void Enqueue(std::function<void()> task) override {
        // Implementation for enqueuing tasks in the NUMA-aware thread pool.
    }

    bool IsRunning() const override {
        // Implementation to check if the NUMA-aware thread pool is running.
        return false; // Placeholder
    }

    size_t WorkerCount() const override {
        // Implementation to get the number of worker threads in the NUMA-aware thread pool.
        return 0; // Placeholder
    }
    ~NumaThreadPool() override {
        // Cleanup resources if necessary.
    }
};
#endif


} // namespace congzhi

#else
#error "This thread pool is only supported on Linux and macOS platforms."
#endif // __APPLE__ || __linux__
#endif // THREAD_POOL_HPP
